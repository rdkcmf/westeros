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

SUBDIRS = 
AM_CXXFLAGS= \
	-I$(top_srcdir)/include \
	-I${srcdir}/protocol \
   -I${PKG_CONFIG_SYSROOT_DIR}/$(includedir)/wayland 
AM_CXXFLAGS += $(WAYLAND_CLIENT_CFLAGS) $(WAYLAND_SERVER_CFLAGS)   
AM_LDFLAGS=$(WAYLANDLIB)
AM_CFLAGS = $(AM_CXXFLAGS)

lib_LTLIBRARIES = libwesteros_simpleshell_server.la libwesteros_simpleshell_client.la

libwesteros_simpleshell_server_la_SOURCES = westeros-simpleshell.cpp ${srcdir}/protocol/simpleshell-protocol.c
libwesteros_simpleshell_server_la_include_HEADERS = westeros-simpleshell.h ${srcdir}/protocol/simpleshell-client-protocol.h
libwesteros_simpleshell_server_la_includedir = $(includedir)

libwesteros_simpleshell_server_la_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(WAYLAND_SERVER_LIBS)

libwesteros_simpleshell_client_la_SOURCES = ${srcdir}/protocol/simpleshell-protocol.c
libwesteros_simpleshell_client_la_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(WAYLAND_CLIENT_LIBS)
   
distcleancheck_listfiles = *-libtool

## IPK Generation Support
IPK_GEN_PATH = $(abs_top_builddir)/ipk
IPK_GEN_STAGING_DIR=$(abs_top_builddir)/staging_dir
IPK_GEN_INPUT  = debian-binary control.tar.gz data.tar.gz
IPK_GEN_OUTPUT = westeros-simpleshell.ipk

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
