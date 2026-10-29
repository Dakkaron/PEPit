/*
 * PEPitCompat — Unit tests: INI parsing (src/hardware/sdHandler.cpp)
 *
 * These tests pin the CURRENT behavior of the hand-rolled INI parser,
 * including its quirks. If a quirk looks like a bug, the test documents it
 * with "KNOWN QUIRK" — fixing the behavior is a firmware decision.
 *
 * NOTE: paths that call checkFailWithMessage() (infinite loop on device)
 * must never be triggered from tests.
 */

#include "catch_amalgamated.hpp"

#include <cstring>

#include "sdHandler.h"
#include "test_helpers.h"

using namespace pepit_test;

namespace {

const char* BASIC_INI =
    "# leading comment\n"
    "[alpha]\n"
    "a = 1\n"
    "b = 2\n"
    "\n"
    "# comment inside alpha\n"
    "c = 3\n"
    "[beta]\n"
    "d = 4\n";

}  // namespace

// ============================================================
// getIniValueFromSection (pure string parsing, no filesystem)
// ============================================================

TEST_CASE("getIniValueFromSection: simple 'key = value'", "[ini]") {
    char section[] = "key = value\nother = 2\n";
    String out, err;
    getIniValueFromSection(section, "key", &out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(out == "value");
}

TEST_CASE("getIniValueFromSection: tabs around '='", "[ini]") {
    char section[] = "key\t=\tvalue\n";
    String out, err;
    getIniValueFromSection(section, "key", &out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(out == "value");
}

TEST_CASE("getIniValueFromSection: no spaces around '='", "[ini]") {
    char section[] = "key=value\n";
    String out, err;
    getIniValueFromSection(section, "key", &out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(out == "value");
}

TEST_CASE("getIniValueFromSection: leading spaces of the value are trimmed", "[ini]") {
    char section[] = "key =   value\n";
    String out, err;
    getIniValueFromSection(section, "key", &out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(out == "value");
}

TEST_CASE("getIniValueFromSection: trailing spaces of the value are kept", "[ini]") {
    char section[] = "key = value  \n";
    String out, err;
    getIniValueFromSection(section, "key", &out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(out == "value  ");
}

TEST_CASE("getIniValueFromSection: empty value yields empty string, no error", "[ini]") {
    char section[] = "key =\n";
    String out("preset"), err;
    getIniValueFromSection(section, "key", &out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(out.isEmpty());
}

TEST_CASE("getIniValueFromSection: missing key sets error and clears output", "[ini]") {
    char section[] = "a = 1\nb = 2\n";
    String out("preset"), err;
    getIniValueFromSection(section, "c", &out, &err);
    REQUIRE(out.isEmpty());
    REQUIRE(err == "Key c not found in INI section.\n");
}

TEST_CASE("getIniValueFromSection: first occurrence of a key wins", "[ini]") {
    char section[] = "k = 1\nk = 2\n";
    String out, err;
    getIniValueFromSection(section, "k", &out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(out == "1");
}

TEST_CASE("REGRESSION getIniValueFromSection: key must not match a longer key sharing its prefix", "[ini]") {
    // The key is matched with an exact length check + strncmp, so a line
    // whose key merely starts with the searched key must NOT match
    // ("wifiSSID" must not match a "wifiSSID2 = ..." line).
    char section[] = "wifiSSID2 = foo\n";
    String out, err;
    getIniValueFromSection(section, "wifiSSID", &out, &err);
    REQUIRE(err == "Key wifiSSID not found in INI section.\n");
    REQUIRE(out.isEmpty());
}

TEST_CASE("KNOWN QUIRK getIniValueFromSection: CRLF input keeps the CR in the value", "[ini]") {
    // getIniSection() normalizes CRLF when building the section buffer, but a
    // CRLF string passed directly to getIniValueFromSection keeps the \r.
    char section[] = "key = value\r\n";
    String out, err;
    getIniValueFromSection(section, "key", &out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(out == "value\r");
}

TEST_CASE("getIniValueFromSection: skips non-matching and comment lines", "[ini]") {
    char section[] = "# comment\nx = y\ntarget = t\n";
    String out, err;
    getIniValueFromSection(section, "target", &out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(out == "t");
}

// ============================================================
// getBoolIniValueFromSection / stringIsTrue semantics
// ============================================================

TEST_CASE("getBoolIniValueFromSection: 'true' and '1' are true (any case, trimmed)", "[ini]") {
    String out;
    String err;
    char s1[] = "k = TRUE\n";
    REQUIRE(getBoolIniValueFromSection(s1, "k", &err, false) == true);
    REQUIRE(err.isEmpty());
    char s2[] = "k =  1  \n";
    REQUIRE(getBoolIniValueFromSection(s2, "k", &err, false) == true);
    char s3[] = "k = False\n";
    REQUIRE(getBoolIniValueFromSection(s3, "k", &err, false) == false);
    char s4[] = "k = 0\n";
    REQUIRE(getBoolIniValueFromSection(s4, "k", &err, false) == false);
}

TEST_CASE("KNOWN QUIRK getBoolIniValueFromSection: garbage with default=true is true", "[ini]") {
    // stringIsTrue(str, def=true) returns "str != false && str != 0", i.e.
    // ANY value (including garbage) is true when the default is true.
    char s[] = "k = banana\n";
    String err;
    REQUIRE(getBoolIniValueFromSection(s, "k", &err, true) == true);
    REQUIRE(getBoolIniValueFromSection(s, "k", &err, false) == false);
}

TEST_CASE("getBoolIniValueFromSection: missing key returns default and sets error", "[ini]") {
    char s[] = "other = 1\n";
    String err;
    REQUIRE(getBoolIniValueFromSection(s, "k", &err, true) == true);
    REQUIRE_FALSE(err.isEmpty());
}

// ============================================================
// getIntIniValueFromSection
// ============================================================

TEST_CASE("getIntIniValueFromSection: parses numbers, tolerates whitespace", "[ini]") {
    String err;
    char s1[] = "k = 42\n";
    REQUIRE(getIntIniValueFromSection(s1, "k", &err, 0) == 42);
    char s2[] = "k =  -7  \n";
    REQUIRE(getIntIniValueFromSection(s2, "k", &err, 0) == -7);
    char s3[] = "k = 12abc\n";
    REQUIRE(getIntIniValueFromSection(s3, "k", &err, 0) == 12);  // atoi() stops at 'a'
}

TEST_CASE("KNOWN QUIRK getIntIniValueFromSection: non-numeric value yields 0, not the default", "[ini]") {
    char s[] = "k = abc\n";
    String err;
    REQUIRE(getIntIniValueFromSection(s, "k", &err, 99) == 0);
}

TEST_CASE("getIntIniValueFromSection: missing key returns the default", "[ini]") {
    char s[] = "other = 1\n";
    String err;
    REQUIRE(getIntIniValueFromSection(s, "k", &err, 99) == 99);
    REQUIRE_FALSE(err.isEmpty());
}

// ============================================================
// isKeyInSection
// ============================================================

TEST_CASE("isKeyInSection: present and absent keys", "[ini]") {
    char section[] = "a = 1\nb = 2\n";
    REQUIRE(isKeyInSection(section, "a") == true);
    REQUIRE(isKeyInSection(section, "b") == true);
    REQUIRE(isKeyInSection(section, "c") == false);
}

// ============================================================
// getIniSection (file-based section extraction)
// ============================================================

TEST_CASE("getIniSection: extracts the section, drops comments, keeps blank lines", "[ini]") {
    auto root = makeSdRoot("getini");
    writeSdFile(root, "basic.ini", BASIC_INI);

    char buffer[1024];
    memset(buffer, 0xAA, sizeof(buffer));  // detect unwritten regions
    String err;

    getIniSection("/basic.ini", "[alpha]", buffer, sizeof(buffer), &err);

    REQUIRE(err.isEmpty());
    REQUIRE(std::string(buffer) == "a = 1\nb = 2\n\nc = 3\n");
}

TEST_CASE("getIniSection: section name works with and without brackets", "[ini]") {
    auto root = makeSdRoot("getini-nobrackets");
    writeSdFile(root, "basic.ini", BASIC_INI);

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    String err;

    getIniSection("/basic.ini", "beta", buffer, sizeof(buffer), &err);

    REQUIRE(err.isEmpty());
    REQUIRE(std::string(buffer) == "d = 4\n");
}

TEST_CASE("getIniSection: missing section sets a detailed error", "[ini]") {
    auto root = makeSdRoot("getini-missing");
    writeSdFile(root, "basic.ini", BASIC_INI);

    char buffer[1024];
    String err;

    getIniSection("/basic.ini", "[gamma]", buffer, sizeof(buffer), &err);

    // The error message uses the virtual path as given (with leading slash).
    REQUIRE(err == "Section [gamma]\n not found in INI file /basic.ini\n");
}

TEST_CASE("KNOWN QUIRK getIniSection: CRLF input yields extra blank lines", "[ini]") {
    // Both \r and \n terminate a line, so every CRLF line is followed by a
    // (spurious) empty line in the extracted section buffer.
    auto root = makeSdRoot("getini-crlf");
    writeSdFile(root, "crlf.ini", "[alpha]\r\nx = 1\r\n[beta]\r\ny = 2\r\n");

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    String err;

    getIniSection("/crlf.ini", "[alpha]", buffer, sizeof(buffer), &err);

    REQUIRE(err.isEmpty());
    REQUIRE(std::string(buffer) == "\nx = 1\n\n");
}

TEST_CASE("KNOWN QUIRK getIniSection: final line without trailing newline is silently dropped", "[ini]") {
    // The reader loop is driven by file.available(); once the last character
    // has been read, available() is false and the in-flight line (with the
    // \n that the code appends) is never copied. No error is reported.
    auto root = makeSdRoot("getini-notrail");
    writeSdFile(root, "notrail.ini", "[alpha]\na = 1");

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    String err;

    getIniSection("/notrail.ini", "[alpha]", buffer, sizeof(buffer), &err);

    REQUIRE(err.isEmpty());
    REQUIRE(std::string(buffer).empty());
}

TEST_CASE("getIniSection: a section repeated later is not appended (first block wins)", "[ini]") {
    auto root = makeSdRoot("getini-dup");
    writeSdFile(root, "dup.ini", "[alpha]\nx = 1\n[alpha]\ny = 2\n");

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    String err;

    getIniSection("/dup.ini", "[alpha]", buffer, sizeof(buffer), &err);

    REQUIRE(err.isEmpty());
    REQUIRE(std::string(buffer) == "x = 1\n");
}

TEST_CASE("getIniSection: nonexistent file sets an open error", "[ini]") {
    auto root = makeSdRoot("getini-noexist");

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    String err;

    getIniSection("/missing.ini", "[alpha]", buffer, sizeof(buffer), &err);

    REQUIRE(err.indexOf("Failed to open INI file") != -1);
}

// NOTE — known sharp edges deliberately NOT exercised here (undefined
// behavior on the device, so a test would crash the suite instead of
// pinning behavior):
//  * lines longer than INI_LINE_BUFFER_LEN (512) overflow lineBuffer[] in
//    getIniSection() (char-by-char accumulation without a length check)
//  * sections longer than the caller's resultBuffer overflow it (strcpy
//    without a length check)
//  * callers read the result buffer before checking errorMessage (the buffer
//    is not cleared on error)
