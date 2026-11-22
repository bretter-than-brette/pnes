#pragma once

#include <pnes/extras/math.hpp>
#include <pnes/technology.hpp>
#include <intsh>
#include <type_traits>
#include <consh>

using namespace br0::intsh;
using namespace br0::consh;

/// Tiles bcd<T>() writes, terminator excluded: one per digit plus a sign column
/// if T is signed. Capacity is 2 * sizeof(T) digits, slightly less than T's
/// range -- a u16 reaches 65535 but prints four digits.
template <nx T>
inline constexpr u8 bcd_tiles = static_cast<u8>((sizeof(std::make_unsigned_t<T>) << 1) + std::is_signed_v<T>);

/// @p num as charmapped tiles, most significant digit first, then the charmap's
/// terminator -- so it can go straight to ui::text::Make with no length. The
/// caller owns the buffer and delete[]s it. @p Map is a template parameter
/// because a charmap is only ever constant-evaluated: as a runtime object its
/// fall-through to the undefined nes_str::unmapped would become a real call.
template <auto& Map, nx T>
[[nodiscard]] MINSIZE NI u8* bcd(const T num) {
    using store_t = std::make_unsigned_t<T>;

    constexpr auto BITS  = static_cast<u8>(sizeof(store_t) << 3);
    constexpr auto TOP   = static_cast<store_t>(1) << (BITS - 1);
    constexpr auto ONES  = static_cast<store_t>(~static_cast<store_t>(0)) / 0xf;  // 0x1111...
    constexpr auto THREE = static_cast<store_t>(ONES * 3);
    constexpr auto EIGHT = static_cast<store_t>(ONES * 8);

    constexpr u8 zero = Map('0');
    constexpr u8 term = Map('\0');

    // Double dabble, so no multiply or divide anywhere -- the 6502 has neither.
    // The per-nibble ">= 5" test runs for every digit at once: a nibble is >= 5
    // exactly when n + 3 sets its bit 3, which never carries out while all
    // digits are <= 9.
    auto    v      = static_cast<store_t>(abs<T>(num));
    store_t packed = 0;

    u8 bits = BITS;
    while (bits && !(v & TOP)) {
        v = static_cast<store_t>(v << 1);
        --bits;
    }

    for (; bits; --bits) {
        const auto c = static_cast<store_t>(((packed + THREE) & EIGHT) >> 3);
        packed = static_cast<store_t>(packed + c + (c << 1));

        packed = static_cast<store_t>(packed << 1) | ((v & TOP) != 0);
        v = static_cast<store_t>(v << 1);
    }

    const auto out = new u8[bcd_tiles<T> + 1];
    u8*        w   = out;

    if constexpr (std::is_signed_v<T>) {
        constexpr u8 minus = Map('-');
        constexpr u8 space = Map(' ');

        *w++ = (num < 0 && packed) ? minus : space;
    }

    const auto raw = reinterpret_cast<const u8*>(&packed);
    for (u8 i = static_cast<u8>(sizeof(store_t)); i--; ) {
        *w++ = static_cast<u8>(zero + (raw[i] >> 4));
        *w++ = static_cast<u8>(zero + (raw[i] & 0x0F));
    }

    *w = term;
    return out;
}
