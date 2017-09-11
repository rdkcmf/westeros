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
#include <dlfcn.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/time.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <GLES/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include "westeros-gl.h"

#define VERBOSE_DEBUG
//#define EMIT_FRAMERATE
// Defaulting Page flip usage as pageflipping error is addressed
#define USE_PAGEFLIP
#define DEFAULT_CARD "/dev/dri/card0"
#define DEFAULT_MODE_WIDTH (1280)
#define DEFAULT_MODE_HEIGHT (720)

typedef EGLDisplay (*PREALEGLGETDISPLAY)(EGLNativeDisplayType);
typedef EGLSurface (*PREALEGLCREATEWINDOWSURFACE)(EGLDisplay, 
                                                  EGLConfig,
                                                  EGLNativeWindowType,
                                                  const EGLint *attrib_list);
typedef EGLBoolean (*PREALEGLSWAPBUFFERS)(EGLDisplay,
                                          EGLSurface surface);

static WstGLCtx* g_wstCtx= NULL;
static PFNEGLGETPLATFORMDISPLAYEXTPROC gRealEGLGetPlatformDisplay= 0;
static PREALEGLGETDISPLAY gRealEGLGetDisplay= 0;
static PREALEGLCREATEWINDOWSURFACE gRealEGLCreateWindowSurface= 0;
static PREALEGLSWAPBUFFERS gRealEGLSwapBuffers= 0;

static void swapDRMBuffers(void* nativeBuffer);
static bool setupDRM( WstGLCtx *ctx );
static void destroyGbmCtx( struct gbm_bo *bo, void *userData );

typedef struct _GbmCtx
{
   int front;
   uint32_t handle;	//Everytime different bo is returned
   int fb_id;		//so no need to handle it
} GbmCtx;

typedef struct _NativeWindowItem
{
   struct _NativeWindowItem *next;
   void *nativeWindow;
   EGLSurface surface;
} NativeWindowItem;

typedef struct _WstGLCtx
{
   int refCnt;
   int drmFd;
   bool drmSetup;
   bool modeSet;
   drmModeRes *drmRes;
   drmModeConnector *drmConnector;
   drmModeEncoder *drmEncoder;
   drmModeModeInfo *modeInfo;
   struct gbm_device* gbm;
   EGLDisplay dpy;
   EGLImageKHR image;
   EGLConfig config;
   NativeWindowItem *nwFirst;
   NativeWindowItem *nwLast;
} WstGLCtx;

