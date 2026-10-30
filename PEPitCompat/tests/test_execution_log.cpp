/*
 * PEPitCompat — Unit tests: execution log CSV (/executionsLog.csv)
 *
 * Covers both directions of the log:
 *  - WRITE: logExecutionToSD() (new file gets the header row, later calls
 *    append; fields: profileName; executionDate; executionTime; successes;
 *    fails; duration)
 *  - READ:  the app's own readers — readFileToNewPSBuffer() (whole file,
 *    consumed by the execution-list UI) and readFileLineToString() — plus
 *    field-level round-trip checks of the date, time, results and duration.
 *
 * displayExecutionList() (the UI that renders the parsed lines) is an
 * interactive screen loop — deliberately not unit tested.
 *
 * NOTE: a failed open() cannot be induced on the shim (files always create),
 * so the write error branch is not covered.
 */

#include "catch_amalgamated.hpp"

#include <cstdlib>  // free() — the shim's heap_caps_malloc is malloc
#include <regex>
#include <string>
#include <vector>

#include "sdHandler.h"
#include "wifiHandler.h"
#include "test_helpers.h"

using namespace pepit_test;

namespace {

const char* EXEC_LOG_HEADER =
    "profileName;executionDate;executionTime;successes;fails;duration\n";

ProfileData namedProfile(const char* name) {
    ProfileData pd = ProfileData();
    pd.name = name;
    return pd;
}

size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    size_t count = 0, pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        count++;
        pos += needle.size();
    }
    return count;
}

std::vector<std::string> splitFields(const std::string& line, char sep) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        size_t pos = line.find(sep, start);
        fields.push_back(line.substr(start, pos == std::string::npos ? pos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return fields;
}

/** Split file content into lines (a trailing newline does not add an empty line). */
std::vector<std::string> splitLines(const std::string& content) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= content.size()) {
        size_t pos = content.find('\n', start);
        if (pos == std::string::npos) {
            if (start < content.size()) lines.push_back(content.substr(start));
            break;
        }
        lines.push_back(content.substr(start, pos - start));
        start = pos + 1;
    }
    return lines;
}

}  // namespace

// ============================================================
// Writing (logExecutionToSD)
// ============================================================

