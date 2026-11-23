#pragma once
#include "base/arena.h"
#include "base/basics.h"
#include "base/str.h"

// Input to Json_Parse must be null-terminated at [Length] (StrBuf guarantees this).

enum class JsonKind : u8 { Null, Bool, Number, String, Array, Object };

struct JsonValue;

struct JsonMember {
  StrView Key;
  JsonValue* Value;
};

struct JsonValue {
  JsonKind Kind;
  bool Bool;
  f64 Number;
  StrView Str;
  JsonValue** Items;
  u32 Count;
  JsonMember* Members;
  u32 MemberCount;
};

// Anything walked recursively (clone, compare, write, schema validation) inherits this bound.
constexpr u32 JSON_MAX_DEPTH = 256;

JsonValue* Json_Parse(Arena* arena, StrView text);

inline JsonValue* Json_ParseCopy(Arena* arena, StrView text) {
  StrBuf input = StrBuf_Make(arena, text.Length + 1);
  StrBuf_Append(&input, text);
  return Json_Parse(arena, StrBuf_View(&input));
}
JsonValue* Json_Clone(Arena* arena, const JsonValue* value);
bool Json_Equals(const JsonValue* a, const JsonValue* b);

JsonValue* Json_Get(const JsonValue* obj, const char* key);
StrView Json_GetStr(const JsonValue* obj, const char* key);
f64 Json_GetNum(const JsonValue* obj, const char* key, f64 fallback);
i64 Json_GetInt(const JsonValue* obj, const char* key, i64 fallback);
bool Json_GetBool(const JsonValue* obj, const char* key, bool fallback);

inline bool Json_IsObject(const JsonValue* v) { return v && v->Kind == JsonKind::Object; }
inline bool Json_IsArray(const JsonValue* v) { return v && v->Kind == JsonKind::Array; }
inline bool Json_IsString(const JsonValue* v) { return v && v->Kind == JsonKind::String; }

u32 Json_FormatNumber(char* dst, u32 cap, f64 value);
void Json_WriteString(StrBuf* b, StrView s);
void Json_WriteValue(StrBuf* b, const JsonValue* value);

// Tracks only whether a comma is due; nesting is the caller's business.
struct JsonWriter {
  StrBuf* Out;
  bool First;
};

inline void JsonWrite_Begin(JsonWriter* w, StrBuf* out) {
  w->Out = out;
  w->First = true;
  StrBuf_AppendChar(out, '{');
}

inline void JsonWrite_Key(JsonWriter* w, const char* key) {
  if (!w->First) StrBuf_AppendChar(w->Out, ',');
  w->First = false;
  Json_WriteString(w->Out, SV(key));
  StrBuf_AppendChar(w->Out, ':');
}

inline void JsonWrite_End(JsonWriter* w) { StrBuf_AppendChar(w->Out, '}'); }

inline void JsonWrite_String(JsonWriter* w, const char* key, StrView value) {
  JsonWrite_Key(w, key);
  Json_WriteString(w->Out, value);
}

inline void JsonWrite_Bool(JsonWriter* w, const char* key, bool value) {
  JsonWrite_Key(w, key);
  StrBuf_AppendCStr(w->Out, value ? "true" : "false");
}

inline void JsonWrite_U64(JsonWriter* w, const char* key, u64 value) {
  JsonWrite_Key(w, key);
  StrBuf_AppendU64(w->Out, value);
}
