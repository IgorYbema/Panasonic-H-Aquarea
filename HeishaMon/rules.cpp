/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include <Arduino.h>
#include <LittleFS.h>

#include "src/common/mem.h"
#include "src/common/stricmp.h"
#include "src/common/strnicmp.h"
#include "src/common/log.h"
#include "src/common/uint32float.h"
#include "src/common/timerqueue.h"
#include "src/common/progmem.h"
#include "src/rules/rules.h"

#include "dallas.h"
#include "webfunctions.h"
#include "decode.h"
#include "HeishaOT.h"
#include "commands.h"

#define MAXCOMMANDSINBUFFER 10
#define OPTDATASIZE 20

bool send_command(byte* command, int length);

extern int dallasDevicecount;
extern dallasDataStruct *actDallasData;
extern settingsStruct heishamonSettings;
extern char actData[DATASIZE];
extern char actOptData[OPTDATASIZE];
extern char actDataExtra[DATASIZE];
extern String openTherm[2];
static uint8_t parsing = 0;

typedef struct vm_gvnull_t {
  VM_GENERIC_FIELDS
  uint8_t rule;
} __attribute__((packed)) vm_gvnull_t;

typedef struct vm_gvinteger_t {
  VM_GENERIC_FIELDS
  uint8_t rule;
  int value;
} __attribute__((packed)) vm_gvinteger_t;

typedef struct vm_gvfloat_t {
  VM_GENERIC_FIELDS
  uint8_t rule;
  float value;
} __attribute__((packed)) vm_gvfloat_t;

static struct rules_t **rules = NULL;
static uint8_t nrrules = 0;

static struct rule_stack_t global_varstack;

static struct vm_vinteger_t vinteger;
static struct vm_vfloat_t vfloat;
static struct vm_vnull_t vnull;

struct rule_options_t rule_options;

unsigned char *mempool = (unsigned char *)MMU_SEC_HEAP;
unsigned int memptr = 0;

static void vm_global_value_prt(char *out, int size);

// static int readRuleFromFS(int i) {
  // char fname[24];
  // memset(&fname, 0, sizeof(fname));
  // sprintf_P((char *)&fname, PSTR("/rule%d.bc"), i);
  // File f = LittleFS.open(fname, "r");
  // if(!f) {
    // logprintf_P(F("failed to open file: %s"), fname);
    // return -1;
  // }
  // FREE(rules[i]->ast.buffer);
  // if((rules[i]->ast.buffer = (unsigned char *)MALLOC(rules[i]->ast.bufsize)) == NULL) {
    // OUT_OF_MEMORY
  // }
  // memset(rules[i]->ast.buffer, 0, rules[i]->ast.bufsize);
  // f.readBytes((char *)rules[i]->ast.buffer, rules[i]->ast.bufsize);
  // f.close();
  // return 0;
// }

// static int writeRuleToFS(int i) {
  // char fname[24];
  // memset(&fname, 0, sizeof(fname));
  // sprintf_P((char *)&fname, PSTR("/rule%d.bc"), i);
  // File f = LittleFS.open(fname, "w");
  // if(!f) {
    // logprintf_P(F("failed to open file: %s"), fname);
    // return -1;
  // }
  // f.write((char *)rules[i]->ast.buffer, rules[i]->ast.bufsize);
  // f.close();
  // return 0;
// }

