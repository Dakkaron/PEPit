/*
 * PEPitCompat — Unit tests: manometer-mode display decisions
 * (src/physioProtocolHandler.cpp)
 *
 * Covers the Inhalation+PEP auto-direction latch (dial orientation follows
 * the breath sign with a 0.5 mbar deadband instead of the counted-blow
 * alternation) and the display-only dial sign/calibration used by the needle.
 * Rendering itself (drawManometerModeDisplay) is file-static and not covered.
 */

#include "catch_amalgamated.hpp"

#include "physioProtocolHandler.h"
#include "constants.h"

namespace {

Catch::Approx near(float expected) {
    return Catch::Approx(static_cast<double>(expected)).margin(1e-3);
}

BlowData makeBlow(uint32_t taskType, float pressurePct, int32_t target, bool negative) {
    BlowData b{};
    b.taskType = taskType;
    b.pressure = pressurePct;
    b.targetPressure = target;
    b.negativePressure = negative;
    return b;
}

}  // namespace

TEST_CASE("manometer auto-direction: 0.5 mbar deadband latch", "[manometer]") {
    manometerAutoDirectionReset();
    REQUIRE(!manometerAutoDirectionIsPep());

    // Rest and sub-deadband noise must not flip the latch.
    manometerAutoDirectionUpdate(0.0f);
    manometerAutoDirectionUpdate(0.49f);
    manometerAutoDirectionUpdate(0.5f);    // boundary: strictly greater flips
    manometerAutoDirectionUpdate(-0.49f);
    REQUIRE(!manometerAutoDirectionIsPep());

    manometerAutoDirectionUpdate(0.51f);
    REQUIRE(manometerAutoDirectionIsPep());

    // Latched pep mode survives the deadband while pressure subsides.
    manometerAutoDirectionUpdate(0.3f);
    manometerAutoDirectionUpdate(0.0f);
    manometerAutoDirectionUpdate(-0.49f);
    REQUIRE(manometerAutoDirectionIsPep());

    manometerAutoDirectionUpdate(-0.51f);
    REQUIRE(!manometerAutoDirectionIsPep());

    // Latched inhale mode survives the deadband.
    manometerAutoDirectionUpdate(0.49f);
    REQUIRE(!manometerAutoDirectionIsPep());
}

TEST_CASE("manometer auto-direction: full inhalepep breathing sequence", "[manometer]") {
    manometerAutoDirectionReset();

    manometerAutoDirectionUpdate(-2.0f);   // inhale  -> inhale mode (initial state confirmed)
    REQUIRE(!manometerAutoDirectionIsPep());
    manometerAutoDirectionUpdate(0.0f);    // release through deadband: stays
    REQUIRE(!manometerAutoDirectionIsPep());
    manometerAutoDirectionUpdate(13.0f);   // press   -> pep mode
    REQUIRE(manometerAutoDirectionIsPep());
    manometerAutoDirectionUpdate(0.2f);    // release through deadband: stays pep
    REQUIRE(manometerAutoDirectionIsPep());
    manometerAutoDirectionUpdate(-2.0f);   // inhale  -> inhale mode again
    REQUIRE(!manometerAutoDirectionIsPep());
}

TEST_CASE("manometer dial: calibrated mbar, negative only for Inhalation+PEP suction", "[manometer]") {
    // Inhalation+PEP inhale phase: 66.7% of target 30 -> -2.001 mbar.
    BlowData inh = makeBlow(PROFILE_TASK_TYPE_INHALATIONPEP, 66.7f, 30, true);
    REQUIRE(manometerDialMbar(inh) == near(-2.001f));

    // Inhalation+PEP pep phase: 76.5% of target 170 -> +13.0 mbar.
    BlowData pep = makeBlow(PROFILE_TASK_TYPE_INHALATIONPEP, 76.5f, 170, false);
    REQUIRE(manometerDialMbar(pep) == near(13.005f));

    // PEP tasks never flip sign, even if negativePressure were set.
    BlowData pepTask = makeBlow(PROFILE_TASK_TYPE_LONGBLOWS, 75.0f, 200, true);
    REQUIRE(manometerDialMbar(pepTask) == near(15.0f));

    // Inhalation-only tasks keep the dial positive.
    BlowData inhTask = makeBlow(PROFILE_TASK_TYPE_INHALATION, 222.0f, 9, true);
    REQUIRE(manometerDialMbar(inhTask) == near(1.998f));

    // Task value untouched by the display conversion.
    REQUIRE(inh.pressure == near(66.7f));
}
