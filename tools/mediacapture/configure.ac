##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2017 RDK Management
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
##########################################################################

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

dnl AC_PREREQ([2.65])
AC_INIT(mediacapture, 1.0.0, jeff_wannamaker@cable.comcast.com,mediacapture)
AC_CONFIG_SRCDIR([])
AC_CONFIG_HEADERS([cfg/config.h])
AC_CONFIG_MACRO_DIR([cfg])
AC_CONFIG_AUX_DIR([cfg])
AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-bzip2 1.9])
LT_INIT

ENABLE_RTREMOTE=" "

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

# Checks for library functions.
#Add the subdirectories to be considered for building.
SUBDIRS=" "
#to setup m4 macros
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
[AM_DEFAULT_VERBOSITY=1
AC_SUBST(AM_DEFAULT_VERBOSITY)])

PKG_CHECK_MODULES([GLIB],[glib-2.0 >= 2.22.5])
PKG_CHECK_MODULES([GTHREAD],[gthread-2.0 >= 2.38.2])
PKG_CHECK_MODULES([GMODULE],[gmodule-2.0 >= 2.00.0])
PKG_CHECK_MODULES([LIBFFI],[libffi >= 1.00.0])
PKG_CHECK_MODULES([CURL],[libcurl >= 7.19.6])

AC_ARG_ENABLE([rtremote],
              AS_HELP_STRING([--enable-rtremote],[enable use of rtRemote (default is no)]),
              [
                case "${enableval}" in
                 yes) ENABLE_RTREMOTE=true ;;
                  no) ENABLE_RTREMOTE=false ;;
                   *) AC_MSG_ERROR([bad value ${enableval} for --enable-rtremote]) ;;
                esac
              ],
              [echo "rtremote use is disabled"])
AM_CONDITIONAL([ENABLE_RTREMOTE], [test x$ENABLE_RTREMOTE = xtrue])              

AM_CONDITIONAL([USE_PXCORE_SHARED_LIBS], [test x$USE_PXCORE_SHARED_LIBS = xTRUE])

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
    PKG_CHECK_MODULES([GSTAPP], [gstreamer-app-1.0 >= 1.4])
    AC_DEFINE(USE_GST1, 1, [Build with GStreamer 1.x])
  ], [])
], [])

AS_IF([test "x$enable_gstreamer1" = "xyes" -a "x$have_gst1" != "xyes"], [
   AC_MSG_ERROR([Could not find GStreamer 1.x dependencies:

$GST_PKG_ERRORS])], [])

AS_IF([test "x$have_gst1" != "xyes"], [
  GST_MAJORMINOR=0.10
  PKG_CHECK_MODULES([GST], [gstreamer-0.10 >= 0.10.28])
  PKG_CHECK_MODULES([GSTBASE], [gstreamer-base-0.10 >= 0.10.28])
  PKG_CHECK_MODULES([GSTAPP], [gstreamer-app-0.10 >= 0.10.28])
], [])

AC_CONFIG_FILES([Makefile])
AC_SUBST(ENABLE_RTREMOTE)
AC_OUTPUT