static int8_t is_variable(char *text, uint16_t size) {
  uint16_t i = 1, x = 0, match = 0;

  if(size == strlen_P(PSTR("ds18b20#2800000000000000")) && strncmp_P(text, PSTR("ds18b20#"), 8) == 0) {
    return 24;
  } else if(text[0] == '$' || text[0] == '#' || text[0] == '@' || text[0] == '%' || text[0] == '?') {
    while(isalnum(text[i])) {
      i++;
    }

    if(text[0] == '%') {
      if(size == 5 && strnicmp(&text[1], "hour", 4) == 0) {
        return 5;
      }
      if(size == 7 && strnicmp(&text[1], "minute", 6) == 0) {
        return 7;
      }
      if(size == 6 && strnicmp(&text[1], "month", 5) == 0) {
        return 6;
      }
      if(size == 4 && strnicmp(&text[1], "day", 3) == 0) {
        return 4;
      }
    }

    if(text[0] == '@') {
      int nrcommands = sizeof(commands)/sizeof(commands[0]);
      for(x=0;x<nrcommands;x++) {
        cmdStruct cmd;
        memcpy_P(&cmd, &commands[x], sizeof(cmd));
        size_t len = strlen(cmd.name);
        if(size-1 == len && strnicmp(&text[1], cmd.name, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }

      int nroptcommands = sizeof(optionalCommands)/sizeof(optionalCommands[0]);
      for(x=0;x<nroptcommands;x++) {
        optCmdStruct cmd;
        memcpy_P(&cmd, &optionalCommands[x], sizeof(cmd));
        size_t len = strlen(cmd.name);
        if(size-1 == len && strnicmp(&text[1], cmd.name, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
      if(match == 0) {
        int nrtopics = sizeof(topics)/sizeof(topics[0]);
        for(x=0;x<nrtopics;x++) {
          char cpy[MAX_TOPIC_LEN];
          memcpy_P(&cpy, topics[x], MAX_TOPIC_LEN);
          size_t len = strlen(cpy);
          if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
            i = len+1;
            match = 1;
            break;
          }
        }
      }
      if(match == 0) {
        int nrtopics = sizeof(optTopics)/sizeof(optTopics[0]);
        for(x=0;x<nrtopics;x++) {
          char cpy[MAX_TOPIC_LEN];
          memcpy_P(&cpy, optTopics[x], MAX_TOPIC_LEN);
          size_t len = strlen(cpy);
          if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
            i = len+1;
            match = 1;
            break;
          }
        }
      }
      if(match == 0) {
        int nrtopics = sizeof(xtopics)/sizeof(xtopics[0]);
        for(x=0;x<nrtopics;x++) {
          char cpy[MAX_TOPIC_LEN];
          memcpy_P(&cpy, xtopics[x], MAX_TOPIC_LEN);
          size_t len = strlen(cpy);
          if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
            i = len+1;
            match = 1;
            break;
          }
        }
      }
      if(match == 0) {
        return -1;
      }
    }
    if(text[0] == '?') {
      int x = 0;
      while(heishaOTDataStruct[x].name != NULL) {
        size_t len = strlen(heishaOTDataStruct[x].name);
        if(size-1 == len && strnicmp(&text[1], heishaOTDataStruct[x].name, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
        x++;
      }
      if(match == 0) {
        logprintf_P(F("err: %s %d"), __FUNCTION__, __LINE__);
        return -1;
      }
    }

    return i;
  }
  return -1;
}

static int8_t is_event(char *text, uint16_t size) {
  int i = 1, x = 0, match = 0;
  if(text[0] == '@') {
    int nrcommands = sizeof(commands)/sizeof(commands[0]);
    for(x=0;x<nrcommands;x++) {
      cmdStruct cmd;
      memcpy_P(&cmd, &commands[x], sizeof(cmd));
      size_t len = strlen(cmd.name);
      if(size-1 == len && strnicmp(&text[1], cmd.name, len) == 0) {
        i = len+1;
        match = 1;
        break;
      }
    }

    if(match == 0) {
      int nroptcommands = sizeof(optionalCommands)/sizeof(optionalCommands[0]);
      for(x=0;x<nroptcommands;x++) {
        optCmdStruct cmd;
        memcpy_P(&cmd, &optionalCommands[x], sizeof(cmd));
        size_t len = strlen(cmd.name);
        if(size-1 == len && strnicmp(&text[1], cmd.name, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
    }
    if(match == 0) {
      int nrtopics = sizeof(topics)/sizeof(topics[0]);
      for(x=0;x<nrtopics;x++) {
        size_t len = strlen_P(topics[x]);
        char cpy[len];
        memcpy_P(&cpy, &topics[x], len);
        if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
    }
    if(match == 0) {
      int nrtopics = sizeof(optTopics)/sizeof(optTopics[0]);
      for(x=0;x<nrtopics;x++) {
        size_t len = strlen_P(optTopics[x]);
        char cpy[len];
        memcpy_P(&cpy, &optTopics[x], len);
        if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
    }
    if(match == 0) {
      int nrtopics = sizeof(xtopics)/sizeof(xtopics[0]);
      for(x=0;x<nrtopics;x++) {
        size_t len = strlen_P(xtopics[x]);
        char cpy[len];
        memcpy_P(&cpy, &xtopics[x], len);
        if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
    }
    if(match == 0) {
      return -1;
    }

    return i;
  }

  if(text[0] == '?') {
    int x = 0;
    while(heishaOTDataStruct[x].name != NULL) {
      size_t len = strlen(heishaOTDataStruct[x].name);
      if(size-1 == len && strnicmp(&text[1], heishaOTDataStruct[x].name, len) == 0) {
        i = len+1;
        match = 1;
        break;
      }
      x++;
    }
    if(match == 0) {
      return -1;
    }
    return i;
  }

  if(size == strlen_P(PSTR("ds18b20#2800000000000000")) && strncmp_P((const char *)text, PSTR("ds18b20#"), 8) == 0) {
    return 24;
  }

  uint8_t nr = rule_by_name(rules, nrrules, text);
  if(nr > 0) {
    return size;
  }

  return -1;
}

static int8_t event_cb(struct rules_t *obj, char *name) {
  int8_t nr = rule_by_name(rules, nrrules, name);
  if(nr == -1) {
    char msg[100];
    sprintf_P((char *)&msg, PSTR("Rule block '%s' not found"), name);
    log_message(name);
    return -1;
  }

  obj->ctx.go = rules[nr];
  rules[nr]->ctx.ret = obj;

  return 1;
}

static unsigned char *vm_value_get(struct rules_t *obj, uint16_t token) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t ret = 0;

  unsigned char *outA = NULL, *outB = NULL;

  int8_t r = rule_token(&obj->ast, token, &outA);
  if(r == -1) {
    return NULL;
  }

  if(outA[0] != TVALUE) {
    FREE(outA);
    return NULL;
  }

  struct vm_tvalue_t *var = (struct vm_tvalue_t *)outA;

  if(var->token[0] == '$') {
    struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
    if(var->go == 0 || varstack->buffer[var->go] == 0) {
      int ret = varstack->nrbytes, suffix = 0;
      unsigned int size = varstack->nrbytes + sizeof(struct vm_vnull_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vnull_t *value = (struct vm_vnull_t *)&varstack->buffer[ret];
      value->type = VNULL;
      value->ret = token;
      var->go = ret;

      varstack->nrbytes = size;

      if(rule_token(&obj->ast, token, &outA) < 0) {
        FREE(outA);
        return NULL;
      }
    }

    uint16_t go = var->go;
    FREE(outA);
    return (unsigned char *)&varstack->buffer[go];
  }

  if(var->token[0] == '#') {
    struct rule_stack_t *varstack = &global_varstack;

    uint16_t x = 0;
    for(x=4;x<varstack->nrbytes;x++) {
      switch(varstack->buffer[x]) {
        case VINTEGER: {
          struct vm_gvinteger_t *val = (struct vm_gvinteger_t *)&varstack->buffer[x];

          if(rule_token(&rules[val->rule-1]->ast, val->ret, &outB) < 0) {
            FREE(outA);
            return NULL;
          }
          if(outB[0] != TVALUE) {
            FREE(outA);
            FREE(outB);
            return NULL;
          }

          struct vm_tvalue_t *foo = (struct vm_tvalue_t *)outB;
          if(stricmp((char *)foo->token, (char *)var->token) == 0) {
            memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
            vinteger.type = VINTEGER;
            vinteger.value = (int)val->value;

            FREE(outA);
            FREE(outB);
            return (unsigned char *)&vinteger;
          }

          FREE(outB);
          x += sizeof(struct vm_gvinteger_t)-1;
        } break;
        case VFLOAT: {
          struct vm_gvfloat_t *val = (struct vm_gvfloat_t *)&varstack->buffer[x];

          if(rule_token(&rules[val->rule-1]->ast, val->ret, &outB) < 0) {
            FREE(outA);
            return NULL;
          }
          if(outB[0] != TVALUE) {
            FREE(outA);
            FREE(outB);
            return NULL;
          }

          struct vm_tvalue_t *foo = (struct vm_tvalue_t *)outB;
          if(stricmp((char *)foo->token, (char *)var->token) == 0) {
            memset(&vfloat, 0, sizeof(struct vm_vfloat_t));
            vfloat.type = VFLOAT;
            vfloat.value = val->value;

            float f = 0.0;
            uint322float(val->value, &f);

            FREE(outA);
            FREE(outB);
            return (unsigned char *)&vfloat;
          }

          FREE(outB);
          x += sizeof(struct vm_gvfloat_t)-1;
        } break;
        case VNULL: {
          struct vm_gvnull_t *val = (struct vm_gvnull_t *)&varstack->buffer[x];

          if(rule_token(&rules[val->rule-1]->ast, val->ret, &outB) < 0) {
            FREE(outA);
            return NULL;
          }
          if(outB[0] != TVALUE) {
            FREE(outA);
            FREE(outB);
            return NULL;
          }

          struct vm_tvalue_t *foo = (struct vm_tvalue_t *)outB;
          if(stricmp((char *)foo->token, (char *)var->token) == 0) {
            memset(&vnull, 0, sizeof(struct vm_vnull_t));
            vnull.type = VNULL;

            FREE(outA);
            FREE(outB);
            return (unsigned char *)&vnull;
          }

          FREE(outB);
          x += sizeof(struct vm_gvnull_t)-1;
        } break;
        default: {
          FREE(outA);
          Serial.printf("err: %s %d\n", __FUNCTION__, __LINE__);
          return NULL;
        } break;
      }
    }

    if(var->go == 0 || varstack->buffer[var->go] == 0) {
      int ret = varstack->nrbytes;
      uint16_t size = varstack->nrbytes + sizeof(struct vm_gvnull_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&varstack->buffer[varstack->nrbytes], 0, sizeof(struct vm_gvnull_t));
      struct vm_gvnull_t *value = (struct vm_gvnull_t *)&varstack->buffer[ret];
      value->type = VNULL;
      value->ret = token;
      value->rule = obj->nr;
      var->go = ret;

      varstack->nrbytes += sizeof(struct vm_gvnull_t);
      varstack->bufsize = size;

      if(rule_token(&obj->ast, token, &outA) < 0) {
        FREE(outA);
        return NULL;
      }
    }

    const char *key = (char *)var->token;

    switch(varstack->buffer[var->go]) {
      case VINTEGER: {
        struct vm_gvinteger_t *na = (struct vm_gvinteger_t *)&varstack->buffer[var->go];

        memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
        vinteger.type = VINTEGER;
        vinteger.value = (int)na->value;

        FREE(outA);
        return (unsigned char *)&vinteger;
      } break;
      case VFLOAT: {
        struct vm_gvfloat_t *na = (struct vm_gvfloat_t *)&varstack->buffer[var->go];

        memset(&vfloat, 0, sizeof(struct vm_vfloat_t));
        vfloat.type = VFLOAT;
        vfloat.value = na->value;

        float f = 0.0;
        uint322float(na->value, &f);

        FREE(outA);
        return (unsigned char *)&vfloat;
      } break;
      case VNULL: {
        memset(&vnull, 0, sizeof(struct vm_vnull_t));
        vnull.type = VNULL;

        FREE(outA);
        return (unsigned char *)&vnull;
      } break;
      default: {
        Serial1.printf("%s %d\n", __FUNCTION__, __LINE__);
        return NULL;
      } break;
    }

    FREE(outA);
    return NULL;
  }

  if(var->token[0] == '@') {
    uint16_t i = 0;
    for(i=0;i<NUMBER_OF_TOPICS;i++) {
      char cpy[MAX_TOPIC_LEN];
      memcpy_P(&cpy, topics[i], MAX_TOPIC_LEN);
      if(stricmp(cpy, (char *)&var->token[1]) == 0) {
        String dataValue = actData[0] == '\0' ? "" : getDataValue(actData, i);
        char *str = (char *)dataValue.c_str();
        if(strlen(str) == 0) {
          memset(&vnull, 0, sizeof(struct vm_vnull_t));
          vnull.type = VNULL;
          vnull.ret = token;

          FREE(outA);
          return (unsigned char *)&vnull;
        } else {
          float var = atof(str);
          float nr = 0;

          // mosquitto_publish
          if(modff(var, &nr) == 0) {
            memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
            vinteger.type = VINTEGER;
            vinteger.value = (int)var;

            FREE(outA);
            return (unsigned char *)&vinteger;
          } else {
            memset(&vfloat, 0, sizeof(struct vm_vfloat_t));
            vfloat.type = VFLOAT;
            vfloat.value = var;

            FREE(outA);
            return (unsigned char *)&vfloat;
          }
        }
      }
    }
    for(i=0;i<NUMBER_OF_OPT_TOPICS;i++) {
      char cpy[MAX_TOPIC_LEN];
      memcpy_P(&cpy, topics[i], MAX_TOPIC_LEN);
      if(stricmp(cpy, (char *)&var->token[1]) == 0) {
        String dataValue = actOptData[0] == '\0' ? "" : getOptDataValue(actOptData, i);
        char *str = (char *)dataValue.c_str();
        if(strlen(str) == 0) {
          memset(&vnull, 0, sizeof(struct vm_vnull_t));
          vnull.type = VNULL;
          vnull.ret = token;

          return (unsigned char *)&vnull;
        } else {
          float var = atof(str);
          float nr = 0;

          // mosquitto_publish
          if(modff(var, &nr) == 0) {
            memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
            vinteger.type = VINTEGER;
            vinteger.value = (int)var;

            return (unsigned char *)&vinteger;
          } else {
            memset(&vfloat, 0, sizeof(struct vm_vfloat_t));
            vfloat.type = VFLOAT;
            vfloat.value = var;

            return (unsigned char *)&vfloat;
          }
        }
      }
    }
    for(i=0;i<NUMBER_OF_TOPICS_EXTRA;i++) {
      char cpy[MAX_TOPIC_LEN];
      memcpy_P(&cpy, xtopics[i], MAX_TOPIC_LEN);
      if(stricmp(cpy, (char *)&var->token[1]) == 0) {
        String dataValue = actDataExtra[0] == '\0' ? "" : getDataValueExtra(actDataExtra, i);
        char *str = (char *)dataValue.c_str();
        if(strlen(str) == 0) {
          memset(&vnull, 0, sizeof(struct vm_vnull_t));
          vnull.type = VNULL;
          vnull.ret = token;

          return (unsigned char *)&vnull;
        } else {
          float var = atof(str);
          float nr = 0;

          // mosquitto_publish
          if(modff(var, &nr) == 0) {
            memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
            vinteger.type = VINTEGER;
            vinteger.value = (int)var;

            return (unsigned char *)&vinteger;
          } else {
            memset(&vfloat, 0, sizeof(struct vm_vfloat_t));
            vfloat.type = VFLOAT;
            vfloat.value = var;

            return (unsigned char *)&vfloat;
          }
        }
      }
    }
  }
  if(var->token[0] == '%') {
    if(stricmp((char *)&var->token[1], "hour") == 0) {
      time_t now = time(NULL);
      struct tm *tm_struct = localtime(&now);

      memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
      vinteger.type = VINTEGER;
      vinteger.value = (int)tm_struct->tm_hour;

      FREE(outA);
      return (unsigned char *)&vinteger;
    } else if(stricmp((char *)&var->token[1], "minute") == 0) {
      time_t now = time(NULL);
      struct tm *tm_struct = localtime(&now);

      memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
      vinteger.type = VINTEGER;
      vinteger.value = (int)tm_struct->tm_min;

      FREE(outA);
      return (unsigned char *)&vinteger;
    } else if(stricmp((char *)&var->token[1], "month") == 0) {
      time_t now = time(NULL);
      struct tm *tm_struct = localtime(&now);

      memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
      vinteger.type = VINTEGER;
      vinteger.value = (int)tm_struct->tm_mon+1;

      FREE(outA);
      return (unsigned char *)&vinteger;
    } else if(stricmp((char *)&var->token[1], "day") == 0) {
      time_t now = time(NULL);
      struct tm *tm_struct = localtime(&now);

      memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
      vinteger.type = VINTEGER;
      vinteger.value = (int)tm_struct->tm_wday+1;

      FREE(outA);
      return (unsigned char *)&vinteger;
    }
  }

  if(var->token[0] == '?') {
    int x = 0;
    while(heishaOTDataStruct[x].name != NULL) {
      if(heishaOTDataStruct[x].rw >= 2 && stricmp((char *)&var->token[1], heishaOTDataStruct[x].name) == 0) {
        if(heishaOTDataStruct[x].type == TBOOL) {
          memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
          vinteger.type = VINTEGER;
          vinteger.value = (int)heishaOTDataStruct[x].value.b;
          // printf("%s %s = %g\n", __FUNCTION__, (char *)var->token, var);

          FREE(outA);
          return (unsigned char *)&vinteger;
        }
        if(heishaOTDataStruct[x].type == TFLOAT) {
          memset(&vfloat, 0, sizeof(struct vm_vfloat_t));
          vfloat.type = VFLOAT;
          vfloat.value = heishaOTDataStruct[x].value.f;
          float2uint32(heishaOTDataStruct[x].value.f, &vfloat.value);
          // printf("%s %s = %g\n", __FUNCTION__, (char *)var->token, var);

          FREE(outA);
          return (unsigned char *)&vfloat;
        }
        break;
      }
      x++;
    }
    logprintf_P(F("err: %s %d"), __FUNCTION__, __LINE__);
  }

  if(strnicmp((const char *)var->token, _F("ds18b20#"), 8) == 0) {
    uint8_t i = 0;
    for(i=0;i<dallasDevicecount;i++) {
      if(strncmp(actDallasData[i].address, (const char *)&var->token[8], 16) == 0) {
        vfloat.type = VFLOAT;
        vfloat.value = actDallasData[i].temperature;

        FREE(outA);
        return (unsigned char *)&vfloat;
      }
    }

    memset(&vnull, 0, sizeof(struct vm_vnull_t));
    vnull.type = VNULL;

    FREE(outA);
    return (unsigned char *)&vnull;
  }

  FREE(outA);
  return NULL;
}


static int8_t vm_value_del(struct rules_t *obj, uint16_t idx) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t x = 0, ret = 0;

  unsigned char *outB = NULL;

  if(idx >= varstack->nrbytes) {
    return -1;
  }
  switch(varstack->buffer[idx]) {
    case VINTEGER: {
      ret = sizeof(struct vm_vinteger_t);
      memmove(&varstack->buffer[idx], &varstack->buffer[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
      varstack->bufsize = varstack->nrbytes-ret;
    } break;
    case VFLOAT: {
      ret = sizeof(struct vm_vfloat_t);
      memmove(&varstack->buffer[idx], &varstack->buffer[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
      varstack->bufsize = varstack->nrbytes-ret;
    } break;
    case VNULL: {
      ret = sizeof(struct vm_vnull_t);
      memmove(&varstack->buffer[idx], &varstack->buffer[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
      varstack->bufsize = varstack->nrbytes-ret;
    } break;
    default: {
      Serial1.printf("err: %s %d\n", __FUNCTION__, __LINE__);
      return -1;
    } break;
  }

  /*
   * Values are linked back to their root node,
   * by their absolute position in the bytecode.
   * If a value is deleted, these positions changes,
   * so we need to update all nodes.
   */
  for(x=idx;x<varstack->nrbytes;x++) {
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          if(rule_token(&obj->ast, node->ret, &outB) < 0) {
            return -1;
          }
          if(outB[0] != TVALUE) {
            FREE(outB);
            return -1;
          }

          struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)outB;
          tmp->go = x;

          if(rule_token(&obj->ast, node->ret, &outB) < 0) {
            FREE(outB);
            return -1;
          }
          FREE(outB);
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          if(rule_token(&obj->ast, node->ret, &outB) < 0) {
            return -1;
          }
          if(outB[0] != TVALUE) {
            FREE(outB);
            return -1;
          }

          struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)outB;
          tmp->go = x;

          if(rule_token(&obj->ast, node->ret, &outB) < 0) {
            FREE(outB);
            return -1;
          }
          FREE(outB);
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          if(rule_token(&obj->ast, node->ret, &outB) < 0) {
            return -1;
          }
          if(outB[0] != TVALUE) {
            FREE(outB);
            return -1;
          }

          struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)outB;
          tmp->go = x;

          if(rule_token(&obj->ast, node->ret, &outB) < 0) {
            FREE(outB);
            return -1;
          }
          FREE(outB);
        }
        x += sizeof(struct vm_vnull_t)-1;
      } break;
      default: {
        Serial1.printf("err: %s %d\n", __FUNCTION__, __LINE__);
        return -1;
      } break;
    }
  }

  return ret;
}

static int8_t vm_value_set(struct rules_t *obj, uint16_t token, uint16_t val) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t ret = 0, x = 0, loop = 1;

  unsigned char *outA = NULL, *outB = NULL, *outC = NULL;

  if(rule_token(&obj->ast, token, &outA) < 0) {
    return -1;
  }

  if(outA[0] != TVALUE) {
    FREE(outA);
    return -1;
  }

  struct vm_tvalue_t *var = (struct vm_tvalue_t *)outA;

  if(rule_token(obj->varstack, val, &outB) < 0) {
    FREE(outA);
    return -1;
  }

  if(var->token[0] == '$') {
    varstack = (struct rule_stack_t *)obj->userdata;

    if(var->go > 0) {
      vm_value_del(obj, var->go);
    };

    ret = varstack->nrbytes;

    var->go = ret;

    switch(outB[0]) {
      case VINTEGER: {
        unsigned int size = varstack->nrbytes+sizeof(struct vm_vinteger_t);
        if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)outB;
        struct vm_vinteger_t *value = (struct vm_vinteger_t *)&varstack->buffer[ret];
        value->type = VINTEGER;
        value->ret = token;
        value->value = (int)cpy->value;

        varstack->nrbytes = size;
      } break;
      case VFLOAT: {
        unsigned int size = varstack->nrbytes+sizeof(struct vm_vfloat_t);
        if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)outB;
        struct vm_vfloat_t *value = (struct vm_vfloat_t *)&varstack->buffer[ret];
        value->type = VFLOAT;
        value->ret = token;
        value->value = cpy->value;

        varstack->nrbytes = size;
      } break;
      case VNULL: {
        unsigned int size = varstack->nrbytes+sizeof(struct vm_vnull_t);
        if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        struct vm_vnull_t *value = (struct vm_vnull_t *)&varstack->buffer[ret];
        value->type = VNULL;
        value->ret = token;
        varstack->nrbytes = size;
      } break;
      default: {
        Serial1.printf("err: %s %d\n", __FUNCTION__, __LINE__);
        return -1;
      } break;
    }

    if(rule_token(&obj->ast, token, &outA) < 0) {
      FREE(outA);
      FREE(outB);
      return -1;
    }
  } else if(var->token[0] == '#') {
    varstack = &global_varstack;

    uint8_t move = 0;
    for(x=4;x<varstack->nrbytes;x++) {
      switch(varstack->buffer[x]) {
        case VINTEGER: {
          struct vm_gvinteger_t *val = (struct vm_gvinteger_t *)&varstack->buffer[x];

          if(rule_token(&rules[val->rule-1]->ast, val->ret, &outC) < 0) {
            FREE(outA);
            FREE(outB);
            return -1;
          }
          if(outC[0] != TVALUE) {
            FREE(outA);
            FREE(outB);
            FREE(outC);
            return -1;
          }

          struct vm_tvalue_t *foo = (struct vm_tvalue_t *)outC;

          if(stricmp((char *)foo->token, (char *)var->token) == 0) {
            move = 1;

            ret = sizeof(struct vm_gvinteger_t);
            memmove(&varstack->buffer[x], &varstack->buffer[x+ret], varstack->nrbytes-x-ret);
            if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
              OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
            }
            varstack->nrbytes -= ret;
          }
          FREE(outC);
        } break;
        case VFLOAT: {
          struct vm_gvfloat_t *val = (struct vm_gvfloat_t *)&varstack->buffer[x];

          if(rule_token(&rules[val->rule-1]->ast, val->ret, &outC) < 0) {
            FREE(outA);
            FREE(outB);
            return -1;
          }
          if(outC[0] != TVALUE) {
            FREE(outA);
            FREE(outB);
            FREE(outC);
            return -1;
          }

          struct vm_tvalue_t *foo = (struct vm_tvalue_t *)outC;

          if(stricmp((char *)foo->token, (char *)var->token) == 0) {
            move = 1;

            ret = sizeof(struct vm_gvfloat_t);
            memmove(&varstack->buffer[x], &varstack->buffer[x+ret], varstack->nrbytes-x-ret);
            if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
              OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
            }
            varstack->nrbytes -= ret;
          }
          FREE(outC);
        } break;
        case VNULL: {
          struct vm_gvnull_t *val = (struct vm_gvnull_t *)&varstack->buffer[x];

          if(rule_token(&rules[val->rule-1]->ast, val->ret, &outC) < 0) {
            FREE(outA);
            FREE(outB);
            return -1;
          }
          if(outC[0] != TVALUE) {
            FREE(outA);
            FREE(outB);
            FREE(outC);
            return -1;
          }

          struct vm_tvalue_t *foo = (struct vm_tvalue_t *)outC;

          if(stricmp((char *)foo->token, (char *)var->token) == 0) {
            move = 1;

            ret = sizeof(struct vm_gvnull_t);
            memmove(&varstack->buffer[x], &varstack->buffer[x+ret], varstack->nrbytes-x-ret);
            if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
              OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
            }
            varstack->nrbytes -= ret;
          }
          FREE(outC);
        } break;
        default: {
          Serial1.printf("err: %s %d\n", __FUNCTION__, __LINE__);
          FREE(outB);
          return -1;
        } break;
      }

      if(x == varstack->nrbytes) {
        break;
      }

      switch(varstack->buffer[x]) {
        case VINTEGER: {
          if(move == 1 && x < varstack->nrbytes) {
            struct vm_gvinteger_t *node = (struct vm_gvinteger_t *)&varstack->buffer[x];
            if(node->ret > 0) {
              if(rule_token(&rules[node->rule-1]->ast, node->ret, &outC) < 0) {
                FREE(outA);
                FREE(outB);
                return -1;
              }
              if(outC[0] != TVALUE) {
                FREE(outA);
                FREE(outB);
                FREE(outC);
                return -1;
              }

              struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)outC;
              tmp->go = x;

              if(rule_token(&rules[node->rule-1]->ast, node->ret, &outC) < 0) {
                FREE(outA);
                FREE(outB);
                FREE(outC);
                return -1;
              }
              FREE(outC);
            }
          }
          x += sizeof(struct vm_gvinteger_t)-1;
        } break;
        case VFLOAT: {
          if(move == 1 && x < varstack->nrbytes) {
            struct vm_gvfloat_t *node = (struct vm_gvfloat_t *)&varstack->buffer[x];
            if(node->ret > 0) {
              if(rule_token(&rules[node->rule-1]->ast, node->ret, &outC) < 0) {
                FREE(outA);
                FREE(outB);
                return -1;
              }
              if(outC[0] != TVALUE) {
                FREE(outA);
                FREE(outB);
                FREE(outC);
                return -1;
              }

              struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)outC;
              tmp->go = x;

              if(rule_token(&rules[node->rule-1]->ast, node->ret, &outC) < 0) {
                FREE(outA);
                FREE(outB);
                FREE(outC);
                return -1;
              }
              FREE(outC);
            }
          }
          x += sizeof(struct vm_gvfloat_t)-1;
        } break;
        case VNULL: {
          if(move == 1 && x < varstack->nrbytes) {
            struct vm_gvnull_t *node = (struct vm_gvnull_t *)&varstack->buffer[x];
            if(node->ret > 0) {
              if(rule_token(&rules[node->rule-1]->ast, node->ret, &outC) < 0) {
                FREE(outA);
                FREE(outB);
                return -1;
              }
              if(outC[0] != TVALUE) {
                FREE(outA);
                FREE(outB);
                FREE(outC);
                return -1;
              }

              struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)outC;
              tmp->go = x;

              if(rule_token(&rules[node->rule-1]->ast, node->ret, &outC) < 0) {
                FREE(outA);
                FREE(outB);
                FREE(outC);
                return -1;
              }
              FREE(outC);
            }
          }
          x += sizeof(struct vm_gvnull_t)-1;
        } break;
      }
    }

    ret = varstack->nrbytes;
    var->go = ret;

    switch(outB[0]) {
      case VINTEGER: {
        uint16_t size = varstack->nrbytes+sizeof(struct vm_gvinteger_t);
        if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        memset(&varstack->buffer[varstack->nrbytes], 0, sizeof(struct vm_gvinteger_t));
        struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)outB;
        struct vm_gvinteger_t *value = (struct vm_gvinteger_t *)&varstack->buffer[ret];
        value->type = VINTEGER;
        value->ret = token;
        value->value = cpy->value;
        value->rule = obj->nr;

        varstack->nrbytes += sizeof(struct vm_gvinteger_t);
        varstack->bufsize = size;
      } break;
      case VFLOAT: {
        uint16_t size = varstack->nrbytes + sizeof(struct vm_gvfloat_t);
        if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        memset(&varstack->buffer[varstack->nrbytes], 0, sizeof(struct vm_gvfloat_t));
        struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)outB;
        struct vm_gvfloat_t *value = (struct vm_gvfloat_t *)&varstack->buffer[ret];
        value->type = VFLOAT;
        value->ret = token;
        value->value = cpy->value;
        value->rule = obj->nr;

        varstack->nrbytes += sizeof(struct vm_gvfloat_t);
        varstack->bufsize = size;
      } break;
      case VNULL: {
        uint16_t size = varstack->nrbytes + sizeof(struct vm_gvnull_t);
        if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        memset(&varstack->buffer[varstack->nrbytes], 0, sizeof(struct vm_gvnull_t));
        struct vm_gvnull_t *value = (struct vm_gvnull_t *)&varstack->buffer[ret];
        value->type = VNULL;
        value->ret = token;
        value->rule = obj->nr;

        varstack->nrbytes += sizeof(struct vm_gvnull_t);
        varstack->bufsize = size;
      } break;
      default: {
        Serial1.printf("err: %s %d\n", __FUNCTION__, __LINE__);
        FREE(outB);
        return -1;
      } break;
    }

    if(rule_token(&obj->ast, token, &outA) < 0) {
      FREE(outA);
      FREE(outB);
      return -1;
    }
  } else if(var->token[0] == '@') {
    char *payload = NULL;
    unsigned int len = 0;

    if(rule_token(obj->varstack, val, &outB) < 0) {
      FREE(outA);
      return -1;
    }

    switch(outB[0]) {
      case VINTEGER: {
        struct vm_vinteger_t *na = (struct vm_vinteger_t *)outB;

        len = snprintf_P(NULL, 0, PSTR("%d"), (int)na->value);
        if((payload = (char *)MALLOC(len+1)) == NULL) {
          OUT_OF_MEMORY
        }
        snprintf_P(payload, len+1, PSTR("%d"), (int)na->value);

      } break;
      case VFLOAT: {
        struct vm_vfloat_t *na = (struct vm_vfloat_t *)outB;

        len = snprintf_P(NULL, 0, PSTR("%g"), (float)na->value);
        if((payload = (char *)MALLOC(len+1)) == NULL) {
          OUT_OF_MEMORY
        }
        snprintf_P(payload, len+1, PSTR("%g"), (float)na->value);
      } break;
      case VCHAR: {
        struct vm_vchar_t *na = (struct vm_vchar_t *)outB;

        len = snprintf_P(NULL, 0, PSTR("%s"), na->value);
        if((payload = (char *)MALLOC(len+1)) == NULL) {
          OUT_OF_MEMORY
        }
        snprintf_P(payload, len+1, PSTR("%s"), na->value);
      } break;
    }
    FREE(outB);

    if(parsing == 0 && !heishamonSettings.listenonly) {
      unsigned char cmd[256] = { 0 };
      char log_msg[256] = { 0 };

      for(uint8_t x = 0; x < sizeof(commands) / sizeof(commands[0]); x++) {
        cmdStruct tmp;
        memcpy_P(&tmp, &commands[x], sizeof(tmp));
        if(stricmp((char *)&var->token[1], tmp.name) == 0) {
          uint16_t len = tmp.func(payload, cmd, log_msg);
          log_message(log_msg);
          send_command(cmd, len);
          break;
        }
      }

      memset(&cmd, 256, 0);
      memset(&log_msg, 256, 0);

      if(heishamonSettings.optionalPCB) {
        //optional commands
        for(uint8_t x = 0; x < sizeof(optionalCommands) / sizeof(optionalCommands[0]); x++) {
          optCmdStruct tmp;
          memcpy_P(&tmp, &optionalCommands[x], sizeof(tmp));
          if(stricmp((char *)&var->token[1], tmp.name) == 0) {
            uint16_t len = tmp.func(payload, log_msg);
            log_message(log_msg);
            break;
          }
        }
      }
    }
    FREE(payload);
  } else if(var->token[0] == '?') {
    int x = 0;
    while(heishaOTDataStruct[x].name != NULL) {
      if(heishaOTDataStruct[x].rw <= 2 && stricmp((char *)&var->token[1], heishaOTDataStruct[x].name) == 0) {
        if(heishaOTDataStruct[x].type == TBOOL) {
          switch(outB[0]) {
            case VINTEGER: {
              struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)outB;

              heishaOTDataStruct[x].value.b = (bool)cpy->value;
            } break;
            case VFLOAT: {
              struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)outB;

              heishaOTDataStruct[x].value.b = (bool)cpy->value;
            } break;
          }
        } else if(heishaOTDataStruct[x].type == TFLOAT) {
          switch(outB[0]) {
            case VINTEGER: {
              struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)outB;

              heishaOTDataStruct[x].value.f = (float)cpy->value;
            } break;
            case VFLOAT: {
              struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)outB;

              heishaOTDataStruct[x].value.f = cpy->value;
            } break;
          }
        }
        break;
      }
      x++;
    }
  }
  FREE(outA);
  FREE(outB);
  return 0;
}

