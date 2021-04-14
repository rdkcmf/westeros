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

all: xdgv4 xdgv5 xdgstable vpc

xdgv4: xgd-server-header-v4 xgd-code-v4

xdgv5: xgd-server-header-v5 xgd-code-v5

xdgstable: xgd-server-header-stable xgd-code-stable

xgd-server-header-v4:
	$(SCANNER_TOOL) server-header < version4/xdg-shell.xml > version4/xdg-shell-server-protocol.h

xgd-code-v4:
	$(SCANNER_TOOL) code < version4/xdg-shell.xml > version4/xdg-shell-protocol.c

xgd-server-header-v5:
	$(SCANNER_TOOL) server-header < version5/xdg-shell.xml > version5/xdg-shell-server-protocol.h

xgd-code-v5:
	$(SCANNER_TOOL) code < version5/xdg-shell.xml > version5/xdg-shell-protocol.c

xgd-server-header-stable:
	$(SCANNER_TOOL) server-header < stable/xdg-shell.xml > stable/xdg-shell-server-protocol.h

xgd-code-stable:
	$(SCANNER_TOOL) code < stable/xdg-shell.xml > stable/xdg-shell-protocol.c

vpc: vpc-client-header vpc-server-header vpc-code

vpc-client-header:
	$(SCANNER_TOOL) client-header < vpc.xml > vpc-client-protocol.h

vpc-server-header:
	$(SCANNER_TOOL) server-header < vpc.xml > vpc-server-protocol.h

vpc-code:
	$(SCANNER_TOOL) code < vpc.xml > vpc-protocol.c

clean:
	@rm -f xdg-shell-server-protocol.h xdg-shell-protocol.c vpc-client-protocol.h vpc-server-protocol.h vpc-protocol.c


