// SPDX-License-Identifier: zlib-acknowledgement 

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <string_view>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdlib>

#define INTERNAL static
#define GLOBAL static
#define LOCAL_PERSIST static

GLOBAL Display *xlib_display;
GLOBAL XVisualInfo xlib_visual_info;
GLOBAL XImage *xlib_image;

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

INTERNAL void
xlib_resize_image(int width, int height)
{
  if (xlib_image != NULL)
  {
    XDestroyImage(xlib_image);
  }

  int bytes_per_pixel = 4;
  // IMPORTANT(Ryan): Would like to use mmap() for page-aligned return and memory 
  // commit/protection options, however XDestroyImage() expects memory to be allocated with stdlib
  char *back_buffer = (char *)calloc(width * height, bytes_per_pixel);
  if (back_buffer == NULL)
  {
    // TODO(Ryan): Error logging
    EBP();
  }

  int xlib_image_offset = 0;
  int xlib_image_scanline_offset = 0;
  int xlib_image_pad_bits = 32;
  xlib_image = XCreateImage(xlib_display, xlib_visual_info.visual, xlib_visual_info.depth,
                            ZPixmap, xlib_image_offset, back_buffer, width, height,
                            xlib_image_pad_bits, xlib_image_scanline_offset);
  if (xlib_image == NULL)
  {
    // TODO(Ryan): Error logging
    BP();
  }
}

INTERNAL void
xlib_display_image(GC gc, Window window, int x0, int y0, int width, int height)
{
  XPutImage(xlib_display, window, gc, xlib_image,
      x0, y0, x0, y0, width, height); 
}

int
main(int argc, char *argv[])
{
  xlib_display = XOpenDisplay(NULL);
  if (xlib_display == NULL)
  {
    // TODO(Ryan): Error logging
    BP();
  }

  XSetErrorHandler(xlib_error_handler);
  XSetIOErrorHandler(xlib_io_error_handler);

  int xlib_screen = XDefaultScreen(xlib_display);
  int xlib_desired_screen_depth = 24;
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

  int xlib_image_width = xlib_window_x1 - xlib_window_x0;
  int xlib_image_height = xlib_window_y1 - xlib_window_y0;
  xlib_resize_image(xlib_image_width, xlib_image_height);

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
          xlib_image_width = xlib_event.xconfigure.width;
          xlib_image_height = xlib_event.xconfigure.height;
          // TODO(Ryan): Restrict to particular resolutions that align with our art
          xlib_resize_image(xlib_image_width, xlib_image_height);
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

    xlib_display_image(xlib_gc, xlib_window, 0, 0, xlib_image_width, xlib_image_height);
  }

  return 0;
}
