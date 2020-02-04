#
# If not stated otherwise in this file or this component's Licenses.txt file the
# following copyright and licenses apply:
#
# Copyright 2018 RDK Management
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

lib_LTLIBRARIES = libwesteros-ut-em.la

libwesteros_ut_em_la_SOURCES = drm-em.cpp protocol/drm-protocol.c
libwesteros_ut_em_la_include_HEADERS = drm-em.h protocol/drm-client-protocol.h
libwesteros_ut_em_la_includedir = $(includedir)

libwesteros_ut_em_la_CXXFLAGS = $(AM_CXXFLAGS) -g -I$(srcdir)../include -I$(srcdir)/../.. -I$(srcdir)/protocol $(WAYLAND_CLIENT_CFLAGS) $(WAYLAND_SERVER_CFLAGS)
libwesteros_ut_em_la_LDFLAGS = $(AM_LDFLAGS) $(WAYLAND_SERVER_LIBS) $(WAYLAND_CLIENT_LIBS) -ldl

distcleancheck_listfiles = *-libtool
