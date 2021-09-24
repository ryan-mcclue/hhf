// SPDX-License-Identifier: zlib-acknowledgement 

#define INTERNAL static
#define GLOBAL static
#define LOCAL_PERSIST static

#define BILLION 1000000000L

#include <sys/utsname.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/netlink.h>
#define NETLINK_MAX_PAYLOAD 8192
union UeventBuffer
{
  struct nlmsghdr netlink_header;
  char raw[NETLINK_MAX_PAYLOAD];
};
#include <linux/input.h>
#define EVDEV_BITFIELD_QUANTA \
  (sizeof(unsigned long) * 8)
#define EVDEV_BITFIELD_LEN(bit_count) \
  ((bit_count) / EVDEV_BITFIELD_QUANTA + 1)
#define EVDEV_BITFIELD_TEST(bitfield, bit) \
  (((bitfield)[(bit) / EVDEV_BITFIELD_QUANTA] >> \
     ((bit) % EVDEV_BITFIELD_QUANTA)) & 0x1)
enum EVDEV_DEVICE_TYPE
{
  EVDEV_DEVICE_TYPE_IGNORE = 1,
  EVDEV_DEVICE_TYPE_KEYBOARD,
  EVDEV_DEVICE_TYPE_GAMEPAD,
  EVDEV_DEVICE_TYPE_MOUSE,
};
#define RLIMIT_NOFILE 1024
#define EVDEV_GAMEPAD_RUMBLE_ID -1 
#define EPOLL_EVDEV_MAX_EVENTS 5
#include <sound/asound.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xpresent.h>

#include <cstdio>
#include <cstring> 
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cctype>

typedef uint8_t u8;
typedef uint32_t u32;
// NOTE(Ryan): This is to avoid compiler adjusting a value like 1234 to 1 which it
//             would have to do if assigning to a bool.
typedef u32 b32;
typedef float r32;

struct XlibPresentPixmap
{
  Pixmap pixmap;
  int width, height;
  u32 serial;
};

struct XlibBackBuffer
{
  XImage *image;
  Pixmap pixmap;
  XlibPresentPixmap present_pixmap;
  XserverRegion region;
  RRCrtc crtc;
  XRenderPictFormat *format;
  // NOTE(Ryan): Memory order: XX RR GG BB
  u8 *memory;
  int width;
  int height;
};

#if defined(HHF_DEV)
INTERNAL void __bp(void) { return; }
INTERNAL void __ebp(void) { __attribute__((unused)) char *err = strerror(errno); }
#define BP() __bp()
#define EBP() __ebp()
#else
#define BP()
#define EBP()
#endif

INTERNAL long
timespec_diff(struct timespec *start, struct timespec *end)
{
  return (BILLION * (end->tv_sec - start->tv_sec)) +
         (end->tv_nsec - start->tv_nsec);
}

INTERNAL int
xlib_error_handler(Display *display, XErrorEvent *err)
{
  char msg_type[32] = {0};
  snprintf(msg_type, sizeof(msg_type), "%d", err->error_code);
  char protocol_request_buf[512] = {0};
	XGetErrorDatabaseText(display, "XRequest", msg_type, "[NOT FOUND]", 
                        protocol_request_buf, 
                        sizeof(protocol_request_buf));

  char err_msg_buf[1024] = {0};
  XGetErrorText(display, err->error_code, err_msg_buf, 
                sizeof(err_msg_buf));

  // TODO(Ryan): Error logging
  BP();

  return 1;
}

INTERNAL int
xlib_io_error_handler(Display *display)
{
  // TODO(Ryan): Error logging
  BP();

  return 1;
}

INTERNAL XlibBackBuffer
xlib_create_back_buffer(Display *display, Window window, XVisualInfo visual_info, 
                        int width, int height)
{
  XlibBackBuffer back_buffer = {};

  int bytes_per_pixel = 4;
  int fd = -1;
  int offset = 0;
  back_buffer.memory = (u8 *)mmap(NULL, width * height * bytes_per_pixel, 
                                  PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 
                                  fd, offset);
  if (back_buffer.memory == NULL)
  {
    // TODO(Ryan): Error logging
    EBP();
  }
  back_buffer.width = width;
  back_buffer.height = height;

  int image_offset = 0;
  int image_scanline_offset = 0;
  int image_pad_bits = 32;
  back_buffer.image = XCreateImage(display, visual_info.visual, visual_info.depth,
                            ZPixmap, image_offset, (char *)back_buffer.memory, width, height,
                            image_pad_bits, image_scanline_offset);
  if (back_buffer.image == NULL)
  {
    // TODO(Ryan): Error logging
    BP();
  }

  back_buffer.pixmap = XCreatePixmap(display, window,
                                     back_buffer.width, back_buffer.height,
                                     visual_info.depth);

  return back_buffer;
}

