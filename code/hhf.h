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

void
hhf_update_and_render(HHFBackBuffer *back_buffer, HHFSoundBuffer *sound_buffer,
                      HHFInput *input);
