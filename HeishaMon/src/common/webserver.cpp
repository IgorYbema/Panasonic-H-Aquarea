/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <Arduino.h>
#include <WiFiClient.h>

#include "webserver.h"

#define MIN(a,b) (((a)<(b))?(a):(b))

static struct webserver_client_t clients[WEBSERVER_MAX_CLIENTS];

#ifdef WEBSERVER_ASYNC
  static tcp_pcb *server = NULL;
  static char *rbuffer = NULL;
#else
  static WiFiServer server(0);
  #define ERR_OK 0
  static uint8_t rbuffer[WEBSERVER_READ_SIZE];
#endif

#ifdef WEBSERVER_ASYNC
static uint16_t tcp_write_P(tcp_pcb *pcb, PGM_P buf, uint16_t len, uint8_t flags) {
  char *str = (char *)malloc(len+1);
  if(str == NULL) {
    Serial.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
  }
  memset(str, 0, len+1);
  strncpy_P(str, buf, len);
  uint16_t ret = tcp_write(pcb, str, len, flags);
  free(str);
  return ret;
}
#endif

static int urldecode(const char *src, int src_len, char *dst, int dst_len, int is_form_url_encoded) {
  int i, j, a, b;

#define HEXTOI(x) (isdigit(x) ? x - '0' : x - 'W')

  for(i = j = 0; i < src_len && j < dst_len - 1; i++, j++) {
    if(src[i] == '%' && i < src_len - 2 &&
      isxdigit(*(const unsigned char *)(src + i + 1)) &&
      isxdigit(*(const unsigned char *)(src + i + 2))) {
      a = tolower(*(const unsigned char *)(src + i + 1));
      b = tolower(*(const unsigned char *)(src + i + 2));
      dst[j] = (char)((HEXTOI(a) << 4) | HEXTOI(b));
      i += 2;
    } else if(is_form_url_encoded && src[i] == '+') {
      dst[j] = ' ';
    } else {
      dst[j] = src[i];
    }
  }

  dst[j] = '\0'; // Null-terminate the destination

  return i >= src_len ? j : -1;
}

static int webserver_post_cb(struct webserver_t *client, int done) {
  int ret = -1;
  uint16_t len = 0;
  char *ptr = strstr(client->buffer, "=");
  char *ptr1 = strstr(client->buffer, " ");
  char *ptr2 = strstr(client->buffer, "&");

  if(ptr == NULL) {
    client->step = WEBSERVER_CLIENT_ARGS;
    struct arguments_t args;
    if(client->callback != NULL) {
      args.name = client->buffer;
      args.value = NULL;
      args.len = 0;
      ret = client->callback(client, &args);
    }
  } else {
    client->step = WEBSERVER_CLIENT_ARGS;

    uint16_t pos = (ptr-client->buffer)+1;
    struct arguments_t args;
    client->buffer[pos-1] = 0;
    int x = 0;
    args.name = &client->buffer[0];
    args.value = &client->buffer[pos];
    args.len = strlen(&client->buffer[pos]);

    if(client->callback != NULL) {
      ret = client->callback(client, &args);
    }
    if(client->callback != NULL && done == 1 && ret > -1) {
      args.value = NULL;
      args.len = 0;
      ret = client->callback(client, &args);
    }
    client->buffer[pos-1] = '=';
    client->ptr = pos;
  }
  return ret;
}

static int webserver_parse_post(struct webserver_t *client, uint16_t size) {
  char *ptr = (char *)memchr(client->buffer, '&', size);
  char *ptr1 = (char *)memchr(client->buffer, '=', size);
  char *ptr2 = (char *)memchr(client->buffer, ' ', size);
  char *ptr3 = (char *)memchr(client->buffer, '\r', size);

  uint16_t pos = 0;

  if(ptr == NULL && ptr2 == NULL) {
    if(client->readlen + size == client->totallen) {
      client->readlen += size;
    }
    return 0;
  }

  if(ptr != NULL && (ptr1 == NULL || ptr1 < ptr)) {
    pos = ptr-client->buffer;
    client->buffer[pos] = 0;

    urldecode(client->buffer,
              pos+1,
              (char *)client->buffer,
              pos+1, 1);

    if(webserver_post_cb(client, 0) == -1) {
      client->step = WEBSERVER_CLIENT_RW;
      client->ptr = 0;
      return -1;
    }

    memmove(&client->buffer[0], &client->buffer[pos+1], size-(pos+1));
    client->ptr = size-(pos+1);
    client->readlen += pos+1;
    client->buffer[client->ptr] = 0;
    return 1;
  }

  if(ptr2 != NULL) {
    if((ptr3 != NULL && ptr2 > ptr3)) {
      return 0;
    }
    pos = ptr2-client->buffer;
    client->buffer[pos] = 0;

    urldecode(client->buffer,
              pos+1,
              (char *)client->buffer,
              pos+1, 1);

    if(webserver_post_cb(client, 0) == -1) {
      client->step = WEBSERVER_CLIENT_RW;
      client->ptr = 0;
      return -1;
    }

    memmove(&client->buffer[0], &client->buffer[pos+1], size-(pos+1));
    client->ptr = size-(pos+1);
    client->readlen += pos+1;
    client->buffer[client->ptr] = 0;
    return 0;
  }

  /*
   * Fixme for memrchar with fixed boundary
   */
  ptr = (char *)memrchr(client->buffer, '%', size);
  if(ptr != NULL) {
    pos = ptr-client->buffer;
  }

  /*
   * A encoded character always start with a
   * percentage mark followed by two numbers.
   * To properly decode an url we need to
   * keep those together.
   */
  if(ptr != NULL && pos+2 >= WEBSERVER_BUFFER_SIZE) {
    client->buffer[pos] = 0;

    urldecode(client->buffer,
              pos,
              (char *)client->buffer,
              pos, 1);

    if(webserver_post_cb(client, 0) == -1) {
      client->step = WEBSERVER_CLIENT_RW;
      client->ptr = 0;
      return -1;
    }
    client->buffer[pos] = '%';

    memmove(&client->buffer[client->ptr], &client->buffer[pos], (size-pos));
    client->ptr += (size-pos);
    client->readlen += pos;
    client->buffer[client->ptr] = 0;
    return 1;
  }

  if(client->ptr >= WEBSERVER_BUFFER_SIZE) {
    urldecode(client->buffer,
              client->ptr,
              (char *)client->buffer,
              client->ptr, 1);

    if(webserver_post_cb(client, 0) == -1) {
      client->step = WEBSERVER_CLIENT_RW;
      client->ptr = 0;
      return -1;
    }
    return 1;
  }

  return 1;
}

