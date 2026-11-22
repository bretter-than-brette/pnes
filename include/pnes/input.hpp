/**
 * @file input.hpp
 * @brief Controller polling and button bits, plus ::input::click -- the frame's
 *        pointer state, written only by ::input::PollControllers.
 */
#pragma once

#include <intsh>
using namespace br0::intsh;

#include "types.hpp"
#include "technology.hpp"
#ifdef TARGET_NES
#ifndef IO_PORT1
#define IO_PORT1 *(volatile u8*)0x4016
#define IO_PORT2 *(volatile u8*)0x4017
#endif
#endif

// Targets with a pointing device the engine can read a position from: a touch
// panel or a mouse. Switch is listed for the handheld panel alone; docked shows
// up the same way a finger that is not touching does.
#if defined(TARGET_NDS) || defined(TARGET_CTR) || defined(TARGET_NX)   \
 || defined(TARGET_WIIU)                                               \
 || defined(TARGET_LINUX) || defined(TARGET_MACOS) || defined(TARGET_WINDOWS)
#define PNES_HAS_CLICK 1
#endif

namespace input {

enum Buttons {
    A       = 0x01, /**< A button. */
    B       = 0x02, /**< B button. */
    SELECT  = 0x04, /**< Select button. */
    START   = 0x08, /**< Start button. */
    UP      = 0x10, /**< D-pad up. */
    DOWN    = 0x20, /**< D-pad down. */
    LEFT    = 0x40, /**< D-pad left. */
    RIGHT   = 0x80, /**< D-pad right. */
};

void PollControllers(u8* port1, u8* port2);

/// constexpr rather than a macro test, so a game writes `if constexpr
/// (input::has_click)` and the guarded code is dropped whole. ::click exists
/// either way, so the name always resolves and the dead branch still compiles.
#ifdef PNES_HAS_CLICK
constexpr bool has_click = true;
#else
constexpr bool has_click = false;
#endif

#ifdef PNES_HAS_CLICK
/// Read-only to a game: the only writer is its one friend, PollControllers, so
/// the pointer arrives on the same tick as the buttons. Every backend normalizes
/// into VIEWPORT PIXELS first, so a game sees one space and never learns which
/// device produced it -- which is also why there is no panel accessor here, a
/// viewport coordinate saying which panel it is in by construction.
class Click {
public:
    /// The last known position when nothing is touching rather than a sentinel,
    /// which is what makes ::Up readable. ::Held says whether it is live; a
    /// coordinate cannot, since (0,0) is a real pixel. Built member-wise because
    /// ::vec2's volatile assignment operator deprecates its copy constructor.
    AI vec2<u16> Get() const noexcept { return { pos.x, pos.y }; }

    AI bool Held() const noexcept { return held; }

    /// True for the one frame it went down -- the click half of the
    /// `inputs & ~prevInputs` strobe. A held finger must not re-fire a button
    /// every frame, exactly as a held A does not.
    AI bool Down() const noexcept { return held && !prev; }

    AI bool Up() const noexcept { return !held && prev; }

private:
    /// Rolls the edge itself, so ::Down and ::Up are each true for exactly one
    /// poll and no backend has to remember last frame. @p p is ignored while
    /// @p down is false: a touch panel reports no position when idle, and ::Up
    /// would otherwise name the top-left corner rather than where the finger left.
    AI void Set(const vec2<u16> p, const bool down) noexcept {
        prev = held;
        held = down;
        if (down) pos = p;
    }

    vec2<u16> pos{0, 0};
    bool held = false;
    bool prev = false;

    friend void PollControllers(u8*, u8*);
};

inline Click click;

#else
/// The clickless stub, so a game can name ::input::click unconditionally: static
/// accessors that fold to constants, so a guarded hit test disappears. What
/// conditional compilation removes is the storage and the backend's poll, not
/// the name.
class Click {
public:
    static constexpr vec2<u16> Get() noexcept { return {0, 0}; }
    static constexpr bool Held() noexcept { return false; }
    static constexpr bool Down() noexcept { return false; }
    static constexpr bool Up() noexcept { return false; }
};

inline constexpr Click click;
#endif

} // namespace input