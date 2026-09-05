/*
 * PEPitCompat — Arduino.h Linux Compatibility Shim
 * 
 * Provides Arduino core API for the original ESP32 code to compile unmodified.
 * All functions are implemented using standard C++17 and POSIX APIs.
 */

#ifndef ARDUINO_H
#define ARDUINO_H

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <string>
#include <algorithm>
#include <functional>
#include <vector>
#include <thread>
#include <unistd.h>
#include <time.h>
#include <strings.h>  // For strcasecmp (used by serialHandler)
#include <sys/time.h> // For settimeofday (used by gfxHandler)

// ESP-IDF logging macros (log_e, log_v, etc.) — needed by EspLuaEngine
#include "esp_log.h"

// ============================================================
// ESP32 Memory Macros (no-op on Linux — all data in RAM)
// ============================================================

#define PROGMEM             // Empty on Linux (no separate flash memory)
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#define pgm_read_ptr(addr)  (*(const void**)(addr))

// ============================================================
// Adafruit_GFX Font Types: NOT defined here — provided by the
// library's own gfxfont.h when LOAD_GFXFF is enabled.
// The TFT_eSPI include chain (TFT_eSPI.h → gfxfont.h) handles it.
// ============================================================

// ============================================================
// Arduino String Class — full compatibility with ESP32 String API
// ============================================================

class String {
public:
    std::string data;

    // Constructors
    String() = default;
    String(const char* cstr) : data(cstr ? cstr : "") {}
    String(const std::string& s) : data(s) {}
    String(int num);
    String(long num);
    String(size_t num);
    String(uint32_t num);
    String(float num, int decimalPlaces = 2);

    // Comparison
    bool equals(const String& rhs) const { return data == rhs.data; }
    bool equals(const char* cstr) const { return data == std::string(cstr); }
    bool equalsIgnoreCase(const String& rhs) const { return strcasecmp(data.c_str(), rhs.data.c_str()) == 0; }
    bool equalsIgnoreCase(const char* cstr) const { return strcasecmp(data.c_str(), cstr) == 0; }
    bool isEmpty() const { return data.empty(); }
    uint32_t length() const { return data.size(); }

    // Operators
    bool operator==(const String& rhs) const { return data == rhs.data; }
    bool operator==(const char* rhs) const { return data == std::string(rhs); }
    bool operator!=(const String& rhs) const { return data != rhs.data; }
    bool operator!=(const char* rhs) const { return data != std::string(rhs); }
    String operator+(const String& rhs) const { return String(data + rhs.data); }
    String operator+(char c) const { String s; s.data = data + c; return s; }
    String& operator+=(const String& rhs) { data += rhs.data; return *this; }
    String& operator+=(char c) { data += c; return *this; }
    String operator+(const char* rhs) const { return String(data + std::string(rhs)); }

    // Modification
    void concat(const String& s) { data += s.data; }
    void concat(const char* cstr) { data += cstr; }
    void concat(int num);
    void trim();
    void toLowerCase();
    void clear() { data.clear(); }

    // Search
    int32_t indexOf(const String& s, uint32_t from = 0) const {
        size_t pos = data.find(s.data, from);
        return pos == std::string::npos ? -1 : (int32_t)pos;
    }
    int32_t indexOf(char c, uint32_t from = 0) const {
        size_t pos = data.find(c, from);
        return pos == std::string::npos ? -1 : (int32_t)pos;
    }
    int32_t indexOf(const char* cstr, uint32_t from = 0) const {
        size_t pos = data.find(cstr, from);
        return pos == std::string::npos ? -1 : (int32_t)pos;
    }

    // Substring
    String substring(uint32_t start) const {
        return String(data.substr(start));
    }
    String substring(uint32_t start, uint32_t end) const {
        return String(data.substr(start, end - start));
    }

    // Conversion
    int32_t toInt() const { return data.empty() ? 0 : std::stoi(data); }
    uint32_t toUint() const { return data.empty() ? 0 : std::stoul(data); }
    const char* c_str() const { return data.c_str(); }

