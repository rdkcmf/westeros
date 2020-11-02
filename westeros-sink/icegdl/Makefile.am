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
   -lessosrmgr \
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
	
