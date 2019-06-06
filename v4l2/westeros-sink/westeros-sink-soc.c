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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include "westeros-sink.h"

#define DEFAULT_DEVICE_NAME "/dev/video10"
#define DEFAULT_VIDEO_SERVER "video"

#define NUM_INPUT_BUFFERS (2)
#define MIN_INPUT_BUFFERS (1)
#define NUM_OUTPUT_BUFFERS (3)
#define MIN_OUTPUT_BUFFERS (3)

#ifdef GLIB_VERSION_2_32
  #define LOCK_SOC( sink ) g_mutex_lock( &((sink)->soc.mutex) );
  #define UNLOCK_SOC( sink ) g_mutex_unlock( &((sink)->soc.mutex) );
#else
  #define LOCK_SOC( sink ) g_mutex_lock( (sink)->soc.mutex );
  #define UNLOCK_SOC( sink ) g_mutex_unlock( (sink)->soc.mutex );
#endif

GST_DEBUG_CATEGORY_EXTERN (gst_westeros_sink_debug);
#define GST_CAT_DEFAULT gst_westeros_sink_debug

enum
{
  PROP_DEVICE= PROP_SOC_BASE,
};
enum
{
   SIGNAL_FIRSTFRAME,
   MAX_SIGNAL
};

static guint g_signals[MAX_SIGNAL]= {0};

static gboolean (*queryOrg)(GstElement *element, GstQuery *query)= 0;

static void wstGetMaxFrameSize( GstWesterosSink *sink );
static bool wstGetInputFormats( GstWesterosSink *sink );
static bool wstGetOutputFormats( GstWesterosSink *sink );
static bool wstSetInputFormat( GstWesterosSink *sink );
static bool wstSetOutputFormat( GstWesterosSink *sink );
static bool wstSetupInputBuffers( GstWesterosSink *sink );
static void wstTearDownInputBuffers( GstWesterosSink *sink );
static bool wstSetupOutputBuffers( GstWesterosSink *sink );
static void wstTearDownOutputBuffers( GstWesterosSink *sink );
static int wstGetInputBuffer( GstWesterosSink *sink );
static int wstGetOutputBuffer( GstWesterosSink *sink );
static int wstFindOutputBuffer( GstWesterosSink *sink, int fd );
static WstVideoClientConnection *wstCreateVideoClientConnection( GstWesterosSink *sink, const char *name );
static void wstDestroyVideoClientConnection( WstVideoClientConnection *conn );
static void wstSendFrameVideoClientConnection( WstVideoClientConnection *conn, int frameFd );
static void wstDecoderReset( GstWesterosSink *sink );
static gpointer wstVideoOutputThread(gpointer data);
static gpointer wstEOSDetectionThread(gpointer data);
static gpointer wstDispatchThread(gpointer data);


static void sbFormat(void *data, struct wl_sb *wl_sb, uint32_t format)
{
   WESTEROS_UNUSED(wl_sb);
   GstWesterosSink *sink= (GstWesterosSink*)data;
   WESTEROS_UNUSED(sink);
   printf("westeros-sink-soc: registry: sbFormat: %X\n", format);
}

static const struct wl_sb_listener sbListener = {
	sbFormat
};

void gst_westeros_sink_soc_class_init(GstWesterosSinkClass *klass)
{
   GObjectClass *gobject_class= (GObjectClass *) klass;

   g_object_class_install_property (gobject_class, PROP_DEVICE,
     g_param_spec_string ("device",
                          "device location",
                          "Location of the device", DEFAULT_DEVICE_NAME, G_PARAM_READWRITE));

   g_signals[SIGNAL_FIRSTFRAME]= g_signal_new( "first-video-frame-callback",
                                               G_TYPE_FROM_CLASS(GST_ELEMENT_CLASS(klass)),
                                               (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                               0,    // class offset
                                               NULL, // accumulator
                                               NULL, // accu data
                                               g_cclosure_marshal_VOID__UINT_POINTER,
                                               G_TYPE_NONE,
                                               2,
                                               G_TYPE_UINT,
                                               G_TYPE_POINTER );
}

gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink )
{
   gboolean result= FALSE;

   #ifdef GLIB_VERSION_2_32
   g_mutex_init( &sink->soc.mutex );
   #else
   sink->soc.mutex= g_mutex_new();
   #endif

   sink->soc.sb= 0;
   sink->soc.activeBuffers= 0;
   sink->soc.frameRate= 0.0;
   sink->soc.frameWidth= -1;
   sink->soc.frameHeight= -1;
   sink->soc.frameInCount= 0;
   sink->soc.frameOutCount= 0;
   sink->soc.inputFormat= 0;
   sink->soc.outputFormat= WL_SB_FORMAT_NV12;
   sink->soc.devname= strdup(DEFAULT_DEVICE_NAME);
   sink->soc.v4l2Fd= -1;
   sink->soc.caps= {0};
   sink->soc.deviceCaps= 0;
   sink->soc.numInputFormats= 0;
   sink->soc.inputFormats= 0;
   sink->soc.numOutputFormats= 0;
   sink->soc.outputFormats= 0;
   sink->soc.fmtIn= {0};
   sink->soc.fmtOut= {0};
   sink->soc.formatsSet= FALSE;
   sink->soc.minBuffersIn= 0;
   sink->soc.minBuffersOut= 0;
   sink->soc.numBuffersIn= 0;
   sink->soc.inBuffers= 0;
   sink->soc.numBuffersOut= 0;
   sink->soc.outBuffers= 0;
   sink->soc.quitVideoOutputThread= FALSE;
   sink->soc.quitEOSDetectionThread= FALSE;
   sink->soc.quitDispatchThread= FALSE;
   sink->soc.videoOutputThread= NULL;
   sink->soc.eosDetectionThread= NULL;
   sink->soc.dispatchThread= NULL;
   sink->soc.videoPlaying= FALSE;
   sink->soc.nextFrameFd= -1;
   sink->soc.prevFrameFd= -1;
   sink->soc.captureEnabled= FALSE;
   sink->soc.useCaptureOnly= FALSE;
   sink->soc.framesBeforeHideVideo= 0;
   sink->soc.videoX= sink->windowX;
   sink->soc.videoY= sink->windowY;
   sink->soc.videoWidth= sink->windowWidth;
   sink->soc.videoHeight= sink->windowHeight;

   // Request caps updates
   sink->passCaps= TRUE;

   // We will use gstreamer for AV sync
   gst_base_sink_set_sync(GST_BASE_SINK(sink), TRUE);
   gst_base_sink_set_async_enabled(GST_BASE_SINK(sink), TRUE);

   if ( getenv("WESTEROS_SINK_USE_GFX") )
   {
      sink->soc.useCaptureOnly= TRUE;
      sink->soc.captureEnabled= TRUE;
      printf("westeros-sink: capture only\n");
   }

   result= TRUE;

   return result;
}

void gst_westeros_sink_soc_term( GstWesterosSink *sink )
{
   if ( sink->soc.sb )
   {
      wl_sb_destroy( sink->soc.sb );
      sink->soc.sb= 0;
   }

   if ( sink->soc.devname )
   {
      free( sink->soc.devname );
   }

   #ifdef GLIB_VERSION_2_32
   g_mutex_clear( &sink->soc.mutex );
   #else
   g_mutex_free( sink->soc.mutex );
   #endif
}

