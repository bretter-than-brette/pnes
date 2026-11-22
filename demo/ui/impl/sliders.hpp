#pragma once

#include <pnes/audio.hpp>
#include <pnes/video.hpp>
#include STRCAT(../../gen/options/GEN_TARGET_DIR/options.hpp)
#include "../../chr/chr.h"
#include "../../src/graphics/windows.hpp"
#include "sounds.hpp"

namespace ui::impl {
    template <bool orient>
    inline void DrawSliderBody(const u16 pos, const u8 width) {
        if constexpr (!orient) return;
        constexpr u8 LeftEdgeGraphic  = windows::ui + blob::ui::slider_left;
        constexpr u8 RightEdgeGraphic = windows::ui + blob::ui::slider_right;
        constexpr u8 MiddleGraphic    = windows::ui + blob::ui::slider_middle;

        ppu::WriteSingleToNameTable(pos, LeftEdgeGraphic);
        ppu::WriteRepeatedToNameTable(pos + 1, MiddleGraphic, width - 2, 0);
        ppu::WriteSingleToNameTable(pos + width - 1, RightEdgeGraphic);
    }

    constexpr u8 SliderTravel(const u8 width) {
        return static_cast<u8>(width - 2);
    }

    // Which handle position a clicked cell means. The -1 is the cap: cell 0 is
    // the left end graphic, so an `amount` is always one less than the cell it
    // is drawn in -- without it every click would land the handle one cell right
    // of the pointer.
    //
    // Both caps resolve to the position nearest them rather than to nothing: a
    // cap is not somewhere the handle may sit, but it is unambiguously a
    // direction. Which is why a click needs its own mapping rather than sharing
    // a nudge's -- DrawChange DROPS an out-of-range amount, right for a press
    // walking off the end and wrong for a pointer aimed at it.
    constexpr u8 SliderAmountForCell(const u8 width, const u8 cell) {
        const u8 travel = SliderTravel(width);
        if (!travel) return 0;
        if (cell == 0) return 0;                                  // left cap
        const u8 amt = static_cast<u8>(cell - 1);
        return amt >= travel ? static_cast<u8>(travel - 1) : amt;  // right cap clamps
    }

    constexpr u8 SliderPercent(const u8 width, const u8 amt) {
        const u8 travel = SliderTravel(width);
        if (travel <= 1) return 100;
        const u8 last = static_cast<u8>(travel - 1);
        if (amt >= last) return 100;
        return static_cast<u8>(static_cast<u16>(amt) * 100 / last);
    }

    constexpr u8 SliderCoefficient(const u8 width, const u8 amt) {
        return static_cast<u8>((static_cast<u16>(SliderPercent(width, amt)) * 255 + 50) / 100);
    }

    constexpr u8 SliderAmountFor(const u8 width, const u8 percent) {
        const u8 travel = SliderTravel(width);
        if (travel <= 1) return 0;
        const u8 last = static_cast<u8>(travel - 1);
        const u8 pct  = percent > 100 ? 100 : percent;
        return static_cast<u8>((static_cast<u16>(pct) * last + 50) / 100);
    }

    template <bool orient>
    inline void ClearSliderHandle(const u16 pos, const u8 amt) {
        if constexpr (!orient) return;
        constexpr u8 MiddleGraphic = windows::ui + blob::ui::slider_middle;
        ppu::WriteSingleToNameTable(pos + 1 + amt, MiddleGraphic);
    }

    template <bool orient>
    inline void DrawSliderHandle(const u16 pos, const u8 amt) {
        if constexpr (!orient) return;
        constexpr u8 ActiveGraphic = windows::ui + blob::ui::slider_cursor;
        ppu::WriteSingleToNameTable(pos + 1 + amt, ActiveGraphic);
    }
}

namespace gen::options::audioCanvas {
#ifndef TARGET_NES
    namespace musicSlider {
        static_assert(!vertical, "vertical sliders are not implemented in this project -- "
                                  "untick Vertical on this slider in uitk, or implement the "
                                  "column-walking path in ui::impl");

        inline void Draw() {
            const Rect r = rect;
            musicText::Draw();
            ui::impl::DrawSliderBody<!vertical>(pos, r.size.x);

            const u8 amt = amount;
            if (amt >= ui::impl::SliderTravel(r.size.x)) return;
            ui::impl::DrawSliderHandle<!vertical>(pos, amt);
        }

        inline void DrawChange(const u8 amt) {
            const Rect r = rect;
            if (amt >= ui::impl::SliderTravel(r.size.x)) return;

            const u8 last = amount;

            if (amt != last) {
                ui::impl::PostSfx(amt > last ? sfx::selectnext : sfx::selectprev);
            }

            ui::impl::ClearSliderHandle<!vertical>(pos, last);
            amount = amt;
            ui::impl::DrawSliderHandle<!vertical>(pos, amt);

            audio::TrackVolume(ui::impl::SliderPercent(r.size.x, amt));
        }

        inline void DrawClick(const u8 cell) {
            const Rect r = rect;
            DrawChange(ui::impl::SliderAmountForCell(r.size.x, cell));
        }
    }

    namespace SFXSlider {
        static_assert(!vertical, "vertical sliders are not implemented in this project -- "
                                  "untick Vertical on this slider in uitk, or implement the "
                                  "column-walking path in ui::impl");

        inline void Draw() {
            const Rect r = rect;
            SFXText::Draw();
            ui::impl::DrawSliderBody<!vertical>(pos, r.size.x);

            const u8 amt = amount;
            if (amt >= ui::impl::SliderTravel(r.size.x)) return;
            ui::impl::DrawSliderHandle<!vertical>(pos, amt);
        }

        inline void DrawChange(const u8 amt) {
            const Rect r = rect;
            if (amt >= ui::impl::SliderTravel(r.size.x)) return;

            const u8 last = amount;

            if (amt != last) {
                ui::impl::PostSfx(amt > last ? sfx::selectnext : sfx::selectprev);
            }

            ui::impl::ClearSliderHandle<!vertical>(pos, last);
            amount = amt;
            ui::impl::DrawSliderHandle<!vertical>(pos, amt);
        }

        inline void DrawClick(const u8 cell) {
            const Rect r = rect;
            DrawChange(ui::impl::SliderAmountForCell(r.size.x, cell));
        }
    }
#endif
}
