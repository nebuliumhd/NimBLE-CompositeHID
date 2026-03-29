/**
 * @file nimble_composite_platform.h
 * @brief Cross-platform helpers for NimBLE-CompositeHID.
 *
 * Platform detection:
 *   ESP32  — Arduino ESP32 core; has ESP-IDF logging and std::mutex.
 *   Others — nRF52, RP2040, etc. via NimBLE-Arduino; logging is a no-op and
 *            mutex is a zero-size stub (reports are produced from one task).
 */

#pragma once

// ── Logging ───────────────────────────────────────────────────────────────────
// Use HID_DEFINE_TAG("Name") at file scope in place of a bare `static const char*`.
// On ESP32 it expands to a real string used by ESP_LOGx; elsewhere it is empty
// and LOG_TAG is suppressed so there is no unused-variable warning.
#if defined(ESP32)
#  if defined(CONFIG_ARDUHAL_ESP_LOG)
#    include "esp32-hal-log.h"
#  else
#    include "esp_log.h"
#  endif
#  define HID_DEFINE_TAG(name)     static const char* LOG_TAG = name
#  define HID_LOG_D(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#  define HID_LOG_I(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#  define HID_LOG_W(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#  define HID_LOG_E(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#else
#  define HID_DEFINE_TAG(name)
#  define HID_LOG_D(tag, fmt, ...) ((void)0)
#  define HID_LOG_I(tag, fmt, ...) ((void)0)
#  define HID_LOG_W(tag, fmt, ...) ((void)0)
#  define HID_LOG_E(tag, fmt, ...) ((void)0)
#endif

// ── Mutex ─────────────────────────────────────────────────────────────────────
// On ESP32, BLE callbacks can race with the app task → real mutex needed.
// On nRF52 with n-able (synchronous external-server mode) there is no contention.
#if defined(ESP32)
#  include <mutex>
#  define HID_MUTEX_TYPE    std::mutex
#  define HID_LOCK(mtx)     std::lock_guard<std::mutex> _hid_lg_(mtx)
#else
struct _HidNullMutex {};
#  define HID_MUTEX_TYPE    _HidNullMutex
#  define HID_LOCK(mtx)     ((void)0)
#endif
