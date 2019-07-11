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
#include <dlfcn.h>
#include <fcntl.h>
#include <memory.h>
#include <pthread.h>
#include <unistd.h>
#include <syscall.h>
#include <sys/time.h>
#include <stdint.h>
#include "berr.h"
#include "nexus_config.h"
#include "default_nexus.h"
#include "bmedia_types.h"
#include "nexus_platform.h"
#include "nexus_simple_stc_channel.h"
#include "nexus_simple_video_decoder.h"
#include "nexus_surface_client.h"
#include "nxclient.h"
#include "westeros-ut-em.h"
#include "wayland-egl.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "EGL/begl_displayplatform.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "wayland-client.h"
#include "wayland-server.h"
#include "bnxs-client-protocol.h"
#include "bnxs-server-protocol.h"

#include <vector>
#include <map>

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

struct wl_bnxs_buffer 
{
   struct wl_resource *resource;
   uint32_t format;
   int32_t width;
   int32_t height;
   int32_t stride;
   uint32_t nexusSurfaceHandle;
};

struct wl_egl_window
{
   EMCTX *ctx;
   struct wl_display *wldisp;
   struct wl_surface *surface;
   struct wl_registry *registry;
   struct wl_bnxs *bnxs;
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

typedef struct _EMEvent
{
} EMEvent;

typedef struct _EMGraphics
{
} EMGraphics;

typedef struct _EMSurface
{
   NEXUS_PixelFormat pixelFormat;
   int width;
   int height;
   NEXUS_SurfaceMemory mem;
} EMSurface;

typedef struct _EMSurfaceClient
{
   unsigned client_id;
   NEXUS_SurfaceClientSettings settings;
   bool isVideoWindow;
   NEXUS_Rect pendingPosition;
   NEXUS_Rect position;
   bool positionIsPending;
   long long pendingPositionTime;
} EMSurfaceClient;

typedef struct _EMNativeWindow
{
   uint32_t magic;
   EMCTX *ctx;
   int x;
   int y;
   int width;
   int height;
} EMNativeWindow;

typedef struct _EMNativePixmap
{
   int width;
   int height;
   EMSurface *s;
} EMNativePixmap;


#define COLORIMETRY_MAX_LEN (20)
#define MASTERINGMETA_MAX_LEN (100)
#define CONTENTLIGHT_MAX_LEN (20)

typedef struct _EMSimpleVideoDecoder
{
   uint32_t magic;
   EMCTX *ctx;
   bool inUse;
   int type;
   uint32_t startPTS;
   void *stcChannel;
   bool started;
   int videoWidth;
   int videoHeight;
   char colorimetry[COLORIMETRY_MAX_LEN+1];
   char masteringMeta[MASTERINGMETA_MAX_LEN+1];
   char contentLight[CONTENTLIGHT_MAX_LEN+1];
   float videoFrameRate;  //FPS
   float videoBitRate;  // Mbps
   bool segmentsStartAtZero;
   bool firstPtsPassed;
   unsigned frameNumber;
   uint32_t currentPTS;
   NEXUS_SimpleVideoDecoderClientSettings clientSettings;
   NEXUS_SimpleVideoDecoderStartSettings startSettings;
   NEXUS_VideoDecoderSettings decoderSettings;
   NEXUS_VideoDecoderTrickState trickState;
   bool captureStarted;
   int captureSurfaceCount;
   int captureSurfaceGetNext;
   int captureSurfacePutNext;
   NEXUS_SimpleVideoDecoderStartCaptureSettings captureSettings;
} EMSimpleVideoDecoder;

typedef struct _EMNXClient
{
   NEXUS_SurfaceComposition composition;
} EMNXClient;

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
#define EM_MAX_NXCLIENT (5)

typedef struct _EMWLBinding
{
   EGLDisplay wlBoundDpy;
   struct wl_display *display;
   struct wl_bnxs *bnxs;
} EMWLBinding;

typedef struct _EMWLRemote
{
   struct wl_display *dspsrc;
   struct wl_display *dspdest;
   struct wl_bnxs *bnxsRemote;
   struct wl_registry *registry;
   struct wl_event_queue *queue;
} EMWLRemote;

typedef struct _EMCTX
{
   int displayWidth;
   int displayHeight;
   EGLContext eglContext;
   EGLDisplay eglDisplayDefault;
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
   struct wl_display* display;
   std::map<struct wl_display*,EMWLBinding> wlBindings;
   std::map<struct wl_display*,EMWLRemote> wlRemotes;
   unsigned nextNxClientConnectId;
   EMSimpleVideoDecoder simpleVideoDecoderMain;
   NEXUS_VideoDecoderStatus simpleVideoDecoderStatusMain;
   EMSurfaceClient *videoWindowMain;
   void *stcChannel;
   int videoCodec;
   void *videoPidChannel;
   int waylandSendTid;
   bool waylandThreadingIssue;
   bool westerosModuleInitShouldFail;
   bool westerosModuleInitCalled;
   bool westerosModuleTermCalled;
   int nextNxClientId;
   EMNXClient nxclients[EM_MAX_NXCLIENT];
   std::map<unsigned,EMSurfaceClient*> surfaceClients;

   EMTextureCreated textureCreatedCB;
   void *textureCreatedUserData;

   char errorDetail[EM_MAX_ERROR];
} EMCTX;

static EMCTX* emGetContext( void );
static EMCTX* emCreate( void );
static void emDestroy( EMCTX* ctx );
static EGLNativeWindowType wlGetNativeWindow( struct wl_egl_window *egl_window );
static void wlSwapBuffers( struct wl_egl_window *egl_window );

static pthread_mutex_t gMutex= PTHREAD_MUTEX_INITIALIZER;
static EMCTX *gCtx= 0;
static int gActiveLevel= 2;
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
   return emGetContext();
}

void EMDestroyContext( EMCTX* ctx )
{
   if ( ctx )
   {
      emDestroy( ctx );
      gCtx= 0;
   }
}

