/**
 * @file save.cpp
 * @brief Save/load/create/locate for the Sony PSP, as a file beside the EBOOT
 *        on whichever device the game was launched from.
 *
 * THE SIXTH BACKEND, AND IT IS THE SWITCH'S AGAIN: blocking stdio against a
 * single device the C runtime had ready before main() ran. src/switch/save.cpp,
 * src/3ds/save.cpp and src/wiiu/save.cpp are already that file three times
 * over, and this is the fourth -- open/read/write/lseek/stat/mkdir/unlink/fsync
 * all work, errno is newlib's where there is one, and nothing is ticked. So
 * everything below is that file's shape, and the FIVE places it is not say so
 * where they are:
 *
 *   1. THERE IS NO MOUNT AND NO DEVOPTAB. The other four register a device
 *      name with libsysbase (or ask fsdev for one) and can lose it; pspsdk's io
 *      IS the kernel's -- sceIoOpen takes "ms0:/..." directly and the Memory
 *      Stick is mounted by the kernel before a homebrew module is even loaded.
 *      So ::Mount collapses into ::Inserted, and the retry throttle all four of
 *      those files carry disappears with it: there is nothing here to retry.
 *   2. THE SAVE'S HOME IS DISCOVERED, NOT COMPILED IN (::Resolve). The other
 *      four know their volume at compile time ("sdmc:", "fs:/vol/external01");
 *      this console has two it can boot from -- ms0: for a Memory Stick and
 *      ef0: for a PSP Go's internal flash -- and which one the game is running
 *      from is not knowable until it runs. getcwd() answers it, because
 *      pspsdk's startup has already chdir'd into the EBOOT's own directory.
 *      See ::Resolve for the chain that makes that true.
 *   3. errno IS NOT ALWAYS SET, AND -1 IS NOT ALWAYS THE FAILURE VALUE. This is
 *      the one place a portable-looking POSIX layer is not portable underneath,
 *      it is a trap rather than a detail, and ::Native is the whole answer to
 *      it. Read that before changing any call in this file.
 *   4. FREE SPACE IS CLUSTERS FROM A DEVCTL (::save::FreeDevices), the 3DS's
 *      multiply rather than the Switch's byte count -- and reached with
 *      sceIoDevctl rather than with the statvfs pspsdk does provide. The reason
 *      is that backend's reason, repeated where it happens.
 *   5. A HALF OF ::save::FreeDevices CANNOT BE MEASURED ON A PSP GO, and this
 *      file says so rather than reporting "nowhere": see ::save::FreeDevices on
 *      the unnarrowed mask, which is src/wiiu/save.cpp's precedent and not a
 *      shortcut taken here first.
 *
 * WHY NEXT TO THE EBOOT AND NOT UNDER PSP/SAVEDATA. The PSP has a real save
 * concept -- a directory under ms0:/PSP/SAVEDATA carrying a PARAM.SFO, listed
 * and deleted by the XMB's own Saved Data Utility, written through
 * sceUtilitySavedata's modal dialog -- and this backend deliberately does not
 * use it. THREE THINGS ARGUE AGAINST IT HERE. A savedata directory is addressed
 * by a game id out of the PARAM.SFO, which a homebrew EBOOT does not own and
 * which this build would have to invent; a directory under PSP/SAVEDATA WITHOUT
 * a valid PARAM.SFO is listed by the XMB as "Corrupted Data", so the first
 * thing a player would be offered is to delete the save; and sceUtilitySavedata
 * is a modal utility driven by sceUtilitySavedataGetStatus across several
 * frames rather than an open/write/close, so it is a ticked backend of the
 * GameCube's kind and not this one. A file beside the EBOOT is what homebrew
 * actually does, it travels with the game when a player moves the folder, and
 * it is invisible to the Saved Data Utility -- which is the honest state of
 * affairs, since the system has no idea this save exists.
 *
 * A BUILD THAT WANTS AN XMB-VISIBLE SAVE WOULD CHANGE THREE THINGS, the same
 * shape of change the Switch's and the 3DS's header comments describe for an
 * installed title: ::Resolve would name ms0:/PSP/SAVEDATA/<gameid><name>
 * instead of asking getcwd, ::Create would have to write a PARAM.SFO beside the
 * save (and the build would have to supply the game id it names), and
 * ::Transfer would become a sceUtilitySavedata state machine with `done` false
 * until its dialog finished -- which is the one change that would reach a
 * caller, and save.hpp's ticked flow already allows for it. Nothing else here
 * is about where the file lives.
 *
 * @p size IS A PREFIX LENGTH, exactly as it is on the five backends before
 * this one, and ::kAllocUnit is what makes that mean the same thing here: a
 * save is rounded up to a whole unit, a prefix shorter than the file reads
 * back, and a prefix longer than the file is save::full.
 *
 * NO LOGGING, for the reason every backend before this one gives: log()
 * resolves on this platform to the generic debug branch of logger.hpp, which
 * writes through std::cout, and this backend has no business owning the console
 * that writes to. So a failure states itself through save::Response and nowhere
 * else: a developer who wants the platform's own code breaks on the failing
 * call, where its return value and errno are both still live.
 */
