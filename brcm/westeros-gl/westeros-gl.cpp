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

#include "nexus_config.h"
#include "nexus_platform.h"
#include "nexus_display.h"
#include "default_nexus.h"
#include "nxclient.h"

/*
 * WstGLNativePixmap:
 * Contains a Nexus surface / NXPL native pixmap pair
 */
typedef struct _WstGLNativePixmap
{
   void *pixmap;
   NEXUS_SurfaceHandle surface;
   int width;
   int height;
} WstNativePixmap;

typedef struct _WstGLCtx 
{
   NXPL_PlatformHandle nxplHandle;
   NEXUS_Graphics2DHandle gfx;
   bool gfxEventCreated;
   BKNI_EventHandle gfxEvent;
} WstGLCtx;

static int ctxCount= 0;

static void gfxCheckPoint( void *data, int unused )
{
   BSTD_UNUSED(unused);
   BKNI_SetEvent((BKNI_EventHandle)data);
}

WstGLCtx* WstGLInit()
{
   WstGLCtx *ctx= 0;
   NEXUS_Error rc= NEXUS_SUCCESS;
   NxClient_JoinSettings joinSettings;

   ctx= (WstGLCtx*)calloc( 1, sizeof(WstGLCtx) );
   if ( ctx )
   {
      if ( ctxCount == 0 )
      {
         NxClient_GetDefaultJoinSettings( &joinSettings );
         snprintf( joinSettings.name, NXCLIENT_MAX_NAME, "%s", "westeros-gl");
         rc= NxClient_Join( &joinSettings );
         printf("WstGLInit: NxClient_Join rc=%X as %s\n", rc, joinSettings.name );
      }
      ++ctxCount;

      NXPL_RegisterNexusDisplayPlatform( &ctx->nxplHandle, 0 );
      printf("WstGLInit: nxplHandle %x\n", ctx->nxplHandle );
      
      BKNI_CreateEvent( &ctx->gfxEvent );
      ctx->gfxEventCreated= true;
      
      ctx->gfx= NEXUS_Graphics2D_Open(NEXUS_ANY_ID, NULL);
      if ( ctx->gfx )
      {
         NEXUS_Graphics2DSettings gfxSettings;
         
         NEXUS_Graphics2D_GetSettings( ctx->gfx, &gfxSettings );
         gfxSettings.checkpointCallback.callback= gfxCheckPoint;
         gfxSettings.checkpointCallback.context= ctx->gfxEvent;
         NEXUS_Graphics2D_SetSettings( ctx->gfx, &gfxSettings );
      }

      if ( !ctx->nxplHandle || !ctx->gfx || !ctx->gfxEventCreated || (NEXUS_SUCCESS != rc) )
      {
         WstGLTerm( ctx );
         ctx= 0;
      }
   }
   
   return ctx;
}

