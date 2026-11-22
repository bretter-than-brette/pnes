#pragma once

#include <pnes/save.hpp>

#include "banks.hpp"

constexpr u16 saveVersion = 1;


enum eOptions : u8 {
#ifndef TARGET_NES
    Continue,
    Retry,
    No,
    Yes,
    Erase,
    Card1,
    Card2
#endif
};

#ifndef TARGET_NES
constexpr auto saveMissingText      = ::charmap_generic.encode("YOU MAY CREATE A SAVE FILE");
#ifdef TARGET_GC
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO MEMORY CARD INSERTED");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE CARD");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE CARD");
#elif defined(TARGET_NX)
// THE SWITCH'S THREE, AND ONLY THE FIRST IS EVER DRAWN. There is one SD card
// and no second device to choose between (src/switch/save.cpp's
// ::kDeviceCount), and the save goes to the game's own directory on the card
// the game booted from -- so this console asks the player NOTHING about where a
// save goes or whether to make one.
//
// ::saveNoDeviceText IS REAL HERE: a failure is reported on every platform, and
// an SD card the system has taken away is the one thing that can stop a save on
// this one.
//
// THE OTHER TWO ARE THE WORDING A ONE-DEVICE CONSOLE WOULD USE IF IT EVER
// ASKED, kept rather than deleted so that the strings and the flow stay in step
// if a project chooses to ask. ::saveChooseCard is still REFERENCED (the
// two-device wheel compiles everywhere, it just cannot be reached with one bit
// in the mask); ::saveUseCard is referenced only inside the TARGET_OGC gate.
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO SD CARD FOUND");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE SD CARD");
#elif defined(TARGET_CTR)
// THE 3DS'S THREE, WHICH ARE THE SWITCH'S WORD FOR WORD and are kept separate
// rather than folded into that branch with a ||. The reason is that the two
// consoles agree by accident and not by nature: both have one SD card, so both
// say "NO SD CARD FOUND" and ask nothing -- but this one's card is REMOVABLE
// while a game is running (src/3ds/save.cpp's ::Detected is the only backend
// that can see that happen), so this is the console where that message is
// actually read, and it is the console whose wording would change first if the
// flow ever had anything more to say about a card that went away mid-game.
//
// THE OTHER TWO ARE THE WORDING A ONE-DEVICE CONSOLE WOULD USE IF IT EVER
// ASKED, for the Switch's reason exactly: a project that defines
// a TARGET_OGC build gets the prompts, and ::saveChooseCard is referenced
// everywhere regardless (the two-device wheel compiles
// everywhere, it just cannot be reached with one bit in the mask).
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO SD CARD FOUND");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE SD CARD");
#elif defined(TARGET_WIIU)
// THE WII U'S THREE, WHICH ARE THE SWITCH'S AND THE 3DS'S WORD FOR WORD and are
// kept as their own branch for the reason the 3DS's are: the agreement is about
// the hardware having one SD card, not about the consoles being the same case.
// This one's card is removable while a game runs, like the 3DS's -- but unlike
// the 3DS there is nothing to ask about the slot, so the backend finds out when
// the system tears the mount down (src/wiiu/save.cpp's ::Mounted) rather than
// when the card moves. "NO SD CARD FOUND" is therefore what a Wii U player sees
// both for a card that was never there and for one that has gone, and this is
// the branch whose wording would change if that distinction ever mattered to
// the flow.
//
// IT MUST NOT FALL THROUGH TO THE WII'S BRANCH BELOW, which is the practical
// point of its existing: that one says "NO SD CARD OR USB DRIVE", and this
// console's backend has one device and no USB story at all
// (src/wiiu/save.cpp's ::kDeviceCount), so a player would be told to try a
// drive the save could never go to.
//
// THE OTHER TWO ARE THE WORDING A ONE-DEVICE CONSOLE WOULD USE IF IT EVER
// ASKED, for the Switch's reason exactly: a project that defines
// a TARGET_OGC build gets the prompts, and ::saveChooseCard is referenced
// everywhere regardless (the two-device wheel compiles
// everywhere, it just cannot be reached with one bit in the mask).
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO SD CARD FOUND");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE SD CARD");
#elif defined(TARGET_PSP)
// THE PSP'S THREE, AND THIS IS THE FIRST CONSOLE WHERE THE DEVICE'S NAME IS A
// GUESS. The other four one-device branches can say what the save is on because
// the backend knows: an SD card, always, on all three of them. This one's save
// goes beside the EBOOT on whatever the game was launched from
// (src/psp/save.cpp), which is a Memory Stick on every retail model and
// internal flash on a PSP Go -- and the message has to be written before either
// is known.
//
// "MEMORY STICK" IS THE RIGHT GUESS ANYWAY, for the reason the backend's own
// fallback picks ms0:. A player who can see this message is a player whose save
// device went missing, and internal flash cannot go missing: a Go running from
// ef0: never reports ::save::no_device, so the one player who would be told the
// wrong thing is the one who will never be told it.
//
// THE OTHER TWO ARE THE WORDING A ONE-DEVICE CONSOLE WOULD USE IF IT EVER
// ASKED, for the Switch's reason exactly: a project that defines
// a TARGET_OGC build gets the prompts, and ::saveChooseCard is referenced
// everywhere regardless (the two-device wheel compiles
// everywhere, it just cannot be reached with one bit in the mask).
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO MEMORY STICK FOUND");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE MEMORY STICK");
#else
// THE WII'S THREE, AND ONLY THE WORDING DIFFERS. "MEMORY CARD" is a thing this
// console does not have: its devices are the front SD slot and a USB disk, so
// a prompt naming a memory card would be asking a player to insert something
// the Wii has no socket for. Kept inside the same 32-tile budget the GameCube's
// 4:3 context band sets (see the write-back trio below), which is why it is
// "NO SD CARD OR USB DRIVE" rather than a sentence.
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO SD CARD OR USB DRIVE");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE DEVICE");
#endif
constexpr auto saveBadErrorText     = ::charmap_generic.encode("BAD ERROR CODE");
constexpr auto saveCorruptText     = ::charmap_generic.encode("SAVE FILE CORRUPTED CONFIRM ERASE SAVE");
constexpr auto saveFullText        = ::charmap_generic.encode("NOT ENOUGH FREE SPACE");
constexpr auto saveLargeText       = ::charmap_generic.encode("SAVE FILE IS TOO LARGE FOR GAME");
// The write-back flow's own three, which ::pregame::SaveScreen shows in the
// same context band pregame's messages use. Short enough for one row of the
// narrowest band this scene has (the 4:3 GameCube's 40 - 8 = 32 tiles), so
// none of them depends on the wrapper to be readable.
constexpr auto saveSavingText       = ::charmap_generic.encode("SAVING");
constexpr auto saveWriteFailedText  = ::charmap_generic.encode("COULD NOT SAVE GAME");
constexpr auto saveNoFileText       = ::charmap_generic.encode("NO SAVE FILE TO WRITE TO");
#endif

