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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <sys/time.h>

#include "westeros-render.h"
#include "wayland-server.h"
#include "wayland-client.h"
#include "wayland-egl.h"

#include "westeros-gl.h"

#ifdef ENABLE_SBPROTOCOL
#include "westeros-simplebuffer.h"
#endif

#include <bcm_host.h>

#include <vector>

#define WST_UNUSED( n ) ((void)n)

#define DEFAULT_SURFACE_WIDTH (0)
#define DEFAULT_SURFACE_HEIGHT (0)

typedef struct _WstRenderResource
{
   bool owned;
   DISPMANX_RESOURCE_HANDLE_T resource;
   int width;
   int height;
   int pixelFormat;
   bool invertY;
} WstRenderResource;

#define NUM_BUFFERS (2)
struct _WstRenderSurface
{
   int x;
   int y;
   int width;
   int height;
   float zorder;
   float opacity;
   bool visible;

   int bufferWidth;
   int bufferHeight;

   bool flip;
   bool dirty;
   bool sizeOverride;
   int front;
   WstRenderResource resource[NUM_BUFFERS];
   DISPMANX_ELEMENT_HANDLE_T element;
};

typedef struct _WstRendererDMX
{
   WstRenderer *renderer;
   int outputWidth;
   int outputHeight;
   std::vector<WstRenderSurface*> surfaces;
   
   DISPMANX_DISPLAY_HANDLE_T dispmanDisplay;
   bool updateInProgress;

   EGLDisplay eglDisplay;
   bool haveWaylandEGL;
   PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
   PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
   PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
} WstRendererDMX;

static WstRendererDMX* wstRendererDMXCreate( WstRenderer *renderer );
static void wstRendererDMXDestroy( WstRendererDMX *renderer );
static WstRenderSurface* wstRenderDMXCreateSurface( WstRendererDMX *renderer );
static void wstRendererDMXDestroySurface( WstRendererDMX *renderer, WstRenderSurface *surface );
static void  wstRendererDMXFlushSurface( WstRendererDMX *rendererDMX, WstRenderSurface *surface );
static void wstRendererDMXCommitShm( WstRendererDMX *renderer, WstRenderSurface *surface, struct wl_resource *resource );
#ifdef ENABLE_SBPROTOCOL
static void wstRendererDMXCommitSB( WstRendererDMX *renderer, WstRenderSurface *surface, struct wl_resource *resource );
#endif
#if defined (WESTEROS_HAVE_WAYLAND_EGL)
static void wstRendererDMXCommitWaylandEGL( WstRendererDMX *rendererDMX, WstRenderSurface *surface, 
                                           struct wl_resource *resource, EGLint format );
#endif

static bool emitFPS= false;

