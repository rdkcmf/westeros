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

all: sb-client-header sb-server-header sb-code

sb-client-header:
	$(SCANNER_TOOL) client-header < simple-buffer.xml > simplebuffer-client-protocol.h

sb-server-header:
	$(SCANNER_TOOL) server-header < simple-buffer.xml > simplebuffer-server-protocol.h

sb-code:
	$(SCANNER_TOOL) code < simple-buffer.xml > simplebuffer-protocol.c

clean:
	@rm -f simplebuffer-client-protocol.h simplebuffer-server-protocol.h simplebuffer-protocol.c


