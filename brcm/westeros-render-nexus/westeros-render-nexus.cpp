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

#include "westeros-render.h"
#include "wayland-server.h"
#include "wayland-client.h"
#include "wayland-egl.h"

#ifdef ENABLE_SBPROTOCOL
#include "westeros-simplebuffer.h"
#endif

#include "nexus_config.h"
#include "nexus_platform.h"
#include "nexus_display.h"
#include "default_nexus.h"
#include "nxclient.h"
#include "nexus_platform_client.h"
#include "nexus_surface_client.h"
#if NEXUS_PLATFORM_VERSION_MAJOR >= 16
#include "nexus_video_decoder.h"
#endif

#include <vector>

#define WST_UNUSED( n ) ((void)n)

#define MAX_ZORDER (100)

#define DEFAULT_SURFACE_WIDTH (0)
#define DEFAULT_SURFACE_HEIGHT (0)
#define NUM_SURFACES (2)

struct _WstRenderSurface
{
   int x;
   int y;
   int width;
   int height;
   float zorder;
   float opacity;
   bool visible;
   bool sizeOverride;
   
   NxClient_AllocResults allocResults;
   bool eventCreated;
   BKNI_EventHandle displayedEvent;
   NEXUS_SurfaceClientHandle gfxSurfaceClient;
   
   int back;
   int surfaceWidth;
   int surfaceHeight;
   int surfacePixelFormat;
   NEXUS_SurfaceHandle surface[NUM_SURFACES];
   NEXUS_SurfaceHandle surfacePending;
   NEXUS_SurfaceHandle surfacePush;
   WstRect rectCurr;
};

typedef struct _WstRendererNX
{
   WstRenderer *renderer;
   int outputWidth;
   int outputHeight;
   std::vector<WstRenderSurface*> surfaces;
   bool isDelegate;
   bool secureGraphics;

   EGLDisplay eglDisplay;
   bool haveWaylandEGL;
   PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
   PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
   PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
} WstRendererNX;

static const NEXUS_BlendEquation graphicsColorBlend = {
  NEXUS_BlendFactor_eSourceColor,
  NEXUS_BlendFactor_eSourceAlpha,
  false,
  NEXUS_BlendFactor_eDestinationColor,
  NEXUS_BlendFactor_eInverseSourceAlpha,
  false,
  NEXUS_BlendFactor_eZero
};

static const NEXUS_BlendEquation graphicsAlphaBlend = {
  NEXUS_BlendFactor_eDestinationAlpha,
  NEXUS_BlendFactor_eOne,
  false,
  NEXUS_BlendFactor_eZero,
  NEXUS_BlendFactor_eZero,
  false,
  NEXUS_BlendFactor_eSourceAlpha
};

static bool useSecureGraphics( void )
{
   bool useSecure= false;

   #if NEXUS_PLATFORM_VERSION_MAJOR >= 16
   char *env= getenv("WESTEROS_SECURE_GRAPHICS");
   if ( env && atoi(env) )
   {
      NEXUS_VideoDecoderCapabilities videoDecoderCap;
      NEXUS_GetVideoDecoderCapabilities(&videoDecoderCap);
      useSecure=  (videoDecoderCap.memory[0].secure == NEXUS_SecureVideo_eSecure) ? true : false;
   }
   #endif

   setenv("WESTEROS_RENDER_PROTECTED_CONTENT", (useSecure ?  "1" : "0"), 1);

   return useSecure;
}

static void gfxCheckpoint(void *data, int unused)
{
    BSTD_UNUSED(unused);
    BKNI_SetEvent((BKNI_EventHandle)data);
}

static void wstRendererPushSurface( WstRendererNX *renderer, WstRenderSurface *surface )
{
   NEXUS_Error rc;

   if ( surface->surfacePush )
   {
      rc= NEXUS_SurfaceClient_PushSurface(surface->gfxSurfaceClient,
                                          surface->surfacePush,
                                          NULL,
                                          true );
      if ( rc )
      {
         printf("westeros_renderer_nexus: NEXUS_SurfaceClient_PushSurface rc %d\n", rc);
      }

      surface->surfacePush= 0;

      size_t n= 0;
      do
      {
         NEXUS_SurfaceHandle surface_list[10];
         int rc = NEXUS_SurfaceClient_RecycleSurface(surface->gfxSurfaceClient, surface_list, 10, &n);
         if (rc) break;
      }
      while (n >= 10);
   }
}

