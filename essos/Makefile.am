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

AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = -I .

lib_LTLIBRARIES = libessos.la

bin_PROGRAMS = essos-sample

BUILT_SOURCES= libessos.la

libessos_la_SOURCES = essos.cpp
libessos_la_include_HEADERS = essos.h essos-app.h essos-system.h
libessos_la_includedir = $(includedir)

libessos_la_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir)/..
libessos_la_LDFLAGS = $(AM_LDFLAGS) -lEGL -ldl

if HAVE_WAYLAND
if HAVE_WAYLAND_EGL
if HAVE_XKB
libessos_la_CXXFLAGS += -DHAVE_WAYLAND $(WAYLAND_CFLAGS) $(XKBCOMMON_CFLAGS)
libessos_la_LDFLAGS += $(WAYLAND_EGL_LIBS) $(WAYLAND_LIBS) $(XKBCOMMON_LIBS)
endif
endif
endif

if HAVE_EGL
libessos_la_CXXFLAGS += ${EGL_CFLAGS}
endif

libessos_la_CXXFLAGS += -DHAVE_WESTEROS $(WESTEROS_CFLAGS)
libessos_la_LDFLAGS += -lwesteros_gl -lwesteros_simpleshell_client

essos_sample_SOURCES = essos-sample.cpp
essos_sample_CXXFLAGS = ${AM_CXXFLAGS}
if HAVE_EGL
essos_sample_CXXFLAGS += ${EGL_CFLAGS}
endif
essos_sample_LDFLAGS = $(AM_FLAGS) -lessos -lGLESv2

## IPK Generation Support
IPK_GEN_PATH = $(abs_top_builddir)/ipk
IPK_GEN_STAGING_DIR=$(abs_top_builddir)/staging_dir
IPK_GEN_INPUT = debian-binary control.tar.gz data.tar.gz
IPK_GEN_OUTPUT = rcmodule.ipk

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
	
