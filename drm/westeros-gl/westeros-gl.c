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
#include <pthread.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
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

#define FATAL(...)                  INT_FATAL(__VA_ARGS__, "")
#define ERROR(...)                  INT_ERROR(__VA_ARGS__, "")
#define WARNING(...)                INT_WARNING(__VA_ARGS__, "")
#define INFO(...)                   INT_INFO(__VA_ARGS__, "")
#define DEBUG(...)                  INT_DEBUG(__VA_ARGS__, "")
#define TRACE1(...)                 INT_TRACE1(__VA_ARGS__, "")
#define TRACE2(...)                 INT_TRACE2(__VA_ARGS__, "")
#define TRACE3(...)                 INT_TRACE3(__VA_ARGS__, "")

#define DEFAULT_CARD "/dev/dri/card0"
#ifdef WESTEROS_PLATFORM_QEMUX86
#define DEFAULT_MODE_WIDTH (1280)
#define DEFAULT_MODE_HEIGHT (1024)
#endif

#define DISPLAY_SAFE_BORDER_PERCENT (5)

#define DRM_NO_SRC_CROP

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
typedef struct _WstOverlayPlane WstOverlayPlane;

typedef struct _VideoServerConnection
{
   pthread_mutex_t mutex;
   VideoServerCtx *server;
   WstOverlayPlane *videoPlane;
   int socketFd;
   int prevFrameFd0;
   int prevFrameFd1;
   int prevFrameFd2;
   pthread_t threadId;
   bool threadStarted;
   bool threadStopRequested;
} VideoServerConnection;

#define MAX_SUN_PATH (80)
#define MAX_VIDEO_CONNECTIONS (4)
typedef struct _VideoServerCtx
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
   VideoServerConnection* connections[MAX_VIDEO_CONNECTIONS];
} VideoServerCtx;

typedef struct _VideoFrame
{
   bool hide;
   bool hidden;
   uint32_t fbId;
   uint32_t handle0;
   uint32_t handle1;
   uint32_t frameWidth;
   uint32_t frameHeight;
   uint32_t frameFormat;
   int rectX;
   int rectY;
   int rectW;
   int rectH;
} VideoFrame;

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
   uint32_t fbId;
   uint32_t handle0;
   uint32_t handle1;
   uint32_t fbIdPrev;
   uint32_t handle0Prev;
   uint32_t handle1Prev;
   VideoFrame videoFrameNext;
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
   uint32_t handle;
   uint32_t fbId;
   struct gbm_bo *bo;
   struct gbm_bo *prevBo;
   uint32_t prevFbId;
   int width;
   int height;
} NativeWindowItem;

