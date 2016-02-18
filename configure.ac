#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

dnl AC_PREREQ([2.65])
AC_INIT(westeros, version-1.0, jeff_wannamaker@cable.comcast.com,westeros)
AC_CONFIG_SRCDIR([])
AC_CONFIG_HEADERS([cfg/config.h])
AC_CONFIG_MACRO_DIR([cfg])
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
ENABLE_WESTEROS_APP=" "
ENABLE_WESTEROS_TEST=" "
EGL_DETECTED=" "
ENABLE_XDG_V4=" "
ENABLE_XDG_V5=" "

# Checks for library functions.
#Add the subdirectories to be considered for building.
SUBDIRS=" "
#to setup m4 macros
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
[AM_DEFAULT_VERBOSITY=1
AC_SUBST(AM_DEFAULT_VERBOSITY)])

PKG_CHECK_MODULES([GLIB],[glib-2.0 >= 2.22.5])
PKG_CHECK_MODULES([GTHREAD],[gthread-2.0 >= 2.38.2])
PKG_CHECK_MODULES([WAYLAND_CLIENT],[wayland-client >= 1.6.0])
PKG_CHECK_MODULES([WAYLAND_SERVER],[wayland-server >= 1.6.0])
PKG_CHECK_MODULES([XKBCOMMON],[xkbcommon >= 0.4])
PKG_CHECK_MODULES([EGL],[egl >= 0.0],[EGL_DETECTED=true],[EGL_DETECTED=false])

AM_CONDITIONAL([HAVE_EGL], [test x$EGL_DETECTED = xtrue])              

AC_ARG_ENABLE([app],
              AS_HELP_STRING([--enable-app],[enable westeros app (default is no)]),
              [
                case "${enableval}" in
                 yes) ENABLE_WESTEROS_APP=true ;;
                  no) AC_MSG_ERROR([westeros app is disabled]) ;;
                   *) AC_MSG_ERROR([bad value ${enableval} for --enable-app]) ;;
                esac
              ],
              [echo "westeros app is disabled"])
AM_CONDITIONAL([ENABLE_WESTEROS_APP], [test x$ENABLE_WESTEROS_APP = xtrue])              

AC_ARG_ENABLE([test],
              AS_HELP_STRING([--enable-test],[enable westeros_test (default is no)]),
              [
                case "${enableval}" in
                 yes) ENABLE_WESTEROS_TEST=true ;;
                  no) AC_MSG_ERROR([westeros_test is disabled]) ;;
                   *) AC_MSG_ERROR([bad value ${enableval} for --enable-test]) ;;
                esac
              ],
              [echo "westeros_test is disabled"])
AM_CONDITIONAL([ENABLE_WESTEROS_TEST], [test x$ENABLE_WESTEROS_TEST = xtrue])              

AC_ARG_ENABLE([rendergl],
              AS_HELP_STRING([--enable-rendergl],[enable westeros opengl renderer (default is no)]),
              [
                case "${enableval}" in
                 yes) ENABLE_WESTEROS_RENDERERGL=true ;;
                  no) AC_MSG_ERROR([opengl renderer is disabled]) ;;
                   *) AC_MSG_ERROR([bad value ${enableval} for --enable-rendergl]) ;;
                esac
              ],
              [echo "westeros opengl renderer is disabled"])
AM_CONDITIONAL([ENABLE_WESTEROS_RENDERERGL], [test x$ENABLE_WESTEROS_RENDERERGL = xtrue])              

AC_ARG_ENABLE([sbprotocol],
              AS_HELP_STRING([--enable-sbprotocol],[enable westeros wl_sb protocol (default is no)]),
              [
                case "${enableval}" in
                 yes) ENABLE_WESTEROS_SBPROTOCOL=true ;;
                  no) AC_MSG_ERROR([westeros wl_sb protocol is disabled]) ;;
                   *) AC_MSG_ERROR([bad value ${enableval} for --enable-sbprotocol]) ;;
                esac
              ],
              [echo "westeros wl_sb protocol is disabled"])
AM_CONDITIONAL([ENABLE_WESTEROS_SBPROTOCOL], [test x$ENABLE_WESTEROS_SBPROTOCOL = xtrue])              

AC_ARG_ENABLE([xdgv4],
              AS_HELP_STRING([--enable-xdgv4],[enable xdg_shell protocol v4 (default is no)]),
              [
                case "${enableval}" in
                 yes) ENABLE_XDG_V4=true ENABLE_XDG_V5=false ;;
                  no) AC_MSG_ERROR([xdg_shell protocol v4 is disabled]) ;;
                   *) AC_MSG_ERROR([bad value ${enableval} for --enable-xdgv4]) ;;
                esac
              ],
              [echo "xdg_shell protocol v4 is disabled"])
AM_CONDITIONAL([ENABLE_XDG_V4], [test x$ENABLE_XDG_V4 = xtrue])              

AC_ARG_ENABLE([xdgv5],
              AS_HELP_STRING([--enable-xdgv5],[enable xdg_shell protocol v5 (default is no)]),
              [
                case "${enableval}" in
                 yes) ENABLE_XDG_V5=true ENABLE_XDG_V4=false ;;
                  no) AC_MSG_ERROR([xdg_shell protocol v5 is disabled]) ;;
                   *) AC_MSG_ERROR([bad value ${enableval} for --enable-xdgv5]) ;;
                esac
              ],
              [echo "xdg_shell protocol v5 is disabled"])
AM_CONDITIONAL([ENABLE_XDG_V5], [test x$ENABLE_XDG_V5 = xtrue])              

WAYLANDLIB="-lwayland-server"
AC_SUBST(WAYLANDLIB)

AC_CONFIG_FILES([Makefile westeros-compositor.pc])
AC_SUBST(WAYLAND_CFLAGS)
AC_SUBST(GUPNP_VERSION)
AC_SUBST(ENABLE_WESTEROS_TEST)
AC_SUBST(ENABLE_WESTEROS_TEST)
AC_OUTPUT

