/*
 * PEPitCompat — Unit tests: time handling (src/hardware/wifiHandler.cpp)
 *
 * Shim semantics: settimeofday() records an offset that getLocalTime()
 * applies (the host clock is never touched); pepit_compat_resetClockOffset()
 * restores real time.
 *
 * All cases repoint the SD root: getFormattedTime() writes a "lastDateTime"
 * blob into the "system" prefs of the current root.
 */

#include "catch_amalgamated.hpp"

#include <chrono>
#include <ctime>
#include <regex>
#include <string>
#include <thread>

#include "wifiHandler.h"
#include "test_helpers.h"

using namespace pepit_test;

namespace {

/** Reset the shim's time offset when the test exits. */
struct ClockOffsetGuard {
    ~ClockOffsetGuard() { pepit_compat_resetClockOffset(); }
};

}  // namespace

TEST_CASE("isTimeAvailable: true on the shim (getLocalTime never fails)", "[time]") {
    makeSdRoot("time-avail");
    REQUIRE(isTimeAvailable() == true);
}

TEST_CASE("getFormattedTime: produces 'YYYY-MM-DD' and 'HH:MM' from the real clock", "[time]") {
    makeSdRoot("time-format");  // keep the lastDateTime prefs write hermetic
    pepit_compat_resetClockOffset();

    String date, time, err;
    getFormattedTime(&date, &time, &err);

    REQUIRE(err.isEmpty());
    REQUIRE_FALSE(date == "N/A");
    REQUIRE_FALSE(time == "N/A");

    const std::string d = date.c_str();
    const std::string t = time.c_str();
    REQUIRE(std::regex_match(d, std::regex("\\d{4}-\\d{2}-\\d{2}")));
    REQUIRE(std::regex_match(t, std::regex("\\d{2}:\\d{2}")));

    // Plausible calendar values (the system clock must be a real date)
    int year = std::stoi(d.substr(0, 4));
    int month = std::stoi(d.substr(5, 2));
    int day = std::stoi(d.substr(8, 2));
    REQUIRE((year > 2000 && year < 2100));
    REQUIRE((month >= 1 && month <= 12));
    REQUIRE((day >= 1 && day <= 31));
}

TEST_CASE("setTimeFromString: app time reads return the faked time; the host clock is untouched", "[time]") {
    makeSdRoot("time-setstr");
    ClockOffsetGuard offset;

    const time_t hostBefore = time(nullptr);

    setTimeFromString("2026-05-06 07:08:09");

    String date, timeStr, err;
    getFormattedTime(&date, &timeStr, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(date == "2026-05-06");
    REQUIRE(timeStr == "07:08");

    const time_t hostAfter = time(nullptr);
    REQUIRE((hostAfter >= hostBefore && hostAfter <= hostBefore + 5));

    // the offset persists across reads
    String date2, time2, err2;
    getFormattedTime(&date2, &time2, &err2);
    REQUIRE(date2 == "2026-05-06");
}

TEST_CASE("setTime: the faked clock keeps running with real elapsed time", "[time]") {
    makeSdRoot("time-set");
    ClockOffsetGuard offset;

    setTime(2026, 5, 6, 7, 8, 58);

    String date, timeStr, err;
    getFormattedTime(&date, &timeStr, &err);
    REQUIRE(date == "2026-05-06");
    REQUIRE(timeStr == "07:08");  // 07:08:58 still formats as 07:08

    // Cross a minute boundary: the faked clock must advance with real time.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    getFormattedTime(&date, &timeStr, &err);
    REQUIRE(date == "2026-05-06");
    REQUIRE(timeStr == "07:09");
}