#include "internal.hpp"
#include <pnes/save.hpp>

// pspiofilemgr.h FIRST AND NOT ALPHABETICALLY: pspmscm.h is a header of static
// inlines that call sceIoDevctl and name SceUID without including anything at
// all, so it only compiles after the header that declares them.
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

/**
 * @brief The `path` a caller's save::Response names, or nullptr for none.
 *
 * THE SAME FUNCTION THE FIVE BACKENDS BEFORE THIS ONE EACH REPEAT, repeated
 * again rather than hoisted into a header, for the reason src/switch/save.cpp
 * gives: hoisting it would be the first shared SOURCE between two platform
 * backends in this library, there is no src/all/save_common.hpp, and one
 * function is not enough to start one with. What matters is that it behaves
 * identically, which is why its reasoning is repeated too --
 *
 * AN EMPTY STRING IS NOT A PATH. save.hpp's flows hand a Response straight back
 * from ::save::GetSaveLocation or ::save::GetDeviceStatus, and one that found
 * nothing carries a null `path`; a caller that built a Response by hand, or
 * zeroed a buffer and passed that, carries "". Both mean "I am not naming a
 * location", so both reach the backend as nullptr and get the resolved path.
 */
const char* PathOf(const save::Response* const response) {
    return (response && response->path && response->path[0]) ? response->path : nullptr;
}

/**
 * @brief This game's directory and save-file names, as the build spells them.
 *
 * TWO NAMES AND NOT ONE PATH, matching every backend since the Wii -- but on
 * this console only ONE OF THEM IS NORMALLY USED. ::PSP_SAVE_FILENAME names the
 * file; ::PSP_SAVE_DIR names the folder in ::kFallbackDir, which is reached only
 * when ::Resolve cannot find out where the game was launched from. In the
 * ordinary case the directory is the EBOOT's own and the build has no say in
 * what it is called, because the player chose it when they copied the game onto
 * their Memory Stick.
 *
 * IT IS STILL A BUILD KNOB RATHER THAN A CONSTANT, for the reason the other
 * four consoles' are: the fallback has to be SOMEWHERE, and a project that
 * renames it after shipping orphans every save written under the old name.
 */
#ifndef PSP_SAVE_DIR
#define PSP_SAVE_DIR "pnes"
#endif
#ifndef PSP_SAVE_FILENAME
#define PSP_SAVE_FILENAME "pnes_save"
#endif

/**
 * @brief Rejects a name that would be a path rather than a name.
 *
 * CHECKED AT COMPILE TIME because the alternative is discovering it on a
 * player's console: ::kFallbackDir is assembled by the preprocessor and
 * ::Resolve assembles the file onto a directory it did not choose, so a name
 * containing a slash or a colon silently points the save somewhere else
 * entirely -- and on this platform "somewhere else" can be another device,
 * since a colon is what separates one here. A name is one component, and the
 * separators are this file's to add.
 */
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

/**
 * @brief How long an assembled path may be.
 *
 * 256 BECAUSE pspsdk'S OWN POSIX LAYER STOPS THERE. Every call below goes
 * through libcglue's __path_absolute, which copies the caller's path into a
 * 256-byte buffer with a 255-character limit before handing it to sceIoOpen --
 * so a longer path is not a kernel error a caller could read, it is a quietly
 * truncated path pointing at a file that is not this save. ::Resolve measures
 * against this and refuses rather than truncating; see its save::bad_path.
 */
constexpr std::size_t kPathMax = 256;

/**
 * @brief Where the save goes when ::Resolve cannot find out where the game is.
 *
 * ms0: AND NOT ef0:, which is the right way round for a guess: a Memory Stick
 * is what every PSP model has a slot for, including the Go, while ef0: exists
 * on the Go alone. A guess that names internal flash would be wrong on every
 * console but one; this one is wrong only where ::Resolve has already failed
 * AND the player has no stick in.
 *
 * PSP/GAME AND NOT THE VOLUME ROOT, for src/switch/save.cpp's reason: a Memory
 * Stick's root is the player's, a bare save file dropped in it is
 * indistinguishable from litter, and PSP/GAME is where the EBOOT this is the
 * fallback for would have been.
 */
constexpr char kFallbackDir[] = "ms0:/PSP/GAME/" PSP_SAVE_DIR;
static_assert(sizeof kFallbackDir + sizeof(PSP_SAVE_FILENAME) <= kPathMax,
    "PSP_SAVE_DIR and PSP_SAVE_FILENAME must fit the fallback path in " "256 bytes");