void gst_westeros_sink_soc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(object);

   WESTEROS_UNUSED(pspec);

   switch (prop_id)
   {
      case PROP_DEVICE:
         {
            const gchar *s= g_value_get_string (value);
            if ( s )
            {
               sink->soc.devname= strdup(s);
            }
         }
         break;
      default:
         G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
         break;
   }
}

void gst_westeros_sink_soc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(object);

   WESTEROS_UNUSED(pspec);

   switch (prop_id)
   {
      case PROP_DEVICE:
         g_value_set_string(value, sink->soc.devname);
         break;
      default:
         G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
         break;
   }
}

void gst_westeros_sink_soc_registryHandleGlobal( GstWesterosSink *sink,
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version)
{
   WESTEROS_UNUSED(version);
   int len;

   len= strlen(interface);

   if ((len==5) && (strncmp(interface, "wl_sb", len) == 0))
   {
      sink->soc.sb= (struct wl_sb*)wl_registry_bind(registry, id, &wl_sb_interface, version);
      printf("westeros-sink-soc: registry: sb %p\n", (void*)sink->soc.sb);
      wl_proxy_set_queue((struct wl_proxy*)sink->soc.sb, sink->queue);
		wl_sb_add_listener(sink->soc.sb, &sbListener, sink);
		printf("westeros-sink-soc: registry: done add sb listener\n");
   }

   if ( sink->soc.useCaptureOnly )
   {
      // Don't use vpc when capture only
      if ( sink->vpc )
      {
         wl_vpc_destroy( sink->vpc );
         sink->vpc= 0;
      }
   }
}

void gst_westeros_sink_soc_registryHandleGlobalRemove( GstWesterosSink *sink,
                                 struct wl_registry *registry,
			                        uint32_t name)
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(registry);
   WESTEROS_UNUSED(name);
}

gboolean gst_westeros_sink_soc_null_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   gboolean result= FALSE;

   WESTEROS_UNUSED(passToDefault);

   int rc;
   struct v4l2_exportbuffer eb;

   sink->soc.v4l2Fd= open( sink->soc.devname, O_RDWR );
   if ( sink->soc.v4l2Fd < 0 )
   {
      GST_ERROR("failed to open device (%s)", sink->soc.devname );
      goto exit;
   }

   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_QUERYCAP, &sink->soc.caps );
   if ( rc < 0 )
   {
      GST_ERROR("failed query caps: %d errno %d", rc, errno);
      goto exit;
   }

   GST_DEBUG("driver (%s) card(%s) bus_info(%s) version %d capabilities %X device_caps %X",
           sink->soc.caps.driver, sink->soc.caps.card, sink->soc.caps.bus_info, sink->soc.caps.version, sink->soc.caps.capabilities, sink->soc.caps.device_caps );

   sink->soc.deviceCaps= (sink->soc.caps.capabilities & V4L2_CAP_DEVICE_CAPS ) ? sink->soc.caps.device_caps : sink->soc.caps.capabilities;

   if ( !(sink->soc.deviceCaps & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE) ))
   {
      GST_ERROR("device (%s) is not a M2M device", sink->soc.devname );
      goto exit;
   }

   if ( !(sink->soc.deviceCaps & V4L2_CAP_STREAMING) )
   {
      GST_ERROR("device (%s) does not support dmabuf: no V4L2_CAP_STREAMING", sink->soc.devname );
      goto exit;
   }

   eb.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
   eb.index= -1;
   eb.plane= -1;
   eb.flags= (O_RDWR|O_CLOEXEC);
   ioctl( sink->soc.v4l2Fd, VIDIOC_EXPBUF, &eb );
   if ( errno == ENOTTY )
   {
      GST_ERROR("device (%s) does not support dmabuf: no VIDIOC_EXPBUF", sink->soc.devname );
      goto exit;
   }

   wstGetInputFormats( sink );

   wstGetOutputFormats( sink );

   wstGetMaxFrameSize( sink );

   if ( !sink->soc.useCaptureOnly )
   {
      sink->soc.conn= wstCreateVideoClientConnection( sink, DEFAULT_VIDEO_SERVER );
      if ( !sink->soc.conn )
      {
         GST_ERROR("unable to connect to video server (%s)", DEFAULT_VIDEO_SERVER );
         goto exit;
      }
   }

   result= TRUE;

exit:
   return result;
}

gboolean gst_westeros_sink_soc_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);

   LOCK(sink);
   sink->startAfterCaps= TRUE;
   sink->soc.videoPlaying= TRUE;
   UNLOCK(sink);

   return TRUE;
}

gboolean gst_westeros_sink_soc_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);

   LOCK( sink );
   sink->soc.videoPlaying= TRUE;
   UNLOCK( sink );

   return TRUE;
}

gboolean gst_westeros_sink_soc_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   LOCK( sink );
   sink->videoStarted= FALSE;
   sink->soc.videoPlaying= FALSE;
   UNLOCK( sink );

   if (gst_base_sink_is_async_enabled(GST_BASE_SINK(sink)))
   {
      // To complete transition to paused state in async_enabled mode, we need a preroll buffer pushed to the pad;
      // This is a workaround to avoid the need for preroll buffer.
      GstBaseSink *basesink;
      basesink = GST_BASE_SINK(sink);
      GST_BASE_SINK_PREROLL_LOCK (basesink);
      basesink->have_preroll = 1;
      GST_BASE_SINK_PREROLL_UNLOCK (basesink);

      *passToDefault= true;
   }
   else
   {
      *passToDefault = false;
   }

   return TRUE;
}

gboolean gst_westeros_sink_soc_paused_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   LOCK( sink );
   sink->videoStarted= FALSE;
   UNLOCK( sink );

   if (gst_base_sink_is_async_enabled(GST_BASE_SINK(sink)))
   {
      *passToDefault= true;
   }
   else
   {
      *passToDefault= false;
   }

   return TRUE;
}

gboolean gst_westeros_sink_soc_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(sink);

   if ( sink->soc.conn )
   {
      wstDestroyVideoClientConnection( sink->soc.conn );
   }

   sink->soc.quitVideoOutputThread= TRUE;
   sink->soc.quitEOSDetectionThread= TRUE;
   sink->soc.quitDispatchThread= TRUE;

   wstTearDownInputBuffers( sink );

   wstTearDownOutputBuffers( sink );

   sink->soc.prevFrameFd= -1;
   sink->soc.nextFrameFd= -1;
   sink->soc.formatsSet= FALSE;

   if ( sink->soc.inputFormats )
   {
      free( sink->soc.inputFormats );
   }
   if ( sink->soc.outputFormats )
   {
      free( sink->soc.outputFormats );
   }
   if ( sink->soc.v4l2Fd >= 0 )
   {
      close( sink->soc.v4l2Fd );
      sink->soc.v4l2Fd= -1;
   }

   if ( sink->soc.videoOutputThread )
   {
      g_thread_join( sink->soc.videoOutputThread );
      sink->soc.videoOutputThread= NULL;
   }

   if ( sink->soc.eosDetectionThread )
   {
      g_thread_join( sink->soc.eosDetectionThread );
      sink->soc.eosDetectionThread= NULL;
   }

   *passToDefault= false;

   return TRUE;
}

