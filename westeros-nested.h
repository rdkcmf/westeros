/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2016 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _WESTEROS_NESTED_H
#define _WESTEROS_NESTED_H

#include <pthread.h>

#include "wayland-client.h"
#include "wayland-server.h"

typedef struct _WstCompositor WstCompositor;
typedef struct _WstNestedConnection WstNestedConnection;

typedef void (*WSTCallbackConnectionEnded)( void *userData );
typedef void (*WSTCallbackOutputHandleGeometry)( void *userData, int32_t x, int32_t y, int32_t mmWidth, int32_t mmHeight,
                                                 int32_t subPixel, const char *make, const char *model, int32_t transform );
typedef void (*WSTCallbackOutputHandleMode)( void *userData, uint32_t flags, int32_t width, int32_t height, int32_t refreshRate );
typedef void (*WSTCallbackOutputHandleDone)( void *UserData );
typedef void (*WSTCallbackOutputHandleScale)( void *UserData, int32_t scale );
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

typedef void (*WSTCallbackVpcVideoPathChange)( void *userData, struct wl_surface *surface, uint32_t new_pathway );
typedef void (*WSTCallbackVpcVideoXformChange)( void *userData,
                                                struct wl_surface *surface,
                                                int32_t x_translation,
                                                int32_t y_translation,
                                                uint32_t x_scale_num,
                                                uint32_t x_scale_denom,
                                                uint32_t y_scale_num,
                                                uint32_t y_scale_denom );

typedef struct _WstNestedConnectionListener
{
   WSTCallbackConnectionEnded connectionEnded;
   WSTCallbackOutputHandleGeometry outputHandleGeometry;
   WSTCallbackOutputHandleMode outputHandleMode;
   WSTCallbackOutputHandleDone outputHandleDone;
   WSTCallbackOutputHandleScale outputHandleScale;
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
   WSTCallbackVpcVideoPathChange vpcVideoPathChange;
   WSTCallbackVpcVideoXformChange vpcVideoXformChange;
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

struct wl_vpc_surface* WstNestedConnectionGetVpcSurface( WstNestedConnection *nc, struct wl_surface *surface );

void WstNestedConnectionDestroyVpcSurface( WstNestedConnection *nc, struct wl_vpc_surface *vpcSurface );

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
                                               struct wl_resource *bufferRemote,
                                               void *deviceBuffer,
                                               uint32_t format,
                                               int32_t stride,
                                               int x,
                                               int y,
                                               int width,
                                               int height );

void WstNestedConnectionReleaseRemoteBuffers( WstNestedConnection *nc );

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

