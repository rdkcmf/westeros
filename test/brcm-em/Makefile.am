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

SUBDIRS = 
AM_CFLAGS= $(GST_CFLAGS) $(XKBCOMMON_CFLAGS)
AM_LDFLAGS= $(GST_LIBS) $(GSTBASE_LIBS) $(XKBCOMMON_LIBS)

AM_CXXFLAGS = $(AM_CFLAGS) -Wno-deprecated-declarations
AM_CXXFLAGS += -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi

bin_PROGRAMS = westeros-unittest

westeros_unittest_SOURCES = ../test-main.cpp \
                            ../test-egl.cpp \
                            ../test-render.cpp \
                            ../test-keyboard.cpp \
                            ../test-pointer.cpp \
                            ../test-touch.cpp \
                            ../test-simpleshell.cpp \
                            ../test-essos.cpp \
                            ../test-clientapp.cpp \
                            ../test-repeaterapp.cpp \
                            soc-video-src.cpp \
                            soc-tests.cpp

westeros_unittest_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(GST_LIBS) \
   $(WAYLAND_SERVER_LIBS) \
   $(WAYLAND_CLIENT_LIBS) \
   -lxkbcommon \
   -lwesteros_gl \
   -lwesteros_compositor \
   -lwesteros_simpleshell_client \
   -lessos \
   -lwesteros-ut-em \
   -ldl -lpthread

distcleancheck_listfiles = *-libtool

## IPK Generation Support
IPK_GEN_PATH = $(abs_top_builddir)/ipk
IPK_GEN_STAGING_DIR=$(abs_top_builddir)/staging_dir
IPK_GEN_INPUT  = debian-binary control.tar.gz data.tar.gz
IPK_GEN_OUTPUT = jomx.ipk

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

