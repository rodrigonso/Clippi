#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/arena.h"
#include "base/thread.h"
#include "check.h"
#include "net/http.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int clippi_socklen_t;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
typedef socklen_t clippi_socklen_t;
#define INVALID_SOCKET (-1)
#endif

namespace {

void TestStatusLineParsing() {
  CHECK(HttpRetryMeta_ParseStatusLine(SVL("HTTP/1.1 200 OK")) == 200);
  CHECK(HttpRetryMeta_ParseStatusLine(SVL("HTTP/1.1 429 Too Many Requests\r\n")) == 429);
  CHECK(HttpRetryMeta_ParseStatusLine(SVL("HTTP/2 500\r\n")) == 500);
  CHECK(HttpRetryMeta_ParseStatusLine(SVL("HTTP/1.0 404 Not Found")) == 404);
  CHECK(HttpRetryMeta_ParseStatusLine(SVL("X-Should-Retry: true")) == -1);
  CHECK(HttpRetryMeta_ParseStatusLine(SVL("")) == -1);
  CHECK(HttpRetryMeta_ParseStatusLine(SVL("HTTP/1.1")) == -1);
}

void TestHeaderLineParsing() {
  {
    HttpRetryMeta meta = {};
    meta.RetryAfterMs = -1;
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("X-Should-Retry: true"));
    CHECK_ALL(meta.HasShouldRetry, meta.ShouldRetry);
  }
  {
    HttpRetryMeta meta = {};
    meta.RetryAfterMs = -1;
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("x-should-retry: FALSE"));
    CHECK_ALL(meta.HasShouldRetry, !meta.ShouldRetry);
  }
  {
    HttpRetryMeta meta = {};
    meta.RetryAfterMs = -1;
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("X-Should-Retry: maybe"));
    CHECK(!meta.HasShouldRetry);
  }
  {
    HttpRetryMeta meta = {};
    meta.RetryAfterMs = -1;
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("Retry-After-Ms: 1234"));
    CHECK(meta.RetryAfterMs == 1234);
  }
  {
    HttpRetryMeta meta = {};
    meta.RetryAfterMs = -1;
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("Retry-After: 3"));
    CHECK(meta.RetryAfterMs == 3000);
  }
  {
    HttpRetryMeta meta = {};
    meta.RetryAfterMs = -1;
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("Retry-After-Ms: 500"));
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("Retry-After: 30"));
    CHECK(meta.RetryAfterMs == 500);
  }
  {
    HttpRetryMeta meta = {};
    meta.RetryAfterMs = -1;
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("Retry-After: 30"));
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("Retry-After-Ms: 500"));
    CHECK(meta.RetryAfterMs == 500);
  }
  {
    HttpRetryMeta meta = {};
    meta.RetryAfterMs = -1;
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("Content-Type: application/json"));
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("HTTP/1.1 429 Too Many Requests"));
    CHECK_ALL(!meta.HasShouldRetry, meta.RetryAfterMs == -1);
  }
  {
    HttpRetryMeta meta = {};
    meta.RetryAfterMs = -1;
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("Retry-After-Ms: soon"));
    HttpRetryMeta_ParseHeaderLine(&meta, SVL("NoColonHere"));
    CHECK(meta.RetryAfterMs == -1);
  }
}

void TestHttpStatusIsRetryable() {
  CHECK_ALL(HttpStatus_IsRetryable(408), HttpStatus_IsRetryable(409), HttpStatus_IsRetryable(429),
            HttpStatus_IsRetryable(500), HttpStatus_IsRetryable(503), HttpStatus_IsRetryable(599));
  CHECK_ALL(!HttpStatus_IsRetryable(200), !HttpStatus_IsRetryable(400), !HttpStatus_IsRetryable(401),
            !HttpStatus_IsRetryable(404), !HttpStatus_IsRetryable(600), !HttpStatus_IsRetryable(-1));
}

