/**
 * @file save.cpp
 * @brief PSP save/load/create/locate: a file beside the EBOOT on whichever
 *        device the game was launched from. Blocking and unticked.
 *
 * Two traps, both written up in .ignore/comment-trim-salvage/save-backends.md:
 * errno is only set for one sce error family (see Native), and the save's home
 * has to be discovered rather than compiled in (see Resolve).
 */
#include "internal.hpp"
#include <pnes/save.hpp>

// pspiofilemgr.h first: pspmscm.h is static inlines calling sceIoDevctl and
// naming SceUID without including anything itself.
#include <pspiofilemgr.h>
#include <pspiofilemgr_devctl.h>
#include <pspmscm.h>

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

#ifndef PSP_SAVE_DIR
#define PSP_SAVE_DIR "pnes"
#endif
#ifndef PSP_SAVE_FILENAME
#define PSP_SAVE_FILENAME "pnes_save"
#endif

// kFallbackDir is assembled by the preprocessor and Resolve appends to a
// directory it did not choose, so a colon here can name another device.
constexpr bool IsPlainName(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s == '/' || *s == '\\' || *s == ':') return false;
    }
    return true;
}
static_assert(IsPlainName(PSP_SAVE_DIR),
    "PSP_SAVE_DIR must be a single non-empty name with no '/', '\\' or ':'");
static_assert(IsPlainName(PSP_SAVE_FILENAME),
    "PSP_SAVE_FILENAME must be a single non-empty name with no '/', '\\' or ':'");

// libcglue's __path_absolute copies into a 256-byte buffer before sceIoOpen, so
// a longer path is silently truncated into naming a different file.
constexpr std::size_t kPathMax = 256;

constexpr char kFallbackDir[] = "ms0:/PSP/GAME/" PSP_SAVE_DIR;
static_assert(sizeof kFallbackDir + sizeof(PSP_SAVE_FILENAME) <= kPathMax,
    "PSP_SAVE_DIR and PSP_SAVE_FILENAME must fit the fallback path in " "256 bytes");

constexpr char kRemovablePrefix[] = "ms";

// One, though the console has two media: the save belongs beside the EBOOT, and
// the EBOOT is on exactly one of them. So index 0 means "the device this game is
// running from" rather than "the stick".
constexpr u8 kDeviceCount = 1;
static_assert(kDeviceCount <= 8, "::save::AccessibleDevices returns the mask in a u8");

constexpr u8 kBootDevice = 0;

constexpr u8 kNoDevice = 0xFF;

// The GameCube's sector size, so @p size means the same prefix length on every
// backend. Not a limit -- a bigger save just takes more units.
constexpr std::size_t kAllocUnit = 8192;

const u8 kZeros[256] = { };

char savePath[kPathMax]  = { };
char saveDevice[16]      = { };
bool removable           = false;

bool resolved   = false;
bool resolvedOk = false;

save::eError lastErr = save::found;

bool        freeValid = false;
u8          freeMask  = 0;
std::size_t freeSize  = 0;

bool haveLocation = false;

constexpr std::size_t RoundUp(const std::size_t n) {
    return ((n + kAllocUnit - 1) / kAllocUnit) * kAllocUnit;
}

// pspsdk stores the low half of an 0x8001xxxx sce error in errno, which is a
// real POSIX errno by construction. Anything outside that family never reaches
// here -- see Native.
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
        // __LINUX_ERRNO_EXTENSIONS__, which pspdev does not define.
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

// The one thing to carry away from this file. An sce error outside 0x8001xxxx is
// returned unchanged, as a large negative number, with errno left alone -- so a
// failure is `< 0` and never `== -1`, and errno means nothing unless it was
// cleared immediately before the call.
save::eError Native() {
    const int e = errno;
    return e ? Fail(MapErrno(e)) : Fail(save::failed);
}

// getcwd is the answer and not a guess: pspsdk's startup chdir'd into the
// EBOOT's own directory before main, so this is ms0:/PSP/GAME/<folder>/ or a Go's
// ef0:/..., which no build knob could tell apart. It also puts the save where the
// game is, so it travels when a player copies the folder. A path that would not
// fit is refused rather than truncated, for the life of the process.
bool Resolve() {
    if (resolved) return resolvedOk;
    resolved = true;

    char home[kPathMax] = { };
    if (!getcwd(home, sizeof home) || !std::strchr(home, ':')) {
        std::memcpy(home, kFallbackDir, sizeof kFallbackDir);
    }

    std::size_t n = std::strlen(home);
    while (n && home[n - 1] == '/') home[--n] = '\0';

    const char* const colon = std::strchr(home, ':');
    const std::size_t dev   = static_cast<std::size_t>(colon - home) + 1;
    if (n == 0 || dev >= sizeof saveDevice) return false;
    std::memcpy(saveDevice, home, dev);
    saveDevice[dev] = '\0';

    if (n + 1 + sizeof(PSP_SAVE_FILENAME) > kPathMax) return false;
    std::memcpy(savePath, home, n);
    savePath[n] = '/';
    std::memcpy(savePath + n + 1, PSP_SAVE_FILENAME, sizeof(PSP_SAVE_FILENAME));

    removable  = std::strncmp(saveDevice, kRemovablePrefix, sizeof kRemovablePrefix - 1) == 0;
    resolvedOk = true;
    return true;
}

