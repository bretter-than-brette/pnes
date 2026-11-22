#pragma once
#include <pnes/pnes.hpp>
#include <pnes/mappers/mmc3.hpp>
#include "modes/level.hpp"

struct audio_tag      {};
struct audio_data_tag {};
struct level_code_tag {};
struct cold_tag  {};
struct title_tag {};
struct actor_tag {};

inline u8 LevelDataBank(const u16 levelIndex) {
    (void)levelIndex;
    return 1;
}

inline u8 LevelDynamicBank(const u16 levelIndex) {
    (void)levelIndex;
    return 6;
}

inline u8 LevelGraphicsBank(const u16 levelIndex) {
    (void)levelIndex;
    return 7;
}

#ifdef TARGET_NES
template <> struct mmc3::bank_layout<level_code_tag> {
    static constexpr mmc3::section_t section() { return { 0x00018000u, 0x2000u }; }
};

template <> struct mmc3::bank_layout<audio_tag> {
    static constexpr mmc3::section_t section() { return { 0x00038000u, 0x2000u }; }
};

template <> struct mmc3::bank_layout<audio_data_tag> {
    static constexpr mmc3::section_t section() { return { 0x0004a000u, 0x2000u }; }
};

template <> struct mmc3::bank_layout<cold_tag> {
    static constexpr mmc3::section_t section() { return { 0x00058000u, 0x2000u }; }
};

template <> struct mmc3::bank_layout<title_tag> {
    static constexpr mmc3::section_t section() { return { 0x00058000u, 0x2000u }; }
};

template <> struct mmc3::bank_layout<actor_tag> {
    static constexpr mmc3::section_t section() { return { 0x00068000u, 0x2000u }; }
};

#endif

#define COLD CREATE_SEGMENT_KEYWORD(".prg_rom_cold")

#define CARTMEM CREATE_SEGMENT_KEYWORD(".cartmem")

#define SAVEMEM CREATE_SEGMENT_KEYWORD(".savemem")

#define SYSMEM CREATE_SEGMENT_KEYWORD(".sysmem")

// On NES, TITLE and TITLE_DATA are injected as compile definitions onto the
// `demo` target instead of being defined here -- so they reach every TU without
// an include-order requirement, and so local.cmake's choice of bank actually
// takes effect (a #define here would silently win over that -D). TITLE_DATA is
// TITLE's value with ".rodata" appended: a distinct section name in the same
// physical bank, since LLD rejects code and data sharing one name.
//
// Off NES nothing generates those flags, but shared code still tags data with
// them unconditionally -- so they just need *a* definition to reach
// CREATE_SEGMENT_KEYWORD's no-op. The literal name passed here is discarded.
#ifndef TARGET_NES
#define TITLE CREATE_SEGMENT_KEYWORD("")
#define TITLE_DATA CREATE_SEGMENT_KEYWORD("")
#endif

#define LEVEL_CODE CREATE_SEGMENT_KEYWORD(".prg_rom_level_code")

#define LEVEL_DATA CREATE_SEGMENT_KEYWORD(".prg_rom_level_data")

#define LEVEL_DYNAMIC CREATE_SEGMENT_KEYWORD(".prg_rom_level_dynamic")

#define LEVEL_GRAPHICS CREATE_SEGMENT_KEYWORD(".prg_rom_level_graphics")


#ifdef TARGET_NES
FIXED void CallInLevelGraphics(void (*fn)(void*), void* ctx);
#else
inline void CallInLevelGraphics(void (*fn)(void*), void* ctx) { fn(ctx); }
#endif

template <typename F>
auto CallLevelGraphics(F&& f) -> decltype(f()) {
    using Ret = decltype(f());
    if constexpr (__is_same(Ret, void)) {
        CallInLevelGraphics([](void* ctx) { (*static_cast<F*>(ctx))(); }, &f);
    } else {
        struct Result { F* fn; Ret value; } result{ &f, Ret{} };
        CallInLevelGraphics([](void* ctx) {
            auto* r = static_cast<Result*>(ctx);
            r->value = (*r->fn)();
        }, &result);
        return result.value;
    }
}

template <typename Block>
decltype(auto) InColdBank(Block &&block) {
    return mmc3::CallInBlock<cold_tag>(static_cast<Block &&>(block));
}

#define ACTORS CREATE_SEGMENT_KEYWORD(".prg_rom_actors")

MMC3_BIND(level::UpdateActors, actor);
MMC3_BIND(level::FeedOAM, actor);
