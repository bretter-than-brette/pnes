/**
 * @file save.cpp
 * @brief Desktop save/load/create/locate through SDL3's user storage, which
 *        puts the file in the OS's per-user data folder (%APPDATA%,
 *        ~/.local/share, ~/Library/Application Support). One device, index 0.
 */
#include "internal.hpp"
#include <pnes/save.hpp>

#include <cstddef>
#include <cstring>

namespace {

#ifndef PC_SAVE_DIR
#define PC_SAVE_DIR "pnes"
#endif
#ifndef PC_SAVE_FILENAME
#define PC_SAVE_FILENAME "pnes_save"
#endif

// Assembled by the preprocessor, so a separator here would escape the folder.
constexpr bool IsPlainName(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s == '/' || *s == '\\' || *s == ':') return false;
    }
    return true;
}
static_assert(IsPlainName(PC_SAVE_DIR),
    "PC_SAVE_DIR must be a single non-empty name with no '/', '\\' or ':'");
static_assert(IsPlainName(PC_SAVE_FILENAME),
    "PC_SAVE_FILENAME must be a single non-empty name with no '/', '\\' or ':'");

// Storage paths are relative to the user folder and always use '/'.
constexpr char kSaveName[] = PC_SAVE_FILENAME;
constexpr char kTempName[] = PC_SAVE_FILENAME ".tmp";

constexpr u8 kDeviceCount = 1;
constexpr u8 kUserDevice  = 0;

// Same unit as the console backends, so @p size means the same everywhere.
constexpr std::size_t kAllocUnit = 8192;

SDL_Storage* storage    = nullptr;
char*        fullPath   = nullptr;  // for display and as the device's identity
u64          openAt     = 0;
bool         openTried  = false;
constexpr u64 kOpenRetryMs = 2000;

bool haveLocation = false;

constexpr std::size_t RoundUp(const std::size_t n) {
    return ((n + kAllocUnit - 1) / kAllocUnit) * kAllocUnit;
}

// Open failures only happen with no usable home folder, so retry slowly.
bool Open() {
    if (storage) return true;

    const u64 now = SDL_GetTicks();
    if (openTried && now - openAt < kOpenRetryMs) return false;
    openTried = true;
    openAt    = now;

    storage = SDL_OpenUserStorage("", PC_SAVE_DIR, 0);
    if (!storage) return false;

    if (!fullPath) {
        if (char* const pref = SDL_GetPrefPath("", PC_SAVE_DIR)) {
            if (SDL_asprintf(&fullPath, "%s%s", pref, kSaveName) < 0) fullPath = nullptr;
            SDL_free(pref);
        }
    }
    return true;
}

bool Ready() {
    return Open() && SDL_StorageReady(storage);
}

const char* Location() {
    return haveLocation ? fullPath : nullptr;
}

bool IsOurs(const save::Response* const response) {
    const char* const p = (response && response->path && response->path[0]) ? response->path : nullptr;
    return !p || (fullPath && std::strcmp(p, fullPath) == 0);
}

// SDL reports failures as strings, so "absent" is the only cause we can name.
bool Length(u64& out) {
    SDL_PathInfo info;
    if (!SDL_GetStoragePathInfo(storage, kSaveName, &info)) return false;
    if (info.type != SDL_PATHTYPE_FILE) return false;
    out = info.size;
    return true;
}

// Whole-file write to a temp name, then renamed over the save, so a crash
// mid-write leaves the old save intact rather than a torn one.
bool WriteWhole(const void* const src, const std::size_t size, const std::size_t total) {
    u8* const buf = static_cast<u8*>(SDL_calloc(1, total));
    if (!buf) return false;
    std::memcpy(buf, src, size);

    const bool ok = SDL_WriteStorageFile(storage, kTempName, buf, total) &&
                    SDL_RenameStoragePath(storage, kTempName, kSaveName);
    SDL_free(buf);
    if (!ok) SDL_RemoveStoragePath(storage, kTempName);
    return ok;
}

