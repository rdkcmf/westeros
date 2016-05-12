#
# If not stated otherwise in this file or this component's Licenses.txt file the
# following copyright and licenses apply:
#
# Copyright 2016 RDK Management
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

include ${PKG_CONFIG_SYSROOT_DIR}/usr/include/platform_app.inc

AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = 

REFSW_VERSION ?= refsw14.4

GST_BASE_CFLAGS ?=
GST_BASE_LIBS ?=
LIB_SUB_DIR= gstreamer-0.10

AM_CFLAGS = -DUSE_WESTEROS $(NEXUS_CFLAGS) $(NXCLIENT_CFLAGS) -I$(ROCKFORD_TOP)/middleware/platform/nexus \
           -I$(ROCKFORD_TOP)/middleware/v3d/interface/khronos/include \
           -I$(ROCKFORD_TOP)/middleware/v3d/interface/khronos/include/EGL 

AM_LDFLAGS = 

lib_LTLIBRARIES = libwesteros_gl.la

libwesteros_gl_la_SOURCES = westeros-gl/westeros-gl.cpp

libwesteros_gl_la_include_HEADERS = westeros-gl/westeros-gl.h
libwesteros_gl_la_includedir = $(includedir)
libwesteros_gl_la_CXXFLAGS= $(AM_CFLAGS)

libwesteros_gl_la_LDFLAGS= \
   $(AM_LDFLAGS) \
   -lwayland-egl \
   -lnexus

distcleancheck_listfiles = *-libtool

## IPK Generation Support
IPK_GEN_PATH = $(abs_top_builddir)/ipk
IPK_GEN_STAGING_DIR=$(abs_top_builddir)/staging_dir
IPK_GEN_INPUT  = debian-binary control.tar.gz data.tar.gz
IPK_GEN_OUTPUT = westeros-soc.ipk

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
