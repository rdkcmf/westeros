/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2018 RDK Management
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
/*
** Copyright (c) 2013 The Khronos Group Inc.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and/or associated documentation files (the
** "Materials"), to deal in the Materials without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Materials, and to
** permit persons to whom the Materials are furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be included
** in all copies or substantial portions of the Materials.
**
** THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
** MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
*/
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <memory.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <syscall.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdint.h>
#include <signal.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include "westeros-ut-em.h"
#include "wayland-egl.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>

#include <linux/videodev2.h>

#include "wayland-client.h"
#include "wayland-server.h"
#include "drm-client-protocol.h"
#include "drm-server-protocol.h"

#include <vector>
#include <map>

#undef open
#undef close
#undef read
#undef ioctl
#undef mmap
#undef munmap
#undef poll
#undef stat
#undef opendir
#undef closedir
#undef readdir

// When running:
//export LD_PRELOAD=../lib/libwesteros-ut-em.so
//export LD_LIBRARY_PATH=../lib

#define INT_FATAL(FORMAT, ...)      emPrintf(0, "FATAL: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_ERROR(FORMAT, ...)      emPrintf(0, "ERROR: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_WARNING(FORMAT, ...)    emPrintf(1, "WARN: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_INFO(FORMAT, ...)       emPrintf(2, "INFO: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_DEBUG(FORMAT, ...)      emPrintf(3, "DEBUG: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE1(FORMAT, ...)     emPrintf(4, "TRACE: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE2(FORMAT, ...)     emPrintf(5, "TRACE: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE3(FORMAT, ...)     emPrintf(6, "TRACE: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)

#define FATAL(...)                  INT_FATAL(__VA_ARGS__, "")
#define ERROR(...)                  INT_ERROR(__VA_ARGS__, "")
#define WARNING(...)                INT_WARNING(__VA_ARGS__, "")
#define INFO(...)                   INT_INFO(__VA_ARGS__, "")
#define DEBUG(...)                  INT_DEBUG(__VA_ARGS__, "")
#define TRACE1(...)                 INT_TRACE1(__VA_ARGS__, "")
#define TRACE2(...)                 INT_TRACE2(__VA_ARGS__, "")
#define TRACE3(...)                 INT_TRACE3(__VA_ARGS__, "")

// Section: internal types ------------------------------------------------------

#define EM_WINDOW_MAGIC (0x55122131)
#define EM_SIMPLE_VIDEO_DECODER_MAGIC (0x55122132)

struct wl_drm_buffer 
{
   struct wl_resource *resource;
   uint32_t format;
   int32_t width;
   int32_t height;
   int32_t offset[3];  
   int32_t stride[3];
   int32_t bufferId;
};

struct wl_egl_window
{
   EMCTX *ctx;
   struct wl_display *wldisp;
   struct wl_surface *surface;
   struct wl_registry *registry;
   struct wl_drm *drm;
   struct wl_event_queue *queue;
   bool windowDestroyPending;
   int activeBuffers;
   int width;
   int height;
   int dx;
   int dy;
   int attachedWidth;
   int attachedHeight;

   EGLNativeWindowType nativeWindow;
   void *singleBuffer;
   void *singleBufferCtx;

   EGLSurface eglSurface;

   int bufferIdBase;
   int bufferIdCount;
   int bufferId;   

   int eglSwapCount;
};

typedef struct _EMGraphics
{
} EMGraphics;

typedef struct _EMSurface
{
   int width;
   int height;
} EMSurface;

typedef struct _EMNativeWindow
{
   uint32_t magic;
   EMCTX *ctx;
   int x;
   int y;
   int width;
   int height;
   int stride;
   uint32_t format;
} EMNativeWindow;

typedef struct _EMNativePixmap
{
   int width;
   int height;
   EMSurface *s;
} EMNativePixmap;

typedef struct _EMSurfaceClient
{
   bool positionIsPending;
   int vxPending;
   int vyPending;
   int vwPending;
   int vhPending;
   int vx;
   int vy;
   int vw;
   int vh;
} EMSurfaceClient;

typedef struct _EMSimpleVideoDecoder
{
   uint32_t magic;
   EMCTX *ctx;
   bool inUse;
   bool signalUnderflow;
   int type;
   uint32_t startPTS;
   bool started;
   int videoWidth;
   int videoHeight;
   float videoFrameRate;  //FPS
   float videoBitRate;  // Mbps
   bool segmentsStartAtZero;
   bool firstPtsPassed;
   unsigned frameNumber;
   uint32_t currentPTS;
   unsigned long long int basePTS;
} EMSimpleVideoDecoder;

#define EM_DEVICE_FD_BASE (1000000)
#define EM_DEVICE_MAGIC (0x55112631)

typedef enum _EM_DEVICE_TYPE
{
   EM_DEVICE_TYPE_NONE= 0,
   EM_DEVICE_TYPE_DRM,
   EM_DEVICE_TYPE_V4L2,
   EM_DEVICE_TYPE_GAMEPAD
} EM_DEVICE_TYPE;

#define EM_DRM_MODE_MAX (32)
#define EM_DRM_HANDLE_MAX (32)
#define EM_V4L2_FMT_MAX (32)
#define EM_V4L2_INBUFF_MAX (4)
#define EM_V4L2_OUTBUFF_MAX (12)
#define EM_V4L2_MAP_MAX (EM_V4L2_INBUFF_MAX+(2*EM_V4L2_OUTBUFF_MAX))
#define EM_V4L2_MIN_BUFFERS_FOR_CAPTURE (5)
#define EM_V4L2_MIN_WIDTH (64)
#define EM_V4L2_MAX_WIDTH (3840)
#define EM_V4L2_STEP_WIDTH (8)
#define EM_V4L2_MIN_HEIGHT (64)
#define EM_V4L2_MAX_HEIGHT (2160)
#define EM_V4L2_STEP_HEIGHT (8)

typedef struct _EMFd
{
   int fd;
   int fd_os;
} EMFd;

typedef struct _EMDrmHandle
{
   uint32_t handle;
   int fd;
   uint32_t fbId;
} EMDrmHandle;

typedef struct _EMDevice
{
   uint32_t magic;
   int fd;
   int fd_os;
   int type;
   const char *path;
   EMCTX *ctx;
   union _dev
   {
      struct _drm
      {
         uint32_t nextId;
         int32_t countModes;
         drmModeModeInfo modes[EM_DRM_MODE_MAX];
         int32_t countCrtcs;
         drmModeCrtc crtcs[1];
         int32_t countEncoders;
         drmModeEncoder encoders[1];
         int32_t countConnectors;
         drmModeConnector connectors[1];
         int32_t countProperties;
         drmModePropertyRes properties[256];
         int32_t *crtcOutFenceFd;
         int32_t countPlaneFormats;
         uint32_t planeFormats[16];
         uint32_t countPlaneTypeEnum;
         struct drm_mode_property_enum planeTypeEnum[3];
         int32_t countPlanes;
         drmModePlane planes[3];
         EMSurfaceClient videoPlane[1];
         EMDrmHandle handles[EM_DRM_HANDLE_MAX];
      } drm;
      struct _v4l2
      {
         int countInputFormats;
         struct v4l2_fmtdesc inputFormats[EM_V4L2_FMT_MAX];
         int countOutputFormats;
         struct v4l2_fmtdesc outputFormats[EM_V4L2_FMT_MAX];
         int subscribeSourceChange;
         struct v4l2_format fmtIn;
         struct v4l2_format fmtOut;
         int inputMemoryMode;
         int countInputBuffers;
         struct v4l2_plane inputBufferPlanes[EM_V4L2_INBUFF_MAX];
         struct v4l2_buffer inputBuffers[EM_V4L2_INBUFF_MAX];
         int outputMemoryMode;
         int countOutputBuffers;
         EMFd outputBufferFds[EM_V4L2_OUTBUFF_MAX*2];
         struct v4l2_plane outputBufferPlanes[EM_V4L2_OUTBUFF_MAX*2];
         struct v4l2_buffer outputBuffers[EM_V4L2_OUTBUFF_MAX];
         unsigned char *map[EM_V4L2_MAP_MAX];
         bool inputStreaming;
         bool outputStreaming;
         int frameWidthSrc;
         int frameHeightSrc;
         int frameWidth;
         int frameHeight;
         int readyFrameCount;
         int outputFrameCount;
         bool needBaseTime;
         struct timeval baseTime;
      } v4l2;
      struct _gamepad
      {
         unsigned int version;
         const char *deviceName;
         int buttonCount;
         uint16_t buttonMap[4];
         int axisCount;
         uint8_t axisMap[4];
         bool eventPending;
         int eventType;
         int eventNumber;
         int eventValue;
      } gamepad;
   } dev;
} EMDevice;

struct gbm_device
{
   EMDevice *dev;
   int refCount;
};

struct gbm_bo
{
   struct gbm_surface *surface;
   bool locked;
   union gbm_bo_handle handle;
   uint32_t fbId;
};

struct gbm_surface
{
   EMNativeWindow nw;
   struct gbm_device *gbm;
   struct gbm_bo buffers[3];
   int front;
};

#define EM_EGL_CONFIG_MAGIC (0x55112231)

typedef struct _EMEGLConfig
{
   uint32_t magic;
   EGLint redSize;
   EGLint greenSize;
   EGLint blueSize;
   EGLint alphaSize;
   EGLint depthSize;
} EMEGLConfig;

#define EM_EGL_SURFACE_MAGIC (0x55112331)

typedef struct _EMEGLSurface
{
   uint32_t magic;
   struct wl_egl_window *egl_window;
} EMEGLSurface;

#define EM_EGL_IMAGE_MAGIC (0x55112431)

typedef struct _EMEGLImage
{
   uint32_t magic;
   EGLClientBuffer clientBuffer;
   EGLenum target;
   int fd;
} EMEGLImage;

#define EM_EGL_CONTEXT_MAGIC (0x55112531)

typedef struct _EMEGLContext
{
   uint32_t magic;
   EMEGLSurface *draw;
   EMEGLSurface *read;
} EMEGLContext;

#define EM_EGL_DISPLAY_MAGIC (0x55112131)

typedef struct _EMEGLDisplay
{
   uint32_t magic;
   EMCTX *ctx;
   EGLNativeDisplayType displayId;
   bool initialized;
   EMEGLContext *context;
   EGLint swapInterval;
} EMEGLDisplay;

#define DEFAULT_DISPLAY_WIDTH (1280)
#define DEFAULT_DISPLAY_HEIGHT (720)

#define EM_MAX_ERROR (4096)
#define EM_DEVICE_MAX (20)

typedef struct _EMWLBinding
{
   EGLDisplay wlBoundDpy;
   struct wl_display *display;
   struct wl_drm *drm;
} EMWLBinding;

typedef void* (*PFNWSTGLINIT)();
typedef void (*PFNWSTGLTERM)( void* );

typedef struct _EMCTX
{
   void *moduleWstGL;
   PFNWSTGLINIT wstGLInit;
   PFNWSTGLTERM wstGLTerm;
   void *wstGLCtx;
   int displayWidth;
   int displayHeight;
   struct gbm_device *gbm_device;
   EGLContext eglContext;
   EGLDisplay eglDisplayDefault;
   EGLDisplay eglDisplayCurrent;
   GLuint nextProgramId;
   GLuint nextShaderId;
   GLuint nextTextureId;
   GLuint nextFramebufferId;
   GLuint nextUniformLocation;
   std::vector<int> framebufferIds; 
   std::vector<int> textureIds; 
   GLfloat clearColor[4];
   GLint scissorBox[4];
   GLint viewport[4];
   bool scissorEnable;
   GLuint currentProgramId;
   GLfloat textureWrapS;
   GLfloat textureWrapT;
   GLint textureMagFilter;
   GLint textureMinFilter;
   std::map<struct wl_display*,EMWLBinding> wlBindings;
   int videoCodec;
   int waylandSendTid;
   bool waylandThreadingIssue;
   bool westerosModuleInitShouldFail;
   bool westerosModuleInitCalled;
   bool westerosModuleTermCalled;
   EMSimpleVideoDecoder simpleVideoDecoderMain;

   EMTextureCreated textureCreatedCB;
   void *textureCreatedUserData;
   EMHolePunched holePunchedCB;
   void *holePunchedUserData;

   uint32_t nextGbmBuffHandle;
   std::vector<struct gbm_bo*> gbmBuffs;

   int deviceCount;
   int deviceNextFd;
   EMDevice devices[EM_DEVICE_MAX];

   char errorDetail[EM_MAX_ERROR];
} EMCTX;

static EMCTX* emGetContext( void );
static EMCTX* emCreate( void );
static void emDestroy( EMCTX* ctx );
static int EMDeviceOpenOS( int fd );
static void EMDeviceCloseOS( int fd_os );
static void EMDevicePruneOS();
static void EMV4l2FreeInputBuffers( EMDevice *dev );
static void EMV4l2FreeOutputBuffers( EMDevice *dev );
static EGLNativeWindowType wlGetNativeWindow( struct wl_egl_window *egl_window );
static void wlSwapBuffers( struct wl_egl_window *egl_window );


static pthread_mutex_t gMutex= PTHREAD_MUTEX_INITIALIZER;
static EMCTX *gCtx= 0;
static int gActiveLevel= 2;
static int gDrmOpenCount= 0;
static bool gDrmHaveMaster= false;
static int gDrmLockFd= -1;
static EMDevice *gDrmMasterDev= 0;
static std::vector<struct wl_egl_window*> gNativeWindows= std::vector<struct wl_egl_window*>();

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static void emPrintf( int level, const char *fmt, ... )
{
   if ( level <= gActiveLevel )
   {
      va_list argptr;
      fprintf( stderr, "%lld: ", getCurrentTimeMillis());
      va_start( argptr, fmt );
      vfprintf( stderr, fmt, argptr );
      va_end( argptr );
   }
}

EMCTX* EMCreateContext( void )
{
   EMCTX *ctx= 0;

   ctx= emGetContext();

   return ctx;
}

void EMDestroyContext( EMCTX* ctx )
{
   if ( ctx )
   {
      EMDevicePruneOS();
      emDestroy( ctx );
      gCtx= 0;
   }
}

bool EMStart( EMCTX *ctx )
{
   bool result= false;

   if ( ctx )
   {
      void *module= 0;
      module= dlopen( "libwesteros_gl.so.0.0.0", RTLD_NOW );
      if ( module )
      {
         // Create a WstGLCtx here so that a context has been created
         // prior to any call to eglGetDisplay. This ensures the global
         // WstGLCtx is created and destroyed for each test context.
         ctx->moduleWstGL= module;
         ctx->wstGLInit= (PFNWSTGLINIT)dlsym( module, "WstGLInit" );
         ctx->wstGLTerm= (PFNWSTGLTERM)dlsym( module, "WstGLTerm" );
         if ( ctx->wstGLInit && ctx->wstGLTerm )
         {
            ctx->wstGLCtx= ctx->wstGLInit();
         }
         TRACE1("wstGLInit %d wstGLTerm %d", ctx->wstGLInit, ctx->wstGLTerm, ctx->wstGLCtx);

         result= true;
      }
   }
}

bool EMSetDisplaySize( EMCTX *ctx, int width, int height )
{
   bool result= false;

   if ( ctx )
   {
      ctx->displayWidth= width;
      ctx->displayHeight= height;

      for( int i= 0; i < EM_DEVICE_MAX; ++i )
      {
         if ( (ctx->devices[i].fd != -1) && (ctx->devices[i].type == EM_DEVICE_TYPE_DRM) )
         {
            EMDevice *d= &ctx->devices[i];
            bool foundMode= false;

            for( int j= 0; j < d->dev.drm.countModes; ++j )
            {
               if ( (d->dev.drm.modes[j].hdisplay == width) && (d->dev.drm.modes[j].vdisplay == height) )
               {
                  foundMode= true;
                  d->dev.drm.crtcs[0].mode_valid= 1;
                  d->dev.drm.crtcs[0].mode= d->dev.drm.modes[j];
                  break;
               }
            }
            if ( !foundMode )
            {
               ERROR("No matching mode for drm device");
               goto exit;
            }
         }
      }

      result= true;
   }

exit:

   return result;
}

bool EMGetWaylandThreadingIssue( EMCTX *ctx )
{
   return ctx->waylandThreadingIssue;
}

void EMSetWesterosModuleIntFail( EMCTX *ctx, bool initShouldFail )
{
   ctx->westerosModuleInitShouldFail= initShouldFail;
}

bool EMGetWesterosModuleInitCalled( EMCTX *ctx )
{
   bool wasCalled= ctx->westerosModuleInitCalled;
   ctx->westerosModuleInitCalled= false;
   return wasCalled;
}

bool EMGetWesterosModuleTermCalled( EMCTX *ctx )
{
   bool wasCalled= ctx->westerosModuleTermCalled;
   ctx->westerosModuleTermCalled= false;
   return wasCalled;
}

void EMSetError( EMCTX *ctx, const char *fmt, ... )
{
   va_list argptr;
   va_start( argptr, fmt );
   vsprintf( ctx->errorDetail, fmt, argptr );
   va_end( argptr );
   fprintf(stderr,"%s\n",ctx->errorDetail);
}

const char* EMGetError( EMCTX *ctx )
{
   return ctx->errorDetail;
}

long long EMGetCurrentTimeMicro(void)
{
   struct timeval tv;
   long long utcCurrentTimeMicro;

   gettimeofday(&tv,0);
   utcCurrentTimeMicro= tv.tv_sec*1000000LL+tv.tv_usec;

   return utcCurrentTimeMicro;
}

void EMSetVideoCodec( EMCTX *ctx, int codec )
{
   ctx->videoCodec= codec;
}

int EMGetVideoCodec( EMCTX *ctx )
{
   return ctx->videoCodec;
}

EMSurfaceClient* EMGetVideoWindow( EMCTX *ctx, int id )
{
   EMSurfaceClient *surfaceClient= 0;

   // ignore id for now

   if ( gDrmMasterDev )
   {
      surfaceClient= &gDrmMasterDev->dev.drm.videoPlane[0];
   }

   return surfaceClient;
}

void EMSurfaceClientGetPosition( EMSurfaceClient *emsc, int *x, int *y, int *width, int *height )
{
   TRACE1("EMSurfaceClientGetPosition: (%d, %d, %d, %d) pending(%d, %d, %d, %d)",
          emsc->vx, emsc->vy, emsc->vw, emsc->vh,
          emsc->vxPending, emsc->vyPending, emsc->vwPending, emsc->vhPending );
   if ( x ) *x= emsc->vx;
   if ( y ) *y= emsc->vy;
   if ( width ) *width= emsc->vw;
   if ( height ) *height= emsc->vh;
}

EMSimpleVideoDecoder* EMGetSimpleVideoDecoder( EMCTX *ctx, int id )
{
   // ignore id for now

   return &ctx->simpleVideoDecoderMain;
}

void EMSimpleVideoDecoderSetVideoSize( EMSimpleVideoDecoder *dec, int width, int height )
{
   dec->videoWidth= width;
   dec->videoHeight= height;
}

void EMSimpleVideoDecoderGetVideoSize( EMSimpleVideoDecoder *dec, int *width, int *height )
{
   if ( dec )
   {
      if ( width ) *width= dec->videoWidth;
      if ( height ) *height= dec->videoHeight;
   }
}

void EMSimpleVideoDecoderSetFrameRate( EMSimpleVideoDecoder *dec, float fps )
{
   dec->videoFrameRate= fps;
}

float EMSimpleVideoDecoderGetFrameRate( EMSimpleVideoDecoder *dec )
{
   return dec->videoFrameRate;
}

void EMSimpleVideoDecoderSetBitRate( EMSimpleVideoDecoder *dec, float MBps )
{
   dec->videoBitRate= MBps;
}

float EMSimpleVideoDecoderGetBitRate( EMSimpleVideoDecoder *dec )
{
   return dec->videoBitRate;
}

void EMSimpleVideoDecoderSetSegmentsStartAtZero( EMSimpleVideoDecoder *dec, bool startAtZero )
{
   dec->segmentsStartAtZero= startAtZero;
}

bool EMSimpleVideoDecoderGetSegmentsStartAtZero( EMSimpleVideoDecoder *dec )
{
   return dec->segmentsStartAtZero;
}

void EMSimpleVideoDecoderSetFrameNumber( EMSimpleVideoDecoder *dec, unsigned frameNumber )
{
   dec->firstPtsPassed= (frameNumber > 0);
   dec->frameNumber= frameNumber;
}

unsigned EMSimpleVideoDecoderGetFrameNumber( EMSimpleVideoDecoder *dec )
{
   return dec->frameNumber;
}

void EMSimpleVideoDecoderSetBasePTS(  EMSimpleVideoDecoder *dec, unsigned long long int pts )
{
   dec->basePTS= (pts & 0x1FFFFFFFFULL);
}

unsigned long long EMSimpleVideoDecoderGetBasePTS( EMSimpleVideoDecoder *dec )
{
   return dec->basePTS;
}

void EMSimpleVideoDecoderSignalUnderflow( EMSimpleVideoDecoder *dec )
{
   if ( dec )
   {
      dec->signalUnderflow= true;
   }
}

int EMWLEGLWindowGetSwapCount( struct wl_egl_window *w )
{
   return w->eglSwapCount;
}

void EMWLEGLWindowSetBufferRange( struct wl_egl_window *w, int base, int count )
{
   w->bufferIdBase= base;
   w->bufferIdCount= count;
   w->bufferId= w->bufferIdBase+w->bufferIdCount-1;
}

void EMSetTextureCreatedCallback( EMCTX *ctx, EMTextureCreated cb, void *userData )
{
   ctx->textureCreatedCB= cb;
   ctx->textureCreatedUserData= userData;
}

void EMSetHolePunchedCallback( EMCTX *ctx, EMHolePunched cb, void *userData )
{
   ctx->holePunchedCB= cb;
   ctx->holePunchedUserData= userData;
}

