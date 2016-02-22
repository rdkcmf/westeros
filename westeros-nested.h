#ifndef _WESTEROS_NESTED_H
#define _WESTEROS_NESTED_H

#include <pthread.h>

#include "wayland-client.h"

typedef struct _WstCompositor WstCompositor;
typedef struct _WstNestedConnection WstNestedConnection;

typedef void (*WSTCallbackConnectionEnded)( void *userData );
typedef void (*WSTCallbackKeyboardHandleKeyMap)( void *userData, uint32_t format, int fd, uint32_t size );
typedef void (*WSTCallbackKeyboardHandleEnter)( void *userData, struct wl_array *keys );
typedef void (*WSTCallbackKeyboardHandleLeave)( void *userData );
typedef void (*WSTCallbackKeyboardHandleKey)( void *userData, uint32_t time, uint32_t key, uint32_t state );
typedef void (*WSTCallbackKeyboardHandleModifiers)( void *userData, uint32_t mods_depressed, uint32_t mods_latched,
                                                    uint32_t mods_locked, uint32_t group );
typedef void (*WSTCallbackKeyboardHandleRepeatInfo)( void *userData, int32_t rate, int32_t delay );

typedef void (*WSTCallbackPointerHandleEnter)( void *userData, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WSTCallbackPointerHandleLeave)( void *userData, struct wl_surface *surface );
typedef void (*WSTCallbackPointerHandleMotion)( void *userData, uint32_t time, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WSTCallbackPointerHandleButton)( void *userData, uint32_t time, uint32_t button, uint32_t state );
typedef void (*WSTCallbackPointerHandleAxis)( void *userData, uint32_t time, uint32_t axis, wl_fixed_t value );

typedef void (*WSTCallbackShmFormat)( void *userData, uint32_t format );

typedef struct _WstNestedConnectionListener
{
   WSTCallbackConnectionEnded connectionEnded;
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
   WSTCallbackShmFormat shmFormat;
} WstNestedConnectionListener;

WstNestedConnection* WstNestedConnectionCreate( WstCompositor *wctx, 
                                                const char *displayName, 
                                                int width, 
                                                int height,
                                                WstNestedConnectionListener *listener,
                                                void *userData );

void WstNestedConnectionDisconnect( WstNestedConnection *nc );

void WstNestedConnectionDestroy( WstNestedConnection *nc );

wl_display* WstNestedConnectionGetDisplay( WstNestedConnection *nc );

wl_surface* WstNestedConnectionGetCompositionSurface( WstNestedConnection *nc );

struct wl_surface* WstNestedConnectionCreateSurface( WstNestedConnection *nc );

void WstNestedConnectionDestroySurface( WstNestedConnection *nc, struct wl_surface *surface );

void WstNestedConnectionSurfaceSetVisible( WstNestedConnection *nc, 
                                           struct wl_surface *surface,
                                           bool visible );

void WstNestedConnectionSurfaceSetGeometry( WstNestedConnection *nc, 
                                            struct wl_surface *surface,
                                            int x,
                                            int y,
                                            int width, 
                                            int height );

void WstNestedConnectionSurfaceSetZOrder( WstNestedConnection *nc, 
                                           struct wl_surface *surface,
                                           float zorder );

void WstNestedConnectionSurfaceSetOpacity( WstNestedConnection *nc, 
                                           struct wl_surface *surface,
                                           float opacity );

void WstNestedConnectionAttachAndCommit( WstNestedConnection *nc,
                                         struct wl_surface *surface,
                                         struct wl_buffer *buffer,
                                         int x,
                                         int y,
                                         int width,
                                         int height );
                                          
void WstNestedConnectionAttachAndCommitDevice( WstNestedConnection *nc,
                                               struct wl_surface *surface,
                                               void *deviceBuffer,
                                               uint32_t format,
                                               int32_t stride,
                                               int x,
                                               int y,
                                               int width,
                                               int height );

void WstNestedConnectionPointerSetCursor( WstNestedConnection *nc, 
                                          struct wl_surface *surface, 
                                          int hotspotX, 
                                          int hotspotY );

struct wl_shm_pool* WstNestedConnnectionShmCreatePool( WstNestedConnection *nc, int fd, int size );

void WstNestedConnectionShmDestroyPool( WstNestedConnection *nc, struct wl_shm_pool *pool );

void WstNestedConnectionShmPoolResize( WstNestedConnection *nc, struct wl_shm_pool *pool, int size );

struct wl_buffer* WstNestedConnectionShmPoolCreateBuffer( WstNestedConnection *nc,
                                                          struct wl_shm_pool *pool,
                                                          int32_t offset,
                                                          int32_t width, 
                                                          int32_t height,
                                                          int32_t stride, 
                                                          uint32_t format);
                                                          
void WstNestedConnectionShmBufferPoolDestroy( WstNestedConnection *nc,
                                              struct wl_shm_pool *pool,
                                              struct wl_buffer *buffer );


#endif