static void wstRendererFreeSurfaces( WstRendererNX *renderer, WstRenderSurface *surface )
{
   int i;
   
   for( i= 0; i < NUM_SURFACES; ++i )
   {
      if ( surface->surface[i] )
      {
         NEXUS_Surface_Destroy( surface->surface[i] );
         surface->surface[i]= 0;         
      }
   }
   surface->surfaceWidth= 0;
   surface->surfaceHeight= 0;
   surface->surfacePixelFormat= NEXUS_PixelFormat_eUnknown;
}

static bool wstRendererAllocSurfaces( WstRendererNX *renderer, WstRenderSurface *surface,
                                      int width, int height, int pixelFormat )
{
   bool result= true;
   NEXUS_SurfaceCreateSettings surfaceCreateSettings;

   if ( width < 16 ) width= 16;
   if ( height < 16 ) height= 16;
   
   if ( (surface->surfaceWidth != width) || 
        (surface->surfaceHeight != height) ||
        (surface->surfacePixelFormat != pixelFormat) )
   {
      wstRendererFreeSurfaces(renderer, surface);
      
      surface->surfaceWidth= width;
      surface->surfaceHeight= height;
      surface->surfacePixelFormat= pixelFormat;

      NEXUS_Surface_GetDefaultCreateSettings(&surfaceCreateSettings);
      surfaceCreateSettings.width= surface->surfaceWidth;
      surfaceCreateSettings.height= surface->surfaceHeight;
      surfaceCreateSettings.pixelFormat= (NEXUS_PixelFormat)surface->surfacePixelFormat;
      for( int i= 0; i < NUM_SURFACES; ++i )
      {
         surface->surface[i]= NEXUS_Surface_Create(&surfaceCreateSettings);
         if ( !surface->surface[i] )
         {
            wstRendererFreeSurfaces(renderer, surface);
            result= false;
            break;
         }
      }
   }
   return result;
}

