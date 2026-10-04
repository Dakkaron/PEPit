# PEPitCompat — ESP32-to-Linux API Mapping Reference

This document maps every ESP32-specific API used in the original code to its Linux equivalent.
Use this as a reference when implementing each shim.

## Arduino Core API (compat/Arduino.h + shim/arduino.cpp) ✅ DONE

### Time Functions
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `millis()` | `std::chrono` since program start | Returns uint32_t, wraps at ~49 days |
| `delay(ms)` | `usleep(ms * 1000)` | Blocking delay in milliseconds |
| `delayMicroseconds(us)` | `usleep(us / 1000)` | Used in setBrightness() |
| `configTime(tz, dst, ...servers)` | `setenv("TZ", ..., 1)` + stub | NTP servers ignored, TZ set for local time |
| `getLocalTime(&tm, offset)` | POSIX `localtime_r()` with timezone offset | ✅ DONE — used in wifiHandler.cpp:85 |

### String Class
The Arduino `String` class is extensively used. Must implement:
- Constructors: `String()`, `String(const char*)`, `String(int)`, `String(uint32_t)`
- Comparison: `equals(const String&)`, `equalsIgnoreCase(const String&)`, `equalsIgnoreCase(const char*)`, `isEmpty()`, `length()`
- Operators: `+` (concat), `+=` (append), `==` (String, const char*), `!=` (String, const char*)
- Modification: `concat(const String&)`, `trim()`, `toLowerCase()`, `clear()`
- Search: `indexOf(const String&)`, `indexOf(char)`, `substring(from, to)`
- Conversion: `toInt()`, `c_str()`
- Special: `setCharAt(index, char)`

Implementation: Wrap `std::string` with Arduino-compatible API. The `equals()` method
is case-sensitive (unlike `equalsIgnoreCase`). Added `operator==(const char*)` and
`operator!=(const char*)` to resolve ambiguous overload errors with string literals.

### Serial Output
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `Serial.begin(baud)` | No-op (stdout always available) | Called in setup() |
| `Serial.print(val)` | `fprintf(stdout, ...)` | Supports String, int, float, const char* |
| `Serial.println(val)` | `fprintf(stdout, ...\n)` | Appends newline (LF on Linux) |
| `Serial.printf(fmt, ...)` | `fprintf(stdout, fmt, ...)` | Direct passthrough |
| `Serial.available()` | Check for stdin input | Returns bytes available to read |
| `Serial.read()` | Read from stdin | Returns char, -1 if none available |
| `Serial.write(buf, len)` | `fwrite(buf, 1, len, stdout)` | Binary output |
| `Serial.flush()` | `fflush(stdout)` | Flush output buffer |
| `Serial.setRxBufferSize(n)` | No-op | Stub for compatibility |
| `Serial.setTxBufferSize(n)` | No-op | Stub for compatibility |

**Critical:** The original code uses `Serial.print()` extensively for debugging. All output
goes to stdout. The serial command handler (`handleSerial()`) reads from stdin.

### GPIO Functions (All Stubbed)
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `pinMode(pin, mode)` | No-op | INPUT/OUTPUT constants defined |
| `digitalWrite(pin, value)` | No-op | HIGH/LOW constants defined |
| `digitalRead(pin)` | Returns 0 | Always returns LOW |
| `analogRead(pin)` | Returns 0 | No ADC on Linux |

Constants defined: `INPUT`, `OUTPUT`, `HIGH` (1), `LOW` (0), `GPIO_NUM_*`

### Math Helpers
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `_min(a, b)` | `std::min(a, b)` | Template function |
| `_max(a, b)` | `std::max(a, b)` | Template function |
| `constrain(val, min, max)` | Custom inline: `std::min(max, std::max(min, val))` | Used throughout codebase |
| `map(value, fromLow, fromHigh, toLow, toHigh)` | Template linear interpolation | ✅ DONE — used in xpt2046.cpp:107-111 |

### Random Numbers
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `randomSeed(seed)` | `srand(seed)` | Seed the PRNG |
| `random(max)` | `rand() % max` | Returns 0 to max-1 |
| `random(min, max)` | `rand() % (max-min) + min` | Returns min to max-1 |

