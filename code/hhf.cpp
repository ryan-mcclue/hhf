// SPDX-License-Identifier: zlib-acknowledgement

#include "hhf.h"

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
output_sound(HHFSoundBuffer *sound_buffer)
{
  int tone_volume = 3000;
  int tone_hz = 256;
  int tone_period = sound_buffer->samples_per_second / tone_hz;
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

void
hhf_update_and_render(HHFBackBuffer *back_buffer, HHFSoundBuffer *sound_buffer, HHFInput *input, HHFMemory *memory)
{
  ASSERT(sizeof(HHFState) <= memory->permanent_size);

  HHFState *state = (HHFState *)memory->permanent;
  if (!memory->is_initialized)
  {
    state->x_offset = 0;
    state->y_offset = 0;
    memory->is_initialized = true;
  }

  // counting how many half transition counts over say half a second gives us
  // whether the user 'dashed'
  for (int controller_i = 0; controller_i < HHF_INPUT_MAX_NUM_CONTROLLERS; ++controller_i)
  {
    HHFInputController controller = input->controllers[controller_i];
    if (controller.is_connected)
    {
      // digital tuning
      if (controller.move_left.ended_down) state->x_offset -= 2;
      if (controller.move_right.ended_down) state->x_offset += 2;
      if (controller.move_up.ended_down) state->y_offset -= 2;
      if (controller.move_down.ended_down) state->y_offset += 2;

      // analog override
      if (controller.is_analog)
      {
      }
    }
  }

  render_weird_gradient(back_buffer, state->x_offset, state->y_offset);

  output_sound(sound_buffer);
}

