/**
 * @file mmc3.hpp
 * @brief MMC3 (mapper 4): PRG/CHR bank switching, scanline IRQ, ::FIXED.
 *
 * As used here (PRG mode 0, CHR mode 1 -- the only combination supported):
 *
 *   $8000-$9FFF  8 KiB PRG, switchable (R6)      $0000-$0FFF  4x 1 KiB (R2-R5)
 *   $A000-$BFFF  8 KiB PRG, switchable (R7)      $1000-$1FFF  2x 2 KiB (R0/R1)
 *   $C000-$FFFF  8 KiB PRG each, hardware-fixed  $6000-$7FFF  PRG-RAM
 *
 * CHR mode 1 ($8000 D7) puts the 1 KiB windows at the front, which is the useful
 * way round: a background table is worth rebanking a kilobyte at a time while a
 * sprite set is swapped whole. D7 shares its byte with the register index, so
 * every $8000 write re-sends it and a bare index must never be written there.
 *
 * Only R6 and R7 can be pointed anywhere, so there is no window3Control. Every
 * register except $A000/$A001 goes through $8000 select / $8001 data.
 */
#pragma once

#include <intsh>
#include <utility>

#include <pnes/technology.hpp>
#include <pnes/types.hpp>

using namespace br0::intsh;

// Needs a fallback here rather than only in mmc3.cpp: every TU that sees the
// class layout below must agree on which members exist. Only 1 is implemented.
#ifndef ALTERNATIVE_NAMETABLE
#define ALTERNATIVE_NAMETABLE 0
#endif

static_assert(ALTERNATIVE_NAMETABLE == 0 || ALTERNATIVE_NAMETABLE == 1,
    "mmc3: ALTERNATIVE_NAMETABLE must be 0 (ordinary switchable 2-screen "
    "mirroring) or 1 (four-screen: +2KiB cartridge VRAM) -- other nonzero "
    "values are reserved for a genuinely different alternative-wiring "
    "scheme, not folded into 1's meaning, and none is implemented yet.");

#define FIXED CREATE_SEGMENT_KEYWORD(".prg_rom_fixed")

class mmc3 {
public:
    /// $8000's CHR A12-inversion bit, as the layout it produces rather than as a
    /// number -- the enumerators ARE the bit, so geometry and select byte cannot
    /// disagree. Not a per-chip constant: D7 rides in every bank-select write.
    enum class chrMode : u8 {
        coarseFirst = 0x00,

        fineFirst = 0x80,
    };

    static constexpr chrMode kChrMode = chrMode::fineFirst;

private:
#ifdef TARGET_NES
    /// Exists so select_latch_scope can put the latch back. $8000/$8001 are a
    /// select/data pair, so a bank write is two stores with live state between
    /// them: an interrupt that banks anything in that gap re-latches $8000 and the
    /// interrupted $8001 store lands in the wrong register. `sei` is not a fix --
    /// NMI is unmaskable, and the per-frame NMI does the most banking.
    static atomic u8 selectLatch;
#endif

#ifndef TARGET_NES
    /// Emu-side shadow of D7, driven by real writes rather than a build setting,
    /// so the inversion mechanic is live off-NES too. Boots as D7 = 0, the chip's
    /// own power-on arrangement, which says nothing about what a game wants.
    static bool shape;

    static u8 banks[6];

    static void NotifyCHRWrite(u8 index, u8 bank);

    static void NotifyModeWrite(chrMode mode);
#endif

public:
#ifndef TARGET_NES
    static u32 GetTileLMA(u16 tileVMA);

#endif

    mmc3() = delete;
    mmc3(const mmc3 &) = delete;
    mmc3 &operator=(const mmc3 &) = delete;

    template <u8 Index>
    class Register {
        atomic u8 shadow_;
    public:
        static_assert(Index <= 7, "MMC3 only has registers R0-R7.");

        Register() = default;                                    ///< trivial: instance lives in .bss
        Register(const Register &o) : shadow_(o.shadow_) {}     ///< snapshot, no poke

