#include "save.hpp"

#ifndef TARGET_NES
#include <cstring>
#endif

#include "pnes/logger.hpp"
#ifndef TARGET_NES
#include "pnes/video.hpp"   // the ::SaveGame fallback pump
#endif

SAVEMEM SaveFile saveData;

// The demo save file and the version stamped into it.
#ifndef GEN_TARGET_DIR
#error "GEN_TARGET_DIR is not set -- add it to this target's CMakeLists.txt branch."
#endif
#include STRCAT(../gen/options/GEN_TARGET_DIR/options.hpp)

#ifdef TARGET_GC
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
#if defined(TARGET_NES) || defined(TARGET_GBA) || defined(TARGET_NDS)
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
    saveData.version = saveVersion;

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