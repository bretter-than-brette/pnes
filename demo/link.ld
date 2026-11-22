/* This demo's own ROM size -- a project choice, not an MMC3 fact. */

PROVIDE(__prg_rom_size = 256);
PROVIDE(__chr_rom_size = 16);

/* Banks reserved off the top for this project's own tagged domains, below. */
PROVIDE(__mmc3_reserved_banks = 30);

PROVIDE(__mmc3_resident_windows = 1);

/* The $6000-$7FFF window is this cart's SAVE memory, not volatile work RAM. */

PROVIDE(__program_ram_size = 0);
PROVIDE(__program_nvram_size = 8);

INCLUDE mmc3-helper.ld

MEMORY {
  /* LEVEL CODE: bank 0, window 1. Reads the static plane and the dynamic
   * plane's ROM source straight out of window 2, so that stays mapped for the
   * whole level session. See banks.hpp's ::level_code_tag. */
  prg_rom_level_code : ORIGIN = 0x00018000, LENGTH = 0x2000  /* bank 0, window 1 */
  prg_rom_level_data : ORIGIN = 0x0002a000, LENGTH = 0x2000  /* bank 1, window 2 */
  prg_rom_audio : ORIGIN = 0x00038000, LENGTH = 0x2000  /* bank 2, window 1 */
  prg_rom_audio_data : ORIGIN = 0x0004a000, LENGTH = 0x2000  /* bank 3, window 2 */
  prg_rom_cold : ORIGIN = 0x00058000, LENGTH = 0x2000  /* bank 4, window 1 */
  prg_rom_actors : ORIGIN = 0x00068000, LENGTH = 0x2000  /* bank 5, window 1 */
  prg_rom_level_dynamic : ORIGIN = 0x0007a000, LENGTH = 0x2000  /* bank 6, window 2 */
  prg_rom_level_graphics : ORIGIN = 0x0008a000, LENGTH = 0x2000  /* bank 7, window 2 */

  prg_rom_options : ORIGIN = 0x00098000, LENGTH = 0x2000  /* bank 8, window 1 */

  prg_rom_empty_1 : ORIGIN = 0x000a8000, LENGTH = 0x2000  /* bank 9, window 1 */
  prg_rom_empty_2 : ORIGIN = 0x000b8000, LENGTH = 0x2000  /* bank 10, window 1 */
  prg_rom_empty_3 : ORIGIN = 0x000c8000, LENGTH = 0x2000  /* bank 11, window 1 */
  prg_rom_empty_4 : ORIGIN = 0x000d8000, LENGTH = 0x2000  /* bank 12, window 1 */
  prg_rom_empty_5 : ORIGIN = 0x000e8000, LENGTH = 0x2000  /* bank 13, window 1 */
  prg_rom_empty_6 : ORIGIN = 0x000f8000, LENGTH = 0x2000  /* bank 14, window 1 */
  prg_rom_empty_7 : ORIGIN = 0x00108000, LENGTH = 0x2000  /* bank 15, window 1 */
  prg_rom_empty_8 : ORIGIN = 0x00118000, LENGTH = 0x2000  /* bank 16, window 1 */
  prg_rom_empty_9 : ORIGIN = 0x00128000, LENGTH = 0x2000  /* bank 17, window 1 */
  prg_rom_empty_10 : ORIGIN = 0x00138000, LENGTH = 0x2000  /* bank 18, window 1 */
  prg_rom_empty_11 : ORIGIN = 0x00148000, LENGTH = 0x2000  /* bank 19, window 1 */
  prg_rom_empty_12 : ORIGIN = 0x00158000, LENGTH = 0x2000  /* bank 20, window 1 */
  prg_rom_empty_13 : ORIGIN = 0x00168000, LENGTH = 0x2000  /* bank 21, window 1 */
  prg_rom_empty_14 : ORIGIN = 0x00178000, LENGTH = 0x2000  /* bank 22, window 1 */
  prg_rom_empty_15 : ORIGIN = 0x00188000, LENGTH = 0x2000  /* bank 23, window 1 */
  prg_rom_empty_16 : ORIGIN = 0x00198000, LENGTH = 0x2000  /* bank 24, window 1 */
  prg_rom_empty_17 : ORIGIN = 0x001a8000, LENGTH = 0x2000  /* bank 25, window 1 */
  prg_rom_empty_18 : ORIGIN = 0x001b8000, LENGTH = 0x2000  /* bank 26, window 1 */
  prg_rom_empty_19 : ORIGIN = 0x001c8000, LENGTH = 0x2000  /* bank 27, window 1 */
  prg_rom_empty_20 : ORIGIN = 0x001d8000, LENGTH = 0x2000  /* bank 28, window 1 */
  prg_rom_empty_21 : ORIGIN = 0x001e8000, LENGTH = 0x2000  /* bank 29, window 1 */
}