struct SaveHeader {
#ifdef TARGET_GC
    OGC_COMMENT(gameName);  // name
    OGC_COMMENT(gameDesc);  // desc
    OGC_BANNER_CI(banner);  // banner in rgba
    OGC_ICON_CI(icon);      // icon frame 1 in ci
#endif
};


#ifndef GEN_TARGET_DIR
#error "GEN_TARGET_DIR is not set -- add it to this target's CMakeLists.txt branch."
#endif


struct SaveFile {
#if defined(TARGET_NES) || defined(TARGET_GBA) || defined(TARGET_NDS)
    u8 magicWord[16];
#endif
    u16 version;
    SaveHeader header;

    struct Config {
#include STRCAT(../gen/options/GEN_TARGET_DIR/bss.hpp)
    } config;
};


extern SAVEMEM SaveFile saveData;

#ifndef TARGET_NES
extern char* savePath;
#endif

#ifndef TARGET_NES
/**
 * @brief Writes ::saveData back to wherever it already lives.
 *
 * THE WHOLE OF SAVING, from a caller's point of view: it stamps the version,
 * runs ::save::Save to completion and hands back the ::save::Response it
 * finished with. What that response MEANS on screen is not this function's
 * business -- ::pregame::SaveScreen is what turns an error into a message and
 * a retry/continue wheel -- so this neither draws nor decides anything.
 *
 * @p pump IS CALLED ONCE PER WAITED FRAME, and it is the reason this is a
 * function at all rather than a line at the call site. Every save call in this
 * library is TICKED: it reports `done` when the operation is over, and until
 * then the game is expected to keep presenting frames (pnes/save.hpp). So the
 * loop needs the caller's own "one frame of my mode" -- ::pregame::WaitBody,
 * which drives audio, spends the sound-effect mailbox and waits for present --
 * and a save that blocked without it would freeze the screen it is drawn on
 * and silence the music. A null @p pump falls back to a bare
 * ::video::WaitForPresent so a caller that has nothing of its own to tick
 * still cannot hang.
 *
 * THE VERSION IS STAMPED HERE, not left to the caller: ::saveVersion describes
 * the layout of the struct this call is about to write, so the only moment it
 * can be known to be right is the moment the bytes go out. ::pregame's
 * ValidateSave is what reads it back, and a save written without it comes back
 * as version 0 and is judged corrupt on the next boot.
 *
 * NOT COMPILED ON NES, where nothing calls it: the options page it is reached
 * from is not shipped there (see ::title::main's own B handling), and a battery
 * cart's RAM is the save -- ::saveData is already in it, so there is no
 * transfer for this to run. Ungating is this one guard, since every call below
 * is declared on every target.
 */
