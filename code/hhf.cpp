// SPDX-License-Identifier: zlib-acknowledgement

#include "hhf.h"
#include "hhf-math.h"

#include <time.h>

struct TileMapDifference
{
  V2 dxy;
  r32 dz;
};

struct TileChunk
{
  u32 *tiles;
};

struct TileChunkPosition
{
  u32 tile_chunk_x;
  u32 tile_chunk_y;
  u32 tile_chunk_z;

  u32 tile_x;
  u32 tile_y;
};

struct TileMap
{
  uint chunk_mask;
  uint chunk_shift;
  uint chunk_dim;

  r32 tile_side_in_metres;

  int num_tile_chunks_x;
  int num_tile_chunks_y;
  int num_tile_chunks_z;

  TileChunk *chunks;
};

struct World
{
  TileMap *tile_map;
};

struct TileMapPosition
{
  u32 abs_tile_x;
  u32 abs_tile_y;
  u32 abs_tile_z;

// IMPORTANT(Ryan): We want at least 8bits for sub-pixel positioning to perform anti-aliasing
// Therefore, only using r32 does not have enough data in mantissa for a large world
  // relative to tile
  V2 offset;
};

struct MemoryArena
{
  u8 *base;
  size_t size;
  size_t used;
};

INTERNAL void
initialise_memory_arena(MemoryArena *arena, size_t size, void *mem)
{
  arena->base = (u8 *)mem;
  arena->size = size;
  arena->used = 0;
}

// NOTE(Ryan): Generally use #defines over globals for flexibility in debug code
#define MEMORY_RESERVE_STRUCT(arena, struct_name) \
  (struct_name *)(obtain_mem(arena, sizeof(struct_name)))
#define MEMORY_RESERVE_ARRAY(arena, len, elem) \
  (elem *)(obtain_mem(arena, (len) * sizeof(elem)))

INTERNAL void *
obtain_mem(MemoryArena *arena, size_t size)
{
  void *result = NULL;
  ASSERT(arena->used + size < arena->size);

  result = (u8 *)arena->base + arena->used;
  arena->used += size;

  return result;
}

struct LoadedBitmap
{
  int width;
  int height;
  u32 *pixels;
};

struct PlayerBitmap
{
  int align_x, align_y;
  LoadedBitmap head;
  LoadedBitmap torso;
  LoadedBitmap legs;
};

struct State
{
  MemoryArena world_arena;
  World *world;

  LoadedBitmap backdrop;
  PlayerBitmap player_bitmaps[4];
  int player_facing_direction;

  TileMapPosition camera_pos;

  TileMapPosition player_pos;
  V2 dplayer_pos;
};

#if 0
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
      // IMPORTANT(Ryan): Compute with native 32bit, store by bitpacking
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
#endif

// TODO(Ryan): Why are coordinates in floats? 
// Allows sub-pixel positioning of sprites via interpolation?
// What is sub-pixel? It makes movement smoother
// NOTE(Ryan): We use floats for speed, ease in operations (blending), flexibility (normalisation)
INTERNAL void
draw_rect(HHFBackBuffer *back_buffer, V2 min, V2 max, r32 r, r32 g, r32 b)
{
  // NOTE(Ryan): Coordinates [x0, x1)
  int min_x = roundf(min.x); // _mm_cvtss_si32(_mm_set_ss(x0));
  int min_y = roundf(min.y);
  int max_x = roundf(max.x);
  int max_y = roundf(max.y);

  if (min_x < 0) min_x = 0;
  if (min_x >= back_buffer->width) min_x = back_buffer->width;
  if (max_x < 0) max_x = 0;
  if (max_x >= back_buffer->width) max_x = back_buffer->width;

  if (min_y < 0) min_y = 0;
  if (min_y >= back_buffer->height) min_y = back_buffer->height;
  if (max_y < 0) max_y = 0;
  if (max_y >= back_buffer->height) max_y = back_buffer->height;

  u32 colour = (u32)roundf(r * 255.0f) << 16 | 
               (u32)roundf(g * 255.0f) << 8 | 
               (u32)roundf(b * 255.0f);

  for (int y = min_y; y < max_y; ++y)
  {
    for (int x = min_x; x < max_x; ++x)
    {
      u32 *pixel = (u32 *)back_buffer->memory + x + (y * back_buffer->width);
      *pixel = colour;
    }
  }

}