    // Copy string content to caller-supplied buffer (Arduino API)
    void toCharArray(char* buf, uint32_t len, uint32_t idx = 0) const {
        if (len == 0) return;
        uint32_t copyLen = std::min(len - 1, (uint32_t)data.size() - idx);
        if (idx < data.size()) {
            data.copy(buf, copyLen, idx);
        }
        buf[copyLen] = '\0';
    }

    // Prefix/suffix checks
    bool startsWith(const String& prefix) const {
        return data.rfind(prefix.data, 0) == 0;
    }
    bool startsWith(const char* prefix) const {
        return data.rfind(prefix, 0) == 0;
    }
    bool endsWith(const String& suffix) const {
        if (suffix.data.size() > data.size()) return false;
        return data.compare(data.size() - suffix.data.size(), suffix.data.size(), suffix.data) == 0;
    }
    bool endsWith(const char* suffix) const {
        size_t len = strlen(suffix);
        if (len > data.size()) return false;
        return data.compare(data.size() - len, len, suffix) == 0;
    }

    // Character access
    char getCharAt(uint32_t index) const {
        return index < data.size() ? data[index] : '\0';
    }
    void setCharAt(uint32_t index, char c) {
        if (index < data.size()) data[index] = c;
    }

    // Boolean conversion (for if statements)
    explicit operator bool() const { return !data.empty(); }

    // Implicit conversion to const char* (for passing to C APIs)
    operator const char*() const { return data.c_str(); }
};

// Free function: "literal" + String (Arduino supports this, but const char* has no member operator+)
inline String operator+(const char* lhs, const String& rhs) {
    return String(std::string(lhs) + rhs.data);
}

// ============================================================
// Time Functions
// ============================================================

uint32_t millis();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

/** Configure NTP time. On Linux, sets TZ env var; NTP servers are ignored. */
void configTime(int timezoneOffset, int daylightOffset, ...);

// ============================================================
// Serial Output (redirected to stdout)
// ============================================================

class HardwareSerial {
public:
    void begin(uint32_t baud) {} // No-op, stdout always available
    size_t write(uint8_t c);
    size_t write(const uint8_t* buffer, size_t size);
    size_t print(const String& s);
    size_t print(const char* s);
    size_t print(int num);
    size_t print(uint32_t num);
    size_t print(uint64_t num);
    size_t print(int64_t num);
    size_t print(long long num);
    size_t print(float num, int decimalPlaces = 2);
    size_t println(const String& s);
    size_t println(const char* s);
    size_t println(int num);
    size_t println(uint32_t num);
    size_t println(uint64_t num);
    size_t println(int64_t num);
    size_t println(long long num);
    size_t println(float num, int decimalPlaces = 2);
    size_t println(); // Just newline
    int printf(const char* format, ...);
    int available();
    int read();
    size_t readBytes(char* buffer, size_t length);
    size_t readBytes(uint8_t* buffer, size_t length);
    void flush();
    void setTimeout(uint32_t ms) {} // Stub — not used on Linux
    void setRxBufferSize(int size) {} // Stub
    void setTxBufferSize(int size) {} // Stub
};

extern HardwareSerial Serial;

// ============================================================
// GPIO Functions (stubbed — no real hardware on Linux)
// ============================================================

#define INPUT 0x0
#define OUTPUT 0x1
#define HIGH 1
#define LOW 0

// GPIO pin numbers from pins.h (defined for compatibility)
#define GPIO_NUM_0  0
#define GPIO_NUM_1  1
#define GPIO_NUM_2  2
#define GPIO_NUM_3  3
#define GPIO_NUM_4  4
#define GPIO_NUM_5  5
#define GPIO_NUM_6  6
#define GPIO_NUM_7  7
#define GPIO_NUM_8  8
#define GPIO_NUM_9  9
#define GPIO_NUM_10 10
#define GPIO_NUM_11 11
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_14 14
#define GPIO_NUM_15 15
#define GPIO_NUM_16 16
#define GPIO_NUM_17 17
#define GPIO_NUM_18 18
#define GPIO_NUM_21 21
#define GPIO_NUM_38 38
#define GPIO_NUM_39 39
#define GPIO_NUM_40 40
#define GPIO_NUM_41 41
#define GPIO_NUM_42 42
#define GPIO_NUM_43 43
#define GPIO_NUM_45 45
#define GPIO_NUM_46 46
#define GPIO_NUM_47 47
#define GPIO_NUM_48 48

