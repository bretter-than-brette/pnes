/**
 * @file save.cpp
 * @brief Memory-card save for the GameCube (libogc CARD_*), and the FAT-volume
 *        backend that does the same job on the Wii, split at the #ifdef.
 *
 * GameCube: a ticked state machine, one libogc step per call, so video and audio
 * keep running between the card's blocking calls. The search is identity-based,
 * which is why CreateSave can let the caller name the file. @p size is a prefix
 * length -- a card file's length is fixed at creation. Paths are "a:name" /
 * "b:name", rejected rather than defaulted. Neither half logs.
 */
#include "internal.hpp"
#include <pnes/save.hpp>

namespace {

// Three tiers is all a caller can act on: nothing there, something there but
// silent, or a real answer. Both halves keep the best any device gave.
constexpr u8 Rank(const save::eError err) {
    switch (err) {
        case save::no_device: return 0;   // nothing in this slot
        case save::busy:      return 1;   // something, but it did not answer
        default:              return 2;   // a real verdict about a real device
    }
}

// An empty string is not a path, which is the half the Wii was missing: a
// Response built by hand carries "", and that has to mean the same as nullptr.
const char* PathOf(const save::Response* const response) {
    return (response && response->path && response->path[0]) ? response->path : nullptr;
}

} // namespace

#ifdef TARGET_GC

#include <ogc/card.h>
#include <ogc/cache.h>
#include <cstddef>
#include <cstring>

