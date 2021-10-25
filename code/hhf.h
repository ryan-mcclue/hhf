// SPDX-License-Identifier: zlib-acknowledgement
#pragma once

#include "hhf-platform.h"

// IMPORTANT(Ryan): No need for <cprefix> as don't care about std:: namespace
#include <math.h>
#include <stdio.h>
#include <string.h> 
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>

#define BILLION 1000000000L

#define CLEAR_ASCII_ESCAPE "\033[1;1H\033[2K"

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

inline int
truncate_r32_to_int(r32 val)
{
  return (int)val;
}