static WstRendererNX* wstRendererNXCreate( WstRenderer *renderer )
{
   WstRendererNX *rendererNX= 0;
   
   rendererNX= (WstRendererNX*)calloc( 1, sizeof(WstRendererNX) );
   if ( rendererNX )
   {
      NEXUS_Error rc;
      NxClient_JoinSettings joinSettings;
      
      rendererNX->renderer= renderer;
      rendererNX->outputWidth= renderer->outputWidth;
      rendererNX->outputHeight= renderer->outputHeight;

      NxClient_GetDefaultJoinSettings( &joinSettings );
      snprintf( joinSettings.name, NXCLIENT_MAX_NAME, "%s", "westeros_renderer_nexus");
      rc= NxClient_Join( &joinSettings );
      printf("westeros_render_nexus: wstRendererNXCreate: NxClient_Join rc=%X as %s\n", rc, joinSettings.name );
      if ( NEXUS_SUCCESS != rc )
      {
         printf("WstGLInit: NxClient_Join failed: rc=%X\n", rc);
         free( rendererNX );
         rendererNX= 0;
         goto exit;
      }

      rendererNX->secureGraphics= useSecureGraphics();
      printf("westeros_render_nexus: secure graphics: %d\n", rendererNX->secureGraphics);
      if ( rendererNX->secureGraphics )
      {
         NxClient_DisplaySettings displaySettings;

         NxClient_GetDisplaySettings( &displaySettings );
         #if NEXUS_PLATFORM_VERSION_MAJOR >= 16
         displaySettings.secure= rendererNX->secureGraphics;
         #endif
         rc= NxClient_SetDisplaySettings( &displaySettings );
         if ( rc != NEXUS_SUCCESS )
         {
            printf("WstGLInit: NxClient_SetDisplaySettings failed: rc=%X\n", rc);
         }
      }

      if ( renderer->display )
      {
         rendererNX->eglDisplay= eglGetDisplay(EGL_DEFAULT_DISPLAY);

         const char *extensions= eglQueryString( rendererNX->eglDisplay, EGL_EXTENSIONS );
         if ( extensions )
         {
            if ( !strstr( extensions, "EGL_WL_bind_wayland_display" ) )
            {
               printf("wayland-egl support expected, but not advertised by eglQueryString(eglDisplay,EGL_EXTENSIONS): not attempting to use\n" );
            }
            else
            {
               printf("wayland-egl support expected, and is advertised by eglQueryString(eglDisplay,EGL_EXTENSIONS): proceeding to use \n" );
            
               rendererNX->eglBindWaylandDisplayWL= (PFNEGLBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglBindWaylandDisplayWL");
               printf( "eglBindWaylandDisplayWL %p\n", rendererNX->eglBindWaylandDisplayWL );

               rendererNX->eglUnbindWaylandDisplayWL= (PFNEGLUNBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglUnbindWaylandDisplayWL");
               printf( "eglUnbindWaylandDisplayWL %p\n", rendererNX->eglUnbindWaylandDisplayWL );

               rendererNX->eglQueryWaylandBufferWL= (PFNEGLQUERYWAYLANDBUFFERWL)eglGetProcAddress("eglQueryWaylandBufferWL");
               printf( "eglQueryWaylandBufferWL %p\n", rendererNX->eglQueryWaylandBufferWL );
               
               if ( rendererNX->eglBindWaylandDisplayWL &&
                    rendererNX->eglUnbindWaylandDisplayWL &&
                    rendererNX->eglQueryWaylandBufferWL )
               {               
                  printf("calling eglBindWaylandDisplayWL with eglDisplay %p and wayland display %p\n", rendererNX->eglDisplay, renderer->display );
                  EGLBoolean rc= rendererNX->eglBindWaylandDisplayWL( rendererNX->eglDisplay, renderer->display );
                  if ( rc )
                  {
                     rendererNX->haveWaylandEGL= true;
                  }
                  else
                  {
                     printf("eglBindWaylandDisplayWL failed: %x\n", eglGetError() );
                  }
               }
               else
               {
                  printf("wayland-egl support expected, and advertised, but methods are missing: no wayland-egl\n" );
               }
            }
         }
         printf("have wayland-egl: %d\n", rendererNX->haveWaylandEGL );
      }
   }

exit:
   return rendererNX;
}

static void wstRendererNXDestroy( WstRendererNX *renderer )
{
   if ( renderer )
   {
      if ( renderer->haveWaylandEGL )
      {
         renderer->eglUnbindWaylandDisplayWL( renderer->eglDisplay, renderer->renderer->display );
         renderer->haveWaylandEGL= false;
      }

      free( renderer );
      
      NxClient_Uninit();
   }
}

static WstRenderSurface* wstRenderNXCreateSurface( WstRendererNX *renderer )
{
   NEXUS_Error rc;
   WstRenderSurface *surface= 0;

   WST_UNUSED(renderer);   
   
   surface= (WstRenderSurface*)calloc( 1, sizeof(WstRenderSurface) );
   if ( surface )
   {
      NxClient_AllocSettings allocSettings;
      NEXUS_SurfaceComposition composition;
      NEXUS_SurfaceClientSettings clientSettings;

      NxClient_GetDefaultAllocSettings( &allocSettings );

      allocSettings.surfaceClient = 1;
      
      rc= NxClient_Alloc( &allocSettings, &surface->allocResults );
      printf( "westeros_render_nexus: wstRendererNXCreateSurface: NxClient_Alloc rc=%X surfaceClientId=%X\n", 
              rc, surface->allocResults.surfaceClient[0].id );

      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);

      surface->width= DEFAULT_SURFACE_WIDTH;
      surface->height= DEFAULT_SURFACE_HEIGHT;
      surface->x= 0;
      surface->y= 0;
      surface->visible= true;
      surface->opacity= 1.0;
      surface->zorder= 0.5;

      composition.position.x= surface->x;
      composition.position.y= surface->y;
      composition.position.width= surface->width;
      composition.position.height= surface->height;
      composition.zorder= (unsigned)(surface->zorder*MAX_ZORDER);;
      composition.colorBlend= graphicsColorBlend;
      composition.alphaBlend= graphicsAlphaBlend;
      composition.visible= false;
      rc= NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      
      renderer->renderer->surfaceSetOpacity( renderer->renderer, surface, surface->opacity );
      
      surface->back= 0;
      surface->surfaceWidth= 0;
      surface->surfaceHeight= 0;
      surface->surfacePixelFormat= NEXUS_PixelFormat_eUnknown;

      /* Create events for gfx feedback */
      BKNI_CreateEvent(&surface->displayedEvent);
      surface->eventCreated= true;

      /* Create a surface client */
      surface->gfxSurfaceClient= NEXUS_SurfaceClient_Acquire(surface->allocResults.surfaceClient[0].id);
      printf("westeros_render_nexus: wstRendererNXCreateSurface: gfxSurfaceClient %p\n", surface->gfxSurfaceClient );

      /* Setup displayed callback and event */
      NEXUS_SurfaceClient_GetSettings(surface->gfxSurfaceClient, &clientSettings);
      clientSettings.displayed.callback= gfxCheckpoint;
      clientSettings.displayed.context= surface->displayedEvent;
      NEXUS_SurfaceClient_SetSettings(surface->gfxSurfaceClient, &clientSettings);
   }
   
   return surface;
}

void wstRendererNXDestroySurface( WstRendererNX *renderer, WstRenderSurface *surface )
{
   WST_UNUSED(renderer);    
   
   if ( surface )
   {
      wstRendererFreeSurfaces( renderer, surface );

      if ( surface->gfxSurfaceClient )
      {
         NEXUS_SurfaceClient_Release(surface->gfxSurfaceClient);
         surface->gfxSurfaceClient= 0;
      }
      
      if ( surface->eventCreated )
      {
         surface->eventCreated= false;
         BKNI_DestroyEvent(surface->displayedEvent);
      }

      NxClient_Free(&surface->allocResults);
      
      free( surface );
   }
}

static void wstRendererNXCommitShm( WstRendererNX *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   struct wl_shm_buffer *shmBuffer;
   int width, height, stride;
   int pixelFormat, i;
   void *data;
   NEXUS_SurfaceMemory mem;
   NEXUS_SurfaceComposition composition;            
   NEXUS_SurfaceHandle nexusSurface= 0;

   shmBuffer= wl_shm_buffer_get( resource );
   if ( shmBuffer )
   {
      width= wl_shm_buffer_get_width(shmBuffer);
      height= wl_shm_buffer_get_height(shmBuffer);
      stride= wl_shm_buffer_get_stride(shmBuffer);

      // The SHM formats describe the structure of the color channels for a pixel as
      // they would appear in a machine register not the byte order in memory.  For 
      // example WL_SHM_FORMAT_ARGB8888 is a 32 bit pixel with alpha in the 8 most significant
      // bits and blue in the 8 list significant bits.  On a little endian machine the
      // byte order in memory would be B, G, R, A.
      switch( wl_shm_buffer_get_format(shmBuffer) )
      {
         case WL_SHM_FORMAT_ARGB8888:
            pixelFormat= NEXUS_PixelFormat_eA8_R8_G8_B8;
            break;
         case WL_SHM_FORMAT_XRGB8888:
            pixelFormat= NEXUS_PixelFormat_eX8_R8_G8_B8;
            break;
         case WL_SHM_FORMAT_BGRA8888:
            pixelFormat= NEXUS_PixelFormat_eB8_G8_R8_A8;
            break;
         case WL_SHM_FORMAT_BGRX8888:
            pixelFormat= NEXUS_PixelFormat_eB8_G8_R8_X8;
            break;
         case WL_SHM_FORMAT_RGB565:
            pixelFormat= NEXUS_PixelFormat_eR5_G6_B5;
            break;
         case WL_SHM_FORMAT_ARGB4444:
            pixelFormat= NEXUS_PixelFormat_eA4_R4_G4_B4;
            break;
         default:
            pixelFormat= NEXUS_PixelFormat_eUnknown;
            break;
      }
      if ( pixelFormat != NEXUS_PixelFormat_eUnknown )
      {
         wl_shm_buffer_begin_access(shmBuffer);
         data= wl_shm_buffer_get_data(shmBuffer);
         
         if ( 
              (surface->surfaceWidth != width) ||
              (surface->surfaceHeight != height) ||
              (surface->surfacePixelFormat != pixelFormat)
            )
         {
            wstRendererAllocSurfaces( renderer, surface, width, height, pixelFormat );
         }
         
         nexusSurface= surface->surface[surface->back];
         if ( nexusSurface )
         {            
            unsigned char *src, *dest;
            
            NEXUS_Surface_GetMemory( nexusSurface, &mem );

            src= (unsigned char *)data;
            dest= (unsigned char *)mem.buffer;

            if ( mem.pitch == stride )
            {
               memcpy( dest, src, height*stride );
               dest += height*mem.pitch;
            }
            else
            {               
               for( i= 0; i < height; ++i )
               {
                  memcpy( dest, src, stride );
                  if ( stride < mem.pitch )
                  {
                     memset( dest+stride, 0, (mem.pitch-stride) );
                  }
                  dest += mem.pitch;
                  src += stride;
               }
            }
            if ( height < surface->surfaceHeight )
            {
               memset( dest, 0, mem.pitch*(surface->surfaceHeight-height) );
            }
            NEXUS_Surface_Flush( nexusSurface );

            if ( !surface->sizeOverride )
            {
               surface->width= surface->surfaceWidth;
               surface->height= surface->surfaceHeight;
            }

            if ( !renderer->isDelegate )
            {
               NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
               composition.position.width= surface->width;
               composition.position.height= surface->height;
               NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
            }

            surface->surfacePending= nexusSurface;
         }

         wl_shm_buffer_end_access(shmBuffer);
         
         ++surface->back;
         if ( surface->back >= NUM_SURFACES )
         {
            surface->back= 0;
         }
      }
   }
}

#ifdef ENABLE_SBPROTOCOL
static void wstRendererNXCommitSB( WstRendererNX *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   NEXUS_Error rc;
   struct wl_sb_buffer *sbBuffer;
   int bufferWidth, bufferHeight;
   void *deviceBuffer;
   
   sbBuffer= WstSBBufferGet( resource );
   if ( sbBuffer )
   {
      deviceBuffer= WstSBBufferGetBuffer( sbBuffer );
      if ( deviceBuffer )
      {
         NEXUS_SurfaceHandle surfaceIn;
         NEXUS_SurfaceStatus surfaceStatusIn;

         surfaceIn= (NEXUS_SurfaceHandle)deviceBuffer;
         NEXUS_Surface_GetStatus( surfaceIn, &surfaceStatusIn );

         bufferWidth= surfaceStatusIn.width;
         bufferHeight= surfaceStatusIn.height;

         if ( (surface->width != bufferWidth) || (surface->height != bufferHeight) )
         {
            NEXUS_SurfaceComposition composition;
            
            if ( !surface->sizeOverride )
            {
               surface->width= bufferWidth;
               surface->height= bufferHeight;
            }

            if ( !renderer->isDelegate )
            {
               NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
               composition.position.width= surface->width;
               composition.position.height= surface->height;
               NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
            }
         }
         
         surface->surfacePending= surfaceIn;
      }
   }
}
#endif

#if defined (WESTEROS_HAVE_WAYLAND_EGL)
static void wstRendererNXCommitBNXS( WstRendererNX *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   NEXUS_Error rc;
   int bufferWidth, bufferHeight;
   void *deviceBuffer;

   deviceBuffer= wl_egl_get_device_buffer( resource );   
   if ( deviceBuffer )
   {
      NEXUS_SurfaceHandle surfaceIn;
      NEXUS_SurfaceStatus surfaceStatusIn;

      surfaceIn= (NEXUS_SurfaceHandle)deviceBuffer;
      NEXUS_Surface_GetStatus( surfaceIn, &surfaceStatusIn );

      bufferWidth= surfaceStatusIn.width;
      bufferHeight= surfaceStatusIn.height;

      if ( (surface->width != bufferWidth) || (surface->height != bufferHeight) )
      {
         NEXUS_SurfaceComposition composition;
         
         if ( !surface->sizeOverride )
         {
            surface->width= bufferWidth;
            surface->height= bufferHeight;
         }

         if ( !renderer->isDelegate )
         {
            NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
            composition.position.width= surface->width;
            composition.position.height= surface->height;
            NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
         }
      }
      
      surface->surfacePending= surfaceIn;
   }
}
#endif

static void wstRendererTerm( WstRenderer *renderer )
{
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   if ( rendererNX )
   {
      wstRendererNXDestroy( rendererNX );
      renderer->renderer= 0;
   }
}

static void wstRendererUpdateScene( WstRenderer *renderer )
{
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   WstRenderSurface *surface;
   NEXUS_SurfaceComposition composition;


   for ( std::vector<WstRenderSurface*>::iterator it= rendererNX->surfaces.begin();
         it != rendererNX->surfaces.end();
         ++it )
   {
      surface= (*it);

      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      if ( composition.visible != surface->visible )
      {
         composition.visible= surface->visible;
         NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      }

      if ( surface->surfacePending )
      {
         surface->surfacePush= surface->surfacePending;
         surface->surfacePending= 0;
         wstRendererPushSurface( rendererNX, surface );
      }
   }
}

static WstRenderSurface* wstRendererSurfaceCreate( WstRenderer *renderer )
{
   WstRenderSurface *surface;
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;

   surface= wstRenderNXCreateSurface(rendererNX);

   std::vector<WstRenderSurface*>::iterator it= rendererNX->surfaces.begin();
   while ( it != rendererNX->surfaces.end() )
   {
      if ( surface->zorder < (*it)->zorder )
      {
         break;
      }
      ++it;
   }
   rendererNX->surfaces.insert(it,surface);
   
   return surface; 
}

static void wstRendererSurfaceDestroy( WstRenderer *renderer, WstRenderSurface *surface )
{
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;

   for ( std::vector<WstRenderSurface*>::iterator it= rendererNX->surfaces.begin(); 
         it != rendererNX->surfaces.end();
         ++it )
   {
      if ( (*it) == surface )
      {
         rendererNX->surfaces.erase(it);
         break;   
      }
   }   
   
   wstRendererNXDestroySurface( rendererNX, surface );
}

static void wstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   EGLint value;

   if ( resource )
   {
      if ( wl_shm_buffer_get( resource ) )
      {
         wstRendererNXCommitShm( rendererNX, surface, resource );
      }
      #ifdef ENABLE_SBPROTOCOL
      else if ( WstSBBufferGet( resource ) )
      {
         wstRendererNXCommitSB( rendererNX, surface, resource );
      }
      #endif
      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      else if ( wl_egl_get_device_buffer( resource ) )
      {
         wstRendererNXCommitBNXS( rendererNX, surface, resource );
      }
      #endif
      else
      {
         printf("wstRendererSurfaceCommit: unsupported buffer type\n");
      }
   }
   else
   {
      surface->surfacePending= 0;
      NEXUS_SurfaceClient_Clear(surface->gfxSurfaceClient);
   }
}

