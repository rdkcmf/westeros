#
# Copyright (C) 2016 RDK Management
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation;
# version 2.1 of the License.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

dnl AC_PREREQ([2.65])
AC_INIT(westeros-sink, version-1.0, jeff_wannamaker@cable.comcast.com,westeros-sink)
AC_CONFIG_SRCDIR([])
AC_CONFIG_HEADERS([cfg/config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([cfg])
AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-bzip2 1.9])
LT_INIT

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_CXX

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_HEADER_STDBOOL

IARM_CFLAGS=" "
GST_VIDEO_DETECTED=" "
GST_AFD_DETECTED=" "
GST_ALLOCATORS_DETECTED=" "

# Checks for library functions.
#Add the subdirectories to be considered for building.
SUBDIRS=" "
#to setup m4 macros
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
[AM_DEFAULT_VERBOSITY=1
AC_SUBST(AM_DEFAULT_VERBOSITY)])

dnl GStreamer version. Use 0.10 by default
AC_ARG_ENABLE(gstreamer1,
              AS_HELP_STRING([--enable-gstreamer1],
                             [Use GStreamer 1.x]), ,
              [enable_gstreamer1=auto])

AS_IF([test "x$enable_gstreamer1" = "xyes"], [
  GST_MAJORMINOR=1.0
  PKG_CHECK_MODULES([GST], [gstreamer-1.0 >= 1.4], have_gst1="yes", have_gst1="no")

  AS_IF([test "x$have_gst1" = "xyes"], [
    PKG_CHECK_MODULES([GSTBASE], [gstreamer-base-1.0 >= 1.4])
    PKG_CHECK_MODULES([GSTVIDEO], [gstreamer-video-1.0 >= 1.4],[GST_VIDEO_DETECTED=true],[GST_VIDEO_DETECTED=false])
    PKG_CHECK_MODULES([GSTALLOCATORS], [gstreamer-allocators-1.0 >= 1.4],[GST_ALLOCATORS_DETECTED=true],[GST_ALLOCATORS_DETECTED=false])
    AC_DEFINE(USE_GST1, 1, [Build with GStreamer 1.x])
    CFLAGS="$CFLAGS $GST_CFLAGS"
    AC_CHECK_TYPES([GstVideoAFDMeta, GstVideoBarMeta],[GST_AFD_DETECTED=true],[GST_AFD_DETECTED=false],[[#include "gstreamer-1.0/gst/video/video-anc.h"]])
  ], [])
], [])

AS_IF([test "x$enable_gstreamer1" = "xyes" -a "x$have_gst1" != "xyes"], [
   AC_MSG_ERROR([Could not find GStreamer 1.x dependencies:

$GST_PKG_ERRORS])], [])

AS_IF([test "x$have_gst1" != "xyes"], [
  GST_MAJORMINOR=0.10
  PKG_CHECK_MODULES([GST], [gstreamer-0.10 >= 0.10.28])
  PKG_CHECK_MODULES([GSTBASE], [gstreamer-base-0.10 >= 0.10.28])
], [])

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"

PKG_CHECK_MODULES([GLIB],[glib-2.0 >= 2.22.5])
PKG_CHECK_MODULES([GTHREAD],[gthread-2.0 >= 2.38.2])
PKG_CHECK_MODULES([WAYLAND_CLIENT],[wayland-client >= 1.6.0])
PKG_CHECK_MODULES([WAYLAND_SERVER],[wayland-server >= 1.6.0])

AM_CONDITIONAL([HAVE_GST_VIDEO], [test x$GST_VIDEO_DETECTED = xtrue])
AM_CONDITIONAL([HAVE_GST_AFD], [test x$GST_AFD_DETECTED = xtrue])
AM_CONDITIONAL([HAVE_GST_ALLOCATORS], [test x$GST_ALLOCATORS_DETECTED = xtrue])

WAYLANDLIB="-lwayland-client"
AC_SUBST(WAYLANDLIB)

AC_CONFIG_FILES([Makefile])
AC_SUBST(WAYLAND_CFLAGS)
AC_SUBST(GUPNP_VERSION)
AC_SUBST(GST_MAJORMINOR)
AC_SUBST(plugindir)
AC_OUTPUT