INTERNAL TileChunk *
get_tile_chunk(TileMap *tile_map, int tile_chunk_x, int tile_chunk_y, int tile_chunk_z)
{
  TileChunk *result = NULL;

  if (tile_chunk_x >= 0 && tile_chunk_x < tile_map->num_tile_chunks_x && 
      tile_chunk_y >=0 && tile_chunk_y < tile_map->num_tile_chunks_y &&
      tile_chunk_z >=0 && tile_chunk_z < tile_map->num_tile_chunks_z)
  {
    result = &tile_map->chunks[
               tile_chunk_z * tile_map->num_tile_chunks_y * tile_map->num_tile_chunks_x + 
               tile_chunk_y * tile_map->num_tile_chunks_x + 
               tile_chunk_x];
  }

  return result;
}

INTERNAL void
recanonicalise_coord(TileMap *tile_map, u32 *tile_coord, r32 *tile_rel)
{
  // IMPORTANT(Ryan): floor if storing offset from corner, round as offset from centre
  int offset = (int)roundf(*tile_rel / tile_map->tile_side_in_metres);
  *tile_coord += offset;
  // TODO(Ryan): If tile_rel is a very small negative, we will wrap back 
  *tile_rel -= (offset * tile_map->tile_side_in_metres);
}

INTERNAL void
recanonicalise_position(TileMap *tile_map, TileMapPosition *pos)
{
  recanonicalise_coord(tile_map, &pos->abs_tile_x, &pos->offset.x);
  recanonicalise_coord(tile_map, &pos->abs_tile_y, &pos->offset.y);
}

INTERNAL TileChunkPosition
get_tile_chunk_position(TileMap *tile_map, u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z)
{
  TileChunkPosition result = {};

  result.tile_chunk_x = abs_tile_x >> tile_map->chunk_shift;
  result.tile_chunk_y = abs_tile_y >> tile_map->chunk_shift;
  result.tile_chunk_z = abs_tile_z;
  result.tile_x = abs_tile_x & tile_map->chunk_mask;
  result.tile_y = abs_tile_y & tile_map->chunk_mask;

  return result;
}

INTERNAL u32
get_tile_value_unchecked(TileMap *tile_map, TileChunk *tile_chunk, u32 tile_x, u32 tile_y)
{
  u32 result = 0;

  result = tile_chunk->tiles[tile_y * tile_map->chunk_dim + tile_x];

  return result;
}

INTERNAL u32
get_tile_value(TileMap *tile_map, TileChunk *tile_chunk, u32 tile_x, u32 tile_y)
{
  u32 result = 0;

  if (tile_chunk != NULL && tile_chunk->tiles != NULL)
  {
    result = get_tile_value_unchecked(tile_map, tile_chunk, tile_x, tile_y);
  }

  return result;
}

INTERNAL u32
get_tile_value(TileMap *tile_map, u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z)
{
  u32 result = 0;

  TileChunkPosition tile_chunk_pos = get_tile_chunk_position(tile_map, abs_tile_x, abs_tile_y,
                                                             abs_tile_z);
  TileChunk *tile_chunk = get_tile_chunk(tile_map, tile_chunk_pos.tile_chunk_x, 
                                          tile_chunk_pos.tile_chunk_y,
                                          tile_chunk_pos.tile_chunk_z);
  result = get_tile_value(tile_map, tile_chunk, tile_chunk_pos.tile_x, tile_chunk_pos.tile_y);

  return result;
}

INTERNAL u32
get_tile_value(TileMap *tile_map, TileMapPosition *pos)
{
  u32 result = 0;

  TileChunkPosition tile_chunk_pos = get_tile_chunk_position(tile_map, pos->abs_tile_x, 
                                                             pos->abs_tile_y,
                                                             pos->abs_tile_z);
  TileChunk *tile_chunk = get_tile_chunk(tile_map, tile_chunk_pos.tile_chunk_x, 
                                          tile_chunk_pos.tile_chunk_y,
                                          tile_chunk_pos.tile_chunk_z);
  result = get_tile_value(tile_map, tile_chunk, tile_chunk_pos.tile_x, tile_chunk_pos.tile_y);

  return result;
}

