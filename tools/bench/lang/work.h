/* Dataset built in the driver. The hot functions live in another file
   so this fill stays visible and the timed loop does not. */
#ifndef LANG_WORK_H
#define LANG_WORK_H

#include <string.h>
#include "hot.h"

enum {
    BIND_N = 48,
    QUERY_N = 1024,
    MSG_N = 64,
    LOOKUP_ROUNDS = 500,
    CODEC_ROUNDS = 400,
    INTERP_BUDGET = 1000000,
    BATCHES = 20,
    WARMUP = 2,
    TIMER_N = 1000
};

static void fill_bindings(uint16_t *keys, uint32_t *ids, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        keys[i] = (uint16_t)(1u + i * 5u);
        ids[i] = 1000u + i;
    }
}

static void fill_queries(uint16_t *q, uint32_t n) {
    uint32_t s = 1u;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s = s * 1664525u + 1013904223u;
        q[i] = (uint16_t)(1u + (s % 250u));
    }
}

static void fill_table(uint32_t *table, const uint16_t *keys, const uint32_t *ids, uint32_t n) {
    uint32_t i;
    memset(table, 0, 256u * sizeof(uint32_t));
    for (i = 0; i < n; i++)
        table[keys[i] & 255u] = ids[i];
}

#endif