static void vm_value_prt(struct rules_t *obj, char *out, uint16_t size) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t x = 0, pos = 0;
  unsigned char *outA = NULL;

  for(x=4;x<varstack->nrbytes;x++) {
    if(x < varstack->nrbytes) {
      switch(varstack->buffer[x]) {
        case VINTEGER: {
          struct vm_vinteger_t *val = (struct vm_vinteger_t *)&varstack->buffer[x];

          if(rule_token(&obj->ast, val->ret, &outA) < 0) {
            return;
          }
          if(outA[0] != TVALUE) {
            FREE(outA);
            return;
          }

          struct vm_tvalue_t *node = (struct vm_tvalue_t *)outA;
          pos += snprintf(&out[pos], size - pos, "%s = %d\n", node->token, val->value);

          FREE(outA);
          x += sizeof(struct vm_vinteger_t)-1;
        } break;
        case VFLOAT: {
          struct vm_vfloat_t *val = (struct vm_vfloat_t *)&varstack->buffer[x];
          if(rule_token(&obj->ast, val->ret, &outA) < 0) {
            return;
          }
          if(outA[0] != TVALUE) {
            FREE(outA);
            return;
          }

          struct vm_tvalue_t *node = (struct vm_tvalue_t *)outA;

          float f = 0.0;
          uint322float(val->value, &f);
          pos += snprintf(&out[pos], size - pos, "%s = %g\n", node->token, f);

          FREE(outA);
          x += sizeof(struct vm_vfloat_t)-1;
        } break;
        case VNULL: {
          struct vm_vnull_t *val = (struct vm_vnull_t *)&varstack->buffer[x];
          if(rule_token(&obj->ast, val->ret, &outA) < 0) {
            return;
          }
          if(outA[0] != TVALUE) {
            FREE(outA);
            return;
          }

          struct vm_tvalue_t *node = (struct vm_tvalue_t *)outA;
          pos += snprintf(&out[pos], size - pos, "%s = NULL\n", node->token);

          FREE(outA);
          x += sizeof(struct vm_vnull_t)-1;
        } break;
        default: {
          return;
        } break;
      }
    }
  }
}