namespace {

// Not CARD_Init(NULL, NULL), which switches off the filtering CARD_FindFirst
// needs -- a search then finds nothing, or every other game's saves.
#ifndef OGC_GAME_CODE
#define OGC_GAME_CODE "PNES"
#endif
#ifndef OGC_COMPANY_CODE
#define OGC_COMPANY_CODE "BR"
#endif
#ifndef OGC_FILENAME
#define OGC_FILENAME "pnes_save"
#endif
constexpr char kGameCode[] = OGC_GAME_CODE;
constexpr char kCompany[]  = OGC_COMPANY_CODE;
constexpr char kFileName[] = OGC_FILENAME;

constexpr char kDefaultPath[] = "a:" OGC_FILENAME;
static_assert(sizeof(kDefaultPath) == sizeof(kFileName) + 2, "kDefaultPath must be \"a:\" + the name");
static_assert(sizeof(kGameCode) == 5, "OGC_GAME_CODE must be exactly 4 characters");
static_assert(sizeof(kCompany)  == 3, "OGC_COMPANY_CODE must be exactly 2 characters");
static_assert(sizeof(kFileName) > 1 && sizeof(kFileName) <= CARD_FILENAMELEN,
    "OGC_FILENAME must be 1-31 characters (CARD_FILENAMELEN is 32, terminator included)");

// What GetDeviceStatus hands back, and the only reason it has a path to hand
// back: OGC_FILENAME is private to this library, so nothing a game includes can
// spell "b:<name>" for itself.
constexpr char kSlotBPath[] = "b:" OGC_FILENAME;
static_assert(sizeof(kSlotBPath) == sizeof(kFileName) + 2, "kSlotBPath must be \"b:\" + the name");
constexpr const char* kDevicePath[2] = { kDefaultPath, kSlotBPath };

constexpr u8 kDeviceCount = 2;
static_assert(kDeviceCount <= 8, "::save::AccessibleDevices returns the mask in a u8");

constexpr u32 kSectorMax = 8192;

// 32-byte aligned because CARD_Read/CARD_Write document it. Also what makes
// @p size a prefix: the whole sector moves every time, zero-filled past the
// caller's bytes.
alignas(32) u8 staging[kSectorMax];

// Copied in and out rather than used in place: the sequence spans frames, and
// CARD_SetStatus takes a non-const card_stat and writes back what it derives
// from icon_addr.
card_stat metaStat;

void* metaOut = nullptr;

// 40 KiB the card system keeps using for as long as the card is mounted, so it
// cannot be a local -- and is better static than a malloc at the moment a player
// asks to save.
alignas(32) u8 workarea[CARD_WORKAREA_SIZE];

enum class Op : u8 { None, Saving, Loading, Creating, Deleting, Finding, GettingMeta, SettingMeta, Surveying };

constexpr bool IsMeta(Op o) { return o == Op::GettingMeta || o == Op::SettingMeta; }

enum class Phase : u8 { Probe, Mount, Resolve, Transfer, Finish };

Op          op        = Op::None;
Phase       phase     = Phase::Probe;
s32         chn       = CARD_SLOTA;
u32         sector    = 0;
std::size_t pending   = 0;        ///< @p size as latched on the first tick.
u8*         dest      = nullptr;  ///< Load's @p tar, likewise latched.
card_file   file;
bool        mounted   = false;
bool        opened    = false;
bool        detached  = false;    ///< Set from the detach callback, off-thread.

save::eError lastErr = save::found;

constexpr s32 kSlots[2] = { CARD_SLOTA, CARD_SLOTB };
u8 slotIndex = 0;
bool slotFixed = false;

// Last slot wins was a wrong answer: a one-card console answered a search
// no_device, because slot B's "nothing here" overwrote slot A's missing -- so a
// game asked for a memory card instead of offering to create a save. Ranked
// instead, ties to the earlier slot.
save::eError sweepErr  = save::no_device;
bool         sweepSeen = false;

// A channel answering BUSY has not decided yet. Capped, or a `while (!done)`
// caller pumps frames until the console is switched off.
constexpr u8 kProbeRetryLimit = 60;
u8 probeRetries = 0;

// A status poll arriving mid-sequence answers from here rather than probing:
// this SDK install ships no card.c, so whether CARD_ProbeEx may touch a channel
// another CARD_* call is partway through is unverifiable. busy is never cached --
// it is the absence of an answer, and caching it would make a UI flicker.
save::eError deviceErr[kDeviceCount]   = { save::no_device, save::no_device };
bool         deviceKnown[kDeviceCount] = {};

// Corruption is not probeable: only CARD_Mount discovers it, which is the
// expensive call GetDeviceStatus exists to avoid. So whoever paid for a mount
// records the verdict here. Only the two health codes set it -- a BUSY or NOCARD
// mount says nothing about the card.
bool deviceBroken[kDeviceCount] = {};

constexpr bool IsUnusableCode(s32 err) {
    return err == CARD_ERROR_BROKEN || err == CARD_ERROR_ENCODING;
}

// Free space cannot be probed: CARD_GetFreeBlocks reads a mounted card's own
// bookkeeping, so one answer costs a CARD_Mount. Hence a sweep, one step per
// call, keyed on the size and device set it was measured for.
std::size_t surveySize   = 0;   ///< Size the in-flight sweep is measuring for.
u32         surveySector = 0;   ///< Current slot's sector width, sweep-local.
u8          surveyMask   = 0;   ///< Bits accumulated so far by the in-flight sweep.
u8          surveyAccess = 0;   ///< Accessible mask the in-flight sweep began with.

u8          freeMask       = 0;      ///< Last finished sweep's answer.
std::size_t freeMaskSize   = 0;      ///< The size that answer was measured for.
u8          freeMaskAccess = 0;      ///< The accessible mask it was measured against.
bool        freeMaskValid  = false;  ///< False until a sweep has finished.

void EnsureInit() {
    static bool initialised = false;
    if (initialised) return;
    CARD_Init(kGameCode, kCompany);
    initialised = true;
}

char targetName[CARD_FILENAMELEN] = {};

char locationBuf[2 + CARD_FILENAMELEN] = {};
bool haveLocation = false;

// Runs in libogc's EXI context, so it only raises a flag. Mid-sequence removal
// is the one failure mode ticking introduces.
void DetachCallback(s32 chan, s32 /*result*/) {
    if (chan == chn) detached = true;
}

save::eError MapError(s32 err) {
    switch (err) {
        case CARD_ERROR_READY:       return save::found;
        case CARD_ERROR_BUSY:        return save::busy;
        case CARD_ERROR_NOCARD:
        case CARD_ERROR_WRONGDEVICE: return save::no_device;
        case CARD_ERROR_NOFILE:      return save::missing;
        case CARD_ERROR_EXIST:       return save::exists;
        // No save::corrupt: a broken card and a foreign-encoding one are both
        // media this console cannot write to, and neither is a game's to repair.
        case CARD_ERROR_BROKEN:
        case CARD_ERROR_ENCODING:    return save::no_device;
        case CARD_ERROR_INSSPACE:
        case CARD_ERROR_NOENT:
        case CARD_ERROR_LIMIT:       return save::full;
        case CARD_ERROR_NOPERM:      return save::denied;
        case CARD_ERROR_NAMETOOLONG: return save::bad_path;
        default:                     return save::failed;
    }
}

void NoteSlotOutcome(const save::eError err) {
    if (!sweepSeen || Rank(err) > Rank(sweepErr)) {
        sweepErr  = err;
        sweepSeen = true;
    }
}

void Abort() {
    if (opened)  { CARD_Close(&file); opened = false; }
    if (mounted) { CARD_Unmount(chn); mounted = false; }
    op           = Op::None;
    phase        = Phase::Probe;
    pending      = 0;
    dest         = nullptr;
    slotIndex    = 0;
    slotFixed    = false;
    probeRetries = 0;
}

// Returns TRUE -- the sequence is OVER, not that it worked. Returning false made
// every error here unreportable: the failure abandoned the sequence while
// answering "not finished", so the next tick began the same doomed attempt.
bool Fail(save::eError err) {
    lastErr = err;
    Abort();
    return true;
}

bool Exhausted(save::eError err) {
    const bool finding = (op == Op::Finding);
    lastErr = err;
    if (finding) haveLocation = false;
    Abort();
    return true;
}

bool ParsePath(const char* path, s32& slotOut, char (&nameOut)[CARD_FILENAMELEN]) {
    if (!path || !path[0] || path[1] != ':') return false;
    switch (path[0]) {
        case 'a': case 'A': slotOut = CARD_SLOTA; break;
        case 'b': case 'B': slotOut = CARD_SLOTB; break;
        default: return false;
    }
    const char* const name = path + 2;
    const std::size_t len  = std::strlen(name);
    if (len == 0 || len >= CARD_FILENAMELEN) return false;
    std::memcpy(nameOut, name, len + 1);
    return true;
}

void RememberLocation(s32 slot, const char* name) {
    locationBuf[0] = (slot == CARD_SLOTB) ? 'b' : 'a';
    locationBuf[1] = ':';
    std::strncpy(locationBuf + 2, name, CARD_FILENAMELEN - 1);
    locationBuf[sizeof(locationBuf) - 1] = '\0';
    haveLocation = true;
}

// CARD_ProbeEx and not CARD_Probe: sect_size is the one quantity this file
// cannot guess. No mount, which is what makes GetDeviceStatus cheap. Shared with
// ProbeCurrentSlot so a UI never shows a slot the next Save would refuse, and it
// touches no sequence state, which is what makes it safe from a status poll.
s32 ProbeMedia(s32 slot, u32& sectorOut, save::eError& errOut) {
    s32 sect = 0;
    const s32 err = CARD_ProbeEx(slot, nullptr, &sect);

    // NOCARD WITH A SECTOR SIZE IN HAND IS A CARD, and that is the ordinary
    // state of one: READY comes back only for a card already mounted, and every
    // sequence probes before it mounts. The discriminator is the out parameter --
    // only the unmounted-card path writes @p sect -- which is why it is zeroed
    // first. Disassembly in .ignore/comment-trim-salvage/save-backends.md.
    const bool present = (err == CARD_ERROR_READY) ||
                         (err == CARD_ERROR_NOCARD && sect > 0);
    if (!present) {
        errOut = MapError(err);
        return err;
    }

    if (sect <= 0 || static_cast<u32>(sect) > kSectorMax) {
        // Wider sector than the staging buffer: refused rather than transferred
        // at the wrong width. too_large, so a UI can say which card it was.
        errOut = save::too_large;
        return CARD_ERROR_FATAL_ERROR;
    }
    sectorOut = static_cast<u32>(sect);
    return CARD_ERROR_READY;
}

bool ProbeCurrentSlot() {
    u32 sect = 0;
    save::eError portable = save::found;
    const s32 err = ProbeMedia(kSlots[slotIndex], sect, portable);
    if (err != CARD_ERROR_READY) {
        lastErr = portable;
        return false;
    }
    if (op != Op::Finding && static_cast<u32>(pending) > sect) {
        lastErr = save::too_large;
        return false;
    }
    chn    = kSlots[slotIndex];
    sector = sect;
    return true;
}

save::eError RefreshDevice(u8 index) {
    u32 sect = 0;
    save::eError portable = save::found;
    const s32 err = ProbeMedia(kSlots[index], sect, portable);
    if (err == CARD_ERROR_BUSY) return save::busy;

    if (err != CARD_ERROR_READY) {
        deviceBroken[index] = false;
    }

    deviceErr[index] = (err == CARD_ERROR_READY) ? save::found : portable;

    if (deviceBroken[index]) deviceErr[index] = save::no_device;

    deviceKnown[index]  = true;
    return deviceErr[index];
}

save::eError CachedDevice(u8 index) {
    return deviceKnown[index] ? deviceErr[index] : save::busy;
}

// Two ways to say yes, and the second is the one a player cares about: enough
// free blocks, or this game's save already there and already big enough --
// overwriting consumes nothing, so a full card is still somewhere a save works.
// CARD_GetFreeBlocks' return is ambiguous in this libogc (documented as either
// an error code or the count), so both readings are accepted.
bool SlotHasRoom() {
    const u32 need = (static_cast<u32>(surveySize) + surveySector - 1) / surveySector;

    u16       blocks = 0;
    const s32 err    = CARD_GetFreeBlocks(chn, &blocks);
    if (err >= CARD_ERROR_READY) {
        const u32 freeBlocks = (blocks != 0)
            ? static_cast<u32>(blocks)
            : (err > CARD_ERROR_READY ? static_cast<u32>(err) : 0u);
        if (freeBlocks >= need) return true;
    }

    card_dir dir;
    if (CARD_FindFirst(chn, &dir, false) != CARD_ERROR_READY) return false;

    card_file probe;
    if (CARD_Open(chn, dir.filename, &probe) != CARD_ERROR_READY) return false;
    const bool fits = static_cast<u32>(probe.len) >= static_cast<u32>(surveySize);
    CARD_Close(&probe);
    return fits;
}

void SurveyPublish() {
    freeMask       = surveyMask;
    freeMaskSize   = surveySize;
    freeMaskAccess = surveyAccess;
    freeMaskValid  = true;
    Abort();
}

void SurveyNextSlot() {
    if (mounted) { CARD_Unmount(chn); mounted = false; }
    ++slotIndex;
    phase = Phase::Probe;
    if (slotIndex >= kDeviceCount) SurveyPublish();
}

void SurveyTick() {
    if (detached) { Abort(); return; }

    switch (phase) {
        case Phase::Probe: {
            u32          sect     = 0;
            save::eError portable = save::found;
            if (ProbeMedia(kSlots[slotIndex], sect, portable) != CARD_ERROR_READY) {
                SurveyNextSlot();
                return;
            }
            chn          = kSlots[slotIndex];
            surveySector = sect;
            phase        = Phase::Mount;
            return;
        }

        case Phase::Mount: {
            const s32 err = CARD_Mount(chn, workarea, DetachCallback);
            if (err != CARD_ERROR_READY) {
                if (IsUnusableCode(err)) deviceBroken[slotIndex] = true;
                SurveyNextSlot();
                return;
            }
            deviceBroken[slotIndex] = false;
            mounted = true;
            phase   = Phase::Resolve;
            return;
        }

        case Phase::Resolve:
            if (SlotHasRoom()) surveyMask |= static_cast<u8>(1u << slotIndex);
            phase = Phase::Finish;
            return;

        case Phase::Transfer:
        case Phase::Finish:
        default:
            SurveyNextSlot();
            return;
    }
}

void SurveyBegin(std::size_t size, u8 accessible) {
    op           = Op::Surveying;
    phase        = Phase::Probe;
    slotIndex    = 0;
    slotFixed    = false;
    surveySize   = size;
    surveySector = 0;
    surveyMask   = 0;
    surveyAccess = accessible;
    detached     = false;
    EnsureInit();
}

bool AdvanceSlot() {
    if (opened)  { CARD_Close(&file); opened = false; }
    if (mounted) { CARD_Unmount(chn); mounted = false; }
    if (slotFixed) return false;
    targetName[0] = '\0';
    ++slotIndex;
    return slotIndex < 2;
}

bool Begin(Op want, const u8* src, u8* tar, std::size_t size, const char* path) {
    if (want != Op::Finding && want != Op::Deleting && !IsMeta(want)) {
        // The widest sector this file is built for; the card's own is not known
        // until ProbeCurrentSlot has asked.
        if (size == 0 || size > kSectorMax) return Fail(save::too_large);
    }

    slotFixed = false;
    slotIndex = 0;
    targetName[0] = '\0';

    if (path) {
        s32 slot = CARD_SLOTA;
        if (!ParsePath(path, slot, targetName)) return Fail(save::bad_path);
        slotIndex = (slot == kSlots[0]) ? 0 : 1;
        slotFixed = true;
    } else if (want != Op::Finding && haveLocation) {
        s32 slot = CARD_SLOTA;
        if (ParsePath(locationBuf, slot, targetName)) {
            slotIndex = (slot == kSlots[0]) ? 0 : 1;
            slotFixed = true;
        }
    }

    if (want == Op::Saving || want == Op::Creating) {
        if (!src) return Fail(save::failed);
        // Copied up front, not on the transfer tick: the game is free to carry
        // on mutating its own state across the frames this takes.
        std::memset(staging, 0, kSectorMax);
        std::memcpy(staging, src, size);
    } else if (want == Op::Loading) {
        if (!tar) return Fail(save::failed);
        dest = tar;
    } else if (want == Op::SettingMeta) {
        if (!src) return Fail(save::failed);
        std::memcpy(&metaStat, src, sizeof metaStat);
        metaOut = nullptr;
    } else if (want == Op::GettingMeta) {
        if (!tar) return Fail(save::failed);
        metaOut = tar;
    }

    op       = want;
    pending  = size;
    phase    = Phase::Probe;
    detached = false;

    sweepErr     = save::no_device;
    sweepSeen    = false;
    probeRetries = 0;

    CARD_Init(kGameCode, kCompany);
    return false;
}

s32 Resolve() {
    if (op == Op::Finding) {
        card_dir dir;
        const s32 err = CARD_FindFirst(chn, &dir, false);
        if (err != CARD_ERROR_READY) return err;
        std::memcpy(targetName, dir.filename, CARD_FILENAMELEN - 1);
        targetName[CARD_FILENAMELEN - 1] = '\0';
        return CARD_ERROR_READY;
    }

    if (op == Op::Creating) {
        const u32 len = ((static_cast<u32>(pending) + sector - 1) / sector) * sector;
        const s32 err = CARD_Create(chn, targetName, len, &file);
        if (err != CARD_ERROR_READY) return err;   // incl. CARD_ERROR_EXIST
        opened = true;
        return CARD_ERROR_READY;
    }

    if (op == Op::Deleting) {
        if (targetName[0] == '\0') {
            card_dir dir;
            const s32 found = CARD_FindFirst(chn, &dir, false);
            if (found != CARD_ERROR_READY) return found;   // incl. CARD_ERROR_NOFILE
            std::memcpy(targetName, dir.filename, CARD_FILENAMELEN - 1);
            targetName[CARD_FILENAMELEN - 1] = '\0';
        }
        return CARD_Delete(chn, targetName);
    }

    if (targetName[0] == '\0') {
        card_dir dir;
        const s32 found = CARD_FindFirst(chn, &dir, false);
        if (found != CARD_ERROR_READY) return found;
        std::memcpy(targetName, dir.filename, CARD_FILENAMELEN - 1);
        targetName[CARD_FILENAMELEN - 1] = '\0';
    }

    const s32 err = CARD_Open(chn, targetName, &file);
    if (err != CARD_ERROR_READY) return err;
    opened = true;
    if (static_cast<u32>(file.len) < sector ||
        pending > static_cast<std::size_t>(file.len)) {
        return CARD_ERROR_LIMIT;
    }
    RememberLocation(chn, targetName);
    return CARD_ERROR_READY;
}

bool Tick(Op want, const u8* src, u8* tar, std::size_t size, const char* path) {
    if (op == Op::None) return Begin(want, src, tar, size, path);

    if (op != want)      { return false; }   // see ::Busy, checked before Tick
    if (detached)        return Fail(save::no_device);
    if (want != Op::Finding && pending != size) return Fail(save::failed);

    switch (phase) {
        case Phase::Probe:
            if (!ProbeCurrentSlot()) {
                // Not settled is not empty: stay on this slot and ask again.
                // Advancing here turned a card still coming up on the bus into
                // "no memory card in either slot".
                if (lastErr == save::busy) {
                    if (++probeRetries < kProbeRetryLimit) return false;
                    lastErr    = save::failed;
                }
                probeRetries = 0;
                NoteSlotOutcome(lastErr);
                if (AdvanceSlot()) return false;      // try the other slot
                return Exhausted(sweepErr);
            }
            probeRetries = 0;
            phase = Phase::Mount;
            return false;

        case Phase::Mount: {
            const s32 err = CARD_Mount(chn, workarea, DetachCallback);
            if (err != CARD_ERROR_READY) {
                if (IsUnusableCode(err)) deviceBroken[slotIndex] = true;
                NoteSlotOutcome(MapError(err));
                if (AdvanceSlot()) { phase = Phase::Probe; return false; }
                return Exhausted(sweepErr);
            }
            deviceBroken[slotIndex] = false;   // it mounted; whatever it was, it is not that now
            mounted = true;
            phase   = Phase::Resolve;
            return false;
        }

        case Phase::Resolve: {
            const s32 err = Resolve();
            if (err != CARD_ERROR_READY) {
                NoteSlotOutcome(MapError(err));
                if (AdvanceSlot()) { phase = Phase::Probe; return false; }
                return Exhausted(sweepErr);
            }
            if (op == Op::Finding) {
                RememberLocation(chn, targetName);
                lastErr = save::found;
                Abort();
                return true;
            }
            // A delete is already done once Resolve returns -- CARD_Delete took
            // the entry -- so there is nothing opened for Finish to close. The
            // location is dropped rather than remembered: it would name an entry
            // that no longer exists.
            if (op == Op::Deleting) {
                haveLocation  = false;
                freeMaskValid = false;
                lastErr       = save::found;
                Abort();
                return true;
            }
            phase = Phase::Transfer;
            return false;
        }

        case Phase::Transfer: {
            if (IsMeta(op)) {
                const s32 err = (op == Op::GettingMeta)
                    ? CARD_GetStatus(chn, file.filenum, &metaStat)
                    : CARD_SetStatus(chn, file.filenum, &metaStat);
                if (err != CARD_ERROR_READY) return Fail(MapError(err));
                if (metaOut) std::memcpy(metaOut, &metaStat, sizeof metaStat);
                phase = Phase::Finish;
                return false;
            }

            // Cache maintenance around the DMA both ways. libogc may do it
            // inside card.c, which is not part of this SDK install; doing it
            // twice costs cycles, not doing it costs a wrong save on a dirty line.
            DCFlushRange(staging, sector);
            if (op == Op::Loading) {
                // Whole sector, not `pending`: CARD_Read quantises to 512, and
                // the prefix is taken out of the buffer rather than off the card.
                const s32 err = CARD_Read(&file, staging, sector, 0);
                if (err != CARD_ERROR_READY) return Fail(MapError(err));
                DCInvalidateRange(staging, sector);
                std::memcpy(dest, staging, pending);
            } else {
                const s32 err = CARD_Write(&file, staging, sector, 0);
                if (err != CARD_ERROR_READY) return Fail(MapError(err));
            }
            phase = Phase::Finish;
            return false;
        }

        case Phase::Finish: {
            // Unmounted every time: a card left mounted is one whose removal
            // goes unnoticed until the next write fails.
            const s32 close   = CARD_Close(&file);
            opened = false;
            const s32 unmount = CARD_Unmount(chn);
            mounted = false;
            const bool created = (op == Op::Creating);
            if (op == Op::Saving || op == Op::Creating) freeMaskValid = false;
            const s32 slot = chn;
            Abort();
            if (close != CARD_ERROR_READY)   return Fail(MapError(close));
            if (unmount != CARD_ERROR_READY) return Fail(MapError(unmount));
            if (created) RememberLocation(slot, targetName);
            lastErr = save::found;
            return true;
        }
    }

    return Fail(save::failed);
}

} // namespace

