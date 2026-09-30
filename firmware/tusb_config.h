#pragma once

#ifndef CFG_TUSB_MCU
#error "CFG_TUSB_MCU must come from the Pico SDK tinyusb_device target"
#endif

#define CFG_TUSB_OS OPT_OS_PICO

// Both spellings are set because the SDK ships a range of TinyUSB versions and
// they disagree about which one is current.
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE
#define CFG_TUD_ENABLED 1

#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC 1
#define CFG_TUD_HID 2
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_CDC_RX_BUFSIZE 512
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_EP_BUFSIZE 64

// Must be at least the keyboard report length.
#define CFG_TUD_HID_EP_BUFSIZE 32
