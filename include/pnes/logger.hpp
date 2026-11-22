/**
 * @file logger.hpp
 * @brief Zero-footprint NES logging.
 *
 * On NES, log("...") costs one `nop` -- the anchor the host tooling resolves to
 * (see ::PNES_LOG_ANCHOR). Everything else lands in `.pnes_log`, a section no
 * mapper script maps to a MEMORY region, so it survives in the .elf for the
 * `logger` tool and contributes nothing to the ROM. tools/pnes.lua then prints
 * each message in Mesen when the CPU reaches that physical location.
 *
 * Off NES, log() is real and prints to stdout immediately.
 */
#pragma once

#include <intsh>
using namespace br0::intsh;

#ifdef TARGET_NES

#include <type_traits>

namespace log_detail {

/// Never the value -- log() formats nothing on NES. `address` is a live CPU
/// address, so @p value must have static storage duration: a local has no
/// address still meaningful by the time Mesen reads it.
struct Arg {
    const volatile void *address;
    u8 size;
    u8 is_signed;
};

struct Entry {
    const char *file;
    u32 line;
    const char *message;
    const Arg *args;
    u8 arg_count;
    u8 kind; // PNES_LOG_KIND_LOG or PNES_LOG_KIND_PAUSE
};

template <typename T>
constexpr Arg MakeArg(const volatile T &value) {
    using U = std::conditional_t<std::is_enum_v<T>, std::underlying_type_t<T>, T>;
    return Arg{ &value, static_cast<u8>(sizeof(T)), static_cast<u8>(std::is_signed_v<U> ? 1 : 0) };
}

} // namespace log_detail

extern const float silentHeapAmount;
extern const float silentStackAmount;

#define PNES_LOG_CAT_(a, b) a##b
#define PNES_LOG_CAT(a, b)  PNES_LOG_CAT_(a, b)

/// An Entry pointing at ordinary .rodata would leak those bytes into the ROM
/// even though the Entry does not. `used`, because nothing at runtime
/// dereferences these -- which is exactly what zero footprint requires.
#define PNES_LOG_TEXT(name, text) \
    static const char name[] __attribute__((section(".pnes_log"), used)) = text

#define PNES_LOG_KIND_LOG   0
#define PNES_LOG_KIND_PAUSE 1

