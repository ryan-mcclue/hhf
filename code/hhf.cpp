// SPDX-License-Identifier: zlib-acknowledgement

#include "hhf.h"

struct State
{
  r32 player_x;
  r32 player_y;
};

struct TileMap
{
  int num_tiles_x;
  int num_tiles_y;

  r32 upper_left_x;
  r32 upper_left_y;
  r32 tile_width;
  r32 tile_height;

  u32 *tiles;
};

struct World
{
  // TODO(Ryan): Add sparseness
  int num_tile_maps_x;
  int num_tile_maps_y;

  TileMap *tile_maps;
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

INTERNAL bool
is_world_point_empty(World *world, int tile_map_x, int tile_map_y, int tile_x, int tile_y)
{
  TileMap *tile_map = get_tile_map(world, tile_map_x, tile_map_y);
  if (tile_map != NULL)
  {
    // is_tile_map_point_empty(tile_map, tile_x, tile_y); 
  }

  return false;
}

INTERNAL inline u32
get_tile_map_value_unchecked(TileMap *tile_map, int x, int y)
{
  return tile_map->tiles[y * tile_map->num_tiles_x + x];
}

INTERNAL bool
is_tile_map_point_empty(TileMap *tile_map, r32 x, r32 y)
{
  int player_tile_x = truncate_r32_to_int((x - tile_map->upper_left_x) / tile_map->tile_width);
  int player_tile_y = truncate_r32_to_int((y - tile_map->upper_left_y) / tile_map->tile_height);

  bool is_tile_empty = false;
  if (player_tile_x >= 0 && player_tile_x < tile_map->num_tiles_x &&
      player_tile_y >= 0 && player_tile_y < tile_map->num_tiles_y)
  {
    u32 tile_map_value = get_tile_map_value_unchecked(tile_map, player_tile_x, player_tile_y);
    is_tile_empty = (tile_map_value == 0);
  }

  return is_tile_empty;
}


extern "C" void
hhf_update_and_render(HHFThreadContext *thread_context, HHFBackBuffer *back_buffer, 
                      HHFSoundBuffer *sound_buffer, HHFInput *input, HHFMemory *memory, 
                      HHFPlatform *platform)
{
  ASSERT(sizeof(State) <= memory->permanent_size);

  State *state = (State *)memory->permanent;
  if (!memory->is_initialized)
  {
    state->player_x = 300;
    state->player_y = 300;
    memory->is_initialized = true;
  }

  #define TILE_MAP_NUM_TILES_X 16
  #define TILE_MAP_NUM_TILES_Y 9
  u32 tile_map_tiles0[TILE_MAP_NUM_TILES_Y][TILE_MAP_NUM_TILES_X] =
  {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
  };
  u32 tile_map_tiles1[TILE_MAP_NUM_TILES_Y][TILE_MAP_NUM_TILES_X] =
  {
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
  };

  TileMap tile_maps[2];
  tile_maps[0].num_tiles_x = TILE_MAP_NUM_TILES_X;
  tile_maps[0].num_tiles_y = TILE_MAP_NUM_TILES_Y;
  tile_maps[0].tile_width = 75.0f;
  tile_maps[0].tile_height = 75.0f;
  // NOTE(Ryan): Useful to have these to only show half of a tile, etc.
  tile_maps[0].upper_left_x = 0.0f;
  tile_maps[0].upper_left_y = 0.0f;
  tile_maps[0].tiles = (u32 *)tile_map_tiles0;
 
  // TODO(Ryan): The fact we are cloning is suspicious
  tile_maps[1] = tile_maps[0];
  tile_maps[1].tiles = (u32 *)tile_map_tiles1;

  TileMap *active_tile_map = &tile_maps[0];

  r32 player_r = 0.5f;
  r32 player_g = 0.2f;
  r32 player_b = 1.0f;
  r32 player_width = 0.75f * active_tile_map->tile_width;
  r32 player_height = active_tile_map->tile_height;

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
        if (controller.action_up.ended_down) dplayer_y = -1.0f;
        if (controller.action_down.ended_down) dplayer_y = 1.0f;

        dplayer_x *= 128.0f;
        dplayer_y *= 128.0f;

        r32 new_player_x = state->player_x + (dplayer_x * input->frame_dt);
        r32 new_player_y = state->player_y + (dplayer_y * input->frame_dt);
        
        // TODO(Ryan): Fix stopping before walls and possible going through thin walls
        bool is_tile_valid = is_tile_map_point_empty(active_tile_map, new_player_x, new_player_y) &&
          is_tile_map_point_empty(active_tile_map, new_player_x + player_width * 0.5f, new_player_y) &&
          is_tile_map_point_empty(active_tile_map, new_player_x - player_width * 0.5f, new_player_y);


        if (is_tile_valid)
        {
          state->player_x = new_player_x;
          state->player_y = new_player_y;
        }

      }

    }
  }

  draw_rect(back_buffer, 0, 0, back_buffer->width, back_buffer->height, 1.0f, 0.0f, 1.0f);

  for (int y = 0; y < 9; ++y)
  {
    for (int x = 0; x < 16; ++x)
    {
      u32 tile_id = get_tile_map_value_unchecked(active_tile_map, x, y);
      r32 grayscale = 0.5f;
      if (tile_id == 1) grayscale = 1.0f;

      r32 min_x = active_tile_map->upper_left_x + ((r32)x * active_tile_map->tile_width);
      r32 min_y = active_tile_map->upper_left_y + ((r32)y * active_tile_map->tile_height);
      r32 max_x = min_x + active_tile_map->tile_width;
      r32 max_y = min_y + active_tile_map->tile_height;

      draw_rect(back_buffer, min_x, min_y, max_x, max_y, grayscale, grayscale, grayscale);
    }
  }


  r32 player_min_x = state->player_x - (player_width * 0.5f);
  r32 player_min_y = state->player_y - player_height;
  draw_rect(back_buffer, player_min_x, player_min_y, player_min_x + player_width, 
      player_min_y + player_height, player_r, player_g, player_b);
}
