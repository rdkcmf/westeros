## Copyright (c) 2013 Comcast. All rights reserved.

AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = 
AM_CXXFLAGS= \
	-I$(top_srcdir)/include \
   -I${PKG_CONFIG_SYSROOT_DIR}/$(includedir)/wayland \
   $(WAYLAND_CLIENT_CFLAGS) $(WAYLAND_SERVER_CFLAGS) \
   $(XKBCOMMON_CFLAGS)   
AM_LDFLAGS=$(WAYLANDLIB)


if HAVE_EGL
AM_CXXFLAGS += -DWESTEROS_PLATFORM_EMBEDDED
endif

if ENABLE_WESTEROS_SBPROTOCOL
AM_CXXFLAGS += -DENABLE_SBPROTOCOL
endif

if ENABLE_XDG_V4
AM_CXXFLAGS += -DUSE_XDG_VERSION4 -I$(srcdir)/protocol/version4
endif

if ENABLE_XDG_V5
AM_CXXFLAGS += -DUSE_XDG_VERSION5 -I$(srcdir)/protocol/version5
endif

AM_CFLAGS = $(AM_CXXFLAGS) -Wno-deprecated-declarations

BUILT_SOURCES= libwesteros_compositor.la \
               libwesteros_render_embedded.la
               
if ENABLE_WESTEROS_RENDERERGL
BUILT_SOURCES += libwesteros_render_gl.la
endif               

if ENABLE_WESTEROS_APP 
if ENABLE_WESTEROS_TEST
bin_PROGRAMS = westeros westeros_test
else
bin_PROGRAMS = westeros
endif
else
if ENABLE_WESTEROS_TEST
bin_PROGRAMS = westeros_test
endif
endif 

westeros_SOURCES = westeros-main.cpp

westeros_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(WAYLAND_SERVER_LIBS) \
   -lwesteros_compositor

westeros_test_SOURCES = westeros-test.cpp
westeros_test_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(WAYLAND_CLIENT_LIBS) \
   -lwesteros_simpleshell_client \
   -lwesteros_egl \
   -lEGL


lib_LTLIBRARIES = libwesteros_compositor.la \
                  libwesteros_render_embedded.la

if ENABLE_WESTEROS_RENDERERGL
lib_LTLIBRARIES += libwesteros_render_gl.la
endif

libwesteros_compositor_la_SOURCES = \
   westeros-compositor.cpp \
   westeros-nested.cpp \
   westeros-render.cpp
libwesteros_compositor_la_include_HEADERS = \
   westeros-compositor.h \
   westeros-render.h
libwesteros_compositor_la_includedir = $(includedir)
libwesteros_compositor_la_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(WAYLAND_SERVER_LIBS) \
   $(XKBCOMMON_LIBS) \
   -lxkbcommon \
   -lwesteros_simpleshell_server \
   -lpthread \
   -ldl

if ENABLE_XDG_V4
libwesteros_compositor_la_SOURCES += protocol/version4/xdg-shell-protocol.c
endif

if ENABLE_XDG_V5
libwesteros_compositor_la_SOURCES += protocol/version5/xdg-shell-protocol.c
endif

if ENABLE_WESTEROS_SBPROTOCOL
libwesteros_compositor_la_LDFLAGS += -lwesteros_simplebuffer_server
endif

libwesteros_render_gl_la_SOURCES = westeros-render-gl.cpp
libwesteros_render_gl_la_includedir = $(includedir)
libwesteros_render_gl_la_LDFLAGS= -lwesteros_egl -lEGL -lwesteros_gl -lwesteros_nxpl

libwesteros_render_embedded_la_SOURCES = westeros-render-embedded.cpp
libwesteros_render_embedded_la_includedir = $(includedir)
if HAVE_EGL
libwesteros_render_embedded_la_LDFLAGS= -lEGL -lwesteros_gl -lwesteros_nxpl
else
libwesteros_render_embedded_la_LDFLAGS= -lglut -lGL -lGLEW
endif

distcleancheck_listfiles = *-libtool

## IPK Generation Support
IPK_GEN_PATH = $(abs_top_builddir)/ipk
IPK_GEN_STAGING_DIR=$(abs_top_builddir)/staging_dir
IPK_GEN_INPUT  = debian-binary control.tar.gz data.tar.gz
IPK_GEN_OUTPUT = westeros.ipk

package_ipk:
	@echo "Generating IPK Package"
	$(MAKE) prefix=${abs_top_builddir}/staging_dir install
	$(mkdir_p) $(IPK_GEN_STAGING_DIR)/usr/bin/
	cp -af $(IPK_GEN_STAGING_DIR)/bin/* $(IPK_GEN_STAGING_DIR)/usr/bin/
	tar -czvf $(IPK_GEN_PATH)/data.tar.gz -C $(IPK_GEN_STAGING_DIR) usr
	tar -czvf $(IPK_GEN_PATH)/control.tar.gz -C $(IPK_GEN_PATH) control postinst prerm
	cd $(IPK_GEN_PATH) && ar cr $(IPK_GEN_OUTPUT) $(IPK_GEN_INPUT) && cd -
	rm -rf ${abs_top_builddir}/staging_dir
	rm -rf $(abs_top_builddir)/ipk/control.tar.gz
	rm -rf $(abs_top_builddir)/ipk/data.tar.gz
