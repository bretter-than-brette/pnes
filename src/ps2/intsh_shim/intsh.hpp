/**
 * @file intsh.hpp
 * @brief PS2-only stand-in for the real `intsh` dependency, which every other
 *        platform FetchContents.
 *
 * ps2sdk's tamtypes.h already typedefs u8/u16/u32/u64/s8..s64 as global
 * typedefs, and internal.hpp includes it before any engine header. If the real
 * intsh also aliased those names through a `using namespace`, both would be
 * visible unqualified at once -- which C++ treats as ambiguous even though they
 * denote the same type, and which breaks tamtypes.h's own body too.
 *
 * So this provides only the names tamtypes.h lacks: intsh's signed i8..i64. It
 * wins by being on the include path ahead of the fetched intsh, and no
 * FetchContent happens for this target at all.
 */
#pragma once

namespace br0::intsh {

using i8  = signed char;
using i16 = short;
using i32 = int;
using i64 = long long;

}