INTERNAL bool
are_on_same_tile(TileMapPosition *pos1, TileMapPosition *pos2)
{
  bool result = false;

  result = (pos1->abs_tile_x == pos2->abs_tile_x && 
            pos1->abs_tile_y == pos2->abs_tile_y && 
            pos1->abs_tile_z == pos2->abs_tile_z);

  return result;
}
 
INTERNAL bool
is_tile_map_point_empty(TileMap *tile_map, TileMapPosition *pos)
{
  bool result = false;

  u32 tile_value = get_tile_value(tile_map, pos->abs_tile_x, pos->abs_tile_y, pos->abs_tile_z);
  result = (tile_value == 1 || tile_value == 3 || tile_value == 4);

  return result;
}


INTERNAL void
set_tile_value(TileMap *tile_map, TileChunk *tile_chunk, int tile_x, int tile_y, u32 value)
{
  tile_chunk->tiles[tile_map->chunk_dim * tile_y + tile_x] = value;
}

INTERNAL void
set_tile_value(MemoryArena *arena, TileMap *tile_map, u32 abs_tile_x, u32 abs_tile_y, 
               u32 abs_tile_z, u32 value)
{
  TileChunkPosition tile_chunk_pos = get_tile_chunk_position(tile_map, abs_tile_x, abs_tile_y, 
                                                             abs_tile_z);
  TileChunk *tile_chunk = get_tile_chunk(tile_map, tile_chunk_pos.tile_chunk_x, 
                                         tile_chunk_pos.tile_chunk_y, 
                                         tile_chunk_pos.tile_chunk_z);
  ASSERT(tile_chunk != NULL);

  if (tile_chunk->tiles == NULL)
  {
    int tile_chunk_size = tile_map->chunk_dim * tile_map->chunk_dim;
    tile_chunk->tiles = MEMORY_RESERVE_ARRAY(arena, tile_chunk_size, u32);
    for (int tile_i = 0; tile_i < tile_chunk_size; ++tile_i)
    {
      tile_chunk->tiles[tile_i] = 1;
    }
  }

  set_tile_value(tile_map, tile_chunk, tile_chunk_pos.tile_x, tile_chunk_pos.tile_y, value);
}

INTERNAL TileMapDifference
subtract(TileMap *tile_map, TileMapPosition *pos1, TileMapPosition *pos2)
{
  TileMapDifference result = {};

  // IMPORTANT(Ryan): Best to use explicit casts when working with floating point to handle
  // cases where working with unsigned may wrap around
  V2 dtile_xy = {
    (r32)pos1->abs_tile_x - (r32)pos2->abs_tile_x,
    (r32)pos1->abs_tile_y - (r32)pos2->abs_tile_y,
  };
  r32 dtile_z = (r32)pos1->abs_tile_z - (r32)pos2->abs_tile_z;

  result.dxy = tile_map->tile_side_in_metres * dtile_xy + (pos1->offset - pos2->offset);
  result.dz = tile_map->tile_side_in_metres * dtile_z;

  return result;
}


struct BitmapHeader
{
  // TODO(Ryan): Will pack to largest size in struct?
  u16 signature;
  u32 file_size;
  u32 reserved;
  u32 data_offset;
  u32 size;
  u32 width;
  u32 height;
  u16 planes;
  u16 bits_per_pixel;
  u32 compression;
  u32 size_of_bitmap;
  u32 horz_resolution;
  u32 vert_resolution;
  u32 colors_used;
  u32 colors_important;

  u32 red_mask;
  u32 green_mask;
  u32 blue_mask;
} __attribute__((packed));


