/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2017 RDK Management
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

#include "essos.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <linux/input.h>
#include <dlfcn.h>
#include <poll.h>
#include <pthread.h>
#include <sys/time.h>

#include <vector>

#ifdef HAVE_WAYLAND
#include <xkbcommon/xkbcommon.h>
#include <sys/mman.h>
#include "wayland-client.h"
#include "wayland-egl.h"
#endif

#ifdef HAVE_WESTEROS
#include "westeros-gl.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "simpleshell-client-protocol.h"
#endif

#include "westeros-version.h"

#define ESS_UNUSED(x) ((void)x)
#define ESS_MAX_ERROR_DETAIL (512)
#define DEFAULT_PLANE_WIDTH (1280)
#define DEFAULT_PLANE_HEIGHT (720)
#define DEFAULT_PLANE_SAFE_BORDER_PERCENT (5)
#define DEFAULT_KEY_REPEAT_DELAY (500)
#define DEFAULT_KEY_REPEAT_PERIOD (100)

#define INT_FATAL(FORMAT, ...)      printf("Essos Fatal: " FORMAT "\n", ##__VA_ARGS__)
#define INT_ERROR(FORMAT, ...)      printf("Essos Error: " FORMAT "\n", ##__VA_ARGS__)
#define INT_WARNING(FORMAT, ...)    printf("Essos Warning: " FORMAT "\n",  ##__VA_ARGS__)
#define INT_INFO(FORMAT, ...)       printf("Essos Info: " FORMAT "\n",  ##__VA_ARGS__)
#define INT_DEBUG(FORMAT, ...)
#define INT_TRACE(FORMAT, ...)

#define FATAL(FORMAT, ...)          INT_FATAL(FORMAT, ##__VA_ARGS__)
#define ERROR(FORMAT, ...)          INT_ERROR(FORMAT, ##__VA_ARGS__)
#define WARNING(FORMAT, ...)        INT_WARNING(FORMAT, ##__VA_ARGS__)
#define INFO(FORMAT, ...)           INT_INFO(FORMAT, ##__VA_ARGS__)
#define DEBUG(FORMAT, ...)          INT_DEBUG(FORMAT, ##__VA_ARGS__)
#define TRACE(FORMAT, ...)          INT_TRACE(FORMAT, ##__VA_ARGS__)

#define ESS_INPUT_POLL_LIMIT (10)
#define ESS_MAX_TOUCH (10)

typedef struct _EssTouchInfo
{
   int id;
   int x;
   int y;
   bool valid;
   bool starting;
   bool stopping;
   bool moved;
} EssTouchInfo;

typedef struct _EssCtx
{
   pthread_mutex_t mutex;
   bool autoMode;
   bool isWayland;
   bool isInitialized;
   bool isRunning;
   bool isExternalEGL;
   char lastErrorDetail[ESS_MAX_ERROR_DETAIL];

   bool haveMode;
   int planeWidth;
   int planeHeight;
   int planeSafeX;
   int planeSafeY;
   int planeSafeW;
   int planeSafeH;
   int windowX;
   int windowY;
   int windowWidth;
   int windowHeight;
   bool pendingGeometryChange;
   bool fullScreen;
   const char *appName;
   uint32_t appSurfaceId;
   int waylandFd;
   pollfd wlPollFd;
   int notifyFd;
   int watchFd;
   std::vector<pollfd> inputDeviceFds;
   int eventLoopPeriodMS;
   long long eventLoopLastTimeStamp;

   int pointerX;
   int pointerY;

   long long lastKeyTime;
   int lastKeyCode;
   bool keyPressed;
   bool keyRepeating;
   int keyRepeatInitialDelay;
   int keyRepeatPeriod;

   EssTouchInfo touch[ESS_MAX_TOUCH];

   void *keyListenerUserData;
   EssKeyListener *keyListener;
   void *pointerListenerUserData;
   EssPointerListener *pointerListener;
   void *touchListenerUserData;
   EssTouchListener *touchListener;
   void *settingsListenerUserData;
   EssSettingsListener *settingsListener;
   void *terminateListenerUserData;
   EssTerminateListener *terminateListener;

   NativeDisplayType displayType;
   NativeWindowType nativeWindow;
   EGLDisplay eglDisplay;
   EGLint eglVersionMajor;
   EGLint eglVersionMinor;
   EGLint *eglCfgAttr;
   EGLint eglCfgAttrSize;
   EGLConfig eglConfig;
   EGLint *eglCtxAttr;
   EGLint eglCtxAttrSize;
   EGLint *eglSurfAttr;
   EGLint eglSurfAttrSize;
   EGLContext eglContext;
   EGLSurface eglSurfaceWindow;
   EGLint eglSwapInterval;

   bool resizePending;
   int resizeWidth;
   int resizeHeight;

   #ifdef HAVE_WAYLAND
   struct wl_display *wldisplay;
   struct wl_registry *wlregistry;
   struct wl_compositor *wlcompositor;
   struct wl_seat *wlseat;
   struct wl_output *wloutput;
   struct wl_keyboard *wlkeyboard;
   struct wl_pointer *wlpointer;
   struct wl_touch *wltouch;
   struct wl_surface *wlsurface;
   struct wl_simple_shell *shell;
   struct wl_egl_window *wleglwindow;

   struct xkb_context *xkbCtx;
   struct xkb_keymap *xkbKeymap;
   struct xkb_state *xkbState;
   xkb_mod_index_t modAlt;
   xkb_mod_index_t modCtrl;
   xkb_mod_index_t modShift;
   xkb_mod_index_t modCaps;
   unsigned int modMask;
   #endif
   #ifdef HAVE_WESTEROS
   WstGLCtx *glCtx;
   #endif
} EssCtx;

static long long essGetCurrentTimeMillis(void);
static bool essPlatformInit( EssCtx *ctx );
static void essPlatformTerm( EssCtx *ctx );
static bool essEGLInit( EssCtx *ctx );
static void essEGLTerm( EssCtx *ctx );
static void essInitInput( EssCtx *ctx );
static void essSetDisplaySize( EssCtx *ctx, int width, int height, bool customSafe, int safeX, int safeY, int safeW, int safeH );
static bool essCreateNativeWindow( EssCtx *ctx, int width, int height );
static bool essDestroyNativeWindow( EssCtx *ctx, NativeWindowType nw );
static bool essResize( EssCtx *ctx, int width, int height );
static void essRunEventLoopOnce( EssCtx *ctx );
static void essProcessKeyPressed( EssCtx *ctx, int linuxKeyCode );
static void essProcessKeyReleased( EssCtx *ctx, int linuxKeyCode );
static void essProcessPointerMotion( EssCtx *ctx, int x, int y );
static void essProcessPointerButtonPressed( EssCtx *ctx, int button );
static void essProcessPointerButtonReleased( EssCtx *ctx, int button );
static void essProcessTouchDown( EssCtx *ctx, int id, int x, int y );
static void essProcessTouchUp( EssCtx *ctx, int id );
static void essProcessTouchMotion( EssCtx *ctx, int id, int x, int y );
static void essProcessTouchFrame( EssCtx *ctx );
#ifdef HAVE_WAYLAND
static bool essPlatformInitWayland( EssCtx *ctx );
static void essPlatformTermWayland( EssCtx *ctx );
static void essProcessRunWaylandEventLoopOnce( EssCtx *ctx );
#endif
#ifdef HAVE_WESTEROS
static bool essPlatformInitDirect( EssCtx *ctx );
static void essPlatformTermDirect( EssCtx *ctx );
static bool essPlatformSetDisplayModeDirect( EssCtx *ctx, const char *mode );
static int essOpenInputDevice( EssCtx *ctx, const char *devPathName );
static char *essGetInputDevice( EssCtx *ctx, const char *path, char *devName );
static void essGetInputDevices( EssCtx *ctx );
static void essMonitorInputDevicesLifecycleBegin( EssCtx *ctx );
static void essMonitorInputDevicesLifecycleEnd( EssCtx *ctx );
static void essReleaseInputDevices( EssCtx *ctx );
static void essProcessInputDevices( EssCtx *ctx );
#endif

