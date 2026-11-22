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
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO SD CARD FOUND");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE SD CARD");
#elif defined(TARGET_CTR)
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO SD CARD FOUND");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE SD CARD");
#elif defined(TARGET_WIIU)
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO SD CARD FOUND");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE SD CARD");
#elif defined(TARGET_PSP)
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO MEMORY STICK FOUND");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE MEMORY STICK");
#else
constexpr auto saveNoDeviceText    = ::charmap_generic.encode("NO SD CARD OR USB DRIVE");
constexpr auto saveChooseCard       = ::charmap_generic.encode("CHOOSE DEVICE");
constexpr auto saveUseCard          = ::charmap_generic.encode("CONFIRM USE DEVICE");
#endif
constexpr auto saveBadErrorText     = ::charmap_generic.encode("BAD ERROR CODE");
constexpr auto saveCorruptText     = ::charmap_generic.encode("SAVE FILE CORRUPTED CONFIRM ERASE SAVE");
constexpr auto saveFullText        = ::charmap_generic.encode("NOT ENOUGH FREE SPACE");
constexpr auto saveLargeText       = ::charmap_generic.encode("SAVE FILE IS TOO LARGE FOR GAME");
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
 * @brief Writes ::saveData back to wherever it already lives: stamps the
 *        version, runs save::Save to completion, hands back the Response it
 *        finished with. What that means on screen is pregame::SaveScreen's
 *        business, so this neither draws nor decides anything.
 *
 * @p pump is called once per waited frame, and is why this is a function rather
 * than a line at the call site: every save call here is ticked, so the game is
 * expected to keep presenting frames until `done`. A save that blocked without
 * the caller's own frame body would freeze the screen it is drawn on and silence
 * the music. A null @p pump falls back to a bare WaitForPresent.
 *
 * The version is stamped here rather than by the caller, since the only moment
 * it is known to be right is the moment the bytes go out -- a save written
 * without it comes back as version 0 and is judged corrupt on the next boot.
 *
 * Not compiled on NES, where a battery cart's RAM IS the save, so there is no
 * transfer to run.
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

    if (fails <= 4) {
        tech::PopulateFromBuffer(saveData.magicWord, 0, templateMagicWord, 16, 1);
    } else return false;    // bad save
#endif
    if (saveData.version != saveVersion) return false;
    return true;
}

void PopulateSaveData();