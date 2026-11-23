#pragma once
#include "auth/credential.h"
#include "base/arena.h"
#include "base/str.h"

struct AuthTokenCache {
  StrView Provider;
  StrView Value;
  StrView AccountId;
  u64 ExpiresAtMs;  // zero for a key that does not expire
};

void AuthTokenCache_Invalidate(AuthTokenCache* cache);
bool AuthTokenCache_Store(Arena* arena, AuthTokenCache* cache, StrView provider,
                          StrView token, StrView accountId = {},
                          u64 expiresAtMs = 0);
bool AuthTokenCache_Get(const AuthTokenCache* cache, StrView provider,
                        StrView* out);

StrView Auth_ResolveToken(Arena* arena, StrView authPath, StrView providerId,
                          const char* envVar);
