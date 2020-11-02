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

SUBDIRS = 
AM_CPPFLAGS = -pthread -Wall -DEMU -I./
plugin_LTLIBRARIES = libgstwesterossink.la
libgstwesterossink_la_SOURCES =  westeros-sink.c  westeros-sink-soc.c
libgstwesterossink_la_CFLAGS =  $(GST_CFLAGS) $(WAYLAND_CLIENT_CFLAGS) $(WAYLAND_SERVER_CFLAGS)
libgstwesterossink_la_LDFLAGS = $(GST_LIBS) 
libgstwesterossink_la_LIBADD = -lwayland-client -lgstvideo-1.0 -lgstreamer-1.0 -lgstbase-1.0 -lwesteros_simplebuffer_client -lwesteros_simpleshell_client -lessosrmgr -lwesteros_compositor -lgstvideo-1.0 -ldl -lEGL
libgstwesterossink_la_LDFLAGS += -module -avoid-version
libgstwesterossink_la_LIBTOOLFLAGS = --tag=disable-static
