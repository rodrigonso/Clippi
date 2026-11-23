#pragma once

#include "app/extensions.h"
#include "app/runtime.h"

// Deliberately lossy watch; machine output uses the synchronous observer below.
struct HarnessEventPump {
  AgentHarnessWatch Watch;
  AgentHarness* Harness;
  Tui* TuiView;
  HarnessEventSink Sink;
  AppExtensions* Extensions;
  Arena CompletionMem;
  Array<StrView> LiveToolEnds;
  Thread Worker;
  Mutex WatchGuard;
  bool WatchGuardInitialized;
  bool StopRequested;
  bool Started;
};

// Every event reaches `extensions` before the sink sees it.
SessionResult StartHarnessEventPump(AgentHarness* harness,
                                    HarnessEventSink sink, Tui* tuiView,
                                    HarnessEventPump* pump,
                                    AppExtensions* extensions = nullptr);
void StopHarnessEventPump(HarnessEventPump* pump);

struct HarnessJsonWriter {
  void* User;
  // Must consume the whole line, including flushing, before returning.
  bool (*Write)(void* user, StrView line);
};

struct HarnessJsonOutput {
  AgentHarness* Harness;
  AppExtensions* Extensions;
  Arena Scratch;
  HarnessJsonWriter Writer;
  SessionResult Failure;
  bool Started;
};

// Start/finish only while idle.
SessionResult StartHarnessJsonOutput(AgentHarness* harness,
                                     HarnessJsonOutput* output,
                                     HarnessJsonWriter writer = {},
                                     AppExtensions* extensions = nullptr);
SessionResult FinishHarnessJsonOutput(HarnessJsonOutput* output,
                                      SessionResult result);

void ReplSink(void* user, const AgentHarnessEvent* ev);

void ReportHarnessResult(SessionResult result);
