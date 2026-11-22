/**
 * @file intsh.hpp
 * @brief PS3-only stand-in for the real `intsh` dependency. The same problem and
 *        fix as src/ps2/intsh_shim/intsh.hpp -- read that one.
 *
 * PSL1GHT's ppu-types.h already typedefs u8..u64 and s8..s64 globally, and two
 * unqualified spellings of one name are ambiguous to C++ even when they denote
 * the same type. So this provides only what it lacks: intsh's signed i8..i64.
 */
#pragma once

namespace br0::intsh {

using i8  = signed char;
using i16 = short;
using i32 = int;
using i64 = long long;

}