static void wstRendererSurfaceSetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool visible )
{
   NEXUS_Error rc;
   NEXUS_SurfaceComposition composition;
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   
   if ( surface )
   {
      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);

      composition.visible= visible;

      rc= NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      
      surface->visible= visible;
   }
}

static bool wstRendererSurfaceGetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool *visible )
{
   bool isVisible= false;
   NEXUS_Error rc;
   NEXUS_SurfaceComposition composition;
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   
   if ( surface )
   {
      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      
      isVisible= surface->visible;

      if(isVisible != composition.visible)
      {
         printf("westeros_render_nexus: %s query received before scene update, returning surface->visible=%d, composition.visible=%d\n", __FUNCTION__, surface->visible, composition.visible);
      }
      
      *visible= isVisible;
   }
   
   return isVisible;
}

static void wstRendererSurfaceSetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height )
{
   NEXUS_Error rc;
   NEXUS_SurfaceComposition composition;
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;

   if ( surface )
   {
      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);

      if ( (width != surface->width) || (height != surface->height) )
      {
         surface->sizeOverride= true;
      }
      surface->x= x;
      surface->y= y;
      surface->width= width;
      surface->height= height;
      
      composition.position.x= x;
      composition.position.y= y;
      composition.position.width= width;
      composition.position.height= height;

      if ( rendererNX->isDelegate )
      {
         // As a delegate we will determine geometry during the wstRendererDelegateUpdateScene method taking
         // into account the active matrix
      }
      else
      {
         rc= NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      }
   }
}

void wstRendererSurfaceGetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height )
{
   NEXUS_Error rc;
   NEXUS_SurfaceComposition composition;
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   
   if ( surface )
   {
      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      
      *x= surface->x;
      *y= surface->y;
      *width= surface->width;
      *height= surface->height;
   }
}

static void wstRendererSurfaceSetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float opacity )
{
   NEXUS_Error rc;
   NEXUS_SurfaceComposition composition;
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   
   if ( surface )
   {
      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);

      composition.colorMatrixEnabled= (opacity < 1.0);

      NEXUS_Graphics2DColorMatrix *pMatrix= &composition.colorMatrix;
      BKNI_Memset(pMatrix, 0, sizeof(*pMatrix));
      pMatrix->shift= 8; /* 2^8 == 256. this causes alpha to be 0%...100%. */
      pMatrix->coeffMatrix[0]=
      pMatrix->coeffMatrix[6]=
      pMatrix->coeffMatrix[12]= 0xff; /* don't modify color */
      pMatrix->coeffMatrix[18]= (unsigned int)(opacity*255); /* reduce by server-side alpha */

      rc= NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      
      surface->opacity= opacity;
   }
}

static float wstRendererSurfaceGetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float *opacity )
{
   NEXUS_Error rc;
   NEXUS_SurfaceComposition composition;
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   float opacityLevel= 1.0;
   
   if ( surface )
   {
      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);

      opacityLevel= ((float)composition.colorMatrix.coeffMatrix[18])/255.0;
      
      *opacity= opacityLevel;
   }
   
   return opacityLevel;
}