static WstRendererDMX* wstRendererDMXCreate( WstRenderer *renderer )
{
   WstRendererDMX *rendererDMX= 0;
   
   rendererDMX= (WstRendererDMX*)calloc( 1, sizeof(WstRendererDMX) );
   if ( rendererDMX )
   {
      int displayId;
      
      rendererDMX->renderer= renderer;
      rendererDMX->outputWidth= renderer->outputWidth;
      rendererDMX->outputHeight= renderer->outputHeight;
      rendererDMX->surfaces= std::vector<WstRenderSurface*>();

      if ( getenv("WESTEROS_RENDER_DISPMANX_FPS" ) )
      {
         emitFPS= true;
      }

      bcm_host_init();

      displayId= DISPMANX_ID_MAIN_LCD;         

      rendererDMX->dispmanDisplay= vc_dispmanx_display_open( displayId );
      if ( rendererDMX->dispmanDisplay == DISPMANX_NO_HANDLE )
      {
         printf("wstRenderDMXCreate: unable to open DISPMANX_ID_MAIN_LCD\n");
      }

      {
         EGLint major, minor;
         EGLBoolean b;

         rendererDMX->eglDisplay= eglGetDisplay(EGL_DEFAULT_DISPLAY);

         b= eglInitialize( rendererDMX->eglDisplay, &major, &minor );
         if ( b )
         {
            printf("wstRenderDMXCreate: eglInitiialize: major: %d minor: %d\n", major, minor );
         }
         else
         {
            printf("wstRenderDMXCreate: unable to initialize EGL display\n" );
         }

         const char *extensions= eglQueryString( rendererDMX->eglDisplay, EGL_EXTENSIONS );
         if ( extensions )
         {
            if ( !strstr( extensions, "EGL_WL_bind_wayland_display" ) )
            {
               printf("wstRendererDMXCreate: "
                      "wayland-egl support expected, but not advertised by eglQueryString(eglDisplay,EGL_EXTENSIONS): "
                      "not attempting to use\n" );
            }
            else
            {
               printf("wstRendererDMXCreate: "
                      "wayland-egl support expected, and is advertised by eglQueryString(eglDisplay,EGL_EXTENSIONS): "
                      "proceeding to use \n" );
            
               rendererDMX->eglBindWaylandDisplayWL= (PFNEGLBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglBindWaylandDisplayWL");
               printf( "wstRendererDMXCreate: eglBindWaylandDisplayWL %p\n", rendererDMX->eglBindWaylandDisplayWL );

               rendererDMX->eglUnbindWaylandDisplayWL= (PFNEGLUNBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglUnbindWaylandDisplayWL");
               printf( "wstRendererDMXCreate: eglUnbindWaylandDisplayWL %p\n", rendererDMX->eglUnbindWaylandDisplayWL );

               rendererDMX->eglQueryWaylandBufferWL= (PFNEGLQUERYWAYLANDBUFFERWL)eglGetProcAddress("eglQueryWaylandBufferWL");
               printf( "wstRendererDMXCreate: eglQueryWaylandBufferWL %p\n", rendererDMX->eglQueryWaylandBufferWL );
               
               if ( rendererDMX->eglBindWaylandDisplayWL &&
                    rendererDMX->eglUnbindWaylandDisplayWL &&
                    rendererDMX->eglQueryWaylandBufferWL )
               {
                  if ( renderer->display )
                  {
                     printf("wstRendererDMXCreate: "
                            "calling eglBindWaylandDisplayWL with eglDisplay %p and wayland display %p\n", 
                            rendererDMX->eglDisplay, renderer->display );
                     EGLBoolean rc= rendererDMX->eglBindWaylandDisplayWL( rendererDMX->eglDisplay, renderer->display );
                     if ( rc )
                     {
                        rendererDMX->haveWaylandEGL= true;
                     }
                     else
                     {
                        printf("wstRendererDMXCreate: eglBindWaylandDisplayWL failed: %x\n", eglGetError() );
                     }
                  }
                  else
                  {
                     rendererDMX->haveWaylandEGL= true;
                  }
               }
               else
               {
                  printf("wstRendererDMXCreate: wayland-egl support expected, and advertised, but methods are missing: no wayland-egl\n" );
               }
            }
         }
         printf("wstRendererDMXCreate: have wayland-egl: %d\n", rendererDMX->haveWaylandEGL );
      }
   }

   return rendererDMX;
}

static void wstRendererDMXDestroy( WstRendererDMX *renderer )
{
   if ( renderer )
   {
      if ( renderer->dispmanDisplay != DISPMANX_NO_HANDLE )
      {
         vc_dispmanx_display_close( renderer->dispmanDisplay );
         renderer->dispmanDisplay= DISPMANX_NO_HANDLE;
      }
      
      if ( renderer->renderer->display && renderer->haveWaylandEGL )
      {
         renderer->eglUnbindWaylandDisplayWL( renderer->eglDisplay, renderer->renderer->display );
         renderer->haveWaylandEGL= false;
      }
      
      bcm_host_deinit();

      free( renderer );
   }
}

static WstRenderSurface* wstRenderDMXCreateSurface( WstRendererDMX *renderer )
{
   WstRenderSurface *surface= 0;

   WST_UNUSED(renderer);   
   
   surface= (WstRenderSurface*)calloc( 1, sizeof(WstRenderSurface) );
   if ( surface )
   {
      surface->width= DEFAULT_SURFACE_WIDTH;
      surface->height= DEFAULT_SURFACE_HEIGHT;
      surface->x= 0;
      surface->y= 0;
      surface->visible= true;
      surface->opacity= 1.0;
      surface->zorder= 0.5;
      surface->flip= false;
      surface->dirty= false;
      
      for( int i= 0; i < NUM_BUFFERS; ++i )
      {
         surface->resource[i].owned= false;
         surface->resource[i].resource= DISPMANX_NO_HANDLE;
      }
      surface->front= 0;
      
      surface->element= DISPMANX_NO_HANDLE;
   }
   
   return surface;
}
static void wstRendererDMXDestroySurface( WstRendererDMX *renderer, WstRenderSurface *surface )
{
   WST_UNUSED(renderer);    
   
   if ( surface )
   {
      wstRendererDMXFlushSurface( renderer, surface );
      
      free( surface );
   }
}

static void  wstRendererDMXFlushSurface( WstRendererDMX *rendererDMX, WstRenderSurface *surface )
{
   if ( surface )
   {
      for( int i= 0; i < NUM_BUFFERS; ++i )
      {
         if ( surface->resource[i].resource != DISPMANX_NO_HANDLE )
         {
            if ( surface->resource[i].owned )
            {
               vc_dispmanx_resource_delete( surface->resource[i].resource );
               surface->resource[i].resource= DISPMANX_NO_HANDLE;
            }
         }
      }
      
      if ( surface->element )
      {
         DISPMANX_UPDATE_HANDLE_T dispmanUpdate= vc_dispmanx_update_start( 0 );
         if ( dispmanUpdate != DISPMANX_NO_HANDLE )
         {
            vc_dispmanx_element_remove( dispmanUpdate, surface->element );
            vc_dispmanx_update_submit_sync( dispmanUpdate );            
         }
         surface->element= DISPMANX_NO_HANDLE;
      }
   }
}

static void wstRendererDMXCommitShm( WstRendererDMX *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   struct wl_shm_buffer *shmBuffer;
   int width, height, stride;
   int pixelFormat, i;
   void *data;
   DISPMANX_RESOURCE_HANDLE_T dispmanResource= DISPMANX_NO_HANDLE;
   uint32_t nativeImage;

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
            pixelFormat= VC_IMAGE_ARGB8888;
            break;
         case WL_SHM_FORMAT_XRGB8888:
            pixelFormat= VC_IMAGE_XRGB8888;
            break;
         case WL_SHM_FORMAT_RGB565:
            pixelFormat= VC_IMAGE_RGB565;
            break;
         default:
            pixelFormat= VC_IMAGE_MIN;
            break;
      }
      if ( pixelFormat != VC_IMAGE_MIN )
      {         
         int back= (surface->front+1)%NUM_BUFFERS;

         if ( (surface->bufferWidth != width) || (surface->bufferHeight != height) )
         {
            surface->bufferWidth= width;
            surface->bufferHeight= height;
            surface->dirty= true;
         }                              

         if ( !surface->resource[back].owned ||              
              (surface->resource[back].width != width) ||
              (surface->resource[back].height != height) ||
              (surface->resource[back].pixelFormat != pixelFormat) )
         {
            if ( (surface->resource[back].owned) &&
                 (surface->resource[back].resource != DISPMANX_NO_HANDLE) )
            {
               vc_dispmanx_resource_delete( surface->resource[back].resource );
            }
            surface->resource[back].resource= DISPMANX_NO_HANDLE;
         }

         dispmanResource= surface->resource[back].resource;
         if ( dispmanResource == DISPMANX_NO_HANDLE )
         {
            dispmanResource= vc_dispmanx_resource_create( (VC_IMAGE_TYPE_T)pixelFormat,
                                                          width | (stride<<16),
                                                          height | (height<<16),
                                                          &nativeImage );
            surface->resource[back].owned= true;
            surface->resource[back].resource= dispmanResource;
            surface->resource[back].width= width;
            surface->resource[back].height= height;
            surface->resource[back].pixelFormat= pixelFormat;
            surface->resource[back].invertY= false;
         }

         if ( dispmanResource != DISPMANX_NO_HANDLE )
         {
            VC_RECT_T rect;
            
            rect.x= 0;
            rect.y= 0;
            rect.width= width;
            rect.height= height;

            wl_shm_buffer_begin_access(shmBuffer);
            data= wl_shm_buffer_get_data(shmBuffer);
            
            vc_dispmanx_resource_write_data( dispmanResource,
                                             (VC_IMAGE_TYPE_T)pixelFormat,
                                             stride,
                                             data,
                                             &rect );
            wl_shm_buffer_end_access(shmBuffer);
            
            surface->flip= true;
         }
      }   
   }
}

