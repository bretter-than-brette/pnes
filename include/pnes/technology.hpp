/**
 * @file technology.hpp
 * @brief Low-level memory access, toolchain shims and assembly-backed helpers:
 *        peek/poke, ::atomic, ::AI / ::NI, ::MINSIZE, the ::SHADOW register
 *        scopes, compile-time charmaps, and the ::CAT / ::STR paste helpers.
 */
#pragma once

#ifdef TARGET_NES
constexpr bool is_nes = true;
#else
constexpr bool is_nes = false;
#endif

#include <intsh>
using namespace br0::intsh;
#include <type_traits>
#include <array>
#include <cstddef>

/// ::CAT_ pastes raw spellings, so both operands are routed through an outer
/// macro first to get them expanded. The result must be one valid token --
/// pasting across a `/` or `.` is undefined; use ::STRCAT for a path.
#define CAT_(a, b) a##b
#define CAT(a, b)  CAT_(a, b)

#define STR_(x) #x
#define STR(x)  STR_(x)

/// A computed #include must collapse to exactly one string-literal token, so
/// write the path bareword with macro pieces embedded. Not concatenation of
/// quoted pieces, which is undefined.
#define STRCAT(path) STR(path)

/// Not a speed hint -- the inliner already says yes to almost everything. ::AI is
/// for where out-of-lining would be a correctness bug: an interrupt handler's
/// budget is one vblank. Deliberately not `inline` as well, because ::AI also
/// tags definitions that must keep external linkage; a body in a header needs
/// `AI inline` spelt out, or it is an ODR violation. Does not compose with
/// noinline, which wins silently -- so never also ::MODULE_PLACEMENT.
#define AI __attribute__((always_inline))

/// For when placement rather than speed is the point: a banked function reached
/// through one call site in a trampoline is a prime LTO inlining target.
#define NI __attribute__((noinline))

/// 1 where screen geometry is resolved at runtime, 0 where it is a compile-time
/// constant. A generated UI node's position is constexpr on the latter and a
/// mutable `atomic vec2` a Make_* call fills on the former. Mirrors uitk's own
/// kVariadicTargets grouping.
#if defined(TARGET_NES) || defined(TARGET_GBA) || defined(TARGET_PSP) || defined(TARGET_PS2) || defined(TARGET_PS3)
  #define TARGET_VARIADIC_DISPLAY 0
#else
  #define TARGET_VARIADIC_DISPLAY 1
#endif


#ifdef TARGET_NES
/// A builder, not a keyword: the preprocessor cannot register a macro name from
/// inside another expansion, so this is only ever the right-hand side of one.
#define CREATE_SEGMENT_KEYWORD(name) __attribute__((section(name)))
#else
#define CREATE_SEGMENT_KEYWORD(name)
#endif

#if defined(TARGET_NES) && defined(NES_MAPPER_BANKSWITCHED)
/// The `noinline` is the whole point: a section governs only a function's
/// out-of-line copy, and LTO pastes the body into callers elsewhere, leaving the
/// section empty. So placement costs that module its inlining -- functions that
/// must stay inline take ::AI and no section. Nothing on NROM, which has nowhere
/// to move code to.
#define MODULE_PLACEMENT(name) CREATE_SEGMENT_KEYWORD(name) \
                               __attribute__((noinline))

#else
#define MODULE_PLACEMENT(name)
#endif


#ifdef __cplusplus
namespace tech {
inline AI
u8 peek(const u16 addr) {
    return *reinterpret_cast<volatile const u8 *>(addr);
}

inline AI
void poke(const u16 addr, const u8 data) {
    *reinterpret_cast<volatile u8 *>(addr) = data;
}

#ifdef TARGET_NES
/// Exactly `c + 15` cycles for any c in 15..270, whatever the caller does to get
/// @p c into place (NESdev's "Delay code": an sbc #5 loop plus a branch cascade
/// for the remainder). Assumes no branch crosses a page boundary, no interrupt
/// fires and no DMA is running.
inline AI
void SpinWait(const u8 c) {
    __asm__ volatile (
        "sec\n"
        "1:\n"
        "sbc #5\n"
        "bcs 1b\n"
        "adc #3\n"
        "bcc 2f\n"
        "lsr a\n"
        "beq 3f\n"
        "2:\n"
        "lsr a\n"
        "3:\n"
        "bcs 4f\n"
        "4:\n"
        :
        : "a" (c)
        : "p"
    );
}
#else
inline void SpinWait(const u8) {}
#endif
} // namespace tech
#else
#define PEEK(addr)       (*(volatile const unsigned char *)(addr))
#define POKE(addr, data) (*(volatile unsigned char *)(addr)) = (data)
#endif

