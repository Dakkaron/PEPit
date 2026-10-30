/*
 * PEPitCompat — Unit tests: pressure sensor filtering (src/hardware/pressuresensor.cpp)
 *
 * The HX711 shim is keyboard-driven in the app; tests use the test-only
 * Adafruit_HX711::testQueueRead() hook to script deterministic readings.
 *
 * readPressure() computes: sensorValue = readChannel / (65 * targetPressure),
 * so a reading representing `pct` percent of target is readChannel = pct*65*target.
 *
 * WARNING — static state and test ordering:
 *   The smoothing state (window, readIndex, total, skips, tare) is static
 *   inside readPressure() and persists across test cases for the whole
 *   process. resetFilter() re-derives a clean state by feeding ten zero
 *   readings, which works as long as `total` is consistent with the window.
 *
 *   The idle-timeout quirk (last test) breaks that consistency: it zeroes
 *   the window WITHOUT resetting `total`, injecting "phantom mass" that
 *   NO sequence of readings can wash out (overwriting a slot changes both
 *   total and the window sum by the same amount — the difference is
 *   invariant). Therefore the quirk test must run LAST: Catch2 executes
 *   test cases in declaration order, so keep it at the bottom of this file.
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

/** readChannel() value the algorithm perceives as `pct` percent of target */
float channelFor(int32_t target, float pct) {
    return pct * 65.0f * static_cast<float>(target);
}

void feed(BlowData& b, float channelValue) {
    Adafruit_HX711::testQueueRead(channelValue);
    b.ms = millis();  // keep the 5s stale-window branch from firing
    readPressure(&b);
}

/**
 * Restore a clean, self-consistent filter state: ten in-range zero readings
 * replace the whole window and drive `total` back to zero (valid only when
 * the state is consistent — i.e. this test did NOT run after the quirk test).
 */
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
    BlowData b = makeBlow();  // target 100

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

// ============================================================
// MUST STAY LAST — see the file header:
// the idle-timeout branch zeros the window but keeps the stale
// running total, permanently polluting every later real-sensor test.
// ============================================================

TEST_CASE("KNOWN QUIRK pressure: idle timeout zeros the window but keeps the stale running total", "[pressure]") {
    // After a pause of >5s the window is zeroed, but the running total is
    // NOT reset, so the first readings after the pause are inflated by up
    // to 10x the pre-pause average (takes 10 readings to wash out).
    resetFilter();
    BlowData b = makeBlow();

    for (int i = 0; i < 10; i++) feed(b, channelFor(100, 100));
    REQUIRE((b.pressure > 99.5f && b.pressure < 100.5f));  // total = 10*100

    // Let the 5s idle timeout expire: the stale branch fires when
    // blowData->ms + 4999 < millis(). (Cannot backdate b.ms — it is
    // unsigned and the test process is younger than the timeout.)
    b.ms = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(5200));

    Adafruit_HX711::testQueueRead(channelFor(100, 100));
    readPressure(&b);

    // The window was zeroed but `total` was NOT, so the new reading
    // replaces a zeroed slot: (10*100 + 100 - 0) / 10 = 110,
    // minus negligible tare drift.
    REQUIRE((b.pressure > 109.5f && b.pressure < 110.5f));
}