#ifdef ENABLE_SBPROTOCOL
static void wstRendererDMXCommitSB( WstRendererDMX *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   struct wl_sb_buffer *sbBuffer;
   void *deviceBuffer;
   int bufferWidth= 0, bufferHeight= 0;
   int sbFormat;
   EGLint format= EGL_NONE;
   
   sbBuffer= WstSBBufferGet( resource );
   if ( sbBuffer )
   {
      deviceBuffer= WstSBBufferGetBuffer( sbBuffer );
      if ( deviceBuffer )
      {
         DISPMANX_RESOURCE_HANDLE_T dispResource= (DISPMANX_RESOURCE_HANDLE_T)deviceBuffer;

         bufferWidth= WstSBBufferGetWidth( sbBuffer );
         bufferHeight= WstSBBufferGetHeight( sbBuffer );

         if ( (surface->bufferWidth != bufferWidth) || (surface->bufferHeight != bufferHeight) )
         {
            surface->bufferWidth= bufferWidth;
            surface->bufferHeight= bufferHeight;
            surface->dirty= true;
         }                              

         int back= (surface->front+1)%NUM_BUFFERS;
         
         if ( surface->resource[back].resource != DISPMANX_NO_HANDLE )
         {
            if ( surface->resource[back].owned )
            {
               vc_dispmanx_resource_delete( surface->resource[back].resource );
            }
            surface->resource[back].resource= DISPMANX_NO_HANDLE;
         }
         surface->resource[back].owned= false;
         surface->resource[back].resource= dispResource;
         surface->resource[back].invertY= true;
         surface->flip= true;
      }
   }
}
#endif

