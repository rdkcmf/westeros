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

#include "westeros-gl.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "nexus_config.h"
#include "nexus_platform.h"
#include "nexus_display.h"
#if NEXUS_PLATFORM_VERSION_MAJOR >= 16
#include "nexus_video_decoder.h"
#endif
#include "default_nexus.h"
#include "nxclient.h"

#include <vector>

#define DISPLAY_SAFE_BORDER_PERCENT (5)

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
   bool secureGraphics;
} WstGLCtx;

typedef struct _WstGLDisplayCtx
{
   NxClient_AllocSettings allocSettings;
   NxClient_AllocResults allocResults;
   NEXUS_SurfaceClientHandle surfaceClient;
   int displayWidth;
   int displayHeight;
} WstGLDisplayCtx;

typedef struct _WstGLSizeCBInfo
{
   WstGLCtx* ctx;
   void *userData;
   WstGLDisplaySizeCallback listener;
   int width;
   int height;
} WstGLSizeCBInfo;

static int ctxCount= 0;
static pthread_mutex_t g_mutex= PTHREAD_MUTEX_INITIALIZER;
static WstGLDisplayCtx *gDisplayCtx= 0;
static std::vector<WstGLSizeCBInfo> gSizeListeners;

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

static void gfxCheckPoint( void *data, int unused )
{
   BSTD_UNUSED(unused);
   BKNI_SetEvent((BKNI_EventHandle)data);
}

static void wstGLGetDisplaySize( void )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   NEXUS_SurfaceClientStatus scStatus;

   pthread_mutex_lock( &g_mutex );
   if ( gDisplayCtx && gDisplayCtx->surfaceClient )
   {
      rc= NEXUS_SurfaceClient_GetStatus( gDisplayCtx->surfaceClient, &scStatus );
      if ( rc == NEXUS_SUCCESS )
      {
         const char *env= 0;
         gDisplayCtx->displayWidth= scStatus.display.framebuffer.width;
         gDisplayCtx->displayHeight= scStatus.display.framebuffer.height;
         printf("WstGLGetDisplaySize: display %dx%d\n", gDisplayCtx->displayWidth, gDisplayCtx->displayHeight);
         env= getenv("WESTEROS_GL_GRAPHICS_SD_USE_720");
         if ( !env &&
              (gDisplayCtx->displayWidth == 720) &&
              (gDisplayCtx->displayHeight == 480) )
         {
            gDisplayCtx->displayWidth= 640;
            printf("WstGLGetDisplaySize: using SD display %dx%d\n", gDisplayCtx->displayWidth, gDisplayCtx->displayHeight);
         }
      }
   }
   pthread_mutex_unlock( &g_mutex );
}

static void wstGLNotifySizeListeners( void )
{
   int width=0, height=0;
   bool haveSize= false;
   std::vector<WstGLSizeCBInfo> listeners;

   pthread_mutex_lock( &g_mutex );
   if ( gDisplayCtx )
   {
      haveSize= true;
      width= gDisplayCtx->displayWidth;
      height= gDisplayCtx->displayHeight;

      for ( std::vector<WstGLSizeCBInfo>::iterator it= gSizeListeners.begin();
            it != gSizeListeners.end();
            ++it )
      {
         WstGLSizeCBInfo cbInfo= (*it);
         if ( (width != cbInfo.width) || (height != cbInfo.height) )
         {
            (*it).width= width;
            (*it).height= height;
            listeners.push_back( cbInfo );
         }
      }
   }
   pthread_mutex_unlock( &g_mutex );

   if ( haveSize )
   {
      for ( std::vector<WstGLSizeCBInfo>::iterator it= listeners.begin();
            it != listeners.end();
            ++it )
      {
         WstGLSizeCBInfo cbInfo= (*it);
         cbInfo.listener( cbInfo.userData, width, height );
      }
   }
   listeners.clear();
}

static void displayStatusChangedCallback( void *context, int param )
{
   wstGLGetDisplaySize();
   wstGLNotifySizeListeners();
}

