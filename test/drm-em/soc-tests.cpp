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
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>

#include <glib.h>
#include <gst/gst.h>

#include <linux/videodev2.h>

#include "../westeros-ut-em.h"

#include "../test-egl.h"
#include "soc-tests.h"
#include "soc-video-src.h"

#include "essos.h"

#include "wayland-client.h"
#include "wayland-egl.h"

#include "simpleshell-client-protocol.h"

#include "westeros-gl.h"
#include "westeros-compositor.h"
#include "westeros-render.h"

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

#define INTERVAL_200_MS (200000)

static bool testCaseSocSinkInit( EMCTX *emctx );
static bool testCaseSocSinkBasicPipeline( EMCTX *ctx );
static bool testCaseSocSinkFirstFrameSignal( EMCTX *emctx );
static bool testCaseSocSinkUnderflowSignal( EMCTX *ctx );
static bool testCaseSocSinkElementRecycle( EMCTX *ctx );
static bool testCaseSocSinkBasicPositionReporting( EMCTX *ctx );
static bool testCaseSocSinkBasicPositionReportingProperty( EMCTX *emctx );
static bool testCaseSocSinkBasicPauseResume( EMCTX *ctx );
static bool testCaseSocSinkBasicSeek( EMCTX *ctx );
static bool testCaseSocSinkBasicSeekZeroBased( EMCTX *ctx );
static bool testCaseSocSinkFrameAdvance( EMCTX *ctx );
static bool testCaseSocSinkInitWithCompositor( EMCTX *emctx );
static bool testCaseSocSinkBasicPipelineWithCompositor( EMCTX *emctx );
static bool testCaseSocSinkFirstFrameSignalWithCompositor( EMCTX *ctx );
static bool testCaseSocSinkUnderflowSignalWithCompositor( EMCTX *ctx );
static bool testCaseSocSinkElementRecycleWithCompositor( EMCTX *ctx );
static bool testCaseSocSinkBasicPositionReportingWithCompositor( EMCTX *ctx );
static bool testCaseSocSinkBasicPauseResumeWithCompositor( EMCTX *ctx );
static bool testCaseSocSinkBasicSeekWithCompositor( EMCTX *ctx );
static bool testCaseSocSinkBasicSeekZeroBasedWithCompositor( EMCTX *ctx );
static bool testCaseSocSinkFrameAdvanceWithCompositor( EMCTX *ctx );
static bool testCaseSocSinkBasicPipelineGfx( EMCTX *ctx );
static bool testCaseSocEssosDualMediaPlayback( EMCTX *emctx );
static bool testCaseSocSinkVideoPosition( EMCTX *emctx );

TESTCASE socTests[]=
{
   { "testSocSinkInit",
     "Test loading westerossink",
     testCaseSocSinkInit
   },
   { "testSocSinkBasicPipeline",
     "Test creating a basic pipeline with westerossink",
     testCaseSocSinkBasicPipeline
   },
   { "testSocSinkFirstFrameSignal",
     "Test first frame signal",
     testCaseSocSinkFirstFrameSignal
   },
   { "testSocSinkUnderflowSignal",
     "Test underflow signal",
     testCaseSocSinkUnderflowSignal
   },
   { "testSocSinkElementRecycle",
     "Test recycling a westerossink element",
     testCaseSocSinkElementRecycle
   },
   { "testSocSinkBasicPositionReporting",
     "Test basic position reporting from a pipeline",
     testCaseSocSinkBasicPositionReporting
   },
   { "testSocSinkBasicPositionReportingProperty",
     "Test basic position reporting from a pipeline via property",
     testCaseSocSinkBasicPositionReportingProperty
   },
   { "testSocSinkBasicPauseResume",
     "Test basic pause and resume",
     testCaseSocSinkBasicPauseResume
   },
   { "testSocSinkBasicSeek",
     "Test basic seek operation",
     testCaseSocSinkBasicSeek
   },
   { "testSocSinkBasicSeekZeroBased",
     "Test basic seek operation with zero based segments",
     testCaseSocSinkBasicSeekZeroBased
   },
   { "testSocSinkFrameAdvance",
     "Test decode with frame advance",
     testCaseSocSinkFrameAdvance
   },
   { "testSocSinkInitWithCompositor",
     "Test loading westerossink with a compositor",
     testCaseSocSinkInitWithCompositor
   },
   { "testSocSinkBasicPipelineWithCompositor",
     "Test creating a basic pipeline with westerossink with a compositor",
     testCaseSocSinkBasicPipelineWithCompositor
   },
   { "testSocSinkFirstFrameSignalWithCompositor",
     "Test first frame signal with a compositor",
     testCaseSocSinkFirstFrameSignalWithCompositor
   },
   { "testSocSinkUnderflowSignalWithCompositor",
     "Test underflow signal with a compositor",
     testCaseSocSinkUnderflowSignalWithCompositor
   },
   { "testSocSinkElementRecycleWithCompositor",
     "Test recycling a westerossink element with a compositor",
     testCaseSocSinkElementRecycleWithCompositor
   },
   { "testSocSinkBasicPositionReportingWithCompositor",
     "Test basic position reporting from a pipeline with a compositor",
     testCaseSocSinkBasicPositionReportingWithCompositor
   },
   { "testSocSinkBasicPauseResumeWithCompositor",
     "Test basic pause and resume with a compositor",
     testCaseSocSinkBasicPauseResumeWithCompositor
   },
   { "testSocSinkBasicSeekWithCompositor",
     "Test basic seek operation with a compositor",
     testCaseSocSinkBasicSeekWithCompositor
   },
   { "testSocSinkBasicSeekZeroBasedWithCompositor",
     "Test basic seek operation with zero based segments with a compositor",
     testCaseSocSinkBasicSeekZeroBasedWithCompositor
   },
   { "testSocSinkFrameAdvanceWithCompositor",
     "Test decode with frame advance with a compositor",
     testCaseSocSinkFrameAdvanceWithCompositor
   },
   { "testSocSinkBasicPipelineGfx",
     "Test creating a basic pipeline with westerossink using graphics path",
     testCaseSocSinkBasicPipelineGfx
   },
   { "testSocEssosDualMediaPlayback",
     "Test dual media playback with Essos",
     testCaseSocEssosDualMediaPlayback
   },
   { "testSocSinkVideoPosition",
     "Test westerossink video positioning",
     testCaseSocSinkVideoPosition
   },
   {
     "", "", (TESTCASEFUNC)0
   }
};

TESTCASE getSocTest( int index )
{
   return socTests[index];
}

static gint64 getSegmentStart( EMSimpleVideoDecoder *dec, gint64 time )
{
   bool startAtZero= EMSimpleVideoDecoderGetSegmentsStartAtZero( dec );

   if ( startAtZero )
   {
      return 0;
   }
   else
   {
      return time;
   }
}

static bool testCaseSocSinkInit( EMCTX *emctx )
{
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   GstElement *sink= 0;

   gst_init( &argc, &argv );

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_element_set_state( sink, GST_STATE_PAUSED );

   gst_element_set_state( sink, GST_STATE_NULL );

   testResult= true;

exit:
   if ( sink )
   {
      gst_object_unref( sink );
   }

   return testResult;
}

static bool testCaseSocSinkBasicPipeline( EMCTX *emctx )
{
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   bool result;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   EGLBoolean b;
   TestEGLCtx eglCtx;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( &eglCtx, 0, sizeof(TestEGLCtx) );

   if ( getenv("WAYLAND_DISPLAY") == 0 )
   {
      EMStart( emctx );

      result= testSetupEGL( &eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      glCtx= WstGLInit();
      if ( !glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
      if ( !nativeWindow )
      {
         EMERROR("Unable to create westeros-gl native window");
         goto exit;
      }

      eglCtx.eglSurfaceWindow= eglCreateWindowSurface( eglCtx.eglDisplay,
                                                     eglCtx.eglConfig,
                                                     (EGLNativeWindowType)nativeWindow,
                                                     NULL );
      printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", eglCtx.eglSurfaceWindow );

      b= eglMakeCurrent( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow, eglCtx.eglSurfaceWindow, eglCtx.eglContext );
      if ( !b )
      {
         EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
         goto exit;
      }

      eglSwapInterval( eglCtx.eglDisplay, 1 );
      eglSwapBuffers(eglCtx.eglDisplay, eglCtx.eglSurfaceWindow);
      usleep( 34000 );
   }

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSetVideoCodec( emctx, V4L2_PIX_FMT_H264 );
   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );
   EMSimpleVideoDecoderSetFrameRate( videoDecoder, 60.0 );

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   // Allow pipeline to run briefly
   usleep( 2000000 );

   gst_element_set_state( pipeline, GST_STATE_NULL );

   testResult= true;

exit:
   if ( pipeline )
   {
      gst_object_unref( pipeline );
   }
   if ( eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow );
      eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }
   testTermEGL( &eglCtx );

   return testResult;
}