void TestShouldRetryDecisions() {
  HttpRetryPolicy off = HttpRetryPolicy_Default();
  HttpRetryMeta noMeta = {};
  noMeta.RetryAfterMs = -1;

  CHECK(!HttpRetry_ShouldRetry(&off, 500, false, &noMeta, 1));
  CHECK(!HttpRetry_ShouldRetry(nullptr, 500, false, &noMeta, 1));

  HttpRetryPolicy on = off;
  on.Enabled = true;

  CHECK(HttpRetry_ShouldRetry(&on, -1, true, &noMeta, 1));
  CHECK_ALL(HttpRetry_ShouldRetry(&on, 429, false, &noMeta, 1),
            HttpRetry_ShouldRetry(&on, 500, false, &noMeta, 1),
            HttpRetry_ShouldRetry(&on, 408, false, &noMeta, 1),
            HttpRetry_ShouldRetry(&on, 409, false, &noMeta, 1));
  CHECK_ALL(!HttpRetry_ShouldRetry(&on, 400, false, &noMeta, 1),
            !HttpRetry_ShouldRetry(&on, 404, false, &noMeta, 1),
            !HttpRetry_ShouldRetry(&on, 200, false, &noMeta, 1));

  CHECK(HttpRetry_ShouldRetry(&on, 500, false, &noMeta, 1));
  CHECK(HttpRetry_ShouldRetry(&on, 500, false, &noMeta, 2));
  CHECK(!HttpRetry_ShouldRetry(&on, 500, false, &noMeta, 3));

  HttpRetryMeta forceTrue = {};
  forceTrue.HasShouldRetry = true;
  forceTrue.ShouldRetry = true;
  forceTrue.RetryAfterMs = -1;
  CHECK(HttpRetry_ShouldRetry(&on, 400, false, &forceTrue, 1));

  HttpRetryMeta forceFalse = {};
  forceFalse.HasShouldRetry = true;
  forceFalse.ShouldRetry = false;
  forceFalse.RetryAfterMs = -1;
  CHECK(!HttpRetry_ShouldRetry(&on, 500, false, &forceFalse, 1));
  CHECK(!HttpRetry_ShouldRetry(&on, -1, true, &forceFalse, 1));
}

void TestDelayMsComputation() {
  HttpRetryPolicy p = HttpRetryPolicy_Default();
  p.Enabled = true;

  HttpRetryMeta noMeta = {};
  noMeta.RetryAfterMs = -1;

  // Exponential fallback: base 500ms doubling, capped at 8000ms, +/-25% jitter.
  CHECK(HttpRetry_DelayMs(&p, &noMeta, 0, 0.0) == 375);
  CHECK(HttpRetry_DelayMs(&p, &noMeta, 0, 1.0) == 625);
  CHECK(HttpRetry_DelayMs(&p, &noMeta, 1, 0.0) == 750);
  CHECK(HttpRetry_DelayMs(&p, &noMeta, 2, 0.0) == 1500);
  CHECK(HttpRetry_DelayMs(&p, &noMeta, 10, 0.0) == 6000);
  CHECK(HttpRetry_DelayMs(&p, &noMeta, 10, 1.0) == 8000);

  HttpRetryMeta server = {};
  server.RetryAfterMs = 2500;
  CHECK(HttpRetry_DelayMs(&p, &server, 0, 0.5) == 2500);

  HttpRetryMeta tooLong = {};
  tooLong.RetryAfterMs = 60000;
  CHECK(HttpRetry_DelayMs(&p, &tooLong, 0, 0.5) == 8000);

  HttpRetryPolicy tight = p;
  tight.MaxDelayMs = 1000;
  CHECK(HttpRetry_DelayMs(&tight, &server, 0, 0.5) == 1000);
  CHECK(HttpRetry_DelayMs(&tight, &noMeta, 5, 0.0) == 750);
}

struct FakeAttempt {
  i32 Status;
  bool BodyStarted;
  bool HasShouldRetry;
  bool ShouldRetry;
};

