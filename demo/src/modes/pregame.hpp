#pragma once

#include <pnes/save.hpp>

namespace pregame {
    /**
     * @brief Gets ::saveData into a state every later mode may rely on, then
     *        hands over to the title.
     *
     * THE FIRST MODE ON EVERY TARGET (main.cpp's ::gameMode starts here),
     * because every other mode is entitled to assume ::saveData means
     * something. It sets ::gameMode to eGameModes::Title on its way out, so
     * main.cpp's dispatch loop goes straight on to the title screen.
     *
     * TWO SHAPES, AND THE DIFFERENCE IS THE MEDIUM RATHER THAN THE JOB. Both
     * end with a ::saveData that is either the player's or a freshly defaulted
     * one, and neither returns until that is true.
     *
     *   - WHERE THE SAVE IS A FILE ON A DEVICE, the device may be missing, the
     *     file may not exist, the player may have to choose where it goes and
     *     be asked whether to spend the space -- so this draws a screen, pumps
     *     its own frames, and reports every way any of that can fail.
     *
     *   - ON NES THE SAVE IS THE CART, so there is nothing to find, nothing to
     *     choose and nothing that can go wrong at the device level: the load
     *     cannot fail, and the only question left is whether what came back is
     *     MEANINGFUL. So that path loads, validates, and populates the
     *     defaults if validation says no. It draws nothing, owns no NMI or IRQ
     *     vector, pumps no frames and blocks on nothing -- a battery cart
     *     reads as garbage once, on the very first boot, and every boot after
     *     that is a validate and a return. See its definition in pregame.cpp.
     */
    void main();
}

// EVERYTHING BELOW IS THE SCREEN, AND THE NES HAS NO SCREEN HERE. The wording,
// the wheels, the option vocabulary and the write-back entry point all belong
// to the drawn flow; ::main's NES shape loads, validates, defaults and returns
// without any of it. ::pregame::main itself is declared above, on every target.
#ifndef TARGET_NES

constexpr auto retryText        = ::charmap_generic.encode("RETRY");
constexpr auto continueText     = ::charmap_generic.encode("CONTINUE");
constexpr auto yesText          = ::charmap_generic.encode("YES");
constexpr auto noText           = ::charmap_generic.encode("NO");
constexpr auto eraseText        = ::charmap_generic.encode("ERASE");
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

}

#endif // TARGET_NES