bool EMSetDisplaySize( EMCTX *ctx, int width, int height )
{
   bool result= false;

   if ( ctx )
   {
      ctx->displayWidth= width;
      ctx->displayHeight= height;

      // Generate display change event
      for( std::map<unsigned,EMSurfaceClient*>::iterator it= ctx->surfaceClients.begin();
           it != ctx->surfaceClients.end();
           ++it )
      {
         EMSurfaceClient *emsc= (*it).second;
         if ( emsc->settings.displayStatusChanged.callback )
         {
            emsc->settings.displayStatusChanged.callback( emsc->settings.displayStatusChanged.context,
                                                          emsc->settings.displayStatusChanged.param );
         }
      }

      result= true;
   }

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

void EMSetStcChannel( EMCTX *ctx, void *stcChannel )
{
   ctx->stcChannel= stcChannel;
}

void* EMGetStcChannel( EMCTX *ctx )
{
   return ctx->stcChannel;
}

void EMSetVideoCodec( EMCTX *ctx, int codec )
{
   ctx->videoCodec= codec;
}

int EMGetVideoCodec( EMCTX *ctx )
{
   return ctx->videoCodec;
}

void EMSetVideoPidChannel( EMCTX *ctx, void *videoPidChannel )
{
   ctx->videoPidChannel= videoPidChannel;
}

void* EMGetVideoPidChannel( EMCTX *ctx )
{
   return ctx->videoPidChannel;
}

EMSurfaceClient* EMGetVideoWindow( EMCTX *ctx, int id )
{
   // ignore id for now

   return ctx->videoWindowMain;
}

void EMSurfaceClientGetPosition( EMSurfaceClient *emsc, int *x, int *y, int *width, int *height )
{
   NEXUS_Rect pendingPosition;
   NEXUS_Rect position;
   bool positionIsPending;
   long long pendingPositionTime;
   if ( emsc->positionIsPending && (getCurrentTimeMillis() > emsc->pendingPositionTime) )
   {
      emsc->positionIsPending= false;
      emsc->position= emsc->pendingPosition;
   }
   if ( x ) *x= emsc->position.x;
   if ( y ) *y= emsc->position.y;
   if ( width ) *width= emsc->position.width;
   if ( height ) *height= emsc->position.height;
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

void EMSimpleVideoDecoderSetColorimetry( EMSimpleVideoDecoder *dec, const char *colorimetry )
{
   memset( dec->colorimetry, 0, sizeof(dec->colorimetry) );
   strncpy( dec->colorimetry, colorimetry, COLORIMETRY_MAX_LEN );
}

const char* EMSimpleVideoDecoderGetColorimetry( EMSimpleVideoDecoder *dec )
{
   return dec->colorimetry;
}

void EMSimpleVideoDecoderSetMasteringMeta( EMSimpleVideoDecoder *dec, const char *masteringMeta )
{
   memset( dec->masteringMeta, 0, sizeof(dec->masteringMeta) );
   strncpy( dec->masteringMeta, masteringMeta, MASTERINGMETA_MAX_LEN );
}

const char* EMSimpleVideoDecoderGetMasteringMeta( EMSimpleVideoDecoder *dec )
{
   return dec->masteringMeta;
}

void EMSimpleVideoDecoderSetContentLight( EMSimpleVideoDecoder *dec, const char *contentLight )
{
   memset( dec->contentLight, 0, sizeof(dec->contentLight) );
   strncpy( dec->contentLight, contentLight, CONTENTLIGHT_MAX_LEN );
}

const char* EMSimpleVideoDecoderGetContentLight( EMSimpleVideoDecoder *dec )
{
   return dec->contentLight;
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

void EMSimpleVideoDecoderSignalUnderflow( EMSimpleVideoDecoder *dec )
{
   if ( dec->decoderSettings.fifoEmpty.callback )
   {
      dec->decoderSettings.fifoEmpty.callback( dec->decoderSettings.fifoEmpty.context, 0 );
   }
}

void EMSimpleVideoDecoderSignalPtsError( EMSimpleVideoDecoder *dec )
{
   if ( dec->decoderSettings.ptsError.callback )
   {
      dec->decoderSettings.ptsError.callback( dec->decoderSettings.ptsError.context, 0 );
   }
}

void EMSimpleVideoDecoderSetTrickStateRate( EMSimpleVideoDecoder *dec, int rate )
{
   dec->trickState.rate= rate;
}

int EMSimpleVideoDecoderGetTrickStateRate( EMSimpleVideoDecoder *dec )
{
   return dec->trickState.rate;
}

int EMSimpleVideoDecoderGetHdrEotf( EMSimpleVideoDecoder *dec )
{
   return dec->startSettings.settings.eotf;
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
      ctx->eglContext= EGL_NO_CONTEXT;
      
      ctx->wlBindings= std::map<struct wl_display*,EMWLBinding>();
      ctx->wlRemotes= std::map<struct wl_display*,EMWLRemote>();
      ctx->surfaceClients= std::map<unsigned,EMSurfaceClient*>();

      ctx->simpleVideoDecoderMain.magic= EM_SIMPLE_VIDEO_DECODER_MAGIC;
      ctx->simpleVideoDecoderMain.inUse= false;
      ctx->simpleVideoDecoderMain.type= NEXUS_DISPLAY_WINDOW_MAIN;
      ctx->simpleVideoDecoderMain.ctx= ctx;
      ctx->simpleVideoDecoderMain.videoFrameRate= 60.0;
      ctx->simpleVideoDecoderMain.videoBitRate= 8.0;
      ctx->simpleVideoDecoderMain.trickState.rate= NEXUS_NORMAL_DECODE_RATE;
      ctx->simpleVideoDecoderMain.startSettings.settings.eotf= NEXUS_VideoEotf_eInvalid;

      ctx->videoCodec= bvideo_codec_unknown;

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
      free( ctx );
   }
}

// Section: bdbg ------------------------------------------------------

void BDBG_P_AssertFailed(const char *expr, const char *file, unsigned line)
{
   ERROR("BDBG_P_AssertFailed: (%s) %s:%d", expr, file, line);
}

// Section: bkni ------------------------------------------------------

void *BKNI_Memset(void *mem, int ch, size_t n)
{
   memset( mem, ch, n );
}

BERR_Code BKNI_CreateEvent(BKNI_EventHandle *event)
{
   BERR_Code rc= BERR_UNKNOWN;
   struct BKNI_EventObj *ev= 0;

   TRACE1("BKNI_CreateEvent");
   ev= (struct BKNI_EventObj *)calloc( 1, sizeof(EMEvent) );
   if ( !ev )
   {
      rc= BERR_OUT_OF_SYSTEM_MEMORY;
      goto exit;
   }

   *event= (BKNI_EventHandle)ev;
   rc= BERR_SUCCESS;

exit:
   return rc;
}

void BKNI_DestroyEvent(BKNI_EventHandle event)
{
   TRACE1("BKNI_DestroyEvent");
   if ( event )
   {
      struct BKNI_EventObj *ev= (struct BKNI_EventObj *)event;
      free( ev );
   }
}

void BKNI_SetEvent(BKNI_EventHandle event)
{
   TRACE1(" BKNI_SetEvent");
}

BERR_Code BKNI_WaitForEvent(BKNI_EventHandle event, int timeoutMsec)
{
   TRACE1("BKNI_WaitForEvent");
}


// Section: default_nexus ------------------------------------------------------

void NXPL_RegisterNexusDisplayPlatform(NXPL_PlatformHandle *handle, NEXUS_DISPLAYHANDLE display)
{
   TRACE1("NXPL_RegisterNexusDisplayPlatform");
   *handle= emGetContext();
}

void NXPL_UnregisterNexusDisplayPlatform(NXPL_PlatformHandle handle)
{
   TRACE1("NXPL_UnregisterNexusDisplayPlatform");
}

void *NXPL_CreateNativeWindow(const NXPL_NativeWindowInfo *info)
{
   void *nativeWindow= 0;
   EMCTX *ctx= 0;
   EMNativeWindow *nw= 0;

   TRACE1("NXPL_CreateNativeWindow");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("NXPL_CreateNativeWindow: emGetContext failed");
      goto exit;
   }

   nw= (EMNativeWindow*)calloc( 1, sizeof(EMNativeWindow) );
   if ( !nw )
   {
      ERROR("NXPL_CreateNativeWindow: no memory");
      goto exit;
   }

   nw->magic= EM_WINDOW_MAGIC;
   nw->ctx= ctx;
   nw->width= info->width;
   nw->height= info->height;

   nativeWindow= nw;

   TRACE1("NXPL_CreateNativeWindow: nw %p", nativeWindow );

exit:
   return nativeWindow;
}

void NXPL_DestroyNativeWindow(void *nativeWindow)
{
   TRACE1("NXPL_DestroyNativeWindow");

   if ( nativeWindow )
   {
      EMNativeWindow *nw= (EMNativeWindow*)nativeWindow;
      free( nw );
   }
}

void NXPL_ResizeNativeWindow( void *nativeWindow, int width, int height, int dx, int dy )
{
   TRACE1("NXPL_DestroyNativeWindow");

   if ( nativeWindow )
   {
      EMNativeWindow *nw= (EMNativeWindow*)nativeWindow;
      nw->x += dx;
      nw->y += dy;
      nw->width = width;
      nw->height = height;
   }
}

NXPL_EXPORT void NXPL_GetDefaultPixmapInfoEXT(struct BEGL_PixmapInfoEXT *info)
{
   memset( info, 0, sizeof(info));
   info->secure= false;
}

bool NXPL_CreateCompatiblePixmapEXT(NXPL_PlatformHandle handle, void **pixmapHandle,
                                    NEXUS_SURFACEHANDLE *surface, struct BEGL_PixmapInfoEXT *info)
{
   bool result= false;
   EMNativePixmap *npm= 0;
   EMSurface *ns= 0;

   TRACE1("NXPL_CreateCompatiblePixmapEXT");

   ns= (EMSurface*)calloc( 1, sizeof(EMSurface) );
   if ( ns )
   {
      ns->width= info->width;
      ns->height= info->height;
      ns->mem.pitch= info->width*4;
      ns->mem.buffer= calloc( 1, ns->mem.pitch*ns->height );
   }

   npm= (EMNativePixmap*)calloc( 1, sizeof(EMNativePixmap) );
   if ( npm )
   {
      npm->width= info->width;
      npm->height= info->height;
      npm->s= ns;
   }

   if ( ns && npm )
   {
      *pixmapHandle= npm;
      *surface= (NEXUS_SurfaceHandle)ns;
      result= true;
   }

   return result;
}

bool NXPL_CreateCompatiblePixmap(NXPL_PlatformHandle handle, void **pixmapHandle,
                                 NEXUS_SurfaceHandle *surface, struct BEGL_PixmapInfo *info)
{
   BEGL_PixmapInfoEXT extInfo;

   TRACE1("NXPL_CreateCompatiblePixmap");

   NXPL_GetDefaultPixmapInfoEXT(&extInfo);

   extInfo.format= info->format;
   extInfo.width= info->width;
   extInfo.height= info->height;

   return NXPL_CreateCompatiblePixmapEXT(handle, pixmapHandle, surface, &extInfo);
}

void NXPL_DestroyCompatiblePixmap(NXPL_PlatformHandle handle, void *pixmapHandle)
{
   EMNativePixmap *npm= (EMNativePixmap*)pixmapHandle;

   TRACE1("NXPL_DestroyCompatiblePixmap");

   if ( npm )
   {
      EMSurface *ns= npm->s;
      if ( ns )
      {
         if ( ns->mem.buffer )
         {
            free( ns->mem.buffer );
         }
         free( ns );
         npm->s= 0;
      }
      free( npm );
   }
}

// Section: nexus_platform --------------------------------------------------------

NEXUS_HeapHandle NEXUS_Platform_GetFramebufferHeap( unsigned displayIndex )
{
   return (NEXUS_HeapHandle)displayIndex;
}

// Section: nexus_graphics2d ------------------------------------------------------

void NEXUS_Graphics2D_GetDefaultOpenSettings(
   NEXUS_Graphics2DOpenSettings *pSettings
   )
{
   TRACE1("NEXUS_Graphics2D_GetDefaultOpenSettings");
}

NEXUS_Graphics2DHandle NEXUS_Graphics2D_Open(
    unsigned index,
    const NEXUS_Graphics2DOpenSettings *pSettings
    )
{
   NEXUS_Graphics2D *gfx= 0;

   TRACE1("NEXUS_Graphics2D_Open");
   gfx= (NEXUS_Graphics2D*)calloc( 1, sizeof(EMGraphics) );
   if ( gfx )
   {
      //TBD
   }

   return (NEXUS_Graphics2DHandle)gfx;
}

void NEXUS_Graphics2D_Close(
    NEXUS_Graphics2DHandle handle
    )
{
   NEXUS_Graphics2D *gfx= (NEXUS_Graphics2D*)handle;
   TRACE1("NEXUS_Graphics2D_Close");
   if ( gfx )
   {
      free( gfx );
   }
}

void NEXUS_Graphics2D_GetSettings(
    NEXUS_Graphics2DHandle handle,
    NEXUS_Graphics2DSettings *pSettings
    )
{
   TRACE1("NEXUS_Graphics2D_GetSettings");
}

NEXUS_Error NEXUS_Graphics2D_SetSettings(
    NEXUS_Graphics2DHandle handle,
    const NEXUS_Graphics2DSettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;

   TRACE1("NEXUS_Graphics2D_SetSettings");

   return rc;
}

void NEXUS_Graphics2D_GetDefaultBlitSettings(
    NEXUS_Graphics2DBlitSettings *pSettings
    )
{
   TRACE1("NEXUS_Graphics2D_GetDefaultBlitSettings");
}

NEXUS_Error NEXUS_Graphics2D_Blit(
    NEXUS_Graphics2DHandle handle,
    const NEXUS_Graphics2DBlitSettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;

   TRACE1("NEXUS_Graphics2D_Blit");

   return rc;
}

NEXUS_Error NEXUS_Graphics2D_Checkpoint(
    NEXUS_Graphics2DHandle handle,
    const NEXUS_CallbackDesc *pLegacyCallback
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;

   TRACE1("NEXUS_Graphics2D_Checkpoint");

   return rc;
}


// Section: nexus_surface ------------------------------------------------------

void NEXUS_Surface_GetDefaultCreateSettings(
    NEXUS_SurfaceCreateSettings *pCreateSettings
    )
{
   TRACE1("NEXUS_Surface_GetDefaultCreateSettings");
   memset( pCreateSettings, 0, sizeof(NEXUS_SurfaceCreateSettings) );
}

NEXUS_SurfaceHandle NEXUS_Surface_Create(
    const NEXUS_SurfaceCreateSettings *pCreateSettings
    )
{
   EMSurface *ns= 0;

   TRACE1("NEXUS_Surface_Create");

   ns= (EMSurface*)calloc( 1, sizeof(EMSurface) );
   if ( ns )
   {
      ns->pixelFormat= pCreateSettings->pixelFormat;
      ns->width= pCreateSettings->width;
      ns->height= pCreateSettings->height;
      switch( ns->pixelFormat )
      {
         default:
         case NEXUS_PixelFormat_eUnknown:
         case NEXUS_PixelFormat_eA8_R8_G8_B8:
         case NEXUS_PixelFormat_eX8_R8_G8_B8:
         case NEXUS_PixelFormat_eB8_G8_R8_A8:
         case NEXUS_PixelFormat_eB8_G8_R8_X8:
            ns->mem.pitch= ns->width*4;
            break;
         case NEXUS_PixelFormat_eR5_G6_B5:
         case NEXUS_PixelFormat_eA4_R4_G4_B4:
            ns->mem.pitch= ns->width*2;
            break;
      }
      ns->mem.buffer= calloc( 1, ns->mem.pitch*ns->height );
   }

   return (NEXUS_SurfaceHandle)ns;
}

NEXUS_Error NEXUS_Surface_GetMemory(
    NEXUS_SurfaceHandle surface,
    NEXUS_SurfaceMemory *pMemory
    )
{
   NEXUS_Error rc;
   EMSurface *ns;

   if ( !surface || ! pMemory )
   {
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   ns= (EMSurface*)surface;
   *pMemory= ns->mem;

   rc= NEXUS_SUCCESS;

exit:
   return rc;
}

void NEXUS_Surface_GetStatus(
    NEXUS_SurfaceHandle surface,
    NEXUS_SurfaceStatus *pStatus
    )
{
   TRACE1("NEXUS_Surface_GetStatus");
}

void NEXUS_Surface_Flush(
    NEXUS_SurfaceHandle surface
    )
{
   TRACE1("NEXUS_Surface_Flush");
}

void NEXUS_Surface_Destroy(
    NEXUS_SurfaceHandle surface
    )
{
   EMSurface *ns;

   TRACE1("NEXUS_Surface_Destroy");

   ns= (EMSurface*)surface;
   if ( ns->mem.buffer )
   {
      free( ns->mem.buffer );
      ns->mem.buffer= 0;
   }
   free( ns );
}

// Section: nexus_surface_client ------------------------------------------------------

NEXUS_Error NEXUS_SurfaceClient_GetStatus(
    NEXUS_SurfaceClientHandle handle,
    NEXUS_SurfaceClientStatus *pStatus
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMCTX *ctx= 0;

   TRACE1("NEXUS_SurfaceClient_GetStatus");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("NEXUS_SurfaceClient_GetStatus: emGetContext failed");
      goto exit;
   }

   pStatus->display.framebuffer.width= ctx->displayWidth;
   pStatus->display.framebuffer.height= ctx->displayHeight;

exit:
   return rc;
}

NEXUS_SurfaceClientHandle NEXUS_SurfaceClient_Acquire(
    NEXUS_SurfaceCompositorClientId client_id
    )
{
   struct NEXUS_SurfaceClient *sc= 0;
   EMSurfaceClient *emsc= 0;
   EMCTX *ctx= 0;

   TRACE1("NEXUS_SurfaceClient_Acquire");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("NEXUS_SurfaceClient_Acquire: emGetContext failed");
      goto exit;
   }

   emsc= (EMSurfaceClient*)calloc( 1, sizeof(EMSurfaceClient));
   if ( !emsc )
   {
      ERROR("NEXUS_SurfaceClient_Acquire: no memory for EMSurfaceClient");
      goto exit;
   }
   emsc->client_id= client_id;

   ctx->surfaceClients.insert( std::pair<unsigned,EMSurfaceClient*>( client_id, emsc ) );

   sc= (struct NEXUS_SurfaceClient*)emsc;

exit:
   return (NEXUS_SurfaceClientHandle)sc;
}