        u8   get() const     { return shadow_; }                          ///< read  = shadow
        void set(const u8 v, const chrMode mode = kChrMode) { shadow_ = v; poke_only(v, mode); } ///< write = shadow + hardware
        /// On NES the real $8000/$8001 pair. Off-NES there is no hardware, so this
        /// records the mode for every write -- a real $8000 write always carries
        /// D7 -- plus the bank for CHR registers, which GetTileLMA resolves
        /// through. R6/R7 are not tracked: there is no banked PRG off-NES.
        void poke_only(const u8 v, const chrMode mode = kChrMode) const {
#ifdef TARGET_NES
            const u8 sel = Detail::SelectByte(mode, Index);
            // Updated BEFORE the $8000 store, which is what makes all three
            // interrupt windows safe: an interrupt before it restores the
            // previous select byte this pair is about to overwrite anyway, and one
            // after -- including in the gap that bites -- puts `sel` back.
            selectLatch = sel;
            tech::poke(0x8000, sel);
            tech::poke(0x8001, v);
#else
            NotifyModeWrite(mode);
            if constexpr (Index <= 5) NotifyCHRWrite(Index, v);
#endif
        }

        operator u8() const { return shadow_; }
        Register &operator=(const u8 v)          { set(v);         return *this; }
        Register &operator=(const Register &o)   { set(o.shadow_); return *this; } ///< restore path
    };

    /**
     * @brief Leaves the $8000 bank-select latch as it was found. DECLARE ONE AT
     *        THE TOP OF EVERY NMI AND IRQ BODY ON THIS MAPPER.
     *
     * Without it any handler that banks anything can corrupt a bank write it
     * interrupted (see ::selectLatch). The repair lives on the handler's side
     * because that is the only side that can act. Nests correctly; off-NES it is
     * an empty type, so a handler can declare one unconditionally.
     */
    class select_latch_scope {
#ifdef TARGET_NES
        u8 saved_;
#endif
    public:
#ifdef TARGET_NES
        select_latch_scope() : saved_(selectLatch) {}
        ~select_latch_scope() {
            // Hardware first, then the shadow: the shadow must never claim a
            // latch that is not in the port yet.
            tech::poke(0x8000, saved_);
            selectLatch = saved_;
        }
#else
        select_latch_scope() = default;
#endif
        select_latch_scope(const select_latch_scope &) = delete;
        select_latch_scope &operator=(const select_latch_scope &) = delete;
    };

    static Register<6> window1Control; ///< PRG-select R6, window 1 ($8000-$9FFF). See above.
    static Register<7> window2Control; ///< PRG-select R7, window 2 ($A000-$BFFF). See ::window1Control.

    /// R0/R1 select 2 KiB windows but still count banks in 1 KiB units, and
    /// hardware ignores the low bit. Nothing here masks it away, so a consumer
    /// resolving a tile address must do that itself.
    static Register<0> chr0Control; ///< CHR-select R0, 2 KiB @ PPU $1000.
    static Register<1> chr1Control; ///< CHR-select R1, 2 KiB @ PPU $1800.
    static Register<2> chr2Control; ///< CHR-select R2, 1 KiB @ PPU $0000.
    static Register<3> chr3Control; ///< CHR-select R3, 1 KiB @ PPU $0400.
    static Register<4> chr4Control; ///< CHR-select R4, 1 KiB @ PPU $0800.
    static Register<5> chr5Control; ///< CHR-select R5, 1 KiB @ PPU $0C00.

    /// Bit 0: 0 vertical, 1 horizontal -- the opposite polarity from the iNES
    /// header's own mirroring bit, which is a well-known MMC3 gotcha. Off-NES it
    /// writes the emu PPU's ::mirroring global instead.
    static void SetMirroring(bool horizontal);

    static tech::wo_register<0xa001> prgRamProtect;

    static tech::wo_register<0xc000> irqLatch;

    /// @p reg is by reference: by value the hardware poke would still happen --
    /// the address is a template constant -- while the shadow update would be
    /// lost. Not used by _reset, which runs before .bss is zeroed.
    template <chrMode Mode = kChrMode, u8 Index>
    static constexpr void SwitchBank(Register<Index> &reg, u8 ctx) {
        reg.set(ctx, Mode);
    }

    template <chrMode Mode = kChrMode, u8 Index>
    static constexpr void SwitchCHRBank(Register<Index> &reg, u8 bank) {
        static_assert(Index <= 5, "SwitchCHRBank only valid for chr0Control-chr5Control (R0-R5).");
        reg.set(bank, Mode);
    }

