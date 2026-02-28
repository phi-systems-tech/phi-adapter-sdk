#pragma once

#include <algorithm>

namespace phicore::adapter::v1 {

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

[[nodiscard]] inline constexpr double kelvinToMired(double kelvin) noexcept
{
    return kelvin > 0.0 ? 1000000.0 / kelvin : 0.0;
}

[[nodiscard]] inline constexpr double miredToKelvin(double mired) noexcept
{
    return mired > 0.0 ? 1000000.0 / mired : 0.0;
}

} // namespace phicore::adapter::v1