void NEXUS_SurfaceClient_Release(
    NEXUS_SurfaceClientHandle client
    )
{
   EMSurfaceClient *emsc= (EMSurfaceClient*)client;
   EMCTX *ctx= 0;

   TRACE1("NEXUS_SurfaceClient_Release");

   if ( emsc )
   {
      ctx= emGetContext();
      if ( !ctx )
      {
         ERROR("NEXUS_SurfaceClient_Release: emGetContext failed");
      }
      else
      {
         std::map<unsigned,EMSurfaceClient*>::iterator it= ctx->surfaceClients.find( emsc->client_id );
         if ( it != ctx->surfaceClients.end() )
         {
            ctx->surfaceClients.erase(it);
         }
      }

      free( emsc );
   }
}

void NEXUS_SurfaceClient_GetSettings(
    NEXUS_SurfaceClientHandle handle,
    NEXUS_SurfaceClientSettings *pSettings
    )
{
   EMSurfaceClient *emsc= (EMSurfaceClient*)handle;

   TRACE1("NEXUS_SurfaceClient_GetSettings");

   *pSettings= emsc->settings;
}

NEXUS_Error NEXUS_SurfaceClient_SetSettings(
    NEXUS_SurfaceClientHandle handle,
    const NEXUS_SurfaceClientSettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSurfaceClient *emsc= (EMSurfaceClient*)handle;

   TRACE1("NEXUS_SurfaceClient_SetSettings");

   if ( emsc->isVideoWindow )
   {
      // Emulate settings change on next vertical
      emsc->positionIsPending= true;
      emsc->pendingPosition= pSettings->composition.position;
      emsc->pendingPositionTime= getCurrentTimeMillis()+8;
   }
   emsc->settings= *pSettings;

   return rc;
}

