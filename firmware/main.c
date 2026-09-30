// Vendetta USB output board.
//
// This firmware has no idea what a macro is. The engine decides every press,
// release and movement and sends it here one line at a time.
//
// Serial protocol, one command per line:
//   K <hid-usage> <0|1>              key up or down, usage is a HID usage id
//   M <buttons> <dx> <dy> <wheel>    buttons are absolute, movement adds on
//   ?                                replies with the USB product string
//   !                                replies with HID ready state, for bring-up
//
// A line that does not parse is ignored. When the serial port closes, every
// held key and button is released so nothing can stay stuck down.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "usb_descriptors.h"

#define LINE_LIMIT 64

static uint8_t kb_mods;
static uint8_t kb_keys[VENDETTA_KEY_SLOTS];
static bool kb_dirty;

static uint8_t mouse_buttons;
static int32_t mouse_dx;
static int32_t mouse_dy;
static int32_t mouse_wheel;
static bool mouse_dirty;

static char line[LINE_LIMIT];
static size_t line_len;
static bool line_dropped;
static bool cdc_was_up;

static void cdc_reply(char const *text) {
  tud_cdc_write_str(text);
  tud_cdc_write_flush();
}

static bool hid_wait(uint8_t instance, uint32_t ms) {
  absolute_time_t deadline = make_timeout_time_ms(ms);
  while (!tud_hid_n_ready(instance)) {
    tud_task();
    if (time_reached(deadline))
      return false;
  }
  return true;
}

static void fill_boot_keys(uint8_t out[6]) {
  memset(out, 0, 6);
  size_t n = 0;
  for (size_t i = 0; i < VENDETTA_KEY_SLOTS && n < 6; i++) {
    if (kb_keys[i])
      out[n++] = kb_keys[i];
  }
}

// Send this snapshot now, before the next serial line is applied. Otherwise a
// down and an up in the same burst never reach Windows.
static bool send_keyboard_now(void) {
  uint8_t keys[6];
  fill_boot_keys(keys);
  if (!hid_wait(VENDETTA_HID_KEYBOARD, 50))
    return false;
  return tud_hid_n_keyboard_report(VENDETTA_HID_KEYBOARD, 0, kb_mods, keys);
}

static int8_t take_8(int32_t *accumulator) {
  int32_t value = *accumulator;
  if (value > 127)
    value = 127;
  if (value < -127)
    value = -127;
  *accumulator -= value;
  return (int8_t)value;
}

static bool send_mouse_now(void) {
  if (!hid_wait(VENDETTA_HID_MOUSE, 50))
    return false;
  int8_t x = take_8(&mouse_dx);
  int8_t y = take_8(&mouse_dy);
  int8_t wheel = take_8(&mouse_wheel);
  if (!tud_hid_n_mouse_report(VENDETTA_HID_MOUSE, 0, mouse_buttons, x, y, wheel, 0)) {
    mouse_dx += x;
    mouse_dy += y;
    mouse_wheel += wheel;
    return false;
  }
  mouse_dirty = false;
  return true;
}

static void release_all(void) {
  kb_mods = 0;
  memset(kb_keys, 0, sizeof(kb_keys));
  kb_dirty = true;
  mouse_buttons = 0;
  mouse_dx = 0;
  mouse_dy = 0;
  mouse_wheel = 0;
  mouse_dirty = true;
}

static void key_set(uint8_t usage, bool down) {
  if (usage == 0)
    return;

  if (usage >= 0xE0 && usage <= 0xE7) {
    uint8_t bit = (uint8_t)(1u << (usage - 0xE0));
    if (down)
      kb_mods |= bit;
    else
      kb_mods = (uint8_t)(kb_mods & ~bit);
    kb_dirty = true;
    return;
  }

  for (size_t i = 0; i < VENDETTA_KEY_SLOTS; i++) {
    if (kb_keys[i] == usage) {
      if (!down) {
        kb_keys[i] = 0;
        kb_dirty = true;
      }
      return;
    }
  }

  if (!down)
    return;

  for (size_t i = 0; i < VENDETTA_KEY_SLOTS; i++) {
    if (kb_keys[i] == 0) {
      kb_keys[i] = usage;
      kb_dirty = true;
      return;
    }
  }
}

static void handle_line(char const *text) {
  if (text[0] == '?') {
    cdc_reply(VENDETTA_PRODUCT "\n");
    return;
  }

  if (text[0] == '!') {
    char buf[48];
    snprintf(buf, sizeof(buf), "hid %u %u mounted %u\n",
             tud_hid_n_ready(VENDETTA_HID_KEYBOARD) ? 1u : 0u,
             tud_hid_n_ready(VENDETTA_HID_MOUSE) ? 1u : 0u, tud_mounted() ? 1u : 0u);
    cdc_reply(buf);
    return;
  }

  if (text[0] == 'K') {
    int usage = 0;
    int down = 0;
    if (sscanf(text + 1, "%i %i", &usage, &down) != 2)
      return;
    if (usage < 0 || usage > 255)
      return;
    key_set((uint8_t)usage, down != 0);
    cdc_reply(send_keyboard_now() ? "ok\n" : "hid-busy\n");
    kb_dirty = false;
    return;
  }

  if (text[0] == 'M') {
    int buttons = 0;
    int dx = 0;
    int dy = 0;
    int wheel = 0;
    if (sscanf(text + 1, "%i %i %i %i", &buttons, &dx, &dy, &wheel) != 4)
      return;
    mouse_buttons = (uint8_t)(buttons & 0x1F);
    mouse_dx += dx;
    mouse_dy += dy;
    mouse_wheel += wheel;
    mouse_dirty = true;
    cdc_reply(send_mouse_now() ? "ok\n" : "hid-busy\n");
    return;
  }
}

static void pump_cdc(void) {
  while (tud_cdc_available()) {
    int c = tud_cdc_read_char();
    if (c < 0)
      break;

    if (c == '\r')
      continue;

    if (c == '\n') {
      if (!line_dropped && line_len > 0) {
        line[line_len] = 0;
        handle_line(line);
      }
      line_len = 0;
      line_dropped = false;
      continue;
    }

    if (line_len + 1 >= LINE_LIMIT) {
      line_dropped = true;
      continue;
    }
    line[line_len++] = (char)c;
  }
}

static void send_reports(void) {
  if (kb_dirty && tud_hid_n_ready(VENDETTA_HID_KEYBOARD))
    kb_dirty = !send_keyboard_now();

  bool moved = mouse_dx != 0 || mouse_dy != 0 || mouse_wheel != 0;
  if ((mouse_dirty || moved) && tud_hid_n_ready(VENDETTA_HID_MOUSE))
    send_mouse_now();
}

int main(void) {
  release_all();
  tusb_init();

  while (true) {
    tud_task();

    bool cdc_up = tud_cdc_connected();
    if (cdc_was_up && !cdc_up)
      release_all();
    cdc_was_up = cdc_up;

    pump_cdc();
    send_reports();
  }
}

void tud_umount_cb(void) {
  release_all();
  cdc_was_up = false;
  line_len = 0;
  line_dropped = false;
}

void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  release_all();
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}
