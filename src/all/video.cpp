#include <pnes/video.hpp>

// The one definition of the measured-refresh flag: every target compiles
// src/all, while DetectRefresh() -- which writes it -- is necessarily
// per-backend. Initialised false, so a backend whose detection is a no-op reads
// as the common 60Hz case rather than as an undefined one.
namespace video {
bool is50Hz = false;
}

/**
 * @brief Weak default: answers 60Hz without measuring.
 *
 * What matters is the rate the GAME ticks at, not what a panel can do, since one
 * frame of logic runs per present. DS, GBA, 3DS, Switch and PSP are ~60Hz paced
 * by their own vblank with nothing to detect; the PS2's mode is fixed; and the
 * desktop ticks on a 16ms software timer, so the monitor's refresh is the wrong
 * thing to read there.
 *
 * Weak, so the four backends whose rate genuinely varies override it:
 * src/nes/video.cpp counts off PPUSTATUS, src/ogc and src/wiiu time their own
 * cadence, and src/ps3 reads the rate lv2 publishes. None of them change the
 * display mode to avoid the question -- scaling the game is a smaller
 * imposition than overriding what the player configured.
 */
__attribute__((weak)) bool video::DetectRefresh() { return false; }
