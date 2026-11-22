/**
 * @file vrc1.hpp
 * @brief VRC1: three switchable 8 KiB PRG windows, two 4 KiB CHR windows, and
 *        the ::FIXED keyword for code that must survive a bankswitch.
 *
 * A bank number is 5 bits: 4 low bits in that window's own select register,
 * plus one high bit in a register the two CHR windows SHARE ($9000) -- so
 * setting one window's high bit is a read-modify-write against the other's.
 */
#pragma once

#include <intsh>
#include <utility>

#include <pnes/technology.hpp>
#ifndef TARGET_NES
#include <pnes/video.hpp>
#endif

using namespace br0::intsh;


#define FIXED CREATE_SEGMENT_KEYWORD(".prg_rom_fixed")

class VRC1 {
public:
    VRC1() = delete;
    VRC1(const VRC1 &) = delete;
    VRC1 &operator=(const VRC1 &) = delete;

    static tech::wo_register<0x8000> window1Control; ///< PRG-select, window 1 ($8000-$9FFF). See above.
    static tech::wo_register<0xa000> window2Control; ///< PRG-select, window 2 ($A000-$BFFF). See ::window1Control.
    static tech::wo_register<0xc000> window3Control; ///< PRG-select, window 3 ($C000-$DFFF). See ::window1Control.

    /// @p reg is by reference: by value the hardware poke would still happen --
    /// the address is a template constant -- while the shadow update would be
    /// lost. Not used by _reset, which runs before .bss is zeroed.
    template <u16 addr>
    static constexpr void SwitchBank(tech::wo_register<addr> &reg, u8 ctx) {
#ifdef TARGET_NES
        reg = ctx;
#else
        (void)reg;
        (void)ctx;
#endif
    }

    /// @p window is required with no default: which window holds the target
    /// changes which physical bank ends up mapped, so getting it wrong jumps into
    /// another bank's content rather than failing to compile. Off-NES this
    /// collapses to a plain `return fn();`.
    template <typename TReturn, typename TFunc>
    static FIXED TReturn Long(TFunc fn, u8 window);


    /// chrHighBits is shared between both pattern tables, which is what the
    /// wo_register shadow exists for: SwitchCHRBank reads it back to flip only the
    /// calling window's bit.
    static tech::wo_register<0x9000> chrHighBits; ///< Shared high bank bit for both pattern tables. See above.
    static tech::wo_register<0xe000> chr0Control; ///< CHR-select, pattern table 0 ($0000-$0FFF). See ::chrHighBits.
    static tech::wo_register<0xf000> chr1Control; ///< CHR-select, pattern table 1 ($1000-$1FFF). See ::chrHighBits.

#ifndef TARGET_NES
    static u8 chrBanks[2];

    /// Library-internal: the only caller is this module's strong
    /// ppu::ResolveTile override. Public only because that caller is a free
    /// function. A game selects banks through SwitchCHRBank.
    static u32 GetTileLMA(u16 tileVMA);
#endif

    template <u16 addr>
    static constexpr void SwitchCHRBank(tech::wo_register<addr> &reg, u8 bank) {
        static_assert(addr == 0xe000 || addr == 0xf000,
                      "SwitchCHRBank only valid for chr0Control ($E000) or chr1Control ($F000)");
#ifdef TARGET_NES
        constexpr u8 bit = (addr == 0xe000) ? 0 : 1;
        reg = bank & 0x0F;
        chrHighBits = (chrHighBits.get() & ~(u8(1) << bit)) | (((bank >> 4) & 1) << bit);
#else
        (void)reg;
        constexpr u8 idx = (addr == 0xe000) ? 0 : 1;
        chrBanks[idx] = bank & 0x1F;
        ++ppu::chrGeneration;
#endif
    }

    static constexpr u32 kChrBankSize = 0x1000;

    static constexpr u32 kMaxChrBank = 0x1F;

    /// Pairs with blobman's generated `__lma`, so a character set is banked in by
    /// offset rather than by a hand-counted number. A template parameter so an
    /// offset off a 4 KiB boundary is a compile error rather than wrong tiles.
    template <u32 Lma>
    static constexpr u8 BankOf() {
        static_assert(Lma % kChrBankSize == 0,
                      "BankOf: CHR offset is not on a 4 KiB bank boundary");
        static_assert(Lma / kChrBankSize <= kMaxChrBank,
                      "BankOf: CHR offset is past the 128 KiB VRC1's 5-bit bank number can reach");
        return static_cast<u8>(Lma / kChrBankSize);
    }