namespace {

bool Busy(Op want) {
    // A sweep has no claim on the card -- it is a cache refresh for a menu, and
    // nothing advances it except FreeDevices. So a real operation abandons it.
    if (op == Op::Surveying && want != Op::Surveying) { Abort(); return false; }
    return op != Op::None && op != want;
}

save::Response Report(bool done) {
    return { .done = done,
             .err  = done ? lastErr : save::found,
             .path = (done && haveLocation) ? locationBuf : nullptr };
}

} // namespace

namespace save {

Response Save(const void* src, const std::size_t size) {
    if (Busy(Op::Saving)) return { .done = false, .err = busy, .path = nullptr };
    return Report(Tick(Op::Saving, static_cast<const u8*>(src), nullptr, size, nullptr));
}

Response Load(void* tar, const std::size_t size) {
    if (Busy(Op::Loading)) return { .done = false, .err = busy, .path = nullptr };
    return Report(Tick(Op::Loading, nullptr, static_cast<u8*>(tar), size, nullptr));
}

Response Load(void* tar, const std::size_t size, const Response* response) {
    if (Busy(Op::Loading)) return { .done = false, .err = busy, .path = nullptr };
    return Report(Tick(Op::Loading, nullptr, static_cast<u8*>(tar), size, PathOf(response)));
}

Response CreateSave(const void* src, const std::size_t size, const Response* response) {
    if (Busy(Op::Creating)) return { .done = false, .err = busy, .path = nullptr };
    const char* const named = PathOf(response);
    return Report(Tick(Op::Creating, static_cast<const u8*>(src), nullptr, size, named ? named : kDefaultPath));
}

Response GetSaveLocation() {
    if (Busy(Op::Finding)) return { .done = false, .err = busy, .path = nullptr };
    return Report(Tick(Op::Finding, nullptr, nullptr, 0, nullptr));
}

Response Delete() {
    if (Busy(Op::Deleting)) return { .done = false, .err = busy, .path = nullptr };
    return Report(Tick(Op::Deleting, nullptr, nullptr, 0, nullptr));
}

Response GetMetaData(void* tar) {
    if (Busy(Op::GettingMeta)) return { .done = false, .err = busy, .path = nullptr };
    return Report(Tick(Op::GettingMeta, nullptr, static_cast<u8*>(tar), 0, nullptr));
}

/// CARD_SetStatus rather than CARD_SetStatusEx, so nothing here can disturb the
/// identity pair CARD_FindFirst filters on. One trap: frame 0's icon speed must
/// be nonzero, or libogc forces its format to 0 and a static icon renders as
/// nothing -- and CARD_SetIconSpeed is defective in this libogc.
Response SetMetaData(const void* src) {
    if (Busy(Op::SettingMeta)) return { .done = false, .err = busy, .path = nullptr };
    return Report(Tick(Op::SettingMeta, static_cast<const u8*>(src), nullptr, 0, nullptr));
}

u8 AccessibleDevices() {
    const bool idle = (op == Op::None);
    if (idle) EnsureInit();

    u8 mask = 0;
    for (u8 i = 0; i < kDeviceCount; ++i) {
        const eError err = idle ? RefreshDevice(i) : CachedDevice(i);
        if (err == found) mask |= static_cast<u8>(1u << i);
    }
    return mask;
}

// One step of the sweep per call; the answer is the last FINISHED sweep's. Zero
// is "nowhere, or not yet" and the two are not distinguished. The cache is keyed
// on the question, not on time: a mask measured for one size or one set of
// inserted cards says nothing about another.
u8 FreeDevices(const std::size_t size) {
    if (size == 0 || size > kSectorMax) return 0;

    const u8   accessible = AccessibleDevices();
    const bool fresh      = freeMaskValid && freeMaskSize == size && freeMaskAccess == accessible;

    if (op == Op::Surveying) {
        if (surveySize == size && surveyAccess == accessible) {
            SurveyTick();                 // our sweep: advance it
            return fresh ? freeMask : 0;
        }
        Abort();
    }

    if (op != Op::None) return fresh ? freeMask : 0;

    if (fresh) return freeMask;

    if (accessible == 0) {
        freeMask       = 0;
        freeMaskSize   = size;
        freeMaskAccess = 0;
        freeMaskValid  = true;
        return 0;
    }

    SurveyBegin(size, accessible);
    return 0;
}

// The only entry point here that is not a tick of the state machine: one EXI
// transaction, so there is nothing to span frames for. It skips Busy and Report,
// both of which are about the in-flight sequence's outcome, and an out-of-range
// index is no_device rather than a refusal.
Response GetDeviceStatus(const u8 index) {
    if (index >= kDeviceCount) return { .done = true, .err = no_device, .path = nullptr };

    eError err = busy;
    if (op != Op::None) {
        err = CachedDevice(index);
    } else {
        EnsureInit();
        err = RefreshDevice(index);
    }

    if (err == busy) return { .done = false, .err = busy, .path = nullptr };

    return { .done = true,
             .err  = err,
             .path = (err == found) ? kDevicePath[index] : nullptr };
}

} // namespace save

