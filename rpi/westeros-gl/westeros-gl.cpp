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

#include "westeros-gl.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <bcm_host.h>

/*
 * WstGLNativePixmap:
 */
typedef struct _WstGLNativePixmap
{
   void *pixmap;
   int width;
   int height;
} WstNativePixmap;

typedef struct _WstGLNativeWindow
{
   public:
     operator EGLNativeWindowType() { return (EGLNativeWindowType)nw; }
     void swap() {} 
   private:
     EGL_DISPMANX_WINDOW_T *nw;
     DISPMANX_RESOURCE_HANDLE_T *front;
     DISPMANX_RESOURCE_HANDLE_T *back;
} WstGLNativeWindow;

typedef struct _WstGLCtx 
{
  int displayId;
  int displayWidth;
  int displayHeight;
  DISPMANX_DISPLAY_HANDLE_T dispmanDisplay;
  DISPMANX_UPDATE_HANDLE_T dispmanUpdate;
} WstGLCtx;

static int ctxCount= 0;

WstGLCtx* WstGLInit()
{
   WstGLCtx *ctx= 0;

   ctx= (WstGLCtx*)calloc( 1, sizeof(WstGLCtx) );
   if ( ctx )
   {
      if ( ctxCount == 0 )
      {         
         bcm_host_init();
      }

      ++ctxCount;

   }
   
   return ctx;
}

void WstGLTerm( WstGLCtx *ctx )
{
   if ( ctx )
   {
      if ( ctxCount > 0 )
      {
         --ctxCount;
      }
      if ( ctxCount == 0 )
      {
         if ( ctx->dispmanDisplay != DISPMANX_NO_HANDLE )
         {
            vc_dispmanx_display_close( ctx->dispmanDisplay );
            ctx->dispmanDisplay= DISPMANX_NO_HANDLE;
         }
         bcm_host_deinit();
      }
      free( ctx );
   }
}

/*
 * WstGLCreateNativeWindow
 * Create a native window suitable for use as an EGLNativeWindow
 */
void* WstGLCreateNativeWindow( WstGLCtx *ctx, int x, int y, int width, int height )
{
   void *nativeWindow= 0;

   if ( ctx )
   {
      if ( !ctx->dispmanDisplay )
      {
         int displayId;
         uint32_t width, height;
         DISPMANX_DISPLAY_HANDLE_T dispmanDisplay;
         DISPMANX_UPDATE_HANDLE_T dispmanUpdate;

         displayId= DISPMANX_ID_MAIN_LCD;         
         int32_t result= graphics_get_display_size( displayId,
                                                    &width,
                                                    &height );
         if ( result < 0 )
         {
            printf("WstGLCreateNativeWindow: graphics_get_display_size failed\n");
            goto exit;
         }
         
         printf("WstGLCreateNativeWindow: display %d size %dx%d\n", displayId, width, height );
         
         dispmanDisplay= vc_dispmanx_display_open( displayId );
         if ( dispmanDisplay == DISPMANX_NO_HANDLE )
         {
            printf("WstGLCreateNativeWindow: vc_dispmanx_display_open failed for display %d\n", displayId );
            goto exit;         
         }
         printf("WstGLCreateNativeWindow: dispmanDisplay %p\n", dispmanDisplay );
         
         dispmanUpdate= vc_dispmanx_update_start( 0 );
         if ( dispmanUpdate == DISPMANX_NO_HANDLE )
         {
            printf("WstGLCreateNativeWindow: vc_dispmanx_update_start failed for display %d\n", displayId );
            vc_dispmanx_display_close( dispmanDisplay );
            goto exit;         
         }      
         printf("WstGLCreateNativeWindow: dispmanUpdate %p\n", dispmanUpdate );

         ctx->displayId= displayId;
         ctx->displayWidth= width;
         ctx->displayHeight= height;
         ctx->dispmanDisplay= dispmanDisplay;
         ctx->dispmanUpdate= dispmanUpdate;
      }

      if ( ctx->dispmanDisplay )
      {
         VC_RECT_T destRect;
         VC_RECT_T srcRect;
         DISPMANX_ELEMENT_HANDLE_T dispmanElement;
         EGL_DISPMANX_WINDOW_T *nw;
      
         nw= (EGL_DISPMANX_WINDOW_T *)calloc( 1, sizeof(EGL_DISPMANX_WINDOW_T) );
         if ( nw )
         {
            // Dest rect uses 32.0 fixed point
            destRect.x= x;
            destRect.y= y;
            destRect.width= width;
            destRect.height= height;

            // Src rect uses 16.16 fixed point
            srcRect.x= 0;
            srcRect.y= 0;
            srcRect.width= (width<<16);
            srcRect.height= (height<<16);
            
            dispmanElement= vc_dispmanx_element_add( ctx->dispmanUpdate,
                                                     ctx->dispmanDisplay,
                                                     0, //layer
                                                     &destRect,
                                                     0, //src
                                                     &srcRect,
                                                     DISPMANX_PROTECTION_NONE,
                                                     0, //alpha
                                                     0, //clamp
                                                     DISPMANX_NO_ROTATE  //transform
                                                   );
             if ( dispmanElement !=  DISPMANX_NO_HANDLE )
             {
                printf("WstGLCreateNativeWindow: dispmanElement %p\n", dispmanElement );
                
                nw->element= dispmanElement;
                nw->width= width;
                nw->height= height;
                
                nativeWindow= (void*)nw;
         
                vc_dispmanx_update_submit_sync( ctx->dispmanUpdate );
             }                                                                            
         }
      }
   }

exit:
   
   return nativeWindow;   
}