### Memory Allocation
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `heap_caps_malloc(size, caps)` | `malloc(size)` | Ignore MALLOC_CAP_SPIRAM — ✅ DONE |
| `heap_caps_realloc(ptr, size, caps)` | `realloc(ptr, size)` | Ignore caps — ✅ DONE (needed by gameLua.cpp) |
| `MALLOC_CAP_SPIRAM` | 0 (defined as constant) | Capability flag, ignored on Linux — ✅ DONE |
| `free(ptr)` | Standard `free()` | Direct passthrough — ✅ DONE |

### Attributes & Types
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `RTC_DATA_ATTR` | Empty macro (no-op) | RTC retained data attribute — ✅ DONE |
| `DRAM_ATTR` | Empty macro (no-op) | ✅ DONE |
| `IRAM_ATTR` | Empty macro (no-op) | ✅ DONE |
| `FLASHMEM` | Empty macro (no-op) | ✅ DONE |

### FreeRTOS Integration (in Arduino.h or thread shim)
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `yield()` | `usleep(1000)` (1ms) | Brief yield to prevent watchdog timeout — ✅ DONE |
| `vTaskDelay(ticks)` | `usleep(ticks * 1000)` | 1 tick = 1ms on ESP32 — ✅ DONE |

---

## ESP-IDF APIs (now in compat/Arduino.h) ✅ DONE

### esp_reset_reason.h
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `esp_reset_reason_t` enum | Full enum with all reset reasons | ✅ DONE — ESP_RST_UNKNOWN through ESP_RST_USB |
| `ESP_RST_SW` = 3 | Defined in enum | Used in powerHandler.cpp:20 |
| `ESP_RST_USB` = 11 | Defined in enum | Used in powerHandler.cpp:20 |
| `esp_reset_reason()` | Returns `ESP_RST_POWERON` | ✅ DONE — used in powerHandler.cpp:19 |

### esp_sleep.h
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `esp_sleep_enable_ext0_wakeup(gpio, level)` | No-op stub, returns 0 | ✅ DONE — used in powerHandler.cpp:41,90 |
| `esp_sleep_enable_timer_wakeup(us)` | No-op stub, returns 0 | ✅ DONE — used in powerHandler.cpp:42 |
| `esp_deep_sleep_start()` | Calls `std::exit(0)` | ✅ DONE — used in powerHandler.cpp:43,91 |
| `esp_sleep_disable_wakeup_source(source)` | No-op stub | ✅ DONE |

### driver/gpio.h
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `gpio_get_level(pin)` | Returns 0 (no hardware) | ✅ DONE — used in powerHandler.cpp:76 |
| `gpio_hold_en(pin)` | No-op stub | ✅ DONE — used in main.cpp:67 |
| `gpio_hold_dis(pin)` | No-op stub | ✅ DONE |
| `gpio_deep_sleep_hold_en()` | No-op stub | ✅ DONE — used in main.cpp:66 |
| `gpio_deep_sleep_hold_dis()` | No-op stub | ✅ DONE |

### esp_system.h
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `esp_restart()` | Calls `std::exit(0)` with log message | ✅ DONE — used in touchHandler.cpp:104 |

---

## SD_MMC Filesystem (compat/SD_MMC.h + shim/filesystem.cpp) ✅ DONE

### SD_MMC Namespace Functions
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `SD_MMC.begin(mountPath, mode)` | Record data_dir path | Returns bool (true if dir exists) |
| `SD_MMC.end()` | No-op | Stub for cleanup |
| `SD_MMC.setPins(sclk, mosi, miso)` | No-op | Stub (pins not used on Linux) |
| `SD_MMC.open(path)` | `fopen(data_dir + path, "r")` | Returns File object |
| `SD_MMC.open(path, mode, create)` | `fopen(data_dir + path, mode_str)` | Supports FILE_WRITE, FILE_APPEND |
| `SD_MMC.exists(path)` | `access(data_dir + path, F_OK) == 0` | Check file existence |
| `SD_MMC.remove(path)` | `unlink(data_dir + path)` | Delete file |
| `SD_MMC.rename(old, new)` | `rename(data_dir+old, data_dir+new)` | Rename file |
| `SD_MMC.mkdir(path)` | `mkdir(data_dir + path, 0755)` | Create directory |
| `SD_MMC.rmdir(path)` | `rmdir(data_dir + path)` | Remove directory |
| `SD_MMC.cardSize()` | Returns 1GB (stubbed) | Used for display, not critical |

