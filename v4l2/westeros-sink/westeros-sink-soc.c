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
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>

#ifdef USE_GST_ALLOCATORS
#include <gst/allocators/gstdmabuf.h>
#endif

#include "westeros-sink.h"

#define DEFAULT_DEVICE_NAME "/dev/video10"
#define DEFAULT_VIDEO_SERVER "video"

#define DEFAULT_FRAME_WIDTH (640)
#define DEFAULT_FRAME_HEIGHT (360)

#define NUM_INPUT_BUFFERS (2)
#define MIN_INPUT_BUFFERS (1)
#define NUM_OUTPUT_BUFFERS (6)
#define MIN_OUTPUT_BUFFERS (3)

#define QOS_INTERVAL (1000)

#define IOCTL ioctl_wrapper

#ifdef GLIB_VERSION_2_32
  #define LOCK_SOC( sink ) g_mutex_lock( &((sink)->soc.mutex) );
  #define UNLOCK_SOC( sink ) g_mutex_unlock( &((sink)->soc.mutex) );
#else
  #define LOCK_SOC( sink ) g_mutex_lock( (sink)->soc.mutex );
  #define UNLOCK_SOC( sink ) g_mutex_unlock( (sink)->soc.mutex );
#endif

GST_DEBUG_CATEGORY_EXTERN (gst_westeros_sink_debug);
#define GST_CAT_DEFAULT gst_westeros_sink_debug

#define INT_FRAME(FORMAT, ...)      frameLog( "FRAME: " FORMAT "\n", __VA_ARGS__)
#define FRAME(...)                  INT_FRAME(__VA_ARGS__, "")

enum
{
  PROP_DEVICE= PROP_SOC_BASE,
  PROP_FRAME_STEP_ON_PREROLL,
  PROP_FORCE_ASPECT_RATIO,
  PROP_ENABLE_TEXTURE
};
enum
{
   SIGNAL_FIRSTFRAME,
   SIGNAL_NEWTEXTURE,
   MAX_SIGNAL
};

static bool g_frameDebug= false;
static const char *gDeviceName= DEFAULT_DEVICE_NAME;
static guint g_signals[MAX_SIGNAL]= {0};

static gboolean (*queryOrg)(GstElement *element, GstQuery *query)= 0;

static void wstSinkSocStopVideo( GstWesterosSink *sink );
static void wstBuildSinkCaps( GstWesterosSinkClass *klass, GstWesterosSink *dummySink );
static void wstDiscoverVideoDecoder( GstWesterosSinkClass *klass );
static void wstStartEvents( GstWesterosSink *sink );
static void wstStopEvents( GstWesterosSink *sink );
static void wstProcessEvents( GstWesterosSink *sink );
static void wstGetMaxFrameSize( GstWesterosSink *sink );
static bool wstGetInputFormats( GstWesterosSink *sink );
static bool wstGetOutputFormats( GstWesterosSink *sink );
static bool wstSetInputFormat( GstWesterosSink *sink );
static bool wstSetOutputFormat( GstWesterosSink *sink );
static bool wstSetupInputBuffers( GstWesterosSink *sink );
static void wstTearDownInputBuffers( GstWesterosSink *sink );
static bool wstSetupOutputBuffers( GstWesterosSink *sink );
static bool wstSetupOutputBuffersMMap( GstWesterosSink *sink );
static bool wstSetupOutputBuffersDmabuf( GstWesterosSink *sink );
static void wstTearDownOutputBuffers( GstWesterosSink *sink );
static void wstTearDownOutputBuffersDmabuf( GstWesterosSink *sink );
static void wstTearDownOutputBuffersMMap( GstWesterosSink *sink );
static void wstSetInputMemMode( GstWesterosSink *sink, int mode );
static void wstSetupInput( GstWesterosSink *sink );
static int wstGetInputBuffer( GstWesterosSink *sink );
static void wstSetOutputMemMode( GstWesterosSink *sink, int mode );
static void wstSetupOutput( GstWesterosSink *sink );
static int wstGetOutputBuffer( GstWesterosSink *sink );
static int wstFindOutputBuffer( GstWesterosSink *sink, int fd );
static void wstLockOutputBuffer( GstWesterosSink *sink, int buffIndex );
static bool wstUnlockOutputBuffer( GstWesterosSink *sink, int buffIndex );
static void wstRequeueOutputBuffer( GstWesterosSink *sink, int buffIndex );
static WstVideoClientConnection *wstCreateVideoClientConnection( GstWesterosSink *sink, const char *name );
static void wstDestroyVideoClientConnection( WstVideoClientConnection *conn );
static void wstSendFlushVideoClientConnection( WstVideoClientConnection *conn );
static bool wstSendFrameVideoClientConnection( WstVideoClientConnection *conn, int buffIndex );
static void wstDecoderReset( GstWesterosSink *sink, bool hard );
static void wstGetVideoBounds( GstWesterosSink *sink, int *x, int *y, int *w, int *h );
static void wstProcessTextureSignal( GstWesterosSink *sink, int buffIndex );
static bool wstProcessTextureWayland( GstWesterosSink *sink, int buffIndex );
static int wstFindVideoBuffer( GstWesterosSink *sink, int frameNumber );
static int wstFindCurrentVideoBuffer( GstWesterosSink *sink );
static gpointer wstVideoOutputThread(gpointer data);
static gpointer wstEOSDetectionThread(gpointer data);
static gpointer wstDispatchThread(gpointer data);
static GstFlowReturn prerollSinkSoc(GstBaseSink *base_sink, GstBuffer *buffer);
static int ioctl_wrapper( int fd, int request, void* arg );

#ifdef USE_AMLOGIC_MESON
#include "meson_drm.h"
#include <xf86drm.h>
#include "svp/aml-meson/svp-util.c"
#endif

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static gint64 getGstClockTime( GstWesterosSink *sink )
{
   gint64 time= 0;
   GstElement *element= GST_ELEMENT(sink);
   GstClock *clock= GST_ELEMENT_CLOCK(element);
   if ( clock )
   {
      time= gst_clock_get_time(clock);
   }
   return time;
}

static void frameLog( const char *fmt, ... )
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

#ifdef USE_GST1
static GstFlowReturn wstChain(GstPad *pad, GstObject *parent, GstBuffer *buf)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(parent);
   long long pts, duration;

   if ( buf )
   {
      pts= GST_BUFFER_PTS(buf);
      duration= GST_BUFFER_DURATION(buf);
      if ( !GST_CLOCK_TIME_IS_VALID(duration) )
      {
         if ( sink->soc.frameRate != 0 )
         {
            duration= GST_SECOND / sink->soc.frameRate;
            GST_BUFFER_DURATION(buf)= duration;
         }
         else
         {
            GST_LOG("wstChain: buffer duration not available");
         }
      }
      if ( GST_CLOCK_TIME_IS_VALID(duration) && (pts+duration < sink->segment.start) )
      {
         GST_LOG("wstChain: accept buf %p pts %lld segment start %lld", buf, pts, sink->segment.start);
         GST_BASE_SINK_PREROLL_LOCK(GST_BASE_SINK(sink));
         gst_westeros_sink_soc_render( sink, buf );
         GST_BASE_SINK_PREROLL_UNLOCK(GST_BASE_SINK(sink));
      }
   }

   return sink->soc.chainOrg( pad, parent, buf );
}
#endif

void gst_westeros_sink_soc_class_init(GstWesterosSinkClass *klass)
{
   GObjectClass *gobject_class= (GObjectClass *) klass;
   GstBaseSinkClass *gstbasesink_class= (GstBaseSinkClass *) klass;

   gst_element_class_set_static_metadata( GST_ELEMENT_CLASS(klass),
                                          "Westeros Sink",
                                          "Codec/Decoder/Video/Sink/Video",
                                          "Writes buffers to the westeros wayland compositor",
                                          "Comcast" );
   wstDiscoverVideoDecoder(klass);

   gstbasesink_class->preroll= GST_DEBUG_FUNCPTR(prerollSinkSoc);

   g_object_class_install_property (gobject_class, PROP_DEVICE,
   g_param_spec_string ("device",
                         "device location",
                         "Location of the device", gDeviceName, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_FRAME_STEP_ON_PREROLL,
     g_param_spec_boolean ("frame-step-on-preroll",
                           "frame step on preroll",
                           "allow frame stepping on preroll into pause", FALSE, G_PARAM_READWRITE));


   g_object_class_install_property (gobject_class, PROP_ENABLE_TEXTURE,
     g_param_spec_boolean ("enable-texture",
                           "enable texture signal",
                           "0: disable; 1: enable", FALSE, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
     g_param_spec_boolean ("force-aspect-ratio",
                           "force aspect ratio",
                           "When enabled scaling respects source aspect ratio", FALSE, G_PARAM_READWRITE));

   g_signals[SIGNAL_FIRSTFRAME]= g_signal_new( "first-video-frame-callback",
                                               G_TYPE_FROM_CLASS(GST_ELEMENT_CLASS(klass)),
                                               (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                               0,    /* class offset */
                                               NULL, /* accumulator */
                                               NULL, /* accu data */
                                               g_cclosure_marshal_VOID__UINT_POINTER,
                                               G_TYPE_NONE,
                                               2,
                                               G_TYPE_UINT,
                                               G_TYPE_POINTER );

   g_signals[SIGNAL_NEWTEXTURE]= g_signal_new( "new-video-texture-callback",
                                               G_TYPE_FROM_CLASS(GST_ELEMENT_CLASS(klass)),
                                               (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                               0,    /* class offset */
                                               NULL, /* accumulator */
                                               NULL, /* accu data */
                                               NULL,
                                               G_TYPE_NONE,
                                               15,
                                               G_TYPE_UINT, /* format: fourcc */
                                               G_TYPE_UINT, /* pixel width */
                                               G_TYPE_UINT, /* pixel height */
                                               G_TYPE_INT,  /* plane 0 fd */
                                               G_TYPE_UINT, /* plane 0 byte length */
                                               G_TYPE_UINT, /* plane 0 stride */
                                               G_TYPE_POINTER, /* plane 0 data */
                                               G_TYPE_INT,  /* plane 1 fd */
                                               G_TYPE_UINT, /* plane 1 byte length */
                                               G_TYPE_UINT, /* plane 1 stride */
                                               G_TYPE_POINTER, /* plane 1 data */
                                               G_TYPE_INT,  /* plane 2 fd */
                                               G_TYPE_UINT, /* plane 2 byte length */
                                               G_TYPE_UINT, /* plane 2 stride */
                                               G_TYPE_POINTER /* plane 2 data */
                                             );
}

gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   const char *env;

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
   sink->soc.frameWidthStream= -1;
   sink->soc.frameHeightStream= -1;
   sink->soc.frameInCount= 0;
   sink->soc.frameOutCount= 0;
   sink->soc.numDropped= 0;
   sink->soc.inputFormat= 0;
   sink->soc.outputFormat= WL_SB_FORMAT_NV12;
   sink->soc.devname= strdup(gDeviceName);
   sink->soc.enableTextureSignal= FALSE;
   sink->soc.v4l2Fd= -1;
   sink->soc.caps= {0};
   sink->soc.deviceCaps= 0;
   sink->soc.isMultiPlane= FALSE;
   sink->soc.preferNV12M= TRUE;
   sink->soc.inputMemMode= V4L2_MEMORY_MMAP;
   sink->soc.outputMemMode= V4L2_MEMORY_MMAP;
   sink->soc.numInputFormats= 0;
   sink->soc.inputFormats= 0;
   sink->soc.numOutputFormats= 0;
   sink->soc.outputFormats= 0;
   sink->soc.fmtIn= {0};
   sink->soc.fmtOut= {0};
   sink->soc.formatsSet= FALSE;
   sink->soc.syncType= -1;
   sink->soc.sessionId= 0;
   sink->soc.bufferCohort= 0;
   sink->soc.minBuffersIn= 0;
   sink->soc.minBuffersOut= 0;
   sink->soc.numBuffersIn= 0;
   sink->soc.inBuffers= 0;
   sink->soc.numBuffersOut= 0;
   sink->soc.bufferIdOutBase= 0;
   sink->soc.outBuffers= 0;
   sink->soc.quitVideoOutputThread= FALSE;
   sink->soc.quitEOSDetectionThread= FALSE;
   sink->soc.quitDispatchThread= FALSE;
   sink->soc.videoOutputThread= NULL;
   sink->soc.eosDetectionThread= NULL;
   sink->soc.dispatchThread= NULL;
   sink->soc.videoPlaying= FALSE;
   sink->soc.videoPaused= FALSE;
   sink->soc.hasEvents= FALSE;
   sink->soc.needCaptureRestart= FALSE;
   sink->soc.nextFrameFd= -1;
   sink->soc.prevFrame1Fd= -1;
   sink->soc.prevFrame2Fd= -1;
   sink->soc.resubFd= -1;
   sink->soc.captureEnabled= FALSE;
   sink->soc.useCaptureOnly= FALSE;
   sink->soc.pauseException= FALSE;
   sink->soc.pauseGetGfxFrame= FALSE;
   sink->soc.useGfxSync= FALSE;
   sink->soc.pauseGfxBuffIndex= -1;
   sink->soc.hideVideoFramesDelay= 2;
   sink->soc.hideGfxFramesDelay= 1;
   sink->soc.framesBeforeHideVideo= 0;
   sink->soc.framesBeforeHideGfx= 0;
   sink->soc.prevFrameTimeGfx= 0;
   sink->soc.prevFramePTSGfx= 0;
   sink->soc.videoX= sink->windowX;
   sink->soc.videoY= sink->windowY;
   sink->soc.videoWidth= sink->windowWidth;
   sink->soc.videoHeight= sink->windowHeight;
   sink->soc.frameStepOnPreroll= FALSE;
   sink->soc.forceAspectRatio= FALSE;
   sink->soc.secureVideo= FALSE;
   sink->soc.useDmabufOutput= FALSE;
   sink->soc.dwMode= -1;
   sink->soc.drmFd= -1;

   #ifdef USE_GST1
   sink->soc.chainOrg= 0;
   if ( GST_BASE_SINK(sink)->sinkpad )
   {
      sink->soc.chainOrg= GST_BASE_SINK(sink)->sinkpad->chainfunc;
      gst_pad_set_chain_function( GST_BASE_SINK(sink)->sinkpad, wstChain );
   }
   else
   {
      GST_ERROR("unable to access sink pad" );
   }
   #endif

   /* Request caps updates */
   sink->passCaps= TRUE;

   if ( getenv("WESTEROS_SINK_USE_FREERUN") )
   {
      gst_base_sink_set_sync(GST_BASE_SINK(sink), FALSE);
      printf("westeros-sink: using freerun\n");
   }
   else
   {
      gst_base_sink_set_sync(GST_BASE_SINK(sink), TRUE);
   }

   gst_base_sink_set_async_enabled(GST_BASE_SINK(sink), TRUE);

   if ( getenv("WESTEROS_SINK_USE_NV21") )
   {
      sink->soc.outputFormat= WL_SB_FORMAT_NV21;
   }

   if ( getenv("WESTEROS_SINK_USE_GFX") )
   {
      sink->soc.useCaptureOnly= TRUE;
      sink->soc.captureEnabled= TRUE;
      printf("westeros-sink: capture only\n");
   }

   env= getenv( "WESTEROS_SINK_DEBUG_FRAME" );
   if ( env )
   {
      int level= atoi( env );
      g_frameDebug= (level > 0 ? true : false);
   }

   result= TRUE;

   return result;
}

void gst_westeros_sink_soc_term( GstWesterosSink *sink )
{
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
      case PROP_FRAME_STEP_ON_PREROLL:
         {
            sink->soc.frameStepOnPreroll= g_value_get_boolean(value);
            break;
         }
      case PROP_FORCE_ASPECT_RATIO:
         {
            sink->soc.forceAspectRatio= g_value_get_boolean(value);
            break;
         }
      case PROP_ENABLE_TEXTURE:
         {
            sink->soc.enableTextureSignal= g_value_get_boolean(value);
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
      case PROP_FRAME_STEP_ON_PREROLL:
         g_value_set_boolean(value, sink->soc.frameStepOnPreroll);
         break;
      case PROP_FORCE_ASPECT_RATIO:
         g_value_set_boolean(value, sink->soc.forceAspectRatio);
         break;
      case PROP_ENABLE_TEXTURE:
         g_value_set_boolean(value, sink->soc.enableTextureSignal);
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
      /* Don't use vpc when capture only */
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

   result= TRUE;

   return result;
}

gboolean gst_westeros_sink_soc_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);

   int rc, len;
   struct v4l2_exportbuffer eb;

   sink->soc.v4l2Fd= open( sink->soc.devname, O_RDWR );
   if ( sink->soc.v4l2Fd < 0 )
   {
      GST_ERROR("failed to open device (%s)", sink->soc.devname );
      goto exit;
   }

   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_QUERYCAP, &sink->soc.caps );
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

   if ( (sink->soc.deviceCaps & V4L2_CAP_VIDEO_M2M_MPLANE) && !(sink->soc.deviceCaps & V4L2_CAP_VIDEO_M2M) )
   {
      GST_DEBUG("device is multiplane");
      sink->soc.isMultiPlane= TRUE;
   }

   len= strlen( (char*)sink->soc.caps.driver );
   if ( (len == 14) && !strncmp( (char*)sink->soc.caps.driver, "aml-vcodec-dec", len) )
   {
      sink->soc.hideVideoFramesDelay= 2;
      sink->soc.hideGfxFramesDelay= 3;
      sink->soc.useGfxSync= TRUE;
   }

   eb.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
   eb.index= -1;
   eb.plane= -1;
   eb.flags= (O_RDWR|O_CLOEXEC);
   IOCTL( sink->soc.v4l2Fd, VIDIOC_EXPBUF, &eb );
   if ( errno == ENOTTY )
   {
      GST_ERROR("device (%s) does not support dmabuf: no VIDIOC_EXPBUF", sink->soc.devname );
      goto exit;
   }

   wstGetInputFormats( sink );

   wstGetOutputFormats( sink );

   wstGetMaxFrameSize( sink );

   wstStartEvents( sink );

   if ( !sink->soc.useCaptureOnly )
   {
      sink->soc.conn= wstCreateVideoClientConnection( sink, DEFAULT_VIDEO_SERVER );
      if ( !sink->soc.conn )
      {
         GST_ERROR("unable to connect to video server (%s)", DEFAULT_VIDEO_SERVER );
         sink->soc.useCaptureOnly= TRUE;
         sink->soc.captureEnabled= TRUE;
         printf("westeros-sink: no video server - capture only\n");
         if ( sink->vpc )
         {
            wl_vpc_destroy( sink->vpc );
            sink->vpc= 0;
         }
      }
   }

   LOCK(sink);
   sink->startAfterCaps= TRUE;
   sink->soc.videoPlaying= TRUE;
   sink->soc.videoPaused= FALSE;
   UNLOCK(sink);

exit:
   return TRUE;
}

gboolean gst_westeros_sink_soc_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);

   LOCK( sink );
   sink->soc.videoPlaying= TRUE;
   sink->soc.videoPaused= FALSE;
   UNLOCK( sink );

   return TRUE;
}

