/* Driver for the #40 hot path. Times calls into hot.c. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hot.h"
#include "work.h"

static long long g_freq;
static int g_fail;

static long long qpc_stamp(void) {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db)
        return -1;
    if (da > db)
        return 1;
    return 0;
}

static void stats_of(double *v, int n, double *p50, double *p95, double *p99, double *mx, double *mean) {
    int i;
    double sum = 0;
    *p50 = *p95 = *p99 = *mx = *mean = -1;
    if (n <= 0)
        return;
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    for (i = 0; i < n; i++)
        sum += v[i];
    *mean = sum / (double)n;
    *mx = v[n - 1];
    *p50 = v[(int)((n - 1) * 0.50)];
    *p95 = v[(int)((n - 1) * 0.95)];
    *p99 = v[(int)((n - 1) * 0.99)];
}

static long long private_bytes(void) {
    PROCESS_MEMORY_COUNTERS_EX c;
    memset(&c, 0, sizeof c);
    c.cb = sizeof c;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&c, sizeof c))
        return -1;
    return (long long)c.PrivateUsage;
}

static void emit(const char *bench, const char *unit, unsigned n,
    double p50, double p95, double p99, double mx, double mean,
    unsigned long long checksum, const char *status, const char *note) {
    printf("%s,%s,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%llu,%s,%s\n",
        bench, unit, n, p50, p95, p99, mx, mean, checksum,
        status ? status : "fail", note ? note : "");
    fflush(stdout);
    if (!status || strcmp(status, "ok") != 0)
        g_fail = 1;
}

static int arg_is(int argc, char **argv, const char *s) {
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], s) == 0)
            return 1;
    }
    return 0;
}

typedef uint64_t (*BatchFn)(void);

static const Step *g_steps;
static uint32_t *g_table;
static HashTable *g_hash;
static const uint16_t *g_queries;
static const HidMsg *g_msgs;
static uint32_t g_budget;
static uint32_t g_rounds;
static uint32_t g_codec_rounds;

static uint64_t batch_interp(void) {
    return interp_run(g_steps, STEP_N, g_budget);
}
static uint64_t batch_array(void) {
    return lookup_array(g_table, g_queries, QUERY_N, g_rounds);
}
static uint64_t batch_hash(void) {
    return lookup_hash(g_hash, g_queries, QUERY_N, g_rounds);
}
static uint64_t batch_bin(void) {
    return codec_bin(g_msgs, MSG_N, g_codec_rounds);
}
static uint64_t batch_json(void) {
    return codec_json(g_msgs, MSG_N, g_codec_rounds);
}

static void time_cpu(const char *bench, BatchFn fn, uint32_t batches, uint32_t ops, const char *what) {
    double samples[64];
    uint32_t i;
    uint64_t expect = 0;
    long long heap0, heap1;
    double p50, p95, p99, mx, mean;
    char note[96];
    const char *status = "ok";
    if (batches > 64)
        batches = 64;
    for (i = 0; i < WARMUP; i++)
        expect = fn();
    heap0 = private_bytes();
    for (i = 0; i < batches; i++) {
        long long a, b;
        uint64_t got;
        a = qpc_stamp();
        got = fn();
        b = qpc_stamp();
        if (i == 0)
            expect = got;
        else if (got != expect)
            status = "fail";
        samples[i] = (double)(b - a) * 1e9 / (double)g_freq / (double)ops;
    }
    heap1 = private_bytes();
    stats_of(samples, (int)batches, &p50, &p95, &p99, &mx, &mean);
    if (heap0 < 0 || heap1 < 0)
        snprintf(note, sizeof note, "%s heap=unknown", what);
    else
        snprintf(note, sizeof note, "%s heap=%lld", what, heap1 - heap0);
    emit(bench, "ns", batches, p50, p95, p99, mx, mean, expect, status, note);
}

static void time_timer(const char *bench, int (*fn)(double *, uint32_t), uint32_t n, const char *what) {
    static double samples[4096];
    double p50, p95, p99, mx, mean;
    char note[96];
    if (n > 4096)
        n = 4096;
    if (!fn(samples, n)) {
        snprintf(note, sizeof note, "%s err=%u", what, timer_last_error());
        emit(bench, "ms", 0, -1, -1, -1, -1, -1, 0, "fail", note);
        return;
    }
    stats_of(samples, (int)n, &p50, &p95, &p99, &mx, &mean);
    snprintf(note, sizeof note, "%s", what);
    emit(bench, "ms", n, p50, p95, p99, mx, mean, 0, "ok", note);
}

static void fill_steps(Step *steps) {
    uint32_t i;
    for (i = 0; i < STEP_N; i++) {
        steps[i].op = 0;
        steps[i].key = 0;
        steps[i].a = 0;
        steps[i].b = 0;
    }
    steps[0].op = OP_REPEAT;
    steps[0].a = 3;
    steps[0].b = 4;
    steps[1].op = OP_DOWN;
    steps[1].key = 20;
    steps[2].op = OP_WAIT;
    steps[2].a = 5;
    steps[3].op = OP_UP;
    steps[3].key = 20;
    steps[4].op = OP_WAIT;
    steps[4].a = 2;
    steps[5].op = OP_DOWN;
    steps[5].key = 21;
    steps[6].op = OP_WAIT;
    steps[6].a = 1;
    steps[7].op = OP_UP;
    steps[7].key = 21;
    steps[8].op = OP_WAIT;
    steps[8].a = 2;
}

static void fill_msgs(HidMsg *m, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t k;
        memset(&m[i], 0, sizeof m[i]);
        m[i].x = (int16_t)((int)i * 3 - 20);
        m[i].y = (int16_t)(5 - (int)i);
        m[i].wheel = (int16_t)((i & 1u) ? -1 : 1);
        m[i].n_down = (uint8_t)(i % 4u);
        m[i].n_up = 1;
        for (k = 0; k < 8; k++) {
            m[i].down[k] = (uint16_t)(10u + i + k);
            m[i].up[k] = (uint16_t)(40u + ((i + k) % 7u));
        }
    }
}

static void pin(void) {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}

int main(int argc, char **argv) {
    Step steps[STEP_N];
    uint16_t keys[BIND_N];
    uint32_t ids[BIND_N];
    uint32_t table[256];
    uint16_t queries[QUERY_N];
    HidMsg msgs[MSG_N];
    HashTable table_hash;
    int check = arg_is(argc, argv, "--check");
    int smoke = arg_is(argc, argv, "--smoke");
    int cpu_only = arg_is(argc, argv, "--cpu");
    uint32_t batches = smoke ? 4u : (uint32_t)BATCHES;
    LARGE_INTEGER freq;

    QueryPerformanceFrequency(&freq);
    g_freq = freq.QuadPart;
    hot_init();
    pin();

    fill_steps(steps);
    fill_bindings(keys, ids, BIND_N);
    fill_table(table, keys, ids, BIND_N);
    fill_queries(queries, QUERY_N);
    fill_msgs(msgs, MSG_N);
    hash_build(&table_hash, keys, ids, BIND_N);

    g_steps = steps;
    g_table = table;
    g_hash = &table_hash;
    g_queries = queries;
    g_msgs = msgs;
    g_budget = smoke ? 100000u : (uint32_t)INTERP_BUDGET;
    g_rounds = smoke ? 50u : (uint32_t)LOOKUP_ROUNDS;
    g_codec_rounds = smoke ? 40u : (uint32_t)CODEC_ROUNDS;

    if (check) {
        uint64_t sum_interp = interp_run(steps, STEP_N, INTERP_BUDGET);
        uint64_t sum_array = lookup_array(table, queries, QUERY_N, LOOKUP_ROUNDS);
        uint64_t sum_hash = lookup_hash(&table_hash, queries, QUERY_N, LOOKUP_ROUNDS);
        uint64_t sum_bin = codec_bin(msgs, MSG_N, CODEC_ROUNDS);
        uint64_t sum_json = codec_json(msgs, MSG_N, CODEC_ROUNDS);
        printf("interp %llu\n", (unsigned long long)sum_interp);
        printf("array %llu\n", (unsigned long long)sum_array);
        printf("hash %llu\n", (unsigned long long)sum_hash);
        printf("bin %llu\n", (unsigned long long)sum_bin);
        printf("json %llu\n", (unsigned long long)sum_json);
        if (sum_array != sum_hash || sum_bin == 0 || sum_interp == 0 || sum_json == 0)
            return 1;
        return 0;
    }

    fprintf(stderr, "cpu batches=%u interp_ops=%u lookup_ops=%u codec_ops=%u\n",
        batches, g_budget, QUERY_N * g_rounds, MSG_N * g_codec_rounds);

    {
        char ops[64];
        snprintf(ops, sizeof ops, "ops=%u", g_budget);
        time_cpu("interp", batch_interp, batches, g_budget, ops);
        snprintf(ops, sizeof ops, "ops=%u", QUERY_N * g_rounds);
        time_cpu("lookup-array", batch_array, batches, QUERY_N * g_rounds, ops);
        time_cpu("lookup-hash", batch_hash, batches, QUERY_N * g_rounds, ops);
        snprintf(ops, sizeof ops, "ops=%u", MSG_N * g_codec_rounds);
        time_cpu("codec-bin", batch_bin, batches, MSG_N * g_codec_rounds, ops);
        time_cpu("codec-json", batch_json, batches, MSG_N * g_codec_rounds, ops);
    }

    if (!cpu_only) {
        uint32_t n = smoke ? 40u : (uint32_t)TIMER_N;
        fprintf(stderr, "timer n=%u\n", n);
        /* Sleep stays last. Reverting MMCSS Pro Audio leaves Sleep on the
           15.6 ms tick even when NtQueryTimerResolution still reports 1 ms.
           The isolated Sleep number is in the decision doc, not this row. */
        time_timer("timer-hires", timer_hires, n, "wait=1ms high-res");
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        time_timer("timer-mmcss", timer_hires_mmcss, n, "wait=1ms high-res Pro Audio");
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        time_timer("timer-sleep", timer_sleep, n, "Sleep(1) timeBeginPeriod(1)");
    }
    return g_fail ? 1 : 0;
}