#define SPACESHIP(l, r) \
    (l == r             \
        ? 0             \
        : l > r         \
            ? 1         \
            : -1)

#ifdef TARGET_NES
#define atomic volatile
#elif defined(__cplusplus)
/// C++ has no `_Atomic` qualifier -- GCC rejects it, and C++23 exposes only the
/// functional form. Every atomic-qualified object here is touched from one
/// thread, so volatile preserves the ordering the NES build relies on.
#define atomic volatile
#else
#define atomic _Atomic
#endif

#ifdef TARGET_NES
/// `.zp.bss`, so access is the 2-byte/3-cycle direct-page form. Zero page is 256
/// bytes shared with the compiler's imaginary registers, so reserve it for the
/// hottest mutable state.
#define direct __attribute__((section(".zp.bss")))
/// llvm-mos's LTO allocator hoists small globals into zero page; this opts a
/// symbol out, so a cold table that is merely walked cannot evict hotter state.
#define absolute __attribute__((section(".rodata")))
#else
#define direct
#define absolute
#endif

#ifdef __cplusplus
#include <br0/tuple>

namespace tech {

/// A port that cannot be read back (PPUCTRL, PPUMASK). The shadow is the readable
/// copy, and because the only write path also pokes the hardware the two cannot
/// drift. Copy-construction snapshots without poking while copy-assignment writes
/// through, which is what ::SHADOW needs.
template <u16 Addr>
class wo_register {
    atomic u8 shadow_;
public:
    wo_register() = default;                                  ///< trivial: instance lives in .bss
    wo_register(const wo_register &o) : shadow_(o.shadow_) {} ///< snapshot, no poke

    u8   get() const          { return shadow_; }                  ///< read  = shadow
    void set(const u8 v)      { shadow_ = v; poke(Addr, v); }      ///< write = shadow + hardware
    void poke_only(const u8 v){ poke(Addr, v); }                    ///< write hardware only, no shadow update

    operator u8() const { return shadow_; }
    wo_register &operator=(const u8 v)           { set(v);         return *this; }
    wo_register &operator=(const wo_register &o) { set(o.shadow_); return *this; } ///< restore path
};

/// A scoped enum keeps flag names out of the namespace but does not convert to
/// its underlying integer. This is that integer plus the bitwise operators, so
/// flag code reads like integer code while staying type-checked: enumerators
/// convert in but not out, so two unrelated enums cannot mix. sizeof equals the
/// underlying type.
///
/// @note On an atomic register keep read-modify-write as one `reg = reg | FLAG;`
///       statement, side-stepping the -Wdeprecated-volatile pitfall.
template <class E>
class enum_flags {
    using U = std::underlying_type_t<E>;
    U bits_{};
public:
    constexpr enum_flags() = default;                                  ///< trivial: lives in .bss
    constexpr enum_flags(E e) : bits_(static_cast<U>(e)) {}            ///< enumerator -> flag set
    constexpr explicit enum_flags(U raw) : bits_(raw) {}              ///< from a raw underlying value

    // One read when copied by value into the operators, one write on assignment,
    // so the P1152 pitfall never arises. These match copy signatures, so the
    // ordinary copy ops are re-defaulted alongside them.
    constexpr enum_flags(const enum_flags &) = default;
    constexpr enum_flags &operator=(const enum_flags &) = default;
    enum_flags(const volatile enum_flags &o) : bits_(o.bits_) {}        ///< read a volatile register
    void operator=(const enum_flags &o) volatile { bits_ = o.bits_; }   ///< write a volatile register

    constexpr explicit operator bool() const { return bits_ != 0; }   ///< `if (flags & mask)`
    constexpr U raw()   const { return bits_; }                       ///< underlying integer
    constexpr E value() const { return static_cast<E>(bits_); }       ///< back to the enum type

    constexpr bool has(enum_flags m) const { return (bits_ & m.bits_) == m.bits_; }

    friend constexpr enum_flags operator|(enum_flags a, enum_flags b) { return enum_flags(static_cast<U>(a.bits_ | b.bits_)); }
    friend constexpr enum_flags operator&(enum_flags a, enum_flags b) { return enum_flags(static_cast<U>(a.bits_ & b.bits_)); }
    friend constexpr enum_flags operator^(enum_flags a, enum_flags b) { return enum_flags(static_cast<U>(a.bits_ ^ b.bits_)); }
    friend constexpr enum_flags operator~(enum_flags a)               { return enum_flags(static_cast<U>(~a.bits_)); }

