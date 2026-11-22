/**
 * @file interrupts.hpp
 * @brief IRQ registration and dispatch. On NES the application defines one
 *        handler with ::IRQ, placed at the vector like ::NMI, with no runtime
 *        arming; off NES there is no vector, so the renderer holds a function
 *        pointer in ::irqPending and drains it once per frame.
 */
#pragma once
#include <intsh>
using namespace br0::intsh;
#include <cstddef>
#include "types.hpp"

/// Use in place of a return type. On NES: `extern "C"` keeps the symbol stable
/// for the linker script, `used` stops LTO discarding a function only the vector
/// table references, and `interrupt_norecurse` emits the save/restore prologue
/// ending in RTI.
#ifdef TARGET_NES
#define interrupt ASM_LINKAGE __attribute__((used, interrupt_norecurse)) void
#else
#define interrupt void
#endif

namespace irq {

#ifndef TARGET_NES

typedef void (*irq_handler_fn)();

/// Overwriting this is a set, not an enqueue: only one IRQ can be pending. The
/// renderer fires it at ::irqPosition and clears ::irqPendingValid.
extern irq_handler_fn irqHandler;
extern vec2<u16>      irqPosition;
extern bool           irqPendingValid;

#endif

} // namespace irq


#ifdef TARGET_NES

#ifdef NES_MAPPER_BANKSWITCHED
/// Pinned to .prg_rom_fixed: main must stay mapped whatever any switchable
/// window holds, since everything including the bankswitching runs from it.
#define RESET __attribute__((section(".prg_rom_fixed"))) int main()
#else
#define RESET int main()
#endif

namespace irq {
inline void EnableInterrupts()  { __asm__ volatile ("cli"); }
inline void DisableInterrupts() { __asm__ volatile ("sei"); }

/// Set by every ::NMI vector after its body returns, cleared by
/// ::video::WaitForPresent -- which is what lets that call mean "block until the
/// next VBlank has been presented" on NES too.
extern volatile bool nmi_done;
} // namespace irq

/// Sits at the hardware vector by raw symbol name: no indirection, no runtime
/// rearming. The optional attribute argument is spliced after the `extern "C"`,
/// which is the only valid position in a linkage-specification, and placement
/// matters -- an interrupt arrives when nothing controls what is mapped, so a
/// handler in a switchable window is correct only while nobody banks it.
///
/// The body becomes a static `nmi_body` the real vector calls before setting
/// ::irq::nmi_done, so every project's NMI marks the flag for free.
#define NMI(...)                                                          \
ASM_LINKAGE __VA_ARGS__ void nmi_vector() asm("nmi");                     \
static __VA_ARGS__ void nmi_body();                                       \
ASM_LINKAGE __VA_ARGS__ __attribute__((used, interrupt_norecurse))        \
void nmi_vector() { nmi_body(); irq::nmi_done = true; }                   \
static __VA_ARGS__ void nmi_body()

#define IRQ(...)                                                          \
ASM_LINKAGE __VA_ARGS__ void irq_vector() asm("irq");                     \
ASM_LINKAGE __VA_ARGS__ __attribute__((used, interrupt_norecurse))        \
void irq_vector()

/// `naked`: no prologue, epilogue or RTI, so the body's asm is verbatim what
/// lands at the vector -- which means it may contain only asm and owns its own
/// housekeeping unless it tail-jumps somewhere that handles it.
#define NAKED_NMI                                            \
ASM_LINKAGE void nmi_vector() asm("nmi");                     \
ASM_LINKAGE __attribute__((naked, used))                      \
void nmi_vector()

#define NAKED_IRQ                                            \
ASM_LINKAGE void irq_vector() asm("irq");                     \
ASM_LINKAGE __attribute__((naked, used))                      \
void irq_vector()

#define JUMP(target) __asm__ volatile ("jmp " #target)

#define JUMP_INDIRECT(target) __asm__ volatile ("jmp (" #target ")")

#else

#include "audio.hpp"
#include "console.hpp"

namespace irq {
extern void init();

extern void post();
} // namespace irq

namespace video {
/// Forward-declared so ::RESET can run refresh detection without including
/// video.hpp, which would be a cycle. Called immediately after ::irq::init(),
/// which is load-bearing on OGC: detection times the vsync cadence there, so the
/// video mode has to be configured first.
bool DetectRefresh();
extern bool is50Hz;
} // namespace video

/// EnsureConsoleOnce() runs first, unconditionally rather than lazily from
/// log(): tying it to the first log() call meant a run that never logged got no
/// console even when it needed one.
#define RESET                       \
static void usr_main();         \
int main(){                     \
::tech::EnsureConsoleOnce();    \
irq::init();         \
::video::is50Hz = ::video::DetectRefresh(); \
usr_main();                 \
irq::post();         \
}                               \
inline static void usr_main ()

#define NMI(...)                \
void nmi_vector()

/// Not called by the renderer directly: it is the fixed entry point library code
/// arms as ::irq::irqHandler for any real interrupt source, with only
/// ::irq::irqPosition varying per source.
#define IRQ(...)                \
void irq_vector()

extern void irq_vector();

#define NAKED_NMI               \
void nmi_vector()

#define NAKED_IRQ               \
void irq_vector()

#define JUMP(target) target()

#define JUMP_INDIRECT(target) (target)()

namespace irq {
inline void EnableInterrupts()  {}

inline void DisableInterrupts() {}
} // namespace irq

#endif

namespace irq {
void reset();
} // namespace irq