static void vm_global_value_prt(char *out, int size) {
  struct rule_stack_t *varstack = &global_varstack;
  uint16_t x = 0, pos = 0;
  unsigned char *outA = NULL;

  for(x=4;x<varstack->nrbytes;x++) {
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        struct vm_gvinteger_t *val = (struct vm_gvinteger_t *)&varstack->buffer[x];

        if(rule_token(&rules[val->rule-1]->ast, val->ret, &outA) < 0) {
          return;
        }
        if(outA[0] != TVALUE) {
          FREE(outA);
          return;
        }

        struct vm_tvalue_t *node = (struct vm_tvalue_t *)outA;
        pos += snprintf(&out[pos], size - pos, "%d %s = %d\n", x, node->token, val->value);

        FREE(outA);
        x += sizeof(struct vm_gvinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_gvfloat_t *val = (struct vm_gvfloat_t *)&varstack->buffer[x];

        if(rule_token(&rules[val->rule-1]->ast, val->ret, &outA) < 0) {
          return;
        }
        if(outA[0] != TVALUE) {
          FREE(outA);
          return;
        }

        struct vm_tvalue_t *node = (struct vm_tvalue_t *)outA;

        float f = 0.0;
        uint322float(val->value, &f);
        pos += snprintf(&out[pos], size - pos, "%d %s = %g\n", x, node->token, f);

        FREE(outA);
        x += sizeof(struct vm_gvfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_gvnull_t *val = (struct vm_gvnull_t *)&varstack->buffer[x];
        if(rule_token(&rules[val->rule-1]->ast, val->ret, &outA) < 0) {
          return;
        }
        if(outA[0] != TVALUE) {
          FREE(outA);
          return;
        }

        struct vm_tvalue_t *node = (struct vm_tvalue_t *)outA;

        pos += snprintf(&out[pos], size - pos, "%d %s = NULL\n", x, node->token);

        FREE(outA);
        x += sizeof(struct vm_gvnull_t)-1;
      } break;
      default: {
        return;
      } break;
    }
  }
}