### File Class
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `File()` default constructor | Null FILE* pointer | Default state is invalid/closed |
| `file.read()` | `fgetc(fp)` | Returns single byte as uint8_t |
| `file.read(buf, len)` | `fread(buf, 1, len, fp)` | Read multiple bytes |
| `file.write(buf, len)` | `fwrite(buf, 1, len, fp)` | Write bytes (for FILE_WRITE mode) |
| `file.println(text)` | `fprintf(fp, "%s\n", text)` | Print with newline |
| `file.print(text)` | `fprintf(fp, "%s", text)` | Print without newline |
| `file.available()` | `!feof(fp) && !ferror(fp)` | More data available? |
| `file.seek(offset)` | `fseek(fp, offset, SEEK_SET)` | Seek to absolute position |
| `file.size()` | `fseek(0,SEEK_END); ftell(); fseek(back)` | Get file size in bytes |
| `file.name()` | `basename(path.c_str())` | Returns filename as const char* |
| `file.isDirectory()` | Check if path is directory | Uses stat() on the path |
| `file.openNextFile()` | `readdir(dir)` → next entry | Iterates directory entries |
| `file.close()` | `fclose(fp)` or `closedir(dir)` | Close file/directory |
| Boolean conversion (`if (file)`) | `fp != nullptr` | File is valid/open |

**Critical:** The `File` class must support both file and directory operations. When
opened as a directory (e.g., `SD_MMC.open("/games")`), it uses `DIR*` and `readdir()`.
When opened as a file, it uses `FILE*`. The `isDirectory()` method distinguishes between
the two modes.

### File Mode Constants
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `FILE_WRITE` | `"w"` mode for fopen | Create/truncate file |
| `FILE_APPEND` | `"a"` mode for fopen | Append to existing file |

### fs::File Namespace
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `fs::File` | Alias to our File class | Used in gfxHandler for BMP loading |

---

## TFT_eSPI Display (compat/TFT_eSPI.h + shim/display.cpp) ✅ DONE

### Color Constants
All colors are 16-bit RGB565 values (same as ESP32):
```cpp
#define TFT_BLACK   0x0000
#define TFT_WHITE   0xFFFF
#define TFT_RED     0xF800
#define TFT_GREEN   0x07E0
#define TFT_BLUE    0x001F
#define TFT_YELLOW  0xFFE0
#define TFT_ORANGE  0xFDA0
```

### Text Datum Constants
```cpp
#define TL_DATUM 0  // Top-left
#define TC_DATUM 1  // Top-center
#define TR_DATUM 2  // Top-right
#define ML_DATUM 3  // Middle-left
#define CL_DATUM 3  // Center-left (alias)
#define MC_DATUM 4  // Middle-center
#define CC_DATUM 4  // Center-center (alias)
#define MR_DATUM 5  // Middle-right
#define CR_DATUM 5  // Center-right (alias)
#define BL_DATUM 6  // Bottom-left
#define BC_DATUM 7  // Bottom-center
#define BR_DATUM 8  // Bottom-right
```