INTERNAL void
xrender_xpresent_back_buffer(Display *display, XRenderPictFormat *format, Window window,
                         GC gc, XlibBackBuffer back_buffer, int window_width, int window_height)
{
  XPutImage(display, back_buffer.pixmap, gc, back_buffer.image, 
          0, 0, 0, 0, back_buffer.width, back_buffer.height);

  if (back_buffer->present_pixmap->width != window_width ||
      back_buffer->present_pixmap->height != window_height)
  {
    back_buffer->present_pixmap->pixmap = XCreatePixmap(display, window, window_width,
                                                        window_height, visual_info.depth);
  }

  XRenderPictureAttributes pict_attributes = {};
  Picture src_pict = XRenderCreatePicture(display, back_buffer.pixmap, 
                                          format, 0, 
                                          &pict_attributes);
  Picture dst_pict = XRenderCreatePicture(display, present_pixmap, 
                                          format, 0, &pict_attributes);
  
  // TODO(Ryan): Restrict to particular resolutions that align with our art
  double x_scale = back_buffer.width / (double)window_width;
  double y_scale = back_buffer.height / (double)window_height;
  XTransform transform_matrix = {{
    {XDoubleToFixed(x_scale), XDoubleToFixed(0), XDoubleToFixed(0)},
    {XDoubleToFixed(0), XDoubleToFixed(y_scale), XDoubleToFixed(0)},
    {XDoubleToFixed(0), XDoubleToFixed(0), XDoubleToFixed(1)}  
  }};
  XRenderSetPictureTransform(display, src_pict, &transform_matrix);
  
  XRenderComposite(display, PictOpSrc, src_pict, 0, dst_pict, 
                  0, 0, 0, 0, 0, 0,
                  window_width, window_height);

  XPresentPixmap(xlib_display, xlib_window, present_pixmap, serial++, 
      present_region, present_region, 0, 0, None, None, None, PresentOptionNone,
      0, 0, 0, NULL, 0);
}

INTERNAL void
render_weird_gradient(XlibBackBuffer *back_buffer, int x_offset, int y_offset)
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

INTERNAL int
xrandr_get_active_refresh_rate(Display *display, Window root_window)
{
  XRRScreenResources *screen_resources = XRRGetScreenResources(display, root_window);
  if (screen_resources == NULL) BP();

  RRMode active_mode_id = 0;
  for (int crtc_num = 0; crtc_num < screen_resources->ncrtc; ++crtc_num) 
  {
    XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, screen_resources, 
                                            screen_resources->crtcs[crtc_num]);
    if (crtc_info == NULL) BP();

    if (crtc_info->mode != None)
    {
      active_mode_id = crtc_info->mode;
      break;
    }
  }

  int refresh_rate = 0;
  for (int mode_num = 0; mode_num < screen_resources->nmode; ++mode_num) 
  {
    XRRModeInfo mode_info = screen_resources->modes[mode_num];
    if (mode_info.id == active_mode_id)
    {
      // NOTE(Ryan): Only need to cast once due to implicit type promotion with
      // operands of less precision
      refresh_rate = (r32)mode_info.dotClock / 
                     (mode_info.hTotal * mode_info.vTotal);
    }
  }

  return refresh_rate;
}

/*
INTERNAL int
create_uevent_socket(void)
{
  int uevent_socket = socket(PF_NETLINK, SOCK_RAW | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
  if (uevent_socket == -1)
  {
    EBP();
  }

  struct sockaddr_nl uevent_addr = {};
  uevent_addr.nl_family = AF_NETLINK;
  uevent_addr.nl_groups = 1 << 0;
  if (bind(uevent_socket, (struct sockaddr *)&uevent_addr, sizeof(uevent_addr)) == -1)
  {
    EBP();
  }

  UeventBuffer uevent_buffer = {};
  struct iovec uevent_iov = {};
  uevent_iov.iov_base = &uevent_buffer;
  uevent_iov.iov_len = sizeof(uevent_buffer);

  struct msghdr uevent_msg = {};
  struct sockaddr_nl uevent_src_addr = {};
  uevent_msg.msg_name = &uevent_src_addr;
  uevent_msg.msg_namelen = sizeof(uevent_src_addr);
  uevent_msg.msg_iov = &uevent_iov;
  uevent_msg.msg_iovlen = 1;

  return uevent_socket;
}

// IMPORTANT(Ryan): This will become more useful when optimising, e.g. removing
// fd from epoll when disconnected
INTERNAL void
poll_uevent_socket_for_evdev_devices(int monitor, evdev_device_b devices[RLIMIT_NOFILE])
{
    int uevent_bytes_received = recvmsg(uevent_socket, &uevent_msg, 0); 
    if (uevent_bytes_received == -1 && errno != EWOULDBLOCK)
    {
        EBP();
    }
    if (uevent_bytes_received > 0)
    {
      // TODO(Ryan): Is there a cleaner way that uses netlink macros to parse?
      char *uevent_buffer_str = uevent_buffer.raw;
      char uevent_command[64] = {};

      int uevent_buffer_i = 0; 
      while (uevent_buffer_str[uevent_buffer_i] != '@')
      {
        uevent_command[uevent_buffer_i] = uevent_buffer_str[uevent_buffer_i];
        uevent_buffer_i++;
      }
      uevent_command[uevent_buffer_i] = '\0';

      char *event_start_str = strstr(uevent_buffer_str, "/event");
      if (event_start_str != NULL)
      {
        char device_id[4] = {};
        int device_id_i = 0;
        event_start_str += 6;
        while (isdigit(*event_start_str))
        {
          device_id[device_id_i] = *event_start_str++;
          device_id_i++;
        }
        device_id[device_id_i] = '\0';

        char evdev_device_path[128] = {};
        strcpy(evdev_device_path, "/dev/input/event");
        strcat(evdev_device_path, device_id);
        //printf("Device: %s was %s\n", evdev_device_path, uevent_command);
      }
      printf("%s\n", uevent_buffer_str);
    }
}
*/