__space_origin_prg_rom_level_code = ORIGIN(prg_rom_level_code);
__space_length_prg_rom_level_code = LENGTH(prg_rom_level_code);
__space_origin_prg_rom_level_data = ORIGIN(prg_rom_level_data);
__space_length_prg_rom_level_data = LENGTH(prg_rom_level_data);
__space_origin_prg_rom_audio = ORIGIN(prg_rom_audio);
__space_length_prg_rom_audio = LENGTH(prg_rom_audio);
__space_origin_prg_rom_audio_data = ORIGIN(prg_rom_audio_data);
__space_length_prg_rom_audio_data = LENGTH(prg_rom_audio_data);
__space_origin_prg_rom_cold = ORIGIN(prg_rom_cold);
__space_length_prg_rom_cold = LENGTH(prg_rom_cold);
__space_origin_prg_rom_actors = ORIGIN(prg_rom_actors);
__space_length_prg_rom_actors = LENGTH(prg_rom_actors);
__space_origin_prg_rom_level_dynamic = ORIGIN(prg_rom_level_dynamic);
__space_length_prg_rom_level_dynamic = LENGTH(prg_rom_level_dynamic);
__space_origin_prg_rom_level_graphics = ORIGIN(prg_rom_level_graphics);
__space_length_prg_rom_level_graphics = LENGTH(prg_rom_level_graphics);
__space_origin_prg_rom_options = ORIGIN(prg_rom_options);
__space_length_prg_rom_options = LENGTH(prg_rom_options);
__space_origin_prg_rom_empty_1 = ORIGIN(prg_rom_empty_1);
__space_length_prg_rom_empty_1 = LENGTH(prg_rom_empty_1);
__space_origin_prg_rom_empty_2 = ORIGIN(prg_rom_empty_2);
__space_length_prg_rom_empty_2 = LENGTH(prg_rom_empty_2);
__space_origin_prg_rom_empty_3 = ORIGIN(prg_rom_empty_3);
__space_length_prg_rom_empty_3 = LENGTH(prg_rom_empty_3);
__space_origin_prg_rom_empty_4 = ORIGIN(prg_rom_empty_4);
__space_length_prg_rom_empty_4 = LENGTH(prg_rom_empty_4);
__space_origin_prg_rom_empty_5 = ORIGIN(prg_rom_empty_5);
__space_length_prg_rom_empty_5 = LENGTH(prg_rom_empty_5);
__space_origin_prg_rom_empty_6 = ORIGIN(prg_rom_empty_6);
__space_length_prg_rom_empty_6 = LENGTH(prg_rom_empty_6);
__space_origin_prg_rom_empty_7 = ORIGIN(prg_rom_empty_7);
__space_length_prg_rom_empty_7 = LENGTH(prg_rom_empty_7);
__space_origin_prg_rom_empty_8 = ORIGIN(prg_rom_empty_8);
__space_length_prg_rom_empty_8 = LENGTH(prg_rom_empty_8);
__space_origin_prg_rom_empty_9 = ORIGIN(prg_rom_empty_9);
__space_length_prg_rom_empty_9 = LENGTH(prg_rom_empty_9);
__space_origin_prg_rom_empty_10 = ORIGIN(prg_rom_empty_10);
__space_length_prg_rom_empty_10 = LENGTH(prg_rom_empty_10);
__space_origin_prg_rom_empty_11 = ORIGIN(prg_rom_empty_11);
__space_length_prg_rom_empty_11 = LENGTH(prg_rom_empty_11);
__space_origin_prg_rom_empty_12 = ORIGIN(prg_rom_empty_12);
__space_length_prg_rom_empty_12 = LENGTH(prg_rom_empty_12);
__space_origin_prg_rom_empty_13 = ORIGIN(prg_rom_empty_13);
__space_length_prg_rom_empty_13 = LENGTH(prg_rom_empty_13);
__space_origin_prg_rom_empty_14 = ORIGIN(prg_rom_empty_14);
__space_length_prg_rom_empty_14 = LENGTH(prg_rom_empty_14);
__space_origin_prg_rom_empty_15 = ORIGIN(prg_rom_empty_15);
__space_length_prg_rom_empty_15 = LENGTH(prg_rom_empty_15);
__space_origin_prg_rom_empty_16 = ORIGIN(prg_rom_empty_16);
__space_length_prg_rom_empty_16 = LENGTH(prg_rom_empty_16);
__space_origin_prg_rom_empty_17 = ORIGIN(prg_rom_empty_17);
__space_length_prg_rom_empty_17 = LENGTH(prg_rom_empty_17);
__space_origin_prg_rom_empty_18 = ORIGIN(prg_rom_empty_18);
__space_length_prg_rom_empty_18 = LENGTH(prg_rom_empty_18);
__space_origin_prg_rom_empty_19 = ORIGIN(prg_rom_empty_19);
__space_length_prg_rom_empty_19 = LENGTH(prg_rom_empty_19);
__space_origin_prg_rom_empty_20 = ORIGIN(prg_rom_empty_20);
__space_length_prg_rom_empty_20 = LENGTH(prg_rom_empty_20);
__space_origin_prg_rom_empty_21 = ORIGIN(prg_rom_empty_21);
__space_length_prg_rom_empty_21 = LENGTH(prg_rom_empty_21);