INTERNAL void
draw_bmp(HHFBackBuffer *back_buffer, LoadedBitmap *bitmap, r32 x, r32 y,
         int align_x = 0, int align_y = 0)
{
  x -= (r32)align_x;
  y -= (r32)align_y;

  int min_x = (u32)roundf(x);
  int max_x = (u32)roundf(x + bitmap->width);
  int min_y = (u32)roundf(y);
  int max_y = (u32)roundf(y + bitmap->height);

  int offset_x = 0;
  if (min_x < 0) 
  {
    offset_x = -min_x;
    min_x = 0;
  }
  int offset_y = 0;
  if (min_y < 0) 
  {
    offset_y = -min_y;
    min_y = 0;
  }

  if (max_x > back_buffer->width) max_x = back_buffer->width;
  if (max_y > back_buffer->height) max_y = back_buffer->height;

  u32 *bitmap_row = (u32 *)bitmap->pixels + (bitmap->width * (bitmap->height - 1));
  bitmap_row += -(bitmap->width * offset_y) + offset_x;
  u32 *buffer_row = (u32 *)back_buffer->memory + (back_buffer->width * min_y + min_x);
  for (int y = min_y; y < max_y; ++y)
  {
    u32 *buffer_cursor = buffer_row;
    u32 *pixel_cursor = bitmap_row;

    for (int x = min_x; x < max_x; ++x)
    {
      // TODO(Ryan): Gamma refers to monitor/graphics card further altering our values to increase their brightness?
      // NOTE(Ryan): 1. Alpha test has a cut-off threshold
      // 2. Alpha linear blend with background
      r32 alpha_blend_t = (*pixel_cursor >> 24 & 0xFF) / 255.0f;
      
      r32 red_orig = (*buffer_cursor >> 16 & 0xFF);
      r32 new_red = (*pixel_cursor >> 16 & 0xFF);
      r32 red_blended = red_orig + alpha_blend_t * (new_red - red_orig);

      r32 green_orig = (*buffer_cursor >> 8 & 0xFF);
      r32 new_green = (*pixel_cursor >> 8 & 0xFF);
      r32 green_blended = green_orig + alpha_blend_t * (new_green - green_orig);

      r32 blue_orig = (*buffer_cursor >> 0 & 0xFF);
      r32 new_blue = (*pixel_cursor >> 0 & 0xFF);
      r32 blue_blended = blue_orig + alpha_blend_t * (new_blue - blue_orig);

      *buffer_cursor = 0xff << 24 | (u32)roundf(red_blended) << 16 | 
                         (u32)roundf(green_blended) << 8 | 
                         (u32)roundf(blue_blended) << 0; 
      pixel_cursor++;
      buffer_cursor++;
    }

    buffer_row += back_buffer->width;
    bitmap_row -= bitmap->width;
  }
}

// TODO(Ryan): PNG RLE may not help us as our graphics are painterly?
INTERNAL LoadedBitmap 
load_bmp(HHFThreadContext *thread, hhf_read_entire_file read_entire_file, char *filename)
{
  LoadedBitmap result = {};

  HHFPlatformReadFileResult read_result = read_entire_file(thread, filename);
  if (read_result.errno_code == 0)
  {
    BitmapHeader *bitmap_header = (BitmapHeader *)read_result.contents;
    result.pixels = (u32 *)((u8 *)bitmap_header + bitmap_header->data_offset);

    // IMPORTANT(Ryan): BMPs can go top-down and have compression. This just handles
    // BMPs we create
    u32 red_mask = bitmap_header->red_mask;
    u32 green_mask = bitmap_header->green_mask;
    u32 blue_mask = bitmap_header->blue_mask;
    u32 alpha_mask = ~(red_mask | green_mask | blue_mask);

    u32 red_shift = __builtin_ctz(red_mask);
    u32 green_shift = __builtin_ctz(green_mask);
    u32 blue_shift = __builtin_ctz(blue_mask);
    u32 alpha_shift = __builtin_ctz(alpha_mask);
    
    // NOTE(Ryan): We determined bottom-up and byte order with structured art
    u32 *pixels = result.pixels;
    for (uint y = 0; y < bitmap_header->height; ++y)
    {
      for (uint x = 0; x < bitmap_header->width; ++x)
      {
        // NOTE(Ryan): This reordering is also known as swizzling
        *pixels = (((*pixels >> alpha_shift) & 0xFF) << 24) |
                  (((*pixels >> red_shift) & 0xFF) << 16) |
                  (((*pixels >> green_shift) & 0xFF) << 8) |
                  (((*pixels >> blue_shift) & 0xFF) << 0);
        pixels++;
      }
    }

    result.width = bitmap_header->width;
    result.height = bitmap_header->height;
  }

  return result;
}

