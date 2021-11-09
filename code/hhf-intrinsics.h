// SPDX-License-Identifier: zlib-acknowledgement
#pragma once

// IMPORTANT(Ryan): This is the only place where platform-specific can be included into
// non-specific code

int
least_significant_bit_set(uint val)
{
  int result = 0;

#if defined(__GNUC__) || defined(__GNUG__)
  result = __builtin_ctz();
#endif

  return result;
}
