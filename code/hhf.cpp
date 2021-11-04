// SPDX-License-Identifier: zlib-acknowledgement

#include "hhf.h"

#include <time.h>

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
  r32 x_offset, y_offset;
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

struct State
{
  MemoryArena world_arena;
  World *world;

  TileMapPosition player_pos;
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
draw_rect(HHFBackBuffer *back_buffer, r32 x0, r32 y0, r32 x1, r32 y1, r32 r, r32 g, r32 b)
{
  // NOTE(Ryan): Coordinates [x0, x1)
  int min_x = roundf(x0); // _mm_cvtss_si32(_mm_set_ss(x0));
  int min_y = roundf(y0);
  int max_x = roundf(x1);
  int max_y = roundf(y1);

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
  recanonicalise_coord(tile_map, &pos->abs_tile_x, &pos->x_offset);
  recanonicalise_coord(tile_map, &pos->abs_tile_y, &pos->y_offset);
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


 
INTERNAL bool
is_tile_map_point_empty(TileMap *tile_map, TileMapPosition *pos)
{
  bool result = false;

  u32 tile_value = get_tile_value(tile_map, pos->abs_tile_x, pos->abs_tile_y, pos->abs_tile_z);
  result = (tile_value == 1);

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
    state->player_pos.abs_tile_x = 3;
    state->player_pos.abs_tile_y = 3;
    state->player_pos.x_offset = 5.0f;
    state->player_pos.y_offset = 5.0f;

    initialise_memory_arena(&state->world_arena, memory->permanent_size - sizeof(State),
                             (u8 *)memory->permanent + sizeof(State));

    state->world = MEMORY_RESERVE_STRUCT(&state->world_arena, World);
    World *world = state->world;

    world->tile_map = MEMORY_RESERVE_STRUCT(&state->world_arena, TileMap);
    TileMap *tile_map = world->tile_map;

    tile_map->chunk_shift = 8;
    tile_map->chunk_mask = (1U << tile_map->chunk_shift) - 1;
    tile_map->chunk_dim = (1U << tile_map->chunk_shift);
    tile_map->num_tile_chunks_x = 4;
    tile_map->num_tile_chunks_y = 4;
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
    for (int screen_i = 0; screen_i < 100; screen_i++)
    {
      for (int tile_y = 0; tile_y < num_tiles_screen_y; ++tile_y)
      {
        for (int tile_x = 0; tile_x < num_tiles_screen_x; ++tile_x)
        {
          int abs_tile_x = screen_x * num_tiles_screen_x + tile_x;
          int abs_tile_y = screen_y * num_tiles_screen_y + tile_y;
          int abs_tile_z = 0;

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
          if (tile_x == 6 && tile_y == 3) tile_value = 3;

          set_tile_value(&state->world_arena, world->tile_map, abs_tile_x, abs_tile_y,
                         abs_tile_z, tile_value);
        }
      }
      // NOTE(Ryan): Think of mod as choice range
      int random_index = rand() % 2;
      if (rand() % 2 == 0) screen_x += 1; else screen_y += 1;
    }
    
    memory->is_initialized = true;
  }

  World *world = state->world;
  TileMap *tile_map = world->tile_map;

  // IMPORTANT(Ryan): Drawing with y is going up. 
  // Compute y with negative and reorder min max in draw rect
  r32 screen_centre_x = (r32)back_buffer->width * 0.5f;
  r32 screen_centre_y = (r32)back_buffer->height * 0.5f;

  r32 tile_side_in_pixels = 75.0f;
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
        r32 dplayer_x = 0.0f; 
        r32 dplayer_y = 0.0f; 

        if (controller.action_left.ended_down) dplayer_x = -1.0f;
        if (controller.action_right.ended_down) dplayer_x = 1.0f;
        if (controller.action_up.ended_down) dplayer_y = 1.0f;
        if (controller.action_down.ended_down) dplayer_y = -1.0f;

        r32 player_speed = 2.0f;

        if (controller.move_down.ended_down) player_speed = 10.0f;

        dplayer_x *= player_speed;
        dplayer_y *= player_speed;

        TileMapPosition test_player_pos = state->player_pos;
        test_player_pos.x_offset += (dplayer_x * input->frame_dt);
        test_player_pos.y_offset += (dplayer_y * input->frame_dt); 
        recanonicalise_position(tile_map, &test_player_pos);

        TileMapPosition player_left_pos = test_player_pos;
        player_left_pos.x_offset -= (0.5f * player_width);
        recanonicalise_position(tile_map, &player_left_pos);

        TileMapPosition player_right_pos = test_player_pos;
        player_right_pos.x_offset += (0.5f * player_width);
        recanonicalise_position(tile_map, &player_right_pos);

        // TODO(Ryan): Fix stopping before walls and possibly going through thin walls
        bool is_tile_valid = is_tile_map_point_empty(tile_map, &test_player_pos) &&
          is_tile_map_point_empty(tile_map, &player_left_pos) && 
          is_tile_map_point_empty(tile_map, &player_right_pos);

        if (is_tile_valid) state->player_pos = test_player_pos;

      }

    }
  }

  draw_rect(back_buffer, 0, 0, back_buffer->width, back_buffer->height, 1.0f, 0.0f, 1.0f);

  for (int rel_y = -10; rel_y < 10; ++rel_y)
  {
    for (int rel_x = -20; rel_x < 20; ++rel_x)
    {
      u32 y = state->player_pos.abs_tile_y + rel_y;
      u32 x = state->player_pos.abs_tile_x + rel_x;
      u32 z = state->player_pos.abs_tile_z;
      u32 tile_id = get_tile_value(tile_map, x, y, z);
      // TODO(Ryan): 0 is not defined, 1 is walkable, 2 is wall
      if (tile_id > 0)
      {
        r32 whitescale = 0.5f;
        if (tile_id == 2) whitescale = 1.0f;
        if (tile_id == 3) whitescale = 0.25f;

        if (x == state->player_pos.abs_tile_x && 
              y == state->player_pos.abs_tile_y) whitescale = 0.0f;

        // IMPORTANT(Ryan): Smooth scrolling acheived by drawing the map around the player,
        // whilst keeping the player in the centre of the screen.
        // Therefore, incorporate the player offset in the tile drawing
        r32 centre_x = screen_centre_x - (metres_to_pixels*state->player_pos.x_offset) +
                        ((r32)rel_x * tile_side_in_pixels);
        r32 centre_y = screen_centre_y + (metres_to_pixels*state->player_pos.y_offset) -
                        ((r32)rel_y * tile_side_in_pixels);
        r32 min_x = centre_x - 0.5f * tile_side_in_pixels; 
        r32 min_y = centre_y - 0.5f * tile_side_in_pixels; 
        r32 max_x = centre_x + 0.5f * tile_side_in_pixels;
        r32 max_y = centre_y + 0.5f * tile_side_in_pixels;

        draw_rect(back_buffer, min_x, min_y, max_x, max_y, whitescale, whitescale, whitescale);
      }
    }
  }


  r32 player_min_x = screen_centre_x - (player_width * metres_to_pixels * 0.5f);
  r32 player_min_y = screen_centre_y - (player_height * metres_to_pixels);
  draw_rect(back_buffer, player_min_x, player_min_y, 
            player_min_x + player_width*metres_to_pixels, 
            player_min_y + player_height*metres_to_pixels, player_r, player_g, player_b);
}
