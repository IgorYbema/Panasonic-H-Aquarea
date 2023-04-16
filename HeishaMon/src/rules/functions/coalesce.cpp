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

#include "../function.h"
#include "../rules.h"

int8_t rule_function_coalesce_callback(struct rules_t *obj, uint16_t argc, uint16_t *argv, uint16_t *ret) {
/* LCOV_EXCL_START*/
#ifdef DEBUG
  printf("%s\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/

  uint16_t i = 0;
  for(i=0;i<argc;i++) {
    unsigned char nodeA[rule_max_var_bytes()];
    rule_stack_pull(obj->varstack, argv[i], nodeA);
    if(nodeA[0] == VNULL) {
      continue;
    } else {
      switch(nodeA[0]) {
        case VINTEGER: {
          struct vm_vinteger_t out;
          struct vm_vinteger_t *val = (struct vm_vinteger_t *)&nodeA[0];
          out.type = VINTEGER;
          out.ret = 0;
          out.value = val->value;

          *ret = rule_stack_push(obj->varstack, &out);
          return 0;
        } break;
        case VFLOAT: {
          struct vm_vfloat_t out;
          struct vm_vfloat_t *val = (struct vm_vfloat_t *)&nodeA[0];
          out.type = VFLOAT;
          out.ret = 0;
          out.value = val->value;

          *ret = rule_stack_push(obj->varstack, &out);
          return 0;
        } break;
        /* LCOV_EXCL_START*/
        case VCHAR: {
          exit(-1);
        } break;
        /* LCOV_EXCL_STOP*/
        default: {

        } break;
      }
    }
  }

  return 0;
}
