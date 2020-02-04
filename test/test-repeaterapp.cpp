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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "test-repeaterapp.h"
#include "test-egl.h"

#include "westeros-compositor.h"

#include "wayland-client.h"
#include "wayland-egl.h"

static void startClient( EMCTX *emctx, const char *displayName, int bufferIdBase );
static void client( int bufferIdBase );

static void terminatedCallback( WstCompositor *ctx, void *userData )
{
   bool *terminated= (bool*)userData;
   
   *terminated= true;
}

void runRepeaterApp(int argc, const char **argv)
{
   int pid;
   bool result;
   EMCTX *emctx= 0;
   WstCompositor *wctx= 0;
   const char *displayName= 0;
   const char *upstreamDisplayName= 0;
   int bufferIdBase= 500;
   bool terminated= false;

   pid= getpid();

   upstreamDisplayName= getenv("WAYLAND_DISPLAY");

   printf("runRepeaterApp: pid %d: start\n", pid);
   if ( argc > 1 )
   {
      displayName= argv[1];
   }
   if ( argc > 2 )
   {
      bufferIdBase= atoi(argv[2]);
   }
   printf("runRepeaterApp: pid %d bufferIdBase %d upstreamDisplayName %s\n", pid, bufferIdBase, upstreamDisplayName);

   emctx= EMCreateContext();
   if ( !emctx )
   {
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctx, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctx, upstreamDisplayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetTerminatedCallback( wctx, terminatedCallback, &terminated );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetTerminatedCallback failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   startClient( emctx, displayName, bufferIdBase );

   while( !terminated )
   {
      usleep( 10000 );
   }

   printf("repeater app: terminating\n");
exit:

   if ( wctx )
   {
      WstCompositorDestroy( wctx );
   }

   if ( emctx )
   {
      EMDestroyContext( emctx );
   }
}

static void startClient( EMCTX *emctx, const char *displayName, int bufferIdBase )
{
   int pid;

   pid= fork();
   if ( pid == 0 )
   {
      setenv( "WAYLAND_DISPLAY", displayName, true );

      client( bufferIdBase );
   }
   else if ( pid == -1 )
   {
      EMERROR("Failed to spawn client");
   }
   else
   {
      printf("started client pid %d\n", pid);
   }
}

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

typedef struct _TestCtx
{
   TestEGLCtx eglCtx;
   struct wl_display *display;
   struct wl_compositor *compositor;
   struct wl_surface *surface;
   struct wl_egl_window *wlEglWindow;
   int windowWidth;
   int windowHeight;
   bool quitRequested;
   bool hasQuit;
} TestCtx;

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
                                 const char *interface, uint32_t version)
{
   TestCtx *ctx= (TestCtx*)data;
   int len;

   len= strlen(interface);

   if ( (len==13) && !strncmp(interface, "wl_compositor", len) ) {
      ctx->compositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
      printf("compositor %p\n", ctx->compositor);
   }
}

static void registryHandleGlobalRemove(void *data, 
                                      struct wl_registry *registry,
                                      uint32_t name)
{
}

static const struct wl_registry_listener registryListener = 
{
   registryHandleGlobal,
   registryHandleGlobalRemove
};

static void* clientDispatchThread( void *arg )
{
   TestCtx *ctx= (TestCtx*)arg;
   while( !ctx->quitRequested )
   {
      if ( wl_display_dispatch( ctx->display ) < 0 )
      {
         break;
      }
   }
   ctx->hasQuit= true;
   return NULL;
}

static void client( int bufferIdBase )
{
   EMCTX *emctx= 0;
   bool result;
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   int bufferIdCount= 3;
   int pid;
   bool error= true;
   pthread_t clientDispatchThreadId= 0;
   int rc;

   pid= getpid();

   memset( &testCtx, 0, sizeof(TestCtx) );

   emctx= EMCreateContext();
   if ( !emctx )
   {
      goto exit;
   }

   display= wl_display_connect(NULL);
   if ( !display )
   {
      EMERROR( "wl_display_connect failed" );
      goto exit;
   }
   ctx->display= display;

   registry= wl_display_get_registry(display);
   if ( !registry )
   {
      EMERROR( "wl_display_get_registrty failed" );
      goto exit;
   }

   wl_registry_add_listener(registry, &registryListener, ctx);

   wl_display_roundtrip(display);

   if ( !ctx->compositor )
   {
      EMERROR("Failed to acquire needed compositor items");
      goto exit;
   }

   result= testSetupEGL( &ctx->eglCtx, display );
   if ( !result )
   {
      EMERROR("testSetupEGL failed for client");
      goto exit;
   }

   ctx->surface= wl_compositor_create_surface(ctx->compositor);
   printf("surface=%p\n", ctx->surface);   
   if ( !ctx->surface )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   ctx->windowWidth= WINDOW_WIDTH;
   ctx->windowHeight= WINDOW_HEIGHT;
   
   ctx->wlEglWindow= wl_egl_window_create(ctx->surface, ctx->windowWidth, ctx->windowHeight);
   if ( !ctx->wlEglWindow )
   {
      EMERROR("error: unable to create wl_egl_window");
      goto exit;
   }
   printf("wl_egl_window %p\n", ctx->wlEglWindow);

   EMWLEGLWindowSetBufferRange( ctx->wlEglWindow, bufferIdBase, bufferIdCount );

   ctx->eglCtx.eglSurfaceWindow= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                  ctx->eglCtx.eglConfig,
                                                  (EGLNativeWindowType)ctx->wlEglWindow,
                                                  NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", ctx->eglCtx.eglSurfaceWindow );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow, ctx->eglCtx.eglSurfaceWindow, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapInterval( ctx->eglCtx.eglDisplay, 1 );

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

   wl_display_roundtrip(display);

   ctx->quitRequested= false;
   rc= pthread_create( &clientDispatchThreadId, NULL, clientDispatchThread, ctx );
   if ( rc )
   {
      EMERROR("unable to start client launch thread");
      goto exit;
   }

   for( int i= 0; i < 100; ++i )
   {
      usleep( 17000 );

      eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

      if ( ctx->hasQuit )
      {
         break;
      }

      wl_display_dispatch( display );
   }

   error= false;

exit:
   printf("clientApp: pid %d: end: success: %d\n", pid, !error);

   if ( ctx->eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow );
      ctx->eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }

   if ( ctx->wlEglWindow )
   {
      wl_egl_window_destroy( ctx->wlEglWindow );
      ctx->wlEglWindow= 0;
   }

   if ( ctx->surface )
   {
      wl_surface_destroy( ctx->surface );
      ctx->surface= 0;
   }

   testTermEGL( &ctx->eglCtx );

   if ( ctx->compositor )
   {
      wl_compositor_destroy( ctx->compositor );
      ctx->compositor= 0;
   }

   if ( registry )
   {
      wl_registry_destroy(registry);
      registry= 0;
   }

   ctx->quitRequested= true;

   if ( display )
   {
      wl_display_roundtrip(display);
      wl_display_disconnect(display);
      display= 0;
   }

   if ( clientDispatchThreadId )
   {
      pthread_join( clientDispatchThreadId, NULL );
   }

   if ( error )
   {
      const char *detail= "unknown";
      if ( emctx )
      {
         detail= EMGetError( emctx );
      }
      printf("client error: %s\n", detail );
   }
   if ( emctx )
   {
      EMDestroyContext( emctx );
   }
   return;
}

