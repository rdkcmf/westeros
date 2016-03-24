## Copyright (c) 2013 Comcast. All rights reserved.

AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = 

AM_CFLAGS = 

AM_LDFLAGS = 

lib_LTLIBRARIES = libwesteros_gl.la

libwesteros_gl_la_SOURCES = westeros-gl/westeros-gl.cpp

libwesteros_gl_la_include_HEADERS = westeros-gl/westeros-gl.h
libwesteros_gl_la_includedir = $(includedir)
libwesteros_gl_la_CXXFLAGS= $(AM_CFLAGS) -I=/usr/include/interface/vmcs_host/linux

libwesteros_gl_la_LDFLAGS= \
   $(AM_LDFLAGS)

pkgconfigdir = $(libdir)/pkgconfig
   
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