gboolean gst_westeros_sink_soc_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   LOCK( sink );
   sink->soc.videoPlaying= FALSE;
   sink->soc.videoPaused= TRUE;
   UNLOCK( sink );

   if (gst_base_sink_is_async_enabled(GST_BASE_SINK(sink)))
   {
      /* To complete transition to paused state in async_enabled mode, we need a preroll buffer pushed to the pad;
         This is a workaround to avoid the need for preroll buffer. */
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
   wstSinkSocStopVideo( sink );
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

   wstSinkSocStopVideo( sink );

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
            int version;
            sink->soc.inputFormat= V4L2_PIX_FMT_MPEG;
            if ( gst_structure_get_int(structure, "mpegversion", &version) )
            {
               switch( version )
               {
                  case 1:
                     sink->soc.inputFormat= V4L2_PIX_FMT_MPEG1;
                     break;
                  default:
                  case 2:
                     sink->soc.inputFormat= V4L2_PIX_FMT_MPEG2;
                     break;
                  case 4:
                     sink->soc.inputFormat= V4L2_PIX_FMT_MPEG4;
                     break;
               }
            }
            result= TRUE;
         }
         #ifdef V4L2_PIX_FMT_HEVC
         else if ( (len == 12) && !strncmp("video/x-h265", mime, len) )
         {
            sink->soc.inputFormat= V4L2_PIX_FMT_HEVC;
            result= TRUE;
         }
         #endif
         #ifdef V4L2_PIX_FMT_VP9
         else if ( (len == 11) && !strncmp("video/x-vp9", mime, len) )
         {
            sink->soc.inputFormat= V4L2_PIX_FMT_VP9;
            result= TRUE;
         }
         #endif
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
            if ( (sink->soc.frameWidth == -1) || (sink->soc.hasEvents == FALSE) )
            {
               sink->soc.frameWidth= width;
            }
            sink->soc.frameWidthStream= width;
         }
         if ( gst_structure_get_int( structure, "height", &height ) )
         {
            if ( (sink->soc.frameHeight != -1) && (sink->soc.frameHeight != height) )
            {
               frameSizeChange= true;
            }
            if ( (sink->soc.frameHeight == -1) || (sink->soc.hasEvents == FALSE) )
            {
               sink->soc.frameHeight= height;
            }
            sink->soc.frameHeightStream= height;
         }

         if ( frameSizeChange && (sink->soc.hasEvents == FALSE) )
         {
            g_print("westeros-sink: frame size change : %dx%d\n", sink->soc.frameWidth, sink->soc.frameHeight);
            wstDecoderReset( sink, true );
            if ( sink->soc.v4l2Fd >= 0 )
            {
               wstSetupInput( sink );
            }
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
   if ( !sink->flushStarted )
   {
      gint64 nanoTime;
      int rc, buffIndex;
      int inSize, offset, avail, copylen;
      unsigned char *inData;
      int memMode= V4L2_MEMORY_MMAP;
      #ifdef USE_GST_ALLOCATORS
      GstMemory *mem;

      mem= gst_buffer_peek_memory( buffer, 0 );
      #endif

      if ( !sink->soc.formatsSet )
      {
         #ifdef USE_GST_ALLOCATORS
         if ( gst_is_dmabuf_memory(mem) )
         {
            GST_DEBUG("using dma-buf for input");
            memMode= V4L2_MEMORY_DMABUF;
         }
         else
         {
            memMode= V4L2_MEMORY_MMAP;
         }
         #endif
         wstSetInputMemMode( sink, memMode );
         wstSetupInput( sink );
      }

      #ifdef USE_GST_ALLOCATORS
      inSize= gst_memory_get_sizes( mem, NULL, NULL );

      if ( sink->soc.inputMemMode == V4L2_MEMORY_DMABUF )
      {
         GST_LOG("gst_westeros_sink_soc_render: buffer %p, len %d timestamp: %lld", buffer, inSize, GST_BUFFER_PTS(buffer) );
      }
      #endif

      if ( GST_BUFFER_PTS_IS_VALID(buffer) )
      {
         guint64 prevPTS;

         nanoTime= GST_BUFFER_PTS(buffer);
         {
            guint64 gstNow= getGstClockTime(sink);
            if ( gstNow <= nanoTime )
               FRAME("in: frame PTS %lld gst clock %lld: lead time %lld us", nanoTime, gstNow, (nanoTime-gstNow)/1000LL);
            else
               FRAME("in: frame PTS %lld gst clock %lld: lead time %lld us", nanoTime, gstNow, (gstNow-nanoTime)/1000LL);
         }
         LOCK(sink)
         if ( nanoTime >= sink->segment.start )
         {
            prevPTS= sink->currentPTS;
            sink->currentPTS= ((nanoTime * 90000LL + GST_SECOND/2)/GST_SECOND);
            if (sink->prevPositionSegmentStart != sink->positionSegmentStart)
            {
               sink->firstPTS= sink->currentPTS;
               sink->prevPositionSegmentStart = sink->positionSegmentStart;
               GST_DEBUG("SegmentStart changed! Updating first PTS to %lld ", sink->firstPTS);
            }
            if ( sink->currentPTS != 0 || sink->soc.frameInCount == 0 )
            {
               if ( (sink->currentPTS < sink->firstPTS) && (sink->currentPTS > 90000) )
               {
                  /* If we have hit a discontinuity that doesn't look like rollover, then
                     treat this as the case of looping a short clip.  Adjust our firstPTS
                     to keep our running time correct. */
                  sink->firstPTS= sink->firstPTS-(prevPTS-sink->currentPTS);
               }
            }
         }
         UNLOCK(sink);
      }

      #ifdef USE_GST_ALLOCATORS
      if ( sink->soc.inputMemMode == V4L2_MEMORY_DMABUF )
      {
         uint32_t bytesused;
         gsize dataOffset, maxSize;

         buffIndex= wstGetInputBuffer( sink );
         if ( buffIndex < 0 )
         {
            GST_ERROR("gst_westeros_sink_soc_render: unable to get input buffer");
            goto exit;
         }

         if ( sink->soc.inBuffers[buffIndex].gstbuf )
         {
            gst_buffer_unref( sink->soc.inBuffers[buffIndex].gstbuf );
            sink->soc.inBuffers[buffIndex].gstbuf= 0;
         }

         if (GST_BUFFER_PTS_IS_VALID(buffer) )
         {
            GstClockTime timestamp= GST_BUFFER_PTS(buffer) + 500LL;
            GST_TIME_TO_TIMEVAL( timestamp, sink->soc.inBuffers[buffIndex].buf.timestamp );
         }

         inSize= gst_memory_get_sizes( mem, &dataOffset, &maxSize );

         sink->soc.inBuffers[buffIndex].buf.bytesused= dataOffset+inSize;
         if ( sink->soc.isMultiPlane )
         {
            sink->soc.inBuffers[buffIndex].buf.m.planes[0].m.fd= gst_dmabuf_memory_get_fd(mem);
            sink->soc.inBuffers[buffIndex].buf.m.planes[0].bytesused= dataOffset+inSize;
            sink->soc.inBuffers[buffIndex].buf.m.planes[0].length= maxSize;
            sink->soc.inBuffers[buffIndex].buf.m.planes[0].data_offset= dataOffset;
         }
         else
         {
            sink->soc.inBuffers[buffIndex].buf.m.fd= gst_dmabuf_memory_get_fd(mem);
            sink->soc.inBuffers[buffIndex].buf.length= maxSize;
         }
         rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_QBUF, &sink->soc.inBuffers[buffIndex].buf );
         if ( rc < 0 )
         {
            GST_ERROR("gst_westeros_sink_soc_render: queuing input buffer failed: rc %d errno %d", rc, errno );
            goto exit;
         }
         sink->soc.inBuffers[buffIndex].queued= true;
         sink->soc.inBuffers[buffIndex].gstbuf= gst_buffer_ref(buffer);
      }
      else
      #endif
      if ( sink->soc.inputMemMode == V4L2_MEMORY_MMAP )
      {
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

         if ( inSize )
         {
            avail= inSize;
            offset= 0;
            while( offset < inSize )
            {
               buffIndex= wstGetInputBuffer( sink );
               if ( (buffIndex < 0) && !sink->flushStarted )
               {
                  GST_ERROR("gst_westeros_sink_soc_render: unable to get input buffer");
                  goto exit;
               }

               if ( sink->flushStarted )
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

               if (GST_BUFFER_PTS_IS_VALID(buffer) )
               {
                  GstClockTime timestamp= GST_BUFFER_PTS(buffer) + 500LL;
                  GST_TIME_TO_TIMEVAL( timestamp, sink->soc.inBuffers[buffIndex].buf.timestamp );
               }
               sink->soc.inBuffers[buffIndex].buf.bytesused= copylen;
               if ( sink->soc.isMultiPlane )
               {
                  sink->soc.inBuffers[buffIndex].buf.m.planes[0].bytesused= copylen;
               }
               rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_QBUF, &sink->soc.inBuffers[buffIndex].buf );
               if ( rc < 0 )
               {
                  GST_ERROR("gst_westeros_sink_soc_render: queuing input buffer failed: rc %d errno %d", rc, errno );
                  goto exit;
               }
               sink->soc.inBuffers[buffIndex].queued= true;
            }
         }

         #ifdef USE_GST1
         gst_buffer_unmap( buffer, &map);
         #endif
      }

      ++sink->soc.frameInCount;

      if ( !sink->videoStarted )
      {
         GST_DEBUG("gst_westeros_sink_soc_render: issue input VIDIOC_STREAMON");
         rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_STREAMON, &sink->soc.fmtIn.type );
         if ( rc < 0 )
         {
            GST_ERROR("streamon failed for input: rc %d errno %d", rc, errno );
            goto exit;
         }

         wstSetupOutput( sink );

         if ( !gst_westeros_sink_soc_start_video( sink ) )
         {
            GST_ERROR("gst_westeros_sink_soc_render: gst_westeros_sink_soc_start_video failed");
         }
      }
   }

exit:
   return;
}

void gst_westeros_sink_soc_flush( GstWesterosSink *sink )
{
   GST_DEBUG("gst_westeros_sink_soc_flush");
   if ( sink->videoStarted )
   {
      wstDecoderReset( sink, false );
   }
   LOCK(sink);
   sink->soc.frameInCount= 0;
   sink->soc.frameOutCount= 0;
   sink->soc.numDropped= 0;
   UNLOCK(sink);
}

gboolean gst_westeros_sink_soc_start_video( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   int rc;

   sink->soc.frameOutCount= 0;
   sink->soc.numDropped= 0;

   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_STREAMON, &sink->soc.fmtIn.type );
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
         GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: starting westeros_sink_dispatch thread");
         sink->soc.dispatchThread= g_thread_new("westeros_sink_dispatch", wstDispatchThread, sink);
      }
   }

   sink->soc.quitVideoOutputThread= FALSE;
   if ( sink->soc.videoOutputThread == NULL )
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: starting westeros_sink_video_output thread");
      sink->soc.videoOutputThread= g_thread_new("westeros_sink_video_output", wstVideoOutputThread, sink);
   }

   sink->soc.quitEOSDetectionThread= FALSE;
   if ( sink->soc.eosDetectionThread == NULL )
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: starting westeros_sink_eos thread");
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

      sink->soc.framesBeforeHideVideo= sink->soc.hideVideoFramesDelay;

      if ( sink->soc.videoPaused )
      {
         sink->soc.pauseException= TRUE;
         sink->soc.pauseGetGfxFrame= TRUE;
      }
   }
   else if ( !useGfxPath && sink->soc.captureEnabled )
   {
      sink->soc.captureEnabled= FALSE;
      sink->soc.prevFrame1Fd= -1;
      sink->soc.prevFrame2Fd= -1;
      sink->soc.nextFrameFd= -1;

      if ( sink->soc.videoPaused )
      {
         if ( sink->soc.pauseGfxBuffIndex >= 0 )
         {
            LOCK(sink);
            if ( wstUnlockOutputBuffer( sink, sink->soc.pauseGfxBuffIndex ) )
            {
               wstRequeueOutputBuffer( sink, sink->soc.pauseGfxBuffIndex );
            }
            sink->soc.pauseGfxBuffIndex= -1;
            UNLOCK(sink);
         }
      }
      sink->soc.framesBeforeHideGfx= sink->soc.hideGfxFramesDelay;
   }
   if ( sink->soc.forceAspectRatio && sink->vpcSurface )
   {
      int vx, vy, vw, vh;
      wstGetVideoBounds( sink, &vx, &vy, &vw, &vh );
      wl_vpc_surface_set_geometry( sink->vpcSurface, vx, vy, vw, vh );
   }
}