gboolean gst_westeros_sink_soc_accept_caps( GstWesterosSink *sink, GstCaps *caps )
{
   bool result= FALSE;
   GstStructure *structure;
   const gchar *mime;
   int len;
   bool frameSizeChange= false;
   bool readyToStart= false;

   gchar *str= gst_caps_to_string(caps);
   g_print("westeros-sink: caps: (%s)\n", str);
   g_free( str );

   structure= gst_caps_get_structure(caps, 0);
   if( structure )
   {
      mime= gst_structure_get_name(structure);
      if ( mime )
      {
         len= strlen(mime);
         if ( (len == 12) && !strncmp("video/x-h264", mime, len) )
         {
            sink->soc.inputFormat= V4L2_PIX_FMT_H264;
            result= TRUE;
         }
         else if ( (len == 10) && !strncmp("video/mpeg", mime, len) )
         {
            sink->soc.inputFormat= V4L2_PIX_FMT_MPEG2;
            result= TRUE;
         }
         else
         {
            GST_ERROR("gst_westeros_sink_soc_accept_caps: not accepting caps (%s)", mime );
         }
      }

      if ( result == TRUE )
      {
         gint num, denom, width, height;
         if ( gst_structure_get_fraction( structure, "framerate", &num, &denom ) )
         {
            if ( denom == 0 ) denom= 1;
            sink->soc.frameRate= (double)num/(double)denom;
            if ( sink->soc.frameRate <= 0.0 )
            {
               g_print("westeros-sink: caps have framerate of 0 - assume 60\n");
               sink->soc.frameRate= 60.0;
            }
         }
         if ( gst_structure_get_int( structure, "width", &width ) )
         {
            if ( (sink->soc.frameWidth != -1) && (sink->soc.frameWidth != width) )
            {
               frameSizeChange= true;
            }
            sink->soc.frameWidth= width;
         }
         if ( gst_structure_get_int( structure, "height", &height ) )
         {
            if ( (sink->soc.frameHeight != -1) && (sink->soc.frameHeight != height) )
            {
               frameSizeChange= true;
            }
            sink->soc.frameHeight= height;
         }

         if ( frameSizeChange )
         {
            g_print("westeros-sink: frame size change : %dx%d\n", sink->soc.frameWidth, sink->soc.frameHeight);
            wstDecoderReset( sink );
         }

         if ( (sink->soc.formatsSet == FALSE) &&
              (sink->soc.frameWidth > 0) &&
              (sink->soc.frameHeight > 0) &&
              (sink->soc.frameRate > 0.0) )
         {
            wstSetInputFormat( sink );
            wstSetOutputFormat( sink );
            wstSetupInputBuffers( sink );
            wstSetupOutputBuffers( sink );
            sink->soc.formatsSet= TRUE;

            readyToStart= true;
         }
      }

      if ( !sink->videoStarted && sink->startAfterCaps && readyToStart )
      {
         GST_DEBUG("have caps: starting video");
         sink->startAfterCaps= FALSE;
         if ( !gst_westeros_sink_soc_start_video( sink ) )
         {
            GST_ERROR("gst_westeros_sink_soc_accept_caps: gst_westeros_sink_soc_start_video failed");
         }
      }
   }

   return result;
}

void gst_westeros_sink_soc_set_startPTS( GstWesterosSink *sink, gint64 pts )
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(pts);
}

void gst_westeros_sink_soc_render( GstWesterosSink *sink, GstBuffer *buffer )
{
   if ( sink->soc.videoPlaying && !sink->flushStarted )
   {
      gint64 nanoTime;
      int rc, buffIndex;
      int inSize, offset, avail, copylen;
      unsigned char *inData;

      #ifdef USE_GST1
      GstMapInfo map;
      gst_buffer_map(buffer, &map, (GstMapFlags)GST_MAP_READ);
      inSize= map.size;
      inData= map.data;
      #else
      inSize= (int)GST_BUFFER_SIZE(buffer);
      inData= GST_BUFFER_DATA(buffer);
      #endif

      GST_LOG("gst_westeros_sink_soc_render: buffer %p, len %d timestamp: %lld", buffer, inSize, GST_BUFFER_PTS(buffer) );

      nanoTime= GST_BUFFER_PTS(buffer);

      if ( inSize )
      {
         avail= inSize;
         offset= 0;
         while( offset < inSize )
         {
            buffIndex= wstGetInputBuffer( sink );
            if ( buffIndex < 0 )
            {
               GST_ERROR("gst_westeros_sink_soc_render: unable to get input buffer");
               goto exit;
            }

            if ( !sink->soc.videoPlaying || sink->flushStarted )
            {
               break;
            }

            copylen= sink->soc.inBuffers[buffIndex].capacity;
            if ( copylen > avail )
            {
               copylen= avail;
            }

            memcpy( sink->soc.inBuffers[buffIndex].start, &inData[offset], copylen );

            offset += copylen;
            avail -= copylen;

            sink->soc.inBuffers[buffIndex].buf.bytesused= copylen;
            rc= ioctl( sink->soc.v4l2Fd, VIDIOC_QBUF, &sink->soc.inBuffers[buffIndex].buf );
            if ( rc < 0 )
            {
               GST_ERROR("gst_westeros_sink_soc_render: queuing input buffer failed: rc %d errno %d", rc, errno );
               goto exit;
            }

            ++sink->soc.frameInCount;
         }
      }

      #ifdef USE_GST1
      gst_buffer_unmap( buffer, &map);
      #endif
   }

exit:
   return;
}

void gst_westeros_sink_soc_flush( GstWesterosSink *sink )
{
   LOCK(sink);
   sink->soc.frameOutCount= 0;
   UNLOCK(sink);
}

gboolean gst_westeros_sink_soc_start_video( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   int rc;

   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_STREAMON, &sink->soc.fmtIn.type );
   if ( rc < 0 )
   {
      GST_ERROR("streamon failed for input: rc %d errno %d", rc, errno );
      goto exit;
   }

   if ( sink->display )
   {
      sink->soc.quitDispatchThread= FALSE;
      if ( sink->soc.dispatchThread == NULL )
      {
         GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: starting westeos_sink_dispatch thread");
         sink->soc.dispatchThread= g_thread_new("westeros_sink_dispatch", wstDispatchThread, sink);
      }
   }

   sink->soc.quitVideoOutputThread= FALSE;
   if ( sink->soc.videoOutputThread == NULL )
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: starting westeos_sink_video_output thread");
      sink->soc.videoOutputThread= g_thread_new("westeros_sink_video_output", wstVideoOutputThread, sink);
   }

   sink->soc.quitEOSDetectionThread= FALSE;
   if ( sink->soc.eosDetectionThread == NULL )
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: starting westeos_sink_eos thread");
      sink->soc.eosDetectionThread= g_thread_new("westeros_sink_eos", wstEOSDetectionThread, sink);
   }

   sink->videoStarted= TRUE;

   result= TRUE;

