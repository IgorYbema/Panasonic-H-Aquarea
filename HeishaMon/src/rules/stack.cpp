/*
	Copyright (C) 2013 - 2016 CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifdef ESP8266
  #include <Arduino.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "stack.h"
#include "rules.h"
#include "../common/uint32float.h"

uint16_t rule_stack_push(struct rule_stack_t *stack, void *in) {
  uint16_t ret = 0;

#ifdef DEBUG
  #if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)stack >= (void *)MMU_SEC_HEAP) {
    printf("%s %d %d\n", __FUNCTION__, __LINE__, mmu_get_uint16(&stack->nrbytes));
  } else {
  #endif
    printf("%s %d %d\n", __FUNCTION__, __LINE__, stack->nrbytes);
  #if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
  #endif
#endif

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)stack >= (void *)MMU_SEC_HEAP) {
		if(in != NULL) {
			uint8_t type = mmu_get_uint8(&((unsigned char *)in)[0]);
			ret = mmu_get_uint16(&stack->nrbytes);
			uint16_t size = ret+rule_max_var_bytes();

			switch(type) {
				case VINTEGER: {
					struct vm_vinteger_t *node = (struct vm_vinteger_t *)in;
					struct vm_vinteger_t *out = (struct vm_vinteger_t *)&stack->buffer[ret];
					mmu_set_uint16(&out->ret, mmu_get_uint8(&node->ret));
					mmu_set_uint8(&out->type, VINTEGER);
					out->value = node->value;
				} break;
				case VFLOAT: {
					struct vm_vfloat_t *node = (struct vm_vfloat_t *)in;
					struct vm_vfloat_t *out = (struct vm_vfloat_t *)&stack->buffer[ret];
					mmu_set_uint16(&out->ret, mmu_get_uint8(&node->ret));
					mmu_set_uint8(&out->type, VFLOAT);
					out->value = node->value;
				} break;
        case VNULL: {
					struct vm_vnull_t *node = (struct vm_vnull_t *)in;
					struct vm_vnull_t *out = (struct vm_vnull_t *)&stack->buffer[ret];
					mmu_set_uint16(&out->ret, mmu_get_uint8(&node->ret));
					mmu_set_uint8(&out->type, VNULL);
				} break;
			}

			mmu_set_uint16(&stack->nrbytes, size);
			mmu_set_uint16(&stack->bufsize, MAX(mmu_get_uint16(&stack->bufsize), size));
		}
	} else {
#endif
		if(in != NULL) {
			uint8_t type = ((unsigned char *)in)[0];
			ret = stack->nrbytes;
			uint16_t size = ret+rule_max_var_bytes();

			switch(type) {
				case VINTEGER: {
					struct vm_vinteger_t *node = (struct vm_vinteger_t *)in;
					struct vm_vinteger_t *out = (struct vm_vinteger_t *)&stack->buffer[ret];
					out->ret = node->ret;
					out->type = VINTEGER;
					out->value = node->value;
				} break;
				case VFLOAT: {
					struct vm_vfloat_t *node = (struct vm_vfloat_t *)in;
					struct vm_vfloat_t *out = (struct vm_vfloat_t *)&stack->buffer[ret];
					out->ret = node->ret;
					out->type = VFLOAT;
					out->value = node->value;
				} break;
				case VNULL: {
					struct vm_vnull_t *node = (struct vm_vnull_t *)in;
					struct vm_vnull_t *out = (struct vm_vnull_t *)&stack->buffer[ret];
					out->ret = node->ret;
					out->type = VNULL;
				} break;
			}

			stack->nrbytes = size;
			stack->bufsize = MAX(stack->bufsize, size);
		}
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
	}
#endif

  return ret;
}

int8_t rule_stack_pull(struct rule_stack_t *stack, uint16_t idx, unsigned char buf[8]) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)stack >= (void *)MMU_SEC_HEAP) {
    uint8_t type = mmu_get_uint8(&stack->buffer[idx]);
    switch(type) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&stack->buffer[idx];
        struct vm_vinteger_t *val = (struct vm_vinteger_t *)&buf[0];
        memset(&buf, 0, sizeof(struct vm_vinteger_t));
        val->type = VINTEGER;
        val->value = node->value;
        val->ret = mmu_get_uint16(&node->ret);
        return 0;
      }
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&stack->buffer[idx];
        struct vm_vfloat_t *val = (struct vm_vfloat_t *)&buf[0];
        memset(&buf, 0, sizeof(struct vm_vfloat_t));
        val->type = VFLOAT;
        val->value = node->value;
        val->ret = mmu_get_uint16(&node->ret);
        return 0;
      }
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&stack->buffer[idx];
        struct vm_vnull_t *val = (struct vm_vnull_t *)&buf[0];
        memset(&buf, 0, sizeof(struct vm_vnull_t));
        val->type = VNULL;
        val->ret = mmu_get_uint16(&node->ret);
        return 0;
      }
    }
	} else {
#endif
		uint8_t type = stack->buffer[idx];
    switch(type) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&stack->buffer[idx];
        struct vm_vinteger_t *val = (struct vm_vinteger_t *)&buf[0];
        memset(&buf, 0, sizeof(struct vm_vinteger_t));
        val->type = VINTEGER;
        val->value = node->value;
        val->ret = node->ret;
        return 0;
      }
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&stack->buffer[idx];
        struct vm_vfloat_t *val = (struct vm_vfloat_t *)&buf[0];
        memset(&buf, 0, sizeof(struct vm_vfloat_t));
        val->type = VFLOAT;
        val->value = node->value;
        val->ret = node->ret;
        return 0;
      }
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&stack->buffer[idx];
        struct vm_vnull_t *val = (struct vm_vnull_t *)&buf[0];
        memset(&buf, 0, sizeof(struct vm_vnull_t));
        val->type = VNULL;
        val->ret = node->ret;
        return 0;
      }
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  return -1;
}