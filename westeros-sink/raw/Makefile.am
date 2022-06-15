#
# Copyright (C) 2020 RDK Management
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

AM_CFLAGS = $(GST_CFLAGS) -DUSE_RAW_SINK -D_FILE_OFFSET_BITS=64

AM_LDFLAGS = $(GST_LIBS) $(GSTBASE_LIBS) $(GSTVIDEO_LIBS) $(GSTALLOCATORS_LIBS) $(WAYLANDLIB) -avoid-version

if HAVE_GST_VIDEO
AM_CFLAGS += -DUSE_GST_VIDEO
endif

if HAVE_GST_AFD
AM_CFLAGS += -DUSE_GST_AFD
endif

if HAVE_GST_ALLOCATORS
AM_CFLAGS += -DUSE_GST_ALLOCATORS
endif

plugin_LTLIBRARIES = libgstwesterosrawsink.la

libgstwesterosrawsink_la_SOURCES = westeros-sink.c westeros-sink-soc.c

libgstwesterosrawsink_la_CFLAGS= \
   $(AM_CFLAGS) \
   $(GST_CFLAGS) \
   -I${STAGING_INCDIR}/libdrm
   
libgstwesterosrawsink_la_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(GST_LIBS)  $(GSTBASE_LIBS) $(WAYLANDLIB) -avoid-version \
   -ldrm -lgbm \
   -lessosrmgr \
   -lwesteros_compositor \
   -lwesteros_simplebuffer_client \
   -lwesteros_simpleshell_client
   
distcleancheck_listfiles = *-libtool

## IPK Generation Support
libgstwesterosrawsink_IPK_OUTDIR = $(abs_top_builddir)/ipk
libgstwesterosrawsink_IPK_STAGING_DIR=$(abs_top_builddir)/staging_dir
libgstwesterosrawsink_IPK_TMP_DIR=$(libgstwesterosrawsink_IPK_STAGING_DIR)/ipk

libgstwesterosrawsink_CONTROL_FILES = control
libgstwesterosrawsink_IPK_FILES  = debian-binary control.tar.gz data.tar.gz
libgstwesterosrawsink_IPK_OUTPUT = libgstwesterosrawsink.ipk

package_ipk:
	@echo "Generating IPK Package"
	$(MAKE) prefix=$(libgstwesterosrawsink_IPK_STAGING_DIR) install
	$(mkdir_p) $(libgstwesterosrawsink_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	cp -aRf $(libgstwesterosrawsink_IPK_STAGING_DIR)/lib/$(LIB_SUB_DIR)/*.so $(libgstwesterosrawsink_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	tar -czvf $(libgstwesterosrawsink_IPK_OUTDIR)/data.tar.gz -C $(libgstwesterosrawsink_IPK_TMP_DIR) usr
	tar -czvf $(libgstwesterosrawsink_IPK_OUTDIR)/control.tar.gz -C $(libgstwesterosrawsink_IPK_OUTDIR) $(libgstwesterosrawsink_CONTROL_FILES)
	cd $(libgstwesterosrawsink_IPK_OUTDIR) && ar cr $(libgstwesterosrawsink_IPK_OUTPUT) $(libgstwesterosrawsink_IPK_FILES) && cd -

