/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/


#ifndef _RULES_H_
#define _RULES_H_

#include <stdint.h>
#include <unistd.h>
#include "stack.h"

#ifndef ESP8266
  #define F
  #define MEMPOOL_SIZE 16000
  typedef struct pbuf {
    struct pbuf *next;
    void *payload;
    uint16_t tot_len;
    uint16_t len;
    uint8_t type;
    uint8_t flags;
    uint16_t ref;
  } pbuf;

  typedef struct serial_t {
    void (*printf)(const char *fmt, ...);
    void (*println)(const char *val);
    void (*flush)(void);
  } serial_t;
  extern struct serial_t Serial;
  extern void *MMU_SEC_HEAP;
  uint8_t mmu_set_uint8(void *ptr, uint8_t src);
  uint8_t mmu_get_uint8(void *ptr);
  uint16_t mmu_set_uint16(void *ptr, uint16_t src);
  uint16_t mmu_get_uint16(void *ptr);
#else
  #include <Arduino.h>
  #include "lwip/pbuf.h"
  #ifdef MMU_SEC_HEAP_SIZE
    #define MEMPOOL_SIZE MMU_SEC_HEAP_SIZE
  #else
    #define MEMPOOL_SIZE 16000
  #endif
#endif

#define EPSILON  0.000001

#define MAX(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define MIN(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

/*
 * Max 32 tokens are allowed
 */
typedef enum {
  TOPERATOR = 1,
  TFUNCTION = 2,
  TSTRING = 3,
  TNUMBER = 4,
  TNUMBER1 = 5,
  TNUMBER2 = 6,
  TNUMBER3 = 7,
  TEOF = 8,
  LPAREN = 9,
  RPAREN = 10,
  TCOMMA = 11,
  TIF = 12,
  TELSE = 13,
  TELSEIF = 14,
  TTHEN = 15,
  TEVENT = 16,
  TCEVENT = 17,
  TEND = 18,
  TVAR = 19,
  TASSIGN = 20,
  TSEMICOLON = 21,
  TTRUE = 22,
  TFALSE = 23,
  TSTART = 24,
  TVALUE = 25,
  VCHAR = 26,
  VINTEGER = 27,
  VFLOAT = 28,
  VNULL = 29
} token_types;

#ifdef DEBUG
struct {
  const char *name;
} token_names[] = {
  "",
  "TOPERATOR",
  "TFUNCTION",
  "TSTRING",
  "TNUMBER",
  "TNUMBER1",
  "TNUMBER2",
  "TNUMBER3",
  "TEOF",
  "LPAREN",
  "RPAREN",
  "TCOMMA",
  "TIF",
  "TELSE",
  "TELSEIF",
  "TTHEN",
  "TEVENT",
  "TCEVENT",
  "TEND",
  "TVAR",
  "TASSIGN",
  "TSEMICOLON",
  "TTRUE",
  "TFALSE",
  "TSTART",
  "TVALUE",
  "VCHAR",
  "VINTEGER",
  "VFLOAT",
  "VNULL"
};
#endif

extern int booting;

typedef struct rule_timer_t {
#ifdef ESP8266
  uint32_t first;
  uint32_t second;
#else
  struct timespec first;
  struct timespec second;
#endif
} __attribute__((aligned(4))) rule_timer_t;

typedef struct rules_t {
  /* --- PUBLIC MEMBERS --- */

   /* To what rule do we return after
    * being called from another rule.
    */
  struct {
    struct rules_t *go;
    struct rules_t *ret;
  } __attribute__((aligned(4))) ctx;
#ifndef NON32XFER_HANDLER
  uint32_t nr;
#else
  uint8_t nr;
#endif

  /* --- PRIVATE MEMBERS --- */

  /* Continue here after we processed
   * another rule call.
   */
  struct {
    uint16_t go;
    uint16_t ret;
  } __attribute__((aligned(4))) cont;

  void *userdata;

  uint8_t sync;

  struct rule_stack_t ast;
  struct rule_stack_t *varstack;
  struct rule_timer_t *timestamp;

} __attribute__((aligned(4))) rules_t;