struct FakeTransportState {
  const FakeAttempt* Attempts;
  u32 Count;
  u32 Calls;
  u32 SleepCalls;
  u32 TotalSleptMs;
  volatile i32* AbortAfterSleep;
  u32 AbortAfterSleepCalls;
  u32 RetryStarts;
  u32 RetryEnds;
  u32 RetrySequence;
  u32 LastRetryAttempt;
  i32 LastRetryStatus;
  bool BodyLimitExceeded;
};

HttpResult FakeTransport(void* user, Arena*, const HttpRequest*, HttpStreamSink, volatile i32*) {
  FakeTransportState* s = (FakeTransportState*)user;
  u32 i = s->Calls < s->Count ? s->Calls : s->Count - 1;
  s->Calls++;
  const FakeAttempt& a = s->Attempts[i];
  HttpResult r = {};
  r.Status = a.Status;
  r.BodyStarted = a.BodyStarted;
  r.BodyLimitExceeded = s->BodyLimitExceeded;
  r.Meta.HasShouldRetry = a.HasShouldRetry;
  r.Meta.ShouldRetry = a.ShouldRetry;
  r.Meta.RetryAfterMs = -1;
  return r;
}

void FakeSleep(void* user, u32 ms) {
  FakeTransportState* s = (FakeTransportState*)user;
  s->SleepCalls++;
  s->TotalSleptMs += ms;
  if (s->AbortAfterSleep && s->SleepCalls >= s->AbortAfterSleepCalls) *s->AbortAfterSleep = 1;
}

double FakeRandZero(void*) { return 0.0; }

void FakeRetryEvent(void* user, const HttpRetryEvent* event) {
  FakeTransportState* s = (FakeTransportState*)user;
  if (event->Kind == HttpRetryEventKind::Start) {
    s->RetryStarts++;
    s->RetrySequence = s->RetrySequence * 10 + 1;
  } else {
    s->RetryEnds++;
    s->RetrySequence = s->RetrySequence * 10 + 2;
  }
  s->LastRetryAttempt = event->Attempt;
  s->LastRetryStatus = event->Status;
}

void TestRetryLoopStopsAfterRetryableFailures() {
  Arena a = ArenaCreate();
  HttpRetryPolicy policy = HttpRetryPolicy_Default();
  policy.Enabled = true;
  policy.MaxRetries = 2;
  policy.BaseDelayMs = 10;
  policy.MaxDelayMs = 1000;

  FakeAttempt attempts[3] = {
      {500, false, false, false},
      {429, false, false, false},
      {200, true, false, false},
  };
  FakeTransportState state = {};
  state.Attempts = attempts;
  state.Count = 3;
  policy.EventUser = &state;
  policy.OnEvent = FakeRetryEvent;

  HttpRetryDeps deps = {};
  deps.Transport = FakeTransport;
  deps.TransportUser = &state;
  deps.Sleep = FakeSleep;
  deps.SleepUser = &state;
  deps.Rand = FakeRandZero;

  HttpRequest req = {};
  req.Method = SVL("GET");
  req.Url = SVL("http://example.invalid/");
  HttpStreamSink sink = {};

  HttpResult r = Http_StreamWithRetryDeps(&a, &req, sink, nullptr, &policy, &deps);
  CHECK_ALL(state.Calls == 3, r.Status == 200, r.BodyStarted, state.SleepCalls == 2,
            state.RetryStarts == 2, state.RetryEnds == 2,
            state.RetrySequence == 1212, state.LastRetryAttempt == 2,
            state.LastRetryStatus == 429);

  ArenaRelease(&a);
}

