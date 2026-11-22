#pragma once

#include <pnes/technology.hpp>
#include "../../chr/chr.h"
#include "windows.hpp"
// Default character map: source character -> CHR tile byte. One rule per
// contiguous run, which is all the font is. Being inline constexpr and only ever
// constant-evaluated, any number of TUs can include it and it emits nothing.
//
// '\0' gets its OWN tile rather than sharing the space tile: ui::text::Make
// finds the end of a runtime buffer by scanning for it, which a terminator equal
// to the splitter would break at the first space.
inline constexpr auto charmap_generic = ::tech::nes_str::charmap({
    { '\0',     windows::ui + blob::ui::empty },           // terminator; its own tile, scannable -- see above
    { ' ',      windows::ui + blob::ui::hud_whitespace },   // dedicated blank tile, not part of the font run
    { '0', '9', windows::ui + (blob::ui::font + 0x00) },
    { 'A', 'Z', windows::ui + (blob::ui::font + 0x0a) },
});
