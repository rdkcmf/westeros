#
# If not stated otherwise in this file or this component's Licenses.txt file the
# following copyright and licenses apply:
#
# Copyright 2020 RDK Management
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

all: linux-dmabuf-unstable-v1-client-header linux-dmabuf-unstable-v1-server-header linux-dmabuf-unstable-v1-code

linux-dmabuf-unstable-v1-client-header:
	$(SCANNER_TOOL) client-header < linux-dmabuf-unstable-v1.xml > linux-dmabuf-unstable-v1-client-protocol.h

linux-dmabuf-unstable-v1-server-header:
	$(SCANNER_TOOL) server-header < linux-dmabuf-unstable-v1.xml > linux-dmabuf-unstable-v1-server-protocol.h

linux-dmabuf-unstable-v1-code:
	$(SCANNER_TOOL) code < linux-dmabuf-unstable-v1.xml > linux-dmabuf-unstable-v1-protocol.c

clean:
	@rm -f linux-dmabuf-unstable-v1-client-protocol.h linux-dmabuf-unstable-v1-server-protocol.h linux-dmabuf-unstable-v1-protocol.c