void EMPushGamepadEvent( EMCTX *ctx, int type, int id, int value )
{
   for( int i= 0; i < EM_DEVICE_MAX; ++i )
   {
      if ( ctx->devices[i].type == EM_DEVICE_TYPE_GAMEPAD )
      {
         if ( !strcmp( ctx->devices[i].path, "/dev/input/event2" ) )
         {
            TRACE1("EMPushGamepadEvent: event type %d id %d value %d pending", type, id, value);
            ctx->devices[i].dev.gamepad.eventType= type;
            ctx->devices[i].dev.gamepad.eventNumber= id;
            ctx->devices[i].dev.gamepad.eventValue= value;
            ctx->devices[i].dev.gamepad.eventPending= true;
         }
         else if ( strstr( ctx->devices[i].path, "js" ) )
         {
            int number= -1;
            switch( type )
            {
               case JS_EVENT_BUTTON:
                  for( int j= 0; j < ctx->devices[i].dev.gamepad.buttonCount; ++ j )
                  {
                     if ( ctx->devices[i].dev.gamepad.buttonMap[j] == id )
                     {
                        number= j;
                        break;
                     }
                  }
                  break;
               case JS_EVENT_AXIS:
                  for( int j= 0; j < ctx->devices[i].dev.gamepad.axisCount; ++ j )
                  {
                     if ( ctx->devices[i].dev.gamepad.axisMap[j] == id )
                     {
                        number= j;
                        break;
                     }
                  }
                  break;
            }
            if ( number >= 0 )
            {
               TRACE1("EMPushGamepadEvent: event type %d number %d value %d pending", type, number, value);
               ctx->devices[i].dev.gamepad.eventType= type;
               ctx->devices[i].dev.gamepad.eventNumber= number;
               ctx->devices[i].dev.gamepad.eventValue= value;
               ctx->devices[i].dev.gamepad.eventPending= true;
            }
            break;
         }
      }
   }
}





extern "C"
{
typedef struct _WstCompositor WstCompositor;

bool moduleInit( WstCompositor *ctx, struct wl_display* display )
{
   bool result= false;
   EMCTX *emctx= 0;

   emctx= emGetContext();
   if ( emctx )
   {
      emctx->westerosModuleInitCalled= true;
      result= !emctx->westerosModuleInitShouldFail;
   }

   return result;
}

void moduleTerm( WstCompositor *ctx )
{
   EMCTX *emctx= 0;

   emctx= emGetContext();
   if ( emctx )
   {
      emctx->westerosModuleTermCalled= true;
   }
}
} // extern "C"

static EMCTX* emGetContext( void )
{
   EMCTX *ctx= 0;

   pthread_mutex_lock( &gMutex );
   if ( !gCtx )
   {
      gCtx= emCreate();
   }
   ctx= gCtx;
   pthread_mutex_unlock( &gMutex );

   return ctx;
}

static EMCTX* emCreate( void )
{
   EMCTX* ctx= 0;
   const char *env;

   ctx= (EMCTX*)calloc( 1, sizeof(EMCTX) );
   if ( ctx )
   {
      ctx->displayWidth= DEFAULT_DISPLAY_WIDTH;
      ctx->displayHeight= DEFAULT_DISPLAY_HEIGHT;

      ctx->eglDisplayDefault= EGL_NO_DISPLAY;
      ctx->eglDisplayCurrent= EGL_NO_DISPLAY;
      ctx->eglContext= EGL_NO_CONTEXT;
      
      ctx->wlBindings= std::map<struct wl_display*,EMWLBinding>();
      ctx->gbmBuffs= std::vector<struct gbm_bo*>();

      ctx->deviceCount= 0;
      ctx->deviceNextFd= EM_DEVICE_FD_BASE;
      for( int i= 0; i < EM_DEVICE_MAX; ++i )
      {
         ctx->devices[i].type= EM_DEVICE_TYPE_NONE;
         ctx->devices[i].fd= -1;
      }

      ctx->simpleVideoDecoderMain.magic= EM_SIMPLE_VIDEO_DECODER_MAGIC;
      ctx->simpleVideoDecoderMain.inUse= false;
      ctx->simpleVideoDecoderMain.signalUnderflow= false;
      ctx->simpleVideoDecoderMain.ctx= ctx;
      ctx->simpleVideoDecoderMain.videoFrameRate= 60.0;
      ctx->simpleVideoDecoderMain.videoBitRate= 8.0;
      ctx->simpleVideoDecoderMain.basePTS= 0;

      ctx->videoCodec= 0;

      env= getenv("WESTEROS_UT_DEBUG");
      if ( env )
      {
         gActiveLevel= atoi(env);
      }

      // TBD
   }

exit:

   return ctx;
}

static void emDestroy( EMCTX* ctx )
{
   if ( ctx )
   {
      // TBD
      if ( ctx->wstGLCtx )
      {
         ctx->wstGLTerm( ctx->wstGLCtx );
         ctx->wstGLCtx= 0;
      }
      if ( ctx->moduleWstGL )
      {
         dlclose( ctx->moduleWstGL );
         ctx->moduleWstGL= 0;
      }
      free( ctx );
   }
}

// Section: Base ------------------------------------------------------

enum _EM_DRM_PROP_IDS
{
   EM_DRM_PROP_CRTC_ID= 0,
   EM_DRM_PROP_ACTIVE,
   EM_DRM_PROP_MODE_ID,
   EM_DRM_PROP_OUT_FENCE_PTR,
   EM_DRM_PROP_TYPE,
   EM_DRM_PROP_IN_FENCE_FD,
   EM_DRM_PROP_FB_ID,
   EM_DRM_PROP_CRTC_X,
   EM_DRM_PROP_CRTC_Y,
   EM_DRM_PROP_CRTC_W,
   EM_DRM_PROP_CRTC_H,
   EM_DRM_PROP_SRC_X,
   EM_DRM_PROP_SRC_Y,
   EM_DRM_PROP_SRC_W,
   EM_DRM_PROP_SRC_H
};

static void EMDrmDeviceInit( EMDevice *d )
{
   int i= 0;

   TRACE1("EMDrmDeviceInit");

   d->dev.drm.modes[i].hdisplay= 3840;
   d->dev.drm.modes[i].vdisplay= 2160;
   d->dev.drm.modes[i].vrefresh= 30;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER|DRM_MODE_TYPE_PREFERRED;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
   ++i;

   d->dev.drm.modes[i].hdisplay= 3840;
   d->dev.drm.modes[i].vdisplay= 2160;
   d->dev.drm.modes[i].vrefresh= 24;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
   ++i;

   d->dev.drm.modes[i].hdisplay= 1920;
   d->dev.drm.modes[i].vdisplay= 1080;
   d->dev.drm.modes[i].vrefresh= 60;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
   ++i;

   d->dev.drm.modes[i].hdisplay= 1920;
   d->dev.drm.modes[i].vdisplay= 1080;
   d->dev.drm.modes[i].vrefresh= 60;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_INTERLACE;
   ++i;

   d->dev.drm.modes[i].hdisplay= 1920;
   d->dev.drm.modes[i].vdisplay= 1080;
   d->dev.drm.modes[i].vrefresh= 50;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_INTERLACE;
   ++i;

   d->dev.drm.modes[i].hdisplay= 1920;
   d->dev.drm.modes[i].vdisplay= 1080;
   d->dev.drm.modes[i].vrefresh= 30;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
   ++i;

   d->dev.drm.modes[i].hdisplay= 1920;
   d->dev.drm.modes[i].vdisplay= 1080;
   d->dev.drm.modes[i].vrefresh= 24;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
   ++i;

   d->dev.drm.modes[i].hdisplay= 1280;
   d->dev.drm.modes[i].vdisplay= 720;
   d->dev.drm.modes[i].vrefresh= 60;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
   ++i;

   d->dev.drm.modes[i].hdisplay= 720;
   d->dev.drm.modes[i].vdisplay= 480;
   d->dev.drm.modes[i].vrefresh= 60;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
   ++i;

   d->dev.drm.modes[i].hdisplay= 720;
   d->dev.drm.modes[i].vdisplay= 480;
   d->dev.drm.modes[i].vrefresh= 60;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_INTERLACE;
   ++i;

   d->dev.drm.modes[i].hdisplay= 800;
   d->dev.drm.modes[i].vdisplay= 400;
   d->dev.drm.modes[i].vrefresh= 60;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
   ++i;

   d->dev.drm.modes[i].hdisplay= 640;
   d->dev.drm.modes[i].vdisplay= 480;
   d->dev.drm.modes[i].vrefresh= 60;
   d->dev.drm.modes[i].type= DRM_MODE_TYPE_DRIVER;
   d->dev.drm.modes[i].flags= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
   ++i;

   d->dev.drm.countModes= i;

   d->dev.drm.countCrtcs= 1;
   d->dev.drm.crtcs[0].crtc_id= ++d->dev.drm.nextId;
   d->dev.drm.crtcs[0].mode_valid= 1;
   d->dev.drm.crtcs[0].mode= d->dev.drm.modes[0];

   TRACE1("EMDrmDeviceInit: looking for mode to match display size %dx%d", d->ctx->displayWidth, d->ctx->displayHeight);
   for( int mi= 0; mi < d->dev.drm.countModes; ++mi )
   {
      if ( (d->dev.drm.modes[mi].hdisplay == d->ctx->displayWidth) && (d->dev.drm.modes[mi].vdisplay == d->ctx->displayHeight) )
      {
         TRACE1("EMDrmDeviceInit: found matching mode %d", mi);
         d->dev.drm.crtcs[0].mode= d->dev.drm.modes[mi];
         break;
      }
   }

   d->dev.drm.countEncoders= 1;
   d->dev.drm.encoders[0].encoder_id= ++d->dev.drm.nextId;
   d->dev.drm.encoders[0].crtc_id= d->dev.drm.crtcs[0].crtc_id;
   d->dev.drm.encoders[0].possible_crtcs= 0x1;

   d->dev.drm.countConnectors= 1;
   d->dev.drm.connectors[0].connector_id= ++d->dev.drm.nextId;
   d->dev.drm.connectors[0].encoder_id= d->dev.drm.encoders[0].encoder_id;
   d->dev.drm.connectors[0].connection= DRM_MODE_CONNECTED;
   d->dev.drm.connectors[0].count_modes= d->dev.drm.countModes;
   d->dev.drm.connectors[0].modes= d->dev.drm.modes;

   i= 0;
   d->dev.drm.planeFormats[i++]= DRM_FORMAT_ARGB8888;
   d->dev.drm.planeFormats[i++]= DRM_FORMAT_NV12;
   d->dev.drm.countPlaneFormats= i;

   d->dev.drm.countPlaneTypeEnum= 3;
   strcpy(d->dev.drm.planeTypeEnum[0].name, "Overlay" );
   d->dev.drm.planeTypeEnum[0].value= DRM_PLANE_TYPE_OVERLAY;
   strcpy(d->dev.drm.planeTypeEnum[1].name, "Primary" );
   d->dev.drm.planeTypeEnum[1].value= DRM_PLANE_TYPE_PRIMARY;
   strcpy(d->dev.drm.planeTypeEnum[2].name, "Cursor" );
   d->dev.drm.planeTypeEnum[2].value= DRM_PLANE_TYPE_CURSOR;

   d->dev.drm.videoPlane[0].positionIsPending= false;

   d->dev.drm.countPlanes= 3;
   i= 0;
   d->dev.drm.planes[i].plane_id= ++d->dev.drm.nextId;
   d->dev.drm.planes[i].count_formats= d->dev.drm.countPlaneFormats-1;
   d->dev.drm.planes[i].formats= &d->dev.drm.planeFormats[0];
   d->dev.drm.planes[i].crtc_id= 0;
   d->dev.drm.planes[i].fb_id= 0;
   d->dev.drm.planes[i].crtc_x= 0;
   d->dev.drm.planes[i].crtc_y= 0;
   d->dev.drm.planes[i].x= 0;
   d->dev.drm.planes[i].y= 0;
   d->dev.drm.planes[i].possible_crtcs= 0x1;
   d->dev.drm.planes[i].gamma_size= 0;
   ++i;   
   d->dev.drm.planes[i].plane_id= ++d->dev.drm.nextId;
   d->dev.drm.planes[i].count_formats= d->dev.drm.countPlaneFormats-1;
   d->dev.drm.planes[i].formats= &d->dev.drm.planeFormats[1];
   d->dev.drm.planes[i].crtc_id= 0;
   d->dev.drm.planes[i].fb_id= 0;
   d->dev.drm.planes[i].crtc_x= 0;
   d->dev.drm.planes[i].crtc_y= 0;
   d->dev.drm.planes[i].x= 0;
   d->dev.drm.planes[i].y= 0;
   d->dev.drm.planes[i].possible_crtcs= 0x1;
   d->dev.drm.planes[i].gamma_size= 0;
   ++i;   
   d->dev.drm.planes[i].plane_id= ++d->dev.drm.nextId;
   d->dev.drm.planes[i].count_formats= d->dev.drm.countPlaneFormats;
   d->dev.drm.planes[i].formats= d->dev.drm.planeFormats;
   d->dev.drm.planes[i].crtc_id= 0;
   d->dev.drm.planes[i].fb_id= 0;
   d->dev.drm.planes[i].crtc_x= 0;
   d->dev.drm.planes[i].crtc_y= 0;
   d->dev.drm.planes[i].x= 0;
   d->dev.drm.planes[i].y= 0;
   d->dev.drm.planes[i].possible_crtcs= 0x1;
   d->dev.drm.planes[i].gamma_size= 0;

   i= 0;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "CRTC_ID" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "ACTIVE" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "MODE_ID" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "OUT_FENCE_PTR" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "type" );
   d->dev.drm.properties[i].count_enums= d->dev.drm.countPlaneTypeEnum;
   d->dev.drm.properties[i].enums= d->dev.drm.planeTypeEnum;
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "FB_ID" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "IN_FENCE_FD" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "CRTC_X" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "CRTC_Y" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "CRTC_W" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "CRTC_H" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "SRC_X" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "SRC_Y" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "SRC_W" );
   ++i;
   d->dev.drm.properties[i].prop_id= ++d->dev.drm.nextId;
   strcpy( d->dev.drm.properties[i].name, "SRC_H" );
   ++i;
   d->dev.drm.countProperties= i;

   for( i= 0; i < EM_DRM_HANDLE_MAX; ++i )
   {
      d->dev.drm.handles[i].handle= i+1;
      d->dev.drm.handles[i].fd= -1;
      d->dev.drm.handles[i].fbId= 0;
   }

   if ( gDrmOpenCount == 0 )
   {
      int fd= open( "/tmp/em-drm.lock",
                        O_CREAT|O_CLOEXEC,
                        S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
      if ( fd >= 0 )
      {
         int rc= flock( fd, LOCK_NB|LOCK_EX );
         if ( rc < 0 )
         {
            gDrmHaveMaster= true;
            close( fd );
         }
         else
         {
            gDrmLockFd= fd;
         }
      }
      else
      {
         gDrmHaveMaster= true;
      }
   }
   ++gDrmOpenCount;
}

static void EMDrmDeviceTerm( EMDevice *d )
{
   TRACE1("EMDrmDeviceTerm");
   if ( gDrmOpenCount > 0 )
   {
      --gDrmOpenCount;
      if ( gDrmOpenCount == 0 )
      {
         if ( gDrmLockFd >= 0 )
         {
            flock( gDrmLockFd, LOCK_UN );
            close( gDrmLockFd );
            gDrmLockFd= -1;
         }
         gDrmHaveMaster= false;
         if ( gDrmMasterDev )
         {
            gDrmMasterDev->dev.drm.videoPlane[0].positionIsPending= false;
         }
         gDrmMasterDev= 0;
      }
   }
}

static void EMV4l2DeviceInit( EMDevice *d )
{
   int i;

   TRACE1("EMV4l2DeviceInit");

   memset( d->dev.v4l2.inputFormats, 0, EM_V4L2_FMT_MAX*sizeof(struct v4l2_fmtdesc) );

   i= 0;
   d->dev.v4l2.inputFormats[i].index= i;
   d->dev.v4l2.inputFormats[i].type= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
   d->dev.v4l2.inputFormats[i].flags= V4L2_FMT_FLAG_COMPRESSED;
   d->dev.v4l2.inputFormats[i].pixelformat= V4L2_PIX_FMT_H264;
   strcpy( (char*)d->dev.v4l2.inputFormats[i].description, "H.264" );
   ++i;

   d->dev.v4l2.inputFormats[i].index= i;
   d->dev.v4l2.inputFormats[i].type= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
   d->dev.v4l2.inputFormats[i].flags= V4L2_FMT_FLAG_COMPRESSED;
   d->dev.v4l2.inputFormats[i].pixelformat= V4L2_PIX_FMT_HEVC;
   strcpy( (char*)d->dev.v4l2.inputFormats[i].description, "HEVC" );
   ++i;

   d->dev.v4l2.inputFormats[i].index= i;
   d->dev.v4l2.inputFormats[i].type= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
   d->dev.v4l2.inputFormats[i].flags= V4L2_FMT_FLAG_COMPRESSED;
   d->dev.v4l2.inputFormats[i].pixelformat= V4L2_PIX_FMT_VP8;
   strcpy( (char*)d->dev.v4l2.inputFormats[i].description, "VP8" );
   ++i;

   d->dev.v4l2.inputFormats[i].index= i;
   d->dev.v4l2.inputFormats[i].type= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
   d->dev.v4l2.inputFormats[i].flags= V4L2_FMT_FLAG_COMPRESSED;
   d->dev.v4l2.inputFormats[i].pixelformat= V4L2_PIX_FMT_VP9;
   strcpy( (char*)d->dev.v4l2.inputFormats[i].description, "VP9" );
   ++i;

   d->dev.v4l2.countInputFormats= i;


   i= 0;
   d->dev.v4l2.outputFormats[i].index= i;
   d->dev.v4l2.outputFormats[i].type= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
   d->dev.v4l2.outputFormats[i].flags= 0;
   d->dev.v4l2.outputFormats[i].pixelformat= V4L2_PIX_FMT_NV12;
   strcpy( (char*)d->dev.v4l2.outputFormats[i].description, "Y/CbCr 4:2:0" );
   ++i;

   d->dev.v4l2.outputFormats[i].index= i;
   d->dev.v4l2.outputFormats[i].type= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
   d->dev.v4l2.outputFormats[i].flags= 0;
   d->dev.v4l2.outputFormats[i].pixelformat= V4L2_PIX_FMT_NV12M;
   strcpy( (char*)d->dev.v4l2.outputFormats[i].description, "Y/CbCr 4:2:0 (N-C)" );
   ++i;

   d->dev.v4l2.countOutputFormats= i;

   for( i= 0; i < EM_V4L2_OUTBUFF_MAX; ++i )
   {
      d->dev.v4l2.outputBufferFds[i*2].fd= -1;
      d->dev.v4l2.outputBufferFds[i*2].fd_os= -1;
      d->dev.v4l2.outputBufferFds[i*2+1].fd= -1;
      d->dev.v4l2.outputBufferFds[i*2+1].fd_os= -1;
   }

   for ( i= 0; i < EM_V4L2_MAP_MAX; ++i )
   {
      d->dev.v4l2.map[i]= 0;
   }
}

static void EMV4l2DeviceTerm( EMDevice *d )
{
   TRACE1("EMV4l2DeviceTerm");
   if ( d )
   {
      EMV4l2FreeInputBuffers( d );
      EMV4l2FreeOutputBuffers( d );
   }
}

static void EMGamepadDeviceInit( EMDevice *d )
{
   TRACE1("EMGamepadDeviceInit");

   d->dev.gamepad.version= 0x010203;
   d->dev.gamepad.deviceName= "EMGamepad";
   d->dev.gamepad.buttonCount= 4;
   d->dev.gamepad.buttonMap[0]= (uint16_t)BTN_A;
   d->dev.gamepad.buttonMap[1]= (uint16_t)BTN_B;
   d->dev.gamepad.buttonMap[2]= (uint16_t)BTN_X;
   d->dev.gamepad.buttonMap[3]= (uint16_t)BTN_Y;
   d->dev.gamepad.axisCount= 4;
   d->dev.gamepad.axisMap[0]= (uint8_t)ABS_X;
   d->dev.gamepad.axisMap[1]= (uint8_t)ABS_Y;
   d->dev.gamepad.axisMap[2]= (uint8_t)ABS_Z;
   d->dev.gamepad.axisMap[3]= (uint8_t)ABS_RZ;
}

static void EMGamepadDeviceTerm( EMDevice *d )
{
   TRACE1("EMGamepadDeviceTerm");
}

static void emSetBit( unsigned char *bits, int bit )
{
   int i= (bit/8);
   int m= (bit%8);
   bits[i] |= (1<<m);
}

static int EMGamepadIOctl( EMDevice *dev, int fd, int request, void *arg )
{
   int rc= -1;
   if ( !strcmp( dev->path, "/dev/input/event2" ) )
   {
      switch( request )
      {
         case EVIOCGVERSION:
            *((unsigned int*)arg)= dev->dev.gamepad.version;
            rc= 0;
            break;
          default:
            if ( (request & ~IOCSIZE_MASK) == EVIOCGNAME(0) )
            {
               int len= _IOC_SIZE(request);
               strncpy( (char*)arg, dev->dev.gamepad.deviceName, len );
               rc= strlen(dev->dev.gamepad.deviceName);
            }
            else if ( (request & ~(IOCSIZE_MASK|EV_MAX)) == EVIOCGBIT(0,0) )
            {
               int len= _IOC_SIZE(request);
               int ev= (request&EV_MAX);
               switch( ev )
               {
                  case EV_SYN:
                     memset( arg, 0, len);
                     ((unsigned char *)arg)[0]= ((1<<EV_KEY)|(1<<EV_ABS));
                     rc= 0;
                     break;
                  case EV_KEY:
                     memset( arg, 0, len);
                     emSetBit( (unsigned char*)arg, BTN_SOUTH );
                     emSetBit( (unsigned char*)arg, BTN_A );
                     emSetBit( (unsigned char*)arg, BTN_B );
                     emSetBit( (unsigned char*)arg, BTN_X );
                     emSetBit( (unsigned char*)arg, BTN_Y );
                     rc= 0;
                     break;
                  case EV_ABS:
                     memset( arg, 0, len);
                     emSetBit( (unsigned char*)arg, ABS_X );
                     emSetBit( (unsigned char*)arg, ABS_Y );
                     emSetBit( (unsigned char*)arg, ABS_Z );
                     emSetBit( (unsigned char*)arg, ABS_RZ );
                     rc= 0;
                     break;
                  default:
                     break;
               }
            }
            else if ( (_IOC_NR(request) & ~ABS_MAX) == _IOC_NR(EVIOCGABS(0)) )
            {
               struct input_absinfo *info= (struct input_absinfo*)arg;
               int ev= (request&ABS_MAX);
               info->minimum= -32768;
               info->maximum= 32767;
               rc= 0;
            }
            break;
      }
   }
   else if ( strstr( dev->path, "js" ) )
   {
      switch( request )
      {
         case JSIOCGVERSION:
            *((unsigned int*)arg)= dev->dev.gamepad.version;
            rc= 0;
            break;
         case JSIOCGBUTTONS:
            *((int*)arg)= dev->dev.gamepad.buttonCount;
            rc= 0;
            break;
         case JSIOCGAXES:
            *((int*)arg)= dev->dev.gamepad.axisCount;
            rc= 0;
            break;
         case JSIOCGBTNMAP:
            {
               uint16_t *map= (uint16_t*)arg;
               memcpy( map, dev->dev.gamepad.buttonMap, dev->dev.gamepad.buttonCount*sizeof(uint16_t) );
               rc= 0;
            }
            break;
         case JSIOCGAXMAP:
            {
               uint8_t *map= (uint8_t*)arg;
               memcpy( map, dev->dev.gamepad.axisMap, dev->dev.gamepad.axisCount*sizeof(uint8_t) );
               rc= 0;
            }
            break;
          default:
            if ( (request & ~IOCSIZE_MASK) == JSIOCGNAME(0) )
            {
               int len= _IOC_SIZE(request);
               strncpy( (char*)arg, dev->dev.gamepad.deviceName, len );
               rc= strlen(dev->dev.gamepad.deviceName);
            }
            break;
      }
   }
   return rc;
}

