/*
 * PEPitCompat — Arduino Core Implementation
 * 
 * Implements time functions, Serial output, String methods, and random numbers.
 */

#include "Arduino.h"
#include <SDL2/SDL.h>
#include <csetjmp>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cstdint>
#include <sys/select.h>
#include <sys/time.h>

// Global quit flag — set by signal handler or event polling when ESC/quit is detected
volatile int32_t g_requestQuit = 0;

// Jump buffer for escaping tight loops on signal (set from main())
static sigjmp_buf g_quitJumpBuf;
static volatile int32_t g_jumpActive = 0;

// Signal handler for Ctrl+C (SIGINT) and kill (SIGTERM).
// Uses siglongjmp to escape from tight loops in original code that never call delay().
static void quitSignalHandler(int) {
    g_requestQuit = 1;
    if (g_jumpActive) {
        siglongjmp(g_quitJumpBuf, 1);
    }
}

// SIGALRM handler: periodic interrupt for tight loops that never call delay().
// Just sets the quit flag and jumps — the actual event checking happens in
// pollEventsAndCheckQuit (called from delay/vTaskDelay/yield during normal operation).
static void periodicCheckHandler(int) {
    // Check for quit conditions that may have been set by the main loop or other handlers
    if (g_requestQuit) {
        if (g_jumpActive) {
            siglongjmp(g_quitJumpBuf, 4); // Periodic check caught a pending quit
        }
    }
}

// Install signal handlers and set up the jump buffer — call from main()
void initSignalHandlers(sigjmp_buf* jumpBuf) {
    if (jumpBuf) {
        memcpy(&g_quitJumpBuf, jumpBuf, sizeof(sigjmp_buf));
        g_jumpActive = 1;
    }

    // SIGINT (Ctrl+C) and SIGTERM (kill) — immediate quit
    struct sigaction saQuit = {};
    saQuit.sa_handler = quitSignalHandler;
    sigemptyset(&saQuit.sa_mask);
    saQuit.sa_flags = 0;
    sigaction(SIGINT, &saQuit, nullptr);
    sigaction(SIGTERM, &saQuit, nullptr);

    // SIGALRM — periodic check for window close / ESC (fires every 200ms)
    struct sigaction saPeriodic = {};
    saPeriodic.sa_handler = periodicCheckHandler;
    sigemptyset(&saPeriodic.sa_mask);
    saPeriodic.sa_flags = 0;
    sigaction(SIGALRM, &saPeriodic, nullptr);

    // Set up periodic timer (200ms interval)
    struct itimerval tv = {};
    tv.it_value.tv_sec = 0;
    tv.it_value.tv_usec = 200000; // First fire after 200ms
    tv.it_interval.tv_sec = 0;
    tv.it_interval.tv_usec = 200000; // Then every 200ms
    setitimer(ITIMER_REAL, &tv, nullptr);
}

// Disable the jump buffer and stop periodic timer (e.g. after clean exit)
void deinitSignalHandlers() {
    g_jumpActive = 0;
    // Stop periodic timer
    struct itimerval tv = {};
    setitimer(ITIMER_REAL, &tv, nullptr);
}

// ============================================================
// Time Functions
// ============================================================

static std::chrono::steady_clock::time_point bootTime;

uint32_t millis() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - bootTime).count()
    );
}

// Helper: poll SDL events and check for quit conditions (used by delay/vTaskDelay/yield)
static void pollEventsAndCheckQuit(uint32_t timeoutMs) {
    if (g_requestQuit) return; // Already requested quit

    // Use select() for interruptible sleep (signals will wake us)
    if (timeoutMs > 0) {
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        select(0, nullptr, nullptr, nullptr, &tv);
    }

    // Poll SDL events and check for quit conditions
    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
        if (evt.type == SDL_QUIT) {
            g_requestQuit = 1;
            return;
        }
        if (evt.type == SDL_WINDOWEVENT && evt.window.event == SDL_WINDOWEVENT_CLOSE) {
            g_requestQuit = 1;
            return;
        }
    }

    // Check for ESC key press
    const Uint8* keystate = SDL_GetKeyboardState(nullptr);
    if (keystate[SDL_SCANCODE_ESCAPE]) {
        g_requestQuit = 1;
    }
}

