##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
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
##########################################################################

AUTOMAKE_OPTIONS = subdir-objects
lib_LTLIBRARIES = libmediacapture.la

libmediacapture_la_SOURCES = mediacapture.cpp

libmediacapture_la_CXXFLAGS = $(AM_CXXFLAGS) $(GST_CFLAGS) $(CURL_CFLAGS)
libmediacapture_la_LDFLAGS = $(AM_LDFLAGS) $(GST_LIBS) $(GSTBASE_LIBS) $(GSTAPP_LIBS) $(CURL_LIBS) -lpthread

if ENABLE_RTREMOTE
libmediacapture_la_CXXFLAGS += -DMEDIACAPTURE_USE_RTREMOTE
if USE_PXCORE_SHARED_LIBS
libmediacapture_la_LDFLAGS += -lrtRemote -lrtCore
else
libmediacapture_la_LDFLAGS += -lrtRemote_s -lrtCore_s -luuid -ldl
endif
endif

bin_PROGRAMS = 

if ENABLE_RTREMOTE
bin_PROGRAMS += mediacapture-daemon
bin_PROGRAMS += mediacapture-test
endif


mediacapture_daemon_SOURCES = mediacapture-daemon.cpp

mediacapture_daemon_CXXFLAGS = $(AM_CXXFLAGS)
mediacapture_daemon_LDFLAGS = $(AM_LDFLAGS) -lpthread

if USE_PXCORE_SHARED_LIBS
mediacapture_daemon_LDFLAGS += -lrtRemote -lrtCore
else
mediacapture_daemon_LDFLAGS += -lrtRemote_s -lrtCore_s -lpthread -luuid -ldl
endif


mediacapture_test_SOURCES = mediacapture-test.cpp

mediacapture_test_CXXFLAGS = $(AM_CXXFLAGS)
mediacapture_test_LDFLAGS = $(AM_LDFLAGS) -lpthread

if USE_PXCORE_SHARED_LIBS
mediacapture_test_LDFLAGS += -lrtRemote -lrtCore
else
mediacapture_test_LDFLAGS += -lrtRemote_s -lrtCore_s -lpthread -luuid -ldl
endif


## IPK Generation Support
IPK_GEN_PATH = $(abs_top_builddir)/ipk
IPK_GEN_STAGING_DIR=$(abs_top_builddir)/staging_dir
IPK_GEN_INPUT = debian-binary control.tar.gz data.tar.gz
IPK_GEN_OUTPUT = mediacapture.ipk

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