static void wstRendererSurfaceSetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float z )
{
   NEXUS_Error rc;
   NEXUS_SurfaceComposition composition;
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   
   if ( surface )
   {
      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      
      composition.zorder= (unsigned)(z*MAX_ZORDER);
      
      rc= NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);
      
      surface->zorder= z;

      // Remove from surface list
      for ( std::vector<WstRenderSurface*>::iterator it= rendererNX->surfaces.begin(); 
            it != rendererNX->surfaces.end();
            ++it )
      {
         if ( (*it) == surface )
         {
            rendererNX->surfaces.erase(it);
            break;   
         }
      }   

      // Re-insert in surface list based on new z-order
      std::vector<WstRenderSurface*>::iterator it= rendererNX->surfaces.begin();
      while ( it != rendererNX->surfaces.end() )
      {
         if ( surface->zorder < (*it)->zorder )
         {
            break;
         }
         ++it;
      }
      rendererNX->surfaces.insert(it,surface);
   }
}

static float wstRendererSurfaceGetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float *z )
{
   NEXUS_Error rc;
   NEXUS_SurfaceComposition composition;
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   float zLevel= MAX_ZORDER;
   
   if ( surface )
   {
      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);

      zLevel= ((float)composition.zorder)/(float)MAX_ZORDER;
      
      *z= zLevel;
   }
   
   return zLevel;
}