INTERNAL void 
evdev_populate_devices(int epoll_fd, EVDEV_DEVICE_TYPE devices[RLIMIT_NOFILE])
{
  char dev_path[64] = {};
  for (int ev_id = 0; ev_id < 64; ++ev_id)
  {
    sprintf(dev_path, "/dev/input/event%d", ev_id);
    if (access(dev_path, F_OK) == 0)
    {
      int dev_fd = open(dev_path, O_RDWR);
      if (dev_fd == -1)
      {
        EBP();
      }

      unsigned long dev_ev_capabilities[EVDEV_BITFIELD_LEN(EV_CNT)] = {};
      unsigned long dev_key_capabilities[EVDEV_BITFIELD_LEN(KEY_CNT)] = {};
      unsigned long dev_abs_capabilities[EVDEV_BITFIELD_LEN(ABS_CNT)] = {};
      unsigned long dev_rel_capabilities[EVDEV_BITFIELD_LEN(REL_CNT)] = {};
      unsigned long dev_ff_capabilities[EVDEV_BITFIELD_LEN(FF_CNT)] = {};
      unsigned long dev_sw_capabilities[EVDEV_BITFIELD_LEN(SW_CNT)] = {};
      if (ioctl(dev_fd, EVIOCGBIT(0, EV_CNT), dev_ev_capabilities) == -1 ||
          ioctl(dev_fd, EVIOCGBIT(EV_KEY, KEY_CNT), dev_key_capabilities) == -1 ||
          ioctl(dev_fd, EVIOCGBIT(EV_ABS, ABS_CNT), dev_abs_capabilities) == -1 ||
          ioctl(dev_fd, EVIOCGBIT(EV_REL, REL_CNT), dev_rel_capabilities) == -1 ||
          ioctl(dev_fd, EVIOCGBIT(EV_FF, FF_CNT), dev_ff_capabilities) == -1 ||
          ioctl(dev_fd, EVIOCGBIT(EV_SW, SW_CNT), dev_sw_capabilities) == -1)
      {
        EBP();
      }

      char dev_name[256] = {};
      if (ioctl(dev_fd, EVIOCGNAME(sizeof(dev_name)), dev_name) == -1)
      {
        EBP();
      }

      EVDEV_DEVICE_TYPE dev_type = EVDEV_DEVICE_TYPE_IGNORE;

      unsigned long esc_keys_letters_mask = 0xfffffffe;
      if ((dev_key_capabilities[0] & esc_keys_letters_mask) != 0 &&
           EVDEV_BITFIELD_TEST(dev_ev_capabilities, EV_REP))
      {
        dev_type = EVDEV_DEVICE_TYPE_KEYBOARD;
        printf("found keyboard: %s\n", dev_name);
      }
      if (EVDEV_BITFIELD_TEST(dev_sw_capabilities, SW_HEADPHONE_INSERT))
      {
        printf("found headphone: %s\n", dev_name);
      }
      if (EVDEV_BITFIELD_TEST(dev_key_capabilities, BTN_GAMEPAD))
      {
        dev_type = EVDEV_DEVICE_TYPE_GAMEPAD;
        printf("found gamepad: %s\n", dev_name);

        // NOTE(Ryan): Standardize gain across different devices.
        struct input_event gain = {};
        gain.type = EV_FF;
        gain.code = FF_GAIN;
        int percent75 = 0xC000;
        gain.value = percent75;
        if (write(dev_fd, &gain, sizeof(gain)) == -1)
        {
          EBP();
        }

        struct ff_effect rumble_effect = {};
        rumble_effect.type = FF_RUMBLE;
        rumble_effect.id = EVDEV_GAMEPAD_RUMBLE_ID;
	      rumble_effect.u.rumble.strong_magnitude = 0x8000;
	      rumble_effect.u.rumble.weak_magnitude = 0;
	      rumble_effect.replay.length = 5000;
	      rumble_effect.replay.delay = 1000;
        if (ioctl(dev_fd, EVIOCSFF, &rumble_effect) == -1)
        {
          EBP();
        }

        // TODO(Ryan): Rumble does not work?
        int rumble_num_times = 1;
        struct input_event play_rumble = {};
        play_rumble.type = EV_FF;
        play_rumble.code = EVDEV_GAMEPAD_RUMBLE_ID; 
        play_rumble.value = rumble_num_times;
        if (write(dev_fd, (const void *)&play_rumble, sizeof(play_rumble)) == -1)
        {
          EBP();
        }

      }
      if (EVDEV_BITFIELD_TEST(dev_ev_capabilities, EV_REL) &&
          EVDEV_BITFIELD_TEST(dev_rel_capabilities, REL_X) && 
          EVDEV_BITFIELD_TEST(dev_rel_capabilities, REL_Y) &&
          EVDEV_BITFIELD_TEST(dev_key_capabilities, BTN_MOUSE))
      {
        dev_type = EVDEV_DEVICE_TYPE_MOUSE;
        printf("found mouse: %s\n", dev_name);
      }

      if (EVDEV_BITFIELD_TEST(dev_ev_capabilities, EV_ABS) &&
          EVDEV_BITFIELD_TEST(dev_abs_capabilities, ABS_X) && 
          EVDEV_BITFIELD_TEST(dev_abs_capabilities, ABS_Y) &&
          EVDEV_BITFIELD_TEST(dev_key_capabilities, BTN_TOOL_FINGER))
      {
        dev_type = EVDEV_DEVICE_TYPE_MOUSE;
        printf("found touchpad: %s\n", dev_name);
      }

      if (dev_type != EVDEV_DEVICE_TYPE_IGNORE)
      {
        struct epoll_event event = {};
        event.events = EPOLLIN;
        event.data.fd = dev_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dev_fd, &event);
        devices[dev_fd] = dev_type;
      }
      else
      {
        close(dev_fd);
      }
    }
  }
}

