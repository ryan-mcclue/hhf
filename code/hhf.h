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

void
hhf_update_and_render(HHFBackBuffer *back_buffer, HHFSoundBuffer *sound_buffer);
