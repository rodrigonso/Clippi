#pragma once
#include "auth/oauth.h"
#include "base/arena.h"
#include "base/str.h"

#define OPENROUTER_OAUTH_AUTHORIZE_URL "https://openrouter.ai/auth"
#define OPENROUTER_OAUTH_CALLBACK_PATH "/callback"

// An empty `callbackUrl` selects OpenRouter's headless (paste-the-code) mode.
StrView OpenRouterOAuth_AuthorizeUrl(Arena* arena, const OAuthPkce* pkce,
                                     StrView callbackUrl);

struct OAuthKeyResult {
  bool Ok;
  StrView Key;
  StrView Error;
};

StrView OpenRouterOAuth_ExchangeBody(Arena* arena, StrView code, StrView verifier);
OAuthKeyResult OpenRouterOAuth_ParseExchange(Arena* arena, i32 status, StrView body,
                                             StrView transportError);

OAuthKeyResult OpenRouterOAuth_Exchange(Arena* arena, StrView keysUrl, StrView code,
                                        StrView verifier, volatile i32* abort);