#define PNES_LOG_NARG_(_0, _1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define PNES_LOG_NARG(...) \
    PNES_LOG_NARG_(dummy __VA_OPT__(,) __VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define PNES_LOG_MAP1(x)      ::log_detail::MakeArg(x)
#define PNES_LOG_MAP2(x, ...) ::log_detail::MakeArg(x), PNES_LOG_MAP1(__VA_ARGS__)
#define PNES_LOG_MAP3(x, ...) ::log_detail::MakeArg(x), PNES_LOG_MAP2(__VA_ARGS__)
#define PNES_LOG_MAP4(x, ...) ::log_detail::MakeArg(x), PNES_LOG_MAP3(__VA_ARGS__)
#define PNES_LOG_MAP5(x, ...) ::log_detail::MakeArg(x), PNES_LOG_MAP4(__VA_ARGS__)
#define PNES_LOG_MAP6(x, ...) ::log_detail::MakeArg(x), PNES_LOG_MAP5(__VA_ARGS__)
#define PNES_LOG_MAP7(x, ...) ::log_detail::MakeArg(x), PNES_LOG_MAP6(__VA_ARGS__)
#define PNES_LOG_MAP8(x, ...) ::log_detail::MakeArg(x), PNES_LOG_MAP7(__VA_ARGS__)
#define PNES_LOG_MAP(N, ...)  PNES_LOG_CAT(PNES_LOG_MAP, N)(__VA_ARGS__)

/// log("hp=%d", hp) captures hp's address, size and signedness beside the format
/// string; tools/pnes.lua reads the live value out of Mesen's memory. __COUNTER__
/// is captured once via the id parameter, since referencing it twice yields two
/// values. The Arg array carries a trailing dummy so it is never zero-size.
#define log(msg, ...) \
    PNES_LOG_IMPL(PNES_LOG_KIND_LOG, msg, __COUNTER__ __VA_OPT__(,) __VA_ARGS__)

#define pause() PNES_LOG_IMPL(PNES_LOG_KIND_PAUSE, "", __COUNTER__)

/// Without the anchor a log point has no address of its own: a statement that
/// emits no instructions gets no DWARF line-table row, so the builder borrows the
/// nearest LATER one. That misfired both ways in this project's own demo -- a log
/// above a loop printed every iteration forever, and logs in a switch's arms
/// resolved to nothing and were dropped. `volatile` is what stops the nop being
/// deleted, duplicated or hoisted. Costs one byte and two cycles per call site;
/// ::PNES_LOG_NO_ANCHOR gets the old approximate behaviour back.
#ifdef PNES_LOG_NO_ANCHOR
    #define PNES_LOG_ANCHOR() ((void)0)
#else
    #define PNES_LOG_ANCHOR() __asm__ volatile("nop")
#endif

#define PNES_LOG_IMPL(kind, msg, id, ...)                                       \
    do {                                                                        \
        PNES_LOG_ANCHOR();                                                      \
        PNES_LOG_TEXT(PNES_LOG_CAT(_pnes_log_file_, id), __FILE__);             \
        PNES_LOG_TEXT(PNES_LOG_CAT(_pnes_log_msg_, id), msg);                   \
        static const ::log_detail::Arg PNES_LOG_CAT(_pnes_log_args_, id)[]      \
            __attribute__((section(".pnes_log"), used)) = {                     \
                __VA_OPT__(PNES_LOG_MAP(PNES_LOG_NARG(__VA_ARGS__), __VA_ARGS__),) \
                ::log_detail::Arg{ nullptr, 0, 0 }                              \
            };                                                                  \
        static const ::log_detail::Entry PNES_LOG_CAT(_pnes_log_entry_, id)     \
            __attribute__((section(".pnes_log"), used)) = {                     \
                PNES_LOG_CAT(_pnes_log_file_, id),                              \
                __LINE__,                                                       \
                PNES_LOG_CAT(_pnes_log_msg_, id),                               \
                PNES_LOG_CAT(_pnes_log_args_, id),                              \
                static_cast<u8>(PNES_LOG_NARG(__VA_ARGS__)),                    \
                static_cast<u8>(kind)                                          \
            };                                                                  \
    } while (0)

#elif defined(TARGET_PS2) && !defined(NDEBUG)

// ps2sdk's EE has no hosted stdio behind it, and <iostream>'s global static-init
// jumped through an unpopulated pointer and crashed the EE before main(). So this
// never includes it: sio_puts writes straight to the serial port, which is what
// PCSX2's "EE SIO output" reads. snprintf-style, so a caller passes ordinary
// printf specifiers.
#include <sio.h>
#include <cstdio>

namespace log_detail {
inline void EnsureSioOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    sio_init(38400, 0, 0, 0, 0);
}
} // namespace log_detail

#define log(msg, ...)                                                 \
    do {                                                              \
        ::log_detail::EnsureSioOnce();                                \
        char _pnes_sio_buf[192];                                      \
        std::snprintf(_pnes_sio_buf, sizeof(_pnes_sio_buf),           \
                      "log: " msg __VA_OPT__(,) __VA_ARGS__);         \
        sio_puts(_pnes_sio_buf);                                      \
    } while (0)

#define pause()                                                       \
    do {                                                              \
        ::log_detail::EnsureSioOnce();                                \
        sio_puts("log: pause() hit -- no stdin on PS2, continuing");  \
    } while (0)

#elif defined(TARGET_PS3) && !defined(NDEBUG)

// The PS2 branch's problem again: PSL1GHT's newlib has nowhere to put stdio's
// bytes either. sysTtyWrite is a static-inline lv2 syscall, so there is no stream
// object and nothing to initialise. RPCS3 surfaces channel 0 in its TTY log.
#include <sys/tty.h>
// <stdio.h> and unqualified snprintf, not std::snprintf: ps3dev's newlib
// declares it in the global namespace only.
#include <stdio.h>
#include <cstring>

namespace log_detail {
inline void TtyPuts(const char *msg) {
    u32 written = 0;
    sysTtyWrite(0, msg, static_cast<u32>(std::strlen(msg)), &written);
}
} // namespace log_detail

#define log(msg, ...)                                                 \
    do {                                                              \
        char _pnes_tty_buf[192];                                      \
        snprintf(_pnes_tty_buf, sizeof(_pnes_tty_buf),           \
                      "log: " msg "\n" __VA_OPT__(,) __VA_ARGS__);    \
        ::log_detail::TtyPuts(_pnes_tty_buf);                         \
    } while (0)

