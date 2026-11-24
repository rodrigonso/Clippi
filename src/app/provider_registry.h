#pragma once
#include "base/arena.h"
#include "base/basics.h"
#include "base/str.h"
#include "llm/model_list.h"
#include "llm/responses.h"
#include "llm/types.h"

struct ProviderSpec;

// Results and their strings are owned by `arena`.
typedef Provider (*ProviderFactoryFn)(Arena* arena, const ProviderSpec* spec);
typedef Model (*ProviderModelFactoryFn)(Arena* arena, const ProviderSpec* spec,
                                        StrView modelId);
typedef ProviderModelList (*ProviderListModelsFn)(Arena* arena,
                                                  const ProviderSpec* spec,
                                                  ProviderAuth auth,
                                                  volatile i32* abort);

enum class ProviderSignIn : u8 { OpenRouterKey, ChatGpt };

struct ProviderSpec {
  StrView Id;
  StrView Name;
  StrView DefaultModel;
  const char* ApiKeyEnvVar;  // null when the provider takes no API key
  ProviderSignIn SignIn;
  const ResponsesEndpoint* Endpoint;
  ProviderFactoryFn CreateProvider;
  ProviderModelFactoryFn CreateModel;
  ProviderListModelsFn ListModels;
  // Idle time after which the vendor has surely evicted the prompt cache; an
  // upper bound, since cold-cache work is wasted if the cache is still warm.
  u32 PromptCacheIdleMinutes;
  // A fast, cheap model in this provider's catalog for permission review.
  StrView DefaultReviewerModel;
};

u32 ProviderRegistry_Count();
const ProviderSpec* ProviderRegistry_At(u32 index);
const ProviderSpec* ProviderRegistry_Find(StrView providerId);

StrView ProviderRegistry_Label(Arena* arena, const ProviderSpec* spec);