static EGLint gDefaultEGLCfgAttrSize= 17;
static EGLint gDefaultEGLCfgAttr[]=
{
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_ALPHA_SIZE, 8,
  EGL_DEPTH_SIZE, 0,
  EGL_STENCIL_SIZE, 0,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

static EGLint gDefaultEGLSurfAttrSize= 1;
static EGLint gDefaultEGLSurfAttr[]=
{
  EGL_NONE
};

static EGLint gDefaultEGLCtxAttrSize= 3;
static EGLint gDefaultEGLCtxAttr[]=
{
  EGL_CONTEXT_CLIENT_VERSION, 2,
  EGL_NONE
};

EssCtx* EssContextCreate()
{
   EssCtx *ctx= 0;

   INFO("westeros (essos) version " WESTEROS_VERSION_FMT, WESTEROS_VERSION );

   ctx= (EssCtx*)calloc( 1, sizeof(EssCtx) );
   if ( ctx )
   {
      pthread_mutex_init( &ctx->mutex, 0 );

      essSetDisplaySize( ctx, DEFAULT_PLANE_WIDTH, DEFAULT_PLANE_HEIGHT, false, 0, 0, 0, 0);
      ctx->windowWidth= DEFAULT_PLANE_WIDTH;
      ctx->windowHeight= DEFAULT_PLANE_HEIGHT;
      ctx->autoMode= true;
      ctx->notifyFd= -1;
      ctx->watchFd= -1;
      ctx->waylandFd= -1;
      ctx->eventLoopPeriodMS= 16;
      if ( getenv("ESSOS_NO_EVENT_LOOP_THROTTLE") )
      {
         ctx->eventLoopPeriodMS= 0;
      }

      ctx->keyRepeatInitialDelay= DEFAULT_KEY_REPEAT_DELAY;
      ctx->keyRepeatPeriod= DEFAULT_KEY_REPEAT_PERIOD;

      ctx->displayType= EGL_DEFAULT_DISPLAY;
      ctx->eglDisplay= EGL_NO_DISPLAY;
      ctx->eglCfgAttr= gDefaultEGLCfgAttr;
      ctx->eglCfgAttrSize= gDefaultEGLCfgAttrSize;
      ctx->eglCtxAttr= gDefaultEGLCtxAttr;
      ctx->eglCtxAttrSize= gDefaultEGLCtxAttrSize;
      ctx->eglContext= EGL_NO_CONTEXT;
      ctx->eglSurfaceWindow= EGL_NO_SURFACE;
      ctx->eglSwapInterval= 1;

      ctx->inputDeviceFds= std::vector<pollfd>();
   }

   return ctx;
}

void EssContextDestroy( EssCtx *ctx )
{
   if ( ctx )
   {
      if ( ctx->isRunning )
      {
         EssContextStop( ctx );
      }

      if ( ctx->isInitialized )
      {
         essPlatformTerm( ctx );
      }

      if ( ctx->appName )
      {
         free( (char*)ctx->appName );
      }

      pthread_mutex_destroy( &ctx->mutex );
      
      free( ctx );
   }
}

const char *EssContextGetLastErrorDetail( EssCtx *ctx )
{
   const char *detail= 0;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      
      detail= ctx->lastErrorDetail;
      
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return detail;
}

bool EssContextSupportWayland( EssCtx *ctx )
{
   #ifdef HAVE_WAYLAND
   return true;
   #else
   return false;
   #endif
}

bool EssContextSupportDirect( EssCtx *ctx )
{
   #ifdef HAVE_WESTEROS
   // We use westeros-gl to hide SOC specifics of EGL
   return true;
   #else
   return false;
   #endif
}

bool EssContextSetUseWayland( EssCtx *ctx, bool useWayland )
{
   bool result= false;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->isInitialized )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Can't change application type when already initialized" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      #ifndef HAVE_WAYLAND
      if ( useWayland )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Wayland mode is not available" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      #endif

      #ifndef HAVE_WESTEROS
      if ( !useWayland )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Direct mode is not available" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      #endif

      ctx->autoMode= false;
      ctx->isWayland= useWayland;

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

exit:   

   return result;
}

bool EssContextGetUseWayland( EssCtx *ctx )
{
   bool result= false;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      result= ctx->isWayland;

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:   

   return result;      
}

bool EssContextSetUseDirect( EssCtx *ctx, bool useDirect )
{
   return EssContextSetUseWayland( ctx, !useDirect );
}

bool EssContextGetUseDirect( EssCtx *ctx )
{
   return !EssContextGetUseWayland( ctx );
}

bool EssContextSetName( EssCtx *ctx, const char *name )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->isInitialized )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Can't change application name when already initialized" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      if ( !name )
      {
         sprintf( ctx->lastErrorDetail,
                  "Invalid parameter: name is null" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      ctx->appName= strdup(name);;

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

exit:

   return result;
}

bool EssContextSetEGLConfigAttributes( EssCtx *ctx, EGLint *attrs, EGLint size )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->eglCfgAttr= (attrs ? attrs : gDefaultEGLCfgAttr);
      ctx->eglCfgAttrSize= (attrs ? size : gDefaultEGLCfgAttrSize);

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

   return result;
}

bool EssContextGetEGLConfigAttributes( EssCtx *ctx, EGLint **attrs, EGLint *size )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( attrs && size )
      {
         *attrs= ctx->eglCfgAttr;
         *size= ctx->eglCfgAttrSize;
         result= true;
      }

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}

bool EssContextSetEGLSurfaceAttributes( EssCtx *ctx, EGLint *attrs, EGLint size )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->eglSurfAttr= (attrs ? attrs : gDefaultEGLSurfAttr);
      ctx->eglSurfAttrSize= (attrs ? size : gDefaultEGLSurfAttrSize);

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

   return result;
}

bool EssContextGetEGLSurfaceAttributes( EssCtx *ctx, EGLint **attrs, EGLint *size )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( attrs && size )
      {
         *attrs= ctx->eglSurfAttr;
         *size= ctx->eglSurfAttrSize;
         result= true;
      }

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}

bool EssContextSetEGLContextAttributes( EssCtx *ctx, EGLint *attrs, EGLint size )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->eglCtxAttr= (attrs ? attrs : gDefaultEGLCtxAttr);
      ctx->eglCtxAttrSize= (attrs ? size : gDefaultEGLCtxAttrSize);

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

   return result;
}

bool EssContextGetEGLContextAttributes( EssCtx *ctx, EGLint **attrs, EGLint *size )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( attrs && size )
      {
         *attrs= ctx->eglCtxAttr;
         *size= ctx->eglCtxAttrSize;
         result= true;
      }

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}

bool EssContextSetDisplayMode( EssCtx *ctx, const char *mode )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !mode )
      {
         sprintf( ctx->lastErrorDetail,
                  "Invalid parameter: mode is null" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      if ( ctx->isWayland )
      {
         WARNING("EssContextSetDisplayMode ignored for Wayland");
         result= true;
      }
      else
      {
         result= essPlatformSetDisplayModeDirect( ctx, mode );
      }

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:

   return result;
}

bool EssContextSetInitialWindowSize( EssCtx *ctx, int width, int height )
{
   bool result= false;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      
      if ( ctx->isRunning )
      {
         pthread_mutex_unlock( &ctx->mutex );
         WARNING("EssContextSetInitialWindowSize: already running: calling resize (%d,%d)", width, height);
         result= EssContextResizeWindow( ctx, width, height );
         goto exit;
      }

      ctx->haveMode= true;
      ctx->windowWidth= width;
      ctx->windowHeight= height;

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

exit:   

   return result;
}

bool EssContextSetSwapInterval( EssCtx *ctx, EGLint swapInterval )
{
   bool result= false;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      
      if ( ctx->isRunning )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Context is already running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      ctx->eglSwapInterval= swapInterval;

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

exit:   

   return result;
}

bool EssContextInit( EssCtx *ctx )
{
   bool result= false;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      
      if ( ctx->isRunning )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Context is already running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      if ( ctx->autoMode )
      {
         ctx->isWayland= ((getenv("WAYLAND_DISPLAY") != 0) ? true : false );
         INFO("auto set mode: isWayland %d", ctx->isWayland);
      }

      pthread_mutex_unlock( &ctx->mutex );

      if ( !EssContextSetUseWayland( ctx, ctx->isWayland ) )
      {
         goto exit;
      }

      result= essPlatformInit(ctx);
      if ( result )
      {
         ctx->isInitialized= true;
      }
   }

exit:   

   return result;
}

bool EssContextGetEGLDisplayType( EssCtx *ctx, NativeDisplayType *displayType )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isInitialized )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Must initialize before getting display type" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      *displayType= ctx->displayType;

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

exit:
   return result;
}

bool EssContextCreateNativeWindow( EssCtx *ctx, int width, int height, NativeWindowType *nativeWindow )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isInitialized )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Must initialize before creating native window" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      result= essCreateNativeWindow( ctx, width, height );
      if ( result )
      {
         *nativeWindow= ctx->nativeWindow;

         /*
          * App is creating its EGL environment outside of Essos
          */
         if ( !ctx->isExternalEGL )
         {
            INFO("essos: app using external EGL");
            ctx->isExternalEGL= true;
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:
   return result;
}

bool EssContextDestroyNativeWindow( EssCtx *ctx, NativeWindowType nativeWindow )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isInitialized )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Must initialize context before calling" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      essDestroyNativeWindow( ctx, nativeWindow );

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:
   return result;
}

void* EssContextGetWaylandDisplay( EssCtx *ctx )
{
   void *wldisplay= 0;

   #ifdef HAVE_WAYLAND
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      wldisplay= (void*)ctx->wldisplay;   

      pthread_mutex_unlock( &ctx->mutex );
   }
   #endif

   return wldisplay;
}

bool EssContextSetKeyListener( EssCtx *ctx, void *userData, EssKeyListener *listener )
{
   bool result= false;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->keyListenerUserData= userData;
      ctx->keyListener= listener;

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}

bool EssContextSetPointerListener( EssCtx *ctx, void *userData, EssPointerListener *listener )
{
   bool result= false;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->pointerListenerUserData= userData;
      ctx->pointerListener= listener;

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}

bool EssContextSetTouchListener( EssCtx *ctx, void *userData, EssTouchListener *listener )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->touchListenerUserData= userData;
      ctx->touchListener= listener;

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}

bool EssContextSetSettingsListener( EssCtx *ctx, void *userData, EssSettingsListener *listener )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->settingsListenerUserData= userData;
      ctx->settingsListener= listener;

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}

bool EssContextSetTerminateListener( EssCtx *ctx, void *userData, EssTerminateListener *listener )
{
   bool result= false;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->terminateListenerUserData= userData;
      ctx->terminateListener= listener;

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}


bool EssContextSetKeyRepeatInitialDelay( EssCtx *ctx, int delay )
{
   bool result= false;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->keyRepeatInitialDelay= delay;

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}

bool EssContextSetKeyRepeatPeriod( EssCtx *ctx, int period )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->keyRepeatPeriod= period;

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

   return result;
}

bool EssContextStart( EssCtx *ctx )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      
      if ( ctx->isRunning )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Context is already running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      if ( !ctx->isInitialized )
      {
         pthread_mutex_unlock( &ctx->mutex );
         result= EssContextInit( ctx );
         if ( !result ) goto exit;
         pthread_mutex_lock( &ctx->mutex );
      }

      if ( !ctx->isExternalEGL )
      {
         result= essEGLInit( ctx );
         if ( !result )
         {
            pthread_mutex_unlock( &ctx->mutex );
            goto exit;
         }
      }

      essInitInput( ctx );

      ctx->isRunning= true;

      pthread_mutex_unlock( &ctx->mutex );

      essRunEventLoopOnce( ctx );

      result= true;
   }

exit:   

   return result;
}

