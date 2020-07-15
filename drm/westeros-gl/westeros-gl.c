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
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <poll.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>

#include "westeros-gl.h"

#define INT_FATAL(FORMAT, ...)      wstLog(0, "FATAL: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_ERROR(FORMAT, ...)      wstLog(0, "ERROR: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_WARNING(FORMAT, ...)    wstLog(1, "WARN: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_INFO(FORMAT, ...)       wstLog(2, "INFO: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_DEBUG(FORMAT, ...)      wstLog(3, "DEBUG: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE1(FORMAT, ...)     wstLog(4, "TRACE: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE2(FORMAT, ...)     wstLog(5, "TRACE: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE3(FORMAT, ...)     wstLog(6, "TRACE: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_FRAME(FORMAT, ...)      wstFrameLog( "FRAME: " FORMAT "\n", __VA_ARGS__)

#define FATAL(...)                  INT_FATAL(__VA_ARGS__, "")
#define ERROR(...)                  INT_ERROR(__VA_ARGS__, "")
#define WARNING(...)                INT_WARNING(__VA_ARGS__, "")
#define INFO(...)                   INT_INFO(__VA_ARGS__, "")
#define DEBUG(...)                  INT_DEBUG(__VA_ARGS__, "")
#define TRACE1(...)                 INT_TRACE1(__VA_ARGS__, "")
#define TRACE2(...)                 INT_TRACE2(__VA_ARGS__, "")
#define TRACE3(...)                 INT_TRACE3(__VA_ARGS__, "")
#define FRAME(...)                  INT_FRAME(__VA_ARGS__, "")

#define DEFAULT_CARD "/dev/dri/card0"
#ifdef WESTEROS_PLATFORM_QEMUX86
#define DEFAULT_MODE_WIDTH (1280)
#define DEFAULT_MODE_HEIGHT (1024)
#endif

#define DISPLAY_SAFE_BORDER_PERCENT (5)

#define DRM_NO_SRC_CROP

#ifndef DRM_NO_OUT_FENCE
#define DRM_USE_OUT_FENCE
#define DRM_NO_NATIVE_FENCE
#endif

#ifndef DRM_NO_NATIVE_FENCE
#ifdef EGL_ANDROID_native_fence_sync
#define DRM_USE_NATIVE_FENCE
#endif
#endif

typedef EGLDisplay (*PREALEGLGETDISPLAY)(EGLNativeDisplayType);
typedef EGLBoolean (*PREALEGLSWAPBUFFERS)(EGLDisplay, EGLSurface surface );
typedef EGLSurface (*PREALEGLCREATEWINDOWSURFACE)(EGLDisplay,
                                                  EGLConfig,
                                                  EGLNativeWindowType,
                                                  const EGLint *attrib_list);
#ifdef DRM_USE_NATIVE_FENCE
typedef EGLSyncKHR (*PREALEGLCREATESYNCKHR)(EGLDisplay, EGLenum, const EGLint *attrib_list);
typedef EGLBoolean (*PREALEGLDESTROYSYNCKHR)(EGLDisplay, EGLSyncKHR);
typedef EGLint (*PREALEGLCLIENTWAITSYNCKHR)(EGLDisplay, EGLSyncKHR, EGLint, EGLint);
typedef EGLint (*PREALEGLWAITSYNCKHR)(EGLDisplay, EGLSyncKHR, EGLint);
#endif

typedef struct _VideoServerCtx VideoServerCtx;
typedef struct _DisplayServerCtx DisplayServerCtx;
typedef struct _WstOverlayPlane WstOverlayPlane;

typedef struct _VideoServerConnection
{
   pthread_mutex_t mutex;
   VideoServerCtx *server;
   WstOverlayPlane *videoPlane;
   int socketFd;
   pthread_t threadId;
   bool threadStarted;
   bool threadStopRequested;
   int refreshRate;
} VideoServerConnection;

typedef struct _DisplayServerConnection
{
   pthread_mutex_t mutex;
   DisplayServerCtx *server;
   int socketFd;
   pthread_t threadId;
   bool threadStarted;
   bool threadStopRequested;
   int responseCode;
   int responseLen;
   char response[256+3];
} DisplayServerConnection;

#define MAX_SUN_PATH (80)
typedef struct _WstServerCtx
{
   pthread_mutex_t mutex;
   int refCnt;
   const char *name;
   struct sockaddr_un addr;
   char lock[MAX_SUN_PATH+6];
   int lockFd;
   int socketFd;
   pthread_t threadId;
   bool threadStarted;
   bool threadStopRequested;
} WstServerCtx;

#define MAX_VIDEO_CONNECTIONS (4)
typedef struct _VideoServerCtx
{
   WstServerCtx *server;
   VideoServerConnection* connections[MAX_VIDEO_CONNECTIONS];
} VideoServerCtx;

#define MAX_DISPLAY_CONNECTIONS (4)
typedef struct _DisplayServerCtx
{
   WstServerCtx *server;
   DisplayServerConnection* connections[MAX_VIDEO_CONNECTIONS];
} DisplayServerCtx;

typedef struct _VideoFrame
{
   WstOverlayPlane *plane;
   bool hide;
   bool hidden;
   uint32_t fbId;
   uint32_t handle0;
   uint32_t handle1;
   int fd0;
   int fd1;
   int fd2;
   uint32_t frameWidth;
   uint32_t frameHeight;
   uint32_t frameFormat;
   int rectX;
   int rectY;
   int rectW;
   int rectH;
   int frameNumber;
   int bufferId;
   long long frameTime;
   void *vf;
} VideoFrame;

#define VFM_QUEUE_CAPACITY (16)
typedef struct _VideoFrameManager
{
   VideoServerConnection *conn;
   int queueSize;
   int queueCapacity;
   VideoFrame *queue;
   bool paused;
   long long vblankTime;
   long long vblankInterval;
   long long vblankIntervalPrev;
   long long flipTimeBase;
   long long frameTimeBase;
   long long flipTimeCurrent;
   long long frameTimeCurrent;
   long long adjust;
   int bufferIdCurrent;
   void *sync;
} VideoFrameManager;

#define ACTIVE_FRAMES (4)

#define FRAME_NEXT (0)
#define FRAME_CURR (1)
#define FRAME_PREV (2)
#define FRAME_FREE (3)

typedef struct _WstOverlayPlane
{
   struct _WstOverlayPlane *next;
   struct _WstOverlayPlane *prev;
   bool inUse;
   bool supportsVideo;
   bool supportsGraphics;
   int zOrder;
   uint32_t crtc_id;
   drmModePlane *plane;
   drmModeObjectProperties *planeProps;
   drmModePropertyRes **planePropRes;
   VideoFrame videoFrame[ACTIVE_FRAMES];
   VideoFrameManager *vfm;
   bool dirty;
   bool readyToFlip;
   int frameCount;
   long long flipTimeBase;
   long long frameTimeBase;
   long long flipTimeCurrent;
   long long frameTimeCurrent;
   VideoServerConnection *conn;
} WstOverlayPlane;

typedef struct _WstOverlayPlanes
{
   int totalCount;
   int usedCount;
   WstOverlayPlane *availHead;
   WstOverlayPlane *availTail;
   WstOverlayPlane *usedHead;
   WstOverlayPlane *usedTail;
   WstOverlayPlane *primary;
} WstOverlayPlanes;

typedef struct _NativeWindowItem
{
   struct _NativeWindowItem *next;
   void *nativeWindow;
   EGLSurface surface;
   WstOverlayPlane *windowPlane;
   uint32_t fbId;
   struct gbm_bo *bo;
   struct gbm_bo *prevBo;
   uint32_t prevFbId;
   int width;
   int height;
   bool dirty;
} NativeWindowItem;

typedef struct _WstGLCtx
{
   pthread_mutex_t mutex;
   int refCnt;
   int drmFd;
   bool isMaster;
   drmModeRes *res;
   drmModeConnector *conn;
   drmModeEncoder *enc;
   drmModeCrtc *crtc;
   drmModeModeInfo *modeInfo;
   drmModeModeInfo modeCurrent;
   drmModeModeInfo modeNext;
   WstOverlayPlanes overlayPlanes;
   struct gbm_device* gbm;
   bool outputEnable;
   bool graphicsEnable;
   bool videoEnable;
   bool usingSetDisplayMode;
   bool modeSet;
   bool modeSetPending;
   bool notifySizeChange;
   bool useVideoServer;
   bool usePlanes;
   bool haveAtomic;
   bool haveNativeFence;
   bool graphicsPreferPrimary;
   drmModeObjectProperties *connectorProps;
   drmModePropertyRes **connectorPropRes;
   drmModeObjectProperties *crtcProps;
   drmModePropertyRes **crtcPropRes;
   NativeWindowItem *nwFirst;
   NativeWindowItem *nwLast;
   EGLDisplay dpy;
   int flipPending;
   #ifdef DRM_USE_NATIVE_FENCE
   EGLSyncKHR fenceSync;
   #endif
   #if (defined DRM_USE_OUT_FENCE || defined DRM_USE_NATIVE_FENCE)
   int nativeOutputFenceFd;
   #endif
   bool dirty;
   bool forceDirty;
   bool useVBlank;
   pthread_t refreshThreadId;
   bool refreshThreadStarted;
   bool refreshThreadStopRequested;
} WstGLCtx;

typedef struct _WstGLSizeCBInfo
{
   struct _WstGLSizeCBInfo *next;
   WstGLCtx* ctx;
   void *userData;
   WstGLDisplaySizeCallback listener;
   int width;
   int height;
} WstGLSizeCBInfo;

static void wstLog( int level, const char *fmt, ... );
static void wstFrameLog( const char *fmt, ... );
static VideoFrameManager *wstCreateVideoFrameManager( VideoServerConnection *conn );
static void wstDestroyVideoFrameManager( VideoFrameManager *vfm );
static void wstVideoFrameManagerPushFrame( VideoFrameManager *vfm, VideoFrame *f );
static VideoFrame* wstVideoFrameManagerPopFrame( VideoFrameManager *vfm );
static void wstVideoFrameManagerPause( VideoFrameManager *vfm, bool pause );
static void wstDestroyVideoServerConnection( VideoServerConnection *conn );
static void wstDestroyDisplayServerConnection( DisplayServerConnection *conn );
static void wstFreeVideoFrameResources( VideoFrame *f );
static void wstVideoServerSendBufferRelease( VideoServerConnection *conn, int bufferId );
static void wstTermCtx( WstGLCtx *ctx );
static void wstUpdateCtx( WstGLCtx *ctx );
static void wstSelectMode( WstGLCtx *ctx, int width, int height );
static void wstStartRefreshThread( WstGLCtx *ctx );
static void wstSwapDRMBuffers( WstGLCtx *ctx );
static void wstSwapDRMBuffersAtomic( WstGLCtx *ctx );

static PFNEGLGETPLATFORMDISPLAYEXTPROC gRealEGLGetPlatformDisplay= 0;
static PREALEGLGETDISPLAY gRealEGLGetDisplay= 0;
static PREALEGLSWAPBUFFERS gRealEGLSwapBuffers= 0;
static PREALEGLCREATEWINDOWSURFACE gRealEGLCreateWindowSurface= 0;
#ifdef DRM_USE_NATIVE_FENCE
static PREALEGLCREATESYNCKHR gRealEGLCreateSyncKHR= 0;
static PREALEGLDESTROYSYNCKHR gRealEGLDestroySyncKHR= 0;
static PREALEGLCLIENTWAITSYNCKHR gRealEGLClientWaitSyncKHR= 0;
static PREALEGLWAITSYNCKHR gRealEGLWaitSyncKHR= 0;
#endif
static pthread_mutex_t gMutex= PTHREAD_MUTEX_INITIALIZER;
static WstGLCtx *gCtx= 0;
static WstGLSizeCBInfo *gSizeListeners= 0;
static VideoServerCtx *gVideoServer= 0;
static DisplayServerCtx *gDisplayServer= 0;
static int gGraphicsMaxWidth= 0;
static int gGraphicsMaxHeight= 0;
static bool emitFPS= false;
static int g_activeLevel= 2;
static bool g_frameDebug= false;

#define WSTGL_CHECK_GRAPHICS_SIZE(w, h) \
   if ( gGraphicsMaxWidth && ((w) > gGraphicsMaxWidth) ) (w)= gGraphicsMaxWidth; \
   if ( gGraphicsMaxHeight && ((h) > gGraphicsMaxHeight) ) (h)= gGraphicsMaxHeight

#define WSTRES_FD_VIDEO 0
#define WSTRES_HD_VIDEO 1
#define WSTRES_FB_VIDEO 2
#define WSTRES_BO_GRAPHICS 3
#define WSTRES_FB_GRAPHICS 4
typedef struct _WstResources
{
   int fdVideoCount;
   int handleVideoCount;
   int fbVideoCount;
   int boGraphicsCount;
   int fbGraphicsCount;
} WstResources;
static pthread_mutex_t resMutex= PTHREAD_MUTEX_INITIALIZER;
static WstResources *gResources= 0;

#ifdef USE_AMLOGIC_MESON
#include "avsync/aml-meson/avsync.c"
#endif

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static long long getMonotonicTimeMicros( void )
{
   int rc;
   struct timespec tm;
   long long timeMicro;
   static bool reportedError= false;
   if ( !reportedError )
   {
      rc= clock_gettime( CLOCK_MONOTONIC, &tm );
   }
   if ( reportedError || rc )
   {
      struct timeval tv;
      if ( !reportedError )
      {
         reportedError= true;
         ERROR("clock_gettime failed rc %d - using timeofday", rc);
      }
      gettimeofday(&tv,0);
      timeMicro= tv.tv_sec*1000000LL+tv.tv_usec;
   }
   else
   {
      timeMicro= tm.tv_sec*1000000LL+(tm.tv_nsec/1000LL);
   }
   return timeMicro;
}

static void wstLog( int level, const char *fmt, ... )
{
   if ( level <= g_activeLevel )
   {
      va_list argptr;
      fprintf( stderr, "%lld: ", getCurrentTimeMillis());
      va_start( argptr, fmt );
      vfprintf( stderr, fmt, argptr );
      va_end( argptr );
   }
}

static void wstFrameLog( const char *fmt, ... )
{
   if ( g_frameDebug )
   {
      va_list argptr;
      fprintf( stderr, "%lld: ", getCurrentTimeMillis());
      va_start( argptr, fmt );
      vfprintf( stderr, fmt, argptr );
      va_end( argptr );
   }
}

static void wstUpdateResources( int type, bool add, long long v, int line )
{
   pthread_mutex_lock( &resMutex );
   if ( !gResources )
   {
      gResources= (WstResources*)calloc( 1, sizeof(WstResources) );
   }
   if ( gResources )
   {
      switch( type )
      {
         case WSTRES_FD_VIDEO:
            if ( add )
               ++gResources->fdVideoCount;
            else
               --gResources->fdVideoCount;
            break;
         case WSTRES_HD_VIDEO:
            if ( add )
               ++gResources->handleVideoCount;
            else
               --gResources->handleVideoCount;
            break;
         case WSTRES_FB_VIDEO:
            if ( add )
               ++gResources->fbVideoCount;
            else
               --gResources->fbVideoCount;
            break;
         case WSTRES_BO_GRAPHICS:
            if ( add )
               ++gResources->boGraphicsCount;
            else
               --gResources->boGraphicsCount;
            break;
         case WSTRES_FB_GRAPHICS:
            if ( add )
               ++gResources->fbGraphicsCount;
            else
               --gResources->fbGraphicsCount;
            break;
         default:
            break;
      }
   }
   TRACE3("fdv %d hnv %d fbv %d bog %d fbg %d : v %llx (%lld) line %d",
          gResources->fdVideoCount,
          gResources->handleVideoCount,
          gResources->fbVideoCount,
          gResources->boGraphicsCount,
          gResources->fbGraphicsCount,
          v, v, line
         );
   pthread_mutex_unlock( &resMutex );
}

static void wstOverlayAppendUnused( WstOverlayPlanes *planes, WstOverlayPlane *overlay )
{
   WstOverlayPlane *insertAfter= planes->availHead;

   if ( insertAfter )
   {
      if ( overlay->zOrder < insertAfter->zOrder )
      {
         insertAfter= 0;
      }
      while ( insertAfter )
      {
         if ( !insertAfter->next || overlay->zOrder <= insertAfter->next->zOrder )
         {
            break;
         }
         insertAfter= insertAfter->next;
      }
   }

   if ( insertAfter )
   {
      overlay->next= insertAfter->next;
      if ( insertAfter->next )
      {
         insertAfter->next->prev= overlay;
      }
      else
      {
         planes->availTail= overlay;
      }
      insertAfter->next= overlay;
   }
   else
   {
      overlay->next= planes->availHead;
      if ( planes->availHead )
      {
         planes->availHead->prev= overlay;
      }
      else
      {
         planes->availTail= overlay;
      }
      planes->availHead= overlay;
   }
   overlay->prev= insertAfter;
}

static WstOverlayPlane *wstOverlayAllocPrimary( WstOverlayPlanes *planes )
{
   WstOverlayPlane *overlay= 0;

   pthread_mutex_lock( &gCtx->mutex );

   if ( planes->primary )
   {
      if ( !planes->primary->inUse )
      {
         ++planes->usedCount;

         overlay= planes->primary;
         if ( overlay->next )
         {
            overlay->next->prev= overlay->prev;
         }
         else
         {
            planes->availTail= overlay->prev;
         }
         if ( overlay->prev )
         {
            overlay->prev->next= overlay->next;
         }
         else
         {
            planes->availHead= overlay->next;
         }

         overlay->next= 0;
         overlay->prev= planes->usedTail;
         if ( planes->usedTail )
         {
            planes->usedTail->next= overlay;
         }
         else
         {
            planes->usedHead= overlay;
         }
         planes->usedTail= overlay;
         overlay->inUse= true;
      }
      else
      {
         WARNING("primary plane already in use");
      }
   }
   else
   {
      ERROR("no primary plane found");
   }

   pthread_mutex_unlock( &gCtx->mutex );

   return overlay;
}

static WstOverlayPlane *wstOverlayAlloc( WstOverlayPlanes *planes, bool graphics )
{
   WstOverlayPlane *overlay= 0;

   pthread_mutex_lock( &gCtx->mutex );

   if (
         (planes->usedCount < planes->totalCount) &&
         (graphics || planes->availHead->supportsVideo)
      )
   {
      ++planes->usedCount;
      if ( graphics )
      {
         overlay= planes->availTail;
         planes->availTail= overlay->prev;
         if ( planes->availTail )
         {
            planes->availTail->next= 0;
         }
         else
         {
            planes->availHead= 0;
         }
      }
      else
      {
         overlay= planes->availHead;
         planes->availHead= overlay->next;
         if ( planes->availHead )
         {
            planes->availHead->prev= 0;
         }
         else
         {
            planes->availTail= 0;
         }
      }

      overlay->next= 0;
      overlay->prev= planes->usedTail;
      if ( planes->usedTail )
      {
         planes->usedTail->next= overlay;
      }
      else
      {
         planes->usedHead= overlay;
      }
      planes->usedTail= overlay;
      overlay->inUse= true;
   }

   pthread_mutex_unlock( &gCtx->mutex );

   return overlay;
}

static void wstOverlayFree( WstOverlayPlanes *planes, WstOverlayPlane *overlay )
{
   if ( overlay )
   {
      int i;
      pthread_mutex_lock( &gCtx->mutex );

      overlay->frameCount= 0;
      overlay->frameTimeBase= 0;
      overlay->flipTimeBase= 0;
      for( i= 0; i < ACTIVE_FRAMES; ++i )
      {
         overlay->videoFrame[i].plane= 0;
         overlay->videoFrame[i].bufferId= -1;
      }
      overlay->inUse= false;
      overlay->conn= 0;
      if ( planes->usedCount <= 0 )
      {
         ERROR("wstOverlayFree: unmatched free");
      }
      --planes->usedCount;
      if ( overlay->next )
      {
         overlay->next->prev= overlay->prev;
      }
      else
      {
         planes->usedTail= overlay->prev;
      }
      if ( overlay->prev )
      {
         overlay->prev->next= overlay->next;
      }
      else
      {
         planes->usedHead= overlay->next;
      }
      overlay->next= 0;
      overlay->prev= 0;
      wstOverlayAppendUnused( planes, overlay );

      pthread_mutex_unlock( &gCtx->mutex );
   }
}

