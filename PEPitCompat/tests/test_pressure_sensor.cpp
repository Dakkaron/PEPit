/*
 * PEPitCompat — Unit tests: pressure sensor filtering (src/hardware/pressuresensor.cpp)
 *
 * The tare algorithm runs in physical signed mbar: a scripted level of `mbar`
 * is queued as channel value mbar * 65000 (65000 HX711 counts per mbar).
 * The compensated display stays in percent-of-target:
 *   pct = compensatedMbar * 1000 / targetPressure   (100% = target/10 mbar)
 *
 * State inside readPressure() is static; tareReset() + tareSetTask() isolate
 * test cases. A fake clock advances 100ms per reading (10Hz) and stays far
 * ahead of real millis() so the 5s stale watchdog never fires — except in the
 * watchdog test, which deliberately uses the real clock.
 */

#include "catch_amalgamated.hpp"

#include <chrono>
#include <thread>

#include "pressuresensor.h"
#include "systemconfig.h"
#include "Adafruit_HX711.h"

namespace {

const uint32_t CLOCK0 = 10000000;  // fake clock base, far ahead of real millis()

BlowData makeBlow(int32_t target) {
    BlowData b{};
    b.ms = CLOCK0;
    b.blowStartMs = b.ms - 100;
    b.targetDurationMs = 10000;
    b.targetPressure = target;
    b.negativePressure = false;
    b.pressure = 0;
    b.cumulativeError = 0;
    return b;
}

void startTare(float inhFS, float pepFS) {
    systemConfig.simulateBlows = false;
    Adafruit_HX711::testClearQueuedReads();
    tareReset();
    tareSetTask(inhFS, pepFS);
}

struct Feeder {
    BlowData& b;
    uint32_t ms;
    Feeder(BlowData& blow, uint32_t start) : b(blow), ms(start) {}

    void step(float mbar) {
        Adafruit_HX711::testQueueRead(mbar * 65000.0f);
        b.ms = ms;
        ms += 100;
        readPressure(&b);
    }
    float compMbar() { return b.pressure * (float)b.targetPressure / 1000.0f; }
    float hold(float mbar, float seconds) {
        float peak = 0;
        int steps = (int)(seconds * 10);
        for (int i=0;i<steps;i++) { step(mbar); if (compMbar() > peak) peak = compMbar(); }
        return peak;
    }
    float restMax(float seconds, float skip) {
        float mx = 0;
        int steps = (int)(seconds * 10), k = (int)(skip * 10);
        for (int i=0;i<steps;i++) { step(0); if (i >= k && compMbar() > mx) mx = compMbar(); }
        return mx;
    }
};

}  // namespace

