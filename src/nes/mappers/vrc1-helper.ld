/*
 * NES-VRC1 linker helper -- library-owned.
 *
 *   $8000-$9FFF  8 KiB PRG, switchable (window 1)
 *   $A000-$BFFF  8 KiB PRG, switchable (window 2)
 *   $C000-$DFFF  8 KiB PRG, switchable (window 3)
 *   $E000-$FFFF  8 KiB PRG, fixed to the last physical bank
 *
 * This file owns only mechanical hardware facts: window geometry, the fixed
 * bank, the reset vector. A consuming project's own top-level script owns
 * __prg_rom_size, __chr_rom_size and every domain it carves out of the
 * switchable region -- see BANKED_CALL_THEORY.txt's "the final linker script
 * must be user-made", and demo/link.ld for a worked example.
 *
 * A project reserves space off the tail of the switchable region with
 * __prg_rom_switchable_reserved, PROVIDE()d before the INCLUDE.
 */

INCLUDE nes-no-pow2.ld
INCLUDE c-in-ram.ld

PROVIDE(__prg_rom_switchable_reserved = 0);

ASSERT(__chr_rom_size <= 8,
       "This VRC1 script does not wire up CHR banking; only the existing flat 8 KiB of CHR-ROM is supported.")

MEMORY {
  /* Windows 1-3, minus whatever the project reserves off the tail. Untagged
   * code and data live here. */
  prg_rom_switchable : ORIGIN = 0x8000, LENGTH = 0x6000 - __prg_rom_switchable_reserved
  /* Hardwired to the last physical bank regardless of any register: where
   * ::FIXED lands. */
  prg_rom_fixed       : ORIGIN = 0xe000, LENGTH = 0x2000
  nes2_header         : ORIGIN = 0x02000000, LENGTH = 16
}

REGION_ALIAS("c_readonly", prg_rom_switchable)

SECTIONS {
  .prg_rom_fixed : { *(.prg_rom_fixed .prg_rom_fixed.*) } >prg_rom_fixed
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
  FULL(prg_rom_switchable)
}