static int wstPutU32( unsigned char *p, unsigned n )
{
   p[0]= (n>>24);
   p[1]= (n>>16);
   p[2]= (n>>8);
   p[3]= (n&0xFF);

   return 4;
}

static unsigned int wstGetU32( unsigned char *p )
{
   unsigned n;

   n= (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|(p[3]);

   return n;
}

static long long wstGetS64( unsigned char *p )
{
   long long u;

   u= ((((long long)(p[0]))<<56) |
       (((long long)(p[1]))<<48) |
       (((long long)(p[2]))<<40) |
       (((long long)(p[3]))<<32) |
       (((long long)(p[4]))<<24) |
       (((long long)(p[5]))<<16) |
       (((long long)(p[6]))<<8) |
       (p[7]) );

   return u;
}

static void wstClosePrimeFDHandles( WstGLCtx *ctx, uint32_t handle0, uint32_t handle1, int line )
{
   if ( ctx )
   {
      int rc;
      struct drm_gem_close close;
      if ( handle0 )
      {
         memset( &close, 0, sizeof(close) );
         close.handle= handle0;
         wstUpdateResources( WSTRES_HD_VIDEO, false, handle0, line);
         rc= ioctl( ctx->drmFd, DRM_IOCTL_GEM_CLOSE, &close );
         if ( rc )
         {
            ERROR("DRM_IOCTL_GEM_CLOSE failed: handle0 %u rc %d", handle0, rc);
         }
         if ( handle1 && (handle1 != handle0) )
         {
            memset( &close, 0, sizeof(close) );
            close.handle= handle1;
            wstUpdateResources( WSTRES_HD_VIDEO, false, handle1, line);
            rc= ioctl( ctx->drmFd, DRM_IOCTL_GEM_CLOSE, &close );
            if ( rc )
            {
               ERROR("DRM_IOCTL_GEM_CLOSE failed: handle1 %u rc %d", handle1, rc);
            }
         }
      }
   }
}

static void wstFreeVideoFrameResources( VideoFrame *f )
{
   if ( f )
   {
      if ( f->vf )
      {
         FRAME("freeing sync vf %p", f->vf);
         free( f->vf );
         f->vf= 0;
      }
      if ( f->fbId )
      {
         wstUpdateResources( WSTRES_FB_VIDEO, false, f->fbId, __LINE__);
         drmModeRmFB( gCtx->drmFd, f->fbId );
         f->fbId= 0;
         wstClosePrimeFDHandles( gCtx, f->handle0, f->handle1, __LINE__ );
         f->handle0= 0;
         f->handle1= 0;
      }
      if ( f->fd0 >= 0 )
      {
         wstUpdateResources( WSTRES_FD_VIDEO, false, f->fd0, __LINE__);
         close( f->fd0 );
         f->fd0= -1;
         if ( f->fd1 >= 0 )
         {
            close( f->fd1 );
            f->fd1= -1;
         }
         if ( f->fd2 >= 0 )
         {
            close( f->fd2 );
            f->fd2= -1;
         }
      }
   }
}

static void wstVideoServerFreeBuffers( VideoServerConnection *conn, bool full )
{
   int i;

   for( i= 0; i < ACTIVE_FRAMES; ++i )
   {
      if ( (i >= 2) || full )
      {
         wstFreeVideoFrameResources( &conn->videoPlane->videoFrame[i] );
      }
   }
}

static void wstVideoServerFlush( VideoServerConnection *conn )
{
   int rc;
   int len;

   DEBUG("wstVideoServerFlush: enter");

   if ( conn->videoPlane->vfm )
   {
      wstDestroyVideoFrameManager( conn->videoPlane->vfm );
      conn->videoPlane->vfm= 0;
   }

   conn->videoPlane->vfm= wstCreateVideoFrameManager( conn );
   if ( !conn->videoPlane->vfm )
   {
      ERROR("Failed to create a new vfm");
   }

   DEBUG("wstVideoServerFlush: exit");
}

static void wstDumpMessage( char *p, int len)
{
   int i, c, col;

   col= 0;
   for( i= 0; i < len; ++i )
   {
      if ( col == 0 ) fprintf(stderr, "%04X: ", i);

      c= p[i];

      fprintf(stderr, "%02X ", c);

      if ( col == 7 ) fprintf( stderr, " - " );

      if ( col == 15 ) fprintf( stderr, "\n" );

      ++col;
      if ( col >= 16 ) col= 0;
   }

   if ( col > 0 ) fprintf(stderr, "\n");
}

static void wstVideoServerSendRefreshRate( VideoServerConnection *conn, int rate )
{
   struct msghdr msg;
   struct iovec iov[1];
   unsigned char mbody[4+4];
   int len;
   int sentLen;

   pthread_mutex_lock( &conn->mutex );

   msg.msg_name= NULL;
   msg.msg_namelen= 0;
   msg.msg_iov= iov;
   msg.msg_iovlen= 1;
   msg.msg_control= 0;
   msg.msg_controllen= 0;
   msg.msg_flags= 0;

   len= 0;
   mbody[len++]= 'V';
   mbody[len++]= 'S';
   mbody[len++]= 5;
   mbody[len++]= 'R';
   len += wstPutU32( &mbody[4], rate );

   iov[0].iov_base= (char*)mbody;
   iov[0].iov_len= len;

   do
   {
      sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
   }
   while ( (sentLen < 0) && (errno == EINTR));

   if ( sentLen == len )
   {
      DEBUG("sent rate %d to client", rate);
      conn->refreshRate= rate;
   }

   pthread_mutex_unlock( &conn->mutex );
}

static void wstVideoServerSendBufferRelease( VideoServerConnection *conn, int bufferId )
{
   struct msghdr msg;
   struct iovec iov[1];
   unsigned char mbody[4+4];
   int len;
   int sentLen;

   pthread_mutex_lock( &conn->mutex );

   msg.msg_name= NULL;
   msg.msg_namelen= 0;
   msg.msg_iov= iov;
   msg.msg_iovlen= 1;
   msg.msg_control= 0;
   msg.msg_controllen= 0;
   msg.msg_flags= 0;

   len= 0;
   mbody[len++]= 'V';
   mbody[len++]= 'S';
   mbody[len++]= 5;
   mbody[len++]= 'B';
   len += wstPutU32( &mbody[4], bufferId );

   iov[0].iov_base= (char*)mbody;
   iov[0].iov_len= len;

   do
   {
      sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
   }
   while ( (sentLen < 0) && (errno == EINTR));

   if ( sentLen == len )
   {
      FRAME("send release buffer %d to client", bufferId);
      //fprintf(stderr,"%lld: TRACE: send release buffer %d\n", getCurrentTimeMillis(), buffIndex );
   }

   pthread_mutex_unlock( &conn->mutex );
}

static void *wstVideoServerConnectionThread( void *arg )
{
   VideoServerConnection *conn= (VideoServerConnection*)arg;
   struct msghdr msg;
   struct cmsghdr *cmsg;
   struct iovec iov[1];
   unsigned char mbody[4+64];
   char cmbody[CMSG_SPACE(3*sizeof(int))];
   int moff= 0, len, i, rc;
   uint32_t fbId= 0;
   uint32_t frameWidth, frameHeight;
   uint32_t frameFormat;
   uint32_t frameSkipX, frameSkipY, rectSkipX, rectSkipY;
   int rectX, rectY, rectW, rectH;
   int fd0, fd1, fd2;
   int offset0, offset1, offset2;
   int stride0, stride1, stride2;
   int bufferId= 0;
   int bufferIdRel;
   long long frameTime= 0;
   VideoFrame videoFrame;

   DEBUG("wstVideoServerConnectionThread: enter");

   conn->videoPlane= wstOverlayAlloc( &gCtx->overlayPlanes, false );
   INFO("video plane %p : zorder: %d", conn->videoPlane, (conn->videoPlane ? conn->videoPlane->zOrder: -1) );

   if ( !conn->videoPlane )
   {
      ERROR("No video plane avaialble");
      goto exit;
   }

   conn->videoPlane->vfm= wstCreateVideoFrameManager( conn );
   if ( !conn->videoPlane->vfm )
   {
      ERROR("Unable to allocate vfm");
      goto exit;
   }

   videoFrame.plane= conn->videoPlane;
   for( i= 0; i < ACTIVE_FRAMES; ++i )
   {
      conn->videoPlane->videoFrame[i].plane= conn->videoPlane;
   }

   conn->videoPlane->conn= conn;

   conn->threadStarted= true;
   while( !conn->threadStopRequested )
   {
      if ( gCtx->modeInfo && gCtx->modeInfo->vrefresh != conn->refreshRate )
      {
         wstVideoServerSendRefreshRate( conn, gCtx->modeInfo->vrefresh );
      }

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= 4;

      cmsg= (struct cmsghdr*)cmbody;
      cmsg->cmsg_len= CMSG_LEN(3*sizeof(int));
      cmsg->cmsg_level= SOL_SOCKET;
      cmsg->cmsg_type= SCM_RIGHTS;

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= cmsg;
      msg.msg_controllen= cmsg->cmsg_len;
      msg.msg_flags= 0;

      do
      {
         len= recvmsg( conn->socketFd, &msg, 0 );
      }
      while ( (len < 0) && (errno == EINTR));

      if ( len > 0 )
      {
         fd0= fd1= fd2= -1;

         if ( g_activeLevel >= 7 )
         {
            wstDumpMessage( mbody, len );
         }
         if ( len == 4 )
         {
            unsigned char *m= mbody;
            if ( (m[0] == 'V') && (m[1] == 'S') )
            {
               int mlen, id;
               mlen= m[2];
               id= m[3];
               switch( id )
               {
                  case 'F':
                     cmsg= CMSG_FIRSTHDR(&msg);
                     if ( cmsg &&
                          cmsg->cmsg_level == SOL_SOCKET &&
                          cmsg->cmsg_type == SCM_RIGHTS &&
                          cmsg->cmsg_len >= CMSG_LEN(sizeof(int)) )
                     {
                        fd0 = ((int*)CMSG_DATA(cmsg))[0];
                        if ( cmsg->cmsg_len >= CMSG_LEN(2*sizeof(int)) )
                        {
                           fd1 = ((int*)CMSG_DATA(cmsg))[1];
                        }
                        if ( cmsg->cmsg_len >= CMSG_LEN(3*sizeof(int)) )
                        {
                           fd2 = ((int*)CMSG_DATA(cmsg))[2];
                        }
                     }
                     break;
                  default:
                     break;
               }

               if ( mlen > sizeof(mbody) )
               {
                  ERROR("bad message length: %d : truncating");
                  mlen= sizeof(mbody);
               }
               if ( mlen > 1 )
               {
                  iov[0].iov_base= (char*)mbody+4;
                  iov[0].iov_len= mlen-1;

                  msg.msg_name= NULL;
                  msg.msg_namelen= 0;
                  msg.msg_iov= iov;
                  msg.msg_iovlen= 1;
                  msg.msg_control= 0;
                  msg.msg_controllen= 0;
                  msg.msg_flags= 0;

                  do
                  {
                     len= recvmsg( conn->socketFd, &msg, 0 );
                  }
                  while ( (len < 0) && (errno == EINTR));
               }

               if ( len > 0 )
               {
                  len += 4;
                  m += 3;
                  if ( g_activeLevel >= 7 )
                  {
                     wstDumpMessage( mbody, len );
                  }
                  switch( id )
                  {
                     case 'F':
                        if ( fd0 >= 0 )
                        {
                           uint32_t handle0, handle1;

                           wstUpdateResources( WSTRES_FD_VIDEO, true, fd0, __LINE__);
                           frameWidth= wstGetU32( m+1 );
                           frameHeight= ((wstGetU32( m+5)+1) & ~1);
                           frameFormat= wstGetU32( m+9);
                           rectX= (int)wstGetU32( m+13 );
                           rectY= (int)wstGetU32( m+17 );
                           rectW= (int)wstGetU32( m+21 );
                           rectH= (int)wstGetU32( m+25 );
                           offset0= (int)wstGetU32( m+29 );
                           stride0= (int)wstGetU32( m+33 );
                           offset1= (int)wstGetU32( m+37 );
                           stride1= (int)wstGetU32( m+41 );
                           offset2= (int)wstGetU32( m+45 );
                           stride2= (int)wstGetU32( m+49 );
                           bufferId= (int)wstGetU32( m+53 );
                           frameTime= (long long)wstGetS64( m+57 );
                           FRAME("got frame %d buffer %d frameTime %lld", conn->videoPlane->frameCount, bufferId, frameTime);

                           TRACE2("got frame fd %d,%d,%d (%dx%d) %X (%d, %d, %d, %d) off(%d, %d, %d) stride(%d, %d, %d)",
                                  fd0, fd1, fd2, frameWidth, frameHeight, frameFormat, rectX, rectY, rectW, rectH,
                                  offset0, offset1, offset2, stride0, stride1, stride2 );

                           rectSkipX= 0;
                           frameSkipX= 0;
                           #ifdef DRM_NO_SRC_CROP
                           /* If drmModeSetPlane won't perform src cropping we will
                              crop here in the creation of the fb.  This would be the case
                              where the target video frame rect has negative x or y
                              coordinates */
                           if ( rectX < 0 )
                           {
                              rectX &= ~1;
                              frameSkipX= -rectX*frameWidth/rectW;
                              rectSkipX= -rectX;
                           }
                           frameSkipY= 0;
                           rectSkipY= 0;
                           if ( rectY < 0 )
                           {
                              rectY &= ~1;
                              frameSkipY= -rectY*frameHeight/rectH;
                              frameSkipY &= ~1;
                              rectSkipY= -rectY;
                           }
                           #endif

                           rc= drmPrimeFDToHandle( gCtx->drmFd, fd0, &handle0 );
                           if ( !rc )
                           {
                              wstUpdateResources( WSTRES_HD_VIDEO, true, handle0, __LINE__);
                              handle1= handle0;
                              if ( fd1 >= 0 )
                              {
                                 rc= drmPrimeFDToHandle( gCtx->drmFd, fd1, &handle1 );
                                 if ( !rc )
                                 {
                                    wstUpdateResources( WSTRES_HD_VIDEO, true, handle1, __LINE__);
                                 }
                              }
                           }
                           if ( !rc )
                           {
                              uint32_t handles[4]= { handle0,
                                                     handle1,
                                                     0,
                                                     0 };
                              uint32_t pitches[4]= { stride0,
                                                     stride1,
                                                     0,
                                                     0 };
                              uint32_t offsets[4]= { offset0+frameSkipX+frameSkipY*stride0,
                                                     offset1+frameSkipX+frameSkipY*(stride1/2),
                                                     0,
                                                     0};

                              rc= drmModeAddFB2( gCtx->drmFd,
                                                 frameWidth-frameSkipX,
                                                 frameHeight-frameSkipY,
                                                 frameFormat,
                                                 handles,
                                                 pitches,
                                                 offsets,
                                                 &fbId,
                                                 0 // flags
                                               );
                              if ( !rc )
                              {
                                 pthread_mutex_lock( &gMutex );
                                 wstUpdateResources( WSTRES_FB_VIDEO, true, fbId, __LINE__);
                                 videoFrame.hide= false;
                                 videoFrame.fbId= fbId;
                                 videoFrame.handle0= handle0;
                                 videoFrame.handle1= handle1;
                                 videoFrame.fd0= fd0;
                                 videoFrame.fd1= fd1;
                                 videoFrame.fd2= fd2;
                                 videoFrame.frameWidth= frameWidth-frameSkipX;
                                 videoFrame.frameHeight= frameHeight-frameSkipY;
                                 videoFrame.frameFormat= frameFormat;
                                 videoFrame.rectX= rectX+rectSkipX;
                                 videoFrame.rectY= rectY+rectSkipY;
                                 videoFrame.rectW= rectW-rectSkipX;
                                 videoFrame.rectH= rectH-rectSkipY;
                                 videoFrame.bufferId= bufferId;
                                 videoFrame.frameTime= frameTime;
                                 videoFrame.frameNumber= conn->videoPlane->frameCount++;
                                 videoFrame.vf= 0;
                                 wstVideoFrameManagerPushFrame( conn->videoPlane->vfm, &videoFrame );
                                 pthread_mutex_unlock( &gMutex );
                              }
                              else
                              {
                                 ERROR("wstVideoServerConnectionThread: drmModeAddFB2 failed: rc %d errno %d", rc, errno);
                                 wstClosePrimeFDHandles( gCtx, handle0, handle1, __LINE__ );
                                 wstUpdateResources( WSTRES_FD_VIDEO, false, fd0, __LINE__);
                                 close( fd0 );
                                 if ( fd1 >= 0 )
                                 {
                                    close( fd1 );
                                 }
                                 if ( fd2 >= 0 )
                                 {
                                    close( fd2 );
                                 }
                              }
                           }
                           else
                           {
                              ERROR("wstVideoServerConnectionThread: drmPrimeFDToHandle failed: rc %d errno %d", rc, errno);
                              wstUpdateResources( WSTRES_FD_VIDEO, false, fd0, __LINE__);
                              close( fd0 );
                              if ( fd1 >= 0 )
                              {
                                 close( fd1 );
                              }
                              if ( fd2 >= 0 )
                              {
                                 close( fd2 );
                              }
                           }
                        }
                        break;
                     case 'H':
                        {
                           DEBUG("got hide video plane %d", conn->videoPlane->plane->plane_id);
                           pthread_mutex_lock( &gMutex );
                           conn->videoPlane->videoFrame[FRAME_NEXT].hide= true;
                           gCtx->dirty= true;
                           conn->videoPlane->dirty= true;
                           wstVideoServerFlush( conn );
                           pthread_mutex_unlock( &gMutex );
                        }
                        break;
                     case 'S':
                        {
                           DEBUG("got flush video plane %d", conn->videoPlane->plane->plane_id);
                           FRAME("got flush video plane %d", conn->videoPlane->plane->plane_id);
                           pthread_mutex_lock( &gMutex );
                           conn->videoPlane->flipTimeBase= 0LL;
                           conn->videoPlane->frameTimeBase= 0LL;
                           wstVideoServerFlush( conn );
                           pthread_mutex_unlock( &gMutex );
                        }
                        break;
                     case 'P':
                        {
                           bool pause= (m[1] == 1);
                           DEBUG("got pause (%d) video plane %d", pause, conn->videoPlane->plane->plane_id);
                           pthread_mutex_lock( &gMutex );
                           wstVideoFrameManagerPause( conn->videoPlane->vfm, pause );
                           pthread_mutex_unlock( &gMutex );
                        }
                        break;
                     default:
                        ERROR("got unknown video server message: mlen %d", mlen);
                        wstDumpMessage( mbody, mlen+3 );
                        break;
                  }
               }
            }
            else
            {
               ERROR("msg bad header");
               wstDumpMessage( mbody, len );
               len= 0;
            }
         }
      }
      else
      {
         DEBUG("video server peer disconnected");
         break;
      }
   }

exit:
   if ( conn->videoPlane && gCtx )
   {
      pthread_mutex_lock( &gMutex );
      pthread_mutex_lock( &gCtx->mutex );

      drmModePlane *plane= conn->videoPlane->plane;
      plane->crtc_id= gCtx->enc->crtc_id;
      DEBUG("wstVideoServerConnectionThread: drmModeSetPlane plane_id %d crtc_id %d", plane->plane_id, plane->crtc_id);
      rc= drmModeSetPlane( gCtx->drmFd,
                           plane->plane_id,
                           plane->crtc_id,
                           0, // fbid
                           0, // flags
                           0, // plane x
                           0, // plane y
                           gCtx->modeInfo->hdisplay,
                           gCtx->modeInfo->vdisplay,
                           0, // fb rect x
                           0, // fb rect y
                           gCtx->modeInfo->hdisplay<<16,
                           gCtx->modeInfo->hdisplay<<16 );

      wstVideoServerFreeBuffers( conn, true );

      pthread_mutex_unlock( &gCtx->mutex );

      if ( conn->videoPlane->vfm )
      {
         wstDestroyVideoFrameManager( conn->videoPlane->vfm );
         conn->videoPlane->vfm= 0;
      }

      wstOverlayFree( &gCtx->overlayPlanes, conn->videoPlane );
      conn->videoPlane= 0;

      pthread_mutex_unlock( &gMutex );
   }

   conn->threadStarted= false;

   if ( !conn->threadStopRequested )
   {
      int i;
      pthread_mutex_lock( &conn->server->server->mutex );
      for( i= 0; i < MAX_VIDEO_CONNECTIONS; ++i )
      {
         if ( conn->server->connections[i] == conn )
         {
            conn->server->connections[i]= 0;
            break;
         }
      }
      pthread_mutex_unlock( &conn->server->server->mutex );

      wstDestroyVideoServerConnection( conn );
   }

   DEBUG("wstVideoServerConnectionThread: exit");

   return 0;
}

static VideoServerConnection *wstCreateVideoServerConnection( VideoServerCtx *server, int fd )
{
   VideoServerConnection *conn= 0;
   int rc;
   bool error= false;

   conn= (VideoServerConnection*)calloc( 1, sizeof(VideoServerConnection) );
   if ( conn )
   {
      pthread_mutex_init( &conn->mutex, 0 );
      conn->socketFd= fd;
      conn->server= server;

      rc= pthread_create( &conn->threadId, NULL, wstVideoServerConnectionThread, conn );
      if ( rc )
      {
         ERROR("unable to start video connection thread: rc %d errno %d", rc, errno);
         error= true;
         goto exit;
      }
   }

exit:

   if ( error )
   {
      if ( conn )
      {
         pthread_mutex_destroy( &conn->mutex );
         free( conn );
         conn= 0;
      }
   }

   return conn;
}

static void wstDestroyVideoServerConnection( VideoServerConnection *conn )
{
   if ( conn )
   {
      if ( conn->socketFd >= 0 )
      {
         shutdown( conn->socketFd, SHUT_RDWR );
      }

      if ( conn->socketFd >= 0 )
      {
         close( conn->socketFd );
         conn->socketFd= -1;
      }

      if ( conn->threadStarted )
      {
         conn->threadStopRequested= true;
         pthread_join( conn->threadId, NULL );
      }

      pthread_mutex_destroy( &conn->mutex );

      free( conn );
   }
}

static void *wstVideoServerThread( void *arg )
{
   int rc;
   VideoServerCtx *server= (VideoServerCtx*)arg;

   DEBUG("wstVideoServerThread: enter");

   while( !server->server->threadStopRequested )
   {
      int fd;
      struct sockaddr_un addr;
      socklen_t addrLen= sizeof(addr);

      DEBUG("waiting for connections...");
      fd= accept4( server->server->socketFd, (struct sockaddr *)&addr, &addrLen, SOCK_CLOEXEC );
      if ( fd >= 0 )
      {
         if ( !server->server->threadStopRequested )
         {
            VideoServerConnection *conn= 0;

            DEBUG("video server received connection: fd %d", fd);

            conn= wstCreateVideoServerConnection( server, fd );
            if ( conn )
            {
               int i;
               DEBUG("created video server connection %p for fd %d", conn, fd );
               pthread_mutex_lock( &server->server->mutex );
               for( i= 0; i < MAX_VIDEO_CONNECTIONS; ++i )
               {
                  if ( conn->server->connections[i] == 0 )
                  {
                     conn->server->connections[i]= conn;
                     break;
                  }
               }
               if ( i >= MAX_VIDEO_CONNECTIONS )
               {
                  ERROR("too many video connections");
                  wstDestroyVideoServerConnection( conn );
               }
               pthread_mutex_unlock( &server->server->mutex );
            }
            else
            {
               ERROR("failed to create video server connection for fd %d", fd);
            }
         }
         else
         {
            close( fd );
         }
      }
      else
      {
         usleep( 10000 );
      }
   }

exit:
   server->server->threadStarted= false;
   DEBUG("wstVideoServerThread: exit");

   return 0;
}

static bool wstInitServiceServer( const char *name, WstServerCtx **newServer )
{
   bool result= false;
   const char *workingDir;
   int rc, pathNameLen, addressSize;
   WstServerCtx *server= 0;

   server= (WstServerCtx*)calloc( 1, sizeof(WstServerCtx) );
   if ( !server )
   {
      ERROR("No memory for server name (%s)", name);
      goto exit;
   }

   pthread_mutex_init( &server->mutex, 0 );
   server->socketFd= -1;
   server->lockFd= -1;
   server->name= name;

   ++server->refCnt;

   workingDir= getenv("XDG_RUNTIME_DIR");
   if ( !workingDir )
   {
      ERROR("wstInitServiceServer: XDG_RUNTIME_DIR is not set");
      goto exit;
   }

   pathNameLen= strlen(workingDir)+strlen("/")+strlen(server->name)+1;
   if ( pathNameLen > (int)sizeof(server->addr.sun_path) )
   {
      ERROR("wstInitServiceServer: name for server unix domain socket is too long: %d versus max %d",
             pathNameLen, (int)sizeof(server->addr.sun_path) );
      goto exit;
   }

   server->addr.sun_family= AF_LOCAL;
   strcpy( server->addr.sun_path, workingDir );
   strcat( server->addr.sun_path, "/" );
   strcat( server->addr.sun_path, server->name );

   strcpy( server->lock, server->addr.sun_path );
   strcat( server->lock, ".lock" );

   server->lockFd= open(server->lock,
                        O_CREAT|O_CLOEXEC,
                        S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
   if ( server->lockFd < 0 )
   {
      ERROR("wstInitServiceServer: failed to create lock file (%s) errno %d", server->lock, errno );
      goto exit;
   }

   rc= flock(server->lockFd, LOCK_NB|LOCK_EX );
   if ( rc < 0 )
   {
      ERROR("wstInitServiceServer: failed to lock.  Is another server running with name %s ?", server->name );
      goto exit;
   }

   (void)unlink(server->addr.sun_path);

   server->socketFd= socket( PF_LOCAL, SOCK_STREAM|SOCK_CLOEXEC, 0 );
   if ( server->socketFd < 0 )
   {
      ERROR("wstInitServiceServer: unable to open socket: errno %d", errno );
      goto exit;
   }

   addressSize= pathNameLen + offsetof(struct sockaddr_un, sun_path);

   rc= bind(server->socketFd, (struct sockaddr *)&server->addr, addressSize );
   if ( rc < 0 )
   {
      ERROR("wstInitServiceServer: Error: bind failed for socket: errno %d", errno );
      goto exit;
   }

   rc= listen(server->socketFd, 1);
   if ( rc < 0 )
   {
      ERROR("wstInitServiceServer: Error: listen failed for socket: errno %d", errno );
      goto exit;
   }

   *newServer= server;

   result= true;

exit:

   if ( !result )
   {
      server->addr.sun_path[0]= '\0';
      server->lock[0]= '\0';
   }

   return result;
}

static void wstTermServiceServer( WstServerCtx *server )
{
   if ( server )
   {
      int i;

      pthread_mutex_lock( &server->mutex );
      if ( --server->refCnt > 0 )
      {
         pthread_mutex_unlock( &server->mutex );
         return;
      }

      if ( server->socketFd >= 0 )
      {
         shutdown( server->socketFd, SHUT_RDWR );
      }

      if ( server->threadStarted )
      {
         server->threadStopRequested= true;
         pthread_mutex_unlock( &server->mutex );
         pthread_join( server->threadId, NULL );
         pthread_mutex_lock( &server->mutex );
      }

      if ( server->socketFd >= 0 )
      {
         close(server->socketFd);
         server->socketFd= -1;
      }

      if ( server->addr.sun_path )
      {
         (void)unlink( server->addr.sun_path );
         server->addr.sun_path[0]= '\0';
      }

      if ( server->lockFd >= 0 )
      {
         close(server->lockFd);
         server->lockFd= -1;
      }

      if ( server->lock[0] != '\0' )
      {
         (void)unlink( server->lock );
         server->lock[0]= '\0';
      }

      pthread_mutex_unlock( &server->mutex );
      pthread_mutex_destroy( &server->mutex );

      free( server );
   }
}

static bool wstInitVideoServer( VideoServerCtx *server )
{
   bool result= false;
   int rc;

   if ( !wstInitServiceServer( "video", &server->server ) )
   {
      ERROR("wstInitVideoServer: Error: unable to start service server");
      goto exit;
   }

   rc= pthread_create( &server->server->threadId, NULL, wstVideoServerThread, server );
   if ( rc )
   {
      ERROR("wstInitVideoServer: Error: unable to start server thread: rc %d errno %d", rc, errno);
      goto exit;
   }
   server->server->threadStarted= true;

   result= true;

exit:
   return result;
}

static void wstTermVideoServer( VideoServerCtx *server )
{
   if ( server )
   {
      int i;

      wstTermServiceServer( server->server );
      server->server= 0;

      for( i= 0; i < MAX_VIDEO_CONNECTIONS; ++i )
      {
         VideoServerConnection *conn= server->connections[i];
         if ( conn )
         {
            pthread_mutex_unlock( &gMutex );
            wstDestroyVideoServerConnection( conn );
            pthread_mutex_lock( &gMutex );
            server->connections[i]= 0;
         }
      }
      free( server );
   }
}

static void wstDisplayServerSendResponse( DisplayServerConnection *conn )
{
   struct msghdr msg;
   struct iovec iov[1];
   unsigned char mbody[256+3];
   int len;
   int sentLen;

   pthread_mutex_lock( &conn->mutex );

   msg.msg_name= NULL;
   msg.msg_namelen= 0;
   msg.msg_iov= iov;
   msg.msg_iovlen= 1;
   msg.msg_control= 0;
   msg.msg_controllen= 0;
   msg.msg_flags= 0;

   len= 0;
   mbody[len++]= 'D';
   mbody[len++]= 'S';
   mbody[len++]= conn->responseLen+1;
   strncpy( &mbody[len], conn->response, conn->responseLen+1 );
   len += (conn->responseLen+1);

   iov[0].iov_base= (char*)mbody;
   iov[0].iov_len= len;

   do
   {
      sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
   }
   while ( (sentLen < 0) && (errno == EINTR));

   if ( sentLen != len )
   {
      ERROR("Unable to send response");
   }

   pthread_mutex_unlock( &conn->mutex );
}

static void wstDisplayServerProcessMessage( DisplayServerConnection *conn, int mlen, char *m )
{
   char *tok, *ctx;
   int tlen;

   DEBUG("ds msg len %d (%s)", mlen, m);

   if ( m && (mlen > 0) )
   {
      conn->response[0]= '\0';
      do
      {
         tok= strtok_r( m, " ", &ctx );
         if ( tok )
         {
            tlen= strlen( tok );
            if ( (tlen == 3) && !strncmp( tok, "get", tlen ) )
            {
               tok= strtok_r( 0, " ", &ctx );
               if ( tok )
               {
                  tlen= strlen( tok );
                  if ( (tlen == 4) && !strncmp( tok, "mode", tlen ) )
                  {
                     if ( gCtx && gCtx->modeInfo )
                     {
                        pthread_mutex_lock( &gCtx->mutex );
                        sprintf( conn->response, "%d: mode %dx%d%sx%d", 0,
                                  gCtx->modeInfo->hdisplay, gCtx->modeInfo->vdisplay,
                                  ((gCtx->modeInfo->flags & DRM_MODE_FLAG_INTERLACE) ? "i" : "p"),
                                  gCtx->modeInfo->vrefresh  );
                        pthread_mutex_unlock( &gCtx->mutex );
                     }
                     else
                     {
                        sprintf( conn->response, "%d: %s", -1, "mode unavailable" );
                     }
                  }
                  else if ( (tlen == 7) && !strncmp( tok, "display", tlen ) )
                  {
                     tok= strtok_r( 0, " ", &ctx );
                     if ( tok )
                     {
                        tlen= strlen( tok );
                        if ( (tlen == 6) && !strncmp( tok, "enable", tlen ) )
                        {
                           bool enabled;
                           pthread_mutex_lock( &gMutex );
                           enabled= gCtx->outputEnable;
                           pthread_mutex_unlock( &gMutex );
                           sprintf( conn->response, "%d: display enable %d", 0, enabled );
                        }
                     }
                     else
                     {
                        sprintf( conn->response, "%d: %s", -1, "get display missing argument(s)" );
                     }
                  }
                  else if ( (tlen == 8) && !strncmp( tok, "graphics", tlen ) )
                  {
                     tok= strtok_r( 0, " ", &ctx );
                     if ( tok )
                     {
                        tlen= strlen( tok );
                        if ( (tlen == 6) && !strncmp( tok, "enable", tlen ) )
                        {
                           bool enabled;
                           pthread_mutex_lock( &gMutex );
                           enabled= gCtx->graphicsEnable;
                           pthread_mutex_unlock( &gMutex );
                           sprintf( conn->response, "%d: graphics enable %d", 0, enabled );
                        }
                     }
                     else
                     {
                        sprintf( conn->response, "%d: %s", -1, "get graphics missing argument(s)" );
                     }
                  }
                  else if ( (tlen == 5) && !strncmp( tok, "video", tlen ) )
                  {
                     tok= strtok_r( 0, " ", &ctx );
                     if ( tok )
                     {
                        tlen= strlen( tok );
                        if ( (tlen == 6) && !strncmp( tok, "enable", tlen ) )
                        {
                           bool enabled;
                           pthread_mutex_lock( &gMutex );
                           enabled= gCtx->videoEnable;
                           pthread_mutex_unlock( &gMutex );
                           sprintf( conn->response, "%d: video enable %d", 0, enabled );
                        }
                     }
                     else
                     {
                        sprintf( conn->response, "%d: %s", -1, "get video missing argument(s)" );
                     }
                  }
                  else if ( (tlen == 8) && !strncmp( tok, "loglevel", tlen ) )
                  {
                     sprintf( conn->response, "%d: loglevel %d", 0, g_activeLevel );
                  }
                  else
                  {
                     sprintf( conn->response, "%d: %s", -1, "get bad argument(s)" );
                  }
               }
               else
               {
                  sprintf( conn->response, "%d: %s", -1, "get missing argument(s)" );
               }
               break;
            }
            else if ( (tlen == 3) && !strncmp( tok, "set", tlen ) )
            {
               tok= strtok_r( 0, " ", &ctx );
               if ( tok )
               {
                  tlen= strlen( tok );
                  if ( (tlen == 4) && !strncmp( tok, "mode", tlen ) )
                  {
                     tok= strtok_r( 0, " ", &ctx );
                     if ( tok )
                     {
                        if ( gCtx && gCtx->modeInfo )
                        {
                           bool result= WstGLSetDisplayMode( gCtx, tok );
                           if ( result )
                           {
                              gCtx->dirty= true;
                              gCtx->forceDirty= true;
                           }
                           sprintf( conn->response, "%d: %s %s", (result ? 0 : -1), "set mode", tok );
                        }
                        else
                        {
                           sprintf( conn->response, "%d: %s", -1, "set mode unavailable" );
                        }
                     }
                     else
                     {
                        sprintf( conn->response, "%d: %s", -1, "set mode missing argument(s)" );
                     }
                  }
                  else if ( (tlen == 7) && !strncmp( tok, "display", tlen ) )
                  {
                     tok= strtok_r( 0, " ", &ctx );
                     if ( tok )
                     {
                        tlen= strlen( tok );
                        if ( (tlen == 6) && !strncmp( tok, "enable", tlen ) )
                        {
                           tok= strtok_r( 0, " ", &ctx );
                           if ( tok )
                           {
                              int value= -1;
                              tlen= strlen( tok );
                              if ( (tlen == 1) && !strncmp( tok, "0", tlen) )
                              {
                                 value= 0;
                              }
                              else if ( (tlen == 1) && !strncmp( tok, "1", tlen) )
                              {
                                 value= 1;
                              }
                              if ( value >= 0 )
                              {
                                 pthread_mutex_lock( &gMutex );
                                 gCtx->outputEnable= value;
                                 gCtx->dirty= true;
                                 gCtx->forceDirty= true;
                                 pthread_mutex_unlock( &gMutex );
                                 sprintf( conn->response, "%d: display set enable %d", 0, value );
                              }
                           }
                           else
                           {
                              sprintf( conn->response, "%d: %s", -1, "set display enable missing argument(s)" );
                           }
                        }
                        else
                        {
                           sprintf( conn->response, "%d: %s", -1, "set display missing argument(s)" );
                        }
                     }
                  }
                  else if ( (tlen == 8) && !strncmp( tok, "graphics", tlen ) )
                  {
                     tok= strtok_r( 0, " ", &ctx );
                     if ( tok )
                     {
                        tlen= strlen( tok );
                        if ( (tlen == 6) && !strncmp( tok, "enable", tlen ) )
                        {
                           tok= strtok_r( 0, " ", &ctx );
                           if ( tok )
                           {
                              int value= -1;
                              tlen= strlen( tok );
                              if ( (tlen == 1) && !strncmp( tok, "0", tlen) )
                              {
                                 value= 0;
                              }
                              else if ( (tlen == 1) && !strncmp( tok, "1", tlen) )
                              {
                                 value= 1;
                              }
                              if ( value >= 0 )
                              {
                                 pthread_mutex_lock( &gMutex );
                                 gCtx->graphicsEnable= value;
                                 gCtx->dirty= true;
                                 gCtx->forceDirty= true;
                                 pthread_mutex_unlock( &gMutex );
                                 sprintf( conn->response, "%d: graphics set enable %d", 0, value );
                              }
                           }
                           else
                           {
                              sprintf( conn->response, "%d: %s", -1, "set graphics enable missing argument(s)" );
                           }
                        }
                        else
                        {
                           sprintf( conn->response, "%d: %s", -1, "set graphics missing argument(s)" );
                        }
                     }
                  }
                  else if ( (tlen == 5) && !strncmp( tok, "video", tlen ) )
                  {
                     tok= strtok_r( 0, " ", &ctx );
                     if ( tok )
                     {
                        tlen= strlen( tok );
                        if ( (tlen == 6) && !strncmp( tok, "enable", tlen ) )
                        {
                           tok= strtok_r( 0, " ", &ctx );
                           if ( tok )
                           {
                              int value= -1;
                              tlen= strlen( tok );
                              if ( (tlen == 1) && !strncmp( tok, "0", tlen) )
                              {
                                 value= 0;
                              }
                              else if ( (tlen == 1) && !strncmp( tok, "1", tlen) )
                              {
                                 value= 1;
                              }
                              if ( value >= 0 )
                              {
                                 pthread_mutex_lock( &gMutex );
                                 gCtx->videoEnable= value;
                                 pthread_mutex_unlock( &gMutex );
                                 sprintf( conn->response, "%d: video set enable %d", 0, value );
                              }
                           }
                           else
                           {
                              sprintf( conn->response, "%d: %s", -1, "set video enable missing argument(s)" );
                           }
                        }
                        else
                        {
                           sprintf( conn->response, "%d: %s", -1, "set video missing argument(s)" );
                        }
                     }
                  }
                  else if ( (tlen == 8) && !strncmp( tok, "loglevel", tlen ) )
                  {
                     tok= strtok_r( 0, " ", &ctx );
                     if ( tok )
                     {
                        int value= atoi(tok);
                        if ( value < 0 ) value= 0;
                        if ( value > 6 ) value= 6;
                        g_activeLevel= value;
                        sprintf( conn->response, "%d: set loglevel %d", 0, g_activeLevel );
                     }
                  }
                  else
                  {
                     sprintf( conn->response, "%d: %s", -1, "set bad argument(s)" );
                  }
               }
               else
               {
                  sprintf( conn->response, "%d: %s", -1, "set missing argument(s)" );
               }
               break;
            }
            else
            {
               sprintf( conn->response, "%d: %s", -1, "unknown cmd" );
               break;
            }
         }
      }
      while( tok );
   }

   conn->responseLen= strlen(conn->response);

   wstDisplayServerSendResponse( conn );
}

static void *wstDisplayServerConnectionThread( void *arg )
{
   DisplayServerConnection *conn= (DisplayServerConnection*)arg;
   struct msghdr msg;
   struct iovec iov[1];
   unsigned char mbody[256+3];
   int len;

   DEBUG("wstDisplayServerConnectionThread: enter");

   conn->threadStarted= true;
   while( !conn->threadStopRequested )
   {
      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= sizeof(mbody);

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      do
      {
         len= recvmsg( conn->socketFd, &msg, 0 );
      }
      while ( (len < 0) && (errno == EINTR));

      if ( len > 0 )
      {
         unsigned char *m= mbody;
         while ( len >= 4 )
         {
            if ( (m[0] == 'D') && (m[1] == 'S') )
            {
               int mlen, id;
               mlen= m[2];
               if ( len >= (mlen+3) )
               {
                  wstDisplayServerProcessMessage( conn, mlen, m+3 );

                  m += (mlen+3);
                  len -= (mlen+3);
               }
               else
               {
                  len= 0;
               }
            }
            else
            {
               len= 0;
            }
         }
      }
      else
      {
         DEBUG("display server peer disconnected");
         break;
      }

      usleep( 10000 );
   }

   conn->threadStarted= false;

   if ( !conn->threadStopRequested )
   {
      int i;
      pthread_mutex_lock( &conn->server->server->mutex );
      for( i= 0; i < MAX_DISPLAY_CONNECTIONS; ++i )
      {
         if ( conn->server->connections[i] == conn )
         {
            conn->server->connections[i]= 0;
            break;
         }
      }
      pthread_mutex_unlock( &conn->server->server->mutex );

      wstDestroyDisplayServerConnection( conn );
   }
   DEBUG("wstDisplayServerConnectionThread: exit");

   return 0;
}

static DisplayServerConnection *wstCreateDisplayServerConnection( DisplayServerCtx *server, int fd )
{
   DisplayServerConnection *conn= 0;
   int rc;
   bool error= false;

   conn= (DisplayServerConnection*)calloc( 1, sizeof(DisplayServerConnection) );
   if ( conn )
   {
      pthread_mutex_init( &conn->mutex, 0 );
      conn->socketFd= fd;
      conn->server= server;

      rc= pthread_create( &conn->threadId, NULL, wstDisplayServerConnectionThread, conn );
      if ( rc )
      {
         ERROR("unable to start display connection thread: rc %d errno %d", rc, errno);
         error= true;
         goto exit;
      }
   }

exit:

   if ( error )
   {
      if ( conn )
      {
         pthread_mutex_destroy( &conn->mutex );
         free( conn );
         conn= 0;
      }
   }

   return conn;
}

static void wstDestroyDisplayServerConnection( DisplayServerConnection *conn )
{
   if ( conn )
   {
      if ( conn->socketFd >= 0 )
      {
         shutdown( conn->socketFd, SHUT_RDWR );
      }

      if ( conn->socketFd >= 0 )
      {
         close( conn->socketFd );
         conn->socketFd= -1;
      }

      if ( conn->threadStarted )
      {
         conn->threadStopRequested= true;
         pthread_join( conn->threadId, NULL );
      }

      pthread_mutex_destroy( &conn->mutex );

      free( conn );
   }
}

static void *wstDisplayServerThread( void *arg )
{
   int rc;
   DisplayServerCtx *server= (DisplayServerCtx*)arg;

   DEBUG("wstDisplayServerThread: enter");

   while( !server->server->threadStopRequested )
   {
      int fd;
      struct sockaddr_un addr;
      socklen_t addrLen= sizeof(addr);

      DEBUG("waiting for connections...");
      fd= accept4( server->server->socketFd, (struct sockaddr *)&addr, &addrLen, SOCK_CLOEXEC );
      if ( fd >= 0 )
      {
         if ( !server->server->threadStopRequested )
         {
            DisplayServerConnection *conn= 0;

            DEBUG("display server received connection: fd %d", fd);

            conn= wstCreateDisplayServerConnection( server, fd );
            if ( conn )
            {
               int i;
               DEBUG("created display server connection %p for fd %d", conn, fd );
               pthread_mutex_lock( &server->server->mutex );
               for( i= 0; i < MAX_DISPLAY_CONNECTIONS; ++i )
               {
                  if ( conn->server->connections[i] == 0 )
                  {
                     conn->server->connections[i]= conn;
                     break;
                  }
               }
               if ( i >= MAX_DISPLAY_CONNECTIONS )
               {
                  ERROR("too many display connections");
                  wstDestroyDisplayServerConnection( conn );
               }
               pthread_mutex_unlock( &server->server->mutex );
            }
            else
            {
               ERROR("failed to create video server connection for fd %d", fd);
            }
         }
         else
         {
            close( fd );
         }
      }
      else
      {
         usleep( 10000 );
      }
   }

exit:
   server->server->threadStarted= false;
   DEBUG("wstDisplayServerThread: exit");

   return 0;
}

static bool wstInitDisplayServer( DisplayServerCtx *server )
{
   bool result= false;
   int rc;

   if ( !wstInitServiceServer( "display", &server->server ) )
   {
      ERROR("wstInitVideoServer: Error: unable to start service server");
      goto exit;
   }

   rc= pthread_create( &server->server->threadId, NULL, wstDisplayServerThread, server );
   if ( rc )
   {
      ERROR("wstInitDisplayServer: Error: unable to start server thread: rc %d errno %d", rc, errno);
      goto exit;
   }
   server->server->threadStarted= true;

   result= true;

exit:
   return result;
}

static void wstTermDisplayServer( DisplayServerCtx *server )
{
   if ( server )
   {
      int i;

      wstTermServiceServer( server->server );
      server->server= 0;
      free( server );
   }
}

static void wstDestroyVideoFrameManager( VideoFrameManager *vfm )
{
   if ( vfm )
   {
      #ifdef WESTEROS_GL_AVSYNC
      wstAVSyncTerm( vfm );
      #endif

      if ( vfm->queue )
      {
         int i;
         for( i= 0; i < vfm->queueSize; ++i )
         {
            if ( vfm->bufferIdCurrent != vfm->queue[i].bufferId )
            {
               wstFreeVideoFrameResources( &vfm->queue[i] );
               wstVideoServerSendBufferRelease( vfm->conn, vfm->queue[i].bufferId );
            }
         }
         free( vfm->queue );
      }
      free( vfm );
   }
}

static VideoFrameManager *wstCreateVideoFrameManager( VideoServerConnection *conn )
{
   VideoFrameManager *vfm= 0;
   bool error= false;

   vfm= (VideoFrameManager*)calloc( 1, sizeof(VideoFrameManager) );
   if ( vfm )
   {
      int i;

      vfm->conn= conn;
      vfm->queue= (VideoFrame*)calloc( VFM_QUEUE_CAPACITY, sizeof(VideoFrame) );
      if ( !vfm->queue )
      {
         ERROR("No memory for vfm frame queue (capacity %d)", VFM_QUEUE_CAPACITY);
         error= true;
         goto exit;
      }

      vfm->queueCapacity= VFM_QUEUE_CAPACITY;
      vfm->queueSize= 0;
      vfm->bufferIdCurrent= -1;

      for( i= 0; i < vfm->queueCapacity; ++i )
      {
         vfm->queue[i].fd0= -1;
         vfm->queue[i].fd1= -1;
         vfm->queue[i].fd2= -1;
         vfm->queue[i].bufferId= -1;
      }

      #ifdef WESTEROS_GL_AVSYNC
      wstAVSyncInit( vfm, 1 );
      #endif
   }

exit:
   if ( error )
   {
      wstDestroyVideoFrameManager( vfm );
   }
   return vfm;
}

static void wstVideoFrameManagerPushFrame( VideoFrameManager *vfm, VideoFrame *f )
{
   if ( vfm->queueSize+1 > vfm->queueCapacity )
   {
      int newCapacity= 2*vfm->queueCapacity+1;
      VideoFrame *newQueue= (VideoFrame*)calloc( newCapacity, sizeof(VideoFrame) );
      if ( newQueue )
      {
         int i;
         memcpy( newQueue, vfm->queue, vfm->queueSize*sizeof(VideoFrame) );
         for( i= vfm->queueSize; i < newCapacity; ++i )
         {
            newQueue[i].fd0= -1;
            newQueue[i].fd1= -1;
            newQueue[i].fd2= -1;
            newQueue[i].bufferId= -1;
         }
         FRAME("vfm expand queue capacity from %d to %d", vfm->queueCapacity, newCapacity);
         free( vfm->queue );
         vfm->queue= newQueue;
         vfm->queueCapacity= newCapacity;
      }
      else
      {
         ERROR("vfm queue full: no memory to expand, dropping frame");
         wstFreeVideoFrameResources( f );
         return;
      }
   }

   FRAME("vfm push frame %d bufferId %d", f->frameNumber, f->bufferId);

   #ifdef WESTEROS_GL_AVSYNC
   if ( vfm->sync )
   {
      wstAVSyncPush( vfm, f );
   }
   #endif

   vfm->queue[vfm->queueSize++]= *f;
}

#define US_TO_MS( t ) (((t) + 500LL) / 1000LL)
static VideoFrame* wstVideoFrameManagerPopFrame( VideoFrameManager *vfm )
{
   VideoFrame *f= 0;
   #ifdef WESTEROS_GL_AVSYNC
   if ( vfm->sync )
   {
      f= wstAVSyncPop( vfm );
      goto done;
   }
   #endif
   if ( !vfm->paused )
   {
      int i;
      if ( (vfm->queueSize && vfm->flipTimeBase) || (vfm->queueSize > 2) )
      {
         long long flipTime;
         if ( vfm->flipTimeBase == 0)
         {
            vfm->flipTimeBase= vfm->vblankTime;
            vfm->frameTimeBase= vfm->queue[0].frameTime;
            FRAME("set base: flipTimeBase %lld frameTimeBase %lld", vfm->flipTimeBase, vfm->frameTimeBase);
         }
         i= 0;
         while ( i < vfm->queueSize )
         {
            VideoFrame *fCheck= &vfm->queue[i];
            flipTime= (fCheck->frameTime - vfm->frameTimeBase) + vfm->flipTimeBase;
            FRAME("i %d flipTime %lld flipTimeCurrent %lld frameTimeCurrent %lld frameTime %lld frameTimeBase %lld flipTimeBase %lld",
                  i, flipTime, vfm->flipTimeCurrent, vfm->frameTimeCurrent, fCheck->frameTime, vfm->frameTimeBase, vfm->flipTimeBase);
            if ( flipTime == vfm->flipTimeCurrent )
            {
               f= fCheck;
            }
            FRAME("  flipTime %lld vblankTime+vblankInterval %lld", flipTime, vfm->vblankTime+vfm->vblankInterval);
            if ( US_TO_MS(vfm->vblankTime+vfm->vblankInterval) >= US_TO_MS(flipTime) )
            {
               if ( (i > 0) && (US_TO_MS(fCheck->frameTime) < US_TO_MS(vfm->frameTimeCurrent+vfm->vblankInterval)) )
               {
                  FRAME("  drop frame %d buffer %d", fCheck->frameNumber, fCheck->bufferId);
                  wstFreeVideoFrameResources( fCheck );
                  wstVideoServerSendBufferRelease( vfm->conn, fCheck->bufferId );
                  if ( vfm->queueSize > 2 )
                  {
                     memmove( &vfm->queue[1], &vfm->queue[2], (vfm->queueSize-2)*sizeof(VideoFrame) );
                  }
                  --vfm->queueSize;
                  continue;
               }
               if ( i > 0 )
               {
                  memmove( &vfm->queue[0], &vfm->queue[1], (vfm->queueSize-1)*sizeof(VideoFrame) );
                  --vfm->queueSize;
                  i= 0;
                  f= &vfm->queue[0];
               }
               else
               {
                  f= fCheck;
               }
               if ( f->bufferId != vfm->bufferIdCurrent )
               {
                  FRAME("  time to flip frame %d buffer %d", f->frameNumber, f->bufferId);
                  vfm->adjust= ((vfm->flipTimeCurrent != 0) ? (vfm->vblankInterval-(f->frameTime-vfm->frameTimeCurrent)) : 0);
                  vfm->flipTimeCurrent= flipTime;
                  vfm->frameTimeCurrent= f->frameTime + vfm->adjust;
                  break;
               }
            }
            ++i;
         }
      }
   }
done:
   if ( f )
   {
      vfm->bufferIdCurrent= f->bufferId;
   }
   return f;
}

static void wstVideoFrameManagerPause( VideoFrameManager *vfm, bool pause )
{
   FRAME("set pause: %d", pause);
   if ( vfm->paused && !pause )
   {
      vfm->flipTimeBase= 0;
   }
   vfm->paused= pause;
   #ifdef WESTEROS_GL_AVSYNC
   if ( vfm->sync )
   {
      wstAVSyncPause( vfm, pause );
   }
   #endif
}

static void wstGLNotifySizeListeners( void )
{
   int width, height;
   WstGLSizeCBInfo *listeners= 0;
   WstGLSizeCBInfo *iter, *cb;

   DEBUG("wstGLNotifySizeListeners: enter");
   pthread_mutex_lock( &gMutex );

   width= gCtx->modeCurrent.hdisplay;
   height= gCtx->modeCurrent.vdisplay;

   WSTGL_CHECK_GRAPHICS_SIZE(width, height);

   iter= gSizeListeners;
   while( iter )
   {
      DEBUG("wstGLNotifySizeListeners: check %p : has %dx%d vs %dx%d", iter, iter->width, iter->height, width, height);
      if ( (width != iter->width) || (height != iter->height) )
      {
         iter->width= width;
         iter->height= height;
         cb= (WstGLSizeCBInfo*)malloc( sizeof(WstGLSizeCBInfo) );
         if ( cb )
         {
            DEBUG("wstGLNotifySizeListeners: add %p to list (from %p)", cb, iter);
            *cb= *iter;
            cb->next= listeners;
            listeners= cb;
         }
      }
      iter= iter->next;
   }

   pthread_mutex_unlock( &gMutex );

   iter= listeners;
   while( iter )
   {
      cb= iter;
      iter= iter->next;
      DEBUG("wstGLNotifySizeListeners: invoke %p", cb);
      cb->listener( cb->userData, width, height);
      free( cb );
   }
   DEBUG("wstGLNotifySizeListeners: exit");
}

static void wstReleaseConnectorProperties( WstGLCtx *ctx )
{
   int i;
   if ( ctx->connectorProps )
   {
      if ( ctx->connectorPropRes )
      {
         for( i= 0; i < ctx->connectorProps->count_props; ++i )
         {
            if ( ctx->connectorPropRes[i] )
            {
               drmModeFreeProperty( ctx->connectorPropRes[i] );
               ctx->connectorPropRes[i]= 0;
            }
         }
         free( ctx->connectorPropRes );
         ctx->connectorPropRes= 0;
      }
      drmModeFreeObjectProperties( ctx->connectorProps );
      ctx->connectorProps= 0;
   }
}

static bool wstAcquireConnectorProperties( WstGLCtx *ctx )
{
   bool error= false;
   int i;

   if ( ctx->conn )
   {
      ctx->connectorProps= drmModeObjectGetProperties( ctx->drmFd, ctx->conn->connector_id, DRM_MODE_OBJECT_CONNECTOR );
      if ( ctx->connectorProps )
      {
         ctx->connectorPropRes= (drmModePropertyRes**)calloc( ctx->connectorProps->count_props, sizeof(drmModePropertyRes*) );
         if ( ctx->connectorPropRes )
         {
            for( i= 0; i < ctx->connectorProps->count_props; ++i )
            {
               ctx->connectorPropRes[i]= drmModeGetProperty( ctx->drmFd, ctx->connectorProps->props[i] );
               if ( ctx->connectorPropRes[i] )
               {
                  DEBUG("connector property %d name (%s) value (%lld)",
                        ctx->connectorProps->props[i], ctx->connectorPropRes[i]->name, ctx->connectorProps->prop_values[i] );
               }
               else
               {
                  error= true;
                  break;
               }
            }
         }
         else
         {
            error= true;
         }
      }
      else
      {
         error= true;
      }
      if ( error )
      {
         wstReleaseConnectorProperties( ctx );
         ctx->haveAtomic= false;
      }
   }

   return !error;
}

static void wstReleaseCrtcProperties( WstGLCtx *ctx )
{
   int i;
   if ( ctx->crtcProps )
   {
      if ( ctx->crtcPropRes )
      {
         for( i= 0; i < ctx->crtcProps->count_props; ++i )
         {
            if ( ctx->crtcPropRes[i] )
            {
               drmModeFreeProperty( ctx->crtcPropRes[i] );
               ctx->crtcPropRes[i]= 0;
            }
         }
         free( ctx->crtcPropRes );
         ctx->crtcPropRes= 0;
      }
      drmModeFreeObjectProperties( ctx->crtcProps );
      ctx->crtcProps= 0;
   }
}

static bool wstAcquireCrtcProperties( WstGLCtx *ctx )
{
   bool error= false;
   int i;

   ctx->crtcProps= drmModeObjectGetProperties( ctx->drmFd, ctx->crtc->crtc_id, DRM_MODE_OBJECT_CRTC );
   if ( ctx->crtcProps )
   {
      ctx->crtcPropRes= (drmModePropertyRes**)calloc( ctx->crtcProps->count_props, sizeof(drmModePropertyRes*) );
      if ( ctx->crtcPropRes )
      {
         for( i= 0; i < ctx->crtcProps->count_props; ++i )
         {
            ctx->crtcPropRes[i]= drmModeGetProperty( ctx->drmFd, ctx->crtcProps->props[i] );
            if ( ctx->crtcPropRes[i] )
            {
               DEBUG("crtc property %d name (%s) value (%lld)",
                     ctx->crtcProps->props[i], ctx->crtcPropRes[i]->name, ctx->crtcProps->prop_values[i] );
            }
            else
            {
               error= true;
               break;
            }
         }
      }
      else
      {
         error= true;
      }
   }
   else
   {
      error= true;
   }
   if ( error )
   {
      wstReleaseCrtcProperties( ctx );
      ctx->haveAtomic= false;
   }

   return !error;
}

static void wstReleasePlaneProperties( WstGLCtx *ctx, WstOverlayPlane *plane )
{
   int i;
   (void)ctx;
   if ( plane->planeProps )
   {
      if ( plane->planePropRes )
      {
         for( i= 0; i < plane->planeProps->count_props; ++i )
         {
            if ( plane->planePropRes[i] )
            {
               drmModeFreeProperty( plane->planePropRes[i] );
               plane->planePropRes[i]= 0;
            }
         }
         free( plane->planePropRes );
         plane->planePropRes= 0;
      }
      drmModeFreeObjectProperties( plane->planeProps );
      plane->planeProps= 0;
   }
}

static bool wstAcquirePlaneProperties( WstGLCtx *ctx, WstOverlayPlane *plane )
{
   bool error= false;
   int i, j;

   plane->planeProps= drmModeObjectGetProperties( ctx->drmFd, plane->plane->plane_id, DRM_MODE_OBJECT_PLANE );
   if ( plane->planeProps )
   {
      plane->planePropRes= (drmModePropertyRes**)calloc( plane->planeProps->count_props, sizeof(drmModePropertyRes*) );
      if ( plane->planePropRes )
      {
         for( i= 0; i < plane->planeProps->count_props; ++i )
         {
            plane->planePropRes[i]= drmModeGetProperty( ctx->drmFd, plane->planeProps->props[i] );
            if ( plane->planePropRes[i] )
            {
               DEBUG("plane %d  property %d name (%s) value (%lld)",
                     plane->plane->plane_id, plane->planeProps->props[i], plane->planePropRes[i]->name, plane->planeProps->prop_values[i] );
               for( j= 0; j < plane->planePropRes[i]->count_enums; ++j )
               {
                  DEBUG("  enum name (%s) value %llu", plane->planePropRes[i]->enums[j].name, plane->planePropRes[i]->enums[j].value );
               }
            }
            else
            {
               error= true;
               break;
            }
         }
      }
      else
      {
         error= true;
      }
   }
   else
   {
      error= true;
   }
   if ( error )
   {
      wstReleasePlaneProperties( ctx, plane );
      ctx->haveAtomic= false;
   }

   return !error;
}

static WstGLCtx *wstInitCtx( void )
{
   WstGLCtx *ctx= 0;
   drmModeRes *res= 0;
   int i, j, k, len;
   uint32_t n;
   const char *card;
   drmModeConnector *conn= 0;
   drmModePlaneRes *planeRes= 0;
   drmModePlane *plane= 0;
   drmModeObjectProperties *props= 0;
   drmModePropertyRes *prop= 0;
   int crtc_idx= -1;
   bool error= true;
   int rc;
   struct drm_set_client_cap clientCap;
   struct drm_mode_atomic atom;

   const char *env= getenv("WESTEROS_GL_GRAPHICS_MAX_SIZE");
   if ( env )
   {
      int w= 0, h= 0;
      if ( sscanf( env, "%dx%d", &w, &h ) == 2 )
      {
         if ( (w > 0) && (h > 0) )
         {
            gGraphicsMaxWidth= w;
            gGraphicsMaxHeight= h;
            INFO("Max graphics size: %dx%d", gGraphicsMaxWidth, gGraphicsMaxHeight );
         }
      }
   }

   if ( getenv("WESTEROS_GL_FPS" ) )
   {
      emitFPS= true;
   }

   card= getenv("WESTEROS_DRM_CARD");
   if ( !card )
   {
      card= DEFAULT_CARD;
   }

   ctx= (WstGLCtx*)calloc( 1, sizeof(WstGLCtx) );
   if ( ctx )
   {
      drmVersionPtr drmver= 0;

      pthread_mutex_init( &ctx->mutex, 0 );
      ctx->refCnt= 1;
      ctx->outputEnable= true;
      ctx->graphicsEnable= true;
      ctx->videoEnable= true;
      #ifndef WESTEROS_GL_NO_PLANES
      ctx->usePlanes= true;
      #endif
      ctx->drmFd= -1;
      ctx->drmFd= open(card, O_RDWR);
      if ( ctx->drmFd < 0 )
      {
         ERROR("wstInitCtx: failed to open card (%s)", card);
         goto exit;
      }
      rc= drmSetMaster( ctx->drmFd );
      if ( rc == 0 )
      {
         ctx->isMaster= true;
      }
      INFO("opened %s: master %d", card, ctx->isMaster);
      if ( getenv("WESTEROS_GL_NO_PLANES") )
      {
         INFO("westeros-gl: no planes");
         ctx->usePlanes= false;
      }
      ctx->useVideoServer= true;
      if ( getenv("WESTEROS_GL_NO_VIDEOSERVER") )
      {
         INFO("westeros-gl: no video server");
         ctx->useVideoServer= false;
      }
      ctx->useVBlank= true;
      if ( getenv("WESTEROS_GL_NO_VBLANK") )
      {
         INFO("westeros-gl: no vblank");
         ctx->useVBlank= false;
      }
      #if (defined DRM_USE_OUT_FENCE || defined DRM_USE_NATIVE_FENCE)
      ctx->nativeOutputFenceFd= -1;
      #endif

      drmver= drmGetVersion( ctx->drmFd );
      if ( drmver )
      {
         DEBUG("westeros-gl: drmGetVersion: %d.%d.%d name (%.*s) date (%.*s) desc (%.*s)",
               drmver->version_major, drmver->version_minor, drmver->version_patchlevel,
               drmver->name_len, drmver->name,
               drmver->date_len, drmver->date,
               drmver->desc_len, drmver->desc );
         drmFreeVersion( drmver );
      }

      if ( ctx->usePlanes )
      {
         clientCap.capability= DRM_CLIENT_CAP_UNIVERSAL_PLANES;
         clientCap.value= 1;
         rc= ioctl( ctx->drmFd, DRM_IOCTL_SET_CLIENT_CAP, &clientCap);
         DEBUG("westeros-gl: DRM_IOCTL_SET_CLIENT_CAP: DRM_CLIENT_CAP_UNIVERSAL_PLANES rc %d", rc);

         clientCap.capability= DRM_CLIENT_CAP_ATOMIC;
         clientCap.value= 1;
         rc= ioctl( ctx->drmFd, DRM_IOCTL_SET_CLIENT_CAP, &clientCap);
         DEBUG("westeros-gl: DRM_IOCTL_SET_CLIENT_CAP: DRM_CLIENT_CAP_ATOMIC rc %d", rc);
         if ( rc == 0 )
         {
            ctx->haveAtomic= true;
            INFO("westeros-gl: have drm atomic mode setting");
         }
      }

      res= drmModeGetResources( ctx->drmFd );
      if ( !res )
      {
         ERROR("wstInitCtx: failed to get resources from card (%s)", card);
         goto exit;
      }
      for( i= 0; i < res->count_connectors; ++i )
      {
         conn= drmModeGetConnector( ctx->drmFd, res->connectors[i] );
         if ( conn )
         {
            if ( conn->count_modes && (conn->connection == DRM_MODE_CONNECTED) )
            {
               break;
            }
            drmModeFreeConnector(conn);
            conn= 0;
         }
      }
      if ( !conn )
      {
         ERROR("wstInitCtx: unable to get connector for card (%s)", card);
      }
      ctx->res= res;
      ctx->conn= conn;

      if ( conn )
      {
         for( i= 0; i < conn->count_modes; ++i )
         {
            DEBUG("mode %d: %dx%dx%d (%s) type 0x%x flags 0x%x",
                   i, conn->modes[i].hdisplay, conn->modes[i].vdisplay, conn->modes[i].vrefresh,
                   conn->modes[i].name, conn->modes[i].type, conn->modes[i].flags );
         }
      }

      ctx->gbm= gbm_create_device( ctx->drmFd );
      if ( !ctx->gbm )
      {
         ERROR("wstInitCtx: unable to create gbm device for card (%s)", card);
         goto exit;
      }
      for( i= 0; i < res->count_encoders; ++i )
      {
         uint32_t crtcId= 0;
         bool found= false;
         ctx->enc= drmModeGetEncoder(ctx->drmFd, res->encoders[i]);
         if ( ctx->enc && conn && (ctx->enc->encoder_id == conn->encoder_id) )
         {
            found= true;
            break;
         }
         for( j= 0; j < res->count_crtcs; j++ )
         {
            if ( ctx->enc->possible_crtcs & (1 << j))
            {
               crtcId= res->crtcs[j];
               for( k= 0; k < res->count_crtcs; k++ )
               {
                  if ( res->crtcs[k] == crtcId )
                  {
                     drmModeFreeEncoder( ctx->enc );
                     ctx->enc= drmModeGetEncoder(ctx->drmFd, res->encoders[k]);
                     ctx->enc->crtc_id= crtcId;
                     DEBUG("got enc %p crtc id %d", ctx->enc, crtcId);
                     found= true;
                     break;
                  }
               }
               if ( found )
               {
                  break;
               }
            }
         }
         if ( !found )
         {
            drmModeFreeEncoder( ctx->enc );
            ctx->enc= 0;
         }
      }
      if ( ctx->enc )
      {
         ctx->crtc= drmModeGetCrtc(ctx->drmFd, ctx->enc->crtc_id);
         if ( ctx->crtc && ctx->crtc->mode_valid )
         {
            ctx->modeCurrent= ctx->crtc->mode;
            INFO("wstInitCtx: current mode %dx%d@%d", ctx->crtc->mode.hdisplay, ctx->crtc->mode.vdisplay, ctx->crtc->mode.vrefresh );

            for( j= 0; j < res->count_crtcs; ++j )
            {
               drmModeCrtc *crtcTest= drmModeGetCrtc( ctx->drmFd, res->crtcs[j] );
               if ( crtcTest )
               {
                  if ( crtcTest->crtc_id == ctx->enc->crtc_id )
                  {
                     crtc_idx= j;
                  }
                  drmModeFreeCrtc( crtcTest );
                  if ( crtc_idx >= 0 )
                  {
                     break;
                  }
               }
            }
         }
         else
         {
            WARNING("wstInitCtx: unable to determine current mode for connector %p on card %s crtc %p id %d", conn, card, ctx->crtc, ctx->crtc?ctx->crtc->crtc_id:0 );
            for( j= 0; j < res->count_crtcs; ++j )
            {
               drmModeCrtc *crtcTest= drmModeGetCrtc( ctx->drmFd, res->crtcs[j] );
               if ( crtcTest )
               {
                  if ( crtcTest->crtc_id == ctx->enc->crtc_id )
                  {
                     crtc_idx= j;
                  }
                  drmModeFreeCrtc( crtcTest );
                  if ( crtc_idx >= 0 )
                  {
                     break;
                  }
               }
            }
         }
      }
      else
      {
         ERROR("wstInitCtx: unable to find encoder for connector for card (%s)", card);
      }

      if ( ctx->haveAtomic )
      {
         wstAcquireConnectorProperties( ctx );
      }
      if ( ctx->haveAtomic )
      {
         wstAcquireCrtcProperties( ctx );
      }
      if ( !ctx->haveAtomic )
      {
         wstReleaseConnectorProperties( ctx );
         wstReleaseCrtcProperties( ctx );
      }

      #ifndef WESTEROS_GL_NO_PLANES
      if ( ctx->usePlanes && (crtc_idx >= 0) )
      {
         bool haveVideoPlanes= false;

         planeRes= drmModeGetPlaneResources( ctx->drmFd );
         if ( planeRes )
         {
            bool isOverlay, isPrimary, isVideo, isGraphics;
            int zpos;

            DEBUG("wstInitCtx: planeRes %p count_planes %d", planeRes, planeRes->count_planes );
            for( n= 0; n < planeRes->count_planes; ++n )
            {
               plane= drmModeGetPlane( ctx->drmFd, planeRes->planes[n] );
               if ( plane )
               {
                  isOverlay= isPrimary= isVideo= isGraphics= false;
                  zpos= 0;

                  props= drmModeObjectGetProperties( ctx->drmFd, planeRes->planes[n], DRM_MODE_OBJECT_PLANE );
                  if ( props )
                  {
                     for( j= 0; j < props->count_props; ++j )
                     {
                        prop= drmModeGetProperty( ctx->drmFd, props->props[j] );
                        if ( prop )
                        {
                           if ( plane->possible_crtcs & (1<<crtc_idx) )
                           {
                              len= strlen(prop->name);
                              if ( (len == 4) && !strncmp( prop->name, "type", len) )
                              {
                                 if ( props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY )
                                 {
                                    isPrimary= true;
                                 }
                                 else if ( props->prop_values[j] == DRM_PLANE_TYPE_OVERLAY )
                                 {
                                    isOverlay= true;
                                 }
                              }
                              else if ( (len == 4) && !strncmp( prop->name, "zpos", len) )
                              {
                                 zpos= props->prop_values[j];
                              }
                           }
                        }
                     }
                  }
                  if ( isPrimary || isOverlay )
                  {
                     WstOverlayPlane *newPlane;
                     newPlane= (WstOverlayPlane*)calloc( 1, sizeof(WstOverlayPlane) );
                     if ( newPlane )
                     {
                        int rc;
                        int pfi;

                        DEBUG("plane %d count_formats %d", plane->plane_id, plane->count_formats);
                        for( pfi= 0; pfi < plane->count_formats; ++pfi )
                        {
                           DEBUG("plane %d format %d: %x (%.*s)", plane->plane_id, pfi, plane->formats[pfi], 4, &plane->formats[pfi]);
                           switch( plane->formats[pfi] )
                           {
                              case DRM_FORMAT_NV12:
                                 isVideo= true;
                                 haveVideoPlanes= true;
                                 break;
                              case DRM_FORMAT_ARGB8888:
                                 isGraphics= true;
                                 break;
                              default:
                                 break;
                           }
                        }
                        ++ctx->overlayPlanes.totalCount;
                        newPlane->plane= plane;
                        newPlane->supportsVideo= isVideo;
                        newPlane->supportsGraphics= isGraphics;
                        newPlane->zOrder= n + zpos*16+((isVideo && !isGraphics) ? 0 : 256);
                        newPlane->inUse= false;
                        newPlane->crtc_id= ctx->enc->crtc_id;
                        for( i= 0; i < ACTIVE_FRAMES; ++i )
                        {
                           newPlane->videoFrame[i].fd0= -1;
                           newPlane->videoFrame[i].fd1= -1;
                           newPlane->videoFrame[i].fd2= -1;
                           newPlane->videoFrame[i].bufferId= -1;
                        }
                        TRACE3("plane zorder %d primary %d overlay %d video %d gfx %d crtc_id %d",
                               newPlane->zOrder, isPrimary, isOverlay, isVideo, isGraphics, newPlane->crtc_id);
                        if ( ctx->haveAtomic )
                        {
                           if ( wstAcquirePlaneProperties( ctx, newPlane ) )
                           {
                              if ( isPrimary )
                              {
                                 ctx->overlayPlanes.primary= newPlane;
                              }
                           }
                        }
                        wstOverlayAppendUnused( &ctx->overlayPlanes, newPlane );

                        plane= 0;
                     }
                     else
                     {
                        ERROR("No memory for WstOverlayPlane");
                     }
                  }
                  if ( prop )
                  {
                     drmModeFreeProperty( prop );
                  }
                  if ( props )
                  {
                     drmModeFreeObjectProperties( props );
                  }
                  if ( plane )
                  {
                     drmModeFreePlane( plane );
                     plane= 0;
                  }
               }
               else
               {
                  ERROR("wstInitCtx: drmModeGetPlane failed: errno %d", errno);
               }
            }
            drmModeFreePlaneResources( planeRes );
         }
         else
         {
            ERROR("wstInitCtx: drmModePlaneGetResoures failed: errno %d", errno );
         }

         INFO( "wstInitCtx; found %d overlay planes", ctx->overlayPlanes.totalCount );

         if (
              haveVideoPlanes &&
              ctx->overlayPlanes.primary &&
              (ctx->overlayPlanes.availHead != ctx->overlayPlanes.primary)
            )
         {
            ctx->graphicsPreferPrimary= true;
         }

         if ( ctx->isMaster )
         {
            gDisplayServer= (DisplayServerCtx*)calloc( 1, sizeof(DisplayServerCtx) );
            if ( gDisplayServer )
            {
               if ( !wstInitDisplayServer( gDisplayServer ) )
               {
                  ERROR("wstInitCtx: failed to initialize display server");
                  free( gDisplayServer );
                  gDisplayServer= 0;
               }
            }
         }

         if ( haveVideoPlanes && (ctx->overlayPlanes.totalCount >= 2) )
         {
            if ( ctx->useVideoServer && ctx->isMaster )
            {
               gVideoServer= (VideoServerCtx*)calloc( 1, sizeof(VideoServerCtx) );
               if ( gVideoServer )
               {
                  ctx->haveNativeFence= false;
                  if ( !wstInitVideoServer( gVideoServer ) )
                  {
                     ERROR("wstInitCtx: failed to initialize video server");
                     free( gVideoServer );
                     gVideoServer= 0;
                  }
               }
            }
         }
         else
         {
            ctx->usePlanes= false;
         }
      }
      #endif

      wstStartRefreshThread(ctx);
   }
   else
   {
      ERROR("wstInitCtx: no memory for WstGLCtx");
   }

   error= false;

exit:

   if ( error )
   {
      wstTermCtx(ctx);
      ctx= 0;
   }

   return ctx;
}

static void wstTermCtx( WstGLCtx *ctx )
{
   if ( ctx )
   {
      if ( gVideoServer )
      {
         wstTermVideoServer( gVideoServer );
         gVideoServer= 0;
      }

      if ( gDisplayServer )
      {
         wstTermDisplayServer( gDisplayServer );
         gDisplayServer= 0;
      }

      pthread_mutex_unlock( &gMutex );
      if ( ctx->refreshThreadStarted )
      {
         ctx->refreshThreadStopRequested= true;
         pthread_join( ctx->refreshThreadId, NULL );
      }
      pthread_mutex_lock( &gMutex );

      wstReleaseConnectorProperties( ctx );
      wstReleaseCrtcProperties( ctx );
      pthread_mutex_lock( &ctx->mutex );
      if ( ctx->overlayPlanes.totalCount )
      {
         WstOverlayPlane *toFree= 0;
         WstOverlayPlane *iter= ctx->overlayPlanes.usedHead;
         while( iter )
         {
            wstReleasePlaneProperties( ctx, iter );
            drmModeFreePlane( iter->plane );
            toFree= iter;
            iter= iter->next;
            free( toFree );
         }
      }
      pthread_mutex_unlock( &ctx->mutex );

      if ( ctx->gbm )
      {
         gbm_device_destroy(ctx->gbm);
         ctx->gbm= 0;
      }
      if ( ctx->crtc )
      {
         drmModeFreeCrtc(ctx->crtc);
         ctx->crtc= 0;
      }
      if ( ctx->enc )
      {
         drmModeFreeEncoder(ctx->enc);
         ctx->enc= 0;
      }
      if ( ctx->conn )
      {
         drmModeFreeConnector(ctx->conn);
         ctx->conn= 0;
      }
      if ( ctx->res )
      {
         drmModeFreeResources(ctx->res);
         ctx->res= 0;
      }
      if ( ctx->drmFd >= 0 )
      {
         close( ctx->drmFd );
         ctx->drmFd= -1;
      }
      pthread_mutex_destroy( &ctx->mutex );
      free( ctx );

      pthread_mutex_unlock( &gMutex );
      while( gSizeListeners )
      {
         WstGLRemoveDisplaySizeListener( ctx, gSizeListeners->listener );
      }
      pthread_mutex_lock( &gMutex );

      pthread_mutex_lock( &resMutex );
      if ( gResources )
      {
         free( gResources );
         gResources= 0;
      }
      pthread_mutex_unlock( &resMutex );
   }
}

static void wstUpdateCtx( WstGLCtx *ctx )
{
   int i, j, k;
   drmModeRes *res= 0;
   drmModeConnector *conn= 0;
   if ( ctx )
   {
      res= ctx->res;
      if ( res )
      {
         wstReleaseConnectorProperties( ctx );

         for( i= 0; i < res->count_connectors; ++i )
         {
            conn= drmModeGetConnector( ctx->drmFd, res->connectors[i] );
            if ( conn )
            {
               if ( conn->count_modes && (conn->connection == DRM_MODE_CONNECTED) )
               {
                  break;
               }
               drmModeFreeConnector(conn);
               conn= 0;
            }
         }
         if ( conn )
         {
            ctx->conn= conn;
            for( i= 0; i < conn->count_modes; ++i )
            {
               DEBUG("mode %d: %dx%dx%d (%s) type 0x%x flags 0x%x",
                      i, conn->modes[i].hdisplay, conn->modes[i].vdisplay, conn->modes[i].vrefresh,
                      conn->modes[i].name, conn->modes[i].type, conn->modes[i].flags );
            }

            for( i= 0; i < res->count_encoders; ++i )
            {
               uint32_t crtcId= 0;
               bool found= false;
               ctx->enc= drmModeGetEncoder(ctx->drmFd, res->encoders[i]);
               if ( ctx->enc && conn && (ctx->enc->encoder_id == conn->encoder_id) )
               {
                  found= true;
                  break;
               }
               for( j= 0; j < res->count_crtcs; j++ )
               {
                  if ( ctx->enc->possible_crtcs & (1 << j))
                  {
                     crtcId= res->crtcs[j];
                     for( k= 0; k < res->count_crtcs; k++ )
                     {
                        if ( res->crtcs[k] == crtcId )
                        {
                           drmModeFreeEncoder( ctx->enc );
                           ctx->enc= drmModeGetEncoder(ctx->drmFd, res->encoders[k]);
                           ctx->enc->crtc_id= crtcId;
                           DEBUG("got enc %p crtc id %d", ctx->enc, crtcId);
                           found= true;
                           break;
                        }
                     }
                     if ( found )
                     {
                        break;
                     }
                  }
               }
               if ( !found )
               {
                  drmModeFreeEncoder( ctx->enc );
                  ctx->enc= 0;
               }
            }

            if ( ctx->haveAtomic )
            {
               wstAcquireConnectorProperties( ctx );
            }

            if ( !ctx->modeSet )
            {
               if ( ctx->nwFirst )
               {
                  int width, height;
                  width= ctx->nwFirst->width;
                  height= ctx->nwFirst->height;

                  DEBUG("Select mode: window %dx%d", width, height);
                  wstSelectMode( ctx, width, height );
               }
            }
         }
      }
   }
}

static void wstSelectMode( WstGLCtx *ctx, int width, int height )
{
   if ( ctx && ctx->conn )
   {
      bool found= false;
      int i, area, largestArea= 0;
      int miPreferred= -1, miBest= -1;
      int refresh;
      const char *usePreferred= 0;
      const char *useBest= 0;
      int maxArea= 0;
      const char *env= getenv("WESTEROS_GL_MAX_MODE");
      if ( env )
      {
         int w= 0, h= 0;
         if ( sscanf( env, "%dx%d", &w, &h ) == 2 )
         {
            DEBUG("max mode: %dx%d", w, h);
            maxArea= w*h;
         }
      }
      if ( ctx->haveAtomic )
      {
         usePreferred= getenv("WESTEROS_GL_USE_PREFERRED_MODE");
         useBest= getenv("WESTEROS_GL_USE_BEST_MODE");
      }
      for( i= 0; i < ctx->conn->count_modes; ++i )
      {
         if ( usePreferred )
         {
            if ( ctx->conn->modes[i].type & DRM_MODE_TYPE_PREFERRED )
            {
               miPreferred= i;
            }
         }
         else if ( useBest )
         {
            area= ctx->conn->modes[i].hdisplay * ctx->conn->modes[i].vdisplay;
            if ( (area > largestArea) && ((maxArea == 0) || (area <= maxArea)) )
            {
               largestArea= area;
               miBest= i;
               refresh= ctx->conn->modes[i].vrefresh;
            }
            else if ( area == largestArea )
            {
               if ( ctx->conn->modes[i].vrefresh > refresh )
               {
                  miBest= i;
                  refresh= ctx->conn->modes[i].vrefresh;
               }
            }
         }
         else if ( (ctx->conn->modes[i].hdisplay == width) &&
              (ctx->conn->modes[i].vdisplay == height) &&
              (ctx->conn->modes[i].type & DRM_MODE_TYPE_DRIVER) )
         {
            found= true;
            ctx->modeCurrent= ctx->conn->modes[i];
            ctx->modeInfo= &ctx->modeCurrent;
            break;
         }
      }
      if ( !found )
      {
         if ( usePreferred && (miPreferred >= 0) )
         {
            ctx->modeCurrent= ctx->conn->modes[miPreferred];
            ctx->modeInfo= &ctx->modeCurrent;
         }
         else if ( useBest && (miBest >= 0) )
         {
            ctx->modeCurrent= ctx->conn->modes[miBest];
            ctx->modeInfo= &ctx->modeCurrent;
         }
         else
         {
            ctx->modeCurrent= ctx->conn->modes[0];
            ctx->modeInfo= &ctx->modeCurrent;
         }
      }
      INFO("choosing output mode: %dx%dx%d", ctx->modeInfo->hdisplay, ctx->modeInfo->vdisplay, ctx->modeInfo->vrefresh);
   }
}

static bool wstCheckPlanes( WstGLCtx *ctx, long long vblankTime, long long vblankInterval )
{
   bool dirty= false;
   NativeWindowItem *nw;

   FRAME("check planes: vblankTime %lld", vblankTime);

   if ( ctx->forceDirty )
   {
      ctx->forceDirty= false;
      dirty= true;
   }

   nw= gCtx->nwFirst;
   while( nw )
   {
      if ( nw->dirty )
      {
         dirty= true;
      }
      nw= nw->next;
   }

   pthread_mutex_lock( &ctx->mutex );
   if ( ctx->overlayPlanes.usedCount )
   {
      WstOverlayPlane *iter= ctx->overlayPlanes.usedHead;
      while( iter )
      {
         VideoFrame *frame= 0;
         if ( iter->vfm )
         {
            iter->vfm->vblankTime= vblankTime;
            iter->vfm->vblankInterval= vblankInterval;
            frame= wstVideoFrameManagerPopFrame( iter->vfm );
            if ( frame )
            {
               if ( frame->fbId != iter->videoFrame[FRAME_CURR].fbId )
               {
                  if ( !iter->videoFrame[FRAME_NEXT].hide )
                  {
                     iter->videoFrame[FRAME_NEXT]= *frame;
                  }
                  iter->dirty= true;
                  iter->readyToFlip= true;
                  dirty= true;
               }
            }
            if ( iter->videoFrame[FRAME_NEXT].hide )
            {
               iter->dirty= true;
               iter->readyToFlip= true;
               dirty= true;
            }
         }
         iter= iter->next;
      }
   }
   pthread_mutex_unlock( &ctx->mutex );

   return dirty;
}

static void wstReleasePreviousBuffers( WstGLCtx *ctx )
{
   NativeWindowItem *nw;
   nw= gCtx->nwFirst;
   while( nw )
   {
      if ( nw->prevBo )
      {
         struct gbm_surface* gs= (struct gbm_surface*)nw->nativeWindow;
         wstUpdateResources( WSTRES_FB_GRAPHICS, false, nw->prevFbId, __LINE__);
         drmModeRmFB( ctx->drmFd, nw->prevFbId );
         wstUpdateResources( WSTRES_BO_GRAPHICS, false, (long long)nw->prevBo, __LINE__);
         gbm_surface_release_buffer(gs, nw->prevBo);
      }
      nw->prevBo= 0;
      nw->prevFbId= 0;

      nw= nw->next;
   }

   pthread_mutex_lock( &ctx->mutex );
   if ( ctx->overlayPlanes.usedCount )
   {
      WstOverlayPlane *iter= ctx->overlayPlanes.usedHead;
      while( iter )
      {
         if ( iter->videoFrame[FRAME_FREE].fbId )
         {
            wstUpdateResources( WSTRES_FB_VIDEO, false, iter->videoFrame[FRAME_FREE].fbId, __LINE__);
            drmModeRmFB( ctx->drmFd, iter->videoFrame[FRAME_FREE].fbId );
            iter->videoFrame[FRAME_FREE].fbId= 0;
            wstClosePrimeFDHandles( ctx, iter->videoFrame[FRAME_FREE].handle0, iter->videoFrame[FRAME_FREE].handle1, __LINE__ );
            iter->videoFrame[FRAME_FREE].handle0= 0;
            iter->videoFrame[FRAME_FREE].handle1= 0;
            if ( iter->videoFrame[FRAME_FREE].fd0 >= 0 )
            {
               wstUpdateResources( WSTRES_FD_VIDEO, false, iter->videoFrame[FRAME_FREE].fd0, __LINE__);
               close( iter->videoFrame[FRAME_FREE].fd0 );
               iter->videoFrame[FRAME_FREE].fd0= -1;
               if ( iter->videoFrame[FRAME_FREE].fd1 >= 0 )
               {
                  close( iter->videoFrame[FRAME_FREE].fd1 );
                  iter->videoFrame[FRAME_FREE].fd1= -1;
               }
               if ( iter->videoFrame[FRAME_FREE].fd2 >= 0 )
               {
                  close( iter->videoFrame[FRAME_FREE].fd2 );
                  iter->videoFrame[FRAME_FREE].fd2= -1;
               }
            }
         }

         if ( iter->videoFrame[FRAME_FREE].bufferId != -1 )
         {
            wstVideoServerSendBufferRelease( iter->conn, iter->videoFrame[FRAME_FREE].bufferId );
            iter->videoFrame[FRAME_FREE].bufferId= -1;
         }

         iter= iter->next;
      }
   }
   pthread_mutex_unlock( &ctx->mutex );
}

static void *wstRefreshThread( void *arg )
{
   WstGLCtx *ctx= (WstGLCtx*)arg;
   drmVBlank vbl;
   long long delay;
   long long refreshInterval= 0LL;
   long long vblankTime= 0LL;

   DEBUG("refresh thread start");
   ctx->refreshThreadStarted= true;
   while( !ctx->refreshThreadStopRequested )
   {
      delay= 16667LL;

      vbl.request.type= DRM_VBLANK_RELATIVE;
      vbl.request.sequence= 1;
      vbl.request.signal= 0;

      if ( !ctx->conn )
      {
         wstUpdateCtx( ctx );
      }

      vblankTime += refreshInterval;

      FRAME("refresh: vblankTime %lld", vblankTime);

      if ( ctx->conn && ctx->modeInfo )
      {
         pthread_mutex_lock( &gMutex );
         if ( ctx->modeInfo->vrefresh )
         {
            refreshInterval= (1000000LL+(ctx->modeInfo->vrefresh/2))/ctx->modeInfo->vrefresh;
         }
         wstReleasePreviousBuffers( ctx );
         if ( wstCheckPlanes( ctx, vblankTime, refreshInterval ) )
         {
            TRACE3("refresh thread calling wstSwapDRMBuffers");
            wstSwapDRMBuffers( ctx );
            delay= 3LL*refreshInterval/4LL;
         }
         pthread_mutex_unlock( &gMutex );
      }

      if ( ctx->modeSet && ctx->useVBlank )
      {
         int rc;

         rc= drmWaitVBlank( ctx->drmFd, &vbl );
         if ( !rc )
         {
            vblankTime= vbl.reply.tval_sec*1000000LL + vbl.reply.tval_usec;
         }
         else
         {
            TRACE3("drmWaitVBlank failed: rc %d errno %d", rc, errno);
            if ( errno == 16 )
            {
               ERROR("drmWaitVBlank failed: rc %d errno %d - try running with 'export WESTEROS_GL_NO_VBLANK=1'", rc, errno);
            }
         }
      }
      else
      {
         if ( delay )
         {
            usleep( delay );
         }
      }
   }
   ctx->refreshThreadStarted= false;
   DEBUG("refresh thread exit");
   return NULL;
}

static void wstStartRefreshThread( WstGLCtx *ctx )
{
   int rc;
   rc= pthread_create( &ctx->refreshThreadId, NULL, wstRefreshThread, ctx );
   if ( rc )
   {
      ERROR("unable to start refresh thread: rc %d errno %d", rc, errno);
   }
}

static void wstAtomicAddProperty( WstGLCtx *ctx, drmModeAtomicReq *req, uint32_t objectId,
                                  int countProps, drmModePropertyRes **propRes, const char *name, uint64_t value )
{
   int rc;
   int i;
   uint32_t propId= 0;

   for( i= 0; i < countProps; ++i )
   {
      if ( !strcmp( name, propRes[i]->name ) )
      {
         propId= propRes[i]->prop_id;
         break;
      }
   }

   if ( propId > 0 )
   {
      TRACE3("wstAtomicAddProperty: objectId %d: %s, %lld", objectId, name, value);
      rc= drmModeAtomicAddProperty( req, objectId, propId, value );
      if ( rc < 0 )
      {
         ERROR("wstAtomicAddProperty: drmModeAtomicAddProperty fail: obj %d prop %d (%s) value %lld: rc %d errno %d", objectId, propId, name, value, rc, errno );
      }
   }
   else
   {
      WARNING("wstAtomicAddProperty: skip prop %s", name);
   }
}

static void pageFlipEventHandler(int fd, unsigned int frame,
				 unsigned int sec, unsigned int usec,
				 void *data)
{
   WstGLCtx *ctx= (WstGLCtx*)data;
   if ( ctx->flipPending )
   {
      --ctx->flipPending;
   }
}

static void wstSwapDRMBuffersAtomic( WstGLCtx *ctx )
{
   int rc;
   drmModeAtomicReq *req;
   uint32_t flags= 0;
   struct gbm_surface* gs;
   struct gbm_bo *bo;
   uint32_t handle, stride;
   NativeWindowItem *nw;

   TRACE3("wstSwapDRMBuffersAtomic: atomic start");

   req= drmModeAtomicAlloc();
   if ( !req )
   {
      ERROR("wstSwapDRMBuffersAtomic: drmModeAtomicAlloc failed, errno %x", errno);
      goto exit;
   }

   #if (defined DRM_USE_OUT_FENCE || defined DRM_USE_NATIVE_FENCE)
   #ifdef DRM_USE_NATIVE_FENCE
   if ( ctx->haveNativeFence )
   {
   #endif
      flags |= DRM_MODE_ATOMIC_NONBLOCK;
   #ifdef DRM_USE_NATIVE_FENCE
   }
   #endif
   #endif

   if ( !ctx->outputEnable )
   {
      if ( ctx->modeSet )
      {
         wstAtomicAddProperty( ctx, req, ctx->conn->connector_id,
                               ctx->connectorProps->count_props, ctx->connectorPropRes,
                               "CRTC_ID", 0 );

         wstAtomicAddProperty( ctx, req, ctx->crtc->crtc_id,
                               ctx->crtcProps->count_props, ctx->crtcPropRes,
                               "MODE_ID", 0 );

         wstAtomicAddProperty( ctx, req, ctx->crtc->crtc_id,
                               ctx->crtcProps->count_props, ctx->crtcPropRes,
                               "ACTIVE", 0 );
         ctx->modeSet= false;
      }
   }
   else
   if ( !ctx->modeSet )
   {
      uint32_t blobId= 0;

      flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
      wstAtomicAddProperty( ctx, req, ctx->conn->connector_id,
                            ctx->connectorProps->count_props, ctx->connectorPropRes,
                            "CRTC_ID", ctx->crtc->crtc_id );
      rc= drmModeCreatePropertyBlob( ctx->drmFd, ctx->modeInfo, sizeof(*ctx->modeInfo), &blobId );
      if ( rc == 0 )
      {
         wstAtomicAddProperty( ctx, req, ctx->crtc->crtc_id,
                               ctx->crtcProps->count_props, ctx->crtcPropRes,
                               "MODE_ID", blobId );

         wstAtomicAddProperty( ctx, req, ctx->crtc->crtc_id,
                               ctx->crtcProps->count_props, ctx->crtcPropRes,
                               "ACTIVE", 1 );
      }
      else
      {
         ERROR("wstSwapDRMBuffersAtomic: drmModeCreatePropertyBlob fail: rc %d errno %d", rc, errno);
      }
   }

   if ( ctx->outputEnable && (!ctx->useVBlank || !ctx->modeSet) )
   {
      #if (defined DRM_USE_OUT_FENCE || defined DRM_USE_NATIVE_FENCE)
      #ifdef DRM_USE_NATIVE_FENCE
      if ( ctx->haveNativeFence )
      {
      #endif
         wstAtomicAddProperty( ctx, req, ctx->crtc->crtc_id,
                               ctx->crtcProps->count_props, ctx->crtcPropRes,
                               "OUT_FENCE_PTR", (uint64_t)(unsigned long)(&ctx->nativeOutputFenceFd) );
      #ifdef DRM_USE_NATIVE_FENCE
      }
      #endif
      #endif
   }

   if ( gCtx )
   {
      nw= gCtx->nwFirst;
      while( nw )
      {
         if ( nw->dirty )
         {
            TRACE3("nw %p dirty", nw);
            gs= (struct gbm_surface*)nw->nativeWindow;
            if ( gs )
            {
               bo= gbm_surface_lock_front_buffer(gs);
               if ( bo )
               {
                  uint32_t fbId;
                  wstUpdateResources( WSTRES_BO_GRAPHICS, true, (long long)bo, __LINE__);

                  handle= gbm_bo_get_handle(bo).u32;
                  stride= gbm_bo_get_stride(bo);

                  rc= drmModeAddFB( ctx->drmFd,
                                    nw->width,
                                    nw->height,
                                    32,
                                    32,
                                    stride,
                                    handle,
                                    &fbId );
                  if ( rc )
                  {
                     ERROR("wstSwapDRMBuffersAtomic: drmModeAddFB rc %d errno %d", rc, errno);
                     gbm_surface_release_buffer(gs, bo);
                  }
                  else
                  {
                     nw->prevBo= nw->bo;
                     nw->prevFbId= nw->fbId;
                     nw->fbId= fbId;
                     wstUpdateResources( WSTRES_FB_GRAPHICS, true, nw->fbId, __LINE__);
                     nw->bo= bo;
                  }
               }
            }
         }
         if ( nw->fbId )
         {
            #ifdef DRM_USE_NATIVE_FENCE
            if ( ctx->fenceSync )
            {
               EGLint waitResult;
               for( ; ; )
               {
                  waitResult= gRealEGLClientWaitSyncKHR( ctx->dpy,
                                                    ctx->fenceSync,
                                                    0, // flags
                                                    EGL_FOREVER_KHR );
                  if ( waitResult == EGL_CONDITION_SATISFIED_KHR )
                  {
                     break;
                  }
               }
               gRealEGLDestroySyncKHR( ctx->dpy, ctx->fenceSync );
               ctx->fenceSync= EGL_NO_SYNC_KHR;
            }
            #endif

            if ( ctx->outputEnable )
            {
               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "FB_ID", nw->fbId );

               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "CRTC_ID", nw->windowPlane->crtc_id );

               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "SRC_X", 0 );

               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "SRC_Y", 0 );

               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "SRC_W", nw->width<<16 );

               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "SRC_H", nw->height<<16 );

               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "CRTC_X", 0 );

               if ( ctx->graphicsEnable )
               {
                  wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                        nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                        "CRTC_Y", 0 );
               }
               else
               {
                  wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                        nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                        "CRTC_Y", -ctx->modeInfo->vdisplay+2 );
               }

               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "CRTC_W", ctx->modeInfo->hdisplay );

               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "CRTC_H", ctx->modeInfo->vdisplay );

               wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                                     nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                                     "IN_FENCE_FD", -1 );
            }
         }
         nw= nw->next;
      }
   }

   pthread_mutex_lock( &ctx->mutex );
   if ( ctx->overlayPlanes.usedCount )
   {
      WstOverlayPlane *iter= ctx->overlayPlanes.usedHead;
      while( iter )
      {
         if ( iter->dirty && iter->readyToFlip )
         {
            if ( iter->videoFrame[FRAME_NEXT].fbId )
            {
               uint32_t fbId= iter->videoFrame[FRAME_NEXT].fbId;
               uint32_t handle0= iter->videoFrame[FRAME_NEXT].handle0;
               uint32_t handle1= iter->videoFrame[FRAME_NEXT].handle1;
               int fd0= iter->videoFrame[FRAME_NEXT].fd0;
               int fd1= iter->videoFrame[FRAME_NEXT].fd1;
               int fd2= iter->videoFrame[FRAME_NEXT].fd2;
               uint32_t frameWidth= iter->videoFrame[FRAME_NEXT].frameWidth;
               uint32_t frameHeight= iter->videoFrame[FRAME_NEXT].frameHeight;
               int rectX= iter->videoFrame[FRAME_NEXT].rectX;
               int rectY= iter->videoFrame[FRAME_NEXT].rectY;
               int rectW= iter->videoFrame[FRAME_NEXT].rectW;
               int rectH= iter->videoFrame[FRAME_NEXT].rectH;
               int bufferId= iter->videoFrame[FRAME_NEXT].bufferId;
               uint32_t sx, sy, sw, sh, dx, dy, dw, dh;
               int modeWidth, modeHeight, gfxWidth, gfxHeight;

               iter->videoFrame[FRAME_FREE]= iter->videoFrame[FRAME_PREV];
               iter->videoFrame[FRAME_PREV]= iter->videoFrame[FRAME_CURR];
               iter->videoFrame[FRAME_CURR]= iter->videoFrame[FRAME_NEXT];

               iter->videoFrame[FRAME_NEXT].fbId= 0;
               iter->videoFrame[FRAME_NEXT].handle0= 0;
               iter->videoFrame[FRAME_NEXT].handle1= 0;
               iter->videoFrame[FRAME_NEXT].fd0= -1;
               iter->videoFrame[FRAME_NEXT].fd1= -1;
               iter->videoFrame[FRAME_NEXT].fd2= -1;
               iter->videoFrame[FRAME_NEXT].hide= false;
               iter->videoFrame[FRAME_NEXT].hidden= false;
               iter->videoFrame[FRAME_NEXT].vf= 0;

               iter->plane->crtc_id= ctx->overlayPlanes.primary->crtc_id;

               sw= frameWidth;
               sh= frameHeight;
               dw= rectW;
               dh= rectH;
               sx= 0;
               dx= rectX;
               if ( rectX < 0 )
               {
                  sx= -rectX*frameWidth/rectW;
                  sw -= sx;
                  dx= 0;
                  dw += rectX;
               }
               sy= 0;
               dy= rectY;
               if ( rectY < 0 )
               {
                  sy= -rectY*frameHeight/rectH;
                  sh -= sy;
                  dy= 0;
                  dh += rectY;
               }

               TRACE3("%dx%d %d,%d,%d,%d : s(%d,%d,%d,%d) d(%d,%d,%d,%d)",
                       frameWidth, frameHeight,
                       rectX, rectY, rectW, rectH,
                       sx, sy, sw, sh,
                       dx, dy, dw, dh );

               /* Adjust video target rect based on output resolution.  Sink will be working
                  with graphics resolution coordinates which may differ from output resolution */
               modeWidth= ctx->modeInfo->hdisplay;
               modeHeight= ctx->modeInfo->vdisplay;
               gfxWidth= ctx->nwFirst ? ctx->nwFirst->width : modeWidth;
               gfxHeight= ctx->nwFirst ? ctx->nwFirst->height : modeWidth;

               if ( (gfxWidth != modeWidth) || (gfxHeight != modeHeight) )
               {
                  dx= dx*modeWidth/gfxWidth;
                  dy= dy*modeHeight/gfxHeight;
                  dw= dw*modeWidth/gfxWidth;
                  dh= dh*modeHeight/gfxHeight;

                  TRACE3("m %dx%d g %dx%d d(%d,%d,%d,%d)",
                          modeWidth, modeHeight,
                          gfxWidth, gfxHeight,
                          dx, dy, dw, dh );
               }

               if ( ctx->outputEnable && ctx->videoEnable )
               {
                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "FB_ID", iter->videoFrame[FRAME_CURR].fbId );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "CRTC_ID", iter->plane->crtc_id );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "SRC_X", sx<<16 );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "SRC_Y", sy<<16 );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "SRC_W", sw<<16 );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "SRC_H", sh<<16 );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "CRTC_X", dx );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "CRTC_Y", dy );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "CRTC_W", dw );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "CRTC_H", dh );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "IN_FENCE_FD", -1 );

                  FRAME("commit frame %d buffer %d", iter->videoFrame[FRAME_CURR].frameNumber, iter->videoFrame[FRAME_CURR].bufferId);
               }
            }
            else if ( iter->videoFrame[FRAME_NEXT].hide && !iter->videoFrame[FRAME_NEXT].hidden )
            {
               iter->videoFrame[FRAME_FREE]= iter->videoFrame[FRAME_PREV];
               iter->videoFrame[FRAME_PREV]= iter->videoFrame[FRAME_CURR];

               if ( iter->videoFrame[FRAME_CURR].fbId != 0 )
               {
                  iter->plane->crtc_id= ctx->overlayPlanes.primary->crtc_id;
                  DEBUG("hiding video plane %d", iter->plane->plane_id);

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "FB_ID", 0 );

                  wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                        iter->planeProps->count_props, iter->planePropRes,
                                        "CRTC_ID", 0 );
               }

               iter->videoFrame[FRAME_CURR].fbId= 0;
               iter->videoFrame[FRAME_CURR].handle0= 0;
               iter->videoFrame[FRAME_CURR].handle1= 0;
               iter->videoFrame[FRAME_CURR].fd0= -1;
               iter->videoFrame[FRAME_CURR].fd1= -1;
               iter->videoFrame[FRAME_CURR].fd2= -1;
               iter->videoFrame[FRAME_CURR].bufferId= -1;
               iter->videoFrame[FRAME_CURR].vf= 0;
               if ( iter->videoFrame[FRAME_PREV].fbId )
               {
                  ctx->dirty= true;
                  iter->dirty= true;
               }
               else
               {
                  iter->videoFrame[FRAME_NEXT].hide= false;
                  iter->videoFrame[FRAME_NEXT].hidden= true;
               }
            }
         }

         iter= iter->next;
      }
   }
   pthread_mutex_unlock( &ctx->mutex );

   rc= drmModeAtomicCommit( ctx->drmFd, req, flags, 0 );
   if ( rc )
   {
      ERROR("drmModeAtomicCommit failed: rc %d errno %d", rc, errno );
   }

   if ( flags & DRM_MODE_ATOMIC_ALLOW_MODESET )
   {
      ctx->modeSet= true;
   }

   #ifdef DRM_USE_OUT_FENCE
   if ( ctx->nativeOutputFenceFd >= 0 )
   {
      int rc;
      struct pollfd pfd;

      TRACE3("waiting on out fence fd %d", ctx->nativeOutputFenceFd);
      pfd.fd= ctx->nativeOutputFenceFd;
      pfd.events= POLLIN;
      pfd.revents= 0;

      for( ; ; )
      {
         rc= poll( &pfd, 1, 3000);
         if ( (rc == -1) && ((errno == EINTR) || (errno == EAGAIN)) )
         {
            continue;
         }
         else if ( rc <= 0 )
         {
            if ( rc == 0 ) errno= ETIME;
            ERROR("drmModeAtomicCommit: wait out fence failed: fd %d errno %d", ctx->nativeOutputFenceFd, errno);
         }
         break;
      }
      close( ctx->nativeOutputFenceFd );
      ctx->nativeOutputFenceFd= -1;
   }
   #endif