void EssContextStop( EssCtx *ctx )
{
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->isRunning )
      {
         if  (!ctx->isWayland )
         {
            essMonitorInputDevicesLifecycleEnd( ctx );
            essReleaseInputDevices( ctx );
         }

         essEGLTerm( ctx );

         ctx->isRunning= false;
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

bool EssContextSetDisplaySize( EssCtx *ctx, int width, int height )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isInitialized )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Must initialize before setting display size" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      if ( ctx->isWayland )
      {
         // ignore
      }
      else
      {
         ctx->haveMode= true;
         essSetDisplaySize( ctx, width, height, false, 0, 0, 0, 0);
      }

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:

   return result;
}

bool EssContextGetDisplaySize( EssCtx *ctx, int *width, int *height )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isInitialized )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Must initialize before querying display size" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      if ( width )
      {
         *width= ctx->planeWidth;
      }
      if ( height )
      {
         *height= ctx->planeHeight;
      }

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:

   return result;
}

bool EssContextGetDisplaySafeArea( EssCtx *ctx, int *x, int *y, int *width, int *height )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isInitialized )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Must initialize before querying display safe area" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      if ( x )
      {
         *x= ctx->planeSafeX;
      }
      if ( y )
      {
         *y= ctx->planeSafeY;
      }
      if ( width )
      {
         *width= ctx->planeSafeW;
      }
      if ( height )
      {
         *height= ctx->planeSafeH;
      }

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:

   return result;
}

bool EssContextSetWindowPosition( EssCtx *ctx, int x, int y )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->windowX= x;
      ctx->windowY= y;

      if ( ctx->isWayland )
      {
         #ifdef HAVE_WAYLAND
         if ( ctx->shell && ctx->appSurfaceId )
         {
            if ( !ctx->fullScreen )
            {
               wl_simple_shell_set_geometry( ctx->shell, ctx->appSurfaceId,
                                             ctx->windowX, ctx->windowY,
                                             ctx->windowWidth, ctx->windowHeight );
            }
         }
         else
         {
            ctx->pendingGeometryChange= true;
         }
         #endif
      }
      else
      {
         // ignore
      }

      result= true;

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:

   return result;
}

bool EssContextResizeWindow( EssCtx *ctx, int width, int height )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      result= essResize( ctx, width, height );

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:

   return result;
}

void EssContextRunEventLoopOnce( EssCtx *ctx )
{
   if ( ctx )
   {
      long long start, end, diff, delay;
      if ( ctx->eventLoopPeriodMS )
      {
         start= essGetCurrentTimeMillis();
      }

      essRunEventLoopOnce( ctx );

      if ( ctx->eventLoopPeriodMS )
      {
         if ( ctx->eventLoopLastTimeStamp )
         {
            diff= start-ctx->eventLoopLastTimeStamp;
            delay= ((long long)ctx->eventLoopPeriodMS - diff);
            if ( (delay > 0) && (delay <= ctx->eventLoopPeriodMS) )
            {
               usleep( delay*1000 );
            }
         }
         ctx->eventLoopLastTimeStamp= start;
      }
   }
}

void EssContextUpdateDisplay( EssCtx *ctx )
{
   if ( ctx )
   {
      if ( ctx->haveMode &&
           (ctx->eglDisplay != EGL_NO_DISPLAY) &&
           (ctx->eglSurfaceWindow != EGL_NO_SURFACE) )
      {
         eglSwapBuffers( ctx->eglDisplay, ctx->eglSurfaceWindow );
      }
   }
}

static long long essGetCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static bool essPlatformInit( EssCtx *ctx )
{
   bool result= false;

   if ( ctx->isWayland )
   {
      #ifdef HAVE_WAYLAND
      result= essPlatformInitWayland( ctx );
      #endif
   }
   else
   {
      #ifdef HAVE_WESTEROS
      ctx->haveMode= true;
      result= essPlatformInitDirect( ctx );
      #endif
   }

   return result;   
}

static void essPlatformTerm( EssCtx *ctx )
{
   if ( ctx->isWayland )
   {
      #ifdef HAVE_WAYLAND
      essPlatformTermWayland( ctx );
      #endif
   }
   else
   {
      #ifdef HAVE_WESTEROS
      essPlatformTermDirect( ctx );
      #endif
   }
}

static bool essEGLInit( EssCtx *ctx )
{
   bool result= false;
   EGLBoolean b;
   EGLConfig *eglConfigs= 0;
   EGLint configCount= 0;
   EGLint redSizeNeed, greenSizeNeed, blueSizeNeed, alphaSizeNeed, depthSizeNeed;
   EGLint redSize, greenSize, blueSize, alphaSize, depthSize;
   int i;
   
   DEBUG("essEGLInit: displayType %p", ctx->displayType);
   ctx->eglDisplay= eglGetDisplay( ctx->displayType );
   if ( ctx->eglDisplay == EGL_NO_DISPLAY )
   {
      sprintf( ctx->lastErrorDetail,
               "Error. Unable to get EGL display: eglError %X", eglGetError() );
      goto exit;
   }

   b= eglInitialize( ctx->eglDisplay, &ctx->eglVersionMajor, &ctx->eglVersionMinor );
   if ( !b )
   {
      sprintf( ctx->lastErrorDetail,
               "Error: Unable to initialize EGL display: eglError %X", eglGetError() );
      goto exit;
   }

   b= eglChooseConfig( ctx->eglDisplay, ctx->eglCfgAttr, 0, 0, &configCount );
   if ( !b || (configCount == 0) )
   {
      sprintf( ctx->lastErrorDetail,
               "Error: eglChooseConfig failed to return number of configurations: count: %d eglError %X\n", configCount, eglGetError() );
      goto exit;
   }

   eglConfigs= (EGLConfig*)malloc( configCount*sizeof(EGLConfig) );
   if ( !eglConfigs )
   {
      sprintf( ctx->lastErrorDetail,
               "Error: Unable to allocate memory for %d EGL configurations", configCount );
      goto exit;
   }

   b= eglChooseConfig( ctx->eglDisplay, ctx->eglCfgAttr, eglConfigs, configCount, &configCount );
   if ( !b || (configCount == 0) )
   {
      sprintf( ctx->lastErrorDetail,
               "Error: eglChooseConfig failed to return list of configurations: count: %d eglError %X\n", configCount, eglGetError() );
      goto exit;
   }

   redSizeNeed= greenSizeNeed= blueSizeNeed= alphaSizeNeed= depthSizeNeed= 0;
   for( i= 0; i < ctx->eglCfgAttrSize; i += 2 )
   {
      EGLint type= ctx->eglCfgAttr[i];
      if ( (type != EGL_NONE) && (i+1 < ctx->eglCfgAttrSize) )
      {
         EGLint value= ctx->eglCfgAttr[i+1];
         switch( ctx->eglCfgAttr[i] )
         {
            case EGL_RED_SIZE:
               redSizeNeed= value;
               break;
            case EGL_GREEN_SIZE:
               greenSizeNeed= value;
               break;
            case EGL_BLUE_SIZE:
               blueSizeNeed= value;
               break;
            case EGL_ALPHA_SIZE:
               alphaSizeNeed= value;
               break;
            case EGL_DEPTH_SIZE:
               depthSizeNeed= value;
               break;
            default:
               break;
         }
      }
   }

   for( i= 0; i < configCount; ++i )
   {
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_RED_SIZE, &redSize );
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_GREEN_SIZE, &greenSize );
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_BLUE_SIZE, &blueSize );
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_ALPHA_SIZE, &alphaSize );
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_DEPTH_SIZE, &depthSize );

      DEBUG("essEGLInit: config %d: red: %d green: %d blue: %d alpha: %d depth: %d",
              i, redSize, greenSize, blueSize, alphaSize, depthSize );
      if ( (redSize == redSizeNeed) &&
           (greenSize == greenSizeNeed) &&
           (blueSize == blueSizeNeed) &&
           (alphaSize == alphaSizeNeed) &&
           (depthSize >= depthSizeNeed) )
      {
         DEBUG( "essEGLInit: choosing config %d", i);
         break;
      }
   }

   if ( i == configCount )
   {
      sprintf( ctx->lastErrorDetail,
               "Error: no suitable configuration available\n");
      goto exit;
   }

   ctx->eglConfig= eglConfigs[i];

   ctx->eglContext= eglCreateContext( ctx->eglDisplay, ctx->eglConfig, EGL_NO_CONTEXT, ctx->eglCtxAttr );
   if ( ctx->eglContext == EGL_NO_CONTEXT )
   {
      sprintf( ctx->lastErrorDetail,
               "Error: eglCreateContext failed: eglError %X\n", eglGetError() );
      goto exit;
   }
   DEBUG("essEGLInit: eglContext %p", ctx->eglContext );

   if ( !essCreateNativeWindow( ctx, ctx->windowWidth, ctx->windowHeight ) )
   {
      goto exit;
   }
   DEBUG("essEGLInit: nativeWindow %p", ctx->nativeWindow );

   ctx->eglSurfaceWindow= eglCreateWindowSurface( ctx->eglDisplay,
                                                  ctx->eglConfig,
                                                  ctx->nativeWindow,
                                                  ctx->eglSurfAttr );
   if ( ctx->eglSurfaceWindow == EGL_NO_SURFACE )
   {
      sprintf( ctx->lastErrorDetail,
               "Error: eglCreateWindowSurface failed: eglError %X\n", eglGetError() );
      goto exit;
   }
   DEBUG("essEGLInit: eglSurfaceWindow %p", ctx->eglSurfaceWindow );

   b= eglMakeCurrent( ctx->eglDisplay, ctx->eglSurfaceWindow, ctx->eglSurfaceWindow, ctx->eglContext );
   if ( !b )
   {
      sprintf( ctx->lastErrorDetail,
               "Error: eglMakeCurrent failed: eglError %X\n", eglGetError() );
      goto exit;
   }
    
   eglSwapInterval( ctx->eglDisplay, ctx->eglSwapInterval );

   result= true;