TEST_CASE("execution log write: a new file gets the header row plus the entry", "[logging]") {
    auto root = makeSdRoot("execlog-new");

    ProfileData pd = namedProfile("Testy");
    String err;
    logExecutionToSD(&pd, "2026-01-02", "03:04:05", 5, 2, 300000, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(readSdFile(root, "executionsLog.csv") ==
            std::string(EXEC_LOG_HEADER) + "Testy;2026-01-02;03:04:05;5;2;300000\n");
}

TEST_CASE("execution log write: an existing file only gets the row appended", "[logging]") {
    auto root = makeSdRoot("execlog-append");
    writeSdFile(root, "executionsLog.csv",
                std::string(EXEC_LOG_HEADER) + "Old;2026-01-01;00:00:00;1;0;1000\n");

    ProfileData pd = namedProfile("New");
    String err;
    logExecutionToSD(&pd, "2026-01-02", "03:04:05", 3, 1, 60000, &err);

    REQUIRE(err.isEmpty());
    std::string content = readSdFile(root, "executionsLog.csv");
    REQUIRE(countOccurrences(content, "profileName;") == 1);  // header written only for new files
    REQUIRE(content.find("New;2026-01-02;03:04:05;3;1;60000\n") != std::string::npos);
    REQUIRE(content.find("Old;2026-01-01;00:00:00;1;0;1000\n") != std::string::npos);
}

TEST_CASE("execution log write: multiple executions append in call order, all fields intact", "[logging]") {
    auto root = makeSdRoot("execlog-multi");
    String err;

    ProfileData pd1 = namedProfile("PEP S");
    logExecutionToSD(&pd1, "2026-01-02", "03:04:05", 10, 0, 240000, &err);
    ProfileData pd2 = namedProfile("RC Cornet+");
    logExecutionToSD(&pd2, "2026-01-03", "08:15:30",  6, 3, 180000, &err);
    ProfileData pd3 = namedProfile("PEP S");
    logExecutionToSD(&pd3, "2026-01-04", "19:45:12",  0, 5,  60000, &err);

    REQUIRE(err.isEmpty());  // nothing to report on success
    REQUIRE(readSdFile(root, "executionsLog.csv") ==
            std::string(EXEC_LOG_HEADER) +
            "PEP S;2026-01-02;03:04:05;10;0;240000\n"
            "RC Cornet+;2026-01-03;08:15:30;6;3;180000\n"
            "PEP S;2026-01-04;19:45:12;0;5;60000\n");
}

// ============================================================
// Reading (the app's own readers)
// ============================================================

TEST_CASE("execution log read: readFileToNewPSBuffer round-trips exactly what was written", "[logging]") {
    auto root = makeSdRoot("execlog-read");
    String err;

    ProfileData pd1 = namedProfile("PEP S");
    logExecutionToSD(&pd1, "2026-01-02", "03:04:05", 10, 0, 240000, &err);
    ProfileData pd2 = namedProfile("RC Cornet+");
    logExecutionToSD(&pd2, "2026-01-03", "08:15:30",  6, 3, 180000, &err);
    REQUIRE(err.isEmpty());

    // The app reads the whole file this way (physioProtocolHandler: the execution list)
    char* buffer = readFileToNewPSBuffer("/executionsLog.csv");
    REQUIRE(buffer != nullptr);

    std::string content(buffer);
    free(buffer);  // the shim's heap_caps_malloc is plain malloc

    REQUIRE(content ==
            std::string(EXEC_LOG_HEADER) +
            "PEP S;2026-01-02;03:04:05;10;0;240000\n"
            "RC Cornet+;2026-01-03;08:15:30;6;3;180000\n");

    // ...and a line count over that buffer sees header + 2 entries
    std::vector<std::string> lines = splitLines(content);
    REQUIRE(lines.size() == 3);
}

TEST_CASE("execution log read: readFileLineToString returns individual lines", "[logging]") {
    auto root = makeSdRoot("execlog-lines");
    String err;
    ProfileData pd1 = namedProfile("A");
    logExecutionToSD(&pd1, "2026-02-01", "07:08:09", 1, 2,  90000, &err);
    ProfileData pd2 = namedProfile("B");
    logExecutionToSD(&pd2, "2026-02-02", "10:11:12", 4, 0, 120000, &err);
    REQUIRE(err.isEmpty());

    REQUIRE(readFileLineToString("/executionsLog.csv", 0) ==
            "profileName;executionDate;executionTime;successes;fails;duration");
    REQUIRE(readFileLineToString("/executionsLog.csv", 1) == "A;2026-02-01;07:08:09;1;2;90000");
    REQUIRE(readFileLineToString("/executionsLog.csv", 2) == "B;2026-02-02;10:11:12;4;0;120000");
    REQUIRE(readFileLineToString("/executionsLog.csv", 99).isEmpty());  // past EOF

    // A missing file yields an empty string (open fails, function returns "")
    REQUIRE(readFileLineToString("/doesNotExist.csv", 0).isEmpty());
}

TEST_CASE("execution log read: date, time, results and duration parse back per row", "[logging]") {
    auto root = makeSdRoot("execlog-fields");
    String err;
    ProfileData pd1 = namedProfile("PEP S");
    logExecutionToSD(&pd1, "2026-03-01", "06:30:00", 12, 0, 300000, &err);
    ProfileData pd2 = namedProfile("RC Cornet+");
    logExecutionToSD(&pd2, "2026-03-02", "23:59:59",  0, 7,  90000, &err);
    REQUIRE(err.isEmpty());

    std::vector<std::string> lines = splitLines(readSdFile(root, "executionsLog.csv"));
    REQUIRE(lines.size() == 3);

    // Row 1: a fully successful execution
    std::vector<std::string> f = splitFields(lines[1], ';');
    REQUIRE(f.size() == 6);
    REQUIRE(f[0] == "PEP S");
    REQUIRE(f[1] == "2026-03-01");   // executionDate  (YYYY-MM-DD)
    REQUIRE(f[2] == "06:30:00");     // executionTime  (HH:MM:SS)
    REQUIRE(f[3] == "12");           // successes (ok)
    REQUIRE(f[4] == "0");            // fails
    REQUIRE(f[5] == "300000");       // duration (ms = 5 min)

    // Row 2: a fully failed execution, at a date boundary
    f = splitFields(lines[2], ';');
    REQUIRE(f.size() == 6);
    REQUIRE(f[0] == "RC Cornet+");
    REQUIRE(f[1] == "2026-03-02");
    REQUIRE(f[2] == "23:59:59");
    REQUIRE(f[3] == "0");
    REQUIRE(f[4] == "7");
    REQUIRE(f[5] == "90000");
}

TEST_CASE("execution log write: the date/time come out in the app's clock format", "[logging][time]") {
    auto root = makeSdRoot("execlog-clock");

    String date, time, err;
    getFormattedTime(&date, &time, &err);  // exercises the (faked/real) app clock
    REQUIRE(err.isEmpty());

    REQUIRE(std::regex_match(date.c_str(), std::regex("\\d{4}-\\d{2}-\\d{2}")));
    REQUIRE(std::regex_match(time.c_str(), std::regex("\\d{2}:\\d{2}")));

    ProfileData pd = namedProfile("ClockTest");
    logExecutionToSD(&pd, date.c_str(), time.c_str(), 1, 1, 42000, &err);
    REQUIRE(err.isEmpty());

    std::string content = readSdFile(root, "executionsLog.csv");
    std::vector<std::string> lines = splitLines(content);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[1] == std::string("ClockTest;") + date.c_str() + ";" + time.c_str() + ";1;1;42000");
}
