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

SUBDIRS = 
AM_CPPFLAGS = -pthread -Wall -DEMU -I./
plugin_LTLIBRARIES = libgstwesterossink.la
libgstwesterossink_la_SOURCES =  westeros-sink.c  westeros-sink-soc.c
libgstwesterossink_la_CFLAGS =  $(GST_CFLAGS) $(WAYLAND_CLIENT_CFLAGS) $(WAYLAND_SERVER_CFLAGS)
libgstwesterossink_la_LDFLAGS = $(GST_LIBS) 
libgstwesterossink_la_LIBADD = -lwayland-client -lgstvideo-1.0 -lgstreamer-1.0 -lgstbase-1.0 -lwesteros_simplebuffer_client -lwesteros_simpleshell_client -lwesteros_compositor -lgstvideo-1.0 -ldl -lEGL
libgstwesterossink_la_LDFLAGS += -module -avoid-version
libgstwesterossink_la_LIBTOOLFLAGS = --tag=disable-static
