/* C++ standard hash map, measured against lookup_array in hot.c.
   This file is allowed to use exceptions. The hot-path builds are not. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <new>
#include <unordered_map>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "hot.h"
#include "work.h"

static long long g_alloc_calls;
static long long g_alloc_bytes;
static long long g_freq;

template <class T>
struct CountAlloc {
    using value_type = T;
    CountAlloc() noexcept {}
    template <class U>
    CountAlloc(const CountAlloc<U> &) noexcept {}
    T *allocate(size_t n) {
        if (n > (size_t)-1 / sizeof(T))
            throw std::bad_alloc();
        g_alloc_calls++;
        g_alloc_bytes += (long long)(n * sizeof(T));
        return static_cast<T *>(::operator new(n * sizeof(T)));
    }
    void deallocate(T *p, size_t) noexcept {
        ::operator delete(p);
    }
};

template <class T, class U>
bool operator==(const CountAlloc<T> &, const CountAlloc<U> &) noexcept {
    return true;
}
template <class T, class U>
bool operator!=(const CountAlloc<T> &, const CountAlloc<U> &) noexcept {
    return false;
}

typedef std::unordered_map<uint32_t, uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>, CountAlloc<std::pair<const uint32_t, uint32_t>>> Map;

static uint64_t map_lookup(const Map *m, const uint16_t *queries, uint32_t nq, uint32_t rounds) {
    uint64_t acc = 0;
    uint32_t r, i;
    for (r = 0; r < rounds; r++) {
        for (i = 0; i < nq; i++) {
            Map::const_iterator it = m->find(queries[i]);
            if (it != m->end())
                acc += it->second;
        }
    }
    return acc;
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

static int arg_is(int argc, char **argv, const char *s) {
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], s) == 0)
            return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    uint16_t keys[BIND_N];
    uint32_t ids[BIND_N];
    uint32_t table[256];
    uint16_t queries[QUERY_N];
    Map map;
    int check = arg_is(argc, argv, "--check");
    int smoke = arg_is(argc, argv, "--smoke");
    uint32_t rounds = smoke ? 50u : (uint32_t)LOOKUP_ROUNDS;
    uint32_t batches = smoke ? 4u : (uint32_t)BATCHES;
    uint32_t ops = QUERY_N * rounds;
    uint32_t i;
    uint64_t expect;
    LARGE_INTEGER freq;
    double samples[64];
    double p50, p95, p99, mx, mean;
    long long calls;
    const char *status = "ok";
    char note[96];

    QueryPerformanceFrequency(&freq);
    g_freq = freq.QuadPart;
    hot_init();
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    fill_bindings(keys, ids, BIND_N);
    fill_table(table, keys, ids, BIND_N);
    fill_queries(queries, QUERY_N);
    map.reserve(128);
    for (i = 0; i < BIND_N; i++)
        map.emplace(keys[i], ids[i]);

    if (check) {
        uint64_t sum_array = lookup_array(table, queries, QUERY_N, LOOKUP_ROUNDS);
        uint64_t sum_map = map_lookup(&map, queries, QUERY_N, LOOKUP_ROUNDS);
        printf("array %llu\n", (unsigned long long)sum_array);
        printf("map %llu\n", (unsigned long long)sum_map);
        return (sum_array == sum_map && sum_array != 0) ? 0 : 1;
    }

    g_alloc_calls = 0;
    g_alloc_bytes = 0;
    expect = map_lookup(&map, queries, QUERY_N, rounds);
    if (g_alloc_calls != 0)
        status = "fail";
    g_alloc_calls = 0;
    g_alloc_bytes = 0;
    for (i = 0; i < batches; i++) {
        LARGE_INTEGER a, b;
        uint64_t got;
        QueryPerformanceCounter(&a);
        got = map_lookup(&map, queries, QUERY_N, rounds);
        QueryPerformanceCounter(&b);
        if (got != expect)
            status = "fail";
        samples[i] = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)g_freq / (double)ops;
    }
    calls = g_alloc_calls;
    if (calls != 0)
        status = "fail";
    stats_of(samples, (int)batches, &p50, &p95, &p99, &mx, &mean);
    snprintf(note, sizeof note, "ops=%u allocs=%lld bytes=%lld", ops, calls, g_alloc_bytes);
    printf("lookup-unordered,%s,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%llu,%s,%s\n",
        "ns", batches, p50, p95, p99, mx, mean,
        (unsigned long long)expect, status, note);
    return strcmp(status, "ok") == 0 ? 0 : 1;
}
