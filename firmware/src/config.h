#pragma once

#include <Arduino.h>

// =============================================================================
// Hardware Pin Definitions (ESP32-C3 SuperMini)
// =============================================================================
// HX711 24-Bit ADC Pins (safe non-strapping GPIOs)
#define HX711_DOUT_PIN          6       // GPIO 6 (Serial Data Out from HX711)
#define HX711_SCK_PIN           7       // GPIO 7 (Serial Clock In to HX711)

// Powerbank Keep-Alive Pulse Pin (optional load pulse circuit)
#define KEEPALIVE_ENABLED       1
#define KEEPALIVE_PIN           10      // GPIO 10 (NPN transistor base via 1 kOhm)

// =============================================================================
// Digital Filtering & Signal Conditioning Parameters
// =============================================================================
// Exponential Moving Average (EMA) smoothing factor: alpha in range (0.0, 1.0]
// Lower value = smoother weight, higher value = faster dynamic response.
// 0.35 provides an optimal balance between fast flow-rate tracking and stability.
#define FILTER_EMA_ALPHA        0.35f

// Noise deadband window (in grams).
// Small oscillations within this window around resting state are clamped
// to prevent single-digit flicker in the hundredths/tenths place.
#define NOISE_THRESHOLD_WINDOW  0.04f

// Flow transient threshold: If weight delta exceeds this value, EMA alpha is
// temporarily boosted to 0.85 for instantaneous tracking of initial espresso drops.
#define DYNAMIC_ALPHA_THRESHOLD 0.20f
#define FILTER_FAST_ALPHA       0.85f

// =============================================================================
// Decent Scale BLE Protocol Definitions (Beanconqueror Compatible)
// =============================================================================
#define BLE_DEVICE_NAME         "Decent Scale"

// Primary Service UUID: 0xFFF0
#define BLE_SERVICE_UUID        "0000FFF0-0000-1000-8000-00805F9B34FB"

// Notification Characteristic UUID: 0xFFF4 (Weight Stream)
#define BLE_CHAR_NOTIFY_UUID    "0000FFF4-0000-1000-8000-00805F9B34FB"

// Write Characteristic UUID: 0xFFF3 (Tare, Display, Control Commands)
#define BLE_CHAR_WRITE_UUID     "0000FFF3-0000-1000-8000-00805F9B34FB"

// Streaming rate: Decent Scale transmits weight at 10 Hz (every 100 ms)
#define BLE_NOTIFY_INTERVAL_MS  100

// Decent Protocol Protocol Identifiers
#define DECENT_HEADER_BYTE      0x03
#define DECENT_TYPE_WEIGHT_STABLE   0xCE
#define DECENT_TYPE_WEIGHT_MOVING   0xCA
#define DECENT_CMD_TARE         0x0F

// Packet Size: 7 Bytes
// [0] Header (0x03)
// [1] Type (0xCE or 0xCA)
// [2] Weight MSB (int16_t, tenths of a gram, grams * 10)
// [3] Weight LSB
// [4] Data / Reserved (0x00)
// [5] Data / Reserved (0x00)
// [6] XOR Checksum: Bytes 0 through 5 XOR'ed
#define DECENT_PACKET_LEN       7

// =============================================================================
// USB Powerbank Keep-Alive & Diagnostics Timing
// =============================================================================
// Periodic pulse interval and duration to prevent powerbank auto-shutoff
#define KEEPALIVE_INTERVAL_MS   5000
#define KEEPALIVE_PULSE_MS      150

// Periodic diagnostics status log interval (every 5000 ms)
#define STATUS_LOG_INTERVAL_MS  5000

// =============================================================================
// Persistent Storage (ESP32 Preferences / NVS)
// =============================================================================
#define NVS_NAMESPACE           "coffeescale"
#define NVS_KEY_CAL_FACTOR      "cal_factor"
#define NVS_KEY_ZERO_OFFSET     "zero_offset"

// Fallback calibration values if NVS is uninitialized
#define DEFAULT_CALIBRATION_FACTOR  420.0f
#define DEFAULT_ZERO_OFFSET         0L