/*
 * WstGLDestroyNativeWindow
 * Destroy a native window created by WstGLCreateNativeWindow
 */
void WstGLDestroyNativeWindow( WstGLCtx *ctx, void *nativeWindow )
{
   if ( ctx )
   {
      EGL_DISPMANX_WINDOW_T *nw;
      DISPMANX_ELEMENT_HANDLE_T dispmanElement;

      nw= (EGL_DISPMANX_WINDOW_T*)nativeWindow;
      if ( nw )
      {
         vc_dispmanx_element_remove( ctx->dispmanUpdate,
                                     nw->element );
                                     
         free( nw );
      }
   }
}

/*
 * WstGLGetNativePixmap
 * Given a native buffer, obtain a native pixmap
 *
 * nativeBuffer : pointer to a Nexus surface
 * nativePixmap : pointer to a pointer to a WstGLNativePixmap
 *
 * If nativePixmap points to a null pointer, a new WstGLNativePixmap will be
 * allocated.  If nativePixmap points to non-null pointer, the WstGLNativePixmap
 * will be re-used.
 *
 * The input Nexus surface contains a frame from a compositor client process.  In order
 * for its contents to be useable to the compositor in OpenGL rendering, it must be
 * copied to a Nexus surface/native pixmap pair created by the compositor process.
 */
bool WstGLGetNativePixmap( WstGLCtx *ctx, void *nativeBuffer, void **nativePixmap )
{
   bool result= false;
    
   if ( ctx )
   {
      // Not yet required
   }
   
   return result;
}

/*
 * WstGLGetNativePixmapDimensions
 * Get the dimensions of the WstGLNativePixmap
 */
void WstGLGetNativePixmapDimensions( WstGLCtx *ctx, void *nativePixmap, int *width, int *height )
{
   if ( ctx )
   {
      // Not yet required
   }
}

/*
 * WstGLReleaseNativePixmap
 * Release a WstGLNativePixmap obtained via WstGLGetNativePixmap
 */
void WstGLReleaseNativePixmap( WstGLCtx *ctx, void *nativePixmap )
{
   if ( ctx )
   {
      // Not yet required
   }
}

/*
 * WstGLGetEGLNativePixmap
 * Get the native pixmap usable as a EGL_NATIVE_PIXMAP_KHR for creating a texture
 * from the provided WstGLNativePixmap instance
 */
void* WstGLGetEGLNativePixmap( WstGLCtx *ctx, void *nativePixmap )
{
   void* eglPixmap= 0;
   
   if ( nativePixmap )
   {
      // Not yet required
   }
   
   return eglPixmap;
}

