#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#include "usb_descriptors.h"

#ifndef VENDETTA_PRODUCT
#define VENDETTA_PRODUCT "Vendetta RP2040"
#endif

#define VENDETTA_VID 0xCafe
#define VENDETTA_PID 0x4004

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = VENDETTA_VID,
    .idProduct = VENDETTA_PID,
    .bcdDevice = 0x0104,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

// Two boot interfaces. Windows only turns HID reports into real keystrokes
// when the keyboard interface uses the boot subclass. A combined keyboard+mouse
// interface with report IDs enumerates as generic HID and the keys never arrive.
static uint8_t const desc_hid_keyboard[] = {TUD_HID_REPORT_DESC_KEYBOARD()};
static uint8_t const desc_hid_mouse[] = {TUD_HID_REPORT_DESC_MOUSE()};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  if (instance == VENDETTA_HID_MOUSE)
    return desc_hid_mouse;
  return desc_hid_keyboard;
}

enum {
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_HID_KEYBOARD,
  ITF_NUM_HID_MOUSE,
  ITF_NUM_TOTAL,
};

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82
#define EPNUM_HID_KEYBOARD 0x83
#define EPNUM_HID_MOUSE 0x84

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + 2 * TUD_HID_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_KEYBOARD, 5, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_keyboard),
                       EPNUM_HID_KEYBOARD, 8, 1),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_MOUSE, 6, HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_mouse),
                       EPNUM_HID_MOUSE, 8, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

static char serial_string[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static char const *const string_table[] = {
    NULL,
    "Vendetta",
    VENDETTA_PRODUCT,
    serial_string,
    "Vendetta Control",
    "Vendetta Keyboard",
    "Vendetta Mouse",
};

static uint16_t string_buffer[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;

  size_t count;

  if (index == 0) {
    string_buffer[1] = 0x0409;
    count = 1;
  } else {
    if (index >= TU_ARRAY_SIZE(string_table))
      return NULL;

    if (index == 3 && serial_string[0] == 0)
      pico_get_unique_board_id_string(serial_string, sizeof(serial_string));

    char const *text = string_table[index];
    if (text == NULL)
      return NULL;

    count = strlen(text);
    if (count > TU_ARRAY_SIZE(string_buffer) - 1)
      count = TU_ARRAY_SIZE(string_buffer) - 1;

    for (size_t i = 0; i < count; i++)
      string_buffer[1 + i] = (uint16_t)text[i];
  }

  string_buffer[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
  return string_buffer;
}
