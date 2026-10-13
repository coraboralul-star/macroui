/* Shared declarations for the #40 language micro-benchmarks.
   The same hot.c is compiled as C17 and as C++20. */
#ifndef LANG_HOT_H
#define LANG_HOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OP_DOWN = 1,
    OP_UP = 2,
    OP_WAIT = 3,
    OP_REPEAT = 4,
    HASH_CAP = 512,
    STEP_N = 9
};

typedef struct Step {
    uint8_t op;
    uint8_t key;
    uint16_t a;
    uint16_t b;
} Step;

typedef struct HidMsg {
    int16_t x;
    int16_t y;
    int16_t wheel;
    uint8_t n_down;
    uint8_t n_up;
    uint16_t down[8];
    uint16_t up[8];
} HidMsg;

typedef struct Slot {
    uint32_t key;
    uint32_t id;
} Slot;

typedef struct HashTable {
    Slot slots[HASH_CAP];
} HashTable;

typedef char step_size_ok[(sizeof(Step) == 6) ? 1 : -1];
typedef char hid_size_ok[(sizeof(HidMsg) == 40) ? 1 : -1];

void hot_init(void);

uint64_t interp_run(const Step *steps, uint32_t nsteps, uint32_t budget);
void hash_build(HashTable *t, const uint16_t *keys, const uint32_t *ids, uint32_t n);
uint64_t lookup_array(const uint32_t *table, const uint16_t *queries, uint32_t nq, uint32_t rounds);
uint64_t lookup_hash(const HashTable *t, const uint16_t *queries, uint32_t nq, uint32_t rounds);
uint64_t codec_bin(const HidMsg *msgs, uint32_t n, uint32_t rounds);
uint64_t codec_json(const HidMsg *msgs, uint32_t n, uint32_t rounds);

int timer_hires(double *out_ms, uint32_t n);
int timer_hires_mmcss(double *out_ms, uint32_t n);
int timer_sleep(double *out_ms, uint32_t n);
unsigned timer_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
