/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _LOG_H_
#define _LOG_H_

#include <Arduino.h>

void logprintln(char *msg);
void logprintf(char *fmt, ...);
void logprintln_P(const __FlashStringHelper *msg);
void logprintf_P(const __FlashStringHelper *fmt, ...);

#endif