INTERNAL bool
alsa_pcm_device_has_playback_subdevice(int ctl_fd, int card_num, int pcm_device_i)
{
  bool has_playback_subdevice = false;

  int pcm_subdevice_i = 0;
  while (true)
  {
    struct snd_pcm_info pcm_info = {};
    pcm_info.device = pcm_device_i;
    pcm_info.subdevice = pcm_subdevice_i;
    pcm_info.card = card_num;
    pcm_info.stream = SNDRV_PCM_STREAM_PLAYBACK;

    if (ioctl(ctl_fd, SNDRV_CTL_IOCTL_PCM_INFO, &pcm_info) == -1)
    {
      bool subdevice_is_not_pcm_playback = (errno == ENOENT);
      if (subdevice_is_not_pcm_playback)
      {
        pcm_subdevice_i++;
        if (pcm_subdevice_i >= (int)pcm_info.subdevices_count) break;
      }
      else EBP();
    }
    else
    {
      has_playback_subdevice = true;
      break;
    }
  }

  return has_playback_subdevice;
}


INTERNAL int
alsa_ctl_find_pcm_playback_device(int ctl_fd)
{
  char pcmp_path[64] = {};
  int pcmp_fd = -1;

  snd_ctl_card_info card_info = {};
  if (ioctl(ctl_fd, SNDRV_CTL_IOCTL_CARD_INFO, &card_info) == -1) EBP();

  int card_num = card_info.card;
  unsigned char *card_driver = card_info.driver;
  unsigned char *card_codec = card_info.mixername;

  int pcm_device_i = 0;
  while (true)
  {
    if (ioctl(ctl_fd, SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE, &pcm_device_i) == -1) EBP();

    bool no_pcm_devices_left = (pcm_device_i < 0);
    if (no_pcm_devices_left) break;

    if (alsa_pcm_device_has_playback_subdevice(ctl_fd, card_num, pcm_device_i))
    {
      // TODO(Ryan): Remove vararg with safer custom version
      sprintf(pcmp_path, "/dev/snd/pcmC%dD%dp", card_num, pcm_device_i);
      printf("using pcm device: %s\n", pcmp_path);
      pcmp_fd = open(pcmp_path, O_RDWR);
      if (pcmp_fd == -1) EBP();
      else break;
    }

    pcm_device_i++;
  }

  return pcmp_fd;
}