exit:
   return result;
}

void gst_westeros_sink_soc_eos_event( GstWesterosSink *sink )
{
   WESTEROS_UNUSED(sink);
}

void gst_westeros_sink_soc_set_video_path( GstWesterosSink *sink, bool useGfxPath )
{
   if ( useGfxPath && !sink->soc.captureEnabled )
   {
      sink->soc.captureEnabled= TRUE;

      sink->soc.framesBeforeHideVideo= 2;
   }
   else if ( !useGfxPath && sink->soc.captureEnabled )
   {
      sink->soc.captureEnabled= FALSE;
      sink->soc.prevFrameFd= -1;
      sink->soc.nextFrameFd= -1;

      wl_surface_attach( sink->surface, 0, sink->windowX, sink->windowY );
      wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
      wl_surface_commit( sink->surface );
      wl_display_flush(sink->display);
      wl_display_dispatch_queue_pending(sink->display, sink->queue);
   }
}

void gst_westeros_sink_soc_update_video_position( GstWesterosSink *sink )
{
   sink->soc.videoX= ((sink->windowX*sink->scaleXNum)/sink->scaleXDenom) + sink->transX;
   sink->soc.videoY= ((sink->windowY*sink->scaleYNum)/sink->scaleYDenom) + sink->transY;
   sink->soc.videoWidth= (sink->windowWidth*sink->scaleXNum)/sink->scaleXDenom;
   sink->soc.videoHeight= (sink->windowHeight*sink->scaleYNum)/sink->scaleYDenom;
   if ( !sink->windowSizeOverride )
   {
      double sizeXFactor= ((double)sink->outputWidth)/DEFAULT_WINDOW_WIDTH;
      double sizeYFactor= ((double)sink->outputHeight)/DEFAULT_WINDOW_HEIGHT;
      sink->soc.videoWidth *= sizeXFactor;
      sink->soc.videoHeight *= sizeYFactor;
   }

   if ( !sink->soc.captureEnabled )
   {
      // Send a buffer to compositor to update hole punch geometry
      if ( sink->soc.sb )
      {
         struct wl_buffer *buff;

         buff= wl_sb_create_buffer( sink->soc.sb,
                                    0,
                                    sink->windowWidth,
                                    sink->windowHeight,
                                    sink->windowWidth*4,
                                    WL_SB_FORMAT_ARGB8888 );
         wl_surface_attach( sink->surface, buff, sink->windowX, sink->windowY );
         wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
         wl_surface_commit( sink->surface );
      }
   }
}

gboolean gst_westeros_sink_soc_query( GstWesterosSink *sink, GstQuery *query )
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(query);

   return FALSE;
}

static void wstGetMaxFrameSize( GstWesterosSink *sink )
{
   struct v4l2_frmsizeenum framesize;
   int rc;
   int maxWidth= 0, maxHeight= 0;

   memset( &framesize, 0, sizeof(struct v4l2_frmsizeenum) );
   framesize.index= 0;
   framesize.pixel_format= V4L2_PIX_FMT_NV12;

   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_ENUM_FRAMESIZES, &framesize);
   if ( rc == 0 )
   {
      if ( framesize.type == V4L2_FRMSIZE_TYPE_DISCRETE )
      {
         maxWidth= framesize.discrete.width;
         maxHeight= framesize.discrete.height;
         while ( rc == 0 )
         {
            ++framesize.index;
            rc= ioctl( sink->soc.v4l2Fd, VIDIOC_ENUM_FRAMESIZES, &framesize);
            if ( rc == 0 )
            {
               if ( framesize.discrete.width > maxWidth )
               {
                  maxWidth= framesize.discrete.width;
               }
               if ( framesize.discrete.height > maxHeight )
               {
                  maxHeight= framesize.discrete.height;
               }
            }
            else
            {
               break;
            }
         }
      }
      else if ( framesize.type == V4L2_FRMSIZE_TYPE_STEPWISE )
      {
         sink->maxWidth= framesize.stepwise.max_width;
         sink->maxHeight= framesize.stepwise.max_height;
      }
   }
   else
   {
      GST_ERROR("wstGetMaxFrameSize: VIDIOC_ENUM_FRAMESIZES error %d", rc);
      maxWidth= 1920;
      maxHeight= 1080;
   }
   if ( (maxWidth > 0) && (maxHeight > 0) )
   {
      g_print("westeros-sink: max frame (%dx%d)\n", maxWidth, maxHeight);
      sink->maxWidth= maxWidth;
      sink->maxHeight= maxHeight;
   }
}

static bool wstGetInputFormats( GstWesterosSink *sink )
{
   bool result= false;
   struct v4l2_fmtdesc format;
   int i, rc;

   i= 0;
   for( ; ; )
   {
      format.index= i;
      format.type= V4L2_BUF_TYPE_VIDEO_OUTPUT;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_ENUM_FMT, &format);
      if ( rc < 0 )
      {
         if ( errno == EINVAL )
         {
            GST_DEBUG("Found %d input formats", i);
            sink->soc.numInputFormats= i;
            break;
         }
         goto exit;
      }
      ++i;
   }

   sink->soc.inputFormats= (struct v4l2_fmtdesc*)calloc( sink->soc.numInputFormats, sizeof(struct v4l2_format) );
   if ( !sink->soc.inputFormats )
   {
      GST_ERROR("getInputFormats: no memory for inputFormats");
      sink->soc.numInputFormats= 0;
      goto exit;
   }

   for( i= 0; i < sink->soc.numInputFormats; ++i)
   {
      sink->soc.inputFormats[i].index= i;
      sink->soc.inputFormats[i].type= V4L2_BUF_TYPE_VIDEO_OUTPUT;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_ENUM_FMT, &sink->soc.inputFormats[i]);
      if ( rc < 0 )
      {
         goto exit;
      }
      GST_DEBUG("input format %d: flags %08x pixelFormat: %x desc: %s",
             i, sink->soc.inputFormats[i].flags, sink->soc.inputFormats[i].pixelformat, sink->soc.inputFormats[i].description );
   }

   result= true;

exit:
   return result;
}

static bool wstGetOutputFormats( GstWesterosSink *sink )
{
   bool result= false;
   struct v4l2_fmtdesc format;
   int i, rc;

   i= 0;
   for( ; ; )
   {
      format.index= i;
      format.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_ENUM_FMT, &format);
      if ( rc < 0 )
      {
         if ( errno == EINVAL )
         {
            GST_DEBUG("Found %d output formats", i);
            sink->soc.numOutputFormats= i;
            break;
         }
         goto exit;
      }
      ++i;
   }

   sink->soc.outputFormats= (struct v4l2_fmtdesc*)calloc( sink->soc.numOutputFormats, sizeof(struct v4l2_format) );
   if ( !sink->soc.outputFormats )
   {
      GST_DEBUG("getOutputFormats: no memory for outputFormats");
      sink->soc.numOutputFormats= 0;
      goto exit;
   }

   for( i= 0; i < sink->soc.numOutputFormats; ++i)
   {
      sink->soc.outputFormats[i].index= i;
      sink->soc.outputFormats[i].type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_ENUM_FMT, &sink->soc.outputFormats[i]);
      if ( rc < 0 )
      {
         goto exit;
      }
      GST_DEBUG("output format %d: flags %08x pixelFormat: %x desc: %s",
             i, sink->soc.outputFormats[i].flags, sink->soc.outputFormats[i].pixelformat, sink->soc.outputFormats[i].description );
   }

   result= true;

