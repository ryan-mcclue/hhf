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
// NOTE(Ryan): Could use this to catch out of bounds array 
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

#define HHF_INPUT_NUM_CONTROLLER_BUTTONS 12
struct HHFInputController
{
  bool is_connected;
  bool is_analog;

  r32 average_stick_x, average_stick_y;

  union
  {
    HHFInputButtonState buttons[HHF_INPUT_NUM_CONTROLLER_BUTTONS];
    // IMPORTANT(Ryan): Ignore -Wpedantic to allow anonymous structs
    __extension__ struct
    {
      HHFInputButtonState move_up;
      HHFInputButtonState move_down;
      HHFInputButtonState move_left;
      HHFInputButtonState move_right;

      HHFInputButtonState action_up;
      HHFInputButtonState action_down;
      HHFInputButtonState action_left;
      HHFInputButtonState action_right;

      HHFInputButtonState left_shoulder;
      HHFInputButtonState right_shoulder;

      HHFInputButtonState back;
      HHFInputButtonState start;

      // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
      // IMPORTANT(Ryan): Add all buttons above here
      HHFInputButtonState __TERMINATOR__;
    };
  };
};

#define HHF_INPUT_MAX_NUM_CONTROLLERS 8
struct HHFInput
{
  // TODO(Ryan): Insert timing values here
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


struct HHFPlatformReadFileResult
{
  void *contents;
  size_t size;
  int errno_code;
};

struct HHFPlatform
{
  HHFPlatformReadFileResult (*read_entire_file)(char *file_name);
  void (*free_read_file_result)(HHFPlatformReadFileResult *read_result);
  int (*write_entire_file)(char *filename, void *memory, size_t size);
};

void
hhf_update_and_render(HHFBackBuffer *back_buffer, HHFSoundBuffer *sound_buffer,
                      HHFInput *input, HHFMemory *memory, HHFPlatform *platform);
