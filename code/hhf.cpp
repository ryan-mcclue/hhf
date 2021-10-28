// SPDX-License-Identifier: zlib-acknowledgement

#include "hhf.h"

struct TileMap
{
  u32 *tiles;
};

struct World
{
  // IMPORTANT(Ryan): We don't want our units to be in pixels
  r32 tile_side_in_metres;
  int tile_side_in_pixels;
  r32 metres_to_pixels;

  // TODO(Ryan): Add sparseness
  int num_tile_maps_x;
  int num_tile_maps_y;

  int num_tiles_x;
  int num_tiles_y;

  // NOTE(Ryan): Useful for drawing say half a tile
  r32 lower_left_x;
  r32 lower_left_y;

  TileMap *tile_maps;
};

struct WorldPosition
{
// IMPORTANT(Ryan): We want at least 8bits for sub-pixel positioning to perform anti-aliasing
// Therefore, only using r32 does not have enough data in mantissa for a large world
  int tile_map_x;
  int tile_map_y;

  int tile_x;
  int tile_y;

  // relative to tile
  r32 x, y;
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

INTERNAL inline TileMap *
get_tile_map(World *world, int x, int y)
{
  TileMap *tile_map = NULL;
  if (x >= 0 && x < world->num_tile_maps_x && y >=0 && y < world->num_tile_maps_y)
  {
    tile_map = &world->tile_maps[y * world->num_tile_maps_x + x];
  }
  return tile_map;
}

INTERNAL void
recanonicalise_coord(World *world, int tile_count, int *tile_map_coord, int *tile_coord, r32 *tile_rel)
{
  int offset = floor_r32_to_int(*tile_rel / world->tile_side_in_metres);
  *tile_coord += offset;
  // TODO(Ryan): If tile_rel is a very small negative, we will wrap back 
  *tile_rel -= (offset * world->tile_side_in_metres);

  if (*tile_coord < 0)
  {
    *tile_coord = *tile_coord + tile_count;
    *tile_map_coord -= 1;
  }
  if (*tile_coord >= tile_count)
  {
    *tile_coord = *tile_coord - tile_count;
    *tile_map_coord += 1;
  }
}

INTERNAL void
recanonicalise_position(World *world, WorldPosition *pos)
{
  recanonicalise_coord(world, world->num_tiles_x, &pos->tile_map_x, &pos->tile_x, &pos->x);
  recanonicalise_coord(world, world->num_tiles_y, &pos->tile_map_y, &pos->tile_y, &pos->y);
}

INTERNAL inline u32
get_tile_map_value_unchecked(World *world, TileMap *tile_map, int x, int y)
{
  return tile_map->tiles[y * world->num_tiles_x + x];
}

INTERNAL bool
is_tile_map_point_empty(World *world, TileMap *tile_map, int tile_x, int tile_y)
{
  u32 tile_map_value = get_tile_map_value_unchecked(world, tile_map, tile_x, tile_y);

  return (tile_map_value == 0);
}

INTERNAL bool
is_world_point_empty(World *world, WorldPosition *pos)
{
  bool is_empty = false;

  TileMap *tile_map = get_tile_map(world, pos->tile_map_x, pos->tile_map_y);
  if (tile_map != NULL)
  {
    is_empty = is_tile_map_point_empty(world, tile_map, pos->tile_x, pos->tile_y);
  }

  return is_empty;
}

// TODO(Ryan): Ensure game is procederal and rich in combinatorics
extern "C" void
hhf_update_and_render(HHFThreadContext *thread_context, HHFBackBuffer *back_buffer, 
                      HHFSoundBuffer *sound_buffer, HHFInput *input, HHFMemory *memory, 
                      HHFPlatform *platform)
{
  // BP(NULL);
  /* TODO(Ryan): 
   * Remove dependency on pixel as units of measurements 
   * Remove inverted cartesian plane 
  */
  ASSERT(sizeof(State) <= memory->permanent_size);

  State *state = (State *)memory->permanent;
  if (!memory->is_initialized)
  {
    state->player_pos.tile_map_x = 0;
    state->player_pos.tile_map_y = 0;
    state->player_pos.tile_x = 4;
    state->player_pos.tile_y = 4;
    state->player_pos.x = 5.0f;
    state->player_pos.y = 5.0f;
    memory->is_initialized = true;
  }

// IMPORTANT(Ryan): Y is going up
  #define TILE_MAP_NUM_TILES_X 16
  #define TILE_MAP_NUM_TILES_Y 9
  u32 tile_map_tiles00[TILE_MAP_NUM_TILES_Y][TILE_MAP_NUM_TILES_X] =
  {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
  };
  u32 tile_map_tiles01[TILE_MAP_NUM_TILES_Y][TILE_MAP_NUM_TILES_X] =
  {
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
  };
  u32 tile_map_tiles10[TILE_MAP_NUM_TILES_Y][TILE_MAP_NUM_TILES_X] =
  {
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
  };
  u32 tile_map_tiles11[TILE_MAP_NUM_TILES_Y][TILE_MAP_NUM_TILES_X] =
  {
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
  };

  TileMap tile_maps[4] = {};
  tile_maps[0].tiles = (u32 *)tile_map_tiles00;
  tile_maps[1].tiles = (u32 *)tile_map_tiles01;
  tile_maps[2].tiles = (u32 *)tile_map_tiles10;
  tile_maps[3].tiles = (u32 *)tile_map_tiles11;

  World world = {};
  world.tile_side_in_metres = 1.4f;
  world.tile_side_in_pixels = 75.0f;
  world.metres_to_pixels = (r32)world.tile_side_in_pixels / (r32)world.tile_side_in_metres;
  world.lower_left_x = 0.0f;
  world.lower_left_y = (r32)back_buffer->height;
  world.num_tiles_x = TILE_MAP_NUM_TILES_X;
  world.num_tiles_y = TILE_MAP_NUM_TILES_Y;
  world.num_tile_maps_x = 2;
  world.num_tile_maps_y = 2;
  world.tile_maps = (TileMap *)tile_maps; 

  TileMap *active_tile_map = get_tile_map(&world, state->player_pos.tile_map_x, state->player_pos.tile_map_y);

  r32 player_r = 0.5f;
  r32 player_g = 0.3f;
  r32 player_b = 1.0f;
  r32 player_width = 0.75f * world.tile_side_in_metres;
  r32 player_height = world.tile_side_in_metres;

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

        dplayer_x *= 5.0f;
        dplayer_y *= 5.0f;

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

  for (int y = 0; y < 9; ++y)
  {
    for (int x = 0; x < 16; ++x)
    {
      u32 tile_id = get_tile_map_value_unchecked(&world, active_tile_map, x, y);
      r32 grayscale = 0.5f;
      if (tile_id == 1) grayscale = 1.0f;

      if (x == state->player_pos.tile_x && y == state->player_pos.tile_y) grayscale = 0.0f;

      r32 min_x = world.lower_left_x + ((r32)x * world.tile_side_in_pixels);
      r32 min_y = world.lower_left_y - ((r32)y * world.tile_side_in_pixels);
      r32 max_x = min_x + world.tile_side_in_pixels;
      r32 max_y = min_y - world.tile_side_in_pixels;

      draw_rect(back_buffer, min_x, max_y, max_x, min_y, grayscale, grayscale, grayscale);
    }
  }


  r32 player_min_x = world.lower_left_x + (world.tile_side_in_pixels * state->player_pos.tile_x) +
                      (world.metres_to_pixels * state->player_pos.x - (player_width * world.metres_to_pixels * 0.5f));
  r32 player_min_y = world.lower_left_y - (world.tile_side_in_pixels * state->player_pos.tile_y) -
                      (world.metres_to_pixels * state->player_pos.y - player_height * world.metres_to_pixels);
  draw_rect(back_buffer, player_min_x, player_min_y - player_height*world.metres_to_pixels, player_min_x + player_width*world.metres_to_pixels, 
      player_min_y, player_r, player_g, player_b);
}
