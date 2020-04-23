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

#include <gdl.h>
#include <gdl_version.h>

/*
 * WstGLNativePixmap:
 */
typedef struct _WstGLNativePixmap
{
   void *pixmap;
   int width;
   int height;
} WstNativePixmap;

typedef struct _WstGLCtx 
{
  unsigned int gdlPlaneId;
} WstGLCtx;

static int ctxCount= 0;

WstGLCtx* WstGLInit()
{
   WstGLCtx *ctx= 0;

   ctx= (WstGLCtx*)calloc( 1, sizeof(WstGLCtx) );
   if ( ctx )
   {
      gdl_init(0);

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
      gdl_close();
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
      const char *env;
      unsigned int planeId= 0;
      
      env= getenv("WESTEROS_GL_PLANE");
      printf("WstGLCreateNativeWindow: WESTEROS_GL_PLANE set to %s\n", env );
      if ( env )
      {
         planeId= atoi(env);
      }
      if ( planeId == 0 )
      {
         printf("WstGLCreateNative: no valid plane assignement: defaulting to UPP_C\n");
         planeId= GDL_PLANE_ID_UPP_C;
      }
      
      {
         gdl_ret_t rc;
         gdl_display_info_t displayInfo;
         gdl_plane_id_t plane= (gdl_plane_id_t)planeId;
         gdl_uint32 pf= GDL_PF_COUNT;
         gdl_uint32 cs= GDL_COLOR_SPACE_COUNT;
         gdl_rectangle_t dstRect;
         gdl_rectangle_t srcRect;
         int modeWidth= -1, modeHeight = -1;
         bool useUpscale= false;

         rc= gdl_get_display_info( GDL_DISPLAY_ID_0, &displayInfo );
         if ( rc == GDL_SUCCESS )
         {
            modeWidth= displayInfo.tvmode.width;
            modeHeight= displayInfo.tvmode.height;
            printf("WstGLCreateNative: display mode (%dx%d)\n", modeWidth, modeHeight);
         }
         
         if ( (modeWidth > 1280) && (modeHeight > 720) )
         {
            useUpscale= true;
            
            srcRect.origin.x= 0;
            srcRect.origin.y= 0;
            srcRect.width= 1280;
            srcRect.height= 720;
         }         

         if ( modeWidth < width ) width= modeWidth;
         if ( modeHeight < height ) height= modeHeight;

         rc= gdl_plane_get_uint( plane, GDL_PLANE_PIXEL_FORMAT, &pf );
         if ( rc == GDL_SUCCESS )
         {
            rc= gdl_plane_get_uint( plane, GDL_PLANE_SRC_COLOR_SPACE, &cs );
            if ( rc == GDL_SUCCESS )
            {
               rc= gdl_plane_get_rect( plane, GDL_PLANE_DST_RECT, &dstRect );
               if ( rc == GDL_SUCCESS )
               {
                  if ( 
                       useUpscale ||
                       (pf != GDL_PF_ARGB_32) ||
                       (cs != GDL_COLOR_SPACE_RGB) ||
                       (dstRect.origin.x != 0) ||
                       (dstRect.origin.y != 0) ||
                       (dstRect.width != width) ||
                       (dstRect.height != height)
                     )
                  {
                     gdl_plane_reset(plane);
                     
                     rc= gdl_plane_config_begin(plane);
                     if ( rc == GDL_SUCCESS )
                     {
                        if ( useUpscale )
                        {            
                           dstRect.origin.x= 0;
                           dstRect.origin.y= 0;
                           dstRect.width= modeWidth;
                           dstRect.height= modeHeight;
                           
                           gdl_plane_set_rect( GDL_PLANE_DST_RECT, &dstRect );
                           gdl_plane_set_rect( GDL_PLANE_SRC_RECT, &srcRect );
                           gdl_plane_set_int( GDL_PLANE_SCALE, GDL_TRUE );
                           printf("WstGLCreateNative: set plane (%d,%d,%d,%d) with upscale\n",
                                   dstRect.origin.x, dstRect.origin.y, dstRect.width, dstRect.height);
                        }
                        else
                        {
                           if ( width < 16 ) width= 16;
                           if ( width > 1280 ) width= 1280;
                           if ( height < 2 ) height= 2;
                           if ( height > 720 ) height= 720;
                           if ( height & 1 ) height &= ~1;
                           dstRect.origin.x= 0;
                           dstRect.origin.y= 0;
                           dstRect.width= width;
                           dstRect.height= height;
                           gdl_plane_set_rect( GDL_PLANE_DST_RECT, &dstRect );
                           gdl_plane_set_int( GDL_PLANE_SCALE, GDL_FALSE );
                           printf("WstGLCreateNative: set plane (%d,%d,%d,%d) no upscale\n",
                                   dstRect.origin.x, dstRect.origin.y, dstRect.width, dstRect.height);
                        }
                        
                        pf= GDL_PF_ARGB_32;
                        gdl_plane_set_uint( GDL_PLANE_PIXEL_FORMAT, pf );
                        
                        cs= GDL_COLOR_SPACE_RGB;
                        gdl_plane_set_uint( GDL_PLANE_SRC_COLOR_SPACE, cs );
                        
                        gdl_plane_config_end(GDL_FALSE);
                     }         
                  }
               }
            }
         }
      }
      
      ctx->gdlPlaneId= planeId;
      
      nativeWindow= (void*)ctx->gdlPlaneId;
   }
   
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
      // Nothing to do
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