typedef struct _WstGLCtx
{
   pthread_mutex_t mutex;
   int refCnt;
   int drmFd;
   drmModeRes *res;
   drmModeConnector *conn;
   drmModeEncoder *enc;
   drmModeCrtc *crtc;
   drmModeModeInfo *modeInfo;
   drmModeModeInfo modeCurrent;
   drmModeModeInfo modeNext;
   WstOverlayPlanes overlayPlanes;
   struct gbm_device* gbm;
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
   int nativeOutputFenceFd;
   #endif
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

static void wstDestroyVideoServerConnection( VideoServerConnection *conn );
static void wstTermCtx( WstGLCtx *ctx );
static void wstSwapDRMBuffers( WstGLCtx *ctx, NativeWindowItem *nw );
static void wstSwapDRMBuffersAtomic( WstGLCtx *ctx, NativeWindowItem *nw );

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
static VideoServerCtx *gServer= 0;
static int gGraphicsMaxWidth= 0;
static int gGraphicsMaxHeight= 0;
static bool emitFPS= false;
static int g_activeLevel= 2;

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

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
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
   TRACE3("fdv %d hnv %d fbv %d bog %d fbg %d : v %llx line %d",
          gResources->fdVideoCount,
          gResources->handleVideoCount,
          gResources->fbVideoCount,
          gResources->boGraphicsCount,
          gResources->fbGraphicsCount,
          v, line
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
      pthread_mutex_lock( &gCtx->mutex );

      overlay->inUse= false;
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

static unsigned int wstGetU32( unsigned char *p )
{
   unsigned n;

   n= (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|(p[3]);

   return n;
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

static void *wstVideoServerConnectionThread( void *arg )
{
   VideoServerConnection *conn= (VideoServerConnection*)arg;
   struct msghdr msg;
   struct cmsghdr *cmsg;
   struct iovec iov[1];
   unsigned char mbody[1+13*4];
   char cmbody[CMSG_SPACE(3*sizeof(int))];
   int len, rc;
   uint32_t fbId= 0;
   uint32_t frameWidth, frameHeight;
   uint32_t frameFormat;
   uint32_t frameSkipX, frameSkipY, rectSkipX, rectSkipY;
   int rectX, rectY, rectW, rectH;
   int fd0, fd1, fd2;
   int offset0, offset1, offset2;
   int stride0, stride1, stride2;

   DEBUG("wstVideoServerConnectionThread: enter");

   conn->videoPlane= wstOverlayAlloc( &gCtx->overlayPlanes, false );
   INFO("video plane %p : zorder: %d", conn->videoPlane, (conn->videoPlane ? conn->videoPlane->zOrder: -1) );

   conn->threadStarted= true;
   while( !conn->threadStopRequested )
   {
      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= sizeof(mbody);

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

         switch ( mbody[0] )
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
                  if ( fd0 >= 0 )
                  {
                     uint32_t handle0, handle1;

                     wstUpdateResources( WSTRES_FD_VIDEO, true, fd0, __LINE__);
                     frameWidth= wstGetU32( mbody+1 );
                     frameHeight= wstGetU32( mbody+5);
                     frameFormat= wstGetU32( mbody+9);
                     rectX= (int)wstGetU32( mbody+13 );
                     rectY= (int)wstGetU32( mbody+17 );
                     rectW= (int)wstGetU32( mbody+21 );
                     rectH= (int)wstGetU32( mbody+25 );
                     if ( len >= (1+13*4) )
                     {
                        offset0= (int)wstGetU32( mbody+29 );
                        stride0= (int)wstGetU32( mbody+33 );
                        offset1= (int)wstGetU32( mbody+37 );
                        stride1= (int)wstGetU32( mbody+41 );
                        offset2= (int)wstGetU32( mbody+45 );
                        stride2= (int)wstGetU32( mbody+49 );

                        TRACE2("got frame fd %d,%d,%d (%dx%d) %X (%d, %d, %d, %d) off(%d, %d, %d) stride(%d, %d, %d)",
                               fd0, fd1, fd2, frameWidth, frameHeight, frameFormat, rectX, rectY, rectW, rectH,
                               offset0, offset1, offset2, stride0, stride1, stride2 );
                     }
                     else
                     {
                        offset0= 0;
                        offset1= frameWidth*frameHeight;
                        stride0= stride1= frameWidth;

                        TRACE2("got frame fd %d (%dx%d) %X (%d, %d, %d, %d)", fd0, frameWidth, frameHeight, frameFormat, rectX, rectY, rectW, rectH);
                     }

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

                     pthread_mutex_lock( &gCtx->mutex );
                     if ( conn->videoPlane->fbIdPrev )
                     {
                        wstUpdateResources( WSTRES_FB_VIDEO, false, conn->videoPlane->fbIdPrev, __LINE__);
                        drmModeRmFB( gCtx->drmFd, conn->videoPlane->fbIdPrev );
                        conn->videoPlane->fbIdPrev= 0;
                        wstClosePrimeFDHandles( gCtx, conn->videoPlane->handle0Prev, conn->videoPlane->handle1Prev, __LINE__ );
                        conn->videoPlane->handle0Prev= 0;
                        conn->videoPlane->handle1Prev= 0;
                     }
                     pthread_mutex_unlock( &gCtx->mutex );

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
                           wstUpdateResources( WSTRES_FB_VIDEO, true, fbId, __LINE__);
                           pthread_mutex_lock( &gCtx->mutex );
                           conn->videoPlane->videoFrameNext.hide= false;
                           conn->videoPlane->videoFrameNext.fbId= fbId;
                           conn->videoPlane->videoFrameNext.handle0= handle0;
                           conn->videoPlane->videoFrameNext.handle1= handle1;
                           conn->videoPlane->videoFrameNext.frameWidth= frameWidth-frameSkipX;
                           conn->videoPlane->videoFrameNext.frameHeight= frameHeight-frameSkipY;
                           conn->videoPlane->videoFrameNext.frameFormat= frameFormat;
                           conn->videoPlane->videoFrameNext.rectX= rectX+rectSkipX;
                           conn->videoPlane->videoFrameNext.rectY= rectY+rectSkipY;
                           conn->videoPlane->videoFrameNext.rectW= rectW-rectSkipX;
                           conn->videoPlane->videoFrameNext.rectH= rectH-rectSkipY;
                           pthread_mutex_unlock( &gCtx->mutex );

                           pthread_mutex_lock( &gMutex );
                           wstSwapDRMBuffers( gCtx, 0 );
                           pthread_mutex_unlock( &gMutex );
                        }
                        else
                        {
                           ERROR("wstVideoServerConnectionThread: drmModeAddFB2 failed: rc %d errno %d", rc, errno);
                        }
                     }
                     else
                     {
                        ERROR("wstVideoServerConnectionThread: drmPrimeFDToHandle failed: rc %d errno %d", rc, errno);
                     }
                  }
               }
               break;
            case 'H':
               DEBUG("got hide video plane %d", conn->videoPlane->plane->plane_id);
               conn->videoPlane->videoFrameNext.hide= true;
               pthread_mutex_lock( &gMutex );
               wstSwapDRMBuffers( gCtx, 0 );
               pthread_mutex_unlock( &gMutex );
               break;
            default:
               ERROR("got unknown video server message");
               break;
         }

         if ( conn->prevFrameFd0 >= 0 )
         {
            wstUpdateResources( WSTRES_FD_VIDEO, false, conn->prevFrameFd0, __LINE__);
            close( conn->prevFrameFd0 );
            conn->prevFrameFd0= -1;
            if ( conn->prevFrameFd1 >= 0 )
            {
               close( conn->prevFrameFd1 );
               conn->prevFrameFd1= -1;
            }
            if ( conn->prevFrameFd2 >= 0 )
            {
               close( conn->prevFrameFd2 );
               conn->prevFrameFd2= -1;
            }
         }
         if ( fd0 >= 0 )
         {
            conn->prevFrameFd0= fd0;
            if ( fd1 >= 0 )
            {
               conn->prevFrameFd1= fd1;
            }
            if ( fd2 >= 0 )
            {
               conn->prevFrameFd2= fd2;
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
   if ( conn->videoPlane )
   {
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
      if ( conn->videoPlane->videoFrameNext.fbId )
      {
         wstUpdateResources( WSTRES_FB_VIDEO, false, conn->videoPlane->videoFrameNext.fbId, __LINE__);
         drmModeRmFB( gCtx->drmFd, conn->videoPlane->videoFrameNext.fbId );
         conn->videoPlane->videoFrameNext.fbId= 0;
         wstClosePrimeFDHandles( gCtx, conn->videoPlane->videoFrameNext.handle0, conn->videoPlane->videoFrameNext.handle1, __LINE__ );
         conn->videoPlane->videoFrameNext.handle0= 0;
         conn->videoPlane->videoFrameNext.handle1= 0;
      }
      if ( conn->videoPlane->fbId )
      {
         wstUpdateResources( WSTRES_FB_VIDEO, false, conn->videoPlane->fbId, __LINE__);
         drmModeRmFB( gCtx->drmFd, conn->videoPlane->fbId );
         conn->videoPlane->fbId= 0;
         wstClosePrimeFDHandles( gCtx, conn->videoPlane->handle0, conn->videoPlane->handle1, __LINE__ );
         conn->videoPlane->handle0= 0;
         conn->videoPlane->handle1= 0;
      }
      if ( conn->videoPlane->fbIdPrev )
      {
         wstUpdateResources( WSTRES_FB_VIDEO, false, conn->videoPlane->fbIdPrev, __LINE__);
         drmModeRmFB( gCtx->drmFd, conn->videoPlane->fbIdPrev );
         conn->videoPlane->fbIdPrev= 0;
         wstClosePrimeFDHandles( gCtx, conn->videoPlane->handle0Prev, conn->videoPlane->handle1Prev, __LINE__ );
         conn->videoPlane->handle0Prev= 0;
         conn->videoPlane->handle1Prev= 0;
      }
      if ( conn->prevFrameFd0 >= 0 )
      {
         wstUpdateResources( WSTRES_FD_VIDEO, false, conn->prevFrameFd0, __LINE__);
         close( conn->prevFrameFd0 );
         conn->prevFrameFd0= -1;
         if ( conn->prevFrameFd1 >= 0 )
         {
            close( conn->prevFrameFd1 );
            conn->prevFrameFd1= -1;
         }
         if ( conn->prevFrameFd2 >= 0 )
         {
            close( conn->prevFrameFd2 );
            conn->prevFrameFd2= -1;
         }
      }
      pthread_mutex_unlock( &gCtx->mutex );

      wstOverlayFree( &gCtx->overlayPlanes, conn->videoPlane );
      conn->videoPlane= 0;
   }

   conn->threadStarted= false;

   if ( !conn->threadStopRequested )
   {
      int i;
      pthread_mutex_lock( &conn->server->mutex );
      for( i= 0; i < MAX_VIDEO_CONNECTIONS; ++i )
      {
         if ( conn->server->connections[i] == conn )
         {
            conn->server->connections[i]= 0;
            break;
         }
      }
      pthread_mutex_unlock( &conn->server->mutex );

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
      conn->prevFrameFd0= -1;
      conn->prevFrameFd1= -1;
      conn->prevFrameFd1= -1;

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
   server->threadStarted= true;

   while( !server->threadStopRequested )
   {
      int fd;
      struct sockaddr_un addr;
      socklen_t addrLen= sizeof(addr);

      DEBUG("waiting for connections...");
      fd= accept4( server->socketFd, (struct sockaddr *)&addr, &addrLen, SOCK_CLOEXEC );
      if ( fd >= 0 )
      {
         VideoServerConnection *conn= 0;

         DEBUG("video server received connection: fd %d", fd);

         conn= wstCreateVideoServerConnection( server, fd );
         if ( conn )
         {
            int i;
            DEBUG("created video server connection %p for fd %d", conn, fd );
            pthread_mutex_lock( &server->mutex );
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
            pthread_mutex_unlock( &server->mutex );
         }
         else
         {
            ERROR("failed to create video server connection for fd %d", fd);
         }
      }
      else
      {
         usleep( 10000 );
      }
   }

exit:
   server->threadStarted= false;
   DEBUG("wstVideoServerThread: exit");

   return 0;
}

static bool wstInitVideoServer( VideoServerCtx *server )
{
   bool result= false;
   const char *workingDir;
   int rc, pathNameLen, addressSize;

   pthread_mutex_init( &server->mutex, 0 );
   server->socketFd= -1;
   server->lockFd= -1;
   server->name= "video";

   ++server->refCnt;

   workingDir= getenv("XDG_RUNTIME_DIR");
   if ( !workingDir )
   {
      ERROR("wstInitVideoServer: XDG_RUNTIME_DIR is not set");
      goto exit;
   }

   pathNameLen= strlen(workingDir)+strlen("/")+strlen(server->name)+1;
   if ( pathNameLen > (int)sizeof(server->addr.sun_path) )
   {
      ERROR("wstInitVideoServer: name for server unix domain socket is too long: %d versus max %d",
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
      ERROR("wstInitVideoServer: failed to create lock file (%s) errno %d", server->lock, errno );
      goto exit;
   }

   rc= flock(server->lockFd, LOCK_NB|LOCK_EX );
   if ( rc < 0 )
   {
      ERROR("wstInitVideoServer: failed to lock.  Is another server running with name %s ?", server->name );
      goto exit;
   }

   (void)unlink(server->addr.sun_path);

   server->socketFd= socket( PF_LOCAL, SOCK_STREAM|SOCK_CLOEXEC, 0 );
   if ( server->socketFd < 0 )
   {
      ERROR("wstInitVideoServer: unable to open socket: errno %d", errno );
      goto exit;
   }

   addressSize= pathNameLen + offsetof(struct sockaddr_un, sun_path);

   rc= bind(server->socketFd, (struct sockaddr *)&server->addr, addressSize );
   if ( rc < 0 )
   {
      ERROR("wstInitVideoServer: Error: bind failed for socket: errno %d", errno );
      goto exit;
   }

   rc= listen(server->socketFd, 1);
   if ( rc < 0 )
   {
      ERROR("wstInitVideoServer: Error: listen failed for socket: errno %d", errno );
      goto exit;
   }

   rc= pthread_create( &server->threadId, NULL, wstVideoServerThread, server );
   if ( rc )
   {
      ERROR("wstInitVideoServer: Error: unable to start server thread: rc %d errno %d", rc, errno);
      goto exit;
   }

   result= true;

exit:

   return result;
}

static void wstTermVideoServer( VideoServerCtx *server )
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

      for( i= 0; i < MAX_VIDEO_CONNECTIONS; ++i )
      {
         VideoServerConnection *conn= server->connections[i];
         if ( conn )
         {
            wstDestroyVideoServerConnection( conn );
            server->connections[i]= 0;
         }
      }

      if ( server->socketFd >= 0 )
      {
         shutdown( server->socketFd, SHUT_RDWR );
      }

      if ( server->threadStarted )
      {
         server->threadStopRequested= true;
         pthread_join( server->threadId, NULL );
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
   }
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
      pthread_mutex_init( &ctx->mutex, 0 );
      ctx->refCnt= 1;
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
      #ifdef DRM_USE_NATIVE_FENCE
      ctx->nativeOutputFenceFd= -1;
      #endif

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
         goto exit;
      }
      ctx->res= res;
      ctx->conn= conn;

      for( i= 0; i < conn->count_modes; ++i )
      {
         DEBUG("mode %d: %dx%dx%d (%s) type 0x%x flags 0x%x",
                i, conn->modes[i].hdisplay, conn->modes[i].vdisplay, conn->modes[i].vrefresh,
                conn->modes[i].name, conn->modes[i].type, conn->modes[i].flags );
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
         if ( ctx->enc && (ctx->enc->encoder_id == conn->encoder_id) )
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

         if ( haveVideoPlanes && (ctx->overlayPlanes.totalCount >= 2) )
         {
            if ( ctx->useVideoServer )
            {
               gServer= (VideoServerCtx*)calloc( 1, sizeof(VideoServerCtx) );
               if ( gServer )
               {
                  ctx->haveNativeFence= false;
                  if ( !wstInitVideoServer( gServer ) )
                  {
                     ERROR("wstInitCtx: failed to initialize video server");
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
      wstTermVideoServer( gServer );

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

static void wstSwapDRMBuffersAtomic( WstGLCtx *ctx, NativeWindowItem *nw )
{
   int rc;
   drmModeAtomicReq *req;
   uint32_t flags= 0;
   struct gbm_surface* gs;
   struct gbm_bo *bo;
   uint32_t handle, stride;

   TRACE3("wstSwapDRMBuffersAtomic: atomic start");
   req= drmModeAtomicAlloc();
   if ( !req )
   {
      ERROR("wstSwapDRMBuffersAtomic: drmModeAtomicAlloc failed, errno %x", errno);
      goto exit;
   }

   #ifdef DRM_USE_NATIVE_FENCE
   if ( ctx->haveNativeFence )
   {
      flags |= DRM_MODE_ATOMIC_NONBLOCK;
   }
   #endif

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

   #ifdef DRM_USE_NATIVE_FENCE
   if ( ctx->haveNativeFence )
   {
      wstAtomicAddProperty( ctx, req, ctx->crtc->crtc_id,
                            ctx->crtcProps->count_props, ctx->crtcPropRes,
                            "OUT_FENCE_PTR", (uint64_t)(unsigned long)(&ctx->nativeOutputFenceFd) );
   }
   #endif

   if ( nw )
   {
      gs= (struct gbm_surface*)nw->nativeWindow;
      if ( gs )
      {
         bo= gbm_surface_lock_front_buffer(gs);
         wstUpdateResources( WSTRES_BO_GRAPHICS, true, (long long)bo, __LINE__);

         handle= gbm_bo_get_handle(bo).u32;
         stride= gbm_bo_get_stride(bo);

         if ( nw->handle != handle )
         {
            rc= drmModeAddFB( ctx->drmFd,
                              nw->width,
                              nw->height,
                              32,
                              32,
                              stride,
                              handle,
                              &nw->fbId );
             if ( rc )
             {
                ERROR("wstSwapDRMBuffersAtomic: drmModeAddFB rc %d errno %d", rc, errno);
                goto exit;
             }
             wstUpdateResources( WSTRES_FB_GRAPHICS, true, nw->fbId, __LINE__);
             nw->handle= handle;
             nw->bo= bo;
         }

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

         wstAtomicAddProperty( ctx, req, nw->windowPlane->plane->plane_id,
                               nw->windowPlane->planeProps->count_props, nw->windowPlane->planePropRes,
                               "CRTC_Y", 0 );

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

   pthread_mutex_lock( &ctx->mutex );
   if ( ctx->overlayPlanes.usedCount )
   {
      WstOverlayPlane *iter= ctx->overlayPlanes.usedHead;
      while( iter )
      {
         if ( iter->fbIdPrev )
         {
            wstUpdateResources( WSTRES_FB_VIDEO, false, iter->fbIdPrev, __LINE__);
            drmModeRmFB( ctx->drmFd, iter->fbIdPrev );
            iter->fbIdPrev= 0;
            wstClosePrimeFDHandles( ctx, iter->handle0Prev, iter->handle1Prev, __LINE__ );
            iter->handle0Prev= 0;
            iter->handle1Prev= 0;
         }

         if ( iter->videoFrameNext.fbId )
         {
            uint32_t fbId= iter->videoFrameNext.fbId;
            uint32_t handle0= iter->videoFrameNext.handle0;
            uint32_t handle1= iter->videoFrameNext.handle1;
            uint32_t frameWidth= iter->videoFrameNext.frameWidth;
            uint32_t frameHeight= iter->videoFrameNext.frameHeight;
            int rectX= iter->videoFrameNext.rectX;
            int rectY= iter->videoFrameNext.rectY;
            int rectW= iter->videoFrameNext.rectW;
            int rectH= iter->videoFrameNext.rectH;
            uint32_t sx, sy, sw, sh, dx, dy, dw, dh;
            int modeWidth, modeHeight, gfxWidth, gfxHeight;

            iter->fbIdPrev= iter->fbId;
            iter->handle0Prev= iter->handle0;
            iter->handle1Prev= iter->handle1;
            iter->fbId= fbId;
            iter->handle0= handle0;
            iter->handle1= handle1;
            iter->plane->crtc_id= ctx->overlayPlanes.primary->crtc_id;

            iter->videoFrameNext.fbId= 0;
            iter->videoFrameNext.hide= false;
            iter->videoFrameNext.hidden= false;

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

            wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                  iter->planeProps->count_props, iter->planePropRes,
                                  "FB_ID", iter->fbId );

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
         }
         else if ( iter->videoFrameNext.hide && !iter->videoFrameNext.hidden )
         {
            iter->fbIdPrev= iter->fbId;
            iter->handle0Prev= iter->handle0;
            iter->handle1Prev= iter->handle1;

            iter->plane->crtc_id= ctx->overlayPlanes.primary->crtc_id;
            DEBUG("hiding video plane %d", iter->plane->plane_id);

            wstAtomicAddProperty( ctx, req, iter->plane->plane_id,
                                  iter->planeProps->count_props, iter->planePropRes,
                                  "FB_ID", 0 );

            iter->fbId= 0;
            iter->handle0= 0;
            iter->handle1= 0;
            iter->videoFrameNext.hide= false;
            iter->videoFrameNext.hidden= true;
         }

         iter= iter->next;
      }
   }
   pthread_mutex_unlock( &ctx->mutex );

   rc= drmModeAtomicCommit( ctx->drmFd, req, flags, 0 );
   if ( rc )
   {
      ERROR("drmModeAtomicCommit failed: rc %d errno %d", rc, errno );
      goto exit;
   }

   if ( flags & DRM_MODE_ATOMIC_ALLOW_MODESET )
   {
      ctx->modeSet= true;
   }

exit:

   TRACE3("wstSwapDRMBuffersAtomic: atomic stop");
   if ( req )
   {
      drmModeAtomicFree( req );
   }

   return;
}

static void wstSwapDRMBuffers( WstGLCtx *ctx, NativeWindowItem *nw )
{
   struct gbm_surface* gs;
   struct gbm_bo *bo;
   uint32_t handle, stride;
   fd_set fds;
   drmEventContext ev;
   drmModePlane *plane= 0;
   int rc;
   bool eventPending= false;

   if ( ctx->modeSetPending )
   {
      ctx->notifySizeChange= true;
      ctx->modeCurrent= ctx->modeNext;
      ctx->modeSetPending= false;
      ctx->modeSet= false;
   }

   if ( ctx->haveAtomic )
   {
      wstSwapDRMBuffersAtomic( ctx, nw );
      goto done;
   }

   if ( nw )
   {
      gs= (struct gbm_surface*)nw->nativeWindow;
      if ( gs )
      {
         bo= gbm_surface_lock_front_buffer(gs);
         wstUpdateResources( WSTRES_BO_GRAPHICS, true, (long long)bo, __LINE__);

         handle= gbm_bo_get_handle(bo).u32;
         stride= gbm_bo_get_stride(bo);

         if ( nw->handle != handle )
         {
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
             nw->handle= handle;
             nw->bo= bo;
         }

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

   if ( !nw )
   {
      pthread_mutex_lock( &ctx->mutex );
      if ( ctx->overlayPlanes.usedCount )
      {
         WstOverlayPlane *iter= ctx->overlayPlanes.usedHead;
         while( iter )
         {
            if ( iter->fbIdPrev )
            {
               wstUpdateResources( WSTRES_FB_VIDEO, false, iter->fbIdPrev, __LINE__);
               drmModeRmFB( ctx->drmFd, iter->fbIdPrev );
               iter->fbIdPrev= 0;
               wstClosePrimeFDHandles( ctx, iter->handle0Prev, iter->handle1Prev, __LINE__ );
               iter->handle0Prev= 0;
               iter->handle1Prev= 0;
            }

            if ( iter->videoFrameNext.fbId )
            {
               uint32_t fbId= iter->videoFrameNext.fbId;
               uint32_t handle0= iter->videoFrameNext.handle0;
               uint32_t handle1= iter->videoFrameNext.handle1;
               uint32_t frameWidth= iter->videoFrameNext.frameWidth;
               uint32_t frameHeight= iter->videoFrameNext.frameHeight;
               int rectX= iter->videoFrameNext.rectX;
               int rectY= iter->videoFrameNext.rectY;
               int rectW= iter->videoFrameNext.rectW;
               int rectH= iter->videoFrameNext.rectH;
               uint32_t sx, sy, sw, sh, dx, dy, dw, dh;
               int modeWidth, modeHeight, gfxWidth, gfxHeight;

               iter->fbIdPrev= iter->fbId;
               iter->handle0Prev= iter->handle0;
               iter->handle1Prev= iter->handle1;

               iter->videoFrameNext.fbId= 0;
               iter->videoFrameNext.hide= false;
               iter->videoFrameNext.hidden= false;

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
                  iter->fbId= fbId;
                  iter->handle0= handle0;
                  iter->handle1= handle1;
               }
               else
               {
                  wstUpdateResources( WSTRES_FB_VIDEO, false, fbId, __LINE__);
                  drmModeRmFB( gCtx->drmFd, fbId );
                  wstClosePrimeFDHandles( ctx, handle0, handle1, __LINE__ );
                  ERROR("wstSwapDRMBuffers: drmModeSetPlane rc %d errno %d", rc, errno );
               }
            }
            else if ( iter->videoFrameNext.hide && !iter->videoFrameNext.hidden )
            {
               iter->fbIdPrev= iter->fbId;
               iter->handle0Prev= iter->handle0;
               iter->handle1Prev= iter->handle1;

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
               iter->fbId= 0;
               iter->handle0= 0;
               iter->handle1= 0;
               iter->videoFrameNext.hide= true;
               iter->videoFrameNext.hidden= true;
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
   if ( nw )
   {
      if ( nw->prevBo )
      {
         gs= (struct gbm_surface*)nw->nativeWindow;
         wstUpdateResources( WSTRES_FB_GRAPHICS, false, nw->prevFbId, __LINE__);
         drmModeRmFB( ctx->drmFd, nw->prevFbId );
         wstUpdateResources( WSTRES_BO_GRAPHICS, false, (long long)nw->prevBo, __LINE__);
         gbm_surface_release_buffer(gs, nw->prevBo);
      }
      nw->prevBo= nw->bo;
      nw->prevFbId= nw->fbId;

      pthread_mutex_lock( &ctx->mutex );
      if ( ctx->overlayPlanes.usedCount )
      {
         WstOverlayPlane *iter= ctx->overlayPlanes.usedHead;
         while( iter )
         {
            if ( iter->videoFrameNext.hidden )
            {
               iter->videoFrameNext.hidden= false;
               if ( iter->fbIdPrev )
               {
                  wstUpdateResources( WSTRES_FB_VIDEO, false, iter->fbIdPrev, __LINE__);
                  drmModeRmFB( ctx->drmFd, iter->fbIdPrev );
                  iter->fbIdPrev= 0;
                  wstClosePrimeFDHandles( ctx, iter->handle0Prev, iter->handle1Prev, __LINE__ );
                  iter->handle0Prev= 0;
                  iter->handle1Prev= 0;
               }
            }
            iter= iter->next;
         }
      }
      pthread_mutex_unlock( &ctx->mutex );
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
                    (gServer == 0 ) &&
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
         pthread_mutex_lock( &gMutex );
         if ( gCtx )
         {
            nwIter= gCtx->nwFirst;
            while( nwIter )
            {
               if ( nwIter->surface == surface )
               {
                  wstSwapDRMBuffers( gCtx, nwIter );
                  break;
               }
               nwIter= nwIter->next;
           }
         }
         pthread_mutex_unlock( &gMutex );
      }
      if ( gCtx->notifySizeChange )
      {
         gCtx->notifySizeChange= false;
         wstGLNotifySizeListeners();
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

   if ( ctx )
   {
      INFO("native window: wxh=%dx%d", width, height);

      WSTGL_CHECK_GRAPHICS_SIZE( width, height );

      if ( !ctx->modeSet && !ctx->modeSetPending )
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
            pthread_mutex_unlock( &gMutex );
         }
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