exit:
   return result;
}

static bool wstSetInputFormat( GstWesterosSink *sink )
{
   bool result= false;
   int rc;

   memset( &sink->soc.fmtIn, 0, sizeof(struct v4l2_format) );
   sink->soc.fmtIn.type= V4L2_BUF_TYPE_VIDEO_OUTPUT;
   sink->soc.fmtIn.fmt.pix.pixelformat= sink->soc.inputFormat;
   sink->soc.fmtIn.fmt.pix.width= sink->soc.frameWidth;
   sink->soc.fmtIn.fmt.pix.height= sink->soc.frameHeight;
   sink->soc.fmtIn.fmt.pix.sizeimage= 1024*1024;
   sink->soc.fmtIn.fmt.pix.field= V4L2_FIELD_NONE;
   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_S_FMT, &sink->soc.fmtIn );
   if ( rc < 0 )
   {
      GST_DEBUG("wstSetInputFormat: failed to format for input: rc %d errno %d", rc, errno);
      goto exit;
   }

   result= true;

exit:
   return result;
}


static bool wstSetOutputFormat( GstWesterosSink *sink )
{
   bool result= false;
   int rc;

   memset( &sink->soc.fmtOut, 0, sizeof(struct v4l2_format) );
   sink->soc.fmtOut.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
   sink->soc.fmtOut.fmt.pix.pixelformat= V4L2_PIX_FMT_NV12;
   sink->soc.fmtOut.fmt.pix.width= sink->soc.frameWidth;
   sink->soc.fmtOut.fmt.pix.height= sink->soc.frameHeight;
   sink->soc.fmtOut.fmt.pix.sizeimage= (sink->soc.fmtOut.fmt.pix.width*sink->soc.fmtOut.fmt.pix.height*3)/2;
   sink->soc.fmtOut.fmt.pix.field= V4L2_FIELD_NONE;
   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_S_FMT, &sink->soc.fmtOut );
   if ( rc < 0 )
   {
      GST_DEBUG("wstSetOutputFormat: initV4l2: failed to format for input: rc %d errno %d", rc, errno);
      goto exit;
   }

   result= true;

exit:
   return result;
}

static bool wstSetupInputBuffers( GstWesterosSink *sink )
{
   bool result= false;
   int rc, neededBuffers;
   struct v4l2_control ctl;
   struct v4l2_requestbuffers reqbuf;
   struct v4l2_buffer *bufIn;
   void *bufStart;

   neededBuffers= NUM_INPUT_BUFFERS;

   memset( &ctl, 0, sizeof(ctl));
   ctl.id= V4L2_CID_MIN_BUFFERS_FOR_OUTPUT;
   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_G_CTRL, &ctl );
   if ( rc == 0 )
   {
      sink->soc.minBuffersIn= ctl.value;
   }

   if ( sink->soc.minBuffersIn == 0 )
   {
      sink->soc.minBuffersIn= MIN_INPUT_BUFFERS;
   }

   memset( &reqbuf, 0, sizeof(reqbuf) );
   reqbuf.count= neededBuffers;
   reqbuf.type= V4L2_BUF_TYPE_VIDEO_OUTPUT;
   reqbuf.memory= V4L2_MEMORY_MMAP;
   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_REQBUFS, &reqbuf );
   if ( rc < 0 )
   {
      GST_ERROR("wstSetupInputBuffers: failed to request %d mmap buffers for input: rc %d errno %d", neededBuffers, rc, errno);
      goto exit;
   }
   sink->soc.numBuffersIn= reqbuf.count;

   if ( reqbuf.count < sink->soc.minBuffersIn )
   {
      GST_ERROR("wstSetupInputBuffers: insufficient buffers: (%d versus %d)", reqbuf.count, neededBuffers );
      goto exit;
   }

   sink->soc.inBuffers= (WstBufferInfo*)calloc( reqbuf.count, sizeof(WstBufferInfo) );
   if ( !sink->soc.inBuffers )
   {
      GST_ERROR("wstSetupInputBuffers: no memory for WstBufferInfo" );
      goto exit;
   }

   for( int i= 0; i < reqbuf.count; ++i )
   {
      bufIn= &sink->soc.inBuffers[i].buf;
      bufIn->type= V4L2_BUF_TYPE_VIDEO_OUTPUT;
      bufIn->index= i;
      bufIn->memory= V4L2_MEMORY_MMAP;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_QUERYBUF, bufIn );
      if ( rc < 0 )
      {
         GST_ERROR("wstSetupInputBuffers: failed to query input buffer %d: rc %d errno %d", i, rc, errno);
         goto exit;
      }

      bufStart= mmap( NULL,
                      bufIn->length,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      sink->soc.v4l2Fd,
                      bufIn->m.offset );
      if ( bufStart == MAP_FAILED )
      {
         GST_ERROR("wstSetupInputBuffers: failed to mmap input buffer %d: errno %d", i, errno);
         goto exit;
      }

      GST_DEBUG("Input buffer: %d", i);
      GST_DEBUG("  index: %d start: %p bytesUsed %d  offset %d length %d flags %08x",
              bufIn->index, bufStart, bufIn->bytesused, bufIn->m.offset, bufIn->length, bufIn->flags );

      sink->soc.inBuffers[i].fd= -1;
      sink->soc.inBuffers[i].start= bufStart;
      sink->soc.inBuffers[i].capacity= bufIn->length;
   }

   result= true;

exit:

   if ( !result )
   {
      wstTearDownInputBuffers( sink );
   }

   return result;
}

static void wstTearDownInputBuffers( GstWesterosSink *sink )
{
   int rc;
   struct v4l2_requestbuffers reqbuf;

   if ( sink->soc.inBuffers )
   {
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_STREAMOFF, &sink->soc.fmtIn.type );
      if ( rc < 0 )
      {
         GST_ERROR("wstTearDownInputBuffers: streamoff failed for input: rc %d errno %d", rc, errno );
      }

      for( int i= 0; i < sink->soc.numBuffersIn; ++i )
      {
         if ( sink->soc.inBuffers[i].start )
         {
            munmap( sink->soc.inBuffers[i].start, sink->soc.inBuffers[i].capacity );
         }
      }
      free( sink->soc.inBuffers );
      sink->soc.inBuffers= 0;
   }

   if ( sink->soc.numBuffersIn )
   {
      memset( &reqbuf, 0, sizeof(reqbuf) );
      reqbuf.count= 0;
      reqbuf.type= V4L2_BUF_TYPE_VIDEO_OUTPUT;
      reqbuf.memory= V4L2_MEMORY_MMAP;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_REQBUFS, &reqbuf );
      if ( rc < 0 )
      {
         GST_ERROR("wstTearDownInputBuffers: failed to release v4l2 buffers for input: rc %d errno %d", rc, errno);
      }
      sink->soc.numBuffersIn= 0;
   }
}

