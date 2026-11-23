#pragma once

#include "app/runtime.h"

// Handlers take the InteractiveRuntime* as their `user` pointer.
AppCommandHandlers RuntimeCommandHandlers(InteractiveRuntime* rt);

bool RuntimeResetSession(InteractiveRuntime* rt, const AppCommandUx* ux);

TuiCommandSpec* RuntimeCommandCatalog(InteractiveRuntime* rt, Arena* arena,
                                      u32* count);

AppCommandHandlerResult HandleMemoryCommand(void* user, const AppCommandUx* ux,
                                            StrView arguments);
AppCommandHandlerResult HandleDreamCommand(void* user, const AppCommandUx* ux,
                                           StrView arguments);
