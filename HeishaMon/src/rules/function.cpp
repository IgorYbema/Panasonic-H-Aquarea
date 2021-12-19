/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifdef ESP8266
  #pragma GCC diagnostic warning "-fpermissive"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <libgen.h>
#include <assert.h>
#include <math.h>

#include "rules.h"
#include "../common/mem.h"
#include "function.h"

#include "max.h"
#include "min.h"
#include "coalesce.h"
#include "round.h"
#include "settimer.h"
#include "isset.h"

struct rule_function_t rule_functions[] = {
  { "max", event_function_max_callback },
  { "min", event_function_min_callback },
  { "coalesce", event_function_coalesce_callback },
  { "round", event_function_round_callback },
  { "setTimer", event_function_set_timer_callback },
  { "isset", event_function_isset_callback },
};

int nr_rule_functions = sizeof(rule_functions)/sizeof(rule_functions[0]);