// A stick is removable and the kernel will say so; internal flash is soldered
// on, so that answers true without a call. This is where the other backends'
// Mount would be, and there is nothing for one to do: the kernel mounts it.
bool Inserted() {
    if (!removable) return true;
    return MScmIsMediumInserted() == 1;
}

bool Ready() {
    return Resolve() && Inserted();
}

u8 ParseDevice(const char* const path) {
    if (!path || !resolvedOk) return kNoDevice;
    return std::strcmp(path, savePath) == 0 ? kBootDevice : kNoDevice;
}

// `w <= 0` and not `== -1`, for Native's reason.
bool WriteAll(const int fd, const void* const src, std::size_t n) {
    const u8* p = static_cast<const u8*>(src);
    while (n) {
        const int w = write(fd, p, n);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

bool ReadAll(const int fd, void* const tar, std::size_t n) {
    u8* p = static_cast<u8*>(tar);
    while (n) {
        const int r = read(fd, p, n);
        if (r <= 0) return false;
        p += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

// Separate from the && it sits in, so the errno it reports is its own.
bool Flush(const int fd) {
    errno = 0;
    const int r = fsync(fd);
    if (r < 0) return false;
    return true;
}

// Writes the whole unit every time, so no tail of a longer earlier save
// survives. pspsdk has no ftruncate to consider anyway.
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
    errno = 0;
    const int ret = stat(path, &st);
    if (ret < 0) {
        errOut = errno ? errno : ret;
        return -1;
    }
    errOut = 0;
    return st.st_size;
}

// Best effort, and open() is the judge: a mkdir failing outside the 0x8001xxxx
// family leaves errno untouched, so "already exists" and "the stick is full" look
// identical, and reporting the wrong one is worse than saying nothing.
void MakeDirs(const char* const path) {
    char buf[kPathMax];
    const std::size_t n = std::strlen(path);
    if (n >= sizeof buf) return;
    std::memcpy(buf, path, n + 1);

    char* p = std::strchr(buf, ':');
    p = p ? p + 1 : buf;
    if (*p == '/') ++p;

    for (; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(buf, 0777);
        *p = '/';
    }
    mkdir(buf, 0777);
}

void DirOf(char* const dir) {
    const std::size_t n = std::strlen(savePath);
    std::memcpy(dir, savePath, n + 1);
    char* const slash = std::strrchr(dir, '/');
    if (slash) *slash = '\0';
}

// One function both ways, so the length check cannot disagree between them.
// O_WRONLY is correct here, unlike on the Wii U: pspsdk maps the POSIX access
// mode straight onto the kernel's. No O_TRUNC -- the padding guarantees the
// unit's contents, and truncating first would leave a crash mid-save short.
save::eError Transfer(const void* const src, void* const tar, const std::size_t size) {
    if (!Resolve())  return Fail(save::bad_path);
    if (!Inserted()) return Fail(save::no_device);

    errno = 0;
    const int fd = open(savePath, src ? O_WRONLY : O_RDONLY);
    if (fd < 0) return Native();

    errno = 0;
    const off_t len = lseek(fd, 0, SEEK_END);
    if (len < 0) {
        const save::eError err = Native();
        close(fd);
        return err;
    }
    if (static_cast<std::size_t>(len) < kAllocUnit || size > static_cast<std::size_t>(len)) {
        close(fd);
        return Fail(save::full);
    }
    errno = 0;
    const off_t back = lseek(fd, 0, SEEK_SET);
    if (back != 0) {
        const save::eError err = Native();
        close(fd);
        return err;
    }

    errno = 0;
    const bool ok = src ? (WriteAll(fd, src, size) &&
                           PadTo(fd, size, RoundUp(size)) &&
                           Flush(fd))
                        : ReadAll(fd, tar, size);
    // Captured before close(), which clobbers errno on its own account.
    const int e = ok ? 0 : errno;
    errno = 0;
    const int closed = close(fd);
    if (closed < 0 && ok) return Native();
    if (!ok) {
        errno = e;
        return Native();
    }

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

// O_EXCL is PSP_O_EXCL handed to the FAT driver rather than a check the C
// library performs, so the stat is belt and braces: a driver that ignored it
// would turn "create" into "overwrite" and lose a player's save. The directories
// stay behind -- here one is almost always the EBOOT's own.
save::eError Create(const void* const src, const std::size_t size, const char* const path) {
    if (size == 0) return Fail(save::too_large);
    if (!src)      return Fail(save::failed);

    if (path && ParseDevice(path) == kNoDevice) return Fail(save::bad_path);
    if (!Resolve())                             return Fail(save::bad_path);
    if (!Inserted())                            return Fail(save::no_device);

    int e = 0;
    if (LengthOf(savePath, e) >= 0) return Fail(save::exists);

    char dir[kPathMax];
    DirOf(dir);
    MakeDirs(dir);

    errno = 0;
    const int fd = open(savePath, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return Native();

    errno = 0;
    const bool ok = WriteAll(fd, src, size) &&
                    PadTo(fd, size, RoundUp(size)) &&
                    Flush(fd);
    const int err = ok ? 0 : errno;
    errno = 0;
    const int closed = close(fd);
    if (closed < 0 && ok) {
        const save::eError code = Native();
        unlink(savePath);
        return code;
    }
    if (!ok) {
        errno = err;
        const save::eError code = Native();
        unlink(savePath);
        return code;
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
             .path = haveLocation ? savePath : nullptr };
}

Response Load(void* tar, const std::size_t size) {
    return { .done = true,
             .err  = SaveOrLoad(nullptr, tar, size, nullptr),
             .path = haveLocation ? savePath : nullptr };
}

Response Load(void* tar, const std::size_t size, const Response* response) {
    return { .done = true,
             .err  = SaveOrLoad(nullptr, tar, size, PathOf(response)),
             .path = haveLocation ? savePath : nullptr };
}

Response CreateSave(const void* src, const std::size_t size, const Response* response) {
    return { .done = true,
             .err  = Create(src, size, PathOf(response)),
             .path = haveLocation ? savePath : nullptr };
}

Response GetSaveLocation() {
    if (!Resolve()) {
        haveLocation = false;
        return { .done = true, .err = Fail(bad_path), .path = nullptr };
    }
    if (!Inserted()) {
        haveLocation = false;
        return { .done = true, .err = Fail(no_device), .path = nullptr };
    }

    int e = 0;
    if (LengthOf(savePath, e) >= 0) {
        haveLocation = true;
        Won();
        return { .done = true, .err = found, .path = savePath };
    }

    haveLocation = false;
    errno = e;
    return { .done = true, .err = Native(), .path = nullptr };
}

Response Delete() {
    if (!Resolve())  return { .done = true, .err = Fail(bad_path), .path = nullptr };
    if (!Inserted()) return { .done = true, .err = Fail(no_device), .path = nullptr };

    errno = 0;
    const int removed = unlink(savePath);

    haveLocation = false;
    freeValid    = false;

    if (removed < 0) return { .done = true, .err = Native(), .path = nullptr };

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
    return Ready() ? static_cast<u8>(1u << kBootDevice) : 0;
}

Response GetDeviceStatus(const u8 index) {
    if (index >= kDeviceCount) return { .done = true, .err = no_device, .path = nullptr };
    if (!Ready())              return { .done = true, .err = no_device, .path = nullptr };
    return { .done = true, .err = found, .path = savePath };
}

// Not a sweep: one devctl with no ticked form. It answers in clusters, and
// freeClusters * sectorCount * sectorSize passes 32 bits on any stick over a few
// GB, so the widening happens first. A failed devctl reports the device FREE
// rather than full: a PSP Go's ef0: need not implement SCE_PR_GETDEV at all, and
// returning 0 would tell such a console it can never save.
u8 FreeDevices(const std::size_t size) {
    if (size == 0) return 0;
    if (freeValid && freeSize == size) return freeMask;
    if (!Ready()) return 0;

    const u8 present = static_cast<u8>(1u << kBootDevice);

    SceDevInf    inf = { };
    SceDevctlCmd cmd = { &inf };
    if (sceIoDevctl(saveDevice, SCE_PR_GETDEV, &cmd, sizeof cmd, nullptr, 0) < 0 ||
        inf.sectorSize <= 0 || inf.sectorCount <= 0) {
        freeMask  = present;
        freeSize  = size;
        freeValid = true;
        return freeMask;
    }

    const u64 avail = static_cast<u64>(inf.freeClusters) *
                      static_cast<u64>(inf.sectorCount) *
                      static_cast<u64>(inf.sectorSize);

    const std::size_t want = RoundUp(size);

    int         e       = 0;
    const off_t already = LengthOf(savePath, e);
    const u64   held    = already > 0 ? static_cast<u64>(already) : 0;
    const u64   need    = held >= want ? 0 : want - held;

    freeMask  = avail >= need ? present : 0;
    freeSize  = size;
    freeValid = true;
    return freeMask;
}

} // namespace save
