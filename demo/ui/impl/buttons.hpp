#pragma once

#include <pnes/types.hpp>
#include <intsh>

#include "../../chr/chr.h"
#include "../../src/graphics/windows.hpp"
#include STRCAT(../../gen/options/GEN_TARGET_DIR/options.hpp)

namespace ui::impl {
    enum eButtonState : u8 {
        Enabled,
        Disabled
    };

    constexpr u16 PageTx() {
        const u16 txMin = video::viewport_tx() < 32 ? 32 : video::viewport_tx();
        return static_cast<u16>((txMin + 3) & ~3);
    }

    constexpr void DrawTextBox(const u16 pos, const vec2<u8>& box) {
        constexpr u8 ULCornerGraphic   = windows::ui + blob::ui::box_ul;
        constexpr u8 URCornerGraphic   = windows::ui + blob::ui::box_ur;
        constexpr u8 BLCornerGraphic   = windows::ui + blob::ui::box_bl;
        constexpr u8 BRCornerGraphic   = windows::ui + blob::ui::box_br;
        constexpr u8 LeftEdgeGraphic   = windows::ui + blob::ui::box_vertical;
        constexpr u8 RightEdgeGraphic  = windows::ui + blob::ui::box_vertical;
        constexpr u8 TopEdgeGraphic    = windows::ui + blob::ui::box_horizontal;
        constexpr u8 BottomEdgeGraphic = windows::ui + blob::ui::box_horizontal;

        ppu::WriteSingleToNameTable(pos, ULCornerGraphic);

        ppu::WriteSingleToNameTable(pos + box.x - 1, URCornerGraphic);

        ppu::WriteSingleToNameTable(pos + PageTx() * (box.y - 1), BLCornerGraphic);

        ppu::WriteSingleToNameTable(pos + box.x + PageTx() * (box.y - 1) - 1, BRCornerGraphic);

        ppu::WriteRepeatedToNameTable(pos + 1, TopEdgeGraphic, box.x - 2, 0);

        ppu::WriteRepeatedToNameTable(
            pos + PageTx() * (box.y - 1) + 1, BottomEdgeGraphic,
            box.x - 2, 0
        );

        ppu::WriteRepeatedToNameTable(
            pos + PageTx(), LeftEdgeGraphic,
            box.y - 2, 1
        );

        ppu::WriteRepeatedToNameTable(
            pos + PageTx() + box.x - 1, RightEdgeGraphic,
            box.y - 2, 1
        );
    }

    constexpr void UpdateTextBox(const eButtonState state, const u16 pos, const vec2<u8> &box) {

    }
}

namespace gen::options {
    // `pos` is already the button's PPU address, resolved in Make(). `rect` is
    // copied out whole before its size is read: on a runtime-viewport target it
    // is volatile, so that is one copy rather than one read per field.
    //
    // One Draw() per button, dispatching on the generated Enabled(): a button's
    // two states are two appearances of one widget, and the branch belongs where
    // the appearance is decided. Each draws its own label too, which is what lets
    // a Canvas paint a whole section in one call -- the generated
    // DrawComponents() deliberately stops at the component, because a button's
    // children are two ALTERNATIVE labels stacked at one position.
    //
    // And it blanks the field first: the two labels are different lengths, so
    // drawing one over the other leaves the tail of the longer one on screen.
    // Both share one rect, so a single Erase() clears it.
    namespace videoCanvas {
        namespace reduceFlashesButton {
            inline void Draw() {
                const Rect r = rect;
                ui::impl::DrawTextBox(pos, r.size);
                ui::impl::UpdateTextBox(Enabled() ? ui::impl::Enabled : ui::impl::Disabled, pos, r.size);
                reduceFlashesText::Erase();
                if (Enabled()) reduceFlashesText::Draw();
                else           enableFlashesText::Draw();
            }
        }

#if TARGET_GC
        namespace enableWidescreenButton {
            inline void Draw() {
                const Rect r = rect;
                ui::impl::DrawTextBox(pos, r.size);
                ui::impl::UpdateTextBox(Enabled() ? ui::impl::Enabled : ui::impl::Disabled, pos, r.size);
                enableWidescreenText::Erase();
                if (Enabled()) disableWidescreenText::Draw();
                else           enableWidescreenText::Draw();
            }
        }
#endif
    }

    namespace audioCanvas {
#if TARGET_NES
        namespace disableMusic {
            inline void Draw() {
                const Rect r = rect;
                ui::impl::DrawTextBox(pos, r.size);
                ui::impl::UpdateTextBox(Enabled() ? ui::impl::Enabled : ui::impl::Disabled, pos, r.size);
                disableMusicText::Erase();
                if (Enabled()) disableMusicText::Draw();
                else           enableMusicText::Draw();
            }
        }

        namespace disableSFX {
            inline void Draw() {
                const Rect r = rect;
                ui::impl::DrawTextBox(pos, r.size);
                ui::impl::UpdateTextBox(Enabled() ? ui::impl::Enabled : ui::impl::Disabled, pos, r.size);
                disableSFXText::Erase();
                if (Enabled()) disableSFXText::Draw();
                else           enableSFXText::Draw();
            }
        }
#else
        namespace enableStereo {
            inline void Draw() {
                const Rect r = rect;
                ui::impl::DrawTextBox(pos, r.size);
                ui::impl::UpdateTextBox(Enabled() ? ui::impl::Enabled : ui::impl::Disabled, pos, r.size);
                enableStereoText::Erase();
                if (Enabled()) enableStereoText::Draw();
                else           disableStereoText::Draw();
            }
        }
#endif
    }
}