#if defined (WESTEROS_HAVE_WAYLAND_EGL)
static void wstRendererDMXCommitWaylandEGL( WstRendererDMX *rendererDMX, WstRenderSurface *surface, 
                                           struct wl_resource *resource, EGLint format )
{
   EGLint value;
   int bufferWidth= 0, bufferHeight= 0;

   if (EGL_TRUE == rendererDMX->eglQueryWaylandBufferWL( rendererDMX->eglDisplay,
                                                         resource,
                                                         EGL_WIDTH,
                                                         &value ) )
   {
      bufferWidth= value;
   }                                                        

   if (EGL_TRUE == rendererDMX->eglQueryWaylandBufferWL( rendererDMX->eglDisplay,
                                                         resource,
                                                         EGL_HEIGHT,
                                                         &value ) )
   {
      bufferHeight= value;
   }
   
   if ( (surface->bufferWidth != bufferWidth) || (surface->bufferHeight != bufferHeight) )
   {
      surface->bufferWidth= bufferWidth;
      surface->bufferHeight= bufferHeight;
      surface->dirty= true;
   }                              

   int back= (surface->front+1)%NUM_BUFFERS;
   
   if ( surface->resource[back].resource != DISPMANX_NO_HANDLE )
   {
      if ( surface->resource[back].owned )
      {
         vc_dispmanx_resource_delete( surface->resource[back].resource );
      }
      surface->resource[back].resource= DISPMANX_NO_HANDLE;
   }
   surface->resource[back].owned= false;
   surface->resource[back].resource= vc_dispmanx_get_handle_from_wl_buffer( resource );
   surface->resource[back].invertY= true;
   surface->flip= true;
}
#endif

