// SPDX-License-Identifier: zlib-acknowledgement
#pragma once

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
