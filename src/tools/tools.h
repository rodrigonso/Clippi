#pragma once
#include "agent/ask.h"
#include "agent/tool.h"

Tool ReadTool();

struct ShellCaptureIo;

// Re-read every call; mutate only while no tool runs (workers read it).
struct ShellToolContext {
  StrView Directory;
  const ShellCaptureIo* OutputIo;
};

Tool ShellTool(ShellToolContext* context = nullptr);
Tool WriteTool();
Tool EditTool();
Tool LsTool();
Tool FindTool();
Tool GrepTool();

struct AgentPermissionEngine;

struct AskUserToolContext {
  AgentAskUser Ask;
  AgentPermissionEngine* Permissions;
};

Tool AskUserTool(AskUserToolContext* context = nullptr);
