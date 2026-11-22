/**
 * @file prxptr_clang_shim.hpp
 * @brief Neutralises PSL1GHT's ATTRIBUTE_PRXPTR for Clang. Include after
 *        <ppu-types.h> and before any <rsx/...> header; a no-op under GCC.
 *
 * PSL1GHT declares the 32-bit pointers inside some lv2/RSX structs with
 * `__attribute__((mode(SI)))`. GCC accepts `mode` on a pointer declarator and
 * gives the 4-byte field the PRX ABI wants; Clang restricts the attribute to
 * integer and floating-point types and hard-errors, so every TU including
 * <rsx/rsx.h> fails to build -- and cmake/PS3.cmake makes Clang this target's
 * compiler for #embed. Still true of PSL1GHT master as of 2026-10-12.
 *
 * Dropping the attribute changes layout: those fields become 8-byte pointers.
 * That is safe here only because nothing in this project passes one of the
 * affected structs across the PRX boundary -- see the per-struct notes below
 * before using this anywhere that does.
 */
#pragma once

#if defined(__clang__)
#  if !defined(__PPU_TYPES_H__)
#    error "prxptr_clang_shim.hpp must be included AFTER <ppu-types.h>"
#  endif
#  undef  ATTRIBUTE_PRXPTR
#  define ATTRIBUTE_PRXPTR
#endif