#else // TARGET_WII

/*
 * The Wii saves to a FAT volume. libfat is blocking with no way to advance a
 * transfer by one step, so nothing is ticked and save::busy is never returned.
 * The search is path-based -- no gamecode filter exists here -- so the location
 * is fixed by the build and any other spelling is bad_path.
 */

#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

#ifndef WII_SAVE_DIR
#define WII_SAVE_DIR "pnes"
#endif
#ifndef WII_SAVE_FILENAME
#define WII_SAVE_FILENAME "pnes_save"
#endif

// The paths are assembled by the preprocessor, so a slash or colon here would
// silently point them at another game's directory.
constexpr bool IsPlainName(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s == '/' || *s == '\\' || *s == ':') return false;
    }
    return true;
}
static_assert(IsPlainName(WII_SAVE_DIR),
    "WII_SAVE_DIR must be a single non-empty name with no '/', '\\' or ':'");
static_assert(IsPlainName(WII_SAVE_FILENAME),
    "WII_SAVE_FILENAME must be a single non-empty name with no '/', '\\' or ':'");
static_assert(sizeof(WII_SAVE_DIR) <= 256, "WII_SAVE_DIR must be at most 255 characters");
static_assert(sizeof(WII_SAVE_FILENAME) <= 256, "WII_SAVE_FILENAME must be at most 255 characters");