TEST_CASE("pressure: simulate mode follows the blow window", "[pressure]") {
    bool savedSim = systemConfig.simulateBlows;
    systemConfig.simulateBlows = true;
    Adafruit_HX711::testClearQueuedReads();

    BlowData b = makeBlow(200);
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

TEST_CASE("pressure: PEP press reaches target and rest re-zeroes", "[pressure]") {
    startTare(0.0f, 20.0f);  // pepLong target 200 -> full scale 20 mbar
    BlowData b = makeBlow(200);
    Feeder f(b, CLOCK0);

    f.hold(0, 5);
    REQUIRE(f.restMax(3, 1) < 0.5f);

    float press = f.hold(20, 5);
    INFO("press comp=" << press << " mbar (true 20)");
    REQUIRE((press > 19.0f && press < 21.0f));

    float rest = f.restMax(8, 1.5);
    INFO("rest comp=" << rest << " mbar");
    REQUIRE(rest < 0.5f);
}

TEST_CASE("pressure: inhalation dips read positive and rest re-zeroes", "[pressure]") {
    startTare(0.9f, 0.0f);  // inhalation target 9 -> full scale 0.9 mbar
    BlowData b = makeBlow(9);
    b.negativePressure = true;
    Feeder f(b, CLOCK0);

    f.hold(0, 5);
    REQUIRE(f.restMax(2, 1) < 0.3f);

    float inh = f.hold(-2, 10);  // -2 mbar suction -> +2 mbar compensation
    INFO("inhale comp=" << inh << " mbar (true 2)");
    REQUIRE((inh > 1.7f && inh < 2.5f));

    float rest = f.restMax(8, 1.5);
    INFO("rest comp=" << rest << " mbar");
    REQUIRE(rest < 0.4f);
}

TEST_CASE("pressure: inhalation+PEP alternating phases both stay accurate", "[pressure]") {
    startTare(3.0f, 17.0f);  // inhale target 30 / exhalation target 170
    BlowData b = makeBlow(30);
    b.negativePressure = true;
    Feeder f(b, CLOCK0);

    f.hold(0, 5);
    REQUIRE((pressureDeviationMbar() > -0.2f && pressureDeviationMbar() < 0.2f));
    float worstInh = 0, worstPep = 0;
    for (int r=0;r<3;r++) {
        float inh = f.hold(-2, 10);  // inhale 10s at -2 mbar
        if (inh > worstInh) worstInh = inh;
        REQUIRE(pressureDeviationMbar() < -1.5f);  // auto-direction latch input
        b.targetPressure = 170;
        b.negativePressure = false;
        float pep = f.hold(10, 5);   // PEP 5s at +10 mbar
        if (pep > worstPep) worstPep = pep;
        REQUIRE(pressureDeviationMbar() > 9.0f);
        b.targetPressure = 30;
        b.negativePressure = true;
    }
    INFO("inhale peak=" << worstInh << " pep peak=" << worstPep);
    REQUIRE((worstInh > 1.7f && worstInh < 2.5f));
    REQUIRE((worstPep > 9.5f && worstPep < 11.0f));

    float rest = f.restMax(30, 2);
    INFO("rest comp=" << rest << " mbar");
    REQUIRE(rest < 0.6f);

    b.targetPressure = 170;
    b.negativePressure = false;
    float press = f.hold(10, 5);
    INFO("press after rest=" << press << " mbar (true 10)");
    REQUIRE((press > 9.5f && press < 11.0f));
}

TEST_CASE("pressure: isolated garbage spikes never reach the dial", "[pressure]") {
    startTare(0.0f, 20.0f);
    BlowData b = makeBlow(200);
    Feeder f(b, CLOCK0);

    f.hold(0, 5);
    float spike = f.hold(40, 0.3f);  // 3 samples: below SPIKE_CONFIRM (4)
    INFO("spike excursion=" << spike << " mbar");
    REQUIRE(spike < 0.5f);
    REQUIRE(f.restMax(2, 0) < 0.5f);
}

TEST_CASE("pressure: readings beyond the +-60 mbar cutoff are skipped", "[pressure]") {
    startTare(0.0f, 20.0f);
    BlowData b = makeBlow(200);
    Feeder f(b, CLOCK0);

    REQUIRE(f.hold(70, 3) == 0.0f);
    REQUIRE(f.hold(-70, 2) == 0.0f);

    float press = f.hold(20, 5);  // the skipped garbage must not have moved the tare
    INFO("press after garbage=" << press << " mbar");
    REQUIRE((press > 19.0f && press < 21.0f));
    REQUIRE(f.restMax(8, 1.5) < 0.5f);
}

TEST_CASE("pressure: weak sub-threshold holds do not drag the tare", "[pressure]") {
    startTare(0.0f, 20.0f);  // pepTh = 0.35*20 = 7 mbar
    BlowData b = makeBlow(200);
    Feeder f(b, CLOCK0);

    for (int c=0;c<3;c++) {
        f.hold(2.2f, 6);  // weak 2.2 mbar hold: must not be classified as an action
        f.hold(0, 4);
    }
    float rest = f.restMax(8, 1.5);
    INFO("rest after weak holds=" << rest << " mbar");
    REQUIRE(rest < 0.6f);

    float press = f.hold(20, 5);
    INFO("press=" << press << " mbar (true 20)");
    REQUIRE((press > 19.0f && press < 21.0f));
}

TEST_CASE("pressure: slow baseline warm-up drift is absorbed, presses stay accurate", "[pressure]") {
    startTare(0.0f, 20.0f);
    BlowData b = makeBlow(200);
    Feeder f(b, CLOCK0);

    float maxCreep = 0;
    for (int i=0;i<600;i++) {  // 60s: baseline creeps 0 -> +1 mbar
        f.step(i / 600.0f);
        if (i >= 50 && f.compMbar() > maxCreep) maxCreep = f.compMbar();
    }
    INFO("max creep comp=" << maxCreep << " mbar");
    REQUIRE(maxCreep < 1.0f);

    float press = f.hold(21, 5);  // press 20 mbar above the drifted baseline
    INFO("press during creep=" << press << " mbar (true 20)");
    REQUIRE((press > 19.0f && press < 21.5f));
}

TEST_CASE("REGRESSION pressure: post-blow baseline step is re-zeroed, not stuck", "[pressure]") {
    startTare(0.0f, 20.0f);
    BlowData b = makeBlow(200);
    Feeder f(b, CLOCK0);

    f.hold(0, 4);
    f.hold(20, 6);
    f.hold(1, 15);  // sensor baseline steps +1 mbar after the blow and dwells

    INFO("rest after step=" << f.compMbar() << " mbar");
    REQUIRE(f.compMbar() < 0.6f);

    float press = f.hold(21, 5);  // real press 20 above the NEW baseline
    INFO("press after step=" << press << " mbar (true 20)");
    REQUIRE((press > 19.0f && press < 21.5f));
    REQUIRE(f.restMax(10, 1.5) < 0.6f);
}

TEST_CASE("pressure: cumulative error decays by 1/16 per reading at target", "[pressure]") {
    startTare(0.0f, 20.0f);
    BlowData b = makeBlow(200);
    Feeder f(b, CLOCK0);

    f.hold(20, 4);  // settled at 100% of target
    b.cumulativeError = 500;
    f.hold(20, 2);  // 20 readings at ~100%: pure 15/16 decay

    INFO("cumulativeError=" << b.cumulativeError);
    REQUIRE(b.cumulativeError > 60);
    REQUIRE(b.cumulativeError < 150);
}

TEST_CASE("pressure: cumulative error is untouched outside the target duration", "[pressure]") {
    startTare(0.0f, 20.0f);
    BlowData b = makeBlow(200);
    b.cumulativeError = 500;
    b.blowStartMs = b.ms - 20000;  // already past the 10s target duration
    Feeder f(b, CLOCK0);

    f.hold(20, 5);
    REQUIRE(b.cumulativeError == 500);
}

TEST_CASE("pressure: 5s of stale readings force a full filter reset", "[pressure]") {
    startTare(0.0f, 20.0f);
    BlowData b = makeBlow(200);

    // Real clock, real 100ms pacing (the watchdog compares against millis()).
    for (int i=0;i<12;i++) {
        Adafruit_HX711::testQueueRead(20 * 65000.0f);
        b.ms = millis();
        readPressure(&b);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE((b.pressure > 95.0f && b.pressure < 105.0f));

    std::this_thread::sleep_for(std::chrono::milliseconds(5200));

    Adafruit_HX711::testQueueRead(20 * 65000.0f);
    readPressure(&b);  // b.ms is stale -> full reset: the MA re-arms from zero
    INFO("first reading after reset=" << b.pressure);
    REQUIRE((b.pressure > 15.0f && b.pressure < 25.0f));

    for (int i=0;i<8;i++) {
        Adafruit_HX711::testQueueRead(20 * 65000.0f);
        b.ms = millis();
        readPressure(&b);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE((b.pressure > 95.0f && b.pressure < 105.0f));
}