static void firstFrameCallback(GstElement *sink, guint size, void *context, gpointer data)
{
   bool *gotSignal= (bool*)data;

   g_print("received first frame signal\n");
   *gotSignal= true;
}

static bool testCaseSocSinkFirstFrameSignal( EMCTX *emctx )
{
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   bool result;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   bool receivedSignal;
   EGLBoolean b;
   TestEGLCtx eglCtx;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( &eglCtx, 0, sizeof(TestEGLCtx) );

   if ( getenv("WAYLAND_DISPLAY") == 0 )
   {
      EMStart( emctx );

      result= testSetupEGL( &eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      glCtx= WstGLInit();
      if ( !glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
      if ( !nativeWindow )
      {
         EMERROR("Unable to create westeros-gl native window");
         goto exit;
      }

      eglCtx.eglSurfaceWindow= eglCreateWindowSurface( eglCtx.eglDisplay,
                                                     eglCtx.eglConfig,
                                                     (EGLNativeWindowType)nativeWindow,
                                                     NULL );
      printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", eglCtx.eglSurfaceWindow );

      b= eglMakeCurrent( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow, eglCtx.eglSurfaceWindow, eglCtx.eglContext );
      if ( !b )
      {
         EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
         goto exit;
      }

      eglSwapInterval( eglCtx.eglDisplay, 1 );
      eglSwapBuffers(eglCtx.eglDisplay, eglCtx.eglSurfaceWindow);
      usleep( 34000 );
   }

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   g_signal_connect( sink, "first-video-frame-callback", G_CALLBACK(firstFrameCallback), &receivedSignal);

   receivedSignal= false;

   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   // Allow pipeline to run briefly
   usleep( 200000 );

   gst_element_set_state( pipeline, GST_STATE_NULL );

   if ( !receivedSignal )
   {
      EMERROR("Failed to receive first video frame signal");
      goto exit;
   }

   testResult= true;

exit:
   if ( pipeline )
   {
      gst_object_unref( pipeline );
   }
   if ( eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow );
      eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }
   testTermEGL( &eglCtx );

   return testResult;
}

static void underflowCallback(GstElement *sink, guint size, void *context, gpointer data)
{
   int *gotSignalCount= (int*)data;

   g_print("received underflow signal\n");
   *gotSignalCount= *gotSignalCount+1;
}

static bool testCaseSocSinkUnderflowSignal( EMCTX *emctx )
{
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   bool result;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   int receivedSignalCount;
   EGLBoolean b;
   TestEGLCtx eglCtx;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( &eglCtx, 0, sizeof(TestEGLCtx) );

   if ( getenv("WAYLAND_DISPLAY") == 0 )
   {
      EMStart( emctx );

      result= testSetupEGL( &eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      glCtx= WstGLInit();
      if ( !glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
      if ( !nativeWindow )
      {
         EMERROR("Unable to create westeros-gl native window");
         goto exit;
      }

      eglCtx.eglSurfaceWindow= eglCreateWindowSurface( eglCtx.eglDisplay,
                                                     eglCtx.eglConfig,
                                                     (EGLNativeWindowType)nativeWindow,
                                                     NULL );
      printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", eglCtx.eglSurfaceWindow );

      b= eglMakeCurrent( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow, eglCtx.eglSurfaceWindow, eglCtx.eglContext );
      if ( !b )
      {
         EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
         goto exit;
      }

      eglSwapInterval( eglCtx.eglDisplay, 1 );
      eglSwapBuffers(eglCtx.eglDisplay, eglCtx.eglSurfaceWindow);
      usleep( 34000 );
   }

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   g_signal_connect( sink, "buffer-underflow-callback", G_CALLBACK(underflowCallback), &receivedSignalCount);

   receivedSignalCount= 0;

   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   // Allow pipeline to run briefly
   usleep( 200000 );

   EMSimpleVideoDecoderSignalUnderflow( videoDecoder );

   // Allow pipeline to run briefly
   usleep( 200000 );

   gst_element_set_state( pipeline, GST_STATE_NULL );

   if ( receivedSignalCount == 0 )
   {
      EMERROR("Failed to receive underflow signal");
      goto exit;
   }

   if ( receivedSignalCount > 1 )
   {
      EMERROR("Received excess underflow signals: count %d", receivedSignalCount);
      goto exit;
   }

   testResult= true;

exit:
   if ( pipeline )
   {
      gst_object_unref( pipeline );
   }
   if ( eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow );
      eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }
   testTermEGL( &eglCtx );

   return testResult;
}

namespace ElementRecycle
{
void textureCreated( EMCTX *ctx, void *userData, int bufferId )
{
   int *textureCount= (int*)userData;
   *textureCount= *textureCount + 1;
}
}; // namespace ElementRecycle

static bool testCaseSocSinkElementRecycle( EMCTX *emctx )
{
   using namespace ElementRecycle;
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   bool result;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   bool receivedSignal;
   bool checkTextures= false;
   int textureCount;
   EGLBoolean b;
   TestEGLCtx eglCtx;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( &eglCtx, 0, sizeof(TestEGLCtx) );

   if ( getenv("WAYLAND_DISPLAY") == 0 )
   {
      EMStart( emctx );

      result= testSetupEGL( &eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      glCtx= WstGLInit();
      if ( !glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
      if ( !nativeWindow )
      {
         EMERROR("Unable to create westeros-gl native window");
         goto exit;
      }

      eglCtx.eglSurfaceWindow= eglCreateWindowSurface( eglCtx.eglDisplay,
                                                     eglCtx.eglConfig,
                                                     (EGLNativeWindowType)nativeWindow,
                                                     NULL );
      printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", eglCtx.eglSurfaceWindow );

      b= eglMakeCurrent( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow, eglCtx.eglSurfaceWindow, eglCtx.eglContext );
      if ( !b )
      {
         EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
         goto exit;
      }

      eglSwapInterval( eglCtx.eglDisplay, 1 );
      eglSwapBuffers(eglCtx.eglDisplay, eglCtx.eglSurfaceWindow);
      usleep( 34000 );
   }

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );

   if ( getenv("WAYLAND_DISPLAY") )
   {
      checkTextures= true;
      EMSetTextureCreatedCallback( emctx, textureCreated, &textureCount );
   }

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   g_signal_connect( sink, "first-video-frame-callback", G_CALLBACK(firstFrameCallback), &receivedSignal);

   receivedSignal= false;
   textureCount= 0;
   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   // Allow pipeline to run briefly
   usleep( 2000000 );

   if ( !receivedSignal )
   {
      EMERROR("Failed to receive first video frame signal");
      goto exit;
   }

   if ( checkTextures )
   {
      if ( textureCount < 1 )
      {
         EMERROR("Failed to receive video texture");
         goto exit;
      }
   }

   gst_element_set_state( pipeline, GST_STATE_READY );

   receivedSignal= false;
   textureCount= 0;
   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   // Allow pipeline to run briefly
   usleep( 2000000 );

   if ( !receivedSignal )
   {
      EMERROR("Failed to receive first video frame signal");
      goto exit;
   }

   if ( checkTextures )
   {
      if ( textureCount < 1 )
      {
         EMERROR("Failed to receive video texture");
         goto exit;
      }
   }

   testResult= true;

exit:
   if ( pipeline )
   {
      gst_element_set_state( pipeline, GST_STATE_NULL );
      gst_object_unref( pipeline );
   }
   if ( eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow );
      eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }
   testTermEGL( &eglCtx );

   return testResult;
}

static bool testCaseSocSinkBasicPositionReporting( EMCTX *emctx )
{
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   bool result;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   float frameRate;
   int frameNumber;
   gint64 pos, posExpected;
   gint64 diff;
   EGLBoolean b;
   TestEGLCtx eglCtx;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( &eglCtx, 0, sizeof(TestEGLCtx) );

   if ( getenv("WAYLAND_DISPLAY") == 0 )
   {
      EMStart( emctx );

      result= testSetupEGL( &eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      glCtx= WstGLInit();
      if ( !glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
      if ( !nativeWindow )
      {
         EMERROR("Unable to create westeros-gl native window");
         goto exit;
      }

      eglCtx.eglSurfaceWindow= eglCreateWindowSurface( eglCtx.eglDisplay,
                                                     eglCtx.eglConfig,
                                                     (EGLNativeWindowType)nativeWindow,
                                                     NULL );
      printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", eglCtx.eglSurfaceWindow );

      b= eglMakeCurrent( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow, eglCtx.eglSurfaceWindow, eglCtx.eglContext );
      if ( !b )
      {
         EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
         goto exit;
      }

      eglSwapInterval( eglCtx.eglDisplay, 1 );
      eglSwapBuffers(eglCtx.eglDisplay, eglCtx.eglSurfaceWindow);
      usleep( 34000 );
   }

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   g_object_set( G_OBJECT(sink), "force-aspect-ratio", TRUE, NULL );

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   for( int i= 0; i < 10; ++i )
   {
      usleep( INTERVAL_200_MS );

      frameRate= EMSimpleVideoDecoderGetFrameRate( videoDecoder );
      frameNumber= videoSrcGetFrameNumber( src );

      posExpected= (frameNumber/frameRate)*GST_SECOND;
      if ( !gst_element_query_position( pipeline, GST_FORMAT_TIME, &pos ) )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Failed to query position");
         goto exit;
      }
      g_print("%d position %" GST_TIME_FORMAT " expected %" GST_TIME_FORMAT "\n", i, GST_TIME_ARGS(pos), GST_TIME_ARGS(posExpected));
      diff= (pos < posExpected) ? posExpected-pos : pos-posExpected;
      if ( diff > 10*1000000000LL/60LL )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Position out of range: expected %" GST_TIME_FORMAT " actual %" GST_TIME_FORMAT, GST_TIME_ARGS(posExpected), GST_TIME_ARGS(pos));
         goto exit;
      }
   }

   gst_element_set_state( pipeline, GST_STATE_NULL );

   testResult= true;

exit:
   if ( pipeline )
   {
      gst_object_unref( pipeline );
   }
   if ( eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow );
      eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }
   testTermEGL( &eglCtx );

   return testResult;
}

static bool testCaseSocSinkBasicPositionReportingProperty( EMCTX *emctx )
{
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   bool result;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   float frameRate;
   int frameNumber;
   unsigned long long basePTS;
   gint64 pos, posExpected;
   EGLBoolean b;
   TestEGLCtx eglCtx;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( &eglCtx, 0, sizeof(TestEGLCtx) );

   if ( getenv("WAYLAND_DISPLAY") == 0 )
   {
      EMStart( emctx );

      result= testSetupEGL( &eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      glCtx= WstGLInit();
      if ( !glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
      if ( !nativeWindow )
      {
         EMERROR("Unable to create westeros-gl native window");
         goto exit;
      }

      eglCtx.eglSurfaceWindow= eglCreateWindowSurface( eglCtx.eglDisplay,
                                                     eglCtx.eglConfig,
                                                     (EGLNativeWindowType)nativeWindow,
                                                     NULL );
      printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", eglCtx.eglSurfaceWindow );

      b= eglMakeCurrent( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow, eglCtx.eglSurfaceWindow, eglCtx.eglContext );
      if ( !b )
      {
         EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
         goto exit;
      }

      eglSwapInterval( eglCtx.eglDisplay, 1 );
      eglSwapBuffers(eglCtx.eglDisplay, eglCtx.eglSurfaceWindow);
      usleep( 34000 );
   }

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );
   EMSimpleVideoDecoderSetBasePTS( videoDecoder, 0x40000000ULL );

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   basePTS= EMSimpleVideoDecoderGetBasePTS( videoDecoder );
   for( int i= 0; i < 10; ++i )
   {
      usleep( INTERVAL_200_MS );

      frameRate= EMSimpleVideoDecoderGetFrameRate( videoDecoder );
      frameNumber= videoSrcGetFrameNumber( src );

      posExpected= basePTS + (frameNumber/frameRate)*90000LL;
      g_object_get( sink, "video-pts", &pos, NULL );
      g_print("%d video-pts %lld expected %lld \n", i, (long long int)pos, (long long int)posExpected);
      if ( (pos < 0.9*posExpected) || (pos > 1.1*posExpected) )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Position out of range: expected %lld actual %lld", posExpected, pos);
         goto exit;
      }
   }

   gst_element_set_state( pipeline, GST_STATE_NULL );

   testResult= true;

exit:
   if ( videoDecoder )
   {
      EMSimpleVideoDecoderSetBasePTS( videoDecoder, 0ULL );
   }

   if ( pipeline )
   {
      gst_object_unref( pipeline );
   }
   if ( eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow );
      eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }
   testTermEGL( &eglCtx );

   return testResult;
}

