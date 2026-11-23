#pragma once

#include "json/json.h"

struct JsonSchema {
  const JsonValue* Root;
};

// Rejects unsupported behavioral keywords so partial validation is never mistaken for full.
bool JsonSchema_Compile(Arena* arena, StrView schemaJson, JsonSchema* out,
                        StrBuf* err);

// Coerces primitive values using Pi-compatible rules, then validates in place.
bool JsonSchema_Validate(Arena* arena, const JsonSchema* schema,
                         JsonValue* value, StrBuf* err);

