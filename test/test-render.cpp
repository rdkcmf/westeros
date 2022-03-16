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
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "test-render.h"
#include "test-egl.h"

#include "westeros-compositor.h"
#include "westeros-gl.h"
#include "wayland-client.h"
#include "wayland-egl.h"

static bool checkForRepeatingSupport( void )
{
   bool result= false;
   WstCompositor *wctx;

   wctx= WstCompositorCreate();
   if ( wctx )
   {
      WstCompositorSetIsRepeater( wctx, true );
      if ( WstCompositorGetIsRepeater( wctx ) )
      {
         result= true;
      }

      WstCompositorDestroy( wctx );
   }

   return result;
}
namespace RenderTests
{

typedef struct _TestCtx
{
   TestEGLCtx eglCtx;  // client / wayland client
   TestEGLCtx eglCtxS; // server / compositor
   struct wl_display *display;
   struct wl_compositor *compositor;
   struct wl_shm *shm;
   struct wl_surface *surface;
   WstGLCtx *glCtx;
   struct wl_egl_window *wlEglWindow;
   void *eglNativeWindow;
   int windowWidth;
   int windowHeight;
   int textureCallbackCount;
   int lastTextureBufferId;
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
   else if ( (len==6) && !strncmp(interface, "wl_shm", len)) {
      ctx->shm= (struct wl_shm*)wl_registry_bind(registry, id, &wl_shm_interface, 1);
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

void textureCreated( EMCTX *ctx, void *userData, int bufferId )
{
   TestCtx *testCtx= (TestCtx*)userData;

   ++testCtx->textureCallbackCount;
   testCtx->lastTextureBufferId= bufferId;
}

} // namespace RenderTests

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

bool testCaseRenderBasicComposition( EMCTX *emctx )
{
   using namespace RenderTests;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   int bufferIdBase= 500;
   int bufferIdCount= 3;
   int expectedBufferId= bufferIdBase;

   EMStart( emctx );

   memset( &testCtx, 0, sizeof(TestCtx) );

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

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   EMSetTextureCreatedCallback( emctx, textureCreated, ctx );

   display= wl_display_connect(displayName);
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

   for( int i= 0; i < 20; ++i )
   {
      usleep( 17000 );

      if ( ctx->lastTextureBufferId != expectedBufferId )
      {
         EMERROR("Unexpected last texture bufferId: expected(%d) actual(%d) iteration %d", expectedBufferId, ctx->lastTextureBufferId, i );
         goto exit;
      }

      eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

      expectedBufferId += 1;
      if ( expectedBufferId >= bufferIdBase+bufferIdCount )
      {
         expectedBufferId= bufferIdBase;
      }

      wl_display_dispatch( display );
   }

   testResult= true;

exit:

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

   if ( display )
   {
      wl_display_roundtrip(display);
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseRenderBasicCompositionEmbedded( EMCTX *emctx )
{
   using namespace RenderTests;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   std::vector<WstRect> rects;
   float matrix[16];
   float alpha= 1.0;
   bool needHolePunch;
   int hints;
   int bufferIdBase= 500;
   int bufferIdCount= 3;
   int expectedBufferId= bufferIdBase;

   EMStart( emctx );

   memset( &testCtx, 0, sizeof(TestCtx) );

   result= testSetupEGL( &ctx->eglCtxS, 0 );
   if ( !result )
   {
      EMERROR("testSetupEGL failed for compositor");
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

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_embedded.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   EMSetTextureCreatedCallback( emctx, textureCreated, ctx );

   display= wl_display_connect(displayName);
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
      EMERROR("testSetupEGL failed");
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

   hints= WstHints_noRotation;
   for( int i= 0; i < 20; ++i )
   {
      usleep( 17000 );

      WstCompositorComposeEmbedded( wctx,
                                    0, // x
                                    0, // y
                                    WINDOW_WIDTH, // width
                                    WINDOW_HEIGHT, // height
                                    matrix,
                                    alpha,
                                    hints,
                                    &needHolePunch,
                                    rects );
                                    
      if ( ctx->lastTextureBufferId != expectedBufferId )
      {
         EMERROR("Unexpected last texture bufferId: expected(%d) actual(%d) iteration %d", expectedBufferId, ctx->lastTextureBufferId, i );
         goto exit;
      }
                                    
      eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

      expectedBufferId += 1;
      if ( expectedBufferId >= bufferIdBase+bufferIdCount )
      {
         expectedBufferId= bufferIdBase;
      }
   }

   testResult= true;

exit:

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

   if ( display )
   {
      wl_display_roundtrip(display);
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctx );

   testTermEGL( &ctx->eglCtxS );

   return testResult;
}

namespace EmbeddedVirtual
{

typedef struct _ClientStatusCtx
{
   int clientStatus;
   int clientPid;
   int detail;
   bool started;
   bool connected;
   bool disconnected;
   bool stoppedNormal;
   bool stoppedAbnormal;
   bool firstFrame;
} ClientStatusCtx;

typedef struct _LaunchCtx
{
   EMCTX *emctx;
   WstCompositor *wctx;
   const char *launchCmd;
   bool launchThreadStarted;
   bool launchError;
} LaunchCtx;

static void clientStatus( WstCompositor *ctx, int status, int clientPID, int detail, void *userData )
{
   using namespace RenderTests;

   ClientStatusCtx *csctx= (ClientStatusCtx*)userData;

   csctx->clientStatus= status;
   csctx->clientPid= clientPID;
   switch( status )
   {
      case WstClient_started:
         csctx->started= true;
         break;
      case WstClient_stoppedNormal:
         csctx->stoppedNormal= true;
         break;
      case WstClient_stoppedAbnormal:
         csctx->stoppedAbnormal= true;
         csctx->detail= detail;
         break;
      case WstClient_connected:
         csctx->connected= true;
         break;
      case WstClient_disconnected:
         csctx->disconnected= true;
         break;
      case WstClient_firstFrame:
         csctx->firstFrame= true;
         break;
   }
}

static void* clientLaunchThread( void *arg )
{
   using namespace RenderTests;

   LaunchCtx *lctx= (LaunchCtx*)arg;
   EMCTX *emctx= lctx->emctx;
   bool result;

   lctx->launchThreadStarted= true;

   result= WstCompositorLaunchClient( lctx->wctx, lctx->launchCmd );
   if ( result == false )
   {
      lctx->launchError= true;
      EMERROR( "WstCompositorLaunchClient failed" );
      goto exit;
   }

exit:
   lctx->launchThreadStarted= false;

   return 0;
}
} // namespace EmbeddedVirtual

bool testCaseRenderBasicCompositionEmbeddedVirtual( EMCTX *emctx )
{
   using namespace RenderTests;
   using namespace EmbeddedVirtual;

   bool testResult= false;
   bool result;
   WstCompositor *master= 0;
   WstCompositor *virt1= 0;
   WstCompositor *virt2= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   pthread_t clientLaunchThreadId1= 0;
   ClientStatusCtx csctx1;
   LaunchCtx lctx1;
   pthread_t clientLaunchThreadId2= 0;
   ClientStatusCtx csctx2;
   LaunchCtx lctx2;
   EGLBoolean b;
   int rc, retryCount;
   int callbackCount;
   std::vector<WstRect> rects;
   float matrix[16];
   float alpha= 1.0;
   bool needHolePunch;
   int hints;
   bool firstFrameVirt1;
   bool firstFrameVirt2;

   EMStart( emctx );

   EMSetTextureCreatedCallback( emctx, textureCreated, ctx );

   memset( &testCtx, 0, sizeof(TestCtx) );

   result= testSetupEGL( &ctx->eglCtxS, 0 );
   if ( !result )
   {
      EMERROR("testSetupEGL failed for compositor");
      goto exit;
   }

   virt1= WstCompositorCreateVirtualEmbedded( NULL );
   if ( !virt1 )
   {
      EMERROR("WstCreateVirtualEmbedded failed for virt1");
      goto exit;
   }

   memset( &csctx1, 0, sizeof(csctx1) );
   result= WstCompositorSetClientStatusCallback( virt1, clientStatus, (void*)&csctx1 );
   if ( !result )
   {
      EMERROR( "WstCompositorSetClientStatusCallback failed" );
      goto exit;
   }

   memset( &lctx1, 0, sizeof(lctx1) );
   lctx1.emctx= emctx;
   lctx1.wctx= virt1;
   lctx1.launchCmd= "./westeros-unittest -x clientApp 600";

   rc= pthread_create( &clientLaunchThreadId1, NULL, clientLaunchThread, &lctx1 );
   if ( rc )
   {
      EMERROR("unable to start client launch thread");
      goto exit;
   }

   virt2= WstCompositorCreateVirtualEmbedded( NULL );
   if ( !virt2 )
   {
      EMERROR("WstCreateVirtualEmbedded failed for virt2");
      goto exit;
   }

   memset( &csctx2, 0, sizeof(csctx2) );
   result= WstCompositorSetClientStatusCallback( virt2, clientStatus, (void*)&csctx2 );
   if ( !result )
   {
      EMERROR( "WstCompositorSetClientStatusCallback failed" );
      goto exit;
   }

   memset( &lctx2, 0, sizeof(lctx2) );
   lctx2.emctx= emctx;
   lctx2.wctx= virt2;
   lctx2.launchCmd= "./westeros-unittest -x clientApp 700";

   rc= pthread_create( &clientLaunchThreadId2, NULL, clientLaunchThread, &lctx2 );
   if ( rc )
   {
      EMERROR("unable to start client launch thread");
      goto exit;
   }

   ctx->glCtx= WstGLInit();
   if ( !ctx->glCtx )
   {
      EMERROR("Unable to create westeros-gl context");
      goto exit;
   }

   ctx->windowWidth= WINDOW_WIDTH;
   ctx->windowHeight= WINDOW_HEIGHT;

   ctx->eglNativeWindow= WstGLCreateNativeWindow( ctx->glCtx, 0, 0, ctx->windowWidth, ctx->windowHeight );
   if ( !ctx->eglNativeWindow )
   {
      EMERROR("error: unable to create egl native window");
      goto exit;
   }
   printf("eglNativeWindow %p\n", ctx->eglNativeWindow);

   ctx->eglCtxS.eglSurfaceWindow= eglCreateWindowSurface( ctx->eglCtxS.eglDisplay,
                                                  ctx->eglCtxS.eglConfig,
                                                  (EGLNativeWindowType)ctx->eglNativeWindow,
                                                  NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", ctx->eglCtxS.eglSurfaceWindow );

   b= eglMakeCurrent( ctx->eglCtxS.eglDisplay, ctx->eglCtxS.eglSurfaceWindow, ctx->eglCtxS.eglSurfaceWindow, ctx->eglCtxS.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapInterval( ctx->eglCtx.eglDisplay, 1 );

   retryCount= 0;
   while( !csctx1.connected && !csctx2.connected )
   {
      usleep( 300000 );
      ++retryCount;
      if ( retryCount > 50 )
      {
         EMERROR("Client failed to connect");
         goto exit;
      }
   }

   firstFrameVirt1= false;
   firstFrameVirt2= false;
   callbackCount=-1;
   hints= WstHints_noRotation;
   for( int i= 0; i < 20; ++i )
   {
      usleep( 32000 );

      csctx1.firstFrame= false;
      csctx2.firstFrame= false;
      WstCompositorComposeEmbedded( virt1,
                                    0, // x
                                    0, // y
                                    WINDOW_WIDTH, // width
                                    WINDOW_HEIGHT, // height
                                    matrix,
                                    alpha,
                                    hints,
                                    &needHolePunch,
                                    rects );
      if ( (callbackCount != ctx->textureCallbackCount) &&
           ((ctx->lastTextureBufferId < 600) || (ctx->lastTextureBufferId > 603)) )
      {
         EMERROR("Unexpected last texture bufferId: expected(600-603) actual(%d) iteration %d", ctx->lastTextureBufferId, i );
         goto exit;
      }
      if ( (callbackCount != ctx->textureCallbackCount) &&
           (
             (!firstFrameVirt1 && !csctx1.firstFrame) ||
             (!firstFrameVirt2 && csctx2.firstFrame)
           ) )
      {
         EMERROR("Unexpected first frame: firstFrame1 %d firstFrame2 %d firstFrameVirt1 %d firstFrameVirt2 %d",
                  csctx1.firstFrame, csctx2.firstFrame, firstFrameVirt1, firstFrameVirt2 );
         goto exit;
      }
      if ( (callbackCount != ctx->textureCallbackCount) && csctx1.firstFrame  )
      {
         firstFrameVirt1= csctx1.firstFrame;
      }

      callbackCount= ctx->textureCallbackCount;

      csctx1.firstFrame= false;
      csctx2.firstFrame= false;
      WstCompositorComposeEmbedded( virt2,
                                    0, // x
                                    0, // y
                                    WINDOW_WIDTH, // width
                                    WINDOW_HEIGHT, // height
                                    matrix,
                                    alpha,
                                    hints,
                                    &needHolePunch,
                                    rects );
      if ( (callbackCount != ctx->textureCallbackCount) &&
           ((ctx->lastTextureBufferId < 700) || (ctx->lastTextureBufferId > 703)) )
      {
         EMERROR("Unexpected last texture bufferId: expected(700-703) actual(%d) iteration %d", ctx->lastTextureBufferId, i );
         goto exit;
      }
      if ( (callbackCount != ctx->textureCallbackCount) &&
           (
             (!firstFrameVirt1 && csctx1.firstFrame) ||
             (!firstFrameVirt2 && !csctx2.firstFrame)
           ) )
      {
         EMERROR("Unexpected first frame: firstFrame1 %d firstFrame2 %d firstFrameVirt1 %d firstFrameVirt2 %d",
                  csctx1.firstFrame, csctx2.firstFrame, firstFrameVirt1, firstFrameVirt2 );
         goto exit;
      }
      if ( (callbackCount != ctx->textureCallbackCount) && csctx2.firstFrame  )
      {
         firstFrameVirt2= csctx2.firstFrame;
      }

      callbackCount= ctx->textureCallbackCount;

      eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);
   }

   master= WstCompositorGetMasterEmbedded();
   if ( !master )
   {
      EMERROR("WstCompositorGetMasterEmbedded failed");
      goto exit;
   }

   if ( clientLaunchThreadId1 && csctx1.clientPid )
   {
      kill( csctx1.clientPid, 6 );
      pthread_join( clientLaunchThreadId1, NULL );
      clientLaunchThreadId1= 0;
   }
   if ( clientLaunchThreadId2 && csctx2.clientPid )
   {
      kill( csctx2.clientPid, 6 );
      pthread_join( clientLaunchThreadId2, NULL );
      clientLaunchThreadId2= 0;
   }

   WstCompositorDestroy( virt1 );
   WstCompositorDestroy( virt2 );
   WstCompositorDestroy( master );

   retryCount= 0;
   while( !csctx1.disconnected || !csctx2.disconnected )
   {
      usleep( 300000 );
      ++retryCount;
      if ( retryCount > 50 )
      {
         EMERROR("Client failed to disconnect");
         goto exit;
      }
   }

   retryCount= 0;
   while( !(csctx1.stoppedNormal || csctx1.stoppedAbnormal) ||
          !(csctx2.stoppedNormal || csctx2.stoppedAbnormal) )
   {
      usleep( 300000 );
      ++retryCount;
      if ( retryCount > 50 )
      {
         EMERROR("Client failed to stop");
         goto exit;
      }
   }

   testResult= true;

exit:

   if ( ctx->eglCtxS.eglSurfaceWindow )
   {
      eglDestroySurface( ctx->eglCtxS.eglDisplay, ctx->eglCtxS.eglSurfaceWindow );
      ctx->eglCtxS.eglSurfaceWindow= EGL_NO_SURFACE;
   }

   if ( ctx->eglNativeWindow )
   {
      WstGLDestroyNativeWindow( ctx->glCtx, ctx->eglNativeWindow );
      ctx->eglNativeWindow= 0;
   }

   if ( ctx->glCtx )
   {
      WstGLTerm( ctx->glCtx );
      ctx->glCtx= 0;
   }

   testTermEGL( &ctx->eglCtxS );

   return testResult;
}

bool testCaseRenderBasicCompositionNested( EMCTX *emctx )
{
   using namespace RenderTests;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   WstCompositor *wctxNested= 0;
   const char *displayName= "test0";
   const char *displayNameNested= "nested0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   int bufferIdBase= 1;
   int bufferIdCount= 3;
   int expectedBufferId= 1;

   EMStart( emctx );

   memset( &testCtx, 0, sizeof(TestCtx) );

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

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }


   wctxNested= WstCompositorCreate();
   if ( !wctxNested )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctxNested, displayNameNested );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctxNested, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetIsNested( wctxNested, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctxNested, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorStart( wctxNested );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   EMSetTextureCreatedCallback( emctx, textureCreated, ctx );

   display= wl_display_connect(displayNameNested);
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

   ctx->lastTextureBufferId= -1;

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

   while ( ctx->lastTextureBufferId == -1 )
   {
      wl_display_roundtrip(display);
   }

   for( int i= 0; i < 20; ++i )
   {
      if ( ctx->lastTextureBufferId != expectedBufferId )
      {
         EMERROR("Unexpected last texture bufferId: expected(%d) actual(%d) iteration %d", expectedBufferId, ctx->lastTextureBufferId, i );
         goto exit;
      }

      eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

      usleep( 16666 );

      expectedBufferId += 1;
      if ( expectedBufferId >= bufferIdCount+1 )
      {
         expectedBufferId= 1;
      }

      wl_display_dispatch( display );
   }

   testResult= true;

exit:

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

   if ( display )
   {
      wl_display_roundtrip(display);
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctxNested );

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseRenderBasicCompositionRepeating( EMCTX *emctx )
{
   using namespace RenderTests;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   WstCompositor *wctxRepeater= 0;
   const char *displayName= "test0";
   const char *displayNameNested= "nested0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   int bufferIdBase= 500;
   int bufferIdCount= 3;
   int expectedBufferId;

   EMStart( emctx );

   memset( &testCtx, 0, sizeof(TestCtx) );

   if ( !checkForRepeatingSupport() )
   {
      // If we cannot repeat, expect default buffer id range
      bufferIdBase= 1;
   }
   expectedBufferId= bufferIdBase;

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

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }


   wctxRepeater= WstCompositorCreate();
   if ( !wctxRepeater )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctxRepeater, displayNameNested );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctxRepeater, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctxRepeater, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorStart( wctxRepeater );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   EMSetTextureCreatedCallback( emctx, textureCreated, ctx );

   display= wl_display_connect(displayNameNested);
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

   for( int i= 0; i < 20; ++i )
   {
      usleep( 17000 );

      if ( ctx->lastTextureBufferId != expectedBufferId )
      {
         EMERROR("Unexpected last texture bufferId: expected(%d) actual(%d) iteration %d", expectedBufferId, ctx->lastTextureBufferId, i );
         goto exit;
      }

      eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

      expectedBufferId += 1;
      if ( expectedBufferId >= bufferIdBase+bufferIdCount )
      {
         expectedBufferId= bufferIdBase;
      }

      wl_display_dispatch( display );
   }

   testResult= true;

exit:

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

   if ( display )
   {
      wl_display_roundtrip(display);
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctxRepeater );

   WstCompositorDestroy( wctx );

   return testResult;
}

static void terminatedCallback( WstCompositor *ctx, void *userData )
{
   bool *terminated= (bool*)userData;

   *terminated= true;
}

bool testCaseRenderShmRepeater( EMCTX *emctx )
{
   using namespace RenderTests;

   bool testResult= false;
   bool result;
   const char *displayName= "display0";
   const char *displayName2= "display1";
   WstCompositor *wctx= 0;
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   bool repeatingSupported;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   unsigned char *imgData= 0;
   int imgWidth, imgHeight;
   int imgDataSize;
   char filename[32];
   int fd= -1;
   int lenDidWrite;
   void *data= 0;
   struct wl_shm_pool *shmPool= 0;
   struct wl_buffer *buffer= 0;
   int expectedBufferId;
   int pid;

   EMStart( emctx );

   memset( &testCtx, 0, sizeof(TestCtx) );

   repeatingSupported= checkForRepeatingSupport();

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   pid= fork();
   if ( pid == 0 )
   {
      EMCTX *emctx2= 0;
      WstCompositor *wctx2= 0;
      bool terminated= false;

      emctx2= EMCreateContext();
      if ( !emctx2 )
      {
         goto repeater_exit;
      }

      wctx2= WstCompositorCreate();
      if ( !wctx2 )
      {
         EMERROR( "WstCompositorCreate failed" );
         goto repeater_exit;
      }

      result= WstCompositorSetDisplayName( wctx2, displayName2 );
      if ( !result )
      {
         EMERROR( "WstCompositorSetDisplayName failed" );
         goto repeater_exit;
      }

      result= WstCompositorSetIsRepeater( wctx2, true );
      if ( !result )
      {
         EMERROR( "WstCompositorSetIsRepeater failed" );
         goto repeater_exit;
      }

      result= WstCompositorSetNestedDisplayName( wctx2, displayName );
      if ( !result )
      {
         EMERROR( "WstCompositorSetNestedDisplayName failed" );
         goto repeater_exit;
      }

      result= WstCompositorSetTerminatedCallback( wctx2, terminatedCallback, &terminated );
      if ( result == false )
      {
         EMERROR( "WstCompositorSetTerminatedCallback failed" );
         goto repeater_exit;
      }

      result= WstCompositorStart( wctx2 );
      if ( result == false )
      {
         EMERROR( "WstCompositorStart failed" );
         goto repeater_exit;
      }

      while( !terminated )
      {
         usleep( 10000 );
      }
      printf("repeater ending\n");

   repeater_exit:
      if ( wctx2 )
      {
         WstCompositorDestroy( wctx2 );
      }
      if ( emctx2 )
      {
         EMDestroyContext( emctx2 );
      }
      printf("repeater ended\n");
      exit(0);
   }
   else if ( pid == -1 )
   {
      EMERROR( "Failed to fork to start repeater" );
      goto exit;
   }
   else
   {
      // wait for repeater to start
      usleep( 100000 );
   }

   EMSetTextureCreatedCallback( emctx, textureCreated, ctx );

   display= wl_display_connect(displayName2);
   if ( !display )
   {
      int retries= 10;

      while( retries-- > 0 )
      {
         display= wl_display_connect(displayName2);
         if ( display )
         {
            break;
         }
         usleep( 100000 );
      }
      if ( !display )
      {
         EMERROR( "wl_display_connect failed" );
         goto exit;
      }
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

   if ( !ctx->compositor || !ctx->shm )
   {
      EMERROR("Failed to acquire needed compositor items");
      goto exit;
   }

   ctx->surface= wl_compositor_create_surface(ctx->compositor);
   printf("surface=%p\n", ctx->surface);
   if ( !ctx->surface )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   wl_display_roundtrip(display);

   imgWidth= 32;
   imgHeight= 32;
   imgDataSize= imgWidth*imgHeight*4;

   imgData= (unsigned char*)calloc( 1, imgDataSize );
   if ( !imgData )
   {
      EMERROR("No memory for image data");
      goto exit;
   }

   strcpy( filename, "/tmp/westeros-XXXXXX" );
   fd= mkostemp( filename, O_CLOEXEC );
   if ( fd < 0 )
   {
      EMERROR("Unable to create temp file");
      goto exit;
   }

   lenDidWrite= write( fd, imgData, imgDataSize );
   if ( lenDidWrite != imgDataSize )
   {
      EMERROR("Unable to write image data to temp file");
      goto exit;
   }

   data= mmap(NULL, imgDataSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if ( data == MAP_FAILED )
   {
      EMERROR("Unable to mmap image data");
      goto exit;
   }

   memcpy( data, imgData, imgDataSize );

   shmPool= wl_shm_create_pool(ctx->shm, fd, imgDataSize);
   if ( shmPool == 0 )
   {
      EMERROR("Unable to create shm pool");
      goto exit;
   }
   wl_display_roundtrip(display);

   buffer= wl_shm_pool_create_buffer(shmPool,
                                     0, //offset
                                     imgWidth,
                                     imgHeight,
                                     imgWidth*4, //stride
                                     WL_SHM_FORMAT_ARGB8888 );
   wl_display_roundtrip(display);
   if ( !buffer )
   {
      EMERROR("Unable to create shm buffer");
      goto exit;
   }

   expectedBufferId= repeatingSupported ? (imgWidth<<16)|imgHeight : 2;

   wl_surface_attach( ctx->surface, buffer, 0, 0 );
   wl_surface_damage( ctx->surface, 0, 0, imgWidth, imgHeight);
   wl_surface_commit( ctx->surface );
   wl_display_roundtrip(display);

   usleep( 17000 );

   if ( ctx->lastTextureBufferId != expectedBufferId )
   {
      EMERROR("Unexpected last texture bufferId: expected(%d) actual(%d)", expectedBufferId, ctx->lastTextureBufferId );
      goto exit;
   }


   testResult= true;

exit:

   if ( ctx->surface )
   {
      wl_surface_destroy( ctx->surface );
      ctx->surface= 0;
   }

   if ( shmPool )
   {
      wl_shm_pool_destroy( shmPool);
   }

   if ( ctx->shm )
   {
      wl_shm_destroy( ctx->shm );
      ctx->shm= 0;
   }

   if ( registry )
   {
      wl_registry_destroy(registry);
      registry= 0;
   }

   if ( ctx->compositor )
   {
      wl_compositor_destroy( ctx->compositor );
      ctx->compositor= 0;
   }

   if ( display )
   {
      wl_display_roundtrip(display);
      wl_display_disconnect(display);
      display= 0;
   }

   if ( data )
   {
      munmap( data, imgDataSize );
   }

   if ( imgData )
   {
      free( imgData );
   }

   if ( fd != -1 )
   {
      close( fd );
      remove( filename );
   }

   if ( wctx )
   {
      WstCompositorDestroy( wctx );
   }

   return testResult;
}

bool testCaseRenderWaylandThreading( EMCTX *emctx )
{
   using namespace RenderTests;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;

   EMStart( emctx );

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

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   memset( &testCtx, 0, sizeof(TestCtx) );

   display= wl_display_connect(displayName);
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
      EMERROR("testSetupEGL failed");
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

   for( int i= 0; i < 20; ++i )
   {
      usleep( 17000 );

      eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

      wl_display_dispatch( display );
   }
   
   if ( EMGetWaylandThreadingIssue( emctx ) )
   {
      EMERROR( "Wayland threading issue: compositor calling wl_resource_post_event_array from multiple threads") ;
      goto exit;
   }

   testResult= true;

exit:

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

   if ( display )
   {
      wl_display_roundtrip(display);
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseRenderWaylandThreadingEmbedded( EMCTX *emctx )
{
   using namespace RenderTests;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;
   std::vector<WstRect> rects;
   float matrix[16];
   float alpha= 1.0;
   bool needHolePunch;
   int hints;

   EMStart( emctx );

   memset( &testCtx, 0, sizeof(TestCtx) );

   result= testSetupEGL( &ctx->eglCtxS, 0 );
   if ( !result )
   {
      EMERROR("testSetupEGL failed for compositor");
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

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_embedded.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   display= wl_display_connect(displayName);
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

   ctx->glCtx= WstGLInit();
   if ( !ctx->glCtx )
   {
      EMERROR("Unable to create westeros-gl context");
      goto exit;
   }

   ctx->eglNativeWindow= WstGLCreateNativeWindow( ctx->glCtx, 0, 0, ctx->windowWidth, ctx->windowHeight );   
   if ( !ctx->eglNativeWindow )
   {
      EMERROR("error: unable to create egl native window");
      goto exit;
   }
   printf("eglNativeWindow %p\n", ctx->eglNativeWindow);

   ctx->eglCtx.eglSurfaceWindow= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                  ctx->eglCtx.eglConfig,
                                                  (EGLNativeWindowType)ctx->eglNativeWindow,
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

   hints= WstHints_noRotation;
   for( int i= 0; i < 20; ++i )
   {
      usleep( 17000 );

      eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

      WstCompositorComposeEmbedded( wctx,
                                    0, // x
                                    0, // y
                                    WINDOW_WIDTH, // width
                                    WINDOW_HEIGHT, // height
                                    matrix,
                                    alpha,
                                    hints,
                                    &needHolePunch,
                                    rects );
   }
   
   if ( EMGetWaylandThreadingIssue( emctx ) )
   {
      EMERROR( "Wayland threading issue: compositor calling wl_resource_post_event_array from multiple threads") ;
      goto exit;
   }

   testResult= true;

exit:

   if ( ctx->eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow );
      ctx->eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }

   if ( ctx->eglNativeWindow )
   {
      WstGLDestroyNativeWindow( ctx->glCtx, ctx->eglNativeWindow );
      ctx->eglNativeWindow= 0;
   }

   if ( ctx->glCtx )
   {
      WstGLTerm( ctx->glCtx );
      ctx->glCtx= 0;
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

   if ( display )
   {
      wl_display_roundtrip(display);
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctx );

   testTermEGL( &ctx->eglCtxS );

   return testResult;
}

namespace EmbeddedRepeater
{
typedef struct _ClientStatusCtx
{
   int clientStatus;
   int clientPid;
   int detail;
   bool started;
   bool connected;
   bool disconnected;
   bool stoppedNormal;
   bool stoppedAbnormal;
   bool firstFrame;
} ClientStatusCtx;

static void clientStatus( WstCompositor *ctx, int status, int clientPID, int detail, void *userData )
{
   ClientStatusCtx *csctx= (ClientStatusCtx*)userData;

   csctx->clientStatus= status;
   csctx->clientPid= clientPID;
   switch( status )
   {
      case WstClient_started:
         csctx->started= true;
         break;
      case WstClient_stoppedNormal:
         csctx->stoppedNormal= true;
         break;
      case WstClient_stoppedAbnormal:
         csctx->stoppedAbnormal= true;
         csctx->detail= detail;
         break;
      case WstClient_connected:
         csctx->connected= true;
         break;
      case WstClient_disconnected:
         csctx->disconnected= true;
         break;
      case WstClient_firstFrame:
         csctx->firstFrame= true;
         break;
   }
}

typedef struct _LaunchCtx
{
   EMCTX *emctx;
   WstCompositor *wctx;
   const char *launchCmd;
   bool launchThreadStarted;
   bool launchError;
} LaunchCtx;

static void* clientLaunchThread( void *arg )
{
   LaunchCtx *lctx= (LaunchCtx*)arg;
   EMCTX *emctx= lctx->emctx;
   bool result;

   lctx->launchThreadStarted= true;

   result= WstCompositorLaunchClient( lctx->wctx, lctx->launchCmd );
   if ( result == false )
   {
      lctx->launchError= true;
      EMERROR( "WstCompositorLaunchClient failed" );
      goto exit;
   }

exit:
   lctx->launchThreadStarted= false;

   return 0;
}

typedef struct _TestCtx
{
   TestEGLCtx eglCtx;
   struct wl_display *display;
   struct wl_compositor *compositor;
   struct wl_surface *surface;
   WstGLCtx *glCtx;
   struct wl_egl_window *wlEglWindow;
   void *eglNativeWindow;
   int windowWidth;
   int windowHeight;
   int lastTextureBufferId;
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

void textureCreated( EMCTX *ctx, void *userData, int bufferId )
{
   TestCtx *testCtx= (TestCtx*)userData;

   testCtx->lastTextureBufferId= bufferId;
}

} // namespace EmbeddedRepeater

bool testCaseRenderBasicCompositionEmbeddedRepeater( EMCTX *emctx )
{
   using namespace EmbeddedRepeater;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   int rc;
   EGLBoolean b;
   pthread_t clientLaunchThreadId= 0;
   ClientStatusCtx csctx;
   LaunchCtx lctx;
   std::vector<WstRect> rects;
   float matrix[16];
   float alpha= 1.0;
   bool needHolePunch;
   int hints;
   int bufferIdBase= 1700;
   int bufferIdCount= 3;
   int retryCount;

   EMStart( emctx );

   memset( &testCtx, 0, sizeof(TestCtx) );

   if ( !checkForRepeatingSupport() )
   {
      // If we cannot repeat, expect default buffer id range
      bufferIdBase= 1;
   }

   result= testSetupEGL( &ctx->eglCtx, 0 );
   if ( !result )
   {
      EMERROR("testSetupEGL failed for compositor");
      goto exit;
   }
   ctx->glCtx= WstGLInit();
   if ( !ctx->glCtx )
   {
      EMERROR("Unable to create westeros-gl context");
      goto exit;
   }

   ctx->windowWidth= WINDOW_WIDTH;
   ctx->windowHeight= WINDOW_HEIGHT;

   ctx->eglNativeWindow= WstGLCreateNativeWindow( ctx->glCtx, 0, 0, ctx->windowWidth, ctx->windowHeight );
   if ( !ctx->eglNativeWindow )
   {
      EMERROR("error: unable to create egl native window");
      goto exit;
   }
   printf("eglNativeWindow %p\n", ctx->eglNativeWindow);

   ctx->eglCtx.eglSurfaceWindow= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                  ctx->eglCtx.eglConfig,
                                                  (EGLNativeWindowType)ctx->eglNativeWindow,
                                                  NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", ctx->eglCtx.eglSurfaceWindow );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow, ctx->eglCtx.eglSurfaceWindow, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapInterval( ctx->eglCtx.eglDisplay, 1 );

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

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_embedded.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   EMSetTextureCreatedCallback( emctx, textureCreated, ctx );

   hints= WstHints_noRotation;
   WstCompositorComposeEmbedded( wctx,
                                 0, // x
                                 0, // y
                                 WINDOW_WIDTH, // width
                                 WINDOW_HEIGHT, // height
                                 matrix,
                                 alpha,
                                 hints,
                                 &needHolePunch,
                                 rects );

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

   memset( &csctx, 0, sizeof(csctx) );
   result= WstCompositorSetClientStatusCallback( wctx, clientStatus, (void*)&csctx );
   if ( !result )
   {
      EMERROR( "WstCompositorSetClientStatusCallback failed" );
      goto exit;
   }

   memset( &lctx, 0, sizeof(lctx) );
   lctx.emctx= emctx;
   lctx.wctx= wctx;
   lctx.launchCmd= "./westeros-unittest -x repeaterApp repeater0 1700";

   rc= pthread_create( &clientLaunchThreadId, NULL, clientLaunchThread, &lctx );
   if ( rc )
   {
      EMERROR("unable to start client launch thread");
      goto exit;
   }

   retryCount= 0;
   while( !csctx.connected )
   {
      usleep( 300000 );
      ++retryCount;
      if ( retryCount > 50 )
      {
         EMERROR("Repeater client failed to connect");
         goto exit;
      }
   }

   for( int i= 0; i < 20; ++i )
   {
      WstCompositorComposeEmbedded( wctx,
                                    0, // x
                                    0, // y
                                    WINDOW_WIDTH, // width
                                    WINDOW_HEIGHT, // height
                                    matrix,
                                    alpha,
                                    hints,
                                    &needHolePunch,
                                    rects );

      if ( (ctx->lastTextureBufferId < bufferIdBase) || (ctx->lastTextureBufferId >= bufferIdBase+bufferIdCount) )
      {
         EMERROR("Unexpected last pushed bufferId: expected(%d-%d) actual(%d) iteration %d", bufferIdBase, bufferIdBase+bufferIdCount-1, ctx->lastTextureBufferId, i );
         goto exit;
      }

      usleep( 17000 );
   }

   if ( !csctx.firstFrame )
   {
      EMERROR("Failed to be notified of client first frame");
      goto exit;
   }

   testResult= true;

exit:

   WstCompositorStop( wctx );

   if ( clientLaunchThreadId )
   {
      pthread_join( clientLaunchThreadId, NULL );
      clientLaunchThreadId= 0;
   }

   WstCompositorDestroy( wctx );

   if ( ctx->eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow );
      ctx->eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }

   if ( ctx->eglNativeWindow )
   {
      WstGLDestroyNativeWindow( ctx->glCtx, ctx->eglNativeWindow );
      ctx->eglNativeWindow= 0;
   }

   if ( ctx->glCtx )
   {
      WstGLTerm( ctx->glCtx );
      ctx->glCtx= 0;
   }

   testTermEGL( &ctx->eglCtx );

   return testResult;
}