static bool wstSetupOutputBuffers( GstWesterosSink *sink )
{
   bool result= false;
   int rc, neededBuffers;
   struct v4l2_control ctl;
   struct v4l2_requestbuffers reqbuf;
   struct v4l2_buffer *bufOut;
   struct v4l2_exportbuffer expbuf;
   void *bufStart;

   neededBuffers= NUM_OUTPUT_BUFFERS;

   memset( &ctl, 0, sizeof(ctl));
   ctl.id= V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_G_CTRL, &ctl );
   if ( rc == 0 )
   {
      sink->soc.minBuffersOut= ctl.value;
   }

   if ( sink->soc.minBuffersOut == 0 )
   {
      sink->soc.minBuffersOut= MIN_OUTPUT_BUFFERS;
   }

   memset( &reqbuf, 0, sizeof(reqbuf) );
   reqbuf.count= neededBuffers;
   reqbuf.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
   reqbuf.memory= V4L2_MEMORY_MMAP;
   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_REQBUFS, &reqbuf );
   if ( rc < 0 )
   {
      GST_ERROR("wstSetupOutputBuffers: failed to request %d mmap buffers for output: rc %d errno %d", neededBuffers, rc, errno);
      goto exit;
   }
   sink->soc.numBuffersOut= reqbuf.count;

   if ( reqbuf.count < sink->soc.minBuffersOut )
   {
      GST_ERROR("wstSetupOutputBuffers: insufficient buffers: (%d versus %d)", reqbuf.count, neededBuffers );
      goto exit;
   }

   sink->soc.outBuffers= (WstBufferInfo*)calloc( reqbuf.count, sizeof(WstBufferInfo) );
   if ( !sink->soc.outBuffers )
   {
      GST_ERROR("wstSetupOutputBuffers: no memory for WstBufferInfo" );
      goto exit;
   }

   for( int i= 0; i < reqbuf.count; ++i )
   {
      sink->soc.outBuffers[i].fd= -1;
   }

   for( int i= 0; i < reqbuf.count; ++i )
   {
      bufOut= &sink->soc.outBuffers[i].buf;
      bufOut->type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
      bufOut->index= i;
      bufOut->memory= V4L2_MEMORY_MMAP;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_QUERYBUF, bufOut );
      if ( rc < 0 )
      {
         GST_ERROR("wstSetupOutputBuffers: failed to query input buffer %d: rc %d errno %d", i, rc, errno);
         goto exit;
      }

      GST_DEBUG("Output buffer: %d", i);
      GST_DEBUG("  index: %d bytesUsed %d length %d flags %08x",
             bufOut->index, bufOut->bytesused, bufOut->length, bufOut->flags );

      memset( &expbuf, 0, sizeof(expbuf) );
      expbuf.type= bufOut->type;
      expbuf.index= i;
      expbuf.flags= O_CLOEXEC;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_EXPBUF, &expbuf );
      if ( rc < 0 )
      {
         GST_ERROR("wstSetupOutputBuffers: failed to export v4l2 output buffer %d: rc %d errno %d", i, rc, errno);
      }
      GST_DEBUG("  index %d export fd %d", expbuf.index, expbuf.fd );

      sink->soc.outBuffers[i].fd= expbuf.fd;
      sink->soc.outBuffers[i].capacity= bufOut->length;
   }

   result= true;

exit:

   if ( !result )
   {
      wstTearDownOutputBuffers( sink );
   }

   return result;
}

static void wstTearDownOutputBuffers( GstWesterosSink *sink )
{
   int rc;
   struct v4l2_requestbuffers reqbuf;

   if ( sink->soc.outBuffers )
   {
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_STREAMOFF, &sink->soc.fmtOut.type );
      if ( rc < 0 )
      {
         GST_ERROR("wstTearDownOutputBuffers: streamoff failed for output: rc %d errno %d", rc, errno );
      }

      for( int i= 0; i < sink->soc.numBuffersOut; ++i )
      {
         if ( sink->soc.outBuffers[i].fd >= 0 )
         {
            close( sink->soc.outBuffers[i].fd );
            sink->soc.outBuffers[i].fd= -1;
         }
      }

      free( sink->soc.outBuffers );
      sink->soc.outBuffers= 0;
   }

   if ( sink->soc.numBuffersOut )
   {
      memset( &reqbuf, 0, sizeof(reqbuf) );
      reqbuf.count= 0;
      reqbuf.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
      reqbuf.memory= V4L2_MEMORY_MMAP;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_REQBUFS, &reqbuf );
      if ( rc < 0 )
      {
         GST_ERROR("wstTearDownOutputBuffers: failed to release v4l2 buffers for output: rc %d errno %d", rc, errno);
      }
      sink->soc.numBuffersOut= 0;
   }
}

static int wstGetInputBuffer( GstWesterosSink *sink )
{
   int bufferIndex= -1;
   int i;
   for( i= 0; i < sink->soc.numBuffersIn; ++i )
   {
      if ( !(sink->soc.inBuffers[i].buf.flags & V4L2_BUF_FLAG_QUEUED) ||
           sink->soc.inBuffers[i].buf.flags & V4L2_BUF_FLAG_DONE )
      {
         bufferIndex= i;
         break;
      }
   }

   if ( bufferIndex < 0 )
   {
      int rc;
      struct v4l2_buffer buf;
      memset( &buf, 0, sizeof(buf));
      buf.type= sink->soc.fmtIn.type;
      buf.memory= V4L2_MEMORY_MMAP;
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_DQBUF, &buf );
      if ( rc == 0 )
      {
         bufferIndex= buf.index;
         sink->soc.inBuffers[bufferIndex].buf= buf;
      }
   }

   return bufferIndex;
}

static int wstGetOutputBuffer( GstWesterosSink *sink )
{
   int bufferIndex= -1;
   int rc;
   struct v4l2_buffer buf;

   memset( &buf, 0, sizeof(buf));
   buf.type= sink->soc.fmtOut.type;
   buf.memory= V4L2_MEMORY_MMAP;
   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_DQBUF, &buf );
   if ( rc == 0 )
   {
      bufferIndex= buf.index;
      sink->soc.outBuffers[bufferIndex].buf= buf;
   }

   return bufferIndex;
}

static int wstFindOutputBuffer( GstWesterosSink *sink, int fd )
{
   int bufferIndex= -1;
   int i;

   for( i= 0; i < sink->soc.numBuffersOut; ++i )
   {
      if ( sink->soc.outBuffers[i].fd == fd )
      {
         bufferIndex= i;
         break;
      }
   }

   return bufferIndex;
}

