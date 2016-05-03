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
#ifndef _WESTEROS_RENDER_H
#define _WESTEROS_RENDER_H

#include <vector>

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

typedef struct _WstRect
{
   int x;
   int y;
   int width;
   int height;
} WstRect;

typedef struct _WstRenderer WstRenderer;
typedef struct _WstRenderSurface WstRenderSurface;
typedef struct _WstNestedConnection WstNestedConnection;

typedef int (*WSTMethodRenderInit)( WstRenderer *renderer, int argc, char **argv);
typedef void (*WSTMethodRenderTerm)( WstRenderer *renderer );
typedef void (*WSTMethodUpdateScene)( WstRenderer *renderer );
typedef WstRenderSurface* (*WSTMethodSurfaceCreate)( WstRenderer *renderer );
typedef void (*WSTMethodSurfaceDestroy)( WstRenderer *renderer, WstRenderSurface *surf );
typedef void (*WSTMethodSurfaceCommit)( WstRenderer *renderer, WstRenderSurface *surface, struct wl_resource *resource );
typedef void (*WSTMethodSurfaceSetVisible)( WstRenderer *renderer, WstRenderSurface *surface, bool visible );
typedef bool (*WSTMethodSurfaceGetVisible)( WstRenderer *renderer, WstRenderSurface *surface, bool *visible );
typedef void (*WSTMethodSurfaceSetGeometry)( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height );
typedef void (*WSTMethodSurfaceGetGeometry)( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height );
typedef void (*WSTMethodSurfaceSetOpacity)( WstRenderer *renderer, WstRenderSurface *surface, float opacity );
typedef float (*WSTMethodSurfaceGetOpacity)( WstRenderer *renderer, WstRenderSurface *surface, float *opaticty );
typedef void (*WSTMethodSurfaceSetZOrder)( WstRenderer *renderer, WstRenderSurface *surface, float z );
typedef float (*WSTMethodSurfaceGetZOrder)( WstRenderer *renderer, WstRenderSurface *surface, float *z );
typedef void (*WSTMethodDelegateUpdateScene)( WstRenderer *renderer, std::vector<WstRect> &rects );

typedef struct _WstRenderer
{
   int outputWidth;
   int outputHeight;
   void *nativeWindow;
   struct wl_display *display;
   void *renderer;
   
   WSTMethodRenderTerm renderTerm;
   WSTMethodUpdateScene updateScene;
   WSTMethodSurfaceCreate surfaceCreate;
   WSTMethodSurfaceDestroy surfaceDestroy;
   WSTMethodSurfaceCommit surfaceCommit;
   WSTMethodSurfaceSetVisible surfaceSetVisible;
   WSTMethodSurfaceGetVisible surfaceGetVisible;
   WSTMethodSurfaceSetGeometry surfaceSetGeometry;
   WSTMethodSurfaceGetGeometry surfaceGetGeometry;
   WSTMethodSurfaceSetOpacity surfaceSetOpacity;
   WSTMethodSurfaceGetOpacity surfaceGetOpacity;
   WSTMethodSurfaceSetZOrder surfaceSetZOrder;
   WSTMethodSurfaceGetZOrder surfaceGetZOrder;
   WSTMethodDelegateUpdateScene delegateUpdateScene;

   // For nested composition
   WstNestedConnection *nc;
   struct wl_display *displayNested;
   struct wl_surface *surfaceNested;
   
   // For embedded composition
   int outputX;
   int outputY;
   float *matrix;
   float alpha;
   bool fastHint;
   bool needHolePunch;
   std::vector<WstRect> rects;
} WstRenderer;

WstRenderer* WstRendererCreate( const char *moduleName, int argc, char **argv, 
                                struct wl_display *display, WstNestedConnection *nc );
void WstRendererDestroy( WstRenderer *renderer );

void WstRendererUpdateScene( WstRenderer *renderer );
WstRenderSurface* WstRendererSurfaceCreate( WstRenderer *renderer );
void WstRendererSurfaceDestroy( WstRenderer *renderer, WstRenderSurface *surface );
void WstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, struct wl_resource *resource );
void WstRendererSurfaceSetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool visible );
bool WstRendererSurfaceGetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool *visible );
void WstRendererSurfaceSetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height );
void WstRendererSurfaceGetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height );
void WstRendererSurfaceSetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float opacity );
float WstRendererSurfaceGetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float *opacity );
void WstRendererSurfaceSetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float z );
float WstRendererSurfaceGetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float *z );
void WstRendererDelegateUpdateScene( WstRenderer *renderer, std::vector<WstRect> &rects );

#endif