static void wstRendererTerm( WstRenderer *renderer )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   if ( rendererDMX )
   {
      wstRendererDMXDestroy( rendererDMX );
      renderer->renderer= 0;
   }
}

static void wstRendererUpdateComplete( DISPMANX_UPDATE_HANDLE_T dispmanUpdate, void *userData )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)userData;
   
   rendererDMX->updateInProgress= false;
}

static void wstRendererUpdateSceneXform( WstRenderer *renderer, float *matrix, std::vector<WstRect> *rects )
{
   WstRenderSurface *surface;
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;   
   DISPMANX_UPDATE_HANDLE_T dispmanUpdate;
   WstRect rect;

   if ( !rendererDMX->updateInProgress )
   {
      dispmanUpdate= vc_dispmanx_update_start( 0 );
      if ( dispmanUpdate != DISPMANX_NO_HANDLE )
      {
         float scalex, scaley, transx, transy;
         
         rendererDMX->updateInProgress= true;

         if ( matrix )
         {
            scalex= renderer->matrix[0];
            scaley= renderer->matrix[5];
            transx= renderer->matrix[12];
            transy= renderer->matrix[13];
         }
         else
         {
            scalex= scaley= 1.0f;
            transx= transy= 0.0f;         
         }

         for ( std::vector<WstRenderSurface*>::iterator it= rendererDMX->surfaces.begin(); 
               it != rendererDMX->surfaces.end();
               ++it )
         {
            surface= (*it);
            
            if ( surface->flip )
            {
               surface->flip= false;
               surface->front= ((surface->front+1)%NUM_BUFFERS);
               if ( !surface->sizeOverride )
               {
                  surface->width= surface->bufferWidth;
                  surface->height= surface->bufferHeight;
               }
            }
 
            if ( 
                 (surface->resource[surface->front].resource != DISPMANX_NO_HANDLE)  &&
                 surface->visible
               )
            {
               int layer;
               VC_RECT_T destRect;
               VC_RECT_T srcRect;
               VC_DISPMANX_ALPHA_T alpha;
               DISPMANX_TRANSFORM_T transform;
               DISPMANX_ELEMENT_HANDLE_T dispmanElement;
               
               dispmanElement= surface->element;
               
               if ( surface->dirty )
               {
                  surface->dirty= false;
                  vc_dispmanx_element_remove( dispmanUpdate, dispmanElement );
                  dispmanElement= surface->element= DISPMANX_NO_HANDLE;
               }

               if ( dispmanElement == DISPMANX_NO_HANDLE )
               {
                  int sx, sy, sw, sh;
                  int dx, dy, dw, dh;

                  rect.x= (renderer->outputX+surface->x)*scalex+(transx-renderer->outputX);
                  rect.y= (renderer->outputY+surface->y)*scaley+(transy-renderer->outputY);
                  rect.width= surface->width*scalex;
                  rect.height= surface->height*scaley;

                  if ( rects )
                  {
                     rects->push_back( rect );
                  }
                  
                  sx= sy= 0;
                  dx= rect.x;
                  dw= rect.width;
                  if ( dx < 0 )
                  {
                     sx = -dx;
                     dw -= sx;
                     if ( dw < 0 ) dw= 0;
                     dx= 0;
                  }
                  dy= rect.y;
                  dh= rect.height;
                  if ( dy < 0 )
                  {
                     dh -= -dy;
                     if ( dh < 0 ) dh= 0;
                     dy= 0;
                  }
                  sw= dw;
                  if ( surface->resource[surface->front].invertY &&
                       (dy+dh > rendererDMX->outputHeight) )
                  {
                     dh -= ((dy+dh)-rendererDMX->outputHeight);
                     if ( dh < 0 ) dh= 0;
                     sy= rect.height-dh;
                  }
                  sh= dh;
                  
                  sx= (int)(sx/scalex);
                  sy= (int)(sy/scaley);
                  sw= (int)(sw/scalex);
                  sh= (int)(sh/scaley);
                  
                  if ( (sw > 0) && (sh > 0) )
                  {
                     // Dest rect uses 32.0 fixed point
                     destRect.x= dx;
                     destRect.y= dy;
                     destRect.width= dw;
                     destRect.height= dh;

                     // Src rect uses 16.16 fixed point
                     srcRect.x= (sx<<16);
                     srcRect.y= (sy<<16);
                     srcRect.width= (sw<<16);
                     srcRect.height= (sh<<16);
                     
                     alpha.flags= (DISPMANX_FLAGS_ALPHA_T)(DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_MIX);
                     if ( rects )
                     {
                        alpha.opacity= renderer->alpha*surface->opacity*255;
                     }
                     else
                     {
                        alpha.opacity= surface->opacity*255;
                     }
                     alpha.mask= 0;
                     
                     layer= surface->zorder*100;
                     
                     transform= surface->resource[surface->front].invertY
                                ? DISPMANX_FLIP_VERT
                                : DISPMANX_NO_ROTATE ;

                     dispmanElement= vc_dispmanx_element_add( dispmanUpdate,
                                                              rendererDMX->dispmanDisplay,
                                                              layer, //layer
                                                              &destRect,
                                                              0, //src
                                                              &srcRect,
                                                              DISPMANX_PROTECTION_NONE,
                                                              &alpha, //alpha
                                                              0, //clamp
                                                              transform
                                                            );
                     if ( dispmanElement == DISPMANX_NO_HANDLE )
                     {
                        printf("wstRendererUpdateScene: error from vc_dispmanx_element_add\n");
                     }
                      
                     surface->element= dispmanElement;
                  }
               }
                
               if ( dispmanElement != DISPMANX_NO_HANDLE )
               {
                  vc_dispmanx_element_change_source( dispmanUpdate, dispmanElement, surface->resource[surface->front].resource );
               }
            }
            else
            {
               vc_dispmanx_element_remove( dispmanUpdate, surface->element );
               surface->element= DISPMANX_NO_HANDLE;
            }
         }
         
         vc_dispmanx_update_submit( dispmanUpdate, wstRendererUpdateComplete, rendererDMX );
      }
   }
}