static bool testCaseSocSinkBasicPauseResume( EMCTX *emctx )
{
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   bool result;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   float frameRate;
   int frameNumber;
   gint64 pos, posExpected= 0;
   gint64 diff;
   bool isPaused;
   EGLBoolean b;
   TestEGLCtx eglCtx;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( &eglCtx, 0, sizeof(TestEGLCtx) );

   if ( getenv("WAYLAND_DISPLAY") == 0 )
   {
      EMStart( emctx );

      result= testSetupEGL( &eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      glCtx= WstGLInit();
      if ( !glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
      if ( !nativeWindow )
      {
         EMERROR("Unable to create westeros-gl native window");
         goto exit;
      }

      eglCtx.eglSurfaceWindow= eglCreateWindowSurface( eglCtx.eglDisplay,
                                                     eglCtx.eglConfig,
                                                     (EGLNativeWindowType)nativeWindow,
                                                     NULL );
      printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", eglCtx.eglSurfaceWindow );

      b= eglMakeCurrent( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow, eglCtx.eglSurfaceWindow, eglCtx.eglContext );
      if ( !b )
      {
         EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
         goto exit;
      }

      eglSwapInterval( eglCtx.eglDisplay, 1 );
      eglSwapBuffers(eglCtx.eglDisplay, eglCtx.eglSurfaceWindow);
      usleep( 34000 );
   }

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   isPaused= false;
   for( int i= 0; i < 10; ++i )
   {
      if ( i == 4 )
      {
         gst_element_set_state( pipeline, GST_STATE_PAUSED );
         isPaused= true;
      }
      if ( i == 7 )
      {
         gst_element_set_state( pipeline, GST_STATE_PLAYING );
         isPaused= false;
      }

      usleep( INTERVAL_200_MS );

      frameRate= EMSimpleVideoDecoderGetFrameRate( videoDecoder );
      frameNumber= videoSrcGetFrameNumber( src );

      if ( !isPaused )
      {
         posExpected= (frameNumber/frameRate)*GST_SECOND;
      }

      if ( !gst_element_query_position( pipeline, GST_FORMAT_TIME, &pos ) )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Failed to query position");
         goto exit;
      }

      g_print("%d paused %d position %" GST_TIME_FORMAT " expected %" GST_TIME_FORMAT "\n", i, isPaused, GST_TIME_ARGS(pos), GST_TIME_ARGS(posExpected));

      diff= (pos < posExpected) ? posExpected-pos : pos-posExpected;
      if ( diff > 10*1000000000LL/60LL )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Position out of range: expected %" GST_TIME_FORMAT " actual %" GST_TIME_FORMAT, GST_TIME_ARGS(posExpected), GST_TIME_ARGS(pos));
         goto exit;
      }
   }

   gst_element_set_state( pipeline, GST_STATE_NULL );

   testResult= true;

exit:
   if ( pipeline )
   {
      gst_object_unref( pipeline );
   }
   if ( eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow );
      eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }
   testTermEGL( &eglCtx );

   return testResult;
}

