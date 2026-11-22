/**
 * @file save.cpp
 * @brief Save/load/create/locate for the native Nintendo Wii U, as a file on
 *        the SD card reached through wut's FSA devoptab.
 *
 * THE FIFTH BACKEND, AND IT IS THE SWITCH'S AND THE 3DS'S AGAIN: blocking
 * stdio against a single device the C runtime mounted before main() ran. wut's
 * own devoptab startup (__init_wut_devoptab) registers a devoptab called "fs",
 * makes it the default device, FSAMounts /dev/sdcard01 onto /vol/external01 and
 * chdirs into it -- so by the time a game calls anything here, the card is
 * mounted exactly as libnx's and libctru's startups leave theirs. Everything
 * below is src/switch/save.cpp's shape, and the five places it is NOT that
 * file's shape say so where they are:
 *
 *   1. ::Mounted IS A stat() OF THE MOUNT POINT, which is the Wii's workaround
 *      and not the Switch's one-pointer lookup. wut publishes no "is the SD
 *      mounted" entry point: the flag exists (__wut_fsa_device_t::mounted) and
 *      is private to the devoptab, and FindDevice("fs") -- the 3DS backend's
 *      test -- answers the WRONG QUESTION here, because "fs" is registered
 *      whether or not the mount behind it succeeded. See ::Mounted for the cost.
 *   2. ::Mount RETRIES WITH WHBMountSdCard(), wut's own public SD mount, rather
 *      than with the FSAMount its devoptab used. See ::Mount.
 *   3. ::Transfer OPENS A SAVE FOR WRITING WITH O_RDWR AND NOT O_WRONLY, which
 *      is not a style choice: wut's devoptab maps POSIX open flags onto Cafe
 *      OS's fopen-style modes and has NO MODE for "write, do not truncate,
 *      do not create" -- a bare O_WRONLY is EINVAL. See ::Transfer.
 *   4. ::save::FreeDevices CANNOT MEASURE ANYTHING on this platform and says so
 *      rather than pretending. It is the only one of the five backends that
 *      reports a mask it did not narrow by capacity; the reason is wut's, not
 *      this file's, and it is written out in full there.
 *   5. The save lives under /vol/external01/wiiu/, which is this console's
 *      homebrew convention in the place sdmc:/switch/ and sdmc:/3ds/ are the
 *      other two's.
 *
 * WHY THE SD CARD AND NOT /vol/save. The Wii U has a real per-title save
 * concept -- a quota'd savedata volume mounted at /vol/save, reached through
 * nn_save (SAVEInit, SAVEInitSaveDir) and the FS/FSA file calls, listed by the
 * system's own save-data manager and copied by its save transfer tool -- and
 * this backend deliberately does not use it. THREE THINGS ARGUE AGAINST IT
 * HERE. It is addressed per account (SAVEInitSaveDir takes a slot number) and
 * per title, and the demo's title id is a placeholder a project is expected to
 * override (WIIU_TITLE_ID, CMakeLists.txt), so a save written under one build's
 * id is not found by the next. It is NOT A DEVOPTAB PATH: nothing under
 * /vol/save is reachable with open/read/write, so a savedata backend is FSA
 * calls and cmd blocks rather than the 270-odd lines below. And it needs a quota
 * flush (FSAFlushQuota) to be durable, which is a commit step with no
 * counterpart in the stdio flow every other filesystem backend here shares.
 * The SD card is what a .wuhb actually has, and sd:/wiiu/ is where every other
 * piece of Wii U homebrew keeps its data -- so that is where this one keeps a
 * save.
 *
 * A BUILD THAT WANTED THE SYSTEM'S SAVE MANAGER WOULD THEREFORE NOT CHANGE
 * THREE LINES the way the Switch's and the 3DS's headers describe for their
 * installed-title forms; it would be a different file. That asymmetry is worth
 * stating because the other two backends' comments invite the reader to expect
 * the easy version of this paragraph.
 *
 * NOTHING IS TICKED, FOR THE OTHER THREE FILESYSTEM BACKENDS' REASON. The
 * devoptab is blocking and has no "advance this transfer by one step" entry
 * point to build a state machine out of, so every call finishes the whole
 * operation and reports `done` on its first tick. A caller's
 *
 *     while (!response.done) { ...one frame... }
 *
 * loop therefore runs exactly once -- the same loop, with the same meaning, as
 * on the GameCube where it runs several times. save::busy is declared on this
 * target (save.hpp gates the vocabulary on SAVE_DEVICE) so that a game written
 * against the GameCube compiles here, and is never returned. THAT IS WORTH
 * SAYING TWICE ON THIS PLATFORM: wut translates five distinct FSA failures to
 * EBUSY (FS_ERROR_BUSY, ALREADY_OPEN, JOURNAL_FULL, UNAVAILABLE_COMMAND,
 * INVALID_PARAM), and ::MapErrno does NOT turn any of them into save::busy,
 * because save::busy means "this operation has not finished yet" and every
 * operation here has.
 *
 * THE COST OF A SAVE IS IPC TO THE /dev/fsa SERVER PLUS SD TRAFFIC, which on an
 * Espresso is fast and is still not free -- milliseconds rather than
 * microseconds, so a save drops a frame or two. The same honest summary the
 * other three filesystem backends give. The expensive calls are named where
 * they happen (::Mounted, ::Mount).
 *
 * A SAVE WHILE THE TITLE IS LOSING THE FOREGROUND is the one failure mode that
 * is this console's own. ProcUI takes the application's filesystem access away
 * when the HOME menu or an overlay applet comes up, and a transfer caught by
 * that fails with an FSA error like any other -- reported as save::failed, or
 * save::no_device if the mount went with it. There is nothing to do about it
 * here: a caller's RETRY wheel is the answer, and it works, because the mount
 * is back by the time the title has the foreground again.
 *
 * THE SEARCH IS PATH-BASED, LIKE THE OTHER THREE FILESYSTEM BACKENDS'. There is
 * no equivalent of CARD_FindFirst's gamecode filter on a FAT volume, so a save
 * is found by looking where it was put: the location is fixed by the build
 * (WIIU_SAVE_DIR / WIIU_SAVE_FILENAME below) and ::ParseDevice accepts ONLY the
 * one complete path this file publishes. A path with any other spelling is
 * save::bad_path rather than a file created where nothing will ever look for it
 * again.
 *
 * @p size IS A PREFIX LENGTH, exactly as it is on the other four consoles, and
 * ::kAllocUnit is what makes that mean the same thing here: a save is rounded
 * up to a whole unit, a prefix shorter than the file reads back, and a prefix
 * longer than the file is save::full.
 *
 * NO LOGGING, for the reason the other four backends give: log() resolves on
 * this platform to the generic debug branch of logger.hpp, which writes through
 * std::cout, and this backend has no business owning the console that writes to.
 * So a failure states itself through save::Response and nowhere else: a
 * developer who wants the platform's own code breaks on the failing call, where
 * its return value and errno are both still live. (WHBMountSdCard logs its own
 * failures through WHBLogPrintf, which is a no-op unless the application
 * installed a log backend -- this one does not, so the one call here that could
 * print does not.)
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

/**
 * @brief The `path` a caller's save::Response names, or nullptr for none.
 *
 * THE SAME FUNCTION THE OTHER FOUR BACKENDS CARRY, repeated here rather than
 * hoisted into a header, because hoisting it would be the first shared SOURCE
 * between two platform backends in this library: there is no
 * src/all/save_common.hpp and one function is not enough to start one with.
 * What matters is that it behaves identically, which is why its reasoning is
 * repeated too --
 *
 * AN EMPTY STRING IS NOT A PATH. save.hpp's flows hand a Response straight
 * back from ::save::GetSaveLocation or ::save::GetDeviceStatus, and one that
 * found nothing carries a null `path`; a caller that built a Response by hand,
 * or zeroed a buffer and passed that, carries "". Both mean "I am not naming a
 * location", so both reach the backend as nullptr and get ::kSaveFile.
 */
