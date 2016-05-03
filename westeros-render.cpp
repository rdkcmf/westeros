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
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <pthread.h>
#include <unistd.h>

#include <dlfcn.h>

#include "westeros-nested.h"
#include "westeros-render.h"

#define DEFAULT_OUTPUT_WIDTH (1280)
#define DEFAULT_OUTPUT_HEIGHT (720)

#define WESTEROS_UNUSED(x) ((void)(x))


WstRenderer* WstRendererCreate( const char *moduleName, int argc, char **argv, struct wl_display *display, WstNestedConnection *nc )
{
   bool error= false;
   void *module= 0, *init;

   WstRenderer *renderer= (WstRenderer*)calloc( 1, sizeof(WstRenderer) );
   if ( renderer )
   {
      int rc;
      const char *displayName= 0;
      int i= 0;
      int len, value;
      int width= DEFAULT_OUTPUT_WIDTH;
      int height= DEFAULT_OUTPUT_HEIGHT;
      void *nativeWindow= 0;
      
      while ( i < argc )
      {
         len= strlen(argv[i]);
         if ( (len == 7) && (strncmp( argv[i], "--width", len) == 0) )
         {
            if ( i+1 < argc )
            {
               ++i;
               value= atoi(argv[i]);
               if ( value > 0 )
               {
                  width= value;
               }
            }
         }
         else
         if ( (len == 8) && (strncmp( argv[i], "--height", len) == 0) )
         {
            if ( i+1 < argc )
            {
               ++i;
               value= atoi(argv[i]);
               if ( value > 0 )
               {
                  height= value;
               }
            }
         }
         else
         if ( (len == 14) && (strncmp( argv[i], "--nativeWindow", len) == 0) )
         {
            if ( i+1 < argc )
            {
               void *value= 0;
               ++i;
               if ( sscanf( argv[i], "%p", &value ) == 1 )
               {
                  nativeWindow= value;
               }
            }
         }
         ++i;
      }
      
      renderer->display= display;
      renderer->nc= nc;
      if ( nc )
      {
         renderer->displayNested= WstNestedConnectionGetDisplay( nc );
         renderer->surfaceNested= WstNestedConnectionGetCompositionSurface( nc );
      }
            
      module= dlopen( moduleName, RTLD_NOW );
      if ( !module )
      {
         printf("WstRendererCreate: failed to load module (%s)\n", moduleName);
         printf("  detail: %s\n", dlerror() );
         error= true;
         goto exit;
      }
      
      init= dlsym( module, RENDERER_MODULE_INIT );
      if ( !init )
      {
         printf("WstRendererCreate: failed to find module (%s) method (%s)\n", moduleName, RENDERER_MODULE_INIT );
         printf("  detail: %s\n", dlerror() );
         error= true;
         goto exit;
      }

      renderer->outputWidth= width;
      renderer->outputHeight= height;
      renderer->nativeWindow= nativeWindow;
      
      rc= ((WSTMethodRenderInit)init)( renderer, argc, argv );
      if ( rc )
      {
         printf("WstRendererCreate: module (%s) init failed: %d\n", moduleName, rc );
         error= true;
         goto exit;
      }
      
      printf("WstRendererCreate: module (%s) loaded and intialized\n", moduleName );
   }
   
exit:

   if ( error )
   {
      if ( renderer )
      {
         WstRendererDestroy( renderer );
      }
      if ( module )
      {
         dlclose( module );
      }
   }
   
   return renderer;
}

void WstRendererDestroy( WstRenderer *renderer )
{
   if ( renderer )
   {
      if ( renderer->renderer )
      {
         renderer->renderTerm( renderer );
         renderer->renderer= 0;
      }
      free( renderer );      
   }
}

void WstRendererUpdateScene( WstRenderer *renderer )
{
   renderer->updateScene( renderer );
}

WstRenderSurface* WstRendererSurfaceCreate( WstRenderer *renderer )
{
   return renderer->surfaceCreate( renderer );
}

void WstRendererSurfaceDestroy( WstRenderer *renderer, WstRenderSurface *surface )
{
   renderer->surfaceDestroy( renderer, surface );
}

void WstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   renderer->surfaceCommit( renderer, surface, resource );
}

void WstRendererSurfaceSetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool visible )
{
   renderer->surfaceSetVisible( renderer, surface, visible );
}

bool WstRendererSurfaceGetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool *visible )
{
   return renderer->surfaceGetVisible( renderer, surface, visible );
}

void WstRendererSurfaceSetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height )
{
   renderer->surfaceSetGeometry( renderer, surface, x, y, width, height );
}

void WstRendererSurfaceGetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height )
{
   renderer->surfaceGetGeometry( renderer, surface, x, y, width, height );
}

void WstRendererSurfaceSetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float opacity )
{
   renderer->surfaceSetOpacity( renderer, surface, opacity );
}

float WstRendererSurfaceGetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float *opacity )
{
   return renderer->surfaceGetOpacity( renderer, surface, opacity );
}

void WstRendererSurfaceSetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float z )
{
   renderer->surfaceSetZOrder( renderer, surface, z );
}

float WstRendererSurfaceGetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float *z )
{
   return renderer->surfaceGetZOrder( renderer, surface, z );
}

void WstRendererDelegateUpdateScene( WstRenderer *renderer, std::vector<WstRect> &rects )
{
   renderer->delegateUpdateScene( renderer, rects );
}

