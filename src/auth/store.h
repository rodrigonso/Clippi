#pragma once
#include "auth/credential.h"
#include "base/arena.h"
#include "base/process_file_lock.h"
#include "base/str.h"

// Every mutation holds `<path>.lock` across its read-modify-write, or one write could
// clobber another process's freshly rotated, single-use refresh token.

StrView AuthStore_DefaultPath(Arena* arena);

// Outlasts a token refresh's request timeout, so a waiter does not give up on a live refresher.
constexpr u64 AUTH_STORE_LOCK_WAIT_MS = 65 * 1000;

struct AuthStoreLock {
  ProcessFileLock Lock;
  StrView Path;
};

// `path` and the lock's own path stay borrowed from `scratch` until AuthStore_Unlock.
bool AuthStore_Lock(Arena* scratch, StrView path, u64 waitMs, volatile i32* abort,
                    AuthStoreLock* out);
void AuthStore_Unlock(AuthStoreLock* lock);

bool AuthStore_Read(Arena* arena, StrView path, StrView providerId, Credential* out);
// Takes the store's lock itself; use _WriteLocked when already holding it.
bool AuthStore_Write(Arena* arena, StrView path, StrView providerId, const Credential* cred);
bool AuthStore_Delete(Arena* arena, StrView path, StrView providerId);
bool AuthStore_WriteLocked(Arena* arena, const AuthStoreLock* held, StrView path,
                           StrView providerId, const Credential* cred);