const char* PathOf(const save::Response* const response) {
    return (response && response->path && response->path[0]) ? response->path : nullptr;
}

/**
 * @brief Where this game's save lives on the SD card, as the build spells it.
 *
 * TWO NAMES AND NOT ONE PATH, matching WII_SAVE_DIR / NX_SAVE_DIR /
 * CTR_SAVE_DIR: the directory has to be created before the file can be, and a
 * mkdir needs it on its own. Compile definitions rather than a CMake-generated
 * header, for the reason the other backends' identity knobs are -- a project
 * sets them once in its own build and nothing here grows an option for it.
 *
 * THE DIRECTORY IS NOT OPTIONAL AND NEITHER IS ITS PARENT. "wiiu" is where the
 * Homebrew Launcher and Aroma list applications from (sd:/wiiu/apps) and where
 * homebrew keeps its per-app data by convention, so a save belongs at
 * /vol/external01/wiiu/<game>/<file> and not in the volume root -- an SD card's
 * root is the player's, shared here with the system's own saviine/install
 * directories, and a bare save file dropped in it is indistinguishable from
 * litter. ::Create makes both levels.
 */
#ifndef WIIU_SAVE_DIR
#define WIIU_SAVE_DIR "pnes"
#endif
#ifndef WIIU_SAVE_FILENAME
#define WIIU_SAVE_FILENAME "pnes_save"
#endif

