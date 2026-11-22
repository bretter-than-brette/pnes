/*
 * Fork of mos-platform/nes/lib/nes.ld (llvm-mos SDK) with exactly one change:
 * the PRG-ROM size ASSERT is removed.
 *
 * Upstream rejects any size that is not an exact power of two -- e.g. 40 KiB,
 * which the banked-call work genuinely needs and which NES2.0's own header can
 * represent exactly. The requirement is not ld.lld's (confirmed by grepping the
 * linker binary for the error text), it lives entirely in that plain ASSERT.
 */

__rc0 = 0x00;
INCLUDE imag-regs.ld
ASSERT(__rc31 == 0x1f, "Inconsistent zero page map.")

ASSERT(__chr_rom_size == 0 || __chr_rom_size == 1 << LOG2CEIL(__chr_rom_size),
       "CHR-ROM size must be a power of 2.")
ASSERT(__prg_ram_size == 0 ||
       __prg_ram_size == 1 << LOG2CEIL(__prg_ram_size),
       "Nonempty PRG-RAM must be a power of 2.")
ASSERT(__prg_nvram_size == 0 ||
       __prg_nvram_size == 1 << LOG2CEIL(__prg_nvram_size),
       "Nonempty PRG-NVRAM must be a power of 2.")
ASSERT(__chr_ram_size == 0 ||
       __chr_ram_size == 1 << LOG2CEIL(__chr_ram_size),
       "Nonempty CHR-RAM must be a power of 2.")
ASSERT(__chr_nvram_size == 0 ||
       __chr_nvram_size == 1 << LOG2CEIL(__chr_nvram_size),
       "Nonempty CHR-NVRAM must be a power of 2.")

MEMORY {
  zp : ORIGIN = __rc31 + 1, LENGTH = 0x100 - (__rc31 + 1)

  /* CPU address space. */
  ram : ORIGIN = 0x0200, LENGTH = 0x0600

  chr_rom : ORIGIN = 0x01000000, LENGTH = __chr_rom_size * 1024
}

SECTIONS {
  .text : {
       INCLUDE text-sections.ld
       KEEP(*(.nmi_begin))
       KEEP(*(SORT_BY_INIT_PRIORITY(.nmi.* .nmi)))
       KEEP(*(.nmi_end))
  } >c_readonly
  INCLUDE rodata.ld
  .aligned (NOLOAD) : { *(.aligned .aligned.*) } >ram
  INCLUDE data.ld
  INCLUDE zp.ld
  INCLUDE bss.ld
  INCLUDE noinit.ld

  .ram (NOLOAD) : { *(.ram .ram.*) } > ram

  .chr_rom : { KEEP(*(.chr_rom .chr_rom.*)) } >chr_rom
}
