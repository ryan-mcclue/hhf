// SPDX-License-Identifier: zlib-acknowledgement

#include <cmath>
#include <cstdio>
#include <cstring> 
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <climits>

#define INTERNAL static
#define GLOBAL static
#define LOCAL_PERSIST static
#define BILLION 1000000000L

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
// NOTE(Ryan): This is to avoid compiler adjusting a value like 1234 to 1 which it
//             would have to do if assigning to a bool.
typedef u32 b32;
typedef float r32;
typedef double r64;

#if defined(HHF_DEV)
INTERNAL void __bp(char const *msg)
{ 
  if (msg != NULL) printf("BP: %s\n", msg);
  return; 
}
INTERNAL void __ebp(char const *msg)
{ 
  if (msg != NULL) printf("EBP: %s (%s)\n", msg, strerror(errno)); 
  return;
}
#define BP(msg) __bp(msg)
#define EBP(msg) __ebp(msg)
#else
#define BP(msg)
#define EBP(msg)
#endif

#define ARRAY_LEN(arr) \
  (sizeof(arr)/sizeof(arr[0]))

#include "hhf.h"
#include "hhf.cpp"

// platform specific last as OS may #define crazy things that override us

#include <x86intrin.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xpresent.h>
#include <X11/extensions/Xfixes.h>

#include <pulse/simple.h>
#include <pulse/error.h>



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
#define MAX_PROCESS_FDS RLIMIT_NOFILE
#define EPOLL_EVDEV_MAX_EVENTS 5