void TestRetryLoopNeverRetriesAfterBodyStarted() {
  Arena a = ArenaCreate();
  HttpRetryPolicy policy = HttpRetryPolicy_Default();
  policy.Enabled = true;
  policy.BaseDelayMs = 10;

  FakeAttempt attempts[1] = {{-1, true, false, false}};
  FakeTransportState state = {};
  state.Attempts = attempts;
  state.Count = 1;

  HttpRetryDeps deps = {};
  deps.Transport = FakeTransport;
  deps.TransportUser = &state;
  deps.Sleep = FakeSleep;
  deps.SleepUser = &state;
  deps.Rand = FakeRandZero;

  HttpRequest req = {};
  HttpStreamSink sink = {};
  HttpResult r = Http_StreamWithRetryDeps(&a, &req, sink, nullptr, &policy, &deps);
  CHECK_ALL(state.Calls == 1, state.SleepCalls == 0, r.Status == -1, r.BodyStarted);

  ArenaRelease(&a);
}

void TestRetryLoopNeverRetriesAfterBodyLimitExceeded() {
  Arena a = ArenaCreate();
  HttpRetryPolicy policy = HttpRetryPolicy_Default();
  policy.Enabled = true;
  const i32 statuses[] = {-1, 429, 529};
  for (i32 status : statuses) {
    FakeAttempt attempts[1] = {{status, false, true, true}};
    FakeTransportState state = {};
    state.Attempts = attempts;
    state.Count = 1;
    state.BodyLimitExceeded = true;

    HttpRetryDeps deps = {};
    deps.Transport = FakeTransport;
    deps.TransportUser = &state;
    deps.Sleep = FakeSleep;
    deps.SleepUser = &state;
    deps.Rand = FakeRandZero;

    HttpRequest req = {};
    req.MaxResponseBytes = 32;
    req.BufferNonSuccessBody = true;
    HttpStreamSink sink = {};
    HttpResult result = Http_StreamWithRetryDeps(
        &a, &req, sink, nullptr, &policy, &deps);
    CHECK_ALL(state.Calls == 1, state.SleepCalls == 0,
              result.Status == status, !result.BodyStarted,
              result.BodyLimitExceeded);
  }
  ArenaRelease(&a);
}

void TestRetryLoopHonorsShouldRetryFalse() {
  Arena a = ArenaCreate();
  HttpRetryPolicy policy = HttpRetryPolicy_Default();
  policy.Enabled = true;
  policy.BaseDelayMs = 10;

  FakeAttempt attempts[1] = {{429, false, true, false}};
  FakeTransportState state = {};
  state.Attempts = attempts;
  state.Count = 1;

  HttpRetryDeps deps = {};
  deps.Transport = FakeTransport;
  deps.TransportUser = &state;
  deps.Sleep = FakeSleep;
  deps.SleepUser = &state;
  deps.Rand = FakeRandZero;

  HttpRequest req = {};
  HttpStreamSink sink = {};
  HttpResult r = Http_StreamWithRetryDeps(&a, &req, sink, nullptr, &policy, &deps);
  CHECK_ALL(state.Calls == 1, state.SleepCalls == 0, r.Status == 429);

  ArenaRelease(&a);
}

void TestRetryLoopDisabledPolicyIsPassthrough() {
  Arena a = ArenaCreate();
  HttpRetryPolicy policy = HttpRetryPolicy_Default();

  FakeAttempt attempts[1] = {{500, false, false, false}};
  FakeTransportState state = {};
  state.Attempts = attempts;
  state.Count = 1;

  HttpRetryDeps deps = {};
  deps.Transport = FakeTransport;
  deps.TransportUser = &state;
  deps.Sleep = FakeSleep;
  deps.SleepUser = &state;
  deps.Rand = FakeRandZero;

  HttpRequest req = {};
  HttpStreamSink sink = {};
  HttpResult r = Http_StreamWithRetryDeps(&a, &req, sink, nullptr, &policy, &deps);
  CHECK_ALL(state.Calls == 1, state.SleepCalls == 0, r.Status == 500);

  FakeTransportState state2 = {};
  state2.Attempts = attempts;
  state2.Count = 1;
  deps.TransportUser = &state2;
  deps.SleepUser = &state2;
  HttpResult r2 = Http_StreamWithRetryDeps(&a, &req, sink, nullptr, nullptr, &deps);
  CHECK_ALL(state2.Calls == 1, r2.Status == 500);

  ArenaRelease(&a);
}

