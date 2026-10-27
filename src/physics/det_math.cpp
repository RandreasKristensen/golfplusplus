#include "physics/det_math.h"

#include <cmath>
#include <limits>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double half_pi = 1.57079632679489661923;
// pi / 2 in three parts of 33 bits each (fdlibm's), whose products with
// whole numbers below 2^20 are exact (Cody and Waite), so reducing an
// angle loses nothing.
constexpr double half_pi_1 = 1.57079632673412561417e+00;
constexpr double half_pi_2 = 6.07710050630396597660e-11;
constexpr double half_pi_3 = 2.02226624871116645580e-21;
constexpr double ln2 = 0.69314718055994530942;
constexpr double sqrt3 = 1.73205080756887729353;
constexpr double tan_pi_12 = 0.26794919243112270647;  // 2 - sqrt(3)
constexpr int power_fraction_bits = 20;

// Taylor series about 0 for |r| <= pi / 4: past their last term the next is
// below a double's last digit.
double sin_series(const double r) {
    const double r2 = r * r;
    double term = r;
    double sum = r;
    for (int n = 1; n <= 9; ++n) {
        term *= -r2 / static_cast<double>((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

double cos_series(const double r) {
    const double r2 = r * r;
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n <= 9; ++n) {
        term *= -r2 / static_cast<double>((2 * n - 1) * (2 * n));
        sum += term;
    }
    return sum;
}

// x into r in [-pi/4, pi/4] and the quarter turn it was in.
double reduce(const double x, int& quadrant) {
    const double k = std::nearbyint(x / half_pi);
    quadrant = static_cast<int>(static_cast<long long>(k) & 3);
    return ((x - k * half_pi_1) - k * half_pi_2) - k * half_pi_3;
}

// atan for 0 <= x <= tan(pi/12): its Taylor series.
double atan_series(const double x) {
    const double x2 = x * x;
    double power = x;
    double sum = x;
    for (int n = 1; n <= 14; ++n) {
        power *= -x2;
        sum += power / static_cast<double>(2 * n + 1);
    }
    return sum;
}

// atan for x >= 0.
double atan_positive(const double x) {
    if (x > 1.0) {
        return half_pi - atan_positive(1.0 / x);
    }
    if (x > tan_pi_12) {
        // atan(x) = pi/6 + atan((x sqrt3 - 1) / (x + sqrt3)).
        return pi / 6.0 + atan_series((x * sqrt3 - 1.0) / (x + sqrt3));
    }
    return atan_series(x);
}

double exp_double(const double x) {
    if (x > 709.0) {
        return std::numeric_limits<double>::infinity();
    }
    if (x < -745.0) {
        return 0.0;
    }
    const double k = std::nearbyint(x / ln2);
    const double r = x - k * ln2;
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n <= 16; ++n) {
        term *= r / static_cast<double>(n);
        sum += term;
    }
    return std::ldexp(sum, static_cast<int>(k));
}

double log_double(const double x) {
    if (x < 0.0 || std::isnan(x)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (x == 0.0) {
        return -std::numeric_limits<double>::infinity();
    }
    int exponent = 0;
    double mantissa = std::frexp(x, &exponent);  // [0.5, 1)
    if (mantissa < 0.70710678118654752440) {
        mantissa *= 2.0;
        --exponent;
    }
    // log(m) = 2 atanh(s), s = (m - 1) / (m + 1), |s| < 0.172.
    const double s = (mantissa - 1.0) / (mantissa + 1.0);
    const double s2 = s * s;
    double power = s;
    double sum = s;
    for (int n = 1; n <= 12; ++n) {
        power *= s2;
        sum += power / static_cast<double>(2 * n + 1);
    }
    return 2.0 * sum + static_cast<double>(exponent) * ln2;
}
}

float det_sin(const float x) {
    int quadrant = 0;
    const double r = reduce(static_cast<double>(x), quadrant);
    switch (quadrant) {
    case 0:
        return static_cast<float>(sin_series(r));
    case 1:
        return static_cast<float>(cos_series(r));
    case 2:
        return static_cast<float>(-sin_series(r));
    default:
        return static_cast<float>(-cos_series(r));
    }
}

float det_cos(const float x) {
    int quadrant = 0;
    const double r = reduce(static_cast<double>(x), quadrant);
    switch (quadrant) {
    case 0:
        return static_cast<float>(cos_series(r));
    case 1:
        return static_cast<float>(-sin_series(r));
    case 2:
        return static_cast<float>(-cos_series(r));
    default:
        return static_cast<float>(sin_series(r));
    }
}

float det_atan2(const float y, const float x) {
    const double dy = static_cast<double>(y);
    const double dx = static_cast<double>(x);
    if (dx == 0.0 && dy == 0.0) {
        return 0.0f;
    }
    if (dx == 0.0) {
        return static_cast<float>(dy > 0.0 ? half_pi : -half_pi);
    }
    const double base = atan_positive(std::fabs(dy / dx));
    double angle = dx > 0.0 ? base : pi - base;
    if (dy < 0.0) {
        angle = -angle;
    }
    return static_cast<float>(angle);
}

float det_exp(const float x) {
    return static_cast<float>(exp_double(static_cast<double>(x)));
}

float det_log(const float x) {
    return static_cast<float>(log_double(static_cast<double>(x)));
}

float det_pow(const float x, const float y) {
    if (y == 0.0f) {
        return 1.0f;
    }
    if (x == 0.0f) {
        return 0.0f;
    }
    return static_cast<float>(exp_double(static_cast<double>(y) * log_double(static_cast<double>(x))));
}

float exact_power(const float base, const float exponent) {
    const float whole = std::floor(exponent);
    float result = 1.0f;
    for (int i = 0; i < static_cast<int>(whole); ++i) {
        result *= base;
    }
    float fraction = exponent - whole;
    float root = base;
    for (int bit = 0; bit < power_fraction_bits && fraction > 0.0f; ++bit) {
        root = std::sqrt(root);
        fraction *= 2.0f;
        if (fraction >= 1.0f) {
            result *= root;
            fraction -= 1.0f;
        }
    }
    return result;
}