SECTIONS {
  .prg_rom_level_code : { *(.prg_rom_level_code .prg_rom_level_code.*) } >prg_rom_level_code
  .prg_rom_level_data : { *(.prg_rom_level_data .prg_rom_level_data.*) } >prg_rom_level_data
  .prg_rom_audio : { *(.prg_rom_audio .prg_rom_audio.*) } >prg_rom_audio
  .prg_rom_audio_data : {
    *(.prg_rom_music .prg_rom_music.*)
    *(.prg_rom_sfx .prg_rom_sfx.*)
  } >prg_rom_audio_data
  .prg_rom_cold : { *(.prg_rom_cold .prg_rom_cold.*) } >prg_rom_cold
  .prg_rom_actors : { *(.prg_rom_actors .prg_rom_actors.*) } >prg_rom_actors
  .prg_rom_level_dynamic : { *(.prg_rom_level_dynamic .prg_rom_level_dynamic.*) } >prg_rom_level_dynamic
  .prg_rom_level_graphics : { *(.prg_rom_level_graphics .prg_rom_level_graphics.*) } >prg_rom_level_graphics

  .prg_rom_options : { *(.prg_rom_options .prg_rom_options.*) } >prg_rom_options
  .prg_rom_empty_1 : { *(.prg_rom_empty_1 .prg_rom_empty_1.*) } >prg_rom_empty_1
  .prg_rom_empty_2 : { *(.prg_rom_empty_2 .prg_rom_empty_2.*) } >prg_rom_empty_2
  .prg_rom_empty_3 : { *(.prg_rom_empty_3 .prg_rom_empty_3.*) } >prg_rom_empty_3
  .prg_rom_empty_4 : { *(.prg_rom_empty_4 .prg_rom_empty_4.*) } >prg_rom_empty_4
  .prg_rom_empty_5 : { *(.prg_rom_empty_5 .prg_rom_empty_5.*) } >prg_rom_empty_5
  .prg_rom_empty_6 : { *(.prg_rom_empty_6 .prg_rom_empty_6.*) } >prg_rom_empty_6
  .prg_rom_empty_7 : { *(.prg_rom_empty_7 .prg_rom_empty_7.*) } >prg_rom_empty_7
  .prg_rom_empty_8 : { *(.prg_rom_empty_8 .prg_rom_empty_8.*) } >prg_rom_empty_8
  .prg_rom_empty_9 : { *(.prg_rom_empty_9 .prg_rom_empty_9.*) } >prg_rom_empty_9
  .prg_rom_empty_10 : { *(.prg_rom_empty_10 .prg_rom_empty_10.*) } >prg_rom_empty_10
  .prg_rom_empty_11 : { *(.prg_rom_empty_11 .prg_rom_empty_11.*) } >prg_rom_empty_11
  .prg_rom_empty_12 : { *(.prg_rom_empty_12 .prg_rom_empty_12.*) } >prg_rom_empty_12
  .prg_rom_empty_13 : { *(.prg_rom_empty_13 .prg_rom_empty_13.*) } >prg_rom_empty_13
  .prg_rom_empty_14 : { *(.prg_rom_empty_14 .prg_rom_empty_14.*) } >prg_rom_empty_14
  .prg_rom_empty_15 : { *(.prg_rom_empty_15 .prg_rom_empty_15.*) } >prg_rom_empty_15
  .prg_rom_empty_16 : { *(.prg_rom_empty_16 .prg_rom_empty_16.*) } >prg_rom_empty_16
  .prg_rom_empty_17 : { *(.prg_rom_empty_17 .prg_rom_empty_17.*) } >prg_rom_empty_17
  .prg_rom_empty_18 : { *(.prg_rom_empty_18 .prg_rom_empty_18.*) } >prg_rom_empty_18
  .prg_rom_empty_19 : { *(.prg_rom_empty_19 .prg_rom_empty_19.*) } >prg_rom_empty_19
  .prg_rom_empty_20 : { *(.prg_rom_empty_20 .prg_rom_empty_20.*) } >prg_rom_empty_20
  .prg_rom_empty_21 : { *(.prg_rom_empty_21 .prg_rom_empty_21.*) } >prg_rom_empty_21
}