#ifdef WEBSERVER_ASYNC
uint8_t http_parse_request(struct webserver_t *client, char *buf, uint16_t len) {
#else
uint8_t http_parse_request(struct webserver_t *client, uint8_t *buf, uint16_t len) {
#endif
  uint16_t hasread = MIN(WEBSERVER_BUFFER_SIZE-client->ptr, len);
  uint16_t pos = 0;

  while(pos < len) {
    hasread = MIN(WEBSERVER_BUFFER_SIZE-client->ptr, len-pos);
    memcpy(&client->buffer[client->ptr], &buf[pos], hasread);

    client->ptr += hasread;
    pos += hasread;

    /*
     * Request method
     */
    if(client->headerstep == 0) {
      if(strncmp_P(client->buffer, PSTR("GET "), 4) == 0) {
        client->method = 0;
        if(client->callback != NULL) {
          client->step = WEBSERVER_CLIENT_REQUEST_METHOD;
          if(client->callback != NULL) {
            if(client->callback(client, (void *)"GET") == -1) {
              client->step = WEBSERVER_CLIENT_CLOSE;
              return -1;
            }
          }
        }
        memmove(&client->buffer[0], &client->buffer[4], client->ptr-4);
        client->ptr -= 4;
      }
      if(strncmp_P(client->buffer, PSTR("POST "), 5) == 0) {
        client->method = 1;
        client->step = WEBSERVER_CLIENT_REQUEST_METHOD;
        if(client->callback != NULL) {
          if(client->callback(client, (void *)"POST") == -1) {
            client->step = WEBSERVER_CLIENT_CLOSE;
            return -1;
          }
        }
        memmove(&client->buffer[0], &client->buffer[5], client->ptr-5);
        client->ptr -= 5;
      }
      client->headerstep = 1;
    }
      /*
       * Request URI
       */
    if(client->headerstep == 1) {
      char *ptr1 = (char *)memchr(client->buffer, '?', client->ptr);
      char *ptr2 = (char *)memchr(client->buffer, ' ', client->ptr);
      if(ptr2 == NULL || (ptr1 != NULL && ptr2 > ptr1)) {
        if(ptr1 == NULL) {
          // Request URI two long
        } else {
          uint16_t pos = ptr1-client->buffer;
          client->buffer[pos] = 0;
          client->step = WEBSERVER_CLIENT_REQUEST_URI;
          if(client->callback != NULL) {
            if(client->callback(client, client->buffer) == -1) {
              client->step = WEBSERVER_CLIENT_CLOSE;
              return -1;
            }
          }
          client->headerstep = 2;
          memmove(&client->buffer[0], &client->buffer[pos+1], client->ptr-(pos+1));
          client->ptr -= (pos+1);
        }
      } else {
        uint16_t pos = ptr2-client->buffer;
        client->buffer[pos] = 0;
        client->headerstep = 2;
        client->step = WEBSERVER_CLIENT_REQUEST_URI;
        if(client->callback != NULL) {
          if(client->callback(client, client->buffer) == -1) {
            client->step = WEBSERVER_CLIENT_CLOSE;
            return -1;
          }
        }
        memmove(&client->buffer[0], &client->buffer[pos+1], client->ptr-(pos+1));
        client->ptr -= pos+1;
      }
    }
    if(client->headerstep == 2) {
      int ret = webserver_parse_post(client, client->ptr);

      if(ret == -1) {
        return -1;
      }
      client->step = WEBSERVER_CLIENT_READ_HEADER;
      if(ret == 0) {
        client->headerstep = 3;
      }
    }
    if(client->headerstep == 3) {
      uint16_t i = 0;
      while(i < client->ptr-2) {
        if(strncmp_P(&client->buffer[i], PSTR("\r\n"), 2) == 0) {
          memmove(&client->buffer[0], &client->buffer[i+2], client->ptr-(i+2));
          client->ptr -= (i + 2);
          client->headerstep = 4;
          break;
        }
        i++;
      }
    }
    if(client->headerstep == 4) {
      char *ptr = (char *)memchr(client->buffer, ':', client->ptr);

      while(ptr != NULL) {
        struct arguments_t args;
        uint16_t i = ptr-client->buffer, x = 0;
        client->buffer[i] = 0;
        args.name = &client->buffer[0];
        args.value = NULL;
        x = i;
        i++;
        while(i < client->ptr-2) {
          if(strncmp_P(&client->buffer[i], PSTR("\r\n"), 2) == 0 ||
             (client->ptr == WEBSERVER_BUFFER_SIZE && i == WEBSERVER_BUFFER_SIZE-3)) {
            while(client->buffer[x+1] == ' ') {
              x++;
            }
            args.value = &client->buffer[x+1];
            args.len = (i-x)-1;
            if((client->ptr == WEBSERVER_BUFFER_SIZE && i == WEBSERVER_BUFFER_SIZE-3)) {
              args.len += 2;
            }

            if(strcmp_P(args.name, PSTR("Content-Length")) == 0) {
              char tmp[args.len+1];
              memset(&tmp, 0, args.len+1);
              strncpy(tmp, &client->buffer[x+1], args.len);
              client->totallen = atoi(tmp);
            }
            client->step = WEBSERVER_CLIENT_HEADER;
            if(client->callback != NULL) {
              if(client->callback(client, &args) == -1) {
                client->step = WEBSERVER_CLIENT_CLOSE;
                return -1;
              }
            }
            client->step = WEBSERVER_CLIENT_READ_HEADER;

            if(i <= client->ptr-4 && strncmp_P(&client->buffer[i], PSTR("\r\n\r\n"), 4) == 0) {
              memmove(&client->buffer[0], &client->buffer[i+4], client->ptr-(i+4));
              client->ptr -= (i + 4);
              client->readlen = 0;
              return 0;
            }
            client->buffer[i] = 0;
            if((client->ptr == WEBSERVER_BUFFER_SIZE && i == WEBSERVER_BUFFER_SIZE-3)) {
              memmove(&client->buffer[x], &client->buffer[i+2], client->ptr-(i+2));
              client->buffer[x-1] = ':';
              client->ptr -= (i + 2 - x);
            } else {
              memmove(&client->buffer[0], &client->buffer[i+2], client->ptr-(i+2));
              client->ptr -= (i + 2);
            }
            break;
          }
          i++;
        }
        if(args.value == NULL) {
          client->buffer[x] = ':';
          break;
        }
        ptr = (char *)memchr(client->buffer, ':', client->ptr);
      }
      if(client->ptr >= 2 && strncmp_P(client->buffer, PSTR("\r\n"), 2) == 0) {
        memmove(&client->buffer[0], &client->buffer[2], client->ptr-2);
        client->ptr -= 2;
        client->readlen = 0;
        return 0;
      }
    }
  }

  return 1;
}

int http_parse_body(struct webserver_t *client, char *buf, uint16_t len) {
  uint16_t hasread = MIN(WEBSERVER_BUFFER_SIZE-client->ptr, len);
  uint16_t pos = 0;

  while(1) {
    if(pos < len) {
      hasread = MIN(WEBSERVER_BUFFER_SIZE-client->ptr, len-pos);
      memcpy(&client->buffer[client->ptr], &buf[pos], hasread);
      client->ptr += hasread;
      pos += hasread;
    }

    int ret = webserver_parse_post(client, client->ptr);

    if(ret == -1) {
      return -1;
    }
    if(ret == 0) {
      break;
    }
  }

  return 0;
}

static PGM_P code_to_text(uint16_t code) {
  switch(code) {
    case 100:
      return PSTR("Continue");
    case 101:
      return PSTR("Switching Protocols");
    case 200:
      return PSTR("OK");
    case 201:
      return PSTR("Created");
    case 202:
      return PSTR("Accepted");
    case 203:
      return PSTR("Non-Authoritative Information");
    case 204:
      return PSTR("No Content");
    case 205:
      return PSTR("Reset Content");
    case 206:
      return PSTR("Partial Content");
    case 300:
      return PSTR("Multiple Choices");
    case 301:
      return PSTR("Moved Permanently");
    case 302:
      return PSTR("Found");
    case 303:
      return PSTR("See Other");
    case 304:
      return PSTR("Not Modified");
    case 305:
      return PSTR("Use Proxy");
    case 307:
      return PSTR("Temporary Redirect");
    case 400:
      return PSTR("Bad Request");
    case 401:
      return PSTR("Unauthorized");
    case 402:
      return PSTR("Payment Required");
    case 403:
      return PSTR("Forbidden");
    case 404:
      return PSTR("Not Found");
    case 405:
      return PSTR("Method Not Allowed");
    case 406:
      return PSTR("Not Acceptable");
    case 407:
      return PSTR("Proxy Authentication Required");
    case 408:
      return PSTR("Request Timeout");
    case 409:
      return PSTR("Conflict");
    case 410:
      return PSTR("Gone");
    case 411:
      return PSTR("Length Required");
    case 412:
      return PSTR("Precondition Failed");
    case 413:
      return PSTR("Request Entity Too Large");
    case 414:
      return PSTR("URI Too Long");
    case 415:
      return PSTR("Unsupported Media Type");
    case 416:
      return PSTR("Range not satisfiable");
    case 417:
      return PSTR("Expectation Failed");
    case 500:
      return PSTR("Internal Server Error");
    case 501:
      return PSTR("Not Implemented");
    case 502:
      return PSTR("Bad Gateway");
    case 503:
      return PSTR("Service Unavailable");
    case 504:
      return PSTR("Gateway Timeout");
    case 505:
      return PSTR("HTTP Version not supported");
    default:
      return PSTR("");
  }
}

static uint16_t webserver_create_header(struct webserver_t *client, uint16_t code, char *mimetype, uint16_t len) {
  uint16_t i = 0;
  char buffer[512], *p = buffer;
  memset(buffer, '\0', sizeof(buffer));

  i += snprintf_P((char *)&p[i], sizeof(buffer), PSTR("HTTP/1.1 %d %s\r\n"), code, code_to_text(code));
  if(client->callback != NULL) {
    client->step = WEBSERVER_CLIENT_CREATE_HEADER;
    struct header_t header;
    header.buffer = &p[i];
    header.ptr = i;

    uint16_t ret = 0;
    if(client->callback(client, &header) == -1) {
      if(strstr_P((char *)&p[i], PSTR("\r\n\r\n")) == NULL) {
        if(strstr((char *)&p[i], PSTR("\r\n")) != NULL) {
          header.ptr += snprintf_P((char *)&p[header.ptr], sizeof(buffer)-header.ptr, PSTR("\r\n"));
        } else {
          header.ptr += snprintf_P((char *)&p[header.ptr], sizeof(buffer)-header.ptr, PSTR("\r\n\r\n"));
        }
      }
      client->step = WEBSERVER_CLIENT_RW;
      i = header.ptr;
      return i;
    }

    if(header.ptr > i && strstr_P((char *)&p[i], PSTR("\r\n")) == NULL) {
      header.ptr += snprintf((char *)&p[header.ptr], sizeof(buffer)-header.ptr, PSTR("\r\n"));
    }
    i = header.ptr;
    client->step = WEBSERVER_CLIENT_RW;
  }
  i += snprintf_P((char *)&p[i], sizeof(buffer) - i, PSTR("Server: ESP8266\r\n"));
  i += snprintf_P((char *)&p[i], sizeof(buffer) - i, PSTR("Keep-Alive: timeout=15, max=100\r\n"));
  i += snprintf_P((char *)&p[i], sizeof(buffer) - i, PSTR("Content-Type: %s\r\n"), mimetype);
  i += snprintf_P((char *)&p[i], sizeof(buffer) - i, PSTR("Content-Length: %d\r\n\r\n"), len);

#ifdef WEBSERVER_ASYNC
  tcp_write(client->pcb, &buffer, i, 0);
  tcp_output(client->pcb);
#else
  if(client->client.write(buffer, i) > 0) {
    client->lastseen = millis();
  }
#endif

  return i;
}

static int webserver_process_send(struct webserver_t *client) {
  struct sendlist_t *tmp = client->sendlist;
  uint16_t cpylen = client->totallen, i = 0, cpyptr = client->ptr;
  char cpy[client->totallen+1];

  if(client->chunked == 1) {
    while(tmp != NULL && cpylen > 0) {
      if(cpyptr == 0) {
        if(cpylen >= tmp->size) {
          cpyptr += tmp->size;
          cpylen -= tmp->size;
          tmp = tmp->next;
          cpyptr = 0;
        } else {
          cpyptr += cpylen;
          cpylen = 0;
        }
      } else if(cpyptr+cpylen >= tmp->size) {
        cpylen -= (tmp->size-cpyptr);
        tmp = tmp->next;
        cpyptr = 0;
      } else {
        cpyptr += cpylen;
        cpylen = 0;
      }
    }

    char chunk_size[12];
    size_t n = snprintf_P((char *)chunk_size, sizeof(chunk_size), PSTR("%X\r\n"), client->totallen - cpylen);
#ifdef WEBSERVER_ASYNC
    tcp_write(client->pcb, chunk_size, n, 0);
#else
    if(client->client.write(chunk_size, n) > 0) {
      client->lastseen = millis();
    }
#endif
    i += n;
  }

  if(client->sendlist != NULL) {
    while(client->sendlist != NULL && client->totallen > 0) {
      if(client->ptr == 0) {
        if(client->totallen >= client->sendlist->size) {
          if(client->sendlist->type == 1) {
            strncpy_P(cpy, &((PGM_P)client->sendlist->ptr)[client->ptr], client->sendlist->size);
#ifdef WEBSERVER_ASYNC
            tcp_write(client->pcb, cpy, client->sendlist->size, TCP_WRITE_FLAG_MORE);
#else
            if(client->client.write(cpy, client->sendlist->size) > 0) {
              client->lastseen = millis();
            }
#endif
          } else {
#ifdef WEBSERVER_ASYNC
            tcp_write(client->pcb, &((char *)client->sendlist->ptr)[client->ptr], client->sendlist->size, TCP_WRITE_FLAG_MORE);
#else
            if(client->client.write(&((char *)client->sendlist->ptr)[client->ptr], client->sendlist->size) > 0) {
              client->lastseen = millis();
            }
#endif
          }
          i += client->sendlist->size;
          client->ptr += client->sendlist->size;
          client->totallen -= client->sendlist->size;

          tmp = client->sendlist;
          client->sendlist = client->sendlist->next;
          if(tmp->type == 0) {
            free(tmp->ptr);
          }
          free(tmp);
          client->ptr = 0;
        } else {
          if(client->sendlist->type == 1) {
            strncpy_P(cpy, &((PGM_P)client->sendlist->ptr)[client->ptr], client->totallen);
#ifdef WEBSERVER_ASYNC
            tcp_write(client->pcb, cpy, client->totallen, TCP_WRITE_FLAG_MORE);
#else
            if(client->client.write(cpy, client->totallen) > 0) {
              client->lastseen = millis();
            }
#endif
          } else {
#ifdef WEBSERVER_ASYNC
            tcp_write(client->pcb, &((char *)client->sendlist->ptr)[client->ptr], client->totallen, TCP_WRITE_FLAG_MORE);
#else
            if(client->client.write(&((char *)client->sendlist->ptr)[client->ptr], client->totallen) > 0) {
              client->lastseen = millis();
            }
#endif
          }
          i += client->totallen;
          client->ptr += client->totallen;
          client->totallen = 0;
        }
      } else if(client->ptr+client->totallen >= client->sendlist->size) {
        if(client->sendlist->type == 1) {
          strncpy_P(cpy, &((PGM_P)client->sendlist->ptr)[client->ptr], (client->sendlist->size-client->ptr));
#ifdef WEBSERVER_ASYNC
          tcp_write(client->pcb, cpy, (client->sendlist->size-client->ptr), TCP_WRITE_FLAG_MORE);
#else
          if(client->client.write(cpy, (client->sendlist->size-client->ptr)) > 0) {
            client->lastseen = millis();
          }
#endif
        } else {
#ifdef WEBSERVER_ASYNC
          tcp_write(client->pcb, &((char *)client->sendlist->ptr)[client->ptr], (client->sendlist->size-client->ptr), TCP_WRITE_FLAG_MORE);
#else
          if(client->client.write(&((char *)client->sendlist->ptr)[client->ptr], (client->sendlist->size-client->ptr)) > 0) {
            client->lastseen = millis();
          }
#endif
        }
        i += (client->sendlist->size-client->ptr);
        client->totallen -= (client->sendlist->size-client->ptr);
        tmp = client->sendlist;
        client->sendlist = client->sendlist->next;
        if(tmp->type == 0) {
          free(tmp->ptr);
        }
        client->ptr = 0;
      } else {
        if(client->sendlist->type == 1) {
          strncpy_P(cpy, &((PGM_P)client->sendlist->ptr)[client->ptr], client->totallen);
#ifdef WEBSERVER_ASYNC
          tcp_write(client->pcb, cpy, client->totallen, TCP_WRITE_FLAG_MORE);
#else
          if(client->client.write(cpy, client->totallen) > 0) {
            client->lastseen = millis();
          }
#endif
        } else {
#ifdef WEBSERVER_ASYNC
          tcp_write(client->pcb, &((char *)client->sendlist->ptr)[client->ptr], client->totallen, TCP_WRITE_FLAG_MORE);
#else
          if(client->client.write(&((char *)client->sendlist->ptr)[client->ptr], client->totallen) > 0) {
            client->lastseen = millis();
          }
#endif
        }
        client->ptr += client->totallen;
        client->totallen = 0;
      }
    }
    if(client->chunked == 1) {
#ifdef WEBSERVER_ASYNC
      tcp_write_P(client->pcb, PSTR("\r\n"), 2, TCP_WRITE_FLAG_MORE);
#else
      if(client->client.write_P(PSTR("\r\n"), 2) > 0) {
        client->lastseen = millis();
      }
#endif
    }
  }

  if(client->sendlist == NULL) {
    client->content++;
    client->step = WEBSERVER_CLIENT_RW;
    if(client->callback(client, NULL) == -1) {
      client->step = WEBSERVER_CLIENT_CLOSE;
    } else {
      client->step = WEBSERVER_CLIENT_SENDING;
    }
    if(client->sendlist == NULL) {
      if(client->chunked == 1) {
#ifdef WEBSERVER_ASYNC
        tcp_write_P(client->pcb, PSTR("0\r\n\r\n"), 5, 0);
#else
        if(client->client.write_P(PSTR("0\r\n\r\n"), 5) > 0) {
          client->lastseen = millis();
        }
#endif
        i += 5;
      } else {
#ifdef WEBSERVER_ASYNC
        tcp_write_P(client->pcb, PSTR("\r\n\r\n"), 4, 0);
#else
        if(client->client.write_P(PSTR("\r\n\r\n"), 4) > 0) {
          client->lastseen = millis();
        }
#endif
        i += 4;
      }
      client->step = WEBSERVER_CLIENT_CLOSE;
      client->ptr = 0;
      client->content = 0;
    }
  }
#ifdef WEBSERVER_ASYNC
  tcp_output(client->pcb);
#endif
 
  return i;
}

int webserver_send_content_P(struct webserver_t *client, PGM_P buf, uint16_t size) {
  struct sendlist_t *node = (struct sendlist_t *)malloc(sizeof(struct sendlist_t));
  if(node == NULL) {
    Serial.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
  }
  memset(node, 0, sizeof(struct sendlist_t));
  node->ptr = (void *)buf;
  node->size = size;
  node->type = 1;
  if(client->sendlist == NULL) {
    client->sendlist = node;
    client->sendlist_head = node;
  } else {
    client->sendlist_head->next = node;
    client->sendlist_head = node;
  }

  return 0;
}

int webserver_send_content(struct webserver_t *client, char *buf, uint16_t size) {
  struct sendlist_t *node = (struct sendlist_t *)malloc(sizeof(struct sendlist_t));
  if(node == NULL) {
    Serial.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
  }
  memset(node, 0, sizeof(struct sendlist_t));
  if((node->ptr = strdup(buf)) == NULL) {
    Serial.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
  }
  node->size = size;
  node->type = 0;
  if(client->sendlist == NULL) {
    client->sendlist = node;
    client->sendlist_head = node;
  } else {
    client->sendlist_head->next = node;
    client->sendlist_head = node;
  }
  return 0;
}

int webserver_send(struct webserver_t *client, uint16_t code, char *mimetype, uint16_t data_len) {
  uint16_t i = 0;
  if(data_len == 0) {
    char buffer[512], *p = buffer;
    memset(buffer, '\0', sizeof(buffer));

    client->chunked = 1;
    i = snprintf_P((char *)p, sizeof(buffer), PSTR("HTTP/1.1 %d %s\r\n"), code, code_to_text(code));
    if(client->callback != NULL) {
      client->step = WEBSERVER_CLIENT_CREATE_HEADER;
      struct header_t header;
      header.buffer = &p[i];
      header.ptr = i;

      uint16_t ret = 0;
      if(client->callback(client, &header) == -1) {
        if(strstr_P((char *)&p[i], PSTR("\r\n\r\n")) == NULL) {
          if(strstr_P((char *)&p[i], PSTR("\r\n")) != NULL) {
            header.ptr += snprintf((char *)&p[header.ptr], sizeof(buffer)-header.ptr, PSTR("\r\n"));
          } else {
            header.ptr += snprintf((char *)&p[header.ptr], sizeof(buffer)-header.ptr, PSTR("\r\n\r\n"));
          }
        }
        client->step = WEBSERVER_CLIENT_RW;
        i = header.ptr;
        goto done;
      }
      if(header.ptr > i && strstr_P((char *)&p[i], PSTR("\r\n")) == NULL) {
        header.ptr += snprintf((char *)&p[header.ptr], sizeof(buffer)-header.ptr, PSTR("\r\n"));
      }
      i = header.ptr;
      client->step = WEBSERVER_CLIENT_RW;
    }
    i += snprintf((char *)&p[i], sizeof(buffer)-i, PSTR("Keep-Alive: timeout=15, max=100\r\n"));
    i += snprintf((char *)&p[i], sizeof(buffer)-i, PSTR("Content-Type: %s\r\n"), mimetype);
    i += snprintf((char *)&p[i], sizeof(buffer)-i, PSTR("Transfer-Encoding: chunked\r\n\r\n"));

done:
#ifdef WEBSERVER_ASYNC
    tcp_write(client->pcb, &buffer, i, 0);
    tcp_output(client->pcb);
#else
  if(client->client.write((uint8_t *)&buffer, i) > 0) {
    client->lastseen = millis();
  }
#endif
  } else {
    client->chunked = 0;
    i = webserver_create_header(client, code, mimetype, data_len);
  }

  return i;
}

#ifdef WEBSERVER_ASYNC
static void webserver_client_close(struct webserver_t *client) {
  Serial.print(F("Closing webserver client: "));
  Serial.print(IPAddress(client->pcb->remote_ip.addr).toString().c_str());
  Serial.print(F(":"));
  Serial.println(client->pcb->remote_port);

  client->step = 0;

  tcp_recv(client->pcb, NULL);
  tcp_sent(client->pcb, NULL);
  tcp_sent(client->pcb, NULL);
  tcp_poll(client->pcb, NULL, 0);

  tcp_close(client->pcb);
  client->pcb = NULL;
}

err_t webserver_sent(void *arg, tcp_pcb *pcb, uint16_t len) {
  uint16_t i = 0;
  for(i=0;i<WEBSERVER_MAX_CLIENTS;i++) {
    if(clients[i].data.pcb == pcb) {
      if(clients[i].data.step == WEBSERVER_CLIENT_RW) {
        if(clients[i].data.callback(&clients[i].data, NULL) == -1) {
          clients[i].data.step = WEBSERVER_CLIENT_CLOSE;
        } else {
          clients[i].data.step = WEBSERVER_CLIENT_SENDING;
        }
      }
      if(clients[i].data.step == WEBSERVER_CLIENT_SENDING) {
        if((clients[i].data.totallen = tcp_sndbuf(clients[i].data.pcb)) > 0) {
          /*
           * Leave room for chunk overhead
           */
          clients[i].data.totallen -= 16;
          webserver_process_send(&clients[i].data);
        }
      }
      if(clients[i].data.step == WEBSERVER_CLIENT_CLOSE) {
        webserver_client_close(&clients[i].data);
      }
      break;
    }
  }
  return ERR_OK;
}

err_t webserver_receive(void *arg, tcp_pcb *pcb, struct pbuf *data, err_t err) {
  uint8_t i = 0;
  uint16_t size = 0;
#else
uint8_t webserver_receive(struct webserver_t *client, uint8_t *rbuffer, uint16_t size) {
#endif
  char *ptr = NULL;
  uint16_t pos = 0, x = 0;
  struct pbuf *b = NULL;

#ifdef WEBSERVER_ASYNC
  if(data == NULL) {
    for(i=0;i<WEBSERVER_MAX_CLIENTS;i++) {
      if(clients[i].data.pcb == pcb) {
        webserver_client_close(&clients[i].data);
      }
    }
    return ERR_OK;
  }

  b = data;

  while(b != NULL) {
    rbuffer = (char *)b->payload;
    size = b->len;

    for(i=0;i<WEBSERVER_MAX_CLIENTS;i++) {
      if(clients[i].data.pcb == pcb) {
        struct webserver_t *client = clients[i].data;
#endif
        if(client->step == WEBSERVER_CLIENT_READ_HEADER) {
          if(http_parse_request(client, rbuffer, size) == 0) {
            if(client->method == 1) {
              client->step = WEBSERVER_CLIENT_ARGS;
              x = 0;
              while(x < size-4 && strncmp_P((char *)&rbuffer[x++], PSTR("\r\n\r\n"), 4) != 0);
              x += 3;
              size -= x;

              memset(client->buffer, 0, WEBSERVER_BUFFER_SIZE);
              client->ptr = 0;
              client->readlen = 0;

              if(http_parse_body(client, (char *)&rbuffer[x], size) == -1) {
                client->step = WEBSERVER_CLIENT_CLOSE;
              }

              if(client->totallen == client->readlen) {
                urldecode(client->buffer,
                  client->ptr+1,
                  (char *)client->buffer,
                  client->ptr+1, 1);

                if(webserver_post_cb(client, 0) == -1) {
                  client->step = WEBSERVER_CLIENT_CLOSE;
                } else {
                  client->step = WEBSERVER_CLIENT_SEND_HEADER;
                }
                client->ptr = 0;
              }
            } else {
              client->step = WEBSERVER_CLIENT_SEND_HEADER;
            }
          }
        }

        if(client->step == WEBSERVER_CLIENT_ARGS) {
          if(http_parse_body(client, (char *)rbuffer, size) == -1) {
            client->step = WEBSERVER_CLIENT_CLOSE;
          }

          if(client->totallen == client->readlen) {
            urldecode(client->buffer,
              client->ptr+1,
              (char *)client->buffer,
              client->ptr+1, 1);

            if(webserver_post_cb(client, 0) == -1) {
              client->step = WEBSERVER_CLIENT_CLOSE;
            } else {
              client->step = WEBSERVER_CLIENT_SEND_HEADER;
            }
            client->ptr = 0;
          }
        }

        if(client->step == WEBSERVER_CLIENT_RW ||
           client->step == WEBSERVER_CLIENT_SEND_HEADER) {
#ifdef WEBSERVER_ASYNC
          if((client->totallen = tcp_sndbuf(client->pcb)) > 0) {
#else
            client->totallen = MTU_SIZE;
            client->totallen -= 16;
#endif
            if(client->callback != NULL) {
              if(client->callback(client, NULL) == -1) {
                client->step = WEBSERVER_CLIENT_CLOSE;
                return -1;
              }
              if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
                client->step = WEBSERVER_CLIENT_RW;
              }
              client->ptr = 0;
            } else {
              client->step = WEBSERVER_CLIENT_CLOSE;
              return -1;
            }
#ifdef WEBSERVER_ASYNC
          }
#endif
        }
#ifdef WEBSERVER_ASYNC
        break;
      }
    }
    tcp_recved(pcb, b->len);
    b = b->next;
  }
  pbuf_free(data);
#endif

  return ERR_OK;
}

#ifdef WEBSERVER_ASYNC
err_t webserver_poll(void *arg, struct tcp_pcb *pcb) {
  uint8_t i = 0;
  for(i=0;i<WEBSERVER_MAX_CLIENTS;i++) {
    if(clients[i].data.pcb == pcb) {
      clients[i].data.step = WEBSERVER_CLIENT_CLOSE;
      webserver_client_close(&clients[i].data);
      break;
    }
  }
  return ERR_OK;
}

err_t webserver_client(void *arg, tcp_pcb *pcb, err_t err) {
  uint8_t i = 0;
  for(i=0;i<WEBSERVER_MAX_CLIENTS;i++) {
    if(clients[i].data.pcb == NULL) {
      clients[i].data.pcb = pcb;
      clients[i].data.route = 0;
      clients[i].data.readlen = 0;
      clients[i].data.totallen = 0;
      clients[i].data.ptr = 0;
      clients[i].data.headerstep = 0;
      clients[i].data.content = 0;
      clients[i].data.sendlist = NULL;
      clients[i].data.callback = (webserver_cb_t *)arg;
      clients[i].data.step = WEBSERVER_CLIENT_READ_HEADER;
      memset(&clients[i].data.buffer, 0, WEBSERVER_BUFFER_SIZE);

      Serial.print(F("New webserver client: "));
      Serial.print(IPAddress(clients[i].data.pcb->remote_ip.addr).toString().c_str());
      Serial.print(F(":"));
      Serial.println(clients[i].data.pcb->remote_port);

      tcp_setprio(pcb, TCP_PRIO_MIN);
      tcp_nagle_disable(pcb);
      tcp_recv(pcb, &webserver_receive);
      tcp_sent(pcb, &webserver_sent);
      // 15 seconds timer
      tcp_poll(pcb, &webserver_poll, WEBSERVER_CLIENT_TIMEOUT*2);
      break;
    }
  }
  return ERR_OK;
}

#else
void webserver_reset_client(struct webserver_t *client) {
  client->readlen = 0;
  client->reqtype = 0;
  client->method = 0;
  client->totallen = 0;
  client->active = 0;
  client->step = 0;
  client->headerstep = 0;
  client->chunked = 0;
  client->ptr = 0;
  client->route = 0;
  client->lastseen = 0;
  client->content = 0;
  client->sendlist = NULL;
  client->sendlist_head = NULL;
  client->boundary = NULL;
  memset(&client->buffer, 0, WEBSERVER_BUFFER_SIZE);
}

void webserver_loop(void) {
  uint16_t pos = 0, x = 0, lastseen = 0, size = 0;
  uint8_t i = 0;

  for(i=0;i<WEBSERVER_MAX_CLIENTS;i++) {
    if(clients[i].data.step == 0) {
      continue;
    }
    if(clients[i].data.step > WEBSERVER_CLIENT_CONNECTING) {
      if((unsigned long)(millis() - clients[i].data.lastseen) > WEBSERVER_CLIENT_TIMEOUT) {
        clients[i].data.step = WEBSERVER_CLIENT_CLOSE;
      }
    }
    switch(clients[i].data.step) {
      case WEBSERVER_CLIENT_CONNECTING: {
        if(clients[i].data.client.available()) {
          clients[i].data.step = WEBSERVER_CLIENT_READ_HEADER;
        }
        clients[i].data.ptr = 0;
        memset(&clients[i].data.buffer, 0, WEBSERVER_BUFFER_SIZE);
      } break;
      case WEBSERVER_CLIENT_ARGS:
      case WEBSERVER_CLIENT_READ_HEADER: {
        if(clients[i].data.client.connected() || clients[i].data.client.available()) {
          if(clients[i].data.client.available()) {
            uint8_t *p = (uint8_t *)rbuffer;
            size = clients[i].data.client.read(
              p,
              WEBSERVER_READ_SIZE
            );
          }
        } else if(!clients[i].data.client.connected()) {
          clients[i].data.step = WEBSERVER_CLIENT_CLOSE;
        } else {
          continue;
        }
        if(size > 0) {
          clients[i].data.lastseen = millis();
        }

        if(webserver_receive(&clients[i].data, rbuffer, size) == -1) {
          clients[i].data.step = WEBSERVER_CLIENT_CLOSE;
        }
      } break;
      case WEBSERVER_CLIENT_RW:
      case WEBSERVER_CLIENT_SEND_HEADER: {
        if(clients[i].data.callback != NULL) {
          if(clients[i].data.step == WEBSERVER_CLIENT_SEND_HEADER) {
            if(clients[i].data.callback(&clients[i].data, NULL) == -1) {
              clients[i].data.step = WEBSERVER_CLIENT_CLOSE;
              continue;
            }
          } else if(clients[i].data.step == WEBSERVER_CLIENT_RW) {
            if(clients[i].data.callback(&clients[i].data, NULL) == -1) {
              clients[i].data.step = WEBSERVER_CLIENT_CLOSE;
            } else {
              clients[i].data.step = WEBSERVER_CLIENT_SENDING;
            }
          }
          if(clients[i].data.step == WEBSERVER_CLIENT_SEND_HEADER) {
            clients[i].data.step = WEBSERVER_CLIENT_RW;
          }
          clients[i].data.ptr = 0;
        } else {
          clients[i].data.step = WEBSERVER_CLIENT_CLOSE;
          continue;
        }
      } break;
      case WEBSERVER_CLIENT_SENDING: {
        clients[i].data.totallen = MTU_SIZE;
        /*
         * Leave room for chunk overhead
         */
        clients[i].data.totallen -= 16;
        webserver_process_send(&clients[i].data);
      } break;
      case WEBSERVER_CLIENT_CLOSE: {
        Serial.print("Closing webserver client: ");
        Serial.print(clients[i].data.client.remoteIP());
        Serial.print(":");
        Serial.println(clients[i].data.client.remotePort());

        clients[i].data.client.stop();
        webserver_reset_client(&clients[i].data);
      } break;
    }
  }

  if(server.hasClient()) {
    for(i=0;i<WEBSERVER_MAX_CLIENTS;i++) {
      if(clients[i].data.active == 0) {
        clients[i].data.client = server.available();
        if(clients[i].data.client) {
          webserver_reset_client(&clients[i].data);

          clients[i].data.active = 1;
          clients[i].data.lastseen = millis();
          clients[i].data.step = WEBSERVER_CLIENT_CONNECTING;

          // clients[i].data.client.setNoDelay(1);
          // clients[i].data.client.setTimeout(200);

          Serial.print("New webserver client: ");
          Serial.print(clients[i].data.client.remoteIP());
          Serial.print(":");
          Serial.println(clients[i].data.client.remotePort());
          break;
        }
      }
    }
  }
}
#endif

int webserver_start(int port, webserver_cb_t *callback) {
#ifdef WEBSERVER_ASYNC
  server = tcp_new();
  if(server == NULL) {
    return -1;
  }

  tcp_setprio(server, TCP_PRIO_MIN);

  ip_addr_t local_addr;
  local_addr.addr = (uint32_t)IPADDR_ANY;
  uint8_t err = tcp_bind(server, &local_addr, port);
  if(err != ERR_OK) {
    tcp_close(server);
    return -1;
  }

  tcp_pcb *listen_pcb = tcp_listen_with_backlog(server, WEBSERVER_MAX_CLIENTS);
  if(listen_pcb == NULL) {
    tcp_close(server);
    return -1;
  }
  server = listen_pcb;
  tcp_nagle_disable(server);
  tcp_setprio(server, TCP_PRIO_MIN);
  tcp_accept(server, &webserver_client);
  tcp_arg(server, (void *)callback);
#else
  uint8_t i = 0;
  for(i=0;i<WEBSERVER_MAX_CLIENTS;i++) {
    webserver_reset_client(&clients[i].data);
    clients[i].data.callback = callback;
  }
  server.begin(port);

  Serial.print("Webserver server started at port: ");
  Serial.println(port);
#endif
  return 0;
}