exit:
   if ( !result )
   {
      essEGLTerm(ctx);
   }

   if ( eglConfigs )
   {
      free( eglConfigs );
   }

   return result;
}

static void essEGLTerm( EssCtx *ctx )
{
   if ( ctx )
   {
      if ( ctx->eglDisplay != EGL_NO_DISPLAY )
      {
         eglMakeCurrent( ctx->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );

         if ( ctx->eglSurfaceWindow != EGL_NO_SURFACE )
         {
            eglDestroySurface( ctx->eglDisplay, ctx->eglSurfaceWindow );
            ctx->eglSurfaceWindow= EGL_NO_SURFACE;
         }

         if ( ctx->eglContext != EGL_NO_CONTEXT )
         {
            eglDestroyContext( ctx->eglDisplay, ctx->eglContext );
            ctx->eglContext= EGL_NO_CONTEXT;
         }
         
         eglTerminate( ctx->eglDisplay );
         ctx->eglDisplay= EGL_NO_DISPLAY;

         eglReleaseThread();
      }
   }
}

static void essInitInput( EssCtx *ctx )
{
   if ( ctx )
   {
      if ( ctx->isWayland )
      {
         // Setup during wayland registry processing
      }
      else
      {
         essGetInputDevices( ctx );
         essMonitorInputDevicesLifecycleBegin( ctx );
      }
   }
}

static void essSetDisplaySize( EssCtx *ctx, int width, int height, bool customSafe, int safeX, int safeY, int safeW, int safeH )
{
   ctx->planeWidth= width;
   ctx->planeHeight= height;
   if ( customSafe )
   {
      ctx->planeSafeX= safeX;
      ctx->planeSafeY= safeY;
      ctx->planeSafeW= safeW;
      ctx->planeSafeH= safeH;
   }
   else
   {
      ctx->planeSafeX= width*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
      ctx->planeSafeY= height*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
      ctx->planeSafeW= width-2*ctx->planeSafeX;
      ctx->planeSafeH= height-2*ctx->planeSafeY;
   }
}

static bool essCreateNativeWindow( EssCtx *ctx, int width, int height )
{
   bool result= false;

   if ( ctx )
   {
      if ( ctx->isWayland )
      {
         #ifdef HAVE_WAYLAND
         ctx->wlsurface= wl_compositor_create_surface(ctx->wlcompositor);
         if ( !ctx->wlsurface )
         {
            sprintf( ctx->lastErrorDetail,
                     "Error.  Unable to create wayland surface" );
            goto exit;
         }

         ctx->wleglwindow= wl_egl_window_create(ctx->wlsurface, width, height);
         if ( !ctx->wleglwindow )
         {
            sprintf( ctx->lastErrorDetail,
                     "Error.  Unable to create wayland egl window" );
            goto exit;
         }

         ctx->nativeWindow= (NativeWindowType)ctx->wleglwindow;         

         result= true;
         #endif
      }
      else
      {
         #ifdef HAVE_WESTEROS
         ctx->nativeWindow= (NativeWindowType)WstGLCreateNativeWindow( ctx->glCtx, 
                                                                       0,
                                                                       0,
                                                                       width,
                                                                       height );
#ifndef WESTEROS_PLATFORM_QEMUX86
         if ( !ctx->nativeWindow )
         {
            sprintf( ctx->lastErrorDetail,
                     "Error.  Unable to create native egl window" );
            goto exit;
         }
#endif
         result= true;
         #endif
      }
   }

exit:
   return result;
}

static bool essDestroyNativeWindow( EssCtx *ctx, NativeWindowType nw )
{
   bool result= false;

   if ( ctx )
   {
      if ( nw == ctx->nativeWindow )
      {
         if ( ctx->isWayland )
         {
            #ifdef HAVE_WAYLAND
            if ( ctx->wleglwindow )
            {
               wl_egl_window_destroy( ctx->wleglwindow );
               ctx->wleglwindow= 0;
            }

            if ( ctx->wlsurface )
            {
               wl_surface_destroy( ctx->wlsurface );
               ctx->wlsurface= 0;
            }
            #endif
         }
         else
         {
            #ifdef HAVE_WESTEROS
            if ( ctx->nativeWindow )
            {
               WstGLDestroyNativeWindow( ctx->glCtx, (void*)ctx->nativeWindow );
               ctx->nativeWindow= 0;
            }
            #endif
         }

         ctx->nativeWindow= 0;
         result= true;
      }
   }

exit:
   return result;
}

static bool essResize( EssCtx *ctx, int width, int height )
{
   bool result= false;

   INFO("essResize %dx%d", width, height);
   if ( ctx->isWayland )
   {
      #ifdef HAVE_WAYLAND
      if ( !ctx->fullScreen && ctx->wleglwindow )
      {
         wl_egl_window_resize( ctx->wleglwindow, width, height, 0, 0 );

         result= true;
      }
      #endif
   }
   else
   {
      if ( ctx->eglDisplay != EGL_NO_DISPLAY )
      {
         eglMakeCurrent( ctx->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );

         if ( ctx->eglSurfaceWindow != EGL_NO_SURFACE )
         {
            eglDestroySurface( ctx->eglDisplay, ctx->eglSurfaceWindow );
            ctx->eglSurfaceWindow= EGL_NO_SURFACE;
         }

         if ( ctx->nativeWindow )
         {
            WstGLDestroyNativeWindow( ctx->glCtx, (void*)ctx->nativeWindow );
            ctx->nativeWindow= 0;
         }

         if ( essCreateNativeWindow( ctx, width, height ) )
         {
            ctx->eglSurfaceWindow= eglCreateWindowSurface( ctx->eglDisplay,
                                                           ctx->eglConfig,
                                                           ctx->nativeWindow,
                                                           NULL );
            if ( ctx->eglSurfaceWindow != EGL_NO_SURFACE )
            {
               DEBUG("essResize: eglSurfaceWindow %p", ctx->eglSurfaceWindow );

               if ( eglMakeCurrent( ctx->eglDisplay, ctx->eglSurfaceWindow, ctx->eglSurfaceWindow, ctx->eglContext ) )
               {
                  result= true;
               }
               else
               {
                  ERROR("Error: eglResize: eglMakeCurrent failed: eglError %X", eglGetError());
               }
            }
            else
            {
               ERROR("Error: eglResize: eglCreateWindowSurface failed: eglError %X", eglGetError());
            }
         }
         else
         {
            ERROR("Error: eglResize: essCreateNativeWindow failed");
         }
      }
   }

   if ( result )
   {
      ctx->windowWidth= width;
      ctx->windowHeight= height;
   }

   return result;
}

static void essRunEventLoopOnce( EssCtx *ctx )
{
   if ( ctx )
   {
      if ( ctx->isWayland )
      {
         #ifdef HAVE_WAYLAND
         essProcessRunWaylandEventLoopOnce( ctx );
         #endif
      }
      else
      {
         #ifdef HAVE_WESTEROS
         essProcessInputDevices( ctx );
         #endif
      }

      if ( ctx->keyPressed )
      {
         long long now= essGetCurrentTimeMillis();
         long long diff= now-ctx->lastKeyTime;
         if (
              (ctx->keyRepeating && (diff >= ctx->keyRepeatPeriod)) ||
              (!ctx->keyRepeating && (diff >= ctx->keyRepeatInitialDelay))
            )
         {
            ctx->lastKeyTime= now;
            ctx->keyRepeating= true;
            essProcessKeyPressed( ctx, ctx->lastKeyCode );
         }
      }

      if ( ctx->resizePending )
      {
         ctx->resizePending= false;
         if ( ctx->settingsListener )
         {
            if ( ctx->settingsListener->displaySize )
            {
               ctx->settingsListener->displaySize( ctx->settingsListenerUserData, ctx->resizeWidth, ctx->resizeHeight );
            }
            if ( ctx->settingsListener->displaySafeArea )
            {
               ctx->settingsListener->displaySafeArea( ctx->settingsListenerUserData,
                                                       ctx->planeSafeX, ctx->planeSafeY,
                                                       ctx->planeSafeW, ctx->planeSafeH );
            }
         }
      }
   }
}

static void essProcessKeyPressed( EssCtx *ctx, int linuxKeyCode )
{
   if ( ctx )
   {
      DEBUG("essProcessKeyPressed: key %d", linuxKeyCode);
      if ( ctx->keyListener && ctx->keyListener->keyPressed )
      {
         ctx->keyListener->keyPressed( ctx->keyListenerUserData, linuxKeyCode );
      }
   }
}

static void essProcessKeyReleased( EssCtx *ctx, int linuxKeyCode )
{
   if ( ctx )
   {
      DEBUG("essProcessKeyReleased: key %d", linuxKeyCode);
      if ( ctx->keyListener && ctx->keyListener->keyReleased )
      {
         ctx->keyListener->keyReleased( ctx->keyListenerUserData, linuxKeyCode );
      }
   }
}

static void essProcessPointerMotion( EssCtx *ctx, int x, int y )
{
   if ( ctx )
   {
      TRACE("essProcessKeyPointerMotion (%d, %d)", x, y );
      ctx->pointerX= x;
      ctx->pointerY= y;
      if ( ctx->pointerListener && ctx->pointerListener->pointerMotion )
      {
         ctx->pointerListener->pointerMotion( ctx->pointerListenerUserData, x,  y );
      }
   }
}

static void essProcessPointerButtonPressed( EssCtx *ctx, int button )
{
   if ( ctx )
   {
      DEBUG("essProcessKeyPointerPressed %d", button );
      if ( ctx->pointerListener && ctx->pointerListener->pointerButtonPressed )
      {
         ctx->pointerListener->pointerButtonPressed( ctx->pointerListenerUserData, button, ctx->pointerX, ctx->pointerY );
      }
   }
}