WstGLCtx* WstGLInit()
{
   WstGLCtx *ctx= 0;
   NEXUS_Error rc= NEXUS_SUCCESS;
   NxClient_JoinSettings joinSettings;
   NEXUS_Graphics2DOpenSettings gfxOpenSettings;

   ctx= (WstGLCtx*)calloc( 1, sizeof(WstGLCtx) );
   if ( ctx )
   {
      pthread_mutex_lock( &g_mutex );
      if ( ctxCount == 0 )
      {
         NxClient_GetDefaultJoinSettings( &joinSettings );
         snprintf( joinSettings.name, NXCLIENT_MAX_NAME, "%s", "westeros-gl");
         rc= NxClient_Join( &joinSettings );
         printf("WstGLInit: NxClient_Join rc=%X as %s\n", rc, joinSettings.name );

         gDisplayCtx= (WstGLDisplayCtx*)calloc( 1, sizeof(WstGLDisplayCtx));
         if ( gDisplayCtx )
         {
            NxClient_GetDefaultAllocSettings( &gDisplayCtx->allocSettings );
            gDisplayCtx->allocSettings.surfaceClient= 1;
            rc= NxClient_Alloc( &gDisplayCtx->allocSettings, &gDisplayCtx->allocResults );
            if ( rc == NEXUS_SUCCESS )
            {
               gDisplayCtx->surfaceClient= NEXUS_SurfaceClient_Acquire(gDisplayCtx->allocResults.surfaceClient[0].id);
               if ( gDisplayCtx->surfaceClient )
               {
                  NEXUS_SurfaceClientSettings settings;

                  NEXUS_SurfaceClient_GetSettings( gDisplayCtx->surfaceClient, &settings );
                  settings.displayStatusChanged.callback= displayStatusChangedCallback;
                  settings.displayStatusChanged.context= gDisplayCtx;
                  settings.displayStatusChanged.param= 0;
                  rc= NEXUS_SurfaceClient_SetSettings( gDisplayCtx->surfaceClient, &settings );
                  if ( rc != NEXUS_SUCCESS )
                  {
                     printf("WstGLInit: NEXUS_SurfaceClient_SetSettings failed: rc=%X\n", rc);
                  }
               }
               else
               {
                  printf("WstGLInit: NEXUS_SurfaceClient_Acquire failed\n");
               }
            }
            else
            {
               printf("WstGLInit: NxClient_Alloc rc=%X", rc);
            }
         }
      }
      ++ctxCount;
      pthread_mutex_unlock( &g_mutex );

      NXPL_RegisterNexusDisplayPlatform( &ctx->nxplHandle, 0 );
      printf("WstGLInit: nxplHandle %x\n", ctx->nxplHandle );
      
      BKNI_CreateEvent( &ctx->gfxEvent );
      ctx->gfxEventCreated= true;
      
      ctx->secureGraphics= useSecureGraphics();
      printf("WstGLInit: secure graphics: %d\n", ctx->secureGraphics);
      if ( ctx->secureGraphics )
      {
         NxClient_DisplaySettings displaySettings;

         NxClient_GetDisplaySettings( &displaySettings );
         #if NEXUS_PLATFORM_VERSION_MAJOR >= 16
         displaySettings.secure= ctx->secureGraphics;
         #endif
         rc= NxClient_SetDisplaySettings( &displaySettings );
         if ( rc != NEXUS_SUCCESS )
         {
            printf("WstGLInit: NxClient_SetDisplaySettings failed: rc=%X\n", rc);
         }
      }

      NEXUS_Graphics2D_GetDefaultOpenSettings(&gfxOpenSettings);
      #if NEXUS_PLATFORM_VERSION_MAJOR >= 16
      gfxOpenSettings.secure= ctx->secureGraphics;
      #endif

      ctx->gfx= NEXUS_Graphics2D_Open(NEXUS_ANY_ID, &gfxOpenSettings);
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

      wstGLGetDisplaySize();
   }
   
   return ctx;
}

void WstGLTerm( WstGLCtx *ctx )
{
   if ( ctx )
   {
      pthread_mutex_lock( &g_mutex );
      for ( std::vector<WstGLSizeCBInfo>::iterator it= gSizeListeners.begin();
            it != gSizeListeners.end();
            ++it )
      {
         if ( (*it).ctx == ctx )
         {
            gSizeListeners.erase(it);
            break;
         }
      }
      pthread_mutex_unlock( &g_mutex );

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
      pthread_mutex_lock( &g_mutex );
      if ( ctxCount > 0 )
      {
         --ctxCount;
         if ( ctxCount == 0 )
         {
            if ( gDisplayCtx )
            {
               if ( gDisplayCtx->surfaceClient )
               {
                  NEXUS_SurfaceClient_Release( gDisplayCtx->surfaceClient );
                  gDisplayCtx->surfaceClient= 0;
               }
               NxClient_Free(&gDisplayCtx->allocResults);
               free( gDisplayCtx );
            }
            NxClient_Uninit();
         }
      }
      pthread_mutex_unlock( &g_mutex );
      free( ctx );
   }
}

#if defined(__cplusplus)
extern "C"
{
#endif
bool _WstGLGetDisplayInfo( WstGLCtx *ctx, WstGLDisplayInfo *displayInfo )
{
   return WstGLGetDisplayInfo( ctx, displayInfo );
}

bool _WstGLGetDisplaySafeArea( WstGLCtx *ctx, int *x, int *y, int *w, int *h )
{
   return WstGLGetDisplaySafeArea( ctx, x, y, w, h );
}

bool _WstGLAddDisplaySizeListener( WstGLCtx *ctx, void *userData, WstGLDisplaySizeCallback listener )
{
   return WstGLAddDisplaySizeListener( ctx, userData, listener );
}

bool _WstGLRemoveDisplaySizeListener( WstGLCtx *ctx, WstGLDisplaySizeCallback listener )
{
   return WstGLRemoveDisplaySizeListener( ctx, listener );
}
#if defined(__cplusplus)
}
#endif