save::Response SaveGame(void (*pump)());
#endif

constexpr void UpdateTextForSave(const char* buff, void* target, std::size_t sName);
constexpr void UpdateGameNameForSave(const char* buff, std::size_t sName);
constexpr void UpdateGameDescriptionForSave(const char* buff,  std::size_t sName);

#if defined(TARGET_NES) || defined(TARGET_GBA) || defined(TARGET_NDS)
constexpr u8 templateMagicWord[16] = "SUPER_MARY_GAME";
#endif

inline bool ValidateSave() {
#if defined(TARGET_NES) || defined(TARGET_GBA) || defined(TARGET_NDS)
    if (saveVersion != saveData.version) return false;
    u8 fails = 0;
    for (u8 i = 0; i < 16; i++) {
        if (saveData.magicWord[i] != templateMagicWord[i]) fails++;
    }

    // repair
    if (fails <= 4) {
        tech::PopulateFromBuffer(saveData.magicWord, 0, templateMagicWord, 16, 1);
    } else return false;    // bad save
#endif
    // run checks on saveData
    if (saveData.version != saveVersion) return false;
    return true;
}

/**
 * @brief Makes ::saveData a save this build would accept: its version, its
 *        identity, the metadata the medium shows, and the options page's
 *        authored defaults.
 *
 * EVERYTHING ::ValidateSave LOOKS FOR, AND EVERYTHING A FRESH PAGE NEEDS, in
 * one call -- because every caller wanted both and there was never a reason to
 * ask for them separately. It is what "there is no save here yet" resolves to:
 * run it and what comes back validates clean on the next boot.
 *
 * NOT INLINE, AND THAT IS FORCED. The options page's own defaults live in
 * uitk's generated accessors, and that header declares references INTO
 * ::saveData -- so it can only be included after this one, which means the body
 * has to be in save.cpp where that include can follow. The GameCube's banner
 * and icon are there with it, as the `#embed`s that own them.
 */
void PopulateSaveData();