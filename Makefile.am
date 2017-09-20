#
# If not stated otherwise in this file or this component's Licenses.txt file the
# following copyright and licenses apply:
#
# Copyright 2016 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = essos
AM_CXXFLAGS= \
	-I$(top_srcdir)/include \
   -I${PKG_CONFIG_SYSROOT_DIR}/$(includedir)/wayland \
   $(WAYLAND_CLIENT_CFLAGS) $(WAYLAND_SERVER_CFLAGS) \
   $(XKBCOMMON_CFLAGS)   
AM_LDFLAGS=$(WAYLANDLIB)

## --- Configurtion -------
if !HAVE_GLEW
AM_CXXFLAGS += -DWESTEROS_PLATFORM_EMBEDDED
endif

if HAVE_WAYLAND_EGL
AM_CXXFLAGS += -DWESTEROS_HAVE_WAYLAND_EGL
WSTEGL_LIBS= -lwayland-egl -lGLESv2
else
WSTEGL_LIBS= -lwesteros_egl
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

bin_PROGRAMS =

if ENABLE_WESTEROS_APP
bin_PROGRAMS += westeros
endif

if ENABLE_WESTEROS_TEST
bin_PROGRAMS += westeros_test
endif

if ENABLE_WESTEROS_PLAYER
bin_PROGRAMS += westeros_player
endif

## --- Sample compositor app -------
westeros_SOURCES = westeros-main.cpp

westeros_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(WAYLAND_SERVER_LIBS) \
   -lwesteros_compositor
if HAVE_GLEW
westeros_LDFLAGS += -lglut -lGL -lGLEW -lX11 -lEGL
endif
if !HAVE_GLEW
westeros_LDFLAGS += -lEGL -lwesteros_gl
if HAVE_WAYLAND_EGL
westeros_LDFLAGS += -lGLESv2
endif
endif

## --- Sample client -------
westeros_test_SOURCES = westeros-test.cpp
westeros_test_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(WAYLAND_CLIENT_LIBS) \
   $(XKBCOMMON_LIBS) \
   -lxkbcommon \
   -lwesteros_simpleshell_client \
   $(WSTEGL_LIBS) \
   -lEGL
if HAVE_GLEW
westeros_test_LDFLAGS += -lGL -lGLEW
endif   

## --- Sample player -------
westeros_player_SOURCES = westeros-player.cpp
westeros_player_CXXFLAGS = $(AM_CXXFLAGS) $(GST_CFLAGS)
westeros_player_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(GST_LIBS) \
   $(WAYLAND_CLIENT_LIBS) \
   $(WSTEGL_LIBS) \
   -lEGL

## --- Compositor -------
lib_LTLIBRARIES = libwesteros_compositor.la \
                  libwesteros_render_embedded.la

if ENABLE_WESTEROS_RENDERERGL
lib_LTLIBRARIES += libwesteros_render_gl.la
endif

libwesteros_compositor_la_SOURCES = \
   westeros-compositor.cpp \
   westeros-nested.cpp \
   westeros-render.cpp \
   protocol/vpc-protocol.c
libwesteros_compositor_la_include_HEADERS = \
   westeros-compositor.h \
   westeros-render.h \
   protocol/vpc-client-protocol.h
libwesteros_compositor_la_includedir = $(includedir)
libwesteros_compositor_la_CXXFLAGS = $(AM_CXXFLAGS) -I$(srcdir)/protocol
libwesteros_compositor_la_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(WAYLAND_SERVER_LIBS) \
   $(WAYLAND_CLIENT_LIBS) \
   $(WAYLAND_EGL_LIBS) \
   $(XKBCOMMON_LIBS) \
   $(WESTEROS_COMPOSITOR_EXTRA_LIBS) \
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

## --- Renderer: OpenGL -------
libwesteros_render_gl_la_SOURCES = westeros-render-gl.cpp
libwesteros_render_gl_la_includedir = $(includedir)
if HAVE_GLEW
libwesteros_render_gl_la_LDFLAGS= $(WSTEGL_LIBS) -lEGL -lGL -lGLEW $(WAYLAND_SERVER_LIBS)
else
libwesteros_render_gl_la_LDFLAGS= $(WSTEGL_LIBS) -lEGL -lwesteros_gl $(WESTEROS_PLATFORM_LIBS) $(WAYLAND_SERVER_LIBS)
endif
if ENABLE_WESTEROS_SBPROTOCOL
libwesteros_render_gl_la_LDFLAGS += -lwesteros_simplebuffer_server
endif

## --- Renderer: Embedded -------
libwesteros_render_embedded_la_SOURCES = westeros-render-embedded.cpp
libwesteros_render_embedded_la_includedir = $(includedir)
if HAVE_GLEW
libwesteros_render_embedded_la_LDFLAGS= -lEGL -lglut -lGL -lGLEW $(WAYLAND_SERVER_LIBS)
else
libwesteros_render_embedded_la_LDFLAGS= $(WSTEGL_LIBS) -lEGL -lwesteros_gl $(WESTEROS_PLATFORM_LIBS) $(WAYLAND_SERVER_LIBS)
endif
if ENABLE_WESTEROS_SBPROTOCOL
libwesteros_render_embedded_la_LDFLAGS += -lwesteros_simplebuffer_server
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = westeros-compositor.pc

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