    static constexpr u32 kChrSpaceSize = 0x2000;


    /// LOADADDR-derived, not the CPU-visible VMA: on a switchable window every
    /// bank that aliases onto it shares one VMA.
    struct section_t {
        u32 rom_address;
        u32 size;
    };

    /// Primary template undefined: an un-tagged Tag is a compile error. Prefer a
    /// constexpr section() matched against a linker rule, ideally with an ASSERT
    /// catching drift -- then CallInSection's resolution folds away. A runtime
    /// one costs ~120 bytes per call site, and must compute fresh in the body:
    /// unprioritized dynamic initializers run after every constructor(N) here.
    template <typename Tag> struct bank_layout;

    template <auto Fn> struct bank_of;

    /// Five bytes against MMC3's four: VRC1's windows are contiguous, so a domain
    /// may span two of them and `windows` records that -- the one part of
    /// CallInSection that does not fold, `size` being link-computed.
    template <typename Ret, typename... Args>
    struct callable_t {
        Ret (*fn)(Args...);  ///< the target, as an ordinary 16-bit pointer
        u8 window;           ///< 0 = window1Control ($8000), 1 = window2Control, 2 = window3Control
        u8 bank;             ///< value written to the selected window's register
        u8 windows;          ///< how many adjacent windows the domain spans (1, or 2 for an oversized domain)
    };

    template <typename T> struct function_traits;

    template <auto Fn, typename... Args>
    static FIXED auto Call(Args &&...args) -> typename function_traits<decltype(Fn)>::return_type;

    template <auto Fn, typename Block>
    static FIXED auto CallBlock(Block &&block) -> decltype(block());

    /// Call it wherever the function is still nameable, then store the result
    /// freely -- a table of these dispatches on a runtime index with each entry's
    /// own bank. constexpr, which needs the tag's section() to be constexpr too.
    template <auto Fn>
    static constexpr auto GetCallable() ->
        typename function_traits<decltype(Fn)>::callable_type;

    template <typename Ret, typename... Args, typename... Actual>
    static FIXED Ret Call(const callable_t<Ret, Args...> &c, Actual &&...args);

private:
    struct Detail {
        /// The three windows run back to back from kWindowBase, so a LOADADDR
        /// address must be rebased against it before dividing.
        static constexpr u16 kWindowBase = 0x8000;
        static constexpr u16 kWindowSize = 0x2000;