// TODO(Ryan): Ensure game is procederal and rich in combinatorics
extern "C" void
hhf_update_and_render(HHFThreadContext *thread_context, HHFBackBuffer *back_buffer, 
                      HHFSoundBuffer *sound_buffer, HHFInput *input, HHFMemory *memory, 
                      HHFPlatform *platform)
{
  //BP(NULL);

  State *state = (State *)memory->permanent;
  if (!memory->is_initialized)
  {
    // IMPORTANT(Ryan): Working with artists, only specify that certain things need to be in different layers
    state->backdrop = load_bmp(thread_context, platform->read_entire_file, "test/test_background.bmp");
    // TODO(Ryan): Not ideal to have large tables of strings in your code
    PlayerBitmap *player_bitmap = state->player_bitmaps;
    player_bitmap->head = load_bmp(thread_context, platform->read_entire_file, 
                                                    "test/test_hero_right_head.bmp");
    player_bitmap->torso = load_bmp(thread_context, platform->read_entire_file,
                                                    "test/test_hero_right_cape.bmp");
    player_bitmap->legs = load_bmp(thread_context, platform->read_entire_file, 
                                                    "test/test_hero_right_torso.bmp");
    player_bitmap->align_x = 72;
    player_bitmap->align_y = 182;
    player_bitmap++;

    player_bitmap->head = load_bmp(thread_context, platform->read_entire_file, 
                                                    "test/test_hero_back_head.bmp");
    player_bitmap->torso = load_bmp(thread_context, platform->read_entire_file,
                                                    "test/test_hero_back_cape.bmp");
    player_bitmap->legs = load_bmp(thread_context, platform->read_entire_file, 
                                                    "test/test_hero_back_torso.bmp");
    player_bitmap->align_x = 72;
    player_bitmap->align_y = 182;
    player_bitmap++;

    player_bitmap->head = load_bmp(thread_context, platform->read_entire_file, 
                                                    "test/test_hero_left_head.bmp");
    player_bitmap->torso = load_bmp(thread_context, platform->read_entire_file,
                                                    "test/test_hero_left_cape.bmp");
    player_bitmap->legs = load_bmp(thread_context, platform->read_entire_file, 
                                                    "test/test_hero_left_torso.bmp");
    player_bitmap->align_x = 72;
    player_bitmap->align_y = 182;
    player_bitmap++;

    player_bitmap->head = load_bmp(thread_context, platform->read_entire_file, 
                                                    "test/test_hero_front_head.bmp");
    player_bitmap->torso = load_bmp(thread_context, platform->read_entire_file,
                                                    "test/test_hero_front_cape.bmp");
    player_bitmap->legs = load_bmp(thread_context, platform->read_entire_file, 
                                                    "test/test_hero_front_torso.bmp");
    player_bitmap->align_x = 72;
    player_bitmap->align_y = 182;

    state->camera_pos.abs_tile_x = 17 / 2;
    state->camera_pos.abs_tile_y = 9 / 2; 

    state->player_pos.abs_tile_x = 1;
    state->player_pos.abs_tile_y = 3;
    state->player_pos.offset.x = 5.0f;
    state->player_pos.offset.y = 5.0f;

    initialise_memory_arena(&state->world_arena, memory->permanent_size - sizeof(State),
                             (u8 *)memory->permanent + sizeof(State));

    state->world = MEMORY_RESERVE_STRUCT(&state->world_arena, World);
    World *world = state->world;

    world->tile_map = MEMORY_RESERVE_STRUCT(&state->world_arena, TileMap);
    TileMap *tile_map = world->tile_map;

    tile_map->chunk_shift = 4;
    tile_map->chunk_mask = (1U << tile_map->chunk_shift) - 1;
    tile_map->chunk_dim = (1U << tile_map->chunk_shift);
    tile_map->num_tile_chunks_x = 128;
    tile_map->num_tile_chunks_y = 128;
    tile_map->num_tile_chunks_z = 2;
    tile_map->tile_side_in_metres = 1.4f;
    // NOTE(Ryan): For basic sparseness we allocate chunk contents when we write to them
    int tile_map_num_chunks = tile_map->num_tile_chunks_x * tile_map->num_tile_chunks_y *
                              tile_map->num_tile_chunks_z;
    tile_map->chunks = MEMORY_RESERVE_ARRAY(&state->world_arena, tile_map_num_chunks, TileChunk);

    srand(time(NULL));
    int num_tiles_screen_x = 17;
    int num_tiles_screen_y = 9;
    int screen_x = 0;
    int screen_y = 0;
    int abs_tile_z = 0;
    bool want_door = false;
    bool have_drawn_door = false;
    int random_index = 0;

    for (int screen_i = 0; screen_i < 100; screen_i++)
    {
      // NOTE(Ryan): Think of mod as choice range
      if (have_drawn_door) random_index = rand() % 2;
      else random_index = rand() % 3;

      if (random_index == 2) want_door = true;

      for (int tile_y = 0; tile_y < num_tiles_screen_y; ++tile_y)
      {
        for (int tile_x = 0; tile_x < num_tiles_screen_x; ++tile_x)
        {
          int abs_tile_x = screen_x * num_tiles_screen_x + tile_x;
          int abs_tile_y = screen_y * num_tiles_screen_y + tile_y;

          u32 tile_value = 1;
          if (tile_x == 0 || tile_x == num_tiles_screen_x - 1) 
          {
            // NOTE(Ryan): Although de morgan's laws could be used to reduce the number of
            // boolean expressions, often best to make clear linguistically
            if (tile_y != num_tiles_screen_y / 2) tile_value = 2;
          }
          if (tile_y == 0 || tile_y == num_tiles_screen_y - 1) 
          {
            if (tile_x != num_tiles_screen_x / 2) tile_value = 2;
          }
          if (tile_x == 6 && tile_y == 3)
          {
            if (want_door)
            {
              if (abs_tile_z == 0) tile_value = 3;
              else tile_value = 4;
            }
            if (have_drawn_door)
            {
              if (abs_tile_z == 1) tile_value = 4;
              else tile_value = 3;
            }
          }

          set_tile_value(&state->world_arena, world->tile_map, abs_tile_x, abs_tile_y,
                         abs_tile_z, tile_value);
        }
      }

      if (random_index == 1)
      {
        screen_x += 1;
      }
      if (random_index == 2)
      {
        screen_y += 1;
      }
      if (have_drawn_door)
      {
        have_drawn_door = false;

        if (abs_tile_z == 0) abs_tile_z = 1;
        else abs_tile_z = 0;
      }
      if (want_door)
      {
        want_door = false;
        have_drawn_door = true;

        if (abs_tile_z == 0) abs_tile_z = 1;
        else abs_tile_z = 0;
      }
    }
    
    memory->is_initialized = true;
  }

  World *world = state->world;
  TileMap *tile_map = world->tile_map;

  // IMPORTANT(Ryan): Drawing with y is going up. 
  // Compute y with negative and reorder min max in draw rect
  r32 screen_centre_x = (r32)back_buffer->width * 0.5f;
  r32 screen_centre_y = (r32)back_buffer->height * 0.5f;

  r32 tile_side_in_pixels = 60.0f;
  r32 metres_to_pixels = (r32)tile_side_in_pixels / (r32)tile_map->tile_side_in_metres;

  r32 player_r = 0.5f;
  r32 player_g = 0.3f;
  r32 player_b = 1.0f;
  r32 player_width = 0.75f * tile_map->tile_side_in_metres;
  r32 player_height = tile_map->tile_side_in_metres;

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

        V2 ddplayer = {};

        // NOTE(Ryan): Implementing rigid body dynamics, i.e. not concerned with deformations
        // linear and angular

        if (controller.action_right.ended_down) 
        {
          state->player_facing_direction = 0;
          ddplayer.x = 1.0f;
        }
        if (controller.action_up.ended_down) 
        {
          state->player_facing_direction = 1;
          ddplayer.y = 1.0f; 
        }
        if (controller.action_left.ended_down) 
        {
          state->player_facing_direction = 2;
          ddplayer.x = -1.0f;
        }
        if (controller.action_down.ended_down) 
        {
          state->player_facing_direction = 3;
          ddplayer.y = -1.0f;
        }
        // cosθ is x coord (think of unit circle)
        // h·cosθ is length of vertical rhs triangle

        // dot product is projecting a vector onto another
        // so, if dot product with unit vector, get what its coordinates are in that space

        // drawing lines to remember matrix multiplication

        // hadamard product rarely used (perhaps in colours)
        if (ddplayer.x != 0.0f && ddplayer.y != 0.0f)
        {
          ddplayer *= 0.7071067811865476f;
        }

        // every frame we have the same acceleration
        // however, the coefficients for the various integrations are different
        // therefore the player movement is a piecewise function (rather than continuous)

        // m/s^2
        r32 player_speed = 10.0f; 
        // by multiplying by time step, we cancel out seconds and get metres
        // in effect, we are integrating

        if (controller.move_down.ended_down) player_speed = 30.0f;

        ddplayer *= player_speed;
        // NOTE(Ryan): Here we tune by altering acceleration and friction with looped editing
        ddplayer += -1.5f * state->dplayer_pos;

        TileMapPosition test_player_pos = state->player_pos;
        // IMPORTANT(Ryan): Always start with real-world equations and then modify to your needs   
        test_player_pos.offset = (0.5f * ddplayer * SQUARE(input->frame_dt) +
                                    state->dplayer_pos * input->frame_dt + 
                                    test_player_pos.offset);
        state->dplayer_pos = ddplayer * input->frame_dt + state->dplayer_pos;  
        

        recanonicalise_position(tile_map, &test_player_pos);

        TileMapPosition player_left_pos = test_player_pos;
        player_left_pos.offset.x -= (0.5f * player_width);
        recanonicalise_position(tile_map, &player_left_pos);

        TileMapPosition player_right_pos = test_player_pos;
        player_right_pos.offset.y += (0.5f * player_width);
        recanonicalise_position(tile_map, &player_right_pos);

        bool collided = false;
        TileMapPosition collided_pos = {};

        if (!is_tile_map_point_empty(tile_map, &test_player_pos))
        {
          collided = true;
          collided_pos = test_player_pos;
        }
        if (!is_tile_map_point_empty(tile_map, &player_left_pos))
        {
          collided = true;
          collided_pos = player_left_pos;
        }
        if (!is_tile_map_point_empty(tile_map, &player_right_pos))
        {
          collided = true;
          collided_pos = player_right_pos;
        }

        if (collided) 
        {
          // IMPORTANT(Ryan): Our vector is orientated a (0, 0)
          // wall normal
          V2 r = {}; 

          if (collided_pos.abs_tile_x < state->player_pos.abs_tile_x)
          {
            r = {1, 0};
          }
          if (collided_pos.abs_tile_x > state->player_pos.abs_tile_x)
          {
            r = {-1, 0};
          }
          if (collided_pos.abs_tile_y < state->player_pos.abs_tile_y)
          {
            r = {0, 1};
          }
          if (collided_pos.abs_tile_y > state->player_pos.abs_tile_y)
          {
            r = {0, -1};
          }

          // subtracting one will cause sticking to the wall
          state->dplayer_pos = state->dplayer_pos - 2 * inner(state->dplayer_pos, r) * r; 
        }
        else
        {
          if (!are_on_same_tile(&state->player_pos, &test_player_pos))
          {
            u32 new_tile_value = get_tile_value(tile_map, &test_player_pos);
            if (new_tile_value == 3)
            {
              test_player_pos.abs_tile_z += 1;
            }
            if (new_tile_value == 4)
            {
              test_player_pos.abs_tile_z -= 1;
            }
          }

          state->player_pos = test_player_pos;
        }

        state->camera_pos.abs_tile_z = state->player_pos.abs_tile_z;

        TileMapDifference diff = subtract(tile_map, &state->player_pos, &state->camera_pos);

        // NOTE(Ryan): Screens are 17 / 9, so half screen widths
        if (diff.dxy.x > (9.0f * tile_map->tile_side_in_metres))
        {
          state->camera_pos.abs_tile_x += 17;
        }
        if (diff.dxy.x < -(9.0f * tile_map->tile_side_in_metres))
        {
          state->camera_pos.abs_tile_x -= 17;
        }
        if (diff.dxy.y > (5.0f * tile_map->tile_side_in_metres))
        {
          state->camera_pos.abs_tile_y += 9;
        }
        if (diff.dxy.y < -(5.0f * tile_map->tile_side_in_metres))
        {
          state->camera_pos.abs_tile_y -= 9;
        }

      }

    }
  }

  draw_bmp(back_buffer, &state->backdrop, 0.0f, 0.0f); 

  // draw_rect(back_buffer, 0, 0, back_buffer->width, back_buffer->height, 1.0f, 0.0f, 1.0f);

  for (int rel_y = -10; rel_y < 10; ++rel_y)
  {
    for (int rel_x = -20; rel_x < 20; ++rel_x)
    {
      u32 y = state->camera_pos.abs_tile_y + rel_y;
      u32 x = state->camera_pos.abs_tile_x + rel_x;
      u32 z = state->camera_pos.abs_tile_z;
      u32 tile_id = get_tile_value(tile_map, x, y, z);
      // TODO(Ryan): 0 is not defined, 1 is walkable, 2 is wall
      if (tile_id > 1)
      {
        r32 whitescale = 0.5f;
        if (tile_id == 2) whitescale = 1.0f;
        if (tile_id == 3 || tile_id == 4) whitescale = 0.25f;

        if (x == state->camera_pos.abs_tile_x && 
              y == state->camera_pos.abs_tile_y) whitescale = 0.0f;

        // IMPORTANT(Ryan): Smooth scrolling acheived by drawing the map around the player,
        // whilst keeping the player in the centre of the screen.
        // Therefore, incorporate the player offset in the tile drawing
        V2 centre = {screen_centre_x - (metres_to_pixels*state->camera_pos.offset.x) +
                        ((r32)rel_x * tile_side_in_pixels),
                    screen_centre_y + (metres_to_pixels*state->camera_pos.offset.y) -
                        ((r32)rel_y * tile_side_in_pixels)};

        V2 tile_side = {0.5f * tile_side_in_pixels, 0.5f * tile_side_in_pixels};
        V2 min = centre - tile_side;
        V2 max = centre + tile_side;

        draw_rect(back_buffer, min, max, whitescale, whitescale, whitescale);
      }
    }
  }

  TileMapDifference diff = subtract(state->world->tile_map, &state->player_pos, &state->camera_pos);
  // the screen centre is always where the camera is
  r32 player_ground_point_x = screen_centre_x + (metres_to_pixels * diff.dxy.x); 
  r32 player_ground_point_y = screen_centre_y - (metres_to_pixels * diff.dxy.y);

  V2 player_left_top = {player_ground_point_x - (player_width * metres_to_pixels * 0.5f),
                        player_ground_point_y - (player_height * metres_to_pixels)};
  V2 player_width_height = {player_width, player_height};

  draw_rect(back_buffer, player_left_top, 
            player_left_top + metres_to_pixels * player_width_height, 
            player_r, player_g, player_b);
  
  PlayerBitmap *active_player_bitmap = &state->player_bitmaps[state->player_facing_direction];
  draw_bmp(back_buffer, &active_player_bitmap->legs, player_ground_point_x, player_ground_point_y,
           active_player_bitmap->align_x, active_player_bitmap->align_y); 
  draw_bmp(back_buffer, &active_player_bitmap->torso, player_ground_point_x, player_ground_point_y,
           active_player_bitmap->align_x, active_player_bitmap->align_y); 
  draw_bmp(back_buffer, &active_player_bitmap->head, player_ground_point_x, player_ground_point_y,
           active_player_bitmap->align_x, active_player_bitmap->align_y); 

}
