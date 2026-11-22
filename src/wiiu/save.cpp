/**
 * @file save.cpp
 * @brief Wii U save/load/create/locate: a file on the SD card via wut's FSA
 *        devoptab. The Switch backend's shape, blocking and unticked. The SD
 *        card and not /vol/save, which is per account and not a devoptab path
 *        at all. ProcUI takes filesystem access away for the HOME menu, so a
 *        transfer caught by that fails and a retry works. See
 *        .ignore/comment-trim-salvage/save-backends.md.
 */
#include "internal.hpp"
#include <pnes/save.hpp>

#include <coreinit/time.h>
#include <whb/sdcard.h>

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

#ifndef WIIU_SAVE_DIR
#define WIIU_SAVE_DIR "pnes"
#endif
#ifndef WIIU_SAVE_FILENAME
#define WIIU_SAVE_FILENAME "pnes_save"
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
static_assert(IsPlainName(WIIU_SAVE_DIR),
    "WIIU_SAVE_DIR must be a single non-empty name with no '/', '\\' or ':'");
static_assert(IsPlainName(WIIU_SAVE_FILENAME),
    "WIIU_SAVE_FILENAME must be a single non-empty name with no '/', '\\' or ':'");
static_assert(sizeof(WIIU_SAVE_DIR) <= 256, "WIIU_SAVE_DIR must be at most 255 characters");
static_assert(sizeof(WIIU_SAVE_FILENAME) <= 256, "WIIU_SAVE_FILENAME must be at most 255 characters");

// Two spellings of one place: the bare path for Cafe OS, the "fs:" prefix for
// stdio. Spelt out rather than left to the default device, which later code can
// change and whose cwd wut only sets if the startup mount succeeded.
constexpr char kMountPath[] = "/vol/external01";
constexpr char kMountPoint[] = "fs:/vol/external01";
constexpr char kHomebrew[]  = "fs:/vol/external01/wiiu";
constexpr char kSaveDir[]   = "fs:/vol/external01/wiiu/" WIIU_SAVE_DIR;
constexpr char kSaveFile[]  = "fs:/vol/external01/wiiu/" WIIU_SAVE_DIR "/" WIIU_SAVE_FILENAME;

constexpr u8 kDeviceCount = 1;
static_assert(kDeviceCount <= 8, "::save::AccessibleDevices returns the mask in a u8");

constexpr u8 kSdDevice = 0;

constexpr u8 kNoDevice = 0xFF;

// The GameCube's sector size, so @p size means the same prefix length on every
// backend. Not a limit -- a bigger save just takes more units.
constexpr std::size_t kAllocUnit = 8192;

const u8 kZeros[256] = { };

// wut's startup mounts the card, so this covers the one case it cannot: a card
// pushed in after the title launched. __init_wut_devoptab runs once.
u64  mountAt    = 0;
bool mountTried = false;
constexpr u64 kMountRetryMs = 2000;

save::eError lastErr = save::found;

bool haveLocation = false;

constexpr std::size_t RoundUp(const std::size_t n) {
    return ((n + kAllocUnit - 1) / kAllocUnit) * kAllocUnit;
}

// EBUSY is not save::busy: wut maps five unrelated FSA failures onto it and none
// of them means "not finished yet". EIO/EILSEQ stay failed -- a card pulled
// mid-write and a bad sector are indistinguishable, and a retry is the answer.
save::eError MapErrno(const int e) {
    switch (e) {
        case 0:            return save::found;
        case ENOENT:       return save::missing;
        case EEXIST:       return save::exists;
        case ENOSPC:
        case EDQUOT:       return save::full;
        case EFBIG:        return save::too_large;
        case EACCES:
        case EPERM:
        case EROFS:        return save::denied;
        case EINVAL:
        case ENAMETOOLONG: return save::bad_path;
        // ENOMEDIUM is behind a guard: newlib declares it only under
        // __LINUX_ERRNO_EXTENSIONS__, which devkitPPC does not define.
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

// A stat, because FindDevice("fs") is true whether or not the mount behind it
// succeeded: wut registers the devoptab first and keeps the real flag private.
bool Mounted() {
    struct stat st;
    return stat(kMountPoint, &st) == 0;
}

// WHBMountSdCard rather than FSAMount: public, already linked, and its own FS
// client. A Cafe OS mount belongs to the process, so the devoptab sees one this
// did not make. The path is checked because every path here assumes it.
bool Mount() {
    if (Mounted()) return true;

    const u64 now = OSTicksToMilliseconds(OSGetSystemTime());
    if (mountTried && now - mountAt < kMountRetryMs) return false;
    mountTried = true;
    mountAt    = now;

    if (!WHBMountSdCard()) return false;

    const char* const where = WHBGetSdCardMountPath();
    if (!where || std::strcmp(where, kMountPath) != 0) return false;

    return Mounted();
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

// One function both ways, so the length check cannot disagree between them.
// O_RDWR for a write: wut picks a Cafe OS fopen mode from the POSIX flags and
// has none for "write, do not truncate, do not create", so O_WRONLY is EINVAL.
save::eError Transfer(const void* const src, void* const tar, const std::size_t size) {
    if (!Mount()) return Fail(save::no_device);

    const int fd = open(kSaveFile, src ? O_RDWR : O_RDONLY);
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

// O_EXCL so "does it exist" and "create it" are one operation. O_WRONLY is fine
// here where it is not in Transfer: wut does map O_WRONLY|O_CREAT|O_EXCL. A
// half-written save is unlinked, or the next locate offers a corrupt game.
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

// Free space is not reachable from a Wii U application at all (salvage notes),
// so this reports accessibility and a full card still shows its bit -- the write
// then answers save::full.
u8 FreeDevices(const std::size_t size) {
    if (size == 0) return 0;
    return AccessibleDevices();
}

} // namespace save
