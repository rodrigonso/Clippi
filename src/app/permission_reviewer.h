#pragma once

#include "agent/permissions.h"
#include "agent/primitives.h"
#include "app/provider_registry.h"

struct ModelPermissionReviewerConfig {
  Provider ProviderImpl;
  Model ModelInfo;
  void* AuthUser;
  AgentResolveAuthFn ResolveAuth;
};

// The tool the reviewer is forced to call; exposed for tests that script it.
constexpr StrView PERMISSION_REVIEWER_DECISION_TOOL = SVL("permission_decision");

// `configured` when set, otherwise the provider's cheap default.
StrView PermissionReviewer_ModelId(const ProviderSpec* spec, StrView configured);

// Retired TypeSafe Jev names are read as "use the provider default".
bool PermissionReviewer_IsRetiredModel(StrView model);

// ModelInfo's strings must outlive the reviewer.
AgentPermissionReviewer ModelPermissionReviewer_Make(
    Arena* arena, const ModelPermissionReviewerConfig* config);

// The reviewer for `spec`'s chat provider; `*outModelId` names the model used.
AgentPermissionReviewer PermissionReviewer_ForProvider(
    Arena* arena, StrView agentDirectory, const ProviderSpec* spec, StrView configured,
    Provider provider, void* authUser, AgentResolveAuthFn resolveAuth,
    StrView* outModelId);
