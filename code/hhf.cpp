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

void
hhf_update_and_render(HHFBackBuffer *back_buffer)
{
  LOCAL_PERSIST int x_offset = 0;
  LOCAL_PERSIST int y_offset = 0;
  render_weird_gradient(back_buffer, x_offset, y_offset);
  x_offset += 2;
}
