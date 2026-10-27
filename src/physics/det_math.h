#pragma once

// Sine, cosine, arctangent, exponential, logarithm and powers from + - x /,
// square roots and exact power-of-two scaling only, which IEEE 754 rounds the
// same on every machine (with -ffp-contract=off), unlike the C library's,
// whose last bits differ between platforms. Everything a shot's result
// depends on uses these, so the server's WASM replays a shot bit for bit.
// Worked in double, so each is within a float's rounding of the true value.

float det_sin(float x);
float det_cos(float x);
// atan2(y, x) in [-pi, pi], as std::atan2.
float det_atan2(float y, float x);
float det_exp(float x);
// Natural logarithm of x > 0 (-infinity at 0, NaN below).
float det_log(float x);
// x^y for x > 0, 1 for y = 0, 0 for x = 0 and y > 0.
float det_pow(float x, float y);
// `base` (0..1) to the power `exponent` (0 or more) by multiplications and
// square roots: exactly as many as the exponent's binary digits need.
float exact_power(float base, float exponent);
