// SPDX-License-Identifier: zlib-acknowledgement
#pragma once

#include <stdint.h>

#define INTERNAL static
#define GLOBAL static
#define LOCAL_PERSIST static

typedef unsigned int uint;

typedef uint8_t u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
// NOTE(Ryan): This is to avoid compiler adjusting a value like 1234 to 1 which it
//             would have to do if assigning to a bool.
typedef u32 b32;
typedef float r32;
typedef double r64;

// NOTE(Ryan): This is pre-emption to make it easier to identify what thread we are in
typedef struct HHFThreadContext
{
  int placeholder;
} HHFThreadContext;

typedef struct HHFBackBuffer
{
  // NOTE(Ryan): Memory order: XX RR GG BB
  u8 *memory;
  int width;
  int height;
} HHFBackBuffer;

typedef struct HHFSoundBuffer
{
  int samples_per_second;
  // NOTE(Ryan): Dual channel
  s16 *samples;
  int num_samples;
} HHFSoundBuffer;

typedef struct HHFInputButtonState
{
  int half_transition_count;
  bool ended_down;
} HHFInputButtonState;

#define HHF_INPUT_NUM_CONTROLLER_BUTTONS 12
typedef struct HHFInputController
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
} HHFInputController;

#define HHF_INPUT_MAX_NUM_CONTROLLERS 8
typedef struct HHFInput
{
  r32 frame_dt;

  union
  {
    bool mouse_buttons[3];
    __extension__ struct
    {
      bool mouse_left;
      bool mouse_middle;
      bool mouse_right;
    };
  };
  int mouse_x, mouse_y, mouse_wheel;

  HHFInputController controllers[HHF_INPUT_MAX_NUM_CONTROLLERS];
} HHFInput;

typedef struct HHFMemory
{
  bool is_initialized;

  // NOTE(Ryan): Required to be cleared to zero
  u8 *permanent;
  u64 permanent_size;
  u8 *transient;
  u64 transient_size;
} HHFMemory;

typedef struct HHFPlatformReadFileResult
{
  void *contents;
  u64 size;
  int errno_code;
} HHFPlatformReadFileResult;

typedef struct HHFPlatform
{
  HHFPlatformReadFileResult (*read_entire_file)(HHFThreadContext *thread, char *file_name);
  void (*free_read_file_result)(HHFThreadContext *thread, HHFPlatformReadFileResult *read_result);
  int (*write_entire_file)(HHFThreadContext *thread, char *filename, void *memory, u64 size);
} HHFPlatform;

#if defined(__cplusplus) 
extern "C"
#endif
void
hhf_update_and_render(HHFThreadContext *thread_context, HHFBackBuffer *back_buffer, 
                      HHFSoundBuffer *sound_buffer, HHFInput *input, HHFMemory *memory, 
                      HHFPlatform *platform);
