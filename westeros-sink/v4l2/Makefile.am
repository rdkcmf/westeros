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
   -lessosrmgr \
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

