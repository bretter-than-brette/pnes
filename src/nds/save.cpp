/**
 * @file save.cpp
 * @brief DS save/load: the Slot-1 cart's backup chip (EEPROM/FLASH) over aux SPI.
 *
 * The GBA backend's shape: one device that never moves, and whether its bytes
 * are ours is the game's magic word to decide. The card bus is claimed per call,
 * since libnds' EEPROM helpers poke the registers without asking calico.
 */
#include <pnes/save.hpp>

#include <nds.h>

namespace {

constexpr const char* kPath = "SLOT1";

save::Response Done(const save::eError err) { return { .done = true, .err = err, .path = kPath }; }

// The chip's type and size, or false with the bus released if there is none.
bool Open(int& type, u32& size) {
    if (!ntrcardOpen()) return false;
    type = cardEepromGetType();
    size = type > 0 ? cardEepromGetSize() : 0;
    if (size) return true;
    ntrcardClose();
    return false;
}

save::Response CopyOut(const void* const src, const std::size_t size) {
    int type; u32 cap;
    if (!Open(type, cap)) return Done(save::no_device);
    if (size > cap) { ntrcardClose(); return Done(save::too_large); }
    // Takes a non-const pointer but only reads it.
    cardWriteEeprom(0, const_cast<u8*>(static_cast<const u8*>(src)), size, type);
    ntrcardClose();
    return Done(save::found);
}

save::Response CopyIn(void* const tar, const std::size_t size) {
    int type; u32 cap;
    if (!Open(type, cap)) return Done(save::no_device);
    if (size > cap) { ntrcardClose(); return Done(save::too_large); }
    cardReadEeprom(0, static_cast<u8*>(tar), size, type);
    ntrcardClose();
    return Done(save::found);
}

}

namespace save {

Response Save(const void* const src, const std::size_t size) { return CopyOut(src, size); }
Response CreateSave(const void* const src, const std::size_t size, const Response*) { return CopyOut(src, size); }

Response Load(void* const tar, const std::size_t size) { return CopyIn(tar, size); }
Response Load(void* const tar, const std::size_t size, const Response*) { return CopyIn(tar, size); }

Response GetSaveLocation() {
    int type; u32 cap;
    if (!Open(type, cap)) return Done(no_device);
    ntrcardClose();
    return Done(found);
}

Response Delete() {
    int type; u32 cap;
    if (!Open(type, cap)) return Done(no_device);
    u8 blank[256];
    for (u8& b : blank) b = 0xFF;
    for (u32 at = 0; at < cap; at += sizeof blank) {
        const u32 n = cap - at < sizeof blank ? cap - at : sizeof blank;
        cardWriteEeprom(at, blank, n, type);
    }
    ntrcardClose();
    return Done(found);
}

u8 AccessibleDevices() { return GetSaveLocation().err == found ? 0b1 : 0; }

Response GetDeviceStatus(const u8 index) { return index ? Done(no_device) : GetSaveLocation(); }

u8 FreeDevices(const std::size_t size) {
    int type; u32 cap;
    if (!Open(type, cap)) return 0;
    ntrcardClose();
    return size <= cap ? 0b1 : 0;
}

}