void TestRetryLoopAbortDuringBackoffStopsRetrying() {
  Arena a = ArenaCreate();
  HttpRetryPolicy policy = HttpRetryPolicy_Default();
  policy.Enabled = true;
  policy.MaxRetries = 5;
  policy.BaseDelayMs = 1000;

  FakeAttempt attempts[1] = {{500, false, false, false}};
  FakeTransportState state = {};
  state.Attempts = attempts;
  state.Count = 1;

  volatile i32 abort = 0;
  state.AbortAfterSleep = &abort;
  state.AbortAfterSleepCalls = 2;
  policy.EventUser = &state;
  policy.OnEvent = FakeRetryEvent;

  HttpRetryDeps deps = {};
  deps.Transport = FakeTransport;
  deps.TransportUser = &state;
  deps.Sleep = FakeSleep;
  deps.SleepUser = &state;
  deps.Rand = FakeRandZero;

  HttpRequest req = {};
  HttpStreamSink sink = {};
  HttpResult r = Http_StreamWithRetryDeps(&a, &req, sink, &abort, &policy, &deps);

  CHECK_ALL(state.Calls == 1, r.Status == -1,
            StrView_EqCStr(r.Error, "aborted"), state.SleepCalls == 2,
            state.RetryStarts == 1, state.RetryEnds == 1,
            state.RetrySequence == 12);

  ArenaRelease(&a);
}

#if defined(_WIN32)
bool NetInit() {
  static bool up = false;
  if (up) return true;
  WSADATA d;
  if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return false;
  up = true;
  return true;
}
void CloseSock(SOCKET fd) { closesocket(fd); }
#else
bool NetInit() { return true; }
void CloseSock(SOCKET fd) { close(fd); }
#endif

void SendAll(SOCKET c, const char* data, u32 len) {
  u32 off = 0;
  while (off < len) {
    int n = (int)send(c, data + off, (int)(len - off), 0);
    if (n <= 0) break;
    off += (u32)n;
  }
}

SOCKET OpenLoopbackListener(u16* outPort) {
  if (!NetInit()) return INVALID_SOCKET;
  SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd == INVALID_SOCKET) return fd;
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(fd, 4) != 0) {
    CloseSock(fd);
    return INVALID_SOCKET;
  }
  sockaddr_in bound;
  memset(&bound, 0, sizeof(bound));
  clippi_socklen_t bl = sizeof(bound);
  getsockname(fd, (sockaddr*)&bound, &bl);
  *outPort = ntohs(bound.sin_port);
  return fd;
}

struct OneShotServerCtx {
  SOCKET ListenFd;
  StrView RawResponse;
};

void ServeOneRequest(void* arg) {
  OneShotServerCtx* ctx = (OneShotServerCtx*)arg;
  SOCKET c = accept(ctx->ListenFd, nullptr, nullptr);
  if (c == INVALID_SOCKET) return;
  char buf[4096];
  int total = 0;
  for (;;) {
    int n = (int)recv(c, buf + total, (int)(sizeof(buf) - 1 - total), 0);
    if (n <= 0) break;
    total += n;
    buf[total] = '\0';
    if (total >= 4 && strstr(buf, "\r\n\r\n")) break;
    if (total >= (int)sizeof(buf) - 1) break;
  }
  SendAll(c, ctx->RawResponse.Data, ctx->RawResponse.Length);
  CloseSock(c);
}

struct Counter {
  u32 Calls;
  u64 Bytes;
};

bool CountingOnData(void* user, StrView chunk) {
  Counter* c = (Counter*)user;
  c->Calls++;
  c->Bytes += chunk.Length;
  return true;
}

