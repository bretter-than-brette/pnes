/*
 * debug-log.ld -- library-owned, mapper-independent.
 *
 * log()'s call sites emit their metadata into .pnes_log (see logger.hpp). This
 * gives that section an output rule with no address and no MEMORY region, so it
 * survives in the linked .elf for the host-side tool but contributes zero bytes
 * to the ROM -- objcopy only copies sections placed into a region.
 *
 * Without an explicit rule, linkers place an orphan section into the nearest
 * matching region anyway, which would silently leak this data into the ROM.
 * INCLUDEd by every mapper helper.
 */

MEMORY {
  pnes_log : ORIGIN = 0x04000000, LENGTH = 0x01000000
}

SECTIONS {
  .pnes_log : { KEEP(*(.pnes_log .pnes_log.*)) } >pnes_log
}