void delay(uint32_t ms) {
    pollEventsAndCheckQuit(ms);
}

void delayMicroseconds(uint32_t us) {
    usleep(us / 1000);
}

// ============================================================
// NTP Time Configuration (stubbed — no real NTP on Linux)
// ============================================================

void configTime(int timezoneOffset, int daylightOffset, ...) {
    // On Linux, just set TZ environment variable for local time
    // NTP servers are ignored (getLocalTime uses system clock)
    char tzBuf[16];
    snprintf(tzBuf, sizeof(tzBuf), "UTC%+d", -timezoneOffset / 3600);
    setenv("TZ", tzBuf, 1);
    tzset();
}

// ============================================================
// Serial Output (redirected to stdout)
// ============================================================

size_t HardwareSerial::write(uint8_t c) {
    return fwrite(&c, 1, 1, stdout);
}

size_t HardwareSerial::write(const uint8_t* buffer, size_t size) {
    return fwrite(buffer, 1, size, stdout);
}

size_t HardwareSerial::print(const String& s) {
    return fwrite(s.c_str(), 1, s.length(), stdout);
}

size_t HardwareSerial::print(const char* s) {
    if (!s) return 0;
    size_t len = strlen(s);
    return fwrite(s, 1, len, stdout);
}

size_t HardwareSerial::print(int num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", num);
    return fwrite(buf, 1, strlen(buf), stdout);
}

size_t HardwareSerial::print(uint32_t num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", num);
    return fwrite(buf, 1, strlen(buf), stdout);
}

size_t HardwareSerial::print(uint64_t num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)num);
    return fwrite(buf, 1, strlen(buf), stdout);
}

size_t HardwareSerial::print(int64_t num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)num);
    return fwrite(buf, 1, strlen(buf), stdout);
}

size_t HardwareSerial::print(long long num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", num);
    return fwrite(buf, 1, strlen(buf), stdout);
}

size_t HardwareSerial::print(float num, int decimalPlaces) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", decimalPlaces, num);
    return fwrite(buf, 1, strlen(buf), stdout);
}

size_t HardwareSerial::println(const String& s) {
    size_t n = print(s);
    n += println();
    return n;
}

size_t HardwareSerial::println(const char* s) {
    size_t n = print(s);
    n += println();
    return n;
}

size_t HardwareSerial::println(int num) {
    size_t n = print(num);
    n += println();
    return n;
}

size_t HardwareSerial::println(uint32_t num) {
    size_t n = print(num);
    n += println();
    return n;
}

size_t HardwareSerial::println(uint64_t num) {
    size_t n = print(num);
    n += println();
    return n;
}

size_t HardwareSerial::println(int64_t num) {
    size_t n = print(num);
    n += println();
    return n;
}

size_t HardwareSerial::println(long long num) {
    size_t n = print(num);
    n += println();
    return n;
}

size_t HardwareSerial::println(float num, int decimalPlaces) {
    size_t n = print(num, decimalPlaces);
    n += println();
    return n;
}

size_t HardwareSerial::println() {
    return fwrite("\n", 1, 1, stdout);
}

int HardwareSerial::printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = vfprintf(stdout, format, args);
    va_end(args);
    return result;
}

int HardwareSerial::available() {
    // Check if there's input waiting on stdin (non-blocking)
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval tv = {0, 0}; // Non-blocking select
    return select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv) > 0 ? 1 : 0;
}

int HardwareSerial::read() {
    if (!available()) return -1;
    uint8_t c;
    if (fread(&c, 1, 1, stdin) == 1) return c;
    return -1;
}