static void wstRendererDelegateUpdateScene( WstRenderer *renderer, std::vector<WstRect> &rects )
{
   WstRendererNX *rendererNX= (WstRendererNX*)renderer->renderer;
   WstRenderSurface *surface;
   float sx, sy, tx, ty, opacity;
   NEXUS_SurfaceComposition composition;
   WstRect rect;
   
   rendererNX->isDelegate= true;

   sx= renderer->matrix[0];
   sy= renderer->matrix[5];
   tx= renderer->matrix[12];
   ty= renderer->matrix[13];

   for ( std::vector<WstRenderSurface*>::iterator it= rendererNX->surfaces.begin(); 
         it != rendererNX->surfaces.end();
         ++it )
   {
      surface= (*it);

      opacity= surface->opacity*renderer->alpha;

      NxClient_GetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);

      composition.colorMatrixEnabled= (opacity < 1.0);
      rect.x= composition.position.x= surface->x*sx+tx;
      rect.y= composition.position.y= surface->y*sy+ty;
      rect.width= composition.position.width= surface->width*sx;
      rect.height= composition.position.height= surface->height*sy;
      if ( composition.visible != surface->visible )
      {
         composition.visible= surface->visible;
      }

      if ( surface->visible )
      {
         rects.push_back( rect );
      }

      surface->surfacePush= surface->surfacePending;
      surface->surfacePending= 0;

      surface->rectCurr= rect;
      composition.position.x= rect.x;
      composition.position.y= rect.y;
      composition.position.width= rect.width;
      composition.position.height= rect.height;

      composition.colorMatrixEnabled= (opacity < 1.0);
      composition.visible= surface->visible;

      NEXUS_Graphics2DColorMatrix *pMatrix= &composition.colorMatrix;
      BKNI_Memset(pMatrix, 0, sizeof(*pMatrix));
      pMatrix->shift= 8; /* 2^8 == 256. this causes alpha to be 0%...100%. */
      pMatrix->coeffMatrix[0]=
      pMatrix->coeffMatrix[6]=
      pMatrix->coeffMatrix[12]= 0xff; /* don't modify color */
      pMatrix->coeffMatrix[18]= (unsigned int)(opacity*255); /* reduce by server-side alpha */

      NxClient_SetSurfaceClientComposition(surface->allocResults.surfaceClient[0].id, &composition);

      wstRendererPushSurface( rendererNX, surface );
   }
   
   renderer->needHolePunch= true;
}