NEXUS_Error NEXUS_SurfaceClient_PushSurface(
    NEXUS_SurfaceClientHandle handle,
    NEXUS_SurfaceHandle surface,
    const NEXUS_Rect *pUpdateRect,
    bool infront
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;

   TRACE1("NEXUS_SurfaceClient_PushSurface");

   return rc;
}

NEXUS_Error NEXUS_SurfaceClient_RecycleSurface(
    NEXUS_SurfaceClientHandle handle,
    NEXUS_SurfaceHandle *recycled,
    size_t num_entries,
    size_t *num_returned
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;

   TRACE1("NEXUS_SurfaceClient_RecycleSurface");

   return rc;
}

void NEXUS_SurfaceClient_Clear(
    NEXUS_SurfaceClientHandle handle
    )
{
   TRACE1("NEXUS_SurfaceClient_Clear");
}

NEXUS_SurfaceClientHandle NEXUS_SurfaceClient_AcquireVideoWindow(
    NEXUS_SurfaceClientHandle parent_handle,
    unsigned window_id
    )
{
   EMSurfaceClient *emsc= 0;
   EMCTX *ctx= 0;

   TRACE1("NEXUS_SurfaceClient_AcquireVideoWindow");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("NEXUS_SurfaceClient_AcquireVideoWindow: emGetContext failed");
      goto exit;
   }

   emsc= (EMSurfaceClient*)calloc( 1, sizeof(EMSurfaceClient));
   if ( !emsc )
   {
      ERROR("NEXUS_SurfaceClient_AcquireVideoWindow: no memory for EMSurfaceClient");
      goto exit;
   }
   emsc->isVideoWindow= true;
   emsc->client_id= window_id;

   ctx->videoWindowMain= emsc;

exit:

   return (NEXUS_SurfaceClientHandle)emsc;
}

void NEXUS_SurfaceClient_ReleaseVideoWindow(
    NEXUS_SurfaceClientHandle window_handle
    )
{
   EMSurfaceClient *emsc= 0;
   EMCTX *ctx= 0;

   TRACE1("NEXUS_SurfaceClient_ReleaseVideoWindow");

   emsc= (EMSurfaceClient*)window_handle;

   if ( !emsc->isVideoWindow )
   {
      ERROR("NEXUS_SurfaceClient_ReleaseVideoWindow: not a video window");
      goto exit;
   }

   free( emsc );

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("NEXUS_SurfaceClient_ReleaseVideoWindow: emGetContext failed");
      goto exit;
   }

   ctx->videoWindowMain= 0;

exit:
   return;
}

// Section: nexus_video_decoder ------------------------------------------------------

void NEXUS_GetVideoDecoderCapabilities(
    NEXUS_VideoDecoderCapabilities *pCapabilities
    )
{
   TRACE1("NEXUS_GetVideoDecoderCapabilities");
   memset( pCapabilities, 0, sizeof(pCapabilities));
   pCapabilities->memory[0].maxFormat= NEXUS_VideoFormat_e3840x2160p24hz;
}

// Section: nexus_simple_stc_channel ------------------------------------------------------

NEXUS_Error NEXUS_SimpleStcChannel_Freeze(
    NEXUS_SimpleStcChannelHandle handle,
    bool frozen
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;

   TRACE1("NEXUS_SimpleStcChannel_Freeze: stc %p frozen %d", handle, frozen);

   return rc;
}

NEXUS_Error NEXUS_SimpleStcChannel_Invalidate(
    NEXUS_SimpleStcChannelHandle handle
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;

   TRACE1("NEXUS_SimpleStcChannel_Invalidate: stc %p", handle);

   return rc;
}


// Section: nexus_simple_video_decoder ------------------------------------------------------

void NEXUS_SimpleVideoDecoder_GetDefaultStartSettings(
    NEXUS_SimpleVideoDecoderStartSettings *pSettings
    )
{
   TRACE1("NEXUS_SimpleVideoDecoder_GetDefauiltStartSettings");
   pSettings->settings.eotf= NEXUS_VideoEotf_eInvalid;
}

NEXUS_SimpleVideoDecoderHandle NEXUS_SimpleVideoDecoder_Acquire( 
    unsigned index
    )
{
   NEXUS_SimpleVideoDecoderHandle handle= 0;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_Acquire");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_Acquire: emGetContext failed");
      goto exit;
   }

   emctx->simpleVideoDecoderMain.inUse= true;

   handle= (NEXUS_SimpleVideoDecoderHandle)&emctx->simpleVideoDecoderMain;

exit:
   return handle;
}

void NEXUS_SimpleVideoDecoder_Release(
    NEXUS_SimpleVideoDecoderHandle handle
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_Release");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_Release: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_Release: bad decoder handle");
      goto exit;
   }

   dec->inUse= false;

exit:
   return;
}

void NEXUS_SimpleVideoDecoder_GetStreamInformation(
    NEXUS_SimpleVideoDecoderHandle handle,
    NEXUS_VideoDecoderStreamInformation *pStreamInfo
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_GetStreamInformation");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_GetStreamInformation: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_GetStreamInformation: bad decoder handle");
      goto exit;
   }

exit:
   return;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_GetClientStatus(
    NEXUS_SimpleVideoDecoderHandle handle,
    NEXUS_SimpleVideoDecoderClientStatus *pStatus
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_GetClientStatus");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_GetClientStatus: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_GetClientStatus: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   pStatus->enabled= dec->inUse;

   rc= NEXUS_SUCCESS;

exit:
   return rc;
}

void NEXUS_SimpleVideoDecoder_GetClientSettings(
    NEXUS_SimpleVideoDecoderHandle handle,
    NEXUS_SimpleVideoDecoderClientSettings *pSettings
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_GetClientSettings");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_GetClientSettings: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_GetClientSettings: bad decoder handle");
      goto exit;
   }

   *pSettings= dec->clientSettings;

exit:
   return;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_SetClientSettings(
    NEXUS_SimpleVideoDecoderHandle handle,
    const NEXUS_SimpleVideoDecoderClientSettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_SetClientSettings");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_SetClientSettings: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_SetClientSettings: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   dec->clientSettings= *pSettings;

   if ( dec->clientSettings.resourceChanged.callback )
   {
      dec->clientSettings.resourceChanged.callback( dec->clientSettings.resourceChanged.context, 0 );
   }

exit:
   return rc;
}

