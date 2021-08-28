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

#include "mem.h"
#include "../../heishamon.h"

void logprintln(char *msg) {
  if(heishamonSettings.logSerial1) {
    Serial1.print(millis());
    Serial1.print(": ");
    Serial1.println(msg);
  }
  if(heishamonSettings.logMqtt && mqtt_client.connected()) {
    char log_topic[256];
    sprintf(log_topic, "%s/%s", heishamonSettings.mqtt_topic_base, mqtt_logtopic);

    if(!mqtt_client.publish(log_topic, msg)) {
      Serial1.print(millis());
      Serial1.print(F(": "));
      Serial1.println(F("MQTT publish log message failed!"));
      mqtt_client.disconnect();
    }
  }
  if(webSocket.connectedClients() > 0) {
    webSocket.broadcastTXT(msg, strlen(msg));
  }
}

void logprintf(char *fmt, ...) {
  char *str = NULL;

  va_list ap, apcpy;
  va_copy(apcpy, ap);
  va_start(apcpy, fmt);

  int bytes = vsnprintf(NULL, 0, fmt, apcpy);

  va_end(apcpy);
  if((str = (char *)MALLOC(bytes+1)) == NULL) {
    OUT_OF_MEMORY
  }
  va_start(ap, fmt);
  vsprintf(str, fmt, ap);
  va_end(ap);

  logprintln(str);

  FREE(str);
}

void logprintln_P(const __FlashStringHelper *msg) {
  PGM_P p = (PGM_P)msg;
  int len = strlen_P((const char *)p);
  char *str = (char *)MALLOC(len+1);
  if(str == NULL) {
    OUT_OF_MEMORY
  }
  strcpy_P(str, p);

  logprintln(str);

  FREE(str);
}

void logprintf_P(const __FlashStringHelper *fmt, ...) {
  PGM_P p = (PGM_P)fmt;
  int len = strlen_P((const char *)p);
  char *foo = (char *)MALLOC(len+1);
  if(foo == NULL) {
    OUT_OF_MEMORY
  }
  strcpy_P(foo, p);

  char *str = NULL;

  va_list ap, apcpy;
  va_copy(apcpy, ap);
  va_start(apcpy, fmt);

  int bytes = vsnprintf(NULL, 0, foo, apcpy);

  va_end(apcpy);
  if((str = (char *)MALLOC(bytes+1)) == NULL) {
    OUT_OF_MEMORY
  }
  va_start(ap, fmt);
  vsprintf(str, foo, ap);
  va_end(ap);

  logprintln(str);

  FREE(str);
}