StrView BuildResponse(Arena* a, const char* statusLine, const char* extraHeaders, StrView body) {
  StrBuf b = StrBuf_Make(a, body.Length + 256);
  StrBuf_AppendCStr(&b, statusLine);
  StrBuf_AppendCStr(&b, "\r\n");
  StrBuf_AppendCStr(&b, extraHeaders);
  StrBuf_AppendCStr(&b, "Content-Length: ");
  StrBuf_AppendU64(&b, body.Length);
  StrBuf_AppendCStr(&b, "\r\nConnection: close\r\n\r\n");
  StrBuf_Append(&b, body);
  return StrBuf_View(&b);
}

void TestRealServerBuffersNonSuccessBody() {
  Arena a = ArenaCreate();
  Http_GlobalInit();

  u16 port = 0;
  SOCKET listenFd = OpenLoopbackListener(&port);
  CHECK(listenFd != INVALID_SOCKET);
  if (listenFd == INVALID_SOCKET) {
    ArenaRelease(&a);
    return;
  }

  StrView body = SVL("{\"error\":\"rate limited\"}");
  StrView response = BuildResponse(
      &a, "HTTP/1.1 429 Too Many Requests",
      "Content-Type: application/json\r\nX-Should-Retry: true\r\nRetry-After-Ms: 1234\r\n", body);

  OneShotServerCtx ctx = {listenFd, response};
  Thread server = {};
  CHECK(Thread_Start(&server, ServeOneRequest, &ctx));

  StrBuf url = StrBuf_Make(&a, 64);
  StrBuf_AppendCStr(&url, "http://127.0.0.1:");
  StrBuf_AppendU64(&url, port);
  StrBuf_AppendCStr(&url, "/v1/chat/completions");

  Counter counter = {};
  HttpStreamSink sink = {};
  sink.User = &counter;
  sink.OnData = CountingOnData;

  HttpRequest req = {};
  req.Method = SVL("GET");
  req.Url = StrBuf_View(&url);
  req.BufferNonSuccessBody = true;

  HttpResult r = Http_Stream(&a, &req, sink, nullptr);
  Thread_Join(&server);
  CloseSock(listenFd);

  CHECK_ALL(r.Status == 429, !r.BodyStarted, counter.Calls == 0, counter.Bytes == 0,
            r.Meta.HasShouldRetry, r.Meta.ShouldRetry, r.Meta.RetryAfterMs == 1234,
            StrView_Eq(r.Body, body));

  Http_GlobalShutdown();
  ArenaRelease(&a);
}

void TestRealServerDefaultForwardsNonSuccessBody() {
  Arena a = ArenaCreate();
  Http_GlobalInit();

  u16 port = 0;
  SOCKET listenFd = OpenLoopbackListener(&port);
  CHECK(listenFd != INVALID_SOCKET);
  if (listenFd == INVALID_SOCKET) {
    ArenaRelease(&a);
    return;
  }

  StrView body = SVL("internal error");
  StrView response = BuildResponse(&a, "HTTP/1.1 500 Internal Server Error", "", body);

  OneShotServerCtx ctx = {listenFd, response};
  Thread server = {};
  CHECK(Thread_Start(&server, ServeOneRequest, &ctx));

  StrBuf url = StrBuf_Make(&a, 64);
  StrBuf_AppendCStr(&url, "http://127.0.0.1:");
  StrBuf_AppendU64(&url, port);
  StrBuf_AppendCStr(&url, "/");

  Counter counter = {};
  HttpStreamSink sink = {};
  sink.User = &counter;
  sink.OnData = CountingOnData;

  HttpRequest req = {};
  req.Method = SVL("GET");
  req.Url = StrBuf_View(&url);

  HttpResult streamed = Http_Stream(&a, &req, sink, nullptr);
  Thread_Join(&server);
  CloseSock(listenFd);

  CHECK_ALL(streamed.Status == 500, streamed.BodyStarted,
            counter.Calls > 0, counter.Bytes == body.Length);

  Http_GlobalShutdown();
  ArenaRelease(&a);
}

