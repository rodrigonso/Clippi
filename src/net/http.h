#pragma once
#include "base/arena.h"
#include "base/str.h"
#include "llm/retry.h"

// Sink chunks are zero-copy slices into curl's buffer.

struct HttpHeader {
  StrView Name;
  StrView Value;
};

inline HttpHeader Http_BearerHeader(Arena* arena, StrView token) {
  StrBuf bearer = StrBuf_Make(arena, token.Length + 8);
  StrBuf_AppendCStr(&bearer, "Bearer ");
  StrBuf_Append(&bearer, token);
  return HttpHeader{SVL("authorization"), StrBuf_View(&bearer)};
}

struct HttpRequest {
  StrView Method;
  StrView Url;
  const HttpHeader* Headers;
  u32 HeaderCount;
  StrView Body;
  bool FollowRedirects;
  // Buffers a non-2xx body in HttpResult::Body instead of the sink, so an SSE parser never sees an error page.
  bool BufferNonSuccessBody;
  // 0 keeps the 10-minute default; the connect phase is capped at min(this, 30 s).
  u32 TimeoutMs;
  u32 MaxResponseBytes;  // zero leaves the response size unbounded
  // The shared connection serializes requests; isolate so background work never delays foreground traffic.
  bool Isolated;
};

struct HttpStreamSink {
  void* User;
  // Return false to abort the transfer.
  bool (*OnData)(void* user, StrView chunk);
};

struct HttpRetryMeta {
  bool HasShouldRetry;
  bool ShouldRetry;
  i64 RetryAfterMs;     // "retry-after-ms", or "retry-after" seconds as ms; -1 if absent/unparseable
};

i32 HttpRetryMeta_ParseStatusLine(StrView line);

// HTTP-date retry-after values are ignored; retry-after-ms wins over retry-after.
void HttpRetryMeta_ParseHeaderLine(HttpRetryMeta* meta, StrView line);

struct HttpResult {
  i32 Status;         // HTTP status code, or -1 on transport error / abort
  StrView Error;
  HttpRetryMeta Meta;  // header-derived retry hints; valid whenever Status >= 0
  // Only set with BufferNonSuccessBody on a non-2xx status; 2xx bytes went to the sink.
  StrView Body;
  // Once true, part of this response reached downstream consumers, so it must never be retried transparently.
  bool BodyStarted;
  bool BodyLimitExceeded;
};

struct HttpResponse {
  i32 Status;
  StrView Body;
  StrView Error;
};

// Idempotent. Initialization may be deferred until immediately before the first network operation.
void Http_GlobalInit();
void Http_GlobalShutdown();

HttpResult Http_Stream(Arena* arena, const HttpRequest* req, HttpStreamSink sink, volatile i32* abort);

HttpResponse Http_Fetch(Arena* arena, const HttpRequest* req, volatile i32* abort);

// Transport-level retry is default-off and never retries once a prior attempt's body reached the sink.

bool HttpStatus_IsRetryable(i32 status);

// `attemptsMade` includes this attempt; an "x-should-retry" hint overrides the status either way.
bool HttpRetry_ShouldRetry(const HttpRetryPolicy* policy, i32 status, bool transportFailure,
                           const HttpRetryMeta* meta, u32 attemptsMade);

// `attemptIndex` is 0 before the first retry. A server-provided delay in `meta` wins over backoff.
u32 HttpRetry_DelayMs(const HttpRetryPolicy* policy, const HttpRetryMeta* meta, u32 attemptIndex,
                     double jitterRandom01);

typedef HttpResult (*HttpTransportFn)(void* user, Arena* arena, const HttpRequest* req,
                                      HttpStreamSink sink, volatile i32* abort);
typedef void (*HttpRetrySleepFn)(void* user, u32 ms);
typedef double (*HttpRetryRandFn)(void* user);  // must return a value in [0,1)

struct HttpRetryDeps {
  HttpTransportFn Transport;
  void* TransportUser;
  HttpRetrySleepFn Sleep;
  void* SleepUser;
  HttpRetryRandFn Rand;
  void* RandUser;
};

// Sleeps in <=100 ms steps polling `*abort`; OnEvent gets a matched Start/End around every backoff,
// including an interrupted one.
HttpResult Http_StreamWithRetryDeps(Arena* arena, const HttpRequest* req, HttpStreamSink sink,
                                   volatile i32* abort, const HttpRetryPolicy* policy,
                                   const HttpRetryDeps* deps);

HttpResult Http_StreamWithRetry(Arena* arena, const HttpRequest* req, HttpStreamSink sink,
                                volatile i32* abort, const HttpRetryPolicy* policy);