### TFT_eSPI Class Methods
| Method | Implementation | Notes |
|---|---|---|
| `TFT_eSPI()` | Init SDL null state | Default constructor — ✅ DONE |
| `init()` | Create SDL window + renderer | Called once in setup() — ✅ DONE |
| `fillScreen(color)` | Fill entire 320x240 buffer | RGB565 pixel format — ✅ DONE |
| `setRotation(n)` | Record rotation (0-3) | Affects coordinate mapping — ✅ DONE |
| `setSwapBytes(enabled)` | Record swap flag | Affects pushImage byte order — ✅ DONE |
| `writecommand(cmd)` | No-op stub | Raw LCD command, not needed on Linux — ✅ DONE |
| `fillRect(x,y,w,h,color)` | Fill rectangle in pixel buffer | Standard raster operation — ✅ DONE |
| `drawRect(x,y,w,h,color)` | Draw rectangle outline | 1-pixel border — ✅ DONE |
| `drawFastHLine(x,y,w,color)` | Draw horizontal line | Optimized for single row — ✅ DONE |
| `drawLine(x0,y0,x1,y1,color)` | Bresenham line algorithm | Arbitrary line drawing — ✅ DONE |
| `drawCircle(x,y,r,color)` | Midpoint circle algorithm | Circle outline only — ✅ DONE |
| `pushImage(x,y,w,h,buf,mask)` | Blit pixel buffer with transparency | maskColor = transparent color — ✅ DONE |
| `pushSpriteFast(x,y)` | Copy sprite buffer to screen | Fast path, no transparency — ✅ DONE |
| `setTextSize(n)` | Record text size multiplier | 1 = normal, 2 = double height — ✅ DONE |
| `setCursor(x,y)` | Record cursor position | For print/println — ✅ DONE |
| `print(val)` | Render text at cursor | Supports String, int, const char* — ✅ DONE |
| `println(val)` | print() + advance cursor to next line | ✅ DONE |
| `drawString(text,x,y)` | Render text at position with datum alignment | Uses current font and color — ✅ DONE |
| `setTextColor(color)` | Record text color for rendering | 16-bit RGB565 — ✅ DONE |
| `setTextDatum(datum)` | Record text alignment datum | Affects drawString positioning — ✅ DONE |
| `getTextDatum()` | Return current datum | Used for save/restore pattern — ✅ DONE |
| `setFreeFont(font)` | Record font pointer | Custom bitmap font from MyFont.h — ✅ DONE |
| `invertDisplay(state)` | Toggle pixel inversion flag | Affects all subsequent rendering — ✅ DONE |

### TFT_eSprite Class Methods
| Method | Implementation | Notes |
|---|---|---|
| `TFT_eSprite(tft)` | Store parent display reference | Constructor takes TFT_eSPI* — ✅ DONE (fixed) |
| `setColorDepth(bits)` | Record color depth (16 = RGB565) | Called before createSprite — ✅ DONE |
| `createSprite(w,h)` | Allocate uint16_t[w*h] buffer | Heap allocation — ✅ DONE |
| `createSprite(w,h,opts)` | Same as above (opts ignored) | Options for DMA on ESP32 — ✅ DONE |
| `deleteSprite()` | Free pixel buffer, null pointer | Release memory — ✅ DONE |
| `created()` | Return buffer != nullptr | Check if sprite is allocated — ✅ DONE |
| `width()`, `height()` | Return buffer dimensions | Used for transform calculations — ✅ DONE |
| `fillSprite(color)` | Fill entire sprite buffer | Like fillScreen but for sprite — ✅ DONE |
| `pushSprite(x,y)` | Blit to parent display | With cursor-based positioning — ✅ DONE |
| `pushSpriteFast(x,y)` | Fast blit to parent display | Direct memcpy, no transparency — ✅ DONE |
| `pushToSprite(target,x,y,mask)` | Blit to another sprite with transparency | For compositing sprites — ✅ DONE |
| `pushToSprite(target,x,y)` | Blit to another sprite (no mask) | Default mask=0 — ✅ DONE |
| `pushToSprite(target,dx,dy,sx,sy,w,h,mask)` | Blit region with transparency | Source rect + mask — ✅ DONE |
| `pushToSprite(target,dx,dy,sx,sy,w,h)` | Blit region (no mask) | Source rect only — ✅ DONE |
| `fillCircle(x,y,r,color)` | Fill circle in sprite buffer | Midpoint algorithm — ✅ DONE |
| `drawFastVLine(x,y,h,color)` | Draw vertical line in sprite buffer | Optimized single column — ✅ DONE |
| `fillTriangle(x0,y0,x1,y1,x2,y2,color)` | Fill triangle in sprite buffer | Scanline fill — ✅ DONE |
| `fontHeight()` | Return current font height in pixels | Used for text layout — ✅ DONE |
| `getCursorX()` / `getCursorY()` | Return cursor position | Used for text positioning — ✅ DONE |
| `drawString(text,x,y,font)` | Render text with custom font | Font param for bitmap fonts — ✅ DONE |
| `get16BitBuffer()` / `getPointer()` | Return uint16_t* pointer | Direct buffer access for transforms — ✅ DONE |
| `frameBuffer(index)` | Switch active frame buffer | Supports double/triple buffering — ✅ DONE |
| `setSwapBytes(enabled)` | Record swap flag for this sprite | Per-sprite byte order — ✅ DONE |
| `getSwapBytes()` | Return current swap flag | Used by gfxHandler transforms — ✅ DONE |

