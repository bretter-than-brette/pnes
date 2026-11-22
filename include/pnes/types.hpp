#pragma once

#include <intsh>
using namespace br0::intsh;

template <typename t>
concept addable = requires (t a, t b) {
    a + b;
    a - b;
};

template <addable t>
struct vec2 {
    t x;
    t y;

    constexpr vec2 operator+(const vec2& rhs) const {
        return { static_cast<t>(x + rhs.x), static_cast<t>(y + rhs.y) };
    }

    constexpr vec2 operator-(const vec2& rhs) const {
        return { static_cast<t>(x - rhs.x), static_cast<t>(y - rhs.y) };
    }

    constexpr vec2& operator+=(const vec2& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    constexpr vec2& operator-=(const vec2& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }

    constexpr vec2& operator=(const vec2& rhs) = default;

    /// A volatile object cannot use the implicit copy assignment, which takes a
    /// plain `const vec2&`, so an `atomic vec2` needs this one spelt out.
    constexpr volatile vec2& operator=(const vec2& rhs) volatile {
        x = rhs.x;
        y = rhs.y;
        return *this;
    }
    constexpr volatile vec2& operator+=(const vec2& rhs) volatile {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    constexpr volatile vec2& operator-=(const vec2& rhs) volatile {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }

    template <addable u>
    constexpr operator vec2<u>() const {
        return { static_cast<u>(x), static_cast<u>(y) };
    }
};