save::eError Transfer(const void* const src, void* const tar, const std::size_t size) {
    if (size == 0)    return save::too_large;
    if (!src && !tar) return save::failed;
    if (!Ready())     return save::no_device;

    u64 len = 0;
    const bool exists = Length(len);

    // Saves replace: a missing or short file is written fresh, never refused.
    if (src) {
        const u64 total = exists && len > RoundUp(size) ? len : RoundUp(size);
        if (!WriteWhole(src, size, static_cast<std::size_t>(total))) return save::failed;
        haveLocation = true;
        return save::found;
    }

    if (!exists) return save::missing;
    if (len < kAllocUnit || size > len) return save::full;
    if (!SDL_ReadStorageFile(storage, kSaveName, tar, size)) return save::failed;

    haveLocation = true;
    return save::found;
}

save::eError Create(const void* const src, const std::size_t size) {
    if (size == 0) return save::too_large;
    if (!src)      return save::failed;
    if (!Ready())  return save::no_device;

    u64 len = 0;
    if (Length(len)) return save::exists;

    if (SDL_GetStorageSpaceRemaining(storage) < RoundUp(size)) return save::full;
    if (!WriteWhole(src, size, RoundUp(size))) return save::failed;

    haveLocation = true;
    return save::found;
}

} // namespace

namespace save {

Response Save(const void* src, const std::size_t size) {
    const eError err = Transfer(src, nullptr, size);
    return { .done = true, .err = err, .path = Location() };
}

Response Load(void* tar, const std::size_t size) {
    const eError err = Transfer(nullptr, tar, size);
    return { .done = true, .err = err, .path = Location() };
}

Response Load(void* tar, const std::size_t size, const Response* response) {
    Open();
    if (!IsOurs(response)) return { .done = true, .err = bad_path, .path = Location() };
    const eError err = Transfer(nullptr, tar, size);
    return { .done = true, .err = err, .path = Location() };
}

Response CreateSave(const void* src, const std::size_t size, const Response* response) {
    Open();
    if (!IsOurs(response)) return { .done = true, .err = bad_path, .path = Location() };
    const eError err = Create(src, size);
    return { .done = true, .err = err, .path = Location() };
}

Response GetSaveLocation() {
    if (!Open())                    { haveLocation = false; return { .done = true,  .err = no_device, .path = nullptr }; }
    if (!SDL_StorageReady(storage)) return { .done = false, .err = busy, .path = nullptr };

    u64 len = 0;
    haveLocation = Length(len);
    return { .done = true, .err = haveLocation ? found : missing, .path = Location() };
}

Response Delete() {
    if (!Ready()) return { .done = true, .err = no_device, .path = nullptr };

    haveLocation = false;
    u64 len = 0;
    if (!Length(len)) return { .done = true, .err = missing, .path = nullptr };
    if (!SDL_RemoveStoragePath(storage, kSaveName)) return { .done = true, .err = failed, .path = nullptr };
    return { .done = true, .err = found, .path = nullptr };
}

Response GetMetaData(void*) {
    return { .done = true, .err = no_device, .path = nullptr };
}

Response SetMetaData(const void*) {
    return { .done = true, .err = no_device, .path = nullptr };
}

u8 AccessibleDevices() {
    return Ready() ? static_cast<u8>(1u << kUserDevice) : 0;
}

Response GetDeviceStatus(const u8 index) {
    if (index >= kDeviceCount) return { .done = true, .err = no_device, .path = nullptr };
    if (!Open())               return { .done = true, .err = no_device, .path = nullptr };
    if (!SDL_StorageReady(storage)) return { .done = false, .err = busy, .path = nullptr };
    return { .done = true, .err = found, .path = fullPath };
}

// An existing save big enough counts as free; otherwise ask SDL for headroom.
u8 FreeDevices(const std::size_t size) {
    if (size == 0 || !Ready()) return 0;

    const u64 want = RoundUp(size);
    u64 held = 0;
    if (Length(held) && held >= want) return static_cast<u8>(1u << kUserDevice);

    // The temp file is written beside the old save, so the whole size is needed.
    return SDL_GetStorageSpaceRemaining(storage) >= want ? static_cast<u8>(1u << kUserDevice) : 0;
}

} // namespace save
