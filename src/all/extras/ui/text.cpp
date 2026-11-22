#include <pnes/extras/ui/text.hpp>

#include "pnes/video.hpp"
#include "pnes/technology.hpp"   // CREATE_SEGMENT_KEYWORD / MODULE_PLACEMENT

// Compulsory, like audio's: no default, so the placement decision is never a
// silent guess. Guarded as MODULE_PLACEMENT itself is -- this file is compiled
// for every backend, not just NES-banked ones.
#if defined(TARGET_NES) && defined(NES_MAPPER_BANKSWITCHED)
#ifndef PNES_UI_SECTION
#error "PNES_UI_SECTION is not set. It names the ELF section \
src/all/extras/ui/*.cpp is placed into. Set it from CMake -- \
local.cmake.example has a worked example."
#endif
#endif
#define UI_BANK MODULE_PLACEMENT(PNES_UI_SECTION) MINSIZE

namespace ui::text::detail {
    UI_BANK textBuffer* Make(const u8* buff, const vec2<u8> box, const u8 splitter, const u8 terminator) {
        const auto rows = new textBuffer[box.y]();

        // The length the caller used to pass, measured here instead by scanning
        // for the charmap's terminator tile -- which the charmap has to give a
        // tile of its own, or the first splitter would end the string.
        //
        // Capped at 960 (32*30, the most tiles a nametable box can show) as a
        // constant rather than box.x*box.y: the 6502 has no multiply, and the
        // product form measured 78 bytes larger. The cap has to be tight, not
        // merely present -- an unterminated buffer scanning on through $2000-$401f
        // would not just read garbage but CHANGE PPU and APU state.
        u16 sBuff = 0;
        while (sBuff != 960 && *(buff + sBuff) != terminator) sBuff++;

        u16 cursor = 0;
        u16 last   = 0;
        u8  row    = 0;

        while (cursor < sBuff && row < box.y) {
            u16  lastWhite  = last;
            bool foundWhite = false;

            for (; cursor < sBuff && cursor - last < box.x; cursor++) {
                if (*(buff + cursor) == splitter) {
                    lastWhite  = cursor;
                    foundWhite = true;
                }
            }

            const u16 end = (cursor == sBuff) ? cursor : (foundWhite ? lastWhite : cursor);

            rows[row].addr = const_cast<u8*>(buff + last);
            rows[row].size = static_cast<u8>(end - last);
            row++;

            last   = (end < sBuff) ? end + 1 : end;
            cursor = last;
        }

        return rows;
    }
}

namespace ui::text {
    UI_BANK void Draw(const textBuffer* rows, const u16 address, const vec2<u8> box, const Alignment align) {
        const u16 rowWidth = video::viewport_tx() < 32 ? 32 : video::viewport_tx();
        u16 rowAddr = address;
        for (u8 i = 0; i < box.y; i++, rowAddr += rowWidth) {
            u16 useAddr;
            switch (align) {
                case Left:
                default:
                    useAddr = rowAddr;
                    break;

                case Right:
                    useAddr = rowAddr + (box.x - rows[i].size);
                    break;

                case Centre:
                    useAddr = rowAddr + (box.x - rows[i].size) / 2;
                    break;
            }

            ppu::WriteFromBufferToNameTable(useAddr, rows[i].addr, rows[i].size, 0);
        }
    }

    UI_BANK void Clear(
        const textBuffer* rows, const u16 address, const vec2<u8> box, const Alignment align, const u8 clear
    ) {
        const u16 rowWidth = video::viewport_tx() < 32 ? 32 : video::viewport_tx();
        u16 rowAddr = address;
        for (u8 i = 0; i < box.y; i++, rowAddr += rowWidth) {
            u16 useAddr;
            switch (align) {
                case Left:
                default:
                    useAddr = rowAddr;
                    break;

                case Right:
                    useAddr = rowAddr + (box.x - rows[i].size);
                    break;

                case Centre:
                    useAddr = rowAddr + (box.x - rows[i].size) / 2;
                    break;
            }

            ppu::WriteRepeatedToNameTable(useAddr, clear, rows[i].size, 0);
        }
    }
}