void NEXUS_SimpleVideoDecoder_GetSettings(
    NEXUS_SimpleVideoDecoderHandle handle,
    NEXUS_VideoDecoderSettings *pSettings
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_GetSettings");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_GetSettings: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_GetSettings: bad decoder handle");
      goto exit;
   }

   *pSettings= dec->decoderSettings;

exit:
   return;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_SetSettings(
    NEXUS_SimpleVideoDecoderHandle handle,
    const NEXUS_VideoDecoderSettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_SetSettings");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_SetSettings: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_SetSettings: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   dec->decoderSettings= *pSettings;

exit:
   return rc;
}

void NEXUS_SimpleVideoDecoder_GetExtendedSettings(
    NEXUS_SimpleVideoDecoderHandle handle,
    NEXUS_VideoDecoderExtendedSettings *pSettings
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_GetExtendedSettings");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_GetExtendedSettings: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_GetExtendedSettings: bad decoder handle");
      goto exit;
   }

exit:
   return;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_SetExtendedSettings(
    NEXUS_SimpleVideoDecoderHandle handle,
    const NEXUS_VideoDecoderExtendedSettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_SetExtendedSettings");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_SetExtendedSettings: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_SetExtendedSettings: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

exit:
   return rc;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_SetStartPts(
    NEXUS_SimpleVideoDecoderHandle handle,
    uint32_t pts
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_SetStartPts");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_SetStartPts: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_SetStartPts: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   dec->startPTS= pts;

exit:
   return rc;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_SetStcChannel(
    NEXUS_SimpleVideoDecoderHandle handle,
    NEXUS_SimpleStcChannelHandle stcChannel
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_SetStcChannel: decoder handle %p stc handle %p", handle, stcChannel);

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_SetStcChannel: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_SetStcChannel: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   dec->stcChannel= stcChannel;

exit:
   return rc;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_GetStatus(
    NEXUS_SimpleVideoDecoderHandle handle,
    NEXUS_VideoDecoderStatus *pStatus
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_GetStatus");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_GetStatus: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_GetStatus: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   pStatus->started= dec->started;
   pStatus->source.width= dec->videoWidth;
   pStatus->source.height= dec->videoHeight;
   pStatus->pts= ((dec->frameNumber/dec->videoFrameRate)*45000);
   pStatus->numDecoded= dec->frameNumber;
   pStatus->numDisplayed= dec->frameNumber;
   pStatus->firstPtsPassed= dec->firstPtsPassed;

exit:
   return rc;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_Start(
    NEXUS_SimpleVideoDecoderHandle handle,
    const NEXUS_SimpleVideoDecoderStartSettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_Start");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_Start: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_Start: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   dec->startSettings= *pSettings;

   dec->started= true;

exit:
   return rc;
}

void NEXUS_SimpleVideoDecoder_Stop(
    NEXUS_SimpleVideoDecoderHandle handle
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_Stop");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_Stop: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_Stop: bad decoder handle");
      goto exit;
   }

   dec->started= false;

exit:
   return;
}

void NEXUS_SimpleVideoDecoder_Flush(
    NEXUS_SimpleVideoDecoderHandle handle
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_Flush");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_Flush: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_Flush: bad decoder handle");
      goto exit;
   }

exit:
   return;
}

void NEXUS_SimpleVideoDecoder_GetTrickState(
    NEXUS_SimpleVideoDecoderHandle handle,
    NEXUS_VideoDecoderTrickState *pSettings
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_GetTrickState");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_GetTrickState: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_GetTrickState: bad decoder handle");
      goto exit;
   }

   *pSettings= dec->trickState;

exit:
   return;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_SetTrickState(
    NEXUS_SimpleVideoDecoderHandle handle,
    const NEXUS_VideoDecoderTrickState *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_SetTrickState");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_SetTrickState: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_SetTrickState: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   dec->trickState= *pSettings;

exit:
   return rc;
}

void NEXUS_SimpleVideoDecoder_GetDefaultStartCaptureSettings(
    NEXUS_SimpleVideoDecoderStartCaptureSettings *pSettings
    )
{
   TRACE1("NEXUS_SimpleVideoDecoder_GetDefaultStartCaptureSettings");
}

NEXUS_Error NEXUS_SimpleVideoDecoder_StartCapture(
    NEXUS_SimpleVideoDecoderHandle handle,
    const NEXUS_SimpleVideoDecoderStartCaptureSettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_StartCapture");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_StartCapture: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_StartCapture: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   for( int i= 0; i < NEXUS_SIMPLE_DECODER_MAX_SURFACES; ++i )
   {
      if ( pSettings->surface[i] == NULL )
      {
         EMERROR("NEXUS_SimpleVideoDecoder_StartCapture: bad surface handle");
         rc= NEXUS_INVALID_PARAMETER;
         goto exit;
      }
   }

   pthread_mutex_lock( &gMutex );
   dec->captureSettings= *pSettings;
   dec->captureSurfaceGetNext= 0;
   dec->captureSurfacePutNext= 0;
   dec->captureSurfaceCount= NEXUS_SIMPLE_DECODER_MAX_SURFACES;
   dec->captureStarted= true;
   pthread_mutex_unlock( &gMutex );

exit:
   return rc;
}

void NEXUS_SimpleVideoDecoder_StopCapture(
    NEXUS_SimpleVideoDecoderHandle handle
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_StopCapture");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_StopCapture: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_StopCapture: bad decoder handle");
      goto exit;
   }

   pthread_mutex_lock( &gMutex );
   dec->captureStarted= false;
   pthread_mutex_unlock( &gMutex );

exit:
   return;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_GetCapturedSurfaces(
    NEXUS_SimpleVideoDecoderHandle handle,
    NEXUS_SurfaceHandle *pSurface,
    NEXUS_VideoDecoderFrameStatus *pStatus,
    unsigned numEntries,
    unsigned *pNumReturned
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_GetCapturedSurfaces");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_GetCapturedSurfaces: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_GetCapturedSurfaces: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   if ( numEntries < 1 )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_GetCapturedSurfaces: bad numEntries");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

   pthread_mutex_lock( &gMutex );
   *pNumReturned= 0;
   if ( !dec->captureStarted )
   {
      rc= NEXUS_NOT_AVAILABLE;
      pthread_mutex_unlock( &gMutex );
      goto exit;
   }
   if ( dec->captureSurfaceCount > 0 )
   {
      --dec->captureSurfaceCount;
      *pSurface= dec->captureSettings.surface[dec->captureSurfaceGetNext];
      dec->captureSurfaceGetNext += 1;
      if ( dec->captureSurfaceGetNext >= NEXUS_SIMPLE_DECODER_MAX_SURFACES )
      {
         dec->captureSurfaceGetNext= 0;;
      }
      *pNumReturned= 1;
   }
   pthread_mutex_unlock( &gMutex );

   if ( *pNumReturned == 1 )
   {
      usleep(9000);
   }

exit:
   return rc;
}

void NEXUS_SimpleVideoDecoder_RecycleCapturedSurfaces(
    NEXUS_SimpleVideoDecoderHandle handle,
    const NEXUS_SurfaceHandle *pSurface,
    unsigned numEntries
    )
{
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_RecycleCapturedSurfaces");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_RecycleCapturedSurfaces: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_RecycleCapturedSurfaces: bad decoder handle");
      goto exit;
   }

   pthread_mutex_lock( &gMutex );
   for( int i= 0; i < numEntries; ++i )
   {
      if ( pSurface[i] && (dec->captureSurfaceCount < NEXUS_SIMPLE_DECODER_MAX_SURFACES) )
      {
         ++dec->captureSurfaceCount;
         dec->captureSettings.surface[dec->captureSurfacePutNext]= pSurface[i];
         dec->captureSurfacePutNext += 1;
         if ( dec->captureSurfacePutNext >= NEXUS_SIMPLE_DECODER_MAX_SURFACES )
         {
            dec->captureSurfacePutNext= 0;
         }
      }
   }
   pthread_mutex_unlock( &gMutex );

exit:
   return;
}