static void essProcessPointerButtonReleased( EssCtx *ctx, int button )
{
   if ( ctx )
   {
      DEBUG("essos: essProcessKeyPointerReleased %d", button );
      if ( ctx->pointerListener && ctx->pointerListener->pointerButtonReleased )
      {
         ctx->pointerListener->pointerButtonReleased( ctx->pointerListenerUserData, button, ctx->pointerX, ctx->pointerY );
      }
   }
}

static void essProcessTouchDown( EssCtx *ctx, int id, int x, int y )
{
   if ( ctx )
   {
      DEBUG("essos: essProcessTouchDown id %d (%d,%d)", id, x, y );
      if ( ctx->touchListener && ctx->touchListener->touchDown )
      {
         ctx->touchListener->touchDown( ctx->touchListenerUserData, id, x, y );
      }
   }
}

static void essProcessTouchUp( EssCtx *ctx, int id )
{
   if ( ctx )
   {
      DEBUG("essos: essProcessTouchUp id %d", id );
      if ( ctx->touchListener && ctx->touchListener->touchUp )
      {
         ctx->touchListener->touchUp( ctx->touchListenerUserData, id );
      }
   }
}

static void essProcessTouchMotion( EssCtx *ctx, int id, int x, int y )
{
   if ( ctx )
   {
      DEBUG("essos: essProcessTouchMotion id %d (%d,%d)", id, x, y );
      if ( ctx->touchListener && ctx->touchListener->touchMotion )
      {
         ctx->touchListener->touchMotion( ctx->touchListenerUserData, id, x, y );
      }
   }
}

static void essProcessTouchFrame( EssCtx *ctx )
{
   if ( ctx )
   {
      DEBUG("essos: essProcessTouchFrame" );
      if ( ctx->touchListener && ctx->touchListener->touchFrame )
      {
         ctx->touchListener->touchFrame( ctx->touchListenerUserData );
      }
   }
}

#ifdef HAVE_WAYLAND
static void essKeyboardKeymap( void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size )
{
   EssCtx *ctx= (EssCtx*)data;

   if ( format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 )
   {
      void *map= mmap( 0, size, PROT_READ, MAP_SHARED, fd, 0 );
      if ( map != MAP_FAILED )
      {
         if ( !ctx->xkbCtx )
         {
            ctx->xkbCtx= xkb_context_new( XKB_CONTEXT_NO_FLAGS );
         }
         else
         {
            ERROR("essKeyboardKeymap: xkb_context_new failed");
         }
         if ( ctx->xkbCtx )
         {
            if ( ctx->xkbKeymap )
            {
               xkb_keymap_unref( ctx->xkbKeymap );
               ctx->xkbKeymap= 0;
            }
            ctx->xkbKeymap= xkb_keymap_new_from_string( ctx->xkbCtx, (char*)map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            if ( !ctx->xkbKeymap )
            {
               ERROR("essKeyboardKeymap: xkb_keymap_new_from_string failed");
            }
            if ( ctx->xkbState )
            {
               xkb_state_unref( ctx->xkbState );
               ctx->xkbState= 0;
            }
            ctx->xkbState= xkb_state_new( ctx->xkbKeymap );
            if ( !ctx->xkbState )
            {
               ERROR("essKeyboardKeymap: xkb_state_new failed");
            }
            if ( ctx->xkbKeymap )
            {
               ctx->modAlt= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_ALT );
               ctx->modCtrl= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CTRL );
               ctx->modShift= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_SHIFT );
               ctx->modCaps= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CAPS );
            }
         }
         munmap( map, size );
      }
   }

   close( fd );
}

static void essKeyboardEnter( void *data, struct wl_keyboard *keyboard, uint32_t serial,
                              struct wl_surface *surface, struct wl_array *keys )
{
   ESS_UNUSED(data);
   ESS_UNUSED(keyboard);
   ESS_UNUSED(serial);
   ESS_UNUSED(keys);

   DEBUG("essKeyboardEnter: keyboard enter surface %p", surface );
}

static void essKeyboardLeave( void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface )
{
   ESS_UNUSED(data);
   ESS_UNUSED(keyboard);
   ESS_UNUSED(serial);

   DEBUG("esKeyboardLeave: keyboard leave surface %p", surface );
}

static void essKeyboardKey( void *data, struct wl_keyboard *keyboard, uint32_t serial,
                            uint32_t time, uint32_t key, uint32_t state )
{
   EssCtx *ctx= (EssCtx*)data;
   ESS_UNUSED(keyboard);
   ESS_UNUSED(serial);
   ESS_UNUSED(time);

   switch( key )
   {
      case KEY_CAPSLOCK:
      case KEY_LEFTSHIFT:
      case KEY_RIGHTSHIFT:
      case KEY_LEFTCTRL:
      case KEY_RIGHTCTRL:
      case KEY_LEFTALT:
      case KEY_RIGHTALT:
         break;
      default:
         if ( state == WL_KEYBOARD_KEY_STATE_PRESSED )
         {
            ctx->lastKeyTime= essGetCurrentTimeMillis();
            ctx->lastKeyCode= key;
            ctx->keyPressed= true;
            ctx->keyRepeating= false;
            essProcessKeyPressed( ctx, key );
         }
         else if ( state == WL_KEYBOARD_KEY_STATE_RELEASED )
         {
            ctx->keyPressed= false;
            essProcessKeyReleased( ctx, key );
         }
         break;
   }
}

static void essUpdateModifierKey( EssCtx *ctx, bool active, int key )
{
   if ( active )
   {
      essProcessKeyPressed( ctx, key );
   }
   else
   {
      essProcessKeyReleased( ctx, key );
   }
}

static void essKeyboardModifiers( void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                  uint32_t mods_depressed, uint32_t mods_latched,
                                  uint32_t mods_locked, uint32_t group )
{
   EssCtx *ctx= (EssCtx*)data;
   if ( ctx->xkbState )
   {
      int wasActive, nowActive, key;

      xkb_state_update_mask( ctx->xkbState, mods_depressed, mods_latched, mods_locked, 0, 0, group );
      DEBUG("essKeyboardModifiers: mods_depressed %X mods locked %X", mods_depressed, mods_locked);

      wasActive= (ctx->modMask & (1<<ctx->modCaps));
      nowActive= (mods_locked & (1<<ctx->modCaps));
      key= KEY_CAPSLOCK;
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modCaps);
         essProcessKeyPressed( ctx, key );
         essProcessKeyReleased( ctx, key );
      }

      wasActive= (ctx->modMask & (1<<ctx->modShift));
      nowActive= (mods_depressed & (1<<ctx->modShift));
      key= KEY_RIGHTSHIFT;
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modShift);
         essUpdateModifierKey( ctx, nowActive, key );
      }

      wasActive= (ctx->modMask & (1<<ctx->modCtrl));
      nowActive= (mods_depressed & (1<<ctx->modCtrl));
      key= KEY_RIGHTCTRL;
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modCtrl);
         essUpdateModifierKey( ctx, nowActive, key );
      }

      wasActive= (ctx->modMask & (1<<ctx->modAlt));
      nowActive= (mods_depressed & (1<<ctx->modAlt));
      key= KEY_RIGHTALT;
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modAlt);
         essUpdateModifierKey( ctx, nowActive, key );
      }
   }
}

static void essKeyboardRepeatInfo( void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay )
{
   ESS_UNUSED(data);
   ESS_UNUSED(keyboard);
   ESS_UNUSED(rate);
   ESS_UNUSED(delay);
}

static const struct wl_keyboard_listener essKeyboardListener= {
   essKeyboardKeymap,
   essKeyboardEnter,
   essKeyboardLeave,
   essKeyboardKey,
   essKeyboardModifiers,
   essKeyboardRepeatInfo
};

static void essPointerEnter( void* data, struct wl_pointer *pointer, uint32_t serial,
                             struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy )
{
   ESS_UNUSED(pointer);
   ESS_UNUSED(serial);
   EssCtx *ctx= (EssCtx*)data;
   int x, y;

   x= wl_fixed_to_int( sx );
   y= wl_fixed_to_int( sy );

   DEBUG("essPointerEnter: pointer enter surface %p (%d,%d)", surface, x, y );
}

static void essPointerLeave( void* data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface )
{
   ESS_UNUSED(data);
   ESS_UNUSED(pointer);
   ESS_UNUSED(serial);

   DEBUG("essPointerLeave: pointer leave surface %p", surface );
}

static void essPointerMotion( void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy )
{
   ESS_UNUSED(pointer);
   EssCtx *ctx= (EssCtx*)data;
   int x, y;

   x= wl_fixed_to_int( sx );
   y= wl_fixed_to_int( sy );

   essProcessPointerMotion( ctx, x, y );
}

static void essPointerButton( void *data, struct wl_pointer *pointer, uint32_t serial,
                              uint32_t time, uint32_t button, uint32_t state )
{
   ESS_UNUSED(pointer);
   ESS_UNUSED(serial);
   EssCtx *ctx= (EssCtx*)data;

   if ( state == WL_POINTER_BUTTON_STATE_PRESSED )
   {
      essProcessPointerButtonPressed( ctx, button );
   }
   else
   {
      essProcessPointerButtonReleased( ctx, button );
   }
}

static void essPointerAxis( void *data, struct wl_pointer *pointer, uint32_t time,
                            uint32_t axis, wl_fixed_t value )
{
   ESS_UNUSED(data);
   ESS_UNUSED(pointer);
   ESS_UNUSED(time);
   ESS_UNUSED(value);
}

static const struct wl_pointer_listener essPointerListener = {
   essPointerEnter,
   essPointerLeave,
   essPointerMotion,
   essPointerButton,
   essPointerAxis
};

static void essTouchDown( void *data, struct wl_touch *touch,
                          uint32_t serial, uint32_t time, struct wl_surface *surface,
                          int32_t id, wl_fixed_t sx, wl_fixed_t sy )
{
   ESS_UNUSED(touch);
   ESS_UNUSED(serial);
   ESS_UNUSED(time);
   ESS_UNUSED(surface);
   EssCtx *ctx= (EssCtx*)data;

   int x, y;

   x= wl_fixed_to_int( sx );
   y= wl_fixed_to_int( sy );

   essProcessTouchDown( ctx, id, x, y );
}