static bool testCaseSocSinkBasicSeek( EMCTX *emctx )
{
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   bool result;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   float frameRate;
   int frameNumber;
   gint64 pos, posExpected;
   gint64 seekPos;
   gint64 diff;
   gboolean rv;
   EGLBoolean b;
   TestEGLCtx eglCtx;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( &eglCtx, 0, sizeof(TestEGLCtx) );

   if ( getenv("WAYLAND_DISPLAY") == 0 )
   {
      EMStart( emctx );

      result= testSetupEGL( &eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      glCtx= WstGLInit();
      if ( !glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
      if ( !nativeWindow )
      {
         EMERROR("Unable to create westeros-gl native window");
         goto exit;
      }

      eglCtx.eglSurfaceWindow= eglCreateWindowSurface( eglCtx.eglDisplay,
                                                     eglCtx.eglConfig,
                                                     (EGLNativeWindowType)nativeWindow,
                                                     NULL );
      printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", eglCtx.eglSurfaceWindow );

      b= eglMakeCurrent( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow, eglCtx.eglSurfaceWindow, eglCtx.eglContext );
      if ( !b )
      {
         EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
         goto exit;
      }

      eglSwapInterval( eglCtx.eglDisplay, 1 );
      eglSwapBuffers(eglCtx.eglDisplay, eglCtx.eglSurfaceWindow);
      usleep( 34000 );
   }

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   for( int i= 0; i < 5; ++i )
   {
      usleep( INTERVAL_200_MS );

      frameRate= EMSimpleVideoDecoderGetFrameRate( videoDecoder );
      frameNumber= videoSrcGetFrameNumber( src );

      posExpected= (frameNumber/frameRate)*GST_SECOND;

      if ( !gst_element_query_position( pipeline, GST_FORMAT_TIME, &pos ) )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Failed to query position");
         goto exit;
      }

      g_print("%d position %" GST_TIME_FORMAT " expected %" GST_TIME_FORMAT "\n", i, GST_TIME_ARGS(pos), GST_TIME_ARGS(posExpected));

      diff= (pos < posExpected) ? posExpected-pos : pos-posExpected;
      if ( diff > 10*1000000000LL/60LL )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Position out of range: expected %" GST_TIME_FORMAT " actual %" GST_TIME_FORMAT, GST_TIME_ARGS(posExpected), GST_TIME_ARGS(pos));
         goto exit;
      }
   }

   seekPos= 30.0 * GST_SECOND;
   seekPos= getSegmentStart( videoDecoder, seekPos );

   rv= gst_element_seek( pipeline,
                         1.0, //rate
                         GST_FORMAT_TIME,
                         GST_SEEK_FLAG_FLUSH,
                         GST_SEEK_TYPE_SET,
                         seekPos,
                         GST_SEEK_TYPE_NONE,
                         GST_CLOCK_TIME_NONE );
   if ( !rv )
   {
      EMERROR("Seek operation failed");
      goto exit;
   }

   for( int i= 0; i < 5; ++i )
   {
      usleep( INTERVAL_200_MS );

      frameRate= EMSimpleVideoDecoderGetFrameRate( videoDecoder );
      frameNumber= videoSrcGetFrameNumber( src );

      posExpected= seekPos + (frameNumber/frameRate)*GST_SECOND;

      if ( !gst_element_query_position( pipeline, GST_FORMAT_TIME, &pos ) )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Failed to query position");
         goto exit;
      }

      g_print("%d position %" GST_TIME_FORMAT " expected %" GST_TIME_FORMAT "\n", i, GST_TIME_ARGS(pos), GST_TIME_ARGS(posExpected));

      diff= (pos < posExpected) ? posExpected-pos : pos-posExpected;
      if ( diff > 10*1000000000LL/60LL )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Position out of range: expected %" GST_TIME_FORMAT " actual %" GST_TIME_FORMAT, GST_TIME_ARGS(posExpected), GST_TIME_ARGS(pos));
         goto exit;
      }
   }

   gst_element_set_state( pipeline, GST_STATE_NULL );

   testResult= true;

exit:
   if ( pipeline )
   {
      gst_object_unref( pipeline );
   }
   if ( eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow );
      eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }
   testTermEGL( &eglCtx );

   return testResult;
}

static bool testCaseSocSinkBasicSeekZeroBased( EMCTX *emctx )
{
   bool testResult= false;
   EMSimpleVideoDecoder *videoDecoder= 0;

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetSegmentsStartAtZero( videoDecoder, true );

   testResult= testCaseSocSinkBasicSeek( emctx );

exit:

   return testResult;
}

static bool testCaseSocSinkFrameAdvance( EMCTX *emctx )
{
   bool testResult= false;
   int argc= 0;
   char **argv= 0;
   bool result;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   float frameRate;
   int frameNumber;
   gint64 pos, posExpected;
   EGLBoolean b;
   TestEGLCtx eglCtx;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( &eglCtx, 0, sizeof(TestEGLCtx) );

   if ( getenv("WAYLAND_DISPLAY") == 0 )
   {
      EMStart( emctx );

      result= testSetupEGL( &eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      glCtx= WstGLInit();
      if ( !glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
      if ( !nativeWindow )
      {
         EMERROR("Unable to create westeros-gl native window");
         goto exit;
      }

      eglCtx.eglSurfaceWindow= eglCreateWindowSurface( eglCtx.eglDisplay,
                                                     eglCtx.eglConfig,
                                                     (EGLNativeWindowType)nativeWindow,
                                                     NULL );
      printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", eglCtx.eglSurfaceWindow );

      b= eglMakeCurrent( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow, eglCtx.eglSurfaceWindow, eglCtx.eglContext );
      if ( !b )
      {
         EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
         goto exit;
      }

      eglSwapInterval( eglCtx.eglDisplay, 1 );
      eglSwapBuffers(eglCtx.eglDisplay, eglCtx.eglSurfaceWindow);
      usleep( 34000 );
   }

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   g_object_set( G_OBJECT(sink), "frame-step-on-preroll", TRUE, NULL );
   g_object_set( G_OBJECT(sink), "async", TRUE, NULL );

   gst_element_set_state( pipeline, GST_STATE_PAUSED );

   frameRate= EMSimpleVideoDecoderGetFrameRate( videoDecoder );
   frameNumber= videoSrcGetFrameNumber( src );

   for( int i= 0; i < 10; ++i )
   {
      usleep( INTERVAL_200_MS );

      posExpected= (frameNumber/frameRate)*GST_SECOND;
      if ( !gst_element_query_position( pipeline, GST_FORMAT_TIME, &pos ) )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Failed to query position");
         goto exit;
      }
      g_print("%d position %" GST_TIME_FORMAT " expected %" GST_TIME_FORMAT "\n", i, GST_TIME_ARGS(pos), GST_TIME_ARGS(posExpected));
      if ( (pos < 0.8*posExpected) || (pos > 1.2*posExpected) )
      {
         gst_element_set_state( pipeline, GST_STATE_NULL );
         EMERROR("Position out of range: expected %" GST_TIME_FORMAT " actual %" GST_TIME_FORMAT, GST_TIME_ARGS(posExpected), GST_TIME_ARGS(pos));
         goto exit;
      }

      frameRate= EMSimpleVideoDecoderGetFrameRate( videoDecoder );
      frameNumber= videoSrcGetFrameNumber( src );

      gst_element_send_event( sink,
                              gst_event_new_step( GST_FORMAT_BUFFERS, 0, 1.0, TRUE, FALSE) );
      videoSrcDoStep( src );

      usleep( INTERVAL_200_MS );
   }

   gst_element_set_state( pipeline, GST_STATE_NULL );

   testResult= true;

exit:
   if ( pipeline )
   {
      gst_object_unref( pipeline );
   }
   if ( eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( eglCtx.eglDisplay, eglCtx.eglSurfaceWindow );
      eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }
   testTermEGL( &eglCtx );

   return testResult;
}

static bool testCaseSocSinkInitWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   testResult= testCaseSocSinkInit( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   return testResult;
}

static bool testCaseSocSinkBasicPipelineWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   testResult= testCaseSocSinkBasicPipeline( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   return testResult;
}

static bool testCaseSocSinkFirstFrameSignalWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   testResult= testCaseSocSinkFirstFrameSignal( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   return testResult;
}

static bool testCaseSocSinkUnderflowSignalWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   testResult= testCaseSocSinkUnderflowSignal( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   return testResult;
}

static bool testCaseSocSinkElementRecycleWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );
   setenv( "WESTEROS_SINK_USE_GFX", "1", true );

   testResult= testCaseSocSinkElementRecycle( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );
   unsetenv( "WESTEROS_SINK_USE_GFX" );

   return testResult;
}

