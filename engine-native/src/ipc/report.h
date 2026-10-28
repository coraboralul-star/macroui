/* The 156-byte report image. Little-endian, no frame header.
   The layout is the report payload in docs/engine/ipc-protocol.md.
   This file does not call SendInput and does not open a port. */
#ifndef ENGINE_REPORT_H
#define ENGINE_REPORT_H

#include <stdint.h>

#define ENGINE_REPORT_BYTES 156
#define ENGINE_REPORT_CAP 32

#define ENGINE_REPORT_ABSOLUTE 0x0001u
#define ENGINE_REPORT_OVERFLOW 0x0002u
/* dx and dy are device counts already. A board does not scale them by dpi.
   A turn step sets it, and the engine scales the rest of that tick's move. */
#define ENGINE_REPORT_COUNTS 0x0004u

#define ENGINE_REPORT_MOVE_LIMIT 16000

struct engine_report {
    uint8_t bytes[ENGINE_REPORT_BYTES];
};

void engine_report_clear(struct engine_report *report);

/* 0 when length is not 156. */
int engine_report_load(struct engine_report *report, const uint8_t *bytes, int length);

uint8_t engine_report_down_count(const struct engine_report *report);
uint8_t engine_report_up_count(const struct engine_report *report);
uint16_t engine_report_flags(const struct engine_report *report);
uint16_t engine_report_down(const struct engine_report *report, int index);
uint16_t engine_report_up(const struct engine_report *report, int index);
int32_t engine_report_dx(const struct engine_report *report);
int32_t engine_report_dy(const struct engine_report *report);
int32_t engine_report_wheel(const struct engine_report *report);
int32_t engine_report_ax(const struct engine_report *report);
int32_t engine_report_ay(const struct engine_report *report);
uint32_t engine_report_dpi(const struct engine_report *report);

/* 1 when the id was stored. 0 is skipped and does not set overflow.
   A full list sets the overflow bit and drops the new id. */
int engine_report_push_down(struct engine_report *report, uint16_t id);
int engine_report_push_up(struct engine_report *report, uint16_t id);

/* Relative move. Clears the absolute bit. Clamps each axis to -16000..16000. */
void engine_report_set_relative(struct engine_report *report, int32_t dx, int32_t dy);

/* Absolute point. Sets the absolute bit and writes dx and dy as 0. */
void engine_report_set_absolute(struct engine_report *report, int32_t ax, int32_t ay);

void engine_report_set_wheel(struct engine_report *report, int32_t wheel);
void engine_report_set_dpi(struct engine_report *report, uint32_t dpi);
void engine_report_set_overflow(struct engine_report *report);
void engine_report_set_counts(struct engine_report *report);

/* A down, an up, a move, a wheel, or the absolute flag.
   Dpi alone is not a send. The overflow bit alone is not a send. */
int engine_report_active(const struct engine_report *report);

#endif