    /// Prefer this wherever the bank is constant: a 2 KiB window ignores its bank
    /// number's low bit, so an odd bank in R0/R1 quietly maps the aligned pair
    /// below. A static_assert here, undetectable in the runtime overload.
    template <u8 Bank, chrMode Mode = kChrMode, u8 Index>
    static constexpr void SwitchCHRBank(Register<Index> &reg) {
        static_assert(Index <= 5, "SwitchCHRBank only valid for chr0Control-chr5Control (R0-R5).");
        static_assert(Index > 1 || Bank % 2 == 0,
                      "SwitchCHRBank: R0/R1 are MMC3's 2 KiB windows, which round an odd 1 KiB "
                      "bank down -- pack the characters 2 KiB-aligned in CHR ROM");
        reg.set(Bank, Mode);
    }

    static constexpr u32 kChrBankSize = 0x400;


    static constexpr u32 kMaxChrBank = 0xFF;

    /// Pairs with blobman's generated `__lma`, so a game banks a character set in
    /// by offset rather than by a hand-counted number. A template parameter so an
    /// offset off a bank boundary is a compile error rather than wrong tiles.
    template <u32 Lma>
    static constexpr u8 BankOf() {
        static_assert(Lma % kChrBankSize == 0,
                      "BankOf: CHR offset is not on a 1 KiB bank boundary");
        static_assert(Lma / kChrBankSize <= kMaxChrBank,
                      "BankOf: CHR offset is past the 256 KiB a CHR register can reach");
        return static_cast<u8>(Lma / kChrBankSize);
    }

    static constexpr u32 kChrSpaceSize = 0x2000;

    /// Sets the reload latch, forces a reload, enables generation. Does NOT
    /// acknowledge one still pending. @p position is off-NES only and is
    /// deliberately not derived from @p scanline, which is tuned against real
    /// A12-edge latency -- mixing the two lands the split on the wrong row.
    static void ScheduleScanlineIRQ(u8 scanline, vec2<u16> position);

    static void AcknowledgeScanlineIRQ();


    /**
     * @brief One bank/segment's location in the linked ROM image, as
     *        `((bank + 1) << 16) | vma` with vma $8000 (R6) or $A000 (R7).
     *
     * LOADADDR-side, not the CPU-visible VMA, since every bank aliasing onto a
     * switchable window shares one. The +1 bias -- VRC1 has none -- is forced
     * twice over: CallInSection reads a zero high half as "no bank encoded", so
     * bank 0 would be unrepresentable, and ld.lld rejects an unbiased bank 0
     * region at ORIGIN 0x8000 for overlapping ordinary .text. Free at runtime,
     * since a JSR operand carries 16 bits.
     */
    struct section_t {
        u32 rom_address;
        u32 size;
    };

    /// Primary template undefined: an un-tagged Tag is a compile error. There is
    /// no `always_mapped` escape hatch -- code already in reach is reached by an
    /// ordinary call and needs no tag at all.
    template <typename Tag> struct bank_layout;

    template <auto Fn> struct bank_of;

    /// A function pointer that carries its own bank: Call<Fn> needs Fn nameable
    /// as a template argument and `table[runtime_index]` never is, so GetCallable
    /// resolves it while Fn still is. Window and bank rather than a section_t,
    /// which keeps an entry at 4 bytes and the arithmetic off the call path.
    template <typename Ret, typename... Args>
    struct callable_t {
        Ret (*fn)(Args...);  ///< the target, as an ordinary 16-bit pointer
        u8 window;           ///< 0 = window1Control ($8000), 1 = window2Control ($A000)
        u8 bank;             ///< value written to R6/R7, already un-biased
    };

    template <typename T> struct function_traits;

    template <typename TReturn, typename TFunc>
    static FIXED TReturn Long(TFunc fn, u8 window = 0);

    template <auto Fn, typename... Args>
    static FIXED auto Call(Args &&...args) -> typename function_traits<decltype(Fn)>::return_type;

    /// Call it wherever the function is still nameable, then store the result
    /// freely -- a table of these dispatches on a runtime index with each entry's
    /// own bank. constexpr, which needs the tag's section() to be constexpr too.
    template <auto Fn>
    static constexpr auto GetCallable() ->
        typename function_traits<decltype(Fn)>::callable_type;

