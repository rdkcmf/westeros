# westeros-unittest
Westeros Automated Test Tool

Westeros-unittest is a automated test tool for the Westeros compositor.  It can be built and executed on 
Linux systems.  Output include code coverage information.

---
# Building

To build on a Linxu system, make sure the system has a GNU toolchain, Autotools, and gcov installed.  The tool 
will download and build its dependencies as part of its build process.

For a complete clean build of the tool and all dependencies:

make -f Makefile.test initall
make -f Makefile.test

To leave dependency packages intact but clean build the tool itself:

make -f Makefile.test clean
make -f Makefile.test

---
# Running

Run all tests with:

./run-tests.sh

To run a specific test:

./run-tests testname

---
# Copyright and license

If not stated otherwise in this file or this component's Licenses.txt file the
following copyright and licenses apply:

Copyright 2018 RDK Management

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