INTERNAL void 
evdev_populate_devices(int epoll_fd, EVDEV_DEVICE_TYPE devices[MAX_PROCESS_FDS])
{
  char dev_path[64] = {};
  for (int ev_id = 0; ev_id < 64; ++ev_id)
  {
    sprintf(dev_path, "/dev/input/event%d", ev_id);
    if (access(dev_path, F_OK) == 0)
    {
      int dev_fd = open(dev_path, O_RDWR);
      if (dev_fd == -1) EBP(NULL);

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
        EBP(NULL);
      }

      char dev_name[256] = {};
      if (ioctl(dev_fd, EVIOCGNAME(sizeof(dev_name)), dev_name) == -1) EBP(NULL);

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

INTERNAL long
timespec_diff(struct timespec *start, struct timespec *end)
{
  return (BILLION * (end->tv_sec - start->tv_sec)) +
         (end->tv_nsec - start->tv_nsec);
}

struct XlibPresentPixmap
{
  Pixmap pixmap;
  int width, height;
  u32 serial;
  XserverRegion region;
  XRectangle *rect;
};

struct XlibRenderPict
{
  Picture src_pict, dst_pict;
  XRenderPictFormat *fmt;
  XRenderPictureAttributes attr;
  XTransform transform_matrix;
};

struct XlibBackBuffer
{
  XImage *image;
  Pixmap pixmap;
  XVisualInfo visual_info;
  XlibRenderPict render_pict;
  XlibPresentPixmap present_pixmap;
  // NOTE(Ryan): Memory order: XX RR GG BB
  u8 *memory;
  int width;
  int height;
};

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

  BP(NULL);

  return 1;
}

INTERNAL int
xlib_io_error_handler(Display *display)
{
  BP(NULL);

  return 1;
}

INTERNAL void
xlib_back_buffer_update_render_pict(Display *display, XlibBackBuffer *back_buffer, 
                                    int window_width, int window_height)
{
  bool are_initializing = (back_buffer->render_pict.fmt == NULL);
  if (are_initializing)
  {
    back_buffer->render_pict.fmt = XRenderFindVisualFormat(display, back_buffer->visual_info.visual);
  }
  else
  {
    XRenderFreePicture(display, back_buffer->render_pict.src_pict);
    XRenderFreePicture(display, back_buffer->render_pict.dst_pict);
  }

  back_buffer->render_pict.src_pict = XRenderCreatePicture(display, back_buffer->pixmap, 
                                          back_buffer->render_pict.fmt, 0, 
                                          &back_buffer->render_pict.attr);
  back_buffer->render_pict.dst_pict = \
    XRenderCreatePicture(display, back_buffer->present_pixmap.pixmap, 
                         back_buffer->render_pict.fmt, 0, 
                         &back_buffer->render_pict.attr);

  double x_scale = back_buffer->width / (double)window_width;
  double y_scale = back_buffer->height / (double)window_height;
  back_buffer->render_pict.transform_matrix = {{
    {XDoubleToFixed(x_scale), XDoubleToFixed(0), XDoubleToFixed(0)},
    {XDoubleToFixed(0), XDoubleToFixed(y_scale), XDoubleToFixed(0)},
    {XDoubleToFixed(0), XDoubleToFixed(0), XDoubleToFixed(1)}  
  }};
  XRenderSetPictureTransform(display, back_buffer->render_pict.src_pict, 
                             &back_buffer->render_pict.transform_matrix);
}

INTERNAL void
xlib_back_buffer_update_present_pixmap(Display *display, Window window, XlibBackBuffer *back_buffer, 
                                       int window_width, int window_height)
{ 
  bool are_initializing = (back_buffer->present_pixmap.rect == NULL);
  if (are_initializing)
  {
    back_buffer->present_pixmap.rect = (XRectangle *)malloc(sizeof(XRectangle));
    back_buffer->present_pixmap.rect->x = 0;
    back_buffer->present_pixmap.rect->y = 0;
  }
  else
  {
    XFreePixmap(display, back_buffer->present_pixmap.pixmap);
    XFixesDestroyRegion(display, back_buffer->present_pixmap.region);
  }

  back_buffer->present_pixmap.pixmap = XCreatePixmap(display, window,
                                                     window_width, window_height,
                                                     back_buffer->visual_info.depth);
  back_buffer->present_pixmap.width = window_width;
  back_buffer->present_pixmap.height = window_height;
  back_buffer->present_pixmap.rect->width = window_width;
  back_buffer->present_pixmap.rect->height = window_height;
  back_buffer->present_pixmap.region = \
    XFixesCreateRegion(display, back_buffer->present_pixmap.rect, 1);
}

INTERNAL XlibBackBuffer
xlib_create_back_buffer(Display *display, XVisualInfo visual_info, Window window, 
                        int window_width, int window_height, int width, int height)
{ 
  XlibBackBuffer back_buffer = {};
  back_buffer.visual_info = visual_info;

  int bytes_per_pixel = 4;
  int fd = -1;
  int offset = 0;
  back_buffer.memory = (u8 *)mmap(NULL, width * height * bytes_per_pixel, 
                                  PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 
                                  fd, offset);
  if (back_buffer.memory == NULL) EBP(NULL);

  back_buffer.width = width;
  back_buffer.height = height;

  int image_offset = 0;
  int image_scanline_offset = 0;
  int image_pad_bits = 32;
  back_buffer.image = XCreateImage(display, visual_info.visual, visual_info.depth,
                            ZPixmap, image_offset, (char *)back_buffer.memory, width, height,
                            image_pad_bits, image_scanline_offset);
  if (back_buffer.image == NULL) BP(NULL);

  back_buffer.pixmap = XCreatePixmap(display, window,
                                     back_buffer.width, back_buffer.height,
                                     visual_info.depth);

  xlib_back_buffer_update_present_pixmap(display, window, &back_buffer, window_width, window_height);
  xlib_back_buffer_update_render_pict(display, &back_buffer, window_width, window_height);

  return back_buffer;
}

INTERNAL void
xrender_xpresent_back_buffer(Display *display, Window window, GC gc, RRCrtc crtc, 
                             XlibBackBuffer *back_buffer, int window_width, int window_height)
{
  XPutImage(display, back_buffer->pixmap, gc, back_buffer->image, 
          0, 0, 0, 0, back_buffer->width, back_buffer->height);

  if (back_buffer->present_pixmap.width != window_width ||
      back_buffer->present_pixmap.height != window_height)
  {
    xlib_back_buffer_update_present_pixmap(display, window, back_buffer, window_width, 
                                           window_height);
    xlib_back_buffer_update_render_pict(display, back_buffer, window_width, window_height);
  }
  
  XRenderComposite(display, PictOpSrc, back_buffer->render_pict.src_pict, 0, 
                   back_buffer->render_pict.dst_pict, 0, 0, 0, 0, 0, 0,
                   window_width, window_height);
  
  XPresentPixmap(display, window, back_buffer->present_pixmap.pixmap, 
                 back_buffer->present_pixmap.serial++, 
                 None, back_buffer->present_pixmap.region, 0, 0, crtc, None, None, 
                 PresentOptionNone, 0, 1, 0, NULL, 0);
}

struct XrandrActiveCRTC
{
  RRCrtc crtc;
  int refresh_rate;
};

INTERNAL XrandrActiveCRTC
xrandr_get_active_crtc(Display *display, Window root_window)
{
  XrandrActiveCRTC active_crtc = {};

  XRRScreenResources *screen_resources = XRRGetScreenResources(display, root_window); 
  if (screen_resources == NULL) BP(NULL);

  RRMode active_mode_id = 0;
  for (int crtc_num = 0; crtc_num < screen_resources->ncrtc; ++crtc_num) 
  {
    RRCrtc crtc = screen_resources->crtcs[crtc_num];
    XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, screen_resources, crtc);
    if (crtc_info == NULL) BP(NULL);

    if (crtc_info->mode != None)
    {
      active_mode_id = crtc_info->mode;
      active_crtc.crtc = crtc;
      break;
    }
  }

  int refresh_rate = 0;
  for (int mode_num = 0; mode_num < screen_resources->nmode; ++mode_num) 
  {
    XRRModeInfo mode_info = screen_resources->modes[mode_num];
    if (mode_info.id == active_mode_id)
    {
      active_crtc.refresh_rate = (r32)mode_info.dotClock / 
                                 (mode_info.hTotal * mode_info.vTotal);
    }
  }

  return active_crtc;
}


