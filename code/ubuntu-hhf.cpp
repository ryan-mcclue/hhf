// SPDX-License-Identifier: zlib-acknowledgement 

#define INTERNAL static
#define GLOBAL static
#define LOCAL_PERSIST static

#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
struct LinuxDirent64
{
  ino64_t d_ino;
  off64_t d_off;
  unsigned short d_reclen;
  unsigned char  d_type;
  char d_name[];
};
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
struct EvdevDevice
{
  EVDEV_DEVICE_TYPE type;
  int event_id;
};

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

#include <string_view>
#include <cstdio>
#include <cstring> 
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cctype>

typedef uint8_t u8;
typedef uint32_t u32;

struct XlibBackBuffer
{
  XImage *image;
  Pixmap pixmap;
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
xlib_display_back_buffer(Display *display, XRenderPictFormat *format, Window window,
                         GC gc, XlibBackBuffer back_buffer, int window_width, int window_height)
{
  XPutImage(display, back_buffer.pixmap, gc, back_buffer.image, 
          0, 0, 0, 0, back_buffer.width, back_buffer.height);
  
  XRenderPictureAttributes pict_attributes = {};
  Picture src_pict = XRenderCreatePicture(display, back_buffer.pixmap, 
                                          format, 0, 
                                          &pict_attributes);
  Picture dst_pict = XRenderCreatePicture(display, window, 
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
}

INTERNAL void
render_weird_gradient(XlibBackBuffer back_buffer, int x_offset, int y_offset)
{
  u32 *pixel = (u32 *)back_buffer.memory;
  for (int back_buffer_y = 0; 
        back_buffer_y < back_buffer.height;
        ++back_buffer_y)
  {
    for (int back_buffer_x = 0; 
        back_buffer_x < back_buffer.width;
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
evdev_create_input_device_monitor(void)
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

INTERNAL void
uevent_monitor_evdev_input_devices(int monitor, evdev_device_b devices[RLIMIT_NOFILE])
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

INTERNAL void 
evdev_populate_input_devices(int epoll_fd, EvdevDevice input_devices[RLIMIT_NOFILE])
{
  // TODO(Ryan): 
  // easier to loop over "/dev/input/event%d" and check if exists

  int input_fd = open("/dev/input", O_RDONLY | O_DIRECTORY);
  if (input_fd == -1)
  {
    EBP();
  }
  char input_dirent_buf[1024] = {};
  int input_dirent_bytes_read = 0, total_input_dirent_bytes_read = 0;
  do
  {
    input_dirent_bytes_read = getdents64(input_fd, input_dirent_buf, 
                                         sizeof(input_dirent_buf));
    if (input_dirent_bytes_read == -1)
    {
      EBP();
    }
    total_input_dirent_bytes_read += input_dirent_bytes_read;
  } while (input_dirent_bytes_read != 0);

  int input_dirent_cursor = 0;
  while (input_dirent_cursor < total_input_dirent_bytes_read)
  {
    LinuxDirent64 *input_dirent = (LinuxDirent64 *)
                                  (input_dirent_buf + input_dirent_cursor);
    if (strncmp(input_dirent->d_name, "event", 5) == 0)
    {
      char dev_path[128] = {};
      strcpy(dev_path, "/dev/input/");
      strcat(dev_path, input_dirent->d_name);

      int dev_fd = open(dev_path, O_RDWR);
      if (dev_fd == -1)
      {
        EBP();
      }

      unsigned long dev_ev_capabilities[EVDEV_BITFIELD_LEN(EV_CNT)] = {};
      if (ioctl(dev_fd, EVIOCGBIT(0, EV_CNT), dev_ev_capabilities) == -1)
      {
        EBP();
      }
      unsigned long dev_key_capabilities[EVDEV_BITFIELD_LEN(KEY_CNT)] = {};
      if (ioctl(dev_fd, EVIOCGBIT(EV_KEY, KEY_CNT), dev_key_capabilities) == -1)
      {
        EBP();
      }
      unsigned long dev_abs_capabilities[EVDEV_BITFIELD_LEN(ABS_CNT)] = {};
      if (ioctl(dev_fd, EVIOCGBIT(EV_ABS, ABS_CNT), dev_abs_capabilities) == -1)
      {
        EBP();
      }
      unsigned long dev_rel_capabilities[EVDEV_BITFIELD_LEN(REL_CNT)] = {};
      if (ioctl(dev_fd, EVIOCGBIT(EV_REL, REL_CNT), dev_rel_capabilities) == -1)
      {
        EBP();
      }
      // ioctl(fd, EVIOCGEFFECTS, &num_simultaneous_effects)
      unsigned long dev_ff_capabilities[EVDEV_BITFIELD_LEN(FF_CNT)] = {};
      if (ioctl(dev_fd, EVIOCGBIT(EV_FF, FF_CNT), dev_ff_capabilities) == -1)
      {
        EBP();
      }
      unsigned long dev_sw_capabilities[EVDEV_BITFIELD_LEN(SW_CNT)] = {};
      if (ioctl(dev_fd, EVIOCGBIT(EV_SW, SW_CNT), dev_sw_capabilities) == -1)
      {
        EBP();
      }

      char dev_name[256] = {};
      if (ioctl(dev_fd, EVIOCGNAME(sizeof(dev_name)), dev_name) == -1)
      {
        EBP();
      }

      EVDEV_DEVICE_TYPE dev_type = EVDEV_TYPE_IGNORE;

      unsigned long esc_keys_letters_mask = 0xfffffffe;
      if ((dev_key_capabilities[0] & esc_keys_letters_mask) != 0 &&
           EVDEV_BITFIELD_TEST(dev_ev_capabilities, EV_REP))
      {
        dev_type = EVDEV_TYPE_KEYBOARD;
        printf("found keyboard: %s\n", dev_name);
      }
      if (EVDEV_BITFIELD_TEST(dev_sw_capabilities, SW_HEADPHONE_INSERT))
      {
        // headphone
        // only through usb?
        // alsa for jack plug
      }
      if (EVDEV_BITFIELD_TEST(dev_key_capabilities, BTN_GAMEPAD))
      {
        dev_type = EVDEV_TYPE_GAMEPAD;
        printf("found gamepad: %s\n", dev_name);
        // NOTE(Ryan): Gain is different across devices, so standardise.
        struct input_event gain = {};
        gain.type = EV_FF;
        gain.code = FF_GAIN;
        int percent75 = 0xC000;
        gain.value = percent75;
        if (write(dev_fd, &gain, sizeof(gain)) != sizeof(gain))
        {
          EBP();
        }
        // upload effect
        struct ff_effect rumble_effect = {};
        rumble_effect.type = FF_RUMBLE;
        rumble_effect.id = -1;
	      rumble_effect.u.rumble.strong_magnitude = 0;
	      rumble_effect.u.rumble.weak_magnitude = 0xC000;
	      rumble_effect.replay.length = 5000;
	      rumble_effect.replay.delay = 0;

        // play effect
        int vibration_num_times = 1;
        play.type = EV_FF;
        play.code = rumble_effect.id;
        play.value = vibration_num_times;
        write(dev_fd, (const void *)&play_vibration, sizeof(play_vibration));
      }
      if (EVDEV_BITFIELD_TEST(dev_ev_capabilities, EV_REL) &&
          EVDEV_BITFIELD_TEST(dev_rel_capabilities, REL_X) && 
          EVDEV_BITFIELD_TEST(dev_rel_capabilities, REL_Y) &&
          EVDEV_BITFIELD_TEST(dev_key_capabilities, BTN_MOUSE))
      {
        dev_type = EVDEV_TYPE_MOUSE;
        printf("found mouse: %s\n", dev_name);
      }

      if (EVDEV_BITFIELD_TEST(dev_ev_capabilities, EV_ABS) &&
          EVDEV_BITFIELD_TEST(dev_abs_capabilities, ABS_X) && 
          EVDEV_BITFIELD_TEST(dev_abs_capabilities, ABS_Y) &&
          EVDEV_BITFIELD_TEST(dev_key_capabilities, BTN_TOOL_FINGER))
      {
        dev_type = EVDEV_TYPE_MOUSE;
        printf("found touchpad: %s\n", dev_name);
      }

      if (dev_type != EVDEV_TYPE_IGNORE)
      {
        struct epoll_event event = {}; 
        event.events = EPOLLIN;
        event.data.fd = dev_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dev_fd, &event);
        // lower 16 event_id, upper 16 device_type
        input_devices[dev_fd] = dev_type;
      }
      else
      {
        close(dev_fd);
      }
    }

    input_dirent_cursor += input_dirent->d_reclen;
  }

  close(input_fd);
}

int
main(int argc, char *argv[])
{
  Display *xlib_display = XOpenDisplay(NULL);
  if (xlib_display == NULL)
  {
    // TODO(Ryan): Error logging
    BP();
  }

  XSetErrorHandler(xlib_error_handler);
  XSetIOErrorHandler(xlib_io_error_handler);

  int xlib_screen = XDefaultScreen(xlib_display);
  int xlib_desired_screen_depth = 24;
  XVisualInfo xlib_visual_info = {};
  Status xlib_visual_info_status = XMatchVisualInfo(xlib_display, xlib_screen, 
                                                    xlib_desired_screen_depth, TrueColor,
                                                    &xlib_visual_info);
  if (xlib_visual_info_status == False)
  {
    // TODO(Ryan): Error logging
    BP();
  }

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

  std::string_view xlib_window_name {"HHF"};
  int xlib_window_name_property_granularity = 8;
  XChangeProperty(xlib_display, xlib_window, XA_WM_NAME, XA_STRING, 
      xlib_window_name_property_granularity, PropModeReplace, 
      (const unsigned char *)xlib_window_name.data(), 
      xlib_window_name.length());

  XMapWindow(xlib_display, xlib_window); 
  XFlush(xlib_display);

  GC xlib_gc = XDefaultGC(xlib_display, xlib_screen);

  Atom xlib_wm_delete_atom = XInternAtom(xlib_display, "WM_DELETE_WINDOW", False);
  if (xlib_wm_delete_atom == None) 
  {
    // TODO(Ryan): Error logging
    BP();
  }
  Atom xlib_wm_atom = XInternAtom(xlib_display, "WM_PROTOCOLS", False);
  if (xlib_wm_atom == None)
  {
    // TODO(Ryan): Error logging
    BP();
  }
  int xlib_window_protocols_property_granularity = 32;
  XChangeProperty(xlib_display, xlib_window, xlib_wm_atom, XA_ATOM, 
      xlib_window_protocols_property_granularity, PropModeReplace, 
      (unsigned char *)&xlib_wm_delete_atom, 1);

  XRenderPictFormat *xrender_pic_format = XRenderFindVisualFormat(xlib_display, 
                                                               xlib_visual_info.visual);

  int xlib_window_width = xlib_window_x1 - xlib_window_x0;
  int xlib_window_height = xlib_window_y1 - xlib_window_y0;
  XlibBackBuffer xlib_back_buffer = xlib_create_back_buffer(xlib_display, xlib_window,
                                                            xlib_visual_info, 1280, 720);

  EvdevDevice evdev_input_devices[RLIMIT_NOFILE] = {};
  int epoll_fd = epoll_create1(0);
  evdev_populate_input_devices(epoll_fd, evdev_input_devices);

  int uevent_monitor = uevent_create_monitor();

  // NOTE(Ryan): Sound devices picked up with mixer events

  bool want_to_run = true;
  int x_offset = 0;
  while (want_to_run)
  {
    XEvent xlib_event = {};
    while (XCheckWindowEvent(xlib_display, xlib_window, StructureNotifyMask, &xlib_event))
    {
      switch (xlib_event.type)
      {
        case ConfigureNotify:
        {
          xlib_window_width = xlib_event.xconfigure.width;
          xlib_window_height = xlib_event.xconfigure.height;
        } break;
      }
    }
    
    while (XCheckTypedWindowEvent(xlib_display, xlib_window, ClientMessage, &xlib_event))
    {
      if (xlib_event.xclient.data.l[0] == (long)(xlib_wm_delete_atom))
      {
        XDestroyWindow(xlib_display, xlib_window);
        want_to_run = false;
        break;
      }
    }

    uevent_monitor_evdev_input_devices(uevent_socket, evdev_input_devices);

#define MAX_EVENTS 5
#define TIMEOUT_MS 1
    struct epoll_event epoll_evdev_input_device_events[MAX_EVENTS] = {0};
    int num_epoll_evdev_input_device_events = 0;
    // TODO(Ryan): Should we poll this more frequently?
    num_epoll_evdev_input_device_events = epoll_wait(epoll_evdev_input_device_fd, 
                                        epoll_evdev_input_device_events, 
                                        MAX_EVENTS, TIMEOUT_MS);
    for (uint epoll_event = 0;
        evdev_epoll_event < num_evdev_epoll_events; 
        ++evdev_epoll_event)
    {
      int device_fd = evdev_epoll_events[evdev_epoll_event].data.fd;
      evdev_device_b device = evdev_input_devices[device_fd];
      EVDEV_DEVICE_TYPE device_type = EVDEV_DEVICE_GET_TYPE(device);

      struct input_event events[32] = {0};
      int len = read(evdev_epoll_events[evdev_epoll_event].data.fd, 
                     events, sizeof(events));
      if (len == -1)
      {
        // TODO(EHANDLING | ELOGGING: Ryan)
        EBP();
      }
      for (uint event_i = 0; event_i < len / sizeof(events[0]); ++event_i)
      {
        int type = events[event_i].type;
        int code = events[event_i].code;
        int value = events[event_i].value;

        bool is_released = (type == EV_KEY ? !value : false);

        if (device_type == EVDEV_DEVICE_TYPE_GAMEPAD)
        {
          bool up = (code == BTN_DPAD_UP);
          bool right = (code == BTN_DPAD_RIGHT);
          bool left = (code == BTN_DPAD_LEFT);
          bool down = (code == BTN_DPAD_DOWN);
          bool select = (code == BTN_SELECT);
          bool start = (code == BTN_START);
          bool home = (code == BTN_MODE);
          bool left_shoulder = (code == BTN_TL);
          bool right_shoulder = (code == BTN_TR);
          bool north = (code == BTN_NORTH);
          bool east = (code == BTN_EAST);
          bool south = (code == BTN_SOUTH);
          bool west = (code == BTN_WEST);

          int stick_x = (code == ABS_X ? value : 0);
          int stick_y = (code == ABS_Y ? value : 0);

          if (south)
          {
            // FF_PERIODIC --> FF_SINE
          }
        }
      }
    }

    render_weird_gradient(xlib_back_buffer, x_offset, 0);
    x_offset++;

    xlib_display_back_buffer(xlib_display, xrender_pic_format, xlib_window,
                             xlib_gc, xlib_back_buffer, xlib_window_width, 
                             xlib_window_height);
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