void gst_westeros_sink_soc_update_video_position( GstWesterosSink *sink )
{
   if ( sink->windowSizeOverride )
   {
      sink->soc.videoX= ((sink->windowX*sink->scaleXNum)/sink->scaleXDenom) + sink->transX;
      sink->soc.videoY= ((sink->windowY*sink->scaleYNum)/sink->scaleYDenom) + sink->transY;
      sink->soc.videoWidth= (sink->windowWidth*sink->scaleXNum)/sink->scaleXDenom;
      sink->soc.videoHeight= (sink->windowHeight*sink->scaleYNum)/sink->scaleYDenom;
   }
   else
   {
      sink->soc.videoX= sink->transX;
      sink->soc.videoY= sink->transY;
      sink->soc.videoWidth= (sink->outputWidth*sink->scaleXNum)/sink->scaleXDenom;
      sink->soc.videoHeight= (sink->outputHeight*sink->scaleYNum)/sink->scaleYDenom;
   }

   if ( !sink->soc.captureEnabled )
   {
      /* Send a buffer to compositor to update hole punch geometry */
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

static void wstSinkSocStopVideo( GstWesterosSink *sink )
{
   LOCK(sink);
   if ( sink->soc.conn )
   {
      wstDestroyVideoClientConnection( sink->soc.conn );
      sink->soc.conn= 0;
   }
   if ( sink->soc.videoOutputThread || sink->soc.eosDetectionThread || sink->soc.dispatchThread )
   {
      sink->videoStarted= FALSE;
      sink->soc.quitVideoOutputThread= TRUE;
      sink->soc.quitEOSDetectionThread= TRUE;
      sink->soc.quitDispatchThread= TRUE;
      if ( sink->display )
      {
         int fd= wl_display_get_fd( sink->display );
         if ( fd >= 0 )
         {
            shutdown( fd, SHUT_RDWR );
         }
      }

      wstStopEvents( sink );

      wstTearDownInputBuffers( sink );

      wstTearDownOutputBuffers( sink );
   }
   UNLOCK(sink);

   sink->soc.prevFrame1Fd= -1;
   sink->soc.prevFrame2Fd= -1;
   sink->soc.nextFrameFd= -1;
   sink->soc.formatsSet= FALSE;
   sink->soc.frameWidth= -1;
   sink->soc.frameHeight= -1;
   sink->soc.frameWidthStream= -1;
   sink->soc.frameHeightStream= -1;
   sink->soc.pauseGfxBuffIndex= -1;
   sink->soc.syncType= -1;

   if ( sink->soc.inputFormats )
   {
      free( sink->soc.inputFormats );
      sink->soc.inputFormats= 0;
   }
   if ( sink->soc.outputFormats )
   {
      free( sink->soc.outputFormats );
      sink->soc.outputFormats= 0;
   }
   if ( sink->soc.v4l2Fd >= 0 )
   {
      close( sink->soc.v4l2Fd );
      sink->soc.v4l2Fd= -1;
   }

   if ( sink->soc.videoOutputThread )
   {
      sink->soc.quitVideoOutputThread= TRUE;
      g_thread_join( sink->soc.videoOutputThread );
      sink->soc.videoOutputThread= NULL;
   }

   if ( sink->soc.eosDetectionThread )
   {
      sink->soc.quitEOSDetectionThread= TRUE;
      g_thread_join( sink->soc.eosDetectionThread );
      sink->soc.eosDetectionThread= NULL;
   }

   if ( sink->soc.dispatchThread )
   {
      sink->soc.quitDispatchThread= TRUE;
      g_thread_join( sink->soc.dispatchThread );
      sink->soc.dispatchThread= NULL;
   }

   if ( sink->soc.sb )
   {
      wl_sb_destroy( sink->soc.sb );
      sink->soc.sb= 0;
   }
}

static void wstBuildSinkCaps( GstWesterosSinkClass *klass, GstWesterosSink *dummySink )
{
   GstCaps *caps= 0;
   GstCaps *capsTemp= 0;
   GstPadTemplate *padTemplate= 0;
   int i;

   caps= gst_caps_new_empty();
   if ( caps )
   {
      for( i= 0; i < dummySink->soc.numInputFormats; ++i )
      {
         switch( dummySink->soc.inputFormats[i].pixelformat )
         {
            case V4L2_PIX_FMT_MPEG:
               capsTemp= gst_caps_from_string(
                                                "video/mpeg, " \
                                                "systemstream = (boolean) true ; "
                                             );
               break;
            case V4L2_PIX_FMT_MPEG1:
               capsTemp= gst_caps_from_string(
                                                "video/mpeg, " \
                                                "mpegversion=(int) 1, " \
                                                "parsed=(boolean) true, " \
                                                "systemstream = (boolean) false ; "
                                             );
               break;
            case V4L2_PIX_FMT_MPEG2:
               capsTemp= gst_caps_from_string(
                                                "video/mpeg, " \
                                                "mpegversion=(int) 2, " \
                                                "parsed=(boolean) true, " \
                                                "systemstream = (boolean) false ; "
                                             );
               break;
            case V4L2_PIX_FMT_MPEG4:
               capsTemp= gst_caps_from_string(
                                                "video/mpeg, " \
                                                "mpegversion=(int) 4, " \
                                                "parsed=(boolean) true, " \
                                                "systemstream = (boolean) false ; "
                                             );
               break;
            case V4L2_PIX_FMT_H264:
               capsTemp= gst_caps_from_string(
                                                "video/x-h264, " \
                                                "parsed=(boolean) true, " \
                                                "alignment=(string) au, " \
                                                "stream-format=(string) byte-stream, " \
                                                "width=(int) [1,MAX], " "height=(int) [1,MAX] ; " \
                                                "video/x-h264(memory:DMABuf) ; "
                                             );
               break;
            #ifdef V4L2_PIX_FMT_VP9
            case V4L2_PIX_FMT_VP9:
               capsTemp= gst_caps_from_string(
                                                "video/x-vp9, " \
                                                "width=(int) [1,MAX], " \
                                                "height=(int) [1,MAX] ; " \
                                                "video/x-vp9(memory:DMABuf) ; "
                                             );
               break;
            #endif
            #ifdef V4L2_PIX_FMT_HEVC
            case V4L2_PIX_FMT_HEVC:
               capsTemp= gst_caps_from_string(
                                                "video/x-h265, " \
                                                "parsed=(boolean) true, " \
                                                "alignment=(string) au, " \
                                                "stream-format=(string) byte-stream, " \
                                                "width=(int) [1,MAX], " \
                                                "height=(int) [1,MAX] ; " \
                                                "video/x-h265(memory:DMABuf) ; "
                                             );
               break;
            #endif
            default:
               break;
         }
         if ( capsTemp )
         {
            gst_caps_append( caps, capsTemp );
            capsTemp =0;
         }
      }

      padTemplate= gst_pad_template_new( "sink",
                                         GST_PAD_SINK,
                                         GST_PAD_ALWAYS,
                                         caps );
      if ( padTemplate )
      {
         GstElementClass *gstelement_class= (GstElementClass *)klass;
         gst_element_class_add_pad_template(gstelement_class, padTemplate);
         padTemplate= 0;
      }
      else
      {
         GST_ERROR("wstBuildSinkCaps: gst_pad_template_new failed");
      }

      gst_caps_unref( caps );
   }
   else
   {
      GST_ERROR("wstBuildSinkCaps: gst_caps_new_empty failed");
   }
}

static void wstDiscoverVideoDecoder( GstWesterosSinkClass *klass )
{
   int rc, len, i, fd;
   bool acceptsCompressed;
   GstWesterosSink dummySink;
   struct v4l2_exportbuffer eb;
   struct dirent *dirent;

   memset( &dummySink, 0, sizeof(dummySink) );

   DIR *dir= opendir("/dev");
   if ( dir )
   {
      for( ; ; )
      {
         fd= -1;

         dirent= readdir( dir );
         if ( dirent == 0 ) break;

         len= strlen(dirent->d_name);
         if ( (len == 1) && !strncmp( dirent->d_name, ".", len) )
         {
            continue;
         }
         else if ( (len == 2) && !strncmp( dirent->d_name, "..", len) )
         {
            continue;
         }

         if ( (len > 5) && !strncmp( dirent->d_name, "video", 5 ) )
         {
            char name[256+10];
            struct v4l2_capability caps;
            uint32_t deviceCaps;

            strcpy( name, "/dev/" );
            strcat( name, dirent->d_name );
            GST_DEBUG("checking device: %s", name);
            fd= open( name, O_RDWR );
            if ( fd < 0 )
            {
               goto done_check;
            }

            rc= IOCTL( fd, VIDIOC_QUERYCAP, &caps );
            if ( rc < 0 )
            {
               goto done_check;
            }

            deviceCaps= (caps.capabilities & V4L2_CAP_DEVICE_CAPS ) ? caps.device_caps : caps.capabilities;

            if ( !(deviceCaps & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE) ))
            {
               goto done_check;
            }

            if ( !(deviceCaps & V4L2_CAP_STREAMING) )
            {
               goto done_check;
            }

            eb.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
            eb.index= -1;
            eb.plane= -1;
            eb.flags= (O_RDWR|O_CLOEXEC);
            IOCTL( fd, VIDIOC_EXPBUF, &eb );
            if ( errno == ENOTTY )
            {
               goto done_check;
            }

            dummySink.soc.v4l2Fd= fd;
            if ( (deviceCaps & V4L2_CAP_VIDEO_M2M_MPLANE) && !(deviceCaps & V4L2_CAP_VIDEO_M2M) )
            {
               dummySink.soc.isMultiPlane= TRUE;
            }

            wstGetInputFormats( &dummySink );
            acceptsCompressed= false;
            for( i= 0; i < dummySink.soc.numInputFormats; ++i)
            {
               if ( dummySink.soc.inputFormats[i].flags & V4L2_FMT_FLAG_COMPRESSED )
               {
                  acceptsCompressed= true;
                  wstBuildSinkCaps( klass, &dummySink );
                  break;
               }
            }

            if ( dummySink.soc.inputFormats )
            {
               free( dummySink.soc.inputFormats );
            }
            dummySink.soc.numInputFormats;

            if ( !acceptsCompressed )
            {
               goto done_check;
            }

            gDeviceName= strdup(name );
            printf("westeros-sink: discover decoder: %s\n", gDeviceName);
            close( fd );
            break;

         done_check:
            if ( fd >= 0 )
            {
               close( fd );
               fd= -1;
            }
         }
      }
      closedir( dir );
   }
}

static void wstStartEvents( GstWesterosSink *sink )
{
   int rc;
   struct v4l2_event_subscription evtsub;

   memset( &evtsub, 0, sizeof(evtsub));

   evtsub.type= V4L2_EVENT_SOURCE_CHANGE;
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_SUBSCRIBE_EVENT, &evtsub );
   if ( rc == 0 )
   {
      sink->soc.hasEvents= TRUE;
   }
   else
   {
      GST_ERROR("wstStartEvents: event subscribe failed rc %d (errno %d)", rc, errno);
   }
}

static void wstStopEvents( GstWesterosSink *sink )
{
   int rc;
   struct v4l2_event_subscription evtsub;

   memset( &evtsub, 0, sizeof(evtsub));

   evtsub.type= V4L2_EVENT_ALL;
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_UNSUBSCRIBE_EVENT, &evtsub );
   if ( rc != 0 )
   {
      GST_ERROR("wstStopEvents: event unsubscribe failed rc %d (errno %d)", rc, errno);
   }
}

static void wstProcessEvents( GstWesterosSink *sink )
{
   int rc;
   struct v4l2_event event;

   LOCK(sink);

   memset( &event, 0, sizeof(event));

   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_DQEVENT, &event );
   if ( rc == 0 )
   {
      if ( (event.type == V4L2_EVENT_SOURCE_CHANGE) &&
           (event.u.src_change.changes == V4L2_EVENT_SRC_CH_RESOLUTION) )
      {
         struct v4l2_format fmtIn, fmtOut;
         int32_t bufferType;

         g_print("westeros-sink: source change event\n");
         memset( &fmtIn, 0, sizeof(fmtIn));
         bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT);
         fmtIn.type= bufferType;
         rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_FMT, &fmtIn );

         memset( &fmtOut, 0, sizeof(fmtOut));
         bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE);
         fmtOut.type= bufferType;
         rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_FMT, &fmtOut );

         if ( (sink->soc.isMultiPlane &&
                ( (fmtOut.fmt.pix_mp.width != sink->soc.fmtOut.fmt.pix_mp.width) ||
                  (fmtOut.fmt.pix_mp.height != sink->soc.fmtOut.fmt.pix_mp.height) ) ) ||
              (!sink->soc.isMultiPlane &&
                ( (fmtOut.fmt.pix.width != sink->soc.fmtOut.fmt.pix.width) ||
                  (fmtOut.fmt.pix.height != sink->soc.fmtOut.fmt.pix.height) ) ) )
         {
            wstTearDownOutputBuffers( sink );

            if ( sink->soc.isMultiPlane )
            {
               sink->soc.frameWidth= fmtOut.fmt.pix_mp.width;
               sink->soc.frameHeight= fmtOut.fmt.pix_mp.height;
            }
            else
            {
               sink->soc.frameWidth= fmtOut.fmt.pix.width;
               sink->soc.frameHeight= fmtOut.fmt.pix.height;
            }
            #ifdef WESTEROS_SINK_SVP
            wstSVPSetInputMemMode( sink, sink->soc.inputMemMode );
            #endif
            wstSetupOutput( sink );
            sink->soc.nextFrameFd= -1;
            sink->soc.prevFrame1Fd= -1;
            sink->soc.prevFrame2Fd= -1;
            sink->soc.needCaptureRestart= TRUE;
         }
      }
   }
   UNLOCK(sink);
}

static void wstGetMaxFrameSize( GstWesterosSink *sink )
{
   struct v4l2_frmsizeenum framesize;
   int rc;
   int maxWidth= 0, maxHeight= 0;

   memset( &framesize, 0, sizeof(struct v4l2_frmsizeenum) );
   framesize.index= 0;
   framesize.pixel_format= ((sink->soc.inputFormat != 0) ? sink->soc.inputFormat : V4L2_PIX_FMT_H264);

   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_ENUM_FRAMESIZES, &framesize);
   if ( rc == 0 )
   {
      if ( framesize.type == V4L2_FRMSIZE_TYPE_DISCRETE )
      {
         maxWidth= framesize.discrete.width;
         maxHeight= framesize.discrete.height;
         while ( rc == 0 )
         {
            ++framesize.index;
            rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_ENUM_FRAMESIZES, &framesize);
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
         maxWidth= framesize.stepwise.max_width;
         maxHeight= framesize.stepwise.max_height;
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
   int32_t bufferType;

   bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT);

   i= 0;
   for( ; ; )
   {
      format.index= i;
      format.type= bufferType;
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_ENUM_FMT, &format);
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
      sink->soc.inputFormats[i].type= bufferType;
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_ENUM_FMT, &sink->soc.inputFormats[i]);
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
   int32_t bufferType;
   bool haveNV12= false;

   bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE);

   i= 0;
   for( ; ; )
   {
      format.index= i;
      format.type= bufferType;
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_ENUM_FMT, &format);
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
      sink->soc.outputFormats[i].type= bufferType;
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_ENUM_FMT, &sink->soc.outputFormats[i]);
      if ( rc < 0 )
      {
         goto exit;
      }
      if ( (sink->soc.outputFormats[i].pixelformat == V4L2_PIX_FMT_NV12) ||
           (sink->soc.outputFormats[i].pixelformat == V4L2_PIX_FMT_NV12M) )
      {
         haveNV12= true;
      }
      GST_DEBUG("output format %d: flags %08x pixelFormat: %x desc: %s",
             i, sink->soc.outputFormats[i].flags, sink->soc.outputFormats[i].pixelformat, sink->soc.outputFormats[i].description );
   }

   if ( !haveNV12 )
   {
      GST_WARNING("no support for NV12/NV12M output detected");
   }

   result= true;

