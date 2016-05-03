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
#ifndef _WESTEROS_SIMPLESHELL_H
#define _WESTEROS_SIMPLESHELL_H

#include "wayland-server.h"

typedef struct _WstRenderer WstRenderer;
typedef struct _WstRenderSurface WstRenderSurface;

struct wl_simple_shell;

struct wayland_simple_shell_callbacks {
   void (*set_name)( void* userData, uint32_t surfaceId, const char *name );
   void (*set_visible)( void* userData, uint32_t surfaceId, bool visible );
   void (*set_geometry)( void* userData, uint32_t surfaceId, int x, int y, int width, int height );
   void (*set_opacity)( void* userData, uint32_t surfaceId, float opacity );
   void (*set_zorder)( void* userData, uint32_t surfaceId, float zorder );
   void (*get_name)( void* userData, uint32_t surfaceId, const char **name );
   void (*get_status)( void *userData, uint32_t surfaceId, bool *visible, 
                       int32_t *x, int32_t *y, int32_t *width, int32_t *height,
                       float *opacity, float *zorder );
};

wl_simple_shell* WstSimpleShellInit( struct wl_display *display,
                                     wayland_simple_shell_callbacks *callbacks,
                                     void *userData ); 
void WstSimpleShellUninit( wl_simple_shell *shell );

void WstSimpleShellNotifySurfaceCreated( wl_simple_shell *shell, struct wl_client *client, 
                                         struct wl_resource *surface_resource, uint32_t surfaceId );

void WstSimpleShellNotifySurfaceDestroyed( wl_simple_shell *shell, struct wl_client *client, uint32_t surfaceId );

#endif