typedef struct rule_options_t {
  /*
   * Identifying callbacks
   */
  int8_t (*is_token_cb)(char *text, uint16_t size);
  int8_t (*is_event_cb)(char *text, uint16_t size);

  /*
   * Variables
   */
  unsigned char *(*get_token_val_cb)(struct rules_t *obj, uint16_t token);
  int8_t (*clr_token_val_cb)(struct rules_t *obj, uint16_t token);
  int8_t (*set_token_val_cb)(struct rules_t *obj, uint16_t token, uint16_t val);
  void (*prt_token_val_cb)(struct rules_t *obj, char *out, uint16_t size);

  /*
   * Events
   */
  int8_t (*event_cb)(struct rules_t *obj, char *name);
} rule_options_t;

extern struct rule_options_t rule_options;

/*
 * Each position field is the closest
 * aligned width of 11 bits.
 */
#define VM_GENERIC_FIELDS \
  uint8_t type; \
  uint16_t ret;

typedef struct vm_vchar_t {
  VM_GENERIC_FIELDS
  char value[];
} __attribute__((aligned(4))) vm_vchar_t;

typedef struct vm_vnull_t {
  VM_GENERIC_FIELDS
} __attribute__((aligned(4))) vm_vnull_t;

typedef struct vm_tvalue_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint8_t token[];
} __attribute__((aligned(4))) vm_tvalue_t;

typedef struct vm_vinteger_t {
  VM_GENERIC_FIELDS
  int32_t value;
} __attribute__((aligned(4))) vm_vinteger_t;

typedef struct vm_vfloat_t {
  VM_GENERIC_FIELDS
  uint32_t value;
} __attribute__((aligned(4))) vm_vfloat_t;

typedef struct vm_tgeneric_t {
  VM_GENERIC_FIELDS
} __attribute__((aligned(4))) vm_tgeneric_t;

typedef struct vm_tstart_t {
  VM_GENERIC_FIELDS
  uint16_t go;
} __attribute__((aligned(4))) vm_tstart_t;

typedef struct vm_tif_t {
  VM_GENERIC_FIELDS
  uint8_t sync;
  uint16_t go;
  uint16_t true_;
  uint16_t false_;
} __attribute__((aligned(4))) vm_tif_t;

typedef struct vm_lparen_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint16_t value;
} __attribute__((aligned(4))) vm_lparen_t;

typedef struct vm_tnumber_t {
  VM_GENERIC_FIELDS
  uint8_t token[];
} __attribute__((aligned(4))) __vm_tnumber_t;

typedef struct vm_ttrue_t {
  VM_GENERIC_FIELDS
  uint8_t nrgo;
  uint16_t go[];
} __attribute__((aligned(4))) vm_ttrue_t;

typedef struct vm_tfunction_t {
  VM_GENERIC_FIELDS
  uint16_t value;
  uint8_t token;
  uint8_t nrgo;
  uint16_t go[];
} __attribute__((aligned(4))) vm_tfunction_t;

typedef struct vm_tvar_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint16_t value;
} __attribute__((aligned(4))) vm_tvar_t;

typedef struct vm_tevent_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint8_t token[];
} __attribute__((aligned(4))) vm_tevent_t;

typedef struct vm_tcevent_t {
  VM_GENERIC_FIELDS
  uint8_t token[];
} __attribute__((aligned(4))) vm_tcevent_t;

typedef struct vm_toperator_t {
  VM_GENERIC_FIELDS
  uint8_t token;
  uint16_t left;
  uint16_t right;
  uint16_t value;
} __attribute__((aligned(4))) vm_toperator_t;

typedef struct vm_teof_t {
  uint8_t type;
} __attribute__((aligned(4))) vm_teof_t;

int8_t rule_token(struct rule_stack_t *obj, uint16_t pos, unsigned char **out);
char *rule_by_nr(struct rules_t **rules, uint8_t nrrules, uint8_t nr);
int8_t rule_by_name(struct rules_t **rules, uint8_t nrrules, char *name);
int8_t rule_initialize(struct pbuf *input, struct rules_t ***rules, uint8_t *nrrules, struct pbuf *mempool, void *userdata);
int8_t rule_max_var_bytes(void);
int8_t rules_loop(struct rules_t **rules, uint8_t nrrules, uint8_t *nr);
void vm_clear_values(struct rules_t *obj);
void valprint(struct rules_t *obj, char *out, uint16_t size);

int8_t rule_call(uint8_t nr);
#endif
