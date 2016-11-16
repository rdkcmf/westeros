Description:
--------------------
DRM adaptation layer for westeros


Usage:
--------------------
Configure Westeros with "--enable-drm=yes" enabled

Run westeros using:

$ LD_PRELOAD=libwesteros_gl.so  westeros --renderer libwesteros_render_gl.so


Run the westeros test (without EGL overload):

$  westeros_test  --display <display_id>

Where <display_id> is the value you get from the output of the westeros command line.

Example output from westeros:

   Westeros Debug: calling wl_display_run for display: westeros-2455-0
                                                             ^
                                                             |
                                                      -----------------
                                                         This is your
                                                         <display_id>
                                                      -----------------
