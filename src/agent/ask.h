#pragma once

#include "base/arena.h"
#include "base/basics.h"
#include "base/str.h"

// Zero-initialized means Unavailable on purpose: a forgotten status must not
// fabricate an answer.
enum class AgentAskStatus : u8 {
  Unavailable,
  Answered,
  Declined,
};

struct AgentAskRequest {
  StrView Question;
  StrView Category;
  const StrView* Choices;
  u32 ChoiceCount;
  // Offers a typed answer beside Choices; such an answer reports
  // ChoiceIndex == ChoiceCount. Off for prompts that dispatch on the index.
  bool AllowFreeText;
};

struct AgentAskResponse {
  AgentAskStatus Status;
  u32 ChoiceIndex;
  // Owned by the arena passed to the call.
  StrView Answer;
};

struct AgentAskUser {
  void* User;
  // False means no user could be asked; a decline is true + Declined and must
  // stay distinct.
  bool (*Ask)(void* user, Arena* arena, const AgentAskRequest* request,
              AgentAskResponse* out);
};

inline AgentAskResponse AgentAskUser_Invoke(const AgentAskUser* ask, Arena* arena,
                                            const AgentAskRequest* request) {
  AgentAskResponse response = {};
  if (!ask || !ask->Ask) return response;
  if (!ask->Ask(ask->User, arena, request, &response)) {
    AgentAskResponse unavailable = {};
    return unavailable;
  }
  return response;
}