constexpr char kSdDir[]   = "sd:/"  WII_SAVE_DIR;
constexpr char kUsbDir[]  = "usb:/" WII_SAVE_DIR;
constexpr char kSdFile[]  = "sd:/"  WII_SAVE_DIR "/" WII_SAVE_FILENAME;
constexpr char kUsbFile[] = "usb:/" WII_SAVE_DIR "/" WII_SAVE_FILENAME;

// Two, and the order is the policy: SD first, then USB, which is where a save
// lands when the caller names nowhere. save.hpp documents the indices, so they
// cannot be reordered to change that.
constexpr u8 kDeviceCount = 2;
static_assert(kDeviceCount <= 8, "::save::AccessibleDevices returns the mask in a u8");

constexpr const char* kDevicePath[kDeviceCount] = { kSdFile, kUsbFile };
constexpr const char* kDeviceDir[kDeviceCount]  = { kSdDir,  kUsbDir  };
constexpr const char* kVolume[kDeviceCount]     = { "sd",    "usb"    };
constexpr const char* kVolumeRoot[kDeviceCount] = { "sd:/",  "usb:/"  };

constexpr u8 kDefaultDevice = 0;

constexpr u8 kNoDevice = 0xFF;

// `const DISC_INTERFACE*` because libogc declares __io_wiisd const and
// __io_usbstorage non-const, and that is the one type both fit without a cast.
const DISC_INTERFACE* const kIface[kDeviceCount] = { &__io_wiisd, &__io_usbstorage };