exit:

   TRACE3("wstSwapDRMBuffersAtomic: atomic stop");
   if ( req )
   {
      drmModeAtomicFree( req );
   }

   return;
}

static void wstSwapDRMBuffers( WstGLCtx *ctx )
{
   struct gbm_surface* gs;
   struct gbm_bo *bo;
   uint32_t handle, stride;
   fd_set fds;
   drmEventContext ev;
   drmModePlane *plane= 0;
   int rc;
   bool eventPending= false;
   NativeWindowItem *nw;

   if ( !ctx->conn )
   {
      wstUpdateCtx( ctx );
      if ( !ctx->conn )
      {
         TRACE3("wstSwapDRMBuffers: no connector");
         ctx->dirty= true;
         goto exit;
      }
   }

   if ( ctx->modeSetPending )
   {
      ctx->notifySizeChange= true;
      ctx->modeCurrent= ctx->modeNext;
      ctx->modeSetPending= false;
      ctx->modeSet= false;
   }

   if ( ctx->haveAtomic )
   {
      wstSwapDRMBuffersAtomic( ctx );
      goto done;
   }

   if ( gCtx )
   {
      nw= gCtx->nwFirst;
      while( nw )
      {
         if ( nw->dirty )
         {
            TRACE3("nw %p dirty", nw);
            nw->prevBo= nw->bo;
            nw->prevFbId= nw->fbId;
            gs= (struct gbm_surface*)nw->nativeWindow;
            if ( gs )
            {
               bo= gbm_surface_lock_front_buffer(gs);
               wstUpdateResources( WSTRES_BO_GRAPHICS, true, (long long)bo, __LINE__);

               handle= gbm_bo_get_handle(bo).u32;
               stride= gbm_bo_get_stride(bo);

               rc= drmModeAddFB( ctx->drmFd,
                                 ctx->modeInfo->hdisplay,
                                 ctx->modeInfo->vdisplay,
                                 32,
                                 32,
                                 stride,
                                 handle,
                                 &nw->fbId );
                if ( rc )
                {
                   ERROR("wstSwapDRMBuffers: drmModeAddFB rc %d errno %d", rc, errno);
                   goto exit;
                }
                wstUpdateResources( WSTRES_FB_GRAPHICS, true, nw->fbId, __LINE__);
                nw->bo= bo;

               if ( !ctx->modeSet )
               {
                  rc= drmModeSetCrtc( ctx->drmFd,
                                      ctx->enc->crtc_id,
                                      nw->fbId,
                                      0,
                                      0,
                                      &gCtx->conn->connector_id,
                                      1,
                                      gCtx->modeInfo );
                   if ( rc )
                   {
                      ERROR("wstSwapDRMBuffers: drmModeSetCrtc: rc %d errno %d", rc, errno);
                      goto exit;
                   }
                   ctx->modeSet= true;
               }
               else if ( nw->windowPlane )
               {
                  FD_ZERO(&fds);
                  memset(&ev, 0, sizeof(ev));

                  rc= drmModePageFlip( ctx->drmFd,
                                       ctx->enc->crtc_id,
                                       0, //fbid
                                       DRM_MODE_PAGE_FLIP_EVENT,
                                       ctx );
                  if ( !rc )
                  {
                     ctx->flipPending++;
                     eventPending= true;
                  }

                  plane= nw->windowPlane->plane;
                  plane->crtc_id= ctx->enc->crtc_id;
                  rc= drmModeSetPlane( ctx->drmFd,
                                       plane->plane_id,
                                       plane->crtc_id,
                                       nw->fbId,
                                       0,
                                       0, // plane x
                                       0, // plane y
                                       ctx->modeInfo->hdisplay,
                                       ctx->modeInfo->vdisplay,
                                       0, // fb rect x
                                       0, // fb rect y
                                       ctx->modeInfo->hdisplay<<16,
                                       ctx->modeInfo->vdisplay<<16 );
                  if ( rc )
                  {
                     ERROR("wstSwapDRMBuffers: drmModeSetPlane rc %d errno %d", rc, errno );
                  }
               }
               else
               {
                  FD_ZERO(&fds);
                  memset(&ev, 0, sizeof(ev));

                  rc= drmModePageFlip( ctx->drmFd,
                                       ctx->enc->crtc_id,
                                       nw->fbId,
                                       DRM_MODE_PAGE_FLIP_EVENT,
                                       ctx );
                  if ( !rc )
                  {
                     ctx->flipPending++;
                     eventPending= true;
                  }
               }
            }
         }
         nw= nw->next;
      }
   }

   {
      pthread_mutex_lock( &ctx->mutex );
      if ( ctx->overlayPlanes.usedCount )
      {
         WstOverlayPlane *iter= ctx->overlayPlanes.usedHead;
         while( iter )
         {
            if ( iter->dirty && iter->readyToFlip )
            {
               if ( iter->videoFrame[FRAME_NEXT].fbId )
               {
                  uint32_t fbId= iter->videoFrame[FRAME_NEXT].fbId;
                  uint32_t handle0= iter->videoFrame[FRAME_NEXT].handle0;
                  uint32_t handle1= iter->videoFrame[FRAME_NEXT].handle1;
                  int fd0= iter->videoFrame[FRAME_NEXT].fd0;
                  int fd1= iter->videoFrame[FRAME_NEXT].fd1;
                  int fd2= iter->videoFrame[FRAME_NEXT].fd2;
                  uint32_t frameWidth= iter->videoFrame[FRAME_NEXT].frameWidth;
                  uint32_t frameHeight= iter->videoFrame[FRAME_NEXT].frameHeight;
                  int rectX= iter->videoFrame[FRAME_NEXT].rectX;
                  int rectY= iter->videoFrame[FRAME_NEXT].rectY;
                  int rectW= iter->videoFrame[FRAME_NEXT].rectW;
                  int rectH= iter->videoFrame[FRAME_NEXT].rectH;
                  int bufferId= iter->videoFrame[FRAME_NEXT].bufferId;
                  uint32_t sx, sy, sw, sh, dx, dy, dw, dh;
                  int modeWidth, modeHeight, gfxWidth, gfxHeight;

                  iter->videoFrame[FRAME_FREE]= iter->videoFrame[FRAME_PREV];
                  iter->videoFrame[FRAME_PREV]= iter->videoFrame[FRAME_CURR];
                  iter->videoFrame[FRAME_CURR]= iter->videoFrame[FRAME_NEXT];

                  iter->videoFrame[FRAME_NEXT].fbId= 0;
                  iter->videoFrame[FRAME_NEXT].handle0= 0;
                  iter->videoFrame[FRAME_NEXT].handle1= 0;
                  iter->videoFrame[FRAME_NEXT].fd0= -1;
                  iter->videoFrame[FRAME_NEXT].fd1= -1;
                  iter->videoFrame[FRAME_NEXT].fd2= -1;
                  iter->videoFrame[FRAME_NEXT].hide= false;
                  iter->videoFrame[FRAME_NEXT].hidden= false;

                  sw= frameWidth;
                  sh= frameHeight;
                  dw= rectW;
                  dh= rectH;
                  sx= 0;
                  dx= rectX;
                  if ( rectX < 0 )
                  {
                     sx= -rectX*frameWidth/rectW;
                     sw -= sx;
                     dx= 0;
                     dw += rectX;
                  }
                  sy= 0;
                  dy= rectY;
                  if ( rectY < 0 )
                  {
                     sy= -rectY*frameHeight/rectH;
                     sh -= sy;
                     dy= 0;
                     dh += rectY;
                  }
                  TRACE3("%dx%d %d,%d,%d,%d : s(%d,%d,%d,%d) d(%d,%d,%d,%d)",
                          frameWidth, frameHeight,
                          rectX, rectY, rectW, rectH,
                          sx, sy, sw, sh,
                          dx, dy, dw, dh );

                  /* Adjust video target rect based on output resolution.  Sink will be working
                     with graphics resolution coordinates which may differ from output resolution */
                  modeWidth= ctx->modeInfo->hdisplay;
                  modeHeight= ctx->modeInfo->vdisplay;
                  gfxWidth= ctx->nwFirst ? ctx->nwFirst->width : modeWidth;
                  gfxHeight= ctx->nwFirst ? ctx->nwFirst->height : modeWidth;

                  if ( (gfxWidth != modeWidth) || (gfxHeight != modeHeight) )
                  {
                     dx= dx*modeWidth/gfxWidth;
                     dy= dy*modeHeight/gfxHeight;
                     dw= dw*modeWidth/gfxWidth;
                     dh= dh*modeHeight/gfxHeight;

                     TRACE3("m %dx%d g %dx%d d(%d,%d,%d,%d)",
                             modeWidth, modeHeight,
                             gfxWidth, gfxHeight,
                             dx, dy, dw, dh );
                  }

                  plane= iter->plane;
                  plane->crtc_id= ctx->enc->crtc_id;
                  rc= drmModeSetPlane( ctx->drmFd,
                                       plane->plane_id,
                                       plane->crtc_id,
                                       fbId,
                                       0,
                                       dx,
                                       dy,
                                       dw,
                                       dh,
                                       sx<<16, // fb rect x
                                       sy<<16, // fb rect y
                                       sw<<16,
                                       sh<<16 );
                  if ( !rc )
                  {
                     iter->videoFrame[FRAME_CURR].fbId= fbId;
                     iter->videoFrame[FRAME_CURR].handle0= handle0;
                     iter->videoFrame[FRAME_CURR].handle1= handle1;
                     iter->videoFrame[FRAME_CURR].fd0= fd0;
                     iter->videoFrame[FRAME_CURR].fd1= fd1;
                     iter->videoFrame[FRAME_CURR].fd2= fd2;
                     iter->videoFrame[FRAME_CURR].bufferId= bufferId;
                  }
                  else
                  {
                     wstUpdateResources( WSTRES_FB_VIDEO, false, fbId, __LINE__);
                     drmModeRmFB( gCtx->drmFd, fbId );
                     wstClosePrimeFDHandles( ctx, handle0, handle1, __LINE__ );
                     if ( fd0 >= 0 )
                     {
                        wstUpdateResources( WSTRES_FD_VIDEO, false, fd0, __LINE__);
                        close( fd0 );
                        if ( fd1 >= 0 )
                        {
                           close( fd1 );
                        }
                        if ( fd2 >= 0 )
                        {
                           close( fd2 );
                        }
                     }
                     ERROR("wstSwapDRMBuffers: drmModeSetPlane rc %d errno %d", rc, errno );
                  }
               }
               else if ( iter->videoFrame[FRAME_NEXT].hide && !iter->videoFrame[FRAME_NEXT].hidden )
               {
                  iter->videoFrame[FRAME_FREE]= iter->videoFrame[FRAME_PREV];
                  iter->videoFrame[FRAME_PREV]= iter->videoFrame[FRAME_CURR];

                  plane= iter->plane;
                  plane->crtc_id= ctx->enc->crtc_id;
                  DEBUG("hiding video plane %d", iter->plane->plane_id);
                  rc= drmModeSetPlane( ctx->drmFd,
                                       plane->plane_id,
                                       plane->crtc_id,
                                       0, // fbid
                                       0, // flags
                                       0, // plane x
                                       0, // plane y
                                       ctx->modeInfo->hdisplay,
                                       ctx->modeInfo->vdisplay,
                                       0, // fb rect x
                                       0, // fb rect y
                                       ctx->modeInfo->hdisplay<<16,
                                       ctx->modeInfo->vdisplay<<16 );
                  if ( rc )
                  {
                     ERROR("wstSwapDRMBuffers: hiding plane: drmModeSetPlane rc %d errno %d", rc, errno );
                  }
                  iter->videoFrame[FRAME_CURR].fbId= 0;
                  iter->videoFrame[FRAME_CURR].handle0= 0;
                  iter->videoFrame[FRAME_CURR].handle1= 0;
                  iter->videoFrame[FRAME_CURR].fd0= -1;
                  iter->videoFrame[FRAME_CURR].fd1= -1;
                  iter->videoFrame[FRAME_CURR].fd2= -1;
                  iter->videoFrame[FRAME_CURR].bufferId= -1;
                  iter->videoFrame[FRAME_NEXT].hide= true;
                  iter->videoFrame[FRAME_NEXT].hidden= true;
               }
            }

            iter= iter->next;
         }
      }
      pthread_mutex_unlock( &ctx->mutex );
   }

   if ( eventPending )
   {
      ev.version= 2;
      ev.page_flip_handler= pageFlipEventHandler;
      FD_SET(0, &fds);
      FD_SET(ctx->drmFd, &fds);
      rc= select( ctx->drmFd+1, &fds, NULL, NULL, NULL );
      if ( rc >= 0 )
      {
         if ( FD_ISSET(ctx->drmFd, &fds) )
         {
            drmHandleEvent(ctx->drmFd, &ev);
         }
      }
   }

done:
   if ( gCtx )
   {
      nw= gCtx->nwFirst;
      while( nw )
      {
         if ( nw->dirty )
         {
            nw->dirty= false;
            TRACE3("nw %p dirty, set dirty false", nw);
         }

         nw= nw->next;
      }
   }

   pthread_mutex_lock( &ctx->mutex );
   if ( ctx->overlayPlanes.usedCount )
   {
      WstOverlayPlane *iter= ctx->overlayPlanes.usedHead;
      while( iter )
      {
         if ( (iter->dirty && iter->readyToFlip && !iter->videoFrame[FRAME_NEXT].hide) || iter->videoFrame[FRAME_NEXT].hidden )
         {
            iter->dirty= false;
            iter->readyToFlip= false;
            iter->videoFrame[FRAME_NEXT].hidden= false;
         }
         iter= iter->next;
      }
   }
   pthread_mutex_unlock( &ctx->mutex );

   if ( gCtx && gCtx->notifySizeChange )
   {
      gCtx->notifySizeChange= false;
      pthread_mutex_unlock( &gMutex );
      wstGLNotifySizeListeners();
      pthread_mutex_lock( &gMutex );
   }

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
         printf("westeros_gl: fps %f\n", fps);
         lastReportTime= now;
         frameCount= 0;
      }
   }