/**
 * @brief Rejects a name that would be a path rather than a name.
 *
 * CHECKED AT COMPILE TIME because the alternative is discovering it on a
 * player's console: the path below is assembled by the preprocessor, so a
 * WIIU_SAVE_DIR containing a slash, a colon or a backslash silently builds a
 * path pointing somewhere else entirely -- possibly at another homebrew app's
 * directory under /vol/external01/wiiu. A name is one component, and the
 * separators are this file's to add.
 */
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
// 255 is FAT's long-name limit per component, which is what the Wii U's SD card
// is formatted as (the console mounts FAT32 only); the whole path is well under
// FS_MAX_PATH either way.
static_assert(sizeof(WIIU_SAVE_DIR) <= 256, "WIIU_SAVE_DIR must be at most 255 characters");
static_assert(sizeof(WIIU_SAVE_FILENAME) <= 256, "WIIU_SAVE_FILENAME must be at most 255 characters");

/**
 * @brief The mount point the SD card is at, in the two spellings it has.
 *
 * TWO SPELLINGS OF ONE PLACE, and the difference is which API is being talked
 * to. "/vol/external01" is the Cafe OS mount path -- what wut's devoptab passed
 * to FSAMount and what WHBGetSdCardMountPath() answers -- while
 * "fs:/vol/external01" is the same directory addressed through the devoptab
 * named "fs", which is the form stdio takes. Every path below carries the
 * prefix.
 *
 * THE PREFIX IS SPELT OUT RATHER THAN LEFT TO THE DEFAULT DEVICE, even though
 * "fs" IS the default device and src/wiiu/audio.cpp reaches /vol/content
 * without it. A default device is a global a later piece of code can change,
 * and an unprefixed relative-looking path is resolved against the devoptab's
 * cwd -- which wut sets to the SD card's root only if the startup mount
 * SUCCEEDED. A save's location should not depend on either, so it does not.
 *
 * IT IS A CONSTANT AND NOT WHBGetSdCardMountPath(), because the path has to be
 * known before anything is mounted (::Mounted stats it) and because save.hpp
 * promises a `path` that outlives the call. /vol/external01 is where
 * FS_MOUNT_SOURCE_SD goes on this console and where wut's devoptab hardcodes
 * its own mount; ::Mount checks that WHB agrees rather than trusting that
 * forever.
 */
constexpr char kMountPath[] = "/vol/external01";
constexpr char kMountPoint[] = "fs:/vol/external01";
/** @brief The homebrew directory, which ::Create makes if a card lacks it. */
constexpr char kHomebrew[]  = "fs:/vol/external01/wiiu";
constexpr char kSaveDir[]   = "fs:/vol/external01/wiiu/" WIIU_SAVE_DIR;
constexpr char kSaveFile[]  = "fs:/vol/external01/wiiu/" WIIU_SAVE_DIR "/" WIIU_SAVE_FILENAME;

/**
 * @brief How far ::save::AccessibleDevices and friends count.
 *
 * ONE, FOR THE SWITCH'S AND THE 3DS'S REASON. The GameCube has two card slots
 * and the Wii an SD slot and a USB port, so both of those backends carry a
 * device table, a preference order, a remembered location and a
 * best-answer-so-far search across devices. This console has one SD card: its
 * USB ports can hold mass storage, and reaching it means mounting a volume the
 * system did not mount for the title -- which needs an FS client of this file's
 * own, a device-insertion listener, and a hotplug story, for a device no Wii U
 * homebrew keeps its saves on.
 *
 * SO THE SEARCHES COLLAPSE, and most of this file is shorter than the Wii's for
 * that one reason: no Rank() because no two devices ever disagree, and no
 * remembered device index because index 0 is the only answer. What does NOT
 * collapse is the device VOCABULARY -- bit 0, index 0, and a path per device --
 * because that is save.hpp's shape and a portable caller walks it the same way
 * on every console. A slot picker drawn against this backend finds exactly one
 * row.
 */
constexpr u8 kDeviceCount = 1;
static_assert(kDeviceCount <= 8, "::save::AccessibleDevices returns the mask in a u8");

/** @brief The only device: the SD card, as bit 0 and index 0. */
constexpr u8 kSdDevice = 0;

/** @brief Not a device; what ::ParseDevice answers for a path it will not take. */
constexpr u8 kNoDevice = 0xFF;

/**
 * @brief The unit a save file's length is rounded up to.
 *
 * 8192 BECAUSE THAT IS THE GAMECUBE'S SECTOR -- the same constant, for the same
 * reason, as the Wii, Switch and 3DS halves' ::kAllocUnit. It is what makes
 * @p size mean the same thing on all five consoles: a save file is a whole
 * number of units long, a shorter prefix reads back, and a prefix longer than
 * the file is save::full. Nothing about it is hardware here; the SD card's own
 * cluster size is the filesystem's business and this file never sees it.
 *
 * IT IS NOT A LIMIT. A larger save simply occupies more units, so a @p size
 * above 8192 is ordinary rather than save::too_large -- a game sized for the
 * GameCube is a strict subset of what works here, which is the point.
 */
