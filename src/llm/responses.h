#pragma once
#include "base/arena.h"
#include "base/array.h"
#include "base/str.h"
#include "llm/model_list.h"
#include "llm/types.h"
#include "net/http.h"

// The only wire protocol Clippi speaks; the endpoint's dialect selects the ChatGPT backend's layout.

// ChatGptCodex rejects `max_output_tokens` and requires `instructions`.
enum class ResponsesDialect : u8 { OpenRouter, ChatGptCodex };

struct ResponsesEndpoint {
  StrView Id;
  StrView BaseUrl;
  const char* BaseUrlEnvVar;
  ResponsesDialect Dialect;
};

// The backend lists only models a Codex client this new can drive; tracks the Codex CLI release Clippi matches.
#define RESPONSES_CODEX_CLIENT_VERSION "0.169.0"

StrView ResponsesEndpoint_ResolveBaseUrl(Arena* arena,
                                         const ResponsesEndpoint* endpoint);

// A root already ending in "/v1" does not get a second one, since that is how most gateways publish their address.
StrView Responses_Url(Arena* arena, StrView baseUrl, const char* path);

StrView Responses_CodexUrl(Arena* arena, StrView baseUrl, const char* path);

StrBuf Responses_BuildRequest(
    Arena* arena, const Model* model, const LlmContext* context,
    const StreamOptions* options,
    ResponsesDialect dialect = ResponsesDialect::OpenRouter);

// The stream keys every event by `output_index`, so a slot is found by that and nothing else.
struct ResponsesSlot {
  i64 OutputIndex;
  u32 ContentIndex;
  u8 Kind;  // 1 text, 2 reasoning, 3 function call
  StrBuf Value;
  StrView ItemId;
  StrView CallId;
  StrView Name;
  StrView Phase;
  StrView Signature;
  bool Finished;
};

struct ResponsesParse {
  Arena* Mem;
  StrBuf Buffer;
  Message* Msg;
  StreamSink Sink;
  Array<ResponsesSlot> Slots;
  Usage Use;
  StopReason Stop;
  StrView Error;
  bool SawTool;
  bool SawTerminal;
  bool SawError;
  bool Finished;
};

void ResponsesParse_Init(ResponsesParse* state, Arena* arena, Message* message,
                         StreamSink sink);
void ResponsesParse_Feed(ResponsesParse* state, StrView chunk);
// Must be called exactly once, after the last Feed, before the message is read.
void ResponsesParse_Finish(ResponsesParse* state);

// A stream that ends without a terminal event was truncated.
inline bool ResponsesParse_SawTerminal(const ResponsesParse* state) {
  return state && state->SawTerminal;
}

Model Responses_Model(Arena* arena, const ResponsesEndpoint* endpoint,
                      StrView modelId);

bool Responses_ParseModelsResponse(Arena* arena,
                                   const ResponsesEndpoint* endpoint,
                                   StrView response, ProviderModelList* out);

bool Responses_ParseCodexModelsResponse(Arena* arena,
                                        const ResponsesEndpoint* endpoint,
                                        StrView response,
                                        ProviderModelList* out);

typedef HttpResponse (*ResponsesModelsFetchFn)(void* user, Arena* arena,
                                               const HttpRequest* request,
                                               volatile i32* abort);

Provider Responses_Provider();
ProviderModelList Responses_ListModels(Arena* arena,
                                       const ResponsesEndpoint* endpoint,
                                       ProviderAuth auth, volatile i32* abort);
ProviderModelList Responses_ListModelsWithFetch(
    Arena* arena, const ResponsesEndpoint* endpoint, ProviderAuth auth,
    volatile i32* abort, ResponsesModelsFetchFn fetch, void* fetchUser);
