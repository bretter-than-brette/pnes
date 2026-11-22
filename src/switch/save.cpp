/**
 * @file save.cpp
 * @brief Switch save/load/create/locate: a file on the SD card via libnx's
 *        fsdev devoptab. Blocking stdio, so nothing is ticked -- every call
 *        reports done on its first turn and save::busy is never returned.
 *        See .ignore/comment-trim-salvage/save-backends.md for why not savedata.
 */
#include "internal.hpp"
#include <pnes/save.hpp>

#include <switch.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* PathOf(const save::Response* const response) {
    return (response && response->path && response->path[0]) ? response->path : nullptr;
}

#ifndef NX_SAVE_DIR
#define NX_SAVE_DIR "pnes"
#endif
#ifndef NX_SAVE_FILENAME
#define NX_SAVE_FILENAME "pnes_save"
#endif

// The path is assembled by the preprocessor, so a slash or colon here would
// silently point it at another app's directory.
constexpr bool IsPlainName(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s == '/' || *s == '\\' || *s == ':') return false;
    }
    return true;
}
static_assert(IsPlainName(NX_SAVE_DIR),
    "NX_SAVE_DIR must be a single non-empty name with no '/', '\\' or ':'");
static_assert(IsPlainName(NX_SAVE_FILENAME),
    "NX_SAVE_FILENAME must be a single non-empty name with no '/', '\\' or ':'");
static_assert(sizeof(NX_SAVE_DIR) <= 256, "NX_SAVE_DIR must be at most 255 characters");
static_assert(sizeof(NX_SAVE_FILENAME) <= 256, "NX_SAVE_FILENAME must be at most 255 characters");

constexpr char kDevice[]  = "sdmc";
constexpr char kHomebrew[] = "sdmc:/switch";
constexpr char kSaveDir[]  = "sdmc:/switch/" NX_SAVE_DIR;
constexpr char kSaveFile[] = "sdmc:/switch/" NX_SAVE_DIR "/" NX_SAVE_FILENAME;

constexpr u8 kDeviceCount = 1;
static_assert(kDeviceCount <= 8, "::save::AccessibleDevices returns the mask in a u8");

constexpr u8 kSdDevice = 0;

constexpr u8 kNoDevice = 0xFF;

// The GameCube's sector size, so @p size means the same prefix length on every
// backend. Not a limit -- a bigger save just takes more units.
constexpr std::size_t kAllocUnit = 8192;

const u8 kZeros[256] = { };

// libnx's startup mounts the card, so this almost never runs. Throttled because
// save.hpp invites a UI to poll a few times a second.
u64  mountAt    = 0;
bool mountTried = false;
constexpr u64 kMountRetryNs = 2000ull * 1000ull * 1000ull;  // 2s

save::eError lastErr = save::found;

bool        freeValid = false;
u8          freeMask  = 0;
std::size_t freeSize  = 0;

bool haveLocation = false;

constexpr std::size_t RoundUp(const std::size_t n) {
    return ((n + kAllocUnit - 1) / kAllocUnit) * kAllocUnit;
}

// fsdev translates the fs service's Result codes into newlib errno, so this
// matches the Wii's. ENOENT is "no save yet" rather than bad_path: the path is
// this file's own and ParseDevice refuses a caller's before any syscall.
save::eError MapErrno(const int e) {
    switch (e) {
        case 0:            return save::found;
        case ENOENT:       return save::missing;
        case EEXIST:       return save::exists;
        case ENOSPC:
        case EDQUOT:       return save::full;
        case EACCES:
        case EPERM:
        case EROFS:        return save::denied;
        case EINVAL:
        case ENAMETOOLONG: return save::bad_path;
        // ENOMEDIUM is behind a guard: newlib declares it only under
        // __LINUX_ERRNO_EXTENSIONS__, which devkitA64 does not define.
        case ENODEV:
        case ENXIO:
#ifdef ENOMEDIUM
        case ENOMEDIUM:
#endif
                           return save::no_device;
        default:           return save::failed;
    }
}

save::eError Fail(const save::eError err) {
    lastErr = err;
    return err;
}

save::eError Won() {
    lastErr = save::found;
    return save::found;
}

bool Mounted() {
    return fsdevGetDeviceFileSystem(kDevice) != nullptr;
}

