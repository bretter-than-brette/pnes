#pragma once

#include <intsh>
#include <limits>
#include <type_traits>

// Shared demo types.
#include <pnes/video.hpp>

using namespace br0::intsh;

namespace demo {

template <typename int_type, auto number>
class Framerated {
    static_assert(std::is_arithmetic_v<int_type>,
        "Framerated: int_type must be an arithmetic type");
    static_assert(std::is_convertible_v<decltype(number), int_type>,
        "Framerated: number must be (weakly) convertible to int_type");

    static constexpr long double raw        = static_cast<long double>(number);
    static constexpr long double scaledUp   = raw * 1.2L;
    static constexpr long double scaledDown = raw * (5.0L / 6.0L);

    static_assert(raw >= static_cast<long double>(std::numeric_limits<int_type>::lowest()) &&
                  raw <= static_cast<long double>(std::numeric_limits<int_type>::max()),
        "Framerated: number does not fit in int_type");
    static_assert(scaledUp >= static_cast<long double>(std::numeric_limits<int_type>::lowest()) &&
                  scaledUp <= static_cast<long double>(std::numeric_limits<int_type>::max()),
        "Framerated: number * 1.2 (50Hz scale-up) overflows/underflows int_type");
    static_assert(scaledDown >= static_cast<long double>(std::numeric_limits<int_type>::lowest()) &&
                  scaledDown <= static_cast<long double>(std::numeric_limits<int_type>::max()),
        "Framerated: number * 5/6 (50Hz scale-down) overflows/underflows int_type");

    static constexpr int_type up[2] = {
        static_cast<int_type>(raw), static_cast<int_type>(scaledUp)
    };
    static constexpr int_type down[2] = {
        static_cast<int_type>(raw), static_cast<int_type>(scaledDown)
    };

public:
    static int_type ScaleUp()   { return up[::video::Is50Hz()]; }
    static int_type ScaleDown() { return down[::video::Is50Hz()]; }
};

}