inline void pinMode(uint8_t pin, uint8_t mode) {}

// Pin state storage: tracks explicit digitalWrite calls.
// Unset pins default to HIGH (pull-up behavior on real hardware).
static inline bool& gpioSet(uint8_t pin) {
    static bool set[50] = {}; // false by default
    return set[pin];
}

static inline uint8_t& gpioVal(uint8_t pin) {
    static uint8_t val[50] = {}; // 0 by default (irrelevant if not set)
    return val[pin];
}

inline void digitalWrite(uint8_t pin, uint8_t val) {
    gpioSet(pin) = true;
    gpioVal(pin) = (val != 0);
}

inline uint8_t digitalRead(uint8_t pin) {
    // Unset pins default to HIGH (pull-up behavior on real hardware).
    return gpioSet(pin) ? gpioVal(pin) : 1;
}
inline int analogRead(uint8_t pin) {
    // BAT_ADC_PIN (5): return value that yields ~4000mV via formula
    //   readBatteryVoltage() = (analogRead * 162505) / 100000
    //   So analogRead = 4000 * 100000 / 162505 ≈ 2461
    if (pin == 5) return 2461; // → ~3999mV
    return 0;
}

// ============================================================
// Math Constants & Helpers (Arduino naming convention)
// ============================================================

#ifndef PI
#define PI 3.14159265358979323846f
#endif

template<typename T, typename U>
inline auto _min(T a, U b) -> decltype(a < b ? a : b) {
    return a < b ? a : b;
}

template<typename T, typename U>
inline auto _max(T a, U b) -> decltype(a > b ? a : b) {
    return a > b ? a : b;
}

// constrain as macro (like Arduino) to handle mixed types without template deduction issues
#define constrain(AMOUNT, LOW, HIGH) \
    ((AMOUNT) < (LOW) ? (LOW) : ((AMOUNT) > (HIGH) ? (HIGH) : (AMOUNT)))

// Arduino base constants for Serial.print() format
#define BIN 2
#define OCT 8
#define DEC 10
#define HEX 16

/**
 * Arduino map() — linear interpolation between two ranges.
 * map(value, fromLow, fromHigh, toLow, toHigh)
 */
