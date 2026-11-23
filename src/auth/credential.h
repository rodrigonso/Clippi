#pragma once
#include "base/basics.h"
#include "base/str.h"

enum class CredentialKind : u8 { ApiKey, OAuth };

struct Credential {
  CredentialKind Kind;
  StrView ApiKey;
  StrView AccessToken;
  StrView RefreshToken;
  StrView AccountId;
  u64 ExpiresAtMs;
};

constexpr u64 CREDENTIAL_REFRESH_MARGIN_MS = 5 * 60 * 1000;

inline bool Credential_NeedsRefresh(u64 expiresAtMs, u64 nowMs) {
  return expiresAtMs <= nowMs + CREDENTIAL_REFRESH_MARGIN_MS;
}