void WstGLTerm( WstGLCtx *ctx )
{
   if ( ctx )
   {
      if ( ctx->gfxEventCreated )
      {
         ctx->gfxEventCreated= false;
         BKNI_DestroyEvent( ctx->gfxEvent );
         ctx->gfxEvent= NULL;
      }
      if ( ctx->gfx )
      {
         NEXUS_Graphics2D_Close( ctx->gfx );
         ctx->gfx= 0;
      }
      if ( ctx->nxplHandle )
      {
         NXPL_UnregisterNexusDisplayPlatform( ctx->nxplHandle );
         ctx->nxplHandle= 0;
      }
      if ( ctxCount > 0 )
      {
         --ctxCount;
         if ( ctxCount == 0 )
         {
            NxClient_Uninit();
         }
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
      NXPL_NativeWindowInfo windowInfo;

      memset( &windowInfo, 0, sizeof(windowInfo) );
      windowInfo.x= 0;
      windowInfo.y= 0;
      windowInfo.width= width;
      windowInfo.height= height;
      windowInfo.stretch= false;
      windowInfo.clientID= 0;
      windowInfo.zOrder= 10000000;
   
      nativeWindow= (void*)NXPL_CreateNativeWindow( &windowInfo );
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
      NXPL_DestroyNativeWindow( nativeWindow );
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
      NEXUS_Error rc;
      NEXUS_SurfaceStatus surfaceStatusIn;
      NEXUS_SurfaceStatus surfaceStatusNPM;
      NEXUS_SurfaceHandle surfaceIn= (NEXUS_SurfaceHandle)nativeBuffer;
      WstNativePixmap *npm;
      
      NEXUS_Surface_GetStatus( surfaceIn, &surfaceStatusIn );
 
      npm= (WstNativePixmap*)*nativePixmap;
      
      if ( npm )
      {
         /*
          * We have an existing Nexus surface/native pixmap pair:
          * it can be re-used as long as its dimensions match those
          * of the new input surface
          */
         NEXUS_Surface_GetStatus( npm->surface, &surfaceStatusNPM );
         if ( (surfaceStatusIn.width != surfaceStatusNPM.width) ||
              (surfaceStatusIn.height != surfaceStatusNPM.height) )
         {
            NXPL_DestroyCompatiblePixmap(ctx->nxplHandle, npm->pixmap );
            npm->pixmap= 0;
            npm->surface= 0;
         }
      }
      else
      {
         npm= (WstNativePixmap*)calloc( 1, sizeof(WstNativePixmap) );
      }

      if ( npm )
      {
         npm->width= surfaceStatusIn.width;
         npm->height= surfaceStatusIn.height;
         
         if ( !npm->pixmap )
         {
            BEGL_PixmapInfo pixmapInfo;

            /*
             * Create a new Nexus surface/native pixmap pair
             */   
            pixmapInfo.width= surfaceStatusIn.width;
            pixmapInfo.height= surfaceStatusIn.height;
            #ifdef BIG_ENDIAN_CPU
            pixmapInfo.format= BEGL_BufferFormat_eR8G8B8A8;
            #else
            pixmapInfo.format= BEGL_BufferFormat_eA8B8G8R8;
            #endif
            if ( !NXPL_CreateCompatiblePixmap(ctx->nxplHandle, &npm->pixmap, &npm->surface, &pixmapInfo) )
            {
               printf("WstGLGetNativePixmap: NXPL_CreateCompatiblePixmap failed\n");
               free( npm );
               npm= 0;
            }
         }

         if ( npm )
         {
            NEXUS_Graphics2DBlitSettings blitSettings;

            /*
             * Copy the contents of the input Nexus surface to the Nexus
             * surface in our WstGLNativePixmap.  The contents then become
             * accessible to EGL/OpenGL via the paired NXPL native pixmap.
             */
            NEXUS_Graphics2D_GetDefaultBlitSettings( &blitSettings );
            blitSettings.source.surface= surfaceIn;
            blitSettings.output.surface= npm->surface;
            rc= NEXUS_Graphics2D_Blit(ctx->gfx, &blitSettings);
            if ( rc == NEXUS_SUCCESS )
            {
               rc= NEXUS_Graphics2D_Checkpoint( ctx->gfx, NULL );
               if ( rc == NEXUS_GRAPHICS2D_QUEUED )
               {
                  BKNI_WaitForEvent(ctx->gfxEvent, BKNI_INFINITE);
               }
            }
            
            result= true;
         }
      }

      *nativePixmap= npm;
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
      WstNativePixmap *npm= (WstNativePixmap*)nativePixmap;
      *width= npm->width;
      *height= npm->height;
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
      WstNativePixmap *npm= (WstNativePixmap*)nativePixmap;
      if ( npm->pixmap )
      {
         NXPL_DestroyCompatiblePixmap(ctx->nxplHandle, npm->pixmap );
         npm->pixmap= 0;
         npm->surface= 0;
      }
      free( npm );
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
      WstNativePixmap *npm= (WstNativePixmap*)nativePixmap;
      eglPixmap= npm->pixmap;
   }
   
   return eglPixmap;
}

