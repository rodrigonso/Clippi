#include <stdio.h>
#include <math.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "app/settings.h"
#include "base/arena.h"
#include "base/config_file.h"
#include "base/str.h"
#include "check.h"

void TestSettings() {
  Arena a = ArenaCreate();
  const char* dir = "clippi_settings_test";
  const char* path = "clippi_settings_test/settings.json";
  remove(path);
#if defined(_WIN32)
  _rmdir(dir);
#else
  rmdir(dir);
#endif

  AppSettings loaded = {};
  CHECK(AppSettings_Load(&a, SV(path), &loaded) == AppSettingsLoadResult::Missing);

  CHECK(ConfigFile_WriteAtomic(&a, SV(path), SVL("{bad")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) == AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(&a, SV(path), SVL("{\"provider\":\"anthropic\"}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) == AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"legacy\"}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Success);
  CHECK_ALL(loaded.DefaultTrust == DefaultProjectTrust::Ask,
            loaded.Profile.Effort == ModelEffort::None,
            loaded.Profile.ContextWindow == 0, loaded.Rounded,
            loaded.PermissionMode == AgentPermissionMode::Auto,
            !loaded.PermissionReviewerModels.Count,
            !loaded.YoloAcknowledged);
  CHECK_ALL(loaded.Compaction.Enabled,
            loaded.Compaction.ReserveTokens ==
                AGENT_COMPACTION_DEFAULT_RESERVE_TOKENS,
            loaded.Compaction.KeepRecentTokens ==
                AGENT_COMPACTION_DEFAULT_KEEP_RECENT_TOKENS);

  CHECK_ALL(loaded.Retry.Enabled,
            loaded.Retry.MaxRetries == AGENT_RETRY_DEFAULT_MAX_RETRIES,
            loaded.Retry.BaseDelayMs == AGENT_RETRY_DEFAULT_BASE_DELAY_MS,
            loaded.Retry.MaxDelayMs == AGENT_RETRY_DEFAULT_MAX_DELAY_MS);

  CHECK_ALL(loaded.TurnLimit.Enabled,
            loaded.TurnLimit.MaxTurns == AGENT_TURN_LIMIT_DEFAULT_MAX_TURNS);

  const char* invalidReviewers[] = {"\"m\"", "null", "[]", "{\"openrouter\":\"\"}",
                                    "{\"openrouter\":42}", "{\"\":\"m\"}"};
  for (u32 i = 0; i < sizeof(invalidReviewers) / sizeof(invalidReviewers[0]);
       i++) {
    StrBuf content = StrBuf_Make(&a, 160);
    StrBuf_AppendCStr(
        &content,
        "{\"provider\":\"openrouter\",\"model\":\"chat\","
        "\"permissionReviewerModels\":");
    StrBuf_AppendCStr(&content, invalidReviewers[i]);
    StrBuf_AppendChar(&content, '}');
    CHECK(ConfigFile_WriteAtomic(&a, SV(path), StrBuf_View(&content)));
    CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
          AppSettingsLoadResult::Malformed);
  }

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"openrouter\",\"model\":\"chat\","
          "\"permissionReviewerModel\":\"jev-latest\",\"permissionReviewerThreshold\":0.95,"
          "\"permissionReviewerModels\":{\"openrouter\":\"jev-latest\"}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) == AppSettingsLoadResult::Success);
  CHECK(!loaded.PermissionReviewerModels.Count);
  CHECK(AppSettings_Save(&a, SV(path), &loaded));
  StrView retired = {};
  CHECK(ConfigFile_ReadAll(&a, SV(path), &retired) == ConfigFileReadResult::Success);
  CHECK_ALL(!StrView_Contains(retired, "permissionReviewer"), !StrView_Contains(retired, "jev"));

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"openrouter\",\"model\":\"chat\","
          "\"permissionReviewerModels\":{\"openrouter\":\"openai/gpt-6-luna\","
          "\"openai-codex\":\"gpt-6-luna\"},"
          "\"permissionMode\":\"ask\",\"yoloAcknowledged\":true,"
          "\"defaultProjectTrust\":\"never\",\"effort\":\"high\","
          "\"contextWindow\":1000000,\"rounded\":false,"
          "\"memory\":false,\"dreamModel\":\"cheap-model\",\"cacheIdleMinutes\":0,"
          "\"compaction\":{\"enabled\":false,\"reserveTokens\":4096,"
          "\"keepRecentTokens\":8192},\"retry\":{\"enabled\":false,"
          "\"maxRetries\":5,\"baseDelayMs\":250,\"maxDelayMs\":3000},"
          "\"turnLimit\":{\"enabled\":false,\"maxTurns\":42},"
          "\"skills\":[\"skills/a\"],\"promptTemplates\":[\"prompts/a\"]}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Success);
  CHECK(AppSettings_Save(&a, SV(path), &loaded));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Success);
  CHECK_ALL(StrView_EqCStr(ProviderModelChoice_Find(&loaded.PermissionReviewerModels,
                                                    SVL("openrouter")),
                           "openai/gpt-6-luna"),
            StrView_EqCStr(ProviderModelChoice_Find(&loaded.PermissionReviewerModels,
                                                    SVL("openai-codex")),
                           "gpt-6-luna"),
            !ProviderModelChoice_Find(&loaded.PermissionReviewerModels, SVL("other")).Length,
            StrView_EqCStr(loaded.Provider, "openrouter"),
            StrView_EqCStr(loaded.Model, "chat"),
            loaded.PermissionMode == AgentPermissionMode::Ask,
            loaded.YoloAcknowledged, !loaded.Rounded, !loaded.Memory,
            StrView_EqCStr(loaded.DreamModel, "cheap-model"),
            loaded.HasCacheIdleMinutes, loaded.CacheIdleMinutes == 0,
            loaded.DefaultTrust == DefaultProjectTrust::Never,
            loaded.Profile.Effort == ModelEffort::High,
            loaded.Profile.ContextWindow == 1000000);
  CHECK_ALL(!loaded.Compaction.Enabled,
            loaded.Compaction.ReserveTokens == 4096,
            loaded.Compaction.KeepRecentTokens == 8192,
            !loaded.Retry.Enabled, loaded.Retry.MaxRetries == 5,
            loaded.Retry.BaseDelayMs == 250, loaded.Retry.MaxDelayMs == 3000,
            !loaded.TurnLimit.Enabled, loaded.TurnLimit.MaxTurns == 42,
            loaded.Skills.Count == 1, loaded.PromptTemplates.Count == 1);
  CHECK_ALL(StrView_EqCStr(loaded.Skills.Data[0], "skills/a"),
            StrView_EqCStr(loaded.PromptTemplates.Data[0], "prompts/a"));

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"legacy\","
          "\"compaction\":{\"enabled\":false}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Success);
  CHECK_ALL(!loaded.Compaction.Enabled, !loaded.HasCacheIdleMinutes,
            loaded.Compaction.ReserveTokens ==
                AGENT_COMPACTION_DEFAULT_RESERVE_TOKENS,
            loaded.Compaction.KeepRecentTokens ==
                AGENT_COMPACTION_DEFAULT_KEEP_RECENT_TOKENS);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\",\"compaction\":8}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);
  const char* badIdle[] = {"-1", "1441", "2.5", "\"10\""};
  for (const char* value : badIdle) {
    StrBuf json = StrBuf_Make(&a, 96);
    StrBuf_AppendCStr(&json, "{\"provider\":\"openrouter\",\"model\":\"m\",\"cacheIdleMinutes\":");
    StrBuf_AppendCStr(&json, value);
    StrBuf_AppendChar(&json, '}');
    CHECK(ConfigFile_WriteAtomic(&a, SV(path), StrBuf_View(&json)));
    CHECK(AppSettings_Load(&a, SV(path), &loaded) == AppSettingsLoadResult::Malformed);
  }
  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"compaction\":{\"enabled\":\"yes\"}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);
  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"compaction\":{\"reserveTokens\":0}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);
  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"compaction\":{\"keepRecentTokens\":2.5}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"effort\":\"extreme\"}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"legacy\","
          "\"retry\":{\"maxRetries\":0}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Success);
  CHECK_ALL(loaded.Retry.Enabled, loaded.Retry.MaxRetries == 0,
            loaded.Retry.BaseDelayMs == AGENT_RETRY_DEFAULT_BASE_DELAY_MS,
            loaded.Retry.MaxDelayMs == AGENT_RETRY_DEFAULT_MAX_DELAY_MS);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"legacy\","
          "\"retry\":{\"enabled\":false,\"baseDelayMs\":500,"
          "\"maxDelayMs\":1500}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Success);
  CHECK_ALL(!loaded.Retry.Enabled,
            loaded.Retry.MaxRetries == AGENT_RETRY_DEFAULT_MAX_RETRIES,
            loaded.Retry.BaseDelayMs == 500, loaded.Retry.MaxDelayMs == 1500);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\",\"retry\":true}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"retry\":{\"maxRetries\":-1}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"retry\":{\"maxRetries\":1.5}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"retry\":{\"enabled\":\"yes\"}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"retry\":{\"baseDelayMs\":0}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"legacy\","
          "\"turnLimit\":{\"maxTurns\":0}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Success);
  CHECK_ALL(loaded.TurnLimit.Enabled, loaded.TurnLimit.MaxTurns == 0,
            !AgentTurnLimitPolicy_IsValid(&loaded.TurnLimit));

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"legacy\","
          "\"turnLimit\":{\"enabled\":false}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Success);
  CHECK_ALL(!loaded.TurnLimit.Enabled,
            loaded.TurnLimit.MaxTurns == AGENT_TURN_LIMIT_DEFAULT_MAX_TURNS);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\",\"turnLimit\":7}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"turnLimit\":{\"maxTurns\":-1}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"turnLimit\":{\"maxTurns\":2.5}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"turnLimit\":{\"enabled\":1}}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"contextWindow\":1.5}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"defaultProjectTrust\":\"sometimes\"}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"rounded\":\"yes\"}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"permissionMode\":\"always\"}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);
  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"permissionMode\":true}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);
  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"permissionReviewerModels\":\"\"}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);
  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"bad\","
          "\"yoloAcknowledged\":\"yes\"}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"provider\":\"anthropic\",\"model\":\"legacy\","
          "\"rounded\":false}")));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) ==
        AppSettingsLoadResult::Success);
  CHECK(!loaded.Rounded);

  AppSettings saved = {};
  saved.Provider = SVL("anthropic");
  saved.Model = SVL("claude-\"test\"");
  saved.DefaultTrust = DefaultProjectTrust::Always;
  saved.Profile.Effort = ModelEffort::High;
  saved.Profile.ContextWindow = 1000000;
  saved.Compaction.Enabled = true;
  saved.Compaction.ReserveTokens = 4096;
  saved.Compaction.KeepRecentTokens = 8192;
  saved.Retry.Enabled = true;
  saved.Retry.MaxRetries = 5;
  saved.Retry.BaseDelayMs = 250;
  saved.Retry.MaxDelayMs = 3000;
  saved.TurnLimit.Enabled = true;
  saved.TurnLimit.MaxTurns = 42;
  saved.PermissionMode = AgentPermissionMode::Yolo;
  saved.PermissionReviewerModels =
      ProviderModelChoice_Set(&a, nullptr, SVL("openrouter"), SVL("openai/gpt-6-luna"));
  saved.YoloAcknowledged = true;
  CHECK(AppSettings_Save(&a, SV(path), &saved));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) == AppSettingsLoadResult::Success);
  CHECK_ALL(StrView_Eq(loaded.Provider, saved.Provider),
            StrView_Eq(loaded.Model, saved.Model),
            loaded.DefaultTrust == DefaultProjectTrust::Always,
  loaded.Profile.Effort == ModelEffort::High,
  loaded.Profile.ContextWindow == 1000000);
  CHECK_ALL(loaded.Compaction.Enabled, loaded.Compaction.ReserveTokens == 4096,
            loaded.Compaction.KeepRecentTokens == 8192, loaded.Rounded);
  CHECK_ALL(loaded.Retry.Enabled, loaded.Retry.MaxRetries == 5,
            loaded.Retry.BaseDelayMs == 250, loaded.Retry.MaxDelayMs == 3000);
  CHECK_ALL(loaded.TurnLimit.Enabled, loaded.TurnLimit.MaxTurns == 42);
  CHECK_ALL(loaded.PermissionMode == AgentPermissionMode::Yolo,
            StrView_EqCStr(ProviderModelChoice_Find(&loaded.PermissionReviewerModels,
                                                    SVL("openrouter")),
                           "openai/gpt-6-luna"),
            loaded.YoloAcknowledged);

  saved.Rounded = false;
  CHECK(AppSettings_Save(&a, SV(path), &saved));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) == AppSettingsLoadResult::Success);
  CHECK(!loaded.Rounded);
  saved.Rounded = true;

  saved.Compaction.Enabled = false;
  CHECK(AppSettings_Save(&a, SV(path), &saved));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) == AppSettingsLoadResult::Success);
  CHECK_ALL(!loaded.Compaction.Enabled, loaded.Compaction.ReserveTokens == 4096,
            loaded.Compaction.KeepRecentTokens == 8192);

  AppSettings zeroed = saved;
  zeroed.Compaction = AgentCompactionPolicy{};
  zeroed.Retry = AgentRetryPolicy{};
  zeroed.TurnLimit = AgentTurnLimitPolicy{};
  CHECK(AppSettings_Save(&a, SV(path), &zeroed));
  CHECK(AppSettings_Load(&a, SV(path), &loaded) == AppSettingsLoadResult::Success);
  CHECK_ALL(!loaded.Compaction.Enabled,
            loaded.Compaction.ReserveTokens ==
                AGENT_COMPACTION_DEFAULT_RESERVE_TOKENS,
            loaded.Compaction.KeepRecentTokens ==
                AGENT_COMPACTION_DEFAULT_KEEP_RECENT_TOKENS);
  CHECK_ALL(!loaded.Retry.Enabled, loaded.Retry.MaxRetries == 0,
            loaded.Retry.BaseDelayMs == AGENT_RETRY_DEFAULT_BASE_DELAY_MS,
            loaded.Retry.MaxDelayMs == AGENT_RETRY_DEFAULT_MAX_DELAY_MS);
  CHECK_ALL(!loaded.TurnLimit.Enabled, loaded.TurnLimit.MaxTurns == 0);

#if !defined(_WIN32)
  struct stat st = {};
  CHECK(stat(path, &st) == 0);
  CHECK((st.st_mode & 0777) == 0600);
#endif

  CHECK_ALL(!AppSettings_Save(&a, SV(dir), &saved),
            AppSettings_Load(&a, SV(dir), &loaded) ==
                AppSettingsLoadResult::ReadFailure);

  remove(path);
#if defined(_WIN32)
  _rmdir(dir);
#else
  rmdir(dir);
#endif
  ArenaRelease(&a);
}
