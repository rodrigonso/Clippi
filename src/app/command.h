#pragma once
#include "base/str.h"

enum class AppCommandKind : u8 {
  NotCommand,
  Auth,
  Model,
  Clear,
  Session,
  Name,
  Sessions,
  Resume,
  Tree,
  Checkout,
  Fork,
  Compact,
  Trust,
  Permissions,
  Reload,
  Memory,
  Dream,
  Debug,
  Rounded,
  Skill,
  Exit,
  Unknown,
};

enum class AppCommandArgumentPolicy : u8 {
  None,
  Optional,
  Required,
};

struct AppCommandSpec {
  AppCommandKind Kind;
  StrView Name;
  StrView Alias;
  StrView Description;
  StrView Usage;
  AppCommandArgumentPolicy Arguments;
};

u32 AppCommandSpec_Count();
const AppCommandSpec* AppCommandSpec_At(u32 index);
StrView AppCommand_Usage(AppCommandKind kind);

struct AppCommand {
  AppCommandKind Kind;
  StrView Arguments;
  StrView SkillName;
  StrView Name;
};

// Views point into `input`.
AppCommand AppCommand_Parse(StrView input);

enum class AppLoopAction : u8 {
  Continue,
  ResetSession,
  Exit,
};

enum class AppCommandHandlerStatus : u8 {
  Success,
  Cancelled,
  Unsupported,
  Failed,
};

struct AppCommandHandlerResult {
  AppCommandHandlerStatus Status;
  StrView Message;
};

struct AppCommandUx {
  void* User;
  void (*Notice)(void* user, StrView message);
  void (*Error)(void* user, StrView message);
};

typedef AppCommandHandlerResult (*AppCommandHandlerFn)(void* user, const AppCommandUx* ux);
typedef AppCommandHandlerResult (*AppCommandArgHandlerFn)(
    void* user, const AppCommandUx* ux, StrView arguments);
typedef AppCommandHandlerResult (*AppCommandSkillHandlerFn)(
    void* user, const AppCommandUx* ux, StrView name, StrView arguments);
typedef bool (*AppCommandTemplateHandlerFn)(void* user, const AppCommandUx* ux,
                                            StrView name, StrView arguments,
                                            AppCommandHandlerResult* out);

struct AppCommandHandlers {
  void* User;
  AppCommandHandlerFn Auth;
  AppCommandHandlerFn Model;
  AppCommandArgHandlerFn Session;
  AppCommandArgHandlerFn Name;
  AppCommandArgHandlerFn Sessions;
  AppCommandArgHandlerFn Resume;
  AppCommandArgHandlerFn Tree;
  AppCommandArgHandlerFn Checkout;
  AppCommandArgHandlerFn Fork;
  AppCommandArgHandlerFn Compact;
  AppCommandArgHandlerFn Trust;
  AppCommandArgHandlerFn Permissions;
  AppCommandArgHandlerFn Reload;
  AppCommandArgHandlerFn Memory;
  AppCommandArgHandlerFn Dream;
  AppCommandArgHandlerFn Debug;
  AppCommandArgHandlerFn Rounded;
  AppCommandSkillHandlerFn Skill;
  AppCommandTemplateHandlerFn PromptTemplate;
};

struct AppCommandDispatchResult {
  // Every slash input, even unknown, is handled and never sent to the model.
  bool Handled;
  AppLoopAction Action;
};

AppCommandDispatchResult AppCommand_Dispatch(StrView input, const AppCommandHandlers* handlers,
                                             const AppCommandUx* ux);
