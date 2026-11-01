/*
 * PEPitCompat — Unit tests: pressure sensor filtering (src/hardware/pressuresensor.cpp)
 *
 * Readings are scripted via the test-only Adafruit_HX711::testQueueRead()
 * (the app's keyboard-driven shim is bypassed). readPressure() perceives
 * `pct` percent of target as readChannel = pct * 65 * targetPressure.
 *
 * WARNING — the smoothing state (window, total, skips, tare) is static and
 * persists across test cases; resetFilter() re-derives a clean state by
 * feeding ten zero readings, which works only while `total` stays consistent
 * with the window. (The idle timeout used to break that by zeroing the
 * window but NOT the total; it now zeroes both, so the state stays
 * consistent for the rest of the process.)
 */

#include "catch_amalgamated.hpp"

#include <chrono>
#include <thread>

#include "pressuresensor.h"
#include "systemconfig.h"
#include "Adafruit_HX711.h"

namespace {

BlowData makeBlow(int32_t target = 100) {
    BlowData b{};
    b.ms = millis();
    b.blowStartMs = b.ms - 100;  // 100ms into the blow: inside the target window
    b.targetDurationMs = 10000;
    b.targetPressure = target;
    b.negativePressure = false;
    b.pressure = 0;
    b.cumulativeError = 0;
    return b;
}

float channelFor(int32_t target, float pct) {
    return pct * 65.0f * static_cast<float>(target);
}

void feed(BlowData& b, float channelValue) {
    Adafruit_HX711::testQueueRead(channelValue);
    b.ms = millis();  // keep the 5s stale-window branch from firing
    readPressure(&b);
}

// Restore a clean filter state: ten zero readings replace the whole window
// and drive `total` back to zero (valid only while the state is consistent).
void resetFilter() {
    Adafruit_HX711::testClearQueuedReads();
    systemConfig.simulateBlows = false;
    BlowData b = makeBlow();
    for (int i = 0; i < 10; i++) feed(b, 0.0f);
}

}  // namespace

TEST_CASE("pressure: simulate mode follows the blow window", "[pressure]") {
    bool savedSim = systemConfig.simulateBlows;
    systemConfig.simulateBlows = true;
    Adafruit_HX711::testClearQueuedReads();

    BlowData b = makeBlow();
    b.targetDurationMs = 1000;
    b.blowStartMs = b.ms;  // duration 0 -> blowing (grace window)

    readPressure(&b);
    REQUIRE(b.pressure == 98);

    b.ms += 1100;  // duration 1100: inside the 5s pause after the blow
    readPressure(&b);
    REQUIRE(b.pressure == 1);

    b.ms += 5200;  // duration 6300 > 1000+100+5000 -> next blow
    readPressure(&b);
    REQUIRE(b.pressure == 98);

    systemConfig.simulateBlows = savedSim;
}

TEST_CASE("pressure: constant reading converges the 10-sample moving average", "[pressure]") {
    resetFilter();
    BlowData b = makeBlow();

    for (int i = 0; i < 10; i++) feed(b, channelFor(100, 100));

    // tare drifts only ~0.00005 over ten readings, so the average is 100 +/- 0.5
    REQUIRE((b.pressure > 99.5f && b.pressure < 100.5f));
}

TEST_CASE("pressure: out-of-cutoff readings are skipped; the 11th consecutive one zeros a slot", "[pressure]") {
    resetFilter();
    BlowData b = makeBlow();

    for (int i = 0; i < 10; i++) feed(b, channelFor(100, 100));
    REQUIRE((b.pressure > 99.5f && b.pressure < 100.5f));

    // 250% exceeds the +/-200 cutoff: skipped, window untouched (skips 1..10)
    for (int i = 0; i < 10; i++) feed(b, channelFor(100, 250));
    REQUIRE((b.pressure > 99.5f && b.pressure < 100.5f));

    // 11th consecutive out-of-range: one window slot zeroed -> (1000-100)/10
    feed(b, channelFor(100, 250));
    REQUIRE((b.pressure > 89.5f && b.pressure < 90.5f));
}

TEST_CASE("pressure: negative pressure is sign-flipped and clamped at zero", "[pressure]") {
    resetFilter();
    BlowData b = makeBlow();
    b.negativePressure = true;

    for (int i = 0; i < 10; i++) feed(b, channelFor(100, 50));  // -> -50 after flip

    // running average is -50, tare drifts slightly negative, clamp to >= 0
    REQUIRE(b.pressure == 0.0f);
}

TEST_CASE("pressure: cumulative error decays by 1/16 per reading while inside the target duration", "[pressure]") {
    resetFilter();
    BlowData b = makeBlow();

    // Phase 1: let the window fill at 100% (the ramp-up's |pressure-100|
    // cost lands on this cumulative error, so...)
    for (int i = 0; i < 10; i++) feed(b, channelFor(100, 100));

    // ...reset it, then feed at a steady 100% where each step only adds
    // the int-truncation residue |99-100| = 1.
    b.cumulativeError = 500;
    for (int i = 0; i < 10; i++) feed(b, channelFor(100, 100));

    // 500 * (15/16)^10 ~= 256, plus <= 1 per step
    REQUIRE(b.cumulativeError > 200);
    REQUIRE(b.cumulativeError < 500);
}

TEST_CASE("pressure: cumulative error is untouched outside the target duration", "[pressure]") {
    resetFilter();
    BlowData b = makeBlow();
    b.cumulativeError = 500;
    b.blowStartMs = b.ms - 20000;  // already past the 10s target duration

    for (int i = 0; i < 10; i++) feed(b, channelFor(100, 100));

    REQUIRE(b.cumulativeError == 500);
}

// Slowest test (5.2 s sleep) — kept last. It no longer pollutes the static
// filter state: the idle timeout resets the total along with the window.

TEST_CASE("REGRESSION pressure: idle timeout resets the running total with the window (was a permanent phantom)", "[pressure]") {
    resetFilter();
    BlowData b = makeBlow();

    for (int i = 0; i < 10; i++) feed(b, channelFor(100, 100));
    REQUIRE((b.pressure > 99.5f && b.pressure < 100.5f));

    // Let the 5s idle timeout expire (can't backdate b.ms — it is unsigned
    // and the test process is younger than the timeout).
    b.ms = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(5200));

    Adafruit_HX711::testQueueRead(channelFor(100, 100));
    readPressure(&b);

    // The window AND the total were reset, so the first reading after the
    // pause re-arms from zero: 100/10 = 10, minus negligible tare drift.
    REQUIRE((b.pressure > 9.5f && b.pressure < 10.5f));

    for (int i = 0; i < 9; i++) feed(b, channelFor(100, 100));
    REQUIRE((b.pressure > 99.5f && b.pressure < 100.5f));
}
