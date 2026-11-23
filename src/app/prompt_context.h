#pragma once

#include "agent/tool.h"
#include "app/prompt_templates.h"
#include "app/skills.h"
#include "base/array.h"
#include "base/str.h"

enum class PromptDiagnosticSeverity : u8 { Warning, Error };

struct PromptDiagnostic {
  PromptDiagnosticSeverity Severity;
  StrView Path;
  StrView Message;
};

struct PromptContextFile {
  StrView Path;
  StrView Content;
};

struct PromptContextOptions {
  StrView WorkingDirectory;
  StrView AgentDirectory;
  const Tool* Tools;
  u32 ToolCount;
  bool NoContextFiles;
  bool NoSkills;
  bool NoPromptTemplates;
  bool ProjectTrusted;
  bool HasExplicitSystemPrompt;
  StrView ExplicitSystemPrompt;
  const StrView* ExplicitAppendPrompts;
  u32 ExplicitAppendPromptCount;
  const StrView* SkillPaths;
  u32 SkillPathCount;
  const StrView* SettingsSkillPaths;
  u32 SettingsSkillPathCount;
  const StrView* PromptTemplatePaths;
  u32 PromptTemplatePathCount;
  const StrView* SettingsPromptTemplatePaths;
  u32 SettingsPromptTemplatePathCount;
  // Empty disables memory, leaving the prompt byte-identical.
  StrView MemoryStore;
};

struct PromptContext {
  Arena Mem;
  bool Initialized;
  StrView SystemPrompt;
  StrView SystemPromptSource;
  StrView AppendPromptSource;
  Array<PromptContextFile> ContextFiles;
  SkillSet Skills;
  Array<StrView> SettingsSkillPaths;
  // Never shown to the model; here so `/reload` swaps them atomically.
  PromptTemplateSet Templates;
  Array<StrView> SettingsPromptTemplatePaths;
  // The block is absent without `read`.
  StrView MemoryPages;
  StrView MemoryBlock;
  u32 MemoryPageCount;
  Array<PromptDiagnostic> Diagnostics;
};

// On failure `out` remains releasable.
bool PromptContext_Build(const PromptContextOptions* options, PromptContext* out,
                         StrBuf* error);
void PromptContext_Release(PromptContext* context);
