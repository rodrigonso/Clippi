#pragma once
#include "base/arena.h"
#include "base/array.h"
#include "base/str.h"

#include "app/loader.h"

struct PromptTemplate {
  StrView Name;
  StrView Description;
  StrView ArgumentHint;
  StrView FilePath;
  StrView Content;
};

struct PromptTemplateSet {
  Array<PromptTemplate> Templates;
  Array<LoadDiagnostic> Diagnostics;
};

struct PromptTemplateLoadOptions {
  StrView AgentDirectory;
  StrView WorkingDirectory;
  bool ProjectTrusted;
  bool NoPromptTemplates;
  const StrView* SettingsPaths;
  u32 SettingsPathCount;
  const StrView* ExplicitPaths;
  u32 ExplicitPathCount;
};

// Results are owned by `arena`; the first template to claim a name wins.
void PromptTemplates_Load(Arena* arena, const PromptTemplateLoadOptions* options,
                          PromptTemplateSet* out);

bool PromptTemplates_ParseFile(Arena* arena, StrView filePath, StrView content,
                               PromptTemplate* out);

const PromptTemplate* PromptTemplates_Find(const PromptTemplateSet* templates,
                                           StrView name);

void PromptTemplates_SplitArgs(Arena* arena, StrView argsString,
                               Array<StrView>* out);

StrView PromptTemplates_Substitute(Arena* arena, StrView content,
                                   const StrView* args, u32 argCount);

StrView PromptTemplates_Expand(Arena* arena, const PromptTemplate* tmpl,
                               StrView argsString);