static bool testCaseSocSinkBasicPositionReportingWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   testResult= testCaseSocSinkBasicPositionReporting( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   return testResult;
}

static bool testCaseSocSinkBasicPauseResumeWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   testResult= testCaseSocSinkBasicPauseResume( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   return testResult;
}

static bool testCaseSocSinkBasicSeekWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   testResult= testCaseSocSinkBasicSeek( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   return testResult;
}

static bool testCaseSocSinkBasicSeekZeroBasedWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   testResult= testCaseSocSinkBasicSeekZeroBased( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   return testResult;
}

static bool testCaseSocSinkFrameAdvanceWithCompositor( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   const char *value;

   EMStart( emctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   testResult= testCaseSocSinkFrameAdvance( emctx );

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   return testResult;
}

static bool testCaseSocSinkBasicPipelineGfx( EMCTX *emctx )
{
   bool testResult= false;
   WstCompositor *wctx= 0;
   bool result;
   TestEGLCtx eglCtxS;
   const char *value;
   int argc= 0;
   char **argv= 0;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   std::vector<WstRect> rects;
   float matrix[16];
   float alpha= 1.0;
   bool needHolePunch;
   int hints;
   int windowWidth= 1920;
   int windowHeight= 1080;
   WstGLCtx *glCtx= 0;
   void  *nativeWindow= 0;

   memset( matrix, 0, sizeof(matrix) );
   matrix[0]= matrix[5]= matrix[10]= matrix[15]= 1.0;

   EMStart( emctx );

   result= testSetupEGL( &eglCtxS, 0 );
   if ( !result )
   {
      EMERROR("testSetupEGL failed for compositor");
      goto exit;
   }

   glCtx= WstGLInit();
   if ( !glCtx )
   {
      EMERROR("Unable to create westeros-gl context");
      goto exit;
   }

   nativeWindow= WstGLCreateNativeWindow( glCtx, 0, 0, windowWidth, windowHeight );
   if ( !nativeWindow )
   {
      EMERROR("Unable to create westeros-gl native window");
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_embedded.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", value, true );

   videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );

   gst_init( &argc, &argv );

   pipeline= gst_pipeline_new("pipeline");
   if ( !pipeline )
   {
      EMERROR("Failed to create pipeline instance");
      goto exit;
   }

   src= createVideoSrc( emctx, videoDecoder );
   if ( !src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }

   sink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !sink )
   {
      EMERROR("Failed to create sink instance");
      goto exit;
   }

   gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

   if ( gst_element_link( src, sink ) != TRUE )
   {
      EMERROR("Failed to link src and sink");
      goto exit;
   }

   gst_element_set_state( pipeline, GST_STATE_PLAYING );

   // Allow pipeline to run briefly.  Use animation hint to force gfx video path
   hints= WstHints_animating;
   for( int i= 0; i < 118; ++i )
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
   }

   gst_element_set_state( pipeline, GST_STATE_NULL );

   testResult= true;

   WstCompositorDestroy( wctx );

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   if ( pipeline )
   {
      gst_object_unref( pipeline );
   }
   if ( nativeWindow )
   {
      WstGLDestroyNativeWindow( glCtx, nativeWindow );
   }
   if ( glCtx )
   {
      WstGLTerm( glCtx );
   }

   testTermEGL( &eglCtxS );

   return testResult;
}

namespace DualMedia
{
typedef struct _TestCtx TestCtx;

typedef struct _Pipeline
{
   TestCtx *ctx;
   EMSimpleVideoDecoder *videoDecoder;
   GstElement *pipeline;
   GstBus *bus;
   GstElement *src;
   GstElement *videoSink;
   int x;
   int y;
   int width;
   int height;
   bool eos;
} Pipeline;

typedef struct _TestCtx
{
   EMCTX *emctx;
   EssCtx *essCtx;
   GMainLoop *loop;
   Pipeline *pipeLine1;
   Pipeline *pipeLine2;
   bool terminated;
} TestCtx;

static void destroyPipeline( Pipeline *pipeLine );

static Pipeline* createPipeline( TestCtx *ctx, int x, int y, int width, int height )
{
   bool result= false;
   EMCTX *emctx= ctx->emctx;
   Pipeline *pipeLine= 0;

   pipeLine= (Pipeline*)calloc( 1, sizeof(Pipeline) );
   if ( !pipeLine )
   {
      EMERROR("Error: no memory for pipeLine");
      goto exit;
   }
   pipeLine->ctx= ctx;
   pipeLine->x= x;
   pipeLine->y= y;
   pipeLine->width= width;
   pipeLine->height= height;

   pipeLine->pipeline= gst_pipeline_new("pipeline");
   if ( !pipeLine->pipeline )
   {
      EMERROR("Error: unable to create pipeline instance" );
      goto exit;
   }

   pipeLine->bus= gst_pipeline_get_bus( GST_PIPELINE(pipeLine->pipeline) );
   if ( !pipeLine->bus )
   {
      EMERROR("Error: unable to get pipeline bus");
      goto exit;
   }

   pipeLine->videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
   if ( !pipeLine->videoDecoder )
   {
      EMERROR("Failed to obtain test video decoder");
      goto exit;
   }

   EMSimpleVideoDecoderSetVideoSize( pipeLine->videoDecoder, 1920, 1080 );

   pipeLine->src= createVideoSrc( emctx, pipeLine->videoDecoder );
   if ( !pipeLine->src )
   {
      EMERROR("Failed to create src instance");
      goto exit;
   }
   gst_object_ref( pipeLine->src );

   pipeLine->videoSink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !pipeLine->videoSink )
   {
      EMERROR("Failed to create video sink instance");
      goto exit;
   }
   gst_object_ref( pipeLine->videoSink );

   gst_bin_add_many( GST_BIN(pipeLine->pipeline),
                     pipeLine->src,
                     pipeLine->videoSink,
                     NULL
                   );
   if ( !gst_element_link_many( pipeLine->src, pipeLine->videoSink, NULL ) )
   {
      EMERROR("Failed to link source and video sink");
      goto exit;
   }

   if ( GST_STATE_CHANGE_FAILURE == gst_element_set_state(pipeLine->pipeline, GST_STATE_PAUSED) )
   {
      EMERROR("Error: unable to start pipeline");
      goto exit;
   }

   gst_element_get_state( pipeLine->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE );

   result= true;

exit:

   if ( !result )
   {
      destroyPipeline( pipeLine );
      pipeLine= 0;
   }

   return pipeLine;
}

static void startPipeline( Pipeline *pipeLine )
{
   EMCTX *emctx= pipeLine->ctx->emctx;
   if ( GST_STATE_CHANGE_FAILURE == gst_element_set_state(pipeLine->pipeline, GST_STATE_PLAYING) )
   {
      EMERROR("Error: unable to start pipeline");
   }
}

