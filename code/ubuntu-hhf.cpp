// SPDX-License-Identifier: zlib-acknowledgement

#include <cmath>
#include <cstdio>
#include <cstring> 
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <climits>
#include <cinttypes>

#define INTERNAL static
#define GLOBAL static
#define LOCAL_PERSIST static
#define BILLION 1000000000L

typedef unsigned int uint;

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
  char *errno_msg = strerror(errno);
  if (msg != NULL) printf("EBP: %s (%s)\n", msg, errno_msg); 
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
#include <sys/poll.h>
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

#include <libudev.h>
enum UDEV_DEVICE_TYPE
{
  UDEV_DEVICE_TYPE_IGNORE = 0,
  UDEV_DEVICE_TYPE_KEYBOARD,
  UDEV_DEVICE_TYPE_GAMEPAD,
  UDEV_DEVICE_TYPE_MOUSE,
};
struct UdevPollDevice
{
  UDEV_DEVICE_TYPE type;
  int hhf_i;
};
//struct UdevHotplugDevice
//{
//  char dev_path[64];
//  int fd;
//};
#define MAX_PROCESS_FDS 1024
#define EPOLL_UDEV_MAX_EVENTS 5
#define MAX_UDEV_DEVICES 32

#include <pulse/simple.h>
#include <pulse/error.h>

INTERNAL void
udev_possibly_add_device(int epoll_fd, struct udev_device *device, 
                         UdevPollDevice poll_devices[MAX_PROCESS_FDS],
                         HHFInput *input)
{
  // LOCAL_PERSIST int hotplug_device_cursor = 0;
  LOCAL_PERSIST int hhf_i = 0;

  UDEV_DEVICE_TYPE dev_type = UDEV_DEVICE_TYPE_IGNORE;

  char const *dev_prop = \
    udev_device_get_property_value(device, "ID_INPUT_KEYBOARD");
  if (dev_prop != NULL && strcmp(dev_prop, "1") == 0) dev_type = UDEV_DEVICE_TYPE_KEYBOARD;

  // device_property_val = udev_device_get_property_value(device, "ID_INPUT_TOUCHPAD");
  dev_prop = udev_device_get_property_value(device, "ID_INPUT_MOUSE");
  if (dev_prop != NULL && strcmp(dev_prop, "1") == 0) dev_type = UDEV_DEVICE_TYPE_MOUSE;

  dev_prop = udev_device_get_property_value(device, "ID_INPUT_JOYSTICK");
  if (dev_prop != NULL && strcmp(dev_prop, "1") == 0) dev_type = UDEV_DEVICE_TYPE_GAMEPAD;

  if (dev_type != UDEV_DEVICE_TYPE_IGNORE)
  {
    const char *dev_path = udev_device_get_devnode(device);
    if (dev_path != NULL)
    {
      int dev_fd = open(dev_path, O_RDWR | O_NONBLOCK);
      if (dev_fd == -1) EBP(NULL);

      struct epoll_event event = {};
      event.events = EPOLLIN;
      event.data.fd = dev_fd;
      epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dev_fd, &event);

      UdevPollDevice *dev = &poll_devices[dev_fd];
      dev->type = dev_type;
      // TODO(Ryan): Ensure these don't go over max number for that particular
      // device in HHFInput
      if (dev->type == UDEV_DEVICE_TYPE_GAMEPAD) 
      {
        input->controllers[hhf_i].is_analog = true;
        dev->hhf_i = hhf_i++;
      }

      //strncpy(hotplug_devices[hotplug_device_cursor].dev_path, dev_path, 64);
      // TODO(Ryan): Removing a device won't reset this so continuosly adding and
      // removing a device will cause this to segfault
      //hotplug_devices[hotplug_device_cursor++].fd = dev_fd;
    }
  }

  udev_device_unref(device);
}

