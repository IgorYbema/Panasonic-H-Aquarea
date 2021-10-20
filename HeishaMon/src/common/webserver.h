/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _WEBSERVER_H_
#define _WEBSERVER_H_

#define ESP_RAW_SOCKET 1

#ifndef MTU_SIZE
  #define MTU_SIZE 2*1460
#endif

#ifndef WEBSERVER_BUFFER_SIZE
  #define WEBSERVER_BUFFER_SIZE 128
#endif

#ifndef WEBSERVER_READ_SIZE
  #define WEBSERVER_READ_SIZE MTU_SIZE
#endif

#ifndef WEBSERVER_MAX_CLIENTS
  #define WEBSERVER_MAX_CLIENTS 5
#endif

#ifndef WEBSERVER_CLIENT_TIMEOUT
  #define WEBSERVER_CLIENT_TIMEOUT 15
#endif

#ifndef ESP8266
typedef struct tcp_pcb {
} tcp_pcb;

typedef struct pbuf {
  unsigned int len;
  void *payload;
  struct pbuf *next;
} pbuf;
#endif

typedef struct header_t {
  char *buffer;
  uint16_t ptr;
} header_t;

struct webserver_t;

typedef int (webserver_cb_t)(struct webserver_t *client, void *data);

typedef struct arguments_t {
  char *name;
  char *value;
  uint16_t len;
} arguments_t;

typedef struct sendlist_t {
  void *ptr;
  uint16_t type:1;
  uint16_t size:15;
  struct sendlist_t *next;
} sendlist_t;

typedef struct webserver_t {
  tcp_pcb *pcb;
  uint8_t method:4;
  uint8_t chunked:4;
  uint8_t step:4;
  uint8_t headerstep:4;
  uint16_t ptr;
  uint16_t totallen;
  uint16_t readlen;
  uint16_t content;
  uint8_t route;
  struct sendlist_t *sendlist;
  struct sendlist_t *sendlist_head;
  webserver_cb_t *callback;
  char buffer[WEBSERVER_BUFFER_SIZE];
} webserver_t;

typedef struct webserver_client_t {
  struct webserver_t data;
} webserver_client_t;

enum {
  WEBSERVER_CLIENT_REQUEST_METHOD = 1,
  WEBSERVER_CLIENT_REQUEST_URI,
  WEBSERVER_CLIENT_READ_HEADER,
  WEBSERVER_CLIENT_SEND_HEADER,
  WEBSERVER_CLIENT_CREATE_HEADER,
  WEBSERVER_CLIENT_RW,
  WEBSERVER_CLIENT_SENDING,
  WEBSERVER_CLIENT_HEADER,
  WEBSERVER_CLIENT_ARGS,
  WEBSERVER_CLIENT_CLOSE,
} webserver_steps;

int webserver_start(int port, webserver_cb_t *callback);
void webserver_loop(void);
int webserver_send_content(struct webserver_t *client, char *buf, uint16_t len);
#ifdef ESP8266
int webserver_send_content_P(struct webserver_t *client, PGM_P buf, uint16_t len);
#endif
int webserver_send(struct webserver_t *client, uint16_t code, char *mimetype, uint16_t data_len);
void webserver_client_stop(struct webserver_t *client);

#endif