/**
 * @brief The device-name prefix a removable medium has on this console.
 *
 * "ms" MATCHES ms0: AND NOTHING ELSE THIS FILE WILL SEE, and it is a prefix
 * rather than the whole name on purpose: the Memory Stick is ms0: on every
 * retail model, but the kernel's own device table has carried other ms* names
 * over the console's life (and custom firmware adds more), while the devices
 * that are NOT a stick -- ef0: on a Go, host0: under PSPLink -- share no prefix
 * with it. ::Resolve uses this to decide which of its two presence answers is
 * the honest one; see ::Inserted.
 */
constexpr char kRemovablePrefix[] = "ms";

/**
 * @brief How far ::save::AccessibleDevices and friends count.
 *
 * ONE, FOR THE SWITCH'S, 3DS'S AND WII U'S REASON AND NOT QUITE THEIRS. Those
 * three have one device because there is only one place a save could go. This
 * console has two -- a Memory Stick and, on a Go, internal flash -- and still
 * has one DEVICE, because the two are not alternatives here: the save belongs
 * beside the EBOOT, and the EBOOT is on exactly one of them (::Resolve). So the
 * searches collapse the same way -- no Rank(), no remembered device index, one
 * candidate for ::save::FreeDevices to measure -- and index 0 means "the device
 * this game is running from" rather than "the SD card".
 *
 * WHAT THAT COSTS IS A PSP GO WITH BOTH: a game on internal flash cannot put
 * its save on a Memory Stick, and a game on a stick cannot put it on flash.
 * Neither is a case a player can get into by accident, and offering the choice
 * would mean carrying the Wii's whole two-device search (src/ogc/save.cpp) to
 * serve one model of one console. A build that wants it adds the second entry
 * here, a second path, and ::SAVE_DEVICE_CHOICE (pnes/technology.hpp) -- the
 * device VOCABULARY below is already the shape that would take.
 */
constexpr u8 kDeviceCount = 1;
static_assert(kDeviceCount <= 8, "::save::AccessibleDevices returns the mask in a u8");

/** @brief The only device: whatever the game booted from, as bit 0 and index 0. */
constexpr u8 kBootDevice = 0;

/** @brief Not a device; what ::ParseDevice answers for a path it will not take. */
constexpr u8 kNoDevice = 0xFF;

/**
 * @brief The unit a save file's length is rounded up to.
 *
 * 8192 BECAUSE THAT IS THE GAMECUBE'S SECTOR -- the same constant, for the same
 * reason, as every backend since. It is what makes @p size mean the same thing
 * on all six consoles: a save file is a whole number of units long, a shorter
 * prefix reads back, and a prefix longer than the file is save::full. Nothing
 * about it is hardware here; the Memory Stick's own cluster size is the FAT
 * driver's business and this file sees it in one place only
 * (::save::FreeDevices, which has to multiply by it).
 *
 * IT IS NOT A LIMIT. A larger save simply occupies more units, so a @p size
 * above 8192 is ordinary rather than save::too_large.
 */
constexpr std::size_t kAllocUnit = 8192;

/**
 * @brief The zero-fill source, in .rodata rather than as an 8 KiB staging copy.
 *
 * The Wii half's reasoning exactly: a write() takes a length, so the caller's
 * own bytes go out from where they already are and the padding is this block,
 * written as many times as it takes (::PadTo). 256 bytes of EBOOT, and no
 * snapshot of the caller's save struct -- which matters more on a Fat PSP's
 * 32MB than it did on any console before it.
 */
const u8 kZeros[256] = { };

/**
 * @brief ::Resolve's answer: the save's full path, its device, and whether the
 *        device is one that can be taken out.
 *
 * STATIC BUFFERS AND NOT A COMPILE-TIME LITERAL, which is the price of
 * difference 2 in this file's header comment. The five backends before this one
 * hand callers a string literal that outlives the process; this one hands back
 * ::savePath, which is written once by ::Resolve and never again -- so it keeps
 * save.hpp's promise ("valid until the next ::GetSaveLocation call") the same
 * way, by never changing after the first call rather than by being constant.
 *
 * ::saveDevice CARRIES THE COLON AND NO PATH ("ms0:"), because that is the form
 * sceIoDevctl takes. 16 bytes is four times the longest device name this
 * console has.
 */
char savePath[kPathMax]  = { };
char saveDevice[16]      = { };
bool removable           = false;

/** @brief Whether ::Resolve has run, and whether it got an answer. */
bool resolved   = false;
bool resolvedOk = false;

save::eError lastErr = save::found;

/** @brief The last finished ::save::FreeDevices answer and what it describes. */
bool        freeValid = false;
u8          freeMask  = 0;
std::size_t freeSize  = 0;

/** @brief Whether something has found this game's save since it last looked. */
bool haveLocation = false;

/** @brief @p n rounded up to a whole ::kAllocUnit. */
constexpr std::size_t RoundUp(const std::size_t n) {
    return ((n + kAllocUnit - 1) / kAllocUnit) * kAllocUnit;
}