template<typename X, typename A, typename B, typename Y, typename C>
inline Y map(X value, A fromLow, A fromHigh, B toLow, C toHigh) {
    return static_cast<Y>((value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow);
}

// Arduino-compatible abs() — ensures float/double work on all GCC versions.
// <cstdlib> defines a bare abs() macro that truncates floats to int (Arduino doesn't do this).
// On GCC 13+ with libstdc++, <cmath> brings std::abs(float/double) into global namespace.
// On older GCC (antiX/Debian 11), it does not — we must provide our own overloads.
#undef abs
#if __GNUC__ < 13 || !defined(__GLIBCXX__)
inline float abs(float x) { return __builtin_fabsf(x); }
inline double abs(double x) { return __builtin_fabs(x); }
#endif

// ============================================================
// Random Numbers
// ============================================================

void randomSeed(uint32_t seed);
long random(long max);
long random(long min, long max);

// ============================================================
// Memory Allocation (ESP32 PSRAM-aware -> standard malloc)
// ============================================================

#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DMA 0
#define MALLOC_CAP_DEFAULT 0

inline void* heap_caps_malloc(size_t size, int caps) { return malloc(size); }
inline void* heap_caps_realloc(void* ptr, size_t size, int caps) { return realloc(ptr, size); }

// ============================================================
// ESP32 Attributes (no-op on Linux)
// ============================================================

#define RTC_DATA_ATTR
#define DRAM_ATTR
#define IRAM_ATTR
#define FLASHMEM

// ============================================================
// ESP-IDF Reset Reason (esp_reset_reason.h)
// ============================================================

typedef enum {
    ESP_RST_UNKNOWN   = 0,
    ESP_RST_POWERON   = 1,
    ESP_RST_EXT       = 2,
    ESP_RST_SW        = 3,
    ESP_RST_PANIC     = 4,
    ESP_RST_INT_WDT   = 5,
    ESP_RST_TASK_WDT  = 6,
    ESP_RST_WDT       = 7,
    ESP_RST_DEEPSLEEP = 8,
    ESP_RST_BROWNOUT  = 9,
    ESP_RST_SDIO      = 10,
    ESP_RST_USB       = 11,
} esp_reset_reason_t;

// ============================================================
// ESP-IDF Sleep API (esp_sleep.h — stubbed)
// ============================================================

typedef int gpio_num_t;

/** Enable external wakeup source (GPIO edge-triggered). Stubbed on Linux. */
inline int esp_sleep_enable_ext0_wakeup(gpio_num_t gpio_num, int level) {
    (void)gpio_num; (void)level; return 0;
}

/** Enable timer wakeup. Stubbed on Linux. */
inline int esp_sleep_enable_timer_wakeup(uint64_t time_us) {
    (void)time_us; return 0;
}

/** Enter deep sleep. On Linux, exits the program. */
void esp_deep_sleep_start();

/** Disable a wakeup source. Stubbed on Linux. */
inline void esp_sleep_disable_wakeup_source(int wakeup_source) {
    (void)wakeup_source;
}

// Power domain retention config for deep sleep (esp_sleep.h)
typedef enum {
    ESP_PD_DOMAIN_CPU = 0,
    ESP_PD_DOMAIN_RTC_PERIPH = 1,
} esp_pd_domain_t;

typedef enum {
    ESP_PD_OPTION_OFF = 0,
    ESP_PD_OPTION_ON = 1,
} esp_pd_option_t;

/** Configure power domain retention during deep sleep. No-op on Linux. */
inline int esp_sleep_pd_config(esp_pd_domain_t domain, esp_pd_option_t option) {
    (void)domain; (void)option; return 0;
}

/** "All wakeup sources" flag for esp_sleep_disable_wakeup_source(). */
#define ESP_SLEEP_WAKEUP_ALL 0xFFFFFFFF

// ============================================================
// ESP-IDF GPIO API (driver/gpio.h — stubbed)
// ============================================================

/** Get GPIO level. Always returns 0 on Linux (no hardware). */
inline uint32_t gpio_get_level(gpio_num_t gpio_num) {
    (void)gpio_num; return 0;
}

/** Enable GPIO hold. No-op on Linux. */
inline void gpio_hold_en(gpio_num_t gpio_num) {
    (void)gpio_num;
}

/** Disable GPIO hold. No-op on Linux. */
inline void gpio_hold_dis(gpio_num_t gpio_num) {
    (void)gpio_num;
}

/** Enable GPIO deep sleep hold. No-op on Linux. */
inline void gpio_deep_sleep_hold_en(void) {}

/** Disable GPIO deep sleep hold. No-op on Linux. */
inline void gpio_deep_sleep_hold_dis(void) {}

// ============================================================
// ESP-IDF System API (esp_system.h — stubbed)
// ============================================================

/** Restart the system. On Linux, exits with code 0. */
void esp_restart();

/** Get reset reason. Always returns ESP_RST_POWERON on Linux. */
inline esp_reset_reason_t esp_reset_reason() {
    return ESP_RST_POWERON;
}

// ============================================================
// Time API (time.h — POSIX getLocalTime)
// ============================================================

/** Get local time. Wraps POSIX localtime_r with optional timezone offset. */
bool getLocalTime(struct tm* timeinfo, uint32_t milliseconds);

/** 1-argument overload for Arduino/ESP32 compatibility (no timezone offset). */
bool getLocalTime(struct tm* timeinfo);

// ============================================================
// Global Hardware Instances (declared here, defined in shim files)
// ============================================================

/** WiFi global instance — defined in compat/WiFi.h */
class WiFiClass;
extern WiFiClass WiFi;

/** SPI global instance — defined in compat/SPI.h */
class SPIClass;
extern SPIClass SPI;

/** NuSerial (Bluetooth UART) global instance — defined in compat/NuSerial.hpp */
class NuSerialClass;
extern NuSerialClass NuSerial;

/** Update (OTA) global instance — defined in compat/Update.h */
class UpdateClass;
extern UpdateClass Update;

// ============================================================
// FreeRTOS Types (must be declared before function signatures)
// ============================================================

typedef int BaseType;
typedef int BaseType_t;
typedef int UBaseType;
typedef int UBaseType_t;

// ============================================================
// FreeRTOS Integration (delegated to thread shim)
// ============================================================

typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;

// FreeRTOS mutex stubs (no real contention on Linux — always succeed)
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void*>(1); }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, uint32_t ticks) { (void)sem; (void)ticks; return 1; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) { (void)sem; return 1; }

