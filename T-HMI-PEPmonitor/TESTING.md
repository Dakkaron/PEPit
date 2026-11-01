# Unit Testing — T-HMI-PEPmonitor

The firmware is tested natively via `../PEPitCompat/`: shim headers (`compat/`)
+ implementations (`shim/`) run the **unmodified** firmware source (`src/`
symlink) on Linux (POSIX files, SDL2, JSON prefs). Test the code as shipped —
no refactoring. Current coverage: INI parser, system/profile/game config,
game selection, pressure filter, execution log (write + read), time, matrix
math.

## Build & run

```bash
cd PEPitCompat
cmake -B build && cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure        # headless (SDL dummy driver)
SDL_VIDEODRIVER=dummy PEPIT_DATA_DIR=$PWD/build/test_data \
    ./build/tests/pepit_tests "[tag]" "*name*"    # Catch2 filters
```

`tests/catch_main.cpp` boots the shims exactly like the app (shared
`shim/appinit.cpp`). CI (`.github/workflows/pepmonitor-tests.yml`) runs the
same ctest step on every change to `T-HMI-PEPmonitor/` or `PEPitCompat/`.

## Rules for writing tests

1. **Hermeticity.** Use `makeSdRoot()` / `writeSdFile()` / `readSdFile()`
   from `tests/test_helpers.h` (fresh temp dir + `SD_MMC.setRootDir` per
   test). Never write into the repo `data/` — a running app instance may have
   it open.
2. **Never reach `checkFailWithMessage()`** — infinite loop on device *and*
   shim (unknown task types, missing required blow params, game-not-found).
   Also untestable by design: `getIniSection` buffer-overflow edges (crash,
   don't pin), `displayExecutionList` (interactive UI loop), `open()`-failure
   branches (shim always creates the file).
3. **Pin quirks, don't hide them.** Current-but-odd behavior →
   `TEST_CASE("KNOWN QUIRK ...")` asserting it; fixed bugs → `REGRESSION`
   tests. Pinned: INI CRLF blank lines; dropped final line; garbage bool with
   `def=true` → true; non-numeric int → 0; `readPressure` idle timeout zeroes
   the window but not the running total; `invertM` singular → silent no-op;
   `normalizeVector` scales output in place.
4. **Static state persists across test cases** in `readPressure()` (window /
   total / tare). Script sensor readings with the test-only
   `Adafruit_HX711::testQueueRead()` / `testClearQueuedReads()` — never rely
   on keyboard state (the sim is dead, see below). The idle-timeout branch
   injects a **permanent** phantom into `total` that no readings wash out →
   that quirk test must stay **last** in `test_pressure_sensor.cpp`.
5. **Time.** `setTimeFromString()`/`setTime()` never touch the host clock;
   they record an offset that `getLocalTime()` applies (faked clock runs with
   real elapsed time, like the device RTC). Tests that set the time must
   restore with `pepit_compat_resetClockOffset()` (RAII guard). The process
   runs `TZ=UTC` (device baseline, set in `initArduino()`) — rely on it; the
   app's `mktime`/`localtime` round-trip is exact only without DST.

## Open sharp edges (context, not test targets)

- **Keyboard pressure sim is dead**: nothing calls `setTargetPressure()`, and
  the formula is 100× off (`target*65` yields 1.0 in the app's 0–100% units).
  Games work via `simulateBlowing=true` (98/1 path).
- **Shim `String` ≠ Arduino `String`** on all edge cases — a test can pass
  natively and differ on device.
- **Shim drift**: a PR changing any shim-visible API in `src/` must also
  update `PEPitCompat/` — CI enforces the build.