exit:
   return result;
}

static bool wstSetInputFormat( GstWesterosSink *sink )
{
   bool result= false;
   int rc;
   int32_t bufferType;
   int bufferSize;

   bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT);

   bufferSize= 4*1024*1024;

   memset( &sink->soc.fmtIn, 0, sizeof(struct v4l2_format) );
   sink->soc.fmtIn.type= bufferType;
   if ( sink->soc.isMultiPlane )
   {
      sink->soc.fmtIn.fmt.pix_mp.pixelformat= sink->soc.inputFormat;
      sink->soc.fmtIn.fmt.pix_mp.width= sink->soc.frameWidth;
      sink->soc.fmtIn.fmt.pix_mp.height= sink->soc.frameHeight;
      sink->soc.fmtIn.fmt.pix_mp.num_planes= 1;
      sink->soc.fmtIn.fmt.pix_mp.plane_fmt[0].sizeimage= bufferSize;
      sink->soc.fmtIn.fmt.pix_mp.plane_fmt[0].bytesperline= 0;
      sink->soc.fmtIn.fmt.pix_mp.field= V4L2_FIELD_NONE;
   }
   else
   {
      sink->soc.fmtIn.fmt.pix.pixelformat= sink->soc.inputFormat;
      sink->soc.fmtIn.fmt.pix.width= sink->soc.frameWidth;
      sink->soc.fmtIn.fmt.pix.height= sink->soc.frameHeight;
      sink->soc.fmtIn.fmt.pix.sizeimage= bufferSize;
      sink->soc.fmtIn.fmt.pix.field= V4L2_FIELD_NONE;
   }
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_S_FMT, &sink->soc.fmtIn );
   if ( rc < 0 )
   {
      GST_DEBUG("wstSetInputFormat: failed to set format for input: rc %d errno %d", rc, errno);
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
   int32_t bufferType;

   bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE);

   memset( &sink->soc.fmtOut, 0, sizeof(struct v4l2_format) );
   sink->soc.fmtOut.type= bufferType;

   /* Get current settings from driver */
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_FMT, &sink->soc.fmtOut );
   if ( rc < 0 )
   {
      GST_DEBUG("wstSetOutputFormat: initV4l2: failed get format for output: rc %d errno %d", rc, errno);
   }

   if ( sink->soc.isMultiPlane )
   {
      int i;
      uint32_t pixelFormat= V4L2_PIX_FMT_NV12;
      if ( sink->soc.preferNV12M )
      {
         for( i= 0; i < sink->soc.numOutputFormats; ++i)
         {
            if ( sink->soc.outputFormats[i].pixelformat == V4L2_PIX_FMT_NV12M )
            {
               pixelFormat= V4L2_PIX_FMT_NV12M;
               break;
            }
         }
      }
      if ( pixelFormat == V4L2_PIX_FMT_NV12 )
      {
         sink->soc.fmtOut.fmt.pix_mp.num_planes= 1;
      }
      else
      {
         sink->soc.fmtOut.fmt.pix_mp.num_planes= 2;
      }
      sink->soc.fmtOut.fmt.pix_mp.pixelformat= pixelFormat;
      sink->soc.fmtOut.fmt.pix_mp.width= sink->soc.frameWidth;
      sink->soc.fmtOut.fmt.pix_mp.height= sink->soc.frameHeight;
      sink->soc.fmtOut.fmt.pix_mp.plane_fmt[0].sizeimage= sink->soc.frameWidth*sink->soc.frameHeight;
      sink->soc.fmtOut.fmt.pix_mp.plane_fmt[0].bytesperline= sink->soc.frameWidth;
      sink->soc.fmtOut.fmt.pix_mp.plane_fmt[1].sizeimage= sink->soc.frameWidth*sink->soc.frameHeight/2;
      sink->soc.fmtOut.fmt.pix_mp.plane_fmt[1].bytesperline= sink->soc.frameWidth;
      sink->soc.fmtOut.fmt.pix_mp.field= V4L2_FIELD_ANY;
   }
   else
   {
      sink->soc.fmtOut.fmt.pix.pixelformat= V4L2_PIX_FMT_NV12;
      sink->soc.fmtOut.fmt.pix.width= sink->soc.frameWidth;
      sink->soc.fmtOut.fmt.pix.height= sink->soc.frameHeight;
      sink->soc.fmtOut.fmt.pix.sizeimage= (sink->soc.fmtOut.fmt.pix.width*sink->soc.fmtOut.fmt.pix.height*3)/2;
      sink->soc.fmtOut.fmt.pix.field= V4L2_FIELD_ANY;
   }
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_S_FMT, &sink->soc.fmtOut );
   if ( rc < 0 )
   {
      GST_DEBUG("wstSetOutputFormat: initV4l2: failed to set format for output: rc %d errno %d", rc, errno);
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
   int32_t bufferType;
   uint32_t memOffset, memLength, memBytesUsed;
   int i, j;

   bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT);

   neededBuffers= NUM_INPUT_BUFFERS;

   memset( &ctl, 0, sizeof(ctl));
   ctl.id= V4L2_CID_MIN_BUFFERS_FOR_OUTPUT;
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_CTRL, &ctl );
   if ( rc == 0 )
   {
      sink->soc.minBuffersIn= ctl.value;
      if ( sink->soc.minBuffersIn != 0 )
      {
         neededBuffers= sink->soc.minBuffersIn;
      }
   }

   if ( sink->soc.minBuffersIn == 0 )
   {
      sink->soc.minBuffersIn= MIN_INPUT_BUFFERS;
   }

   memset( &reqbuf, 0, sizeof(reqbuf) );
   reqbuf.count= neededBuffers;
   reqbuf.type= bufferType;
   reqbuf.memory= sink->soc.inputMemMode;
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_REQBUFS, &reqbuf );
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

   for( i= 0; i < reqbuf.count; ++i )
   {
      bufIn= &sink->soc.inBuffers[i].buf;
      bufIn->type= bufferType;
      bufIn->index= i;
      bufIn->memory= sink->soc.inputMemMode;
      if ( sink->soc.isMultiPlane )
      {
         memset( sink->soc.inBuffers[i].planes, 0, sizeof(struct v4l2_plane)*WST_MAX_PLANES);
         bufIn->m.planes= sink->soc.inBuffers[i].planes;
         bufIn->length= 3;
      }
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_QUERYBUF, bufIn );
      if ( rc < 0 )
      {
         GST_ERROR("wstSetupInputBuffers: failed to query input buffer %d: rc %d errno %d", i, rc, errno);
         goto exit;
      }
      if ( sink->soc.inputMemMode == V4L2_MEMORY_MMAP )
      {
         if ( sink->soc.isMultiPlane )
         {
            if ( bufIn->length != 1 )
            {
               GST_ERROR("wstSetupInputBuffers: num planes expected to be 1 for compressed input but is %d", bufIn->length);
               goto exit;
            }
            sink->soc.inBuffers[i].planeCount= 0;
            memOffset= bufIn->m.planes[0].m.mem_offset;
            memLength= bufIn->m.planes[0].length;
            memBytesUsed= bufIn->m.planes[0].bytesused;
         }
         else
         {
            memOffset= bufIn->m.offset;
            memLength= bufIn->length;
            memBytesUsed= bufIn->bytesused;
         }

         bufStart= mmap( NULL,
                         memLength,
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED,
                         sink->soc.v4l2Fd,
                         memOffset );
         if ( bufStart == MAP_FAILED )
         {
            GST_ERROR("wstSetupInputBuffers: failed to mmap input buffer %d: errno %d", i, errno);
            goto exit;
         }

         GST_DEBUG("Input buffer: %d", i);
         GST_DEBUG("  index: %d start: %p bytesUsed %d  offset %d length %d flags %08x",
                 bufIn->index, bufStart, memBytesUsed, memOffset, memLength, bufIn->flags );

         sink->soc.inBuffers[i].start= bufStart;
         sink->soc.inBuffers[i].capacity= memLength;
      }
      else if ( sink->soc.inputMemMode == V4L2_MEMORY_DMABUF )
      {
         for ( j= 0; j < WST_MAX_PLANES; ++j )
         {
            sink->soc.inBuffers[i].planes[j].m.fd= -1;
         }
      }
      sink->soc.inBuffers[i].fd= -1;
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
   int rc, i;
   struct v4l2_requestbuffers reqbuf;
   int32_t bufferType;

   bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT);

   if ( sink->soc.inBuffers )
   {
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_STREAMOFF, &sink->soc.fmtIn.type );
      if ( rc < 0 )
      {
         GST_ERROR("wstTearDownInputBuffers: streamoff failed for input: rc %d errno %d", rc, errno );
      }

      for( i= 0; i < sink->soc.numBuffersIn; ++i )
      {
         if ( sink->soc.inBuffers[i].start )
         {
            munmap( sink->soc.inBuffers[i].start, sink->soc.inBuffers[i].capacity );
         }
         #ifdef USE_GST_ALLOCATORS
         if ( sink->soc.inBuffers[i].gstbuf )
         {
            gst_buffer_unref( sink->soc.inBuffers[i].gstbuf );
            sink->soc.inBuffers[i].gstbuf= 0;
         }
         #endif
      }
      free( sink->soc.inBuffers );
      sink->soc.inBuffers= 0;
   }

   if ( sink->soc.numBuffersIn )
   {
      memset( &reqbuf, 0, sizeof(reqbuf) );
      reqbuf.count= 0;
      reqbuf.type= bufferType;
      reqbuf.memory= sink->soc.inputMemMode;
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_REQBUFS, &reqbuf );
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
   int32_t bufferType;
   int i, j;

   bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE);

   neededBuffers= NUM_OUTPUT_BUFFERS;

   memset( &ctl, 0, sizeof(ctl));
   ctl.id= V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_CTRL, &ctl );
   if ( rc == 0 )
   {
      sink->soc.minBuffersOut= ctl.value;
      if ( (sink->soc.minBuffersOut != 0) && (sink->soc.minBuffersOut > NUM_OUTPUT_BUFFERS) )
      {
         neededBuffers= sink->soc.minBuffersOut+1;
      }
   }

   if ( sink->soc.minBuffersOut == 0 )
   {
      sink->soc.minBuffersOut= MIN_OUTPUT_BUFFERS;
   }

   memset( &reqbuf, 0, sizeof(reqbuf) );
   reqbuf.count= neededBuffers;
   reqbuf.type= bufferType;
   reqbuf.memory= sink->soc.outputMemMode;
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_REQBUFS, &reqbuf );
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

   for( i= 0; i < reqbuf.count; ++i )
   {
      sink->soc.outBuffers[i].bufferId= sink->soc.bufferIdOutBase+i;
      sink->soc.outBuffers[i].fd= -1;
      for( j= 0; j < 3; ++j )
      {
         sink->soc.outBuffers[i].planeInfo[j].fd= -1;
      }
   }

   if ( sink->soc.outputMemMode == V4L2_MEMORY_DMABUF )
   {
      result= wstSetupOutputBuffersDmabuf( sink );
   }
   else if ( sink->soc.outputMemMode == V4L2_MEMORY_MMAP )
   {
      result= wstSetupOutputBuffersMMap( sink );
   }
   else
   {
      GST_ERROR("Unsupported memory mode for output: %d", sink->soc.outputMemMode );
   }

   result= true;

exit:

   if ( !result )
   {
      wstTearDownOutputBuffers( sink );
   }

   return result;
}

static bool wstSetupOutputBuffersMMap( GstWesterosSink *sink )
{
   bool result= false;
   int32_t bufferType;
   struct v4l2_buffer *bufOut;
   struct v4l2_exportbuffer expbuf;
   void *bufStart;
   int rc, i, j;

   bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE);

   for( i= 0; i < sink->soc.numBuffersOut; ++i )
   {
      bufOut= &sink->soc.outBuffers[i].buf;
      bufOut->type= bufferType;
      bufOut->index= i;
      bufOut->memory= sink->soc.outputMemMode;
      if ( sink->soc.isMultiPlane )
      {
         memset( sink->soc.outBuffers[i].planes, 0, sizeof(struct v4l2_plane)*WST_MAX_PLANES);
         bufOut->m.planes= sink->soc.outBuffers[i].planes;
         bufOut->length= sink->soc.fmtOut.fmt.pix_mp.num_planes;
      }
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_QUERYBUF, bufOut );
      if ( rc < 0 )
      {
         GST_ERROR("wstSetupOutputBuffers: failed to query input buffer %d: rc %d errno %d", i, rc, errno);
         goto exit;
      }
      if ( sink->soc.isMultiPlane )
      {
         sink->soc.outBuffers[i].planeCount= bufOut->length;
         for( j= 0; j < sink->soc.outBuffers[i].planeCount; ++j )
         {
            GST_DEBUG("Output buffer: %d", i);
            GST_DEBUG("  index: %d bytesUsed %d offset %d length %d flags %08x",
                   bufOut->index, bufOut->m.planes[j].bytesused, bufOut->m.planes[j].m.mem_offset, bufOut->m.planes[j].length, bufOut->flags );

            memset( &expbuf, 0, sizeof(expbuf) );
            expbuf.type= bufOut->type;
            expbuf.index= i;
            expbuf.plane= j;
            expbuf.flags= O_CLOEXEC;
            rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_EXPBUF, &expbuf );
            if ( rc < 0 )
            {
               GST_ERROR("wstSetupOutputBuffers: failed to export v4l2 output buffer %d: plane: %d rc %d errno %d", i, j, rc, errno);
            }
            GST_DEBUG("  plane %d index %d export fd %d", j, expbuf.index, expbuf.fd );

            sink->soc.outBuffers[i].planeInfo[j].fd= expbuf.fd;
            sink->soc.outBuffers[i].planeInfo[j].capacity= bufOut->m.planes[j].length;

            if ( !sink->display )
            {
               bufStart= mmap( NULL,
                               bufOut->m.planes[j].length,
                               PROT_READ,
                               MAP_SHARED,
                               sink->soc.v4l2Fd,
                               bufOut->m.planes[j].m.mem_offset );
               if ( bufStart != MAP_FAILED )
               {
                  sink->soc.outBuffers[i].planeInfo[j].start= bufStart;
               }
               else
               {
                  GST_ERROR("wstSetupOutputBuffers: failed to mmap input buffer %d: errno %d", i, errno);
               }
            }
         }

         /* Use fd of first plane to identify buffer */
         sink->soc.outBuffers[i].fd= sink->soc.outBuffers[i].planeInfo[0].fd;
      }
      else
      {
         GST_DEBUG("Output buffer: %d", i);
         GST_DEBUG("  index: %d bytesUsed %d offset %d length %d flags %08x",
                bufOut->index, bufOut->bytesused, bufOut->m.offset, bufOut->length, bufOut->flags );

         memset( &expbuf, 0, sizeof(expbuf) );
         expbuf.type= bufOut->type;
         expbuf.index= i;
         expbuf.flags= O_CLOEXEC;
         rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_EXPBUF, &expbuf );
         if ( rc < 0 )
         {
            GST_ERROR("wstSetupOutputBuffers: failed to export v4l2 output buffer %d: rc %d errno %d", i, rc, errno);
         }
         GST_DEBUG("  index %d export fd %d", expbuf.index, expbuf.fd );

         sink->soc.outBuffers[i].fd= expbuf.fd;
         sink->soc.outBuffers[i].capacity= bufOut->length;

         if ( !sink->display )
         {
            bufStart= mmap( NULL,
                            bufOut->length,
                            PROT_READ,
                            MAP_SHARED,
                            sink->soc.v4l2Fd,
                            bufOut->m.offset );
            if ( bufStart != MAP_FAILED )
            {
               sink->soc.outBuffers[i].start= bufStart;
            }
            else
            {
               GST_ERROR("wstSetupOutputBuffers: failed to mmap input buffer %d: errno %d", i, errno);
            }
         }
      }
   }