void rules_timer_cb(int nr) {
  char *name = NULL;
  int x = 0, i = 0;

  i = snprintf_P(NULL, 0, PSTR("timer=%d"), nr);
  if((name = (char *)MALLOC(i+2)) == NULL) {
    OUT_OF_MEMORY
  }
  memset(name, 0, i+2);
  snprintf_P(name, i+1, PSTR("timer=%d"), nr);

  // logprintf_P(F("_______ %s %s"), __FUNCTION__, name);

  nr = rule_by_name(rules, nrrules, name);
  if(nr > -1) {
    rule_call(nr);
  }
  FREE(name);
}

int rules_parse(char *file) {
  File frules = LittleFS.open(file, "r");
  if(frules) {
    parsing = 1;

    if(nrrules > 0) {
      for(int i=0;i<nrrules;i++) {
        struct rule_stack_t *node = (struct rule_stack_t *)rules[i]->userdata;
        FREE(node->buffer);
        FREE(node);
      }
      nrrules = 0;
    }
    memset(mempool, 0, MEMPOOL_SIZE);

    global_varstack.buffer = NULL;
    global_varstack.nrbytes = 4;

#define BUFFER_SIZE 128
    char content[BUFFER_SIZE];
    memset(content, 0, BUFFER_SIZE);
    int len = frules.size();
    int chunk = 0, len1 = 0;

    unsigned int txtoffset = alignedbuffer(MEMPOOL_SIZE-len-5);

    while(1) {
      memset(content, 0, BUFFER_SIZE);
      frules.seek(chunk*BUFFER_SIZE, SeekSet);
      if (chunk * BUFFER_SIZE <= len) {
        frules.readBytes(content, BUFFER_SIZE);
        len1 = BUFFER_SIZE;
      } else if ((chunk * BUFFER_SIZE) >= len && (chunk * BUFFER_SIZE) <= len + BUFFER_SIZE) {
        frules.readBytes(content, len - ((chunk - 1)*BUFFER_SIZE));
        len1 = len - ((chunk - 1) * BUFFER_SIZE);
      } else {
        break;
      }
      memcpy(&mempool[txtoffset+(chunk*BUFFER_SIZE)], &content, alignedbuffer(len1));
      chunk++;
    }
    frules.close();

    struct rule_stack_t *varstack = (struct rule_stack_t *)MALLOC(sizeof(struct rule_stack_t));
    if(varstack == NULL) {
      OUT_OF_MEMORY
    }
    memset(varstack, 0, sizeof(struct rule_stack_t));
    varstack->buffer = NULL;
    varstack->nrbytes = 4;
    varstack->bufsize = 4;

    struct pbuf mem;
    struct pbuf input;
    memset(&mem, 0, sizeof(struct pbuf));
    memset(&input, 0, sizeof(struct pbuf));

    mem.payload = mempool;
    mem.len = 0;
    mem.tot_len = MEMPOOL_SIZE;

    input.payload = &mempool[txtoffset];
    input.len = txtoffset;
    input.tot_len = len;

    int ret = 0;
    while((ret = rule_initialize(&input, &rules, &nrrules, &mem, varstack)) == 0) {
      varstack = (struct rule_stack_t *)MALLOC(sizeof(struct rule_stack_t));
      if(varstack == NULL) {
        OUT_OF_MEMORY
      }
      varstack->buffer = NULL;
      varstack->nrbytes = 4;
      varstack->bufsize = 4;
      input.payload = &mempool[input.len];
    }

    logprintf_P(F("rules memory used: %d / %d"), mem.len, mem.tot_len);

    if(ret >= 0 && nrrules > 1) {
      FREE(varstack);
    }

    /*
     * Clear all timers
     */
    struct timerqueue_t *node = NULL;
    while((node = timerqueue_pop()) != NULL) {
      FREE(node);
    }

    uint8_t i = 0;
    for(i=0;i<nrrules;i++) {
      vm_clear_values(rules[i]);
    }

    FREE(global_varstack.buffer);
    global_varstack.buffer = NULL;
    global_varstack.nrbytes = 4;

    if(ret == -1) {
      if(nrrules > 0) {
        for(uint8_t i=0;i<nrrules;i++) {
          struct rule_stack_t *node = (struct rule_stack_t *)rules[i]->userdata;
          FREE(node->buffer);
          FREE(node);
        }
        FREE(rules[i]->timestamp);
        nrrules = 0;
      }
      nrrules = 0;
      FREE(rules);
      return -1;
    }

    for(uint8_t i=0;i<nrrules;i++) {
      vm_clear_values(rules[i]);
    }
    parsing = 0;
    return 0;
  } else {
    return -1;
  }
}