static WstVideoClientConnection *wstCreateVideoClientConnection( GstWesterosSink *sink, const char *name )
{
   WstVideoClientConnection *conn= 0;
   int rc;
   bool error= true;
   const char *workingDir;
   int pathNameLen, addressSize;

   conn= (WstVideoClientConnection*)calloc( 1, sizeof(WstVideoClientConnection));
   if ( conn )
   {
      conn->socketFd= -1;
      conn->name= name;
      conn->sink= sink;

      workingDir= getenv("XDG_RUNTIME_DIR");
      if ( !workingDir )
      {
         GST_ERROR("wstCreateVideoClientConnection: XDG_RUNTIME_DIR is not set");
         goto exit;
      }

      pathNameLen= strlen(workingDir)+strlen("/")+strlen(conn->name)+1;
      if ( pathNameLen > (int)sizeof(conn->addr.sun_path) )
      {
         GST_ERROR("wstCreateVideoClientConnection: name for server unix domain socket is too long: %d versus max %d",
                pathNameLen, (int)sizeof(conn->addr.sun_path) );
         goto exit;
      }

      conn->addr.sun_family= AF_LOCAL;
      strcpy( conn->addr.sun_path, workingDir );
      strcat( conn->addr.sun_path, "/" );
      strcat( conn->addr.sun_path, conn->name );

      conn->socketFd= socket( PF_LOCAL, SOCK_STREAM|SOCK_CLOEXEC, 0 );
      if ( conn->socketFd < 0 )
      {
         GST_ERROR("wstCreateVideoClientConnection: unable to open socket: errno %d", errno );
         goto exit;
      }

      addressSize= pathNameLen + offsetof(struct sockaddr_un, sun_path);

      rc= connect(conn->socketFd, (struct sockaddr *)&conn->addr, addressSize );
      if ( rc < 0 )
      {
         GST_ERROR("wstCreateVideoClientConnection: connect failed for socket: errno %d", errno );
         goto exit;
      }

      error= false;
   }

exit:

   if ( error )
   {
      wstDestroyVideoClientConnection( conn );
      conn= 0;
   }

   return conn;
}

static void wstDestroyVideoClientConnection( WstVideoClientConnection *conn )
{
   if ( conn )
   {
      conn->addr.sun_path[0]= '\0';

      if ( conn->socketFd >= 0 )
      {
         close( conn->socketFd );
         conn->socketFd= -1;
      }

      free( conn );
      conn= 0;
   }
}

static int putU32( unsigned char *p, unsigned n )
{
   p[0]= (n>>24);
   p[1]= (n>>16);
   p[2]= (n>>8);
   p[3]= (n&0xFF);

   return 4;
}

static void wstSendFrameVideoClientConnection( WstVideoClientConnection *conn, int frameFd )
{
   int sentLen;

   if ( conn  )
   {
      struct msghdr msg;
      struct cmsghdr *cmsg;
      struct iovec iov[1];
      unsigned char mbody[1+4+4+4+4+4+4+4];
      char cmbody[CMSG_SPACE(sizeof(int))];
      int i, len;
      int *fd;
      int fdToSend;

      if ( frameFd >= 0 )
      {
         fdToSend= fcntl( frameFd, F_DUPFD_CLOEXEC, 0 );
         if ( fdToSend < 0 )
         {
            GST_ERROR("wstSendFrameVideoClientConnection: failed to dup");
            return;
         }

         i= 0;
         mbody[i++]= 'F';
         i += putU32( &mbody[i], conn->sink->soc.frameWidth );
         i += putU32( &mbody[i], conn->sink->soc.frameHeight );
         i += putU32( &mbody[i], conn->sink->soc.fmtOut.fmt.pix.pixelformat );
         i += putU32( &mbody[i], conn->sink->soc.videoX );
         i += putU32( &mbody[i], conn->sink->soc.videoY );
         i += putU32( &mbody[i], conn->sink->soc.videoWidth );
         i += putU32( &mbody[i], conn->sink->soc.videoHeight );

         iov[0].iov_base= (char*)mbody;
         iov[0].iov_len= i;

         cmsg= (struct cmsghdr*)cmbody;
         cmsg->cmsg_len= CMSG_LEN(sizeof(int));
         cmsg->cmsg_level= SOL_SOCKET;
         cmsg->cmsg_type= SCM_RIGHTS;

         msg.msg_name= NULL;
         msg.msg_namelen= 0;
         msg.msg_iov= iov;
         msg.msg_iovlen= 1;
         msg.msg_control= cmsg;
         msg.msg_controllen= cmsg->cmsg_len;
         msg.msg_flags= 0;

         fd= (int*)CMSG_DATA(cmsg);
         *fd= fdToSend;
      }
      else
      {
         i= 0;
         mbody[i++]= 'H';

         iov[0].iov_base= (char*)mbody;
         iov[0].iov_len= i;

         msg.msg_name= NULL;
         msg.msg_namelen= 0;
         msg.msg_iov= iov;
         msg.msg_iovlen= 1;
         msg.msg_control= 0;
         msg.msg_controllen= 0;
         msg.msg_flags= 0;

         fdToSend= -1;
      }

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, 0 );
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( fdToSend >= 0 )
      {
         close( fdToSend );
      }
   }
}

static void wstDecoderReset( GstWesterosSink *sink )
{
   long long delay;

   if ( sink->soc.frameRate > 0 )
   {
      delay= 1000000/sink->soc.frameRate;
   }
   delay= ((sink->soc.frameRate > 0) ? 1000000/sink->soc.frameRate : 1000000/60);
   usleep( delay );

   sink->soc.quitVideoOutputThread= TRUE;

   wstTearDownInputBuffers( sink );

   wstTearDownOutputBuffers( sink );

   if ( sink->soc.videoOutputThread )
   {
      g_thread_join( sink->soc.videoOutputThread );
      sink->soc.videoOutputThread= NULL;
   }

   if ( sink->soc.v4l2Fd >= 0 )
   {
      close( sink->soc.v4l2Fd );
      sink->soc.v4l2Fd= -1;
   }

   sink->soc.v4l2Fd= open( sink->soc.devname, O_RDWR );
   if ( sink->soc.v4l2Fd < 0 )
   {
      GST_ERROR("failed to open device (%s)", sink->soc.devname );
   }

   sink->videoStarted= FALSE;
   sink->startAfterCaps= TRUE;
   sink->soc.prevFrameFd= -1;
   sink->soc.nextFrameFd= -1;
   sink->soc.formatsSet= FALSE;
}

typedef struct bufferInfo
{
   GstWesterosSink *sink;
   int buffIndex;
} bufferInfo;

static void buffer_release( void *data, struct wl_buffer *buffer )
{
   int rc;
   bufferInfo *binfo= (bufferInfo*)data;

   GstWesterosSink *sink= binfo->sink;

   if ( binfo->buffIndex >= 0 )
   {
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_QBUF, &sink->soc.outBuffers[binfo->buffIndex].buf );
      if ( rc < 0 )
      {
         GST_ERROR("failed to re-queue output buffer: rc %d errno %d", rc, errno);
      }
   }

   --sink->soc.activeBuffers;
   wl_buffer_destroy( buffer );

   free( binfo );
}

static struct wl_buffer_listener wl_buffer_listener=
{
   buffer_release
};