void TestRealServerSuccessStreamsToSink() {
  Arena a = ArenaCreate();
  Http_GlobalInit();

  u16 port = 0;
  SOCKET listenFd = OpenLoopbackListener(&port);
  CHECK(listenFd != INVALID_SOCKET);
  if (listenFd == INVALID_SOCKET) {
    ArenaRelease(&a);
    return;
  }

  StrView body = SVL("hello streamed body");
  StrView response = BuildResponse(&a, "HTTP/1.1 200 OK", "", body);

  OneShotServerCtx ctx = {listenFd, response};
  Thread server = {};
  CHECK(Thread_Start(&server, ServeOneRequest, &ctx));

  StrBuf url = StrBuf_Make(&a, 64);
  StrBuf_AppendCStr(&url, "http://127.0.0.1:");
  StrBuf_AppendU64(&url, port);
  StrBuf_AppendCStr(&url, "/");

  Counter counter = {};
  HttpStreamSink sink = {};
  sink.User = &counter;
  sink.OnData = CountingOnData;

  HttpRequest req = {};
  req.Method = SVL("GET");
  req.Url = StrBuf_View(&url);
  req.BufferNonSuccessBody = true;

  HttpResult r = Http_Stream(&a, &req, sink, nullptr);
  Thread_Join(&server);
  CloseSock(listenFd);

  CHECK_ALL(r.Status == 200, r.BodyStarted, counter.Calls > 0, counter.Bytes == body.Length,
            r.Body.Length == 0);

  Http_GlobalShutdown();
  ArenaRelease(&a);
}

bool LiveOnData(void* user, StrView chunk) {
  ((Counter*)user)->Bytes += chunk.Length;
  return true;
}

void TestLiveHttp() {
  const char* url = getenv("CLIPPI_TEST_HTTP_URL");
  if (!url) url = "https://example.com";

  Http_GlobalInit();
  Arena a = ArenaCreate();

  HttpRequest req = {};
  req.Method = SV("GET");
  req.Url = SV(url);

  Counter c = {};
  HttpStreamSink sink = {};
  sink.User = &c;
  sink.OnData = LiveOnData;

  HttpResult streamed = Http_Stream(&a, &req, sink, nullptr);
  if (streamed.Status < 200 || streamed.Status >= 400) {
    printf("  http: GET %s failed status=%d err=%.*s\n", url, streamed.Status,
           (int)streamed.Error.Length,
           streamed.Error.Data ? streamed.Error.Data : "");
  }
  CHECK_ALL(streamed.Status >= 200, streamed.Status < 400, c.Bytes > 0);

  HttpResponse fetched = Http_Fetch(&a, &req, nullptr);
  CHECK_ALL(fetched.Status >= 200, fetched.Status < 400,
            fetched.Body.Length > 0);

  ArenaRelease(&a);
  Http_GlobalShutdown();
}

}

void TestHttp() {
  TestStatusLineParsing();
  TestHeaderLineParsing();
  TestHttpStatusIsRetryable();
  TestShouldRetryDecisions();
  TestDelayMsComputation();
  TestRetryLoopStopsAfterRetryableFailures();
  TestRetryLoopNeverRetriesAfterBodyStarted();
  TestRetryLoopNeverRetriesAfterBodyLimitExceeded();
  TestRetryLoopHonorsShouldRetryFalse();
  TestRetryLoopDisabledPolicyIsPassthrough();
  TestRetryLoopAbortDuringBackoffStopsRetrying();
  TestRealServerBuffersNonSuccessBody();
  TestRealServerDefaultForwardsNonSuccessBody();
  TestRealServerSuccessStreamsToSink();
}

void TestHttpLive() {
  TestLiveHttp();
}
