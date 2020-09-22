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

Most of Westeros is licensed under the Apache License, Version 2.0.  GStreamer plugins are licensed under
the LGPL-2.1 license.  Licensing for the Apache-licensed portion is described in LICENSE and NOTICE, in
the top level directory.  Licensing for the GStreamer plugins is described in westeros-sink/COPYING.