// The GameCube's sector size, so @p size means the same prefix length on both
// halves. Not a limit here, unlike kSectorMax -- a bigger save takes more units.
constexpr std::size_t kAllocUnit = 8192;

const u8 kZeros[256] = { };

bool started[kDeviceCount] = { };
bool mountedByUs[kDeviceCount] = { };

// __io_usbstorage's startup() enumerates the USB bus and waits, which on a Wii
// with nothing plugged in takes seconds -- so a failed one is not retried for
// kStartupRetryMs, or a polling menu freezes for as long as it is open.
u64  startupAt[kDeviceCount]    = { };
bool startupTried[kDeviceCount] = { };
constexpr u32 kStartupRetryMs   = 2000;

// A volume libfat will not mount is not somewhere this game can save, and there
// is no code for "the medium is damaged". Cleared when the medium leaves, which
// is what makes a reformat recoverable without restarting.
bool deviceBroken[kDeviceCount] = { };

bool haveLocation = false;
u8   locationDev  = 0;

save::eError lastErr = save::found;

bool        freeValid  = false;
u8          freeMask   = 0;
u8          freeAccess = 0;
std::size_t freeSize   = 0;

constexpr std::size_t RoundUp(const std::size_t n) {
    return ((n + kAllocUnit - 1) / kAllocUnit) * kAllocUnit;
}

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