// The mount is libnx's, not ours, so there is nothing to release. The slow call
// here; status polls stop at Mounted(), a device-table lookup.
bool Mount() {
    if (Mounted()) return true;

    const u64 now = armTicksToNs(armGetSystemTick());
    if (mountTried && now - mountAt < kMountRetryNs) return false;
    mountTried = true;
    mountAt    = now;

    if (R_FAILED(fsdevMountSdmc())) {
        freeValid = false;
        return false;
    }
    return true;
}

u8 ParseDevice(const char* const path) {
    if (!path) return kNoDevice;
    return std::strcmp(path, kSaveFile) == 0 ? kSdDevice : kNoDevice;
}

bool WriteAll(const int fd, const void* const src, std::size_t n) {
    const u8* p = static_cast<const u8*>(src);
    while (n) {
        const ssize_t w = write(fd, p, n);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

bool ReadAll(const int fd, void* const tar, std::size_t n) {
    u8* p = static_cast<u8*>(tar);
    while (n) {
        const ssize_t r = read(fd, p, n);
        if (r <= 0) return false;
        p += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

// Writes the whole unit every time, so no tail of a longer earlier save
// survives. Not ftruncate(), which would not overwrite a file already long
// enough.
bool PadTo(const int fd, std::size_t from, const std::size_t to) {
    while (from < to) {
        const std::size_t span = to - from;
        const std::size_t n    = span < sizeof kZeros ? span : sizeof kZeros;
        if (!WriteAll(fd, kZeros, n)) return false;
        from += n;
    }
    return true;
}

off_t LengthOf(const char* const path, int& errOut) {
    struct stat st;
    if (stat(path, &st) != 0) {
        errOut = errno;
        return -1;
    }
    errOut = 0;
    return st.st_size;
}

// One function both ways, because the length check must be shared: a save and a
// load disagreeing about the file's length is how a prefix gets written that can
// never be read back. fsync is part of the && -- a save is the one moment a
// player may hold the power button.
save::eError Transfer(const void* const src, void* const tar, const std::size_t size) {
    if (!Mount()) return Fail(save::no_device);

    const int fd = open(kSaveFile, src ? O_WRONLY : O_RDONLY);
    if (fd < 0) {
        const int e = errno;
        return Fail(MapErrno(e));
    }

    const off_t len = lseek(fd, 0, SEEK_END);
    if (len < 0) {
        const int e = errno;
        close(fd);
        return Fail(MapErrno(e));
    }
    if (static_cast<std::size_t>(len) < kAllocUnit || size > static_cast<std::size_t>(len)) {
        close(fd);
        return Fail(save::full);
    }
    if (lseek(fd, 0, SEEK_SET) != 0) {
        const int e = errno;
        close(fd);
        return Fail(MapErrno(e));
    }

    const bool ok = src ? (WriteAll(fd, src, size) &&
                           PadTo(fd, size, RoundUp(size)) &&
                           fsync(fd) == 0)
                        : ReadAll(fd, tar, size);
    const int e = ok ? 0 : errno;
    if (close(fd) != 0 && ok) {
        const int ce = errno;
        return Fail(MapErrno(ce));
    }
    if (!ok) return Fail(MapErrno(e));

    haveLocation = true;
    if (src) freeValid = false;
    return Won();
}

save::eError SaveOrLoad(const void* const src, void* const tar,
                        const std::size_t size, const char* const path) {
    // size == 0 is too_large because that is what the GameCube answers for it.
    if (size == 0)    return Fail(save::too_large);
    if (!src && !tar) return Fail(save::failed);

    if (path && ParseDevice(path) == kNoDevice) return Fail(save::bad_path);

    return Transfer(src, tar, size);
}

// O_EXCL so "does it exist" and "create it" are one operation. Two mkdirs
// because mkdir makes no parents. A half-written save is unlinked: left behind,
// the next locate would offer the player a corrupt game rather than a new one.
save::eError Create(const void* const src, const std::size_t size, const char* const path) {
    if (size == 0) return Fail(save::too_large);
    if (!src)      return Fail(save::failed);

    if (path && ParseDevice(path) == kNoDevice) return Fail(save::bad_path);
    if (!Mount())                               return Fail(save::no_device);

    for (const char* const dir : { kHomebrew, kSaveDir }) {
        if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
            const int e = errno;
            return Fail(MapErrno(e));
        }
    }

    const int fd = open(kSaveFile, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        const int e = errno;
        return Fail(MapErrno(e));
    }

    const bool ok = WriteAll(fd, src, size) &&
                    PadTo(fd, size, RoundUp(size)) &&
                    fsync(fd) == 0;
    const int e = ok ? 0 : errno;
    if (close(fd) != 0 && ok) {
        const int ce = errno;
        unlink(kSaveFile);
        return Fail(MapErrno(ce));
    }
    if (!ok) {
        unlink(kSaveFile);
        return Fail(MapErrno(e));
    }

    haveLocation = true;
    freeValid    = false;
    return Won();
}

} // namespace

namespace save {

Response Save(const void* src, const std::size_t size) {
    return { .done = true,
             .err  = SaveOrLoad(src, nullptr, size, nullptr),
             .path = haveLocation ? kSaveFile : nullptr };
}

Response Load(void* tar, const std::size_t size) {
    return { .done = true,
             .err  = SaveOrLoad(nullptr, tar, size, nullptr),
             .path = haveLocation ? kSaveFile : nullptr };
}

Response Load(void* tar, const std::size_t size, const Response* response) {
    return { .done = true,
             .err  = SaveOrLoad(nullptr, tar, size, PathOf(response)),
             .path = haveLocation ? kSaveFile : nullptr };
}

Response CreateSave(const void* src, const std::size_t size, const Response* response) {
    return { .done = true,
             .err  = Create(src, size, PathOf(response)),
             .path = haveLocation ? kSaveFile : nullptr };
}

Response GetSaveLocation() {
    if (!Mount()) {
        haveLocation = false;
        return { .done = true, .err = Fail(no_device), .path = nullptr };
    }

    int e = 0;
    if (LengthOf(kSaveFile, e) >= 0) {
        haveLocation = true;
        Won();
        return { .done = true, .err = found, .path = kSaveFile };
    }

    haveLocation = false;
    return { .done = true, .err = Fail(MapErrno(e)), .path = nullptr };
}

Response Delete() {
    if (!Mount()) return { .done = true, .err = Fail(no_device), .path = nullptr };

    errno = 0;
    const int removed = unlink(kSaveFile);

    haveLocation = false;
    freeValid    = false;

    if (removed < 0) return { .done = true, .err = Fail(MapErrno(errno)), .path = nullptr };

    Won();
    return { .done = true, .err = found, .path = nullptr };
}

Response GetMetaData(void*) {
    return { .done = true, .err = Fail(no_device), .path = nullptr };
}

Response SetMetaData(const void*) {
    return { .done = true, .err = Fail(no_device), .path = nullptr };
}

u8 AccessibleDevices() {
    return Mounted() ? static_cast<u8>(1u << kSdDevice) : 0;
}

Response GetDeviceStatus(const u8 index) {
    if (index >= kDeviceCount) return { .done = true, .err = no_device, .path = nullptr };
    if (!Mounted())            return { .done = true, .err = no_device, .path = nullptr };
    return { .done = true, .err = found, .path = kSaveFile };
}

// Not a sweep: one IPC call with no ticked form, so it settles immediately.
// fsFsGetFreeSpace rather than fsdev's statvfs, which answers in units of its
// own choosing. An existing save big enough counts as free, since overwriting
// consumes nothing.
u8 FreeDevices(const std::size_t size) {
    if (size == 0) return 0;
    if (freeValid && freeSize == size) return freeMask;
    if (!Mount()) return 0;

    FsFileSystem* const fs = fsdevGetDeviceFileSystem(kDevice);
    if (!fs) return 0;

    s64 avail = 0;
    if (R_FAILED(fsFsGetFreeSpace(fs, "/", &avail)) || avail < 0) return 0;

    const std::size_t want = RoundUp(size);

    int         e       = 0;
    const off_t already = LengthOf(kSaveFile, e);
    const u64   held    = already > 0 ? static_cast<u64>(already) : 0;
    const u64   need    = held >= want ? 0 : want - held;

    freeMask  = static_cast<u64>(avail) >= need ? static_cast<u8>(1u << kSdDevice) : 0;
    freeSize  = size;
    freeValid = true;
    return freeMask;
}

} // namespace save
