#pragma once
#include "pnes/types.hpp"

/// A UI element's tile-space anchor and footprint as one value, which is what
/// uitk emits for every placeable node. One value rather than two, because that
/// is what lets a whole element's geometry be copied out of `atomic` storage in
/// one statement -- and `atomic` is volatile, which rules out the implicit copy
/// operations, hence the constructors below.
struct Rect {
    vec2<u16> pos;
    vec2<u8>  size;

    Rect() = default;
    constexpr Rect(const vec2<u16>& pos, const vec2<u8>& size) noexcept : pos(pos), size(size) {}
    constexpr Rect(const Rect&) = default;
    constexpr Rect& operator=(const Rect&) = default;

    Rect(const volatile Rect& other) noexcept
        : pos{other.pos.x, other.pos.y}, size{other.size.x, other.size.y} {}

    void operator=(const Rect& other) volatile noexcept {
        pos.x  = other.pos.x;
        pos.y  = other.pos.y;
        size.x = other.size.x;
        size.y = other.size.y;
    }

    constexpr bool Contains(const vec2<u16> &target) const noexcept {
        if (target.x < pos.x) return false;
        if (target.y < pos.y) return false;
        if (target.x >= pos.x + size.x) return false;
        if (target.y >= pos.y + size.y) return false;
        return true;
    }
};

/// One entry in a generated Canvas's component table. A Canvas is a focus ring
/// over the nodes nested in it, and moving focus needs both halves by index,
/// which is only known at runtime. Pointers rather than copies, which is what
/// keeps the table constexpr: a runtime-viewport node's rect is mutable storage,
/// but its ADDRESS is a constant everywhere.
///
/// Taking `func`'s address forces an out-of-line copy of a function uitk marks
/// always-inline, so a Canvas's components cost a call. That is the price of
/// indexing by a runtime value.
struct InvokeableComponant {
    const volatile Rect* rect;  ///< The component's own `rect` storage.
    void (*func)(u8);           ///< The component's own `Pass`.
};