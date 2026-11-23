#pragma once
#include <string.h>

#include "base/arena.h"
#include "base/basics.h"

// Growth reallocates within the arena, so element pointers do not survive an append.
template <typename T>
struct Array {
  T* Data;
  u32 Count;
  u32 Capacity;
  Arena* Backing;
};

namespace ArrayInternal {
template <typename T>
inline void Grow(Array<T>* a, u32 minCapacity) {
  // A u32 capacity doubled past 2^31 wraps to zero and never grows again.
  u64 newCap = a->Capacity ? (u64)a->Capacity * 2 : 8;
  while (newCap < minCapacity) newCap *= 2;
  if (newCap > 0xFFFFFFFFu) newCap = 0xFFFFFFFFu;
  T* nd = (T*)ArenaAlloc(a->Backing, sizeof(T) * (usize)newCap, alignof(T));
  if (a->Count) memcpy(nd, a->Data, sizeof(T) * (usize)a->Count);
  a->Data = nd;
  a->Capacity = (u32)newCap;
}
}

template <typename T>
inline Array<T> Array_Make(Arena* arena, u32 initialCapacity = 8) {
  if (initialCapacity < 1) initialCapacity = 1;
  Array<T> r;
  r.Backing = arena;
  r.Count = 0;
  r.Capacity = initialCapacity;
  r.Data = (T*)ArenaAlloc(arena, sizeof(T) * (usize)initialCapacity, alignof(T));
  return r;
}

template <typename T>
inline T* Array_Push(Array<T>* a, const T& value) {
  FFI(a->Count == 0xFFFFFFFFu);
  if (a->Count >= a->Capacity) ArrayInternal::Grow(a, a->Count + 1);
  a->Data[a->Count] = value;
  return &a->Data[a->Count++];
}

template <typename T>
inline T* Array_Get(Array<T>* a, u32 index) {
  return index < a->Count ? &a->Data[index] : nullptr;
}

template <typename T>
inline T* Array_Last(Array<T>* a) {
  return a->Count ? &a->Data[a->Count - 1] : nullptr;
}

template <typename T>
inline T Array_Pop(Array<T>* a) {
  FFI(a->Count == 0);
  return a->Data[--a->Count];
}

template <typename T>
inline void Array_Clear(Array<T>* a) {
  a->Count = 0;
}

// Stable insertion sort; only for short arrays.
template <typename T, typename Before>
inline void Array_Sort(Array<T>* a, Before before) {
  for (u32 i = 1; i < a->Count; i++) {
    T value = a->Data[i];
    u32 slot = i;
    while (slot > 0 && before(value, a->Data[slot - 1])) {
      a->Data[slot] = a->Data[slot - 1];
      slot--;
    }
    a->Data[slot] = value;
  }
}
