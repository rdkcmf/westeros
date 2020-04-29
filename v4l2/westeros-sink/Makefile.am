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

AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = 

GST_BASE_CFLAGS ?=
GST_BASE_LIBS ?=
LIB_SUB_DIR= gstreamer-0.10

AM_CFLAGS = $(GST_CFLAGS)

AM_LDFLAGS = $(GST_LIBS) $(GSTBASE_LIBS) $(GSTALLOCATORS_LIBS) $(WAYLANDLIB) -avoid-version

if HAVE_GST_ALLOCATORS
AM_CFLAGS += -DUSE_GST_ALLOCATORS
endif

plugin_LTLIBRARIES = libgstwesterossink.la

libgstwesterossink_la_SOURCES = westeros-sink.c westeros-sink-soc.c

libgstwesterossink_la_CFLAGS= \
   $(AM_CFLAGS) \
   $(GST_CFLAGS) \
   -I${STAGING_INCDIR}/libdrm
   
libgstwesterossink_la_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(GST_LIBS)  $(GSTBASE_LIBS) $(WAYLANDLIB) -avoid-version \
   -ldrm -lgbm \
   -lwesteros_compositor \
   -lwesteros_simplebuffer_client \
   -lwesteros_simpleshell_client
   
distcleancheck_listfiles = *-libtool

## IPK Generation Support
libgstwesterossink_IPK_OUTDIR = $(abs_top_builddir)/ipk
libgstwesterossink_IPK_STAGING_DIR=$(abs_top_builddir)/staging_dir
libgstwesterossink_IPK_TMP_DIR=$(libgstwesterossink_IPK_STAGING_DIR)/ipk

libgstwesterossink_CONTROL_FILES = control
libgstwesterossink_IPK_FILES  = debian-binary control.tar.gz data.tar.gz
libgstwesterossink_IPK_OUTPUT = libgstwesterossink.ipk

package_ipk:
	@echo "Generating IPK Package"
	$(MAKE) prefix=$(libgstwesterossink_IPK_STAGING_DIR) install
	$(mkdir_p) $(libgstwesterossink_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	cp -aRf $(libgstwesterossink_IPK_STAGING_DIR)/lib/$(LIB_SUB_DIR)/*.so $(libgstwesterossink_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	tar -czvf $(libgstwesterossink_IPK_OUTDIR)/data.tar.gz -C $(libgstwesterossink_IPK_TMP_DIR) usr
	tar -czvf $(libgstwesterossink_IPK_OUTDIR)/control.tar.gz -C $(libgstwesterossink_IPK_OUTDIR) $(libgstwesterossink_CONTROL_FILES)
	cd $(libgstwesterossink_IPK_OUTDIR) && ar cr $(libgstwesterossink_IPK_OUTPUT) $(libgstwesterossink_IPK_FILES) && cd -

