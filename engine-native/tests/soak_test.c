#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

/* Trigger soak on a real profile. fixtures/soak.txt is the script, read by
   this test and by the AHK --self-test. Each tick runs in playback_tick's
   order: pick the config, reap and reseed on a switch, seed, drain the
   fingers, poll, tick, then the owed presses.

   Per press of a live trigger, by mode:
   - Run Once (once, no hold block): one start on the down, none while a pass
     plays, and holding never repeats it.
   - once with a hold block: one start on the down.
   - whileHeld: a runner plays the whole hold, from 3 ms after the down to
     the up, and it is never stopped and started again inside one hold.
   A focus switch is not checked inside the hold it cuts.

   The summary per scenario and output must equal fixtures/soak-expected.txt.
   --write prints it instead. */

#include "command.h"
#include "profile.h"
#include "runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EV_CAP 512
#define MACRO_CAP ENGINE_TRIGGER_MEM
#define SUM_CAP (64 * 1024)

#ifndef SOAK_DIR
#define SOAK_DIR "tests/fixtures"
#endif

enum { EV_DOWN, EV_UP, EV_PUSH, EV_FOCUS, EV_REPEAT };

struct ev {
    int64_t at;
    int kind;
    uint16_t key;
    char exe[64];
};

struct scen {
    char name[64];
    char front[64];
    int64_t end;
    struct ev ev[EV_CAP];
    int count;
};

/* One macro of the live config, by id, for the whole run. */
struct track {
    char id[ENGINE_ID_CAP];
    char name[64];
    uint16_t key;
    int kind;
    int starts;
    int held_press;
    int blocked;
    int stops_held;
    int64_t press_at;
    int in_press;
    int press_ok;
    int press_starts;
    int press_stops;
    int playing_at_down;
    int64_t gap_ms;
};

enum { K_RUN_ONCE, K_ONCE_HOLD, K_HELD, K_OTHER };

static int fails;
static char summary[SUM_CAP];
static size_t sum_len;

static void fail(const char *scen, const char *out, const char *what, const char *name, int64_t at)
{
    fprintf(stderr, "FAIL %s %s %s: %s at %lld ms\n", scen, out, name, what, (long long)at);
    fails++;
}

static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long len;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    *n = fread(buf, 1, (size_t)len, f);
    buf[*n] = 0;
    fclose(f);
    return buf;
}

static int add(struct scen *s, int64_t at, int kind, uint16_t key, const char *exe)
{
    struct ev *e;

    if (s->count >= EV_CAP)
        return 0;
    e = &s->ev[s->count++];
    memset(e, 0, sizeof *e);
    e->at = at;
    e->kind = kind;
    e->key = key;
    if (exe)
        snprintf(e->exe, sizeof e->exe, "%s", exe);
    return 1;
}

static void hold(struct scen *s, uint16_t key, int64_t at, int64_t len, int repeat)
{
    int64_t t;

    add(s, at, EV_DOWN, key, NULL);
    if (repeat) {
        for (t = at + 500; t < at + len; t += 33)
            add(s, t, EV_REPEAT, key, NULL);
    }
    add(s, at + len, EV_UP, key, NULL);
}

/* Events at one ms keep file order. A stable sort by time. */
static void order(struct scen *s)
{
    int i;
    int j;

    for (i = 1; i < s->count; i++) {
        struct ev e = s->ev[i];

        for (j = i - 1; j >= 0 && s->ev[j].at > e.at; j--)
            s->ev[j + 1] = s->ev[j];
        s->ev[j + 1] = e;
    }
}

