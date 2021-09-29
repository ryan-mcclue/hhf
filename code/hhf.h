// SPDX-License-Identifier: zlib-acknowledgement
#pragma once

struct HHFBackBuffer
{
  // NOTE(Ryan): Memory order: XX RR GG BB
  u8 *memory;
  int width;
  int height;
};
