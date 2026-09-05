/*
 * PEPitCompat — ESP-IDF Logging Shim
 * 
 * Replaces esp_log.h with printf-based colored output.
 */

#ifndef ESP_LOG_H
#define ESP_LOG_H

#include <cstdio>
#include <cstdarg>

// Log level constants (from ESP-IDF)
#define ESP_LOG_NONE 0
#define ESP_LOG_ERROR 1
#define ESP_LOG_WARN 2
#define ESP_LOG_INFO 3
#define ESP_LOG_DEBUG 4
#define ESP_LOG_VERBOSE 5

// ANSI color codes for terminal output
#define ESP_LOG_COLOR_ERROR   "\033[31m"  // Red
#define ESP_LOG_COLOR_WARN    "\033[33m"  // Yellow
#define ESP_LOG_COLOR_INFO    "\033[32m"  // Green
#define ESP_LOG_COLOR_DEBUG   "\033[90m"  // Gray
#define ESP_LOG_COLOR_VERBOSE "\033[36m"  // Cyan
#define ESP_LOG_COLOR_RESET   "\033[0m"

// Log level macros with colored output
#define ESP_LOGE(tag, format, ...) \
    fprintf(stderr, "%s[E]%s %s: " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_ERROR, tag, ##__VA_ARGS__)

#define ESP_LOGW(tag, format, ...) \
    fprintf(stderr, "%s[W]%s %s: " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_WARN, tag, ##__VA_ARGS__)

#define ESP_LOGI(tag, format, ...) \
    fprintf(stdout, "%s[I]%s %s: " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_INFO, tag, ##__VA_ARGS__)

#ifdef DEBUG
#define ESP_LOGD(tag, format, ...) \
    fprintf(stdout, "%s[D]%s %s: " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_DEBUG, tag, ##__VA_ARGS__)
#else
#define ESP_LOGD(tag, format, ...) ((void)0)
#endif

#ifdef VERBOSE
#define ESP_LOGV(tag, format, ...) \
    fprintf(stdout, "%s[V]%s %s: " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_VERBOSE, tag, ##__VA_ARGS__)
#else
#define ESP_LOGV(tag, format, ...) ((void)0)
#endif

// Stub for log level setting (not implemented on Linux)
inline void esp_log_level_set(const char* tag, int level) {}

// ESP-IDF shorthand logging functions (used by EspLuaEngine and other libraries)
// These are the non-tag versions: log_e, log_w, log_i, log_d, log_v
#define log_e(format, ...) \
    fprintf(stderr, "%s[E]%s " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_ERROR, ##__VA_ARGS__)

#define log_w(format, ...) \
    fprintf(stderr, "%s[W]%s " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_WARN, ##__VA_ARGS__)

#define log_i(format, ...) \
    fprintf(stdout, "%s[I]%s " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_INFO, ##__VA_ARGS__)

#ifdef DEBUG
#define log_d(format, ...) \
    fprintf(stdout, "%s[D]%s " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_DEBUG, ##__VA_ARGS__)
#else
#define log_d(format, ...) ((void)0)
#endif

#ifdef VERBOSE
#define log_v(format, ...) \
    fprintf(stdout, "%s[V]%s " format "\n" ESP_LOG_COLOR_RESET, \
            ESP_LOG_COLOR_VERBOSE, ##__VA_ARGS__)
#else
#define log_v(format, ...) ((void)0)
#endif

#endif // ESP_LOG_H