/* Returns the number of scenarios, or -1 on a bad line. */
static int parse(char *text, struct scen *out, int cap)
{
    int n = -1;
    char *line = strtok(text, "\r\n");

    for (; line; line = strtok(NULL, "\r\n")) {
        char word[32];
        char a[64];
        char b[64];
        long long x = 0;
        long long y = 0;
        long long z = 0;
        long long w = 0;
        uint16_t key = 0;
        struct scen *s;

        while (*line == ' ')
            line++;
        if (!*line || *line == '#')
            continue;
        if (sscanf(line, "%31s", word) != 1)
            continue;
        if (strcmp(word, "scenario") == 0) {
            if (n + 1 >= cap || sscanf(line, "%*s %63s %63s", a, b) != 2)
                return -1;
            n++;
            memset(&out[n], 0, sizeof out[n]);
            snprintf(out[n].name, sizeof out[n].name, "%s", a);
            snprintf(out[n].front, sizeof out[n].front, "%s", b);
            continue;
        }
        if (n < 0)
            return -1;
        s = &out[n];
        if (strcmp(word, "end") == 0) {
            if (sscanf(line, "%*s %lld", &x) != 1)
                return -1;
            s->end = x;
            order(s);
            continue;
        }
        if (strcmp(word, "push") == 0) {
            if (sscanf(line, "%*s %lld", &x) != 1)
                return -1;
            add(s, x, EV_PUSH, 0, NULL);
            continue;
        }
        if (strcmp(word, "focus") == 0) {
            if (sscanf(line, "%*s %lld %63s", &x, a) != 2)
                return -1;
            add(s, x, EV_FOCUS, 0, a);
            continue;
        }
        if (sscanf(line, "%*s %63s", a) != 1 || !engine_key_id(a, &key) || key == 0) {
            fprintf(stderr, "soak: unknown key in [%s]\n", line);
            return -1;
        }
        if (strcmp(word, "down") == 0 || strcmp(word, "up") == 0) {
            if (sscanf(line, "%*s %*s %lld", &x) != 1)
                return -1;
            add(s, x, word[0] == 'd' ? EV_DOWN : EV_UP, key, NULL);
        } else if (strcmp(word, "hold") == 0 || strcmp(word, "holdr") == 0) {
            if (sscanf(line, "%*s %*s %lld %lld", &x, &y) != 2)
                return -1;
            hold(s, key, x, y, word[4] == 'r');
        } else if (strcmp(word, "taps") == 0) {
            long long i;

            if (sscanf(line, "%*s %*s %lld %lld %lld %lld", &x, &y, &z, &w) != 4)
                return -1;
            for (i = 0; i < w; i++)
                hold(s, key, x + i * (y + z), y, 0);
        } else {
            return -1;
        }
    }
    return n + 1;
}

static int hold_block(const struct engine_macro *m)
{
    uint32_t n = m->top_count ? m->top_count : m->step_count;
    uint32_t i;

    for (i = 0; i < n && i < m->step_count; i++) {
        uint8_t t = m->steps[i].type;

        if (t == ENGINE_STEP_HOLD_LOOP || t == ENGINE_STEP_SWAP_LOOP ||
            t == ENGINE_STEP_ONCE_HELD || t == ENGINE_STEP_EDGE_HOLD)
            return 1;
    }
    return 0;
}

static int kind_of(const struct engine_macro *m)
{
    if (m->mode == ENGINE_MODE_ONCE)
        return hold_block(m) ? K_ONCE_HOLD : K_RUN_ONCE;
    if (m->mode == ENGINE_MODE_WHILE_HELD)
        return K_HELD;
    return K_OTHER;
}

static struct track *track_of(struct track *t, int *count, const struct engine_macro *m,
                              const struct engine_profile_extra *x)
{
    int i;

    for (i = 0; i < *count; i++) {
        if (strcmp(t[i].id, m->id) == 0)
            return &t[i];
    }
    if (*count >= MACRO_CAP * 3)
        return NULL;
    memset(&t[*count], 0, sizeof t[*count]);
    snprintf(t[*count].id, sizeof t[*count].id, "%s", m->id);
    snprintf(t[*count].name, sizeof t[*count].name, "%s", x ? x->name : m->id);
    t[*count].key = m->trigger;
    t[*count].kind = kind_of(m);
    return &t[(*count)++];
}

static struct track *find_track(struct track *t, int count, const char *id)
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(t[i].id, id) == 0)
            return &t[i];
    }
    return NULL;
}

