#pragma once

#include <pnes/save.hpp>

namespace pregame {
// The pregame mode: what runs between boot and the title screen.
    void main();
}

#ifndef TARGET_NES

constexpr auto retryText        = ::charmap_generic.encode("RETRY");
constexpr auto continueText     = ::charmap_generic.encode("CONTINUE");
constexpr auto yesText          = ::charmap_generic.encode("YES");
constexpr auto noText           = ::charmap_generic.encode("NO");
constexpr auto eraseText        = ::charmap_generic.encode("ERASE");
#ifdef TARGET_GC
constexpr auto card1Text        = ::charmap_generic.encode("CARD 1");
constexpr auto card2Text        = ::charmap_generic.encode("CARD 2");
#elif defined(TARGET_PSP)
constexpr auto card1Text        = ::charmap_generic.encode("MEMORY STICK");
constexpr auto card2Text        = ::charmap_generic.encode("MEMORY STICK");
#elif defined(TARGET_NX) || defined(TARGET_CTR) || defined(TARGET_WIIU)
constexpr auto card1Text        = ::charmap_generic.encode("SD CARD");
constexpr auto card2Text        = ::charmap_generic.encode("SD CARD");
#else
constexpr auto card1Text        = ::charmap_generic.encode("SD CARD");
constexpr auto card2Text        = ::charmap_generic.encode("USB DRIVE");
#endif


namespace pregame {

    bool SaveScreen();

}

#endif // TARGET_NES