void Release(const u8 d) {
    if (!mountedByUs[d]) return;
    fatUnmount(kVolume[d]);
    mountedByUs[d] = false;
}

bool Started(const u8 d) {
    if (started[d]) return true;

    const u64 now = gettime();
    if (startupTried[d] &&
        ticks_to_millisecs(diff_ticks(startupAt[d], now)) < kStartupRetryMs) {
        return false;
    }
    startupTried[d] = true;
    startupAt[d]    = now;

    if (!kIface[d]->startup()) return false;
    started[d] = true;
    return true;
}

// Removal is noticed here and nowhere else, which is why it has side effects: a
// medium that has gone takes its mount with it, clears the broken verdict so a
// reformatted card works again, and drops started so the driver comes back up.
bool Inserted(const u8 d) {
    if (d >= kDeviceCount) return false;
    if (!Started(d)) return false;
    if (kIface[d]->isInserted()) return true;

    Release(d);
    deviceBroken[d] = false;
    started[d]      = false;
    freeValid       = false;
    return false;
}

bool Mounted(const u8 d) {
    struct stat st;
    return stat(kVolumeRoot[d], &st) == 0;
}

// A stat of the volume root rather than a flag of our own, because the
// application may have mounted it first: a game that called fatInitDefault()
// already owns "sd:", and unmounting it when a save finished would pull the
// volume out from under the game. Release only ever drops one this file made.
bool Mount(const u8 d) {
    if (Mounted(d)) return true;
    if (fatMountSimple(kVolume[d], kIface[d])) {
        mountedByUs[d] = true;
        return true;
    }
    deviceBroken[d] = true;
    return false;
}

u8 ParseDevice(const char* const path) {
    if (!path) return kNoDevice;
    for (u8 d = 0; d < kDeviceCount; ++d) {
        if (std::strcmp(path, kDevicePath[d]) == 0) return d;
    }
    return kNoDevice;
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

// One function both ways, so the length check cannot disagree between them: a
// prefix longer than the file is save::full, the same answer a full card gives.
save::eError Transfer(const u8 d, const void* const src, void* const tar, const std::size_t size) {
    if (!Mount(d)) return Fail(save::no_device);

    const int fd = open(kDevicePath[d], src ? O_WRONLY : O_RDONLY);
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

    // fsync is part of the &&, not a courtesy call: close() flushes the file,
    // fsync also pushes libfat's cache through to the medium, and a save is the
    // one moment a player may switch the console off.
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
    return Won();
}

// With no path it searches the remembered device, then 0, then 1. A remembered
// location that no longer holds the file is forgotten and the search continues;
// only missing is worth retrying elsewhere, a real failure is reported.
save::eError SaveOrLoad(const void* const src, void* const tar,
                        const std::size_t size, const char* const path) {
    // size == 0 is too_large because that is what Begin answers for it.
    if (size == 0)   return Fail(save::too_large);
    if (!src && !tar) return Fail(save::failed);

    if (path) {
        const u8 d = ParseDevice(path);
        if (d == kNoDevice) return Fail(save::bad_path);
        if (!Inserted(d) || deviceBroken[d]) return Fail(save::no_device);

        const save::eError err = Transfer(d, src, tar, size);
        if (err == save::found) {
            haveLocation = true;
            locationDev  = d;
            if (src) freeValid = false;
        }
        return err;
    }

    if (haveLocation && Inserted(locationDev) && !deviceBroken[locationDev]) {
        const save::eError err = Transfer(locationDev, src, tar, size);
        if (err == save::found) {
            if (src) freeValid = false;
            return err;
        }
        if (err != save::missing) return err;
        haveLocation = false;
    }

    save::eError best = save::no_device;
    bool         seen = false;
    for (u8 d = 0; d < kDeviceCount; ++d) {
        if (!Inserted(d) || deviceBroken[d]) continue;

        const save::eError err = Transfer(d, src, tar, size);
        if (err == save::found) {
            haveLocation = true;
            locationDev  = d;
            if (src) freeValid = false;
            return err;
        }
        if (!seen || Rank(err) > Rank(best)) {
            best = err;
            seen = true;
        }
    }
    return Fail(best);
}

// O_EXCL so "does it exist" and "create it" are one operation. A half-written
// save is unlinked: left behind, the next locate would offer the player a
// corrupt game rather than a new one.
save::eError Create(const void* const src, const std::size_t size, const char* const path) {
    if (size == 0) return Fail(save::too_large);
    if (!src)      return Fail(save::failed);

    u8 d = kDefaultDevice;
    if (path) {
        d = ParseDevice(path);
        if (d == kNoDevice) return Fail(save::bad_path);
    }
    if (!Inserted(d) || deviceBroken[d]) return Fail(save::no_device);
    if (!Mount(d))                       return Fail(save::no_device);

    if (mkdir(kDeviceDir[d], 0777) != 0 && errno != EEXIST) {
        const int e = errno;
        return Fail(MapErrno(e));
    }

    const int fd = open(kDevicePath[d], O_WRONLY | O_CREAT | O_EXCL, 0666);
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
        unlink(kDevicePath[d]);
        return Fail(MapErrno(ce));
    }
    if (!ok) {
        unlink(kDevicePath[d]);
        return Fail(MapErrno(e));
    }

    haveLocation = true;
    locationDev  = d;
    freeValid    = false;
    return Won();
}

} // namespace