/* The checks at the end of one press. */
static void close_press(const struct scen *s, const char *out, struct track *t, int64_t at)
{
    if (!t->press_ok)
        return;
    if (t->kind == K_RUN_ONCE) {
        int want = t->playing_at_down ? 0 : 1;

        if (t->press_starts != want)
            fail(s->name, out, want ? "a press did not play once" : "a press while playing started it",
                 t->name, t->press_at);
    } else if (t->kind == K_ONCE_HOLD) {
        if (t->press_starts != 1)
            fail(s->name, out, "a press did not start exactly once", t->name, t->press_at);
    } else if (t->kind == K_HELD) {
        if (t->press_starts > 1)
            fail(s->name, out, "stopped and started again inside one hold", t->name, t->press_at);
        if (t->gap_ms > 0)
            fail(s->name, out, "not playing for part of the hold", t->name, at);
    }
}

static void sum(const char *fmt, const char *a, const char *b, const char *c, int w, int x,
                int y, int z)
{
    int n;

    if (sum_len >= SUM_CAP)
        return;
    n = snprintf(summary + sum_len, SUM_CAP - sum_len, fmt, a, b, c, w, x, y, z);
    if (n > 0)
        sum_len += (size_t)n;
}

static void run(const char *text, size_t length, const struct scen *s, int output,
                const char *out_name)
{
    struct engine_profile_image *image = NULL;
    struct engine_live_config live;
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    struct engine_finger_lost lost;
    struct track tracks[MACRO_CAP * 3];
    int track_count = 0;
    char live_id[ENGINE_ID_CAP] = "";
    char front[64];
    uint64_t seq = 1;
    uint64_t seen_seq = 0;
    int next = 0;
    int64_t now;
    int i;

    if (!play || engine_profile_compile(text, length, &image) != 0 || !image) {
        fail(s->name, out_name, "profile did not compile", "-", 0);
        free(play);
        return;
    }
    snprintf(front, sizeof front, "%s", s->front);
    engine_play_init(play);
    play->output.mode = output;
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    memset(&lost, 0, sizeof lost);
    for (now = 0; now <= s->end; now++) {
        int focus_moved = 0;

        while (next < s->count && s->ev[next].at == now && s->ev[next].kind >= EV_PUSH &&
               s->ev[next].kind != EV_REPEAT) {
            if (s->ev[next].kind == EV_PUSH) {
                struct engine_profile_image *fresh = NULL;

                if (engine_profile_compile(text, length, &fresh) == 0 && fresh) {
                    /* Runners copy their steps, so the old image can go once
                       the live config points at the new one, below. */
                    engine_profile_image_free(image);
                    image = fresh;
                    seq++;
                }
            } else {
                snprintf(front, sizeof front, "%s", s->ev[next].exe);
                focus_moved = 1;
            }
            next++;
        }
        memset(&live, 0, sizeof live);
        engine_profile_pick(image, front, image->active_id, &live);
        engine_play_set_macros(play, live.macros, live.macro_count);
        if (seq != seen_seq) {
            seen_seq = seq;
            engine_reap_orphans(play, &live);
        }
        engine_trigger_note_profile(&mem, seq);
        if (strcmp(live_id, live.live_id) != 0) {
            snprintf(live_id, sizeof live_id, "%s", live.live_id);
            engine_reap_orphans(play, &live);
            engine_trigger_reseed(&mem);
        }
        engine_trigger_seed_due(&mem, &live, &fingers);
        for (i = 0; i < live.macro_count; i++)
            track_of(tracks, &track_count, &live.macros[i], live.extras ? &live.extras[i] : NULL);
        /* A focus switch voids the checks of every press it cuts. */
        if (focus_moved) {
            for (i = 0; i < track_count; i++)
                tracks[i].press_ok = 0;
        }
        while (next < s->count && s->ev[next].at == now) {
            const struct ev *e = &s->ev[next++];

            if (e->kind == EV_PUSH || e->kind == EV_FOCUS)
                continue;
            engine_finger_note(&fingers, e->key, e->kind != EV_UP, &lost);
            for (i = 0; i < track_count; i++) {
                struct track *t = &tracks[i];
                int in_live = 0;
                int m;

                if (t->key != e->key || e->kind == EV_REPEAT)
                    continue;
                for (m = 0; m < live.macro_count; m++) {
                    if (strcmp(live.macros[m].id, t->id) == 0)
                        in_live = 1;
                }
                if (e->kind == EV_DOWN) {
                    t->in_press = 1;
                    t->press_ok = in_live;
                    t->press_at = now;
                    t->press_starts = 0;
                    t->press_stops = 0;
                    t->gap_ms = 0;
                    t->playing_at_down = engine_play_has_runner(play, t->id);
                } else {
                    close_press(s, out_name, t, now);
                    t->in_press = 0;
                    t->press_ok = 0;
                }
            }
        }
        engine_play_set_fingers(play, &fingers);
        engine_poll_triggers(play, &live, &mem, &fingers, front, "");
        engine_play_tick(play, now);
        engine_trigger_owed(play, &live, &mem, &fingers, front, "");
        for (i = 0; i < play->note_count; i++) {
            const struct engine_note *note = &play->notes[i];
            struct track *t = find_track(tracks, track_count, note->id);

            if (!t)
                continue;
            if (note->kind == ENGINE_NOTE_START) {
                t->starts++;
                if (note->why == ENGINE_WHY_HELD_PRESS)
                    t->held_press++;
                if (t->in_press)
                    t->press_starts++;
            } else if (note->kind == ENGINE_NOTE_BLOCK) {
                t->blocked++;
            } else if (note->kind == ENGINE_NOTE_STOP) {
                if (engine_fingers_down(&fingers, t->key))
                    t->stops_held++;
                if (t->in_press)
                    t->press_stops++;
            }
        }
        play->note_count = 0;
        for (i = 0; i < track_count; i++) {
            struct track *t = &tracks[i];

            if (t->kind == K_HELD && t->in_press && t->press_ok && now >= t->press_at + 3 &&
                !engine_play_has_runner(play, t->id))
                t->gap_ms++;
        }
    }
    for (i = 0; i < track_count; i++) {
        const struct track *t = &tracks[i];

        if (t->starts || t->blocked)
            sum("%s %s %s starts=%d held-press=%d blocked=%d stops-held=%d\n", s->name, out_name,
                t->name, t->starts, t->held_press, t->blocked, t->stops_held);
    }
    engine_play_stop_all(play);
    engine_profile_image_free(image);
    free(play);
}

