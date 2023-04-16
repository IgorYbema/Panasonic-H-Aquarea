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
#include "../rules.h"

int8_t rule_operator_minus_callback(struct rules_t *obj, uint16_t a, uint16_t b, uint16_t *ret) {
  unsigned char nodeA[rule_max_var_bytes()], nodeB[rule_max_var_bytes()];
  rule_stack_pull(obj->varstack, a, nodeA);
  rule_stack_pull(obj->varstack, b, nodeB);

  if(nodeA[0] == VNULL || nodeB[0] == VNULL) {
    struct vm_vnull_t out;
    out.ret = 0;
    out.type = VNULL;

    *ret = rule_stack_push(obj->varstack, &out);

/* LCOV_EXCL_START*/
#ifdef DEBUG
    printf("%s NULL\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/

  } else if(nodeA[0] == VCHAR || nodeB[0] == VCHAR) {
  } else if(nodeA[0] == VFLOAT && nodeB[0] == VFLOAT) {
    struct vm_vfloat_t out;
    struct vm_vfloat_t *na = (struct vm_vfloat_t *)&nodeA[0];
    struct vm_vfloat_t *nb = (struct vm_vfloat_t *)&nodeB[0];

    float av = 0.0, bv = 0.0;
    uint322float(na->value, &av);
    uint322float(nb->value, &bv);

    out.ret = 0;
    out.type = VFLOAT;

    float2uint32(av - bv, &out.value);

/* LCOV_EXCL_START*/
#ifdef DEBUG
    printf("%s %g %g\n", __FUNCTION__, av, bv);
#endif
/* LCOV_EXCL_STOP*/

    *ret = rule_stack_push(obj->varstack, &out);
  } else if(nodeA[0] == VFLOAT || nodeB[0] == VFLOAT) {
    float f = 0;
    int i = 0;

    struct vm_vfloat_t out;
    out.ret = 0;
    out.type = VFLOAT;

    if(nodeA[0] == VFLOAT) {
      struct vm_vfloat_t *na = (struct vm_vfloat_t *)&nodeA[0];
      uint322float(na->value, &f);
    } else if(nodeA[0] == VINTEGER) {
      struct vm_vinteger_t *na = (struct vm_vinteger_t *)&nodeA[0];
      i = na->value;
    }
    if(nodeB[0] == VFLOAT) {
      struct vm_vfloat_t *nb = (struct vm_vfloat_t *)&nodeB[0];
      uint322float(nb->value, &f);
      float2uint32(i - f, &out.value);
    } else if(nodeB[0] == VINTEGER) {
      struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&nodeB[0];
      float2uint32(f - nb->value, &out.value);
    }


/* LCOV_EXCL_START*/
#ifdef DEBUG
    printf("%s %g %d\n", __FUNCTION__, f, i);
#endif
/* LCOV_EXCL_STOP*/

    *ret = rule_stack_push(obj->varstack, &out);
  } else {
    struct vm_vinteger_t out;
    struct vm_vinteger_t *na = (struct vm_vinteger_t *)&nodeA[0];
    struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&nodeB[0];

    out.ret = 0;
    out.type = VINTEGER;
    out.value = na->value - nb->value;

/* LCOV_EXCL_START*/
#ifdef DEBUG
    printf("%s %d %d\n", __FUNCTION__, na->value, nb->value);
#endif
/* LCOV_EXCL_STOP*/

    *ret = rule_stack_push(obj->varstack, &out);
  }

  return 0;
}