        /// The [[gnu::noinline]] is load-bearing: a section attribute places only
        /// an out-of-line symbol, and inlining this into a caller that lives in
        /// the window being switched leaves the restore half embedded in the bank
        /// just switched away. A reproduced crash, not a theory.
        template <typename TReturn, u16 addr, typename TFunc>
        [[gnu::noinline]] static FIXED TReturn CallInWindow(tech::wo_register<addr> &reg, u8 bank, TFunc fn) {
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

        /// The three windows are physically contiguous, so adjacent ones drive one
        /// flat region -- and only two adjacent pairs exist, hence no general loop.
        template <typename TReturn, typename TFunc>
        [[gnu::noinline]] static FIXED TReturn CallInWindows2(u8 windowIndexBase, u8 bankBase, TFunc fn) {
            switch (windowIndexBase) {
                case 0:
                    return CallInWindow<TReturn>(window1Control, bankBase, [&]() -> TReturn {
                        return CallInWindow<TReturn>(window2Control, static_cast<u8>(bankBase + 1), fn);
                    });
                case 1:
                default:
                    return CallInWindow<TReturn>(window2Control, bankBase, [&]() -> TReturn {
                        return CallInWindow<TReturn>(window3Control, static_cast<u8>(bankBase + 1), fn);
                    });
            }
        }

        /// The window comes from rom_address's low 16 bits, which is what an
        /// absolute JSR truncates to; the bank is the window index unless
        /// bank_layout encoded one in the high bits. The window count is `size`
        /// ceil-divided, and size is genuinely link-computed for an oversized
        /// domain, so that division costs real bytes.
        template <typename TReturn, typename TFunc>
        [[gnu::noinline]] static FIXED TReturn CallInSection(const section_t &section, TFunc fn) {
            const u16 vma = static_cast<u16>(section.rom_address);
            const u8 windowIndex = static_cast<u8>((vma - kWindowBase) / kWindowSize);
            const u8 explicitBank = static_cast<u8>(section.rom_address >> 16);
            const u8 bank = explicitBank != 0 ? explicitBank : windowIndex;
            const u8 windowCount = static_cast<u8>((section.size + kWindowSize - 1) / kWindowSize);
            if (windowCount > 1) {
                return CallInWindows2<TReturn>(windowIndex, bank, fn);
            }
            switch (windowIndex) {
                case 1:  return CallInWindow<TReturn>(window2Control, bank, fn);
                case 2:  return CallInWindow<TReturn>(window3Control, bank, fn);
                default: return CallInWindow<TReturn>(window1Control, bank, fn);
            }
        }
    };
};

template <typename R, typename... A>
struct VRC1::function_traits<R (*)(A...)> {
    using return_type = R;
    using callable_type = VRC1::callable_t<R, A...>;
};

template <typename TReturn, typename TFunc>
FIXED TReturn VRC1::Long(TFunc fn, const u8 window) {
#ifdef TARGET_NES
    switch (window) {
        case 1:  return Detail::CallInWindow<TReturn>(window2Control, 1, fn);
        case 2:  return Detail::CallInWindow<TReturn>(window3Control, 2, fn);
        default: return Detail::CallInWindow<TReturn>(window1Control, 0, fn);
    }
#else
    return fn();
#endif
}

template <auto Fn, typename... Args>
FIXED auto VRC1::Call(Args &&...args) -> typename function_traits<decltype(Fn)>::return_type {
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

template <auto Fn, typename Block>
FIXED auto VRC1::CallBlock(Block &&block) -> decltype(block()) {
#ifdef TARGET_NES
    using L = typename bank_of<Fn>::layout;
    return Detail::CallInSection<decltype(block())>(
        L::section(), std::forward<Block>(block));
#else
    return block();
#endif
}

template <auto Fn>
constexpr auto VRC1::GetCallable() ->
    typename function_traits<decltype(Fn)>::callable_type {
#ifdef TARGET_NES
    using L = typename bank_of<Fn>::layout;
    const section_t s = L::section();
    const u16 vma = static_cast<u16>(s.rom_address);
    const u8 window = static_cast<u8>((vma - Detail::kWindowBase) / Detail::kWindowSize);
    const u8 explicitBank = static_cast<u8>(s.rom_address >> 16);
    const u8 windows = static_cast<u8>((s.size + Detail::kWindowSize - 1) / Detail::kWindowSize);
    return { Fn, window, static_cast<u8>(explicitBank != 0 ? explicitBank : window), windows };
#else
    return { Fn, 0, 0, 1 };
#endif
}

template <typename Ret, typename... Args, typename... Actual>
FIXED Ret VRC1::Call(const callable_t<Ret, Args...> &c, Actual &&...args) {
#ifdef TARGET_NES
    auto invoke = [&]() -> Ret { return c.fn(static_cast<Actual &&>(args)...); };
    if (c.windows > 1) return Detail::CallInWindows2<Ret>(c.window, c.bank, invoke);
    switch (c.window) {
        case 1:  return Detail::CallInWindow<Ret>(window2Control, c.bank, invoke);
        case 2:  return Detail::CallInWindow<Ret>(window3Control, c.bank, invoke);
        default: return Detail::CallInWindow<Ret>(window1Control, c.bank, invoke);
    }
#else
    return c.fn(static_cast<Actual &&>(args)...);
#endif
}

/// section_name is whatever literal the caller's linker script uses; tag is a
/// separate identifier-safe token, because real section names are not valid
/// identifiers. The _IMPL indirection forces both to final text before ## sees
/// them. Prefixed, so both mapper headers can be included by one TU.
#define VRC1_BANKED(section_name, tag, ret, name, ...) \
    VRC1_BANKED_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define VRC1_BANKED_IMPL(section_name, tag, ret, name, ...)                      \
    CREATE_SEGMENT_KEYWORD(section_name) ret name(__VA_ARGS__);                 \
    template <> struct VRC1::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = VRC1::bank_layout<tag##_tag>;                            \
    }

/// Registration only, for a symbol this codegen never emits -- hand-written
/// assembly, or an engine assembled separately. Placement belongs to whatever
/// assembled it and must be kept in sync by hand.
#define VRC1_BANKED_EXTERN(section_name, tag, ret, name, ...) \
    VRC1_BANKED_EXTERN_IMPL(section_name, tag, ret, name, __VA_ARGS__)
#define VRC1_BANKED_EXTERN_IMPL(section_name, tag, ret, name, ...)               \
    extern "C" ret name(__VA_ARGS__);                                           \
    template <> struct VRC1::bank_of<static_cast<ret (*)(__VA_ARGS__)>(&name)> { \
        using layout = VRC1::bank_layout<tag##_tag>;                            \
    }