int main(int argc, char **argv)
{
    static struct scen scens[32];
    const char *dir = SOAK_DIR;
    char path[512];
    char *profile;
    char *script;
    char *expected;
    size_t profile_len = 0;
    size_t script_len = 0;
    size_t expected_len = 0;
    int write = 0;
    int count;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--write") == 0)
            write = 1;
        else
            dir = argv[i];
    }
    snprintf(path, sizeof path, "%s/soak-profile.json", dir);
    profile = slurp(path, &profile_len);
    snprintf(path, sizeof path, "%s/soak.txt", dir);
    script = slurp(path, &script_len);
    if (!profile || !script) {
        fprintf(stderr, "soak: cannot read %s\n", path);
        return 1;
    }
    count = parse(script, scens, (int)(sizeof scens / sizeof scens[0]));
    if (count <= 0) {
        fprintf(stderr, "soak: bad script\n");
        return 1;
    }
    for (i = 0; i < count; i++) {
        run(profile, profile_len, &scens[i], ENGINE_OUTPUT_SOFTWARE, "software");
        run(profile, profile_len, &scens[i], ENGINE_OUTPUT_RP2350, "board");
    }
    if (write) {
        fputs(summary, stdout);
        return fails ? 1 : 0;
    }
    snprintf(path, sizeof path, "%s/soak-expected.txt", dir);
    expected = slurp(path, &expected_len);
    if (!expected) {
        fprintf(stderr, "soak: cannot read %s\n", path);
        return 1;
    }
    {
        /* CRLF on a Windows checkout. */
        size_t r = 0;
        size_t w = 0;

        for (; r < expected_len; r++) {
            if (expected[r] != '\r')
                expected[w++] = expected[r];
        }
        expected[w] = 0;
    }
    if (strcmp(expected, summary) != 0) {
        fprintf(stderr, "FAIL summary differs from soak-expected.txt. This run:\n%s", summary);
        fails++;
    }
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
