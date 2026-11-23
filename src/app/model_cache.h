#pragma once
#include "app/provider_registry.h"
#include "base/arena.h"
#include "base/basics.h"
#include "base/str.h"
#include "llm/types.h"

bool ModelCache_Save(StrView agentDirectory, StrView providerId,
                     const Model* models, u32 count);

Model ModelCache_CreateModel(Arena* arena, StrView agentDirectory,
                             const ProviderSpec* spec, StrView modelId);