static gpointer wstVideoOutputThread(gpointer data)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   struct v4l2_selection selection;
   int i, buffIndex, rc;

   GST_DEBUG("wstVideoOutputThread: enter");

   for( i= 0; i < sink->soc.numBuffersOut; ++i )
   {
      rc= ioctl( sink->soc.v4l2Fd, VIDIOC_QBUF, &sink->soc.outBuffers[i].buf );
      if ( rc < 0 )
      {
         GST_ERROR("wstVideoOutputThread: failed to queue output buffer: rc %d errno %d", rc, errno);
         goto exit;
      }
   }

   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_STREAMON, &sink->soc.fmtOut.type );
   if ( rc < 0 )
   {
      GST_ERROR("wstVideoOutputThread: streamon failed for output: rc %d errno %d", rc, errno );
      goto exit;
   }

   g_print("westeros-sink: frame size %dx%d\n", sink->soc.frameWidth, sink->soc.frameHeight);

   memset( &selection, 0, sizeof(selection) );
   selection.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
   selection.target= V4L2_SEL_TGT_COMPOSE_DEFAULT;
   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_G_SELECTION, &selection );
   if ( rc < 0 )
   {
      GST_ERROR("wstVideoOutputThread: failed to get compose rect: rc %d errno %d", rc, errno );
      goto exit;
   }
   GST_DEBUG("Out compose default: (%d, %d, %d, %d)", selection.r.left, selection.r.top, selection.r.width, selection.r.height );

   memset( &selection, 0, sizeof(selection) );
   selection.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
   selection.target= V4L2_SEL_TGT_COMPOSE;
   rc= ioctl( sink->soc.v4l2Fd, VIDIOC_G_SELECTION, &selection );
   if ( rc < 0 )
   {
      GST_ERROR("wstVideoOutputThread: failed to get compose rect: rc %d errno %d", rc, errno );
      goto exit;
   }
   GST_DEBUG("Out compose: (%d, %d, %d, %d)", selection.r.left, selection.r.top, selection.r.width, selection.r.height );

   for( ; ; )
   {
      if ( sink->soc.quitVideoOutputThread )
      {
         break;
      }
      else
      {
         buffIndex= wstGetOutputBuffer( sink );

         if ( sink->soc.quitVideoOutputThread ) break;

         if ( buffIndex >= 0 )
         {
            int resubFd= -1;

            LOCK(sink);
            if (sink->soc.frameOutCount == 0)
            {
                g_signal_emit (G_OBJECT (sink), g_signals[SIGNAL_FIRSTFRAME], 0, 2, NULL);
            }
            ++sink->soc.frameOutCount;
            if ( sink->windowChange )
            {
               sink->windowChange= false;
               gst_westeros_sink_soc_update_video_position( sink );
            }
            UNLOCK(sink);

            if ( sink->soc.captureEnabled )
            {
               bufferInfo *binfo;

               GST_LOG("Video out: fd %d", sink->soc.outBuffers[buffIndex].fd );

               binfo= (bufferInfo*)malloc( sizeof(bufferInfo) );
               if ( binfo )
               {
                  binfo->sink= sink;
                  binfo->buffIndex= buffIndex;

                  struct wl_buffer *wlbuff;

                  wlbuff= wl_sb_create_planar_buffer_fd( sink->soc.sb,
                                                         sink->soc.outBuffers[buffIndex].fd,
                                                         sink->soc.frameWidth,
                                                         sink->soc.frameHeight,
                                                         sink->soc.outputFormat,
                                                         0, //offset0
                                                         sink->soc.frameWidth*sink->soc.frameHeight, //offset1
                                                         0, //offset2
                                                         sink->soc.frameWidth, //stride0
                                                         sink->soc.frameWidth, //stride1
                                                         0  //stride2
                                                       );
                  if ( wlbuff )
                  {
                     wl_buffer_add_listener( wlbuff, &wl_buffer_listener, binfo );
                     wl_surface_attach( sink->surface, wlbuff, sink->windowX, sink->windowY );
                     wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
                     wl_surface_commit( sink->surface );
                     wl_display_flush( sink->display );

                     ++sink->soc.activeBuffers;

                     buffIndex= -1;

                     /* Advance any frames sent to video server towards requeueing to decoder */
                     resubFd= sink->soc.prevFrameFd;
                     sink->soc.prevFrameFd= sink->soc.nextFrameFd;
                     sink->soc.nextFrameFd= -1;

                     if ( sink->soc.framesBeforeHideVideo )
                     {
                        if ( --sink->soc.framesBeforeHideVideo == 0 )
                        {
                           wstSendFrameVideoClientConnection( sink->soc.conn, -1 );
                        }
                     }
                  }
                  else
                  {
                     free( binfo );
                  }
               }
            }
            else
            {
               resubFd= sink->soc.prevFrameFd;
               sink->soc.prevFrameFd= sink->soc.nextFrameFd;
               sink->soc.nextFrameFd= sink->soc.outBuffers[buffIndex].fd;

               wstSendFrameVideoClientConnection( sink->soc.conn, sink->soc.outBuffers[buffIndex].fd );

               buffIndex= -1;
            }

            if ( resubFd >= 0 )
            {
               buffIndex= wstFindOutputBuffer( sink, resubFd );
               resubFd= -1;
            }

            if ( buffIndex >= 0 )
            {
               rc= ioctl( sink->soc.v4l2Fd, VIDIOC_QBUF, &sink->soc.outBuffers[buffIndex].buf );
               if ( rc < 0 )
               {
                  GST_ERROR("wstVideoOutputThread: failed to re-queue output buffer: rc %d errno %d", rc, errno);
                  goto exit;
               }
            }
         }
      }
   }

exit:

   GST_DEBUG("wstVideoOutputThread: exit");

   return NULL;
}

static gpointer wstEOSDetectionThread(gpointer data)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   int outputFrameCount, count, eosCountDown;
   bool videoPlaying;
   bool eosEventSeen;

   GST_DEBUG("wstVideoEOSThread: enter");

   eosCountDown= 10;
   LOCK(sink)
   outputFrameCount= sink->soc.frameOutCount;
   UNLOCK(sink);
   while( !sink->soc.quitEOSDetectionThread )
   {
      usleep( 1000000/sink->soc.frameRate );

      LOCK(sink)
      count= sink->soc.frameOutCount;
      videoPlaying= sink->soc.videoPlaying;
      eosEventSeen= sink->eosEventSeen;
      UNLOCK(sink)
      if ( videoPlaying && eosEventSeen && (outputFrameCount > 0) && (outputFrameCount == count) )
      {
         --eosCountDown;
         if ( eosCountDown == 0 )
         {
            g_print("westeros-sink: EOS detected\n");
            gst_westeros_sink_eos_detected( sink );
            break;
         }
      }
      else
      {
         outputFrameCount= count;
         eosCountDown= 10;
      }
   }

   GST_DEBUG("wstVideoEOSThread: exit");

   return NULL;
}

static gpointer wstDispatchThread(gpointer data)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   if ( sink->display )
   {
      GST_DEBUG("dispatchThread: enter");
      while( !sink->soc.quitDispatchThread )
      {
         if ( wl_display_dispatch_queue( sink->display, sink->queue ) == -1 )
         {
            break;
         }
      }
      GST_DEBUG("dispatchThread: exit");
   }
   return NULL;
}