    constexpr enum_flags &operator|=(enum_flags o) { bits_ = static_cast<U>(bits_ | o.bits_); return *this; }
    constexpr enum_flags &operator&=(enum_flags o) { bits_ = static_cast<U>(bits_ & o.bits_); return *this; }
    constexpr enum_flags &operator^=(enum_flags o) { bits_ = static_cast<U>(bits_ ^ o.bits_); return *this; }

    friend constexpr bool operator==(enum_flags, enum_flags) = default;   ///< also yields !=
};

namespace prsv {
    template <std::size_t... I, class... Ts>
    void save_impl(u8* snap, br0::index_sequence<I...>, Ts&... regs) {
        ((snap[I] = static_cast<u8>(regs)), ...);
    }

    template <std::size_t... I, class... Ts>
    void restore_impl(u8* snap, br0::index_sequence<I...>, Ts&... regs) {
        ((regs = snap[I]), ...);
    }

    template <class... Ts>
    void save(u8* snap, Ts&... regs) {
        save_impl(snap, br0::index_sequence_for<Ts...>{}, regs...);
    }

    template <class... Ts>
    void restore(u8* snap, Ts&... regs) {
        restore_impl(snap, br0::index_sequence_for<Ts...>{}, regs...);
    }
} // namespace prsv

/// A register family is a `#define MY_REGS a, b, c` plus an `inline u8
/// MY_REGS_snapshot[3]`. The `##` stops the name expanding while building that
/// symbol, so PRESERVE and RESTORE agree on it across translation units.
#define PRESERVE(name) ::tech::prsv::save(name##_snapshot, name)

#define RESTORE(name) ::tech::prsv::restore(name##_snapshot, name)

/// `SHADOW(a, b) { body }` snapshots each register, runs the body once, then
/// restores in reverse as the block exits, including on a return out of it. The
/// registers are TEMPLATE parameters, so their addresses are compile-time and the
/// scope holds only the saved bytes -- they must therefore have linkage.
///
/// @warning An `if` with an init-statement, so a trailing `else` binds to it.
template <auto &Reg>
struct one_shadow {
    // remove_reference_t is load-bearing: decltype on a reference template
    // parameter yields the REFERENCE type, and remove_cv_t does not strip it.
    // Without it `saved` binds to the register instead of copying it and the
    // restore writes the current value back -- silently, since it compiles.
    std::remove_cv_t<std::remove_reference_t<decltype(Reg)>> saved{Reg};

    ~one_shadow() { Reg = saved; }
};

template <auto &...Regs>
struct shadow_scope : one_shadow<Regs>... {};

template <class... Ts>
struct shadow_scope_dynamic {
    br0::tuple<Ts&...> refs;                    ///< the originals (pointers only)
    br0::tuple<std::remove_cv_t<Ts>...> saved;  ///< one read each at entry
    bool first = true;

    explicit shadow_scope_dynamic(Ts&... rs) : refs(rs...), saved(rs...) {}
    ~shadow_scope_dynamic() { restore(br0::index_sequence_for<Ts...>{}); }

    template <class D, class S>
    static void store(D& dst, const S& src) { dst = src; }

    template <std::size_t... I>
    void restore(br0::index_sequence<I...>) {
        constexpr std::size_t N = sizeof...(Ts);
        (store(br0::get<N - 1 - I>(refs), br0::get<N - 1 - I>(saved)), ...);
    }

    explicit operator bool() { const bool f = first; first = false; return f; }
};
template <class... Ts>
shadow_scope_dynamic(Ts&...) -> shadow_scope_dynamic<Ts...>;

} // namespace tech

#define SHADOW(...)                                                    \
    if (::tech::shadow_scope<__VA_ARGS__> CAT(_shadow_, __LINE__){}; true)
#endif // __cplusplus

#if !defined(__cplusplus) && __STDC_VERSION__ < 202311L
#define auto __auto_type   // pre-C23: borrow the extension
#endif                     // C23+ / C++: native auto, no macro needed

#ifdef __cplusplus
#define ASM_LINKAGE extern "C"
#else
#define ASM_LINKAGE extern
#endif

#if defined(__clang__)
#define MINSIZE __attribute__((minsize))
#else
#define MINSIZE
#endif

