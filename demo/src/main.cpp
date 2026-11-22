#include <pnes>
#include <pnes/apu.hpp>
#include <pnes/audio.hpp>
#include <pnes/mappers/mmc3.hpp>
#include "header.hpp"
#include "main.hpp"

#include "modes/level.hpp"
#include "modes/pregame.hpp"
#include "modes/title.hpp"
#include "modes/world.hpp"
#include "../ui/impl/sounds.hpp"
#include "banks.hpp"
#include <pnes/logger.hpp>


u8 scratchpad[5];

atomic u16 frameCounter = 0;

// The demo entry point: one mode dispatch per present, with the NMI handler
// each mode installs doing its vblank work.
atomic eGameModes gameMode = eGameModes::Pregame;

SYSMEM oam::sprite_t OAMBuffer[64] __attribute__((aligned(256)));

void (*pNMI)();
void (*pIRQ)();


NMI(FIXED) {
    const mmc3::select_latch_scope latch;

    const u16 frame = static_cast<u16>(frameCounter + 1);
    frameCounter = frame;

    pNMI();
}

IRQ(FIXED) {
    const mmc3::select_latch_scope latch;
    pIRQ();
}

RESET {
    log("RESET: entered");
    apu::DisableFrameIRQ();
    apu::DisableDMCIRQ();

    ui::impl::RegisterSfx();

    log("RESET: audio::Init starting");
    mmc3::CallInBlock<audio_tag>([] {
        mmc3::CallInBlock<audio_data_tag>([] {
            audio::Init(video::Is50Hz());
        });
    });
    log("RESET: audio::Init returned");

    while (!quit) {
        log("RESET: %d", gameMode);
        switch (gameMode) {
            case eGameModes::Level:
                log("RESET: entering level::main");
                mmc3::CallInBlock<level_code_tag>(level::main);
                continue;

            case eGameModes::World:
                log("RESET: entering world::main");
                world::main();
                continue;

            case eGameModes::Pregame:
                log("RESET: entering pregame::main");
                pregame::main();
                continue;

            case eGameModes::Title:
                log("RESET: entering title::main");
                mmc3::CallInBlock<title_tag>(title::main);
        }
    }
}