exit:
   return result;
}

static bool wstSetupOutputBuffersDmabuf( GstWesterosSink *sink )
{
   bool result= false;

   #ifdef WESTEROS_SINK_SVP
   result= wstSVPSetupOutputBuffersDmabuf( sink );
   #endif

   return result;
}

static void wstTearDownOutputBuffers( GstWesterosSink *sink )
{
   int rc;
   struct v4l2_requestbuffers reqbuf;
   int32_t bufferType;

   ++sink->soc.bufferCohort;

   bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE);

   if ( sink->soc.outBuffers )
   {
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_STREAMOFF, &sink->soc.fmtOut.type );
      if ( rc < 0 )
      {
         GST_ERROR("wstTearDownOutputBuffers: streamoff failed for output: rc %d errno %d", rc, errno );
      }

      sink->soc.bufferIdOutBase += sink->soc.numBuffersOut;

      if ( sink->soc.outputMemMode == V4L2_MEMORY_DMABUF )
      {
         wstTearDownOutputBuffersDmabuf( sink );
      }
      else if ( sink->soc.outputMemMode == V4L2_MEMORY_MMAP )
      {
         wstTearDownOutputBuffersMMap( sink );
      }

      free( sink->soc.outBuffers );
      sink->soc.outBuffers= 0;
   }

   if ( sink->soc.numBuffersOut )
   {
      memset( &reqbuf, 0, sizeof(reqbuf) );
      reqbuf.count= 0;
      reqbuf.type= bufferType;
      reqbuf.memory= sink->soc.outputMemMode;
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_REQBUFS, &reqbuf );
      if ( rc < 0 )
      {
         GST_ERROR("wstTearDownOutputBuffers: failed to release v4l2 buffers for output: rc %d errno %d", rc, errno);
      }
      sink->soc.numBuffersOut= 0;
   }
}

static void wstTearDownOutputBuffersDmabuf( GstWesterosSink *sink )
{
   #ifdef WESTEROS_SINK_SVP
   wstSVPTearDownOutputBuffersDmabuf( sink );
   #endif
}

static void wstTearDownOutputBuffersMMap( GstWesterosSink *sink )
{
   int i, j;

   for( i= 0; i < sink->soc.numBuffersOut; ++i )
   {
      if ( sink->soc.outBuffers[i].planeCount )
      {
         for( j= 0; j < sink->soc.outBuffers[i].planeCount; ++j )
         {
            if ( sink->soc.outBuffers[i].planeInfo[j].fd >= 0 )
            {
               close( sink->soc.outBuffers[i].planeInfo[j].fd );
               sink->soc.outBuffers[i].planeInfo[j].fd= -1;
            }
            if ( sink->soc.outBuffers[i].planeInfo[j].start )
            {
               munmap( sink->soc.outBuffers[i].planeInfo[j].start, sink->soc.outBuffers[i].planeInfo[j].capacity );
            }
         }
         sink->soc.outBuffers[i].fd= -1;
         sink->soc.outBuffers[i].planeCount= 0;
      }
      if ( sink->soc.outBuffers[i].start )
      {
         munmap( sink->soc.outBuffers[i].start, sink->soc.outBuffers[i].capacity );
      }
      if ( sink->soc.outBuffers[i].fd >= 0 )
      {
         close( sink->soc.outBuffers[i].fd );
         sink->soc.outBuffers[i].fd= -1;
      }
   }
}

static void wstSetInputMemMode( GstWesterosSink *sink, int mode )
{
   sink->soc.inputMemMode= mode;
}

static void wstSetupInput( GstWesterosSink *sink )
{
   if ( sink->soc.formatsSet == FALSE )
   {
      if ( (sink->soc.frameWidth < 0) &&
           (sink->soc.frameHeight < 0) &&
           (sink->soc.hasEvents == TRUE) )
      {
         /* Set defaults and update on source change event */
         sink->soc.frameWidth= sink->soc.frameWidthStream= DEFAULT_FRAME_WIDTH;
         sink->soc.frameHeight= sink->soc.frameHeightStream= DEFAULT_FRAME_HEIGHT;
      }
      if ( (sink->soc.frameWidth > 0) &&
           (sink->soc.frameHeight > 0) )
      {
         wstGetMaxFrameSize( sink );
         #ifdef WESTEROS_SINK_SVP
         wstSVPSetInputMemMode( sink, sink->soc.inputMemMode );
         #endif
         wstSetInputFormat( sink );
         wstSetupInputBuffers( sink );
         sink->soc.formatsSet= TRUE;
      }
   }
}

static int wstGetInputBuffer( GstWesterosSink *sink )
{
   int bufferIndex= -1;
   int i;
   for( i= 0; i < sink->soc.numBuffersIn; ++i )
   {
      if ( !sink->soc.inBuffers[i].queued )
      {
         bufferIndex= i;
         break;
      }
   }

   if ( bufferIndex < 0 )
   {
      int rc;
      struct v4l2_buffer buf;
      struct v4l2_plane planes[WST_MAX_PLANES];
      memset( &buf, 0, sizeof(buf));
      buf.type= sink->soc.fmtIn.type;
      buf.memory= sink->soc.inputMemMode;
      if ( sink->soc.isMultiPlane )
      {
         buf.length= 1;
         buf.m.planes= planes;
      }
      GST_BASE_SINK_PREROLL_UNLOCK(GST_BASE_SINK(sink));
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_DQBUF, &buf );
      GST_BASE_SINK_PREROLL_LOCK(GST_BASE_SINK(sink));
      if ( rc == 0 )
      {
         bufferIndex= buf.index;
         if ( sink->soc.isMultiPlane )
         {
            memcpy( sink->soc.inBuffers[bufferIndex].buf.m.planes, buf.m.planes, sizeof(struct v4l2_plane)*WST_MAX_PLANES);
            buf.m.planes= sink->soc.inBuffers[bufferIndex].buf.m.planes;
         }
         sink->soc.inBuffers[bufferIndex].buf= buf;
         sink->soc.inBuffers[bufferIndex].queued= false;
      }
   }

   return bufferIndex;
}

static void wstSetOutputMemMode( GstWesterosSink *sink, int mode )
{
   int rc;
   int32_t bufferType= (sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE);

   memset( &sink->soc.fmtOut, 0, sizeof(struct v4l2_format) );
   sink->soc.fmtOut.type= bufferType;

   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_FMT, &sink->soc.fmtOut );
   if ( rc < 0 )
   {
      GST_DEBUG("wstSetOutputMemMode: initV4l2: failed get format for output: rc %d errno %d", rc, errno);
   }

   sink->soc.outputMemMode= mode;

   #ifdef WESTEROS_SINK_SVP
   wstSVPSetOutputMemMode( sink, mode );
   #endif
}

static void wstSetupOutput( GstWesterosSink *sink )
{
   int memMode;

   memMode= (sink->soc.useDmabufOutput ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP);

   wstSetOutputMemMode( sink, memMode );
   wstSetOutputFormat( sink );
   wstSetupOutputBuffers( sink );
}

static int wstGetOutputBuffer( GstWesterosSink *sink )
{
   int bufferIndex= -1;
   int rc;
   struct v4l2_buffer buf;
   struct v4l2_plane planes[WST_MAX_PLANES];

   memset( &buf, 0, sizeof(buf));
   buf.type= sink->soc.fmtOut.type;
   buf.memory= sink->soc.outputMemMode;
   if ( sink->soc.isMultiPlane )
   {
      buf.length= sink->soc.outBuffers[0].planeCount;
      buf.m.planes= planes;
   }
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_DQBUF, &buf );
   if ( rc == 0 )
   {
      bufferIndex= buf.index;
      if ( sink->soc.isMultiPlane )
      {
         memcpy( sink->soc.outBuffers[bufferIndex].buf.m.planes, buf.m.planes, sizeof(struct v4l2_plane)*WST_MAX_PLANES);
         buf.m.planes= sink->soc.outBuffers[bufferIndex].buf.m.planes;
      }
      sink->soc.outBuffers[bufferIndex].buf= buf;
      sink->soc.outBuffers[bufferIndex].queued= false;
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

static void wstLockOutputBuffer( GstWesterosSink *sink, int buffIndex )
{
   sink->soc.outBuffers[buffIndex].locked= true;
   ++sink->soc.outBuffers[buffIndex].lockCount;
}

static bool wstUnlockOutputBuffer( GstWesterosSink *sink, int buffIndex )
{
   bool unlocked= false;
   if ( !sink->soc.outBuffers[buffIndex].locked )
   {
      GST_ERROR("attempt to unlock buffer that is not locked: index %d", buffIndex);
   }
   if ( sink->soc.outBuffers[buffIndex].lockCount > 0 )
   {
      if ( --sink->soc.outBuffers[buffIndex].lockCount == 0 )
      {
         sink->soc.outBuffers[buffIndex].locked= false;
         unlocked= true;
      }
   }
   return unlocked;
}

static void wstRequeueOutputBuffer( GstWesterosSink *sink, int buffIndex )
{
   if ( !sink->soc.outBuffers[buffIndex].locked )
   {
      int rc;
      sink->soc.outBuffers[buffIndex].drop= false;
      sink->soc.outBuffers[buffIndex].frameNumber= -1;
      FRAME("out:       requeue buffer %d (%d)", sink->soc.outBuffers[buffIndex].bufferId, buffIndex);
      GST_LOG( "%lld: requeue: buffer %d (%d)", getCurrentTimeMillis(), sink->soc.outBuffers[buffIndex].bufferId, buffIndex);
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_QBUF, &sink->soc.outBuffers[buffIndex].buf );
      if ( rc < 0 )
      {
         GST_ERROR("wstRequeueOutputBuffer: failed to re-queue output buffer index %d: rc %d errno %d", buffIndex, rc, errno);
      }
      else
      {
         sink->soc.outBuffers[buffIndex].queued= true;
      }
   }
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
   }
}

static unsigned int getU32( unsigned char *p )
{
   unsigned n;

   n= (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|(p[3]);

   return n;
}

static int putU32( unsigned char *p, unsigned n )
{
   p[0]= (n>>24);
   p[1]= (n>>16);
   p[2]= (n>>8);
   p[3]= (n&0xFF);

   return 4;
}

static int putS64( unsigned char *p,  gint64 n )
{
   p[0]= (((guint64)n)>>56);
   p[1]= (((guint64)n)>>48);
   p[2]= (((guint64)n)>>40);
   p[3]= (((guint64)n)>>32);
   p[4]= (((guint64)n)>>24);
   p[5]= (((guint64)n)>>16);
   p[6]= (((guint64)n)>>8);
   p[7]= (((guint64)n)&0xFF);

   return 8;
}

static void wstSendFlushVideoClientConnection( WstVideoClientConnection *conn )
{
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[4];
      int len;
      int sentLen;

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
      mbody[len++]= 1;
      mbody[len++]= 'S';

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         GST_LOG("sent flush to video server");
         FRAME("sent flush to video server");
      }
   }
}

static void wstSendPauseVideoClientConnection( WstVideoClientConnection *conn, bool pause )
{
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[7];
      int len;
      int sentLen;

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
      mbody[len++]= 2;
      mbody[len++]= 'P';
      mbody[len++]= (pause ? 1 : 0);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         GST_LOG("sent pause %d to video server", pause);
         FRAME("sent pause %d to video server", pause);
      }
   }
}

static void wstSendHideVideoClientConnection( WstVideoClientConnection *conn, bool hide )
{
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[7];
      int len;
      int sentLen;

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
      mbody[len++]= 2;
      mbody[len++]= 'H';
      mbody[len++]= (hide ? 1 : 0);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         GST_LOG("sent hide %d to video server", hide);
         FRAME("sent hide %d to video server", hide);
      }
   }
}

static void wstSendSessionInfoVideoClientConnection( WstVideoClientConnection *conn )
{
   if ( conn )
   {
      GstWesterosSink *sink= conn->sink;
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[7];
      int len;
      int sentLen;

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
      mbody[len++]= 6;
      mbody[len++]= 'I';
      mbody[len++]= sink->soc.syncType;
      len += putU32( &mbody[len], conn->sink->soc.sessionId );

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         GST_DEBUG("sent session info: type %d sessionId %d to video server", sink->soc.syncType, sink->soc.sessionId);
         g_print("sent session info: type %d sessionId %d to video server\n", sink->soc.syncType, sink->soc.sessionId);
      }
   }
}

static void wstSetSessionInfo( GstWesterosSink *sink )
{
   #ifdef USE_AMLOGIC_MESON
   if ( sink->soc.conn )
   {
      GstElement *element= GST_ELEMENT(sink);
      GstClock *clock= GST_ELEMENT_CLOCK(element);
      int syncTypePrev= sink->soc.syncType;
      int sessionIdPrev= sink->soc.sessionId;
      sink->soc.syncType= 0;
      sink->soc.sessionId= 0;
      if ( clock )
      {
         const char *socClockName;
         gchar *clockName;
         clockName= gst_object_get_name(GST_OBJECT_CAST(clock));
         if ( clockName )
         {
            int sclen;
            int len= strlen(clockName);
            socClockName= getenv("WESTEROS_SINK_CLOCK");
            if ( !socClockName )
            {
               socClockName= "GstAmlSinkClock";
            }
            sclen= strlen(socClockName);
            if ( (len == sclen) && !strncmp(clockName, socClockName, len) )
            {
               sink->soc.syncType= 1;
               /* TBD: set sessionid */
            }
         }
      }
      if ( (syncTypePrev != sink->soc.syncType) || (sessionIdPrev != sink->soc.sessionId) )
      {
         wstSendSessionInfoVideoClientConnection( sink->soc.conn );
      }
   }
   #endif
}

