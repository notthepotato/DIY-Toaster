// ============================================================================
//  ProtoDeck - user configuration
// ============================================================================
#pragma once

// ---- Board revision ---------------------------------------------------------
// Printed on the back of the CrowPanel Advance 7.0 PCB.
//   10 = V1.0  (backlight on a TCA9534/PCA9557 IO expander @0x18; auto-detected)
//   12 = V1.2  (STC8 helper MCU @0x30, coarse brightness)
//   13 = V1.3 / V1.4 / V1.5 (STC8 @0x30, 0-244 brightness + buzzer)
#ifndef BOARD_REV
#define BOARD_REV 13
#endif

// ---- Display ----------------------------------------------------------------
// 16 MHz is what Elecrow ships. If the picture shifts/jitters while the radio
// is busy, drop to 14 or 12 MHz.
#define LCD_PCLK_HZ        (16 * 1000 * 1000)
#define LCD_BOUNCE_LINES   10          // bounce buffer height (DMA from SRAM)
#define LVGL_BUF_LINES     24          // two partial draw buffers in SRAM (2 x 37.5 KB)

// ---- Identity ---------------------------------------------------------------
#define DECK_NAME          "PROTO//DECK"
#define DECK_VERSION       "1.0.0"

// ---- ProtoLink radio --------------------------------------------------------
// Must match PL_NET_ID on your nodes (see protolink.h) - a filter, not crypto.
// #define PL_NET_ID 0x0F0F
#define LINK_HOME_CHANNEL   1         // ProtoESP's soft-AP defaults to channel 1
#define LINK_SCAN_DWELL_MS  110       // time spent listening on each channel
#define LINK_TIMEOUT_MS     450       // request -> no answer -> retry once
#define LINK_OFFLINE_MS     12000     // node shown as offline after this silence
#define LINK_MAX_PEERS      16

// ---- UI ---------------------------------------------------------------------
#define UI_DEFAULT_BACKLIGHT 85       // percent
#define UI_TELEMETRY_FAST_MS 250      // poll rate while the POWER page is open
#define UI_TELEMETRY_SLOW_MS 1500     // poll rate otherwise (selected node only)
#define UI_CHART_POINTS      120