static void destroyPipeline( Pipeline *pipeLine )
{
   if ( pipeLine->pipeline )
   {
      gst_element_set_state(pipeLine->pipeline, GST_STATE_NULL);
      gst_element_get_state( pipeLine->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE );
   }
   if ( pipeLine->videoSink )
   {
      gst_object_unref( pipeLine->videoSink );
      pipeLine->videoSink= 0;
   }
   if ( pipeLine->src )
   {
      gst_object_unref( pipeLine->src );
      pipeLine->src= 0;
   }
   if ( pipeLine->bus )
   {
      gst_object_unref( pipeLine->bus );
      pipeLine->bus= 0;
   }
   if ( pipeLine->pipeline )
   {
      gst_object_unref( GST_OBJECT(pipeLine->pipeline) );
      pipeLine->pipeline= 0;
   }
}

static bool initGst( TestCtx *ctx )
{
   bool result= false;
   int argc= 0;
   char **argv= 0;
   EMCTX *emctx= ctx->emctx;

   gst_init( &argc, &argv );

   ctx->loop= g_main_loop_new(NULL,FALSE);
   if ( !ctx->loop )
   {
      EMERROR("Error: unable to create glib main loop");
      goto exit;
   }

   unsetenv("WESTEROS_SINK_USE_GFX");
   ctx->pipeLine1= createPipeline( ctx, 0, 270, 960, 540 );
   if ( !ctx->pipeLine1 )
   {
      EMERROR("Error: unable to create pipeline");
      goto exit;
   }
   g_print("pipeline 1 created\n");

   setenv("WESTEROS_SINK_USE_GFX", "1", true);
   ctx->pipeLine2= createPipeline( ctx, 960, 270, 960, 540 );
   if ( !ctx->pipeLine2 )
   {
      EMERROR("Error: unable to create pipeline");
      goto exit;
   }
   g_print("pipeline 2 created\n");

   startPipeline( ctx->pipeLine1 );
   g_print("pipeline 1 started\n");

   startPipeline( ctx->pipeLine2 );
   g_print("pipeline 2 started\n");

   result= true;

exit:

   return result;
}

static void termGst( TestCtx *ctx )
{
   unsetenv("WESTEROS_SINK_USE_GFX");

   if ( ctx->pipeLine1 )
   {
      destroyPipeline( ctx->pipeLine1 );
      ctx->pipeLine1= 0;
   }

   if ( ctx->pipeLine2 )
   {
      destroyPipeline( ctx->pipeLine2 );
      ctx->pipeLine2= 0;
   }

   if ( ctx->loop )
   {
      g_main_loop_unref(ctx->loop);
      ctx->loop= 0;
   }
}

static void terminated( void *userData )
{
   TestCtx *ctx= (TestCtx*)userData;
   printf("terminated event\n");
   ctx->terminated= true;
}

static EssTerminateListener terminateListener=
{
   terminated
};

};

bool testCaseSocEssosDualMediaPlayback( EMCTX *emctx )
{
   using namespace DualMedia;

   bool testResult= false;
   bool result;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;
   TestCtx tCtx;
   TestCtx *testCtx= &tCtx;

   EMStart( emctx );

   memset( testCtx, 0, sizeof(TestCtx) );
   testCtx->emctx= emctx;

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

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   for( int i= 0; i < 2; ++i )
   {
      testCtx->essCtx= EssContextCreate();
      if ( !testCtx->essCtx )
      {
         EMERROR("EssContextCreate failed");
         goto exit;
      }

      result= EssContextSetTerminateListener( testCtx->essCtx, testCtx, &terminateListener );
      if ( result == false )
      {
         EMERROR("EssContextSetTerminateListener failed");
         goto exit;
      }

      result= EssContextStart( testCtx->essCtx );
      if ( result == false )
      {
         EMERROR("EssContextStart failed");
         goto exit;
      }

      if ( !initGst( testCtx ) )
      {
         EMERROR("intGst failed");
         goto exit;
      }

      for( int j= 0; j < 100; ++j )
      {
         EssContextRunEventLoopOnce( testCtx->essCtx );
         if ( testCtx->terminated )
         {
            EMERROR("Unexpected terminate callback");
            goto exit;
         }
         usleep(2000);
      }

      termGst(testCtx);

      if ( testCtx->essCtx )
      {
         EssContextDestroy( testCtx->essCtx );
         testCtx->essCtx= 0;
      }
   }

   testResult= true;

exit:

   termGst(testCtx);

   if ( testCtx->essCtx )
   {
      EssContextDestroy( testCtx->essCtx );
   }

   if ( wctx )
   {
      WstCompositorDestroy( wctx );
   }

   return testResult;
}

namespace SocSinkVideoPosition
{
typedef struct _TestCtx
{
   TestEGLCtx eglCtx;
   WstGLCtx *glCtx;
   void *eglNativeWindow;
   int windowWidth;
   int windowHeight;
   int hx;
   int hy;
   int hw;
   int hh;
} TestCtx;

static void holePunched( EMCTX *emctx, void* userData, int hx, int hy, int hw, int hh )
{
   TestCtx *ctx= (TestCtx*)userData;

   if ( ctx )
   {
      //printf("holePunched: (%d, %d, %d, %d)\n", hx, hy, hw, hh);
   }
}

static void outputHandleGeometry( void *,
                                  int,
                                  int,
                                  int,
                                  int,
                                  int,
                                  const char *,
                                  const char *,
                                  int )
{
}

static void outputHandleMode( void *,
                              uint32_t flags,
                              int width,
                              int height,
                              int )
{
   if ( flags & WL_OUTPUT_MODE_CURRENT )
   {
      printf("nested mode listener: mode %dx%d\n", width, height);
   }
}

static void outputHandleDone( void * )
{
}

static void outputHandleScale( void *,
                               int32_t )
{
}

WstOutputNestedListener nestedOutputListener=
{
   outputHandleGeometry,
   outputHandleMode,
   outputHandleDone,
   outputHandleScale,
};

} //namespace SocSinkGfxTransition

