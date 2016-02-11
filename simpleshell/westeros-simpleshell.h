#ifndef _WESTEROS_SIMPLESHELL_H
#define _WESTEROS_SIMPLESHELL_H

#include <wayland-server.h>

typedef struct _WstRenderer WstRenderer;
typedef struct _WstRenderSurface WstRenderSurface;

struct wl_simple_shell;

struct wayland_simple_shell_callbacks {
   void (*set_name)( void* userData, uint32_t surfaceId, const char *name );
   void (*get_name)( void* userData, uint32_t surfaceId, const char **name );
   void (*set_visible)( void* userData, uint32_t surfaceId, bool visible );
   void (*get_visible)( void* userData, uint32_t surfaceId, bool *visible );
   void (*set_geometry)( void* userData, uint32_t surfaceId, int x, int y, int width, int height );
   void (*get_geometry)( void* userData, uint32_t surfaceId, int *x, int *y, int *width, int *height );
   void (*set_opacity)( void* userData, uint32_t surfaceId, float opacity );
   void (*get_opacity)( void* userData, uint32_t surfaceId, float *opacity );
   void (*set_zorder)( void* userData, uint32_t surfaceId, float zorder );
   void (*get_zorder)( void* userData, uint32_t surfaceId, float *zorder );
};

wl_simple_shell* WstSimpleShellInit( struct wl_display *display,
                                     wayland_simple_shell_callbacks *callbacks,
                                     void *userData ); 
void WstSimpleShellUninit( wl_simple_shell *shell );

void WstSimpleShellNotifySurfaceCreated( wl_simple_shell *shell, struct wl_client *client, uint32_t surfaceId );

void WstSimpleShellNotifySurfaceDestroyed( wl_simple_shell *shell, struct wl_client *client, uint32_t surfaceId );

#endif