int
main(int argc, char *argv[])
{
  Display *xlib_display = XOpenDisplay(NULL);
  if (xlib_display == NULL) BP(NULL);

  XSetErrorHandler(xlib_error_handler);
  XSetIOErrorHandler(xlib_io_error_handler);

  int xlib_screen = XDefaultScreen(xlib_display);
  int xlib_desired_screen_depth = 24;
  XVisualInfo xlib_visual_info = {};
  Status xlib_visual_info_status = XMatchVisualInfo(xlib_display, xlib_screen, 
                                                    xlib_desired_screen_depth, TrueColor,
                                                    &xlib_visual_info);
  if (xlib_visual_info_status == False) BP(NULL);

  XSetWindowAttributes xlib_window_attr = {};
  int red = 0xee;
  int green = 0xe8;
  int blue = 0xd5;
  xlib_window_attr.background_pixel = (red << 16) | (green << 8) | blue;
  xlib_window_attr.bit_gravity = StaticGravity;
  xlib_window_attr.event_mask = StructureNotifyMask;
  Window xlib_root_window = XDefaultRootWindow(xlib_display);
  int xlib_window_x0 = 0;
  int xlib_window_y0 = 0;
  int xlib_window_x1 = 1280;
  int xlib_window_y1 = 720;
  int xlib_window_border_width = 0;
  unsigned long attribute_mask = CWEventMask | CWBackPixel | CWBitGravity;
  Window xlib_window = XCreateWindow(xlib_display, xlib_root_window,
      xlib_window_x0, xlib_window_y0, xlib_window_x1, xlib_window_y1,
      xlib_window_border_width, xlib_visual_info.depth, InputOutput,
      xlib_visual_info.visual, attribute_mask, &xlib_window_attr);

  XStoreName(xlib_display, xlib_window, "HHF");

  int present_op = 0, event = 0, error = 0;
  XPresentQueryExtension(xlib_display, &present_op, &event, &error);
  XPresentSelectInput(xlib_display, xlib_window, PresentCompleteNotifyMask);

  XMapWindow(xlib_display, xlib_window); 
  XFlush(xlib_display);

  GC xlib_gc = XDefaultGC(xlib_display, xlib_screen);

  Atom xlib_wm_delete_atom = XInternAtom(xlib_display, "WM_DELETE_WINDOW", False);
  if (xlib_wm_delete_atom == None) BP(NULL);
  if (XSetWMProtocols(xlib_display, xlib_window, &xlib_wm_delete_atom, 1) == False) BP(NULL);

  int xlib_window_width = xlib_window_x1 - xlib_window_x0;
  int xlib_window_height = xlib_window_y1 - xlib_window_y0;
  int xlib_back_buffer_width = 1280;
  int xlib_back_buffer_height = 720;
  XlibBackBuffer xlib_back_buffer = \
    xlib_create_back_buffer(xlib_display, xlib_visual_info, xlib_window,
                            xlib_window_width, xlib_window_height,
                            xlib_back_buffer_width, xlib_back_buffer_height);
  HHFBackBuffer hhf_back_buffer = {};
  hhf_back_buffer.width = xlib_back_buffer.width;
  hhf_back_buffer.height = xlib_back_buffer.height;
  hhf_back_buffer.memory = xlib_back_buffer.memory;

  EVDEV_DEVICE_TYPE evdev_devices[MAX_PROCESS_FDS] = {};
  int epoll_evdev_fd = epoll_create1(0);
  evdev_populate_devices(epoll_evdev_fd, evdev_devices);

  XrandrActiveCRTC xrandr_active_crtc = xrandr_get_active_crtc(xlib_display, 
                                                               xlib_root_window);
  r32 frame_dt = 1.0f / xrandr_active_crtc.refresh_rate;

  int pulse_samples_per_second = 44100;
  int pulse_num_channels = 2;
  int pulse_error_code = 0;
  pa_sample_spec pulse_spec = {};
  pulse_spec.format = PA_SAMPLE_S16LE;
  pulse_spec.rate = pulse_samples_per_second;
  pulse_spec.channels = pulse_num_channels;

  pa_simple *pulse_player = pa_simple_new(NULL, "HHF", PA_STREAM_PLAYBACK, NULL, 
                                          "HHF Sound", &pulse_spec, NULL, NULL,
                                          &pulse_error_code);
  if (pulse_player == NULL) BP(pa_strerror(pulse_error_code));

  int pulse_buffer_num_base_samples = pulse_samples_per_second * frame_dt; 
  int pulse_buffer_num_samples =  pulse_buffer_num_base_samples * pulse_num_channels;
  s16 pulse_buffer[pulse_buffer_num_samples] = {};


  xrender_xpresent_back_buffer(xlib_display, xlib_window, xlib_gc,
                               xrandr_active_crtc.crtc, &xlib_back_buffer, xlib_window_width, 
                               xlib_window_height);

  u64 prev_cycle_count = __rdtsc();
  struct timespec prev_timespec = {};
  clock_gettime(CLOCK_MONOTONIC_RAW, &prev_timespec);

  bool want_to_run = true;
  int x_offset = 0, y_offset = 0;
  while (want_to_run)
  {
    XEvent xlib_event = {};
    while (XPending(xlib_display) > 0)
    {
      XNextEvent(xlib_display, &xlib_event);
      if (xlib_event.type == ConfigureNotify)
      {
        xlib_window_width = xlib_event.xconfigure.width;
        xlib_window_height = xlib_event.xconfigure.height;
      }
      if (xlib_event.type == ClientMessage)
      {
        if (xlib_event.xclient.data.l[0] == (long)(xlib_wm_delete_atom))
        {
          XDestroyWindow(xlib_display, xlib_window);
          want_to_run = false;
          break;
        }
      }
      if (xlib_event.type == GenericEvent)
      {
        XGenericEventCookie *cookie = (XGenericEventCookie *)&xlib_event.xcookie;
        if (cookie->extension == present_op)
        {
          XGetEventData(xlib_display, cookie);
          if (cookie->evtype == PresentCompleteNotify)
          {
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
                if (dev_event_bytes_read == -1) EBP(NULL);

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

            double rad = 0.0;
            s16 *pulse_samples = pulse_buffer;
            for (int pulse_buffer_sample_i = 0; 
                 pulse_buffer_sample_i < pulse_buffer_num_base_samples;
                 pulse_buffer_sample_i++)
            {
              double val = sin(rad) * 32767;
              *pulse_samples++ = val;
              *pulse_samples++ = val;

              rad += 0.1;
              rad = fmod(rad, 2.0 * M_PI);
            }
            if (pa_simple_write(pulse_player, pulse_buffer, sizeof(pulse_buffer), 
                                &pulse_error_code) < 0) BP(pa_strerror(pulse_error_code));
            // should call pa_simple_drain(pulse_player, &pulse_error_code)?
            
            hhf_update_and_render(&hhf_back_buffer);

            u64 end_cycle_count = __rdtsc();
            struct timespec end_timespec = {};
            clock_gettime(CLOCK_MONOTONIC_RAW, &end_timespec);
            printf("ms per frame: %.02f\n", timespec_diff(&prev_timespec, &end_timespec) / 1000000.0f); 
            printf("mega cycles per frame: %.02f\n", (r64)(end_cycle_count - prev_cycle_count) / 1000000.0f); 

            prev_timespec = end_timespec;
            prev_cycle_count = end_cycle_count;
            
            xrender_xpresent_back_buffer(xlib_display, xlib_window, xlib_gc,
                                         xrandr_active_crtc.crtc, &xlib_back_buffer,
                                         xlib_window_width, xlib_window_height);
          }
          XFreeEventData(xlib_display, cookie);
        }
      }
    }
  }

  return 0;
}
