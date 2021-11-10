// SPDX-License-Identifier: zlib-acknowledgement
#pragma once

// IMPORTANT(Ryan): There can become a proliferation of constructors as once you define one,
// C++ forces you to use it, e.g. now I have to define a null constructor, etc.
// In reality, {} syntax gives all I need without having to type more

union V2
{
  __extension__ struct 
  {
    r32 x;
    r32 y;
  };
  r32 e[2];
  //r32 &operator[](int index) { return (&x)[index]; }
  // v2{1, 2}
};

// NOTE(Ryan): Using const reference to avoid coppying and allow binding to rvalues,
// e.g. V2 v = (a + b + c)
V2 operator*(r32 a, const V2 &b)
{
  V2 result = {};

  result.x = a * b.x;
  result.y = a * b.y;

  return result;
}

V2 operator*(const V2 &b, r32 a)
{
  V2 result = {};

  result = a * b;

  return result;
}

V2 operator*=(V2 &a, r32 b)
{
  a = b * a;

  return a;
}

V2 operator-(const V2 &a)
{
  V2 result = {};

  result.x = -a.x;
  result.y = -a.y;

  return result;
}

V2 operator+(const V2 &a, const V2 &b)
{
  V2 result = {};

  result.x = a.x + b.x;
  result.y = a.y + b.y;

  return result;
}

V2 operator+=(V2 &a, const V2 &b)
{
  a = a + b;

  return a;
}

V2 operator-(const V2 &a, const V2 &b)
{
  V2 result = {};

  result.x = a.x - b.x;
  result.y = a.y - b.y;

  return result;
}

V2 operator-=(V2 &a, const V2 &b)
{
  a = a - b;

  return a;
}
