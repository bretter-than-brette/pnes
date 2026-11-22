; FamiStudio sound engine configuration -- project-owned, not CMake-generated.
; CMakeLists.txt regex-extracts most values back out of this file, so the engine
; and the C++ side cannot disagree about them.

.define FAMISTUDIO_CA65_ZP_SEGMENT   ZEROPAGE
.define FAMISTUDIO_CA65_RAM_SEGMENT  BSS
.ifdef PNES_AUDIO_SEGMENT_INC
    .include "pnes_audio_segment.inc"
.else
    .define PNES_AUDIO_SEGMENT _pprg__rom__audio_fx
.endif
.define FAMISTUDIO_CA65_CODE_SEGMENT PNES_AUDIO_SEGMENT

FAMISTUDIO_CFG_EXTERNAL     = 1
FAMISTUDIO_CFG_NTSC_SUPPORT = 1
FAMISTUDIO_CFG_PAL_SUPPORT  = 1
FAMISTUDIO_CFG_DPCM_SUPPORT = 1
FAMISTUDIO_CFG_SFX_SUPPORT  = 1
.ifndef PNES_SFX_STREAMS
    PNES_SFX_STREAMS = 2
.endif
FAMISTUDIO_CFG_SFX_STREAMS  = PNES_SFX_STREAMS
FAMISTUDIO_USE_VOLUME_TRACK = 1

.include "famistudio_ca65.s"