All drawing primitives (fillRect, drawString, etc.) work the same as TFT_eSPI but
operate on the sprite's pixel buffer instead of the main display.

**Critical:** The `get16BitBuffer()` method is used extensively in gfxHandler for
direct pixel manipulation (sprite transforms, scaling, rotation). The buffer must be
a contiguous `uint16_t[]` array in row-major order (left-to-right, top-to-bottom).

---

## Touch Controller (XPT2046 replacement) ✅ DONE

### XPT2046 Class Methods (shim/touch.cpp replaces original xpt2046.cpp)
| Method | Implementation | Notes |
|---|---|---|
| `XPT2046(SPI, cs, irq)` | Store SPI ref (unused) | SPI/bus params ignored on Linux — ✅ DONE |
| `begin(xres, yres)` | Record resolution (240x320) | Used for coordinate mapping — ✅ DONE |
| `pressed()` | SDL mouse button 1 (left click) | Left mouse button state — ✅ DONE |
| `RawX()`, `RawY()` | Mouse pos scaled to raw ADC (0-4095) | Window coords → ADC range — ✅ DONE |
| `X()`, `Y()` | Calibrated coords (map raw → display) | Calibration + rotation applied — ✅ DONE |
| `setCal(...)` | Store calibration points | Used for raw→calibrated mapping — ✅ DONE |
| `setRotation(n)` | Record rotation for coord swap | Affects X/Y coordinate mapping — ✅ DONE |
| `setZThreshold(n)` | No-op stub | Sensitivity not applicable on Linux — ✅ DONE |

**Critical:** CMake must exclude original xpt2046.cpp and include our shim/touch.cpp.
The touch shim uses `getSDLWindow()` from display.cpp and `setTouchDisplayParams()` for scaling.

---

## Preferences (compat/Preferences.h + shim/prefs.cpp) ✅ DONE

### Preferences Class Methods
| Method | Implementation | Notes |
|---|---|---|
| `Preferences()` | Default constructor | Empty state, no namespace open — ✅ DONE |
| `begin(namespace)` | Load from `<data_dir>/prefs_<namespace>.dat` | Read-write mode — ✅ DONE |
| `begin(namespace, readonly)` | Load (read-only flag) | Changes not saved on end() — ✅ DONE |
| `end()` | Save to disk (if not readonly) | Flush changes — ✅ DONE |
| `isKey(key)` | Check if key exists in loaded data | Returns bool — ✅ DONE |
| `getUInt(key)` | Read uint32_t from file | Default 0 if missing — ✅ DONE |
| `getInt(key)` | Read int32_t from file | Default 0 if missing — ✅ DONE |
| `getUShort(key)` | Read uint16_t from file | Default 0 if missing — ✅ DONE |
| `getShort(key)` | Read int16_t from file | Default 0 if missing — ✅ DONE |
| `getULong64(key)` | Read uint64_t from file | Default 0 if missing — ✅ DONE |
| `getLong64(key)` | Read int64_t from file | Default 0 if missing — ✅ DONE |
| `getUChar(key)` | Read uint8_t from file | Default 0 if missing — ✅ DONE |
| `getChar(key)` | Read int8_t from file | Default 0 if missing — ✅ DONE |
| `getString(key, buf, len)` | Read string to buffer, return length | Null-terminates — ✅ DONE |
| `getString(key, default)` | Return String with default if missing | 2-arg overload — ✅ DONE |
| `getBytesLength(key)` | Return decoded blob size | Base64-decoded length — ✅ DONE |
| `getBytes(key, buf, len)` | Decode base64 blob to buffer | Binary data storage — ✅ DONE |
| `putUInt(key, val)` | Store uint32_t in file | In-memory, saved on end() — ✅ DONE |
| `putInt(key, val)` | Store int32_t in file | In-memory, saved on end() — ✅ DONE |
| `putBytes(key, buf, len)` | Encode binary as base64 in file | For touch calibration data — ✅ DONE |
| `putFloat(key, val)` | Store float in file | In-memory, saved on end() — ✅ DONE |
| `getFloat(key)` | Read float from file | Default 0 if missing — ✅ DONE |
| `clear()` | Remove all keys from current namespace | Empties data — ✅ DONE |
| `remove(key)` | Remove specific key from file | Deletes entry — ✅ DONE |
| `freeEntries()` | Returns 999 (stubbed) | Not meaningful on Linux — ✅ DONE |