extern "C"
{

int renderer_init( WstRenderer *renderer, int argc, char **argv )
{
   int rc= 0;
   WstRendererNX *rendererNX= 0;
   const char *displayName= 0;
   int i= 0;
   int len, value;
   
   while ( i < argc )
   {
      len= strlen(argv[i]);
      if ( (len == 9) && (strncmp( argv[i], "--display", len) == 0) )
      {
         if ( i+1 < argc )
         {
            ++i;
            displayName= argv[i];
         }
      }
      ++i;
   }
   
   if ( displayName )
   {
      rc= -1;
      printf("unsupported argument: --display: Nexus renderer does not support nested composition to a wayland display\n");
      goto exit;
   }

   rendererNX= wstRendererNXCreate( renderer );
   if ( rendererNX )
   {
      renderer->renderer= rendererNX;
      renderer->renderTerm= wstRendererTerm;
      renderer->updateScene= wstRendererUpdateScene;
      renderer->surfaceCreate= wstRendererSurfaceCreate;
      renderer->surfaceDestroy= wstRendererSurfaceDestroy;
      renderer->surfaceCommit= wstRendererSurfaceCommit;
      renderer->surfaceSetVisible= wstRendererSurfaceSetVisible;
      renderer->surfaceGetVisible= wstRendererSurfaceGetVisible;
      renderer->surfaceSetGeometry= wstRendererSurfaceSetGeometry;
      renderer->surfaceGetGeometry= wstRendererSurfaceGetGeometry;
      renderer->surfaceSetOpacity= wstRendererSurfaceSetOpacity;
      renderer->surfaceGetOpacity= wstRendererSurfaceGetOpacity;
      renderer->surfaceSetZOrder= wstRendererSurfaceSetZOrder;
      renderer->surfaceGetZOrder= wstRendererSurfaceGetZOrder;
      renderer->delegateUpdateScene= wstRendererDelegateUpdateScene;
   }
   else
   {
      rc= -1;
   }
   
exit:   
   
   return rc;
}

}