    template <typename Ret, typename... Args, typename... Actual>
    static FIXED Ret Call(const callable_t<Ret, Args...> &c, Actual &&...args);

    template <typename Tag, typename Block>
    static FIXED auto CallInBlock(Block &&block) -> decltype(block());

    /// For a module whose code and data live in different banks and must be
    /// mapped together. Two is the hard maximum and the tags must target
    /// different windows, or the second switch displaces the first. With both
    /// switched, nothing of the ordinary program at $8000-$BFFF is mapped: the
    /// block may call the two banks, ::FIXED code and RAM, nothing else.
    template <typename CodeTag, typename DataTag, typename Block>
    static FIXED auto CallPairedBlock(Block &&block) -> decltype(block());

private:
    struct Detail {
        /// $8000 packs the register index with these two mode bits in one byte,
        /// so selecting an index without them clears whichever was left out.
        /// Every $8000 write goes through here.
        static constexpr u8 kPrgModeBit = 0;

        static constexpr u8 SelectByte(const chrMode mode, const u8 index) {
            return kPrgModeBit | static_cast<u8>(mode) | index;
        }

        static constexpr u16 kWindowBase = 0x8000;
        static constexpr u16 kWindowSize = 0x2000;

        /// The [[gnu::noinline]] is load-bearing: `fixed` only places an
        /// out-of-line symbol, and inlining this into a caller that lives in the
        /// window being switched away from is a reproduced crash.
        template <typename TReturn, u8 Index, typename TFunc>
        [[gnu::noinline]] static FIXED TReturn CallInWindow(Register<Index> &reg, u8 bank, TFunc fn) {
            // Deliberately not ::SHADOW, and measured: a shadow scope keeps more
            // live state across fn() than llvm-mos holds in registers, so the
            // thunk pays a soft-stack frame -- most of its cost, against a
            // 35-cycle bank switch.
            const u8 saved = reg.get();
            SwitchBank(reg, bank);
            if constexpr (__is_same(TReturn, void)) {
                fn();
                reg.set(saved);
            } else {
                TReturn result = fn();
                reg.set(saved);
                return result;
            }
        }

        /// Only windowIndex 0 or 1 are valid, since no register reaches
        /// $C000-$DFFF. NOT range-checked: an address at $C000 or above falls
        /// through to window1Control and silently mis-banks.
        template <typename TReturn, typename TFunc>
        [[gnu::noinline]] static FIXED TReturn CallInSection(const section_t &section, TFunc fn) {
            const u16 vma = static_cast<u16>(section.rom_address);
            const u8 windowIndex = static_cast<u8>((vma - kWindowBase) / kWindowSize);
            // High half is bank + 1 (see section_t); zero still means "nothing
            // encoded" and falls back to the window index.
            const u8 encodedBank = static_cast<u8>(section.rom_address >> 16);
            const u8 bank = encodedBank != 0 ? static_cast<u8>(encodedBank - 1) : windowIndex;
            switch (windowIndex) {
                case 1:  return CallInWindow<TReturn>(window2Control, bank, fn);
                default: return CallInWindow<TReturn>(window1Control, bank, fn);
            }
        }
    };
};

template <typename R, typename... A>
struct mmc3::function_traits<R (*)(A...)> {
    using return_type = R;
    using callable_type = mmc3::callable_t<R, A...>;
};

template <typename TReturn, typename TFunc>
FIXED TReturn mmc3::Long(TFunc fn, const u8 window) {
#ifdef TARGET_NES
    switch (window) {
        case 1:  return Detail::CallInWindow<TReturn>(window2Control, 1, fn);
        default: return Detail::CallInWindow<TReturn>(window1Control, 0, fn);
    }
#else
    return fn();
#endif
}

template <auto Fn, typename... Args>
FIXED auto mmc3::Call(Args &&...args) -> typename function_traits<decltype(Fn)>::return_type {
    using TReturn = typename function_traits<decltype(Fn)>::return_type;
#ifdef TARGET_NES
    using L = typename bank_of<Fn>::layout;
    return Detail::CallInSection<TReturn>(
        L::section(),
        [&]() -> TReturn { return Fn(std::forward<Args>(args)...); });
#else
    return Fn(std::forward<Args>(args)...);
#endif
}