#ifdef EMIT_FRAMERATE
static long long currentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}
#endif

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType displayId)
{
   EGLDisplay eglDisplay= EGL_NO_DISPLAY;

   printf("westeros-gl: eglGetDisplay: enter: displayId %x\n", displayId);
   
   if ( !g_wstCtx )
   {
      g_wstCtx= WstGLInit();
   }

   if ( !gRealEGLGetDisplay )
   {
      printf("westeros-gl: eglGetDisplay: failed linkage to underlying EGL impl\n" );
      goto exit;
   }   

   if ( displayId == EGL_DEFAULT_DISPLAY )
   {
      if ( !g_wstCtx->drmSetup )
      {
         g_wstCtx->drmSetup= setupDRM( g_wstCtx );
      }
      if ( g_wstCtx->drmSetup )
      {
         g_wstCtx->dpy = gRealEGLGetDisplay( (NativeDisplayType)g_wstCtx->gbm );
      }
   }
   else
   {
      g_wstCtx->dpy = gRealEGLGetDisplay(displayId);
   }
   
   if ( g_wstCtx->dpy )
   {
      eglDisplay= g_wstCtx->dpy;
   }

exit:

   return eglDisplay;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface( EGLDisplay dpy, EGLConfig config,
                                                      EGLNativeWindowType win,
                                                      const EGLint *attrib_list )
{
   EGLSurface eglSurface= EGL_NO_SURFACE;
   EGLNativeWindowType nativeWindow;
   NativeWindowItem *nwIter;
    
   if ( !gRealEGLCreateWindowSurface )
   {
      printf("westeros-gl: eglCreateWindowSurface: failed linkage to underlying EGL impl\n" );
      goto exit;
   }

   eglSurface= gRealEGLCreateWindowSurface( dpy, config, win, attrib_list );
   if ( eglSurface != EGL_NO_SURFACE )
   {
      if ( g_wstCtx )
      {
         nwIter= g_wstCtx->nwFirst;
         while( nwIter )
         {
            if ( nwIter->nativeWindow == win )
            {
               nwIter->surface= eglSurface;
               break;
            }
         }
      }    
   }

exit:
   
   return eglSurface;
}

EGLAPI EGLBoolean eglSwapBuffers( EGLDisplay dpy, EGLSurface surface )
{
   EGLBoolean result= EGL_FALSE;
   NativeWindowItem *nwIter;

   if ( !gRealEGLSwapBuffers )
   {
      printf("westeros-gl: eglSwapBuffers: failed linkage to underlying EGL impl\n" );
      goto exit;
   }
   
   if ( gRealEGLSwapBuffers )
   {
      result= gRealEGLSwapBuffers( dpy, surface );
      if ( g_wstCtx )
      {
         nwIter= g_wstCtx->nwFirst;
         while( nwIter )
         {
            if ( nwIter->surface == surface )
            {
               swapDRMBuffers(nwIter->nativeWindow);
               break;
            }
        }
      }    
   }
   
exit:

   return result;
}

WstGLCtx* WstGLInit() 
{
   /*
    *  Establish the overloading of a subset of EGL methods
    */
   if ( !gRealEGLGetDisplay )
   {
      gRealEGLGetDisplay= (PREALEGLGETDISPLAY)dlsym( RTLD_NEXT, "eglGetDisplay" );
      printf("westeros-gl: wstGLInit: realEGLGetDisplay=%p\n", (void*)gRealEGLGetDisplay );
      if ( !gRealEGLGetDisplay )
      {
         printf("westeros-gl: wstGLInit: unable to resolve eglGetDisplay\n");
         goto exit;
      }
   }

   if ( !gRealEGLCreateWindowSurface )
   {
      gRealEGLCreateWindowSurface= (PREALEGLCREATEWINDOWSURFACE)dlsym( RTLD_NEXT, "eglCreateWindowSurface" );
      printf("westeros-gl: wstGLInit: realEGLCreateWindowSurface=%p\n", (void*)gRealEGLCreateWindowSurface );
      if ( !gRealEGLCreateWindowSurface )
      {
         printf("westeros-gl: wstGLInit: unable to resolve eglCreateWindowSurface\n");
         goto exit;
      }
   }

   if ( !gRealEGLSwapBuffers )
   {
      gRealEGLSwapBuffers= (PREALEGLSWAPBUFFERS)dlsym( RTLD_NEXT, "eglSwapBuffers" );
      printf("westeros-gl: wstGLInit: realEGLSwapBuffers=%p\n", (void*)gRealEGLSwapBuffers );
      if ( !gRealEGLSwapBuffers )
      {
         printf("westeros-gl: eglSwapBuffers: unable to resolve eglSwapBuffers\n");
         goto exit;
      }
   }

   if( g_wstCtx != NULL )
   {
      ++g_wstCtx->refCnt;
      return g_wstCtx;
   }

   g_wstCtx= (WstGLCtx*)calloc(1, sizeof(WstGLCtx));
   if ( g_wstCtx )
   {
      g_wstCtx->refCnt= 1;
      g_wstCtx->drmFd= -1;
   }

exit:

   return g_wstCtx;
}

void WstGLTerm( WstGLCtx *ctx ) 
{
   if ( ctx )
   {
      if ( ctx != g_wstCtx )
      {
         printf("westeros-gl: WstGLTerm: bad ctx %p, should be %p\n", ctx, g_wstCtx );
         return;
      }
      
      --ctx->refCnt;
      if ( ctx->refCnt <= 0 )
      {
         if ( ctx->gbm )
         {
            gbm_device_destroy(ctx->gbm);
            ctx->gbm= 0;
         }
         
         if ( ctx->drmEncoder )
         {
            drmModeFreeEncoder(ctx->drmEncoder);
            ctx->drmEncoder= 0;
         }
         
         if ( ctx->drmConnector )
         {
            drmModeFreeConnector(ctx->drmConnector);
            ctx->drmConnector= 0;
         }
         
         if ( ctx->drmFd >= 0)
         {
            close( ctx->drmFd );
            ctx->drmFd= -1;
         }
         
         free( ctx );
         
         g_wstCtx= 0;
      }      
   }
}

void* WstGLCreateNativeWindow( WstGLCtx *ctx, int x, int y, int width, int height ) 
{
   void *nativeWindow= 0;
   NativeWindowItem *nwItem= 0;
    
   if ( !ctx->drmSetup )
   {
     ctx->drmSetup= setupDRM( ctx );
   }
   if ( ctx->drmSetup )
   {
      nwItem= (NativeWindowItem*)calloc( 1, sizeof(NativeWindowItem) );
      if ( nwItem )
      {
         nativeWindow= gbm_surface_create(ctx->gbm, 
                                          width, 
                                          height,
                                          GBM_FORMAT_XRGB8888,
                                          GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
         if ( nativeWindow )
         {
            nwItem->nativeWindow= nativeWindow;
            if ( ctx->nwFirst )
            {
               nwItem->next= ctx->nwFirst;
               ctx->nwFirst= nwItem;
            }
            else
            {
               ctx->nwFirst= ctx->nwLast= nwItem;
            }
         }
      }
      else
      {
         printf("westeros-gl: WstGLCreateNativeWindow: unable to allocate native window list item\n");
      }
   }
                 
    return nativeWindow;
}

void WstGLDestroyNativeWindow( WstGLCtx *ctx, void *nativeWindow ) 
{
   NativeWindowItem *nwIter, *nwPrev;
    
   if ( ctx && nativeWindow )
   {
      struct gbm_surface *gs = nativeWindow;
       
      gbm_surface_destroy( gs );
       
      nwPrev= 0;
      nwIter= ctx->nwFirst;
      while ( nwIter )
      {
         if ( nwIter->nativeWindow == nativeWindow )
         {
            if ( nwPrev )
            {
               nwPrev->next= nwIter->next;
            }
            else
            {
               ctx->nwFirst= nwIter->next;
            }
            if ( !nwIter->next )
            {
               ctx->nwLast= nwPrev;
            }
            free( nwIter );
            break;
         }
         else
         {
            nwPrev= nwIter;
         }
         nwIter= nwIter->next;
      }
   }
}

bool WstGLGetNativePixmap( WstGLCtx *ctx, void *nativeBuffer, void **nativePixmap )
{
   bool result= false;
   struct gbm_bo *bo;
   
   bo= gbm_bo_create( ctx->gbm, 
                      ctx->modeInfo->hdisplay, 
                      ctx->modeInfo->vdisplay,
                      GBM_FORMAT_XRGB8888, 
                      GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT );

   if( bo )
   {
      *nativePixmap= bo;
      
      result= true;
   }
   
   return result;
}

void WstGLGetNativePixmapDimensions( WstGLCtx *ctx, void *nativePixmap, int *width, int *height )
{
   struct gbm_bo *bo;
    
   bo= (struct gbm_bo*)nativePixmap;
   if ( bo )
   {
      if ( width )
      {
         *width= gbm_bo_get_width(bo);
      }
      if ( height )
      {
         *height = gbm_bo_get_height(bo);
      }
   }
}

void WstGLReleaseNativePixmap( WstGLCtx *ctx, void *nativePixmap )
{
   struct gbm_bo *bo;
   
   bo= (struct gbm_bo*)nativePixmap;
   if ( bo )
   {
      gbm_bo_destroy(bo);
   }
}

void* WstGLGetEGLNativePixmap( WstGLCtx *ctx, void *nativePixmap ) 
{
   return nativePixmap;
}

#ifdef USE_PAGEFLIP
// Variable to track pending pageflip
static int pageflip_pending = 0;

// Page flip event handler
static void page_flip_event_handler(int fd, unsigned int frame,
				    unsigned int sec, unsigned int usec,
				    void *data)
{
    if (pageflip_pending){
        pageflip_pending--;
    }
    else
	printf("pflip sync failure\n");
}
#endif

static void swapDRMBuffers(void* nativeBuffer) 
{
   struct gbm_surface* surface;
   struct gbm_bo *bo;
   static struct gbm_bo *previousBO = NULL;
   uint32_t handle, stride;
   GbmCtx *gbmCtx= 0;
   int fb_id;
   int ret;
   #ifdef EMIT_FRAMERATE
   static long long startTime= -1LL;
   static int frameCount= 0;
   long long now;
   #endif
   #ifdef USE_PAGEFLIP
   fd_set fds;
   drmEventContext ev;
   #endif
   
   if ( g_wstCtx )
   {
      if ( g_wstCtx->drmSetup )
      {
         surface= (struct gbm_surface*)nativeBuffer;
         if ( surface )
         {
            bo= gbm_surface_lock_front_buffer(surface);
            
            handle= gbm_bo_get_handle(bo).u32;
            stride = gbm_bo_get_stride(bo);

            gbmCtx= (GbmCtx*)gbm_bo_get_user_data( bo );
            if ( !gbmCtx )
            {
               gbmCtx= (GbmCtx*)calloc( 1, sizeof(GbmCtx) );
               if ( gbmCtx )
               {
                  gbm_bo_set_user_data(bo, gbmCtx, destroyGbmCtx );
               }
               else
               {
                  printf("westeros-gl: WstGLSwapBuffers: unable to allocate gbm ctx for surface %p\n", surface );
               }
            }

            if ( gbmCtx )
            {
               /* buffering logic change to fix: very minor lines observed in video occasionally. Releasing the current bo when it's                   fb is with display can cause glitches. */
               if ( gbmCtx->handle != handle )
               {
                  /**
                   * TODO: Support different buffer types. Currently hardcoded to 24bpp
                   */
                  ret= drmModeAddFB( g_wstCtx->drmFd, g_wstCtx->modeInfo->hdisplay, g_wstCtx->modeInfo->vdisplay,
                                     24, 32, stride, handle, &fb_id );
                  if ( ret )
                  {
                     printf( "westeros_gl: WstGLSwapBuffers: drmModeAddFB error: %d errno %d\n", ret, errno );
                     return;
                  }
                  printf( "westeros_gl: WstGLSwapBuffers: handle %d fb_id %d\n", handle, fb_id );
                  gbmCtx->handle = handle;
                  gbmCtx->fb_id = fb_id;
               }
               else
               {
                  fb_id= gbmCtx->fb_id;
               }
            }

            #ifdef USE_PAGEFLIP
            FD_ZERO(&fds);
	    memset(&ev, 0, sizeof(ev));

            if ( !g_wstCtx->modeSet )
            {
               ret = drmModeSetCrtc(g_wstCtx->drmFd, g_wstCtx->drmEncoder->crtc_id, fb_id, 0, 0,
                                    &g_wstCtx->drmConnector->connector_id, 1,  g_wstCtx->modeInfo);
               if ( ret )
               {
                  printf( "westeros_gl: WstGLSwapBuffers: drmModeSetCrtc error: %d errno %d\n", ret, errno );
                  return;
               }
               g_wstCtx->modeSet= true;
            }
            else
            {
	       ret= drmModePageFlip( g_wstCtx->drmFd, g_wstCtx->drmEncoder->crtc_id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, g_wstCtx );
               if ( ret )
               {
                  #ifdef VERBOSE_DEBUG
                  printf( "westeros_gl: WstGLSwapBuffers: drmModePageFlip error: %d errno %d\n", ret, errno );
                  #endif
               }

               pageflip_pending++;
	       ev.version = 2;
	       ev.page_flip_handler = page_flip_event_handler;
	       FD_SET(0, &fds);
	       FD_SET(g_wstCtx->drmFd, &fds);

               /* handling pageflip event */
	       ret = select(g_wstCtx->drmFd + 1, &fds, NULL, NULL, NULL);
	       if (ret < 0) {
			fprintf(stderr, "select() failed with %d: %m\n", errno);
	       }
	       else if (FD_ISSET(g_wstCtx->drmFd, &fds)) {
			drmHandleEvent(g_wstCtx->drmFd, &ev);
	       }
            }
            #else
            ret = drmModeSetCrtc(g_wstCtx->drmFd, g_wstCtx->drmEncoder->crtc_id, fb_id, 0, 0,
                                 &g_wstCtx->drmConnector->connector_id, 1,  g_wstCtx->modeInfo);
            if ( ret )
            {
               printf( "westeros_gl: WstGLSwapBuffers: drmModeSetCrtc error: %d errno %d\n", ret, errno );
               return;
            }
            g_wstCtx->modeSet= true;
            #endif

            #ifdef EMIT_FRAMERATE
            ++frameCount;
            now= currentTimeMillis();
            if ( startTime == -1LL )
            {
               startTime= now;
            }
            else
            {
               long long diff= now-startTime;
               if ( diff >= 10000 )
               {
                  double fps= (double)frameCount/(double)diff*1000.0;
                  printf( "fps=%f\n", fps );
                  frameCount= 1;
                  startTime= now;
               }
            }
            #endif

         /* releasing previous bo safetly as current bo is already set to display controller */
         if (previousBO)
         {
            gbm_surface_release_buffer(surface, previousBO);
         }
         previousBO=bo;
         }

         #ifndef USE_PAGEFLIP
	 /* release the used buffer to gbm surface if no free buffer is available */
         if(!gbm_surface_has_free_buffers(surface))
         {
                gbm_surface_release_buffer(surface, bo);
         }
         #endif
      }
   }
}

static uint32_t find_crtc_for_encoder(drmModeRes *res, drmModeEncoder *encoder)
{
    int i;

    for (i = 0; i < res->count_crtcs; i++)
    {
        if (encoder->possible_crtcs & (1 << i))
        {
            return res->crtcs[i];
        }
    }

    return -1;
}

static uint32_t find_crtc_for_connector(WstGLCtx *ctx, drmModeRes *res, drmModeConnector *conn)
{
    int i;

    for (i = 0; i < conn->count_encoders; i++)
    {
        drmModeEncoder *encoder = drmModeGetEncoder(ctx->drmFd, conn->encoders[i]);

        if (encoder)
        {
            uint32_t crtc_id = find_crtc_for_encoder(res, encoder);

            drmModeFreeEncoder(encoder);
            if (crtc_id != 0)
            {
                return crtc_id;
            }
        }
    }

    return -1;
}

static bool setupDRM( WstGLCtx *ctx )
{
   bool result= false;
   const char *card;
   drmModeRes *res= 0;
   drmModeConnector *conn= 0;
   drmModeCrtc *crtc= 0;
   int i, mi= 0;
   
   card= DEFAULT_CARD;
   
   if ( ctx->drmFd < 0 )
   {
      ctx->drmFd= open(card, O_RDWR);
   }
   
   if ( ctx->drmFd < 0 )
   {
      printf("westeros-gl: setupDrm: unable open card (%s)\n", card );
      goto exit;
   }

   res= drmModeGetResources( ctx->drmFd );
   if ( res )
   {
      for( i= 0; i < res->count_connectors; ++i )
      {
         conn= drmModeGetConnector( ctx->drmFd, res->connectors[i] );
         if ( conn )
         {
            if ( (conn->connection == DRM_MODE_CONNECTED) && 
                 (conn->count_modes > 0) )
            {
               break;
            }
            
            drmModeFreeConnector(conn);
            conn= 0;
         }
      }      
   }
   else
   {
      printf("westeros-gl: setupDrm: unable to access drm resources for card (%s)\n", card);
   }
   
   if ( !conn )
   {
      printf("westeros-gl: setupDrm: unable to access a drm connector for card (%s)\n", card);
      goto exit;
   }
   
   ctx->drmRes= res;
   ctx->drmConnector= conn;
   
   ctx->gbm= gbm_create_device( ctx->drmFd );
   if ( ctx->gbm )
   {
      if ( !gRealEGLGetPlatformDisplay )
      {
         gRealEGLGetPlatformDisplay= (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress( "eglGetPlatformDisplayEXT" );
      }
      if ( gRealEGLGetPlatformDisplay )
      {
         ctx->dpy= gRealEGLGetPlatformDisplay( EGL_PLATFORM_GBM_KHR, ctx->gbm, NULL );
      }
      else
      {
         ctx->dpy= eglGetDisplay( ctx->gbm );
      }
      if ( ctx->dpy != EGL_NO_DISPLAY )
      {
         EGLint major, minor;
         EGLBoolean b;
         
         b= eglInitialize( ctx->dpy, &major, &minor );
         if ( !b )
         {
            printf("westeros-gl: setupDrm: error from eglInitialze for display %p\n", ctx->dpy);
            goto exit;
         }
      }
   }
   else
   {
      printf("westeros-gl: setupDrm: unable to create gbm device for card (%s)\n", card);
      goto exit;
   }
   
   /* List all reported display modes and find a good 720p mode */
   conn= ctx->drmConnector;
   for( i= 0; i < conn->count_modes; ++i )
   {
      printf("westeros_gl: setupDrm: mode %d: %dx%dx%d (%s) type 0x%x flags 0x%x\n", i, conn->modes[i].hdisplay, conn->modes[i].vdisplay, 
             conn->modes[i].vrefresh, conn->modes[i].name, conn->modes[i].type, conn->modes[i].flags );
      if ( (conn->modes[i].hdisplay == DEFAULT_MODE_WIDTH) && 
           (conn->modes[i].vdisplay == DEFAULT_MODE_HEIGHT) && 
           (conn->modes[i].type & DRM_MODE_TYPE_DRIVER) )
      {       
         mi= i;
      }       
   }
   printf("westeros_gl: choosing mode: %d\n", mi );
   ctx->modeInfo= &conn->modes[mi];

   for( i= 0; i < res->count_encoders; ++i )
   {
      ctx->drmEncoder= drmModeGetEncoder(ctx->drmFd, res->encoders[i]);
      if ( ctx->drmEncoder )
      {
         if ( ctx->drmEncoder->encoder_id == ctx->drmConnector->encoder_id )
         {
            break;
         }
         drmModeFreeEncoder( ctx->drmEncoder );
         ctx->drmEncoder= 0;
      }
   }
   
   if ( ctx->drmEncoder )
   {
      result= true;
      
      crtc= drmModeGetCrtc(ctx->drmFd, ctx->drmEncoder->crtc_id);
      if ( crtc && crtc->mode_valid )
      {
         printf("westeros-gl: setupDrm: current mode %dx%d@%d\n", crtc->mode.hdisplay, crtc->mode.vdisplay, crtc->mode.vrefresh );
      }
      else
      {
         printf("westeros-gl: setupDrm: unable to determine current mode for connector %p on card (%s)\n",
                ctx->drmConnector, card );
      }
   }
   else
   {
        uint32_t crtc_id = find_crtc_for_connector(ctx, res, conn);
        if (crtc_id == 0)
        {
            printf("westeros-gl: setupDrm: unable to get an encoder/CRTC for connector %p on card (%s)\n",
                   ctx->drmConnector, card);
        }
        else
        {
            printf("westeros-gl: setupDrm: found the CRTC %d for the connecter %p\n", crtc_id, ctx->drmConnector);
            for (i = 0; i < res->count_crtcs; i++)
            {
                if (res->crtcs[i] == crtc_id)
                {
                    break;
                }
            }
            ctx->drmEncoder = drmModeGetEncoder(ctx->drmFd, res->encoders[i]);
            ctx->drmEncoder->crtc_id = crtc_id;
            result= true;
        }
   }

exit:

   if ( !result )
   {
      if ( conn )
      {
         drmModeFreeConnector( conn );
      }
      if ( res )
      {
         drmModeFreeResources( res );
      }
   }
   
   return result;
}

static void destroyGbmCtx( struct gbm_bo *bo, void *userData )
{
   GbmCtx *gbmCtx= (GbmCtx*)userData;
   drmModeRmFB(g_wstCtx->drmFd, gbmCtx->fb_id);
   printf("westeros-gl: destroyGbmCtx %p\n", gbmCtx );
   if ( gbmCtx )
   {
      free( gbmCtx );
   }
}

