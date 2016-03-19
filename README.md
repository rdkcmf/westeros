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

