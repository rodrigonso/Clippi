#pragma once
#include "base/arena.h"
#include "base/str.h"

struct OAuthPkce {
  StrView Verifier;
  StrView Challenge;
  StrView State;
};

OAuthPkce OAuthPkce_Create(Arena* arena);
StrView OAuthPkce_Challenge(Arena* arena, StrView verifier);

void OAuthQuery_Append(StrBuf* url, const char* name, StrView value);
void OAuthForm_Append(StrBuf* body, const char* name, StrView value);
bool OAuthQuery_Find(Arena* arena, StrView query, const char* name, StrView* out);

enum class OAuthCallbackStatus : u8 { Code, WrongPath, StateMismatch, Denied, MissingCode };

struct OAuthCallback {
  OAuthCallbackStatus Status;
  StrView Code;
  StrView Error;
};

OAuthCallback OAuthCallback_Parse(Arena* arena, StrView target, StrView path,
                                  StrView expectedState);
