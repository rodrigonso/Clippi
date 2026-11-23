#pragma once
#include "base/basics.h"

typedef void (*ThreadFn)(void* arg);

struct ThreadPoolTask {
  ThreadFn Fn;
  void* Arg;
};

struct Thread {
  alignas(16) unsigned char Storage[16];
  ThreadFn Fn;
  void* Arg;
};

bool Thread_Start(Thread* t, ThreadFn fn, void* arg);
void Thread_Join(Thread* t);

// `task` must remain alive until the submitted callback has completed.
bool ThreadPool_Submit(ThreadPoolTask* task, ThreadFn fn, void* arg);

struct Mutex {
  alignas(16) unsigned char Storage[64];
};

void Mutex_Init(Mutex* m);
void Mutex_Destroy(Mutex* m);
void Mutex_Lock(Mutex* m);
void Mutex_Unlock(Mutex* m);

struct CondVar {
  alignas(16) unsigned char Storage[64];
};

void Cond_Init(CondVar* c);
void Cond_Destroy(CondVar* c);
void Cond_Wait(CondVar* c, Mutex* m);
void Cond_WaitTimeout(CondVar* c, Mutex* m, u32 ms);
void Cond_Signal(CondVar* c);
void Cond_Broadcast(CondVar* c);

void Sleep_Ms(u32 ms);

// MSVC volatile has acquire/release semantics only under /volatile:ms, so both sides go through
// these. Relaxed is intentional: a cancellation flag publishes no data alongside itself.
void Atomic_StoreI32(volatile i32* target, i32 value);
i32 Atomic_LoadI32(const volatile i32* target);

u64 MonotonicMillis();

u64 MonotonicMicros();

struct ScopedLock {
  Mutex* M;
  explicit ScopedLock(Mutex* m) : M(m) { Mutex_Lock(m); }
  ~ScopedLock() { Mutex_Unlock(M); }
  ScopedLock(const ScopedLock&) = delete;
  ScopedLock& operator=(const ScopedLock&) = delete;
};
