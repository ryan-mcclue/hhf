// SPDX-License-Identifier: zlib-acknowledgement

#include "hhf.h"

#include <x86intrin.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>

#include <dlfcn.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xpresent.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xcursor/Xcursor.h>
#define _NET_WM_STATE_TOGGLE (2)

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

// TODO(Ryan): Investigate using $(pasuspender -- ./build/ubuntu-hhf) to allow ALSA usage directly
#include <pulse/simple.h>
#include <pulse/error.h>

GLOBAL bool want_to_run = true;

struct XlibInfo 
{
  Display *display;
  Window root, window;
  int window_width, window_height;
  Atom state, maxh, maxv, fullscreen;
};

INTERNAL void
udev_possibly_add_device(int epoll_fd, struct udev_device *device, 
                         UdevPollDevice poll_devices[MAX_PROCESS_FDS],
                         HHFInput *input)
{
  LOCAL_PERSIST int hhf_i = 0;

  UDEV_DEVICE_TYPE dev_type = UDEV_DEVICE_TYPE_IGNORE;

  char *dev_prop = (char *)udev_device_get_property_value(device, "ID_INPUT_KEYBOARD");
  if (dev_prop != NULL && strcmp(dev_prop, "1") == 0) dev_type = UDEV_DEVICE_TYPE_KEYBOARD;

  // device_property_val = udev_device_get_property_value(device, "ID_INPUT_TOUCHPAD");
  dev_prop = (char *)udev_device_get_property_value(device, "ID_INPUT_MOUSE");
  if (dev_prop != NULL && strcmp(dev_prop, "1") == 0) dev_type = UDEV_DEVICE_TYPE_MOUSE;

  dev_prop = (char *)udev_device_get_property_value(device, "ID_INPUT_JOYSTICK");
  if (dev_prop != NULL && strcmp(dev_prop, "1") == 0) dev_type = UDEV_DEVICE_TYPE_GAMEPAD;

  if (dev_type != UDEV_DEVICE_TYPE_IGNORE)
  {
    char *dev_path = (char *)udev_device_get_devnode(device);
    if (dev_path != NULL)
    {
      int dev_fd = open(dev_path, O_RDWR | O_NONBLOCK);
      if (dev_fd == -1) EBP(NULL);

      struct epoll_event event = {};
      event.events = EPOLLIN;
      event.data.fd = dev_fd;
      epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dev_fd, &event);

      if (dev_type != UDEV_DEVICE_TYPE_MOUSE)
      {
        UdevPollDevice *dev = &poll_devices[dev_fd];
        dev->type = dev_type;

        ASSERT(hhf_i < HHF_INPUT_MAX_NUM_CONTROLLERS);
        input->controllers[hhf_i].is_connected = true;
        if (dev->type == UDEV_DEVICE_TYPE_GAMEPAD) 
        {
          input->controllers[hhf_i].is_analog = true;
        }
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

// TODO(Ryan): Replace with raw evdev. Limiting solely using udev as "input" is presumabley only HID
// Udev only of use with hotplugging.
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
    char *udev_entry_syspath = (char *)udev_list_entry_get_name(udev_entry);
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

  // TODO(Ryan): Implement scaling ourselves (resampling)
  // NOTE(Ryan): Could offset game display here
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

INTERNAL void
udev_process_analog_button(HHFInputButtonState *prev_button_state_neg,
                           HHFInputButtonState *cur_button_state_neg,
                           HHFInputButtonState *prev_button_state_pos,
                           HHFInputButtonState *cur_button_state_pos,
                           int value)
{
  if (value < 0)
  {
    udev_process_digital_button(prev_button_state_neg, cur_button_state_neg, true);
  }
  else if (value > 0) 
  {
    udev_process_digital_button(prev_button_state_pos, cur_button_state_pos, true);
  }
  else
  {
    udev_process_digital_button(prev_button_state_neg, cur_button_state_neg, false);
    udev_process_digital_button(prev_button_state_pos, cur_button_state_pos, false);
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

struct RecordingState
{
  bool are_recording;
  bool are_playing;

  void *mem;
  int mem_size;

  void *input;
  int max_input_size;
  int input_bytes_written;
  int input_bytes_read;
};

INTERNAL void
udev_check_poll_devices(int epoll_fd, UdevPollDevice poll_devices[MAX_PROCESS_FDS], 
                        HHFInput *prev_input, HHFInput *cur_input,
                        XlibInfo *info, HHFMemory *hhf_memory, RecordingState *recording_state)
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
      u16 dev_event_type = dev_events[dev_event_i].type;
      u16 dev_event_code = dev_events[dev_event_i].code;
      s32 dev_event_value = dev_events[dev_event_i].value;

      //if (dev_event_type == EV_KEY)
      //{
      //   printf("type: %" PRIu16 " code: %" PRIu16 ", value: %" PRId32"\n", dev_event_type, dev_event_code, dev_event_value);
      //}
      
      if (dev_event_type == EV_SYN) continue;

      bool was_released = (dev_event_type == EV_KEY ? dev_event_value == 0 : false);
      bool first_down = (dev_event_type == EV_KEY ? dev_event_value == 1 : false);
      bool was_down = (dev_event_type == EV_KEY ? dev_event_value == 2 : false);
      bool is_down = (first_down || was_down);

      if (dev_event_code == BTN_LEFT) cur_input->mouse_left = is_down;
      if (dev_event_code == BTN_RIGHT) cur_input->mouse_right = is_down;
      if (dev_event_code == BTN_MIDDLE) cur_input->mouse_middle = is_down;
      if (dev_event_code == REL_X) 
      {
        if (cur_input->mouse_x + dev_event_value < 0)
        {
          cur_input->mouse_x = 0;
        }
        else if (cur_input->mouse_x + dev_event_value > info->window_width)
        {
          cur_input->mouse_x = info->window_width;
        }
        else
        {
          cur_input->mouse_x += dev_event_value;
        }
      }
      if (dev_event_code == REL_Y) 
      {
        if (cur_input->mouse_y + dev_event_value < 0)
        {
          cur_input->mouse_y = 0;
        }
        else if (cur_input->mouse_y + dev_event_value > info->window_height)
        {
          cur_input->mouse_y = info->window_height;
        }
        else
        {
          cur_input->mouse_y += dev_event_value;
        }
      }
      if (dev_event_code == REL_WHEEL) cur_input->mouse_wheel += dev_event_value;

      HHFInputController *cur_controller_state = &cur_input->controllers[dev.hhf_i];
      HHFInputController *prev_controller_state = &prev_input->controllers[dev.hhf_i];

      // TODO(Ryan): Gamepad cannot vibrate
      if (dev_event_code == BTN_DPAD_LEFT || dev_event_code == KEY_A)
      {
        udev_process_digital_button(&prev_controller_state->move_left, 
                                    &cur_controller_state->move_left, is_down);
      }
      if (dev_event_code == BTN_DPAD_RIGHT || dev_event_code == KEY_D)
      {
        udev_process_digital_button(&prev_controller_state->move_right, 
                                    &cur_controller_state->move_right, is_down);
      }
      if (dev_event_code == BTN_DPAD_UP || dev_event_code == KEY_W)
      {
        udev_process_digital_button(&prev_controller_state->move_up, 
                                    &cur_controller_state->move_up, is_down);
      }
      if (dev_event_code == BTN_DPAD_DOWN || dev_event_code == KEY_S)
      {
        udev_process_digital_button(&prev_controller_state->move_down, 
                                    &cur_controller_state->move_down, is_down);
      }
      // IMPORTANT(Ryan): For some reason, dpad can be analog/digital or both.
      if (dev_event_code == ABS_HAT0X)
      {
        udev_process_analog_button(&prev_controller_state->move_left, 
                                   &cur_controller_state->move_left,
                                   &prev_controller_state->move_right,
                                   &cur_controller_state->move_right,
                                   dev_event_value);
      }
      if (dev_event_code == ABS_HAT0Y)
      {
        udev_process_analog_button(&prev_controller_state->move_up, 
                                   &cur_controller_state->move_up,
                                   &prev_controller_state->move_down,
                                   &cur_controller_state->move_down,
                                   dev_event_value);
      }

      // TODO(Ryan): evdev doesn't expose deadzone.
      // Perhaps it is handled for us? (XInput deadzone is significant, e.g. 25% of range)
      // If we were to handle, we would assume a rectangular deadzone (as oppose to circular)
      if (dev_event_code == ABS_X)
      {
        cur_controller_state->average_stick_x = (dev_event_value >= 0 ? 
                                                ((r32)dev_event_value / INT32_MAX) : 
                                                ((r32)dev_event_value / INT32_MIN));
      }
      if (dev_event_code == ABS_Y)
      {
        cur_controller_state->average_stick_y = (dev_event_value >= 0 ? 
                                                ((r32)dev_event_value / INT32_MAX) : 
                                                ((r32)dev_event_value / INT32_MIN));
      }

      if (dev_event_code == BTN_TL || dev_event_code == KEY_Q)
      {
        udev_process_digital_button(&prev_controller_state->left_shoulder,
                                    &cur_controller_state->left_shoulder, is_down);
      }
      if (dev_event_code == BTN_TR || dev_event_code == KEY_E)
      {
        udev_process_digital_button(&prev_controller_state->right_shoulder,
                                    &cur_controller_state->right_shoulder, is_down);
      }

      if (dev_event_code == BTN_NORTH || dev_event_code == KEY_UP)
      {
        udev_process_digital_button(&prev_controller_state->action_up,
                                    &cur_controller_state->action_up, is_down);
      }
      if (dev_event_code == BTN_SOUTH || dev_event_code == KEY_DOWN)
      {
        udev_process_digital_button(&prev_controller_state->action_down,
                                    &cur_controller_state->action_down, is_down);
      }
      if (dev_event_code == BTN_EAST || dev_event_code == KEY_RIGHT)
      {
        udev_process_digital_button(&prev_controller_state->action_right,
                                    &cur_controller_state->action_right, is_down);
      }
      if (dev_event_code == BTN_WEST || dev_event_code == KEY_LEFT)
      {
        udev_process_digital_button(&prev_controller_state->action_left,
                                    &cur_controller_state->action_left, is_down);
      }
      if (dev_event_code == BTN_SELECT || dev_event_code == KEY_SPACE)
      {
        udev_process_digital_button(&prev_controller_state->back,
                                    &cur_controller_state->back, is_down);
      }
      if (dev_event_code == BTN_START || dev_event_code == KEY_ESC)
      {
        udev_process_digital_button(&prev_controller_state->start,
                                    &cur_controller_state->start, is_down);
      }
#if defined(HHF_INTERNAL)
      if (dev_event_code == KEY_F10 && first_down)
      {
        XClientMessageEvent maximise_ev = {};
        maximise_ev.type = ClientMessage;
        maximise_ev.format = 32;
        maximise_ev.window = info->window;
        maximise_ev.message_type = info->state;
        maximise_ev.data.l[0] = _NET_WM_STATE_TOGGLE; 
        maximise_ev.data.l[1] = info->maxh;
        maximise_ev.data.l[2] = info->maxv;
        maximise_ev.data.l[3] = 1;
        
        XSendEvent(info->display, info->root, False, 
                   SubstructureNotifyMask, (XEvent *)&maximise_ev);
      }
      if (dev_event_code == KEY_F11 && first_down)
      {
        XClientMessageEvent maximise_ev = {};
        maximise_ev.type = ClientMessage;
        maximise_ev.format = 32;
        maximise_ev.window = info->window;
        maximise_ev.message_type = info->state;
        maximise_ev.data.l[0] = _NET_WM_STATE_TOGGLE; 
        maximise_ev.data.l[1] = info->fullscreen;
        maximise_ev.data.l[3] = 1;
        
        XSendEvent(info->display, info->root, False, 
                   SubstructureNotifyMask, (XEvent *)&maximise_ev);
      }

      if (dev_event_code == KEY_F5) want_to_run = false;

      if (dev_event_code == KEY_R && first_down)
      {
        if (!recording_state->are_recording)
        {
          memcpy(recording_state->mem, hhf_memory->permanent, recording_state->mem_size);

          recording_state->input_bytes_written = 0;

          recording_state->are_recording = true;
          recording_state->are_playing = false;
        }
        else
        {
          memcpy(hhf_memory->permanent, recording_state->mem, recording_state->mem_size);

          recording_state->input_bytes_read = 0;
          
          recording_state->are_recording = false;
          recording_state->are_playing = true;
        }
      }
      if (dev_event_code == KEY_T && first_down)
      {
        if (recording_state->are_recording || recording_state->are_playing)
        {
          hhf_memory->permanent = (u8 *)recording_state->mem;
          recording_state->input_bytes_written = 0;
          recording_state->input_bytes_read = 0;

          recording_state->are_recording = false;
          recording_state->are_playing = false;
        }
      }
#endif
    }
  }
}

void
record_input(RecordingState *recording_state, HHFInput *input)
{
  int bytes_to_write = sizeof(*input);
  ASSERT(recording_state->input_bytes_written + bytes_to_write < 
          recording_state->max_input_size);

  void *input_write_location = (u8 *)recording_state->input + 
                                 recording_state->input_bytes_written;
  memcpy(input_write_location, (void *)input, bytes_to_write);  

  recording_state->input_bytes_written += bytes_to_write;
}

void
playback_input(RecordingState *recording_state, HHFInput **input)
{
  if (recording_state->input_bytes_read == recording_state->input_bytes_written)
  {
    recording_state->input_bytes_read = 0;
  }

  *input = (HHFInput *)((u8 *)recording_state->input + recording_state->input_bytes_read);
  
  recording_state->input_bytes_read += sizeof(*input);
}

void
hhf_platform_free_read_file_result(HHFThreadContext *thread_context, HHFPlatformReadFileResult *file_result)
{
  free(file_result->contents);
}

// TODO(Ryan): Remove blocking and add write protection (writing to intermediate file)
int
hhf_platform_write_entire_file(HHFThreadContext *thread_context, char *file_name, void *memory, u64 size)
{
  int result = 0;

  size_t bytes_to_write = 0;
  u8 *byte_location = NULL;
  int file_fd = 0;

  int open_res = open(file_name, O_CREAT | O_WRONLY | O_TRUNC, 0777); 
  if (open_res < 0) 
  {
    EBP(NULL);
    result = errno;
    goto end;
  }
  file_fd = open_res;

  bytes_to_write = size;
  byte_location = (u8 *)memory;
  while (bytes_to_write > 0) 
  {
    int write_res = write(file_fd, byte_location, bytes_to_write); 
    if (write_res < 0) 
    {
      EBP(NULL);
      result = errno;
      goto end_open;
    }
    else
    {
      int bytes_written = write_res;
      bytes_to_write -= bytes_written;
      byte_location += bytes_written;
    }
  }
end_open:
  close(file_fd); 
end:
  return result;
}

// TODO(Ryan): Remove dynamic memory allocation here and obtain from memory pool
// Avoid round tripping by writing to a queue
// Introduce streaming, i.e background loading
HHFPlatformReadFileResult
hhf_platform_read_entire_file(HHFThreadContext *thread_context, char *file_name)
{
  HHFPlatformReadFileResult result = {0};

  // IMPORTANT(Ryan): Require forward declarations to avoid g++ error of jumping over init
  size_t bytes_to_read = 0;
  u8 *byte_location = NULL;
  int file_fd = 0, fstat_res = 0;
  struct stat file_status = {0};

  int open_res = open(file_name, O_RDONLY); 
  if (open_res < 0) 
  {
    EBP(NULL);
    result.errno_code = errno; 
    goto end;
  }
  file_fd = open_res;

  fstat_res = fstat(file_fd, &file_status);
  if (fstat_res < 0) 
  {
    EBP(NULL);
    result.errno_code = errno; 
    goto end_open;
  }

  result.contents = malloc(file_status.st_size);
  if (result.contents == NULL)
  {
    EBP(NULL);
    result.errno_code = errno;
    goto end_open;
  }
  result.size = file_status.st_size;

  bytes_to_read = file_status.st_size;
  byte_location = (u8 *)result.contents;
  while (bytes_to_read > 0) 
  {
    int read_res = read(file_fd, byte_location, bytes_to_read); 
    if (read_res < 0) 
    {
      EBP(NULL);
      result.errno_code = errno;
      free(result.contents);
      goto end_open;
    }
    else
    {
      int bytes_read = read_res;
      bytes_to_read -= bytes_read;
      byte_location += bytes_read;
    }
  }
end_open:
  close(file_fd); 
end:
  return result;
}

void
copy_file(char *src_file, char *dst_file)
{
  int src_fd = open(src_file, O_RDONLY);
  if (src_fd == -1) EBP(NULL);

  int dst_fd = open(dst_file, O_CREAT | O_WRONLY | O_TRUNC, 0777);
  if (dst_fd == -1) EBP(NULL);

  struct stat src_file_stat = {};
  if (fstat(src_fd, &src_file_stat) == -1) EBP(NULL);
  int src_size = src_file_stat.st_size;

  if (sendfile(dst_fd, src_fd, NULL, src_size) != src_size) EBP(NULL);

  close(src_fd);
  close(dst_fd);
}

//INTERNAL void
//begin_recording_input(void)
//{
//  int input_recording_handle = open();
//  // Modern systems have first-party DMA (bus mastering, i.e. devices directly with RAM)
//  // as oppose to third-party DMA (DMA controller on southbridge of motherboard)
//  // DMA controller not useful, largely pointless as source of sta
//  // create memory mapped file with HHFMemory initially written to it.
//  // use a separate file for input
//  write(memory);
//}

// NOTE(Ryan): Not actually atomic, just ensuring that read and write in one go.
// e.g. long long might require several instructions with lower and upper bits
GLOBAL volatile sig_atomic_t want_to_reload_update_and_render = 1;

void
signal_reload_update_and_render(int sig_num)
{
  want_to_reload_update_and_render = 1;
}

typedef void (*hhf_update_and_render_t)(HHFThreadContext *, HHFBackBuffer *, HHFSoundBuffer *, HHFInput *, HHFMemory *, HHFPlatform *); 

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
  //int xlib_window_x1 = 1280;
  //int xlib_window_y1 = 720;
  int xlib_window_x1 = 960;
  int xlib_window_y1 = 540;

  int xlib_window_border_width = 0;
  unsigned long attribute_mask = CWEventMask | CWBackPixel | CWBitGravity;
  Window xlib_window = XCreateWindow(xlib_display, xlib_root_window,
      xlib_window_x0, xlib_window_y0, xlib_window_x1, xlib_window_y1,
      xlib_window_border_width, xlib_visual_info.depth, InputOutput,
      xlib_visual_info.visual, attribute_mask, &xlib_window_attr);

  XStoreName(xlib_display, xlib_window, "HHF");
  XClassHint xlib_class_hint = {};
  xlib_class_hint.res_name = "HHF";
  xlib_class_hint.res_class = "Game";
  XSetClassHint(xlib_display, xlib_window, &xlib_class_hint);

  int xpresent_op = 0, event = 0, error = 0;
  XPresentQueryExtension(xlib_display, &xpresent_op, &event, &error);
  XPresentSelectInput(xlib_display, xlib_window, PresentCompleteNotifyMask);

  // NOTE(Ryan): These are themed cursors with same name as default xlib cursors from:
  // https://tronche.com/gui/x/xlib/appendix/b/, e.g. XC_sb_v_double_arrow
  Cursor cur = XcursorLibraryLoadCursor(xlib_display, "sb_v_double_arrow");
  XDefineCursor(xlib_display, xlib_window, cur);

  // only hide on fullscreen
  // XFixesHideCursor(xlib_display, xlib_root_window);
  // XFixesShowCursor(xlib_display, xlib_root_window);

  XMapWindow(xlib_display, xlib_window); 
  XFlush(xlib_display);

  // TODO(Ryan): Investigate what a compositor is and how it fits into X11 architecture.
  // Also see if we would benefit from this property.
  //unsigned long value = 1;
  //XChangeProperty(
  //  x11.display,
  //  x11.window,
  //  x11atoms._NET_WM_BYPASS_COMPOSITOR,
  //  XA_CARDINAL,
  //  32,
  //  PropModeReplace,
  //  (unsigned char *)&value,
  //  1
  //);

  Atom xlib_netwm_state_atom = XInternAtom(xlib_display, "_NET_WM_STATE", False);
  Atom xlib_netwm_state_maxh_atom = \
    XInternAtom(xlib_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
  Atom xlib_netwm_state_maxv_atom = \
    XInternAtom(xlib_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
  Atom xlib_netwm_state_fullscreen_atom = \
    XInternAtom(xlib_display, "_NET_WM_STATE_FULLSCREEN", False);
  if (xlib_netwm_state_atom == None || xlib_netwm_state_maxv_atom == None || 
      xlib_netwm_state_maxh_atom == None || xlib_netwm_state_fullscreen_atom == None) BP(NULL);

  int xlib_window_width = xlib_window_x1 - xlib_window_x0;
  int xlib_window_height = xlib_window_y1 - xlib_window_y0;

  XlibInfo xlib_info = {};
  xlib_info.window_width = xlib_window_width;
  xlib_info.window_height = xlib_window_height;
  xlib_info.display = xlib_display;
  xlib_info.root = xlib_root_window;
  xlib_info.window = xlib_window;
  xlib_info.state = xlib_netwm_state_atom;
  xlib_info.maxh = xlib_netwm_state_maxh_atom;
  xlib_info.maxv = xlib_netwm_state_maxv_atom;
  xlib_info.fullscreen = xlib_netwm_state_fullscreen_atom;

  GC xlib_gc = XDefaultGC(xlib_display, xlib_screen);

  Atom xlib_wm_delete_atom = XInternAtom(xlib_display, "WM_DELETE_WINDOW", False);
  if (xlib_wm_delete_atom == None) BP(NULL);
  if (XSetWMProtocols(xlib_display, xlib_window, &xlib_wm_delete_atom, 1) == False) BP(NULL);

  // TODO(Ryan): For shipping, want 1920 x 1080 x 60Hz
  // For software, 1/8 so 960 x 540 x 30Hz
  // We want power of 2 textures for GPU
  int xlib_back_buffer_width = 960;
  int xlib_back_buffer_height = 540;
  XlibBackBuffer xlib_back_buffer = \
    xlib_create_back_buffer(xlib_display, xlib_visual_info, xlib_window,
                            xlib_window_width, xlib_window_height,
                            xlib_back_buffer_width, xlib_back_buffer_height);
  HHFBackBuffer hhf_back_buffer = {};
  hhf_back_buffer.width = xlib_back_buffer.width;
  hhf_back_buffer.height = xlib_back_buffer.height;
  hhf_back_buffer.memory = xlib_back_buffer.memory;

  XrandrActiveCRTC xrandr_active_crtc = xrandr_get_active_crtc(xlib_display, 
                                                               xlib_root_window);
  // TODO(Ryan): Have a max fps to prevent floating point going out.
  r32 frame_dt = 1.0f / xrandr_active_crtc.refresh_rate;

  HHFInput hhf_cur_input = {}, hhf_prev_input = {};
  bool hhf_input_controller_buttons_bounds_check = \
    &hhf_cur_input.controllers[0].__TERMINATOR__ - &hhf_cur_input.controllers[0].buttons[0] == 
      ARRAY_LEN(hhf_cur_input.controllers[0].buttons);
  ASSERT(hhf_input_controller_buttons_bounds_check);

  Window root_win = 0, child_win = 0;
  int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
  unsigned int mask = 0;
  XQueryPointer(xlib_display, xlib_window, &root_win, &child_win,
                &root_x, &root_y, &win_x, &win_y, &mask);
  // NOTE(Ryan): Mouse hardware only give relative events, so require Xlib to give us absolute
  hhf_cur_input.mouse_x = win_x;
  hhf_cur_input.mouse_y = win_y;
  hhf_cur_input.frame_dt = frame_dt;

  struct udev *udev_obj = udev_new();
  if (udev_obj == NULL) BP(NULL);

  //UdevHotplugDevice udev_hotplug_devices[MAX_UDEV_DEVICES] = {};
  UdevPollDevice udev_poll_devices[MAX_PROCESS_FDS] = {};
  int epoll_udev_fd = epoll_create1(0);
  udev_populate_devices(udev_obj, epoll_udev_fd, udev_poll_devices, &hhf_cur_input);

  //struct udev_monitor *udev_mon = udev_monitor_new_from_netlink(udev_obj, "udev");
  //udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "input", NULL);
  //udev_monitor_enable_receiving(udev_mon);


  int pulse_samples_per_second = 44100;
  int pulse_num_channels = 2;
  int pulse_error_code = 0;
  pa_sample_spec pulse_spec = {};
  pulse_spec.format = PA_SAMPLE_S16LE;
  pulse_spec.rate = pulse_samples_per_second;
  pulse_spec.channels = pulse_num_channels;

  // IMPORTANT(Ryan): With integrated audio card, over 100ms of latency is expected 
  pa_simple *pulse_player = pa_simple_new(NULL, "HHF", PA_STREAM_PLAYBACK, NULL, 
                                          "HHF Sound", &pulse_spec, NULL, NULL,
                                          &pulse_error_code);
  if (pulse_player == NULL) BP(pa_strerror(pulse_error_code));

  // TODO(Ryan): Handle audio skips when exceeding frame rate (utilise past frame time)
  int pulse_buffer_num_base_samples = pulse_samples_per_second * frame_dt; 
  int pulse_buffer_num_samples =  pulse_buffer_num_base_samples * pulse_num_channels;
  s16 *pulse_buffer = (s16 *)calloc(pulse_buffer_num_samples, sizeof(s16));
  if (pulse_buffer == NULL) EBP(NULL);

  HHFSoundBuffer hhf_sound_buffer = {};
  hhf_sound_buffer.samples_per_second = pulse_samples_per_second;
  hhf_sound_buffer.samples = pulse_buffer;
  hhf_sound_buffer.num_samples = pulse_buffer_num_base_samples; 

  HHFMemory hhf_memory = {};
  // TODO(Ryan): Allocate based on information from sysinfo()
  u64 hhf_permanent_size = MEGABYTES(64);
  u64 hhf_transient_size = GIGABYTES(2);
  u64 hhf_memory_raw_size = hhf_permanent_size + hhf_transient_size;
#if defined(HHF_INTERNAL)
  void *hhf_memory_raw_base_addr = (void *)TERABYTES(2);
#else
  void *hhf_memory_raw_base_addr = NULL;
#endif
  // NOTE(Ryan): Virtual memory is prevalent. As the lookup is not free, most CPUs have a
  // MMU (MMU contains translation lookaside buffer which is a cache of mappings)
  // So, by enabling large page size, we can alleviate the TLB
  // However, large page size must be enabled via a kernel boot param (so leave for now)
  // This can be acheived with boot-repair under grub options
  void *hhf_memory_raw = mmap(hhf_memory_raw_base_addr, hhf_memory_raw_size, 
                              PROT_READ | PROT_WRITE, 
                              MAP_ANONYMOUS | MAP_PRIVATE, 
                              -1, 0);
  if (hhf_memory_raw == MAP_FAILED) EBP(NULL);

  memset(hhf_memory_raw, 0x00, hhf_memory_raw_size);

  hhf_memory.permanent = (u8 *)hhf_memory_raw;
  hhf_memory.permanent_size = hhf_permanent_size;
  hhf_memory.transient = (u8 *)hhf_memory_raw + hhf_permanent_size;
  hhf_memory.transient_size = hhf_transient_size;

  HHFThreadContext hhf_thread_context = {};

  HHFPlatform hhf_platform = {};
  hhf_platform.read_entire_file = hhf_platform_read_entire_file;
  hhf_platform.free_read_file_result = hhf_platform_free_read_file_result;
  hhf_platform.write_entire_file = hhf_platform_write_entire_file;

  // TODO(Ryan): Replace breakpoints with proper NULL and error handling

  // TODO(Ryan): write() prevents sparseness

  // TODO(Ryan): Use PATH_MAX from <linux/limits.h>?
  char hhf_location[128] = {};
  readlink("/proc/self/exe", hhf_location, sizeof(hhf_location));
  char *last_slash = NULL;
  for (char *cursor = hhf_location; *cursor != '\0'; ++cursor)
  {
    if (*cursor == '/') last_slash = cursor;
  }
  char hhf_lib_loc[128] = {};
  char hhf_temp_lib_loc[128] = {};
  snprintf(hhf_lib_loc, sizeof(hhf_lib_loc), "%.*s/hhf.so", 
           (int)(last_slash - hhf_location), hhf_location);
  snprintf(hhf_temp_lib_loc, sizeof(hhf_lib_loc), "%.*s/hhf.temp-so", 
           (int)(last_slash - hhf_location), hhf_location);

  // IMPORTANT(Ryan): Signals utilised as it seems that the modification time of a file is
  // changed before writing has completed. 
  struct sigaction signal_reload_act = {};
  signal_reload_act.sa_handler = signal_reload_update_and_render;
  if (sigaction(SIGUSR1, &signal_reload_act, NULL) == -1) EBP(NULL);
  void *update_and_render_lib = NULL;
  hhf_update_and_render_t update_and_render = NULL;


  xrender_xpresent_back_buffer(xlib_display, xlib_window, xlib_gc,
                               xrandr_active_crtc.crtc, &xlib_back_buffer, 
                               xlib_window_width, xlib_window_height);


  u64 prev_cycle_count = __rdtsc();
  struct timespec prev_timespec = {};
  clock_gettime(CLOCK_MONOTONIC_RAW, &prev_timespec);

  RecordingState recording_state = {};
  recording_state.mem_size = hhf_memory.permanent_size + hhf_memory.transient_size;
  recording_state.mem = malloc(recording_state.mem_size);
  recording_state.max_input_size = sizeof(HHFInput) * 60 * 10;
  recording_state.input = malloc(recording_state.max_input_size); 
  HHFInput *new_input = NULL;

  bool input_passed_to_hhf = false;
  while (want_to_run)
  {
    XEvent xlib_event = {};
    while (XPending(xlib_display) > 0)
    {
      XNextEvent(xlib_display, &xlib_event);

      if (xlib_event.type == ConfigureNotify)
      {
          xlib_info.window_width = xlib_event.xconfigure.width;
          xlib_info.window_height = xlib_event.xconfigure.height;
          //printf("x: %d, y: %d\n", xlib_event.xconfigure.x, xlib_event.xconfigure.y);
      }

      if (xlib_event.xclient.data.l[0] == (long)(xlib_wm_delete_atom))
      {
        XDestroyWindow(xlib_display, xlib_window);
        want_to_run = false;
        break;
      }

      // udev_check_hotplug_devices();

      Window xlib_focused_window = 0;
      int xlib_focused_window_state = 0;
      XGetInputFocus(xlib_display, &xlib_focused_window, &xlib_focused_window_state);
      if (xlib_focused_window == xlib_window)
      {
        udev_check_poll_devices(epoll_udev_fd, udev_poll_devices, &hhf_prev_input, 
                                &hhf_cur_input, &xlib_info, &hhf_memory, &recording_state);
      }

      if (xlib_event.type == GenericEvent)
      {
        XGenericEventCookie *cookie = (XGenericEventCookie *)&xlib_event.xcookie;
        if (cookie->extension == xpresent_op)
        {
          XGetEventData(xlib_display, cookie);
          if (cookie->evtype == PresentCompleteNotify)
          {
            if (want_to_reload_update_and_render)
            {
              if (update_and_render_lib != NULL) dlclose(update_and_render_lib);
              copy_file(hhf_lib_loc, hhf_temp_lib_loc);
              // TODO(Ryan): Understand how executables and shared objects exist in memory
              update_and_render_lib = dlopen(hhf_temp_lib_loc, RTLD_NOW);
              if (update_and_render_lib == NULL) EBP(NULL);
              update_and_render = (hhf_update_and_render_t)dlsym(update_and_render_lib, "hhf_update_and_render");
              if (update_and_render == NULL) EBP(dlerror());
              want_to_reload_update_and_render = 0;
            }
            
            if (recording_state.are_recording)
            {
              record_input(&recording_state, &hhf_cur_input);
            }
            if (recording_state.are_playing)
            {
              if (recording_state.input_bytes_read == recording_state.input_bytes_written)
              {
                recording_state.input_bytes_read = 0;
                memcpy(hhf_memory.permanent, recording_state.mem, recording_state.mem_size);
              }

              new_input = (HHFInput *)((u8 *)recording_state.input + recording_state.input_bytes_read);

              recording_state.input_bytes_read += sizeof(HHFInput);
            }
            if (recording_state.are_playing)
            {
              update_and_render(&hhf_thread_context, &hhf_back_buffer, &hhf_sound_buffer, new_input, 
                  &hhf_memory, &hhf_platform);
            }
            else
            {
              update_and_render(&hhf_thread_context, &hhf_back_buffer, &hhf_sound_buffer, &hhf_cur_input, 
                  &hhf_memory, &hhf_platform);
            }

            input_passed_to_hhf = true;

            // TODO(Ryan): Add however long last frame took to audio minimum size
            if (pa_simple_write(pulse_player, pulse_buffer, sizeof(s16) * 2 * pulse_buffer_num_base_samples, 
                                &pulse_error_code) < 0) BP(pa_strerror(pulse_error_code));

            /*#if defined(HHF_INTERNAL)
            {
              int pad_x = 16, pad_y = 16;
              int x0 = pad_x, x1 = width - pad_x;
              int pos[30] = {}; // store 30 most recent
              if (index > 30) index = 0;
              debug_display_buffer()
              {
                // Assume length of buffer is same as width of back buffer
              }
            }
            #endif*/

            xrender_xpresent_back_buffer(xlib_display, xlib_window, xlib_gc,
                                         xrandr_active_crtc.crtc, &xlib_back_buffer,
                                         xlib_info.window_width, xlib_info.window_height);
            
            u64 end_cycle_count = __rdtsc();
            struct timespec end_timespec = {};
            clock_gettime(CLOCK_MONOTONIC_RAW, &end_timespec);
            r32 ms_per_frame = timespec_diff(&prev_timespec, &end_timespec) / 1000000.0f;

            char ms_per_frame_buf[16] = {};
            snprintf(ms_per_frame_buf, 16, "%.02f", ms_per_frame); 
            XStoreName(xlib_display, xlib_window, ms_per_frame_buf);
            //printf("ms per frame: %.02f\n", ms_per_frame); 
            //printf("mega cycles per frame: %.02f\n", (r64)(end_cycle_count - prev_cycle_count) / 1000000.0f); 

            prev_timespec = end_timespec;
            prev_cycle_count = end_cycle_count;
          }
          XFreeEventData(xlib_display, cookie);
        }
      }

      // NOTE(Ryan): Preserve transition count if not passed to hhf
      // TODO(Ryan): For speed just exchange pointers
      hhf_prev_input = hhf_cur_input;
      if (input_passed_to_hhf)
      {
        hhf_cur_input = {};
        hhf_cur_input.frame_dt = hhf_prev_input.frame_dt;
        hhf_cur_input.mouse_x = hhf_prev_input.mouse_x; 
        hhf_cur_input.mouse_y = hhf_prev_input.mouse_y;
        for (int controller_i = 0; controller_i < HHF_INPUT_MAX_NUM_CONTROLLERS; controller_i++)
        {
          HHFInputController *cur_controller = &hhf_cur_input.controllers[controller_i];
          HHFInputController *prev_controller = &hhf_prev_input.controllers[controller_i];
          cur_controller->is_analog = prev_controller->is_analog;
          cur_controller->is_connected = prev_controller->is_connected;
          for (int controller_button_i = 0; 
               controller_button_i < HHF_INPUT_NUM_CONTROLLER_BUTTONS;
               controller_button_i++)
          {
            cur_controller->buttons[controller_button_i].ended_down = \
              prev_controller->buttons[controller_button_i].ended_down;
          }
        }
        input_passed_to_hhf = false;
      }

    } 

  }

  return 0;
}