INTERNAL void
alsa_find_pcm_playback_devices(int *ctl_fd, int *pcmp_fd)
{
  char ctl_path[64] = {};
  int possible_ctl_fd = -1, possible_pcmp_fd = -1;
  for (int card_i = 0; card_i < 4; ++card_i)
  {
    sprintf(ctl_path, "/dev/snd/controlC%d", card_i);
    if (access(ctl_path, F_OK) == 0)
    {
      possible_ctl_fd = open(ctl_path, O_RDONLY);
      if (possible_ctl_fd == -1) EBP();

      possible_pcmp_fd = alsa_ctl_find_pcm_playback_device(possible_ctl_fd);
      if (possible_pcmp_fd != -1) break;
      else close(possible_ctl_fd);
    }
  }
  
  printf("using control: %s\n", ctl_path);

  *pcmp_fd = possible_pcmp_fd;
  *ctl_fd = possible_ctl_fd;
}

INTERNAL void
alsa_init(void)
{
  // TODO(Ryan): Monitor for events
  // int subscribe = 0;
  // ioctl(ctl_fd, SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS, &subscribe, 0);
  int ctl_fd = 0, pcmp_fd = 0;
  alsa_find_pcm_playback_devices(&ctl_fd, &pcmp_fd);
  if (pcmp_fd == -1 || ctl_fd == -1) BP();

  // write a frame's worth of samples. requires an enforced frame rate.
  // int num_seconds_of_audio = 1;
  // alsa_get_pcm_playback_ring_buffer(pcmp_fd, num_seconds_of_audio);
  // int16 *samples =;
  // *samples++ = LEFT; *samples++ = RIGHT;
  // alsa_write_pcm_playback_buffer(pcmp_fd, buffer);

  // alsa-kernel and alsa-lib

  // we are copying to kernel buffer which then gets copied to device
  // may get single-threaded overrun as copying to device will start immediately
  // getting errors using software parameters to fill this and manually start

  //// 48k samples per second
  //// want 2 channels/stereo to allow for panning effects
  //// write sound slightly ahead of play cursor

  //// hardware parameters
  //snd_pcm_hw_params hw_params = {};
  //for (int hw_param_mask_i = SNDRV_PCM_HW_PARAM_FIRST_MASK;
  //     hw_param_mask_i <= SNDRV_PCM_HW_PARAM_LAST_MASK; 
  //     ++hw_param_mask_i) 
  //{
  //  snd_mask *mask = &hw_params.masks[hw_param_mask_i - SNDRV_PCM_HW_PARAM_FIRST_MASK];
  //  memset(mask, 0xff, sizeof(*mask));

  //  hw_params.cmask |= 1 << hw_param_mask_i;
  //  hw_params.rmask |= 1 << hw_param_mask_i;
  //}
  //for (int hw_param_interval_i = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;
  //     hw_param_interval_i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; 
  //     hw_param_interval_i++)
  //{
  //  snd_interval *interval = &hw_params.intervals[hw_param_interval_i - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
  //  memset(interval, 0x00, sizeof(*interval));
  //  // or UINT_MAX;
  //  interval->max = -1;

  //  hw_params.cmask |= 1 << hw_param_interval_i;
  //  hw_params.rmask |= 1 << hw_param_interval_i;
  //}
  //hw_params.rmask = ~0U;
  //hw_params.cmask = 0;
  //hw_params.info = ~0U;

  //if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_HW_REFINE, &hw_params) == -1)
  //{
  //  EBP();
  //}

  //int access = SNDRV_PCM_ACCESS_RW_INTERLEAVED;
  //snd_mask *mask = &hw_params.masks[SNDRV_PCM_HW_PARAM_ACCESS - SNDRV_PCM_HW_PARAM_FIRST_MASK];

  //int mask_index = access / sizeof(hw_params.masks[0].bits[0]);
  //int mask_offset = access % sizeof(hw_params.masks[0].bits[0]);
  //int mask_num_bits = sizeof(mask->bits) / sizeof(mask->bits[0]);
  //for (int i = 0; i < mask_num_bits; ++i)
  //{
  //  mask->bits[i] = 0x00;
  //}
  //mask->bits[mask_index] &= ~(1 << mask_offset);
  //mask->bits[mask_index] |= (1 << mask_offset);
  //hw_params.rmask |= (1 << SNDRV_PCM_HW_PARAM_ACCESS);
  //// call IOCTL_HW_REFINE again?

  //int format = SNDRV_PCM_FORMAT_S16_LE;
  //SNDDRV_PCM_HW_PARAM_FORMAT;
  //snd_pcm_hw_params_set_format(snd_fd, hwparams, SND_PCM_FORMAT_S16_LE);

  // 
  // more configuring here
  // ...

  // TODO(Ryan): Investigate altering software params to prepare buffer with frames to
  // avoid overrun

  //  write_audio(buf, num_);  
  
//void alsa_audio_write_s16(void *ao, int16_t *buf, uint32_t bufsize)
//{
//    struct snd_xferi xferi;
//    int snd_fd = (int)((size_t)ao);
//	unsigned int i, j;
//    short outbuf[16384];
//    for(i=0; i<bufsize; i++) {
//        outbuf[2*i] = buf[i];
//        outbuf[2*i+1] = buf[i];
//    }
//    xferi.result = 0;
//    xferi.buf = (char*) outbuf;
//    xferi.frames = bufsize;
//    j = ioctl(snd_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xferi);
//	if (j >= 0) {
//		j = xferi.result;
//	}
//    if(j == -32) { //-EPIPE 
//        ioctl(snd_fd, SNDRV_PCM_IOCTL_PREPARE);
//    }
//}
}

