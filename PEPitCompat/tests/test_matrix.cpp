/*
 * PEPitCompat — Unit tests: 2D vector/matrix math (src/hardware/matrix.cpp)
 *
 * Pure math, no shims involved — the cheapest tests in the suite.
 *
 * Conventions used by the firmware:
 *  - Matrix2D {a,b,c,d} is a column-vector matrix: [a b; c d]
 *    (multMV: x' = a*x + b*y, y' = c*x + d*y)
 *  - The int variants are Q8.8 fixed point (>> 8 after each product).
 */

#include "catch_amalgamated.hpp"

#include <cmath>

#include "matrix.h"

namespace {

const double HALF_PI = M_PI / 2.0;  // matrix.h pulls in Arduino.h -> <cmath>

// Absolute tolerance (margin), not relative — Approx's relative epsilon
// degenerates to zero for an expected value of 0, which would reject the
// ~1e-7 rounding of cos(pi/2).
Catch::Approx near(float expected) {
    return Catch::Approx(static_cast<double>(expected)).margin(1e-4);
}

}  // namespace

// ============================================================
// vectorFromAngle
// ============================================================

TEST_CASE("vectorFromAngle: angle 0 points along +x, scaled by length", "[matrix]") {
    Vector2D v{};
    vectorFromAngle(0.0f, 5.0f, &v);
    REQUIRE(v.x == near(5.0f));
    REQUIRE(v.y == near(0.0f));
}

TEST_CASE("vectorFromAngle: pi/2 points along +y", "[matrix]") {
    Vector2D v{};
    vectorFromAngle(static_cast<float>(HALF_PI), 1.0f, &v);
    REQUIRE(v.x == near(0.0f));
    REQUIRE(v.y == near(1.0f));
}

TEST_CASE("vectorFromAngle: two-arg overload returns the unit vector", "[matrix]") {
    Vector2D v{};
    vectorFromAngle(static_cast<float>(HALF_PI), &v);
    REQUIRE(v.x == near(0.0f));
    REQUIRE(v.y == near(1.0f));
}

// ============================================================
// dotProduct
// ============================================================

TEST_CASE("dotProduct: vector-vector", "[matrix]") {
    Vector2D a{1.0f, 2.0f};
    Vector2D b{3.0f, 4.0f};
    REQUIRE(dotProduct(&a, &b) == near(11.0f));
}

TEST_CASE("dotProduct: vector against an angle", "[matrix]") {
    Vector2D a{1.0f, 0.0f};
    REQUIRE(dotProduct(&a, 0.0f) == near(1.0f));
    Vector2D b{0.0f, 1.0f};
    REQUIRE(dotProduct(&b, static_cast<float>(HALF_PI)) == near(1.0f));
}

// ============================================================
// normalizeVector
// ============================================================

TEST_CASE("KNOWN QUIRK normalizeVector: scales the OUTPUT in place, caller must pre-copy input into it", "[matrix]") {
    // The function reads the input's length but multiplies OUTPUT, so
    // output must be a copy of input before the call; a zeroed output
    // stays zero. (Currently uncalled in the firmware — pinned here so a
    // future caller starts from the documented contract.)
    Vector2D in{3.0f, 4.0f};
    Vector2D pre{3.0f, 4.0f};  // pre-copy usage
    normalizeVector(&in, &pre);
    REQUIRE(pre.x == near(0.6f));
    REQUIRE(pre.y == near(0.8f));

    Vector2D zeroed{0.0f, 0.0f};
    normalizeVector(&in, &zeroed);
    REQUIRE(zeroed.x == 0.0f);
    REQUIRE(zeroed.y == 0.0f);
}

// ============================================================
// Float matrix ops
// ============================================================

TEST_CASE("multMV: matrix-vector product (column-vector convention)", "[matrix]") {
    Matrix2D m{1.0f, 2.0f, 3.0f, 4.0f};
    Vector2D v{1.0f, 1.0f};
    Vector2D out{};
    multMV(&m, &v, &out);
    REQUIRE(out.x == near(3.0f));  // 1*1 + 2*1
    REQUIRE(out.y == near(7.0f));  // 3*1 + 4*1
}

TEST_CASE("multMF: scalar matrix scale", "[matrix]") {
    Matrix2D m{1.0f, 2.0f, 3.0f, 4.0f};
    Matrix2D out{};
    multMF(&m, 2.0f, &out);
    REQUIRE(out.a == near(2.0f));
    REQUIRE(out.b == near(4.0f));
    REQUIRE(out.c == near(6.0f));
    REQUIRE(out.d == near(8.0f));
}