constexpr std::size_t kAllocUnit = 8192;

/**
 * @brief The zero-fill source, in .rodata rather than as an 8 KiB staging copy.
 *
 * The other filesystem backends' reasoning exactly: a write() takes a length,
 * so the caller's own bytes go out from where they already are and the padding
 * is this block, written as many times as it takes (::PadTo). 256 bytes of
 * .rpx, and no snapshot of the caller's save struct.
 */
const u8 kZeros[256] = { };

/**
 * @brief When a failed mount was last attempted, and whether one ever was.
 *
 * A THROTTLE FOR A CALL THAT ALMOST NEVER RUNS, as on the Switch: wut's startup
 * mounts the SD card before main(), so ::Mount's first question is answered by
 * ::Mounted and nothing is mounted at all in the ordinary case. The retry
 * exists for the one case that startup cannot cover -- the console was booted,
 * or the title launched, with no card in the slot, and one was pushed in
 * afterwards. __init_wut_devoptab runs once and does not come back to look.
 *
 * THE COST IT GUARDS AGAINST IS A CLIENT REGISTRATION PLUS A MOUNT, which is
 * the most expensive thing in this file: save.hpp invites a UI to poll
 * ::save::AccessibleDevices "a few times a second", and a failing mount
 * re-attempted on every poll is IPC a menu does not need to pay for. Hotplug
 * still works -- the next attempt after the interval finds the card.
 */
u64  mountAt    = 0;
bool mountTried = false;
constexpr u64 kMountRetryMs = 2000;

save::eError lastErr = save::found;

/** @brief Whether something has found this game's save since it last looked. */
bool haveLocation = false;

/** @brief @p n rounded up to a whole ::kAllocUnit. */
constexpr std::size_t RoundUp(const std::size_t n) {
    return ((n + kAllocUnit - 1) / kAllocUnit) * kAllocUnit;
}

/**
 * @brief Translates the failing call's errno into the library's own code.
 *
 * THE OTHER FILESYSTEM BACKENDS' ::MapErrno, AND IT HAS TO BE, because what
 * reaches here is newlib's errno either way: wut's devoptab translates the FSA
 * server's own FSError codes into errno inside __wut_fsa_translate_error (a
 * missing file is ENOENT, a full card ENOSPC, a write-protected one EROFS), so
 * the mapping a caller sees is the same one the Wii's libfat produced. A game
 * that handles save::full on one console handles it here.
 *
 * ENOENT IS save::missing AND NOT save::bad_path, which is unambiguous only
 * because the path is this file's own: the directory and file names come from
 * the build and the volume is known to be mounted before any of them is opened,
 * so the one thing ENOENT can mean is that this game's save is not there yet --
 * the cue for ::save::CreateSave. A caller's misspelt path never reaches a
 * syscall at all; ::ParseDevice refuses it first.
 *
 * EBUSY IS NOT save::busy, and this is the platform where that has to be said.
 * wut maps five unrelated FSA failures onto it -- a busy server, a file another
 * client has open, a full journal, a command the volume will not take right now
 * and an invalid parameter -- and NONE of them means what save::busy means in
 * this API, which is "the operation you asked for has not finished". Every
 * operation here finishes. So EBUSY falls through to save::failed, which is the
 * honest answer: something went wrong that this API has no finer word for.
 *
 * EIO AND EILSEQ ARE save::failed AND NOT save::no_device, which is a real
 * judgement rather than an oversight. FS_ERROR_MEDIA_ERROR (EIO) and
 * FS_ERROR_DATA_CORRUPTED (EILSEQ) are what a card pulled out mid-write can
 * produce as well as what a genuinely bad sector produces, and the two are not
 * distinguishable from here. save::failed is the answer that is true in both
 * cases, it puts the caller's RETRY wheel in front of the player, and a retry
 * after the card is pushed back in succeeds -- whereas save::no_device would
 * ask a player with a perfectly good card in the slot to insert one. The Wii
 * and the Switch make the same call for the same codes.
 *
 * EFBIG IS save::too_large, which is the one code this map adds to the Switch's.
 * FS_ERROR_FILE_TOO_BIG is the volume refusing the LENGTH of a write rather
 * than refusing to find room for it, which is what save::too_large says and
 * what save::full would not; a 4GiB-capped FAT32 file is the only way to reach
 * it with a save this shape, and a developer who does reach it is better served
 * by the specific code.
 */
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
        // The volume is not there to be written to: ENODEV is what wut answers
        // for FS_ERROR_NOT_INIT, which is the FSA server saying it has no such
        // mount -- the card was taken away, or never arrived.
        //
        // ENOMEDIUM IS BEHIND A GUARD because newlib declares it only under
        // __LINUX_ERRNO_EXTENSIONS__, which devkitPPC does not define, so
        // naming it unconditionally is a compile error rather than a dead
        // branch. It is still worth listing, for a toolchain update that
        // exposes it.
        case ENODEV:
        case ENXIO:
#ifdef ENOMEDIUM
        case ENOMEDIUM:
#endif
                           return save::no_device;
        default:           return save::failed;
    }
}

/** @brief Records an outcome and returns it, for `return Fail(...)`. */
save::eError Fail(const save::eError err) {
    lastErr = err;
    return err;
}

/** @brief Records success: save::found. */
save::eError Won() {
    lastErr = save::found;
    return save::found;
}

/**
 * @brief True when the SD card is mounted at ::kMountPoint right now.
 *
 * A stat() OF THE MOUNT POINT, WHICH IS THE WII'S WORKAROUND AND NOT THE
 * SWITCH'S LOOKUP, and the reason is that the cheap test available on the other
 * two filesystem backends would be WRONG here rather than merely unavailable.
 * The Switch asks fsdev whether "sdmc" is registered and the 3DS asks
 * libsysbase the same about its own devoptab; on this platform the devoptab is
 * called "fs", it is registered by wut's startup BEFORE the mount is attempted,
 * and it stays registered when that mount fails -- so FindDevice("fs") is true
 * on a console with no SD card in it at all. wut knows the difference
 * (__wut_fsa_device_t::mounted) and does not publish it, and the devoptab also
 * carries /vol/content and any other Cafe OS path, so the device's existence
 * says nothing about the card.
 *
 * WHAT IT COSTS IS ONE FSAGetStat ROUND TRIP to the /dev/fsa server, where the
 * other two backends' answer is a pointer comparison. That is the most
 * expensive status poll of the five backends and it is still cheap: it is
 * metadata about a mount root, it touches no file and reads no card sector, and
 * it is the same order as the GameCube's bus transaction for the same question.
 * save.hpp's "a few times a second" is the right cadence for it; every frame
 * would be IPC spent on an answer that cannot change that fast.
 *
 * IT IS ALSO THE WHOLE REMOVAL STORY, which is where this backend sits between
 * the other two: the 3DS can ask the fs process about the physical slot
 * (FSUSER_IsSdmcDetected) and the Switch cannot ask anything, while this stats
 * the mount -- so a card taken out of a running console is noticed as soon as
 * the system tears the mount down, and not before.
 */
bool Mounted() {
    struct stat st;
    return stat(kMountPoint, &st) == 0;
}

/**
 * @brief Makes the SD card usable, mounting it if nothing has.
 *
 * ALREADY MOUNTED IS THE NORMAL CASE, AND THE MOUNT IS NOT OURS. wut's devoptab
 * startup owns /vol/external01 before main() runs, the application's own data
 * may be open on it, and wut's own shutdown unmounts it -- so there is nothing
 * here to release and no `mountedByUs` flag of the Wii's kind: unmounting a
 * volume this backend did not mount would pull the SD card out from under
 * whatever else is reading it, and unmounting one it DID mount would do the same
 * to the next save. The device stays up for the life of the process either way.
 *
 * WHBMountSdCard AND NOT FSAMount, which is the choice worth explaining because
 * the devoptab's own mount is the other candidate. WHBMountSdCard is wut's
 * PUBLIC way to do this -- it is in libwut.a, which this target already links,
 * it registers its own FS client so this file needs none, it finds the card
 * with FSGetMountSource(FS_MOUNT_SOURCE_SD) rather than hardcoding
 * /dev/sdcard01, and it is idempotent (a static flag, so a second call after
 * success is free). Calling FSAMount directly would mean an FSA client of this
 * file's own and a duplicate of a function wut already ships.
 *
 * A MOUNT MADE BY ANOTHER CLIENT IS STILL VISIBLE TO THE DEVOPTAB, which is
 * what makes that work: a Cafe OS mount point belongs to the PROCESS, not to
 * the FS or FSA client that created it, so the devoptab's own client resolves
 * /vol/external01 afterwards exactly as it resolves the mount its startup made.
 * This is the arrangement Wii U homebrew has always used -- WHBMountSdCard
 * followed by ordinary fopen -- and it is why ::Mounted, a devoptab stat, is a
 * valid test of a mount this function did not make.
 *
 * THE PATH IS CHECKED RATHER THAN ASSUMED. Every path in this file is built
 * around /vol/external01, so a WHB that mounted the card somewhere else would
 * leave this backend saving into a directory nothing will look in again --
 * which is exactly the failure ::ParseDevice exists to prevent on the caller's
 * side. A disagreement is reported as save::bad_path and no device: it is a
 * platform that changed under the build, and there is nothing a player can do
 * about it.
 *
 * THE SLOW CALL IN THIS FILE, in the case where it does run. Nothing calls it on
 * a status poll -- ::save::AccessibleDevices and ::save::GetDeviceStatus stop at
 * ::Mounted -- and it runs when a real operation needs the filesystem.
 */
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