NEXUS_Error NEXUS_SimpleVideoDecoder_FrameAdvance(
    NEXUS_SimpleVideoDecoderHandle handle
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMSimpleVideoDecoder *dec= (EMSimpleVideoDecoder*)handle;
   EMCTX *emctx= 0;

   TRACE1("NEXUS_SimpleVideoDecoder_FrameAdvance");

   emctx= emGetContext();
   if ( !emctx )
   {
      ERROR("NEXUS_SimpleVideoDecoder_FrameAdvance: emGetContext failed");
      goto exit;
   }

   if ( !dec || (dec->magic != EM_SIMPLE_VIDEO_DECODER_MAGIC) )
   {
      EMERROR("NEXUS_SimpleVideoDecoder_FrameAdvance: bad decoder handle");
      rc= NEXUS_INVALID_PARAMETER;
      goto exit;
   }

exit:
   return rc;
}


// Section: nxclient ------------------------------------------------------

void NxClient_Uninit(void)
{
   TRACE1("NxClient_Uninit");
}

void NxClient_GetDefaultJoinSettings(
    NxClient_JoinSettings *pSettings
    )
{
   TRACE1("NxClient_GetDefaultJoinSettings");
}

NEXUS_Error NxClient_Join(
    const NxClient_JoinSettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;

   TRACE1("NxClient_Join");

   return rc;
}

void NxClient_GetDefaultAllocSettings(
    NxClient_AllocSettings *pSettings
    )
{
   TRACE1("NxClient_GetDefaultAllocSettings");
}

NEXUS_Error NxClient_Alloc(
    const NxClient_AllocSettings *pSettings,
    NxClient_AllocResults *pResults
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMCTX *ctx= 0;

   TRACE1("NxClient_Alloc");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("NxClient_Alloc: emGetContext failed");
      goto exit;
   }

   if ( pSettings->surfaceClient )
   {
      pResults->surfaceClient[0].id= ++ctx->nextNxClientId;
   }

   assert( ctx->nextNxClientId < EM_MAX_NXCLIENT );

exit:
   return rc;
}

void NxClient_Free(
    const NxClient_AllocResults *pResults
    )
{
   TRACE1("NxClient_Free");
}

void NxClient_GetDefaultConnectSettings(
    NxClient_ConnectSettings *pSettings
    )
{
   TRACE1("NxClient_GetDefaultConnectSettings");
}

NEXUS_Error NxClient_Connect(
    const NxClient_ConnectSettings *pSettings,
    unsigned *pConnectId
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMCTX *ctx= 0;

   TRACE1("NxClient_Connect");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("NxClient_Connect: emGetContext failed");
      goto exit;
   }

   *pConnectId= ++ctx->nextNxClientConnectId;

exit:
   return rc;
}

void NxClient_Disconnect(unsigned connectId)
{
   TRACE1("NxClient_Disconnect");
}

void NxClient_GetSurfaceClientComposition(
    unsigned surfaceClientId,
    NEXUS_SurfaceComposition *pSettings
    )
{
   EMCTX *ctx= 0;

   TRACE1("NxClient_GetSurfaceClientComposition");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("NxClient_GetSurfaceClientComposition: emGetContext failed");
      goto exit;
   }

   *pSettings= ctx->nxclients[surfaceClientId].composition;

exit:
   return;
}

NEXUS_Error NxClient_SetSurfaceClientComposition(
    unsigned surfaceClientId,
    const NEXUS_SurfaceComposition *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;
   EMCTX *ctx= 0;

   TRACE1("NxClient_SetSurfaceClientComposition");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("NxClient_SetSurfaceClientComposition: emGetContext failed");
      goto exit;
   }

   ctx->nxclients[surfaceClientId].composition= *pSettings;

exit:
   return rc;
}

void NxClient_GetDisplaySettings(
    NxClient_DisplaySettings *pSettings
    )
{
   TRACE1("NxClient_GetDisplaySettings");
}

NEXUS_Error NxClient_SetDisplaySettings(
    const NxClient_DisplaySettings *pSettings
    )
{
   NEXUS_Error rc= NEXUS_SUCCESS;

   TRACE1("NxClient_SetDisplaySettings");

   return rc;
}

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

   TRACE1("eglGetDisplay");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("eglGetDisplay: emGetContext failed");
      gEGLError= EGL_BAD_ACCESS;
      goto exit;
   }

   if ( (displayId == EGL_DEFAULT_DISPLAY) && (ctx->eglDisplayDefault != EGL_NO_DISPLAY) )
   {
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

   if ( (displayId == EGL_DEFAULT_DISPLAY) && (ctx->eglDisplayDefault == EGL_NO_DISPLAY) )
   {
      ctx->eglDisplayDefault= eglDisplay;
   }

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
         s= "EGL_WL_bind_wayland_display";
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
   EMNativeWindow *nw= (EMNativeWindow*)native_window;
   EMEGLSurface *surf= 0;
   struct wl_egl_window *egl_window= 0;

   TRACE1("eglCreateWindowSurface" );

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

   if ( egl_window )
   {
      nw= (EMNativeWindow*)wlGetNativeWindow( egl_window );
   }
   printf("wayland-egl: eglCreateWindowSurface: nativeWindow=%p\n", nw );

   if ( nw->magic != EM_WINDOW_MAGIC )
   {
      gEGLError= EGL_BAD_NATIVE_WINDOW;
      goto exit;
   }

   surf= (EMEGLSurface*)calloc( 1, sizeof(EMEGLSurface) );
   if ( !surf )
   {
      ERROR("eglCreateWindowSurface: no memory");
      gEGLError= EGL_BAD_ALLOC;
      goto exit;
   }

   surf->magic= EM_EGL_SURFACE_MAGIC;
   surf->egl_window= egl_window;

   surface= (EGLSurface)surf;

exit:
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

   TRACE1("eglSwapBuffers");

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
      wlSwapBuffers( egl_window );
   }
   else
   {
      // Emulated 'normal' render to screen.  Nothing to do.
   }

   result= EGL_TRUE;

exit:
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

#define WL_EXPORT

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

static void bnxsFormat(void *data, struct wl_bnxs *bnxs, uint32_t format)
{
   WAYEGL_UNUSED(data);
   WAYEGL_UNUSED(bnxs);
   printf("wayland-egl: registry: bnxsFormat: %X\n", format);
}

struct wl_bnxs_listener bnxsListener = {
	bnxsFormat
};

