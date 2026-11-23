#pragma once
#include "base/basics.h"

#define SHA256_DIGEST_SIZE 32

void Sha256(const byte* data, u32 len, byte out[SHA256_DIGEST_SIZE]);
