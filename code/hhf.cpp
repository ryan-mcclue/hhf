// SPDX-License-Identifier: zlib-acknowledgement

#include "hhf.h"

struct TileChunk
{
  u32 *tiles;
};

// TODO(Ryan): Move out into a TileMap struct and have World contain it
struct World
{
  u32 chunk_mask;
  u32 chunk_shift;
  u32 chunk_dim;

  // IMPORTANT(Ryan): We don't want our units to be in pixels
  r32 tile_side_in_metres;
  int tile_side_in_pixels;
  r32 metres_to_pixels;

  // TODO(Ryan): Add sparseness
  int num_tile_chunks_x;
  int num_tile_chunks_y;

  TileChunk *tile_chunks;
};

struct WorldPosition
{
  u32 abs_tile_x;
  u32 abs_tile_y;

// IMPORTANT(Ryan): We want at least 8bits for sub-pixel positioning to perform anti-aliasing
// Therefore, only using r32 does not have enough data in mantissa for a large world
  // relative to tile
  // TODO(Ryan): rename to offset?
  r32 x, y;
};

struct TileChunkPosition
{
  u32 tile_chunk_x;
  u32 tile_chunk_y;
  u32 rel_tile_x;
  u32 rel_tile_y;
};

struct State
{
  WorldPosition player_pos;
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
get_tile_chunk(World *world, int x, int y)
{
  TileChunk *result = NULL;

  if (x >= 0 && x < world->num_tile_chunks_x && y >=0 && y < world->num_tile_chunks_y)
  {
    result = &world->tile_chunks[y * world->num_tile_chunks_x + x];
  }

  return result;
}

INTERNAL void
recanonicalise_coord(World *world, u32 *tile_coord, r32 *tile_rel)
{
  int offset = (int)roundf(*tile_rel / world->tile_side_in_metres);
  *tile_coord += offset;
  // TODO(Ryan): If tile_rel is a very small negative, we will wrap back 
  *tile_rel -= (offset * world->tile_side_in_metres);
}

INTERNAL void
recanonicalise_position(World *world, WorldPosition *pos)
{
  recanonicalise_coord(world, &pos->abs_tile_x, &pos->x);
  recanonicalise_coord(world, &pos->abs_tile_y, &pos->y);
}

INTERNAL TileChunkPosition
get_tile_chunk_position(World *world, u32 abs_tile_x, u32 abs_tile_y)
{
  TileChunkPosition result = {};

  result.tile_chunk_x = abs_tile_x >> world->chunk_shift;
  result.tile_chunk_y = abs_tile_y >> world->chunk_shift;
  result.rel_tile_x = abs_tile_x & world->chunk_mask;
  result.rel_tile_y = abs_tile_y & world->chunk_mask;

  return result;
}

INTERNAL u32
get_tile_value_unchecked(World *world, TileChunk *tile_chunk, u32 tile_x, u32 tile_y)
{
  u32 result = 0;

  result = tile_chunk->tiles[tile_y * world->chunk_dim + tile_x];

  return result;
}

INTERNAL u32
get_tile_value(World *world, TileChunk *tile_chunk, u32 tile_x, u32 tile_y)
{
  u32 result = 0;

  if (tile_chunk != NULL)
  {
    result = get_tile_value_unchecked(world, tile_chunk, tile_x, tile_y);
  }

  return result;
}

INTERNAL u32
get_tile_value(World *world, u32 abs_tile_x, u32 abs_tile_y)
{
  u32 result = 0;

  TileChunkPosition tile_chunk_pos = get_tile_chunk_position(world, abs_tile_x, abs_tile_y);
  TileChunk *tile_chunk = get_tile_chunk(world, tile_chunk_pos.tile_chunk_x, 
                                          tile_chunk_pos.tile_chunk_x);
  result = get_tile_value(world, tile_chunk, tile_chunk_pos.rel_tile_x, 
                            tile_chunk_pos.rel_tile_y);
  return result;
}


 
INTERNAL bool
is_world_point_empty(World *world, WorldPosition *pos)
{
  bool result = false;

  u32 tile_value = get_tile_value(world, pos->abs_tile_x, pos->abs_tile_y);
  result = (tile_value == 0);

  return result;
}

struct MemoryArena
{
  u8 *base;
  size_t size;
  size_t used;
};

INTERNAL void
initialise_memory_arena(MemoryArena *arena, size_t size, void *mem)
{
  arena->base = mem;
  arena->size = size;
  arena->used = 0;
}

#define RESERVE_STRUCT(arena, struct) \
  (struct *)(obtain_mem(arena, sizeof(struct)));
#define RESERVE_ARRAY(arena, elem, len) \
  (elem *)(obtain_mem(arena, sizeof(elem) * (len)));

INTERNAL void *
obtain_mem(MemoryArena *arena, size_t size)
{
  void *result = NULL;
  ASSERT(arena->used + size < arena->size);

  result = (u8 *)arena->base + arena->used;
  arena->used += size;

  return result;
}

INTERNAL u32
set_tile_value(TileChunk *tile_chunk, u32 tile_x, u32 tile_y, u32 value)
{
  u32 result = 0;

  if (tile_chunk != NULL)
  {
    result = set_tile_value_unchecked(world, tile_chunk, tile_x, tile_y, value);
  }

  return result;
}

INTERNAL void
set_tile_value(MemoryArena *arena, TileMap *tile_map, u32 abs_tile_x, u32 abs_tile_y, u32 value);
{
  TileChunkPosition tile_chunk_pos = get_tile_chunk_position(world, abs_tile_x, abs_tile_y);
  TileChunk *tile_chunk = get_tile_chunk(world, tile_chunk_pos.tile_chunk_x, 
                                          tile_chunk_pos.tile_chunk_x);
  ASSERT(tile_chunk != NULL);
}


// TODO(Ryan): Ensure game is procederal and rich in combinatorics
extern "C" void
hhf_update_and_render(HHFThreadContext *thread_context, HHFBackBuffer *back_buffer, 
                      HHFSoundBuffer *sound_buffer, HHFInput *input, HHFMemory *memory, 
                      HHFPlatform *platform)
{
   //BP(NULL);

  ASSERT(sizeof(State) <= memory->permanent_size);

  State *state = (State *)memory->permanent;
  if (!memory->is_initialized)
  {
    state->player_pos.abs_tile_x = 4;
    state->player_pos.abs_tile_y = 4;
    state->player_pos.x = 5.0f;
    state->player_pos.y = 5.0f;

    initialise_memory_arena(&state->world_arena, memory->permanent - sizeof(State),
                             (u8 *)memory->permanent_size + sizeof(State));

    state->world = reserve_struct(&state->world_arena, World);
    World *world = state->world;
    TileMap *tile_map = world->tile_map;
    tile_map = reserve_struct(&state->world_arena, TileMap);
    tile_map->chunks = reserve_array(TileChunk, tile_map->num_tile_chunks_x *
                                                  tile_map->num_tile_chunks_y);

    for (int y = 0; y < world->num_tile_chunks_y; ++y)
    {
      for (int x = 0; x < world->num_tile_chunks_x; ++x)
      {
        tile_map->chunks[y * tile_map->num_tile_chunks_x + x] = \
            reserve_array(u32, tile_map->chunk_dim * tile_map->chunk_dim);
      }
    }

    int num_tiles_x = 17;
    int num_tiles_y = 9;
    for (int screen_y = 0; screen_y < 32; screen_y++)
    {
      for (int screen_x = 0; screen_x < 32; screen_x++)
      {
        for (int tile_y = 0; tile_y < num_tiles_y; ++tile_y)
        {
          for (int tile_x = 0; tile_x < num_tiles_x; ++tile_x)
          {
            int abs_tile_x = screen_x * num_tiles_x + tile_x;
            int abs_tile_y = screen_y * num_tiles_y + tile_y;
            set_tile_value(world->tile_map, abs_tile_x, abs_tile_y, 
                (x == y) && (x % 2) ? 1 : 0);
          }
        }
      }
    }
    
    memory->is_initialized = true;
  }

// IMPORTANT(Ryan): Drawing with y is going up. 

  World world = {};
  world.chunk_shift = 8;
  world.chunk_mask = (1U << world.chunk_shift) - 1;
  world.chunk_dim = (1U << world.chunk_shift);

  TileChunk tile_chunk = {};
  tile_chunk.tiles = (u32 *)temp_tile_chunk;
  world.tile_chunks = &tile_chunk;
  world.num_tile_chunks_x = 4;
  world.num_tile_chunks_y = 4;

  world.tile_side_in_metres = 1.4f;
  world.tile_side_in_pixels = 75.0f;
  world.metres_to_pixels = (r32)world.tile_side_in_pixels / (r32)world.tile_side_in_metres;

  r32 player_r = 0.5f;
  r32 player_g = 0.3f;
  r32 player_b = 1.0f;
  r32 player_width = 0.75f * world.tile_side_in_metres;
  r32 player_height = world.tile_side_in_metres;

  r32 screen_centre_x = (r32)back_buffer->width * 0.5f;
  r32 screen_centre_y = (r32)back_buffer->height * 0.5f;

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

        WorldPosition test_player_pos = state->player_pos;
        test_player_pos.x += (dplayer_x * input->frame_dt);
        test_player_pos.y += (dplayer_y * input->frame_dt); 
        recanonicalise_position(&world, &test_player_pos);

        WorldPosition player_left_pos = test_player_pos;
        player_left_pos.x -= (0.5f * player_width);
        recanonicalise_position(&world, &player_left_pos);

        WorldPosition player_right_pos = test_player_pos;
        player_right_pos.x += (0.5f * player_width);
        recanonicalise_position(&world, &player_right_pos);

        // TODO(Ryan): Fix stopping before walls and possibly going through thin walls
        bool is_tile_valid = is_world_point_empty(&world, &test_player_pos) &&
          is_world_point_empty(&world, &player_left_pos) && 
          is_world_point_empty(&world, &player_right_pos);

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
      u32 tile_id = get_tile_value(&world, x, y);
      r32 grayscale = 0.5f;
      if (tile_id == 1) grayscale = 1.0f;

      if (x == state->player_pos.abs_tile_x && 
            y == state->player_pos.abs_tile_y) grayscale = 0.0f;

      // IMPORTANT(Ryan): Smooth scrolling acheived by drawing the map around the player,
      // whilst keeping the player in the centre of the screen.
      // Therefore, incorporate the player offset in the tile drawing
      r32 centre_x = screen_centre_x - (world.tile_side_in_pixels * state->player_pos.x) + ((r32)rel_x * world.tile_side_in_pixels);
      r32 centre_y = screen_centre_y + (world.tile_side_in_pixels * state->player_pos.y) - ((r32)rel_y * world.tile_side_in_pixels);
      r32 min_x = centre_x - 0.5f * world.tile_side_in_pixels; 
      r32 min_y = centre_y - 0.5f * world.tile_side_in_pixels; 
      r32 max_x = centre_x + 0.5f * world.tile_side_in_pixels;
      r32 max_y = centre_y + 0.5f * world.tile_side_in_pixels;

      draw_rect(back_buffer, min_x, min_y, max_x, max_y, grayscale, grayscale, grayscale);
    }
  }


  r32 player_min_x = screen_centre_x - (player_width * world.metres_to_pixels * 0.5f);
  r32 player_min_y = screen_centre_y - (player_height * world.metres_to_pixels);
  draw_rect(back_buffer, player_min_x, player_min_y, 
            player_min_x + player_width*world.metres_to_pixels, 
            player_min_y + player_height*world.metres_to_pixels, player_r, player_g, player_b);
}