TEST_CASE("addVV / subVV: elementwise", "[matrix]") {
    Vector2D a{1.0f, 2.0f};
    Vector2D b{3.0f, 4.0f};
    Vector2D sum{}, diff{};
    addVV(&a, &b, &sum);
    subVV(&b, &a, &diff);
    REQUIRE(sum.x == near(4.0f));
    REQUIRE(sum.y == near(6.0f));
    REQUIRE(diff.x == near(2.0f));
    REQUIRE(diff.y == near(2.0f));
}

TEST_CASE("multMMF: composing scale-2 and rotate-90", "[matrix]") {
    Matrix2D scale2{2.0f, 0.0f, 0.0f, 2.0f};
    Matrix2D rot90{0.0f, -1.0f, 1.0f, 0.0f};
    Matrix2D out{};
    multMMF(&scale2, &rot90, &out);
    REQUIRE(out.a == near(0.0f));
    REQUIRE(out.b == near(-2.0f));
    REQUIRE(out.c == near(2.0f));
    REQUIRE(out.d == near(0.0f));
}

// ============================================================
// Int (Q8.8 fixed point) ops
// ============================================================

TEST_CASE("multMV (fixed point): Q8.8 products shifted by 8", "[matrix]") {
    // a=256(1.0), b=512(2.0), c=768(3.0), d=1024(4.0), vector (1,1)
    Matrix2Di m{256, 512, 768, 1024};
    Vector2Di v{1, 1};
    Vector2Di out{};
    multMV(&m, &v, &out);
    REQUIRE(out.x == (256 + 512) >> 8);  // 3
    REQUIRE(out.y == (768 + 1024) >> 8); // 7
    REQUIRE(out.x == 3);
    REQUIRE(out.y == 7);
}

TEST_CASE("multMF (fixed point): scale-2 times 2 is scale-4", "[matrix]") {
    Matrix2Di m{512, 0, 0, 512};  // scale 2 in Q8.8
    Matrix2Di out{};
    multMF(&m, 2, &out);
    REQUIRE(out.a == 4);  // (512*2) >> 8
    REQUIRE(out.b == 0);
    REQUIRE(out.c == 0);
    REQUIRE(out.d == 4);
}

TEST_CASE("addVV / subVV (fixed point): no shift, plain integer math", "[matrix]") {
    Vector2Di a{1, 2};
    Vector2Di b{3, 4};
    Vector2Di sum{}, diff{};
    addVV(&a, &b, &sum);
    subVV(&b, &a, &diff);
    REQUIRE(sum.x == 4);
    REQUIRE(sum.y == 6);
    REQUIRE(diff.x == 2);
    REQUIRE(diff.y == 2);
}

// ============================================================
// invertM
// ============================================================

TEST_CASE("invertM: inverse round-trips through multMV", "[matrix]") {
    Matrix2D m{2.0f, 3.0f, 4.0f, 5.0f};  // det = -2
    Matrix2D inv{};
    invertM(&m, &inv);

    // det = 2*5 - 3*4 = -2 -> inv = {-2.5, 1.5, 2, -1}
    REQUIRE(inv.a == near(-2.5f));
    REQUIRE(inv.b == near(1.5f));
    REQUIRE(inv.c == near(2.0f));
    REQUIRE(inv.d == near(-1.0f));

    // multMV reads both vector components of the input, so the input and
    // output must be distinct objects (aliased input/output would corrupt
    // the second component — not a firmware call pattern, just a trap).
    Vector2D v{3.0f, 4.0f};
    Vector2D mV{}, result{};
    multMV(&m, &v, &mV);          // mV = M * v = (18, 32)
    REQUIRE(mV.x == near(18.0f));
    REQUIRE(mV.y == near(32.0f));
    multMV(&inv, &mV, &result);   // result = M^-1 * mV = v
    REQUIRE(result.x == near(3.0f));
    REQUIRE(result.y == near(4.0f));
}

TEST_CASE("KNOWN QUIRK invertM: a singular matrix silently leaves the output unmodified", "[matrix]") {
    // det = 1*4 - 2*2 = 0 -> the function returns without touching *out
    // and reports no error; the caller is left with whatever was in *out.
    Matrix2D singular{1.0f, 2.0f, 2.0f, 4.0f};
    Matrix2D out{9.0f, 9.0f, 9.0f, 9.0f};
    invertM(&singular, &out);
    REQUIRE(out.a == 9.0f);
    REQUIRE(out.b == 9.0f);
    REQUIRE(out.c == 9.0f);
    REQUIRE(out.d == 9.0f);
}
