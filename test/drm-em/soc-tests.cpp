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
   {
     "", "", (TESTCASEFUNC)0
   }
};

TESTCASE getSocTest( int index )
{
   return socTests[index];
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
   GstElement *pipeline= 0;
   GstElement *src= 0;
   GstElement *sink= 0;
   EMSimpleVideoDecoder *videoDecoder= 0;

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

   return testResult;
}

