#
# Copyright (C) 2017 RDK Management
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
dnl                                               -*- Autoconf -*-
dnl Process this file with autoconf to produce a configure script.

dnl AC_PREREQ([2.69])
AC_INIT(gst-plugin-westerossinkbin, version-1.0, pavan.nandyala@LNTTECHSERVICES.COM padmapriya.kumarasamy@LNTTECHSERVICES.COM,gst-plugin-westerossink)
AC_CONFIG_SRCDIR([])
AM_CONFIG_HEADER(cfg/config.h)
AC_CONFIG_MACRO_DIR([cfg])
AC_CONFIG_AUX_DIR([cfg])
AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-bzip2 1.9])
LT_INIT

dnl subdirectories.
SUBDIRS=""

dnl Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_CPP
AC_PROG_CXXCPP

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])




dnl **********************************
dnl checks for dependencies
dnl **********************************
AC_HEADER_STDC

dnl Checks for header files.
AC_CHECK_HEADERS([limits.h memory.h stdlib.h string.h sys/socket.h unistd.h])

dnl Checks for typedefs, structures, and compiler characteristics.
dnl AC_CHECK_HEADER_STDBOOL
AC_TYPE_UINT8_T

dnl Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset strdup strerror])

dnl GStreamer version. Use 0.10 by default
AC_ARG_ENABLE(gstreamer1,
              AS_HELP_STRING([--enable-gstreamer1],
                             [Use GStreamer 1.x]), ,
              [enable_gstreamer1=auto])

AS_IF([test "x$enable_gstreamer1" = "xyes"], [
  GST_MAJORMINOR=1.0
  PKG_CHECK_MODULES([GST], [gstreamer-1.0 >= 1.4], have_gst1="yes", have_gst1="no")

  AS_IF([test "x$have_gst1" = "xyes"], [
    AC_DEFINE(USE_GST1, 1, [Build with GStreamer 1.x])
  ], [])
], [])

AS_IF([test "x$enable_gstreamer1" = "xyes" -a "x$have_gst1" != "xyes"], [
   AC_MSG_ERROR([Could not find GStreamer 1.x dependencies:

$GST_PKG_ERRORS])], [])

AS_IF([test "x$have_gst1" != "xyes"], [
  GST_MAJORMINOR=0.10
  PKG_CHECK_MODULES([GST], [gstreamer-0.10 >= 0.10.28])
], [])

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 0.10.28])
PKG_CHECK_MODULES([WAYLAND_CLIENT],[wayland-client >= 1.6.0])
PKG_CHECK_MODULES([WAYLAND_SERVER],[wayland-server >= 1.6.0])
AC_SUBST(GST_MAJORMINOR)
AC_SUBST(plugindir)
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
