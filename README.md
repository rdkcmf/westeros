# westeros
Wayland Compositor

Westeros is a light-weight Wayland compositor library. It uses the Wayland protocols, and is designed 
to be compatible with applications built to use Wayland compositors. It implements a library that 
enables an application to create one or more Wayland displays. It supports the creation of normal, nested, and
embedded wayland compositors.  A normal compositor displays its composited output to the screen, while a nested
compositor sends its composited output to another compositor as a client surface.  An embedded compositor allows 
the application that has created the embedded Wayland compositor to incorporate its composited output into the 
applications UI.  This allows for easy integration of the UI of external third party applications into an 
applications's UI.

---
# Copyright and license

If not stated otherwise in this file or this component's Licenses.txt file the
following copyright and licenses apply:

Copyright 2016 RDK Management

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

