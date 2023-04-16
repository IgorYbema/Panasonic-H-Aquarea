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

int8_t rule_operator_le_callback(struct rules_t *obj, uint16_t a, uint16_t b, uint16_t *ret) {
  struct vm_vinteger_t out;
  out.ret = 0;
  out.type = VINTEGER;

  unsigned char nodeA[rule_max_var_bytes()], nodeB[rule_max_var_bytes()];
  rule_stack_pull(obj->varstack, a, nodeA);
  rule_stack_pull(obj->varstack, b, nodeB);

  /*
   * Values can only be equal when the type matches
   */
  if(
      (
        nodeA[0] == VNULL || nodeA[0] == VCHAR ||
        nodeB[0] == VNULL || nodeB[0] == VCHAR
      )
    &&
      (nodeA[0] != nodeB[0])
    ) {
    out.value = 0;
  } else {
    switch(nodeA[0]) {
      case VNULL: {
        out.value = 0;

/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("%s NULL\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/
      } break;
      case VINTEGER: {
        float av = 0.0;
        float bv = 0.0;
        if(nodeA[0] == VINTEGER) {
          struct vm_vinteger_t *na = (struct vm_vinteger_t *)&nodeA[0];
          av = (float)na->value;
        }
        if(nodeB[0] == VFLOAT) {
          struct vm_vfloat_t *nb = (struct vm_vfloat_t *)&nodeB[0];
          uint322float(nb->value, &bv);
        }
        if(nodeB[0] == VINTEGER) {
          struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&nodeB[0];
          bv = (float)nb->value;
        }
        if(av < bv || fabs(av-bv) < EPSILON) {
          out.value = 1;
        } else {
          out.value = 0;
        }

/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("%s %g %g\n", __FUNCTION__, av, bv);
#endif
/* LCOV_EXCL_STOP*/
      } break;
      case VFLOAT: {
        float av = 0.0;
        float bv = 0.0;
        if(nodeA[0] == VFLOAT) {
          struct vm_vfloat_t *na = (struct vm_vfloat_t *)&nodeA[0];
          uint322float(na->value, &av);
        }
        if(nodeB[0] == VFLOAT) {
          struct vm_vfloat_t *nb = (struct vm_vfloat_t *)&nodeB[0];
          uint322float(nb->value, &bv);
        }
        if(nodeB[0] == VINTEGER) {
          struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&nodeB[0];
          bv = (float)nb->value;
        }

        if(av < bv || fabs(av-bv) < EPSILON) {
          out.value = 1;
        } else {
          out.value = 0;
        }

/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("%s %g %g\n", __FUNCTION__, av, bv);
#endif
/* LCOV_EXCL_STOP*/
      } break;
      /*
       * FIXME
       */
      /* LCOV_EXCL_START*/
      case VCHAR: {
      } break;
      /* LCOV_EXCL_STOP*/
    }
  }

  *ret = rule_stack_push(obj->varstack, &out);

  return 0;
}