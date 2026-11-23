#pragma once
#include "auth/oauth.h"
#include "base/arena.h"
#include "base/str.h"

#define OPENAI_CODEX_OAUTH_CLIENT_ID "app_EMoamEEZ73f0CkXaXp7hrann"
#define OPENAI_CODEX_OAUTH_AUTHORIZE_URL "https://auth.openai.com/oauth/authorize"
#define OPENAI_CODEX_OAUTH_TOKEN_URL "https://auth.openai.com/oauth/token"
#define OPENAI_CODEX_OAUTH_SCOPE "openid profile email offline_access"
// Fixed, not OS-assigned: the client is registered with exactly this redirect.
#define OPENAI_CODEX_OAUTH_CALLBACK_PORT 1455
#define OPENAI_CODEX_OAUTH_CALLBACK_PATH "/auth/callback"
#define OPENAI_CODEX_OAUTH_REDIRECT_URI "http://localhost:1455/auth/callback"

StrView OpenAICodexOAuth_AuthorizeUrl(Arena* arena, const OAuthPkce* pkce);
StrView OpenAICodexOAuth_ExchangeBody(Arena* arena, StrView code, StrView verifier);
StrView OpenAICodexOAuth_RefreshBody(Arena* arena, StrView refreshToken);

struct OpenAICodexTokens {
  bool Ok;
  StrView Access;
  StrView Refresh;
  StrView AccountId;
  u64 ExpiresAtMs;
  StrView Error;
};

OpenAICodexTokens OpenAICodexOAuth_ParseTokens(Arena* arena, i32 status, StrView body,
                                               StrView transportError,
                                               StrView previousRefresh, u64 nowMs);

StrView OpenAICodexOAuth_AccountId(Arena* arena, StrView accessToken);
u64 OpenAICodexOAuth_TokenExpiry(Arena* arena, StrView accessToken);

bool OpenAICodexOAuth_ParsePastedInput(Arena* arena, StrView input, StrView expectedState,
                                       StrView* code, StrView* error);

OpenAICodexTokens OpenAICodexOAuth_Exchange(Arena* arena, StrView code, StrView verifier,
                                            volatile i32* abort);
OpenAICodexTokens OpenAICodexOAuth_Refresh(Arena* arena, StrView refreshToken,
                                           volatile i32* abort);