void yield();
void vTaskDelay(uint32_t ticks);
void vTaskDelete(TaskHandle_t handle);
TaskHandle_t xTaskGetCurrentTaskHandle();
BaseType_t xTaskCreatePinnedToCore(
    void (*taskFunction)(void*),
    const char* name,
    uint32_t stackSize,
    void* parameter,
    UBaseType_t priority,
    TaskHandle_t* taskHandle,
    BaseType_t coreID
);

// ============================================================
// ESP Class (ESP32 system info — stubbed)
// ============================================================

class ESPClass {
public:
    void restart() { esp_restart(); } // Delegates to esp_restart() → std::exit(0)
    uint32_t getFreeHeap() { return 1024 * 1024; } // Stub: 1 MB free heap
    uint32_t getFreePsram() { return 0; } // Stub: no PSRAM on Linux
};

extern ESPClass ESP;

// ============================================================
// Arduino Framework Macros
// ============================================================

#define F(string) string  // Flash string macro — no flash on Linux
#define PSTR(s) s         // Program string — just regular string on Linux

// ============================================================
// Arduino type aliases (used by xpt2046.h and other libraries)
// Note: uint8_t, int32_t, etc. are already provided by <cstdint> above
// ============================================================

using byte = uint8_t;
using boolean = bool;

// ============================================================
// Arduino C-string conversion helpers (non-standard but widely used)
// ============================================================

/** Convert long to string with given base (2-36). Arduino ltoa() equivalent. */
inline char* ltoa(long value, char* result, int base) {
    char buf[34];
    int idx = 0;
    if (value < 0 && base == 10) {
        result[idx++] = '-';
    }
    unsigned long v = (value < 0 && base == 10) ? -value : static_cast<unsigned long>(value);
    do {
        int r = v % base;
        buf[idx++] = "0123456789abcdef"[r];
    } while (v /= base);
    if (value < 0 && base == 10) {
        result[idx++] = '-';
    }
    for (int i = 0; i < idx; i++) result[i] = buf[idx - 1 - i];
    result[idx] = '\0';
    return result;
}

/** Convert unsigned long to string with given base (2-36). Arduino utoa() equivalent. */
inline char* utoa(unsigned long value, char* result, int base) {
    char buf[34];
    int idx = 0;
    do {
        int r = value % base;
        buf[idx++] = "0123456789abcdef"[r];
    } while (value /= base);
    for (int i = 0; i < idx; i++) result[i] = buf[idx - 1 - i];
    result[idx] = '\0';
    return result;
}

/** Convert int to string with given base (2-36). Arduino itoa() equivalent. */
inline char* itoa(int value, char* result, int base) {
    return ltoa(static_cast<long>(value), result, base);
}

// ============================================================
// std::min / std::max — Arduino code uses bare min()/max() calls
// ============================================================

using std::min;
using std::max;

#endif // ARDUINO_H
