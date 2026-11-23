#pragma once
#include "base/str.h"

// A raw \r, tab, or escape would move the cursor out from under the renderer's cell grid.
// Mirrors pi's getTextOutput() sanitize chain (stripAnsi -> drop control chars -> strip carriage returns).
void Sanitize_Display(StrBuf* out, StrView in);