size_t HardwareSerial::readBytes(char* buffer, size_t length) {
    size_t bytesRead = 0;
    while (bytesRead < length) {
        if (!available()) break; // No more data, return what we have
        int c = read();
        if (c == -1) break;
        buffer[bytesRead++] = (char)c;
    }
    return bytesRead;
}

size_t HardwareSerial::readBytes(uint8_t* buffer, size_t length) {
    return readBytes(reinterpret_cast<char*>(buffer), length);
}

void HardwareSerial::flush() {
    fflush(stdout);
}

HardwareSerial Serial; // Global instance

// ============================================================
// String Implementation (constructors and methods not in header)
// ============================================================

String::String(int num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", num);
    data = buf;
}

String::String(long num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", num);
    data = buf;
}

String::String(size_t num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%zu", num);
    data = buf;
}

String::String(uint32_t num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", num);
    data = buf;
}

String::String(float num, int decimalPlaces) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", decimalPlaces, num);
    data = buf;
}

void String::concat(int num) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", num);
    data += buf;
}

void String::trim() {
    // Remove leading whitespace
    size_t start = data.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        data.clear();
        return;
    }
    // Remove trailing whitespace
    size_t end = data.find_last_not_of(" \t\n\r");
    data = data.substr(start, end - start + 1);
}

void String::toLowerCase() {
    for (auto& c : data) {
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    }
}

// ============================================================
// Random Numbers
// ============================================================

void randomSeed(uint32_t seed) {
    srand(seed);
}

long random(long max) {
    return rand() % max;
}

long random(long min, long max) {
    if (min >= max) return min;
    return rand() % (max - min) + min;
}

// ============================================================
// FreeRTOS Integration (delegated to thread shim)
// ============================================================

void yield() {
    // Poll SDL events and check for quit conditions (ESC, window close, Ctrl+C).
    // This is critical during blocking UI loops where the original code calls yield()
    // frequently but never polls SDL directly.
    pollEventsAndCheckQuit(1); // 1ms yield to prevent watchdog timeout
}

void vTaskDelay(uint32_t ticks) {
    // Poll SDL events and check for quit conditions during task delays.
    // This ensures ESC, window close button, and Ctrl+C work even when the app
    // is stuck in a blocking loop that calls vTaskDelay.
    pollEventsAndCheckQuit(ticks); // 1 tick = 1ms on ESP32
}

// ============================================================
// ESP-IDF System API (stubbed)
// ============================================================

void esp_deep_sleep_start() {
    // On Linux, deep sleep = exit the program
    Serial.println("[PEPitCompat] esp_deep_sleep_start() — exiting");
    std::exit(0);
}

void esp_restart() {
    Serial.println("[PEPitCompat] esp_restart() — exiting");
    std::exit(0);
}

// ============================================================
// Time API (POSIX getLocalTime)
// ============================================================

bool getLocalTime(struct tm* timeinfo, uint32_t milliseconds) {
    if (!timeinfo) return false;

    time_t now = time(nullptr);

    // Apply optional timezone offset (milliseconds)
    if (milliseconds > 0) {
        now += milliseconds / 1000;
    }

    // Use localtime_r for thread-safe conversion
    struct tm result;
    if (localtime_r(&now, &result)) {
        *timeinfo = result;
        return true;
    }
    return false;
}

// 1-argument overload for Arduino/ESP32 compatibility (no timezone offset)
bool getLocalTime(struct tm* timeinfo) {
    return getLocalTime(timeinfo, 0);
}

// ============================================================
// Initialize boot time (called from main)
// ============================================================

void initArduino() {
    auto now = std::chrono::steady_clock::now();
    bootTime = now;
    // Seed with actual system time (millis() is 0 here, so use epoch count directly)
    auto seed = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    randomSeed(seed ^ 0x5A5A);
}