exit:

   return;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType displayId)
{
   EGLDisplay eglDisplay= EGL_NO_DISPLAY;

   DEBUG("westeros-gl: eglGetDisplay: enter: displayId %x", displayId);

   if ( !gCtx )
   {
      gCtx= WstGLInit();
   }

   if ( !gRealEGLGetDisplay )
   {
      ERROR("westeros-gl: eglGetDisplay: failed linkage to underlying EGL impl" );
      goto exit;
   }

   if ( displayId == EGL_DEFAULT_DISPLAY )
   {
      if ( gCtx->gbm )
      {
         PFNEGLGETPLATFORMDISPLAYEXTPROC realEGLGetPlatformDisplay= 0;
         realEGLGetPlatformDisplay= (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress( "eglGetPlatformDisplayEXT" );
         if ( realEGLGetPlatformDisplay )
         {
            gCtx->dpy= realEGLGetPlatformDisplay( EGL_PLATFORM_GBM_KHR, gCtx->gbm, NULL );
         }
         else
         {
            gCtx->dpy= gRealEGLGetDisplay( (NativeDisplayType)gCtx->gbm );
         }
      }
   }
   else
   {
      gCtx->dpy = gRealEGLGetDisplay(displayId);
   }

   if ( gCtx->dpy )
   {
      eglDisplay= gCtx->dpy;
   }

exit:

   return eglDisplay;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface( EGLDisplay dpy, EGLConfig config,
                                                      EGLNativeWindowType win,
                                                      const EGLint *attrib_list )
{
   EGLSurface eglSurface= EGL_NO_SURFACE;

   if ( gRealEGLCreateWindowSurface )
   {
      eglSurface= gRealEGLCreateWindowSurface( dpy, config, win, attrib_list );
      if ( eglSurface != EGL_NO_SURFACE )
      {
         pthread_mutex_lock( &gMutex );
         if ( gCtx )
         {
            #ifdef DRM_USE_NATIVE_FENCE
            const char *extensions;
            extensions= eglQueryString( gCtx->dpy, EGL_EXTENSIONS );
            if ( extensions )
            {
               if ( strstr( extensions, "EGL_ANDROID_native_fence_sync" ) &&
                    strstr( extensions, "EGL_KHR_wait_sync" ) &&
                    (gVideoServer == 0 ) &&
                    gRealEGLCreateSyncKHR &&
                    gRealEGLDestroySyncKHR &&
                    gRealEGLClientWaitSyncKHR &&
                    gRealEGLWaitSyncKHR )
               {
                  gCtx->haveNativeFence= true;
                  INFO("westeros-gl: have native fence");
               }
            }
            #endif

            NativeWindowItem *nwIter= gCtx->nwFirst;
            while( nwIter )
            {
               if ( nwIter->nativeWindow == win )
               {
                  nwIter->surface= eglSurface;
                  break;
               }
               nwIter= nwIter->next;
            }
         }
         pthread_mutex_unlock( &gMutex );
      }
   }

exit:

   return eglSurface;
}

EGLAPI EGLBoolean eglSwapBuffers( EGLDisplay dpy, EGLSurface surface )
{
   EGLBoolean result= EGL_FALSE;
   NativeWindowItem *nwIter;

   if ( gRealEGLSwapBuffers )
   {
      #ifdef DRM_USE_NATIVE_FENCE
      if ( gCtx->nativeOutputFenceFd >= 0 )
      {
         EGLint attrib[3];
         attrib[0]= EGL_SYNC_NATIVE_FENCE_FD_ANDROID;
         attrib[1]= gCtx->nativeOutputFenceFd;
         attrib[2]= EGL_NONE;
         gCtx->fenceSync= gRealEGLCreateSyncKHR( dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, attrib );
         if ( gCtx->fenceSync )
         {
            TRACE2("fenceSync %p created for nativeOutputFenceFd %d", gCtx->fenceSync, gCtx->nativeOutputFenceFd);
            gCtx->nativeOutputFenceFd= -1;
            gRealEGLWaitSyncKHR( dpy, gCtx->fenceSync, 0);
         }
         else
         {
            ERROR("failed to create fenceSync for nativeOutputFenceFd %d", gCtx->nativeOutputFenceFd);
         }
      }
      #endif
      result= gRealEGLSwapBuffers( dpy, surface );
      if ( EGL_TRUE == result )
      {
         if ( gCtx )
         {
            pthread_mutex_lock( &gMutex );
            nwIter= gCtx->nwFirst;
            while( nwIter )
            {
               if ( nwIter->surface == surface )
               {
                  gCtx->dirty= true;
                  nwIter->dirty= true;
                  TRACE3("mark nw %p dirty", nwIter);
                  break;
               }
               nwIter= nwIter->next;
           }
           pthread_mutex_unlock( &gMutex );
         }
      }
   }

exit:

   return result;
}

WstGLCtx* WstGLInit()
{
   WstGLCtx *ctx= 0;

   const char *env= getenv( "WESTEROS_GL_DEBUG" );
   if ( env )
   {
      int level= atoi( env );
      g_activeLevel= level;
   }

   env= getenv( "WESTEROS_GL_DEBUG_FRAME" );
   if ( env )
   {
      int level= atoi( env );
      g_frameDebug= (level > 0 ? true : false);
   }

   /*
    *  Establish the overloading of a subset of EGL methods
    */
   if ( !gRealEGLGetDisplay )
   {
      gRealEGLGetDisplay= (PREALEGLGETDISPLAY)dlsym( RTLD_NEXT, "eglGetDisplay" );
      DEBUG("westeros-gl: wstGLInit: realEGLGetDisplay=%p", (void*)gRealEGLGetDisplay );
      if ( !gRealEGLGetDisplay )
      {
         ERROR("westeros-gl: wstGLInit: unable to resolve eglGetDisplay");
         goto exit;
      }
   }

   if ( !gRealEGLCreateWindowSurface )
   {
      gRealEGLCreateWindowSurface= (PREALEGLCREATEWINDOWSURFACE)dlsym( RTLD_NEXT, "eglCreateWindowSurface" );
      DEBUG("westeros-gl: wstGLInit: realEGLCreateWindowSurface=%p", (void*)gRealEGLCreateWindowSurface );
      if ( !gRealEGLCreateWindowSurface )
      {
         ERROR("westeros-gl: wstGLInit: unable to resolve eglCreateWindowSurface");
         goto exit;
      }
   }

   if ( !gRealEGLSwapBuffers )
   {
      gRealEGLSwapBuffers= (PREALEGLSWAPBUFFERS)dlsym( RTLD_NEXT, "eglSwapBuffers" );
      DEBUG("westeros-gl: wstGLInit: realEGLSwapBuffers=%p", (void*)gRealEGLSwapBuffers );
      if ( !gRealEGLSwapBuffers )
      {
         ERROR("westeros-gl: eglSwapBuffers: unable to resolve eglSwapBuffers");
         goto exit;
      }
   }

   #ifdef DRM_USE_NATIVE_FENCE
   if ( !gRealEGLCreateSyncKHR )
   {
      gRealEGLCreateSyncKHR= (PREALEGLCREATESYNCKHR)eglGetProcAddress( "eglCreateSyncKHR" );
      DEBUG("westeros-gl: wstGLInit: eglCreateSyncKHR=%p", (void*)gRealEGLCreateSyncKHR );
   }
   if ( !gRealEGLDestroySyncKHR )
   {
      gRealEGLDestroySyncKHR= (PREALEGLDESTROYSYNCKHR)eglGetProcAddress( "eglDestroySyncKHR" );
      DEBUG("westeros-gl: wstGLInit: eglDestroySyncKHR=%p", (void*)gRealEGLDestroySyncKHR );
   }
   if ( !gRealEGLClientWaitSyncKHR )
   {
      gRealEGLClientWaitSyncKHR= (PREALEGLCLIENTWAITSYNCKHR)eglGetProcAddress( "eglClientWaitSyncKHR" );
      DEBUG("westeros-gl: wstGLInit: eglClientWaitSyncKHR=%p", (void*)gRealEGLClientWaitSyncKHR );
   }
   if ( !gRealEGLWaitSyncKHR )
   {
      gRealEGLWaitSyncKHR= (PREALEGLWAITSYNCKHR)eglGetProcAddress( "eglWaitSyncKHR" );
      DEBUG("westeros-gl: wstGLInit: eglWaitSyncKHR=%p", (void*)gRealEGLWaitSyncKHR );
   }
   #endif

   pthread_mutex_lock( &gMutex );
   if( gCtx != NULL )
   {
      ++gCtx->refCnt;
      ctx= gCtx;
   }
   if ( !ctx )
   {
      ctx= wstInitCtx();
      if ( ctx )
      {
         gCtx= ctx;
      }
   }
   pthread_mutex_unlock( &gMutex );

exit:

   return ctx;
}

void WstGLTerm( WstGLCtx *ctx )
{
   if ( ctx )
   {
      pthread_mutex_lock( &gMutex );
      if ( ctx != gCtx )
      {
         ERROR("westeros-gl: WstGLTerm: bad ctx %p, should be %p", ctx, gCtx );
         pthread_mutex_unlock( &gMutex );
         return;
      }

      --ctx->refCnt;
      if ( ctx->refCnt <= 0 )
      {
         wstTermCtx( ctx );

         gCtx= 0;
      }
      pthread_mutex_unlock( &gMutex );
   }
}

bool _WstGLGetDisplayCaps( WstGLCtx *ctx, unsigned int *caps )
{
   return WstGLGetDisplayCaps( ctx, caps );
}

bool _WstGLSetDisplayMode( WstGLCtx *ctx, const char *mode )
{
   return WstGLSetDisplayMode( ctx, mode );
}

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

bool WstGLGetDisplayCaps( WstGLCtx *ctx, unsigned int *caps )
{
   bool result= false;

   if ( ctx && caps )
   {
      unsigned int displayCaps= WstGLDisplayCap_modeset;

      *caps= displayCaps;

      result= true;
   }

   return result;
}

bool WstGLSetDisplayMode( WstGLCtx *ctx, const char *mode )
{
   bool result= false;

   if ( ctx && mode )
   {
      int width= -1, height= -1, rate= -1;
      bool interlaced= false;
      bool haveTarget= false;
      bool useBestRate= true;

      DEBUG("WstGLSetDisplayMode: mode (%s)", mode);

      if ( sscanf( mode, "%dx%dx%d", &width, &height, &rate ) == 3 )
      {
         interlaced= false;
      }
      else if ( sscanf( mode, "%dx%dp%d", &width, &height, &rate ) == 3 )
      {
         interlaced= false;
      }
      else if ( sscanf( mode, "%dx%di%d", &width, &height, &rate ) == 3 )
      {
         interlaced= true;
      }
      else if ( sscanf( mode, "%dx%d", &width, &height ) == 2 )
      {
         int len= strlen(mode);
         interlaced= (mode[len-1] == 'i');
      }
      else if ( sscanf( mode, "%dp", &height ) == 1 )
      {
         int len= strlen(mode);
         interlaced= (mode[len-1] == 'i');
         width= -1;
      }
      if ( height > 0 )
      {
         if ( width < 0 )
         {
            switch( height )
            {
               case 480:
               case 576:
                  width= 720;
                  break;
               case 720:
                  width= 1280;
                  break;
               case 1080:
                  width= 1920;
                  break;
               case 1440:
                  width= 2560;
                  break;
               case 2160:
                  width= 3840;
                  break;
               case 2880:
                  width= 5120;
                  break;
               case 4320:
                  width= 7680;
                  break;
               default:
                  break;
            }
         }
      }
      if ( rate >= 0 )
      {
         useBestRate= false;
      }
      if ( (width > 0) && (height > 0) )
      {
         if ( ctx->drmFd >= 0  )
         {
            drmModeRes *res= 0;
            drmModeConnector *conn= 0;

            res= drmModeGetResources( ctx->drmFd );
            if ( res )
            {
               int i;
               for( i= 0; i < res->count_connectors; ++i )
               {
                  conn= drmModeGetConnector( ctx->drmFd, res->connectors[i] );
                  if ( conn )
                  {
                     if ( conn->count_modes && (conn->connection == DRM_MODE_CONNECTED) )
                     {
                        break;
                     }
                     drmModeFreeConnector(conn);
                     conn= 0;
                  }
               }
               if ( conn )
               {
                  uint32_t rateBest= 0;
                  int miBest= -1;

                  DEBUG("wstGLSetDisplayMode: want %dx%dx%d interlaced %d use best rate %d", width, height, rate, interlaced, useBestRate);
                  for( i= 0; i < conn->count_modes; ++i )
                  {
                     DEBUG("wstGLSetDisplayMode: consider mode %d: %dx%dx%d (%s) type 0x%x flags 0x%x",
                            i, conn->modes[i].hdisplay, conn->modes[i].vdisplay, conn->modes[i].vrefresh,
                            conn->modes[i].name, conn->modes[i].type, conn->modes[i].flags );

                     if ( (conn->modes[i].hdisplay == width) &&
                          (conn->modes[i].vdisplay == height) )
                     {
                        bool modeIsInterlaced= (conn->modes[i].flags & DRM_MODE_FLAG_INTERLACE);
                        if ( modeIsInterlaced != interlaced )
                        {
                           continue;
                        }
                        if ( useBestRate )
                        {
                           if ( conn->modes[i].vrefresh > rateBest )
                           {
                              rateBest= conn->modes[i].vrefresh;
                              miBest= i;
                           }
                        }
                        else if ( conn->modes[i].vrefresh == rate )
                        {
                           miBest= i;
                           break;
                        }
                     }
                  }
                  if ( miBest >= 0 )
                  {
                     pthread_mutex_lock( &ctx->mutex );
                     ctx->modeNext= conn->modes[miBest];
                     ctx->usingSetDisplayMode= true;
                     ctx->modeSetPending= true;
                     pthread_mutex_unlock( &ctx->mutex );

                     INFO("WstGLSetDisplayMode: choosing output mode: %dx%dx%d (%s) flags 0x%x",
                           ctx->modeNext.hdisplay,
                           ctx->modeNext.vdisplay,
                           ctx->modeNext.vrefresh,
                           ctx->modeNext.name,
                           ctx->modeNext.flags );

                     result= true;
                  }
                  else
                  {
                     ERROR("WstGLSetDisplayMode: failed to find a mode matching (%s)", mode);
                  }

                  drmModeFreeConnector( conn );
               }
               else
               {
                  ERROR("wstGLSetDisplayMode: unable to get connector for card");
               }
            }
            else
            {
               ERROR("WstGLSetDisplayMode: unable to get card resources");
            }
         }
         else
         {
            ERROR("WstGLSetDisplayMode: no open device");
         }
      }
      else
      {
         ERROR("WstGLSetDisplayMode: unable to parse mode (%s)", mode);
      }
   }

   return result;
}

bool WstGLGetDisplayInfo( WstGLCtx *ctx, WstGLDisplayInfo *displayInfo )
{
   bool result= false;

   if ( ctx && displayInfo )
   {
      pthread_mutex_lock( &gMutex );

      displayInfo->width= ctx->modeCurrent.hdisplay;
      displayInfo->height= ctx->modeCurrent.vdisplay;

      WSTGL_CHECK_GRAPHICS_SIZE( displayInfo->width, displayInfo->height );

      /* Use the SMPTE ST 2046-1 5% safe area border */
      displayInfo->safeArea.x= displayInfo->width*DISPLAY_SAFE_BORDER_PERCENT/100;
      displayInfo->safeArea.y= displayInfo->height*DISPLAY_SAFE_BORDER_PERCENT/100;
      displayInfo->safeArea.w= displayInfo->width - 2*displayInfo->safeArea.x;
      displayInfo->safeArea.h= displayInfo->height - 2*displayInfo->safeArea.y;

      result= true;

      pthread_mutex_unlock( &gMutex );
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
      WstGLSizeCBInfo *iter, *prev;

      pthread_mutex_lock( &gMutex );
      prev= 0;
      iter= gSizeListeners;
      while ( iter )
      {
         if ( iter->listener == listener )
         {
            found= true;
            break;
         }
         prev= iter;
         iter= iter->next;
      }
      if ( !found )
      {
         WstGLSizeCBInfo *newInfo= (WstGLSizeCBInfo*)calloc( 1, sizeof(WstGLSizeCBInfo) );
         if ( newInfo )
         {
            newInfo->ctx= ctx;
            newInfo->userData= userData;
            newInfo->listener= listener;
            newInfo->width= 0;
            newInfo->height= 0;

            if ( prev )
            {
               prev->next= newInfo;
            }
            else
            {
               gSizeListeners= newInfo;
            }

            result= true;
         }
         else
         {
            ERROR("No memory for new display size listener");
         }
      }
      pthread_mutex_unlock( &gMutex );
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
      WstGLSizeCBInfo *iter, *prev;

      pthread_mutex_lock( &gMutex );

      prev= 0;
      iter= gSizeListeners;
      while ( iter )
      {
         if ( iter->listener == listener )
         {
            found= true;
            if ( prev )
            {
               prev->next= iter->next;
            }
            else
            {
               gSizeListeners= iter->next;
            }
            free( iter );
            break;
         }
         prev= iter;
         iter= iter->next;
      }

      if ( found )
      {
         result= true;
      }

      pthread_mutex_unlock( &gMutex );
   }

   return result;
}

void* WstGLCreateNativeWindow( WstGLCtx *ctx, int x, int y, int width, int height )
{
   void *nativeWindow= 0;
   NativeWindowItem *nwItem= 0;
   bool modeSetPending= false;

   if ( ctx )
   {
      INFO("native window: wxh=%dx%d", width, height);

      WSTGL_CHECK_GRAPHICS_SIZE( width, height );

      if ( !ctx->modeSet && !ctx->modeSetPending && ctx->conn )
      {
         wstSelectMode( ctx, width, height );
         modeSetPending= true;
      }

      nwItem= (NativeWindowItem*)calloc( 1, sizeof(NativeWindowItem) );
      if ( nwItem )
      {
         nativeWindow= gbm_surface_create(ctx->gbm,
                                          width, height,
                                          GBM_FORMAT_ARGB8888,
                                          GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING );
         if ( nativeWindow )
         {
            nwItem->nativeWindow= nativeWindow;
            nwItem->width= width;
            nwItem->height= height;

            if ( ctx->usePlanes && !ctx->graphicsPreferPrimary )
            {
               nwItem->windowPlane= wstOverlayAlloc( &ctx->overlayPlanes, true );
               INFO("plane %p : zorder: %d", nwItem->windowPlane, (nwItem->windowPlane ? nwItem->windowPlane->zOrder: -1) );
            }
            else if ( ctx->haveAtomic )
            {
               nwItem->windowPlane= wstOverlayAllocPrimary( &ctx->overlayPlanes );
               INFO("plane %p : primary: zorder: %d", nwItem->windowPlane, (nwItem->windowPlane ? nwItem->windowPlane->zOrder: -1) );
            }

            if ( !nwItem->windowPlane )
            {
               ctx->haveAtomic= false;
            }

            pthread_mutex_lock( &gMutex );
            if ( ctx->nwFirst )
            {
               nwItem->next= ctx->nwFirst;
               ctx->nwFirst= nwItem;
            }
            else
            {
               ctx->nwFirst= ctx->nwLast= nwItem;
            }
            if ( modeSetPending )
            {
               ctx->modeNext= ctx->modeCurrent;
               ctx->modeSetPending= true;
            }
            pthread_mutex_unlock( &gMutex );
         }
      }
   }

   {
      const char *mode= getenv("WESTEROS_GL_MODE");
      if ( mode )
      {
         WstGLSetDisplayMode( ctx, mode );
      }
   }

   return nativeWindow;
}

void WstGLDestroyNativeWindow( WstGLCtx *ctx, void *nativeWindow )
{
   if ( ctx )
   {
      NativeWindowItem *nwIter, *nwPrev;

      struct gbm_surface *gs= (struct gbm_surface*)nativeWindow;
      if ( gs )
      {
         pthread_mutex_lock( &gMutex );
         nwPrev= 0;
         nwIter= ctx->nwFirst;
         while ( nwIter )
         {
            if ( nwIter->nativeWindow == nativeWindow )
            {
               if ( nwIter->prevBo )
               {
                  gbm_surface_release_buffer(gs, nwIter->prevBo);
                  drmModeRmFB( ctx->drmFd, nwIter->prevFbId );
                  nwIter->prevBo= 0;
                  nwIter->prevFbId= 0;
               }
               nwIter->nativeWindow= 0;
               if ( nwIter->windowPlane )
               {
                  wstOverlayFree( &ctx->overlayPlanes, nwIter->windowPlane );
               }
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
               if ( !ctx->nwFirst && !ctx->usingSetDisplayMode )
               {
                  ctx->modeSet= false;
               }
               break;
            }
            else
            {
               nwPrev= nwIter;
            }
            nwIter= nwIter->next;
         }
         pthread_mutex_unlock( &gMutex );

         gbm_surface_destroy( gs );
      }
   }
}

bool WstGLGetNativePixmap( WstGLCtx *ctx, void *nativeBuffer, void **nativePixmap )
{
   bool result= false;

   if ( ctx )
   {
      /* Not yet required */
   }

   return result;
}

void WstGLGetNativePixmapDimensions( WstGLCtx *ctx, void *nativePixmap, int *width, int *height )
{
   if ( ctx )
   {
      /* Not yet required */
   }
}

void WstGLReleaseNativePixmap( WstGLCtx *ctx, void *nativePixmap )
{
   if ( ctx )
   {
      /* Not yet required */
   }
}

void* WstGLGetEGLNativePixmap( WstGLCtx *ctx, void *nativePixmap )
{
   void* eglPixmap= 0;

   if ( nativePixmap )
   {
      /* Not yet required */
   }

   return eglPixmap;
}