File format: `key:type:value` (one per line), NOT JSON as originally planned.
Binary blobs stored as base64 strings.

---

## WiFi + HTTPClient (Stubbed) ✅ DONE

### WiFi Class
| Method | Implementation | Notes |
|---|---|---|
| `WiFi.scanNetworks()` | Returns 0 | No networks available |
| `WiFi.SSID(i)` | Returns empty String | No SSIDs to return |
| `WiFi.mode(mode)` | No-op stub | Mode setting ignored |
| `WiFi.begin(ssid, pass)` | Returns true (OK) | But status stays disconnected |
| `WiFi.status()` | Returns WL_NO_SSID_AVAIL | No WiFi available on Linux |

Constants: `WIFI_STA`, `WIFI_CONNECTION_SEARCHING=0`, `WIFI_CONNECTION_NOWIFI=1`, `WIFI_CONNECTION_OK=2`

### HTTPClient Class
| Method | Implementation | Notes |
|---|---|---|
| `begin(url)` | Stores URL (OK) | For reference |
| `GET()` | Returns 200 OK | No error, but stream is empty |
| `POST(payload)` | Returns 200 OK | No error, no-op |
| `getSize()` | Returns 0 | No content (loop runs zero iterations) |
| `getStreamPtr()` | Returns WiFiClient* | Empty stream (available=0, readBytes returns 0) |
| `setFollowRedirects(mode)` | No-op stub (OK) | HTTPC_STRICT_FOLLOW_REDIRECTS defined |

**Design:** GET returns 200 with empty stream so code flow continues normally.
Download loops execute zero times (size=0), update check gets empty URL → NOT_AVAILABLE.

### WiFiClient Class
| Method | Implementation | Notes |
|---|---|---|
| `connect(host, port)` | Returns true (OK) | No actual connection |
| `available()` | Returns 0 | No data available |
| `readBytes(buf, len)` | Returns 0 | No data to read |

---

## FreeRTOS (shim/thread.cpp) ✅ DONE

### Task Management
| ESP32 | Linux Equivalent | Notes |
|---|---|---|
| `TaskHandle_t` | `void*` (pthread_t cast) | Opaque task handle type — ✅ DONE |
| `xTaskCreatePinnedToCore(func, name, stack, param, priority, handle, core)` | `new std::thread(func, param)` | Ignore name, stack, priority, core — ✅ DONE |
| `vTaskDelay(ticks)` | `usleep(ticks * 1000)` | 1 tick = 1ms — ✅ DONE (in arduino.cpp) |
| `vTaskDelete(handle)` | Thread join + delete, or std::exit(0) if NULL | Clean up thread resources — ✅ DONE |
| `xTaskGetCurrentTaskHandle()` | `(void*)pthread_self()` | Current thread handle — ✅ DONE |

---

## ESP-IDF Stubs (separate headers) ✅ DONE