static void essTouchUp( void *data, struct wl_touch *touch,
                        uint32_t serial, uint32_t time, int32_t id )
{
   ESS_UNUSED(touch);
   ESS_UNUSED(serial);
   ESS_UNUSED(time);
   EssCtx *ctx= (EssCtx*)data;

   essProcessTouchUp( ctx, id );
}

static void essTouchMotion( void *data, struct wl_touch *touch,
                            uint32_t time, int32_t id, wl_fixed_t sx, wl_fixed_t sy )
{
   ESS_UNUSED(touch);
   ESS_UNUSED(time);
   EssCtx *ctx= (EssCtx*)data;

   int x, y;

   x= wl_fixed_to_int( sx );
   y= wl_fixed_to_int( sy );

   essProcessTouchMotion( ctx, id, x, y );
}

static void essTouchFrame( void *data, struct wl_touch *touch )
{
   ESS_UNUSED(touch);
   EssCtx *ctx= (EssCtx*)data;

   essProcessTouchFrame( ctx );
}

static const struct wl_touch_listener essTouchListener= {
   essTouchDown,
   essTouchUp,
   essTouchMotion,
   essTouchFrame
};

static void essSeatCapabilities( void *data, struct wl_seat *seat, uint32_t capabilities )
{
   EssCtx *ctx= (EssCtx*)data;

   DEBUG("essSeatCapabilities: seat %p caps: %X", seat, capabilities );
   
   if ( capabilities & WL_SEAT_CAPABILITY_KEYBOARD )
   {
      DEBUG("essSeatCapabilities:  seat has keyboard");
      ctx->wlkeyboard= wl_seat_get_keyboard( ctx->wlseat );
      DEBUG("essSeatCapabilities:  keyboard %p", ctx->wlkeyboard );
      wl_keyboard_add_listener( ctx->wlkeyboard, &essKeyboardListener, ctx );
   }
   if ( capabilities & WL_SEAT_CAPABILITY_POINTER )
   {
      DEBUG("essSeatCapabilities:  seat has pointer");
      ctx->wlpointer= wl_seat_get_pointer( ctx->wlseat );
      DEBUG("essSeatCapabilities:  pointer %p", ctx->wlpointer );
      wl_pointer_add_listener( ctx->wlpointer, &essPointerListener, ctx );
   }
   if ( capabilities & WL_SEAT_CAPABILITY_TOUCH )
   {
      DEBUG("essSeatCapabilities:  seat has touch");
      ctx->wltouch= wl_seat_get_touch( ctx->wlseat );
      DEBUG("essSeatCapabilities:  touch %p", ctx->wltouch );
      wl_touch_add_listener( ctx->wltouch, &essTouchListener, ctx );
   }   
}

static void essSeatName( void *data, struct wl_seat *seat, const char *name )
{
   ESS_UNUSED(data);
   ESS_UNUSED(seat);
   ESS_UNUSED(name);
}

static const struct wl_seat_listener essSeatListener = {
   essSeatCapabilities,
   essSeatName 
};

static void essOutputGeometry( void *data, struct wl_output *output, int32_t x, int32_t y,
                               int32_t physical_width, int32_t physical_height, int32_t subpixel,
                               const char *make, const char *model, int32_t transform )
{
   ESS_UNUSED(output);
   ESS_UNUSED(x);
   ESS_UNUSED(y);
   ESS_UNUSED(physical_width);
   ESS_UNUSED(physical_height);
   ESS_UNUSED(subpixel);
   ESS_UNUSED(transform);

   EssCtx *ctx= (EssCtx*)data;
   if ( data && make && model )
   {
      int lenMake= strlen(make);
      int lenModel= strlen(model);
      if (
           ((lenMake == 8) && !strncmp( make, "Westeros", lenMake) ) &&
           ((lenModel == 17) && !strncmp( model, "Westeros-embedded", lenModel) )
         )
      {
         ctx->fullScreen= true;
      }
   }
}

static void essOutputMode( void *data, struct wl_output *output, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh )
{
   EssCtx *ctx= (EssCtx*)data;

   INFO("essOutputMode: outputMode: mode %d(%d) (%dx%d)", flags, WL_OUTPUT_MODE_CURRENT, width, height);
   if ( flags & WL_OUTPUT_MODE_CURRENT )
   {
      ctx->haveMode= true;
      if ( (width != ctx->planeWidth) || (height != ctx->planeHeight) )
      {
         essSetDisplaySize( ctx, width, height, false, 0, 0, 0, 0);
         ctx->windowWidth= width;
         ctx->windowHeight= height;

         if ( ctx->fullScreen && ctx->wleglwindow )
         {
            wl_egl_window_resize( ctx->wleglwindow, width, height, 0, 0 );
         }

         if ( ctx->settingsListener )
         {
            if ( ctx->settingsListener->displaySize )
            {
               ctx->settingsListener->displaySize( ctx->settingsListenerUserData, width, height );
            }
            if ( ctx->settingsListener->displaySafeArea )
            {
               ctx->settingsListener->displaySafeArea( ctx->settingsListenerUserData,
                                                       ctx->planeSafeX, ctx->planeSafeY,
                                                       ctx->planeSafeW, ctx->planeSafeH );
            }
         }
      }
   }
}

static void essOutputDone( void *data, struct wl_output *output )
{
   ESS_UNUSED(data);
   ESS_UNUSED(output);
}

static void essOutputScale( void *data, struct wl_output *output, int32_t factor )
{
   ESS_UNUSED(data);
   ESS_UNUSED(output);
   ESS_UNUSED(factor);
}

static const struct wl_output_listener essOutputListener = {
   essOutputGeometry,
   essOutputMode,
   essOutputDone,
   essOutputScale
};

static void essShellSurfaceId(void *data,
                           struct wl_simple_shell *wl_simple_shell,
                           struct wl_surface *surface,
                           uint32_t surfaceId)
{
   EssCtx *ctx= (EssCtx*)data;
   char name[32];

   DEBUG("shell: surface created: %p id %x", surface, surfaceId);
   ctx->appSurfaceId= surfaceId;
   if ( ctx->appName )
   {
      wl_simple_shell_set_name( ctx->shell, surfaceId, ctx->appName );
   }
   else
   {
      sprintf( name, "essos-app-%x", surfaceId );
      wl_simple_shell_set_name( ctx->shell, surfaceId, name );
   }
   if ( ctx->pendingGeometryChange )
   {
      ctx->pendingGeometryChange= false;
      if ( !ctx->fullScreen )
      {
         wl_simple_shell_set_geometry( ctx->shell, ctx->appSurfaceId,
                                       ctx->windowX, ctx->windowY,
                                       ctx->windowWidth, ctx->windowHeight );
      }
   }
}

static void essShellSurfaceCreated(void *data,
                                struct wl_simple_shell *wl_simple_shell,
                                uint32_t surfaceId,
                                const char *name)
{
   ESS_UNUSED(data);
   ESS_UNUSED(wl_simple_shell);
   ESS_UNUSED(name);
}

static void essShellSurfaceDestroyed(void *data,
                                  struct wl_simple_shell *wl_simple_shell,
                                  uint32_t surfaceId,
                                  const char *name)
{
   ESS_UNUSED(data);
   ESS_UNUSED(wl_simple_shell);
   ESS_UNUSED(surfaceId);
   ESS_UNUSED(name);
}

static void essShellSurfaceStatus(void *data,
                               struct wl_simple_shell *wl_simple_shell,
                               uint32_t surfaceId,
                               const char *name,
                               uint32_t visible,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height,
                               wl_fixed_t opacity,
                               wl_fixed_t zorder)
{
   ESS_UNUSED(data);
   ESS_UNUSED(wl_simple_shell);
   ESS_UNUSED(surfaceId);
   ESS_UNUSED(name);
   ESS_UNUSED(visible);
   ESS_UNUSED(x);
   ESS_UNUSED(y);
   ESS_UNUSED(width);
   ESS_UNUSED(height);
   ESS_UNUSED(opacity);
   ESS_UNUSED(zorder);
}

static void essShellGetSurfacesDone(void *data,
                                 struct wl_simple_shell *wl_simple_shell)
{
   ESS_UNUSED(data);
   ESS_UNUSED(wl_simple_shell);
}

static const struct wl_simple_shell_listener shellListener =
{
   essShellSurfaceId,
   essShellSurfaceCreated,
   essShellSurfaceDestroyed,
   essShellSurfaceStatus,
   essShellGetSurfacesDone
};

static void essRegistryHandleGlobal(void *data, 
                                    struct wl_registry *registry, uint32_t id,
                                    const char *interface, uint32_t version)
{
   EssCtx *ctx= (EssCtx*)data;
   int len;

   DEBUG("essRegistryHandleGlobal: id %d interface (%s) version %d", id, interface, version );

   len= strlen(interface);
   if ( (len==13) && !strncmp(interface, "wl_compositor", len) ) {
      ctx->wlcompositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
      DEBUG("essRegistryHandleGlobal: wlcompositor %p", ctx->wlcompositor);
   } 
   else if ( (len==7) && !strncmp(interface, "wl_seat", len) ) {
      ctx->wlseat= (struct wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 4);
      DEBUG("essRegistryHandleGlobal: wlseat %p", ctx->wlseat);
      wl_seat_add_listener(ctx->wlseat, &essSeatListener, ctx);
   }
   else if ( (len==9) && !strncmp(interface, "wl_output", len) ) {
      ctx->wloutput= (struct wl_output*)wl_registry_bind(registry, id, &wl_output_interface, 2);
      DEBUG("essRegistryHandleGlobal: wloutput %p", ctx->wloutput);
      wl_output_add_listener(ctx->wloutput, &essOutputListener, ctx);
      wl_display_roundtrip(ctx->wldisplay);
   }
   else if ( (len==15) && !strncmp(interface, "wl_simple_shell", len) ) {
      ctx->shell= (struct wl_simple_shell*)wl_registry_bind(registry, id, &wl_simple_shell_interface, 1);
      DEBUG("shell %p", ctx->shell );
      wl_simple_shell_add_listener(ctx->shell, &shellListener, ctx);
   }
}

