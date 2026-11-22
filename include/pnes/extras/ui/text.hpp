#pragma once

#include <intsh>

#include <pnes/technology.hpp>
#include <pnes/types.hpp>
#include <pnes/video.hpp>

using namespace br0::intsh;



namespace ui::text {
    enum Alignment : u8 {
        Left,
        Centre,
        Right
    };

    struct textBuffer {
        u8* addr;
        u8 size;
    };

    /// Finds the end of buff by scanning for the charmap's terminator, so there is
    /// no length to pass. The scan stops at 255 bytes, so an unterminated buffer
    /// truncates rather than running away. @p Map and @p Splitter are template
    /// parameters because a charmap is only ever constant-evaluated.
    namespace detail {
        NI textBuffer* Make(const u8* buff, vec2<u8> box, u8 splitter, u8 terminator);
    }

    inline constexpr auto ascii = ::tech::nes_str::charmap({ { '\0', '\x7f', 0 } });

    template <auto& Map, char Splitter = ' '>
    AI inline textBuffer* Make(const u8* buff, const vec2<u8> box) {
        constexpr u8 splitter   = Map(Splitter);
        constexpr u8 terminator = Map('\0');
        return detail::Make(buff, box, splitter, terminator);
    }

    AI void Draw(const textBuffer* chunks, vec2<u16> pos, vec2<u8> box, Alignment align);

    NI void Draw(const textBuffer* rows, u16 address, vec2<u8> box, Alignment align);

    NI void Clear(
        const textBuffer* rows, u16 address, vec2<u8> box, Alignment align, u8 clear
    );

    AI inline void Draw(const textBuffer* chunks, const vec2<u16> pos, const vec2<u8> box, const Alignment align) {
        Draw(chunks, ppu::CartesianToAddress(pos), box, align);
    }
}