### esp_log.h
| Macro | Implementation | Notes |
|---|---|---|
| `ESP_LOG_ERROR(tag, fmt, ...)` | `fprintf(stderr, "[E]%s: ", tag); fprintf(stderr, fmt, ...);` | Red color on terminal |
| `ESP_LOG_WARN(tag, fmt, ...)` | `fprintf(stderr, "[W]%s: ", tag); fprintf(stderr, fmt, ...);` | Yellow color on terminal |
| `ESP_LOG_INFO(tag, fmt, ...)` | `fprintf(stdout, "[I]%s: ", tag); fprintf(stdout, fmt, ...);` | Green color on terminal |
| `ESP_LOG_DEBUG(tag, fmt, ...)` | Conditional (only if DEBUG defined) | Gray color on terminal |
| `ESP_LOG_VERBOSE(tag, fmt, ...)` | Conditional (only if VERBOSE defined) | Cyan color on terminal |
| `esp_log_level_set(tag, level)` | No-op stub | Level filtering not implemented — ✅ DONE |
| `log_e(tag, fmt, ...)` | Shorthand for ESP_LOG_ERROR | Used by EspLuaEngine — ✅ DONE |
| `log_v(tag, fmt, ...)` | Shorthand for ESP_LOG_VERBOSE (requires VERBOSE define) | Used by EspLuaEngine — ✅ DONE |

### esp_rom_crc.h
| Function | Implementation | Notes |
|---|---|---|
| `esp_rom_crc32_le(init, data, len)` | Standard CRC-32/ISO-HDLC algorithm | Used by serialHandler for file upload verification |

### nvs_flash.h / nvs.h ✅ DONE
All functions are no-op stubs returning OK (0). The original code uses NVS for prefs dump:
- `nvs_flash_erase()` → no-op, returns 0 (OK)
- `nvs_flash_init()` → no-op, returns 0 (OK)
- `nvs_open(ns, mode, &handle)` → sets handle to nullptr, returns 0 (OK)
- `nvs_close(handle)` → no-op, returns 0 (OK)
- `nvs_entry_find(ns, key, mode, &iterator)` → sets iterator to nullptr, returns 0 (OK)
- `nvs_entry_next(&iterator)` → sets iterator to nullptr, returns 1 (done)
- `nvs_entry_info(iterator, &info)` → returns 1 (done/no entries)
- Constants: `NVS_DEFAULT_PART_NAME = "nvs"`, `NVS_TYPE_ANY = 0xFFFFFFFF`

### driver/rtc_io.h
All RTC GPIO functions are no-op stubs:
- `rtc_gpio_init(pin)` → no-op
- `rtc_gpio_set_direction(pin, mode)` → no-op
- `rtc_gpio_pullup_dis(pin)` → no-op
- `rtc_gpio_pulldown_dis(pin)` → no-op

### esp32-hal.h
No-op stub header. Used by wifiHandler.cpp, which is already fully stubbed.

---

## Other Stubs ✅ DONE

### SPI.h
No-op SPIClass + global `SPI` instance. Methods: begin(), end(), transfer().

### SD.h
Empty header for include compatibility (SD_MMC is the actual interface used).

### OneButton.h
No-op class. Accepts nullptr callbacks (powerHandler passes nullptr to detach).
Methods: attachClick(), attachDoubleClick(), attachLongPressStop(), attachLongPressStart(), tick().

### Update.h
OTA stub. begin()/writeStream() return OK. **end() returns false** to prevent ESP.restart().
Constants: `U_FLASH = 0`. Methods: onProgress() no-op callback.

### NuSerial.hpp (Bluetooth)
isConnected() returns false → simulation mode in original code handles trampoline input.
Methods: begin(), available(), read(), print(), end() — all no-op.

### NimBLEDevice.h
BLE device stubs + **CONFIG_BT_ENABLED** and **CONFIG_BLUEDROID_ENABLED** defines.
These defines prevent the `#error` in bluetoothHandler.cpp compile-time check.
Methods: init(), getAdvertising(), setSecurityPasskey(), getServer(), deinit() — all no-op.

### Wire.h (I2C)
No-op TwoWire class + global `Wire` instance. Used by joystickHandler (but joystick uses keyboard).
Methods: begin(), end(), requestFrom(), beginTransmission(), endTransmission(), write(), available(), read().