bool WstGLGetDisplayInfo( WstGLCtx *ctx, WstGLDisplayInfo *displayInfo )
{
   bool result= false;

   if ( ctx && displayInfo )
   {
      wstGLGetDisplaySize();

      pthread_mutex_lock( &g_mutex );
      if ( gDisplayCtx )
      {
         displayInfo->width= gDisplayCtx->displayWidth;
         displayInfo->height= gDisplayCtx->displayHeight;

         // Use the SMPTE ST 2046-1 5% safe area border
         displayInfo->safeArea.x= displayInfo->width*DISPLAY_SAFE_BORDER_PERCENT/100;
         displayInfo->safeArea.y= displayInfo->height*DISPLAY_SAFE_BORDER_PERCENT/100;
         displayInfo->safeArea.w= displayInfo->width - 2*displayInfo->safeArea.x;
         displayInfo->safeArea.h= displayInfo->height - 2*displayInfo->safeArea.y;

         displayInfo->secureGraphics= ctx->secureGraphics;

         result= true;
      }
      pthread_mutex_unlock( &g_mutex );
   }

   return result;
}

bool WstGLGetDisplaySafeArea( WstGLCtx *ctx, int *x, int *y, int *w, int *h )
{
   bool result= false;
   WstGLDisplayInfo di;

   if ( ctx && x && y && w && h )
   {
      if ( WstGLGetDisplayInfo( ctx, &di ) )
      {
         *x= di.safeArea.x;
         *y= di.safeArea.y;
         *w= di.safeArea.w;
         *h= di.safeArea.h;

         result= true;
      }
   }

   return result;
}

bool WstGLAddDisplaySizeListener( WstGLCtx *ctx, void *userData, WstGLDisplaySizeCallback listener )
{
   bool result= false;
   bool found= false;

   if ( ctx )
   {
      pthread_mutex_lock( &g_mutex );

      for ( std::vector<WstGLSizeCBInfo>::iterator it= gSizeListeners.begin();
            it != gSizeListeners.end();
            ++it )
      {
         if ( (*it).listener == listener )
         {
            found= true;
            break;
         }
      }
      if ( !found )
      {
         WstGLSizeCBInfo newInfo;
         newInfo.ctx= ctx;
         newInfo.userData= userData;
         newInfo.listener= listener;
         newInfo.width= 0;
         newInfo.height= 0;
         gSizeListeners.push_back( newInfo );

         result= true;
      }

      pthread_mutex_unlock( &g_mutex );
   }

   if ( result )
   {
      wstGLNotifySizeListeners();
   }

   return result;
}

bool WstGLRemoveDisplaySizeListener( WstGLCtx *ctx, WstGLDisplaySizeCallback listener )
{
   bool result= false;
   bool found= false;

   if ( ctx )
   {
      pthread_mutex_lock( &g_mutex );

      for ( std::vector<WstGLSizeCBInfo>::iterator it= gSizeListeners.begin();
            it != gSizeListeners.end();
            ++it )
      {
         if ( (*it).listener == listener )
         {
            found= true;
            gSizeListeners.erase( it );
            break;
         }
      }
      if ( found )
      {
         result= true;
      }

      pthread_mutex_unlock( &g_mutex );
   }

   return result;
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
            /*
             * Create a new Nexus surface/native pixmap pair
             */   
            #if NEXUS_PLATFORM_VERSION_MAJOR >= 16
            if ( ctx->secureGraphics )
            {
               BEGL_PixmapInfoEXT pixmapInfo;

               NXPL_GetDefaultPixmapInfoEXT(&pixmapInfo);

               pixmapInfo.width= surfaceStatusIn.width;
               pixmapInfo.height= surfaceStatusIn.height;
               #ifdef BIG_ENDIAN_CPU
               pixmapInfo.format= BEGL_BufferFormat_eR8G8B8A8;
               #else
               pixmapInfo.format= BEGL_BufferFormat_eA8B8G8R8;
               #endif
               pixmapInfo.secure= true;
               if ( !NXPL_CreateCompatiblePixmapEXT(ctx->nxplHandle, &npm->pixmap, &npm->surface, &pixmapInfo) )
               {
                  printf("WstGLGetNativePixmap: NXPL_CreateCompatiblePixmapEXT failed\n");
                  free( npm );
                  npm= 0;
               }
            }
            else
            #endif
            {
               BEGL_PixmapInfoEXT pixmapInfo;

               NXPL_GetDefaultPixmapInfoEXT(&pixmapInfo);

               pixmapInfo.width= surfaceStatusIn.width;
               pixmapInfo.height= surfaceStatusIn.height;
               #ifdef BIG_ENDIAN_CPU
               pixmapInfo.format= BEGL_BufferFormat_eR8G8B8A8;
               #else
               pixmapInfo.format= BEGL_BufferFormat_eA8B8G8R8;
               #endif

               if ( !NXPL_CreateCompatiblePixmapEXT(ctx->nxplHandle, &npm->pixmap, &npm->surface, &pixmapInfo) )
               {
                  printf("WstGLGetNativePixmap: NXPL_CreateCompatiblePixmapEXT failed\n");
                  free( npm );
                  npm= 0;
               }
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

