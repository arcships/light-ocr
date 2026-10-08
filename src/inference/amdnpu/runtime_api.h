#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// A versioned C boundary keeps IREE symbols and types out of the addon.
// All buffers are borrowed for a call; create/load copy their input bytes.
// Sessions must be destroyed before their engine. Calls are serialized by
// the native backend; an engine/session is not concurrently callable.
typedef struct LightOcrAieApiV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  int (*create)(const uint8_t*, size_t, void**, char*, size_t);
  int (*load)(void*, const uint8_t*, size_t, uint32_t, void**, char*, size_t);
  int (*run)(void*, const float*, size_t, float*, size_t, char*, size_t);
  void (*destroy_session)(void*);
  void (*destroy_engine)(void*);
} LightOcrAieApiV1;

const LightOcrAieApiV1* LightOcrAieGetApi(uint32_t version);
#ifdef __cplusplus
}
#endif
