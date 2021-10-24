// SPDX-License-Identifier: zlib-acknowledgement

#include "hhf.h"

struct HHFState
{
  int x_offset;
  int y_offset;

  int player_x;
  int player_y;

  int tone_hz; 
};

INTERNAL void
render_weird_gradient(HHFBackBuffer *back_buffer, int x_offset, int y_offset)
{
  u32 *pixel = (u32 *)back_buffer->memory;
  for (int back_buffer_y = 0; 
        back_buffer_y < back_buffer->height;
        ++back_buffer_y)
  {
    for (int back_buffer_x = 0; 
        back_buffer_x < back_buffer->width;
        ++back_buffer_x)
    {
      u8 red = back_buffer_x + x_offset;
      u8 green = back_buffer_y + y_offset;
      u8 blue = 0x33;
      *pixel++ = red << 16 | green << 8 | blue;
    }
  }
}

INTERNAL void
output_sound(HHFSoundBuffer *sound_buffer, HHFState *state)
{
  int tone_volume = 3000;
  int tone_period = sound_buffer->samples_per_second / state->tone_hz;
  LOCAL_PERSIST r32 tone_t = 0.0f;

  s16 *samples = sound_buffer->samples;
  for (int sample_i = 0; 
      sample_i < sound_buffer->num_samples;
      sample_i++)
  {
    r32 val = sin(tone_t) * tone_volume;
    *samples++ = val;
    *samples++ = val;

    tone_t += ((2.0f * M_PI) / (r32)tone_period);
    // IMPORTANT(Ryan): This is to prevent pitch changing as we lose precision
    if (tone_t > 2.0f * M_PI)
    {
      tone_t -= 2.0f * M_PI;
    }
  }
}

// TODO(Ryan): Why are coordinates in floats? 
// Allows sub-pixel positioning of sprites via interpolation?
INTERNAL void
draw_rect(HHFBackBuffer *back_buffer, r32 x0, r32 y0, r32 x1, r32 x2)
{

}

extern "C" void
hhf_update_and_render(HHFThreadContext *thread_context, HHFBackBuffer *back_buffer, 
                      HHFSoundBuffer *sound_buffer, HHFInput *input, HHFMemory *memory, 
                      HHFPlatform *platform)
{
  ASSERT(sizeof(HHFState) <= memory->permanent_size);

  HHFState *state = (HHFState *)memory->permanent;
  if (!memory->is_initialized)
  {
    state->x_offset = 0;
    state->y_offset = 0;
    state->player_x = 200;
    state->player_y = 200;
    state->tone_hz = 256;
    memory->is_initialized = true;
  }

  // counting how many half transition counts over say half a second gives us
  // whether the user 'dashed'
  for (int controller_i = 0; controller_i < HHF_INPUT_MAX_NUM_CONTROLLERS; ++controller_i)
  {
    // printf("mouse x: %d, mouse y: %d, mouse z: %d\n", input->mouse_x, input->mouse_y, input->mouse_z);
    HHFInputController controller = input->controllers[controller_i];
    if (controller.is_connected)
    {
      // analog override
      if (controller.is_analog)
      {
      }
      else
      {
        // digital tuning
        if (controller.action_left.ended_down) state->x_offset -= 2;
        if (controller.action_right.ended_down) state->x_offset += 2;
        if (controller.action_up.ended_down) state->y_offset -= 2;
        if (controller.action_down.ended_down) state->y_offset += 2;

        if (controller.move_left.ended_down) state->player_x -= 2;
        if (controller.move_right.ended_down) state->player_x += 2;

        if (controller.right_shoulder.ended_down) state->tone_hz += 2;
        if (controller.left_shoulder.ended_down) state->tone_hz -= 2;
      }

    }
  }

  render_weird_gradient(back_buffer, state->x_offset, state->y_offset);

  for (int y = state->player_y; y < state->player_y + 10; ++y)
  {
    for (int x = state->player_x; x < state->player_x + 10; ++x)
    {
      u32 *pixel = (u32 *)back_buffer->memory + x + (y * back_buffer->width);
      *pixel++ = 0xffffffff;
    }
  }

  output_sound(sound_buffer, state);
}