static void winRegistryHandleGlobal(void *data,
                                    struct wl_registry *registry, uint32_t id,
		                              const char *interface, uint32_t version)
{
   struct wl_egl_window *egl_window= (struct wl_egl_window*)data;
   printf("wayland-egl: registry: id %d interface (%s) version %d\n", id, interface, version );
   
   int len= strlen(interface);
   if ( (len==7) && !strncmp(interface, "wl_bnxs", len) ) {
      egl_window->bnxs= (struct wl_bnxs*)wl_registry_bind(registry, id, &wl_bnxs_interface, 1);
      printf("wayland-egl: registry: bnxs %p\n", (void*)egl_window->bnxs);
      wl_proxy_set_queue((struct wl_proxy*)egl_window->bnxs, egl_window->queue);
		wl_bnxs_add_listener(egl_window->bnxs, &bnxsListener, egl_window);
		printf("wayland-egl: registry: done add bnxs listener\n");
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

static void bnxsIBufferDestroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

const static struct wl_buffer_interface bufferInterface = {
   bnxsIBufferDestroy
};


static void bnxsDestroyBuffer(struct wl_resource *resource)
{
   struct wl_bnxs_buffer *buffer = (struct wl_bnxs_buffer*)resource->data;

   free(buffer);
}

static void bnxsICreateBuffer(struct wl_client *client, struct wl_resource *resource,
                              uint32_t id, uint32_t nexus_surface_handle, int32_t width, int32_t height,
                              uint32_t stride, uint32_t format)
{
   struct wl_bnxs_buffer *buff;
   
   switch (format) 
   {
      case WL_BNXS_FORMAT_ARGB8888:
         break;
      default:
         wl_resource_post_error(resource, WL_BNXS_ERROR_INVALID_FORMAT, "invalid format");
         return;
   }

   buff= (wl_bnxs_buffer*)calloc(1, sizeof *buff);
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
   buff->stride= stride;
   buff->nexusSurfaceHandle= nexus_surface_handle;

   wl_resource_set_implementation(buff->resource,
                                 (void (**)(void)) &bufferInterface,
                                 buff, bnxsDestroyBuffer);

}

static struct wl_bnxs_interface bnxs_interface = 
{
   bnxsICreateBuffer
};

static void bind_bnxs(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource;
   EMCTX *ctx= (EMCTX*)data;
   struct wl_display *display;
   struct wl_bnxs *bnxs= 0;

	printf("wayland-egl: bind_bnxs: enter: client %p data %p version %d id %d\n", (void*)client, data, version, id);

   resource= wl_resource_create(client, &wl_bnxs_interface, MIN(version, 1), id);
   if (!resource) 
   {
      wl_client_post_no_memory(client);
      return;
   }

   display= wl_client_get_display( client );

   std::map<struct wl_display*,EMWLBinding>::iterator it= ctx->wlBindings.find( display );
   if ( it != ctx->wlBindings.end() )
   {
      bnxs= it->second.bnxs;
   }

   if ( !bnxs )
   {
      printf("wayland-egl: bind_bnxs: no valid EGL for compositor\n" );
      wl_client_post_no_memory(client);
      return;
   }

   wl_resource_set_implementation(resource, &bnxs_interface, data, NULL);

   wl_resource_post_event(resource, WL_BNXS_FORMAT, WL_BNXS_FORMAT_ARGB8888);      
	
	printf("wayland-egl: bind_bnxs: exit: client %p id %d\n", (void*)client, id);
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
         if ( binding.bnxs )
         {
            result= EGL_TRUE;
         }
      }
      else
      {
         memset( &binding, 0, sizeof(binding) );
         binding.wlBoundDpy= dpy;
         binding.display= display;
         binding.bnxs= (wl_bnxs*)wl_global_create( display,
                                           &wl_bnxs_interface,
                                           1,
                                           ctx,
                                           bind_bnxs );
         if ( binding.bnxs )
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
         if ( binding.bnxs )
         {
            wl_global_destroy( (wl_global*)binding.bnxs );
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
   struct wl_bnxs_buffer *bnxsBuffer;
   int bnxsFormat;

   TRACE1("eglQueryWaylandBufferWL");
   
   if( wl_resource_instance_of( resource, &wl_buffer_interface, &bufferInterface ) ) 
   {
      bnxsBuffer= (wl_bnxs_buffer *)wl_resource_get_user_data( (wl_resource*)resource );
      if ( bnxsBuffer )
      {
         result= EGL_TRUE;
         switch( attribute )
         {
            case EGL_WIDTH:
               *value= bnxsBuffer->width;
               break;
            case EGL_HEIGHT:
               *value= bnxsBuffer->height;
               break;
            case EGL_TEXTURE_FORMAT:
               bnxsFormat= bnxsBuffer->format;
               switch( bnxsFormat )
               {
                  case WL_BNXS_FORMAT_ARGB8888:
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
         {
            EMEGLImage *img= 0;
            img= (EMEGLImage*)calloc( 1, sizeof(EMEGLImage));
            if ( img )
            {
               img->magic= EM_EGL_IMAGE_MAGIC;
               img->clientBuffer= buffer;
               img->target= target;
            }
            image= (EGLImageKHR)img;
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
   //void *bufferCtx;
   int deviceBuffer;
} bufferInfo;

static void buffer_release( void *data, struct wl_buffer *buffer )
{
   bufferInfo *binfo= (bufferInfo*)data;

   --binfo->egl_window->activeBuffers;
   //if ( binfo->bufferCtx )
   //{
   //   // Signal buffer is available again
   //   NXPL_ReleaseBuffer( (void*)binfo->bufferCtx );
   //}
   
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
   if ( egl_window->bnxs && !egl_window->windowDestroyPending )
   {
      int buffer;
      struct wl_buffer *wlBuff= 0;
      bufferInfo *binfo= 0;

      egl_window->eglSwapCount += 1;

      egl_window->bufferId += 1;
      if ( egl_window->bufferId >= egl_window->bufferIdBase+egl_window->bufferIdCount )
      {
         egl_window->bufferId= egl_window->bufferIdBase;
      }
      buffer= egl_window->bufferId;

      wl_proxy_set_queue((struct wl_proxy*)egl_window->bnxs, egl_window->queue);
      wlBuff= wl_bnxs_create_buffer( egl_window->bnxs, 
                                    (uint32_t)buffer,
                                    egl_window->width, 
                                    egl_window->height, 
                                    egl_window->width*4,
                                    WL_BNXS_FORMAT_ARGB8888 );
      if ( wlBuff )
      {
         binfo= (bufferInfo*)malloc( sizeof(bufferInfo) );
         if ( binfo )
         {
            binfo->egl_window= egl_window;
            //binfo->bufferCtx= bufferCtx;
            binfo->deviceBuffer= buffer;

            ++egl_window->activeBuffers;
            wl_buffer_add_listener( wlBuff, &wl_buffer_listener, binfo );

            egl_window->attachedWidth= egl_window->width;
            egl_window->attachedHeight= egl_window->height;
            wl_surface_attach( egl_window->surface, wlBuff, egl_window->dx, egl_window->dy );
            wl_surface_damage( egl_window->surface, 0, 0, egl_window->width, egl_window->height);
            wl_surface_commit( egl_window->surface );   

            wl_display_flush( egl_window->ctx->display );
            
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
            //NXPL_ReleaseBuffer( bufferCtx );
            wl_buffer_destroy( wlBuff );
         }
      }
      
      wl_display_dispatch_queue_pending( egl_window->ctx->display, egl_window->queue );
   }
}

extern "C" {

struct wl_egl_window *wl_egl_window_create(struct wl_surface *surface, int width, int height)
{
   struct wl_egl_window *egl_window= 0;
   NXPL_NativeWindowInfo windowInfo;
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
         ctx->display= wldisp;

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
        
         egl_window->queue= wl_display_create_queue(egl_window->ctx->display);
         if ( !egl_window->queue )
         {
            printf("wayland-egl: wl_egl_window_create: unable to create event queue\n");
            free( egl_window );
            egl_window= 0;
            goto exit;
         }

         egl_window->registry= wl_display_get_registry( egl_window->ctx->display );
         if ( !egl_window->registry )
         {
            printf("wayland-egl: wl_egl_window_create: unable to get display registry\n");
            free( egl_window );
            egl_window= 0;
            goto exit;
         }
         wl_proxy_set_queue((struct wl_proxy*)egl_window->registry, egl_window->queue);
         wl_registry_add_listener(egl_window->registry, &winRegistryListener, egl_window);
         wl_display_roundtrip_queue(egl_window->ctx->display, egl_window->queue);
         
         if ( !egl_window->bnxs )
         {
            printf("wayland-egl: wl_egl_window_create: no wl_bnxs protocol available\n");
            wl_egl_window_destroy( egl_window );
            egl_window= 0;
            goto exit;
         }

         memset( &windowInfo, 0, sizeof(windowInfo) );
         windowInfo.x= 0;
         windowInfo.y= 0;
         windowInfo.width= width;
         windowInfo.height= height;
         windowInfo.stretch= false;
         windowInfo.clientID= 0;
         windowInfo.zOrder= 10000000;
      
         egl_window->nativeWindow= (EGLNativeWindowType)NXPL_CreateNativeWindow( &windowInfo );
         if ( egl_window->nativeWindow )
         {
            printf("wayland-egl: wl_egl_window_create: egl_window %p nativeWindow %p\n", (void*)egl_window, (void*)egl_window->nativeWindow );
         
            //windowListener.referenceBuffer= bnxsReferenceBuffer;
            //windowListener.newSingleBuffer= bnxsNewSingleBuffer;
            //windowListener.dispatchPending= bnxsDispatchPending;

            //NXPL_AttachNativeWindow( egl_window->nativeWindow, egl_window, &windowListener );
         }
                 
         pthread_mutex_lock( &gMutex );
         gNativeWindows.push_back( egl_window );
         pthread_mutex_unlock( &gMutex );
      }
   }

exit:   

   return egl_window;
}

void wl_egl_window_destroy(struct wl_egl_window *egl_window)
{
   TRACE1("wl_egl_window_destroy");

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

         if ( egl_window->nativeWindow )
         {
            NXPL_DestroyNativeWindow( egl_window->nativeWindow );
            egl_window->nativeWindow= 0;
         }
         
         if ( egl_window->bnxs )
         {
            wl_proxy_set_queue((struct wl_proxy*)egl_window->bnxs, 0);
            wl_bnxs_destroy( egl_window->bnxs );
            egl_window->bnxs= 0;
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

   if ( egl_window->nativeWindow )
   {
      egl_window->dx += dx;
      egl_window->dy += dy;
      egl_window->width= width;
      egl_window->height= height;
      NXPL_ResizeNativeWindow( egl_window->nativeWindow, width, height, dx, dy );
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

void *wl_egl_get_device_buffer(struct wl_resource *resource)
{
   void *deviceBuffer= 0;

   if( wl_resource_instance_of( resource, &wl_buffer_interface, &bufferInterface ) ) 
   {
      struct wl_bnxs_buffer *bnxsBuffer;
      
      bnxsBuffer= (wl_bnxs_buffer *)wl_resource_get_user_data( (wl_resource*)resource );
      if ( bnxsBuffer )
      {
         deviceBuffer= (void *)bnxsBuffer->nexusSurfaceHandle;
      }
   }
   
   return deviceBuffer;
}

struct wl_egl_window *getWlEglWindow( struct wl_display *display )
{
   struct wl_egl_window *egl_window= 0;

   pthread_mutex_lock( &gMutex );
   for( std::vector<struct wl_egl_window*>::iterator it= gNativeWindows.begin();
        it != gNativeWindows.end();
        ++it )
   {
      if ( (*it)->wldisp == display )
      {
         egl_window= (*it);
         break;
      }
   }
   pthread_mutex_unlock( &gMutex );

   return egl_window;
}

static void wayRegistryHandleGlobal(void *data,
                                    struct wl_registry *registry, uint32_t id,
		                              const char *interface, uint32_t version)
{
   EMWLRemote *r= (EMWLRemote*)data;

   int len= strlen(interface);
   if ( (len==7) && !strncmp(interface, "wl_bnxs", len) ) {
      r->bnxsRemote= (struct wl_bnxs*)wl_registry_bind(registry, id, &wl_bnxs_interface, 1);
      printf("wayland-egl: registry: bnxs %p\n", (void*)r->bnxsRemote);
      wl_proxy_set_queue((struct wl_proxy*)r->bnxsRemote, 0);
   }
}

static void wayRegistryHandleGlobalRemove(void *data,
                                          struct wl_registry *registry,
			                                 uint32_t name)
{
   WAYEGL_UNUSED(data);
   WAYEGL_UNUSED(registry);
   WAYEGL_UNUSED(name);
}

static const struct wl_registry_listener wayRegistryListener =
{
	wayRegistryHandleGlobal,
	wayRegistryHandleGlobalRemove
};

extern "C" {

bool wl_egl_remote_begin( struct wl_display *dspsrc, struct wl_display *dspdest )
{
   bool result= false;
   EMCTX *ctx= 0;
   EMWLRemote remote;

   TRACE1("wl_egl_remote_begin");
   ctx= emGetContext();
   if ( ctx )
   {
      std::map<struct wl_display*,EMWLRemote>::iterator it= ctx->wlRemotes.find( dspsrc );
      if ( it == ctx->wlRemotes.end() )
      {
         EMWLRemote *r= &remote;
         memset( &remote, 0, sizeof(remote) );
         r->dspsrc= dspsrc;
         r->dspdest= dspdest;
         r->registry= wl_display_get_registry( dspdest );
         if ( r->registry )
         {
            r->queue= wl_display_create_queue(dspdest);
            if ( r->queue )
            {
               wl_proxy_set_queue((struct wl_proxy*)r->registry, r->queue);
               wl_registry_add_listener(r->registry, &wayRegistryListener, r);
               wl_display_roundtrip_queue(dspdest, r->queue);

               if ( r->bnxsRemote )
               {
                  ctx->wlRemotes.insert( std::pair<struct wl_display*,EMWLRemote>( dspsrc, remote ) );
                  result= true;
               }
            }
         }
      }
   }

   return result;
}

void wl_egl_remote_end( struct wl_display *dspsrc, struct wl_display *dspdest )
{
   EMCTX *ctx= 0;

   TRACE1("wl_egl_remote_end");
   ctx= emGetContext();
   if ( ctx )
   {
      std::map<struct wl_display*,EMWLRemote>::iterator it= ctx->wlRemotes.find( dspsrc );
      if ( it != ctx->wlRemotes.end() )
      {
         EMWLRemote *r= &it->second;

         if ( r->bnxsRemote )
         {
            wl_bnxs_destroy( r->bnxsRemote );
            r->bnxsRemote= 0;
         }
         if ( r->registry )
         {
            wl_registry_destroy(r->registry);
            r->registry= 0;
         }
         if ( r->queue )
         {
            wl_event_queue_destroy( r->queue );
            r->queue= 0;
         }
         ctx->wlRemotes.erase( it );
      }
   }
}

struct wl_buffer* wl_egl_remote_buffer_clone( struct wl_display *dspsrc,
                                              struct wl_resource *resource,
                                              struct wl_display *dspdest,
                                              int *width,
                                              int *height )
{
   EMCTX *ctx= 0;
   struct wl_buffer *clone= 0;

   TRACE1("wl_egl_remote_buffer_clone");
   if( wl_resource_instance_of( resource, &wl_buffer_interface, &bufferInterface ) )
   {
      struct wl_bnxs_buffer *bnxsBuffer;

      bnxsBuffer= (wl_bnxs_buffer *)wl_resource_get_user_data( (wl_resource*)resource );
      if ( bnxsBuffer )
      {
         ctx= emGetContext();
         if ( ctx )
         {
            std::map<struct wl_display*,EMWLRemote>::iterator it= ctx->wlRemotes.find( dspsrc );
            if ( it != ctx->wlRemotes.end() )
            {
               EMWLRemote *r= &it->second;

               if ( r->bnxsRemote )
               {
                  clone= wl_bnxs_create_buffer( r->bnxsRemote,
                                                bnxsBuffer->nexusSurfaceHandle,
                                                bnxsBuffer->width,
                                                bnxsBuffer->height,
                                                bnxsBuffer->stride,
                                                bnxsBuffer->format );
                  if ( clone )
                  {
                     if ( width ) *width= bnxsBuffer->width;
                     if ( height ) *height= bnxsBuffer->height;
                  }
               }
            }
         }
      }
   }

   return clone;
}

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
   TRACE1("glClear");
}

GL_APICALL void GL_APIENTRY glClearColor (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
   TRACE1("glClearColor");
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
         {
            EMEGLImage* img= (EMEGLImage*)image;
            if ( img )
            {
               if ( img->magic == EM_EGL_IMAGE_MAGIC )
               {
                  int bufferId=-1;

                  if ( img->target == EGL_WAYLAND_BUFFER_WL )
                  {
                     struct wl_resource *resource= (struct wl_resource*)img->clientBuffer;
                     void *deviceBuffer= wl_egl_get_device_buffer( resource );
                     if( (long long)deviceBuffer < 0 )
                     {
                        ERROR("glEGLImageTargetTexture2DOES: image %p had bad device buffer %d", img, deviceBuffer);
                     }
                     else
                     {
                        bufferId= (((long long)deviceBuffer)&0xFFFFFFFF);
                     }
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
         s= "";
         break;
   }

exit:

   TRACE1("glGetString for %X: (%s)", name, s );

   return s;
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

   TRACE1("glScissor");

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
   TRACE1("glTexImage2D");
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
      ERROR("glViewport: emGetContext failed");
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

   TRACE1("glViewport");

   ctx= emGetContext();
   if ( !ctx )
   {
      ERROR("glViewport: emGetContext failed");
      goto exit;
   }

   ctx->viewport[0]= x;
   ctx->viewport[1]= y;
   ctx->viewport[2]= width;
   ctx->viewport[3]= height;

exit:
   return;
}