static void wstProcessMessagesVideoClientConnection( WstVideoClientConnection *conn )
{
   if ( conn )
   {
      GstWesterosSink *sink= conn->sink;
      struct pollfd pfd;
      int rc;

      pfd.fd= conn->socketFd;
      pfd.events= POLLIN;
      pfd.revents= 0;

      rc= poll( &pfd, 1, 0);
      if ( rc == 1 )
      {
         struct msghdr msg;
         struct iovec iov[1];
         unsigned char mbody[64];
         unsigned char *m= mbody;
         int len;

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

         while ( len >= 4 )
         {
            if ( (m[0] == 'V') && (m[1] == 'S') )
            {
               int mlen, id;
               mlen= m[2];
               if ( len >= (mlen+3) )
               {
                  id= m[3];
                  switch( id )
                  {
                     case 'R':
                        if ( mlen >= 5)
                        {
                          int rate= getU32( &m[4] );
                          GST_DEBUG("got rate %d from video server", rate);
                          conn->serverRefreshRate= rate;
                          if ( rate )
                          {
                             conn->serverRefreshPeriod= 1000000LL/rate;
                          }
                          FRAME("got rate %d (period %lld us) from video server", rate, conn->serverRefreshPeriod);
                        }
                        break;
                     case 'B':
                        if ( mlen >= 5)
                        {
                          int bid= getU32( &m[4] );
                          if ( (bid >= sink->soc.bufferIdOutBase) && (bid < sink->soc.bufferIdOutBase+sink->soc.numBuffersOut) )
                          {
                             int bi= bid-sink->soc.bufferIdOutBase;
                             if ( sink->soc.outBuffers[bi].locked )
                             {
                                FRAME("out:       release received for buffer %d (%d)", bid, bi);
                                if ( sink->soc.useGfxSync &&
                                     !sink->soc.videoPaused &&
                                     (bi != sink->soc.pauseGfxBuffIndex) &&
                                     (sink->soc.enableTextureSignal ||
                                      (sink->soc.captureEnabled && sink->soc.sb)) )
                                {
                                   int buffIndex= wstFindVideoBuffer( sink, sink->soc.outBuffers[bi].frameNumber+2 );
                                   if ( buffIndex >= 0 )
                                   {
                                      if ( sink->soc.enableTextureSignal )
                                      {
                                         wstProcessTextureSignal( sink, buffIndex );
                                      }
                                      else if ( sink->soc.captureEnabled && sink->soc.sb )
                                      {
                                         wstProcessTextureWayland( sink, buffIndex );
                                      }
                                   }
                                }
                                if ( wstUnlockOutputBuffer( sink, bi ) )
                                {
                                   wstRequeueOutputBuffer( sink, bi );
                                }
                             }
                             else
                             {
                                GST_ERROR("release received for non-locked buffer %d (%d)\n", bid, bi );
                                FRAME("out:       error: release received for non-locked buffer %d (%d)", bid, bi);
                             }
                          }
                          else
                          {
                             GST_DEBUG("release received for stale buffer %d\n", bid );
                             FRAME("out:       note: release received for stale buffer %d", bid);
                          }
                        }
                        break;
                     default:
                        break;
                  }
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
   }
}

static bool wstSendFrameVideoClientConnection( WstVideoClientConnection *conn, int buffIndex )
{
   bool result= false;
   GstWesterosSink *sink= conn->sink;
   int sentLen;

   if ( conn  )
   {
      struct msghdr msg;
      struct cmsghdr *cmsg;
      struct iovec iov[1];
      unsigned char mbody[4+64];
      char cmbody[CMSG_SPACE(3*sizeof(int))];
      int i, len;
      int *fd;
      int numFdToSend;
      int frameFd0= -1, frameFd1= -1, frameFd2= -1;
      int fdToSend0= -1, fdToSend1= -1, fdToSend2= -1;
      int offset0, offset1, offset2;
      int stride0, stride1, stride2;
      uint32_t pixelFormat;
      int bufferId= -1;
      int vx, vy, vw, vh;

      wstProcessMessagesVideoClientConnection( conn );

      if ( buffIndex >= 0 )
      {
         sink->soc.resubFd= -1;

         bufferId= sink->soc.outBuffers[buffIndex].bufferId;

         numFdToSend= 1;
         offset0= offset1= offset2= 0;
         stride0= stride1= stride2= sink->soc.frameWidth;
         if ( sink->soc.outBuffers[buffIndex].planeCount > 1 )
         {
            frameFd0= sink->soc.outBuffers[buffIndex].planeInfo[0].fd;
            stride0= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[0].bytesperline;

            frameFd1= sink->soc.outBuffers[buffIndex].planeInfo[1].fd;
            stride1= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[1].bytesperline;
            if ( frameFd1 < 0 )
            {
               offset1= sink->soc.frameWidth*sink->soc.fmtOut.fmt.pix.height;
               stride1= stride0;
            }

            frameFd2= sink->soc.outBuffers[buffIndex].planeInfo[2].fd;
            stride2= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[2].bytesperline;
            if ( frameFd2 < 0 )
            {
               offset2= offset1+(sink->soc.frameWidth*sink->soc.fmtOut.fmt.pix.height)/2;
               stride2= stride0;
            }
         }
         else
         {
            frameFd0= sink->soc.outBuffers[buffIndex].fd;
            if ( sink->soc.isMultiPlane )
               stride0= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[0].bytesperline;
            else
               stride0= sink->soc.fmtOut.fmt.pix.bytesperline;
            offset1= stride0*sink->soc.fmtOut.fmt.pix.height;
            stride1= stride0;
            offset2= 0;
            stride2= 0;
         }

         pixelFormat= conn->sink->soc.fmtOut.fmt.pix.pixelformat;
         switch( pixelFormat  )
         {
            case V4L2_PIX_FMT_NV12:
            case V4L2_PIX_FMT_NV12M:
               pixelFormat= V4L2_PIX_FMT_NV12;
               break;
            default:
               GST_WARNING("unsupported pixel format: %X", conn->sink->soc.fmtOut.fmt.pix.pixelformat);
               break;
         }

         fdToSend0= fcntl( frameFd0, F_DUPFD_CLOEXEC, 0 );
         if ( fdToSend0 < 0 )
         {
            GST_ERROR("wstSendFrameVideoClientConnection: failed to dup fd0");
            goto exit;
         }
         if ( frameFd1 >= 0 )
         {
            fdToSend1= fcntl( frameFd1, F_DUPFD_CLOEXEC, 0 );
            if ( fdToSend1 < 0 )
            {
               GST_ERROR("wstSendFrameVideoClientConnection: failed to dup fd1");
               goto exit;
            }
            ++numFdToSend;
         }
         if ( frameFd2 >= 0 )
         {
            fdToSend2= fcntl( frameFd2, F_DUPFD_CLOEXEC, 0 );
            if ( fdToSend2 < 0 )
            {
               GST_ERROR("wstSendFrameVideoClientConnection: failed to dup fd2");
               goto exit;
            }
            ++numFdToSend;
         }

         vx= sink->soc.videoX;
         vy= sink->soc.videoY;
         vw= sink->soc.videoWidth;
         vh= sink->soc.videoHeight;
         if ( sink->soc.forceAspectRatio )
         {
            wstGetVideoBounds( sink, &vx, &vy, &vw, &vh );
         }

         i= 0;
         mbody[i++]= 'V';
         mbody[i++]= 'S';
         mbody[i++]= 65;
         mbody[i++]= 'F';
         i += putU32( &mbody[i], conn->sink->soc.frameWidth );
         i += putU32( &mbody[i], conn->sink->soc.frameHeight );
         i += putU32( &mbody[i], pixelFormat );
         i += putU32( &mbody[i], vx );
         i += putU32( &mbody[i], vy );
         i += putU32( &mbody[i], vw );
         i += putU32( &mbody[i], vh );
         i += putU32( &mbody[i], offset0 );
         i += putU32( &mbody[i], stride0 );
         i += putU32( &mbody[i], offset1 );
         i += putU32( &mbody[i], stride1 );
         i += putU32( &mbody[i], offset2 );
         i += putU32( &mbody[i], stride2 );
         i += putU32( &mbody[i], bufferId );
         i += putS64( &mbody[i], sink->soc.outBuffers[buffIndex].frameTime );

         iov[0].iov_base= (char*)mbody;
         iov[0].iov_len= i;

         cmsg= (struct cmsghdr*)cmbody;
         cmsg->cmsg_len= CMSG_LEN(numFdToSend*sizeof(int));
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
         fd[0]= fdToSend0;
         if ( fdToSend1 >= 0 )
         {
            fd[1]= fdToSend1;
         }
         if ( fdToSend2 >= 0 )
         {
            fd[2]= fdToSend2;
         }
         GST_LOG( "%lld: send frame: %d, fd (%d, %d, %d [%d, %d, %d])", getCurrentTimeMillis(), buffIndex, frameFd0, frameFd1, frameFd2, fdToSend0, fdToSend1, fdToSend2);
         wstLockOutputBuffer( sink, buffIndex );
         FRAME("out:       send frame %d buffer %d (%d)", conn->sink->soc.frameOutCount-1, conn->sink->soc.outBuffers[buffIndex].bufferId, buffIndex);

         do
         {
            sentLen= sendmsg( conn->socketFd, &msg, 0 );
         }
         while ( (sentLen < 0) && (errno == EINTR));

         conn->sink->soc.outBuffers[buffIndex].frameNumber= conn->sink->soc.frameOutCount-1;

         if ( sentLen == iov[0].iov_len )
         {
            result= true;
         }
         else
         {
            FRAME("out:       failed send frame %d buffer %d (%d)", conn->sink->soc.frameOutCount-1, conn->sink->soc.outBuffers[buffIndex].bufferId, buffIndex);
            wstUnlockOutputBuffer( sink, buffIndex );
         }
      }

exit:
      if ( fdToSend0 >= 0 )
      {
         close( fdToSend0 );
      }
      if ( fdToSend1 >= 0 )
      {
         close( fdToSend1 );
      }
      if ( fdToSend2 >= 0 )
      {
         close( fdToSend2 );
      }
   }
   return result;
}

static void wstDecoderReset( GstWesterosSink *sink, bool hard )
{
   long long delay;

   sink->soc.quitVideoOutputThread= TRUE;

   delay= ((sink->soc.frameRate > 0) ? 1000000/sink->soc.frameRate : 1000000/60);
   usleep( delay );

   wstTearDownInputBuffers( sink );

   wstTearDownOutputBuffers( sink );

   if ( sink->soc.videoOutputThread )
   {
      g_thread_join( sink->soc.videoOutputThread );
      sink->soc.videoOutputThread= NULL;
   }

   if ( hard )
   {
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

      wstStartEvents( sink );
   }

   sink->videoStarted= FALSE;
   sink->startAfterCaps= TRUE;
   sink->soc.prevFrameTimeGfx= 0;
   sink->soc.prevFramePTSGfx= 0;
   sink->soc.prevFrame1Fd= -1;
   sink->soc.prevFrame2Fd= -1;
   sink->soc.nextFrameFd= -1;
   sink->soc.formatsSet= FALSE;
}

typedef struct bufferInfo
{
   GstWesterosSink *sink;
   int buffIndex;
   int cohort;
} bufferInfo;

static void buffer_release( void *data, struct wl_buffer *buffer )
{
   int rc;
   bufferInfo *binfo= (bufferInfo*)data;

   GstWesterosSink *sink= binfo->sink;

   if ( (sink->soc.v4l2Fd >= 0) &&
        (binfo->buffIndex >= 0) &&
        (binfo->cohort == sink->soc.bufferCohort) )
   {
      FRAME("out:       wayland release received for buffer %d", binfo->buffIndex);
      LOCK(sink);
      if ( binfo->buffIndex == sink->soc.pauseGfxBuffIndex )
      {
         sink->soc.pauseGfxBuffIndex= -1;
      }
      if ( wstUnlockOutputBuffer( sink, binfo->buffIndex ) )
      {
         wstRequeueOutputBuffer( sink, binfo->buffIndex );
      }
      UNLOCK(sink);
   }

   --sink->soc.activeBuffers;
   wl_buffer_destroy( buffer );

   free( binfo );
}

static struct wl_buffer_listener wl_buffer_listener=
{
   buffer_release
};

static bool wstLocalRateControl( GstWesterosSink *sink, int buffIndex )
{
   bool drop= false;
   WstVideoClientConnection *conn= sink->soc.conn;
   gint64 framePTS;
   gint64 currFrameTime;

   framePTS= sink->soc.outBuffers[buffIndex].frameTime;

   if ( framePTS < sink->segment.start/1000LL )
   {
      FRAME("out:       drop out-of-segment frame");
      drop= true;
      goto exit;
   }

   if ( !sink->soc.conn || !sink->soc.useGfxSync )
   {
      if ( sink->soc.enableTextureSignal ||
           (sink->soc.captureEnabled && sink->soc.sb) )
      {
         if ( conn )
         {
            gint64 interval= (sink->soc.prevFramePTSGfx != 0LL) ? framePTS-sink->soc.prevFramePTSGfx : 0LL;

            /*
             * If the stream frame rate is greater than the display
             * refresh rate reported by the server, drop frames as necessary
             */
            if ( conn->serverRefreshRate && (buffIndex >= 0) )
            {
               if ( (sink->soc.prevFramePTSGfx != 0LL) && (interval < conn->serverRefreshPeriod) )
               {
                  FRAME("set drop true: interval %lld refresh period %lld gfx", interval, conn->serverRefreshPeriod);
                  drop= true;
                  goto exit;
               }
            }
         }

         currFrameTime= g_get_monotonic_time();
         if ( sink->soc.prevFrameTimeGfx && sink->soc.prevFramePTSGfx )
         {
            gint64 framePeriod= currFrameTime-sink->soc.prevFrameTimeGfx;
            gint64 nominalFramePeriod= framePTS-sink->soc.prevFramePTSGfx;
            gint64 delay= (nominalFramePeriod-framePeriod)/1000;
            if ( (delay > 2) && (delay <= nominalFramePeriod) )
            {
               usleep( (delay-1)*1000 );
               currFrameTime= g_get_monotonic_time();
            }
         }
         sink->soc.prevFrameTimeGfx= currFrameTime;
         sink->soc.prevFramePTSGfx= framePTS;
      }
   }

exit:
   return drop;
}

static void wstGetVideoBounds( GstWesterosSink *sink, int *x, int *y, int *w, int *h )
{
   int vx, vy, vw, vh;
   double arf, ard;
   vx= sink->soc.videoX;
   vy= sink->soc.videoY;
   vw= sink->soc.videoWidth;
   vh= sink->soc.videoHeight;
   ard= (double)sink->soc.videoWidth/(double)sink->soc.videoHeight;
   arf= (double)sink->soc.frameWidth/(double)sink->soc.frameHeight;
   if ( arf >= ard )
   {
      vh= (sink->soc.frameHeight * sink->soc.videoWidth) / sink->soc.frameWidth;
      vy= vy+(sink->soc.videoHeight-vh)/2;
   }
   else
   {
      vw= (sink->soc.frameWidth * sink->soc.videoHeight) / sink->soc.frameHeight;
      vx= vx+(sink->soc.videoWidth-vw)/2;
   }
   *x= vx;
   *y= vy;
   *w= vw;
   *h= vh;
}

static void wstProcessTextureSignal( GstWesterosSink *sink, int buffIndex )
{
   int fd0, l0, s0, fd1, l1, fd2, s1, l2, s2;
   void *p0, *p1, *p2;
   if ( sink->soc.outBuffers[buffIndex].planeCount > 1 )
   {
      fd0= sink->soc.outBuffers[buffIndex].planeInfo[0].fd;
      fd1= sink->soc.outBuffers[buffIndex].planeInfo[1].fd;
      fd2= -1;
      s0= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[0].bytesperline;
      s1= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[1].bytesperline;
      s2= 0;
      l0= s0*sink->soc.fmtOut.fmt.pix.height;
      l1= s0*sink->soc.fmtOut.fmt.pix.height/2;
      l2= 0;
      p0= sink->soc.outBuffers[buffIndex].planeInfo[0].start;
      p1= sink->soc.outBuffers[buffIndex].planeInfo[1].start;
      p2= 0;
   }
   else
   {
      fd0= sink->soc.outBuffers[buffIndex].fd;
      fd1= fd0;
      fd2= -1;
      if ( sink->soc.isMultiPlane )
         s0= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[0].bytesperline;
      else
         s0= sink->soc.fmtOut.fmt.pix.bytesperline;
      s1= s0;
      s2= 0;
      l0= s0*sink->soc.fmtOut.fmt.pix.height;
      l1= s0*sink->soc.fmtOut.fmt.pix.height/2;
      l2= 0;
      p0= sink->soc.outBuffers[buffIndex].start;
      p1= (char*)p0 + s0*sink->soc.fmtOut.fmt.pix.height;
      p2= 0;
   }

   g_signal_emit( G_OBJECT(sink),
                  g_signals[SIGNAL_NEWTEXTURE],
                  0,
                  sink->soc.outputFormat,
                  sink->soc.frameWidth,
                  sink->soc.frameHeight,
                  fd0, l0, s0, p0,
                  fd1, l1, s1, p1,
                  fd2, l2, s2, p2
                );
}

static bool wstProcessTextureWayland( GstWesterosSink *sink, int buffIndex )
{
   bool result= false;
   bufferInfo *binfo;

   GST_LOG("Video out: fd %d", sink->soc.outBuffers[buffIndex].fd );

   binfo= (bufferInfo*)malloc( sizeof(bufferInfo) );
   if ( binfo )
   {
      binfo->sink= sink;
      binfo->buffIndex= buffIndex;
      binfo->cohort= sink->soc.bufferCohort;

      struct wl_buffer *wlbuff;

      if ( sink->soc.outBuffers[buffIndex].planeCount > 1 )
      {
         int fd0, fd1, fd2;
         int stride0, stride1, stride2;
         int offset1= 0;
         fd0= sink->soc.outBuffers[buffIndex].planeInfo[0].fd;
         fd1= sink->soc.outBuffers[buffIndex].planeInfo[1].fd;
         fd2= sink->soc.outBuffers[buffIndex].planeInfo[2].fd;
         stride0= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[0].bytesperline;
         stride1= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[1].bytesperline;
         stride2= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[2].bytesperline;
         if ( fd1 < 0 )
         {
            fd1= fd0;
            stride1= stride0;
            offset1= stride0*sink->soc.fmtOut.fmt.pix.height;
         }
         if ( fd2 < 0 ) fd2= fd0;

         wlbuff= wl_sb_create_planar_buffer_fd2( sink->soc.sb,
                                                 fd0,
                                                 fd1,
                                                 fd2,
                                                 sink->soc.frameWidth,
                                                 sink->soc.frameHeight,
                                                 sink->soc.outputFormat,
                                                 0, /* offset0 */
                                                 offset1, /* offset1 */
                                                 0, /* offset2 */
                                                 stride0, /* stride0 */
                                                 stride1, /* stride1 */
                                                 0  /* stride2 */
                                               );
      }
      else
      {
         int stride;

         if ( sink->soc.isMultiPlane )
            stride= sink->soc.fmtOut.fmt.pix_mp.plane_fmt[0].bytesperline;
         else
            stride= sink->soc.fmtOut.fmt.pix.bytesperline;

         wlbuff= wl_sb_create_planar_buffer_fd( sink->soc.sb,
                                                sink->soc.outBuffers[buffIndex].fd,
                                                sink->soc.frameWidth,
                                                sink->soc.frameHeight,
                                                sink->soc.outputFormat,
                                                0, /* offset0 */
                                                stride*sink->soc.fmtOut.fmt.pix.height, /* offset1 */
                                                0, /* offset2 */
                                                stride, /* stride0 */
                                                stride, /* stride1 */
                                                0  /* stride2 */
                                              );
      }
      if ( wlbuff )
      {
         FRAME("out:       wayland send frame %d buffer %d (%d)", sink->soc.frameOutCount-1, sink->soc.outBuffers[buffIndex].bufferId, buffIndex);
         wl_buffer_add_listener( wlbuff, &wl_buffer_listener, binfo );
         wl_surface_attach( sink->surface, wlbuff, sink->windowX, sink->windowY );
         wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
         wl_surface_commit( sink->surface );
         wl_display_flush( sink->display );

         wstLockOutputBuffer( sink, buffIndex );

         ++sink->soc.activeBuffers;

         result= true;;

         /* Advance any frames sent to video server towards requeueing to decoder */
         sink->soc.resubFd= sink->soc.prevFrame2Fd;
         sink->soc.prevFrame2Fd=sink->soc.prevFrame1Fd;
         sink->soc.prevFrame1Fd= sink->soc.nextFrameFd;
         sink->soc.nextFrameFd= -1;

         if ( sink->soc.framesBeforeHideVideo )
         {
            if ( --sink->soc.framesBeforeHideVideo == 0 )
            {
               wstSendHideVideoClientConnection( sink->soc.conn, true );
               if ( !sink->soc.useGfxSync )
               {
                  wstSendFlushVideoClientConnection( sink->soc.conn );
               }
            }
         }
      }
      else
      {
         free( binfo );
      }
   }

   return result;
}

static int wstFindVideoBuffer( GstWesterosSink *sink, int frameNumber )
{
   int buffIndex= -1;
   int i;

   for( i= 0; i < sink->soc.numBuffersOut; ++i )
   {
      if ( sink->soc.outBuffers[i].locked )
      {
         if ( sink->soc.outBuffers[i].frameNumber == frameNumber )
         {
            buffIndex= i;
            break;
         }
      }
   }

   return buffIndex;
}

static int wstFindCurrentVideoBuffer( GstWesterosSink *sink )
{
   int buffIndex= -1;
   int i, oldestFrame= INT_MAX;

   for( i= 0; i < sink->soc.numBuffersOut; ++i )
   {
      if ( sink->soc.outBuffers[i].locked )
      {
         if ( (sink->soc.outBuffers[i].frameNumber >= 0) && (sink->soc.outBuffers[i].frameNumber < oldestFrame) )
         {
            oldestFrame= sink->soc.outBuffers[i].frameNumber;
            buffIndex= i;
         }
      }
   }

   return buffIndex;
}

static gpointer wstVideoOutputThread(gpointer data)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   struct v4l2_selection selection;
   int i, j, buffIndex, rc;
   int32_t bufferType;
   bool haveBaseTime= false;
   bool wasPaused= false;

   GST_DEBUG("wstVideoOutputThread: enter");

capture_start:
   for( i= 0; i < sink->soc.numBuffersOut; ++i )
   {
      if ( sink->soc.isMultiPlane )
      {
         for( j= 0; j < sink->soc.outBuffers[i].planeCount; ++j )
         {
            sink->soc.outBuffers[i].buf.m.planes[j].bytesused= sink->soc.outBuffers[i].buf.m.planes[j].length;
         }
      }
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_QBUF, &sink->soc.outBuffers[i].buf );
      if ( rc < 0 )
      {
         GST_ERROR("wstVideoOutputThread: failed to queue output buffer: rc %d errno %d", rc, errno);
         goto exit;
      }
      sink->soc.outBuffers[i].queued= true;
   }

   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_STREAMON, &sink->soc.fmtOut.type );
   if ( rc < 0 )
   {
      GST_ERROR("wstVideoOutputThread: streamon failed for output: rc %d errno %d", rc, errno );
      goto exit;
   }

   bufferType= sink->soc.isMultiPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :V4L2_BUF_TYPE_VIDEO_CAPTURE;
   memset( &selection, 0, sizeof(selection) );
   selection.type= bufferType;
   selection.target= V4L2_SEL_TGT_COMPOSE_DEFAULT;
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_SELECTION, &selection );
   if ( rc < 0 )
   {
      bufferType= V4L2_BUF_TYPE_VIDEO_CAPTURE;
      memset( &selection, 0, sizeof(selection) );
      selection.type= bufferType;
      selection.target= V4L2_SEL_TGT_COMPOSE_DEFAULT;
      rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_SELECTION, &selection );
      if ( rc < 0 )
      {
         GST_WARNING("wstVideoOutputThread: failed to get compose rect: rc %d errno %d", rc, errno );
      }
   }
   GST_DEBUG("Out compose default: (%d, %d, %d, %d)", selection.r.left, selection.r.top, selection.r.width, selection.r.height );

   memset( &selection, 0, sizeof(selection) );
   selection.type= bufferType;
   selection.target= V4L2_SEL_TGT_COMPOSE;
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_SELECTION, &selection );
   if ( rc < 0 )
   {
      GST_WARNING("wstVideoOutputThread: failed to get compose rect: rc %d errno %d", rc, errno );
   }
   GST_DEBUG("Out compose: (%d, %d, %d, %d)", selection.r.left, selection.r.top, selection.r.width, selection.r.height );
   if ( rc == 0 )
   {
      sink->soc.frameWidth= selection.r.width;
      sink->soc.frameHeight= selection.r.height;
   }

   g_print("westeros-sink: frame size %dx%d\n", sink->soc.frameWidth, sink->soc.frameHeight);

   wstSetSessionInfo( sink );

   for( ; ; )
   {
      if ( sink->soc.quitVideoOutputThread )
      {
         break;
      }
      else if ( sink->soc.videoPaused )
      {
         if ( !wasPaused )
         {
            wstSendPauseVideoClientConnection( sink->soc.conn, true);
         }
         wasPaused= true;

         if ( sink->soc.pauseException )
         {
            LOCK(sink);
            sink->soc.pauseException= FALSE;
            if ( sink->soc.useGfxSync && sink->soc.pauseGetGfxFrame )
            {
               sink->soc.pauseGetGfxFrame= FALSE;
               buffIndex= wstFindCurrentVideoBuffer( sink );
               if ( buffIndex >= 0 )
               {
                  sink->soc.pauseGfxBuffIndex= buffIndex;
                  if ( sink->soc.enableTextureSignal )
                  {
                     wstProcessTextureSignal( sink, buffIndex );
                  }
                  else if ( sink->soc.captureEnabled && sink->soc.sb )
                  {
                     wstProcessTextureWayland( sink, buffIndex );
                  }
               }
            }
            UNLOCK(sink);
         }

         if ( sink->soc.hasEvents )
         {
            struct pollfd pfd;

            pfd.fd= sink->soc.v4l2Fd;
            pfd.events= POLLIN | POLLRDNORM | POLLPRI;
            pfd.revents= 0;

            poll( &pfd, 1, 0);

            if ( sink->soc.quitVideoOutputThread ) break;

            if ( pfd.revents & POLLPRI )
            {
               wstProcessEvents( sink );
               if ( sink->soc.needCaptureRestart )
               {
                  break;
               }
            }

            if ( sink->soc.quitVideoOutputThread ) break;
         }

         usleep( 1000 );
      }
      else
      {
         int rc;

         LOCK(sink);
         wstProcessMessagesVideoClientConnection( sink->soc.conn );

         if ( wasPaused && !sink->soc.videoPaused )
         {
            wstSendPauseVideoClientConnection( sink->soc.conn, false);
            wasPaused= false;
            haveBaseTime= false;
         }
         UNLOCK(sink);

         if ( sink->soc.hasEvents )
         {
            struct pollfd pfd;

            pfd.fd= sink->soc.v4l2Fd;
            pfd.events= POLLIN | POLLRDNORM | POLLPRI;
            pfd.revents= 0;

            poll( &pfd, 1, 0);

            if ( sink->soc.quitVideoOutputThread ) break;

            if ( pfd.revents & POLLPRI )
            {
               wstProcessEvents( sink );
               if ( sink->soc.needCaptureRestart )
               {
                  break;
               }
            }

            if ( sink->soc.quitVideoOutputThread ) break;

            if ( (pfd.revents & (POLLIN|POLLRDNORM)) == 0  )
            {
               usleep( 1000 );
               continue;
            }
         }

         buffIndex= wstGetOutputBuffer( sink );

         if ( sink->soc.quitVideoOutputThread ) break;

         {
            bool gfxFrame= false;
            sink->soc.resubFd= -1;

            gint64 currFramePTS= sink->soc.outBuffers[buffIndex].buf.timestamp.tv_sec * 1000000LL + sink->soc.outBuffers[buffIndex].buf.timestamp.tv_usec;
            guint64 frameTime= sink->soc.outBuffers[buffIndex].buf.timestamp.tv_sec * 1000000000LL + sink->soc.outBuffers[buffIndex].buf.timestamp.tv_usec * 1000LL;
            gint64 currentPTS= ((frameTime * 90000LL + GST_SECOND/2)/GST_SECOND);
            FRAME("out:       frame %d buffer %d (%d) PTS %lld decoded", sink->soc.frameOutCount, sink->soc.outBuffers[buffIndex].bufferId, buffIndex, frameTime);

            sink->soc.outBuffers[buffIndex].frameTime= currFramePTS;

            LOCK(sink);
            if ( wstLocalRateControl( sink, buffIndex ) )
            {
               wstRequeueOutputBuffer( sink, buffIndex );
               UNLOCK(sink);
               continue;
            }

            sink->position= sink->positionSegmentStart + ((currentPTS - sink->firstPTS) * GST_MSECOND) / 90LL;
            if ( sink->soc.quitVideoOutputThread )
            {
               UNLOCK(sink);
               break;
            }
            if (sink->soc.frameOutCount == 0)
            {
                GST_DEBUG("wstVideoOutputThread: emit first frame signal");
                g_signal_emit (G_OBJECT (sink), g_signals[SIGNAL_FIRSTFRAME], 0, 2, NULL);
            }
            ++sink->soc.frameOutCount;
            if ( sink->windowChange )
            {
               sink->windowChange= false;
               gst_westeros_sink_soc_update_video_position( sink );
            }

            if ( !sink->soc.useGfxSync || !sink->soc.conn )
            {
               if ( sink->soc.enableTextureSignal )
               {
                  gfxFrame= true;
                  wstProcessTextureSignal( sink, buffIndex );
               }
               else if ( sink->soc.captureEnabled && sink->soc.sb )
               {
                  if ( wstProcessTextureWayland( sink, buffIndex ) )
                  {
                     gfxFrame= true;
                     buffIndex= -1;
                  }
               }
            }
            if ( sink->soc.conn && !gfxFrame )
            {
               sink->soc.resubFd= sink->soc.prevFrame2Fd;
               sink->soc.prevFrame2Fd= sink->soc.prevFrame1Fd;
               sink->soc.prevFrame1Fd= sink->soc.nextFrameFd;
               sink->soc.nextFrameFd= sink->soc.outBuffers[buffIndex].fd;

               if ( wstSendFrameVideoClientConnection( sink->soc.conn, buffIndex ) )
               {
                  buffIndex= -1;
               }

               if ( sink->soc.framesBeforeHideGfx )
               {
                  if ( --sink->soc.framesBeforeHideGfx == 0 )
                  {
                     wl_surface_attach( sink->surface, 0, sink->windowX, sink->windowY );
                     wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
                     wl_surface_commit( sink->surface );
                     wl_display_flush(sink->display);
                     UNLOCK(sink);
                     wl_display_dispatch_queue_pending(sink->display, sink->queue);
                     LOCK(sink);
                     wstSendHideVideoClientConnection( sink->soc.conn, false );
                  }
               }
            }

            if ( sink->soc.resubFd >= 0 )
            {
               buffIndex= wstFindOutputBuffer( sink, sink->soc.resubFd );
               GST_LOG( "%lld: resub: frame fd %d index %d", getCurrentTimeMillis(), sink->soc.resubFd, buffIndex);
               sink->soc.resubFd= -1;
            }

            if ( buffIndex >= 0 )
            {
               wstRequeueOutputBuffer( sink, buffIndex );
            }

            if ( ((sink->soc.frameInCount % QOS_INTERVAL) == 0) && sink->soc.frameInCount )
            {
               GstMessage *msg= gst_message_new_qos( GST_OBJECT_CAST(sink),
                                                     FALSE, /* live */
                                                     (sink->position-sink->positionSegmentStart), /* running time */
                                                     (sink->position-sink->positionSegmentStart), /* stream time */
                                                     sink->position, /* timestamp */
                                                     16000000UL /* duration */ );
               if ( msg )
               {
                  gst_message_set_qos_stats( msg,
                                             GST_FORMAT_BUFFERS,
                                             sink->soc.frameInCount,
                                             sink->soc.numDropped );
                  GST_INFO("post QoS: processed %u dropped %u", sink->soc.frameInCount, sink->soc.numDropped);
                  if ( !gst_element_post_message( GST_ELEMENT(sink), msg ) )
                  {
                     GST_WARNING("unable to post QoS");
                     gst_message_unref( msg );
                  }
               }
            }
            UNLOCK(sink);
         }
      }
   }

   if ( sink->soc.needCaptureRestart )
   {
      sink->soc.needCaptureRestart= FALSE;
      goto capture_start;
   }