static int EMGamepadPoll( EMDevice *dev, struct pollfd *fds, int nfds, int timeout )
{
   int rc= 0;
   if ( dev->dev.gamepad.eventPending )
   {
      for( int i= 0; i < nfds; ++i )
      {
         if ( fds[i].fd == dev->fd )
         {
            TRACE1("EMGamepadPoll: POLLIN");
            fds[i].revents |= POLLIN;
            rc= 1;
            break;
         }
      }
   }
   return rc;
}

static int EMGamepadRead( EMDevice *dev, void *buf, size_t count )
{
   int rc= -1;

   TRACE1("EMGamepadRead: eventPending %d", dev->dev.gamepad.eventPending);
   if ( dev->dev.gamepad.eventPending )
   {
      if ( !strcmp( dev->path, "/dev/input/event2" ) )
      {
         struct input_event ev;
         memset( &ev, 0, sizeof(ev) );
         ev.type= dev->dev.gamepad.eventType;
         ev.code= dev->dev.gamepad.eventNumber;
         ev.value= dev->dev.gamepad.eventValue;
         dev->dev.gamepad.eventPending= false;
         memcpy( buf, &ev, sizeof(ev) );
         rc= sizeof(ev);
      }
      else if ( strstr( dev->path, "js" ) )
      {
         struct js_event js;
         memset( &js, 0, sizeof(js) );
         js.type= dev->dev.gamepad.eventType;
         js.number= dev->dev.gamepad.eventNumber;
         js.value= dev->dev.gamepad.eventValue;
         dev->dev.gamepad.eventPending= false;
         memcpy( buf, &js, sizeof(js) );
         rc= sizeof(js);
      }
   }

   return rc;
}

#define EMFDOSFILE_PREFIX "em-drm-"
#define EMFDOSFILE_TEMPLATE "/tmp/" EMFDOSFILE_PREFIX "%d-XXXXXX"

static int EMDeviceOpenOS( int fd )
{
   int fd_os= -1;
   char work[34];

   snprintf( work, sizeof(work), EMFDOSFILE_TEMPLATE, getpid() );
   fd_os= mkostemp( work, O_CLOEXEC );
   if ( fd_os >= 0 )
   {
     int len, lenwritten;
     len= snprintf( work, sizeof(work), "%d", fd );
     lenwritten= write( fd_os, work, len );
     if ( lenwritten != len )
     {
        ERROR("Unable to write to fd_os file");
        close( fd_os );
        fd_os= -1;
     }
   }
   else
   {
      ERROR("Unable to create fd_os temp file");
   }

   return fd_os;
}

static void EMDeviceCloseOS( int fd_os )
{
   int pid= getpid();
   int len, prefixlen;
   char path[32];
   char link[256];
   bool haveTempFilename= false;
   
   prefixlen= strlen(EMFDOSFILE_PREFIX);
   sprintf(path, "/proc/%d/fd/%d", pid, fd_os );
   len= readlink( path, link, sizeof(link)-1 );
   if ( len > prefixlen )
   {
      link[len]= '\0';
      if ( strstr( link, EMFDOSFILE_PREFIX ) )
      {
         haveTempFilename= true;
      }
   }
   
   close( fd_os );
   
   if ( haveTempFilename )
   {
      remove( link );
   }
}

int EMDeviceGetFdFromFdOS( int fd_os )
{
   int fd= -1;
   int rc;
   char work[34];
   rc= lseek( fd_os, 0, SEEK_SET );
   if ( rc >= 0 )
   {
      memset( work, 0, sizeof(work) );
      rc= read( fd_os, work, sizeof(work)-1 );
      if ( rc > 0 )
      {
         fd= atoi(work);
      }
   }
   else
   {
      ERROR("Unable to seek to start of fd_os %d", fd_os);
   }

   return fd;
}

static void EMDevicePruneOS()
{
   DIR *dir;
   struct dirent *result;
   struct stat fileinfo;
   int prefixLen;
   int pid, rc;
   char work[34];
   if ( NULL != (dir = opendir( "/tmp" )) )
   {
      prefixLen= strlen(EMFDOSFILE_PREFIX);
      while( NULL != (result = readdir( dir )) )
      {
         if ( (result->d_type != DT_DIR) &&
             !strncmp(result->d_name, EMFDOSFILE_PREFIX, prefixLen) )
         {
            snprintf( work, sizeof(work), "%s/%s", "/tmp", result->d_name);
            if ( sscanf( work, EMFDOSFILE_TEMPLATE, &pid ) == 1 )
            {
               rc= kill( pid, 0 );
               if ( (pid == getpid()) || (rc != 0) )
               {
                  // Remove file since owned by us or owning process nolonger exists
                  snprintf( work, sizeof(work), "%s/%s", "/tmp", result->d_name);
                  remove( work );
               }
            }
         }
      }

      closedir( dir );
   }
}

static int EMDeviceOpen( int type, const char *pathname, int flags )
{
   int fd= -1;
   EMCTX *ctx= 0;

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("EMDeviceOpen: emGetContext failed");
      goto exit;
   }

   if ( ctx->deviceCount < EM_DEVICE_MAX-1 )
   {
      for( int i= 0; i < EM_DEVICE_MAX; ++i )
      {
         if ( ctx->devices[i].fd == -1 )
         {
            ++ctx->deviceCount;
            fd= ctx->deviceNextFd++;
            TRACE1("EMDeviceOpen: open %s as fd %d type %d slot %d", pathname, fd, type, i);
            ctx->devices[i].magic= EM_DEVICE_MAGIC;
            ctx->devices[i].fd= fd;
            ctx->devices[i].fd_os= EMDeviceOpenOS(fd);
            ctx->devices[i].type= type;
            ctx->devices[i].path= strdup(pathname);
            ctx->devices[i].ctx= ctx;
            switch( type )
            {
               case EM_DEVICE_TYPE_DRM:
                  EMDrmDeviceInit( &ctx->devices[i] );
                  break;
               case EM_DEVICE_TYPE_V4L2:
                  EMV4l2DeviceInit( &ctx->devices[i] );
                  break;
               case EM_DEVICE_TYPE_GAMEPAD:
                  EMGamepadDeviceInit( &ctx->devices[i] );
                  break;
               default:
                  assert(false);
                  break;
            }
            break;
         }
      }
      
   }

exit:
   return fd;
}

