/* #40 hot path. No malloc. Compiled as C17 and again as C++20.
   Stays in this translation unit so the driver cannot fold the work away.
   Do not build this file with link-time optimization. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <avrt.h>
#include <stdio.h>
#include <string.h>
#include "hot.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static long long g_freq;
static unsigned g_timer_error;

void hot_init(void) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
}

static long long qpc_now(void) {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

static double qpc_ms(long long a, long long b) {
    return (double)(b - a) * 1000.0 / (double)g_freq;
}

uint64_t interp_run(const Step *steps, uint32_t nsteps, uint32_t budget) {
    typedef struct Frame {
        uint32_t base;
        uint32_t index;
        uint32_t end;
        uint32_t repeats_left;
    } Frame;
    Frame stack[8];
    int sp = 0;
    uint8_t held[64];
    uint64_t clock = 0;
    uint64_t acc = 0;
    uint32_t done = 0;
    uint32_t i;

    if (!steps || nsteps == 0 || budget == 0)
        return 0;
    memset(held, 0, sizeof held);
    stack[0].base = 0;
    stack[0].index = 0;
    stack[0].end = nsteps;
    stack[0].repeats_left = 1;

    while (done < budget) {
        Frame *f = &stack[sp];
        const Step *s;
        if (f->index >= f->end) {
            if (f->repeats_left > 1) {
                f->repeats_left--;
                f->index = f->base;
                continue;
            }
            if (sp > 0) {
                sp--;
                continue;
            }
            f->index = f->base;
            f->repeats_left = 1;
            continue;
        }
        s = &steps[f->index];
        f->index++;
        done++;
        switch (s->op) {
        case OP_DOWN:
            if (s->key < 64)
                held[s->key] = 1;
            acc += s->key;
            break;
        case OP_UP:
            if (s->key < 64)
                held[s->key] = 0;
            acc += 1;
            break;
        case OP_WAIT:
            clock += s->a;
            acc += s->a;
            break;
        case OP_REPEAT:
            if (sp + 1 < 8 && s->b > 0) {
                Frame *c;
                uint32_t start = f->index;
                sp++;
                c = &stack[sp];
                c->base = start;
                c->end = start + s->b;
                if (c->end > nsteps)
                    c->end = nsteps;
                c->index = c->base;
                c->repeats_left = s->a ? s->a : 1;
                f->index = c->end;
            }
            break;
        default:
            acc += 1;
            break;
        }
    }

    acc ^= clock << 1;
    for (i = 0; i < 64; i++)
        acc += held[i];
    return acc;
}

void hash_build(HashTable *t, const uint16_t *keys, const uint32_t *ids, uint32_t n) {
    uint32_t i;
    memset(t, 0, sizeof *t);
    for (i = 0; i < n; i++) {
        uint32_t slot;
        uint32_t probes = 0;
        if (keys[i] == 0)
            continue;
        slot = (keys[i] * 0x9E3779B1u) & (HASH_CAP - 1);
        while (t->slots[slot].key != 0 && probes < HASH_CAP) {
            slot = (slot + 1) & (HASH_CAP - 1);
            probes++;
        }
        t->slots[slot].key = keys[i];
        t->slots[slot].id = ids[i];
    }
}

uint64_t lookup_array(const uint32_t *table, const uint16_t *queries, uint32_t nq, uint32_t rounds) {
    uint64_t acc = 0;
    uint32_t r, i;
    for (r = 0; r < rounds; r++) {
        for (i = 0; i < nq; i++)
            acc += table[queries[i] & 255u];
    }
    return acc;
}

uint64_t lookup_hash(const HashTable *t, const uint16_t *queries, uint32_t nq, uint32_t rounds) {
    uint64_t acc = 0;
    uint32_t r, i;
    for (r = 0; r < rounds; r++) {
        for (i = 0; i < nq; i++) {
            uint32_t key = queries[i];
            uint32_t slot = (key * 0x9E3779B1u) & (HASH_CAP - 1);
            uint32_t probes = 0;
            uint32_t id = 0;
            while (probes < HASH_CAP) {
                uint32_t k = t->slots[slot].key;
                if (k == 0)
                    break;
                if (k == key) {
                    id = t->slots[slot].id;
                    break;
                }
                slot = (slot + 1) & (HASH_CAP - 1);
                probes++;
            }
            acc += id;
        }
    }
    return acc;
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* Wire image is 48 bytes. HidMsg itself is 40. */
static void encode_bin(const HidMsg *m, uint8_t out[48]) {
    uint32_t i;
    out[0] = 0x48;
    out[1] = 0x49;
    out[2] = 0x44;
    out[3] = 0x31;
    out[4] = m->n_down;
    out[5] = m->n_up;
    out[6] = 0;
    out[7] = 0;
    put_u16(out + 8, (uint16_t)m->x);
    put_u16(out + 10, (uint16_t)m->y);
    put_u16(out + 12, (uint16_t)m->wheel);
    put_u16(out + 14, 0);
    for (i = 0; i < 8; i++) {
        put_u16(out + 16 + i * 2, m->down[i]);
        put_u16(out + 32 + i * 2, m->up[i]);
    }
}