### m5_unit_joystick2.hpp + shim/joystick.cpp
Keyboard-mocked I2C joystick. **begin() returns true** (joystick is "present" on Linux).
Added `JOYSTICK2_ADDR` constant (0x3E) for joystickHandler compatibility.
- Arrow keys / WASD → X/Y axis deflection (16-bit ADC: 0-65535, center=32768)
- Space / Enter → button press (get_button_value returns 0 = pressed)
- set_rgb_color() → no-op (no physical LED)

### Adafruit_HX711.h + shim/sensor.cpp
Keyboard-mocked pressure sensor. **isBusy() always returns false** (keyboard always ready).
- During init: readChannelRaw returns 0 → tare offset = 0
- After taring: keyboard-driven values based on target pressure
  - No key: 0 pressure (not blowing)
  - Low-pressure key ('A' default): ~50% of target pressure
  - Good-pressure key ('S' default): ~100% of target pressure
- Keys configurable via --low-pressure-key / --good-pressure-key CLI params

### ESP32-targz.h
Firmware extraction stubs. All methods return OK (no actual unpacking).
- `tarGzFS` global FS instance with begin() returning OK
- `TarUnpacker` / `TarGzUnpacker` classes with all callback setters as no-op
- `targzTotalBytesFn` / `targzFreeBytesFn` stub functions (return 0 / large number)

### ESP Class (in Arduino.h)
- `ESP.restart()` → delegates to esp_restart() → std::exit(0)
- `ESP.getFreeHeap()` → returns 1 MB (stubbed value)
- `ESP.getFreePsram()` → returns 0 (no PSRAM on Linux)

---

## Implementation Order Dependencies

All compat headers and shim implementations are complete. Build compiles cleanly with zero errors.

1. ~~**Arduino.h**~~ — ✅ DONE (foundation for everything else)
2. ~~**esp_log.h**~~ — ✅ DONE (logging used by all components, includes log_e/log_v)
3. ~~**thread.cpp**~~ — ✅ DONE (FreeRTOS wrappers needed by Arduino.h)
4. ~~**filesystem.cpp + SD_MMC.h**~~ — ✅ DONE (File I/O needed by prefs, gfx, games)
5. ~~**prefs.cpp + Preferences.h**~~ — ✅ DONE (Needed by touchHandler, sdHandler; added getString/getFloat/putFloat)
6. ~~**display.cpp + TFT_eSPI.h**~~ — ✅ DONE (Display rendering, depends on SDL2; added pushToSprite overloads, fillCircle, drawFastVLine, fontHeight)
7. ~~**touch.cpp + xpt2046.h**~~ — ✅ DONE (Touch input, replaces original xpt2046.cpp)
8. ~~**sensor.cpp + Adafruit_HX711.h**~~ — ✅ DONE (Pressure sensor, keyboard input)
9. ~~**joystick.cpp + m5_unit_joystick2.hpp**~~ — ✅ DONE (Joystick, keyboard input; added JOYSTICK2_ADDR)
10. ~~**All compat headers**~~ — ✅ DONE (SPI.h, SD.h, WiFi.h, HTTPClient.h, Update.h,
    OneButton.h, esp_rom_crc.h, nvs_flash.h, nvs.h, Wire.h, ESP32-targz.h, etc.)
11. ~~**instances.cpp**~~ — ✅ DONE (Global instance definitions + drawButton/drawImageButton wrappers)
12. ~~**main.cpp**~~ — ✅ DONE (Entry point, CLI parsing, SDL init)
13. ~~**src/ symlink**~~ — ✅ DONE (points to ../T-HMI-PEPmonitor/src/)
14. ~~**data/ directory**~~ — ✅ DONE (default profiles.ini, systemConfig.ini)
15. ~~**Git submodules**~~ — ✅ DONE (ArduinoJson, EspLuaEngine, base64_arduino)
16. ~~**CMakeLists.txt**~~ — ✅ DONE (Build system with SDL2, Lua, include paths, compile definitions)