static void wstRendererUpdateScene( WstRenderer *renderer )
{
   if ( emitFPS )
   {
      static int frameCount= 0;
      static long long lastReportTime= -1LL;
      struct timeval tv;
      long long now;
      gettimeofday(&tv,0);
      now= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);
      ++frameCount;
      if ( lastReportTime == -1LL ) lastReportTime= now;
      if ( now-lastReportTime > 5000 )
      {
         double fps= ((double)frameCount*1000)/((double)(now-lastReportTime));
         printf("westeros-render-dispmanx: fps %f\n", fps);
         lastReportTime= now;
         frameCount= 0;
      }
   }
   wstRendererUpdateSceneXform( renderer, 0, 0 );
}

static WstRenderSurface* wstRendererSurfaceCreate( WstRenderer *renderer )
{
   WstRenderSurface *surface;
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;

   surface= wstRenderDMXCreateSurface(rendererDMX);

   std::vector<WstRenderSurface*>::iterator it= rendererDMX->surfaces.begin();
   while ( it != rendererDMX->surfaces.end() )
   {
      if ( surface->zorder < (*it)->zorder )
      {
         break;
      }
      ++it;
   }
   rendererDMX->surfaces.insert(it,surface);
   
   return surface; 
}

static void wstRendererSurfaceDestroy( WstRenderer *renderer, WstRenderSurface *surface )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;

   for ( std::vector<WstRenderSurface*>::iterator it= rendererDMX->surfaces.begin(); 
         it != rendererDMX->surfaces.end();
         ++it )
   {
      if ( (*it) == surface )
      {
         rendererDMX->surfaces.erase(it);
         break;   
      }
   }   
   
   wstRendererDMXDestroySurface( rendererDMX, surface );
}