/**
 * @brief Which device @p path names, or ::kNoDevice.
 *
 * ONE PATH AND ONE DEVICE, which is the stricter half of the path-based search
 * described in this file's header comment. A caller gets ::kSaveFile from
 * ::save::GetDeviceStatus or ::save::GetSaveLocation and hands it back;
 * anything else is a path the next ::save::GetSaveLocation would not look at,
 * so creating a file there would lose it.
 */
u8 ParseDevice(const char* const path) {
    if (!path) return kNoDevice;
    return std::strcmp(path, kSaveFile) == 0 ? kSdDevice : kNoDevice;
}

/** @brief write() until @p n bytes are gone or something refuses them. */
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

/** @brief read() until @p n bytes have arrived or the file runs out. */
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

/**
 * @brief Zero-fills from @p from to @p to.
 *
 * WHAT MAKES @p size A PREFIX. The whole allocation unit is written every time,
 * zero past the caller's bytes, so no tail of an earlier and longer save
 * survives inside it -- the same guarantee the GameCube half gets from staging
 * a whole sector and the other filesystem backends get from this same loop.
 *
 * NOT ftruncate(), WHICH wut DOES IMPLEMENT. Extending a file with a truncate
 * would zero the extension and cost one call instead of a few writes, and it
 * would stop the padding from OVERWRITING anything: a save that shrank would
 * leave the previous save's bytes sitting in the unit past it, since the file is
 * already long enough to need no extension at all. Writing the zeros is what
 * makes the unit's whole content this save's.
 */
bool PadTo(const int fd, std::size_t from, const std::size_t to) {
    while (from < to) {
        const std::size_t span = to - from;
        const std::size_t n    = span < sizeof kZeros ? span : sizeof kZeros;
        if (!WriteAll(fd, kZeros, n)) return false;
        from += n;
    }
    return true;
}

/** @brief The length of @p path, or -1 with @p errOut set. */
off_t LengthOf(const char* const path, int& errOut) {
    struct stat st;
    if (stat(path, &st) != 0) {
        errOut = errno;
        return -1;
    }
    errOut = 0;
    return st.st_size;
}

/**
 * @brief Moves @p size bytes to or from the save on the SD card.
 *
 * ONE FUNCTION FOR BOTH DIRECTIONS, like the other filesystem backends'
 * ::Transfer, because everything except the transfer itself is shared and the
 * length check in particular MUST be: a save and a load disagreeing about
 * whether a file is long enough is how a game writes a prefix it can never read
 * back.
 *
 * O_RDWR FOR A WRITE, AND IT IS NOT A STYLE CHOICE -- it is the one place this
 * file cannot be the Switch's. wut's devoptab does not pass POSIX flags
 * through; it picks a Cafe OS fopen-style mode from them, and Cafe OS has no
 * mode meaning "write, do not truncate, do not create". So O_WRONLY on its own
 * matches NONE of __wut_fsa_open's cases and comes back EINVAL -- which this
 * backend would report as save::bad_path, about a path that is perfectly good.
 * O_RDWR with no O_CREAT/O_TRUNC/O_APPEND is wut's "r+", which is what an
 * in-place save actually wants. The file is never read through that handle.
 *
 * THE LENGTH CHECK IS THE GAMECUBE'S. A prefix longer than the file on the
 * medium is save::full, the same code a card with no room reports, so a game
 * whose save struct has outgrown its existing save file hears the same answer
 * on all five consoles. THE TWO CAUSES ARE NOT DISTINGUISHED, deliberately:
 * "there is not room for this" is true of a short file and a full card alike,
 * and it is the whole of what a game can act on either way.
 *
 * fsync BEFORE close AND NOT INSTEAD OF IT. A save is the one moment a player
 * may hold the power button or pull the card believing their progress is on it;
 * close() releases the file, fsync() (FSAFlushFile underneath) is what pushes
 * it through the FSA server's own buffering, and its failure is a failed save
 * rather than a detail -- which is why it is part of the && and not a courtesy
 * call. A SAVEDATA BUILD WOULD ALSO NEED FSAFlushQuota here; see this file's
 * header comment on /vol/save.
 */
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

/**
 * @brief ::save::Save and both ::save::Load overloads, which differ only in
 *        which way the bytes go and whether the caller named a location.
 *
 * NO SEARCH, BECAUSE THERE IS NOWHERE TO SEARCH. The Wii's counterpart tries
 * the remembered device, then device 0, then device 1, and keeps the best
 * answer any of them gave; with one device the remembered location IS the only
 * location and the best answer is the only answer. What survives of that flow
 * is the VALIDATION -- a named path is checked rather than defaulted, because
 * defaulting a wrong path is how a save lands somewhere it will not be found
 * again.
 */