namespace save {

Response Save(const void* src, const std::size_t size) {
    return { .done = true,
             .err  = SaveOrLoad(src, nullptr, size, nullptr),
             .path = haveLocation ? kDevicePath[locationDev] : nullptr };
}

Response Load(void* tar, const std::size_t size) {
    return { .done = true,
             .err  = SaveOrLoad(nullptr, tar, size, nullptr),
             .path = haveLocation ? kDevicePath[locationDev] : nullptr };
}

Response Load(void* tar, const std::size_t size, const Response* response) {
    return { .done = true,
             .err  = SaveOrLoad(nullptr, tar, size, PathOf(response)),
             .path = haveLocation ? kDevicePath[locationDev] : nullptr };
}

Response CreateSave(const void* src, const std::size_t size, const Response* response) {
    return { .done = true,
             .err  = Create(src, size, PathOf(response)),
             .path = haveLocation ? kDevicePath[locationDev] : nullptr };
}

Response GetSaveLocation() {
    eError best = no_device;
    bool   seen = false;

    for (u8 d = 0; d < kDeviceCount; ++d) {
        if (!Inserted(d) || deviceBroken[d]) continue;
        if (!Mount(d)) {
            if (!seen || Rank(no_device) > Rank(best)) {
                best = no_device;
                seen = true;
            }
            continue;
        }

        int e = 0;
        if (LengthOf(kDevicePath[d], e) >= 0) {
            haveLocation = true;
            locationDev  = d;
            Won();
            return { .done = true, .err = found, .path = kDevicePath[d] };
        }

        const eError err = MapErrno(e);
        if (!seen || Rank(err) > Rank(best)) {
            best = err;
            seen = true;
        }
    }

    haveLocation = false;
    Fail(best);
    return { .done = true, .err = best, .path = nullptr };
}

// Every device, not just the remembered one: a player who has copied a save
// between SD and USB can have one on each, and a delete that stopped at
// locationDev would leave the other for the next locate to find. found if any of
// them held one, so deleting twice answers the same both times.
Response Delete() {
    eError best = no_device;
    bool   seen = false;
    bool   gone = false;

    for (u8 d = 0; d < kDeviceCount; ++d) {
        if (!Inserted(d) || deviceBroken[d]) continue;
        if (!Mount(d)) {
            if (!seen || Rank(no_device) > Rank(best)) {
                best = no_device;
                seen = true;
            }
            continue;
        }

        errno = 0;
        if (unlink(kDevicePath[d]) == 0) {
            gone = true;
            continue;
        }

        const eError err = MapErrno(errno);
        if (!seen || Rank(err) > Rank(best)) {
            best = err;
            seen = true;
        }
    }

    haveLocation = false;
    freeValid    = false;

    if (gone) {
        Won();
        return { .done = true, .err = found, .path = nullptr };
    }

    Fail(best);
    return { .done = true, .err = best, .path = nullptr };
}

Response GetMetaData(void*) {
    return { .done = true, .err = Fail(no_device), .path = nullptr };
}

Response SetMetaData(const void*) {
    return { .done = true, .err = Fail(no_device), .path = nullptr };
}

u8 AccessibleDevices() {
    u8 mask = 0;
    for (u8 d = 0; d < kDeviceCount; ++d) {
        if (Inserted(d) && !deviceBroken[d]) mask = static_cast<u8>(mask | (1u << d));
    }
    return mask;
}

Response GetDeviceStatus(const u8 index) {
    if (index >= kDeviceCount)                     return { .done = true, .err = no_device, .path = nullptr };
    if (!Inserted(index) || deviceBroken[index])   return { .done = true, .err = no_device, .path = nullptr };
    return { .done = true, .err = found, .path = kDevicePath[index] };
}

// Not a sweep: statvfs needs a mount and libfat may walk the FAT to count free
// clusters, but there is no ticked form of either, so a partial answer costs what
// a whole one does. The first call after a mount is the expensive one; libfat
// caches the count afterwards.
u8 FreeDevices(const std::size_t size) {
    if (size == 0) return 0;

    const u8 access = AccessibleDevices();
    if (freeValid && freeSize == size && freeAccess == access) return freeMask;

    const std::size_t want = RoundUp(size);
    u8 mask = 0;

    for (u8 d = 0; d < kDeviceCount; ++d) {
        if (!(access & (1u << d))) continue;
        if (!Mount(d)) continue;

        struct statvfs vfs;
        if (statvfs(kVolumeRoot[d], &vfs) != 0) continue;

        // f_frsize is what free blocks are counted in; a zero would make the
        // product read as "full", so f_bsize stands in.
        const u64 unit  = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
        const u64 avail = static_cast<u64>(vfs.f_bavail) * unit;

        int         e       = 0;
        const off_t already = LengthOf(kDevicePath[d], e);
        const u64   held    = already > 0 ? static_cast<u64>(already) : 0;
        const u64   need    = held >= want ? 0 : want - held;

        if (avail >= need) mask = static_cast<u8>(mask | (1u << d));
    }

    freeMask   = mask;
    freeSize   = size;
    freeAccess = access;
    freeValid  = true;
    return mask;
}

} // namespace save

#endif // TARGET_GC