static int decode_bin(const uint8_t in[48], HidMsg *m) {
    uint32_t i;
    if (in[0] != 0x48 || in[1] != 0x49 || in[2] != 0x44 || in[3] != 0x31)
        return 0;
    memset(m, 0, sizeof *m);
    m->n_down = in[4];
    m->n_up = in[5];
    m->x = (int16_t)get_u16(in + 8);
    m->y = (int16_t)get_u16(in + 10);
    m->wheel = (int16_t)get_u16(in + 12);
    for (i = 0; i < 8; i++) {
        m->down[i] = get_u16(in + 16 + i * 2);
        m->up[i] = get_u16(in + 32 + i * 2);
    }
    return 1;
}

uint64_t codec_bin(const HidMsg *msgs, uint32_t n, uint32_t rounds) {
    uint64_t acc = 0;
    uint32_t r, i;
    for (r = 0; r < rounds; r++) {
        for (i = 0; i < n; i++) {
            uint8_t buf[48];
            HidMsg out;
            encode_bin(&msgs[i], buf);
            if (!decode_bin(buf, &out))
                return 0;
            acc += (uint16_t)out.x;
            acc += out.n_down;
            acc += out.down[0];
            acc += out.up[0];
            acc += (uint16_t)out.wheel;
        }
    }
    return acc;
}

uint64_t codec_json(const HidMsg *msgs, uint32_t n, uint32_t rounds) {
    uint64_t acc = 0;
    uint32_t r, i;
    for (r = 0; r < rounds; r++) {
        for (i = 0; i < n; i++) {
            char buf[160];
            int len;
            int k;
            const HidMsg *m = &msgs[i];
            len = snprintf(buf, sizeof buf,
                "{\"v\":1,\"type\":\"hid\",\"x\":%d,\"y\":%d,\"wheel\":%d,\"down\":%u,\"up\":%u}",
                (int)m->x, (int)m->y, (int)m->wheel,
                (unsigned)m->down[0], (unsigned)m->up[0]);
            if (len < 0)
                return 0;
            if (len > (int)sizeof buf)
                len = (int)sizeof buf;
            for (k = 0; k < len; k++)
                acc += (uint8_t)buf[k];
        }
    }
    return acc;
}

static int wait_loop(double *out_ms, uint32_t n, int high_res) {
    HANDLE timer;
    uint32_t i;
    DWORD flags = high_res ? CREATE_WAITABLE_TIMER_HIGH_RESOLUTION : 0;
    if (!out_ms || n == 0 || g_freq == 0)
        return 0;
    g_timer_error = 0;
    timer = CreateWaitableTimerExW(NULL, NULL, flags, TIMER_ALL_ACCESS);
    if (!timer) {
        g_timer_error = GetLastError();
        return 0;
    }
    for (i = 0; i < n; i++) {
        LARGE_INTEGER due;
        long long a, b;
        due.QuadPart = -10000; /* 1 ms in 100 ns units */
        a = qpc_now();
        if (!SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
            g_timer_error = GetLastError();
            CloseHandle(timer);
            return 0;
        }
        if (WaitForSingleObject(timer, 100) != WAIT_OBJECT_0) {
            g_timer_error = GetLastError();
            if (g_timer_error == 0)
                g_timer_error = 1;
            CloseHandle(timer);
            return 0;
        }
        b = qpc_now();
        out_ms[i] = qpc_ms(a, b);
    }
    CloseHandle(timer);
    return 1;
}

int timer_hires(double *out_ms, uint32_t n) {
    return wait_loop(out_ms, n, 1);
}

int timer_hires_mmcss(double *out_ms, uint32_t n) {
    DWORD index = 0;
    HANDLE task;
    int ok;
    g_timer_error = 0;
    task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
    if (!task) {
        g_timer_error = GetLastError();
        return 0;
    }
    ok = wait_loop(out_ms, n, 1);
    AvRevertMmThreadCharacteristics(task);
    return ok;
}

int timer_sleep(double *out_ms, uint32_t n) {
    uint32_t i;
    MMRESULT begin;
    if (!out_ms || n == 0 || g_freq == 0)
        return 0;
    g_timer_error = 0;
    begin = timeBeginPeriod(1);
    if (begin != TIMERR_NOERROR) {
        g_timer_error = (unsigned)begin;
        return 0;
    }
    for (i = 0; i < n; i++) {
        long long a = qpc_now();
        long long b;
        Sleep(1);
        b = qpc_now();
        out_ms[i] = qpc_ms(a, b);
    }
    timeEndPeriod(1);
    return 1;
}

unsigned timer_last_error(void) {
    return g_timer_error;
}
