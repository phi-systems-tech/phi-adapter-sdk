#pragma once

// Canonical color contract for v1 adapters. Qt-free on purpose: converting
// between the canonical sRGB representation and a device's native color space
// (Hue/Zigbee xy, HSV, ...) is an adapter responsibility, so every adapter must
// be able to do it without pulling in Qt or phi-adapter-sdk-qt.
//
// The Qt wrapper (phi/adapter/qt/color.h in phi-adapter-sdk-qt) only adds the
// Qt meta-type registration on top of these types and functions.

#include <algorithm>
#include <cmath>

namespace phicore::adapter::v1 {

// ============================================================================
// Canonical color type
// ============================================================================

// Gamma-encoded sRGB color, components in [0, 1].
// This is the *one* canonical color representation used across phi.
// Brightness is a separate channel (ChannelKind::Brightness).
struct Color {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

[[nodiscard]] inline constexpr double clamp01(double v) noexcept
{
    if (v < 0.0)
        return 0.0;
    if (v > 1.0)
        return 1.0;
    return v;
}

[[nodiscard]] inline constexpr Color makeColor(double r, double g, double b) noexcept
{
    return Color{clamp01(r), clamp01(g), clamp01(b)};
}

[[nodiscard]] inline constexpr Color colorBlack() noexcept { return Color{0.0, 0.0, 0.0}; }
[[nodiscard]] inline constexpr Color colorWhite() noexcept { return Color{1.0, 1.0, 1.0}; }

/// Approximate relative luminance in [0, 1] (sRGB coefficients).
[[nodiscard]] inline constexpr double colorLuminance(const Color &c) noexcept
{
    return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

// ============================================================================
// HSV + xy helper structs (conversion only, never persisted)
// ============================================================================

struct Hsv {
    double hDeg = 0.0; // 0..360
    double s = 0.0;    // 0..1
    double v = 0.0;    // 0..1
};

struct Xy {
    double x = 0.0;
    double y = 0.0;
    double bri = 1.0; // used as Y in XYZ, 0..1
};

[[nodiscard]] inline double wrapHue360(double h) noexcept
{
    double r = std::fmod(h, 360.0);
    if (r < 0.0)
        r += 360.0;
    return r;
}

// ============================================================================
// Color temperature helpers (Kelvin <-> mired)
// ============================================================================
//
// phi-core uses mired (micro reciprocal Kelvin) as the canonical unit for
// ChannelKind::ColorTemperature in automations and internal logic. UI clients
// typically present Kelvin to users and convert at the boundary.
//
//   mired  = 1'000'000 / Kelvin
//   Kelvin = 1'000'000 / mired
//
// Common examples:
//   2700 K  ~ 370 mired  (warm white)
//   4000 K  ~ 250 mired  (neutral white)
//   6500 K  ~ 154 mired  (daylight)

[[nodiscard]] inline constexpr double kelvinToMired(double kelvin) noexcept
{
    return kelvin > 0.0 ? 1'000'000.0 / kelvin : 0.0;
}

[[nodiscard]] inline constexpr double miredToKelvin(double mired) noexcept
{
    return mired > 0.0 ? 1'000'000.0 / mired : 0.0;
}

// ============================================================================
// HSV <-> Color (sRGB gamma)
// ============================================================================

[[nodiscard]] inline Color hsvToColor(double hDeg, double s01, double v01)
{
    const double h = wrapHue360(hDeg);
    const double s = clamp01(s01);
    const double v = clamp01(v01);

    if (s <= 0.0)
        return makeColor(v, v, v);

    const double c = v * s;
    const double hPrime = h / 60.0;
    const double x = c * (1.0 - std::fabs(std::fmod(hPrime, 2.0) - 1.0));

    double r1 = 0.0;
    double g1 = 0.0;
    double b1 = 0.0;
    if (0.0 <= hPrime && hPrime < 1.0) {
        r1 = c; g1 = x; b1 = 0.0;
    } else if (1.0 <= hPrime && hPrime < 2.0) {
        r1 = x; g1 = c; b1 = 0.0;
    } else if (2.0 <= hPrime && hPrime < 3.0) {
        r1 = 0.0; g1 = c; b1 = x;
    } else if (3.0 <= hPrime && hPrime < 4.0) {
        r1 = 0.0; g1 = x; b1 = c;
    } else if (4.0 <= hPrime && hPrime < 5.0) {
        r1 = x; g1 = 0.0; b1 = c;
    } else { // 5.0 <= hPrime && hPrime < 6.0
        r1 = c; g1 = 0.0; b1 = x;
    }

    const double m = v - c;
    return makeColor(r1 + m, g1 + m, b1 + m);
}

[[nodiscard]] inline Hsv colorToHsv(const Color &c)
{
    Hsv out;
    const double r = clamp01(c.r);
    const double g = clamp01(c.g);
    const double b = clamp01(c.b);

    const double maxC = std::max(r, std::max(g, b));
    const double minC = std::min(r, std::min(g, b));
    const double delta = maxC - minC;

    out.v = maxC;

    if (delta <= 1e-9) {
        out.hDeg = 0.0;
        out.s = 0.0;
        return out;
    }

    out.s = delta / maxC;

    double h = 0.0;
    if (maxC == r)
        h = 60.0 * std::fmod(((g - b) / delta), 6.0);
    else if (maxC == g)
        h = 60.0 * (((b - r) / delta) + 2.0);
    else // maxC == b
        h = 60.0 * (((r - g) / delta) + 4.0);

    out.hDeg = wrapHue360(h);
    return out;
}

/// Alias for hue/saturation/brightness style call sites.
[[nodiscard]] inline Color colorFromHsB(double hDeg, double s01, double b01)
{
    return hsvToColor(hDeg, s01, b01);
}

// ============================================================================
// Gamma <-> linear + XYZ <-> xy
// ============================================================================

struct LinearRgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

[[nodiscard]] inline double srgbToLinear(double c) noexcept
{
    c = clamp01(c);
    if (c <= 0.04045)
        return c / 12.92;
    return std::pow((c + 0.055) / 1.055, 2.4);
}

[[nodiscard]] inline double linearToSrgb(double c) noexcept
{
    if (c <= 0.0)
        return 0.0;
    if (c >= 1.0)
        return 1.0;
    if (c <= 0.0031308)
        return 12.92 * c;
    return 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

[[nodiscard]] inline LinearRgb colorToLinear(const Color &c)
{
    LinearRgb out;
    out.r = srgbToLinear(c.r);
    out.g = srgbToLinear(c.g);
    out.b = srgbToLinear(c.b);
    return out;
}

[[nodiscard]] inline Color linearToColor(const LinearRgb &lin)
{
    Color c;
    c.r = clamp01(linearToSrgb(lin.r));
    c.g = clamp01(linearToSrgb(lin.g));
    c.b = clamp01(linearToSrgb(lin.b));
    return c;
}

inline void linearRgbToXyz(const LinearRgb &rgbLin, double &X, double &Y, double &Z) noexcept
{
    const double r = rgbLin.r;
    const double g = rgbLin.g;
    const double b = rgbLin.b;

    X = 0.4124 * r + 0.3576 * g + 0.1805 * b;
    Y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    Z = 0.0193 * r + 0.1192 * g + 0.9505 * b;
}

[[nodiscard]] inline LinearRgb xyzToLinearRgb(double X, double Y, double Z) noexcept
{
    LinearRgb rgb;
    rgb.r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
    rgb.g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
    rgb.b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;

    rgb.r = clamp01(rgb.r);
    rgb.g = clamp01(rgb.g);
    rgb.b = clamp01(rgb.b);
    return rgb;
}

// ============================================================================
// xy <-> Color (sRGB)
// ============================================================================

/// xy chromaticity + brightness (Y) -> gamma-encoded sRGB.
[[nodiscard]] inline Color colorFromXy(double x, double y, double bri01)
{
    Xy xy;
    xy.x = x;
    xy.y = y;
    xy.bri = clamp01(bri01);

    if (xy.y <= 1e-6) {
        const double v = xy.bri;
        return makeColor(v, v, v);
    }

    const double Y = xy.bri;
    const double X = (Y / xy.y) * xy.x;
    const double Z = (Y / xy.y) * (1.0 - xy.x - xy.y);

    const LinearRgb lin = xyzToLinearRgb(X, Y, Z);
    return linearToColor(lin);
}

/// Gamma-encoded sRGB -> xy chromaticity + derived brightness.
[[nodiscard]] inline Xy colorToXy(const Color &c)
{
    const LinearRgb lin = colorToLinear(c);

    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    linearRgbToXyz(lin, X, Y, Z);

    Xy xy;
    const double sum = X + Y + Z;
    if (sum <= 1e-9) {
        // D65 white point as a defined fallback for a black input.
        xy.x = 0.3127;
        xy.y = 0.3290;
        xy.bri = 0.0;
        return xy;
    }

    xy.x = X / sum;
    xy.y = Y / sum;
    xy.bri = clamp01(Y);
    return xy;
}

inline void colorToXy(const Color &c, double &outX, double &outY)
{
    const Xy xy = colorToXy(c);
    outX = xy.x;
    outY = xy.y;
}

} // namespace phicore::adapter::v1