REGION_ALIAS("c_writeable", prg_ram)
__stack = ORIGIN(prg_ram) + LENGTH(prg_ram) - 1;

__static_writeable_end = __heap_start;

__save_reserve = 256;
__save_base = ORIGIN(prg_ram) + LENGTH(prg_ram) - __save_reserve;
SECTIONS {
  .savemem __save_base (NOLOAD) : { *(.savemem .savemem.*) } >prg_ram
}

__heap_default_limit = 128;
__heap_start = __save_base - 128;
__stack = __heap_start - 1;
ASSERT(__static_writeable_end <= __heap_start,
       "prg_ram's .data/.bss/.noinit grew into the reserved 128-byte heap at the top of prg_ram -- shrink static writeable usage or grow prg_ram/__prg_ram_size.")
ASSERT(__save_base + SIZEOF(.savemem) <= ORIGIN(prg_ram) + LENGTH(prg_ram),
       "saveData overflows its 256-byte reservation at the top of prg_ram -- grow __save_reserve (and move __heap_start with it) or shrink SaveFile.")

PROVIDE(__audio_engine_size = SIZEOF(.prg_rom_audio));
ASSERT(__audio_engine_size > 0 || !DEFINED(nmi_handler),
       "The audio bank is empty -- demo/famistudio_config.s's FAMISTUDIO_CA65_CODE_SEGMENT no longer decodes to .prg_rom_audio (ld.lld XO65 escapes: _p is '.', __ is '_'), so audio::Update() would call an engine that is not in the ROM.")

ASSERT(ORIGIN(zp) == 0x20,
       "Zero page's free region no longer starts at $20 -- llvm-mos's imaginary-register block (__rc0-__rc31) grew or shrank. `direct`-tagged globals and FamiStudio's ZEROPAGE segment assume ORIGIN(zp) == 0x20; update anything that hardcodes that address.")

OUTPUT_FORMAT {
  FULL(prg_rom_level_code)
  FULL(prg_rom_level_data)
  FULL(prg_rom_audio)
  FULL(prg_rom_audio_data)
  FULL(prg_rom_cold)
  FULL(prg_rom_actors)
  FULL(prg_rom_level_dynamic)
  FULL(prg_rom_level_graphics)
  FULL(prg_rom_options)
  FULL(prg_rom_empty_1)
  FULL(prg_rom_empty_2)
  FULL(prg_rom_empty_3)
  FULL(prg_rom_empty_4)
  FULL(prg_rom_empty_5)
  FULL(prg_rom_empty_6)
  FULL(prg_rom_empty_7)
  FULL(prg_rom_empty_8)
  FULL(prg_rom_empty_9)
  FULL(prg_rom_empty_10)
  FULL(prg_rom_empty_11)
  FULL(prg_rom_empty_12)
  FULL(prg_rom_empty_13)
  FULL(prg_rom_empty_14)
  FULL(prg_rom_empty_15)
  FULL(prg_rom_empty_16)
  FULL(prg_rom_empty_17)
  FULL(prg_rom_empty_18)
  FULL(prg_rom_empty_19)
  FULL(prg_rom_empty_20)
  FULL(prg_rom_empty_21)
  FULL(prg_rom_spare)
  FULL(prg_rom_switchable)
  FULL(prg_rom_fixed)
  FULL(chr_rom)
}
