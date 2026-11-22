#include "save.hpp"

#ifndef TARGET_NES
#include <cstring>
#endif

#include "pnes/logger.hpp"
#ifndef TARGET_NES
#include "pnes/video.hpp"   // the ::SaveGame fallback pump
#endif

SAVEMEM SaveFile saveData;

// AFTER ::saveData AND NOT BEFORE IT, which is the whole reason
// ::PopulateSaveData is defined in this file rather than in the header. The
// generated page declares its state as references into this very object
// (`inline atomic u8& amount = saveData.config....`), so it needs the
// definition above already in scope -- and it needs ::charmap_generic, which
// save.hpp brought in.
#ifndef GEN_TARGET_DIR
#error "GEN_TARGET_DIR is not set -- add it to this target's CMakeLists.txt branch."
#endif
#include STRCAT(../gen/options/GEN_TARGET_DIR/options.hpp)

#ifdef TARGET_GC
// THE CARD'S OWN METADATA, which is the one thing a GameCube save carries that
// is not the game's state: the system menu draws a save with these. They live
// beside ::PopulateSaveData because it is their only reader -- they used to sit
// in demo/src/modes/pregame.cpp, which made a mode own the bytes that describe
// the save rather than the save owning them.
const OGC_BANNER_CI(banner) = {
#embed <card/banner.ci>
};

const OGC_ICON_CI(icon) = {
#embed <card/icon.ci>
};

#ifdef REGION_JP
const char gameName[32] = "\x00";   // shift-JIS todo: the japanese are hungry for letters
const char gameDesc[32] = "\x00";   // shift-JIS
#else
const char gameName[32] = "Super Mary Sisters\x00";   // ASCII
const char gameDesc[32] = "It has Mary in it.\x00";   // ASCII
#endif
#endif

void PopulateSaveData() {
    saveData.version = saveVersion;
#ifdef TARGET_GC
    memcpy(saveData.header.banner,      banner,     sizeof(banner));
    memcpy(saveData.header.icon,        icon,       sizeof(icon));
    memcpy(saveData.header.gameName,    gameName,   sizeof(gameName));
    memcpy(saveData.header.gameDesc,    gameDesc,   sizeof(gameDesc));
#endif
#ifdef TARGET_NES
    tech::PopulateFromBuffer(saveData.magicWord, 0, templateMagicWord, 16, 1);
#endif
#ifdef TARGET_NES
    gen::options::videoCanvas::reduceFlashesButton::Enable();
    gen::options::audioCanvas::disableMusic::Enable();
    gen::options::audioCanvas::disableSFX::Enable();
#else
    gen::options::audioCanvas::musicSlider::amount = 0xFF;
    gen::options::audioCanvas::SFXSlider::amount   = 0xFF;
#endif
}

#ifndef TARGET_NES
save::Response SaveGame(void (* const pump)()) {
    // Before the bytes, not after: this is what makes the file readable next
    // boot (see this function's declaration, save.hpp).
    saveData.version = saveVersion;

    // NO DEVICE PRE-CHECK, DELIBERATELY. ::save::AccessibleDevices would be
    // the obvious guard and it is the wrong one: the weak backend every target
    // but the GameCube still links answers 0 forever (src/all/save.cpp), and
    // the NES battery path overrides ::save::Save without overriding that --
    // so a pre-check would refuse to save on exactly the platforms whose save
    // works. ::save::Save reports ::save::no_device itself when there is
    // nowhere to write, which is the same answer one frame later and true
    // everywhere.
    save::Response response { };
    while (response = save::Save(&saveData, sizeof(saveData)), !response.done) {
        if (pump) pump();
        else      video::WaitForPresent();
    }

    log("SaveGame: err=%d", response.err);
    return response;
}
#endif

constexpr void UpdateTextForSave(const char* buff, void* target, const std::size_t sName) {
#ifdef TARGET_GC
    if (sName > 32 || !sName) {
#if !NDEBUG
        log("text change in saved memory request was either zero or above 32 bytes in size");
#endif
        return;
    }
#if !NDEBUG
    if (buff[sName - 1] != '\0') {
        log("text change in saved memory request was not null terminated");
        return;
    }
#endif

    memcpy(target, buff, sName);
#endif
}

constexpr void UpdateGameNameForSave(const char* buff, const std::size_t sName) {
#ifdef TARGET_GC
    UpdateTextForSave(buff, saveData.header.gameName, sName);
#endif
}

constexpr void UpdateGameDescriptionForSave(const char* buff, const std::size_t sName) {
#ifdef TARGET_GC
    UpdateTextForSave(buff, saveData.header.gameDesc, sName);
#endif
}