int
main(int argc, char *argv[])
{
  // default scheduling policy is SCHED_OTHER round robin, no priority
  // SCHED_FIFO, SCHED_RR are real time scheduling policies with priority 0 - 99
  struct utsname sys_info = {};
  if (uname(&sys_info) == -1) EBP();
  printf("using kernel: %s\n", sys_info.release); 
 
  int our_pid = getpid();
  //int min_niceness = -20;
  //if (setpriority(PRIO_PROCESS, our_pid, min_niceness) == -1) EBP();

  int schedular_policy = sched_getscheduler(our_pid);
  switch (schedular_policy)
  {
    case SCHED_OTHER: puts("scheduler other"); break;
    case SCHED_FIFO: puts("scheduler fifo"); break;
    case SCHED_RR: puts("scheduler rrr"); break;
    default: puts("schedular unknown"); break;
  }

  // don't use realtime scheduling which allows setting static priority as
  // they will restrict time access to other processes (FIFO first in first out,
  // RR maximum quantum)
  struct sched_param param = {};
  if (sched_getparam(our_pid, &param) == -1) EBP();
  printf("current schedular priority: %d\n", param.sched_priority);
  printf("max schedular priority: %d\n", sched_get_priority_max(schedular_policy));
  printf("min schedular priority: %d\n", sched_get_priority_min(schedular_policy));

  Display *xlib_display = XOpenDisplay(NULL);
  if (xlib_display == NULL) BP();

  XSetErrorHandler(xlib_error_handler);
  XSetIOErrorHandler(xlib_io_error_handler);

  int xlib_screen = XDefaultScreen(xlib_display);
  int xlib_desired_screen_depth = 24;
  XVisualInfo xlib_visual_info = {};
  Status xlib_visual_info_status = XMatchVisualInfo(xlib_display, xlib_screen, 
                                                    xlib_desired_screen_depth, TrueColor,
                                                    &xlib_visual_info);
  if (xlib_visual_info_status == False) BP();

  XSetWindowAttributes xlib_window_attr = {};
  int red = 0xee;
  int green = 0xe8;
  int blue = 0xd5;
  xlib_window_attr.background_pixel = (red << 16) | (green << 8) | blue;
  xlib_window_attr.event_mask = StructureNotifyMask;
  Window xlib_root_window = XDefaultRootWindow(xlib_display);
  int xlib_window_x0 = 0;
  int xlib_window_y0 = 0;
  int xlib_window_x1 = 1280;
  int xlib_window_y1 = 720;
  int xlib_window_border_width = 0;
  unsigned long attribute_mask = CWEventMask | CWBackPixel;
  Window xlib_window = XCreateWindow(xlib_display, xlib_root_window,
      xlib_window_x0, xlib_window_y0, xlib_window_x1, xlib_window_y1,
      xlib_window_border_width, xlib_visual_info.depth, InputOutput,
      xlib_visual_info.visual, attribute_mask, &xlib_window_attr);

  XStoreName(xlib_display, xlib_window, "HHF");

  XSizeHints *xlib_size_hints = XAllocSizeHints();
  xlib_size_hints->flags |= PWinGravity;
  xlib_size_hints->win_gravity = 5; // center
  XSetWMNormalHints(xlib_display, xlib_window, xlib_size_hints);
  XFree(xlib_size_hints);

  int present_op = 0, event = 0, error = 0;
  XPresentQueryExtension(xlib_display, &present_op, &event, &error);
  XPresentSelectInput(xlib_display, xlib_window, PresentCompleteNotifyMask);

  XMapWindow(xlib_display, xlib_window); 
  XFlush(xlib_display);

  GC xlib_gc = XDefaultGC(xlib_display, xlib_screen);

  Atom xlib_wm_delete_atom = XInternAtom(xlib_display, "WM_DELETE_WINDOW", False);
  if (xlib_wm_delete_atom == None) BP();
  if (XSetWMProtocols(xlib_display, xlib_window, &xlib_wm_delete_atom, 1) == False) BP();


  XRenderPictFormat *xrender_pic_format = XRenderFindVisualFormat(xlib_display, 
                                                               xlib_visual_info.visual);

  int xlib_window_width = xlib_window_x1 - xlib_window_x0;
  int xlib_window_height = xlib_window_y1 - xlib_window_y0;
  XlibBackBuffer xlib_back_buffer = xlib_create_back_buffer(xlib_display, xlib_window,
                                                            xlib_visual_info, 1280, 720);

  EVDEV_DEVICE_TYPE evdev_devices[RLIMIT_NOFILE] = {};
  int epoll_evdev_fd = epoll_create1(0);
  evdev_populate_devices(epoll_evdev_fd, evdev_devices);

  // NOTE(Ryan): Sound devices picked up with mixer events
  // we are choosing 'always hit' frame rate for audio, i.e. no frame lag, guard thread etc.
  alsa_init();

  // important to get this in at the start to ensure assumptions are correct
  // 0th frame will be arbitrarily long, 
  // IMPORTANT(Ryan): We are not locking the frame rate, rather fixing it from refresh rate.
  // We could change it to something low if users computer is slow (however if 15fps, basically unplayable and better to get new computer)
  int refresh_rate = xrandr_get_active_refresh_rate(xlib_display, xlib_root_window);
  long desired_ns_per_frame = BILLION / (r32)refresh_rate;


  struct timespec prev_timespec = {};
  // what is clock drift?
  // set low nice value to decrease schedular quantum for us
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &prev_timespec) == -1) EBP();
  // this is wall time unaffected by NTP.  (__rtdsc() would be cpu time)

  bool want_to_run = true;
  int x_offset = 0, y_offset = 0;
  while (want_to_run)
  {
    XEvent xlib_event = {};
    while (XPending(xlib_display) > 0)
    {
      XNextEvent(xlib_display, &xlib_event);
      switch (xlib_event.type)
      {
        case ConfigureNotify:
        {
          xlib_window_width = xlib_event.xconfigure.width;
          xlib_window_height = xlib_event.xconfigure.height;
        } break;
        case GenericEvent:
        {
          puts("hi");
        } break;
        case ClientMessage:
        {
          if (xlib_event.xclient.data.l[0] == (long)(xlib_wm_delete_atom))
          {
            XDestroyWindow(xlib_display, xlib_window);
            want_to_run = false;
          }
        } break;
      }
    }

    /*
      if (have_not_updated_this_frame)
      {
        update()
        have_not_updated_this_frame = false;
      }
    */
    
    //while (XCheckTypedWindowEvent(xlib_display, xlib_window, GenericEvent, &xlib_event))
    //{
    //  puts("generic");
    //XGenericEventCookie *cookie = (XGenericEventCookie *)&xlib_event.xcookie;
    //if (cookie->extension == present_op)
    //{
    //  XGetEventData(xlib_display, cookie);
    //  if (cookie->evtype == PresentCompleteNotify)
    //  {
    //    puts("present");
    //    // display();
    //  }
    //  XFreeEventData(xlib_display, cookie);
    //   }
    //}

    // TODO(Ryan): Investigate using _NET_ACTIVE_WINDOW atom
    Window xlib_focused_window = 0;
    int xlib_focused_window_state = 0;
    XGetInputFocus(xlib_display, &xlib_focused_window, &xlib_focused_window_state);
    if (xlib_focused_window == xlib_window)
    {
      struct epoll_event epoll_evdev_events[EPOLL_EVDEV_MAX_EVENTS] = {0};
      int timeout_ms = 1;
      // TODO(Ryan): Should we poll this more frequently?
      int num_epoll_evdev_events = epoll_wait(epoll_evdev_fd, epoll_evdev_events, 
                                              EPOLL_EVDEV_MAX_EVENTS, timeout_ms);
      for (int epoll_evdev_event_i = 0;
          epoll_evdev_event_i < num_epoll_evdev_events; 
          ++epoll_evdev_event_i)
      {
        int dev_fd = epoll_evdev_events[epoll_evdev_event_i].data.fd;
        EVDEV_DEVICE_TYPE dev_type = evdev_devices[dev_fd];

        struct input_event dev_events[32] = {0};
        int dev_event_bytes_read = read(dev_fd, dev_events, sizeof(dev_events));
        if (dev_event_bytes_read == -1)
        {
          // TODO(Ryan): Logging
          EBP();
        }
        int num_dev_events = dev_event_bytes_read / sizeof(dev_events[0]); 
        for (int dev_event_i = 0; dev_event_i < num_dev_events; ++dev_event_i)
        {
          int dev_event_type = dev_events[dev_event_i].type;
          int dev_event_code = dev_events[dev_event_i].code;
          int dev_event_value = dev_events[dev_event_i].value;

          bool is_released = (dev_event_type == EV_KEY ? dev_event_value == 0 : false);
          bool is_down = (dev_event_type == EV_KEY ? dev_event_value == 1 : false);
          bool was_down = (dev_event_type == EV_KEY ? dev_event_value == 2 : false);

          if (dev_type == EVDEV_DEVICE_TYPE_GAMEPAD)
          {
            bool up = (dev_event_code == BTN_DPAD_UP);
            bool right = (dev_event_code == BTN_DPAD_RIGHT);
            bool left = (dev_event_code == BTN_DPAD_LEFT);
            bool down = (dev_event_code == BTN_DPAD_DOWN);
            bool select = (dev_event_code == BTN_SELECT);
            bool start = (dev_event_code == BTN_START);
            bool home = (dev_event_code == BTN_MODE);
            bool left_shoulder = (dev_event_code == BTN_TL);
            bool right_shoulder = (dev_event_code == BTN_TR);
            bool north = (dev_event_code == BTN_NORTH);
            bool east = (dev_event_code == BTN_EAST);
            bool south = (dev_event_code == BTN_SOUTH);
            bool west = (dev_event_code == BTN_WEST);

            int stick_x = (dev_event_code == ABS_X ? dev_event_value : 0);
            int stick_y = (dev_event_code == ABS_Y ? dev_event_value : 0);

            x_offset += stick_x >> 12;
            y_offset += stick_y >> 12;
          }

          if (dev_type == EVDEV_DEVICE_TYPE_KEYBOARD)
          {
            bool w = (dev_event_code == KEY_W);
            bool a = (dev_event_code == KEY_A);
            bool s = (dev_event_code == KEY_S);
            bool d = (dev_event_code == KEY_D);
            bool q = (dev_event_code == KEY_Q);
            bool e = (dev_event_code == KEY_E);
            bool up = (dev_event_code == KEY_UP);
            bool down = (dev_event_code == KEY_DOWN);
            bool left = (dev_event_code == KEY_LEFT);
            bool right = (dev_event_code == KEY_RIGHT);
            bool escape = (dev_event_code == KEY_ESC);
            bool space = (dev_event_code == KEY_SPACE);
            bool enter = (dev_event_code == KEY_ENTER);
            bool ctrl = (dev_event_code == KEY_LEFTCTRL);

            if (a && !was_down)
            {
              printf("a: ");
              if (is_released) 
              {
                printf("was_released");
              }
              else
              {
                printf("is_down");
              }
              printf("\n");
              fflush(stdout);
            }
          }
        }
      }
    }

    render_weird_gradient(&xlib_back_buffer, x_offset, y_offset);

    xrender_display_back_buffer(xlib_display, xrender_pic_format, xlib_window,
                             xlib_gc, xlib_back_buffer, xlib_window_width, 
                             xlib_window_height);

    struct timespec end_timespec = {};
    clock_gettime(CLOCK_MONOTONIC_RAW, &end_timespec);
    long ns_elapsed = timespec_diff(&prev_timespec, &end_timespec);

    long ns_delta = desired_ns_per_frame - ns_elapsed;
    while (timespec_diff(&prev_timespec, &end_timespec) < ns_delta)
    {
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_timespec);
    }

    struct timespec final_timespec = {};
    clock_gettime(CLOCK_MONOTONIC_RAW, &final_timespec);
    printf("ms: %f\n", timespec_diff(&prev_timespec, &final_timespec) / 1000000.0f); 

    prev_timespec = final_timespec;
  }

  return 0;
}

