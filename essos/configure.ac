#
# If not stated otherwise in this file or this component's Licenses.txt file the
# following copyright and licenses apply:
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
#
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

dnl AC_PREREQ([2.65])
AC_INIT(essos, 1.0.0, jeff_wannamaker@cable.comcast.com,essos)
AC_CONFIG_SRCDIR([])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([.])
AC_CONFIG_AUX_DIR([.])
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

# Checks for library functions.
#Add the subdirectories to be considered for building.
SUBDIRS=" "
#to setup m4 macros
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
[AM_DEFAULT_VERBOSITY=1
AC_SUBST(AM_DEFAULT_VERBOSITY)])

HAVE_WAYLAND=" "
HAVE_WAYLAND_EGL=" "
HAVE_XKB=" "
PKG_CHECK_MODULES([EGL], [egl >= 0.0], [HAVE_EGL=true], [HAVE_EGL=false])
PKG_CHECK_MODULES([WAYLAND],[wayland-client >= 1.6.0], [HAVE_WAYLAND=true], [HAVE_WAYLAND=false])
PKG_CHECK_MODULES([WAYLAND_EGL],[wayland-egl >= 1.0.0], [HAVE_WAYLAND_EGL=true], [HAVE_WAYLAND_EGL=false])
PKG_CHECK_MODULES([XKBCOMMON],[xkbcommon >= 0.5.0], [HAVE_XKB=true], [HAVE_XKB=false])

AM_CONDITIONAL([HAVE_EGL], [test x$HAVE_EGL = xtrue])
AM_CONDITIONAL([HAVE_WAYLAND], [test x$HAVE_WAYLAND = xtrue])
AM_CONDITIONAL([HAVE_WAYLAND_EGL], [test x$HAVE_WAYLAND_EGL = xtrue])
AM_CONDITIONAL([HAVE_XKB], [test x$HAVE_XKB = xtrue])

AC_CONFIG_FILES([Makefile])
AC_SUBST(HAVE_EGL)
AC_SUBST(HAVE_WAYLAND)
AC_SUBST(HAVE_WAYLAND_EGL)
AC_SUBST(HAVE_XKB)
AC_OUTPUT

