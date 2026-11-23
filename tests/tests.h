#pragma once

extern const char* g_test_executable;

int RunAgentBenchmark(int argc, char** argv);

#define CLIPPI_TEST_SUITE(name, fn, tag, description) void fn();
#include "test_suites.inc"
#undef CLIPPI_TEST_SUITE
