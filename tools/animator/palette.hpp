/**
 * @file palette.hpp
 * @brief Named 4-colour NES palettes: one backdrop entry plus three colours,
 *        each an index into the console own 64-colour master palette.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace pal {

constexpr int kColoursPerPalette = 4;

constexpr int kNesColours = 64;

constexpr std::uint8_t kTransparencyColour = 0x0F;

inline constexpr std::array<std::uint32_t, kNesColours> kNesPalette = {
    0x656565, 0x002D69, 0x131F7F, 0x3C137C, 0x600B62, 0x730A37, 0x710F07, 0x5A1A00,
    0x342800, 0x0B3400, 0x003C00, 0x003D10, 0x003840, 0x000000, 0x000000, 0x000000,
    0xABABAB, 0x0057C6, 0x2E3CF0, 0x6C1BD0, 0x9C14A0, 0xB01260, 0xAE1B21, 0x8E3100,
    0x614A00, 0x305D00, 0x0E6900, 0x006B2A, 0x00655E, 0x000000, 0x000000, 0x000000,
    0xFFFFFF, 0x29A4FF, 0x7187FF, 0xB069FF, 0xE35BFF, 0xFF5CBE, 0xFF6569, 0xF7802C,
    0xBD9A00, 0x86AD00, 0x55BA2E, 0x2CBE6F, 0x1EBAB0, 0x4E4E4E, 0x000000, 0x000000,
    0xFFFFFF, 0xAADEFF, 0xC6CFFF, 0xDDC4FF, 0xF2BEFF, 0xFFC0E6, 0xFFC3C2, 0xFFCCA2,
    0xEDD98E, 0xD4E38E, 0xBEE8A2, 0xAFE9C4, 0xABE7E8, 0xB8B8B8, 0x000000, 0x000000,
};

struct Palette {
    std::string                name;
    std::array<std::uint8_t, 3> colours = {0x16, 0x27, 0x30};  ///< NES indices for colours 1-3.
};

[[nodiscard]] std::uint32_t rgbOf(std::uint8_t index);

[[nodiscard]] std::array<std::uint32_t, kColoursPerPalette> rgbOf(const Palette& palette);

[[nodiscard]] std::array<std::uint32_t, kColoursPerPalette> placeholderRgb();

} // namespace pal
