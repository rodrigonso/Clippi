#pragma once
#if !defined(_WIN32) && !defined(__APPLE__)
#error "Clippi supports only Windows and macOS."
#endif

#include <stddef.h>
#include <stdint.h>
#include <time.h>

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;
using i64 = int64_t;
using i32 = int32_t;
using i16 = int16_t;
using i8 = int8_t;
using f32 = float;
using f64 = double;
using byte = uint8_t;
using usize = size_t;

inline void Memory_Zero(void* memory, usize size) {
  volatile byte* bytes = (volatile byte*)memory;
  while (size--) *bytes++ = 0;
}

#if defined(_MSC_VER)
#include <intrin.h>
#define CLIPPI_TRAP() __fastfail(0)
#else
#define CLIPPI_TRAP() __builtin_trap()
#endif

// FFI(x) traps if x is true, i.e. x describes the disallowed state.
#define FFI(cond)             \
  do {                        \
    if (cond) CLIPPI_TRAP(); \
  } while (0)

template <typename T>
inline T ClippiMin(T a, T b) {
  return a < b ? a : b;
}

// The addend is converted to the accumulator type first, so it must not be the wider of the two.
template <typename T, typename U>
inline T AddSaturated(T a, U b) {
  T limit = (T)~(T)0;
  T addend = (T)b;
  return limit - a < addend ? limit : (T)(a + addend);
}

// Wall clock, not monotonic -- use MonotonicMillis() from base/thread.h for durations.
u64 NowMillis();