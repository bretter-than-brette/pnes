/*
 * NES-MMC3 (mapper 4) linker helper -- library-owned.
 *
 *   $8000-$9FFF  8 KiB PRG, switchable (R6)   $6000-$7FFF  PRG-RAM
 *   $A000-$BFFF  8 KiB PRG, switchable (R7)
 *   $C000-$DFFF  8 KiB PRG, fixed to the second-to-last physical bank
 *   $E000-$FFFF  8 KiB PRG, fixed to the last physical bank
 *
 * This file owns only mechanical hardware facts: window geometry, the two fixed
 * banks, the reset vector, PRG-RAM's address. A consuming project's own script
 * still owns __prg_rom_size/__chr_rom_size and its own tagged domains -- see
 * BANKED_CALL_THEORY.txt and demo/link.ld for a worked example.
 *
 * Unlike VRC1's three independently selectable windows, MMC3 in PRG mode 0
 * exposes only two registers a project can point anywhere; $C000-$DFFF is
 * hardware-fixed and is not a third switchable window.
 *
 * PRG-ROM is 32 KiB to 512 KiB, MMC3's own maximum (R6/R7 are 6-bit bank
 * numbers), and the ASSERT below rejects more rather than linking banks no
 * register can select. __prg_rom_size must be PROVIDE()d before the INCLUDE,
 * since the layout is computed from it.
 *
 * Size-independence comes from placing the always-resident image at the TOP of
 * the ROM file, so the fixed windows land on the last two banks whatever the
 * total size is.
 */

INCLUDE nes-no-pow2.ld

PROVIDE(__program_ram_size = 8);
PROVIDE(__program_nvram_size = 0);

ASSERT(__program_ram_size + __program_nvram_size <= 8,
       "MMC3 only supports up to 8 KiB of PRG-(NV)RAM.")

PROVIDE(__mmc3_resident_windows = 3);

ASSERT(__mmc3_resident_windows >= 1 && __mmc3_resident_windows <= 3,
       "__mmc3_resident_windows must be 1 ($C000-$DFFF), 2 ($A000-$DFFF) or 3 ($8000-$DFFF) -- MMC3 exposes no other resident PRG span.")

PROVIDE(__mmc3_reserved_banks = 0);

ASSERT(__prg_rom_size <= 512,
       "MMC3 addresses at most 512 KiB of PRG-ROM (R6/R7 are 6-bit bank numbers) -- __prg_rom_size is larger, so the top banks would be unreachable by any register write.")
ASSERT(__prg_rom_size >= (__mmc3_resident_windows + __mmc3_reserved_banks + 1) * 8,
       "__prg_rom_size is smaller than what is already spoken for: __mmc3_resident_windows * 8 KiB + __mmc3_reserved_banks * 8 KiB + the 8 KiB fixed bank.")

__mmc3_prg_rom_banks     = __prg_rom_size / 8;
__mmc3_boot_bank_window1 = __mmc3_prg_rom_banks - __mmc3_resident_windows - 1;
__mmc3_boot_bank_window2 = __mmc3_boot_bank_window1 + 1;
/* c_writeable/__stack are deliberately not set here: where a project's RAM goes
 * is its own decision, not a hardware fact. */


MEMORY {
  /* $8000-$DFFF by default, or $8000-$BFFF if a project sets
   * __mmc3_resident_windows to 2. Untagged code and data live here, always at
   * the top banks of the ROM immediately below prg_rom_fixed -- which is what
   * makes the third window really be the second-to-last bank at any ROM size. */
  prg_rom_switchable : ORIGIN = 0xe000 - __mmc3_resident_windows * 0x2000,
                       LENGTH = __mmc3_resident_windows * 0x2000
  prg_rom_spare      : ORIGIN = 0x03000000,
                       LENGTH = (__mmc3_prg_rom_banks - __mmc3_resident_windows
                                 - __mmc3_reserved_banks - 1) * 0x2000
  /* Hardwired to the LAST physical bank regardless of any register: the one
   * permanently resident home, which is where ::FIXED lands. */
  prg_rom_fixed       : ORIGIN = 0xe000, LENGTH = 0x2000
  prg_ram             : ORIGIN = 0x6000, LENGTH = (__program_ram_size + __program_nvram_size) * 1024
  nes2_header         : ORIGIN = 0x02000000, LENGTH = 16
}

__space_origin_prg_rom_switchable = ORIGIN(prg_rom_switchable);
__space_length_prg_rom_switchable = LENGTH(prg_rom_switchable);
__space_origin_prg_rom_fixed      = ORIGIN(prg_rom_fixed);
__space_length_prg_rom_fixed      = LENGTH(prg_rom_fixed);
__space_origin_prg_rom_spare      = ORIGIN(prg_rom_spare);
__space_length_prg_rom_spare      = LENGTH(prg_rom_spare);
__space_origin_prg_ram            = ORIGIN(prg_ram);
__space_length_prg_ram            = LENGTH(prg_ram);
__space_origin_ram                = ORIGIN(ram);
__space_length_ram                = LENGTH(ram);
__space_origin_zp                 = ORIGIN(zp);
__space_length_zp                 = LENGTH(zp);
__space_origin_chr_rom            = ORIGIN(chr_rom);
__space_length_chr_rom            = LENGTH(chr_rom);

REGION_ALIAS("c_readonly", prg_rom_switchable)

SECTIONS {
  .prg_rom_fixed : { *(.prg_rom_fixed .prg_rom_fixed.*) } >prg_rom_fixed
  .cartmem (NOLOAD) : { *(.cartmem .cartmem.*) } >prg_ram
  .sysmem  (NOLOAD) : { *(.sysmem .sysmem.*) } >ram
  .nes2_header   : { KEEP(*(.nes2_header)) } >nes2_header
}

EXTERN(_nes2_header)

INCLUDE rompoke-table.ld
INCLUDE dpcm.ld
INCLUDE debug-log.ld

PROVIDE(_reset = _start);

SECTIONS {
  .vector 0xfffa : { SHORT(nmi) SHORT(_reset) SHORT(irq) } >prg_rom_fixed
}

OUTPUT_FORMAT {
  FULL(nes2_header)
}