static void wstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   EGLint value;

   if ( resource )
   {
      if ( wl_shm_buffer_get( resource ) )
      {
         wstRendererDMXCommitShm( rendererDMX, surface, resource );
      }
      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      else if ( rendererDMX->haveWaylandEGL && 
                (EGL_TRUE == rendererDMX->eglQueryWaylandBufferWL( rendererDMX->eglDisplay,
                                                                   resource,
                                                                   EGL_TEXTURE_FORMAT,
                                                                   &value ) ) )
      {
         wstRendererDMXCommitWaylandEGL( rendererDMX, surface, resource, value );
      }
      #endif
      #ifdef ENABLE_SBPROTOCOL
      else if ( WstSBBufferGet( resource ) )
      {
         wstRendererDMXCommitSB( rendererDMX, surface, resource );
      }
      #endif
      else
      {
         printf("wstRenderSurfaceCommit: unsupported buffer type\n");
      }
   }
   else
   {
      wstRendererDMXFlushSurface( rendererDMX, surface );
   }
}

static void wstRendererSurfaceSetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool visible )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   
   if ( surface )
   {
      surface->visible= visible;
   }
}

static bool wstRendererSurfaceGetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool *visible )
{
   bool isVisible= false;
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   
   if ( surface )
   {
      isVisible= surface->visible;
      
      *visible= isVisible;
   }
   
   return isVisible;
}

static void wstRendererSurfaceSetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   
   if ( surface )
   {
      if ( (width != surface->width) || (height != surface->height) )
      {
         surface->sizeOverride= true;
      }
      surface->x= x;
      surface->y= y;
      surface->width= width;
      surface->height= height;
      
      surface->dirty= true;
   }
}

void wstRendererSurfaceGetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   
   if ( surface )
   {
      *x= surface->x;
      *y= surface->y;
      *width= surface->width;
      *height= surface->height;
   }
}

static void wstRendererSurfaceSetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float opacity )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   
   if ( surface )
   {
      surface->opacity= opacity;
      surface->dirty= true;
   }
}

static float wstRendererSurfaceGetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float *opacity )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   float opacityLevel= 1.0;
   
   if ( surface )
   {
      opacityLevel= surface->opacity;
      
      *opacity= opacityLevel;
   }
   
   return opacityLevel;
}

static void wstRendererSurfaceSetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float z )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   
   if ( surface )
   {
      surface->zorder= z;

      // Remove from surface list
      for ( std::vector<WstRenderSurface*>::iterator it= rendererDMX->surfaces.begin(); 
            it != rendererDMX->surfaces.end();
            ++it )
      {
         if ( (*it) == surface )
         {
            rendererDMX->surfaces.erase(it);
            break;   
         }
      }   

      // Re-insert in surface list based on new z-order
      std::vector<WstRenderSurface*>::iterator it= rendererDMX->surfaces.begin();
      while ( it != rendererDMX->surfaces.end() )
      {
         if ( surface->zorder < (*it)->zorder )
         {
            break;
         }
         ++it;
      }
      rendererDMX->surfaces.insert(it,surface);

      surface->dirty= true;
   }
}

static float wstRendererSurfaceGetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float *z )
{
   WstRendererDMX *rendererDMX= (WstRendererDMX*)renderer->renderer;
   float zLevel= 1.0;
   
   if ( surface )
   {
      zLevel= surface->zorder;
      
      *z= zLevel;
   }
   
   return zLevel;
}

static void wstRendererDelegateUpdateScene( WstRenderer *renderer, std::vector<WstRect> &rects )
{
   wstRendererUpdateSceneXform( renderer, renderer->matrix, &rects );
   
   renderer->needHolePunch= true;
}


extern "C" {

int renderer_init( WstRenderer *renderer, int argc, char **argv )
{
   int rc= 0;
   WstRendererDMX *rendererDMX= 0;
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

   rendererDMX= wstRendererDMXCreate( renderer );
   if ( rendererDMX )
   {
      renderer->renderer= rendererDMX;
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

