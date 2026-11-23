#pragma once
#include "base/str.h"

enum class HttpRetryEventKind : u8 {
  Start,
  End,
};

struct HttpRetryEvent {
  HttpRetryEventKind Kind;
  u32 Attempt;  // 1 for the first retry
  u32 MaxRetries;
  u32 DelayMs;
  i32 Status;
  StrView Error;
};

typedef void (*HttpRetryEventFn)(void* user, const HttpRetryEvent* event);

struct HttpRetryPolicy {
  bool Enabled;
  u32 MaxRetries;  // retry attempts after the first try
  u32 BaseDelayMs;
  u32 MaxDelayMs;  // caps server-provided delays as well as computed backoff
  void* EventUser;
  HttpRetryEventFn OnEvent;
};

HttpRetryPolicy HttpRetryPolicy_Default();

// Port of pi's isRetryableAssistantError, with case-insensitive literal substrings in place of pi's regexes.
bool IsRetryableError(StrView errorMessage);