void rules_event_cb(const char *prefix, const char *name) {
  uint8_t len = strlen(name), len1 = strlen(prefix), tlen = 0;
  char buf[100] = { '\0' };
  snprintf_P((char *)&buf, 100, PSTR("%s%s"), prefix, name);
  int8_t nr = rule_by_name(rules, nrrules, (char *)buf);
  if(nr > -1) {
    logprintf_P(F("%s %s %s"), F("===="), name, F("===="));
    // logprintf_P(F("%s %d %s %d"), F(">>> rule"), nr, F("nrbytes:"), rules[nr]->ast.nrbytes);
    // logprintf_P(F("%s %d"), F(">>> global stack nrbytes:"), global_varstack.nrbytes);

    rule_call(nr);
    return;
  }
}

void rules_boot(void) {
  int8_t nr = rule_by_name(rules, nrrules, (char *)"System#Boot");
  if(nr > -1) {
    rule_call(nr);
  }
}

void rules_setup(void) {
  if(!LittleFS.begin()) {
    return;
  }
  memset(mempool, 0, MEMPOOL_SIZE);

  logprintf_P(F("rules mempool size: %d"), MEMPOOL_SIZE);

  logprintln_P(F("reading rules"));

  global_varstack.buffer = NULL;
  global_varstack.nrbytes = 4;

  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_token_cb = is_variable;
  rule_options.is_event_cb = is_event;
  rule_options.set_token_val_cb = vm_value_set;
  rule_options.get_token_val_cb = vm_value_get;
  rule_options.prt_token_val_cb = vm_value_prt;
  rule_options.clr_token_val_cb = vm_value_del;
  rule_options.event_cb = event_cb;

  if(LittleFS.exists("/rules.txt")) {
    if(rules_parse("/rules.txt") == -1) {
      return;
    }
  }

  rules_boot();
}

void rules_execute(void) {
  int8_t ret = 0;
  uint8_t nr = 0;
  while(1) {
    ret = rules_loop(rules, nrrules, &nr);
    if(ret == 0) {
      char out[512];
      char *name = rule_by_nr(rules, nrrules, nr);
      if(name != NULL) {
        log_message(name);
        logprintf_P(F("%s %s %s"), F("===="), name, F("===="));
        FREE(name);
      }
      // logprintf_P(F("%s %d %s %d"), F(">>> rule"), nr, F("nrbytes:"), rules[nr]->ast.nrbytes);
      // logprintf_P(F("%s %d"), F(">>> global stack nrbytes:"), global_varstack.nrbytes);

      logprintln_P(F("\n>>> local variables"));
      memset(&out, 0, sizeof(out));
      vm_value_prt(rules[nr], (char *)&out, sizeof(out));
      logprintln(out);
      logprintln_P(F(">>> global variables"));
      memset(&out, 0, sizeof(out));
      vm_global_value_prt((char *)&out, sizeof(out));
      logprintln(out);
    }
    if(ret == -2) {
      break;
    }
  }
}
