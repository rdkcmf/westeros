#ifndef _WESTEROS_RENDER_H
#define _WESTEROS_RENDER_H

#include "wayland-client.h"

/*
 * Westeros Renderer Interface
 *
 * This interface is used by the compositor to preform all necessary
 * rendering activities required for compositing surfaces.
 *
 * Modules that implement this interface must supply an "renderer_init" 
 * entry point that populates the supplied WstRenderer structure.  
 */

#define RENDERER_MODULE_INIT "renderer_init"

typedef enum _WstRenderer_format
{
   WstRenderer_format_unknown,
   WstRenderer_format_ARGB8888,
   WstRenderer_format_BGRA8888,
   WstRenderer_format_XRGB8888,
   WstRenderer_format_BGRX8888,
   WstRenderer_format_RGB565,
   WstRenderer_format_ARGB4444
} WstRenderer_format;

typedef struct _WstRenderer WstRenderer;
typedef struct _WstRenderSurface WstRenderSurface;

typedef int (*WSTMethodRenderInit)( WstRenderer *renderer, int argc, char **argv);
typedef void (*WSTMethodRenderTerm)( WstRenderer *renderer );
typedef void (*WSTMethodUpdateScene)( WstRenderer *renderer );
typedef WstRenderSurface* (*WSTMethodSurfaceCreate)( WstRenderer *renderer );
typedef void (*WSTMethodSurfaceDestroy)( WstRenderer *renderer, WstRenderSurface *surf );
typedef void (*WSTMethodSurfaceCommit)( WstRenderer *renderer, WstRenderSurface *surface, void *buffer );
typedef void (*WSTMethodSurfaceCommitMemory)( WstRenderer *renderer, WstRenderSurface *surface, 
                                              void *data, int width, int height, int format, int stride );
typedef void (*WSTMethodSurfaceSetVisible)( WstRenderer *renderer, WstRenderSurface *surface, bool visible );
typedef bool (*WSTMethodSurfaceGetVisible)( WstRenderer *renderer, WstRenderSurface *surface, bool *visible );
typedef void (*WSTMethodSurfaceSetGeometry)( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height );
typedef void (*WSTMethodSurfaceGetGeometry)( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height );
typedef void (*WSTMethodSurfaceSetOpacity)( WstRenderer *renderer, WstRenderSurface *surface, float opacity );
typedef float (*WSTMethodSurfaceGetOpacity)( WstRenderer *renderer, WstRenderSurface *surface, float *opaticty );
typedef void (*WSTMethodSurfaceSetZOrder)( WstRenderer *renderer, WstRenderSurface *surface, float z );
typedef float (*WSTMethodSurfaceGetZOrder)( WstRenderer *renderer, WstRenderSurface *surface, float *z );

typedef void (*WSTCallbackKeyboardHandleKeyMap)( void *userData, uint32_t format, int fd, uint32_t size );
typedef void (*WSTCallbackKeyboardHandleEnter)( void *userData, struct wl_array *keys );
typedef void (*WSTCallbackKeyboardHandleLeave)( void *userData );
typedef void (*WSTCallbackKeyboardHandleKey)( void *userData, uint32_t time, uint32_t key, uint32_t state );
typedef void (*WSTCallbackKeyboardHandleModifiers)( void *userData, uint32_t mods_depressed, uint32_t mods_latched,
                                                    uint32_t mods_locked, uint32_t group );
typedef void (*WSTCallbackKeyboardHandleRepeatInfo)( void *userData, int32_t rate, int32_t delay );

typedef void (*WSTCallbackPointerHandleEnter)( void *userData, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WSTCallbackPointerHandleLeave)( void *userData );
typedef void (*WSTCallbackPointerHandleMotion)( void *userData, uint32_t time, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WSTCallbackPointerHandleButton)( void *userData, uint32_t time, uint32_t button, uint32_t state );
typedef void (*WSTCallbackPointerHandleAxis)( void *userData, uint32_t time, uint32_t axis, wl_fixed_t value );

typedef struct _WstRenderNestedListener
{
   WSTCallbackKeyboardHandleKeyMap keyboardHandleKeyMap;
   WSTCallbackKeyboardHandleEnter keyboardHandleEnter;
   WSTCallbackKeyboardHandleLeave keyboardHandleLeave;
   WSTCallbackKeyboardHandleKey keyboardHandleKey;
   WSTCallbackKeyboardHandleModifiers keyboardHandleModifiers;
   WSTCallbackKeyboardHandleRepeatInfo keyboardHandleRepeatInfo;
   WSTCallbackPointerHandleEnter pointerHandleEnter;
   WSTCallbackPointerHandleLeave pointerHandleLeave;
   WSTCallbackPointerHandleMotion pointerHandleMotion;
   WSTCallbackPointerHandleButton pointerHandleButton;
   WSTCallbackPointerHandleAxis pointerHandleAxis;
} WstRenderNestedListener;

typedef struct _WstRenderer
{
   int outputWidth;
   int outputHeight;
   void *renderer;
   
   WSTMethodRenderTerm renderTerm;
   WSTMethodUpdateScene updateScene;
   WSTMethodSurfaceCreate surfaceCreate;
   WSTMethodSurfaceDestroy surfaceDestroy;
   WSTMethodSurfaceCommit surfaceCommit;
   WSTMethodSurfaceCommitMemory surfaceCommitMemory;
   WSTMethodSurfaceSetVisible surfaceSetVisible;
   WSTMethodSurfaceGetVisible surfaceGetVisible;
   WSTMethodSurfaceSetGeometry surfaceSetGeometry;
   WSTMethodSurfaceGetGeometry surfaceGetGeometry;
   WSTMethodSurfaceSetOpacity surfaceSetOpacity;
   WSTMethodSurfaceGetOpacity surfaceGetOpacity;
   WSTMethodSurfaceSetZOrder surfaceSetZOrder;
   WSTMethodSurfaceGetZOrder surfaceGetZOrder;

   // For nested composition
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_compositor *compositor;
   struct wl_surface *surface;
   struct wl_seat *seat;
   struct wl_keyboard *keyboard;
   struct wl_pointer *pointer;
   struct wl_touch *touch;
   int nestedWidth;
   int nestedHeight;
   void *nestedListenerUserData;
   WstRenderNestedListener *nestedListener;
   bool started;
   bool stopRequested;
   pthread_t nestedThreadId;
   
   // For embedded composition
   int resW;
   int resH;
   float *matrix;
   float alpha;
} WstRenderer;

WstRenderer* WstRendererCreate( const char *moduleName, int argc, char **argv, WstRenderNestedListener *listener, void *userData );
void WstRendererDestroy( WstRenderer *renderer );

void WstRendererUpdateScene( WstRenderer *renderer );
WstRenderSurface* WstRendererSurfaceCreate( WstRenderer *renderer );
void WstRendererSurfaceDestroy( WstRenderer *renderer, WstRenderSurface *surface );
void WstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, void *buffer );
void WstRendererSurfaceCommitMemory( WstRenderer *renderer, WstRenderSurface *surface,
                                     void *data, int width, int height, int format, int stride );
void WstRendererSurfaceSetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool visible );
bool WstRendererSurfaceGetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool *visible );
void WstRendererSurfaceSetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height );
void WstRendererSurfaceGetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height );
void WstRendererSurfaceSetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float opacity );
float WstRendererSurfaceGetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float *opacity );
void WstRendererSurfaceSetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float z );
float WstRendererSurfaceGetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float *z );

#endif

