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

AM_LDFLAGS = $(GST_LIBS)  $(GSTBASE_LIBS) $(WAYLANDLIB) -avoid-version

plugin_LTLIBRARIES = libwesteros_sink.la

libwesteros_sink_la_SOURCES = westeros-sink.c westeros-sink-soc.c

libwesteros_sink_la_CFLAGS= \
   $(AM_CFLAGS) \
   $(GST_CFLAGS)
   
libwesteros_sink_la_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(GST_LIBS)  $(GSTBASE_LIBS) $(WAYLANDLIB) -avoid-version \
   -lwayland-egl \
   -lwesteros_compositor \   
   -lwesteros_simplebuffer_client \
   -lwesteros_simpleshell_client \
   -lismd_core \
   -ldl
   
distcleancheck_listfiles = *-libtool

## IPK Generation Support
libwesteros_sink_IPK_OUTDIR = $(abs_top_builddir)/ipk
libwesteros_sink_IPK_STAGING_DIR=$(abs_top_builddir)/staging_dir
libwesteros_sink_IPK_TMP_DIR=$(libwesteros_sink_IPK_STAGING_DIR)/ipk

libwesteros_sink_CONTROL_FILES = control
libwesteros_sink_IPK_FILES  = debian-binary control.tar.gz data.tar.gz
libwesteros_sink_IPK_OUTPUT = libwesteros_sink.ipk

package_ipk:
	@echo "Generating IPK Package"
	$(MAKE) prefix=$(libwesteros_sink_IPK_STAGING_DIR) install
	$(mkdir_p) $(libwesteros_sink_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	cp -aRf $(libwesteros_sink_IPK_STAGING_DIR)/lib/$(LIB_SUB_DIR)/*.so $(libwesteros_sink_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	tar -czvf $(libwesteros_sink_IPK_OUTDIR)/data.tar.gz -C $(libwesteros_sink_IPK_TMP_DIR) usr
	tar -czvf $(libwesteros_sink_IPK_OUTDIR)/control.tar.gz -C $(libwesteros_sink_IPK_OUTDIR) $(libwesteros_sink_CONTROL_FILES)
	cd $(libwesteros_sink_IPK_OUTDIR) && ar cr $(libwesteros_sink_IPK_OUTPUT) $(libwesteros_sink_IPK_FILES) && cd -
	
