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
#include <math.h>
#include <sys/time.h>
#include <unistd.h>

#include "../function.h"
#include "../../common/log.h"
#include "../../common/mem.h"
#include "../rules.h"
#include "../../common/timerqueue.h"

int8_t rule_function_set_timer_callback(struct rules_t *obj, uint16_t argc, uint16_t *argv, uint16_t *ret) {
  struct timerqueue_t *node = NULL;
  struct itimerval it_val;
  uint16_t i = 0, x = 0, sec = 0, usec = 0, nr = 0;

  unsigned char nodeA[rule_max_var_bytes()];
  unsigned char nodeB[rule_max_var_bytes()];

  if(argc > 2) {
    return -1;
  }

  if(argc == 2) {
    rule_stack_pull(obj->varstack, argv[0], nodeA);
    rule_stack_pull(obj->varstack, argv[1], nodeB);

    if(nodeA[0] != VINTEGER) {
      return -1;
    }
    if(nodeB[0] != VINTEGER) {
      return -1;
    }

    struct vm_vinteger_t *val = (struct vm_vinteger_t *)&nodeA[0];
    nr = val->value;

    val = (struct vm_vinteger_t *)&nodeB[0];

    timerqueue_insert(val->value, 0, nr);

    logprintf_P(F("%s set timer #%d to %d seconds"), __FUNCTION__, nr, val->value);
  }

  if(argc == 1) {
    if(nodeA[0] != VINTEGER) {
      return -1;
    }

    struct vm_vinteger_t *val = (struct vm_vinteger_t *)&nodeA[0];
    nr = val->value;

    uint16_t size = 0;

    int a = 0;
    for(a=0;a<timerqueue_size;a++) {
      if(timerqueue[a]->nr == nr) {
        struct vm_vinteger_t out;
        out.ret = 0;
        out.type = VINTEGER;
        out.value = timerqueue[a]->sec;

        *ret = rule_stack_push(obj->varstack, &out);
        break;
      }
    }
    if(size == 0) {
      struct vm_vnull_t out;
      out.ret = 0;
      out.type = VNULL;

      *ret = rule_stack_push(obj->varstack, &out);
    }
  }

  return 0;
}