static void essRegistryHandleGlobalRemove(void *data, 
                                          struct wl_registry *registry,
                                          uint32_t name)
{
   ESS_UNUSED(data);
   ESS_UNUSED(registry);
   ESS_UNUSED(name);
}

static const struct wl_registry_listener essRegistryListener = 
{
   essRegistryHandleGlobal,
   essRegistryHandleGlobalRemove
};

static bool essPlatformInitWayland( EssCtx *ctx )
{
   bool result= false;

   if ( ctx )
   {
      ctx->wldisplay= wl_display_connect( NULL );
      if ( !ctx->wldisplay )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Failed to connect to wayland display" );
         goto exit;
      }

      DEBUG("essPlatformInitWayland: wldisplay %p", ctx->wldisplay);

      ctx->wlregistry= wl_display_get_registry( ctx->wldisplay );
      if ( !ctx->wlregistry )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Failed to get wayland display registry" );
         goto exit;
      }

      wl_registry_add_listener( ctx->wlregistry, &essRegistryListener, ctx );
      wl_display_roundtrip( ctx->wldisplay );

      ctx->waylandFd= wl_display_get_fd( ctx->wldisplay );
      if ( ctx->waylandFd < 0 )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Failed to get wayland display fd" );
         goto exit;
      }
      ctx->wlPollFd.fd= ctx->waylandFd;
      ctx->wlPollFd.events= POLLIN | POLLERR | POLLHUP;
      ctx->wlPollFd.revents= 0;

      ctx->displayType= (NativeDisplayType)ctx->wldisplay;

      result= true;
   }

exit:
   return result;
}

static void essPlatformTermWayland( EssCtx *ctx )
{
   if ( ctx )
   {
      if ( ctx->wldisplay )
      {
         if ( ctx->wleglwindow )
         {
            wl_egl_window_destroy( ctx->wleglwindow );
            ctx->wleglwindow= 0;
         }

         if ( ctx->wlsurface )
         {
            wl_surface_destroy( ctx->wlsurface );
            ctx->wlsurface= 0;
         }

         if ( ctx->wlcompositor )
         {
            wl_compositor_destroy( ctx->wlcompositor );
            ctx->wlcompositor= 0;
         }

         if ( ctx->wlseat )
         {
            wl_seat_destroy(ctx->wlseat);
            ctx->wlseat= 0;
         }

         if ( ctx->wloutput )
         {
            wl_output_destroy(ctx->wloutput);
            ctx->wloutput= 0;
         }

         if ( ctx->wlregistry )
         {
            wl_registry_destroy( ctx->wlregistry );
            ctx->wlregistry= 0;
         }

         wl_display_disconnect( ctx->wldisplay );
         ctx->wldisplay= 0;
      }
   }
}

static void essInitInputWayland( EssCtx *ctx )
{
   if ( ctx )
   {
      #ifdef HAVE_WAYLAND
      if ( ctx->wlseat )
      {
         wl_seat_add_listener(ctx->wlseat, &essSeatListener, ctx);
         wl_display_roundtrip(ctx->wldisplay);
      }
      #endif
   }
}

static void essProcessRunWaylandEventLoopOnce( EssCtx *ctx )
{
   int n;
   bool error= false;

   wl_display_flush( ctx->wldisplay );
   wl_display_dispatch_pending( ctx->wldisplay );

   n= poll(&ctx->wlPollFd, 1, 0);
   if ( n >= 0 )
   {
      if ( ctx->wlPollFd.revents & POLLIN )
      {
         if ( wl_display_dispatch( ctx->wldisplay ) == -1 )
         {
            error= true;
         }
      }
      if ( ctx->wlPollFd.revents & (POLLERR|POLLHUP) )
      {
         error= true;
      }
      if ( error )
      {
         if ( ctx->terminateListener && ctx->terminateListener->terminated )
         {
            ctx->terminateListener->terminated( ctx->terminateListenerUserData );
         }
      }
   }
}
#endif

#ifdef HAVE_WESTEROS
extern "C"
{
   typedef void (*DisplaySizeCallback)( void *userData, int width, int height );
   typedef bool (*AddDisplaySizeListener)( WstGLCtx *ctx, void *userData, DisplaySizeCallback listener );
   typedef bool (*GetDisplaySafeArea)( WstGLCtx *ctx, int *x, int *y, int *w, int *h );
   typedef bool (*GetDisplayCaps)( WstGLCtx *ctx, unsigned int *caps );
   typedef bool (*SetDisplayMode)( WstGLCtx *ctx, const char *mode );

}

static GetDisplaySafeArea gGetDisplaySafeArea= 0;
static GetDisplayCaps gGetDisplayCaps= 0;
static SetDisplayMode gSetDisplayMode= 0;

void displaySizeCallback( void *userData, int width, int height )
{
   EssCtx *ctx= (EssCtx*)userData;
   int safex= 0, safey= 0, safew= 0, safeh= 0;
   bool customSafe= false;
   INFO("displaySizeCallback: display size %dx%d", width, height );
   if ( gGetDisplaySafeArea )
   {
      if ( gGetDisplaySafeArea( ctx->glCtx, &safex, &safey, &safew, &safeh ) )
      {
         INFO("displaySizeCallback: display safe (%d,%d,%d,%d)", safex, safey, safew, safeh );
         customSafe= true;
      }
      else
      {
         ERROR("failure to get display safe area");
      }
   }
   essSetDisplaySize( ctx, width, height, customSafe, safex, safey, safew, safeh);
   if ( !ctx->haveMode )
   {
      ctx->windowWidth= width;
      ctx->windowHeight= height;
   }
   ctx->resizePending= true;
   ctx->resizeWidth= width;
   ctx->resizeHeight= height;
}

static bool essPlatformInitDirect( EssCtx *ctx )
{
   bool result= false;

   if ( ctx )
   {
      ctx->glCtx= WstGLInit();
      if ( !ctx->glCtx )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Failed to create a platform context" );
         goto exit;
      }
      DEBUG("essPlatformInitDirect: glCtx %p", ctx->glCtx);

      {
         void *module= dlopen( "libwesteros_gl.so.0.0.0", RTLD_NOW );
         if ( module )
         {
            AddDisplaySizeListener addDisplaySizeListener= 0;
            addDisplaySizeListener= (AddDisplaySizeListener)dlsym( module, "_WstGLAddDisplaySizeListener" );
            gGetDisplaySafeArea= (GetDisplaySafeArea)dlsym( module, "_WstGLGetDisplaySafeArea" );
            gGetDisplayCaps= (GetDisplayCaps)dlsym( module, "_WstGLGetDisplayCaps" );
            gSetDisplayMode= (SetDisplayMode)dlsym( module, "_WstGLSetDisplayMode" );
            if ( addDisplaySizeListener )
            {
               addDisplaySizeListener( ctx->glCtx, ctx, displaySizeCallback );
            }
            dlclose( module );
         }
      }

      ctx->displayType= EGL_DEFAULT_DISPLAY;

      result= true;
   }

exit:
   return result;
}

static void essPlatformTermDirect( EssCtx *ctx )
{
   if ( ctx )
   {
      if ( ctx->nativeWindow )
      {
         WstGLDestroyNativeWindow( ctx->glCtx, (void*)ctx->nativeWindow );
         ctx->nativeWindow= 0;
      }
      if ( ctx->glCtx )
      {
         WstGLTerm( ctx->glCtx );
         ctx->glCtx= 0;
      }
   }
}

static bool essPlatformSetDisplayModeDirect( EssCtx *ctx, const char *mode )
{
   bool result= false;
   bool canSetMode= false;

   #ifdef WESTEROS_GL_DISPLAY_CAPS
   if ( gGetDisplayCaps && gSetDisplayMode )
   {
      unsigned int displayCaps= 0;
      if ( gGetDisplayCaps( ctx->glCtx, &displayCaps ) )
      {
         if ( displayCaps & WstGLDisplayCap_modeset )
         {
            canSetMode= true;
         }
      }
   }
   #endif

   if ( !canSetMode )
   {
      sprintf( ctx->lastErrorDetail,
               "Error.  Mode setting not supported" );
      goto exit;
   }

   result= gSetDisplayMode( ctx->glCtx, mode );
   if ( !result )
   {
      sprintf( ctx->lastErrorDetail,
               "Error.  Mode setting for (%s) failed", mode );
      goto exit;
   }

exit:

   return result;
}

static const char *inputPath= "/dev/input/";

static int essOpenInputDevice( EssCtx *ctx, const char *devPathName )
{
   int fd= -1;   
   struct stat buf;
   
   if ( stat( devPathName, &buf ) == 0 )
   {
      if ( S_ISCHR(buf.st_mode) )
      {
         fd= open( devPathName, O_RDONLY | O_CLOEXEC );
         if ( fd < 0 )
         {
            snprintf( ctx->lastErrorDetail, ESS_MAX_ERROR_DETAIL,
            "Error: error opening device: %s\n", devPathName );
         }
         else
         {
            pollfd pfd;
            DEBUG( "essOpenInputDevice: opened device %s : fd %d", devPathName, fd );
            pfd.fd= fd;
            ctx->inputDeviceFds.push_back( pfd );
         }
      }
      else
      {
         DEBUG("essOpenInputDevice: ignoring non character device %s", devPathName );
      }
   }
   else
   {
      DEBUG( "essOpenInputDevice: error performing stat on device: %s", devPathName );
   }
   
   return fd;
}

