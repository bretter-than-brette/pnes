#pragma once

#include <pnes/save.hpp>

// NOT ON NES, AND THAT IS THE WHOLE OF THE CONDITION. The save there IS the
// cart (src/nes/mappers/mmc3.cpp), so there is no file to find and no device
// to pick, and the mode would need a bank of its own besides (main.cpp's note
// on ::mmc3::CallInBlock). Every other target asks ::save::AccessibleDevices
// what it has and acts on the answer, so none of them needs telling here.
#ifndef TARGET_NES

constexpr auto retryText        = ::charmap_generic.encode("RETRY");
constexpr auto continueText     = ::charmap_generic.encode("CONTINUE");
constexpr auto yesText          = ::charmap_generic.encode("YES");
constexpr auto noText           = ::charmap_generic.encode("NO");
constexpr auto eraseText        = ::charmap_generic.encode("ERASE ALL");
// THE TWO DEVICE LABELS, AND THE ONLY PART OF THIS WHEEL THAT IS PER CONSOLE.
// ::Card1 / ::Card2 are device indices 0 and 1 (demo/src/save.hpp), and what
// sits at those indices is what changes: slots A and B on the GameCube, the
// front SD slot and USB mass storage on the Wii, the launch medium on the PSP,
// and on the Switch, the 3DS and
// the Wii U index 0 alone. Same wheel, same indices, the player's own words for
// what they are putting their save on.
#ifdef TARGET_GC
constexpr auto card1Text        = ::charmap_generic.encode("CARD 1");
constexpr auto card2Text        = ::charmap_generic.encode("CARD 2");
#elif defined(TARGET_PSP)
// THE PSP'S ONE DEVICE, NAMED BY ITS MOST LIKELY HARDWARE for the reason
// demo/src/save.hpp's TARGET_PSP branch gives: the save goes beside the EBOOT on
// whichever medium the game was launched from, and "MEMORY STICK" is right on
// every model but a PSP Go running from internal flash -- which is also the one
// configuration that can never see this wheel, since it is drawn only when two
// devices are accessible at once, and this backend has one
// (src/psp/save.cpp's ::kDeviceCount).
constexpr auto card1Text        = ::charmap_generic.encode("MEMORY STICK");
constexpr auto card2Text        = ::charmap_generic.encode("MEMORY STICK");
#elif defined(TARGET_NX) || defined(TARGET_CTR) || defined(TARGET_WIIU)
// ONE DEVICE, AND ::card2Text LABELS AN INDEX THAT DOES NOT EXIST HERE. The
// wheel these label is only drawn when two devices are accessible at once,
// which is never true on any of these consoles -- so these are the words a
// Switch, 3DS or Wii U
// player would see if it ever did, and saying "SD CARD" twice is a less wrong
// thing to show than naming hardware none of them saves to. (The Wii U does
// have USB ports; what it does not have is a backend that would put a save on
// one -- see src/wiiu/save.cpp's ::kDeviceCount.) See demo/src/save.hpp's note
// on ::Card2.
//
// THE THREE SHARE A BRANCH HERE AND NOT IN demo/src/save.hpp, which is not an
// inconsistency: the device LABEL is a fact about the hardware -- all three
// save to one SD card and there is nothing else to call it -- while the
// MESSAGES over there differ in which console actually shows them. See that
// file's TARGET_CTR and TARGET_WIIU branches.
constexpr auto card1Text        = ::charmap_generic.encode("SD CARD");
constexpr auto card2Text        = ::charmap_generic.encode("SD CARD");
#else
constexpr auto card1Text        = ::charmap_generic.encode("SD CARD");
constexpr auto card2Text        = ::charmap_generic.encode("USB DRIVE");
#endif

// user rtTextBoxes to draw this text, its GC so its compute is OK

namespace pregame {
    /**
     * @brief Finds, loads or creates the save, then hands over to the title.
     *
     * The FIRST mode off the NES (main.cpp's ::gameMode starts here), because
     * every other mode is entitled to assume ::saveData means something. It
     * sets ::gameMode to eGameModes::Title on its way out, so main.cpp's
     * dispatch loop goes straight on to the title screen.
     *
     * NOT PRESENT ON NES. A battery cart has no device to find, nothing to
     * choose between and no metadata to lay out -- the MMC3 backend answers
     * every save call with ::save::found and the save simply IS the cart -- so
     * an NES build boots to the title directly and none of this is compiled.
     * The Wii runs all of it: it has two devices that may each be absent, which
     * is the situation this mode exists for. So does the Switch, with one
     * device that may be absent -- which is the same situation minus the
     * choice, and the flow handles it by never drawing the wheel. The 3DS is
     * that case again, and the one where "may be absent" is literal: its card
     * can leave the slot mid-game (src/3ds/save.cpp). The Wii U is the Switch's
     * case once more -- one device, no choice -- with a card that can also
     * leave mid-game, noticed when the mount goes rather than when the card
     * does (src/wiiu/save.cpp).
     */
    void main();

    /**
     * @brief Writes ::saveData, shows SAVING while it goes where that is worth
     *        showing, and offers RETRY / CONTINUE if it fails.
     *
     * The other half of this mode, for the other end of the session: ::main
     * finds or creates the save before anything has been played, and this
     * writes it back when the player has changed something worth keeping. It
     * is a FUNCTION AND NOT A MODE because it is drawn over whatever mode
     * called it -- it borrows this mode's scene, its frame pump and its NMI
     * handler, points the camera at the bottom-right quadrant the scene lives
     * in, and gives the NMI vector back on the way out.
     *
     * Blocks until the player is done with it, so it must be called from a
     * MAINLINE and never from a handler. See its definition in pregame.cpp for
     * what it does and does not restore.
     */
    bool SaveScreen();

#ifdef TARGET_GC
    extern const OGC_BANNER_CI(banner);
    extern const OGC_ICON_CI(icon);
    extern const char gameName[32];
    extern const char gameDesc[32];
#endif
}

#endif // TARGET_NES
