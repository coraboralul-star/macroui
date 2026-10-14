#include "report.h"

#include <string.h>

_Static_assert(sizeof(struct engine_report) == ENGINE_REPORT_BYTES, "report image");

static uint16_t load_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void store_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint32_t load_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void store_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static int clamp_count(uint8_t count)
{
    return count > ENGINE_REPORT_CAP ? ENGINE_REPORT_CAP : (int)count;
}

static int32_t clamp_move(int32_t value)
{
    if (value > ENGINE_REPORT_MOVE_LIMIT)
        return ENGINE_REPORT_MOVE_LIMIT;
    if (value < -ENGINE_REPORT_MOVE_LIMIT)
        return -ENGINE_REPORT_MOVE_LIMIT;
    return value;
}

void engine_report_clear(struct engine_report *report)
{
    if (!report)
        return;
    memset(report->bytes, 0, sizeof report->bytes);
}

int engine_report_load(struct engine_report *report, const uint8_t *bytes, int length)
{
    if (!report)
        return 0;
    if (!bytes || length != ENGINE_REPORT_BYTES)
        return 0;
    memcpy(report->bytes, bytes, ENGINE_REPORT_BYTES);
    return 1;
}

uint8_t engine_report_down_count(const struct engine_report *report)
{
    if (!report)
        return 0;
    return (uint8_t)clamp_count(report->bytes[0]);
}

uint8_t engine_report_up_count(const struct engine_report *report)
{
    if (!report)
        return 0;
    return (uint8_t)clamp_count(report->bytes[1]);
}

uint16_t engine_report_flags(const struct engine_report *report)
{
    if (!report)
        return 0;
    return load_u16(report->bytes + 2);
}

uint16_t engine_report_down(const struct engine_report *report, int index)
{
    if (!report || index < 0 || index >= engine_report_down_count(report))
        return 0;
    return load_u16(report->bytes + 4 + (index * 2));
}

uint16_t engine_report_up(const struct engine_report *report, int index)
{
    if (!report || index < 0 || index >= engine_report_up_count(report))
        return 0;
    return load_u16(report->bytes + 68 + (index * 2));
}

int32_t engine_report_dx(const struct engine_report *report)
{
    if (!report)
        return 0;
    return (int32_t)load_u32(report->bytes + 132);
}

int32_t engine_report_dy(const struct engine_report *report)
{
    if (!report)
        return 0;
    return (int32_t)load_u32(report->bytes + 136);
}

int32_t engine_report_wheel(const struct engine_report *report)
{
    if (!report)
        return 0;
    return (int32_t)load_u32(report->bytes + 140);
}

int32_t engine_report_ax(const struct engine_report *report)
{
    if (!report)
        return 0;
    return (int32_t)load_u32(report->bytes + 144);
}

int32_t engine_report_ay(const struct engine_report *report)
{
    if (!report)
        return 0;
    return (int32_t)load_u32(report->bytes + 148);
}

uint32_t engine_report_dpi(const struct engine_report *report)
{
    if (!report)
        return 0;
    return load_u32(report->bytes + 152);
}

static int push_id(struct engine_report *report, int offset, uint16_t id, int up)
{
    uint8_t *count_at;
    int count;

    if (!report || id == 0)
        return 0;
    count_at = up ? &report->bytes[1] : &report->bytes[0];
    count = *count_at;
    if (count >= ENGINE_REPORT_CAP) {
        engine_report_set_overflow(report);
        return 0;
    }
    store_u16(report->bytes + offset + (count * 2), id);
    *count_at = (uint8_t)(count + 1);
    return 1;
}

int engine_report_push_down(struct engine_report *report, uint16_t id)
{
    return push_id(report, 4, id, 0);
}

int engine_report_push_up(struct engine_report *report, uint16_t id)
{
    return push_id(report, 68, id, 1);
}

void engine_report_set_relative(struct engine_report *report, int32_t dx, int32_t dy)
{
    uint16_t flags;

    if (!report)
        return;
    flags = (uint16_t)(engine_report_flags(report) & ~ENGINE_REPORT_ABSOLUTE);
    store_u16(report->bytes + 2, flags);
    store_u32(report->bytes + 132, (uint32_t)clamp_move(dx));
    store_u32(report->bytes + 136, (uint32_t)clamp_move(dy));
}

void engine_report_set_absolute(struct engine_report *report, int32_t ax, int32_t ay)
{
    uint16_t flags;

    if (!report)
        return;
    flags = (uint16_t)(engine_report_flags(report) | ENGINE_REPORT_ABSOLUTE);
    store_u16(report->bytes + 2, flags);
    store_u32(report->bytes + 132, 0);
    store_u32(report->bytes + 136, 0);
    store_u32(report->bytes + 144, (uint32_t)ax);
    store_u32(report->bytes + 148, (uint32_t)ay);
}

void engine_report_set_wheel(struct engine_report *report, int32_t wheel)
{
    if (!report)
        return;
    store_u32(report->bytes + 140, (uint32_t)wheel);
}

void engine_report_set_dpi(struct engine_report *report, uint32_t dpi)
{
    if (!report)
        return;
    store_u32(report->bytes + 152, dpi);
}

void engine_report_set_overflow(struct engine_report *report)
{
    uint16_t flags;

    if (!report)
        return;
    flags = (uint16_t)(engine_report_flags(report) | ENGINE_REPORT_OVERFLOW);
    store_u16(report->bytes + 2, flags);
}

int engine_report_active(const struct engine_report *report)
{
    if (!report)
        return 0;
    if (engine_report_down_count(report) || engine_report_up_count(report))
        return 1;
    if (engine_report_flags(report) & ENGINE_REPORT_ABSOLUTE)
        return 1;
    if (engine_report_dx(report) || engine_report_dy(report) || engine_report_wheel(report))
        return 1;
    return 0;
}
