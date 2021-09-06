// SPDX-License-Identifier: zlib-acknowledgement 

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <string_view>
#include <cstdio>
#include <cstring>

#define INTERNAL static

#if defined(HHF_DEV)
INTERNAL void __bp(void) { return; }
#define BP() __bp()
#else
#define BP()
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
      reinterpret_cast<const unsigned char *>(xlib_window_name.data()), 
      xlib_window_name.length());

  XMapWindow(xlib_display, xlib_window); 
  XFlush(xlib_display);

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
      reinterpret_cast<unsigned char *>(&xlib_wm_delete_atom), 1);

  bool want_to_run = true;
  while (want_to_run)
  {
    XEvent xlib_event = {};
    while (XCheckWindowEvent(xlib_display, xlib_window, StructureNotifyMask, &xlib_event))
    {
      switch (xlib_event.type)
      {
        case ConfigureNotify:
        {
          printf("window width: %d\n", xlib_event.xconfigure.width);
          printf("window height: %d\n", xlib_event.xconfigure.height);
        } break;
      }
    }
    
    while (XCheckTypedWindowEvent(xlib_display, xlib_window, ClientMessage, &xlib_event))
    {
      if (xlib_event.xclient.data.l[0] == static_cast<long>(xlib_wm_delete_atom))
      {
        XDestroyWindow(xlib_display, xlib_window);
        want_to_run = false;
        break;
      }
    }
  }

  return 0;
}