/// A charmap is an ordinary object built from a braced list of ::Rule, one per
/// contiguous run of source characters. ::Charmap::encode turns a string literal
/// into a std::array the linker folds to one .rodata object. The terminator is
/// mapped like any other character -- whatever `'\0'` maps to, not necessarily
/// 0x00 -- so sizeof includes it and ::SIZED_STR is what a drawing routine wants.
/// Only ever constant-evaluated, so nothing of the map itself reaches the image.
namespace tech::nes_str {
/// Declared but never defined, and not constexpr: naming it during constant
/// evaluation makes that evaluation non-constant, so an unmapped character is a
/// compile error at the offending string rather than a link failure.
    [[noreturn]] u8 unmapped(char c);

/// Not a throw: `throw` is rejected at parse time under -fno-exceptions even on a
/// branch that never runs, and an llvm-mos target may build that way.
    [[noreturn]] void charmap_error(const char* why);

    struct Rule {
        char lo{}, hi{};
        u8   base{};
        constexpr Rule() = default;
        constexpr Rule(char c, u8 v)         : lo(c), hi(c), base(v) {}
        constexpr Rule(char l, char h, u8 b) : lo(l), hi(h), base(b) {}
    };

    template <std::size_t N>
    struct Charmap {
        std::array<Rule, N> rules;

        constexpr u8 operator()(char c) const {
            for (const Rule& r : rules) {
                if (c >= r.lo && c <= r.hi) return static_cast<u8>(r.base + (c - r.lo));
            }
            return unmapped(c);
        }

        template <std::size_t M>
        constexpr std::array<u8, M> encode(const char (&s)[M]) const {
            std::array<u8, M> out{};
            for (std::size_t i = 0; i < M; ++i) out[i] = (*this)(s[i]);
            return out;
        }
    };

/// consteval rather than constexpr so the checks can neither be skipped nor cost
/// a byte: a rule may not run backwards, overflow a tile index, or overlap
/// another.
    template <std::size_t N>
    consteval Charmap<N> charmap(const Rule (&r)[N]) {
        Charmap<N> m{};
        for (std::size_t i = 0; i < N; ++i) m.rules[i] = r[i];
        for (std::size_t i = 0; i < N; ++i) {
            if (m.rules[i].lo > m.rules[i].hi) {
                charmap_error("charmap rule runs backwards");
            }
            if (m.rules[i].base + (m.rules[i].hi - m.rules[i].lo) > 0xff) {
                charmap_error("charmap rule overflows a tile index");
            }
            for (std::size_t j = i + 1; j < N; ++j) {
                if (m.rules[i].lo <= m.rules[j].hi && m.rules[j].lo <= m.rules[i].hi) {
                    charmap_error("charmap rules overlap");
                }
            }
        }
        return m;
    }

    template <class T, std::size_t N> constexpr const T*      data(const T (&a)[N]) { return a; }
    template <class T, std::size_t N> constexpr std::size_t   size(const T (&)[N])  { return N; }
    template <class A> constexpr auto data(const A& a) -> decltype(a.data()) { return a.data(); }
    template <class A> constexpr auto size(const A& a) -> decltype(a.size()) { return a.size(); }

    template <class T, std::size_t N> constexpr std::size_t length(const T (&)[N]) { return N - 1; }
    template <class A> constexpr auto length(const A& a) -> decltype(a.size()) { return a.size() - 1; }
}

#define SIZED_OBJ(obj) ::tech::nes_str::data(obj), ::tech::nes_str::size(obj)

/// encode() always terminates, and that terminator is a real mapped tile rather
/// than a byte a nametable write ignores, so ::SIZED_OBJ of an encoded string
/// draws one tile too many.
#define SIZED_STR(obj) ::tech::nes_str::data(obj), ::tech::nes_str::length(obj)

namespace tech {
template <typename Count, typename Step>
void PopulateFromBuffer(u8* target, const u16 offset,
                        const u8* buffer, const Count sBuffer, const Step step) {
    const auto base = target + offset;
    for (Count i = 0; i < sBuffer; ++i) base[static_cast<int>(i) * step] = buffer[i];
}

template <typename Idx, typename Count, typename Step>
void PopulateFromProvider(u8* target, const u16 offset,
                          u8 (*fn)(Idx), const Count amt, const Step step) {
    const auto base = target + offset;
    for (Count i = 0; i < amt; ++i)
        base[static_cast<int>(i) * step] = fn(static_cast<Idx>(i));
}
} // namespace tech