save::eError SaveOrLoad(const void* const src, void* const tar,
                        const std::size_t size, const char* const path) {
    // size == 0 IS save::too_large, WHICH READS ODDLY AND IS THE GAMECUBE'S
    // ANSWER: ::Begin over there refuses `size == 0 || size > kSectorMax` with
    // that one code, and a portable caller handling it for one console should
    // not meet a different code here for the same mistake. The upper half of
    // that test has no counterpart; see ::kAllocUnit.
    if (size == 0)    return Fail(save::too_large);
    if (!src && !tar) return Fail(save::failed);

    if (path && ParseDevice(path) == kNoDevice) return Fail(save::bad_path);

    return Transfer(src, tar, size);
}

/**
 * @brief ::save::CreateSave: the one operation that makes the file and the
 *        directories holding it.
 *
 * O_EXCL AND NOT A stat() FIRST, because "does it exist" and "create it" have
 * to be one operation for save::exists to mean anything -- and because the
 * GameCube's CARD_Create reports CARD_ERROR_EXIST from inside the create for
 * the same reason. wut's devoptab honours the pair: O_CREAT | O_EXCL on a file
 * that is there is EEXIST, which ::MapErrno turns back into save::exists.
 *
 * O_WRONLY IS CORRECT HERE, UNLIKE IN ::Transfer, and the asymmetry is wut's
 * rather than this file's: O_WRONLY | O_CREAT | O_EXCL is one of the
 * combinations __wut_fsa_open does map (to "w"), while O_WRONLY alone is not.
 *
 * TWO DIRECTORIES, NOT ONE, and that is this backend's own wrinkle as much as
 * the Switch's: the save lives at /vol/external01/wiiu/<dir>/<file>, and while
 * "wiiu" exists on any card that has ever run homebrew, a card freshly
 * formatted for a title that was installed some other way may not have it --
 * and mkdir does not create parents. An EEXIST from either level is the
 * ordinary case and not an error.
 *
 * A HALF-WRITTEN NEW SAVE IS DELETED. If the card fills up or is taken away
 * between the open and the fsync, what is left is a file this game will find on
 * its next ::save::GetSaveLocation and load as if it were a save -- worse than
 * no file at all, since the player is offered a corrupt game rather than a new
 * one. The unlink is best-effort: its own failure changes nothing about what the
 * caller is told. THE DIRECTORIES ARE LEFT BEHIND, deliberately -- an empty
 * directory is not mistakable for a save, and removing one this call may not
 * have created is a worse mistake than leaving it.
 */
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

/**
 * @brief The response for an operation that has just finished, which is every
 *        one of them on this console.
 *
 * `done` IS ALWAYS TRUE, and that is this backend's whole async story: see this
 * file's header comment. The `path` is ::kSaveFile's own storage rather than a
 * buffer this file fills -- a string literal outlives every call, which is
 * stricter than save.hpp's promise that it survives until the next
 * ::GetSaveLocation and costs nothing to honour.
 */
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

/**
 * @brief Finds where this game's save lives.
 *
 * A COMPLETED SEARCH THAT FOUND NOTHING IS NOT AN ERROR -- `done` with
 * save::missing and a null `path`, which is the cue to call ::CreateSave. A
 * card that is not there is different and says save::no_device, so a game can
 * ask for an SD card rather than offer to start a new save on one that is
 * absent. Exactly the other backends' two answers, from the same vocabulary.
 *
 * IT IS A stat AND NOTHING MORE: the location is fixed by the build, so
 * "finding" the save is asking whether the one path this file publishes exists.
 * A search that finds nothing forgets what it thought it knew, because a `path`
 * handed back afterwards would name a file this very call failed to stat.
 */
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

/**
 * @brief No counterpart on this console, and not a gap to be filled.
 *
 * These two move a `card_stat` -- a 64-byte entry in a GameCube memory card's
 * own directory, carrying the comment block, banner and icon offsets the system
 * menu draws a save with. A file on an SD card has no directory entry of that
 * shape, and this console's own save-data manager does not list homebrew saves
 * at all (they are not savedata volumes; see this file's header comment), so
 * there is nothing here to read and nothing to state.
 *
 * save::no_device AND NOT save::failed, matching the other filesystem backends:
 * the honest content of the answer is that the DEVICE this concept belongs to
 * is not present, which is what src/all/save.cpp says on every target with no
 * card.
 */
Response GetMetaData(void*) {
    return { .done = true, .err = Fail(no_device), .path = nullptr };
}

