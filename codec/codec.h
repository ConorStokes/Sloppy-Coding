#pragma once

#include <stdint.h>
#include <stddef.h>


size_t Encode( uint8_t initialDictionary, const uint8_t* data, uint64_t dataSize, uint64_t* out );

void Decode( uint8_t initialDictionary, const uint64_t* compressed, uint64_t uncompressedSize, uint8_t* outBuffer );