static bool testCaseSocSinkVideoPosition( EMCTX *emctx )
{
   using namespace SocSinkVideoPosition;

   bool testResult= false;
   WstCompositor *wctx= 0;
   WstCompositor *wctx2= 0;
   WstCompositor *wctx3= 0;
   bool result;
   const char *value;
   int argc= 0;
   char **argv= 0;
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;
   EMSurfaceClient *videoWindow= 0;
   std::vector<WstRect> rects;
   float matrix[16];
   float alpha= 1.0;
   bool needHolePunch;
   int hints;
   EGLBoolean b;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   bool setRect= false;
   bool setSize= false;
   bool useEmbedded= false;
   bool isBridged= false;
   bool isNested= false;
   bool isRepeater= false;
   float scale= 1.0, scale2= 1.0, scale3= 1.0;
   float transx= 0, transx2= 0, transx3= 0;
   float transy= 0, transy2= 0, transy3= 0;
   int ow, oh, ow2= 0, oh2= 0, ow3= 0, oh3= 0;
   int rx= 0, ry= 0, rw= 0, rh= 0;
   int vx, vy, vw, vh;
   int vxexp, vyexp, vwexp, vhexp;
   int iteration;

   EMStart( emctx );

   for( iteration= 0; iteration < 28; ++iteration )
   {
      printf("iteration: %d\n", iteration);
      memset( &testCtx, 0, sizeof(TestCtx) );

      switch( iteration )
      {
         case 0:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= false;
            isBridged= false;
            setRect= false;
            vxexp= 0;
            vyexp= 0;
            vwexp= 1280;
            vhexp= 720;
            break;
         case 1:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= false;
            isBridged= false;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            vxexp= 100;
            vyexp= 100;
            vwexp= 640;
            vhexp= 360;
            break;
         case 2:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= false;
            isBridged= false;
            setRect= false;
            vxexp= 0;
            vyexp= 0;
            vwexp= 1920;
            vhexp= 1080;
            break;
         case 3:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= false;
            isBridged= false;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            vxexp= 100;
            vyexp= 100;
            vwexp= 640;
            vhexp= 360;
            break;
         case 4:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= true;
            isBridged= false;
            setRect= false;
            vxexp= 0;
            vyexp= 0;
            vwexp= 1280;
            vhexp= 720;
            break;
         case 5:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= true;
            isBridged= false;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            vxexp= 100;
            vyexp= 100;
            vwexp= 640;
            vhexp= 360;
            break;
         case 6:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= true;
            isBridged= false;
            setRect= false;
            vxexp= 0;
            vyexp= 0;
            vwexp= 1920;
            vhexp= 1080;
            break;
         case 7:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= true;
            isBridged= false;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            vxexp= 100;
            vyexp= 100;
            vwexp= 640;
            vhexp= 360;
            break;
         case 8:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= false;
            vxexp= 0;
            vyexp= 0;
            vwexp= 1280;
            vhexp= 720;
            break;
         case 9:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            vxexp= 100;
            vyexp= 100;
            vwexp= 640;
            vhexp= 360;
            break;
         case 10:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= false;
            vxexp= 0;
            vyexp= 0;
            vwexp= 1920;
            vhexp= 1080;
            break;
         case 11:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            vxexp= 100;
            vyexp= 100;
            vwexp= 640;
            vhexp= 360;
            break;
         case 12:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= true;
            isBridged= false;
            setRect= false;
            scale= 0.5;
            transx= 100;
            transy= 100;
            vxexp= 100;
            vyexp= 100;
            vwexp= 640;
            vhexp= 360;
            break;
         case 13:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= true;
            isBridged= false;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            scale= 0.5;
            transx= 100;
            transy= 100;
            vxexp= 150;
            vyexp= 150;
            vwexp= 320;
            vhexp= 180;
            break;
         case 14:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= true;
            isBridged= false;
            setRect= false;
            scale= 0.5;
            transx= 100;
            transy= 100;
            vxexp= 100;
            vyexp= 100;
            vwexp= 960;
            vhexp= 540;
            break;
         case 15:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            useEmbedded= true;
            isBridged= false;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            scale= 0.5;
            transx= 100;
            transy= 100;
            vxexp= 150;
            vyexp= 150;
            vwexp= 320;
            vhexp= 180;
            break;
         case 16:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= false;
            scale= 0.5;
            transx= 100;
            transy= 100;
            vxexp= 100;
            vyexp= 100;
            vwexp= 960;
            vhexp= 540;
            break;
         case 17:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            scale= 0.5;
            transx= 100;
            transy= 100;
            vxexp= 150;
            vyexp= 150;
            vwexp= 320;
            vhexp= 180;
            break;
         case 18:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= false;
            scale= 0.5;
            transx= 100;
            transy= 100;
            vxexp= 100;
            vyexp= 100;
            vwexp= 960;
            vhexp= 540;
            break;
         case 19:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= true;
            rx= 100;
            ry= 100;
            rw= 640;
            rh= 360;
            scale= 0.5;
            transx= 100;
            transy= 100;
            vxexp= 150;
            vyexp= 150;
            vwexp= 320;
            vhexp= 180;
            break;
         case 20:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= 480;
            oh= 270;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= false;
            scale= 1.0;
            transx= 200;
            transy= 200;
            vxexp= 200;
            vyexp= 200;
            vwexp= 480;
            vhexp= 270;
            break;
         case 21:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= 480;
            oh= 270;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            useEmbedded= true;
            isBridged= true;
            setRect= true;
            rx= 25;
            ry= 25;
            rw= 160;
            rh= 90;
            scale= 1.0;
            transx= 200;
            transy= 200;
            vxexp= 225;
            vyexp= 225;
            vwexp= 160;
            vhexp= 90;
            break;
         case 22:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            ow3= 940;
            oh3= 540;
            useEmbedded= true;
            isBridged= false;
            isNested= true;
            setRect= false;
            scale= 1.0;
            transx= 0;
            transy= 0;
            scale3= 1.0;
            transx3= 100;
            transy3= 100;
            vxexp= 100;
            vyexp= 100;
            vwexp= 940;
            vhexp= 540;
            break;
         case 23:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            ow3= 940;
            oh3= 540;
            useEmbedded= true;
            isBridged= false;
            isNested= false;
            isRepeater= true;
            setRect= false;
            scale= 1.0;
            transx= 0;
            transy= 0;
            scale3= 1.0;
            transx3= 100;
            transy3= 100;
            vxexp= 100;
            vyexp= 100;
            vwexp= 940;
            vhexp= 540;
            break;
         case 24:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            ow3= 940;
            oh3= 540;
            useEmbedded= true;
            isBridged= false;
            isNested= false;
            isRepeater= true;
            setRect= false;
            setSize= true;
            scale= 1.0;
            transx= 0;
            transy= 0;
            scale3= 1.0;
            transx3= 50;
            transy3= 100;
            vxexp= 50;
            vyexp= 100;
            vwexp= 940;
            vhexp= 540;
            break;
         case 25:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            ow3= 940;
            oh3= 540;
            useEmbedded= true;
            isBridged= false;
            isNested= true;
            isRepeater= false;
            setRect= false;
            setSize= true;
            scale= 1.0;
            transx= 0;
            transy= 0;
            scale3= 1.0;
            transx3= 50;
            transy3= 200;
            vxexp= 50;
            vyexp= 200;
            vwexp= 940;
            vhexp= 540;
            break;
         case 26:
            ctx->windowWidth= 1280;
            ctx->windowHeight= 720;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            ow3= 940;
            oh3= 540;
            useEmbedded= true;
            isBridged= true;
            isNested= false;
            isRepeater= true;
            setRect= false;
            setSize= true;
            scale= 1.0;
            transx= 0;
            transy= 0;
            scale3= 1.0;
            transx3= 50;
            transy3= 100;
            vxexp= 50;
            vyexp= 100;
            vwexp= 940;
            vhexp= 540;
            break;
         case 27:
            ctx->windowWidth= 1920;
            ctx->windowHeight= 1080;
            ow= ctx->windowWidth;
            oh= ctx->windowHeight;
            ow2= ctx->windowWidth;
            oh2= ctx->windowHeight;
            ow3= 940;
            oh3= 540;
            useEmbedded= true;
            isBridged= true;
            isNested= true;
            isRepeater= false;
            setRect= false;
            setSize= true;
            scale= 1.0;
            transx= 0;
            transy= 0;
            scale3= 1.0;
            transx3= 50;
            transy3= 200;
            vxexp= 50;
            vyexp= 200;
            vwexp= 940;
            vhexp= 540;
            break;
      }

      EMSetDisplaySize( emctx, ctx->windowWidth, ctx->windowHeight );

      EMSetHolePunchedCallback( emctx, holePunched, ctx );

      result= testSetupEGL( &ctx->eglCtx, 0 );
      if ( !result )
      {
         EMERROR("testSetupEGL failed");
         goto exit;
      }

      ctx->glCtx= WstGLInit();
      if ( !ctx->glCtx )
      {
         EMERROR("Unable to create westeros-gl context");
         goto exit;
      }

      wctx= WstCompositorCreate();
      if ( !wctx )
      {
         EMERROR( "WstCompositorCreate failed" );
         goto exit;
      }

      value= WstCompositorGetDisplayName( wctx );
      if ( value == 0 )
      {
         EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
         goto exit;
      }

      if ( useEmbedded )
      {
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

         result= WstCompositorSetRendererModule( wctx, "libwesteros_render_embedded.so.0.0.0" );
         if ( result == false )
         {
            EMERROR( "WstCompositorSetRenderedModule failed" );
            goto exit;
         }

         result= WstCompositorSetIsEmbedded( wctx, true );
         if ( result == false )
         {
            EMERROR( "WstCompositorSetIsEmbedded failed" );
            goto exit;
         }
      }
      else
      {
         result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
         if ( result == false )
         {
            EMERROR( "WstCompositorSetRenderedModule failed" );
            goto exit;
         }
      }

      result= WstCompositorSetOutputSize( wctx, ow, oh );
      if ( !result )
      {
         EMERROR( "WstCompositorSetOutputSize failed" );
         goto exit;
      }

      result= WstCompositorStart( wctx );
      if ( result == false )
      {
         EMERROR( "WstCompositorStart failed" );
         goto exit;
      }

      usleep( 34000 );
      WstCompositorResolutionChangeBegin( wctx );

      WstCompositorResolutionChangeEnd( wctx, ow, oh );

      WstCompositorInvalidateScene( wctx );
      usleep( 34000 );

      if ( isBridged )
      {
         setenv( "WESTEROS_VPC_BRIDGE", value, true );

         wctx2= WstCompositorCreate();
         if ( !wctx2 )
         {
            EMERROR( "WstCompositorCreate failed" );
            goto exit;
         }

         value= WstCompositorGetDisplayName( wctx2 );
         if ( value == 0 )
         {
            EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
            goto exit;
         }

         result= WstCompositorSetRendererModule( wctx2, "libwesteros_render_embedded.so.0.0.0" );
         if ( result == false )
         {
            EMERROR( "WstCompositorSetRenderedModule failed" );
            goto exit;
         }

         result= WstCompositorSetIsEmbedded( wctx2, true );
         if ( result == false )
         {
            EMERROR( "WstCompositorSetIsEmbedded failed" );
            goto exit;
         }

         result= WstCompositorSetOutputSize( wctx2, ow2, oh2 );
         if ( !result )
         {
            EMERROR( "WstCompositorSetOutputSize failed" );
            goto exit;
         }

         result= WstCompositorStart( wctx2 );
         if ( result == false )
         {
            EMERROR( "WstCompositorStart failed" );
            goto exit;
         }
      }

      if ( isNested || isRepeater )
      {
         wctx3= WstCompositorCreate();
         if ( !wctx3 )
         {
            EMERROR( "WstCompositorCreate failed" );
            goto exit;
         }

         if ( isNested )
         {
            result= WstCompositorSetIsNested( wctx3, true );
            if ( !result )
            {
               EMERROR( "WstCompositorSetIsNested failed" );
               goto exit;
            }
         }
         else
         {
            result= WstCompositorSetIsRepeater( wctx3, true );
            if ( !result )
            {
               EMERROR( "WstCompositorSetIsRepeater failed" );
               goto exit;
            }
         }

         result= WstCompositorSetNestedDisplayName( wctx3, value );
         if ( !result )
         {
            EMERROR( "WstCompositorSetNestedDisplayName failed" );
            goto exit;
         }

         value= WstCompositorGetDisplayName( wctx3 );
         if ( value == 0 )
         {
            EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
            goto exit;
         }

         result= WstCompositorSetRendererModule( wctx3, "libwesteros_render_gl.so.0.0.0" );
         if ( result == false )
         {
            EMERROR( "WstCompositorSetRenderedModule failed" );
            goto exit;
         }

         result= WstCompositorSetOutputNestedListener( wctx3, &nestedOutputListener, 0 );
         if ( !result )
         {
            EMERROR( "WstCompositorSetOutputNestedListener failed" );
            goto exit;
         }

         result= WstCompositorStart( wctx3 );
         if ( result == false )
         {
            EMERROR( "WstCompositorStart failed" );
            goto exit;
         }
      }

      setenv( "WAYLAND_DISPLAY", value, true );

      videoDecoder= EMGetSimpleVideoDecoder( emctx, EM_TUNERID_MAIN );
      if ( !videoDecoder )
      {
         EMERROR("Failed to obtain test video decoder");
         goto exit;
      }

      EMSimpleVideoDecoderSetVideoSize( videoDecoder, 1920, 1080 );

      gst_init( &argc, &argv );

      pipeline= gst_pipeline_new("pipeline");
      if ( !pipeline )
      {
         EMERROR("Failed to create pipeline instance");
         goto exit;
      }

      src= createVideoSrc( emctx, videoDecoder );
      if ( !src )
      {
         EMERROR("Failed to create src instance");
         goto exit;
      }

      sink= gst_element_factory_make( "westerossink", "vsink" );
      if ( !sink )
      {
         EMERROR("Failed to create sink instance");
         goto exit;
      }

      gst_bin_add_many( GST_BIN(pipeline), src, sink, NULL );

      if ( gst_element_link( src, sink ) != TRUE )
      {
         EMERROR("Failed to link src and sink");
         goto exit;
      }

      gst_element_set_state( pipeline, GST_STATE_PLAYING );

      if ( setRect )
      {
         char work[64];
         sprintf(work,"%d,%d,%d,%d", rx, ry, rw, rh);
         g_object_set( G_OBJECT(sink), "rectangle", work, NULL );
      }

      // Allow pipeline to run briefly.
      usleep( 200000 );

      hints= WstHints_noRotation;
      for( int i= 0; i < 10; ++i )
      {
         usleep( 17000 );

         if ( isBridged )
         {
            memset( &matrix, 0, sizeof(matrix) );
            matrix[10]= matrix[15]= 1.0;
            matrix[0]= matrix[5]= scale2;
            matrix[12]= transx2;
            matrix[13]= transy2;

            rects.clear();
            WstCompositorComposeEmbedded( wctx2,
                                          0, // x
                                          0, // y
                                          ow2, // width
                                          oh2, // height
                                          matrix,
                                          alpha,
                                          hints,
                                          &needHolePunch,
                                          rects );
            if ( needHolePunch )
            {
               ctx->hx= rects[0].x;
               ctx->hy= rects[0].y;
               ctx->hw= rects[0].width;
               ctx->hh= rects[0].height;
            }
         }

         if ( (isNested || isRepeater) && (i == 2) )
         {
            ow= ow3;
            oh= oh3;
            scale= scale3;
            if ( setSize && isBridged )
            {
               transx2= transx3;
               transy2= transy3;
               result= WstCompositorSetOutputSize( wctx2, ow, oh );
            }
            else
            {
               transx= transx3;
               transy= transy3;
               result= WstCompositorSetOutputSize( wctx, ow, oh );
            }
            if ( !result )
            {
               EMERROR( "WstCompositorSetOutputSize failed" );
               goto exit;
            }
         }

         if ( useEmbedded )
         {
            memset( &matrix, 0, sizeof(matrix) );
            matrix[10]= matrix[15]= 1.0;
            matrix[0]= matrix[5]= scale;
            matrix[12]= transx;
            matrix[13]= transy;

            rects.clear();
            WstCompositorComposeEmbedded( wctx,
                                          0, // x
                                          0, // y
                                          ow, // width
                                          oh, // height
                                          matrix,
                                          alpha,
                                          hints,
                                          &needHolePunch,
                                          rects );
            if ( !(isBridged && (isNested || isRepeater)) && needHolePunch )
            {
               ctx->hx= rects[0].x;
               ctx->hy= rects[0].y;
               ctx->hw= rects[0].width;
               ctx->hh= rects[0].height;
            }
         }

         eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);
      }

      videoWindow= EMGetVideoWindow( emctx, EM_TUNERID_MAIN );

      EMSurfaceClientGetPosition( videoWindow, &vx, &vy, &vw, &vh );

      gst_element_set_state( pipeline, GST_STATE_NULL );

      if ( wctx )
      {
         WstCompositorDestroy( wctx );
         wctx= 0;
      }

      if ( wctx2 )
      {
         WstCompositorDestroy( wctx2 );
         wctx2= 0;
      }

      if ( wctx3 )
      {
         WstCompositorDestroy( wctx3 );
         wctx3= 0;
      }

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

      unsetenv( "WAYLAND_DISPLAY" );
      unsetenv( "WESTEROS_VPC_BRIDGE" );

      if ( (vx != vxexp) || (vy != vyexp) || (vw != vwexp) || (vh != vhexp) )
      {
         EMERROR("Unexpected video position: iteration %d expected (%d,%d,%d,%d) actual (%d,%d,%d,%d)",
                  iteration, vxexp, vyexp, vwexp, vhexp, vx, vy, vw, vh );
         goto exit;
      }
      if ( useEmbedded )
      {
         if ( (ctx->hx != vxexp) || (ctx->hy != vyexp) || (ctx->hw != vwexp) || (ctx->hh != vhexp) )
         {
            EMERROR("Unexpected hole punch position: iteration %d expected (%d,%d,%d,%d) actual (%d,%d,%d,%d)",
                     iteration, vxexp, vyexp, vwexp, vhexp, ctx->hx, ctx->hy, ctx->hw, ctx->hh );
            goto exit;
         }
      }
   }

   testResult= true;

exit:

   return testResult;
}