Response SetMetaData(const void*) {
    return { .done = true, .err = Fail(no_device), .path = nullptr };
}

/**
 * @brief Which devices hold media right now, as a bitfield.
 *
 * ONE BIT, AND IT IS THE MOUNT'S OWN ANSWER (::Mounted) -- a stat of
 * /vol/external01, which is one FSA round trip rather than the Switch's pointer
 * comparison and still cheap enough for the "a few times a second" poll
 * save.hpp describes. Every frame would be IPC spent on an answer that cannot
 * change that fast.
 *
 * A MOUNT IS NOT ATTEMPTED FROM HERE, which is what keeps it that cheap: a card
 * inserted into a running console reads as inaccessible until something that
 * really needs the filesystem retries the mount (::Mount, on a save, a load, a
 * create or a locate). The one thing this cannot say is that the card is
 * UNREADABLE as opposed to absent -- being broken is only discoverable by
 * mounting, which save.hpp's own documentation says of every backend.
 *
 * IT LEAVES ::lastErr ALONE, as save.hpp requires of a status poll: a menu
 * refreshing itself must not overwrite what a failed save left behind for the
 * game to report.
 */
u8 AccessibleDevices() {
    return Mounted() ? static_cast<u8>(1u << kSdDevice) : 0;
}

/**
 * @brief Whether device @p index holds usable media, and where a save on it
 *        would live.
 *
 * EVERY INDEX BUT 0 IS save::no_device, which is how a caller walking indices
 * finds out there is one device without being told a count -- the loop stops
 * at the first ::no_device, and on this console that is index 1.
 *
 * save::too_large IS UNREACHABLE HERE, as on the Wii, the Switch and the 3DS:
 * that answer is about a card whose sector width the backend cannot transfer,
 * and a filesystem presents no such number. save::busy is unreachable too; the
 * probe is a single stat and always has its answer by the time it returns.
 */
Response GetDeviceStatus(const u8 index) {
    if (index >= kDeviceCount) return { .done = true, .err = no_device, .path = nullptr };
    if (!Mounted())            return { .done = true, .err = no_device, .path = nullptr };
    return { .done = true, .err = found, .path = kSaveFile };
}

/**
 * @brief Which devices could take a save of @p size bytes.
 *
 * THE ONE CALL IN THIS LIBRARY THAT CANNOT ANSWER ITS OWN QUESTION, and it says
 * so here rather than inventing a number. Free space on this volume is not
 * measurable from a Wii U application through anything this file can reach:
 *
 *   - statvfs() IS REFUSED BY wut FOR THIS DEVICE, deliberately and in one
 *     line: __wut_fsa_statvfs begins `if (deviceData->isSDCard) { errno =
 *     ENOSYS; return -1; }`, and isSDCard is true for exactly the mount every
 *     path in this file lives on. That is not a gap to work around; it is
 *     upstream reporting that the measurement underneath does not hold for an
 *     FSA bind mount of /dev/sdcard01.
 *   - FSAGetFreeSpaceSize IS THE CALL IT WOULD HAVE MADE, and it takes the
 *     devoptab's own FSA client handle -- private to wut. Registering a second
 *     client to ask the same server the same question wut has already decided
 *     not to trust for this volume would be two mistakes rather than one.
 *   - THE FS-API FORM (FSGetFreeSpaceSize, with a client and a command block of
 *     this file's own) is the remaining candidate, and it is ~8KiB of static
 *     client state plus a code path no other backend has, to narrow a mask on a
 *     multi-gigabyte FAT32 card against a save of one 8KiB unit.
 *
 * SO THIS REPORTS ACCESSIBILITY, AND THE DEVIATION IS DOCUMENTED RATHER THAN
 * HIDDEN. save.hpp says bit `n` is set when device `n` holds usable media AND a
 * write of @p size bytes would have somewhere to go; this backend can only
 * establish the first half, so a card with no room left still reports its bit.
 * What that costs a caller is one honest failure instead of one greyed-out row:
 * the write itself answers save::full with ENOSPC, which is the same thing a UI
 * would have been told by a measurement taken a moment before the card filled
 * up -- and which save.hpp already requires every caller to handle, because no
 * backend's mask is a promise about the future.
 *
 * NO CACHE AND NO SWEEP, consequently. The GameCube spreads this over several
 * calls because CARD_GetFreeBlocks needs a mount; the Switch and the 3DS cache
 * one service call's answer. There is nothing to spread or remember here, so
 * this is a stat's worth of work and always a settled answer -- never the "zero
 * means nowhere, OR not yet" save.hpp allows for.
 *
 * A @p size OF 0 IS STILL NO DEVICES, matching every other backend: a write of
 * nothing is not a write that would succeed.
 */
u8 FreeDevices(const std::size_t size) {
    if (size == 0) return 0;
    return AccessibleDevices();
}

} // namespace save