template <auto Fn>
constexpr auto mmc3::GetCallable() ->
    typename function_traits<decltype(Fn)>::callable_type {
#ifdef TARGET_NES
    using L = typename bank_of<Fn>::layout;
    const section_t s = L::section();
    const u16 vma = static_cast<u16>(s.rom_address);
    const u8 window = static_cast<u8>((vma - Detail::kWindowBase) / Detail::kWindowSize);
    const u8 encodedBank = static_cast<u8>(s.rom_address >> 16);
    return { Fn, window, static_cast<u8>(encodedBank != 0 ? encodedBank - 1 : window) };
#else
    return { Fn, 0, 0 };
#endif
}

template <typename Ret, typename... Args, typename... Actual>
FIXED Ret mmc3::Call(const callable_t<Ret, Args...> &c, Actual &&...args) {
#ifdef TARGET_NES
    auto invoke = [&]() -> Ret { return c.fn(static_cast<Actual &&>(args)...); };
    if (c.window == 1) return Detail::CallInWindow<Ret>(window2Control, c.bank, invoke);
    return Detail::CallInWindow<Ret>(window1Control, c.bank, invoke);
#else
    return c.fn(static_cast<Actual &&>(args)...);
#endif
}

template <typename Tag, typename Block>
FIXED auto mmc3::CallInBlock(Block &&block) -> decltype(block()) {
#ifdef TARGET_NES
    using L = bank_layout<Tag>;
    return Detail::CallInSection<decltype(block())>(L::section(), block);
#else
    return block();
#endif
}

template <typename CodeTag, typename DataTag, typename Block>
FIXED auto mmc3::CallPairedBlock(Block &&block) -> decltype(block()) {
#ifdef TARGET_NES
    // Only checkable when both section()s work in a constant expression; a
    // runtime one is exempt rather than rejected.
    if constexpr (requires {
            std::integral_constant<u32, bank_layout<CodeTag>::section().rom_address>{};
            std::integral_constant<u32, bank_layout<DataTag>::section().rom_address>{}; }) {
        static_assert(
            (static_cast<u16>(bank_layout<CodeTag>::section().rom_address) & 0xE000) !=
            (static_cast<u16>(bank_layout<DataTag>::section().rom_address) & 0xE000),
            "mmc3::CallPairedBlock: both tags map through the SAME window, so the "
            "second bank would immediately displace the first. Encode one at $8000 "
            "(R6) and the other at $A000 (R7).");
    }
    return CallInBlock<CodeTag>([&]() -> decltype(block()) {
        return CallInBlock<DataTag>(block);
    });
#else
    return block();
#endif
}

/// Declares a tagged out-of-line function and registers it with bank_of<>. The
/// _IMPL indirection forces section_name/tag to final text before # or ## sees
/// them. Prefixed, so both mapper headers can be included by one TU.
#define MMC3_BANKED(section_name, tag, ret, name, ...) \
    MMC3_BANKED_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define MMC3_BANKED_IMPL(section_name, tag, ret, name, ...)                      \
    CREATE_SEGMENT_KEYWORD(section_name) ret name(__VA_ARGS__);                 \
    template <> struct mmc3::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = mmc3::bank_layout<tag##_tag>;                            \
    }

/// The namespace-friendly counterpart to ::MMC3_BANKED, which can only produce
/// an unqualified name. Place the function where it is defined, then bind it
/// here by qualified name. Nothing checks that placement and binding agree:
/// binding to a tag whose layout names another bank executes garbage.
#define MMC3_BIND(fn, tag)                     \
    template <> struct mmc3::bank_of<&fn> {    \
        using layout = mmc3::bank_layout<tag##_tag>; \
    }

#define MMC3_BANKED_EXTERN(section_name, tag, ret, name, ...) \
    MMC3_BANKED_EXTERN_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define MMC3_BANKED_EXTERN_IMPL(section_name, tag, ret, name, ...)               \
    extern "C" ret name(__VA_ARGS__);                                           \
    template <> struct mmc3::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = mmc3::bank_layout<tag##_tag>;                            \
    }