INTERNAL void
udev_populate_devices(struct udev *udev_obj, int epoll_fd, 
                      UdevPollDevice poll_devices[MAX_PROCESS_FDS],
                      HHFInput *hhf_input)
{
  struct udev_enumerate *udev_enum = udev_enumerate_new(udev_obj);
  if (udev_enum == NULL) BP(NULL);

  if (udev_enumerate_add_match_subsystem(udev_enum, "input") != 0) BP(NULL);

  if (udev_enumerate_scan_devices(udev_enum) != 0) BP(NULL);

  struct udev_list_entry *udev_entries = udev_enumerate_get_list_entry(udev_enum);
  if (udev_entries == NULL) BP(NULL);

  struct udev_list_entry *udev_entry = NULL;
  udev_list_entry_foreach(udev_entry, udev_entries)
  {
    char const *udev_entry_syspath = udev_list_entry_get_name(udev_entry);
    struct udev_device *device = udev_device_new_from_syspath(udev_obj, 
                                                              udev_entry_syspath);

    udev_possibly_add_device(epoll_fd, device, poll_devices, hhf_input);
  }

  udev_enumerate_unref(udev_enum);
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

INTERNAL void
udev_process_digital_button(HHFInputButtonState *prev_button_state,
                             HHFInputButtonState *cur_button_state,
                             bool ended_down)
{
  cur_button_state->ended_down = ended_down;
  if (cur_button_state->ended_down != prev_button_state->ended_down)
  {
    cur_button_state->half_transition_count++;
  }
}

//INTERVAL void
//udev_check_hotplug_devices(struct udev_monitor *monitor,
//                           UdevPollDevice poll_devices[MAX_PROCESS_FDS], 
//                           UdevHotplugDevice hotplug_devices[MAX_UDEV_DEVICES])
//{
//   int hotplug_fd = udev_monitor_get_fd(monitor);
//   int have_hotplug = 0;
//   do
//   {
//     struct pollfd udev_poll = {};
//     udev_poll.fd = hotplug_fd;
//     udev_poll.events = POLLIN | POLLPRI;
//     have_hotplug = poll(&udev_poll, 1, 0);
//   } while (have_hotplug < 0 && errno == EINTR);
//
//   if (have_hotplug)
//   {
//     struct udev_device *dev = udev_monitor_receive_device(monitor);
//     const char *action = udev_device_get_action(dev);
//     if (strcmp(action, "add") == 0)
//     {
//       udev_possibly_add_device(epoll_fd, dev, poll_devices, hotplug_devices);  
//     }
//     if (strcmp(action, "remove") == 0)
//     {
//       // TODO(Ryan): What to do when this is NULL? perhaps its sys first then dev?
//       const char *dev_path = udev_device_get_devnode(dev);
//       if (dev_path == NULL) BP(NULL); // call again?
//       for (int hotplug_device_i = 0; 
//            hotplug_device_i < MAX_UDEV_DEVICES;
//            ++hotplug_device_i)
//       {
//         UdevHotplugDevice h_dev = hotplug_devices[hotplug_device_i];
//         if (strcmp(h_dev.dev_path, dev_path) == 0)
//         {
//           udev_hotplug_devices[hotplug_device_i] = {};
//           close(h_dev.fd);
//           udev_poll_devices[h_dev.fd] = UDEV_DEVICE_TYPE_IGNORE;
//         }
//       }
//     }
//     udev_device_unref(dev);
//   }
//}

INTERNAL void
udev_check_poll_devices(int epoll_fd, UdevPollDevice poll_devices[MAX_PROCESS_FDS], 
                        HHFInput *prev_input, HHFInput *cur_input)
{
  struct epoll_event epoll_events[EPOLL_UDEV_MAX_EVENTS] = {0};
  int num_epoll_events = epoll_wait(epoll_fd, epoll_events, EPOLL_UDEV_MAX_EVENTS, 0);
  for (int epoll_event_i = 0; epoll_event_i < num_epoll_events; ++epoll_event_i)
  {
    int dev_fd = epoll_events[epoll_event_i].data.fd;
    UdevPollDevice dev = poll_devices[dev_fd];

    struct input_event dev_events[4] = {0};
    int dev_event_bytes_read = read(dev_fd, dev_events, sizeof(dev_events));
    if (dev_event_bytes_read == -1) EBP(NULL);

    int num_dev_events = dev_event_bytes_read / sizeof(dev_events[0]); 
    for (int dev_event_i = 0; dev_event_i < num_dev_events; ++dev_event_i)
    {
      int dev_event_type = dev_events[dev_event_i].type;
      int dev_event_code = dev_events[dev_event_i].code;
      int dev_event_value = dev_events[dev_event_i].value;

      printf("type: %d, code: %d, value: %d\n", dev_event_type, dev_event_code, dev_event_value);
      
      if (dev_event_type == EV_SYN) continue;

      bool was_released = (dev_event_type == EV_KEY ? dev_event_value == 0 : false);
      bool is_down = (dev_event_type == EV_ABS ? dev_event_value == 1 : false);
      bool was_down = (dev_event_type == EV_KEY ? dev_event_value == 2 : false);

      // TODO(Ryan): Cannot vibrate
      if (dev.type == UDEV_DEVICE_TYPE_GAMEPAD)
      {
        HHFInputController *cur_controller_state = &cur_input->controllers[dev.hhf_i];
        HHFInputController *prev_controller_state = &prev_input->controllers[dev.hhf_i];


        // IMPORTANT(Ryan): For some reason, dpad can be analog/digital or both.
        if (dev_event_code == BTN_DPAD_LEFT)
        {
          udev_process_digital_button(&prev_controller_state->left, 
                                      &cur_controller_state->left, is_down);
        }
        bool right = (dev_event_code == BTN_DPAD_RIGHT);
        bool left = (dev_event_code == BTN_DPAD_LEFT);
        bool down = (dev_event_code == BTN_DPAD_DOWN);

        if (dev_event_code == ABS_HAT0X)
        {
          if (dev_event_value < 0)
          {
            udev_process_digital_button(&prev_controller_state->left, 
                                        &cur_controller_state->left, true);
          }
          else if (dev_event_value > 0) 
          {
            udev_process_digital_button(&prev_controller_state->right, 
                                        &cur_controller_state->right, true);
          }
          else
          {
            udev_process_digital_button(&prev_controller_state->left, 
                                        &cur_controller_state->left, false);
            udev_process_digital_button(&prev_controller_state->right, 
                                        &cur_controller_state->right, false);
          }
        }

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

      }

      // TODO(Ryan): hitting keys on different keyboards results in lag
      if (dev.type == UDEV_DEVICE_TYPE_KEYBOARD)
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
      }
    }
  }
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

  int xpresent_op = 0, event = 0, error = 0;
  XPresentQueryExtension(xlib_display, &xpresent_op, &event, &error);
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

  HHFInput hhf_cur_input = {}, hhf_prev_input = {};

  struct udev *udev_obj = udev_new();
  if (udev_obj == NULL) BP(NULL);

  //UdevHotplugDevice udev_hotplug_devices[MAX_UDEV_DEVICES] = {};
  UdevPollDevice udev_poll_devices[MAX_PROCESS_FDS] = {};
  int epoll_udev_fd = epoll_create1(0);
  udev_populate_devices(udev_obj, epoll_udev_fd, udev_poll_devices, &hhf_cur_input);

  //struct udev_monitor *udev_mon = udev_monitor_new_from_netlink(udev_obj, "udev");
  //udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "input", NULL);
  //udev_monitor_enable_receiving(udev_mon);

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

  // TODO(Ryan):
  // this incurs a 67Mib allocation.
  // libpulse allocates an additional 200Mib.
  // why!?
  // frequency seems to change after a period of long time running
  pa_simple *pulse_player = pa_simple_new(NULL, "HHF", PA_STREAM_PLAYBACK, NULL, 
                                          "HHF Sound", &pulse_spec, NULL, NULL,
                                          &pulse_error_code);
  if (pulse_player == NULL) BP(pa_strerror(pulse_error_code));

  int pulse_buffer_num_base_samples = pulse_samples_per_second * frame_dt; 
  int pulse_buffer_num_samples =  pulse_buffer_num_base_samples * pulse_num_channels;
  s16 pulse_buffer[pulse_buffer_num_samples] = {};

  HHFSoundBuffer hhf_sound_buffer = {};
  hhf_sound_buffer.samples_per_second = pulse_samples_per_second;
  hhf_sound_buffer.samples = pulse_buffer;
  hhf_sound_buffer.num_samples = pulse_buffer_num_base_samples; 

  xrender_xpresent_back_buffer(xlib_display, xlib_window, xlib_gc,
                               xrandr_active_crtc.crtc, &xlib_back_buffer, 
                               xlib_window_width, xlib_window_height);

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

      if (xlib_event.xclient.data.l[0] == (long)(xlib_wm_delete_atom))
      {
        XDestroyWindow(xlib_display, xlib_window);
        want_to_run = false;
        break;
      }

      // udev_check_hotplug_devices();

        udev_check_poll_devices(epoll_udev_fd, udev_poll_devices, &hhf_prev_input, 
                                &hhf_cur_input);

      Window xlib_focused_window = 0;
      int xlib_focused_window_state = 0;
      XGetInputFocus(xlib_display, &xlib_focused_window, &xlib_focused_window_state);
      if (xlib_focused_window == xlib_window)
      {
        udev_check_poll_devices(epoll_udev_fd, udev_poll_devices, &hhf_prev_input, 
                                &hhf_cur_input);
      }

      if (xlib_event.type == GenericEvent)
      {
        XGenericEventCookie *cookie = (XGenericEventCookie *)&xlib_event.xcookie;
        if (cookie->extension == xpresent_op)
        {
          XGetEventData(xlib_display, cookie);
          if (cookie->evtype == PresentCompleteNotify)
          {
            hhf_update_and_render(&hhf_back_buffer, &hhf_sound_buffer, &hhf_cur_input);

            if (pa_simple_write(pulse_player, pulse_buffer, sizeof(pulse_buffer), 
                                &pulse_error_code) < 0) BP(pa_strerror(pulse_error_code));

            xrender_xpresent_back_buffer(xlib_display, xlib_window, xlib_gc,
                                         xrandr_active_crtc.crtc, &xlib_back_buffer,
                                         xlib_window_width, xlib_window_height);

            hhf_prev_input = hhf_cur_input;
            hhf_cur_input = {};
            for (int controller_i = 0; 
                 controller_i < HHF_INPUT_MAX_NUM_CONTROLLERS;
                 controller_i++)
            {
              HHFInputController *cur_controller = &hhf_cur_input.controllers[controller_i];
              HHFInputController *prev_controller = &hhf_prev_input.controllers[controller_i];
              cur_controller->is_analog = prev_controller->is_analog;
              for (int controller_button_i = 0; 
                  controller_button_i < HHF_INPUT_NUM_CONTROLLER_BUTTONS;
                  controller_button_i++)
              {
                cur_controller->buttons[controller_button_i].ended_down = \
                  prev_controller->buttons[controller_button_i].ended_down;
              }
            }
            
            u64 end_cycle_count = __rdtsc();
            struct timespec end_timespec = {};
            clock_gettime(CLOCK_MONOTONIC_RAW, &end_timespec);
            //printf("ms per frame: %.02f\n", timespec_diff(&prev_timespec, &end_timespec) / 1000000.0f); 
            //printf("mega cycles per frame: %.02f\n", (r64)(end_cycle_count - prev_cycle_count) / 1000000.0f); 

            prev_timespec = end_timespec;
            prev_cycle_count = end_cycle_count;
          }
          XFreeEventData(xlib_display, cookie);
        }
      }

    } 

  }

  return 0;
}
