#pragma once
#include "auth/credential.h"
#include "base/arena.h"
#include "base/process_file_lock.h"
#include "base/str.h"

// Credential storage in a JSON file keyed by provider id, one credential each.
// Writes are atomic (temp + rename) with restrictive perms on POSIX. Entries
// are `{"type":"api_key","key":...}` or, as pi writes them,
// `{"type":"oauth","access":...,"refresh":...,"expires":<ms>,"accountId":...}`
// with `accountId` optional. A store holding any other shape is never
// rewritten, so an unknown entry is preserved rather than dropped.
//
// Every mutation rewrites the whole file, so each one holds `<path>.lock`
// across its read-modify-write; otherwise a write for one provider could
// overwrite another process's freshly rotated, single-use refresh token.

// Default path: <home>/.clippi/auth.json.
StrView AuthStore_DefaultPath(Arena* arena);

// Covers a token refresh's own request timeout, so a waiter outlasts a holder
// that is still refreshing rather than giving up while it works.
constexpr u64 AUTH_STORE_LOCK_WAIT_MS = 65 * 1000;

struct AuthStoreLock {
  ProcessFileLock Lock;
  StrView Path;
};

// Waits up to `waitMs` (or until `abort` is set) for the store's lock. `path`
// and the lock's own path stay borrowed from `scratch` until AuthStore_Unlock.
bool AuthStore_Lock(Arena* scratch, StrView path, u64 waitMs, volatile i32* abort,
                    AuthStoreLock* out);
void AuthStore_Unlock(AuthStoreLock* lock);

bool AuthStore_Read(Arena* arena, StrView path, StrView providerId, Credential* out);
// Take the store's lock for the duration of the write or delete.
bool AuthStore_Write(Arena* arena, StrView path, StrView providerId, const Credential* cred);
bool AuthStore_Delete(Arena* arena, StrView path, StrView providerId);
// For a caller that already holds the store's lock across a read and a write.
bool AuthStore_WriteLocked(Arena* arena, const AuthStoreLock* held, StrView path,
                           StrView providerId, const Credential* cred);