static char *essGetInputDevice( EssCtx *ctx, const char *path, char *devName )
{
   int len;
   char *devicePathName= 0;
   struct stat buffer;
   
   if ( !devName )
      return devicePathName; 
      
   len= strlen( devName );
   
   devicePathName= (char *)malloc( strlen(path)+len+1);
   if ( devicePathName )
   {
      strcpy( devicePathName, path );
      strcat( devicePathName, devName );     

      if ( !stat(devicePathName, &buffer) )
      {
         DEBUG( "essGetInputDevice: found %s", devicePathName );
      }
      else
      {
         free( devicePathName );
         devicePathName= 0;
      }
   }

   return devicePathName;
}

static void essGetInputDevices( EssCtx *ctx )
{
   DIR *dir;
   struct dirent *result;
   char *devPathName;
   if ( NULL != (dir = opendir( inputPath )) )
   {
      while( NULL != (result = readdir( dir )) )
      {
         if ( (result->d_type != DT_DIR) &&
             !strncmp(result->d_name, "event", 5) )
         {
            devPathName= essGetInputDevice( ctx, inputPath, result->d_name );
            if ( devPathName )
            {
               if (essOpenInputDevice( ctx, devPathName ) < 0 )
               {
                  ERROR("essos: could not open device %s", devPathName);
               }
               free( devPathName );
            }
         }
      }

      closedir( dir );
   }
}

static void essMonitorInputDevicesLifecycleBegin( EssCtx *ctx )
{
   pollfd pfd;

   ctx->notifyFd= inotify_init();
   if ( ctx->notifyFd >= 0 )
   {
      pfd.fd= ctx->notifyFd;
      ctx->watchFd= inotify_add_watch( ctx->notifyFd, inputPath, IN_CREATE | IN_DELETE );
      ctx->inputDeviceFds.push_back( pfd );
   }
}

static void essMonitorInputDevicesLifecycleEnd( EssCtx *ctx )
{
   if ( ctx->notifyFd >= 0 )
   {
      if ( ctx->watchFd >= 0 )
      {
         inotify_rm_watch( ctx->notifyFd, ctx->watchFd );
         ctx->watchFd= -1;
      }
      ctx->inputDeviceFds.pop_back();
      close( ctx->notifyFd );
      ctx->notifyFd= -1;
   }
}

static void essReleaseInputDevices( EssCtx *ctx )
{
   while( ctx->inputDeviceFds.size() > 0 )
   {
      pollfd pfd= ctx->inputDeviceFds[0];
      DEBUG( "essos: closing device fd: %d", pfd.fd );
      close( pfd.fd );
      ctx->inputDeviceFds.erase( ctx->inputDeviceFds.begin() );
   }
}

static void essProcessInputDevices( EssCtx *ctx )
{
   int deviceCount;
   int i, n;
   int pollCnt= 0;
   input_event e;
   char intfyEvent[512];
   static bool mouseMoved= false;
   static int mouseAccel= 1;
   static int mouseX= 0;
   static int mouseY= 0;
   static int currTouchSlot= 0;
   static bool touchChanges= false;
   static bool touchClean= false;

   deviceCount= ctx->inputDeviceFds.size();

   for( i= 0; i < deviceCount; ++i )
   {
      ctx->inputDeviceFds[i].events= POLLIN | POLLERR;
      ctx->inputDeviceFds[i].revents= 0;
   }

   n= poll(&ctx->inputDeviceFds[0], deviceCount, 0);
   while ( n >= 0 )
   {
      for( i= 0; i < deviceCount; ++i )
      {
         if ( ctx->inputDeviceFds[i].revents & POLLIN )
         {
            if ( ctx->inputDeviceFds[i].fd == ctx->notifyFd )
            {
               // A hotplug event has occurred
               n= read( ctx->notifyFd, &intfyEvent, sizeof(intfyEvent) );
               if ( n >= sizeof(struct inotify_event) )
               {
                  struct inotify_event *iev= (struct inotify_event*)intfyEvent;
                  {
                     // Re-discover devices
                     DEBUG("essProcessInputDevices: inotify: mask %x (%s) wd %d (%d)", iev->mask, iev->name, iev->wd, ctx->watchFd );
                     pollfd pfd= ctx->inputDeviceFds.back();
                     ctx->inputDeviceFds.pop_back();
                     essReleaseInputDevices( ctx );
                     usleep( 100000 );
                     essGetInputDevices( ctx );
                     ctx->inputDeviceFds.push_back( pfd );
                     deviceCount= ctx->inputDeviceFds.size();
                     break;
                  }
               }
            }
            else
            {
               n= read( ctx->inputDeviceFds[i].fd, &e, sizeof(input_event) );
               if ( n > 0 )
               {
                  switch( e.type )
                  {
                     case EV_KEY:
                        switch( e.code )
                        {
                           case BTN_LEFT:
                           case BTN_RIGHT:
                           case BTN_MIDDLE:
                           case BTN_SIDE:
                           case BTN_EXTRA:
                              {
                                 unsigned int keyCode= e.code;
                                 
                                 switch ( e.value )
                                 {
                                    case 0:
                                       essProcessPointerButtonReleased( ctx, keyCode );
                                       break;
                                    case 1:
                                       essProcessPointerButtonPressed( ctx, keyCode );
                                       break;
                                    default:
                                       break;
                                 }
                              }
                              break;
                           case BTN_TOUCH:
                              // Ignore
                              break;
                           default:
                              {
                                 int keyCode= e.code;
                                 long long timeMillis= e.time.tv_sec*1000LL+e.time.tv_usec/1000LL;

                                 switch ( e.value )
                                 {
                                    case 0:
                                       ctx->keyPressed= false;
                                       essProcessKeyReleased( ctx, keyCode );
                                       break;
                                    case 1:
                                       ctx->lastKeyTime= timeMillis;
                                       ctx->lastKeyCode= keyCode;
                                       ctx->keyPressed= true;
                                       ctx->keyRepeating= false;
                                       essProcessKeyPressed( ctx, keyCode );
                                       break;
                                    default:
                                       break;
                                 }
                              }
                              break;
                        }
                        break;
                     case EV_REL:
                        switch( e.code )
                        {
                           case REL_X:
                              mouseX= mouseX + e.value * mouseAccel;
                              if ( mouseX < 0 ) mouseX= 0;
                              if ( mouseX > ctx->planeWidth ) mouseX= ctx->planeWidth;
                              mouseMoved= true;
                              break;
                           case REL_Y:
                              mouseY= mouseY + e.value * mouseAccel;
                              if ( mouseY < 0 ) mouseY= 0;
                              if ( mouseY > ctx->planeHeight ) mouseY= ctx->planeHeight;
                              mouseMoved= true;
                              break;
                           default:
                              break;
                        }
                        break;
                     case EV_SYN:
                        {
                           if ( mouseMoved )
                           {
                              essProcessPointerMotion( ctx, mouseX, mouseY );
                              
                              mouseMoved= false;
                           }
                           if ( touchChanges )
                           {
                              bool touchEvents= false;
                              for( int i= 0; i < ESS_MAX_TOUCH; ++i )
                              {
                                 if ( ctx->touch[i].valid )
                                 {
                                    if ( ctx->touch[i].starting )
                                    {
                                       essProcessTouchDown( ctx, ctx->touch[i].id, ctx->touch[i].x, ctx->touch[i].y );
                                       touchEvents= true;
                                    }
                                    else if ( ctx->touch[i].stopping )
                                    {
                                       essProcessTouchUp( ctx, ctx->touch[i].id );
                                       touchEvents= true;
                                    }
                                    else if ( ctx->touch[i].moved )
                                    {
                                       essProcessTouchMotion( ctx, ctx->touch[i].id, ctx->touch[i].x, ctx->touch[i].y );
                                       touchEvents= true;
                                    }
                                 }
                              }

                              if ( touchEvents )
                              {
                                 essProcessTouchFrame( ctx );
                              }

                              if ( touchClean )
                              {
                                 touchClean= false;
                                 for( int i= 0; i < ESS_MAX_TOUCH; ++i )
                                 {
                                    ctx->touch[i].starting= false;
                                    if ( ctx->touch[i].stopping )
                                    {
                                       ctx->touch[i].valid= false;
                                       ctx->touch[i].stopping= false;
                                       ctx->touch[i].id= -1;
                                    }
                                 }
                              }
                              touchChanges= false;
                           }
                        }
                        break;
                     case EV_ABS:
                        switch( e.code )
                        {
                           case ABS_MT_SLOT:
                              currTouchSlot= e.value;
                              break;
                           case ABS_MT_POSITION_X:
                              if ( (currTouchSlot >= 0) && (currTouchSlot < ESS_MAX_TOUCH) )
                              {
                                 ctx->touch[currTouchSlot].x= e.value;
                                 ctx->touch[currTouchSlot].moved= true;
                                 touchChanges= true;
                              }
                              break;
                           case ABS_MT_POSITION_Y:
                              if ( (currTouchSlot >= 0) && (currTouchSlot < ESS_MAX_TOUCH) )
                              {
                                 ctx->touch[currTouchSlot].y= e.value;
                                 ctx->touch[currTouchSlot].moved= true;
                                 touchChanges= true;
                              }
                              break;
                           case ABS_MT_TRACKING_ID:
                              if ( (currTouchSlot >= 0) && (currTouchSlot < ESS_MAX_TOUCH) )
                              {
                                 ctx->touch[currTouchSlot].valid= true;
                                 if ( e.value >= 0 )
                                 {
                                    ctx->touch[currTouchSlot].id= e.value;
                                    ctx->touch[currTouchSlot].starting= true;
                                 }
                                 else
                                 {
                                    ctx->touch[currTouchSlot].stopping= true;
                                 }
                                 touchClean= true;
                                 touchChanges= true;
                              }
                              break;
                           default:
                              break;
                        }
                        break;
                     default:
                        break;
                  }
               }
            }
         }
      }

      if ( ++pollCnt >= ESS_INPUT_POLL_LIMIT )
      {
         break;
      }
      n= poll(&ctx->inputDeviceFds[0], deviceCount, 0);
   }
}
#endif