/**
 * @brief Translates the failing call's errno into the library's own code.
 *
 * THE SWITCH'S ::MapErrno, AND THE VALUES REALLY DO ARRIVE AS errno -- but by a
 * different route, which is why ::Native exists next door and why this function
 * is never called with a bare `errno` on this platform. pspsdk's POSIX layer
 * does not translate anything: it recognises the sce error family 0x8001xxxx,
 * whose low half IS a POSIX errno by construction (a missing file is
 * 0x80010002, ENOENT; an existing one 0x80010011, EEXIST), and stores that half.
 * Everything in that family therefore maps exactly as it does on the Switch and
 * the 3DS. Everything OUTSIDE it never reaches this function at all.
 *
 * ENOENT IS save::missing AND NOT save::bad_path, which is unambiguous only
 * because the path is this file's own: ::savePath is assembled once by
 * ::Resolve from a directory the runtime was already sitting in, so the one
 * thing ENOENT can mean is that this game's save is not there yet -- the cue for
 * ::save::CreateSave. A caller's misspelt path never reaches a syscall at all;
 * ::ParseDevice refuses it first.
 */
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
        // The stick went away mid-operation, which on this console is a player
        // pulling it out from under a running game -- the Memory Stick slot has
        // no door and no software eject, so this is the one of the six consoles
        // where it is a routine occurrence rather than a system decision.
        //
        // ENOMEDIUM IS BEHIND A GUARD because newlib declares it only under
        // __LINUX_ERRNO_EXTENSIONS__, which pspdev does not define, so naming it
        // unconditionally is a compile error rather than a dead branch. It is
        // still worth listing, for a toolchain update that exposes it.
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
 * @brief Turns a failed POSIX call's return value into the library's own code.
 *
 * THIS FUNCTION IS DIFFERENCE 3 IN THIS FILE'S HEADER COMMENT, and it is the
 * one thing a reader has to carry away from this file. pspsdk's POSIX layer
 * (libcglue's __set_errno) handles exactly one family of failures:
 *
 *   - An sce error of the form 0x8001xxxx has its LOW HALF stored in errno --
 *     which is a real POSIX errno, by the kernel's own construction -- and the
 *     call returns -1. This is the ordinary case, it behaves exactly like the
 *     other four stdio backends, and ::MapErrno is written for it.
 *   - ANY OTHER sce error is RETURNED UNCHANGED, as the large negative number
 *     the kernel produced, AND errno IS LEFT ALONE. 0x80020321 ("no media") is
 *     one such; so is every error the FAT driver raises on its own behalf.
 *
 * TWO CONSEQUENCES, BOTH OF WHICH EVERY CALL IN THIS FILE OBSERVES. A failure
 * is `< 0` and never `== -1`, because the failure value may be the error code
 * itself. And errno is only meaningful if it was cleared first, because
 * otherwise the value sitting in it belongs to some earlier call -- reporting
 * save::full for a read that failed with "no media" because a write filled the
 * stick ten minutes ago is exactly the bug this guards against.
 *
 * SO THE CONVENTION IS: clear errno, make the call, and on a negative return
 * come here. A non-zero errno is the POSIX path and maps as usual; a zero errno
 * means the kernel's own code came back in the return value instead, which this
 * API has no word finer than save::failed for -- a developer who needs the code
 * itself breaks on the failing call, which is the only place it exists.
 */
save::eError Native() {
    const int e = errno;
    return e ? Fail(MapErrno(e)) : Fail(save::failed);
}

/**
 * @brief Finds where this game's save goes, once, for the life of the process.
 *
 * getcwd() IS THE ANSWER, AND IT IS NOT A GUESS: pspsdk's startup has already
 * chdir'd into the directory the running EBOOT was loaded from, before main().
 * The chain is crt0's _main -> __libcglue_init(argc, argv) -> __init_cwd(argv[0])
 * -> chdir() of argv[0] truncated after its last '/', and argv[0] is the full
 * path the kernel loaded the module from. So on a Fat or Slim the cwd is
 * "ms0:/PSP/GAME/<folder>/" and on a PSP Go running from internal flash it is
 * "ef0:/PSP/GAME/<folder>/", with no #ifdef and no build knob able to tell them
 * apart -- which is the whole reason this is resolved at runtime.
 *
 * IT ALSO PUTS THE SAVE WHERE THE GAME IS, which is the behaviour a player
 * expects of this console: a PSP game folder is a thing people copy between
 * sticks and between a stick and a Go, and a save inside it travels with it. A
 * compiled-in "ms0:/PSP/GAME/<dir>" would strand the save behind on every one of
 * those moves, and would not exist at all on a Go with no stick in.
 *
 * THE ':' TEST IS THE WHOLE VALIDATION, and it is enough because of what
 * libcglue does with a cwd that has no device: it prepends it to relative paths
 * and hands the result to the kernel, which takes device-qualified paths only.
 * A cwd with no colon in it is therefore not a location at all -- the two ways
 * to get one are a module whose argv[0] the kernel did not supply (nothing was
 * chdir'd, and __cwd is still the empty string this reads back) and a cwd
 * longer than ::kPathMax (getcwd sets ERANGE, copies nothing, and the
 * zero-initialised buffer below is what is left). Both fall back to
 * ::kFallbackDir, which does name a device.
 *
 * TRAILING SEPARATORS ARE STRIPPED because __init_cwd leaves one on ("…/<folder>/")
 * and ::kFallbackDir has none, so the assembly below has to agree about who adds
 * the '/'. Stripping is also what makes ::MakeDirs' walk terminate on the
 * directory itself rather than on an empty last component.
 *
 * A PATH THAT WOULD NOT FIT IS REFUSED, NOT TRUNCATED, and ::resolvedOk stays
 * false for the life of the process: every entry point answers save::bad_path
 * from then on. See ::kPathMax for what truncation would actually do, which is
 * silently name a different file.
 */
bool Resolve() {
    if (resolved) return resolvedOk;
    resolved = true;

    char home[kPathMax] = { };
    if (!getcwd(home, sizeof home) || !std::strchr(home, ':')) {
        std::memcpy(home, kFallbackDir, sizeof kFallbackDir);
    }

    std::size_t n = std::strlen(home);
    while (n && home[n - 1] == '/') home[--n] = '\0';

    // The device is everything up to and including the colon: "ms0:/PSP/GAME/x"
    // gives "ms0:". A cwd this far in is known to have one (above), and the
    // fallback is a literal that does.
    const char* const colon = std::strchr(home, ':');
    const std::size_t dev   = static_cast<std::size_t>(colon - home) + 1;
    if (n == 0 || dev >= sizeof saveDevice) return false;
    std::memcpy(saveDevice, home, dev);
    saveDevice[dev] = '\0';

    // "<home>/<file>", with room for the separator and the terminator.
    if (n + 1 + sizeof(PSP_SAVE_FILENAME) > kPathMax) return false;
    std::memcpy(savePath, home, n);
    savePath[n] = '/';
    std::memcpy(savePath + n + 1, PSP_SAVE_FILENAME, sizeof(PSP_SAVE_FILENAME));

    removable  = std::strncmp(saveDevice, kRemovablePrefix, sizeof kRemovablePrefix - 1) == 0;
    resolvedOk = true;
    return true;
}

/**
 * @brief Whether the device this game's save goes on is there right now.
 *
 * TWO ANSWERS, AND WHICH ONE IS HONEST DEPENDS ON THE DEVICE ::Resolve FOUND.
 * A Memory Stick is removable, and the kernel will say whether one is in: that
 * is MScmIsMediumInserted (pspmscm.h), one devctl to the "mscmhc0:" control
 * device, which touches the slot's status and not the filesystem. Internal
 * flash on a PSP Go is soldered to the board -- "is it present" has one answer
 * and asking the question would be theatre, so this returns true for it without
 * a call.
 *
 * THIS IS WHERE THE OTHER FOUR BACKENDS' ::Mount WOULD BE, and there is nothing
 * for one to do: the kernel mounts the stick, a homebrew module cannot mount or
 * unmount it, and a stick pushed back in after being pulled out is usable again
 * with no action from this file. So no mount, no `mountedByUs`, and no retry
 * throttle of the Switch's or the 3DS's kind -- a status poll costs one devctl
 * and a real operation costs the same, which is why every entry point below can
 * afford to ask.
 *
 * A DEVCTL THAT FAILS IS "NOT INSERTED". MScmIsMediumInserted answers negative
 * for an error and 0 for an empty slot, and this collapses both: a control
 * device that will not answer is not a stick a save can go on, which is the
 * same reading src/3ds/save.cpp's ::Detected gives a failed FSUSER call.
 */
bool Inserted() {
    if (!removable) return true;
    return MScmIsMediumInserted() == 1;
}

/** @brief ::Resolve and ::Inserted together: ready to touch the filesystem. */
bool Ready() {
    return Resolve() && Inserted();
}

/**
 * @brief Which device @p path names, or ::kNoDevice.
 *
 * ONE PATH AND ONE DEVICE, which is the stricter half of the path-based search
 * described in this file's header comment. A caller gets ::savePath from
 * ::save::GetDeviceStatus or ::save::GetSaveLocation and hands it back;
 * anything else is a path the next ::save::GetSaveLocation would not look at,
 * so creating a file there would lose it.
 */
u8 ParseDevice(const char* const path) {
    if (!path || !resolvedOk) return kNoDevice;
    return std::strcmp(path, savePath) == 0 ? kBootDevice : kNoDevice;
}

/**
 * @brief write() until @p n bytes are gone or something refuses them.
 *
 * `w <= 0` AND NOT `w == -1`, for the reason ::Native gives: a failing write on
 * this platform may return the kernel's own error code, which is negative but
 * not -1. A zero-byte write is a refusal too -- there is no short-write case
 * the loop could usefully retry.
 */
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

/** @brief read() until @p n bytes have arrived or the file runs out. */
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

/**
 * @brief fsync(), with errno cleared first so a failure is its own.
 *
 * SEPARATE FROM THE && IT SITS IN for the reason every call in this file clears
 * errno before it: the value left in errno has to belong to THIS call, and the
 * writes that ran before it in the same && may have set one. `>= 0` and not
 * `== 0`, for ::Native's reason.
 */
bool Flush(const int fd) {
    errno = 0;
    const int r = fsync(fd);
    if (r < 0) return false;
    return true;
}

/**
 * @brief Zero-fills from @p from to @p to.
 *
 * WHAT MAKES @p size A PREFIX. The whole allocation unit is written every time,
 * zero past the caller's bytes, so no tail of an earlier and longer save
 * survives inside it -- the same guarantee the GameCube half gets from staging
 * a whole sector and the four stdio backends from this same loop.
 *
 * NOT ftruncate(), AND HERE THERE IS NOT EVEN ONE TO CONSIDER: pspsdk's POSIX
 * layer has no ftruncate at all. The Switch's note on why writing the zeros is
 * better than extending the file still applies -- padding must OVERWRITE, not
 * just extend, or a save that shrank leaves the previous save's bytes inside its
 * own unit -- it just has nothing to argue against on this console.
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

/**
 * @brief The length of @p path, or -1 with @p errOut set to ::Native's code.
 *
 * @p errOut IS WHAT THE CALLER REPORTS AND IT IS NOT ALWAYS AN errno, which is
 * ::Native's rule carried into a helper that has to return two things: a failed
 * stat leaves the POSIX errno where there was one, and the kernel's own
 * negative code where there was not. Only the first means anything to
 * ::MapErrno, which is why a caller restores it into errno and goes through
 * ::Native rather than mapping it itself.
 */
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

/**
 * @brief Makes the directories @p path's file needs, as far as it can.
 *
 * BEST EFFORT, AND open() IS THE JUDGE -- which is the one place this file
 * departs from src/switch/save.cpp's flow rather than its shape. That backend
 * treats a failed mkdir as the error it reports, because its errno is
 * trustworthy: an existing directory is EEXIST and anything else is real. Here
 * a mkdir that fails outside the 0x8001xxxx family leaves errno untouched
 * (::Native), so "it already exists" and "the stick is full" can arrive looking
 * identical -- and reporting the wrong one of those is worse than not reporting
 * it. So this creates what it can and says nothing, and the open() that follows
 * is what fails, with a code that describes the file the caller actually asked
 * for.
 *
 * IT WALKS THE PATH because the fallback (::kFallbackDir) is three levels deep
 * and sceIoMkdir, like every mkdir, creates one. In the ordinary case the
 * directory is the EBOOT's own and every level of it already exists, so this is
 * a handful of mkdirs that all report EEXIST and change nothing -- which is why
 * it is not worth special-casing the two situations apart.
 *
 * THE DEVICE PREFIX IS SKIPPED. mkdir("ms0:") is not a directory creation and
 * the kernel does not answer it usefully; the first component that can be made
 * is the one after the volume's own '/'.
 */
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

/** @brief The directory part of ::savePath, into @p dir. */
void DirOf(char* const dir) {
    const std::size_t n = std::strlen(savePath);
    std::memcpy(dir, savePath, n + 1);
    char* const slash = std::strrchr(dir, '/');
    if (slash) *slash = '\0';
}

/**
 * @brief Moves @p size bytes to or from the save on the boot device.
 *
 * ONE FUNCTION FOR BOTH DIRECTIONS, like every backend since the Wii, because
 * everything except the transfer itself is shared and the length check in
 * particular MUST be: a save and a load disagreeing about whether a file is
 * long enough is how a game writes a prefix it can never read back.
 *
 * THE LENGTH CHECK IS THE GAMECUBE'S. A prefix longer than the file on the
 * medium is save::full, the same code a stick with no room reports, so a game
 * whose save struct has outgrown its existing save file hears the same answer
 * on all six consoles. THE TWO CAUSES ARE NOT DISTINGUISHED, deliberately:
 * "there is not room for this" is true of a short file and a full stick alike,
 * and it is the whole of what a game can act on either way.
 *
 * O_WRONLY IS CORRECT HERE, WHICH IS WORTH SAYING ONLY BECAUSE IT IS NOT ON THE
 * WII U: wut's devoptab has no mode for "write, do not truncate, do not create"
 * and answers EINVAL (src/wiiu/save.cpp's ::Transfer), while pspsdk's layer maps
 * the POSIX access mode straight onto the kernel's -- (flags & 3) + 1 is
 * PSP_O_WRONLY -- and sceIoOpen opens an existing file for writing without
 * touching its length. No O_TRUNC either way: the padding is what guarantees
 * the unit's contents, and truncating first would leave a crash mid-save with a
 * shorter file than the one it started with.
 *
 * fsync BEFORE close AND NOT INSTEAD OF IT. A save is the one moment a player
 * may close the lid or pull the stick out believing their progress is on it;
 * close() flushes the file, fsync() (sceIoSync on this device underneath) is
 * what pushes it through the driver's own buffering, and its failure is a failed
 * save rather than a detail -- which is why ::Flush is part of the && and not a
 * courtesy call.
 */
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
    // CAPTURED BEFORE close() RUNS, because close() clobbers errno on its own
    // account and the failure being reported is the transfer's.
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

/**
 * @brief ::save::Save and both ::save::Load overloads, which differ only in
 *        which way the bytes go and whether the caller named a location.
 *
 * NO SEARCH, BECAUSE THERE IS NOWHERE TO SEARCH -- the Switch's note, and for a
 * slightly different reason: there it is because the console has one device,
 * here because the game is running from one of two and the save belongs where
 * the game is (::kDeviceCount). What survives of the Wii's flow is the
 * VALIDATION: a named path is checked rather than defaulted, because defaulting
 * a wrong path is how a save lands somewhere it will not be found again.
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
 * O_EXCL AND A stat() FIRST, WHICH IS NOT WHAT THE OTHER BACKENDS DO and is
 * this console's own wrinkle. The Switch's note stands on its merits -- "does it
 * exist" and "create it" have to be one operation for save::exists to mean
 * anything -- and O_EXCL is passed here for exactly that. But O_EXCL on this
 * platform is PSP_O_EXCL handed to sceIoOpen, which is the Memory Stick FAT
 * driver's flag to honour rather than a check the C library performs, and a
 * driver that ignored it would turn "create" into "overwrite": a player with a
 * save would lose it to a flow that believed it was making a new one. The stat
 * costs one devctl-free lookup and removes that possibility. There is no
 * TOCTOU window worth worrying about between the two -- nothing else on this
 * console is writing this file -- and the O_EXCL stays because it is the
 * cheaper and stricter of the two tests where it works.
 *
 * THE DIRECTORIES ARE MADE AND NOT CHECKED (::MakeDirs), for the errno reason
 * given there.
 *
 * A HALF-WRITTEN NEW SAVE IS DELETED. If the stick fills up or is pulled out
 * between the open and the fsync, what is left is a file this game will find on
 * its next ::save::GetSaveLocation and load as if it were a save -- worse than
 * no file at all, since the player is offered a corrupt game rather than a new
 * one. The unlink is best-effort: its own failure changes nothing about what the
 * caller is told. THE DIRECTORIES ARE LEFT BEHIND, deliberately -- an empty
 * directory is not mistakable for a save, and on this console the directory is
 * almost always the EBOOT's own, which removing would be a far worse mistake
 * than leaving it.
 */
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

/**
 * @brief The response for an operation that has just finished, which is every
 *        one of them on this console.
 *
 * `done` IS ALWAYS TRUE, and that is this backend's whole async story: see this
 * file's header comment. The `path` is ::savePath's own storage, written once by
 * ::Resolve and never again, so it outlives every call -- which is stricter than
 * save.hpp's promise that it survives until the next ::GetSaveLocation and costs
 * nothing to honour. It is null until something has actually found the save,
 * exactly as on the other five.
 */
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

/**
 * @brief Finds where this game's save lives.
 *
 * A COMPLETED SEARCH THAT FOUND NOTHING IS NOT AN ERROR -- `done` with
 * save::missing and a null `path`, which is the cue to call ::CreateSave. A
 * stick that is not there is different and says save::no_device, so a game can
 * ask for a Memory Stick rather than offer to start a new save on one that is
 * absent. Exactly the five backends before this one, from the same vocabulary.
 *
 * IT IS A ::Resolve AND A stat AND NOTHING MORE: the location is fixed by where
 * the game was launched from, so "finding" the save is asking whether the one
 * path this file publishes exists. A search that finds nothing forgets what it
 * thought it knew, because a `path` handed back afterwards would name a file
 * this very call failed to stat.
 */
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

/**
 * @brief No counterpart on this console, and not a gap to be filled.
 *
 * These two move a `card_stat` -- a 64-byte entry in a GameCube memory card's
 * own directory, carrying the comment block, banner and icon offsets the system
 * menu draws a save with. A file on a Memory Stick has no directory entry of
 * that shape, and the PSP's own Saved Data Utility does not list this save at
 * all (it is not a PARAM.SFO savedata directory; see this file's header
 * comment), so there is nothing here to read and nothing to state.
 *
 * save::no_device AND NOT save::failed, matching the four backends before this
 * one: the honest content of the answer is that the DEVICE this concept belongs
 * to is not present, which is what src/all/save.cpp says on every target with no
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
 * ONE BIT, AND ONE DEVCTL AT MOST. ::Inserted asks the Memory Stick's control
 * device whether a stick is in, which is a kernel call and not a filesystem
 * one -- cheaper than the 3DS's fs:USER round trip, in the same place, and free
 * entirely on a PSP Go running from internal flash, where the answer is a
 * constant. A UI may poll this as often as save.hpp invites it to.
 *
 * THE FIRST CALL ALSO RESOLVES THE LOCATION (::Resolve), which is a getcwd and
 * some string work, once, for the life of the process. It is here rather than
 * deferred to a real operation because the device whose presence this reports
 * is not known until it has run.
 *
 * WHAT THIS CANNOT SAY is that the stick is UNREADABLE as opposed to absent --
 * a stick that is in but broken answers "inserted", and being broken is only
 * discoverable by reading it, which save.hpp's own documentation says of every
 * backend.
 *
 * IT LEAVES ::lastErr ALONE, as save.hpp requires of a status poll: a menu
 * refreshing itself must not overwrite what a failed save left behind for the
 * game to report.
 */
u8 AccessibleDevices() {
    return Ready() ? static_cast<u8>(1u << kBootDevice) : 0;
}

/**
 * @brief Whether device @p index holds usable media, and where a save on it
 *        would live.
 *
 * EVERY INDEX BUT 0 IS save::no_device, which is how a caller walking indices
 * finds out there is one device without being told a count -- the loop stops at
 * the first ::no_device, and on this console that is index 1.
 *
 * save::too_large IS UNREACHABLE HERE, as on the Wii, the Switch, the 3DS and
 * the Wii U: that answer is about a card whose sector width the backend cannot
 * transfer, and a filesystem presents no such number. save::busy is unreachable
 * too; the probe is one devctl and always has its answer.
 *
 * A WRITE-PROTECTED STICK STILL ANSWERS save::found, deliberately -- the 3DS's
 * reasoning, and this console has the same lock switch on the side of its
 * media. This call's vocabulary has no code for "there, and not writable", and
 * ::no_device would tell a player to insert something that is already in. The
 * write itself reports save::denied, which is where that fact belongs.
 */
Response GetDeviceStatus(const u8 index) {
    if (index >= kDeviceCount) return { .done = true, .err = no_device, .path = nullptr };
    if (!Ready())              return { .done = true, .err = no_device, .path = nullptr };
    return { .done = true, .err = found, .path = savePath };
}

/**
 * @brief Which devices could take a save of @p size bytes.
 *
 * NOT A SWEEP, FOR THE SWITCH'S AND THE 3DS'S REASON. The GameCube spreads this
 * over several calls because CARD_GetFreeBlocks needs a mount and a mount is the
 * slowest thing it does; here the measurement is one devctl, there is no ticked
 * form of it to spread, and a partial answer would cost what a whole one costs.
 * So this measures and answers completely, and does NOT return 0 on a first
 * call -- which save.hpp allows for rather than requires ("zero means nowhere,
 * OR not yet").
 *
 * sceIoDevctl AND NOT statvfs, AND pspsdk DOES HAVE statvfs -- which is the
 * Switch's and the 3DS's choice for their reason exactly. libcglue's statvfs is
 * built out of this same SCE_PR_GETDEV devctl and then distributes the answer
 * across f_bsize / f_frsize / f_blocks / f_bfree / f_bavail in a way that is its
 * own implementation's business; this file would be depending on which field got
 * which product. The devctl is declared in pspsdk's own header
 * (pspiofilemgr_devctl.h), fills a documented SceDevInf, and cannot be misread.
 *
 * IT ANSWERS IN CLUSTERS, WHICH IS THE 3DS'S WRINKLE AND WORSE HERE: free bytes
 * are freeClusters x sectorCount x sectorSize, three 32-bit numbers, and the
 * product passes 32 bits on any stick bigger than a few gigabytes. The widening
 * happens BEFORE the first multiply rather than after it, because a truncated
 * product reports a nearly-empty 32GB stick as full.
 *
 * A DEVCTL THAT FAILS REPORTS THE DEVICE AS FREE, NOT AS FULL, which is the one
 * answer in this file that is deliberately not narrowed by a measurement -- and
 * src/wiiu/save.cpp is the precedent, where no measurement is possible at all.
 * The case that needs it is a PSP Go's ef0:, whose driver need not implement
 * SCE_PR_GETDEV: with `return 0` there, a Go running from internal flash would
 * be told it can never save, which is both wrong and unrecoverable. Reporting
 * the bit means a save that really does not fit fails at the write with
 * save::full and ENOSPC -- a worse-timed answer than a measurement, and a far
 * better one than a refusal.
 *
 * AN EXISTING SAVE BIG ENOUGH FOR @p size COUNTS AS FREE even on a full stick,
 * because overwriting a file consumes no new space -- save.hpp's rule, and the
 * ordinary case for a player whose stick is packed with other games.
 *
 * THE CACHE IS THE SAME ONE, invalidated by a completed save or create and by a
 * different @p size. There is no "set of accessible devices" for it to be
 * invalidated by: with one device, a stick that went away is an ::Inserted that
 * fails on the next measurement anyway.
 */
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