exit:

   wstSendFlushVideoClientConnection( sink->soc.conn );

   GST_DEBUG("wstVideoOutputThread: exit");

   return NULL;
}

static gpointer wstEOSDetectionThread(gpointer data)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   int outputFrameCount, count, eosCountDown;
   bool videoPlaying;
   bool eosEventSeen;
   double frameRate;

   GST_DEBUG("wstVideoEOSThread: enter");

   eosCountDown= 10;
   LOCK(sink)
   outputFrameCount= sink->soc.frameOutCount;
   frameRate= (sink->soc.frameRate > 0.0 ? sink->soc.frameRate : 30.0);
   UNLOCK(sink);
   while( !sink->soc.quitEOSDetectionThread )
   {
      usleep( 1000000/frameRate );

      if ( !sink->soc.quitEOSDetectionThread )
      {
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

static GstFlowReturn prerollSinkSoc(GstBaseSink *base_sink, GstBuffer *buffer)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(base_sink);

   sink->soc.videoPlaying= TRUE;

   if ( sink->soc.frameStepOnPreroll )
   {
      gst_westeros_sink_soc_render( sink, buffer );
   }

   GST_INFO("preroll ok");

   return GST_FLOW_OK;
}

static int ioctl_wrapper( int fd, int request, void* arg )
{
   const char *req= 0;
   int rc;
   GstDebugLevel level;

   level= gst_debug_category_get_threshold( gst_westeros_sink_debug );
   if ( level >= GST_LEVEL_LOG )
   {
      switch( request )
      {
         case VIDIOC_QUERYCAP: req= "VIDIOC_QUERYCAP"; break;
         case VIDIOC_ENUM_FMT: req= "VIDIOC_ENUM_FMT"; break;
         case VIDIOC_G_FMT: req= "VIDIOC_G_FMT"; break;
         case VIDIOC_S_FMT: req= "VIDIOC_S_FMT"; break;
         case VIDIOC_REQBUFS: req= "VIDIOC_REQBUFS"; break;
         case VIDIOC_QUERYBUF: req= "VIDIOC_QUERYBUF"; break;
         case VIDIOC_G_FBUF: req= "VIDIOC_G_FBUF"; break;
         case VIDIOC_S_FBUF: req= "VIDIOC_S_FBUF"; break;
         case VIDIOC_OVERLAY: req= "VIDIOC_OVERLAY"; break;
         case VIDIOC_QBUF: req= "VIDIOC_QBUF"; break;
         case VIDIOC_EXPBUF: req= "VIDIOC_EXPBUF"; break;
         case VIDIOC_DQBUF: req= "VIDIOC_DQBUF"; break;
         case VIDIOC_DQEVENT: req= "VIDIOC_DQEVENT"; break;
         case VIDIOC_STREAMON: req= "VIDIOC_STREAMON"; break;
         case VIDIOC_STREAMOFF: req= "VIDIOC_STREAMOFF"; break;
         case VIDIOC_G_PARM: req= "VIDIOC_G_PARM"; break;
         case VIDIOC_S_PARM: req= "VIDIOC_S_PARM"; break;
         case VIDIOC_G_STD: req= "VIDIOC_G_STD"; break;
         case VIDIOC_S_STD: req= "VIDIOC_S_STD"; break;
         case VIDIOC_ENUMSTD: req= "VIDIOC_ENUMSTD"; break;
         case VIDIOC_ENUMINPUT: req= "VIDIOC_ENUMINPUT"; break;
         case VIDIOC_G_CTRL: req= "VIDIOC_G_CTRL"; break;
         case VIDIOC_S_CTRL: req= "VIDIOC_S_CTRL"; break;
         case VIDIOC_QUERYCTRL: req= "VIDIOC_QUERYCTRL"; break;
         case VIDIOC_ENUM_FRAMESIZES: req= "VIDIOC_ENUM_FRAMESIZES"; break;
         case VIDIOC_TRY_FMT: req= "VIDIOC_TRY_FMT"; break;
         case VIDIOC_CROPCAP: req= "VIDIOC_CROPCAP"; break;
         case VIDIOC_CREATE_BUFS: req= "VIDIOC_CREATE_BUFS"; break;
         case VIDIOC_G_SELECTION: req= "VIDIOC_G_SELECTION"; break;
         case VIDIOC_SUBSCRIBE_EVENT: req= "VIDIOC_SUBSCRIBE_EVENT"; break;
         case VIDIOC_UNSUBSCRIBE_EVENT: req= "VIDIOC_UNSUBSCRIBE_EVENT"; break;
         default: req= "NA"; break;
      }
      g_print("westerossink-ioctl: ioct( %d, %x ( %s ) )\n", fd, request, req );
      if ( request == (int)VIDIOC_S_FMT )
      {
         struct v4l2_format *format= (struct v4l2_format*)arg;
         g_print("westerossink-ioctl: : type %d\n", format->type);
         if ( (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ||
              (format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) )
         {
            g_print("westerossink-ioctl: pix_mp: pixelFormat %X w %d h %d field %d cs %d flg %x num_planes %d p0: sz %d bpl %d p1: sz %d bpl %d\n",
                    format->fmt.pix_mp.pixelformat,
                    format->fmt.pix_mp.width,
                    format->fmt.pix_mp.height,
                    format->fmt.pix_mp.field,
                    format->fmt.pix_mp.colorspace,
                    format->fmt.pix_mp.flags,
                    format->fmt.pix_mp.num_planes,
                    format->fmt.pix_mp.plane_fmt[0].sizeimage,
                    format->fmt.pix_mp.plane_fmt[0].bytesperline,
                    format->fmt.pix_mp.plane_fmt[1].sizeimage,
                    format->fmt.pix_mp.plane_fmt[1].bytesperline
                  );
         }
         else if ( (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
                   (format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) )
         {
            g_print("westerossink-ioctl: pix: pixelFormat %X w %d h %d field %d bpl %d\n",
                    format->fmt.pix.pixelformat,
                    format->fmt.pix.width,
                    format->fmt.pix.height,
                    format->fmt.pix.field,
                    format->fmt.pix.bytesperline
                  );
         }
      }
      else if ( request == (int)VIDIOC_REQBUFS )
      {
         struct v4l2_requestbuffers *rb= (struct v4l2_requestbuffers*)arg;
         g_print("westerossink-ioctl: count %d type %d mem %d\n", rb->count, rb->type, rb->memory);
      }
      else if ( request == (int)VIDIOC_QUERYBUF )
      {
            struct v4l2_buffer *buf= (struct v4l2_buffer*)arg;
            g_print("westerossink-ioctl: index %d type %d mem %d\n", buf->index, buf->type, buf->memory);
      }
      else if ( request == (int)VIDIOC_S_CTRL )
      {
         struct v4l2_control *control= (struct v4l2_control*)arg;
         g_print("westerossink-ioctl: ctrl id %d value %d\n", control->id, control->value);
      }
      else if ( request == (int)VIDIOC_CREATE_BUFS )
      {
         struct v4l2_create_buffers *cb= (struct v4l2_create_buffers*)arg;
         struct v4l2_format *format= &cb->format;
         g_print("westerossink-ioctl: count %d mem %d\n", cb->count, cb->memory);
         g_print("westerossink-ioctl: pix_mp: pixelFormat %X w %d h %d num_planes %d p0: sz %d bpl %d p1: sz %d bpl %d\n",
                 format->fmt.pix_mp.pixelformat,
                 format->fmt.pix_mp.width,
                 format->fmt.pix_mp.height,
                 format->fmt.pix_mp.num_planes,
                 format->fmt.pix_mp.plane_fmt[0].sizeimage,
                 format->fmt.pix_mp.plane_fmt[0].bytesperline,
                 format->fmt.pix_mp.plane_fmt[1].sizeimage,
                 format->fmt.pix_mp.plane_fmt[1].bytesperline
               );
      }
      else if ( request == (int)VIDIOC_QBUF )
      {
         struct v4l2_buffer *buf= (struct v4l2_buffer*)arg;
         g_print("westerossink-ioctl: buff: index %d q: type %d bytesused %d flags %X field %d mem %x length %d timestamp sec %ld usec %ld\n",
                buf->index, buf->type, buf->bytesused, buf->flags, buf->field, buf->memory, buf->length, buf->timestamp.tv_sec, buf->timestamp.tv_usec);
         if ( buf->m.planes &&
              ( (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ||
                (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ) )
         {
            if ( buf->memory == V4L2_MEMORY_DMABUF )
            {
               g_print("westerossink-ioctl: buff: p0 bu %d len %d fd %d doff %d p1 bu %d len %d fd %d doff %d\n",
                      buf->m.planes[0].bytesused, buf->m.planes[0].length, buf->m.planes[0].m.fd, buf->m.planes[0].data_offset,
                      buf->m.planes[1].bytesused, buf->m.planes[1].length, buf->m.planes[1].m.fd, buf->m.planes[1].data_offset );
            }
            else
            {
               g_print("westerossink-ioctl: buff: p0: bu %d len %d moff %d doff %d p1: bu %d len %d moff %d doff %d\n",
                      buf->m.planes[0].bytesused, buf->m.planes[0].length, buf->m.planes[0].m.mem_offset, buf->m.planes[0].data_offset,
                      buf->m.planes[1].bytesused, buf->m.planes[1].length, buf->m.planes[1].m.mem_offset, buf->m.planes[1].data_offset );
            }
         }
      }
      else if ( request == (int)VIDIOC_DQBUF )
      {
         struct v4l2_buffer *buf= (struct v4l2_buffer*)arg;
         g_print("westerossink-ioctl: buff: index %d s dq: type %d bytesused %d flags %X field %d mem %x length %d timestamp sec %ld usec %ld\n",
                buf->index, buf->type, buf->bytesused, buf->flags, buf->field, buf->memory, buf->length, buf->timestamp.tv_sec, buf->timestamp.tv_usec);
         if ( buf->m.planes &&
              ( (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ||
                (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ) )
         {
            g_print("westerossink-ioctl: buff: p0: bu %d len %d moff %d doff %d p1: bu %d len %d moff %d doff %d\n",
                   buf->m.planes[0].bytesused, buf->m.planes[0].length, buf->m.planes[0].m.mem_offset, buf->m.planes[0].data_offset,
                   buf->m.planes[1].bytesused, buf->m.planes[1].length, buf->m.planes[1].m.mem_offset, buf->m.planes[1].data_offset );
         }
      }
      else if ( (request == (int)VIDIOC_STREAMON) || (request == (int)VIDIOC_STREAMOFF) )
      {
         int *type= (int*)arg;
         g_print("westerossink-ioctl: : type %d\n", *type);
      }
   }

   rc= ioctl( fd, request, arg );


   if ( level >= GST_LEVEL_LOG )
   {
      if ( rc < 0 )
      {
         g_print("westerossink-ioctl: ioct( %d, %x ) rc %d errno %d\n", fd, request, rc, errno );
      }
      else
      {
         g_print("westerossink-ioctl: ioct( %d, %x ) rc %d\n", fd, request, rc );
         if ( (request == (int)VIDIOC_G_FMT) || (request == (int)VIDIOC_S_FMT) )
         {
            struct v4l2_format *format= (struct v4l2_format*)arg;
            if ( (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ||
                 (format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) )
            {
               g_print("westerossink-ioctl: pix_mp: pixelFormat %X w %d h %d field %d cs %d flg %x num_planes %d p0: sz %d bpl %d p1: sz %d bpl %d\n",
                       format->fmt.pix_mp.pixelformat,
                       format->fmt.pix_mp.width,
                       format->fmt.pix_mp.height,
                       format->fmt.pix_mp.field,
                       format->fmt.pix_mp.colorspace,
                       format->fmt.pix_mp.flags,
                       format->fmt.pix_mp.num_planes,
                       format->fmt.pix_mp.plane_fmt[0].sizeimage,
                       format->fmt.pix_mp.plane_fmt[0].bytesperline,
                       format->fmt.pix_mp.plane_fmt[1].sizeimage,
                       format->fmt.pix_mp.plane_fmt[1].bytesperline
                     );
            }
            else if ( (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
                      (format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) )
            {
               g_print("westerossink-ioctl: pix: pixelFormat %X w %d h %d field %d bpl %d\n",
                       format->fmt.pix.pixelformat,
                       format->fmt.pix.width,
                       format->fmt.pix.height,
                       format->fmt.pix.field,
                       format->fmt.pix.bytesperline
                     );
            }
         }
         else if ( request == (int)VIDIOC_REQBUFS )
         {
            struct v4l2_requestbuffers *rb= (struct v4l2_requestbuffers*)arg;
            g_print("westerossink-ioctl: count %d type %d mem %d\n", rb->count, rb->type, rb->memory);
         }
         else if ( request == (int)VIDIOC_CREATE_BUFS )
         {
            struct v4l2_create_buffers *cb= (struct v4l2_create_buffers*)arg;
            g_print("westerossink-ioctl: index %d count %d mem %d\n", cb->index, cb->count, cb->memory);
         }
         else if ( request == (int)VIDIOC_QUERYBUF )
         {
            struct v4l2_buffer *buf= (struct v4l2_buffer*)arg;
            g_print("westerossink-ioctl: index %d type %d flags %X mem %d\n", buf->index, buf->type, buf->flags, buf->memory);
         }
         else if ( request == (int)VIDIOC_G_CTRL )
         {
            struct v4l2_control *ctrl= (struct v4l2_control*)arg;
            g_print("westerossink-ioctl: id %d value %d\n", ctrl->id, ctrl->value);
         }
         else if ( request == (int)VIDIOC_DQBUF )
         {
            struct v4l2_buffer *buf= (struct v4l2_buffer*)arg;
            g_print("westerossink-ioctl: buff: index %d f dq: type %d bytesused %d flags %X field %d mem %x length %d seq %d timestamp sec %ld usec %ld\n",
                   buf->index, buf->type, buf->bytesused, buf->flags, buf->field, buf->memory, buf->length, buf->sequence, buf->timestamp.tv_sec, buf->timestamp.tv_usec);
            if ( buf->m.planes &&
                 ( (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ||
                   (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ) )
            {
               if ( buf->memory == V4L2_MEMORY_MMAP )
               {
                  g_print("westerossink-ioctl: buff: p0: bu %d len %d moff %d doff %d p1: bu %d len %d moff %d doff %d\n",
                         buf->m.planes[0].bytesused, buf->m.planes[0].length, buf->m.planes[0].m.mem_offset, buf->m.planes[0].data_offset,
                         buf->m.planes[1].bytesused, buf->m.planes[1].length, buf->m.planes[1].m.mem_offset, buf->m.planes[1].data_offset );
               }
               else if ( buf->memory == V4L2_MEMORY_DMABUF )
               {
                  g_print("westerossink-ioctl: buff: p0: bu %d len %d fd %d doff %d p1: bu %d len %d fd %d doff %d\n",
                         buf->m.planes[0].bytesused, buf->m.planes[0].length, buf->m.planes[0].m.fd, buf->m.planes[0].data_offset,
                         buf->m.planes[1].bytesused, buf->m.planes[1].length, buf->m.planes[1].m.fd, buf->m.planes[1].data_offset );
               }
            }
         }
         else if ( request == (int)VIDIOC_ENUM_FRAMESIZES )
         {
            struct v4l2_frmsizeenum *frmsz= (struct v4l2_frmsizeenum*)arg;
            g_print("westerossink-ioctl: fmt %x idx %d type %d\n", frmsz->pixel_format, frmsz->index, frmsz->type);
            if ( frmsz->type == V4L2_FRMSIZE_TYPE_DISCRETE )
            {
               g_print("westerossink-ioctl: discrete %dx%d\n", frmsz->discrete.width, frmsz->discrete.height);
            }
            else if ( frmsz->type == V4L2_FRMSIZE_TYPE_STEPWISE )
            {
               g_print("westerossink-ioctl: stepwise minw %d maxw %d stepw %d minh %d maxh %d steph %d\n",
                       frmsz->stepwise.min_width, frmsz->stepwise.max_width, frmsz->stepwise.step_width,
                       frmsz->stepwise.min_height, frmsz->stepwise.max_height, frmsz->stepwise.step_height );
            }
            else
            {
               g_print("westerossink-ioctl: continuous\n");
            }
         }
         else if ( request == (int)VIDIOC_DQEVENT )
         {
            struct v4l2_event *event= (struct v4l2_event*)arg;
            g_print("westerossink-ioctl: event: type %d\n", event->type);
         }
      }
   }

   return rc;
}
