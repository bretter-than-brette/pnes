/**
 * @file save.cpp
 * @brief GBA save/load: battery SRAM, copied a byte at a time to and from RAM.
 *
 * SRAM sits on an 8-bit bus -- wider loads read one byte repeated and wider
 * stores drop the rest -- so the save lives in RAM and only volatile u8 copies
 * touch the cart. Always present, so there is one device and it never moves.
 */
#include <pnes/save.hpp>

#include <gba.h>

namespace {

// The save file starts at the base of SRAM. 32 KB is the usual battery chip.
constexpr std::size_t kSize = 0x8000;
volatile u8* const sram = reinterpret_cast<volatile u8*>(SRAM);

constexpr const char* kPath = "SRAM";

// Emulators and flashcarts pick the save type by scanning the ROM for this,
// word-aligned. Referenced below so --gc-sections keeps it.
alignas(4) constexpr char kSramTag[12] = "SRAM_V113";

void KeepTag() { asm volatile("" :: "r"(kSramTag)); }

save::Response Done(const save::eError err) { return { .done = true, .err = err, .path = kPath }; }

save::Response CopyOut(const void* const src, const std::size_t size) {
    KeepTag();
    if (size > kSize) return Done(save::too_large);
    const u8* const from = static_cast<const u8*>(src);
    for (std::size_t i = 0; i < size; i++) sram[i] = from[i];
    return Done(save::found);
}

save::Response CopyIn(void* const tar, const std::size_t size) {
    KeepTag();
    if (size > kSize) return Done(save::too_large);
    u8* const to = static_cast<u8*>(tar);
    for (std::size_t i = 0; i < size; i++) to[i] = sram[i];
    return Done(save::found);
}

}

namespace save {

Response Save(const void* const src, const std::size_t size) { return CopyOut(src, size); }
Response CreateSave(const void* const src, const std::size_t size, const Response*) { return CopyOut(src, size); }

Response Load(void* const tar, const std::size_t size) { return CopyIn(tar, size); }
Response Load(void* const tar, const std::size_t size, const Response*) { return CopyIn(tar, size); }

// Whether the bytes are ours is the game's magic word to decide.
Response GetSaveLocation() { return Done(found); }

Response Delete() {
    for (std::size_t i = 0; i < kSize; i++) sram[i] = 0xFF;
    return Done(found);
}

u8 AccessibleDevices() { return 0b1; }

Response GetDeviceStatus(const u8 index) { return Done(index ? no_device : found); }

u8 FreeDevices(const std::size_t size) { return size <= kSize ? 0b1 : 0; }

}