/*
  TODO(Ryan): Use BPF (allows calls to kernel without context switching)
  gcc will compile restricted c code to bpf byte-code which will be checked by bpf-verifier
  have access to bpf helper functions
  kernel contains a bpf interpreter/jit that will execute this
  bpf program will run on some event 

  kprobe event allows us to trace on a kernel routine. if we map a syscall to
  a kernel routine, e.g clone, can monitor when processes are spawned

  #include <linux/filter.h>

  struct sock_fprog filter = {};
  filter.len = num_instructions;
  filter.filter = instructions;

  setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter));

  struct sock_filter instructions[512] = {};
  
  bpf_statement();

  bpf_stmt(ins, &i, BPF_LD|BPF_W|BPF_ABS, offsetof(monitor_netlink_header, magic));

  static void bpf_stmt(struct sock_filter *ins, unsigned *i,
                     unsigned short code, unsigned data) {
        ins[(*i)++] = (struct sock_filter) {
                .code = code,
                .k = data,
        };


  bpf_jmp(ins, &i, BPF_JMP|BPF_JEQ|BPF_K, UDEV_MONITOR_MAGIC, 1, 0);

static void bpf_jmp(struct sock_filter *ins, unsigned *i,
                    unsigned short code, unsigned data,
                    unsigned short jt, unsigned short jf) {
        ins[(*i)++] = (struct sock_filter) {
                .code = code,
                .jt = jt,
                .jf = jf,
                .k = data,
        };
}
}
*/
