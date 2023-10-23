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

#include "../../common/uint32float.h"
#include "../function.h"
#include "../rules.h"

int8_t rule_function_floor_callback(struct rules_t *obj, uint16_t argc, uint16_t *argv, uint16_t *ret) {
/* LCOV_EXCL_START*/
#ifdef DEBUG
  printf("%s\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/

  if(argc != 1) {
    return -1;
  }

  struct vm_vinteger_t out;
  out.ret = 0;
  out.type = VINTEGER;
  out.value = 0;

  unsigned char nodeA[rule_max_var_bytes()];
  rule_stack_pull(obj->varstack, argv[0], nodeA);
  switch(nodeA[0]) {
    case VINTEGER: {
      struct vm_vinteger_t *val = (struct vm_vinteger_t *)nodeA;
      out.value = val->value;
    } break;
    case VFLOAT: {
      struct vm_vfloat_t *val = (struct vm_vfloat_t *)nodeA;
      float v = 0.0;
      uint322float(val->value, &v);
      out.value = (int)floor(v);
    } break;
  }

  *ret = rule_stack_push(obj->varstack, &out);

  return 0;
}