#define pause()                                                       \
    ::log_detail::TtyPuts("log: pause() hit -- no stdin on PS3, continuing\n")

#elif defined(TARGET_CTR) && !defined(NDEBUG)

// consoleInit() is not an option -- it claims a screen's framebuffer, and both
// belong to citro2d -- so the SD card is what is left, and it is the only channel
// that reads the same on hardware and under an emulator. Opened once and flushed
// per line, because an open/close pair per message is an FS round trip through a
// service call. Truncating, so the file describes THIS run.
#include <cstdio>

namespace log_detail {
inline FILE* CtrLogFile() {
    static FILE* f = std::fopen("sdmc:/pnes.log", "w");
    return f;
}
} // namespace log_detail

#define log(msg, ...)                                                 \
    do {                                                              \
        FILE* _pnes_ctr_log = ::log_detail::CtrLogFile();             \
        if (_pnes_ctr_log) {                                          \
            std::fprintf(_pnes_ctr_log,                               \
                         "log: " msg "\n" __VA_OPT__(,) __VA_ARGS__); \
            std::fflush(_pnes_ctr_log);                               \
        }                                                             \
    } while (0)

#define pause()                                                       \
    log("pause() hit at %s:%d -- no stdin on 3DS, continuing", __FILE__, __LINE__)

#elif !defined(NDEBUG)

#include <iostream>
#include <utility>
#include <type_traits>
#include "console.hpp"

namespace log_detail {

/// operator<< has no overload for a scoped enum, so print the underlying value,
/// promoted when byte-sized (operator<< treats a char as a character). NOT
/// conditional_t<is_enum_v<T>, underlying_type_t<T>, T>, which cannot work: both
/// branches are instantiated before the condition picks one, and
/// underlying_type<T> has no `type` for a non-enum. A partial specialisation
/// defers the lookup instead.
template <typename T, bool = std::is_enum_v<T>>
struct UnderlyingOf { using type = T; };

template <typename T>
struct UnderlyingOf<T, true> { using type = std::underlying_type_t<T>; };

template <typename T>
constexpr auto LogPrintable(T &&value) {
    using Raw = std::remove_cvref_t<T>;
    using Underlying = typename UnderlyingOf<Raw>::type;
    if constexpr (std::is_integral_v<Underlying> && sizeof(Underlying) == 1) {
        using Promoted = std::conditional_t<std::is_signed_v<Underlying>, int, unsigned>;
        return static_cast<Promoted>(static_cast<Underlying>(value));
    } else if constexpr (std::is_enum_v<Raw>) {
        return static_cast<Underlying>(value);
    } else {
        return static_cast<Raw>(value);
    }
}

inline void LogFormat(const char *fmt) {
    std::cout << fmt;
}

/// %x/%X get hex to match what tools/pnes.lua produces for the same specifier.
template <typename T, typename... Rest>
void LogFormat(const char *fmt, T &&value, Rest &&...rest) {
    while (*fmt) {
        if (fmt[0] == '%' && fmt[1] != '\0') {
            const char spec = fmt[1];
            auto printable = LogPrintable(std::forward<T>(value));
            if (spec == 'x') std::cout << std::hex << printable << std::dec;
            else if (spec == 'X') std::cout << std::uppercase << std::hex << printable << std::nouppercase << std::dec;
            else std::cout << printable;
            LogFormat(fmt + 2, std::forward<Rest>(rest)...);
            return;
        }
        std::cout << *fmt++;
    }
}

} // namespace log_detail

/// Gated behind DEBUG entirely, so a release build never pays for this, matching
/// log()'s NES side always costing nothing. EnsureConsoleOnce() is not called
/// here -- ::RESET runs it at startup, so a run that only logs once still has a
/// console.
#define log(msg, ...) \
    ((std::cout << "log: "), ::log_detail::LogFormat(msg __VA_OPT__(,) __VA_ARGS__), (std::cout << '\n'))

/// @note WaitForKeypress reads real stdin, which is not the terminal
/// EnsureConsoleOnce may have spawned for stdout -- so pause() only works as
/// intended when the process was launched from a real terminal.
#define pause() \
    ((std::cout << "log: pause() hit at " << __FILE__ << ":" << __LINE__ \
                 << " -- press any key to continue...\n" << std::flush), \
     ::tech::WaitForKeypress())

#else

#define log(msg, ...) ((void)0)
#define pause() ((void)0)

#endif
