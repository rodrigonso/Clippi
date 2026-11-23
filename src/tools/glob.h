#pragma once
#include "base/str.h"

bool Glob_Match(StrView pattern, StrView path, bool caseInsensitive);
bool Glob_MatchChecked(StrView pattern, StrView path, bool caseInsensitive,
                       bool* matched);
// Strict, for user patterns; ignore files stay permissive about unmatched [ {.
bool Glob_Validate(StrView pattern, StrBuf* err);
