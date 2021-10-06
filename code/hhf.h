// SPDX-License-Identifier: zlib-acknowledgement
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring> 
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <climits>
#include <cinttypes>

#define INTERNAL static
#define GLOBAL static
#define LOCAL_PERSIST static
#define BILLION 1000000000L

typedef unsigned int uint;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
// NOTE(Ryan): This is to avoid compiler adjusting a value like 1234 to 1 which it
//             would have to do if assigning to a bool.
typedef u32 b32;
typedef float r32;
typedef double r64;

#if defined(HHF_SLOW)
INTERNAL void __bp(char const *msg)
{ 
  if (msg != NULL) printf("BP: %s\n", msg);
  return; 
}
INTERNAL void __ebp(char const *msg)
{ 
  char *errno_msg = strerror(errno);
  if (msg != NULL) printf("EBP: %s (%s)\n", msg, errno_msg); 
  return;
}
#define BP(msg) __bp(msg)
#define EBP(msg) __ebp(msg)
#define ASSERT(cond) if (!(cond)) {BP("ASSERT");}
#else
#define BP(msg)
#define EBP(msg)
#define ASSERT(cond)
#endif

#define ARRAY_LEN(arr) \
  (sizeof(arr)/sizeof(arr[0]))

#define KILOBYTES(n) \
  ((n) * 1024UL)
#define MEGABYTES(n) \
  ((n) * KILOBYTES(n))
#define GIGABYTES(n) \
  ((n) * MEGABYTES(n))
#define TERABYTES(n) \
  ((n) * GIGABYTES(n))

inline u32
safe_truncate_u64(u64 val)
{
  ASSERT(val <= UINT32_MAX);
  return (u32)val;
}

struct HHFPlatformReadFileResult
{
  void *contents;
  size_t size;
  int errno_code;
};

#if defined(HHF_INTERNAL)
void
hhf_platform_free_file_memory(HHFPlatformReadFileResult *file_result);

HHFPlatformReadFileResult
hhf_platform_read_entire_file(char const *file_name);

int
hhf_platform_write_entire_file(char const *file_name, size_t size, void *memory);
#endif

struct HHFBackBuffer
{
  // NOTE(Ryan): Memory order: XX RR GG BB
  u8 *memory;
  int width;
  int height;
};

struct HHFSoundBuffer
{
  int samples_per_second;
  // NOTE(Ryan): Dual channel
  s16 *samples;
  int num_samples;
};

struct HHFInputButtonState
{
  int half_transition_count;
  bool ended_down;
};

#define HHF_INPUT_NUM_CONTROLLER_BUTTONS 6
struct HHFInputController
{
  // TODO(Ryan): Insert timing values here
  bool is_analog;

  r32 min_x, min_y, max_x, max_y;
  r32 start_x, start_y, end_x, end_y;

  union
  {
    HHFInputButtonState buttons[HHF_INPUT_NUM_CONTROLLER_BUTTONS];
    struct
    {
      HHFInputButtonState up;
      HHFInputButtonState down;
      HHFInputButtonState left;
      HHFInputButtonState right;
      HHFInputButtonState left_shoulder;
      HHFInputButtonState right_shoulder;
    };
  };
};

#define HHF_INPUT_MAX_NUM_CONTROLLERS 4
struct HHFInput
{
  HHFInputController controllers[HHF_INPUT_MAX_NUM_CONTROLLERS];
};

struct HHFMemory
{
  bool is_initialized;

  // NOTE(Ryan): Required to be cleared to zero
  u8 *permanent;
  u64 permanent_size;
  u8 *transient;
  u64 transient_size;
};

void
hhf_update_and_render(HHFBackBuffer *back_buffer, HHFSoundBuffer *sound_buffer,
                      HHFInput *input, HHFMemory *memory);

// TODO(Ryan): The platform layer does not need to know about this
struct HHFState
{
  int x_offset;
  int y_offset;
};