static int EMDeviceClose( int fd )
{
   int rc= -1;
   EMCTX *ctx= 0;

   TRACE1("EMDeviceClose: fd %d", fd);
   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("EMDeviceClose: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < EM_DEVICE_MAX; ++i )
   {
      if ( ctx->devices[i].fd == fd )
      {
         TRACE1("EMDeviceClose: closing device fd %d, %s", fd, ctx->devices[i].path );
         if ( ctx->devices[i].path )
         {
            free( (void*)ctx->devices[i].path );
            ctx->devices[i].path= 0;
         }
         if ( ctx->devices[i].fd_os >= 0 )
         {
            EMDeviceCloseOS( ctx->devices[i].fd_os );
            ctx->devices[i].fd_os= -1;
         }
         switch( ctx->devices[i].type )
         {
            case EM_DEVICE_TYPE_DRM:
               EMDrmDeviceTerm( &ctx->devices[i] );
               break;
            case EM_DEVICE_TYPE_V4L2:
               EMV4l2DeviceTerm( &ctx->devices[i] );
               break;
            case EM_DEVICE_TYPE_GAMEPAD:
               EMGamepadDeviceTerm( &ctx->devices[i] );
               break;
            default:
               assert(false);
               break;
         }
         ctx->devices[i].fd= -1;
         ctx->devices[i].type= EM_DEVICE_TYPE_NONE;
         ctx->devices[i].magic= 0;
         --ctx->deviceCount;
         rc= 0;
         break;
      }
   }

exit:
   return rc;
}

int EMDeviceRead( int fd, void *buf, size_t count )
{
   int rc= -1;
   EMCTX *ctx= 0;

   TRACE1("EMDeviceRead: fd %d",fd);
   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("EMDeviceClose: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < EM_DEVICE_MAX; ++i )
   {
      if ( ctx->devices[i].fd == fd )
      {
         switch( ctx->devices[i].type )
         {
            case EM_DEVICE_TYPE_GAMEPAD:
               rc= EMGamepadRead( &ctx->devices[i], buf, count );
               break;
            default:
               assert(false);
               break;
         }
         break;
      }
   }
exit:
   return rc;
}

static int EMDrmIOctl( EMDevice *dev, int fd, int request, void *arg )
{
   int rc= -1;
   switch( request )
   {
      case DRM_IOCTL_SET_CLIENT_CAP:
         {
            struct drm_set_client_cap *clientCap= (struct drm_set_client_cap *)arg;
            if ( clientCap )
            {
               switch( clientCap->capability )
               {
                  case DRM_CLIENT_CAP_UNIVERSAL_PLANES:
                     rc= 0;
                     break;
                  case DRM_CLIENT_CAP_ATOMIC:
                     rc= 0;
                     break;
                  default:
                     break;
               }
            }
         }
         break;
      case DRM_IOCTL_GEM_CLOSE:
         {
            struct drm_gem_close *close= (struct drm_gem_close *)arg;
            TRACE1("DRM_IOCTL_GEM_CLOSE: handle %u", close->handle );
            for( int i= 0; i < EM_DRM_HANDLE_MAX; ++i )
            {
               if ( dev->dev.drm.handles[i].handle == close->handle )
               {
                  dev->dev.drm.handles[i].fd= -1;
                  dev->dev.drm.handles[i].fbId= 0;
                  rc= 0;
                  break;
               }
            }
         }
         break;
      default:
         break;
   }
   return rc;
}

static int EMV4l2MapBuffer( EMDevice *dev, int length )
{
   int offset= -1;
   int i;
   pthread_mutex_lock( &gMutex );
   for ( i= 0; i < EM_V4L2_MAP_MAX; ++i )
   {
      if ( dev->dev.v4l2.map[i] == 0 )
      {
         dev->dev.v4l2.map[i]= (unsigned char*)malloc( length );
         if ( dev->dev.v4l2.map[i] )
         {
            offset= i;
            break;
         }
      }
   }
   pthread_mutex_unlock( &gMutex );
   return offset;
}

static void *EMV4l2GetMap( EMDevice *dev, int offset )
{
   void *map= MAP_FAILED;
   pthread_mutex_lock( &gMutex );
   if ( (offset >= 0) && (offset < EM_V4L2_MAP_MAX) )
   {
      map= dev->dev.v4l2.map[offset];
   }
   pthread_mutex_unlock( &gMutex );
   return map;
}

static int EMV4l2ReleaseMap( EMDevice *dev, void *addr )
{
   int rc= -1;
   int i;
   pthread_mutex_lock( &gMutex );
   for ( i= 0; i < EM_V4L2_MAP_MAX; ++i )
   {
      if ( dev->dev.v4l2.map[i] == addr )
      {
         free( addr );
         dev->dev.v4l2.map[i]= 0;
         rc= 0;
         break;
      }
   }
   pthread_mutex_unlock( &gMutex );
   return rc;
}

static void EMV4l2FreeInputBuffers( EMDevice *dev )
{
   if ( dev )
   {
      for( int i= 0; i < dev->dev.v4l2.countInputBuffers; ++i )
      {
         void *addr= EMV4l2GetMap(dev, dev->dev.v4l2.inputBuffers[i].m.planes[0].m.mem_offset );
         if ( addr != MAP_FAILED )
         {
            EMV4l2ReleaseMap( dev, addr );
         }
      }
      dev->dev.v4l2.countInputBuffers= 0;
   }
}

static void EMV4l2FreeOutputBuffers( EMDevice *dev )
{
   if ( dev )
   {
      for( int i= 0; i < dev->dev.v4l2.countOutputBuffers; ++i )
      {
         for( int j= 0; j < 2; ++j )
         {
            void *addr= EMV4l2GetMap(dev, dev->dev.v4l2.outputBuffers[i].m.planes[j].m.mem_offset );
            if ( addr != MAP_FAILED )
            {
               EMV4l2ReleaseMap( dev, addr );
            }
         }
      }
      for( int i= 0; i < dev->dev.v4l2.countOutputBuffers; ++i )
      {
         for( int j= 0; j < 2; ++j )
         {
            int fd= dev->dev.v4l2.outputBufferFds[i*j+j].fd;
            if ( fd >= 0 )
            {
               int fd_os= dev->dev.v4l2.outputBufferFds[i*2+j].fd_os;
               if ( fd_os >= 0 )
               {
                  EMDeviceCloseOS( fd_os );
               }
               dev->dev.v4l2.outputBufferFds[i*2+j].fd= -1;
               dev->dev.v4l2.outputBufferFds[i*2+j].fd_os= -1;
            }
         }
      }
      dev->dev.v4l2.countOutputBuffers= 0;
   }
}

static void EMV4l2CheckFrameSize( EMDevice *dev, int *frameWidth, int *frameHeight )
{
   int w, h, width, height;

   width= *frameWidth;
   w= EM_V4L2_MIN_WIDTH + ((width-EM_V4L2_MIN_WIDTH)/EM_V4L2_STEP_WIDTH)*EM_V4L2_STEP_WIDTH;
   if ( w < width ) w += EM_V4L2_STEP_WIDTH;
   if ( w > EM_V4L2_MAX_WIDTH )
   {
      w= EM_V4L2_MAX_WIDTH;
   }
   *frameWidth= w;

   height= *frameHeight;
   h= EM_V4L2_MIN_HEIGHT + ((height-EM_V4L2_MIN_HEIGHT)/EM_V4L2_STEP_HEIGHT)*EM_V4L2_STEP_HEIGHT;
   if ( h < height ) h += EM_V4L2_STEP_HEIGHT;
   if ( h > EM_V4L2_MAX_HEIGHT )
   {
      h= EM_V4L2_MAX_HEIGHT;
   }
   *frameHeight= h;
}

static int EMV4l2IOctl( EMDevice *dev, int fd, int request, void *arg )
{
   int rc= -1;
   switch( request )
   {
      case VIDIOC_QUERYCAP:
         {
            struct v4l2_capability *caps= (struct v4l2_capability*)arg;

            TRACE1("VIDIOC_QUERYCAP");

            if ( !strcmp( dev->path, "/dev/video10") )
            { 
               memset( caps, 0, sizeof(struct v4l2_capability));
               caps->device_caps= V4L2_CAP_STREAMING|V4L2_CAP_EXT_PIX_FORMAT|V4L2_CAP_VIDEO_M2M_MPLANE;
               caps->capabilities= V4L2_CAP_DEVICE_CAPS | caps->device_caps;
               caps->version= 1;
               strncpy( (char*)caps->driver, "WesterosUTEM", 16 );
               strncpy( (char*)caps->card, "WesterosUTEM", 32 );
               strncpy( (char*)caps->bus_info, "WesterosUTEM", 32 );
               rc= 0;
            }
         }
         break;
      case VIDIOC_ENUM_FMT:
         {
            struct v4l2_fmtdesc *fmt= (struct v4l2_fmtdesc *)arg;

            TRACE1("VIDIOC_ENUM_FMT");

            switch( fmt->type )
            {
               case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                  if ( fmt->index < dev->dev.v4l2.countInputFormats )
                  {
                     fmt->flags= dev->dev.v4l2.inputFormats[fmt->index].flags;
                     fmt->pixelformat= dev->dev.v4l2.inputFormats[fmt->index].pixelformat;
                     strcpy( (char*)fmt->description, (char*)dev->dev.v4l2.inputFormats[fmt->index].description );
                     rc= 0;
                  }
                  else
                  {
                     rc= -1;
                     errno= EINVAL;
                  }
                  break;
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  if ( fmt->index < dev->dev.v4l2.countOutputFormats )
                  {
                     fmt->flags= dev->dev.v4l2.outputFormats[fmt->index].flags;
                     fmt->pixelformat= dev->dev.v4l2.outputFormats[fmt->index].pixelformat;
                     strcpy( (char*)fmt->description, (char*)dev->dev.v4l2.outputFormats[fmt->index].description );
                     rc= 0;
                  }
                  else
                  {
                     rc= -1;
                     errno= EINVAL;
                  }
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_ENUM_FRAMESIZES:
         {
            struct v4l2_frmsizeenum *fsz= (struct v4l2_frmsizeenum*)arg;

            TRACE1("VIDIOC_ENUM_FRAMESIZES");

            if ( fsz->index == 0 )
            {
               switch ( fsz->pixel_format )
               {
                  case V4L2_PIX_FMT_H264:
                  case V4L2_PIX_FMT_HEVC:
                  case V4L2_PIX_FMT_VP8:
                  case V4L2_PIX_FMT_VP9:
                     fsz->type= V4L2_FRMIVAL_TYPE_STEPWISE;
                     fsz->stepwise.min_width= EM_V4L2_MIN_WIDTH;
                     fsz->stepwise.max_width= EM_V4L2_MAX_WIDTH;
                     fsz->stepwise.step_width= EM_V4L2_STEP_WIDTH;
                     fsz->stepwise.min_height= EM_V4L2_MIN_HEIGHT;
                     fsz->stepwise.max_height= EM_V4L2_MAX_HEIGHT;
                     fsz->stepwise.step_height= EM_V4L2_STEP_HEIGHT;
                     rc= 0;
                     break;
                  default:
                     rc= -1;
                     errno= EINVAL;
                     break;
               }
            }
            else
            {
               rc= -1;
               errno= EINVAL;
            }
         }
         break;
      case VIDIOC_SUBSCRIBE_EVENT:
         {
            struct v4l2_event_subscription *sub= (struct v4l2_event_subscription*)arg;

            TRACE1("VIDIOC_SUBSCRIBE_EVENT");

            switch( sub->type )
            {
               case V4L2_EVENT_SOURCE_CHANGE:
                  dev->dev.v4l2.subscribeSourceChange= true;
                  rc= 0;
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }

         }
         break;
      case VIDIOC_UNSUBSCRIBE_EVENT:
         {
            struct v4l2_event_subscription *sub= (struct v4l2_event_subscription*)arg;

            TRACE1("VIDIOC_UNSUBSCRIBE_EVENT");

            switch( sub->type )
            {
               case V4L2_EVENT_SOURCE_CHANGE:
                  dev->dev.v4l2.subscribeSourceChange= false;
                  rc= 0;
                  break;
               case V4L2_EVENT_ALL:
                  dev->dev.v4l2.subscribeSourceChange= false;
                  rc= 0;
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }

         }
         break;
      case VIDIOC_G_CTRL:
         {
            struct v4l2_control *ctrl= (struct v4l2_control*)arg;

            TRACE1("VIDIOC_G_CTRL");

            rc= 0;
            switch( ctrl->id )
            {
               case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
                  ctrl->value= EM_V4L2_MIN_BUFFERS_FOR_CAPTURE;
                  break;
               case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_G_SELECTION:
         {
            struct v4l2_selection *selection= (struct v4l2_selection*)arg;

            TRACE1("VIDIOC_G_SELECTION");

            switch( selection->type )
            {
               // Some drivers fail with V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
               // so use V4L2_BUF_TYPE_VIDEO_CAPTURE in emulation
               case V4L2_BUF_TYPE_VIDEO_CAPTURE:
                  switch( selection->target )
                  {
                     case V4L2_SEL_TGT_COMPOSE:
                     case V4L2_SEL_TGT_COMPOSE_DEFAULT:
                        selection->r.left= 0;
                        selection->r.top= 0;
                        selection->r.width= dev->dev.v4l2.frameWidthSrc;
                        selection->r.height= dev->dev.v4l2.frameHeightSrc;
                        rc= 0;
                        break;
                     default:
                        rc= -1;
                        errno= EINVAL;
                        break;
                  }
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_G_FMT:
         {
            struct v4l2_format *fmt= (struct v4l2_format*)arg;

            TRACE1("VIDIOC_G_FMT");

            switch( fmt->type )
            {
               case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                  *fmt= dev->dev.v4l2.fmtIn;
                  fmt->type= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                  rc= 0;
                  break;
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  *fmt= dev->dev.v4l2.fmtOut;
                  fmt->type= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                  rc= 0;
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_S_FMT:
         {
            struct v4l2_format *fmt= (struct v4l2_format*)arg;

            TRACE1("VIDIOC_S_FMT");

            switch( fmt->type )
            {
               case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                  {
                     bool found= false;
                     for( int i= 0; i < dev->dev.v4l2.countInputFormats; ++i )
                     {
                        if ( fmt->fmt.pix_mp.pixelformat == dev->dev.v4l2.inputFormats[i].pixelformat )
                        {
                           found= true;
                           break;
                        }
                     }
                     if ( found )
                     {
                        dev->dev.v4l2.fmtIn= *fmt;
                        rc= 0;
                     }
                     else
                     {
                        rc= -1;
                        errno= EINVAL;
                     }
                  }
                  break;
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  {
                     bool found= false;
                     for( int i= 0; i < dev->dev.v4l2.countOutputFormats; ++i )
                     {
                        if ( fmt->fmt.pix_mp.pixelformat == dev->dev.v4l2.outputFormats[i].pixelformat )
                        {
                           found= true;
                           break;
                        }
                     }
                     if ( found )
                     {
                        int w, h;

                        dev->dev.v4l2.fmtOut= *fmt;

                        w= dev->dev.v4l2.fmtOut.fmt.pix_mp.width;
                        h= dev->dev.v4l2.fmtOut.fmt.pix_mp.height;

                        EMV4l2CheckFrameSize( dev, &w, &h );

                        dev->dev.v4l2.fmtOut.fmt.pix_mp.width= w;
                        dev->dev.v4l2.fmtOut.fmt.pix_mp.height= h;
                        rc= 0;
                     }
                     else
                     {
                        rc= -1;
                        errno= EINVAL;
                     }
                  }
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_REQBUFS:
         {
            struct v4l2_requestbuffers *reqbuf= (struct v4l2_requestbuffers *)arg;

            TRACE1("VIDIOC_REQBUFS");

            switch( reqbuf->type )
            {
               case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                  switch( reqbuf->memory )
                  {
                     case V4L2_MEMORY_MMAP:
                        if ( reqbuf->count > EM_V4L2_INBUFF_MAX )
                        {
                           reqbuf->count= EM_V4L2_INBUFF_MAX;
                        }
                        if ( reqbuf->count == 0 )
                        {
                           EMV4l2FreeInputBuffers( dev );
                        }
                        dev->dev.v4l2.inputMemoryMode= reqbuf->memory;
                        dev->dev.v4l2.countInputBuffers= reqbuf->count;
                        for( int i= 0; i < reqbuf->count; ++i )
                        {
                           dev->dev.v4l2.inputBuffers[i].type= reqbuf->type;
                           dev->dev.v4l2.inputBuffers[i].index= i;
                           dev->dev.v4l2.inputBuffers[i].length= 1;
                           dev->dev.v4l2.inputBuffers[i].memory= dev->dev.v4l2.inputMemoryMode;
                           dev->dev.v4l2.inputBuffers[i].m.planes= &dev->dev.v4l2.inputBufferPlanes[i];
                           dev->dev.v4l2.inputBuffers[i].m.planes[0].length= dev->dev.v4l2.fmtIn.fmt.pix_mp.plane_fmt[0].sizeimage;
                           dev->dev.v4l2.inputBuffers[i].m.planes[0].bytesused= 0;
                           dev->dev.v4l2.inputBuffers[i].m.planes[0].m.mem_offset= EMV4l2MapBuffer( dev, dev->dev.v4l2.inputBuffers[i].m.planes[0].length );
                        }
                        rc= 0;
                        break;
                     case V4L2_MEMORY_DMABUF:
                     case V4L2_MEMORY_USERPTR:
                     default:
                        rc= -1;
                        errno= EINVAL;
                        break;
                  }
                  dev->dev.v4l2.needBaseTime= true;
                  break;
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  switch( reqbuf->memory )
                  {
                     case V4L2_MEMORY_MMAP:
                        if ( reqbuf->count > EM_V4L2_OUTBUFF_MAX )
                        {
                           reqbuf->count= EM_V4L2_OUTBUFF_MAX;
                        }
                        if ( reqbuf->count == 0 )
                        {
                           EMV4l2FreeOutputBuffers( dev );
                        }
                        dev->dev.v4l2.outputMemoryMode= reqbuf->memory;
                        dev->dev.v4l2.countOutputBuffers= reqbuf->count;
                        for( int i= 0; i < reqbuf->count; ++i )
                        {
                           dev->dev.v4l2.outputBuffers[i].type= reqbuf->type;
                           dev->dev.v4l2.outputBuffers[i].index= i;
                           dev->dev.v4l2.outputBuffers[i].length= 2;
                           dev->dev.v4l2.outputBuffers[i].memory= dev->dev.v4l2.outputMemoryMode;
                           dev->dev.v4l2.outputBuffers[i].m.planes= &dev->dev.v4l2.outputBufferPlanes[i*2];
                           for ( int j= 0; j < dev->dev.v4l2.outputBuffers[i].length; ++j )
                           {
                              dev->dev.v4l2.outputBuffers[i].m.planes[j].length= dev->dev.v4l2.fmtOut.fmt.pix_mp.plane_fmt[j].sizeimage;
                              dev->dev.v4l2.outputBuffers[i].m.planes[j].bytesused= 0;
                              dev->dev.v4l2.outputBuffers[i].m.planes[j].m.mem_offset= EMV4l2MapBuffer( dev, dev->dev.v4l2.outputBuffers[i].m.planes[j].length );
                           }
                        }
                        rc= 0;
                        break;
                     case V4L2_MEMORY_DMABUF:
                     case V4L2_MEMORY_USERPTR:
                     default:
                        rc= -1;
                        errno= EINVAL;
                        break;
                  }
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_QUERYBUF:
         {
            struct v4l2_buffer *buf= (struct v4l2_buffer*)arg;

            TRACE1("VIDIOC_QUERYBUF");

            switch( buf->type )
            {
               case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                  if ( buf->index < dev->dev.v4l2.countInputBuffers )
                  {
                     buf->length= dev->dev.v4l2.inputBuffers[buf->index].length;
                     buf->flags= dev->dev.v4l2.inputBuffers[buf->index].flags;
                     buf->field= dev->dev.v4l2.inputBuffers[buf->index].field;
                     buf->memory= dev->dev.v4l2.inputBuffers[buf->index].memory;
                     buf->m.planes[0]= dev->dev.v4l2.inputBuffers[buf->index].m.planes[0];
                     rc= 0;
                  }
                  else
                  {
                     rc= -1;
                     errno= EINVAL;
                  }
                  break;
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  if ( buf->index < dev->dev.v4l2.countOutputBuffers )
                  {
                     buf->length= dev->dev.v4l2.outputBuffers[buf->index].length;
                     buf->flags= dev->dev.v4l2.outputBuffers[buf->index].flags;
                     buf->field= dev->dev.v4l2.outputBuffers[buf->index].field;
                     buf->memory= dev->dev.v4l2.outputBuffers[buf->index].memory;
                     for( int j= 0; j < buf->length; ++j )
                     {
                        buf->m.planes[j]= dev->dev.v4l2.outputBuffers[buf->index].m.planes[j];
                     }
                     rc= 0;
                  }
                  else
                  {
                     rc= -1;
                     errno= EINVAL;
                  }
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_EXPBUF:
         {
            struct v4l2_exportbuffer *expb= (struct v4l2_exportbuffer*)arg;

            TRACE1("VIDIOC_EXPBUF");

            switch( expb->type )
            {
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  if (
                       (expb->index < dev->dev.v4l2.countOutputBuffers) &&
                       (expb->plane < 2)
                     )
                  {
                     int fd= dev->ctx->deviceNextFd++;
                     dev->dev.v4l2.outputBufferFds[expb->index*2+expb->plane].fd= fd;
                     dev->dev.v4l2.outputBufferFds[expb->index*2+expb->plane].fd_os= EMDeviceOpenOS(fd);
                     expb->fd= dev->dev.v4l2.outputBufferFds[expb->index*2+expb->plane].fd_os;
                     rc= 0;
                  }
                  else
                  {
                     rc= -1;
                     errno= EINVAL;
                  }
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_QBUF:
         {
            struct v4l2_buffer *buf= (struct v4l2_buffer*)arg;

            TRACE1("VIDIOC_QBUF");

            switch( buf->type )
            {
               case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                  if ( buf->index < dev->dev.v4l2.countInputBuffers )
                  {
                     int i;
                     buf->flags |= V4L2_BUF_FLAG_QUEUED;
                     if ( dev->dev.v4l2.needBaseTime )
                     {
                        dev->dev.v4l2.needBaseTime= false;
                        dev->dev.v4l2.baseTime= buf->timestamp;
                     }
                     ++dev->dev.v4l2.readyFrameCount;
                     dev->dev.v4l2.inputBuffers[buf->index].flags |= V4L2_BUF_FLAG_QUEUED;
                     i= ((buf->index+1) % dev->dev.v4l2.countInputBuffers);
                     while( i != buf->index )
                     {
                        if ( dev->dev.v4l2.inputBuffers[i].flags & V4L2_BUF_FLAG_QUEUED )
                        {
                           dev->dev.v4l2.inputBuffers[i].flags &= ~V4L2_BUF_FLAG_QUEUED;
                           dev->dev.v4l2.inputBuffers[i].flags |= V4L2_BUF_FLAG_DONE;
                           break;
                        }
                        i= ((i+1) % dev->dev.v4l2.countInputBuffers);
                     }
                     if ( buf->m.planes[0].bytesused >= 8 )
                     {
                        int width, height;
                        unsigned char *data= (unsigned char*)EMV4l2GetMap( dev, buf->m.planes[0].m.mem_offset );
                        if ( data != MAP_FAILED )
                        {
                           width= ((data[0]<<24)|(data[1]<<16)|(data[2]<<8)|(data[3]));
                           height= ((data[4]<<24)|(data[5]<<16)|(data[6]<<8)|(data[7]));
                           if ( (width != dev->dev.v4l2.frameWidthSrc) || (height != dev->dev.v4l2.frameHeightSrc) )
                           {
                              int w, h;

                              dev->dev.v4l2.frameWidthSrc= width;
                              dev->dev.v4l2.frameHeightSrc= height;

                              w= width;
                              h= height;
                              EMV4l2CheckFrameSize( dev, &w, &h );

                              dev->dev.v4l2.frameWidth= w;
                              dev->dev.v4l2.frameHeight= h;

                              TRACE1("source frame %dx%d device frame %dx%d", width, height, w, h);

                              dev->dev.v4l2.fmtOut.fmt.pix_mp.width= w;
                              dev->dev.v4l2.fmtOut.fmt.pix_mp.height= h;

                              // TBD: source change event
                           }
                        }
                     }
                     rc= 0;
                  }
                  else
                  {
                     rc= -1;
                     errno= EINVAL;
                  }
                  break;
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  if ( buf->index < dev->dev.v4l2.countOutputBuffers )
                  {
                     int i;
                     buf->flags |= V4L2_BUF_FLAG_QUEUED;
                     dev->dev.v4l2.outputBuffers[buf->index].flags |= V4L2_BUF_FLAG_QUEUED;
                     i= ((buf->index+1) % dev->dev.v4l2.countOutputBuffers);
                     while( i != buf->index )
                     {
                        if ( dev->dev.v4l2.outputBuffers[i].flags & V4L2_BUF_FLAG_QUEUED )
                        {
                           dev->dev.v4l2.outputBuffers[i].flags &= ~V4L2_BUF_FLAG_QUEUED;
                           dev->dev.v4l2.outputBuffers[i].flags |= V4L2_BUF_FLAG_DONE;
                           break;
                        }
                        i= ((i+1) % dev->dev.v4l2.countOutputBuffers);
                     }
                     rc= 0;
                  }
                  else
                  {
                     rc= -1;
                     errno= EINVAL;
                  }
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_DQBUF:
         {
            struct v4l2_buffer *buf= (struct v4l2_buffer*)arg;

            TRACE1("VIDIOC_DQBUF");

            switch( buf->type )
            {
               case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                  rc= -1;
                  for( int i= 0; i < dev->dev.v4l2.countInputBuffers; ++i )
                  {
                     if ( dev->dev.v4l2.inputBuffers[i].flags & V4L2_BUF_FLAG_DONE )
                     {
                        dev->dev.v4l2.inputBuffers[i].flags &= ~(V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE);
                        *buf= dev->dev.v4l2.inputBuffers[i];
                        rc= 0;
                        break;
                     }
                  }
                  if ( rc < 0 )
                  {
                     errno= EINVAL;
                  }
                  break;
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  rc= -1;
                  if ( dev->dev.v4l2.outputStreaming )
                  {
                     bool gotFrame= false;
                     while( dev->dev.v4l2.outputStreaming && !gotFrame )
                     {
                        if ( dev->ctx->simpleVideoDecoderMain.signalUnderflow )
                        {
                           TRACE1("Producing underflow");
                           dev->ctx->simpleVideoDecoderMain.signalUnderflow= false;
                           usleep( 120000 );
                           TRACE1("Done underflow delay");
                        }
                        if ( dev->dev.v4l2.readyFrameCount > 0 )
                        {
                           --dev->dev.v4l2.readyFrameCount;
                           for( int i= 0; i < dev->dev.v4l2.countOutputBuffers; ++i )
                           {
                              if ( dev->dev.v4l2.outputBuffers[i].flags & V4L2_BUF_FLAG_DONE )
                              {
                                 if ( dev->dev.v4l2.outputStreaming )
                                 {
                                    time_t sec;
                                    suseconds_t usec;
                                    long long time;
                                    time= dev->dev.v4l2.baseTime.tv_sec * 1000000LL + dev->dev.v4l2.baseTime.tv_usec;
                                    time += ((dev->dev.v4l2.outputFrameCount * 1000000LL) / dev->ctx->simpleVideoDecoderMain.videoFrameRate);
                                    dev->dev.v4l2.outputBuffers[i].flags &= ~(V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE);
                                    *buf= dev->dev.v4l2.outputBuffers[i];
                                    buf->timestamp.tv_sec= (time / 1000000LL);
                                    buf->timestamp.tv_usec= (time % 1000000LL);
                                    ++dev->dev.v4l2.outputFrameCount;
                                    gotFrame= true;
                                    rc= 0;
                                 }
                                 break;
                              }
                           }
                        }
                        if ( !gotFrame )
                        {
                           usleep( 16000 );
                        }
                     }
                  }
                  if ( rc < 0 )
                  {
                     errno= EINVAL;
                  }
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_STREAMON:
         {
            int *type= (int*)arg;

            TRACE1("VIDIOC_STREAMON");

            switch( *type )
            {
               case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                  dev->dev.v4l2.inputStreaming= true;
                  rc= 0;
                  break;
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  dev->dev.v4l2.outputStreaming= true;
                  dev->dev.v4l2.outputFrameCount= 0;
                  rc= 0;
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;
      case VIDIOC_STREAMOFF:
         {
            int *type= (int*)arg;

            TRACE1("VIDIOC_STREAMOFF");

            switch( *type )
            {
               case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
                  dev->dev.v4l2.inputStreaming= false;
                  rc= 0;
                  break;
               case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
                  dev->dev.v4l2.outputStreaming= false;
                  dev->dev.v4l2.readyFrameCount= 0;
                  rc= 0;
                  break;
               default:
                  rc= -1;
                  errno= EINVAL;
                  break;
            }
         }
         break;

      //TBD

      default:
         break;
   }
   return rc;
}

static int EMV4l2Poll( EMDevice *dev, struct pollfd *fds, int nfds, int timeout )
{
   int rc= 0;
   for( int i= 0; i < dev->dev.v4l2.countOutputBuffers; ++i )
   {
      if ( dev->dev.v4l2.outputBuffers[i].flags & V4L2_BUF_FLAG_DONE )
      {
         rc= 1;
         fds->revents |= (POLLIN|POLLRDNORM);
         break;
      }
   }
   // TBD: events
   return rc;
}

static int EMDeviceIOctl( int fd, int request, void *arg )
{
   int rc= -1;
   EMCTX *ctx= 0;

   TRACE1("EMDeviceIOctl: fd %d request %d arg %p", fd, request, arg );

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("EMDeviceIOctl: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < EM_DEVICE_MAX; ++i )
   {
      if ( ctx->devices[i].fd == fd )
      {
         switch( ctx->devices[i].type )
         {
            case EM_DEVICE_TYPE_DRM:
               rc= EMDrmIOctl( &ctx->devices[i], fd, request, arg );
               break;
            case EM_DEVICE_TYPE_V4L2:
               rc= EMV4l2IOctl( &ctx->devices[i], fd, request, arg );
               break;
            case EM_DEVICE_TYPE_GAMEPAD:
               rc= EMGamepadIOctl( &ctx->devices[i], fd, request, arg );
               break;
         }
         break;
      }
   }

exit:
   return rc;
}

static void *EMDeviceMmap( void *addr, size_t length, int prot, int flags, int fd, off_t offset )
{
   void *map= MAP_FAILED;
   EMCTX *ctx= 0;

   TRACE1("EMDeviceMmap: fd %d length %d offset %d", fd, length, offset );

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("EMDeviceMmap: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < EM_DEVICE_MAX; ++i )
   {
      if ( ctx->devices[i].fd == fd )
      {
         switch( ctx->devices[i].type )
         {
            case EM_DEVICE_TYPE_DRM:
               break;
            case EM_DEVICE_TYPE_V4L2:
               map= EMV4l2GetMap( &ctx->devices[i], offset );
               break;
            case EM_DEVICE_TYPE_GAMEPAD:
               break;
         }
         break;
      }
   }

exit:
   return map;
}

static int EMDeviceMunmap( void *addr, size_t length )
{
   int rc= -1;
   EMCTX *ctx= 0;

   TRACE1("EMDeviceMunmap: addr %p length %d", addr, length );

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("EMDeviceMunmap: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < EM_DEVICE_MAX; ++i )
   {
      if ( ctx->devices[i].fd >= 0 )
      {
         switch( ctx->devices[i].type )
         {
            case EM_DEVICE_TYPE_DRM:
               break;
            case EM_DEVICE_TYPE_V4L2:
               if ( EMV4l2ReleaseMap( &ctx->devices[i], addr ) == 0 )
               {
                  rc= 0;
               }
               break;
            case EM_DEVICE_TYPE_GAMEPAD:
               break;
         }
      }
      if ( rc == 0 )
      {
         break;
      }
   }

exit:
   return rc;
}

static int EMDevicePoll( struct pollfd *fds, int nfds, int timeout )
{
   int rc= -1;
   EMCTX *ctx= 0;

   TRACE1("EMDevicePoll: fd %d nfds %d timeout %d", fds->fd, nfds, timeout );

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("EMDevicePoll: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < EM_DEVICE_MAX; ++i )
   {
      if ( ctx->devices[i].fd == fds->fd )
      {
         switch( ctx->devices[i].type )
         {
            case EM_DEVICE_TYPE_DRM:
               break;
            case EM_DEVICE_TYPE_V4L2:
               rc= EMV4l2Poll( &ctx->devices[i], fds, nfds, timeout );
               break;
            case EM_DEVICE_TYPE_GAMEPAD:
               rc= EMGamepadPoll( &ctx->devices[i], fds, nfds, timeout );
               break;
         }
         break;
      }
   }

exit:
   return rc;
}

extern "C"
{

int EMIOctl( int fd, int request, void *arg )
{
   int rc= -1;
   if ( fd >= EM_DEVICE_FD_BASE )
   {
      rc= EMDeviceIOctl( fd, request, arg );
   }
   else
   {
      rc= ioctl( fd, request, arg );
   }
   return rc;
}

void *EMMmap( void *addr, size_t length, int prot, int flags, int fd, off_t offset ) __THROW
{
   void *map= 0;
   if ( fd >= EM_DEVICE_FD_BASE )
   {
      map= EMDeviceMmap( addr, length, prot, flags, fd, offset );
   }
   else
   {
      map= mmap( addr, length, prot, flags, fd, offset );
   }
   return map;
}

int EMMunmap( void *addr, size_t length ) __THROW
{
   int rc= -1;
   rc= EMDeviceMunmap( addr, length );
   if ( rc < 0 )
   {
      rc= munmap( addr, length );
   }
   return rc;
}

int EMPoll( struct pollfd *fds, nfds_t nfds, int timeout )
{
   int rc= -1;
   if ( fds->fd >= EM_DEVICE_FD_BASE )
   {
      rc= EMDevicePoll( fds, nfds, timeout );
   }
   else
   {
      rc= poll( fds, nfds, timeout );
   }
   return rc;
}

int EMStat(const char *path, struct stat *buf) __THROW
{
   int rc= -1;

   TRACE1("EMStat (%s)", path);
   if ( strstr( path, "/dev/input/event" ) )
   {
      TRACE1("intercept stat of %s", path );
   }
   else if ( strstr( path, "/dev/input/js" ) )
   {
      TRACE1("intercept stat of %s", path );
   }
   else
   {
      goto passthru;
   }

   rc= 0;
   buf->st_mode= S_IFCHR;
   goto exit;

passthru:
   rc= stat( path, buf );

exit:
   return rc;
}

int EMOpen2( const char *pathname, int flags )
{
   int fd= -1;
   int type= EM_DEVICE_TYPE_NONE;

   TRACE1("open name %s flags %x", pathname, flags);

   if ( strstr( pathname, "/dev/dri/card" ) )
   {
      TRACE1("intercept open of %s", pathname );
      type= EM_DEVICE_TYPE_DRM;
   }
   else if ( strstr( pathname, "/dev/video" ) )
   {
      TRACE1("intercept open of %s", pathname );
      type= EM_DEVICE_TYPE_V4L2;
   }
   else if ( !strcmp( pathname, "/dev/input/event2" ) )
   {
      TRACE1("intercept open of %s", pathname );
      type= EM_DEVICE_TYPE_GAMEPAD;
   }
   else if ( strstr( pathname, "/dev/input/js" ) )
   {
      TRACE1("intercept open of %s", pathname );
      type= EM_DEVICE_TYPE_GAMEPAD;
   }
   else
   {
      goto passthru;
   }

   fd= EMDeviceOpen( type, pathname, flags );
   goto exit;

passthru:
  fd= open( pathname, flags );

exit:
   return fd;
}

int EMOpen3( const char *pathname, int flags, mode_t mode )
{
   int fd= -1;
   int type= EM_DEVICE_TYPE_NONE;

   TRACE1("open name %s flags %x mode %x\n", pathname, flags, mode);

   if ( strstr( pathname, "/dev/dri/card" ) )
   {
      TRACE1("intercept open of %s", pathname );
      type= EM_DEVICE_TYPE_DRM;
   }
   else if ( strstr( pathname, "/dev/video" ) )
   {
      TRACE1("intercept open of %s", pathname );
      type= EM_DEVICE_TYPE_V4L2;
   }
   else if ( !strcmp( pathname, "/dev/input/event2" ) )
   {
      TRACE1("intercept open of %s", pathname );
      type= EM_DEVICE_TYPE_GAMEPAD;
   }
   else if ( strstr( pathname, "/dev/input/js" ) )
   {
      TRACE1("intercept open of %s", pathname );
      type= EM_DEVICE_TYPE_GAMEPAD;
   }
   else
   {
      goto passthru;
   }

   fd= EMDeviceOpen( type, pathname, flags );
   goto exit;

passthru:
   fd= open( pathname, flags, mode );

exit:
   return fd;
}

int EMClose( int fd )
{
   int rc;
   if ( fd >= EM_DEVICE_FD_BASE )
   {
      rc= EMDeviceClose( fd );
   }
   else
   {
      rc= close( fd );
   }
   return rc;
}

ssize_t EMRead( int fd, void *buf, size_t count )
{
   int rc;
   TRACE1("EMRead: fd %d",fd);
   if ( fd >= EM_DEVICE_FD_BASE )
   {
      rc= EMDeviceRead( fd, buf, count );
   }
   else
   {
      rc= read( fd, buf, count );
   }
   return rc;
}


typedef struct EMDIR_
{
   int next;
   int count;
   const char *names[10];
   struct dirent entry;
} EMDIR;

DIR *EMOpenDir(const char *name)
{
   EMDIR *dir= 0;

   TRACE1("EMOpenDir (%s)", name);
   dir= (EMDIR*)calloc( 1, sizeof(EMDIR) );
   if ( dir )
   {
      int len= strlen(name);
      if ( (len == 4) && !strncmp( name, "/dev", len) )
      {
         dir->count= 1;
         dir->names[0]= "video10";
      }
      else
      if ( (len == 11) && !strncmp( name, "/dev/input/", len) )
      {
         dir->count= 2;
         dir->names[0]= "event2";
         dir->names[1]= "js0";
      }
      else
      {
         // return empty
      }
   }
   return (DIR*)dir;
}

int EMCloseDir(DIR *dirp)
{
   TRACE1("EMCloseDir");
   if ( dirp )
   {
      free( dirp );
   }
}

struct dirent *EMReadDir(DIR *dirp)
{
   struct dirent *result= 0;
   EMDIR *dir= (EMDIR*)dirp;

   TRACE1("EMReadDir");

   if ( dir )
   {
      if ( dir->next < dir->count )
      {
         result= &dir->entry;
         result->d_type= DT_CHR;
         strcpy( result->d_name, dir->names[dir->next] );
         TRACE1("EMReadDir: (%s)", result->d_name);
         ++dir->next;
      }
   }

   return result;
}

} //extern "C"

// Section DRM -------------------------------------------------------

extern "C"
{

EMDevice *EMDrmGetDevice( int fd )
{
   EMDevice *dev= 0;
   EMCTX *ctx= 0;

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("EMDrmGetDevice: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < EM_DEVICE_MAX; ++i )
   {
      if ( ctx->devices[i].fd == fd )
      {
         switch( ctx->devices[i].type )
         {
            case EM_DEVICE_TYPE_DRM:
               dev= &ctx->devices[i];
               break;
            default:
               ERROR("Bad EM device fd %d", fd);
               break;
         }
         break;
      }
   }

exit:
   return dev;
}

drmVersionPtr drmGetVersion(int fd)
{
   drmVersionPtr ver= 0;
   EMDevice *dev= 0;

   TRACE1("drmGetVersion: fd %d", fd);

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      ver= calloc( 1, sizeof(struct drm_version) );
      if ( ver )
      {
         ver->version_major= 1;
         ver->version_minor= 0;
         ver->version_patchlevel= 0;

         ver->name= strdup("meson");
         if ( ver->name )
         {
            ver->name_len= strlen(ver->name);
         }

         ver->date= strdup("20180321");
         if ( ver->date )
         {
            ver->date_len= strlen(ver->date);
         }

         ver->desc= strdup("Amlogic Meson DRM driver");
         if ( ver->desc )
         {
            ver->desc_len= strlen(ver->desc);
         }
      }
   }

   return ver;
}

void drmFreeVersion( drmVersionPtr ver )
{
   TRACE1("drmFreeVersion: ver %p", ver);

   if ( ver )
   {
      if ( ver->name )
      {
         free( ver->name );
      }
      if ( ver->date )
      {
         free( ver->date );
      }
      if ( ver->desc )
      {
         free( ver->desc );
      }
      free( ver );
   }
}

int drmSetMaster( int fd )
{
   int rc= -1;
   EMDevice *dev= 0;

   TRACE1("drmSetMaster: fd %d", fd);

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      if ( gDrmHaveMaster )
      {
         rc= 13;
      }
      else
      {
         gDrmMasterDev= dev;
         gDrmHaveMaster= true;
         rc= 0;
      }
   }
   return rc;
}

int drmWaitVBlank( int fd, drmVBlankPtr vbl )
{
   int rc= 0;

   TRACE1("drmWaitVBlank");

   usleep( 16000 );

   if ( vbl )
   {
      int rc;
      struct timespec tm;
      rc= clock_gettime( CLOCK_MONOTONIC, &tm );
      if ( !rc )
      {
         vbl->reply.tval_sec= tm.tv_sec;
         vbl->reply.tval_usec= tm.tv_nsec/1000LL;
         vbl->reply.tval_usec += 667;
         if ( vbl->reply.tval_usec > 1000000)
         {
            vbl->reply.tval_usec -= 1000000;
            vbl->reply.tval_sec += 1;
         }
      }
      else
      {
         ERROR("clock_gettime failed: rc %d errno %d\n", rc, errno);
      }
   }

   return rc;
}

drmModeResPtr drmModeGetResources(int fd)
{
   drmModeRes *res= 0;
   EMDevice *dev= 0;
   int i;

   TRACE1("drmModeGetResources: fd %d", fd);

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      res= (drmModeRes*)calloc( 1, sizeof(drmModeRes) );
      if ( res )
      {
         res->count_crtcs= dev->dev.drm.countCrtcs;
         res->crtcs= (uint32_t*)calloc( 1, res->count_crtcs*sizeof(uint32_t));
         if ( !res->crtcs )
         {
            goto error;
         }
         for( i= 0; i < res->count_crtcs; ++i )
         {
            res->crtcs[i]= dev->dev.drm.crtcs[i].crtc_id;
         }

         res->count_connectors= dev->dev.drm.countConnectors;
         res->connectors= (uint32_t*)calloc( 1, res->count_connectors*sizeof(uint32_t));
         if ( !res->connectors )
         {
            goto error;
         }
         for( i= 0; i < res->count_connectors; ++i )
         {
            res->connectors[i]= dev->dev.drm.connectors[i].connector_id;
         }

         res->count_encoders= dev->dev.drm.countEncoders;
         res->encoders= (uint32_t*)calloc( 1, res->count_encoders*sizeof(uint32_t));
         if ( !res->encoders )
         {
            goto error;
         }
         for( i= 0; i < res->count_encoders; ++i )
         {
            res->encoders[i]= dev->dev.drm.encoders[i].encoder_id;
         }

         goto exit;
      }
   }

error:
   drmModeFreeResources( res );
   res= 0;

exit:
   return res;
}

void drmModeFreeResources( drmModeResPtr ptr )
{
   TRACE1("drmModeFreeResources: ptr %p");
   if ( ptr )
   {
      if ( ptr->crtcs )
      {
         free( ptr->crtcs );
      }
      free( ptr );
   }
}

drmModeConnectorPtr drmModeGetConnector( int fd, uint32_t connectorId )
{
   drmModeConnector *conn= 0;
   EMDevice *dev= 0;
   int i;

   TRACE1("drmModeGetConnector: fd %d, connectorId %u", fd, connectorId);

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      for( i= 0; i < dev->dev.drm.countConnectors; ++i )
      {
         if ( dev->dev.drm.connectors[i].connector_id == connectorId )
         {
            conn= (drmModeConnector*)calloc( 1, sizeof(drmModeConnector) );
            if ( conn )
            {
               *conn= dev->dev.drm.connectors[i];
               break;
            }
         }
      }
   }

   return conn;
}

void drmModeFreeConnector( drmModeConnectorPtr ptr )
{
   TRACE1("drmModeFreeConnector: ptr %p", ptr);
   if ( ptr )
   {
      free( ptr );
   }
}

drmModeEncoderPtr drmModeGetEncoder( int fd, uint32_t encoderId )
{
   drmModeEncoder *enc= 0;
   EMDevice *dev= 0;
   int i;

   TRACE1("drmModeGetEncoder: fd %d encoderId %u", fd, encoderId);

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      for( i= 0; i < dev->dev.drm.countEncoders; ++i )
      {
         if ( dev->dev.drm.encoders[i].encoder_id == encoderId )
         {
            enc= (drmModeEncoder*)calloc( 1, sizeof(drmModeEncoder) );
            if ( enc )
            {
               *enc= dev->dev.drm.encoders[i];
               break;
            }
         }
      }
   }

   return enc;
}

void drmModeFreeEncoder( drmModeEncoderPtr ptr )
{
   TRACE1("drmModeFreeEncoder: ptr %p", ptr);
   if ( ptr )
   {
      free( ptr );
   }
}

drmModeCrtcPtr drmModeGetCrtc( int fd, uint32_t crtcId )
{
   drmModeCrtc *crtc= 0;
   EMDevice *dev= 0;
   int i;

   TRACE1("drmModeGetCrtc: fd %d crtcId %u", fd, crtcId);

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      for( i= 0; i < dev->dev.drm.countCrtcs; ++i )
      {
         if ( dev->dev.drm.crtcs[i].crtc_id == crtcId )
         {
            crtc= (drmModeCrtc*)calloc( 1, sizeof(drmModeCrtc) );
            if ( crtc )
            {
               *crtc= dev->dev.drm.crtcs[i];
               break;
            }
         }
      }
   }

   return crtc;
}

void drmModeFreeCrtc( drmModeCrtcPtr ptr )
{
   TRACE1("drmModeFreeCrtc: ptr %p", ptr);
   if ( ptr )
   {
      free( ptr );
   }
}

drmModeObjectPropertiesPtr drmModeObjectGetProperties( int fd, uint32_t objectId, uint32_t objectType )
{
   drmModeObjectProperties *props= 0;
   EMDevice *dev= 0;
   int i;

   TRACE1("drmModeObjectGetProperties: fd %d objectId %u objectType %u", fd, objectId, objectType );

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      switch( objectType )
      {
         case DRM_MODE_OBJECT_CONNECTOR:
            for( i= 0; i < dev->dev.drm.countConnectors; ++i )
            {
               if ( dev->dev.drm.connectors[i].connector_id == objectId )
               {
                  props= (drmModeObjectProperties*)calloc( 1, sizeof(drmModeObjectProperties));
                  if ( props )
                  {
                     props->count_props= 1;
                     props->props= (uint32_t*)calloc( props->count_props, sizeof(uint32_t) );
                     props->prop_values= (uint64_t*)calloc( props->count_props, sizeof(uint64_t) );
                     if ( props->props && props->prop_values )
                     {
                        if ( props->props )
                        {
                           props->props[0]= dev->dev.drm.properties[EM_DRM_PROP_CRTC_ID].prop_id;
                        }
                        if ( props->prop_values )
                        {               
                           props->prop_values[0]= dev->dev.drm.crtcs[0].crtc_id;
                        }
                     }
                     else
                     {
                        drmModeFreeObjectProperties( props );
                        props= 0;
                     }
                  }
                  break;
               }
            }
            break;
         case DRM_MODE_OBJECT_CRTC:
            for( i= 0; i < dev->dev.drm.countCrtcs; ++i )
            {
               if ( dev->dev.drm.crtcs[i].crtc_id == objectId )
               {
                  props= (drmModeObjectProperties*)calloc( 1, sizeof(drmModeObjectProperties));
                  if ( props )
                  {
                     props->count_props= 3;
                     props->props= (uint32_t*)calloc( props->count_props, sizeof(uint32_t) );
                     props->prop_values= (uint64_t*)calloc( props->count_props, sizeof(uint64_t) );
                     if ( props->props && props->prop_values )
                     {
                        if ( props->props )
                        {
                           props->props[0]= dev->dev.drm.properties[EM_DRM_PROP_ACTIVE].prop_id;
                           props->props[1]= dev->dev.drm.properties[EM_DRM_PROP_MODE_ID].prop_id;
                           props->props[2]= dev->dev.drm.properties[EM_DRM_PROP_OUT_FENCE_PTR].prop_id;
                        }
                        if ( props->prop_values )
                        {               
                           props->prop_values[0]= 1;
                           props->prop_values[1]= 0;
                           props->prop_values[2]= dev->dev.drm.crtcOutFenceFd;
                        }
                     }
                     else
                     {
                        drmModeFreeObjectProperties( props );
                        props= 0;
                     }
                  }
                  break;
               }
            }
            break;
         case DRM_MODE_OBJECT_PLANE:
            for( i= 0; i < dev->dev.drm.countPlanes; ++i )
            {
               if ( dev->dev.drm.planes[i].plane_id == objectId )
               {
                  props= (drmModeObjectProperties*)calloc( 1, sizeof(drmModeObjectProperties));
                  if ( props )
                  {
                     props->count_props= 12;
                     props->props= (uint32_t*)calloc( props->count_props, sizeof(uint32_t) );
                     props->prop_values= (uint64_t*)calloc( props->count_props, sizeof(uint64_t) );
                     if ( props->props && props->prop_values )
                     {
                        if ( props->props )
                        {
                           props->props[0]= dev->dev.drm.properties[EM_DRM_PROP_TYPE].prop_id;
                           props->props[1]= dev->dev.drm.properties[EM_DRM_PROP_FB_ID].prop_id;
                           props->props[2]= dev->dev.drm.properties[EM_DRM_PROP_IN_FENCE_FD].prop_id;
                           props->props[3]= dev->dev.drm.properties[EM_DRM_PROP_CRTC_ID].prop_id;
                           props->props[4]= dev->dev.drm.properties[EM_DRM_PROP_CRTC_X].prop_id;
                           props->props[5]= dev->dev.drm.properties[EM_DRM_PROP_CRTC_Y].prop_id;
                           props->props[6]= dev->dev.drm.properties[EM_DRM_PROP_CRTC_W].prop_id;
                           props->props[7]= dev->dev.drm.properties[EM_DRM_PROP_CRTC_H].prop_id;
                           props->props[8]= dev->dev.drm.properties[EM_DRM_PROP_SRC_X].prop_id;
                           props->props[9]= dev->dev.drm.properties[EM_DRM_PROP_SRC_Y].prop_id;
                           props->props[10]= dev->dev.drm.properties[EM_DRM_PROP_SRC_W].prop_id;
                           props->props[11]= dev->dev.drm.properties[EM_DRM_PROP_SRC_H].prop_id;
                        }
                        if ( props->prop_values )
                        {
                           if ( objectId == dev->dev.drm.planes[0].plane_id )
                           {
                              props->prop_values[0]= DRM_PLANE_TYPE_PRIMARY;
                           }
                           else if ( objectId == dev->dev.drm.planes[1].plane_id )
                           {
                              props->prop_values[0]= DRM_PLANE_TYPE_OVERLAY;
                           }
                           else if ( objectId == dev->dev.drm.planes[2].plane_id )
                           {
                              props->prop_values[0]= DRM_PLANE_TYPE_CURSOR;
                           }
                           else
                           {
                              props->prop_values[0]= DRM_PLANE_TYPE_OVERLAY;
                           }
                           props->prop_values[1]= 0;
                           props->prop_values[2]= -1;
                           props->prop_values[3]= 0;
                           props->prop_values[4]= 0;
                           props->prop_values[5]= 0;
                           props->prop_values[6]= 0;
                           props->prop_values[7]= 0;
                           props->prop_values[8]= 0;
                           props->prop_values[9]= 0;
                           props->prop_values[10]= 0;
                           props->prop_values[11]= 0;
                        }
                     }
                     else
                     {
                        drmModeFreeObjectProperties( props );
                        props= 0;
                     }
                  }
                  break;
               }
            }
            break;
         default:
            ERROR("unexpected object type: %u", objectType);
            break;
      }
   }

   return props;
}

void drmModeFreeObjectProperties( drmModeObjectPropertiesPtr ptr )
{
   TRACE1("drmModeFreeObjectProperties: ptr %p", ptr);
   if ( ptr )
   {
      if ( ptr->props )
      {
         free( ptr->props );
      }
      if ( ptr->prop_values )
      {
         free( ptr->prop_values );
      }
      free( ptr );
   }
}

drmModePropertyPtr drmModeGetProperty( int fd, uint32_t propertyId )
{
   drmModePropertyRes *propres= 0;
   EMDevice *dev= 0;
   int i;

   TRACE1("drmModeGetProperty: fd %d propertyId %u", fd, propertyId );

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      for( i= 0; i < dev->dev.drm.countProperties; ++i )
      {
         if ( dev->dev.drm.properties[i].prop_id == propertyId )
         {
            propres= (drmModePropertyRes*)malloc( sizeof(drmModePropertyRes) );
            if ( propres )
            {
               *propres= dev->dev.drm.properties[i];
            }
            break;
         }
      }
   }

   return propres;
}

void drmModeFreeProperty( drmModePropertyPtr ptr )
{
   TRACE1("drmModeFreeProperty: ptr %p", ptr);
   if ( ptr )
   {
      free( ptr );
   }
}

drmModePlaneResPtr drmModeGetPlaneResources( int fd )
{
   drmModePlaneRes *planeres= 0;
   EMDevice *dev= 0;
   int i;

   TRACE1("drmModeGetPlaneRes: fd %d", fd );

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      planeres= (drmModePlaneRes*)calloc( 1, sizeof(drmModePlaneRes));
      if ( planeres )
      {
         planeres->count_planes= dev->dev.drm.countPlanes;
         planeres->planes= (uint32_t*)malloc( sizeof(uint32_t)*dev->dev.drm.countPlanes );
         if ( !planeres->planes )
         {
            goto error;
         }
         for( i= 0; i < dev->dev.drm.countPlanes; ++i )
         {
            planeres->planes[i]= dev->dev.drm.planes[i].plane_id;
         }

         goto exit;
      }
   }

error:
   drmModeFreePlaneResources( planeres );
   planeres= 0;

exit:
   return planeres;
}

void drmModeFreePlaneResources( drmModePlaneResPtr ptr )
{
   TRACE1("drmModeFreePlaneResources: ptr %p", ptr);
   if ( ptr )
   {
      if ( ptr->planes )
      {
         free( ptr->planes );
      }
   }
}

drmModePlanePtr drmModeGetPlane( int fd, uint32_t planeId )
{
   drmModePlane *plane= 0;
   EMDevice *dev= 0;
   int i;

   TRACE1("drmModeGetPlane: fd %d planeId %u", fd, planeId );

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      for( i= 0; i < dev->dev.drm.countPlanes; ++i )
      {
         if ( dev->dev.drm.planes[i].plane_id == planeId )
         {
            plane= (drmModePlane*)calloc( 1, sizeof(drmModePlane) );
            if ( plane )
            {
               *plane= dev->dev.drm.planes[i];
            }
            break;
         }
      }
   }

   return plane;
}

void drmModeFreePlane( drmModePlanePtr ptr )
{
   TRACE1("drmModeFreePlane: ptr %p", ptr);
   if ( ptr )
   {
      free( ptr );
   }
}

int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
   int rc= -1;
   EMDevice *dev= 0;
   uint32_t hndl= 0;

   TRACE1("drmPrimeFDToHandle: fd %d prime_fd %d", fd, prime_fd);

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      for( int i= 0; i < EM_DRM_HANDLE_MAX; ++i )
      {
         if ( dev->dev.drm.handles[i].fd == -1 )
         {
            hndl= dev->dev.drm.handles[i].handle;
            dev->dev.drm.handles[i].fd= prime_fd;
            dev->dev.drm.handles[i].fbId= 0;
            rc= 0;
            break;
         }
      }
   }

   *handle= hndl;

   return rc;
}

int drmModeAddFB( int fd, uint32_t width, uint32_t height,
                  uint8_t depth, uint8_t bpp, uint32_t pitch,
                  uint32_t bo_handle, uint32_t *buf_id )
{
   int rc= -1;
   EMDevice *dev= 0;

   TRACE1("drmModeAddFB: fd %d bo_handle %u", fd, bo_handle );

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      struct gbm_bo *bo= 0;
      for( std::vector<struct gbm_bo*>::iterator it= dev->ctx->gbmBuffs.begin();
           it != dev->ctx->gbmBuffs.end();
           ++it )
      {
         if ( (*it)->handle.u32 == bo_handle )
         {
            bo= (*it);
            *buf_id= bo->fbId;
            break;
         }
      }
      if ( bo )
      {
         TRACE1("bo_handle %u is bo %p", bo_handle, bo);
         rc= 0;
      }
   }

   return rc;
}

int drmModeAddFB2( int fd, uint32_t width, uint32_t height,
                   uint32_t pixel_format, const uint32_t bo_handles[4],
                   const uint32_t pitches[4], const uint32_t offsets[4],
                   uint32_t *buf_id, uint32_t flags )
{
   int rc= -1;
   EMDevice *dev= 0;
   uint32_t fbId= 0;

   TRACE1("drmModeAddFB2: fd %d %dx%d handles (%u, %u, %u, %u)",
          fd, width, height, bo_handles[0], bo_handles[1], bo_handles[2], bo_handles[3] );

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      bool haveHandle[4];
      bool foundHandle[4];
      rc= 0;
      for( int i= 0; i < 4; ++i )
      {
         haveHandle[i]= false;
         foundHandle[i]= false;
         if ( bo_handles[i] != 0 )
         {
            haveHandle[i]= true;
            for( int j= 0; j < EM_DRM_HANDLE_MAX; ++j )
            {
               if ( dev->dev.drm.handles[j].handle == bo_handles[i] )
               {
                  foundHandle[i]= true;
                  break;
               }
            }
         }
         if ( haveHandle[i] && !foundHandle[i] )
         {
            rc= -1;
            break;
         }
      }
      if ( rc == 0 )
      {
         fbId= ++dev->dev.drm.nextId;
         for( int i= 0; i < 4; ++i )
         {
            for( int j= 0; j < EM_DRM_HANDLE_MAX; ++j )
            {
               if ( dev->dev.drm.handles[j].handle == bo_handles[i] )
               {
                  dev->dev.drm.handles[j].fbId= fbId;
               }
            }
         }
      }
   }

   *buf_id= fbId;

   return rc;
}

int drmModeAddFB2WithModifiers(int fd, uint32_t width, uint32_t height,
                               uint32_t pixel_format, const uint32_t bo_handles[4],
                               const uint32_t pitches[4], const uint32_t offsets[4],
                               const uint64_t modifier[4], uint32_t *buf_id,
                               uint32_t flags)
{
   int rc= -1;
   EMDevice *dev= 0;
   uint32_t fbId= 0;

   TRACE1("drmModeAddFB2WithModifiers: fd %d %dx%d handles (%u, %u, %u, %u)",
          fd, width, height, bo_handles[0], bo_handles[1], bo_handles[2], bo_handles[3] );

   rc= drmModeAddFB2( fd, width, height, pixel_format,
                      bo_handles, pitches, offsets,
                      buf_id, flags );

   return rc;
}

int drmModeRmFB( int fd, uint32_t bufferId )
{
   int rc= -1;
   EMDevice *dev= 0;

   TRACE1("drmModeRmFB: fd %d bufferId %u", fd, bufferId );

   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      for( std::vector<struct gbm_bo*>::iterator it= dev->ctx->gbmBuffs.begin();
           it != dev->ctx->gbmBuffs.end();
           ++it )
      {
         if ( (*it)->fbId == bufferId )
         {
            rc= 0;
            break;
         }
      }
      if ( rc != 0 )
      {
         for( int i= 0; i < EM_DRM_HANDLE_MAX; ++i )
         {
            if ( bufferId= dev->dev.drm.handles[i].fbId )
            {
               dev->dev.drm.handles[i].fbId= 0;
               rc= 0;
            }
         }
      }
   }

   return rc;
}

#define EM_DRM_ATOMIC_MAGIC (0x98126527)
struct _drmModeAtomicReq
{
   uint32_t magic;
};

drmModeAtomicReqPtr drmModeAtomicAlloc(void)
{
   drmModeAtomicReq *req= 0;
   TRACE1("drmModeAtomicAlloc");
   req= (drmModeAtomicReq*)calloc(1, sizeof(drmModeAtomicReq));
   if ( req )
   {
      req->magic= EM_DRM_ATOMIC_MAGIC;
   }
   return req;
}

void drmModeAtomicFree( drmModeAtomicReqPtr ptr )
{
   TRACE1("drmModeAtomicFree: ptr %p", ptr);
   if ( ptr )
   {
      if ( ptr->magic == EM_DRM_ATOMIC_MAGIC )
      {
         ptr->magic= 0;
      }
      free( ptr );
   }
}

int drmModeAtomicAddProperty( drmModeAtomicReqPtr req,
                              uint32_t objectId, uint32_t propertyId,
                              uint64_t value )
{
   int rc= -1;
   TRACE1("drmModeAtomicAddProperty: req %p objectId %u propertyId %u value %llu", req, objectId, propertyId, value);
   if ( req )
   {
      EMDevice *dev= gDrmMasterDev;
      if ( dev )
      {
         if ( objectId == dev->dev.drm.planes[1].plane_id )
         {
            if ( propertyId == dev->dev.drm.properties[EM_DRM_PROP_CRTC_X].prop_id )
            {
               dev->dev.drm.videoPlane[0].positionIsPending= true;
               dev->dev.drm.videoPlane[0].vxPending= (int)value;
            }
            else if ( propertyId == dev->dev.drm.properties[EM_DRM_PROP_CRTC_Y].prop_id )
            {
               dev->dev.drm.videoPlane[0].positionIsPending= true;
               dev->dev.drm.videoPlane[0].vyPending= (int)value;
            }
            else if ( propertyId == dev->dev.drm.properties[EM_DRM_PROP_CRTC_W].prop_id )
            {
               dev->dev.drm.videoPlane[0].positionIsPending= true;
               dev->dev.drm.videoPlane[0].vwPending= (int)value;
            }
            else if ( propertyId == dev->dev.drm.properties[EM_DRM_PROP_CRTC_H].prop_id )
            {
               dev->dev.drm.videoPlane[0].positionIsPending= true;
               dev->dev.drm.videoPlane[0].vhPending= (int)value;
            }
         }
      }

      rc= 0;
   }
   return rc;
}

int drmModeCreatePropertyBlob( int fd, const void *data, size_t size, uint32_t *id )
{
   int rc= -1;
   EMDevice *dev= 0;

   TRACE1("drmModeCreatePropertyBlob");
   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      *id= ++dev->dev.drm.nextId;
      rc= 0;
   }   

   return rc;
}

int drmModeAtomicCommit( int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data )
{
   int rc= -1;
   EMDevice *dev= 0;

   TRACE1("drmModeAtomicCommit");
   dev= EMDrmGetDevice(fd);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      if ( req && (req->magic == EM_DRM_ATOMIC_MAGIC) )
      {
         if ( dev->dev.drm.videoPlane[0].positionIsPending )
         {
            dev->dev.drm.videoPlane[0].positionIsPending= false;
            dev->dev.drm.videoPlane[0].vx= dev->dev.drm.videoPlane[0].vxPending;
            dev->dev.drm.videoPlane[0].vy= dev->dev.drm.videoPlane[0].vyPending;
            dev->dev.drm.videoPlane[0].vw= dev->dev.drm.videoPlane[0].vwPending;
            dev->dev.drm.videoPlane[0].vh= dev->dev.drm.videoPlane[0].vhPending;
         }
         rc= 0;
      }
   }

   return rc;
}

} //extern "C"


// Section gbm -------------------------------------------------------

extern "C"
{

#define GBM_DEV_REF( gbm ) ++gbm->refCount
#define GBM_DEV_UNREF( gbm ) gbm_device_destroy(gbm)

struct gbm_device *gbm_create_device( int fd )
{
   struct gbm_device *gbm= 0;
   EMDevice *dev= 0;
   EMCTX *ctx= 0;

   TRACE1("gbm_create_device: fd %d", fd);

   dev= EMDrmGetDevice(fd);
   TRACE1("gbm_create_device: fd %d dev %p", fd, dev);
   if ( dev && (dev->type == EM_DEVICE_TYPE_DRM) )
   {
      TRACE1("gbm_create_device: fd %d dev %p type %d", fd, dev, dev->type);
      gbm= (struct gbm_device*)calloc( 1, sizeof(struct gbm_device) );
      if ( gbm )
      {
         gbm->dev= dev;
         gbm->refCount= 1;
         ctx= emGetContext();
         if ( ctx )
         {
            TRACE1("gbm_create_device: fd %d settig em ctx gbm_device to %p", fd, gbm);
            ctx->gbm_device= gbm;
         }
      }
   }

   TRACE1("gbm_create_device: fd %d gbm %p", fd, gbm);

   return gbm;
}

void gbm_device_destroy( struct gbm_device *gbm )
{
   EMCTX *ctx= 0;

   TRACE1("gbm_destroy_device: gbm %p", gbm);

   if ( gbm )
   {
      if ( gbm->refCount > 0 )
      {
         --gbm->refCount;
      }
      if ( gbm->refCount == 0 )
      {
         TRACE1("gbm_destroy_device: gbm %p reCount 0: destroying", gbm);
         ctx= emGetContext();
         if ( ctx && ctx->gbm_device == gbm )
         {
            ctx->gbm_device= 0;
         }
         free( gbm );
      }
   }
}

struct gbm_surface *gbm_surface_create( struct gbm_device *gbm,
                                        uint32_t width, uint32_t height,
                                        uint32_t format, uint32_t flags )
{
   struct gbm_surface *surface= 0;

   TRACE1("gbm_surface_create: gbm %p", gbm);

   if ( gbm )
   {
      if ( gbm->dev->type == EM_DEVICE_TYPE_DRM )
      {
         surface= (struct gbm_surface*)calloc( 1, sizeof(struct gbm_surface));
         if ( surface )
         {
            surface->gbm= gbm;
            GBM_DEV_REF(surface->gbm);
            surface->nw.magic= EM_WINDOW_MAGIC;
            surface->nw.ctx= emGetContext();
            surface->nw.x= 0;
            surface->nw.y= 0;
            surface->nw.width= width;
            surface->nw.height= height;
            surface->nw.format= format;
            surface->nw.stride= width*4;

            for( int i= 0; i < 3; ++i )
            {
               surface->buffers[i].surface= surface;
               surface->buffers[i].locked= false;
               surface->buffers[i].handle.u32= ++gbm->dev->ctx->nextGbmBuffHandle;
               surface->buffers[i].fbId= ++gbm->dev->dev.drm.nextId;
               gbm->dev->ctx->gbmBuffs.push_back( &surface->buffers[i] );
            }
         }
      }
   }

   TRACE1("gbm_surface_create: gbm %p surface %p", gbm, surface);

   return surface;
}

struct gbm_surface *gbm_surface_create_with_modifiers(struct gbm_device *gbm,
                                  uint32_t width, uint32_t height,
                                  uint32_t format,
                                  const uint64_t *modifiers,
                                  const unsigned int count)
{
   struct gbm_surface *surface= 0;

   TRACE1("gbm_surface_create_with_modifiers: gbm %p modifiers %p count %d", gbm, modifiers, count);

   if ( gbm )
   {
      surface= gbm_surface_create( gbm,
                                   width,
                                   height,
                                   format,
                                   GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING );
   }

   TRACE1("gbm_surface_create_with_modifiers: gbm %p surface %p", gbm, surface);

   return surface;
}

void gbm_surface_destroy( struct gbm_surface *surface )
{
   TRACE1("gbm_surface_destroy: gbm_surface %p", surface);
   if ( surface )
   {
      TRACE1("gbm_surface_destroy: gbm_surface %p gbm %p dev %p", surface, surface->gbm), surface->gbm->dev;
      for( int i= 0; i < 3; ++i )
      {
         for( std::vector<struct gbm_bo*>::iterator it= surface->gbm->dev->ctx->gbmBuffs.begin();
              it != surface->gbm->dev->ctx->gbmBuffs.end();
              ++it )
         {
            if ( (*it)->handle.u32 == surface->buffers[i].handle.u32 )
            {
               surface->gbm->dev->ctx->gbmBuffs.erase( it );
               break;
            }
         }
      }
      GBM_DEV_UNREF(surface->gbm);
      free( surface );
   }
}

struct gbm_bo* gbm_surface_lock_front_buffer( struct gbm_surface *surface )
{
   struct gbm_bo *bo= 0;

   TRACE1("gbm_surface_lock_front_buffer: surface %p", surface);

   if ( surface->nw.magic == EM_WINDOW_MAGIC )
   {
      int front= surface->front;
      if ( ++surface->front >= 3 )
      {
         surface->front= 0;
      }
      bo= &surface->buffers[front];
      if ( !bo->locked )
      {
         bo->locked= true;
      }
      else
      {
         ERROR("gbm_surface_lock_front_buffer: already locked: surface %p front %d", surface, front);
         bo= 0;
      }
   }
   else
   {
      ERROR("gbm_surface_lock_front_buffer: bad gbm surface: %p", surface);
   }

   return bo;
}

void gbm_surface_release_buffer( struct gbm_surface *surface, struct gbm_bo *bo )
{
   TRACE1("gbm_surface_release_buffer: surface %p bo %p", surface, bo);

   if ( surface->nw.magic == EM_WINDOW_MAGIC )
   {
      if ( bo->surface == surface )
      {
         if ( bo->locked )
         {
            bo->locked= false;
         }
         else
         {
            ERROR("gbm_surface_release_buffer: buffer not locked: gbm bo: %p", bo);
         }
      }
      else
      {
         ERROR("gbm_surface_release_buffer: bad gbm bo: %p", bo);
      }
   }
   else
   {
      ERROR("gbm_surface_release_buffer: bad gbm surface: %p", surface);
   }
}

union gbm_bo_handle gbm_bo_get_handle( struct gbm_bo *bo )
{
   union gbm_bo_handle handle;

   handle.u32= 0;

   if ( bo->surface->nw.magic == EM_WINDOW_MAGIC )
   {
      handle= bo->handle;
   }
   else
   {
      ERROR("gbm_bo_get_handle: bad gbm_bo %p", bo);
   }

   return handle;
}

uint32_t gbm_bo_get_stride( struct gbm_bo *bo )
{
   uint32_t stride= 0;

   if ( bo->surface->nw.magic == EM_WINDOW_MAGIC )
   {
      stride= bo->surface->nw.stride;
   }
   else
   {
      ERROR("gbm_bo_get_stride: bad gbm_bo %p", bo);
   }

   return stride;
}

uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane)
{
   uint32_t offset= 0;

   if ( bo->surface->nw.magic == EM_WINDOW_MAGIC )
   {
      if ( plane != 0 )
      {
         ERROR("gbm_bo_get_offset: bad plane %p", plane);
      }
      else
      {
         offset= 0;
      }
   }
   else
   {
      ERROR("gbm_bo_get_offset: bad gbm_bo %p", bo);
   }

   return offset;
}

uint64_t gbm_bo_get_modifier(struct gbm_bo *bo)
{
   uint64_t modifier= 0;
   if ( bo->surface->nw.magic == EM_WINDOW_MAGIC )
   {
      modifier= 0;
   }
   else
   {
      ERROR("gbm_bo_get_modifier: bad gbm_bo %p", bo);
   }

   return modifier;
}

} //extern "C"


// Section: EGL ------------------------------------------------------

static EGLint gEGLError= EGL_SUCCESS;
static EGLint gNumConfigs= 4;
static EMEGLConfig gConfigs[4]=
{
   { EM_EGL_CONFIG_MAGIC, 8, 8, 8, 0, 0 },
   { EM_EGL_CONFIG_MAGIC, 8, 8, 8, 8, 0 },
   { EM_EGL_CONFIG_MAGIC, 8, 8, 8, 0, 24 },
   { EM_EGL_CONFIG_MAGIC, 8, 8, 8, 8, 24 }   
};

EGLAPI EGLint EGLAPIENTRY eglGetError( void )
{
   EGLint err= gEGLError;
   gEGLError= EGL_SUCCESS;
   return err;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType displayId)
{
   EGLDisplay eglDisplay= EGL_NO_DISPLAY;
   EMCTX *ctx= 0;
   EMEGLDisplay *dsp= 0;

   TRACE1("eglGetDisplay: displayId %p", displayId);

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("eglGetDisplay: emGetContext failed");
      gEGLError= EGL_BAD_ACCESS;
      goto exit;
   }

   if ( 
        ( 
          (displayId == EGL_DEFAULT_DISPLAY) ||
          (displayId == ctx->gbm_device) 
        ) && 
        (ctx->eglDisplayDefault != EGL_NO_DISPLAY)
      )
   {
      TRACE1("eglGetDisplay: displayId %p using default display", displayId);
      eglDisplay= ctx->eglDisplayDefault;
      goto exit;
   }

   dsp= (EMEGLDisplay*)calloc( 1, sizeof(EMEGLDisplay) );
   if ( !dsp )
   {
      ERROR("eglGetDisplay: no memory");
      gEGLError= EGL_BAD_ALLOC;
      goto exit;
   }

   dsp->magic= EM_EGL_DISPLAY_MAGIC;
   dsp->ctx= ctx;
   dsp->displayId= displayId;
   dsp->context= 0;
   dsp->swapInterval= 1;

   eglDisplay= (EGLDisplay)dsp;

   if ( 
        ( 
          (displayId == EGL_DEFAULT_DISPLAY) ||
          (displayId == ctx->gbm_device) 
        ) && 
        (ctx->eglDisplayDefault == EGL_NO_DISPLAY)
      )
   {
      TRACE1("eglGetDisplay: displayId %p setting default display %p", displayId, eglDisplay);
      ctx->eglDisplayDefault= eglDisplay;
   }

exit:

   TRACE1("eglGetDisplay: displayId %p return eglDisplay %p", displayId, eglDisplay);

   return eglDisplay;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
   EGLDisplay eglDisplay= EGL_NO_DISPLAY;
   EMCTX *ctx= 0;

   TRACE1("eglGetCurrentDisplay");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("eglGetDisplay: emGetContext failed");
      gEGLError= EGL_BAD_ACCESS;
      goto exit;
   }

   eglDisplay= ctx->eglDisplayCurrent;

exit:

   return eglDisplay;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize( EGLDisplay display,
                                  EGLint *major,
                                  EGLint *minor )
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;

   TRACE1("eglInitialize");

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   dsp->initialized= true;
   if ( major ) *major= 1;
   if ( minor ) *minor= 4;

   result= EGL_TRUE;

exit:
   return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate( EGLDisplay display )
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;
   EMCTX *ctx= 0;

   TRACE1("eglTerminate");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("eglGetDisplay: emGetContext failed");
      gEGLError= EGL_BAD_ACCESS;
      goto exit;
   }

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( display == ctx->eglDisplayDefault )
   {
      ctx->eglDisplayDefault= EGL_NO_DISPLAY;
   }

   if ( display == ctx->eglDisplayCurrent )
   {
      ctx->eglDisplayCurrent= EGL_NO_DISPLAY;
   }

   dsp->initialized= false;

   free( dsp );

   result= EGL_TRUE;

exit:
   return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread( void )
{
   TRACE1("eglReleaseThread");

   return EGL_TRUE;
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *procname)
{
   void *proc= 0;
   int len;
   
   len= strlen( procname );
   
   if ( (len == 23) && !strncmp( procname, "eglBindWaylandDisplayWL", len ) )
   {
      proc= (void*)eglBindWaylandDisplayWL;
   }
   else
   if ( (len == 25) && !strncmp( procname, "eglUnbindWaylandDisplayWL", len ) )
   {
      proc= (void*)eglUnbindWaylandDisplayWL;
   }
   else
   if ( (len == 23) && !strncmp( procname, "eglQueryWaylandBufferWL", len ) )
   {
      proc= (void*)eglQueryWaylandBufferWL;
   }
   else
   if ( (len == 17) && !strncmp( procname, "eglCreateImageKHR", len ) )
   {
      proc= (void*)eglCreateImageKHR;
   }
   else
   if ( (len == 18) && !strncmp( procname, "eglDestroyImageKHR", len ) )
   {
      proc= (void*)eglDestroyImageKHR;
   }
   else
   if ( (len == 28) && !strncmp( procname, "glEGLImageTargetTexture2DOES", len ) )
   {
      proc= (void*)glEGLImageTargetTexture2DOES;
   }
   else
   if ( (len == 7) && !strncmp( procname, "glFlush", len ) )
   {
      proc= (void*)glFlush;
   }
   else
   if ( (len == 8) && !strncmp( procname, "glFinish", len ) )
   {
      proc= (void*)glFinish;
   }
   else
   {
      proc= (void*)0;
   }
   
exit:

   return (__eglMustCastToProperFunctionPointerType)proc;
} 

EGLAPI const char * EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
   const char *s= 0;

   TRACE1("eglQueryString for %X", name );

   switch( name )
   {
      case EGL_EXTENSIONS:
         s= "EGL_WL_bind_wayland_display EGL_EXT_image_dma_buf_import";
         break;
   }

exit:

   TRACE1("eglQueryString for %X: (%s)", name, s );

   return s;   
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs( EGLDisplay display,
                                 EGLConfig *configs,
                                 EGLint config_size,
                                 EGLint *num_config)
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;

   TRACE1("eglConfig" );

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( !num_config )
   {
      gEGLError= EGL_BAD_PARAMETER;
      goto exit;
   }

   *num_config= gNumConfigs;

   if ( configs )
   {
      EMEGLConfig **dst= (EMEGLConfig**)configs;
      for( int i= 0; (i < config_size) && (i < gNumConfigs) ; ++i )
      {
         dst[i]= &gConfigs[i];
      }
   }

   result= EGL_TRUE;

exit:
   return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig( EGLDisplay display,
                                 EGLint const *attrib_list,
                                 EGLConfig *configs,
                                 EGLint config_size,
                                 EGLint *num_config )
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;

   TRACE1("eglChooseConfig" );

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( !num_config )
   {
      gEGLError= EGL_BAD_PARAMETER;
      goto exit;
   }

   // Ignore attrib_list.  Just return our configs.
   *num_config= gNumConfigs;

   if ( configs )
   {
      EMEGLConfig **dst= (EMEGLConfig**)configs;
      for( int i= 0; (i < config_size) && (i < gNumConfigs) ; ++i )
      {
         dst[i]= &gConfigs[i];
      }
   }

   result= EGL_TRUE;

exit:
   return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib( EGLDisplay display,
                                 EGLConfig config,
                                 EGLint attribute,
                                 EGLint *value )
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;
   EMEGLConfig *cfg= (EMEGLConfig*)config;

   TRACE1("eglChooseConfig" );

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( cfg->magic != EM_EGL_CONFIG_MAGIC )
   {
      gEGLError= EGL_BAD_CONFIG;
      goto exit;
   }   

   result= EGL_TRUE;

   switch( attribute )
   {
      case EGL_RED_SIZE:
         *value= cfg->redSize;
         break;
      case EGL_GREEN_SIZE:
         *value= cfg->greenSize;
         break;
      case EGL_BLUE_SIZE:
         *value= cfg->blueSize;
         break;
      case EGL_ALPHA_SIZE:
         *value= cfg->alphaSize;
         break;
      case EGL_DEPTH_SIZE:
         *value= cfg->depthSize;
         break;
      default:
         gEGLError= EGL_BAD_ATTRIBUTE;
         result= EGL_FALSE;
         break;
   }

exit:
   return result;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface( EGLDisplay display,
                                 EGLConfig config,
                                 NativeWindowType native_window,
                                 EGLint const *attrib_list )
{
   EGLSurface surface= EGL_NO_SURFACE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;
   EMEGLConfig *cfg= (EMEGLConfig*)config;
   EMNativeWindow *nw= &((struct gbm_surface*)native_window)->nw;
   EMEGLSurface *surf= 0;
   struct wl_egl_window *egl_window= 0;

   TRACE1("eglCreateWindowSurface: native_window %p, nw %p", native_window, nw );

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( cfg->magic != EM_EGL_CONFIG_MAGIC )
   {
      gEGLError= EGL_BAD_CONFIG;
      goto exit;
   }

   pthread_mutex_lock( &gMutex );
   for( std::vector<struct wl_egl_window*>::iterator it= gNativeWindows.begin();
        it != gNativeWindows.end();
        ++it )
   {
      if ( (*it) == (struct wl_egl_window*)native_window )
      {
         egl_window= (struct wl_egl_window*)native_window;
         break;
      }
   }
   pthread_mutex_unlock( &gMutex );

   surf= (EMEGLSurface*)calloc( 1, sizeof(EMEGLSurface) );
   if ( !surf )
   {
      ERROR("eglCreateWindowSurface: no memory");
      gEGLError= EGL_BAD_ALLOC;
      goto exit;
   }

   surf->magic= EM_EGL_SURFACE_MAGIC;
   surf->egl_window= egl_window;
   gEGLError= EGL_SUCCESS;

   surface= (EGLSurface)surf;

exit:
   TRACE1("eglCreateWindowSurface: native_window %p, nw %p surface %p eglError %X", native_window, nw, surface, gEGLError );
   return surface;
}

EGLAPI EGLContext EGLAPIENTRY eglCreateContext( EGLDisplay display,
                                 EGLConfig config,
                                 EGLContext share_context,
                                 EGLint const *attrib_list )
{
   EGLContext context= EGL_NO_CONTEXT;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;
   EMEGLConfig *cfg= (EMEGLConfig*)config;
   EMEGLContext *ct= 0;

   TRACE1("eglCreateContext" );

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( cfg->magic != EM_EGL_CONFIG_MAGIC )
   {
      gEGLError= EGL_BAD_CONFIG;
      goto exit;
   }

   ct= (EMEGLContext*)calloc( 1, sizeof(EMEGLContext) );
   if ( !ct )
   {
      ERROR("eglCreateContext: no memory");
      gEGLError= EGL_BAD_ALLOC;
      goto exit;
   }

   ct->magic= EM_EGL_CONTEXT_MAGIC;
   ct->draw= 0;
   ct->read= 0;

   context= (EGLContext)ct;

exit:
   return context;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext( EGLDisplay display,
                                                 EGLContext context )
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;
   EMEGLContext *ct= (EMEGLContext*)context;

   TRACE1("eglDestroyContext" );

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( context == EGL_NO_CONTEXT )
   {
      gEGLError= EGL_BAD_CONTEXT;
      goto exit;
   }

   if ( ct->magic != EM_EGL_CONTEXT_MAGIC )
   {
      gEGLError= EGL_BAD_CONTEXT;
      goto exit;
   }

   free( ct );
   result= EGL_TRUE;

exit:
   return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent( EGLDisplay display,
                                 EGLSurface draw,
                                 EGLSurface read,
                                 EGLContext context )
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;
   EMEGLSurface *surfDraw= (EMEGLSurface*)draw;
   EMEGLSurface *surfRead= (EMEGLSurface*)read;
   EMEGLContext *ct= (EMEGLContext*)context;
   EMCTX *ctx= 0;

   TRACE1("eglMakeCurrent" );

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( draw != EGL_NO_SURFACE )
   {
      if ( surfDraw->magic != EM_EGL_SURFACE_MAGIC )
      {
         gEGLError= EGL_BAD_SURFACE;
         goto exit;
      }
   }
   else if ( context != EGL_NO_CONTEXT )
   {
      gEGLError= EGL_BAD_SURFACE;
      goto exit;
   }

   if ( read != EGL_NO_SURFACE )
   {
      if ( surfRead->magic != EM_EGL_SURFACE_MAGIC )
      {
         gEGLError= EGL_BAD_SURFACE;
         goto exit;
      }
   }
   else if ( context != EGL_NO_CONTEXT )
   {
      gEGLError= EGL_BAD_SURFACE;
      goto exit;
   }

   if ( context != EGL_NO_CONTEXT )
   {
      if ( ct->magic != EM_EGL_CONTEXT_MAGIC )
      {
         gEGLError= EGL_BAD_CONTEXT;
         goto exit;
      }
   }

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("eglMakeContext: emGetContext failed");
      goto exit;
   }

   if ( context != EGL_NO_CONTEXT )
   {
      ct->draw= surfDraw;
      ct->read= surfRead;
   }

   ctx->eglContext= context;
   ctx->eglDisplayCurrent= display;
   TRACE1("eglMakeCurrent: draw %p read %p context %p", surfDraw, surfRead, context);

   result= EGL_TRUE;

exit:
   return result;
}

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext( void )
{
   EGLContext context= EGL_NO_CONTEXT;
   EMCTX *ctx= 0;

   TRACE1("eglGetCurrentContext" );

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("eglGetCurrentContext: emGetContext failed");
      goto exit;
   }

   context= ctx->eglContext;
   TRACE1("eglGetCurrentContext: context %p", context );

exit:
   return context;
}

EGLAPI EGLBoolean eglSwapBuffers( EGLDisplay display, EGLSurface surface )
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;
   EMEGLSurface *surf= (EMEGLSurface*)surface;
   struct wl_egl_window *egl_window= 0;

   TRACE1("eglSwapBuffers: display %p surface %p", display, surface);

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( surface == EGL_NO_SURFACE )
   {
      gEGLError= EGL_BAD_SURFACE;
      goto exit;
   }

   if ( surf->magic != EM_EGL_SURFACE_MAGIC )
   {
      gEGLError= EGL_BAD_SURFACE;
      goto exit;
   }

   egl_window= surf->egl_window;
   if ( egl_window )
   {
      TRACE1("eglSwapBuffers: display %p surface %p egl_window %p", display, surface, egl_window);
      wlSwapBuffers( egl_window );
   }
   else
   {
      // Emulated 'normal' render to screen.  Nothing to do.
   }

   result= EGL_TRUE;

exit:
   TRACE1("eglSwapBuffers: display %p surface %p rc %d", display, surface, result);
   return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval( EGLDisplay display,
                                 EGLint interval )
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;

   TRACE1("eglSwapInterval" );

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   dsp->swapInterval= interval;

   result= EGL_TRUE;

exit:
   return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface( EGLDisplay display, EGLSurface surface )
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;
   EMEGLSurface *surf= (EMEGLSurface*)surface;

   TRACE1("eglDestroySurface" );

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( surface == EGL_NO_SURFACE )
   {
      gEGLError= EGL_BAD_SURFACE;
      goto exit;
   }

   if ( surf->magic != EM_EGL_SURFACE_MAGIC )
   {
      gEGLError= EGL_BAD_SURFACE;
      goto exit;
   }

   free( surf );

   result= EGL_TRUE;

exit:
   return result;
}

// Section: wayland ------------------------------------------------------

#ifndef WL_EXPORT
#define WL_EXPORT
#endif

typedef void (*WLRESOURCEPOSTEVENTARRAY)( struct wl_resource *resource, uint32_t opcode, union wl_argument *args );

WLRESOURCEPOSTEVENTARRAY gRealWlResourcePostEventArray= 0;

WL_EXPORT void wl_resource_post_event_array(struct wl_resource *resource, uint32_t opcode, union wl_argument *args)
{
   EMCTX *emctx;
   int tid;

   emctx= emGetContext();
   if ( emctx )
   {
      tid= syscall(__NR_gettid);
      if ( (emctx->waylandSendTid != 0) && (emctx->waylandSendTid != tid) )
      {
         EMCTX *emctx;
         emctx= emGetContext();
         if ( emctx )
         {
            emctx->waylandThreadingIssue= true;
         }
         else
         {
            ERROR("wl_resource_post_event_arrary: emGetContext failed");
         }
      }
      emctx->waylandSendTid= tid;
   }
   else
   {
      ERROR("wl_resource_post_event_arrary: emGetContext failed");
   }

   if ( !gRealWlResourcePostEventArray )
   {
      gRealWlResourcePostEventArray= (WLRESOURCEPOSTEVENTARRAY)dlsym( RTLD_NEXT, "wl_resource_post_event_array" );
      if ( !gRealWlResourcePostEventArray )
      {
         ERROR("Unable to locate underlying wl_resource_post_event_array");
      }
   }
   if ( gRealWlResourcePostEventArray )
   {
      gRealWlResourcePostEventArray( resource, opcode, args );
   }
}

// Section: wayland-egl ------------------------------------------------------

#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#define WAYEGL_UNUSED(x) ((void)x)

void wl_egl_window_destroy(struct wl_egl_window *egl_window);

static void wlDrmDevice(void *data, struct wl_drm *drm, const char *name)
{
   WAYEGL_UNUSED(data);
   WAYEGL_UNUSED(drm);
   printf("wayland-egl: registry: wlDrmDevice: %s\n", name);
}

static void wlDrmFormat(void *data, struct wl_drm *drm, uint32_t format)
{
   WAYEGL_UNUSED(data);
   WAYEGL_UNUSED(drm);
   printf("wayland-egl: registry: wlDrmFormat: %X\n", format);
}

static void wlDrmAuthenticated(void *data, struct wl_drm *drm)
{
   WAYEGL_UNUSED(data);
   WAYEGL_UNUSED(drm);
   printf("wayland-egl: registry: wlDrmAuthenticated\n");
}

static void wlDrmCapabilities(void *data, struct wl_drm *drm, uint32_t value)
{
   WAYEGL_UNUSED(data);
   WAYEGL_UNUSED(drm);
   printf("wayland-egl: registry: wlDrmCapabilities: %X\n", value);
}

struct wl_drm_listener drmListener = {
   wlDrmDevice,
	wlDrmFormat,
	wlDrmAuthenticated,
	wlDrmCapabilities
};

static void winRegistryHandleGlobal(void *data,
                                    struct wl_registry *registry, uint32_t id,
		                              const char *interface, uint32_t version)
{
   struct wl_egl_window *egl_window= (struct wl_egl_window*)data;
   printf("wayland-egl: registry: id %d interface (%s) version %d\n", id, interface, version );
   
   int len= strlen(interface);
   if ( (len==6) && !strncmp(interface, "wl_drm", len) ) {
      egl_window->drm= (struct wl_drm*)wl_registry_bind(registry, id, &wl_drm_interface, version);
      printf("wayland-egl: registry: drm %p\n", (void*)egl_window->drm);
      wl_proxy_set_queue((struct wl_proxy*)egl_window->drm, egl_window->queue);
		wl_drm_add_listener(egl_window->drm, &drmListener, egl_window);
      wl_display_roundtrip_queue(egl_window->wldisp, egl_window->queue);
		printf("wayland-egl: registry: done add drm listener\n");
   }
}

static void winRegistryHandleGlobalRemove(void *data,
                                          struct wl_registry *registry,
			                                 uint32_t name)
{
   WAYEGL_UNUSED(data);
   WAYEGL_UNUSED(registry);
   WAYEGL_UNUSED(name);
}

static const struct wl_registry_listener winRegistryListener =
{
	winRegistryHandleGlobal,
	winRegistryHandleGlobalRemove
};

static void drmIBufferDestroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

const static struct wl_buffer_interface bufferInterface = {
   drmIBufferDestroy
};


static void drmDestroyBuffer(struct wl_resource *resource)
{
   struct wl_drm_buffer *buffer = (struct wl_drm_buffer*)resource->data;

   free(buffer);
}

static void drmIAuthenticate( struct wl_client *client, struct wl_resource *resource, uint32_t id )
{
   TRACE1("drmIAuthenticate");
   //TBD
}

static void drmICreateBuffer( struct wl_client *client, struct wl_resource *resource, uint32_t id,
                              uint32_t name, int32_t width, int32_t height, 
                              uint32_t stride, uint32_t format )
{
   TRACE1("drmICreateBuffer");
   //TBD
}

static void drmICreatePlanarBuffer( struct wl_client *client, struct wl_resource *resource, uint32_t id,
                                    uint32_t name, int32_t width, int32_t height, uint32_t format, 
                                    int32_t offset0, int32_t stride0,
                                    int32_t offset1, int32_t stride1,
                                    int32_t offset2, int32_t stride2 )
{
   TRACE1("drmICreatePlanarBuffer");
   //TBD
}

static void drmICreatePrimeBuffer( struct wl_client *client, struct wl_resource *resource, uint32_t id,
                                   int32_t name, int32_t width, int32_t height, uint32_t format, 
                                   int32_t offset0, int32_t stride0,
                                   int32_t offset1, int32_t stride1,
                                   int32_t offset2, int32_t stride2 )
{
   struct wl_drm_buffer *buff;

   TRACE1("drmICreatePrimeBuffer: client %p name %d", client, name);
   buff= (wl_drm_buffer*)calloc(1, sizeof *buff);
   if (!buff) 
   {
      wl_resource_post_no_memory(resource);
      return;
   }

   buff->resource= wl_resource_create(client, &wl_buffer_interface, 1, id);
   if (!buff->resource)
   {
      wl_resource_post_no_memory(resource);
      free(buff);
      return;
   }
   
   buff->width= width;
   buff->height= height;
   buff->format= format;
   buff->bufferId= EMDeviceGetFdFromFdOS(name);
   buff->offset[0]= offset0;
   buff->stride[0]= stride0;
   buff->offset[1]= offset1;
   buff->stride[1]= stride1;
   buff->offset[2]= offset2;
   buff->stride[2]= stride2;

   wl_resource_set_implementation(buff->resource,
                                 (void (**)(void)) &bufferInterface,
                                 buff, drmDestroyBuffer);
}

static struct wl_drm_interface drm_interface = 
{
   drmIAuthenticate,
   drmICreateBuffer,
   drmICreatePlanarBuffer,
   drmICreatePrimeBuffer
};

static void bind_drm(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource;
   EMCTX *ctx= (EMCTX*)data;
   struct wl_display *display;
   struct wl_drm *drm= 0;

	printf("wayland-egl: bind_drm: enter: client %p data %p version %d id %d\n", (void*)client, data, version, id);

   resource= wl_resource_create(client, &wl_drm_interface, MIN(version, 2), id);
   if (!resource)
   {
      wl_client_post_no_memory(client);
      return;
   }

   display= wl_client_get_display( client );

   std::map<struct wl_display*,EMWLBinding>::iterator it= ctx->wlBindings.find( display );
   if ( it != ctx->wlBindings.end() )
   {
      drm= it->second.drm;
   }

   if ( !drm )
   {
      printf("wayland-egl: bind_drm: no valid EGL for compositor\n" );
      wl_client_post_no_memory(client);
      return;
   }

   wl_resource_set_implementation(resource, &drm_interface, data, NULL);

   wl_resource_post_event(resource, WL_DRM_FORMAT, WL_DRM_FORMAT_ARGB8888);      
	
	printf("wayland-egl: bind_drm: exit: client %p id %d\n", (void*)client, id);
}

EGLBoolean eglBindWaylandDisplayWL( EGLDisplay dpy,
                                    struct wl_display *display )
{
   EGLBoolean result= EGL_FALSE;
   EMCTX *ctx= 0;
   EMWLBinding binding;

   TRACE1("eglBindWaylandDisplayWL");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glCreateProgram: emGetContext failed");
      goto exit;
   }

   {
      std::map<struct wl_display*,EMWLBinding>::iterator it= ctx->wlBindings.find( display );
      if ( it != ctx->wlBindings.end() )
      {
         binding= it->second;
         if ( binding.drm )
         {
            result= EGL_TRUE;
         }
      }
      else
      {
         memset( &binding, 0, sizeof(binding) );
         binding.wlBoundDpy= dpy;
         binding.display= display;
         binding.drm= (wl_drm*)wl_global_create( display,
                                           &wl_drm_interface,
                                           2,
                                           ctx,
                                           bind_drm );
         if ( binding.drm )
         {
            ctx->wlBindings.insert( std::pair<struct wl_display*,EMWLBinding>( display, binding ) );
            result= EGL_TRUE;
         }
      }
   }

exit:   
   return result;
}

EGLBoolean eglUnbindWaylandDisplayWL( EGLDisplay dpy,
                                      struct wl_display *display )
{
   EGLBoolean result= EGL_FALSE;
   EMCTX *ctx= 0;
   EMWLBinding binding;

   TRACE1("eglUnbindWaylandDisplayWL");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glCreateProgram: emGetContext failed");
      goto exit;
   }

   {
      std::map<struct wl_display*,EMWLBinding>::iterator it= ctx->wlBindings.find( display );
      if ( it != ctx->wlBindings.end() )
      {
         binding= it->second;
         if ( binding.drm )
         {
            wl_global_destroy( (wl_global*)binding.drm );
            ctx->wlBindings.erase( it );
            result= EGL_TRUE;
         }
      }
   }

exit:   
   return result;
}

EGLBoolean eglQueryWaylandBufferWL( EGLDisplay dpy,
                                    struct wl_resource *resource,
                                    EGLint attribute, EGLint *value )
{
   EGLBoolean result= EGL_FALSE;
   struct wl_drm_buffer *drmBuffer;
   int drmFormat;

   TRACE1("eglQueryWaylandBufferWL");
   
   if( wl_resource_instance_of( resource, &wl_buffer_interface, &bufferInterface ) ) 
   {
      drmBuffer= (wl_drm_buffer *)wl_resource_get_user_data( (wl_resource*)resource );
      if ( drmBuffer )
      {
         result= EGL_TRUE;
         switch( attribute )
         {
            case EGL_WIDTH:
               *value= drmBuffer->width;
               break;
            case EGL_HEIGHT:
               *value= drmBuffer->height;
               break;
            case EGL_TEXTURE_FORMAT:
               drmFormat= drmBuffer->format;
               switch( drmFormat )
               {
                  case WL_DRM_FORMAT_ARGB8888:
                     *value= EGL_TEXTURE_RGBA;
                     break;
                  default:
                     *value= EGL_NONE;
                     result= EGL_FALSE;
                     break;
               }
               break;
            default:
               result= EGL_FALSE;
               break;
         }
      }
   }
   
   return result;
}

EGLAPI EGLImageKHR EGLAPIENTRY eglCreateImageKHR (EGLDisplay display, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)
{
   EGLImageKHR image= EGL_NO_IMAGE_KHR;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;

   TRACE1("eglCreateImageKHR");

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   switch( target )
   {
      case EGL_WAYLAND_BUFFER_WL:
      case EGL_NATIVE_PIXMAP_KHR:
      case EGL_LINUX_DMA_BUF_EXT:
         {
            EMEGLImage *img= 0;
            img= (EMEGLImage*)calloc( 1, sizeof(EMEGLImage));
            if ( img )
            {
               img->magic= EM_EGL_IMAGE_MAGIC;
               img->clientBuffer= buffer;
               img->target= target;
               if ( attrib_list && (target == EGL_LINUX_DMA_BUF_EXT) )
               {
                  int i= 0;
                  while ( attrib_list[i] != 0 )
                  {
                     if ( attrib_list[i] == EGL_DMA_BUF_PLANE0_FD_EXT )
                     {
                        img->fd= EMDeviceGetFdFromFdOS( attrib_list[i+1] );
                        TRACE1("eglCreateImageKHR: fd %d (%d)", img->fd, attrib_list[i+1]);
                        break;
                     }
                     i += 2;
                  }
               }
            }
            image= (EGLImageKHR)img;
            gEGLError= EGL_SUCCESS;
         }
         break;
      default:
         WARNING("eglCreateImageKHR: unsupported target %X", target);
         break;
   }

exit:
   TRACE1("eglCreateImageKHR: image %p eglError %x", image, gEGLError);
   return image;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImageKHR (EGLDisplay display, EGLImageKHR image)
{
   EGLBoolean result= EGL_FALSE;
   EMEGLDisplay *dsp= (EMEGLDisplay*)display;
   EMEGLImage *img= (EMEGLImage*)image;

   TRACE1("eglDestroyImageKHR");

   if ( display == EGL_NO_DISPLAY )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }

   if ( dsp->magic != EM_EGL_DISPLAY_MAGIC )
   {
      gEGLError= EGL_BAD_DISPLAY;
      goto exit;
   }   

   if ( !dsp->initialized )
   {
      gEGLError= EGL_NOT_INITIALIZED;
      goto exit;
   }

   if ( img->magic != EM_EGL_IMAGE_MAGIC )
   {
      gEGLError= EGL_BAD_NATIVE_PIXMAP;
      goto exit;
   }

   free( img );

exit:   
   return result;
}

struct em_wl_object
{
   const void *interface;
   const void *implementation;
   uint32_t id;
};

struct em_wl_proxy
{
   struct em_wl_object object;
   struct wl_display *display;
   // etc
};

static struct wl_display* getWLDisplayFromProxy( void *proxy )
{
   struct wl_display *wldisp= 0;
   
   if ( proxy )
   {
      wldisp= (struct wl_display*)*((void**)(proxy+sizeof(struct em_wl_object)));
   }
   
   return wldisp;
}

static EGLNativeWindowType wlGetNativeWindow( struct wl_egl_window *egl_window )
{
   return egl_window->nativeWindow;
}

typedef struct bufferInfo
{
   struct wl_egl_window *egl_window;
   int deviceBuffer;
   int fd_os;
} bufferInfo;

static void buffer_release( void *data, struct wl_buffer *buffer )
{
   bufferInfo *binfo= (bufferInfo*)data;

   --binfo->egl_window->activeBuffers;

   if ( binfo->fd_os >= 0 )
   {
      EMDeviceCloseOS( binfo->fd_os );
   }   

   wl_buffer_destroy( buffer );
   
   if ( binfo->egl_window->windowDestroyPending && (binfo->egl_window->activeBuffers <= 0) )
   {
      wl_egl_window_destroy( binfo->egl_window );      
   }
   free( binfo );
}

static struct wl_buffer_listener wl_buffer_listener= 
{
   buffer_release
};

static void wlSwapBuffers( struct wl_egl_window *egl_window )
{
   TRACE1("wlSwapBuffers");
   if ( egl_window->drm && !egl_window->windowDestroyPending )
   {
      int buffer;
      struct wl_buffer *wlBuff= 0;
      bufferInfo *binfo= 0;
      int fd_os= -1;

      egl_window->eglSwapCount += 1;

      egl_window->bufferId += 1;
      if ( egl_window->bufferId >= egl_window->bufferIdBase+egl_window->bufferIdCount )
      {
         egl_window->bufferId= egl_window->bufferIdBase;
      }
      buffer= egl_window->bufferId;

      fd_os= EMDeviceOpenOS(buffer);
      wl_proxy_set_queue((struct wl_proxy*)egl_window->drm, egl_window->queue);
      wlBuff= wl_drm_create_prime_buffer( egl_window->drm,
                                          fd_os,
                                          egl_window->width,
                                          egl_window->height,
                                          WL_DRM_FORMAT_ARGB8888,
                                          0,
                                          egl_window->width*4,
                                          0,
                                          0,
                                          buffer,
                                          0
                                        );
      if ( wlBuff )
      {
         binfo= (bufferInfo*)malloc( sizeof(bufferInfo) );
         if ( binfo )
         {
            binfo->egl_window= egl_window;
            binfo->deviceBuffer= buffer;
            binfo->fd_os= fd_os;

            ++egl_window->activeBuffers;
            wl_buffer_add_listener( wlBuff, &wl_buffer_listener, binfo );

            egl_window->attachedWidth= egl_window->width;
            egl_window->attachedHeight= egl_window->height;
            wl_surface_attach( egl_window->surface, wlBuff, egl_window->dx, egl_window->dy );
            wl_surface_damage( egl_window->surface, 0, 0, egl_window->width, egl_window->height);
            wl_surface_commit( egl_window->surface );   

            wl_display_flush( egl_window->wldisp );
            
            // A call to roundtrip here allows weston-simple-egl to run since that app doesn't
            // call wl_display_dispatch() but merely wl_display_dispatch_pending() which
            // doesn't read from display fd.  This means it doesn't get all the buffer
            // relesae messages.  Adding a round-trip here caused the fd to be read and
            // fixes issue with weston-simple-egl.  However, this breaks apps that call
            // wl_display_dispatch() since it results in multiple threads reading the fd
            // and dispatching the queue.  We would rather have apps other than weston-simple-egl
            // work so we're leaving this call commented.
            //wl_display_roundtrip_queue(egl_window->display->display, egl_window->queue);
         }
         else
         {
            wl_buffer_destroy( wlBuff );
         }
      }
      
      wl_display_dispatch_queue_pending( egl_window->wldisp, egl_window->queue );
   }
}

extern "C" {

struct wl_egl_window *wl_egl_window_create(struct wl_surface *surface, int width, int height)
{
   struct wl_egl_window *egl_window= 0;
   //WEGLNativeWindowListener windowListener;
   struct wl_display *wldisp;
   EMCTX *ctx= 0;

   TRACE1("wl_egl_window_create");

   wldisp= getWLDisplayFromProxy(surface);
   if ( wldisp )
   {
      ctx= emGetContext();
      if ( ctx )
      {
         egl_window= (wl_egl_window*)calloc( 1, sizeof(struct wl_egl_window) );
         if ( !egl_window )
         {
            printf("wayland-egl: wl_egl_window_create: failed to allocate wl_egl_window\n");
            goto exit;
         }

         egl_window->ctx= ctx;
         egl_window->wldisp= wldisp;
         egl_window->surface= surface;
         egl_window->width= width;
         egl_window->height= height;
         egl_window->windowDestroyPending= false;
         egl_window->activeBuffers= 0;
         egl_window->bufferIdBase= 1;
         egl_window->bufferIdCount= 3;
         egl_window->bufferId= 3;
        
         egl_window->queue= wl_display_create_queue(egl_window->wldisp);
         if ( !egl_window->queue )
         {
            printf("wayland-egl: wl_egl_window_create: unable to create event queue\n");
            free( egl_window );
            egl_window= 0;
            goto exit;
         }

         egl_window->registry= wl_display_get_registry( egl_window->wldisp );
         if ( !egl_window->registry )
         {
            printf("wayland-egl: wl_egl_window_create: unable to get display registry\n");
            free( egl_window );
            egl_window= 0;
            goto exit;
         }
         wl_proxy_set_queue((struct wl_proxy*)egl_window->registry, egl_window->queue);
         wl_registry_add_listener(egl_window->registry, &winRegistryListener, egl_window);
         wl_display_roundtrip_queue(egl_window->wldisp, egl_window->queue);
         
         if ( !egl_window->drm )
         {
            printf("wayland-egl: wl_egl_window_create: no wl_drm protocol available\n");
            wl_egl_window_destroy( egl_window );
            egl_window= 0;
            goto exit;
         }

         pthread_mutex_lock( &gMutex );
         gNativeWindows.push_back( egl_window );
         pthread_mutex_unlock( &gMutex );
      }
   }

exit:   

   TRACE1("wl_egl_window_create: egl_window %p", egl_window);

   return egl_window;
}

void wl_egl_window_destroy(struct wl_egl_window *egl_window)
{
   TRACE1("wl_egl_window_destroy: egl_window %p", egl_window);

   if ( egl_window )
   {
      if ( egl_window->activeBuffers )
      {
         egl_window->windowDestroyPending= true;
      }
      else
      {
         pthread_mutex_lock( &gMutex );
         for( std::vector<struct wl_egl_window*>::iterator it= gNativeWindows.begin();
              it != gNativeWindows.end();
              ++it )
         {
            if ( (*it) == egl_window )
            {
               gNativeWindows.erase(it);
               break;
            }
         }
         pthread_mutex_unlock( &gMutex );
         
         if ( egl_window->drm )
         {
            wl_proxy_set_queue((struct wl_proxy*)egl_window->drm, 0);
            wl_drm_destroy( egl_window->drm );
            egl_window->drm= 0;
         }

         if ( egl_window->registry )
         {
            wl_proxy_set_queue((struct wl_proxy*)egl_window->registry, 0);
            wl_registry_destroy(egl_window->registry);
            egl_window->registry= 0;
         }
         
         if ( egl_window->queue )
         {
            wl_event_queue_destroy( egl_window->queue );
            egl_window->queue= 0;
         }
         
         free( egl_window );      
      }
   }
}

void wl_egl_window_resize(struct wl_egl_window *egl_window, int width, int height, int dx, int dy)
{
   TRACE1("wl_egl_window_resize");

   if ( egl_window )
   {
      egl_window->dx += dx;
      egl_window->dy += dy;
      egl_window->width= width;
      egl_window->height= height;
   }
}

void wl_egl_window_get_attached_size(struct wl_egl_window *egl_window, int *width, int *height)
{
   if ( egl_window )
   {
      if ( width )
      {
         *width= egl_window->attachedWidth;
      }
      if ( height )
      {
         *height= egl_window->attachedHeight;
      }
   }
}

} /* extern "C" */

static void *wlEglGetDeviceBuffer(struct wl_resource *resource)
{
   void *deviceBuffer= 0;

   if( wl_resource_instance_of( resource, &wl_buffer_interface, &bufferInterface ) ) 
   {
      struct wl_drm_buffer *drmBuffer;
      
      drmBuffer= (wl_drm_buffer *)wl_resource_get_user_data( (wl_resource*)resource );
      if ( drmBuffer )
      {
         // The bufferId is passed both via fd and as offset2 in this
         // emulation.  The value passed via fd is only reliable when the client
         // and server are in the main emulation process.  The value passed via 
         // offset2 is always reliable so we use it.
         deviceBuffer= (void *)(intptr_t)drmBuffer->offset[2];
      }
   }
   
   return deviceBuffer;
}


// Section: GLES ------------------------------------------------------

GLenum gGLError= GL_NO_ERROR;

GL_APICALL void GL_APIENTRY glActiveTexture (GLenum texture)
{
   TRACE1("glActiveTextue");
}

GL_APICALL void GL_APIENTRY glAttachShader (GLuint program, GLuint shader)
{
   TRACE1("glAttachShader");
}

GL_APICALL void GL_APIENTRY glBindAttribLocation (GLuint program, GLuint index, const GLchar *name)
{
   TRACE1("glBindAttribLocation");
}

GL_APICALL void GL_APIENTRY glBindFramebuffer (GLenum target, GLuint framebuffer)
{
   TRACE1("glBindFramebuffer");
}

GL_APICALL void GL_APIENTRY glBindTexture (GLenum target, GLuint texture)
{
   TRACE1("glBindTexture");
}

GL_APICALL void GL_APIENTRY glBlendColor (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
   TRACE1("glBlendColor");
}

GL_APICALL void GL_APIENTRY glBlendFuncSeparate (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
   TRACE1("glBlendFuncSeparate");
}

GL_APICALL GLenum GL_APIENTRY glCheckFramebufferStatus (GLenum target)
{
   GLenum result;

   TRACE1("glCheckFramebufferStatus");

   result= GL_FRAMEBUFFER_COMPLETE;

   return result;
}

GL_APICALL void GL_APIENTRY glClear (GLbitfield mask)
{
   EMCTX *ctx= 0;

   TRACE1("glClear");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glClear: emGetContext failed");
      goto exit;
   }

   if ( ctx->holePunchedCB && (ctx->clearColor[3] == 0.0) )
   {
      int hx, hy, hw, hh;
      hx= ctx->scissorBox[0];
      hy= ctx->viewport[3]-ctx->scissorBox[1]-ctx->scissorBox[3];
      hw= ctx->scissorBox[2];
      hh= ctx->scissorBox[3];
      ctx->holePunchedCB( ctx, ctx->holePunchedUserData, hx, hy, hw, hh );
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glClearColor (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
   EMCTX *ctx= 0;

   TRACE1("glClearColor");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glClearColor: emGetContext failed");
      goto exit;
   }

   ctx->clearColor[0]= red;
   ctx->clearColor[1]= green;
   ctx->clearColor[2]= blue;
   ctx->clearColor[3]= alpha;

exit:
   return;
}

GL_APICALL void GL_APIENTRY glCompileShader (GLuint shader)
{
   TRACE1("glCompileShader");
}

GL_APICALL GLuint GL_APIENTRY glCreateProgram (void)
{
   GLuint programId;
   EMCTX *ctx= 0;

   TRACE1("glCreateProgram");
   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glCreateProgram: emGetContext failed");
      goto exit;
   }

   programId= ++ctx->nextProgramId;

exit:
   return programId;
}

GL_APICALL GLuint GL_APIENTRY glCreateShader (GLenum type)
{
   GLuint shaderId;
   EMCTX *ctx= 0;

   TRACE1("glCreateShader");
   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glCreateShader: emGetContext failed");
      goto exit;
   }

   shaderId= ++ctx->nextShaderId;

exit:
   return shaderId;
}

GL_APICALL void GL_APIENTRY glDeleteFramebuffers (GLsizei n, const GLuint *framebuffers)
{
   EMCTX *ctx= 0;
   bool found;

   TRACE1("glDeleteFramebuffers");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glDeleteFramebuffers: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < n; ++i )
   {
      found= false;
      for( int j= 0; j < ctx->framebufferIds.size(); ++j )
      {
         if ( ctx->framebufferIds[j] == framebuffers[i] )
         {
            ctx->framebufferIds[j]= 0;
            found= true;
            break;
         }
      }
      if ( !found )
      {
         ERROR("glDeleteFramebuffers: bad framebuffer id %d", framebuffers[i] );
      }
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glDeleteProgram (GLuint program)
{
   TRACE1("glDeleteProgram");
}

GL_APICALL void GL_APIENTRY glDeleteShader (GLuint shader)
{
   TRACE1("glDeleteShader");
}

GL_APICALL void GL_APIENTRY glDeleteTextures (GLsizei n, const GLuint *textures)
{
   EMCTX *ctx= 0;
   bool found;

   TRACE1("glDeleteTextures");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glDeleteTextures: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < n; ++i )
   {
      found= false;
      for( int j= 0; j < ctx->textureIds.size(); ++j )
      {
         if ( ctx->textureIds[j] == textures[i] )
         {
            ctx->textureIds[j]= 0;
            found= true;
            break;
         }
      }
      if ( !found )
      {
         ERROR("glDeleteTextures: bad texture id %d", textures[i] );
      }
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glDetachShader (GLuint program, GLuint shader)
{
   TRACE1("glDetachShader");
}

GL_APICALL void GL_APIENTRY glDisable (GLenum cap)
{
   TRACE1("glDisable");
}

GL_APICALL void GL_APIENTRY glDisableVertexAttribArray (GLuint index)
{
   TRACE1("glDisableVertexAttribArray");
}

GL_APICALL void GL_APIENTRY glDrawArrays (GLenum mode, GLint first, GLsizei count)
{
   TRACE1("glDrawArrays");
}

GL_APICALL void GL_APIENTRY glEGLImageTargetTexture2DOES (GLenum target, GLeglImageOES image)
{
   TRACE1("glEGLImageTargetTexture2DOES");
   switch( target )
   {
      case GL_TEXTURE_2D:
      case GL_TEXTURE_EXTERNAL_OES:
         {
            EMEGLImage* img= (EMEGLImage*)image;
            if ( img )
            {
               if ( target == GL_TEXTURE_EXTERNAL_OES )
               {
                  TRACE1("glEGLImageTargetTexture2DOES: target GL_TEXTURE_EXTERNAL_OES");
               }
               if ( img->magic == EM_EGL_IMAGE_MAGIC )
               {
                  int bufferId=-1;

                  if ( img->target == EGL_WAYLAND_BUFFER_WL )
                  {
                     struct wl_resource *resource= (struct wl_resource*)img->clientBuffer;
                     void *deviceBuffer= wlEglGetDeviceBuffer( resource );
                     if( (long long)deviceBuffer < 0 )
                     {
                        ERROR("glEGLImageTargetTexture2DOES: image %p had bad device buffer %d", img, deviceBuffer);
                     }
                     else
                     {
                        bufferId= (((long long)deviceBuffer)&0xFFFFFFFF);
                     }
                  }
                  else if ( img->target == EGL_LINUX_DMA_BUF_EXT )
                  {
                     bufferId= img->fd;
                  }
                  else if ( img->target == EGL_NATIVE_PIXMAP_KHR )
                  {
                     bufferId= 0;
                  }

                  if ( bufferId >= 0 )
                  {
                     EMCTX *ctx= 0;
                     ctx= emGetContext();
                     if ( ctx )
                     {
                        if ( ctx->textureCreatedCB )
                        {
                           ctx->textureCreatedCB( ctx, ctx->textureCreatedUserData, bufferId );
                        }
                     }
                     else
                     {
                        ERROR("glEGLImageTargetTexture2DOES: emGetContext failed");
                     }
                  }
               }
               else
               {
                  ERROR("glEGLImageTargetTexture2DOES: bad image %p", image);
               }
            }
         }
         break;
      default:
         WARNING("glEGLImageTargetTexture2DOES: unsupported target %X", target);
         break; 
   }
}

GL_APICALL void GL_APIENTRY glEnable (GLenum cap)
{
   TRACE1("glEnable");
}

GL_APICALL void GL_APIENTRY glEnableVertexAttribArray (GLuint index)
{
   TRACE1("glEnableVertexAttribArray");
}

GL_APICALL void GL_APIENTRY glFinish (void)
{
   TRACE1("glFinish");
}

GL_APICALL void GL_APIENTRY glFlush (void)
{
   TRACE1("glFlush");
}

GL_APICALL void GL_APIENTRY glFramebufferTexture2D (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
   TRACE1("glFramebufferTexture2D");
}

GL_APICALL void GL_APIENTRY glGenFramebuffers (GLsizei n, GLuint *framebuffers)
{
   EMCTX *ctx= 0;

   TRACE1("glGenFramebuffers");
   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glGenFramebuffers: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < n; ++i )
   {
      GLuint framebufferId= 0;
      for( int j= 0; j < ctx->framebufferIds.size(); ++j )
      {
         if ( ctx->framebufferIds[j] == 0 )
         {
            framebufferId= j+1;
            ctx->framebufferIds[j]= framebufferId;
            break;
         }
      }
      if ( framebufferId == 0 )
      {
         framebufferId= ++ctx->nextFramebufferId;
         INFO("new framebufferId %d", framebufferId);
         ctx->framebufferIds.push_back(framebufferId);
      }
      framebuffers[i]= framebufferId;
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glGenTextures (GLsizei n, GLuint *textures)
{
   EMCTX *ctx= 0;

   TRACE1("glGenTextures");
   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glGenTextures: emGetContext failed");
      goto exit;
   }

   for( int i= 0; i < n; ++i )
   {
      GLuint textureId= 0;
      for( int j= 0; j < ctx->textureIds.size(); ++j )
      {
         if ( ctx->textureIds[j] == 0 )
         {
            textureId= j+1;
            ctx->textureIds[j]= textureId;
            break;
         }
      }
      if ( textureId == 0 )
      {
         textureId= ++ctx->nextTextureId;
         INFO("new textureId %d", textureId);
         ctx->textureIds.push_back(textureId);
      }
      textures[i]= textureId;
   }

exit:
   return;
}

GL_APICALL GLenum GL_APIENTRY glGetError (void)
{
   GLenum err;

   TRACE1("glGetError");
   
   err= gGLError;
   gGLError= GL_NO_ERROR;

   return err;
}

GL_APICALL void GL_APIENTRY glGetFloatv (GLenum pname, GLfloat *data)
{
   EMCTX *ctx= 0;

   TRACE1("glGetFloatv");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glGetFloatv: emGetContext failed");
      goto exit;
   }

   switch( pname )
   {
      case GL_COLOR_CLEAR_VALUE:
         data[0]= ctx->clearColor[0];
         data[1]= ctx->clearColor[1];
         data[2]= ctx->clearColor[2];
         data[3]= ctx->clearColor[3];
         break;
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glGetIntegerv (GLenum pname, GLint *data)
{
   EMCTX *ctx= 0;

   TRACE1("glGetIntegerv");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glGetIntegerv: emGetContext failed");
      goto exit;
   }

   switch( pname )
   {
      case GL_SCISSOR_BOX:
         data[0]= ctx->scissorBox[0];
         data[1]= ctx->scissorBox[1];
         data[2]= ctx->scissorBox[2];
         data[3]= ctx->scissorBox[3];
         break;
      case GL_VIEWPORT:
         data[0]= ctx->viewport[0];
         data[1]= ctx->viewport[1];
         data[2]= ctx->viewport[2];
         data[3]= ctx->viewport[3];
         break;
      case GL_CURRENT_PROGRAM:
         data[0]= ctx->currentProgramId;
         break;
      default:
         WARNING("glGetIntegerv: unsupported pname: %x", pname);
         break;
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glGetProgramiv (GLuint program, GLenum pname, GLint *params)
{
   EMCTX *ctx= 0;

   TRACE1("glGetProgramiv");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glGetProgramiv: emGetContext failed");
      goto exit;
   }

   switch( pname )
   {
      case GL_LINK_STATUS:
         *params= GL_TRUE;
         break;
      default:
         WARNING("glGetProgramiv: unsupported pname: %x", pname);
         break;
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glGetProgramInfoLog (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
   TRACE1("glGetProgramInfoLog");
   *length= 0;
   *infoLog= '\0';
}

GL_APICALL void GL_APIENTRY glGetShaderiv (GLuint shader, GLenum pname, GLint *params)
{
   EMCTX *ctx= 0;

   TRACE1("glGetShaderiv");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glGetShaderiv: emGetContext failed");
      goto exit;
   }

   switch( pname )
   {
      case GL_COMPILE_STATUS:
         *params= GL_TRUE;
         break;
      default:
         WARNING("glGetShaderiv: unsupported pname: %x", pname);
         break;
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glGetShaderInfoLog (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
   TRACE1("glGetShaderInfoLog");
   *length= 0;
   *infoLog= '\0';
}

GL_APICALL const GLubyte *GL_APIENTRY glGetString (GLenum name)
{
   const char *s= 0;

   TRACE1("glGetString for %X", name );

   switch( name )
   {
      case GL_EXTENSIONS:
         s= "GL_OES_EGL_image_external";
         break;
   }

exit:

   TRACE1("glGetString for %X: (%s)", name, s );

   return (const GLubyte*)s;
}

GL_APICALL GLint GL_APIENTRY glGetUniformLocation (GLuint program, const GLchar *name)
{
   GLint location= 0;
   EMCTX *ctx= 0;

   TRACE1("glGetUniformLocation");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glIsEnabled: emGetContext failed");
      goto exit;
   }

   location= ++ctx->nextUniformLocation;

exit:
   return location;
}

GL_APICALL GLboolean GL_APIENTRY glIsEnabled (GLenum cap)
{
   GLboolean result= GL_FALSE;
   EMCTX *ctx= 0;

   TRACE1("glIsEnabled");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glIsEnabled: emGetContext failed");
      goto exit;
   }

   switch( cap )
   {
      case GL_SCISSOR_TEST:
         result= ctx->scissorEnable;
         break;
      default:
         WARNING("glIsEnabled: unsupported cap: %x", cap);
         break;
   }

exit:
   return result;
}

GL_APICALL void GL_APIENTRY glLinkProgram (GLuint program)
{
   TRACE1("glLinkProgram");
}

GL_APICALL void GL_APIENTRY glScissor (GLint x, GLint y, GLsizei width, GLsizei height)
{
   EMCTX *ctx= 0;

   TRACE1("glScissor: (%d, %d, %d, %d)", x, y, width, height);

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glScissor: emGetContext failed");
      goto exit;
   }

   ctx->scissorBox[0]= x;
   ctx->scissorBox[1]= y;
   ctx->scissorBox[2]= width;
   ctx->scissorBox[3]= height;

exit:
   return;
}

GL_APICALL void GL_APIENTRY glShaderSource (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length)
{
   TRACE1("glShaderSource");
}

GL_APICALL void GL_APIENTRY glTexImage2D (GLenum target, 
                                          GLint level, 
                                          GLint internalformat, 
                                          GLsizei width, 
                                          GLsizei height, 
                                          GLint border, 
                                          GLenum format, 
                                          GLenum type, 
                                          const void *pixels)
{
   EMCTX *ctx= 0;

   TRACE1("glTexImage2D");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glTexImage2D: emGetContext failed");
      goto exit;
   }

   if ( ctx->textureCreatedCB )
   {
      int bufferId= (width<<16)|(height);
      ctx->textureCreatedCB( ctx, ctx->textureCreatedUserData, bufferId );
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glTexParameterf (GLenum target, GLenum pname, GLfloat param)
{
   EMCTX *ctx= 0;

   TRACE1("glTexParameterf");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glTexParameterf: emGetContext failed");
      goto exit;
   }

   switch( pname )
   {
      case GL_TEXTURE_WRAP_S:
         ctx->textureWrapS= param;
         break;
      case GL_TEXTURE_WRAP_T:
         ctx->textureWrapT= param;
         break;
      default:
         WARNING("glTexParameterf: unsupported pname %X", pname);
         break;
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glTexParameteri (GLenum target, GLenum pname, GLint param)
{
   EMCTX *ctx= 0;

   TRACE1("glTexParameteri");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glTexParameteri: emGetContext failed");
      goto exit;
   }

   switch( pname )
   {
      case GL_TEXTURE_MAG_FILTER:
         ctx->textureMagFilter= param;
         break;
      case GL_TEXTURE_MIN_FILTER:
         ctx->textureMinFilter= param;
         break;
      default:
         WARNING("glTexParameteri: unsupported pname %X", pname);
         break;
   }

exit:
   return;
}

GL_APICALL void GL_APIENTRY glUniform1f (GLint location, GLfloat v0)
{
   TRACE1("glUniform1f");
}

GL_APICALL void GL_APIENTRY glUniform2f (GLint location, GLfloat v0, GLfloat v1)
{
   TRACE1("glUniform2f");
}

GL_APICALL void GL_APIENTRY glUniform4f (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
   TRACE1("glUniform4f");
}

GL_APICALL void GL_APIENTRY glUniform1i (GLint location, GLint v0)
{
   TRACE1("glUniform1i");
}

GL_APICALL void GL_APIENTRY glUniformMatrix4fv (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   TRACE1("glUniformMatrix4fv");
}

GL_APICALL void GL_APIENTRY glUseProgram (GLuint program)
{
   EMCTX *ctx= 0;

   TRACE1("glUseProgram");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glUseProgram: emGetContext failed");
      goto exit;
   }

   ctx->currentProgramId= program;

exit:
   return;
}

GL_APICALL void GL_APIENTRY glVertexAttribPointer (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
   TRACE1("glVertexAttribPointer");
}

GL_APICALL void GL_APIENTRY glViewport (GLint x, GLint y, GLsizei width, GLsizei height)
{
   EMCTX *ctx= 0;

   TRACE1("glViewport: (%d, %d, %d, %d)", x, y, width, height);

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glViewport: emGetContext failed");
      goto exit;
   }

   ctx->viewport[0]= ctx->scissorBox[0]= x;
   ctx->viewport[1]= ctx->scissorBox[1]= y;
   ctx->viewport[2]= ctx->scissorBox[2]= width;
   ctx->viewport[3]= ctx->scissorBox[3]= height;

exit:
   return;
}


