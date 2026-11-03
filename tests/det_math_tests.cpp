#include "doctest.h"

#include "physics/det_math.h"

#include <cmath>
#include <limits>

namespace {
// Within a few float roundings of the C library's value.
bool close(const float ours, const double theirs) {
    const double tolerance = 4.0 * std::numeric_limits<float>::epsilon() * std::max(1.0, std::fabs(theirs));
    return std::fabs(static_cast<double>(ours) - theirs) <= tolerance;
}
}

TEST_CASE("deterministic trigonometry matches the C library's to a float's rounding") {
    int checked = 0;
    for (double x = -100.0; x <= 100.0; x += 0.0137) {
        const float f = static_cast<float>(x);
        CHECK(close(det_sin(f), std::sin(static_cast<double>(f))));
        CHECK(close(det_cos(f), std::cos(static_cast<double>(f))));
        ++checked;
    }
    CHECK(checked > 10000);
    for (double y = -5.0; y <= 5.0; y += 0.173) {
        for (double x = -5.0; x <= 5.0; x += 0.191) {
            const float fy = static_cast<float>(y);
            const float fx = static_cast<float>(x);
            CHECK(close(det_atan2(fy, fx), std::atan2(static_cast<double>(fy), static_cast<double>(fx))));
        }
    }
    CHECK(det_atan2(0.0f, 0.0f) == 0.0f);
    CHECK(close(det_atan2(1.0f, 0.0f), std::atan2(1.0, 0.0)));
    CHECK(close(det_atan2(-1.0f, 0.0f), std::atan2(-1.0, 0.0)));
    CHECK(close(det_atan2(0.0f, -1.0f), std::atan2(0.0, -1.0)));
}

TEST_CASE("deterministic exponentials, logarithms and powers match the C library's to a float's rounding") {
    for (double x = -60.0; x <= 60.0; x += 0.0311) {
        const float f = static_cast<float>(x);
        CHECK(close(det_exp(f), std::exp(static_cast<double>(f))));
    }
    for (double x = 1e-6; x <= 1e6; x *= 1.017) {
        const float f = static_cast<float>(x);
        CHECK(close(det_log(f), std::log(static_cast<double>(f))));
    }
    CHECK(det_log(1.0f) == 0.0f);
    CHECK(std::isinf(det_log(0.0f)));
    CHECK(std::isnan(det_log(-1.0f)));
    for (double x = 0.05; x <= 50.0; x *= 1.31) {
        for (double y = -4.0; y <= 6.0; y += 0.37) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            const double theirs = std::pow(static_cast<double>(fx), static_cast<double>(fy));
            const double tolerance = 16.0 * std::numeric_limits<float>::epsilon() * std::max(1.0, std::fabs(theirs));
            CHECK(std::fabs(static_cast<double>(det_pow(fx, fy)) - theirs) <= tolerance);
        }
    }
    CHECK(det_pow(7.0f, 0.0f) == 1.0f);
    CHECK(det_pow(0.0f, 2.0f) == 0.0f);
    // The barometric exponent the air's density takes.
    CHECK(close(det_pow(0.99f, 5.2559f), std::pow(static_cast<double>(0.99f), static_cast<double>(5.2559f))));
    CHECK(close(exact_power(0.5f, 2.5f), std::pow(0.5, 2.5)));
}
