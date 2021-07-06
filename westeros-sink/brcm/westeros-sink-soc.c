/*
 * Copyright (C) 2016 RDK Management
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "westeros-sink.h"

#if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
#include <gst/video/video-color.h>
#endif
#if (NEXUS_PLATFORM_VERSION_MAJOR > 17) || ((NEXUS_PLATFORM_VERSION_MAJOR == 17) && (NEXUS_PLATFORM_VERSION_MINOR > 1))
#ifdef ENABLE_DOLBYVISION
#include "nexus_hdmi_output_dbv.h"
#endif
#endif
#include "bmedia_types.h"

#define FRAME_POLL_TIME (8000)
#define EOS_DETECT_DELAY (500000)
#define DEFAULT_CAPTURE_WIDTH (1280)
#define DEFAULT_CAPTURE_HEIGHT (720)
#define MAX_PIP_WIDTH (640)
#define MAX_PIP_HEIGHT (360)
#define DEFAULT_LATENCY_TARGET (100)
#define MAX_ZORDER (100)
#define QOS_INTERVAL (1000)
#define DECODE_VERIFY_DELAY (5000000)
#define DECODE_VERIFY_MAX_BYTES (4*1024*1024)
#define MAX_STARTPTS_TO_BUFFER_PTS_MS (5000) /* related to max reasonable Iframe distances */

#ifndef DRM_FORMAT_RGBA8888
#define DRM_FORMAT_RGBA8888 (0x34324152)
#endif

GST_DEBUG_CATEGORY_EXTERN (gst_westeros_sink_debug);
#define GST_CAT_DEFAULT gst_westeros_sink_debug

#define postDecodeError( sink ) postErrorMessage( (sink), GST_STREAM_ERROR_DECODE, "video decode error" )

enum
{
  PROP_VIDEO_PTS_OFFSET= PROP_SOC_BASE,
  PROP_BUFFERED_BYTES,
  PROP_VIDEO_DECODER,
  PROP_ENABLE_CC_PASSTHRU,
  PROP_DISPLAY_RESOLUTION,
  PROP_WINDOW_SHOW,
  PROP_ZOOM_MODE,
  PROP_FORCE_ASPECT_RATIO,
  PROP_SERVER_PLAY_SPEED,
  PROP_LOW_DELAY,
  PROP_LATENCY_TARGET,
  PROP_CAPTURE_SIZE,
  PROP_HIDE_VIDEO_DURING_CAPTURE,
  PROP_CAMERA_LATENCY,
  PROP_IMMEDIATE_OUTPUT,
  PROP_FRAME_STEP_ON_PREROLL,
  PROP_ENABLE_TEXTURE,
  PROP_QUEUED_FRAMES,
  PROP_REPORT_DECODE_ERRORS
  #if (NEXUS_NUM_VIDEO_WINDOWS > 1)
  ,
  PROP_PIP
  #endif
  #if (NEXUS_PLATFORM_VERSION_MAJOR>=16)
  ,
  PROP_SECURE_VIDEO
  #endif
};

enum
{
   SIGNAL_FIRSTFRAME,
   SIGNAL_UNDERFLOW,
   SIGNAL_PTSERROR,
   SIGNAL_DECODEERROR,
   SIGNAL_NEWTEXTURE,
   SIGNAL_TIMECODE,
   MAX_SIGNAL
};

static guint g_signals[MAX_SIGNAL]= {0, 0, 0};

static void sinkSocStopVideo( GstWesterosSink *sink );
static void freeCaptureSurfaces( GstWesterosSink *sink );
static gboolean allocCaptureSurfaces( GstWesterosSink *sink );
static gboolean queryPeerHandles(GstWesterosSink *sink);
static void postErrorMessage( GstWesterosSink *sink, int errorCode, const char *errorText );
static gpointer captureThread(gpointer data);
static void processFrame( GstWesterosSink *sink );
static void updateVideoStatus( GstWesterosSink *sink );
static void firstPtsPassedCallback( void *userData, int n );
static void underflowCallback( void *userData, int n );
static void ptsErrorCallback( void *userData, int n );
static NEXUS_VideoCodec convertVideoCodecToNexus(bvideo_codec codec);
static long long getCurrentTimeMillis(void);
static void updateClientPlaySpeed( GstWesterosSink *sink, gfloat speed, gboolean playing );
static gboolean processEventSinkSoc( GstWesterosSink *sink, GstPad *pad, GstEvent *event, gboolean *passToDefault);
static GstFlowReturn prerollSinkSoc(GstBaseSink *base_sink, GstBuffer *buffer);
#if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
static void parseMasteringDisplayColorVolume( const gchar *metadata, NEXUS_MasteringDisplayColorVolume *colorVolume );
static void parseContentLightLevel( const gchar *str, NEXUS_ContentLightLevel *contentLightLevel );
#endif
static bool swIsSWDecode( GstWesterosSink *sink );
#ifdef ENABLE_SW_DECODE
static bool swInit( GstWesterosSink *sink );
static void swTerm( GstWesterosSink *sink );
static void swLink( GstWesterosSink *sink );
static void swUnLink( GstWesterosSink *sink );
static void swEvent( GstWesterosSink *sink, int id, int p1, void *p2 );
static void swDisplay( GstWesterosSink *sink, SWFrame *frame );
static bool establishSource( GstWesterosSink *sink );
#endif
static int sinkAcquireVideo( GstWesterosSink *sink );
static void sinkReleaseVideo( GstWesterosSink *sink );
static int sinkAcquireResources( GstWesterosSink *sink );
static void sinkReleaseResources( GstWesterosSink *sink );

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
   GstElementClass *gstelement_class;
   GstBaseSinkClass *gstbasesink_class= (GstBaseSinkClass *) klass;

   gstbasesink_class->preroll= GST_DEBUG_FUNCPTR(prerollSinkSoc);

   g_object_class_install_property (gobject_class, PROP_VIDEO_PTS_OFFSET,
     g_param_spec_uint ("video_pts_offset",
                        "PTS OFFSET",
                        "Measured in units of 45KHz",
                        0, G_MAXUINT, 0, G_PARAM_WRITABLE));

   g_object_class_install_property (gobject_class, PROP_BUFFERED_BYTES,
     g_param_spec_uint ("buffered_bytes",
                        "buffered bytes",
                        "Current buffered bytes",
                        0, G_MAXUINT32, 0, G_PARAM_READABLE));

   g_object_class_install_property (gobject_class, PROP_VIDEO_DECODER,
     g_param_spec_pointer ("videodecoder",
                           "Video decoder handle",
                           "Get the Nexus video decoder handle", G_PARAM_READABLE));

   g_object_class_install_property (gobject_class, PROP_ENABLE_CC_PASSTHRU,
     g_param_spec_boolean ("enable-cc-passthru",
                           "enable closed caption passthru",
                           "0: disable; 1: enable", TRUE, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_DISPLAY_RESOLUTION,
     g_param_spec_string ("resolution",
                          "Display resolution",
                          "Set the display resolution to one of the following strings: 480p, 720p, 1080p", "1080p", G_PARAM_WRITABLE));

   g_object_class_install_property (gobject_class, PROP_WINDOW_SHOW,
     g_param_spec_boolean ("show-video-window",
                           "make video window visible",
                           "true: visible, false: hidden", TRUE, G_PARAM_WRITABLE));

   g_object_class_install_property (gobject_class, PROP_ZOOM_MODE,
     g_param_spec_int ("zoom-mode",
                       "zoom-mode",
                       "Set zoom mode",
                       0, NEXUS_VideoWindowContentMode_eMax-1, NEXUS_VideoWindowContentMode_eFull, G_PARAM_READWRITE ));

   g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
     g_param_spec_boolean ("force-aspect-ratio",
                           "force aspect ratio",
                           "When enabled scaling respects source aspect ratio", FALSE, G_PARAM_READWRITE));

   g_object_class_install_property(gobject_class, PROP_SERVER_PLAY_SPEED,
      g_param_spec_float("server-play-speed", "server play speed",
          "Server side applied play speed",
          -G_MAXFLOAT, G_MAXFLOAT, 1.0, G_PARAM_READWRITE));

   g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CAPTURE_SIZE,
       g_param_spec_string ("capture_size", "capture size",
           "Capture Size Format: width,height",
           NULL, G_PARAM_WRITABLE));

   g_object_class_install_property (gobject_class, PROP_LOW_DELAY,
     g_param_spec_boolean ("low-delay",
                           "low delay",
                           "Control low delay mode", FALSE, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_LATENCY_TARGET,
     g_param_spec_int ("latency-target",
                       "latency target",
                       "Target latency in ms (use with low-delay)",
                       1, 2000, DEFAULT_LATENCY_TARGET, G_PARAM_READWRITE ));

   g_object_class_install_property (gobject_class, PROP_HIDE_VIDEO_DURING_CAPTURE,
     g_param_spec_boolean ("hide-video-during-capture",
                           "hide video window during video capture to graphics",
                           "true: hide video, false: don't hide video", TRUE, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_CAMERA_LATENCY,
     g_param_spec_boolean ("camera-latency",
                           "low latency camera mode",
                           "configure for low latency mode suitable for cameras", FALSE, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_IMMEDIATE_OUTPUT,
     g_param_spec_boolean ("immediate-output",
                           "immediate output mode",
                           "Decoded frames are output with minimum delay. B frames are dropped.", FALSE, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_FRAME_STEP_ON_PREROLL,
     g_param_spec_boolean ("frame-step-on-preroll",
                           "frame step on preroll",
                           "allow frame stepping on preroll into pause", FALSE, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_ENABLE_TEXTURE,
     g_param_spec_boolean ("enable-texture",
                           "enable texture signal",
                           "0: disable; 1: enable", FALSE, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_QUEUED_FRAMES,
     g_param_spec_uint ("queued-frames",
                       "queued frames",
                       "Get number for frames that are decoded and queued for rendering",
                       0, G_MAXUINT32, 0, G_PARAM_READABLE ));

   g_object_class_install_property (gobject_class, PROP_REPORT_DECODE_ERRORS,
     g_param_spec_boolean ("report-decode-errors",
                           "enable decodoe error signal",
                           "0: disable; 1: enable", FALSE, G_PARAM_READWRITE));


   #if (NEXUS_NUM_VIDEO_WINDOWS > 1)
   g_object_class_install_property (gobject_class, PROP_PIP,
     g_param_spec_boolean ("pip",
                           "use pip window",
                           "true: use pip window, false: use main window", FALSE, G_PARAM_READWRITE));
   #endif

   #if (NEXUS_PLATFORM_VERSION_MAJOR>=16)
   g_object_class_install_property (gobject_class, PROP_SECURE_VIDEO,
     g_param_spec_boolean ("secure-video",
                           "enable secure video",
                           "true: secure video enabled, false: secure video disabled", FALSE, G_PARAM_READWRITE));
   #endif

   gstelement_class= GST_ELEMENT_CLASS(klass);
   g_signals[SIGNAL_FIRSTFRAME]= g_signal_new( "first-video-frame-callback",
                                               G_TYPE_FROM_CLASS(gstelement_class),
                                               (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                               0,    // class offset
                                               NULL, // accumulator
                                               NULL, // accu data
                                               g_cclosure_marshal_VOID__UINT_POINTER,
                                               G_TYPE_NONE,
                                               2,
                                               G_TYPE_UINT,
                                               G_TYPE_POINTER );
   g_signals[SIGNAL_PTSERROR]= g_signal_new( "pts-error-callback",
                                              G_TYPE_FROM_CLASS(gstelement_class),
                                              (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                              0,    // class offset
                                              NULL, // accumulator
                                              NULL, // accu data
                                              g_cclosure_marshal_VOID__UINT_POINTER,
                                              G_TYPE_NONE,
                                              2,
                                              G_TYPE_UINT,
                                              G_TYPE_POINTER );
   g_signals[SIGNAL_DECODEERROR]= g_signal_new( "decode-error-callback",
                                                G_TYPE_FROM_CLASS(gstelement_class),
                                                (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                                0,    // class offset
                                                NULL, // accumulator
                                                NULL, // accu data
                                                g_cclosure_marshal_VOID__UINT_POINTER,
                                                G_TYPE_NONE,
                                                2,
                                                G_TYPE_UINT,
                                                G_TYPE_POINTER );
   g_signals[SIGNAL_UNDERFLOW]= g_signal_new( "buffer-underflow-callback",
                                              G_TYPE_FROM_CLASS(gstelement_class),
                                              (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                              0,    // class offset
                                              NULL, // accumulator
                                              NULL, // accu data
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

   #ifdef USE_GST_VIDEO
   g_signals[SIGNAL_TIMECODE]= g_signal_new( "timecode-callback",
                                              G_TYPE_FROM_CLASS(GST_ELEMENT_CLASS(klass)),
                                              (GSignalFlags) (G_SIGNAL_RUN_LAST),
                                              0,    /* class offset */
                                              NULL, /* accumulator */
                                              NULL, /* accu data */
                                              NULL,
                                              G_TYPE_NONE,
                                              3,
                                              G_TYPE_UINT, /* hours */
                                              G_TYPE_UINT, /* minutes */
                                              G_TYPE_UINT  /* seconds */
                                             );
   #endif

   klass->canUseResMgr= 0;
   {
      const char *env= getenv("WESTEROS_SINK_USE_ESSRMGR");
      if ( env && (atoi(env) != 0) )
      {
         klass->canUseResMgr= 1;

         #ifdef ENABLE_SW_DECODE
         env= getenv("WESTEROS_SINK_USE_SW_DECODE");
         if ( env && (atoi(env) != 0) )
         {
            GstCaps *caps= 0;
            caps= gst_caps_new_empty();
            if ( caps )
            {
               GstCaps *capsTemp= 0;
               GstPadTemplate *padTemplate= 0;

               capsTemp= gst_caps_from_string(
                                                "video/x-h264, " \
                                                "parsed=(boolean) true, " \
                                                "alignment=(string) au, " \
                                                "stream-format=(string) byte-stream, " \
                                                "width=(int) [1,MAX], " "height=(int) [1,MAX] ; "
                                             );
               gst_caps_append( caps, capsTemp );

               padTemplate= gst_pad_template_new( "sink",
                                                  GST_PAD_SINK,
                                                  GST_PAD_ALWAYS,
                                                  caps );
               if ( padTemplate )
               {
                  gst_element_class_add_pad_template(gstelement_class, padTemplate);
                  padTemplate= 0;
               }
               gst_caps_unref( caps );
            }
         }
         #endif
      }
   }
   printf("westerossink: enabling Essos RMgr: %d\n", klass->canUseResMgr);
}

#include <dlfcn.h>
typedef unsigned int (*GETSTCINFO)( NEXUS_SimpleStcChannelHandle stc );
bool checkIndependentVideoClock( GstWesterosSink *sink )
{
   bool independentClock= false;
   void *module= dlopen( "libbrcmgstutil.so", RTLD_NOW );
   if (!module)
   {
       module= dlopen( "libbrcmsystemclock.so", RTLD_NOW );
   }
   if ( module )
   {
      GETSTCINFO getSTCInfo= (GETSTCINFO)dlsym( module, "gst_brcm_system_clock_get_stc_channel_info" );
      if ( getSTCInfo )
      {
         unsigned int count= (*getSTCInfo)( sink->soc.stcChannel );
         if ( count < 2 )
         {
            independentClock= true;
         }
      }
      dlclose( module );
   }
   GST_INFO_OBJECT(sink, "independent video clock: %d", independentClock);
   return independentClock;
}

static bool isSVPEnabled( void )
{
   bool enabled= false;
   #if NEXUS_PLATFORM_VERSION_MAJOR >= 16
   NEXUS_VideoDecoderCapabilities videoDecoderCap;
   NEXUS_GetVideoDecoderCapabilities(&videoDecoderCap);
   enabled=  (videoDecoderCap.memory[0].secure == NEXUS_SecureVideo_eSecure) ? true : false;
   #endif
   printf("westerossink: using secure video: %d\n", enabled);
   return enabled;
}

static bool useSecureGraphics( void )
{
   bool useSecure= false;

   char *env= getenv("WESTEROS_SECURE_GRAPHICS");
   if ( env && atoi(env) )
   {
      useSecure= isSVPEnabled();
   }
   printf("westerossink: using secure graphics: %d\n", useSecure);

   return useSecure;
}

static void resourceChangedCallback( void *context, int param )
{
   NEXUS_Error rc;
   GstWesterosSink *sink= (GstWesterosSink*)context;
   NEXUS_SimpleVideoDecoderClientStatus clientStatus;
   rc= NEXUS_SimpleVideoDecoder_GetClientStatus( sink->soc.videoDecoder, &clientStatus );
   if ( rc == NEXUS_SUCCESS )
   {
      sink->soc.emitResourceChange= TRUE;
      sink->soc.haveResources= clientStatus.enabled;
      GST_INFO_OBJECT(sink, "haveResources: %d", sink->soc.haveResources);
      if ( !sink->soc.haveResources )
      {
         sink->soc.timeResourcesLost= getCurrentTimeMillis();
         sink->soc.positionResourcesLost= sink->position;
         sink->soc.ignoreDiscontinuity= TRUE;
      }
   }
}

#if (NEXUS_PLATFORM_VERSION_MAJOR > 17) || ((NEXUS_PLATFORM_VERSION_MAJOR == 17) && (NEXUS_PLATFORM_VERSION_MINOR > 1))
#ifdef ENABLE_DOLBYVISION
static bool IsDbvUnsupportedFormat(void)
{
   bool result;
   NxClient_DisplaySettings displaySettings;
   NEXUS_HdmiOutputStatus hdmiStatus;
   NEXUS_HdmiOutputHandle aliasedHdmi;
   NEXUS_HdmiOutputDbvEdidData dbvEdid;
   NEXUS_HdmiOutputEdidData edid;

   aliasedHdmi= NEXUS_HdmiOutput_Open(NEXUS_ALIAS_ID, NULL);
   NxClient_GetDisplaySettings(&displaySettings);
   NEXUS_HdmiOutput_GetStatus(aliasedHdmi, &hdmiStatus);
   NEXUS_HdmiOutput_GetDbvEdidData(aliasedHdmi, &dbvEdid);
   NEXUS_HdmiOutput_GetEdidData(aliasedHdmi, &edid);

   #define BHDM_HDMI_2_0_MAX_RATE 594

   result=
    (
        ( displaySettings.format == NEXUS_VideoFormat_e1080i ) || /* 1080i is not supported */
        (
            displaySettings.format == NEXUS_VideoFormat_e3840x2160p60hz
            &&
            (
                dbvEdid.supports2160p60 == NEXUS_TristateEnable_eDisable ||
                !hdmiStatus.videoFormatSupported[NEXUS_VideoFormat_e3840x2160p60hz] || /* 4kp60 422-12 isn't supported */
                !(edid.hdmiForumVsdb.valid) ||
                (edid.hdmiForumVsdb.maxTMDSCharacterRate < BHDM_HDMI_2_0_MAX_RATE)
            )
        ) ||
        (
            displaySettings.format == NEXUS_VideoFormat_e3840x2160p50hz
            &&
            (
                dbvEdid.supports2160p60 == NEXUS_TristateEnable_eDisable || /* also applies to 50 Hz */
                !hdmiStatus.videoFormatSupported[NEXUS_VideoFormat_e3840x2160p50hz] || /* 4kp50 422-12 isn't supported */
                !(edid.hdmiForumVsdb.valid) ||
                (edid.hdmiForumVsdb.maxTMDSCharacterRate < BHDM_HDMI_2_0_MAX_RATE)
            )
        )
    );

   return result;
}
#endif
#endif

#if (NEXUS_PLATFORM_VERSION_MAJOR > 15) || ((NEXUS_PLATFORM_VERSION_MAJOR == 15) && (NEXUS_PLATFORM_VERSION_MINOR > 2))
static void streamChangedCallback(void * context, int param)
{
   GstWesterosSink *sink= (GstWesterosSink*) context;
   NEXUS_SimpleVideoDecoderHandle decoderHandle = sink->soc.videoDecoder;
   NEXUS_VideoDecoderStreamInformation streamInfo;
   BSTD_UNUSED(param);

   NEXUS_SimpleVideoDecoder_GetStreamInformation(decoderHandle, &streamInfo);
   #if (NEXUS_PLATFORM_VERSION_MAJOR > 17) || ((NEXUS_PLATFORM_VERSION_MAJOR == 17) && (NEXUS_PLATFORM_VERSION_MINOR > 1))
   #ifdef ENABLE_DOLBYVISION
   NxClient_DisplaySettings displaySettings;
   NEXUS_Error rc= NEXUS_SUCCESS;
   NEXUS_VideoDynamicRangeMode dynamicRangeMode= NEXUS_VideoDynamicRangeMode_eTrackInput;
   #endif
   switch (streamInfo.dynamicMetadataType)
   {
      case NEXUS_VideoDecoderDynamicRangeMetadataType_eDolbyVision:
         #ifdef ENABLE_DOLBYVISION
         #if (NEXUS_PLATFORM_VERSION_MAJOR >= 19)
         dynamicRangeMode= NEXUS_VideoDynamicRangeMode_eDolbyVisionSourceLed;
         #else
         dynamicRangeMode= NEXUS_VideoDynamicRangeMode_eDolbyVision;
         #endif
         #endif
         GST_WARNING("Dolby Vision content decoding begins.");
         break;
      case NEXUS_VideoDecoderDynamicRangeMetadataType_eTechnicolorPrime:
         GST_WARNING(" Technicolor Prime content decoding begins.");
         break;
      case NEXUS_VideoDecoderDynamicRangeMetadataType_eNone:
      default:
         switch (streamInfo.eotf)
         {
            /*
             * eHdr represents gamma-based HDR, which is not used by anyone.
             * The enum value is deprecated and assigned to eInvalid now
             */
            case NEXUS_VideoEotf_eHdr10:
               GST_WARNING(" HDR content decoding begins.");
               break;
            case NEXUS_VideoEotf_eHlg:
               GST_WARNING(" HLG content decoding begins.");
               break;
            default:
               break;
         }
         break;
   }
   #ifdef ENABLE_DOLBYVISION
   NxClient_GetDisplaySettings(&displaySettings);
   #if (NEXUS_PLATFORM_VERSION_MAJOR >= 19)
   if(dynamicRangeMode == NEXUS_VideoDynamicRangeMode_eDolbyVisionSourceLed && IsDbvUnsupportedFormat())
   #else
   if(dynamicRangeMode == NEXUS_VideoDynamicRangeMode_eDolbyVision && IsDbvUnsupportedFormat())
   #endif
   {
       dynamicRangeMode= NEXUS_VideoDynamicRangeMode_eTrackInput;
       GST_WARNING("Dolby Vision not supported with current video format setting output mode to eTrackInput.");
   }

   if(displaySettings.hdmiPreferences.dynamicRangeMode != dynamicRangeMode)
   {
      displaySettings.hdmiPreferences.dynamicRangeMode= dynamicRangeMode;
      rc= NxClient_SetDisplaySettings(&displaySettings);
      if (rc)
      {
         GST_WARNING("unable to set display format using NxClient_SetDisplaySettings (%d)...", rc);
         return;
      }
   }
   #endif
   #endif

   GST_INFO("\nNEXUS_SimpleVideoDecoder_GetStreamInformation\n \
   \tvalid=%d \n \
   \tsourceHorizontalSize=%d \n \
   \tsourceHorizontalSizesourceVerticalSize=%d \n \
   \tsourceVerticalSizecodedSourceHorizontalSize=%d \n \
   \tcodedSourceHorizontalSizecodedSourceVerticalSize=%d \n \
   \tcodedSourceVerticalSizedisplayHorizontalSize=%d \n \
   \tdisplayHorizontalSizedisplayVerticalSize=%d \n \
   \tdisplayVerticalSizeaspectRatio=%d \n \
   \taspectRatiosampleAspectRatioX=%d \n \
   \tsampleAspectRatioXsampleAspectRatioY=%d \n \
   \tsampleAspectRatioYframeRate=%d \n \
   \tframeRateframeProgressive=%d \n \
   \tframeProgressivestreamProgressive=%d \n \
   \tstreamProgressivehorizontalPanScan=%d \n \
   \thorizontalPanScanverticalPanScan=%d \n \
   \tverticalPanScanlowDelayFlag=%d \n \
   \tlowDelayFlagfixedFrameRateFlag=%d \n \
   \tdynamicMetadataTypeeotf=%d " \
   , streamInfo.valid \
   , streamInfo.sourceHorizontalSize \
   , streamInfo.sourceVerticalSize \
   , streamInfo.codedSourceHorizontalSize \
   , streamInfo.codedSourceVerticalSize \
   , streamInfo.displayHorizontalSize \
   , streamInfo.displayVerticalSize \
   , streamInfo.aspectRatio \
   , streamInfo.sampleAspectRatioX \
   , streamInfo.sampleAspectRatioY \
   , streamInfo.frameRate \
   , streamInfo.frameProgressive \
   , streamInfo.streamProgressive \
   , streamInfo.horizontalPanScan \
   , streamInfo.verticalPanScan \
   , streamInfo.lowDelayFlag \
   , streamInfo.fixedFrameRateFlag \
   , streamInfo.eotf);
#if (NEXUS_PLATFORM_VERSION_MAJOR > 17) || ((NEXUS_PLATFORM_VERSION_MAJOR == 17) && (NEXUS_PLATFORM_VERSION_MINOR > 1))
   GST_INFO("\tfixedFrameRateFlagdynamicMetadataType=%d", streamInfo.dynamicMetadataType);
#endif
#if (NEXUS_PLATFORM_VERSION_MAJOR > 16) || ((NEXUS_PLATFORM_VERSION_MAJOR == 16) && (NEXUS_PLATFORM_VERSION_MINOR > 1))
   GST_INFO("\teotfcolorDepth=%d", streamInfo.colorDepth);
#endif
}
#endif

gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   NEXUS_Error rc;
   NxClient_AllocSettings allocSettings;
   int i;

   sink->soc.captureWidth= -1;
   sink->soc.captureHeight= -1;
   sink->soc.captureWidthNext= DEFAULT_CAPTURE_WIDTH;
   sink->soc.captureHeightNext= DEFAULT_CAPTURE_HEIGHT;;
   for( i= 0; i < NUM_CAPTURE_SURFACES; ++i )
   {
      sink->soc.captureSurface[i]= NULL;
   }
   sink->soc.hideVideoDuringCapture= TRUE;
   sink->soc.usePip= FALSE;
   sink->soc.useImmediateOutput= FALSE;
   sink->soc.useLowDelay= FALSE;
   sink->soc.frameStepOnPreroll= FALSE;
   sink->soc.enableTextureSignal= FALSE;
   sink->soc.enableDecodeErrorSignal= FALSE;
   sink->soc.latencyTarget= DEFAULT_LATENCY_TARGET;
   sink->soc.connectId= 0;
   sink->soc.quitCaptureThread= TRUE;
   sink->soc.captureThread= NULL;
   sink->soc.captureCount= 0;
   sink->soc.frameCount= 0;
   sink->soc.framesBeforeHideVideo= 0;
   sink->soc.numDecoded= 0;
   sink->soc.numDropped= 0;
   sink->soc.numDroppedOutOfSegment= 0;
   sink->soc.noFrameCount= 0;
   sink->soc.ignoreDiscontinuity= FALSE;
   sink->soc.checkForEOS= FALSE;
   sink->soc.emitEOS= FALSE;
   sink->soc.emitUnderflow= FALSE;
   sink->soc.emitPTSError= FALSE;
   sink->soc.emitResourceChange= FALSE;
   sink->soc.emitDecodeError= FALSE;
   sink->soc.decodeError= FALSE;
   sink->soc.prevQueueDepth= 0;
   sink->soc.prevFifoDepth= 0;
   sink->soc.prevNumDecoded= 0;
   sink->soc.prevNumDecodeErrors= 0;
   sink->soc.sb= 0;
   sink->soc.activeBuffers= 0;
   sink->soc.captureEnabled= FALSE;
   sink->soc.presentationStarted= FALSE;
   sink->soc.surfaceClientId= 0;
   sink->soc.surfaceClient= 0;
   sink->soc.videoWindow= 0;
   sink->soc.videoDecoderId= 0;
   sink->soc.videoDecoder= 0;
   sink->soc.ptsOffset= 0;
   sink->soc.zoomSet= FALSE;
   sink->soc.zoomMode= NEXUS_VideoWindowContentMode_eFull;
   sink->soc.pixelAspectRatio= 1.0;
   sink->soc.havePixelAspectRatio= FALSE;
   sink->soc.pixelAspectRatioChanged= FALSE;
   sink->soc.forceAspectRatio= FALSE;
   sink->soc.lastStartPts45k= 0;
   sink->soc.chkBufToStartPts= FALSE;
   sink->soc.outputFormat= NEXUS_VideoFormat_eUnknown;
   sink->soc.serverPlaySpeed= 1.0;
   sink->soc.clientPlaySpeed= 1.0;
   sink->soc.stoppedForPlaySpeedChange= FALSE;
   sink->soc.videoPlaying= FALSE;
   sink->soc.haveResources= FALSE;
   sink->soc.timeResourcesLost= 0;
   sink->soc.positionResourcesLost= 0;
   sink->soc.haveHardware= FALSE;
   #ifdef ENABLE_SW_DECODE
   sink->soc.dataProbeNeedStartCodes= FALSE;
   sink->soc.dataProbePad= 0;
   sink->soc.dataProbeId= 0;
   sink->soc.removeDataProbe= false;
   sink->soc.dataProbeCodecData= 0;
   sink->soc.dataProbeCodecDataLen= 0;
   sink->soc.swPrerolled= false;
   sink->soc.swWorkSurface= 0;
   sink->soc.swNextCaptureSurface= 0;
   sink->soc.firstFrameThread= NULL;
   sink->soc.g2d= 0;
   sink->soc.g2dEventCreated= false;
   sink->soc.frameWidth= -1;
   sink->soc.frameHeight= -1;
   sink->swInit= swInit;
   sink->swTerm= swTerm;
   sink->swLink= swLink;
   sink->swUnLink= swUnLink;
   sink->swEvent= swEvent;
   sink->swDisplay= swDisplay;
   {
      const char *env= getenv("WESTEROS_SINK_USE_SW_DECODE");
      if ( env && (atoi(env) != 0) )
      {
         printf("westerossink: using software decode\n");
         sink->resUsage= 3;
         sink->swLink= 0;
      }
   }
   #endif

   gst_base_sink_set_async_enabled(GST_BASE_SINK(sink), TRUE);

   sink->processPadEvent= processEventSinkSoc;
   sink->acquireResources= sinkAcquireVideo;
   sink->releaseResources= sinkReleaseVideo;

   rc= NxClient_Join(NULL);
   if ( rc == NEXUS_SUCCESS )
   {
      sink->soc.secureGraphics= useSecureGraphics();
      #if (NEXUS_PLATFORM_VERSION_MAJOR>=16)
      sink->soc.secureVideo= isSVPEnabled();
      #endif
      sink->soc.stcChannel= NULL;
      sink->soc.videoPidChannel= NULL;
      sink->soc.codec= bvideo_codec_unknown;

      #if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
      sink->soc.eotf= NEXUS_VideoEotf_eInvalid;
      memset(&sink->soc.masteringDisplayColorVolume,0,sizeof(NEXUS_MasteringDisplayColorVolume));
      memset(&sink->soc.contentLightLevel,0,sizeof(NEXUS_ContentLightLevel));
      #endif

      NEXUS_VideoDecoderCapabilities videoDecoderCaps;
      NEXUS_GetVideoDecoderCapabilities(&videoDecoderCaps);
      if ( videoDecoderCaps.memory[0].maxFormat >= NEXUS_VideoFormat_e3840x2160p24hz )
      {
         sink->maxWidth= 3840;
         sink->maxHeight= 2160;
      }
      else
      {
         sink->maxWidth= 1920;
         sink->maxHeight= 1080;
      }

      result= TRUE;
   }
   else
   {
      GST_ERROR("gst_westeros_sink_soc_init: NxClient_Join failed %d", rc);
   }
   
   return result;
}

void gst_westeros_sink_soc_term( GstWesterosSink *sink )
{
   NxClient_Uninit(); 
}

void gst_westeros_sink_soc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(object);

   WESTEROS_UNUSED(pspec);

   switch (prop_id)
   {
      case PROP_VIDEO_PTS_OFFSET:
         sink->soc.ptsOffset= g_value_get_uint(value);
         if ( sink->soc.videoDecoder )
         {
            NEXUS_VideoDecoderSettings settings;
            NEXUS_SimpleVideoDecoder_GetSettings(sink->soc.videoDecoder, &settings);
            settings.ptsOffset= sink->soc.ptsOffset;
            NEXUS_SimpleVideoDecoder_SetSettings(sink->soc.videoDecoder, &settings);
         }
         break;
      case PROP_ENABLE_CC_PASSTHRU:
         sink->soc.enableCCPassthru= g_value_get_boolean(value);
         if ( sink->soc.videoDecoder )
         {
            NEXUS_SimpleVideoDecoderClientSettings settings;
            NEXUS_SimpleVideoDecoder_GetClientSettings(sink->soc.videoDecoder, &settings);
            settings.closedCaptionRouting= sink->soc.enableCCPassthru;
            NEXUS_SimpleVideoDecoder_SetClientSettings(sink->soc.videoDecoder, &settings);
         }
         break;
      case PROP_DISPLAY_RESOLUTION:
         {
            const gchar *s= g_value_get_string (value);
            if ( s )
            {
               int len= strlen(s);

               if ( (len == 4) && !strcasecmp(s, "480p"))
               {
                  sink->soc.outputFormat= NEXUS_VideoFormat_e480p;
               }
               else
               if ( (len == 4) && !strcasecmp(s, "720p"))
               {
                  sink->soc.outputFormat= NEXUS_VideoFormat_e720p;
               }
               else
               if ( (len == 5) && !strcasecmp(s, "1080p"))
               {
                  sink->soc.outputFormat= NEXUS_VideoFormat_e1080p;
               }

               NxClient_DisplaySettings dspSettings;
               NxClient_GetDisplaySettings( &dspSettings );
               if ( dspSettings.format != sink->soc.outputFormat )
               {
                  NEXUS_Error rc;

                  if ( sink->soc.outputFormat == NEXUS_VideoFormat_eUnknown )
                  {
                     sink->soc.outputFormat= dspSettings.format;
                  }

                  rc= NxClient_SetDisplaySettings( &dspSettings );
                  if ( rc )
                  {
                     GST_ERROR("NxClient_SetDisplaySettings: error: %d", rc);
                  }
               }
            }
         }
         break;
      case PROP_WINDOW_SHOW:
         {
            sink->show= g_value_get_boolean(value);

            if ( sink->soc.videoWindow )
            {
               NEXUS_SurfaceClientSettings settings;

               sink->visible= sink->show;
               NEXUS_SurfaceClient_GetSettings( sink->soc.videoWindow, &settings );
               if ( settings.composition.visible != sink->visible )
               {
                  settings.composition.visible= sink->visible;
                  NEXUS_SurfaceClient_SetSettings( sink->soc.videoWindow, &settings );
               }
            }
            if ( sink->soc.videoDecoder )
            {
               NEXUS_SimpleVideoDecoderClientSettings settings;

               NEXUS_SimpleVideoDecoder_GetClientSettings(sink->soc.videoDecoder, &settings);
               settings.closedCaptionRouting= sink->visible;
               NEXUS_SimpleVideoDecoder_SetClientSettings(sink->soc.videoDecoder, &settings);
            }
         }
         break;
      case PROP_ZOOM_MODE:
         {
            int intValue= g_value_get_int(value);

            sink->soc.zoomSet= TRUE;
            switch( intValue )
            {
               case 0:
                  sink->soc.zoomMode= NEXUS_VideoWindowContentMode_eFull;
                  break;
               case 3:
                  sink->soc.zoomMode= NEXUS_VideoWindowContentMode_eZoom;
                  break;
               default:
                  sink->soc.zoomMode= intValue;
                  break;
            }
            if ( sink->soc.videoWindow )
            {
               NEXUS_SurfaceClientSettings clientSettings;

               NEXUS_SurfaceClient_GetSettings(sink->soc.videoWindow, &clientSettings);
               if ( clientSettings.composition.contentMode != sink->soc.zoomMode )
               {
                  clientSettings.composition.contentMode= sink->soc.zoomMode;
                  NEXUS_SurfaceClient_SetSettings(sink->soc.videoWindow, &clientSettings);
               }
            }
         }
         break;
      case PROP_FORCE_ASPECT_RATIO:
         if ( !sink->soc.zoomSet )
         {
            sink->soc.forceAspectRatio= g_value_get_boolean(value);
            if ( sink->soc.forceAspectRatio )
            {
               sink->soc.zoomMode= NEXUS_VideoWindowContentMode_eBox;
            }
            else
            {
               sink->soc.zoomMode= NEXUS_VideoWindowContentMode_eFull;
            }
            if ( sink->soc.videoWindow )
            {
               NEXUS_SurfaceClientSettings clientSettings;

               NEXUS_SurfaceClient_GetSettings(sink->soc.videoWindow, &clientSettings);
               clientSettings.composition.contentMode= sink->soc.zoomMode;;
               NEXUS_SurfaceClient_SetSettings(sink->soc.videoWindow, &clientSettings);
            }
         }
         break;
      case PROP_SERVER_PLAY_SPEED:
         {
            gfloat serverPlaySpeed;

            serverPlaySpeed= g_value_get_float(value);
            if ( sink->videoStarted )
            {
               NEXUS_VideoDecoderSettings settings;
               NEXUS_SimpleVideoDecoder_GetSettings(sink->soc.videoDecoder, &settings);
               settings.channelChangeMode= NEXUS_VideoDecoder_ChannelChangeMode_eHoldUntilTsmLock;
               NEXUS_SimpleVideoDecoder_SetSettings(sink->soc.videoDecoder, &settings);

               NEXUS_SimpleVideoDecoder_Stop(sink->soc.videoDecoder);

               sink->videoStarted= FALSE;
               sink->soc.presentationStarted= FALSE;
               sink->soc.frameCount= 0;
               sink->soc.stoppedForPlaySpeedChange= TRUE;
               sink->soc.serverPlaySpeed= serverPlaySpeed;
               sink->soc.lastStartPts45k= 0;

               NEXUS_VideoDecoderTrickState trickState;
               NEXUS_SimpleVideoDecoder_GetTrickState(sink->soc.videoDecoder, &trickState);
               if ( (sink->soc.serverPlaySpeed >= -1.0) && (sink->soc.serverPlaySpeed <= 1.0) )
               {
                  trickState.topFieldOnly= false;
                  trickState.decodeMode= NEXUS_VideoDecoderDecodeMode_eAll;
               }
               else
               if ( (sink->soc.serverPlaySpeed > 1.0) && (sink->soc.serverPlaySpeed <= 4.0) )
               {
                  trickState.topFieldOnly= true;
                  trickState.decodeMode= NEXUS_VideoDecoderDecodeMode_eAll;
               }
               else
               {
                  trickState.topFieldOnly= true;
                  trickState.decodeMode= NEXUS_VideoDecoderDecodeMode_eI;
               }
               trickState.rate= NEXUS_NORMAL_DECODE_RATE;
               NEXUS_SimpleVideoDecoder_SetTrickState(sink->soc.videoDecoder, &trickState);
            }
         }
         break;
      case PROP_LOW_DELAY:
         sink->soc.useLowDelay= g_value_get_boolean(value);
         break;
      case PROP_LATENCY_TARGET:
         sink->soc.latencyTarget= g_value_get_int(value);
         break;
      case PROP_CAPTURE_SIZE:
         {
            const gchar *str= g_value_get_string(value);
            gchar **parts= g_strsplit(str, ",", 2);

            if ( !parts[0] || !parts[1] )
            {
               GST_ERROR( "Bad capture size properties string" );
            }
            else
            {
               LOCK( sink );
               sink->soc.captureWidthNext= atoi( parts[0] );
               sink->soc.captureHeightNext= atoi( parts[1] );
               UNLOCK( sink );

               printf("gst_westeros_sink_set_property set capture size (%d,%d)\n",
                       sink->soc.captureWidthNext, sink->soc.captureHeightNext );
            }

            g_strfreev(parts);
            break;
         }
      case PROP_HIDE_VIDEO_DURING_CAPTURE:
         {
            sink->soc.hideVideoDuringCapture= g_value_get_boolean(value);
            break;
         }
      case PROP_IMMEDIATE_OUTPUT:
      case PROP_CAMERA_LATENCY:
         {
            sink->soc.useImmediateOutput= g_value_get_boolean(value);
            break;
         }
      case PROP_FRAME_STEP_ON_PREROLL:
         {
            sink->soc.frameStepOnPreroll= g_value_get_boolean(value);
            break;
         }
      case PROP_ENABLE_TEXTURE:
         {
            sink->soc.enableTextureSignal= g_value_get_boolean(value);
            if ( !sink->display && sink->videoStarted )
            {
               if ( sink->soc.enableTextureSignal != sink->soc.captureEnabled )
               {
                  gst_westeros_sink_soc_set_video_path( sink, sink->soc.enableTextureSignal );
               }
            }
            break;
         }
      case PROP_REPORT_DECODE_ERRORS:
         {
            sink->soc.enableDecodeErrorSignal= g_value_get_boolean(value);
            break;
         }
      #if (NEXUS_NUM_VIDEO_WINDOWS > 1)
      case PROP_PIP:
         {
            sink->soc.usePip= g_value_get_boolean(value);
            printf("gst_westeros_sink_set_property set pip (%d)\n", sink->soc.usePip);
            break;
         }
      #endif
      #if (NEXUS_PLATFORM_VERSION_MAJOR>=16)
      case PROP_SECURE_VIDEO:
         {
            sink->soc.secureVideo= g_value_get_boolean(value);
            printf("gst_westeros_sink_set_property set secure-video (%d)\n", sink->soc.secureVideo);
            break;
         }
      #endif
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
      case PROP_BUFFERED_BYTES:
         {
            NEXUS_Error rc;
            NEXUS_VideoDecoderStatus videoStatus;

            videoStatus.fifoDepth= 0;
            if ( sink->soc.videoDecoder )
            {
               rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus);
               if ( NEXUS_SUCCESS != rc )
               {
                  GST_ERROR("Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
               }
            }
            g_value_set_uint(value, videoStatus.fifoDepth);
         }
         break;
      case PROP_VIDEO_DECODER:
         g_value_set_pointer(value, sink->soc.videoDecoder);
         break;
      case PROP_ENABLE_CC_PASSTHRU:
         g_value_set_boolean(value, sink->soc.enableCCPassthru);
         break;
      case PROP_WINDOW_SHOW:
         g_value_set_boolean(value, sink->show);
         break;
      case PROP_ZOOM_MODE:
         {
            int intValue;

            if ( sink->soc.zoomMode == NEXUS_VideoWindowContentMode_eFull )
            {
               intValue= 0;
            }
            else
            {
               intValue= 1;
            }
            g_value_set_int( value, intValue );
         }
         break;
      case PROP_FORCE_ASPECT_RATIO:
         g_value_set_boolean(value, sink->soc.forceAspectRatio);
         break;
      case PROP_SERVER_PLAY_SPEED:
         g_value_set_float(value, sink->soc.serverPlaySpeed);
         break;
      case PROP_LOW_DELAY:
         g_value_set_boolean(value, sink->soc.useLowDelay);
         break;
      case PROP_LATENCY_TARGET:
         g_value_set_int(value, sink->soc.latencyTarget);
         break;
      case PROP_HIDE_VIDEO_DURING_CAPTURE:
         g_value_set_boolean(value, sink->soc.hideVideoDuringCapture);
         break;
      case PROP_IMMEDIATE_OUTPUT:
      case PROP_CAMERA_LATENCY:
         g_value_set_boolean(value, sink->soc.useImmediateOutput);
         break;
      case PROP_FRAME_STEP_ON_PREROLL:
         g_value_set_boolean(value, sink->soc.frameStepOnPreroll);
         break;
      case PROP_ENABLE_TEXTURE:
         g_value_set_boolean(value, sink->soc.enableTextureSignal);
         break;
      case PROP_REPORT_DECODE_ERRORS:
         g_value_set_boolean(value, sink->soc.enableDecodeErrorSignal);
         break;
      case PROP_QUEUED_FRAMES:
         {
            NEXUS_Error rc;
            NEXUS_VideoDecoderStatus videoStatus;

            videoStatus.queueDepth= 0;
            if ( sink->soc.videoDecoder )
            {
               rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus);
               if ( NEXUS_SUCCESS != rc )
               {
                  GST_ERROR("Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
               }
            }
            g_value_set_uint(value, videoStatus.queueDepth);
         }
         break;
       #if (NEXUS_NUM_VIDEO_WINDOWS > 1)
      case PROP_PIP:
         g_value_set_boolean(value, sink->soc.usePip);
         break;
      #endif
      #if (NEXUS_PLATFORM_VERSION_MAJOR>=16)
      case PROP_SECURE_VIDEO:
         g_value_set_boolean(value, sink->soc.secureVideo);
         break;
      #endif
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
      sink->soc.sb= (struct wl_sb*)wl_registry_bind(registry, id, &wl_sb_interface, 1);
      printf("westeros-sink-soc: registry: sb %p\n", (void*)sink->soc.sb);
      wl_proxy_set_queue((struct wl_proxy*)sink->soc.sb, sink->queue);
		wl_sb_add_listener(sink->soc.sb, &sbListener, sink);
		printf("westeros-sink-soc: registry: done add sb listener\n");
      wl_display_roundtrip_queue(sink->display,sink->queue);
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
   NEXUS_Error rc;

   gboolean result= FALSE;
   
   WESTEROS_UNUSED(passToDefault);

   if ( sinkAcquireResources( sink ) )
   {
      result= TRUE;
   }
   else
   {
      GST_ERROR("gst_westeros_sink_null_to_ready: sinkAcquireResources failed");
   }
   
   return result;
}

gboolean gst_westeros_sink_soc_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);
   NEXUS_Error rc;
   
   if ( !queryPeerHandles(sink) )
   {
      sink->startAfterLink= TRUE;
   }
   else if ( sink->soc.codec == bvideo_codec_unknown )
   {
      sink->startAfterCaps= TRUE;
   }

   sink->soc.serverPlaySpeed= 1.0;
   sink->soc.clientPlaySpeed= 1.0;
   sink->soc.stoppedForPlaySpeedChange= FALSE;

   LOCK(sink);
   if ( !sink->startAfterLink && !sink->startAfterCaps )
   {
      if ( !gst_westeros_sink_soc_start_video( sink ) )
      {
         GST_ERROR("gst_westeros_sink_soc_ready_to_paused: gst_westeros_sink_soc_start_video failed");
      }
   }
   UNLOCK(sink);

   return TRUE;
}

gboolean gst_westeros_sink_soc_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);
   queryPeerHandles(sink);	

   LOCK(sink);
   sink->soc.videoPlaying = TRUE;
   UNLOCK(sink);

   if( (sink->soc.stcChannel != NULL) && (sink->soc.codec != bvideo_codec_unknown) )
   {
      #if (NEXUS_PLATFORM_VERSION_MAJOR>15)
      if ( sink->soc.codec == bvideo_codec_vp9 )
      {
         if ( sink->startAfterCaps )
         {
            // Wait till we get final caps since there might be HDR info
            GST_DEBUG("defer video start till caps");
            return TRUE;
         }
      }
      #endif

      LOCK( sink );
      if ( !sink->videoStarted )
      {
         if ( !gst_westeros_sink_soc_start_video( sink ) )
         {
            GST_ERROR("gst_westeros_sink_soc_paused_to_playing: gst_westeros_sink_soc_start_video failed");
         }

         if ( sink->soc.stoppedForPlaySpeedChange )
         {
            NEXUS_VideoDecoderSettings settings;
            NEXUS_SimpleVideoDecoder_GetSettings(sink->soc.videoDecoder, &settings);
            settings.channelChangeMode= NEXUS_VideoDecoder_ChannelChangeMode_eMuteUntilFirstPicture;
            NEXUS_SimpleVideoDecoder_SetSettings(sink->soc.videoDecoder, &settings);
            sink->soc.stoppedForPlaySpeedChange= FALSE;
         }
      }
      else
      {
         NEXUS_Error rc;
         NEXUS_SimpleVideoDecoderClientStatus clientStatus;
         rc= NEXUS_SimpleVideoDecoder_GetClientStatus(sink->soc.videoDecoder, &clientStatus);
         if ( rc == NEXUS_SUCCESS )
         {
            if ( !clientStatus.enabled && sink->soc.connectId )
            {
               GST_INFO_OBJECT(sink, "refreshing nxclient connection %d", sink->soc.connectId);
               NxClient_RefreshConnect(sink->soc.connectId);
            }
         }
         else
         {
            GST_ERROR("gst_westeros_sink_soc_paused_to_playing: NEXUS_SimpleVideoDecoder_GetClientStatus error: %d", (int)rc);
         }
         sink->videoStarted= TRUE;
         if ( sink->soc.stcChannel )
         {
            rc= NEXUS_SimpleStcChannel_Freeze(sink->soc.stcChannel, FALSE);
            GST_DEBUG("NEXUS_SimpleStcChannel_Freeze FALSE ");
            if ( rc != NEXUS_SUCCESS )
            {
               GST_ERROR("gst_westeros_sink_soc_paused_to_playing: NEXUS_SimpleStcChannel_Freeze FALSE failed: %d", (int)rc);
            }
         }
         if ( sink->soc.clientPlaySpeed != sink->playbackRate )
         {
            updateClientPlaySpeed(sink, sink->playbackRate, TRUE);
         }
      }

      if ( checkIndependentVideoClock( sink ) )
      {
         NEXUS_VideoDecoderTrickState trickState;
         NEXUS_SimpleVideoDecoder_GetTrickState(sink->soc.videoDecoder, &trickState);
         trickState.tsmEnabled= NEXUS_TsmMode_eDisabled;
         NEXUS_SimpleVideoDecoder_SetTrickState(sink->soc.videoDecoder, &trickState);
         GST_INFO_OBJECT(sink, "disable TsmMode");
      }
      UNLOCK( sink );
   }
   else
   {
      sink->startAfterLink= TRUE;
   }
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   LOCK(sink);
   sink->soc.videoPlaying = FALSE;
   UNLOCK(sink);

   if ( sink->soc.stcChannel )
   {
      NEXUS_Error rc;
      rc= NEXUS_SimpleStcChannel_Freeze(sink->soc.stcChannel, TRUE);
      if ( rc != NEXUS_SUCCESS )
      {
         GST_ERROR("gst_westeros_sink_soc_playing_to_paused: NEXUS_SimpleStcChannel_Freeze TRUE failed: %d", (int)rc);
      }
   }

   updateClientPlaySpeed(sink, 0, FALSE);

   if (gst_base_sink_is_async_enabled(GST_BASE_SINK(sink)))
   {
      // as the pipeline is operating without syncronizing by the clock, all the buffers might have been already consumed by the sink.
      // But to complete transition to paused state in async_enabled mode, we need a preroll buffer pushed to the pad;
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
   WESTEROS_UNUSED(passToDefault);

   sinkSocStopVideo( sink );
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault )
{
   *passToDefault= false;

   sinkSocStopVideo( sink );

   freeCaptureSurfaces(sink);

   sinkReleaseResources( sink );
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_accept_caps( GstWesterosSink *sink, GstCaps *caps )
{
   bool result= FALSE;
   GstStructure *structure;
   const gchar *mime;

   WESTEROS_UNUSED(sink);

   structure= gst_caps_get_structure(caps, 0);
   if(structure )
   {
      mime= gst_structure_get_name(structure);
      if (strcmp("video/x-brcm-avd", mime) == 0)
      {
         result= TRUE;
      }

      #if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
      {
         const char *colorimetry;
         const char *masteringDisplayMetadata;
         const char *contentLightLevel;
         if ( gst_structure_has_field(structure, "colorimetry") )
         {
            GstVideoColorimetry colorimetryInfo;
            GST_LOG("gst_westeros_sink_soc_accept_caps has colorimetry");
            colorimetry= gst_structure_get_string(structure,"colorimetry");
            gst_video_colorimetry_from_string(&colorimetryInfo,colorimetry);
            if ( colorimetryInfo.transfer == 13 )
            { // GST_VIDEO_TRANSFER_SMPTE_ST_2084
               sink->soc.eotf= NEXUS_VideoEotf_eHdr10;
            }
            else if ( colorimetryInfo.transfer == 14 )
            { // GST_VIDEO_TRANSFER_ARIB_STD_B67
               sink->soc.eotf= NEXUS_VideoEotf_eAribStdB67;
            }
            else
            {
               sink->soc.eotf= NEXUS_VideoEotf_eSdr;
            }
         }

         if ( gst_structure_has_field(structure, "mastering-display-metadata") )
         {
            GST_LOG("gst_westeros_sink_soc_accept_caps has mastering-display-metadata");
            masteringDisplayMetadata= gst_structure_get_string(structure,"mastering-display-metadata");
            if ( masteringDisplayMetadata )
            {
               parseMasteringDisplayColorVolume(masteringDisplayMetadata, &sink->soc.masteringDisplayColorVolume);
            }
         }

         if ( gst_structure_has_field(structure, "content-light-level") )
         {
            GST_LOG("gst_westeros_sink_soc_accept_caps has content-light-level");
            contentLightLevel= gst_structure_get_string(structure,"content-light-level");
            if ( contentLightLevel )
            {
               parseContentLightLevel(contentLightLevel, &sink->soc.contentLightLevel);
            }
         }
      }
      #endif

      LOCK(sink);
      if ( !sink->videoStarted && sink->startAfterCaps )
      {
         GST_DEBUG("have caps: starting video");
         sink->startAfterCaps= FALSE;
         if ( !gst_westeros_sink_soc_start_video( sink ) )
         {
            GST_ERROR("gst_westeros_sink_soc_accept_caps: gst_westeros_sink_soc_start_video failed");
         }
      }
      UNLOCK(sink);
   }

   return result;   
}

void gst_westeros_sink_soc_set_startPTS( GstWesterosSink *sink, gint64 pts )
{
   unsigned int pts45k= (unsigned int)( pts / 2 );
   NEXUS_VideoDecoderStatus videoStatus;
   if ( pts == 0 )
   {
      NEXUS_Error rc;
      rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus);
      if ( rc == NEXUS_SUCCESS )
      {
         if ( videoStatus.fifoDepth || videoStatus.queueDepth )
         {
            /* We have seen the following sequence: startSeg!=0, buffer0,1 startSeg==0
               In this case flushing will toss the first IFrame and cause video to jump forward to next iframe, which can be more than a second
               video is lost, then audio is delayed as it needs to dump lots of frames to catch up */
            GST_WARNING("Not flushing video decoder on startSeg==0, fifodepth: %d  frames: %d  pts: %ums  pts45k: %ums  lastStartPts45k %ums",
                         videoStatus.fifoDepth, videoStatus.queueDepth, videoStatus.pts/45, pts45k/45, sink->soc.lastStartPts45k/45 );
            /* The other concern is that there maybe a sequnce similar to above but the buffer0 PTS could be "far" from the previous startSeg!=0,
               which is still in force since we haven't flushed, and therefore playback won't start
               as the video decoder will be waiting for the buffer PTS to match/exceed the previous SetStartPts.
               Add debug in _render to catch/Warn this case and correct later if necessary */
         }
         else
         {
             GST_DEBUG("Flushing Decoder, will clear _SetStartPts");
             NEXUS_SimpleVideoDecoder_Flush( sink->soc.videoDecoder );
             sink->soc.lastStartPts45k= 0;
         }
      }
      else
      {
         GST_ERROR("NEXUS_SimpleVideoDecoder_GetStatus failed");
      }
   }
   else
   {
      GST_DEBUG("NEXUS_SimpleVideoDecoder_SetStartPts %ums", pts45k/45);
      NEXUS_SimpleVideoDecoder_SetStartPts( sink->soc.videoDecoder, pts45k );
      sink->soc.lastStartPts45k= pts45k;
      sink->soc.chkBufToStartPts= TRUE;
   }

   if ( sink->soc.clientPlaySpeed != sink->playbackRate )
   {
      updateClientPlaySpeed(sink, sink->playbackRate, GST_STATE(GST_ELEMENT(sink)) == GST_STATE_PLAYING );
   }
}

void gst_westeros_sink_soc_render( GstWesterosSink *sink, GstBuffer *buffer )
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(buffer);
   #ifdef ENABLE_SW_DECODE
   if ( swIsSWDecode( sink ) && (sink->soc.dataProbeId == 0) )
   {
      wstsw_render( sink, buffer );
   }
   #endif

   if ( sink->soc.chkBufToStartPts && sink->soc.lastStartPts45k && GST_BUFFER_PTS_IS_VALID(buffer) )
   {
      guint bufferPtsMs= GST_TIME_AS_MSECONDS(GST_BUFFER_PTS(buffer));

      if ( sink->soc.lastStartPts45k/45 > bufferPtsMs + MAX_STARTPTS_TO_BUFFER_PTS_MS )
      {
         /* In this case the user might be waiting a long while to see the video, warn for now, might consider calling SetStartPTS with the buffer PTS */
         GST_WARNING("Buffer PTS %u ms is far from lastStartPts %u ms", bufferPtsMs, sink->soc.lastStartPts45k/45);
      }
      sink->soc.chkBufToStartPts= FALSE;
   }
}

void gst_westeros_sink_soc_flush( GstWesterosSink *sink )
{
   NEXUS_SimpleVideoDecoder_Flush( sink->soc.videoDecoder );
   LOCK(sink);
   sink->soc.captureCount= 0;
   sink->soc.frameCount= 0;
   sink->soc.numDecoded= 0;
   sink->soc.numDropped= 0;
   sink->soc.numDroppedOutOfSegment= 0;
   sink->soc.ignoreDiscontinuity= TRUE;
   sink->soc.checkForEOS= FALSE;
   sink->soc.emitEOS= FALSE;
   sink->soc.emitUnderflow= FALSE;
   sink->soc.emitPTSError= FALSE;
   sink->soc.emitResourceChange= FALSE;
   sink->soc.emitDecodeError= FALSE;
   sink->soc.decodeError= FALSE;
   sink->soc.noFrameCount= 0;
   sink->soc.prevQueueDepth= 0;
   sink->soc.prevFifoDepth= 0;
   sink->soc.prevNumDecoded= 0;
   sink->soc.prevNumDecodeErrors= 0;
   sink->soc.presentationStarted= FALSE;
   sink->soc.lastStartPts45k= 0;
   UNLOCK(sink);


   // Drop frames pending for display
   if ( sink->soc.captureThread )
   {
       NEXUS_SimpleVideoDecoderCaptureStatus captureStatus;
       NEXUS_SurfaceHandle captureSurface= NULL;
       unsigned numReturned= 0;
       do {
           NEXUS_SimpleVideoDecoder_GetCapturedSurfaces(sink->soc.videoDecoder, &captureSurface, &captureStatus, 1, &numReturned);
           if ( numReturned > 0 ) {
               gint64 pts= ((gint64)captureStatus.pts)*2LL;
               GST_DEBUG_OBJECT(sink, "Dropping frame at pts: %lld %"GST_TIME_FORMAT, pts, GST_TIME_ARGS((pts * GST_MSECOND) / 90LL));
               NEXUS_SimpleVideoDecoder_RecycleCapturedSurfaces(sink->soc.videoDecoder, &captureSurface, 1);
           }
       } while ( numReturned > 0 );
   }

   if ( sink->soc.stcChannel )
   {
      NEXUS_Error rc;
      GST_DEBUG("soc-flush calling NEXUS_SimpleStcChannel_Invalidate");
      rc= NEXUS_SimpleStcChannel_Invalidate( sink->soc.stcChannel );
      if ( rc != NEXUS_SUCCESS )
      {
         GST_ERROR("NEXUS_SimpleStcChannel_Invalidate failed: rc %d",rc);
      }
   }
}

gboolean gst_westeros_sink_soc_start_video( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   NEXUS_Error rc;
   NEXUS_SimpleVideoDecoderStartSettings startSettings;
     
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: enter");
   
   if ( sink->videoStarted )
   {
       GST_DEBUG_OBJECT(sink, "Video decoder already started");
       return TRUE;
   }

   if ( !queryPeerHandles( sink ) )
   {
       GST_DEBUG_OBJECT(sink, "!queryPeerHandles");
       goto exit;
   }

   /* Start video decoder */
   NEXUS_SimpleVideoDecoder_GetDefaultStartSettings(&startSettings);
   startSettings.settings.codec= convertVideoCodecToNexus(sink->soc.codec);
   startSettings.settings.pidChannel= sink->soc.videoPidChannel;
   startSettings.settings.progressiveOverrideMode= NEXUS_VideoDecoderProgressiveOverrideMode_eDisable;
   startSettings.settings.timestampMode= NEXUS_VideoDecoderTimestampMode_eDisplay;                
   startSettings.settings.prerollRate= 1;
   startSettings.displayEnabled= true;
   startSettings.smoothResolutionChange= TRUE;
   startSettings.maxWidth= sink->maxWidth;
   startSettings.maxHeight= sink->maxHeight;
   if ( sink->soc.usePip )
   {
      startSettings.maxWidth= MAX_PIP_HEIGHT;
      startSettings.maxHeight= MAX_PIP_WIDTH;
   }

   rc= NEXUS_SimpleVideoDecoder_SetStcChannel(sink->soc.videoDecoder, sink->soc.stcChannel);
   if ( rc != NEXUS_SUCCESS )
   {
      GST_ERROR("gst_westeros_sink_soc_start_video: NEXUS_SimpleVideoDecoder_SetStcChannel failed: %d", (int)rc);
      postDecodeError( sink );
      goto exit;
   }

   #if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
   if ( sink->soc.eotf != NEXUS_VideoEotf_eInvalid )
   {
      startSettings.settings.eotf= sink->soc.eotf;
      memcpy(&startSettings.settings.masteringDisplayColorVolume, &sink->soc.masteringDisplayColorVolume, sizeof(NEXUS_MasteringDisplayColorVolume));
      memcpy(&startSettings.settings.contentLightLevel, &sink->soc.contentLightLevel, sizeof(NEXUS_ContentLightLevel));
   }
   #endif

   if ( sink->soc.haveHardware )
   {
      sink->soc.presentationStarted= FALSE;
      sink->soc.frameCount= 0;
      sink->eosEventSeen= FALSE;
   }
   rc= NEXUS_SimpleVideoDecoder_Start(sink->soc.videoDecoder, &startSettings);
   if ( rc != NEXUS_SUCCESS )
   {
      GST_ERROR("gst_westeros_sink_soc_start_video: NEXUS_SimpleVideoDecoder_Start failed: %d", (int)rc);
      postDecodeError( sink );
      goto exit;
   }

   if ( sink->soc.stcChannel )
   {
      rc= NEXUS_SimpleStcChannel_Freeze(sink->soc.stcChannel, GST_STATE(GST_ELEMENT(sink)) != GST_STATE_PLAYING ? TRUE : FALSE);
      if ( rc != NEXUS_SUCCESS )
      {
         goto exit;
      }
   }

   if ( sink->soc.haveHardware && (sink->startPTS != 0) )
   {
      gst_westeros_sink_soc_set_startPTS( sink, sink->startPTS );
   }

   sink->soc.quitCaptureThread= FALSE;
   if ( sink->soc.captureThread == NULL ) 
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: starting westeros_sink_capture thread");
      sink->soc.captureThread= g_thread_new("westeros_sink_capture", captureThread, sink);        
   }
 
   sink->videoStarted= TRUE;

   updateClientPlaySpeed(sink, sink->playbackRate, GST_STATE(GST_ELEMENT(sink)) == GST_STATE_PLAYING ? TRUE : FALSE);

   result= TRUE;

exit:

   return result;   
}

void gst_westeros_sink_soc_eos_event( GstWesterosSink *sink )
{
   gboolean sendEOS= FALSE;
   LOCK(sink);
   if ( sink->videoStarted )
   {
      NEXUS_VideoDecoderStatus videoStatus;
      if ( NEXUS_SUCCESS == NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus) )
      {
         if ( videoStatus.fifoDepth == 0 )
         {
            sendEOS= TRUE;
         }
      }
      else
      {
         sendEOS= TRUE;
      }
      if ( !sendEOS )
      {
         GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_eos_event: starting checking for eos");
         sink->soc.checkForEOS= TRUE;
         sink->soc.noFrameCount= 0;
      }
   }
   else
   {
      sendEOS= TRUE;
   }
   UNLOCK(sink);
   if ( sendEOS )
   {
      gst_westeros_sink_eos_detected( sink );
   }
}

static void sinkSocStopVideo( GstWesterosSink *sink )
{
   LOCK( sink );
   if ( sink->soc.captureThread )
   {
      sink->soc.quitCaptureThread= TRUE;
      if ( sink->display )
      {
         int fd= wl_display_get_fd( sink->display );
         if ( fd >= 0 )
         {
            shutdown( fd, SHUT_RDWR );
         }
      }
      UNLOCK( sink );
      g_thread_join( sink->soc.captureThread );
      LOCK( sink );
      sink->soc.captureThread= NULL;
   }

   if ( sink->soc.sb )
   {
      wl_sb_destroy( sink->soc.sb );
      sink->soc.sb= 0;
   }

   if ( sink->videoStarted && sink->soc.videoDecoder )
   {
      #if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
      NxClient_DisplaySettings displaySettings;
      NEXUS_VideoDecoderStreamInformation streamInfo;
      NEXUS_SimpleVideoDecoder_GetStreamInformation(sink->soc.videoDecoder, &streamInfo);
      #endif
      NEXUS_SimpleVideoDecoder_Stop(sink->soc.videoDecoder);
      #if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
      NxClient_GetDisplaySettings(&displaySettings);
      if (NEXUS_VideoEotf_eHdr10 == streamInfo.eotf || NEXUS_VideoEotf_eAribStdB67 == streamInfo.eotf)
      {
         NEXUS_VideoDynamicRangeMode tempMode= displaySettings.hdmiPreferences.dynamicRangeMode;
         displaySettings.hdmiPreferences.dynamicRangeMode= NEXUS_VideoDynamicRangeMode_eSdr;
         NxClient_SetDisplaySettings(&displaySettings);
         GST_WARNING("sinkSocStopVideo: reset to SDR from : %d",tempMode);
         displaySettings.hdmiPreferences.dynamicRangeMode= tempMode;
         NxClient_SetDisplaySettings(&displaySettings);
      }
      #endif
   }

   sink->videoStarted= FALSE;
   sink->soc.presentationStarted= FALSE;
   sink->soc.pixelAspectRatio= 1.0;
   sink->soc.havePixelAspectRatio= FALSE;
   sink->soc.serverPlaySpeed= 1.0;
   sink->soc.clientPlaySpeed= 1.0;
   sink->soc.stoppedForPlaySpeedChange= FALSE;
   sink->soc.lastStartPts45k= 0;
   sink->soc.numDecoded= 0;
   sink->soc.numDropped= 0;
   sink->soc.numDroppedOutOfSegment= 0;
   sink->soc.prevQueueDepth= 0;
   sink->soc.prevFifoDepth= 0;
   sink->soc.prevNumDecoded= 0;
   sink->soc.prevNumDecodeErrors= 0;
   sink->soc.checkForEOS= FALSE;
   sink->soc.emitEOS= FALSE;
   sink->soc.emitUnderflow= FALSE;
   sink->soc.emitPTSError= FALSE;
   sink->soc.emitResourceChange= FALSE;
   sink->soc.emitDecodeError= FALSE;
   sink->soc.decodeError= FALSE;
   UNLOCK( sink );
}

static gboolean allocCaptureSurfaces( GstWesterosSink *sink )
{
   gboolean result= TRUE;
   NEXUS_SurfaceCreateSettings videoSurfaceCreateSettings;

   if ( (sink->soc.captureWidth != sink->soc.captureWidthNext) || (sink->soc.captureHeight != sink->soc.captureHeightNext) )
   {
      int i;

      freeCaptureSurfaces(sink);
      
      sink->soc.captureWidth= sink->soc.captureWidthNext;
      sink->soc.captureHeight= sink->soc.captureHeightNext;

      /* Create a set of surfaces for decoded frame capture */
      NEXUS_Surface_GetDefaultCreateSettings(&videoSurfaceCreateSettings);
      videoSurfaceCreateSettings.width= sink->soc.captureWidth;
      videoSurfaceCreateSettings.height= sink->soc.captureHeight;
      if ( swIsSWDecode( sink ) )
      {
         videoSurfaceCreateSettings.pixelFormat= NEXUS_PixelFormat_eY08_Cr8_Y18_Cb8;
      }
      else
      {
         videoSurfaceCreateSettings.pixelFormat= NEXUS_PixelFormat_eA8_R8_G8_B8;
      }
      #if NEXUS_PLATFORM_VERSION_MAJOR >= 16
      if ( sink->soc.secureGraphics )
      {
         videoSurfaceCreateSettings.heap = NEXUS_Platform_GetFramebufferHeap(NEXUS_OFFSCREEN_SECURE_GRAPHICS_SURFACE);
      }
      #endif
      for( i= 0; i < NUM_CAPTURE_SURFACES; ++i )
      {
         sink->soc.captureSurface[i]= NEXUS_Surface_Create(&videoSurfaceCreateSettings);
         GST_LOG("video capture surface %d: %p (%dx%d)", i,
                  (void*)sink->soc.captureSurface[i], sink->soc.captureWidth, sink->soc.captureHeight);
         if ( sink->soc.captureSurface[i] == NULL )
         {
            GST_ERROR("Error unable to create video surface %d of %d (%dx%d)", 
                       i, NUM_CAPTURE_SURFACES, sink->soc.captureWidth, sink->soc.captureHeight );
            freeCaptureSurfaces(sink);
            sink->soc.captureWidth= -1;
            sink->soc.captureHeight= -1;
            result= FALSE;
            break;
         }
      }
   }
   return result;
}

static void freeCaptureSurfaces( GstWesterosSink *sink )
{
   int i;
   for( i= 0; i < NUM_CAPTURE_SURFACES; ++i )
   {
      if ( sink->soc.captureSurface[i] != NULL )
      {
         NEXUS_Surface_Destroy( sink->soc.captureSurface[i] );
         sink->soc.captureSurface[i]= NULL;
      }
   }
}

static gboolean queryPeerHandles(GstWesterosSink *sink) 
{    
   GstQuery *query;
   GstStructure *structure;
   const GstStructure *structure2;
   const GValue *val;
   gpointer *ptr;
   gboolean result;

   GST_DEBUG_OBJECT(sink, "queryPeerHandles: enter: peerPad %p", (void*)sink->peerPad);
   if ( !sink->peerPad ) 
   {
      return FALSE;
   }

   if ( sink->soc.stcChannel == NULL )
   {
      structure= gst_structure_new("get_stc_channel_handle", "stc_channel", G_TYPE_POINTER, 0, NULL);
      #ifdef USE_GST1
      query= gst_query_new_custom(GST_QUERY_CUSTOM, structure);
      #else
      query= gst_query_new_application(GST_QUERY_CUSTOM, structure);
      #endif
      result= gst_pad_query(sink->peerPad, query);
      if (!result) 
      {
         GST_DEBUG("queryPeerHandles: pad query for stc_channel failed");
         gst_query_unref(query);
         return FALSE;
      }    
      structure2= gst_query_get_structure(query);
      val= gst_structure_get_value(structure2, "stc_channel");
      if (val == NULL) 
      {
         GST_ERROR("queryPeerHandles: struc value for stc_channel failed");
         gst_query_unref(query);
         return FALSE;
      }    
      ptr= g_value_get_pointer(val);   
      sink->soc.stcChannel= (NEXUS_SimpleStcChannelHandle )ptr;
      gst_query_unref(query);
   }


   structure = gst_structure_new("get_video_codec", "video_codec", G_TYPE_INT, 0, NULL);
   #ifdef USE_GST1
   query= gst_query_new_custom(GST_QUERY_CUSTOM, structure);
   #else
   query= gst_query_new_application(GST_QUERY_CUSTOM, structure);
   #endif
   result= gst_pad_query(sink->peerPad, query);
   if (!result) 
   {
      GST_ERROR("queryPeerHandles: pad query for codec failed");
      gst_query_unref(query);
      return FALSE;
   }
   structure2= gst_query_get_structure(query);
   val= gst_structure_get_value(structure2, "video_codec");
   if (val == NULL) 
   {
      GST_ERROR("queryPeerHandles: struc value for codec failed");
      gst_query_unref(query);
      return FALSE;
   }    
   sink->soc.codec= g_value_get_int(val);    
   gst_query_unref(query);


   structure = gst_structure_new("get_video_pid_channel", "video_pid_channel", G_TYPE_POINTER, 0, NULL);
   #ifdef USE_GST1
   query= gst_query_new_custom(GST_QUERY_CUSTOM, structure);
   #else
   query= gst_query_new_application(GST_QUERY_CUSTOM, structure);
   #endif
   result= gst_pad_query(sink->peerPad, query);
   if (!result) 
   {
      GST_ERROR("queryPeerHandles: pad query for video_pid_channel failed");
      gst_query_unref(query);
      return FALSE;
   }
   structure2= gst_query_get_structure(query);
   val= gst_structure_get_value(structure2, "video_pid_channel");
   if (val == NULL) 
   {
      GST_ERROR("queryPeerHandles: struc value for video_pid_channel failed");
      gst_query_unref(query);
      return FALSE;
   }
   sink->soc.videoPidChannel= (NEXUS_PidChannelHandle)g_value_get_pointer(val);
   gst_query_unref(query);

   #if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
   {
      structure = gst_structure_new("get_hdr_metadata", "colorimetry", G_TYPE_STRING, 0, NULL);
      #ifdef USE_GST1
      query= gst_query_new_custom(GST_QUERY_CUSTOM, structure);
      #else
      query= gst_query_new_application(GST_QUERY_CUSTOM, structure);
      #endif
      result= gst_pad_query(sink->peerPad, query);
      if ( !result )
      {
         GST_LOG("queryPeerHandles: could not query hdr metadata from peer");
      }
      else
      {
         const char *colorimetry;
         const char *masteringDisplayMetadata;
         const char *contentLightLevel;

         structure= (GstStructure *)gst_query_get_structure(query);

         if ( gst_structure_has_field(structure, "colorimetry") )
         {
            GstVideoColorimetry colorimetryInfo;
            GST_LOG("queryPeerHandles: have colorimetry");
            colorimetry= gst_structure_get_string(structure,"colorimetry");
            gst_video_colorimetry_from_string(&colorimetryInfo,colorimetry);
            if ( colorimetryInfo.transfer == 13 )
            { // GST_VIDEO_TRANSFER_SMPTE_ST_2084
               sink->soc.eotf= NEXUS_VideoEotf_eHdr10;
            }
            else if ( colorimetryInfo.transfer == 14 )
            { // GST_VIDEO_TRANSFER_ARIB_STD_B67
               sink->soc.eotf= NEXUS_VideoEotf_eAribStdB67;
            }
            else
            {
               sink->soc.eotf= NEXUS_VideoEotf_eSdr;
            }
         }

         if ( gst_structure_has_field(structure, "mastering_display_metadata") )
         {
            GST_LOG("queryPeerHandles have mastering_display_metadata");
            masteringDisplayMetadata= gst_structure_get_string(structure,"mastering_display_metadata");
            if ( masteringDisplayMetadata )
            {
               parseMasteringDisplayColorVolume(masteringDisplayMetadata, &sink->soc.masteringDisplayColorVolume);
            }
         }

         if ( gst_structure_has_field(structure, "content_light_level") )
         {
            GST_LOG("queryPeerHandles: have content_light_level");
            contentLightLevel= gst_structure_get_string(structure,"content_light_level");
            if ( contentLightLevel )
            {
               parseContentLightLevel(contentLightLevel, &sink->soc.contentLightLevel);
            }
         }
      }
      gst_query_unref(query);
   }
   #endif

   allocCaptureSurfaces( sink );

   return TRUE;
}

static void postErrorMessage( GstWesterosSink *sink, int errorCode, const char *errorText )
{
   GError *error= g_error_new(GST_STREAM_ERROR, errorCode, errorText);
   if ( error )
   {
      GstElement *parent= GST_ELEMENT_PARENT(GST_ELEMENT(sink));
      GstMessage *msg= gst_message_new_error( GST_OBJECT(parent), error, errorText );
      if ( msg )
      {
         if ( !gst_element_post_message( GST_ELEMENT(sink), msg ) )
         {
            GST_ERROR( "Error: gst_element_post_message failed for errorCode %d errorText (%s)", errorCode, errorText);
         }
      }
      else
      {
         GST_ERROR( "Error: gst_message_new_error failed for errorCode %d errorText (%s)", errorCode, errorText);
      }
   }
   else
   {
      GST_ERROR("Error: g_error_new failed for errorCode %d errorText (%s)", errorCode, errorText );
   }
}

static gpointer captureThread(gpointer data) 
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   
   GST_DEBUG_OBJECT(sink, "captureThread: enter");
   sink->soc.startTime= getCurrentTimeMillis();

   if ( getenv("WESTEROS_SINK_USE_GFX") )
   {
      GST_INFO_OBJECT(sink, "WESTEROS_SINK_USE_GFX defined - enabling capture");
      gst_westeros_sink_soc_set_video_path( sink, true );
   }

   if ( !sink->display && sink->soc.enableTextureSignal && !sink->soc.captureEnabled )
   {
      gst_westeros_sink_soc_set_video_path( sink, true );
   }

   /*
    * Check for new video frames at a rate that
    * can support video at up to 60 fps
    */
   while( !sink->soc.quitCaptureThread )
   {
      LOCK( sink );
      if ( !sink->soc.haveHardware )
      {
         UNLOCK( sink );
         goto end_loop;
      }
      gboolean videoPlaying= sink->soc.videoPlaying;
      gboolean eosDetected= sink->eosDetected;
      if ( sink->windowChange )
      {
         sink->windowChange= false;
         gst_westeros_sink_soc_update_video_position( sink );
      }
      gboolean emitEOS= sink->soc.emitEOS;
      sink->soc.emitEOS= FALSE;
      gboolean emitUnderflow= sink->soc.emitUnderflow;
      sink->soc.emitUnderflow= FALSE;
      gboolean emitPTSError= sink->soc.emitPTSError;
      sink->soc.emitPTSError= FALSE;
      gboolean emitResourceChange= sink->soc.emitResourceChange;
      sink->soc.emitResourceChange= FALSE;
      gboolean haveResources= sink->soc.haveResources;
      gboolean emitDecodeError= sink->soc.emitDecodeError;
      sink->soc.emitDecodeError= FALSE;
      sink->soc.decodeError= TRUE;
      UNLOCK( sink );

      if ( emitEOS )
      {
         gst_westeros_sink_eos_detected( sink );
      }

      if ( emitUnderflow )
      {
         NEXUS_Error rc;
         NEXUS_VideoDecoderStatus videoStatus;

         rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus);
         if ( NEXUS_SUCCESS != rc )
         {
            GST_ERROR("Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
         }

         g_signal_emit (G_OBJECT(sink), g_signals[SIGNAL_UNDERFLOW], 0, videoStatus.fifoDepth, videoStatus.queueDepth);

         GstPad *pad= gst_pad_get_peer( sink->peerPad );
         if ( pad )
         {
            GstStructure *structure;
            structure= gst_structure_new("video_buffer_underflow",
                                         "buffer_underflow", G_TYPE_BOOLEAN, TRUE,
                                         "decoder_pts", G_TYPE_UINT, videoStatus.pts,
                                          NULL );
            if ( structure )
            {
               gst_pad_push_event( pad, gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, structure));
            }
            gst_object_unref( pad );
         }
      }

      if ( emitPTSError )
      {
         NEXUS_Error rc;
         NEXUS_VideoDecoderStatus videoStatus;

         g_signal_emit (G_OBJECT (sink), g_signals[SIGNAL_PTSERROR], 0, (unsigned int)(sink->currentPTS/2), NULL);

         rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus);
         if ( NEXUS_SUCCESS != rc )
         {
            GST_ERROR("Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
         }

         GstPad *pad= gst_pad_get_peer( sink->peerPad );
         if ( pad )
         {
            GstStructure *structure;
            structure= gst_structure_new("capture_stream_on_error",
                                         "error_string", G_TYPE_STRING, "VideoPtsError",
                                         "decoder_pts", G_TYPE_UINT, videoStatus.pts,
                                         NULL );
            if ( structure )
            {
               gst_pad_push_event( pad, gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, structure));
            }
            gst_object_unref( pad );
         }
      }

      if ( emitResourceChange )
      {
         GstPad *pad= gst_pad_get_peer( sink->peerPad );
         if ( pad )
         {
            GstStructure *structure;
            const char *fieldName= haveResources ? "resource_get" : "resource_lost";
            structure = gst_structure_new_empty(fieldName);
            if ( structure )
            {
               gst_pad_push_event( pad, gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, structure));
            }
            gst_object_unref( pad );
         }
      }

      if ( emitDecodeError )
      {
         NEXUS_Error rc;
         NEXUS_VideoDecoderStatus videoStatus;

         rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus);
         if ( NEXUS_SUCCESS != rc )
         {
            GST_ERROR("Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
         }

         if ( videoStatus.numDecodeErrors != sink->soc.prevNumDecodeErrors )
         {
            sink->soc.prevNumDecodeErrors= videoStatus.numDecodeErrors;
            if ( sink->soc.enableDecodeErrorSignal )
            {
               GST_INFO("fire decode-error-callback: errcnt %d", videoStatus.numDecodeErrors);
               g_signal_emit(G_OBJECT(sink), g_signals[SIGNAL_DECODEERROR], 0, (unsigned int)(sink->currentPTS/2), NULL);
            }
         }
         else
         {
            postDecodeError( sink );
         }
      }

      if ( sink->soc.captureEnabled )
      {
         if ( videoPlaying && (sink->visible || sink->soc.enableTextureSignal) && !eosDetected )
         {
            processFrame( sink );
         }
      }

      updateVideoStatus( sink );

   end_loop:

      if ( sink->display && wl_display_dispatch_queue_pending(sink->display, sink->queue) == 0 )
      {
         wl_display_flush(sink->display);
         if ( !eosDetected )
         {
            wl_display_roundtrip_queue(sink->display,sink->queue);
         }
      }
      
      usleep( FRAME_POLL_TIME );
   }

   if ( sink->soc.captureEnabled )
   {
      NEXUS_SimpleVideoDecoder_StopCapture(sink->soc.videoDecoder);
      sink->soc.captureEnabled= FALSE;
   }
   
   GST_DEBUG_OBJECT(sink, "captureThread: exit");
   
   LOCK( sink );
   sink->soc.captureThread= NULL;
   UNLOCK( sink );
   
   g_thread_exit(NULL);
   
   return NULL;
}

void gst_westeros_sink_soc_set_video_path( GstWesterosSink *sink, bool useGfxPath )
{
   if ( swIsSWDecode( sink ) )
   {
      return;
   }

   if ( useGfxPath && !sink->soc.captureEnabled )
   {
      NEXUS_Error rc;
      NEXUS_SimpleVideoDecoderStartCaptureSettings captureSettings;
      
      /* Start video frame capture */
      NEXUS_SimpleVideoDecoder_GetDefaultStartCaptureSettings(&captureSettings);
      captureSettings.displayEnabled= true;
      #if NEXUS_PLATFORM_VERSION_MAJOR >= 16
      if ( sink->soc.secureGraphics )
      {
         captureSettings.secure= true;
      }
      #endif

      memcpy(&captureSettings.surface, &sink->soc.captureSurface, sizeof(sink->soc.captureSurface));
      rc= NEXUS_SimpleVideoDecoder_StartCapture(sink->soc.videoDecoder, &captureSettings);
      if (rc != 0)
      {
          GST_ERROR("Error NEXUS_SimpleVideoDecoder_StartCapture: %d", (int)rc);
      }
      sink->soc.captureEnabled= TRUE;

      if ( sink->soc.hideVideoDuringCapture )
      {
         sink->soc.framesBeforeHideVideo= 2;
      }
   }
   else if ( !useGfxPath && sink->soc.captureEnabled )
   {
      NEXUS_SurfaceComposition vComposition;

      /* Move HW path video back on-screen */
      NxClient_GetSurfaceClientComposition(sink->soc.surfaceClientId, &vComposition);
      vComposition.position.x= sink->soc.videoX;
      vComposition.position.y= sink->soc.videoY;
      vComposition.position.width= sink->soc.videoWidth;
      vComposition.position.height= sink->soc.videoHeight;
      NxClient_SetSurfaceClientComposition(sink->soc.surfaceClientId, &vComposition);

      /* Stop video frame capture */
      NEXUS_SimpleVideoDecoder_StopCapture(sink->soc.videoDecoder);
      sink->soc.captureEnabled= FALSE;

      if ( sink->display && sink->surface )
      {
         wl_surface_attach( sink->surface, 0, sink->windowX, sink->windowY );
         wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
         wl_surface_commit( sink->surface );
         wl_display_flush(sink->display);
         wl_display_dispatch_queue_pending(sink->display, sink->queue);
      }
   }
}

typedef struct bufferInfo
{
   GstWesterosSink *sink;
   void *deviceBuffer;
} bufferInfo;

static void buffer_release( void *data, struct wl_buffer *buffer )
{
   NEXUS_SurfaceHandle captureSurface;
   bufferInfo *binfo= (bufferInfo*)data;
   
   GstWesterosSink *sink= binfo->sink;
   captureSurface= (NEXUS_SurfaceHandle)binfo->deviceBuffer;
   if ( sink->soc.videoDecoder )
   {
      NEXUS_SimpleVideoDecoder_RecycleCapturedSurfaces(sink->soc.videoDecoder, &captureSurface, 1);
   }
   
   --sink->soc.activeBuffers;
   wl_buffer_destroy( buffer );
   
   free( binfo );
}

static struct wl_buffer_listener wl_buffer_listener= 
{
   buffer_release
};

static void processTextureSignal( GstWesterosSink *sink, NEXUS_SurfaceHandle surface )
{
   NEXUS_Error rc;
   void *mem= 0;

   rc= NEXUS_Surface_Lock( surface, &mem );
   if ( rc == NEXUS_SUCCESS )
   {
      int fd0, l0, s0, fd1, l1, s1, fd2, l2, s2;
      void *p0, *p1, *p2;
      fd0= fd1= fd2= -1;
      p1= p2= 0;
      l1= l2= s1= s2= 0;
      p0= mem;
      s0= sink->soc.captureWidth*4;
      l0= s0*sink->soc.captureHeight;

      g_signal_emit( G_OBJECT(sink),
                     g_signals[SIGNAL_NEWTEXTURE],
                     0,
                     DRM_FORMAT_RGBA8888,
                     sink->soc.captureWidth,
                     sink->soc.captureHeight,
                     fd0, l0, s0, p0,
                     fd1, l1, s1, p1,
                     fd2, l2, s2, p2
                   );

      NEXUS_Surface_Unlock( surface );
   }
   else
   {
      GST_ERROR("Error NEXUS_Surface_Lock: %d", (int)rc);
   }
}

static void processTextureWayland( GstWesterosSink *sink, NEXUS_SurfaceHandle surface )
{
   struct wl_buffer *buff;

   bufferInfo *binfo= (bufferInfo*)malloc( sizeof(bufferInfo) );
   if ( binfo && sink->surface )
   {
      binfo->sink= sink;
      binfo->deviceBuffer= surface;

      buff= wl_sb_create_buffer( sink->soc.sb,
                                 (uint32_t)surface,
                                 sink->windowWidth,
                                 sink->windowHeight,
                                 sink->windowWidth*4,
                                 WL_SB_FORMAT_ARGB8888 );
      wl_buffer_add_listener( buff, &wl_buffer_listener, binfo );
      wl_surface_attach( sink->surface, buff, sink->windowX, sink->windowY );
      wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
      wl_surface_commit( sink->surface );
      ++sink->soc.activeBuffers;
   }
}

static void processFrame( GstWesterosSink *sink )
{
   NEXUS_SimpleVideoDecoderCaptureStatus captureStatus;
   gboolean haveResources= FALSE;
   NEXUS_SurfaceHandle captureSurface= NULL;
   unsigned numReturned= 0;

   GST_LOG("processFrame: enter");

   if ( (sink->soc.captureWidth == -1) || (sink->soc.captureHeight == -1) )
   {
      allocCaptureSurfaces( sink );
   }

   LOCK( sink );
   haveResources= sink->soc.haveResources;
   UNLOCK( sink );

   if ( sink->soc.haveResources )
   {
      sink->soc.captureCount++;
      captureSurface= 0;
      NEXUS_SimpleVideoDecoder_GetCapturedSurfaces(sink->soc.videoDecoder, &captureSurface, &captureStatus, 1, &numReturned);
      if ( numReturned > 0 )
      {
         long long now= getCurrentTimeMillis();
         long long elapsed= now-sink->soc.startTime;
         GST_LOG("%lld.%03lld: cap surf %p: frame %d pts %u (%d) serial %u iter %d",
                 elapsed/1000LL, elapsed%1000LL, (void*)captureSurface, sink->soc.frameCount,
                 captureStatus.pts, captureStatus.ptsValid, captureStatus.serialNumber, sink->soc.captureCount );

         if ( sink->soc.enableTextureSignal )
         {
            processTextureSignal( sink, captureSurface );
         }
         else
         if ( sink->soc.sb )
         {
            processTextureWayland( sink, captureSurface );
            captureSurface= 0;
         }
         if ( sink->soc.framesBeforeHideVideo )
         {
            if ( --sink->soc.framesBeforeHideVideo == 0 )
            {
               /* Move HW path video off screen.  The natural inclination would be to suppress
                * its presentation by setting captureSettings.displayEnable to false, but doing
                * so seems to cause HW path video to never present again when capture is disabled.
                * Similarly, hiding the HW path video by setting its opacity to 0 seems to not work.
                */
               NEXUS_SurfaceComposition vComposition;
               NxClient_GetSurfaceClientComposition(sink->soc.surfaceClientId, &vComposition);
               vComposition.position.y= -vComposition.position.height;
               NxClient_SetSurfaceClientComposition(sink->soc.surfaceClientId, &vComposition);
            }
         }
         if ( captureSurface )
         {
            NEXUS_SimpleVideoDecoder_RecycleCapturedSurfaces(sink->soc.videoDecoder, &captureSurface, 1);
         }
      }
   }

exit:

   GST_LOG("processFrame: exit");
}

static void updateVideoStatus( GstWesterosSink *sink )
{
   NEXUS_VideoDecoderStatus videoStatus;
   gboolean haveHardware= FALSE;
   gboolean checkForEOS= FALSE;
   gboolean eosDetected= FALSE;
   gboolean videoPlaying= FALSE;
   gboolean flushStarted= FALSE;
   gboolean emitFirstFrame= FALSE;
   guint64 prevPTS;

   LOCK( sink );
   haveHardware= sink->soc.haveHardware;
   checkForEOS= sink->soc.checkForEOS;
   eosDetected= sink->eosDetected;
   videoPlaying= sink->soc.videoPlaying;
   flushStarted= sink->flushStarted;
   UNLOCK( sink );

   if ( haveHardware == FALSE )
   {
      return;
   }

   if ( NEXUS_SUCCESS == NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus) )
   {
      if ( videoPlaying && !flushStarted )
      {
         LOCK( sink );
         if ( sink->flushStarted )
         {
             // no-op
         }
         else
         if ( !sink->soc.presentationStarted && videoStatus.numDisplayed == 0 )
         {
             if ( !sink->soc.decodeError )
             {
                int limit= sink->soc.noFrameCount*FRAME_POLL_TIME;
                if ( videoStatus.started && (videoStatus.numDecoded > sink->soc.prevNumDecoded) )
                {
                   ++sink->soc.noFrameCount;
                   limit += FRAME_POLL_TIME;
                   if ( limit >= DECODE_VERIFY_DELAY )
                   {
                      sink->soc.emitDecodeError= TRUE;
                   }
                }
                if ( (videoStatus.numDecoded > DECODE_VERIFY_MAX_BYTES) &&
                     (videoStatus.numPicturesReceived == 0) &&
                     (limit > 2*DECODE_VERIFY_DELAY/3) )
                {
                   sink->soc.emitDecodeError= TRUE;
                }
                sink->soc.prevNumDecoded= videoStatus.numDecoded;
             }
         }
         else
         if ( !sink->soc.haveResources )
         {
            long long now= getCurrentTimeMillis();
            gint64 elapsed= (gint64)(now-sink->soc.timeResourcesLost);
            sink->position= sink->soc.positionResourcesLost+elapsed*1000000ULL;
         }
         else
         if ( (videoStatus.firstPtsPassed || videoStatus.numDecoded > sink->soc.numDecoded) && (sink->currentPTS/2 != videoStatus.pts) )
         {
            sink->soc.ignoreDiscontinuity= FALSE;
            sink->soc.numDecoded= videoStatus.numDecoded;
            prevPTS= sink->currentPTS;
            sink->currentPTS= ((gint64)videoStatus.pts)*2LL;
            if (sink->prevPositionSegmentStart != sink->positionSegmentStart)
            {
               if ( sink->currentPTS == 0 )
               {
                  gint64 newPts=  90LL * sink->positionSegmentStart / GST_MSECOND + /*ceil*/ 1;
                  sink->firstPTS= newPts;
               }
               else
               {
                  sink->firstPTS= sink->currentPTS;
               }
               sink->prevPositionSegmentStart = sink->positionSegmentStart;
               GST_DEBUG("SegmentStart changed! Updating first PTS to %lld ", sink->firstPTS);
            }
            if ( sink->currentPTS != 0 || sink->soc.frameCount == 0 )
            {
               if ( (sink->currentPTS < sink->firstPTS) && (sink->currentPTS > 90000) )
               {
                  // If we have hit a discontinuity that doesn't look like rollover, then
                  // treat this as the case of looping a short clip.  Adjust our firstPTS
                  // to keep our running time correct.
                  sink->firstPTS= sink->firstPTS-(prevPTS-sink->currentPTS);
               }
               sink->position= sink->positionSegmentStart + ((sink->currentPTS - sink->firstPTS) * GST_MSECOND) / 90LL;
               if ( sink->timeCodePresent && sink->enableTimeCodeSignal )
               {
                  sink->timeCodePresent( sink, sink->position, g_signals[SIGNAL_TIMECODE] );
               }
            }

            if (sink->soc.frameCount == 0)
            {
               emitFirstFrame= TRUE;
            }

            if ( sink->soc.enableDecodeErrorSignal && (videoStatus.numDecodeErrors != sink->soc.prevNumDecodeErrors) )
            {
               sink->soc.emitDecodeError= TRUE;
            }

            sink->soc.frameCount++;
            sink->soc.noFrameCount= 0;
            sink->soc.presentationStarted= TRUE;

            if ( ((videoStatus.numDisplayDrops-sink->soc.numDroppedOutOfSegment) > sink->soc.numDropped) ||
                 (((videoStatus.numDecoded % QOS_INTERVAL) == 0) && videoStatus.numDecoded) )
            {
               sink->soc.numDropped= videoStatus.numDisplayDrops-sink->soc.numDroppedOutOfSegment;

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
                                             videoStatus.numDecoded,
                                             sink->soc.numDropped );
                  GST_INFO("post QoS: processed %u dropped %u", videoStatus.numDecoded, videoStatus.numDisplayDrops);
                  if ( !gst_element_post_message( GST_ELEMENT(sink), msg ) )
                  {
                     GST_WARNING("unable to post QoS");
                     gst_message_unref( msg );
                  }
               }
            }
         }

         if ( (sink->soc.prevQueueDepth == videoStatus.queueDepth) &&
              (sink->soc.prevFifoDepth == videoStatus.fifoDepth) &&
              (sink->soc.prevNumDecoded == videoStatus.numDecoded) )
         {
            ++sink->soc.noFrameCount;
         }
         else
         {
            sink->soc.noFrameCount= 0;
         }

         sink->soc.prevQueueDepth= videoStatus.queueDepth;
         sink->soc.prevFifoDepth= videoStatus.fifoDepth;
         sink->soc.prevNumDecoded= videoStatus.numDecoded;

         sink->srcWidth= videoStatus.source.width;
         sink->srcHeight= videoStatus.source.height;
         UNLOCK( sink );
      }
      else if ( !flushStarted )
      {
         LOCK( sink );
         if ( (videoStatus.firstPtsPassed || videoStatus.numDecoded > sink->soc.numDecoded) && (sink->currentPTS/2 != videoStatus.pts) )
         {
            if (sink->soc.frameCount == 0)
            {
               emitFirstFrame= TRUE;
            }
            sink->soc.frameCount++;
         }
         UNLOCK( sink );
      }
   }

   if ( emitFirstFrame )
   {
      g_signal_emit (G_OBJECT (sink), g_signals[SIGNAL_FIRSTFRAME], 0, 2, NULL);
   }

   if ( videoPlaying && !eosDetected && checkForEOS )
   {
      int limit= EOS_DETECT_DELAY;
      if ( sink->soc.noFrameCount*FRAME_POLL_TIME > limit )
      {
         GST_INFO_OBJECT(sink, "updateVideoStatus: eos detected: firstPTS %lld currentPTS %lld", sink->firstPTS, sink->currentPTS);
         sink->eosEventSeen= TRUE;
         gst_westeros_sink_eos_detected( sink );
         sink->soc.noFrameCount= 0;
      }
   }
}

void gst_westeros_sink_soc_update_video_position( GstWesterosSink *sink )
{
   NEXUS_SurfaceClientSettings vClientSettings;
   NEXUS_SurfaceComposition vComposition;
   NxClient_DisplaySettings nxDspSettings;

   if ( swIsSWDecode( sink ) )
   {
      return;
   }

   NxClient_GetDisplaySettings( &nxDspSettings );
   if ( nxDspSettings.format != sink->soc.outputFormat )
   {
      sink->soc.outputFormat= nxDspSettings.format;
   }

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

   if ( sink->soc.videoWindow )
   {
      NEXUS_SurfaceClient_GetSettings( sink->soc.videoWindow, &vClientSettings );
      if ( vClientSettings.composition.visible != sink->visible )
      {
         vClientSettings.composition.visible= sink->visible;
         NEXUS_SurfaceClient_SetSettings( sink->soc.videoWindow, &vClientSettings );
      }
   }

   if ( !sink->soc.captureEnabled )
   {
      NxClient_GetSurfaceClientComposition(sink->soc.surfaceClientId, &vComposition);
      switch ( sink->soc.outputFormat )
      {
         case NEXUS_VideoFormat_e480p:
         case NEXUS_VideoFormat_eNtsc:
            vComposition.virtualDisplay.width= 640;
            vComposition.virtualDisplay.height= 480;
            break;
         default:
            {
               NEXUS_SurfaceClientStatus scStatus;
               bool using1080Graphics= false;
               int rc= NEXUS_SurfaceClient_GetStatus( sink->soc.surfaceClient, &scStatus );
               if ( rc == NEXUS_SUCCESS )
               {
                  if ( (scStatus.display.framebuffer.width == 1920) &&
                       (scStatus.display.framebuffer.height == 1080) )
                  {
                     using1080Graphics= true;
                  }
               }
               if ( using1080Graphics )
               {
                  vComposition.virtualDisplay.width= 1920;
                  vComposition.virtualDisplay.height= 1080;
               }
               else
               {
                  vComposition.virtualDisplay.width= 1280;
                  vComposition.virtualDisplay.height= 720;
               }
            }
            break;
      }

      vComposition.position.x= sink->soc.videoX;
      vComposition.position.y= sink->soc.videoY;
      vComposition.position.width= sink->soc.videoWidth;
      vComposition.position.height= sink->soc.videoHeight;
      if ( sink->soc.usePip )
      {
         // Restrict PIP window size
         if ( sink->soc.videoWidth > MAX_PIP_WIDTH )
         {
            vComposition.position.width= MAX_PIP_WIDTH;
         }
         if ( sink->soc.videoHeight > MAX_PIP_HEIGHT )
         {
            vComposition.position.height= MAX_PIP_HEIGHT;
         }
      }
      NxClient_SetSurfaceClientComposition(sink->soc.surfaceClientId, &vComposition );

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
   gboolean rv = FALSE;
   GValue val = {0, };

   switch (GST_QUERY_TYPE(query))
   {
      case GST_QUERY_CUSTOM:
         {
            GstStructure *query_structure = (GstStructure*) gst_query_get_structure(query);
            const gchar *struct_name = gst_structure_get_name(query_structure);
            if (!strcasecmp(struct_name, "get_video_handle"))
            {
               g_value_init(&val, G_TYPE_POINTER);
               g_value_set_pointer(&val, (gpointer)sink->soc.videoDecoder);

               gst_structure_set_value(query_structure, "video_handle", &val);

               rv = TRUE;
            }
            else if (!strcasecmp(struct_name, "get_current_pts"))
            {
               g_value_init(&val, G_TYPE_POINTER);
               g_value_set_pointer(&val, (gpointer)(guint)(sink->currentPTS/2));

               gst_structure_set_value(query_structure, "current_pts", &val);

               rv = TRUE;
            }
            else if (!strcasecmp(struct_name, "get_first_pts"))
            {
               g_value_init(&val, G_TYPE_POINTER);
               g_value_set_pointer(&val, (gpointer)(guint)(sink->firstPTS/2));

               gst_structure_set_value(query_structure, "first_pts", &val);

               rv = TRUE;
            }
            else if (!strcasecmp(struct_name, "get_video_playback_quality"))
            {
               NEXUS_Error rc;
               NEXUS_VideoDecoderStatus videoStatus;
               if ( sink->soc.videoDecoder )
               {
                  rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus );
                  if ( NEXUS_SUCCESS != rc )
                  {
                     GST_WARNING_OBJECT(sink, "Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
                  }
                  else
                  {
                     g_value_init(&val, G_TYPE_UINT);

                     g_value_set_uint(&val, videoStatus.numDecoded);
                     gst_structure_set_value(query_structure, "total", &val);

                     g_value_set_uint(&val, videoStatus.numDecodeDrops + videoStatus.numDisplayDrops - sink->soc.numDroppedOutOfSegment);
                     gst_structure_set_value(query_structure, "dropped", &val);

                     g_value_set_uint(&val, videoStatus.numDecodeErrors + videoStatus.numDisplayErrors);
                     gst_structure_set_value(query_structure, "corrupted", &val);
                  }
               }
               rv = TRUE;
            }
            else if (!strcasecmp(struct_name, "get_decoder_status"))
            {
               guint pts= 0, fifoDepth= 0;
               NEXUS_Error rc;
               NEXUS_VideoDecoderStatus videoStatus;
               if ( sink->soc.videoDecoder )
               {
                   rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus );
                   if ( NEXUS_SUCCESS == rc )
                   {
                      pts= videoStatus.pts;
                      fifoDepth= videoStatus.fifoDepth;
                   }
                   else
                   {
                      GST_WARNING_OBJECT(sink, "Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
                   }
                }

                GST_LOG("Video decoder status queried: pts %x fifoDepth %u", pts, fifoDepth);

                g_value_init(&val, G_TYPE_UINT);

                g_value_set_uint(&val, pts);
                gst_structure_set_value(query_structure, "pts", &val);

                g_value_set_uint(&val, fifoDepth);
                gst_structure_set_value(query_structure, "fifoDepth", &val);
                rv= TRUE;
            }
         }
         break;

      default:
         break;
   }

   return rv;
}

static void firstPtsPassedCallback( void *userData, int n )
{
   GstWesterosSink *sink= (GstWesterosSink*)userData;
   WESTEROS_UNUSED(n);
   NEXUS_Error rc;
   NEXUS_VideoDecoderStatus videoStatus;

   if ( sink->soc.videoDecoder )
   {
      rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus );
      if ( NEXUS_SUCCESS != rc )
      {
         GST_WARNING_OBJECT(sink, "Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
      }
      else
      {
         sink->soc.numDroppedOutOfSegment= videoStatus.numDisplayDrops;
      }
   }

   if ( sink->videoStarted )
   {
      sink->soc.presentationStarted= TRUE;
   }
}

static void underflowCallback( void *userData, int n )
{
   GstWesterosSink *sink= (GstWesterosSink*)userData;
   NEXUS_Error rc;
   NEXUS_VideoDecoderStatus videoStatus;
   WESTEROS_UNUSED(n);

   if ( sink->videoStarted )
   {
      rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus);
      if ( NEXUS_SUCCESS == rc )
      {
         GST_INFO("underflow: EOS: %d queueDepth %d", sink->eosEventSeen, videoStatus.queueDepth);
         LOCK(sink);
         if ( sink->eosEventSeen )
         {
            if ( videoStatus.queueDepth > 0 )
            {
               sink->soc.checkForEOS= TRUE;
               sink->eosEventSeen= FALSE;
               sink->soc.noFrameCount= 0;
            }
            else
            {
               GST_INFO("underflow: emitting EOS");
               sink->soc.emitEOS= TRUE;
               sink->soc.checkForEOS= FALSE;
            }
         }
         else
         {
            GST_INFO("underflow: presentationStarted %d ignoreDisc %d numBytesDecoded %llu PTS 0x%x eosSeen %d",
                      sink->soc.presentationStarted, sink->soc.ignoreDiscontinuity, videoStatus.numBytesDecoded,
                      videoStatus.pts, sink->eosEventSeen );
            if ( sink->soc.presentationStarted && !sink->soc.ignoreDiscontinuity && videoStatus.numBytesDecoded )
            {
               sink->soc.emitUnderflow= TRUE;
            }
         }
         UNLOCK(sink);
      }
      else
      {
         GST_ERROR("Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
      }
   }
}

static void ptsErrorCallback( void *userData, int n )
{
   GstWesterosSink *sink= (GstWesterosSink*)userData;
   WESTEROS_UNUSED(n);
   NEXUS_Error rc;
   NEXUS_VideoDecoderStatus videoStatus;

   videoStatus.pts= 0;
   rc= NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus );
   if ( rc != NEXUS_SUCCESS )
   {
      GST_ERROR("Error NEXUS_SimpleVideoDecoder_GetStatus: %d", (int)rc);
   }
   GST_INFO("pts error: current pts 0x%lx decoder pts 0x%x ignoreDisc %d frameCount %d",
            sink->currentPTS/2, videoStatus.pts, sink->soc.ignoreDiscontinuity, sink->soc.frameCount);
   LOCK(sink);
   /* Ignore PTS error if we have just flushed */
   if ( !sink->soc.ignoreDiscontinuity && sink->soc.frameCount > 1 )
   {
      sink->soc.emitPTSError= TRUE;
   }
   UNLOCK(sink);
}

static NEXUS_VideoCodec convertVideoCodecToNexus(bvideo_codec codec) 
{
   NEXUS_VideoCodec nexusVideoCodec;
   
   switch (codec) 
   {        
      case bvideo_codec_mpeg1:
         nexusVideoCodec= NEXUS_VideoCodec_eMpeg1;
         break;
        
      case bvideo_codec_mpeg2:
         nexusVideoCodec= NEXUS_VideoCodec_eMpeg2;
         break;

      case bvideo_codec_mpeg4_part2:
         nexusVideoCodec= NEXUS_VideoCodec_eMpeg4Part2;
         break;

      case bvideo_codec_h263:
         nexusVideoCodec= NEXUS_VideoCodec_eH263;
         break;

      case bvideo_codec_h264:
         nexusVideoCodec= NEXUS_VideoCodec_eH264;
         break;

      case bvideo_codec_h264_svc:
         nexusVideoCodec= NEXUS_VideoCodec_eH264_Svc;
         break;

      case bvideo_codec_h264_mvc:
         nexusVideoCodec= NEXUS_VideoCodec_eH264_Mvc;
         break;

      case bvideo_codec_h265:
        nexusVideoCodec= NEXUS_VideoCodec_eH265;
        break;

      case bvideo_codec_vc1:
         nexusVideoCodec= NEXUS_VideoCodec_eVc1;
         break;

      case bvideo_codec_vc1_sm:
         nexusVideoCodec= NEXUS_VideoCodec_eVc1SimpleMain;
         break;

      case bvideo_codec_vp8:
         nexusVideoCodec= NEXUS_VideoCodec_eVp8;
         break;

#if (NEXUS_PLATFORM_VERSION_MAJOR>15)
      case bvideo_codec_vp9:
        nexusVideoCodec= NEXUS_VideoCodec_eVp9;
        break;
#endif

#if (NEXUS_PLATFORM_VERSION_MAJOR>19)
      case bvideo_codec_av1:
         nexusVideoCodec= NEXUS_VideoCodec_eAv1;
         break;
#endif

      default:                
         nexusVideoCodec= NEXUS_VideoCodec_eUnknown;
         break;
   }
   
   return nexusVideoCodec;
}

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static void updateClientPlaySpeed( GstWesterosSink *sink, gfloat clientPlaySpeed, gboolean playing )
{
   NEXUS_VideoDecoderTrickState trickState;
   NEXUS_VideoDecoderSettings settings;

   if ( !sink->videoStarted || sink->soc.serverPlaySpeed != 1.0 )
   {
      return;
   }

   if ( sink->soc.useImmediateOutput )
   {
       NEXUS_VideoDecoderTrickState trickState;
       NEXUS_SimpleVideoDecoder_GetTrickState(sink->soc.videoDecoder, &trickState);
       trickState.rate= NEXUS_NORMAL_DECODE_RATE * 2;
       trickState.tsmEnabled= NEXUS_TsmMode_eDisabled;
       NEXUS_SimpleVideoDecoder_SetTrickState(sink->soc.videoDecoder, &trickState);
       return;
   }

   if ( clientPlaySpeed < 0 )
   {
      GST_WARNING_OBJECT(sink, "Ignoring negative play speed");
      return;
   }

   sink->soc.clientPlaySpeed= clientPlaySpeed;

   NEXUS_SimpleVideoDecoder_GetTrickState(sink->soc.videoDecoder, &trickState);

   if ( ((clientPlaySpeed != 1.0) && (clientPlaySpeed != 0.0)) ||
        ((trickState.rate != NEXUS_NORMAL_DECODE_RATE) && (trickState.rate != 0)) )
   {
      trickState.rate= NEXUS_NORMAL_DECODE_RATE * clientPlaySpeed;
      trickState.stcTrickEnabled= TRUE;

      if ( clientPlaySpeed <= 1.0 )
      {
         trickState.topFieldOnly= false;
         trickState.decodeMode= NEXUS_VideoDecoderDecodeMode_eAll;
      }
      else
      if ( (clientPlaySpeed > 1.0) && (clientPlaySpeed <= 2.0) )
      {
         trickState.topFieldOnly= true;
         trickState.decodeMode= NEXUS_VideoDecoderDecodeMode_eAll;
      }
      else
      if ( (clientPlaySpeed > 2.0) && (clientPlaySpeed <= 4.0) )
      {
         trickState.topFieldOnly= true;
         trickState.decodeMode= NEXUS_VideoDecoderDecodeMode_eIP;
      }
      else
      {
         trickState.topFieldOnly= true;
         trickState.decodeMode= NEXUS_VideoDecoderDecodeMode_eI;
      }

      if ( clientPlaySpeed == 1.0 )
      {
         trickState.tsmEnabled= checkIndependentVideoClock( sink ) ? NEXUS_TsmMode_eDisabled : NEXUS_TsmMode_eEnabled;
      }
      else
      {
         trickState.tsmEnabled= NEXUS_TsmMode_eDisabled;
      }

      NEXUS_Error rc = NEXUS_SimpleVideoDecoder_SetTrickState(sink->soc.videoDecoder, &trickState);
      if ( NEXUS_SUCCESS != rc )
      {
         GST_ERROR_OBJECT(sink, "Error NEXUS_SimpleVideoDecoder_SetTrickState: %d", (int)rc);
      }
      else
      {
         GST_INFO_OBJECT(sink, "Play speed set to %f", clientPlaySpeed);
      }

      rc= NEXUS_SimpleStcChannel_Freeze(sink->soc.stcChannel, (clientPlaySpeed == 0.0 || !playing) ? TRUE : FALSE);
      if ( rc != NEXUS_SUCCESS )
      {
         GST_ERROR("updateClientPlaySpeed: NEXUS_SimpleStcChannel_Freeze FALSE failed: %d", (int)rc);
      }
      if ( clientPlaySpeed == 0.0 )
      {
         rc= NEXUS_SimpleStcChannel_Invalidate(sink->soc.stcChannel);
         if ( rc != NEXUS_SUCCESS )
         {
            GST_ERROR("updateClientPlaySpeed: NEXUS_SimpleStcChannel_Invalidate failed: %d", (int)rc);
         }
      }
   }
}

static gboolean processEventSinkSoc(GstWesterosSink *sink, GstPad *pad, GstEvent *event, gboolean *passToDefault )
{
   gboolean result= FALSE;

   LOCK( sink );
   if ( sink->startAfterLink && !sink->videoStarted )
   {
      if ( queryPeerHandles(sink) )
      {
         if ( !gst_westeros_sink_soc_start_video( sink ) )
         {
            GST_ERROR("prerollSinkSoc: gst_westeros_sink_soc_start_video failed");
         }
      }
   }
   UNLOCK( sink );

   switch (GST_EVENT_TYPE(event))
   {
      case GST_EVENT_CUSTOM_DOWNSTREAM:
      case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
         {
            GstStructure *structure;
            const GValue *val;
            structure= (GstStructure*)gst_event_get_structure(event);
            val= gst_structure_get_value(structure, "new_pid_channel");
            if ( val )
            {
               gpointer *ptr= g_value_get_pointer(val);
               if ( ptr )
               {
                  LOCK(sink);
                  GST_DEBUG("Pid channel changing: old %p", sink->soc.videoPidChannel);
                  sink->soc.videoPidChannel= (NEXUS_PidChannelHandle)ptr;
                  GST_DEBUG("Pid channel changing: new %p", sink->soc.videoPidChannel);

                  if ( sink->videoStarted )
                  {
                     sink->videoStarted= FALSE;
                     sink->soc.presentationStarted= FALSE;
                     NEXUS_SimpleVideoDecoder_Stop( sink->soc.videoDecoder );
                     sink->soc.frameCount= 0;
                     if ( !gst_westeros_sink_soc_start_video( sink ) )
                     {
                        GST_ERROR("gst_westeros_sink_soc_start_video failed");
                     }
                  }
                  UNLOCK(sink);
               }
            }

            *passToDefault= FALSE;

            result= true;
         }
         break;
      case GST_EVENT_STREAM_START:
         if ( swIsSWDecode( sink ) )
         {
            #ifdef ENABLE_SW_DECODE
            if ( !sink->soc.dataProbeId && sink->swLink )
            {
               GST_DEBUG("call establishSource from stream start");
               establishSource( sink );
            }
            #endif
         }
         break;
   }

   return result;
}

static GstFlowReturn prerollSinkSoc(GstBaseSink *base_sink, GstBuffer *buffer)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(base_sink);

   if ( swIsSWDecode( sink ) )
   {
      #ifdef ENABLE_SW_DECODE
      sink->soc.swPrerolled= true;
      #endif
      goto done;
   }

   if ( buffer )
   {
      if ( queryPeerHandles(sink) )
      {
         NEXUS_Error rc;

         #if (NEXUS_PLATFORM_VERSION_MAJOR>15)
         if ( sink->startAfterCaps )
         {
            // Wait till we get final caps since there might be HDR info
            GST_DEBUG("defer video start till caps");
            return GST_FLOW_OK;
         }
         #endif

         LOCK( sink );
         if ( !sink->videoStarted )
         {
            if ( !gst_westeros_sink_soc_start_video( sink ) )
            {
               GST_ERROR("prerollSinkSoc: gst_westeros_sink_soc_start_video failed");
            }

            if ( checkIndependentVideoClock( sink ) )
            {
               NEXUS_VideoDecoderTrickState trickState;
               NEXUS_SimpleVideoDecoder_GetTrickState(sink->soc.videoDecoder, &trickState);
               trickState.tsmEnabled= NEXUS_TsmMode_eDisabled;
               NEXUS_SimpleVideoDecoder_SetTrickState(sink->soc.videoDecoder, &trickState);
               GST_INFO_OBJECT(sink, "disable TsmMode");
            }
         }

         if ( sink->videoStarted && !sink->soc.videoPlaying )
         {
            NEXUS_Error rc;

            updateClientPlaySpeed( sink, 0.0, GST_STATE(GST_ELEMENT(sink)) == GST_STATE_PLAYING );

            if ( sink->soc.frameStepOnPreroll )
            {
               rc= NEXUS_SimpleVideoDecoder_FrameAdvance(sink->soc.videoDecoder);
               if ( NEXUS_SUCCESS != rc )
               {
                  GST_ERROR_OBJECT(sink, "prerollSinkSoc: Error NEXUS_SimpleVideoDecoder_FrameAdvance: %d", (int)rc);
               }
               sink->soc.videoPlaying= TRUE;
               UNLOCK( sink );
               updateVideoStatus(sink);
               LOCK( sink );
               sink->soc.videoPlaying= FALSE;
            }
         }
         UNLOCK( sink );
      }
   }

done:
   if ( !sink->soc.frameStepOnPreroll )
   {
      /* Set need_preroll to FALSE so that base sink will not block in
         wait_preroll since this would prevent further buffering while in
         paused state.  This is because westerossink does both the decoding
         and the display.  Set have_preroll to TRUE so that the transition
         from PAUSED to PLAYING is not forced to be async */
      GST_BASE_SINK(sink)->need_preroll= FALSE;
      GST_BASE_SINK(sink)->have_preroll= TRUE;
   }

   GST_INFO("preroll ok");

   return GST_FLOW_OK;
}

#if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
static void parseMasteringDisplayColorVolume( const gchar *metadata, NEXUS_MasteringDisplayColorVolume *colorVolume )
{
   gdouble lumaMax, lumaMin;
   gdouble Rx, Ry, Gx, Gy, Bx, By, Wx, Wy;

   memset (colorVolume, 0, sizeof(NEXUS_MasteringDisplayColorVolume));

   if (sscanf (metadata, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf",
            &Rx, &Ry, &Gx, &Gy, &Bx, &By, &Wx, &Wy, &lumaMax, &lumaMin) == 10)
   {
      colorVolume->redPrimary.x= (int)(Rx*50000);
      colorVolume->redPrimary.y= (int)(Ry*50000);
      colorVolume->greenPrimary.x= (int)(Gx*50000);
      colorVolume->greenPrimary.y= (int)(Gy*50000);
      colorVolume->bluePrimary.x= (int)(Bx*50000);
      colorVolume->bluePrimary.y= (int)(By*50000);
      colorVolume->whitePoint.x= (int)(Wx*50000);
      colorVolume->whitePoint.y= (int)(Wy*50000);

      // 1 cd / m^2
      colorVolume->luminance.max= (int)(lumaMax);

      // 0.0001 cd / m^2
      colorVolume->luminance.min= (int)(lumaMin*100000);
   }

   GST_DEBUG("mastering_display_metadata r(%d, %d) g(%d, %d) b(%d, %d) w(%d, %d) l(%d, %d)",
               colorVolume->redPrimary.x,
               colorVolume->redPrimary.y,
               colorVolume->greenPrimary.x,
               colorVolume->greenPrimary.y,
               colorVolume->bluePrimary.x,
               colorVolume->bluePrimary.y,
               colorVolume->whitePoint.x,
               colorVolume->whitePoint.y,
               colorVolume->luminance.max,
               colorVolume->luminance.min);
}

static void parseContentLightLevel( const gchar *str, NEXUS_ContentLightLevel *contentLightLevel )
{
   guint maxFALL, maxCLL;

   memset (contentLightLevel, 0, sizeof(NEXUS_ContentLightLevel));

   if ( sscanf (str, "%u:%u", &maxCLL, &maxFALL) == 2 )
   {
      contentLightLevel->max= maxCLL;
      contentLightLevel->maxFrameAverage= maxFALL;
   }

   GST_DEBUG("content_light_level (%d, %d) ", contentLightLevel->max , contentLightLevel->maxFrameAverage);
}
#endif

static bool swIsSWDecode( GstWesterosSink *sink )
{
   bool result= false;
   #ifdef ENABLE_SW_DECODE
   if ( sink->rm && sink->resAssignedId >= 0 )
   {
      if ( sink->resCurrCaps.capabilities & EssRMgrVidCap_software )
      {
         result= true;
      }
   }
   #endif
   return result;
}

#ifdef ENABLE_SW_DECODE
static void swFreeWorkSurface( GstWesterosSink *sink )
{
   if ( sink->soc.swWorkSurface != NULL )
   {
      NEXUS_Surface_Destroy( sink->soc.swWorkSurface );
      sink->soc.swWorkSurface= NULL;
   }
}

static void swAllocWorkSurface( GstWesterosSink *sink )
{
   NEXUS_SurfaceCreateSettings surfaceCreateSettings;

   swFreeWorkSurface( sink );

   /* Create a work surfaces for sw decoded frames */
   NEXUS_Surface_GetDefaultCreateSettings(&surfaceCreateSettings);
   surfaceCreateSettings.width= sink->soc.captureWidth;
   surfaceCreateSettings.height= sink->soc.captureHeight;
   surfaceCreateSettings.pixelFormat= NEXUS_PixelFormat_eA8_R8_G8_B8;

   sink->soc.swWorkSurface= NEXUS_Surface_Create(&surfaceCreateSettings);
   GST_LOG("sw work surface %p (%dx%d)", (void*)sink->soc.swWorkSurface, sink->soc.captureWidth, sink->soc.captureHeight);
   g_print("sw work surface %p (%dx%d)\n", (void*)sink->soc.swWorkSurface, sink->soc.captureWidth, sink->soc.captureHeight);
   if ( sink->soc.swWorkSurface == NULL )
   {
      GST_ERROR("Error unable to create sw work surface (%dx%d)", sink->soc.captureWidth, sink->soc.captureHeight );
   }
}

static gpointer swFirstFrameThread(gpointer data)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;

   if ( sink )
   {
      g_signal_emit (G_OBJECT (sink), g_signals[SIGNAL_FIRSTFRAME], 0, 2, NULL);
      g_thread_unref( sink->soc.firstFrameThread );
      sink->soc.firstFrameThread= NULL;
   }

   return NULL;
}

static void swGetVideoBounds( GstWesterosSink *sink, int *x, int *y, int *w, int *h )
{
   int vx, vy, vw, vh;
   int frameWidth, frameHeight;
   double contentWidth, contentHeight;
   double arf, ard;
   vx= sink->windowX;
   vy= sink->windowY;
   vw= sink->windowWidth;
   vh= sink->windowHeight;
   if ( sink->soc.pixelAspectRatioChanged ) GST_DEBUG("pixelAspectRatio: %f", sink->soc.pixelAspectRatio );
   frameWidth= sink->soc.frameWidth;
   frameHeight= sink->soc.frameHeight;
   if ( sink->soc.pixelAspectRatio >= 1 )
   {
      contentWidth= frameWidth*sink->soc.pixelAspectRatio;
      contentHeight= frameHeight;
   }
   else
   {
      contentWidth= frameWidth;
      contentHeight= frameHeight/sink->soc.pixelAspectRatio;
   }
   if ( sink->soc.pixelAspectRatioChanged ) GST_DEBUG("frame %dx%d contentWidth: %f contentHeight %f", frameWidth, frameHeight, contentWidth, contentHeight );
   ard= (double)sink->windowWidth/(double)sink->windowHeight;
   arf= (double)contentWidth/(double)contentHeight;
   if ( sink->soc.pixelAspectRatioChanged ) GST_DEBUG("ard %f arf %f\n", ard, arf);
   if ( arf >= ard )
   {
      vh= (contentHeight * sink->windowWidth) / contentWidth;
      vy= vy+(sink->windowHeight-vh)/2;
   }
   else
   {
      vw= (contentWidth * sink->windowHeight) / contentHeight;
      vx= vx+(sink->windowWidth-vw)/2;
   }
   if ( sink->soc.pixelAspectRatioChanged ) GST_DEBUG("vrect %d, %d, %d, %d", vx, vy, vw, vh);
   sink->soc.pixelAspectRatioChanged= FALSE;
   *x= vx;
   *y= vy;
   *w= vw;
   *h= vh;
}

static void swG2dCheckPoint( void *data, int unused )
{
   BSTD_UNUSED(unused);
   BKNI_SetEvent((BKNI_EventHandle)data);
}

static bool swInit( GstWesterosSink *sink )
{
   GST_DEBUG("swInit");
   if ( sinkAcquireResources( sink ) )
   {
      NEXUS_Graphics2DOpenSettings g2dOpenSettings;

      BKNI_CreateEvent( &sink->soc.g2dEvent );
      sink->soc.g2dEventCreated= true;

      NEXUS_Graphics2D_GetDefaultOpenSettings(&g2dOpenSettings);
      #if NEXUS_PLATFORM_VERSION_MAJOR >= 16
      g2dOpenSettings.secure= sink->soc.secureGraphics;
      #endif

      sink->soc.g2d= NEXUS_Graphics2D_Open(NEXUS_ANY_ID, &g2dOpenSettings);
      if ( sink->soc.g2d )
      {
         NEXUS_Graphics2DSettings g2dSettings;

         NEXUS_Graphics2D_GetSettings( sink->soc.g2d, &g2dSettings );
         g2dSettings.checkpointCallback.callback= swG2dCheckPoint;
         g2dSettings.checkpointCallback.context= sink->soc.g2dEvent;
         NEXUS_Graphics2D_SetSettings( sink->soc.g2d, &g2dSettings );
      }
   }
   else
   {
      GST_ERROR("swInit: sinkAcquireResources failed");
   }

   return true;
}

static void swTerm( GstWesterosSink *sink )
{
   GST_DEBUG("swTerm");
   LOCK(sink);
   if ( sink->soc.dataProbeId && sink->soc.dataProbePad )
   {
      GstStructure *structure= gst_structure_new_empty( "wstsw-remove-probe" );
      if ( structure )
      {
         /* push an event so probe will see remove request */
         sink->soc.removeDataProbe= true;
         UNLOCK(sink);
         GST_DEBUG("swTerm: pad send event");
         gst_pad_send_event( sink->soc.dataProbePad, gst_event_new_custom( GST_EVENT_CUSTOM_DOWNSTREAM, structure ) );
         LOCK(sink);
         GST_DEBUG("swTerm: pad send event done");
      }
   }
   UNLOCK(sink);
   if ( sink->soc.g2dEventCreated )
   {
      if ( sink->soc.g2d )
      {
         NEXUS_Graphics2D_Close( sink->soc.g2d );
         sink->soc.g2d= 0;
      }
      sink->soc.g2dEventCreated= false;
      BKNI_DestroyEvent( sink->soc.g2dEvent );
   }

   swFreeWorkSurface( sink );
   freeCaptureSurfaces( sink );

   sinkReleaseResources( sink );
}

static void swLink( GstWesterosSink *sink )
{
   GST_DEBUG("swLink");
   if ( !sink->soc.dataProbeId )
   {
      GST_DEBUG("call establishSource from swLink");
      establishSource( sink );
   }
}

static void swUnLink( GstWesterosSink *sink )
{
   GST_DEBUG("swUnLink");
   WESTEROS_UNUSED(sink);
}

static void swEvent( GstWesterosSink *sink, int id, int p1, void *p2 )
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(p1);
   WESTEROS_UNUSED(p2);

   GST_DEBUG("swEvent: id %d p1 %d p2 %p", id, p1, p2 );
   switch( id )
   {
      case SWEvt_pause:
         break;
      default:
         break;
   }
}

void swDisplay( GstWesterosSink *sink, SWFrame *frame )
{
   int ci;
   NEXUS_SurfaceHandle captureSurface;

   sink->soc.frameWidth= frame->width;
   sink->soc.frameHeight= frame->height;

   if ( (sink->soc.captureWidth != frame->width) || (sink->soc.captureHeight != frame->height) )
   {
      sink->soc.captureWidthNext= frame->width;
      sink->soc.captureHeightNext= frame->height;
      allocCaptureSurfaces( sink );
   }

   ci= sink->soc.swNextCaptureSurface;
   if ( ++sink->soc.swNextCaptureSurface >= NUM_CAPTURE_SURFACES )
   {
      sink->soc.swNextCaptureSurface= 0;
   }
   captureSurface= sink->soc.captureSurface[ci];

   if ( captureSurface )
   {
      NEXUS_Error rc;
      NEXUS_SurfaceMemory memSurf;
      NEXUS_Graphics2DBlitSettings g2dBlitSettings;
      unsigned char *srcY, *srcYRow;
      unsigned char *srcU, *srcURow;
      unsigned char *srcV, *srcVRow;
      unsigned char *dest, *destRow;
      int row, i;

      srcYRow= frame->Y;
      srcURow= frame->U;
      srcVRow= frame->V;
      NEXUS_Surface_GetMemory( captureSurface, &memSurf );
      destRow= memSurf.buffer;
      for( row= 0; row < frame->height; ++row )
      {
         srcY= srcYRow;
         srcU= srcURow;
         srcV= srcVRow;
         dest= destRow;
         for( i= 0; i < frame->width; ++i )
         {
            #ifdef BIG_ENDIAN_CPU
            *(dest++)= *(srcY++);
            if ( i & 1 )
            {
               *(dest++)= *(srcU++);
            }
            else
            {
               *(dest++)= *(srcV++);
            }
            #else
            if ( i & 1 )
            {
               *(dest++)= *(srcV++);
               *(dest++)= *(srcY++-1);
            }
            else
            {
               *(dest++)= *(srcU++);
               *(dest++)= *(++srcY);
            }
            #endif
         }
         srcYRow += frame->Ystride;
         if ( row & 1 )
         {
            srcURow += frame->Ustride;
            srcVRow += frame->Vstride;
         }
         destRow += memSurf.pitch;
      }
      NEXUS_Surface_Flush( captureSurface );

      if ( frame->frameNumber == 0 )
      {
         sink->soc.firstFrameThread= g_thread_new("westeros_first_frame", swFirstFrameThread, sink);
      }

      if ( sink->soc.forceAspectRatio && sink->vpcSurface )
      {
         int vx, vy, vw, vh;
         swGetVideoBounds( sink, &vx, &vy, &vw, &vh );
         wl_vpc_surface_set_geometry( sink->vpcSurface, vx, vy, vw, vh );
      }

      if ( sink->soc.enableTextureSignal )
      {
         if ( !sink->soc.swWorkSurface )
         {
            swAllocWorkSurface( sink );
         }
         if ( sink->soc.swWorkSurface )
         {
            NEXUS_Graphics2D_GetDefaultBlitSettings( &g2dBlitSettings );
            g2dBlitSettings.source.surface= captureSurface;
            g2dBlitSettings.output.surface= sink->soc.swWorkSurface;
            rc= NEXUS_Graphics2D_Blit(sink->soc.g2d, &g2dBlitSettings);
            if ( rc == NEXUS_SUCCESS )
            {
               rc= NEXUS_Graphics2D_Checkpoint( sink->soc.g2d, NULL );
               if ( rc == NEXUS_GRAPHICS2D_QUEUED )
               {
                  BKNI_WaitForEvent( sink->soc.g2dEvent, BKNI_INFINITE );
               }
            }
            processTextureSignal( sink, sink->soc.swWorkSurface );
         }
      }
      else if ( sink->soc.sb )
      {
         processTextureWayland( sink, captureSurface );
         if ( sink->display && wl_display_dispatch_queue_pending(sink->display, sink->queue) == 0 )
         {
            wl_display_flush(sink->display);
            if ( !sink->eosDetected )
            {
               wl_display_roundtrip_queue(sink->display,sink->queue);
            }
         }
      }
   }
}

static unsigned char* convertToBinary( gchar *asciiHexData, int* dataLen )
{
   unsigned char *buffer= 0;
   int bufferSize;
   int len, i, d, byte;

   len= strlen(asciiHexData);
   bufferSize= (len/2 );
   buffer= (unsigned char*)malloc( bufferSize );
   if ( buffer )
   {
      for( i= 0; i < len; ++i )
      {
         d= asciiHexData[i];
         if ( (d >= '0') && (d <= '9') )
         {
            d= d-'0';
         }
         else if ( (d >= 'A') && (d <= 'F') )
         {
            d= (d-'A')+10;
         }
         else if ( (d >= 'a') && (d <= 'f') )
         {
            d= (d-'a')+10;
         }
         else
         {
            GST_ERROR("convertToBinary: bad character (%02X) in codec_data", d);
            d= 0;
         }
         if ( i % 2 == 0 )
         {
            byte= d;
         }
         else
         {
            byte= (byte << 4)+d;
            buffer[i/2]= byte;
         }
      }
      *dataLen= bufferSize;
   }

   return buffer;
}

static void dataProbeGenerateH264InitialationData( GstWesterosSink *sink, gchar *codecData )
{
   if ( sink && codecData )
   {
      unsigned char *binary= 0;
      int initDataLen;
      unsigned char *initData= 0;
      int i, binaryLen= 0;
      int spsLen, ppsLen;

      binary= convertToBinary( codecData, &binaryLen );
      if ( binary )
      {
         if ( binaryLen > 10 )
         {
            i= 6;
            spsLen= (binary[i] << 8)|binary[i+1];
            i += (spsLen + 3);
            ppsLen= (binary[i] << 8)|binary[i+1];
            initDataLen= 4 + spsLen + 4 + ppsLen;
            initData= (unsigned char*)malloc( initDataLen );
            if ( initData )
            {
               initData[0]= 0;
               initData[1]= 0;
               initData[2]= 0;
               initData[3]= 1;
               memcpy( initData+4, binary+8, spsLen );
               initData[4+spsLen+0]= 0;
               initData[4+spsLen+1]= 0;
               initData[4+spsLen+2]= 0;
               initData[4+spsLen+3]= 1;
               memcpy( initData+4+spsLen+4, binary+8+spsLen+3, ppsLen );

               if ( sink->soc.dataProbeCodecData )
               {
                  free( sink->soc.dataProbeCodecData );
                  sink->soc.dataProbeCodecData= 0;
                  sink->soc.dataProbeCodecDataLen= 0;
               }
               sink->soc.dataProbeCodecData= initData;
               sink->soc.dataProbeCodecDataLen= initDataLen;

               wstsw_set_codec_init_data( sink, initDataLen, initData );
            }
            else
            {
               GST_ERROR("dataProbeGenerateCodecInitialationData: unble to allocate memory for sps/pps buffer");
            }
         }

         free( binary );
      }
      else
      {
         GST_ERROR("dataProbeGenerateCodecInitialationData: unble to allocate memory for binary codec_data");
      }
   }
}

static void dataProbeProcessCaps( GstWesterosSink *sink, GstCaps *caps )
{
   GstStructure *structure;

   structure= gst_caps_get_structure(caps,0);
   if ( structure )
   {
      const gchar *mime= gst_structure_get_name(structure);
      if ( strstr( mime, "video/x-h264" ) )
      {
         gchar *str= gst_caps_to_string(caps);
         if ( !strstr( str, "stream-format=(string)byte-stream" ) )
         {
            const GValue *val= gst_structure_get_value( structure, "codec_data");
            if ( val )
            {
               gchar *codecData= gst_value_serialize(val);
               if ( codecData )
               {
                  sink->soc.dataProbeNeedStartCodes= TRUE;
                  dataProbeGenerateH264InitialationData( sink, codecData );
                  g_free( codecData );
               }
            }
         }
         g_free( str );
      }
   }
   wstsw_process_caps( sink, caps );
}

static void dataProbeDestroy( gpointer userData )
{
   GstWesterosSink *sink= (GstWesterosSink*)userData;
   if ( sink )
   {
      gulong probeId;
      LOCK(sink);
      probeId= sink->soc.dataProbeId;
      if ( probeId )
      {
         g_print("dataProbeDestroy: probeId %u\n", probeId );
         gst_object_unref( sink->soc.dataProbePad );
         sink->soc.dataProbePad= 0;
         sink->soc.dataProbeId= 0;
         g_print("dataProbeDestroy: probeId %u done\n", probeId);
      }
      UNLOCK(sink);
   }
}

static GstPadProbeReturn dataProbe( GstPad *pad, GstPadProbeInfo *info, gpointer userData )
{
   GstPadProbeReturn result= GST_PAD_PROBE_OK;
   GstWesterosSink *sink= (GstWesterosSink*)userData;
   if ( sink )
   {
      if ( sink->soc.removeDataProbe )
      {
         result= GST_PAD_PROBE_REMOVE;
         goto exit;
      }
      if ( info->type & GST_PAD_PROBE_TYPE_BUFFER )
      {
         GstBuffer *buffer= (GstBuffer*)info->data;
         #ifdef USE_GST1
         GstMapInfo map;
         #endif
         int inSize= 0;
         unsigned char *inData= 0;
         int i, unitlen;

         #ifdef USE_GST1
         gst_buffer_map(buffer, &map,
                        (sink->soc.dataProbeNeedStartCodes ?
                        (GstMapFlags)GST_MAP_READ|GST_MAP_WRITE :
                        (GstMapFlags)GST_MAP_READ) );
         inSize= map.size;
         inData= map.data;
         #else
         inSize= (int)GST_BUFFER_SIZE(buffer);
         inData= GST_BUFFER_DATA(buffer);
         #endif

         if ( sink->soc.dataProbeNeedStartCodes )
         {
            i= 0;
            while( i < inSize-3 )
            {
               unitlen= (inData[i+0] << 24) | (inData[i+1] << 16) | (inData[i+2] << 8) | inData[i+3];
               if ( (i == 0) && (unitlen == 1) )
               {
                  break;
               }
               inData[i+0]= 0;
               inData[i+1]= 0;
               inData[i+2]= 0;
               inData[i+3]= 1;
               i += (unitlen + 4);
            }
         }

         #ifdef USE_GST1
         gst_buffer_unmap( buffer, &map);
         #endif

         gst_buffer_ref(buffer);

         if ( !wstsw_render( sink, buffer ) )
         {
            result= GST_PAD_PROBE_REMOVE;
         }

         gst_buffer_unref( buffer );

         if ( sink->soc.swPrerolled )
         {
            result= GST_PAD_PROBE_DROP;
         }
      }
      else if ( info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM )
      {
         GstEvent *event= (GstEvent*)info->data;
         if ( event )
         {
            switch (GST_EVENT_TYPE(event))
            {
               case GST_EVENT_CAPS:
                  {
                     GstCaps *caps;
                     gst_event_parse_caps(event, &caps);
                     dataProbeProcessCaps( sink, caps );
                  }
                  break;
               case GST_EVENT_EOS:
                  if ( !sink->soc.removeDataProbe )
                  {
                     sink->eosEventSeen= TRUE;
                     gst_westeros_sink_eos_detected( sink );
                     result= GST_PAD_PROBE_REMOVE;
                  }
                  break;
            }
         }
      }
      if ( sink->soc.removeDataProbe )
      {
         result= GST_PAD_PROBE_REMOVE;
      }
   }
exit:
   if ( result == GST_PAD_PROBE_REMOVE )
   {
      GST_DEBUG("remove probe requested");
   }
   return result;
}

static bool establishSource( GstWesterosSink *sink )
{
   bool result= false;
   GstElement *src= 0;
   GstPad *sinkPad= 0;
   GstElement *pipeline= 0;
   GstElement *element, *elementPrev= 0;
   GstIterator *iterator;

   element= GST_ELEMENT_CAST(sink);
   do
   {
      if ( elementPrev )
      {
         gst_object_unref( elementPrev );
      }
      element= GST_ELEMENT_CAST(gst_element_get_parent( element ));
      if ( element )
      {
         elementPrev= pipeline;
         pipeline= element;
      }
   }
   while( element != 0 );

   GST_DEBUG("establishSource: pipeline %p", pipeline);
   if ( pipeline )
   {
      GstIterator *iterElement= gst_bin_iterate_recurse( GST_BIN(pipeline) );
      if ( iterElement )
      {
         GValue itemElement= G_VALUE_INIT;
         while( gst_iterator_next( iterElement, &itemElement ) == GST_ITERATOR_OK )
         {
            element= (GstElement*)g_value_get_object( &itemElement );
            if ( element && !GST_IS_BIN(element) )
            {
               GstElementClass *ec= GST_ELEMENT_GET_CLASS(element);
               if ( ec )
               {
                  const gchar *meta= gst_element_class_get_metadata( ec, GST_ELEMENT_METADATA_KLASS);
                  if ( meta && strstr(meta, "Parser") && strstr(meta, "Video") && strstr(meta, "Codec") )
                  {
                     GST_DEBUG("establishSource: filter element %p", element);
                     GstIterator *iterPad= gst_element_iterate_sink_pads( element );
                     if ( iterPad )
                     {
                        GValue itemPad= G_VALUE_INIT;
                        while( gst_iterator_next( iterPad, &itemPad ) == GST_ITERATOR_OK )
                        {
                           GstPad *pad= (GstPad*)g_value_get_object( &itemPad );
                           if ( pad )
                           {
                              GST_DEBUG("establishSource: filter element pad %p", pad);
                              GstCaps *caps= gst_pad_get_current_caps(pad);
                              if ( caps )
                              {
                                 GstStructure *structure;
                                 const gchar *mime;
                                 structure= gst_caps_get_structure(caps, 0);
                                 if(structure )
                                 {
                                    mime= gst_structure_get_name(structure);
                                    if ( strstr( mime, "video" ) )
                                    {
                                       sinkPad= pad;
                                       g_value_reset( &itemPad );
                                       break;
                                    }
                                 }
                              }
                           }
                           g_value_reset( &itemPad );
                        }
                     }
                     src= (GstElement*)gst_object_ref( element );
                     gchar *name= gst_element_get_name( element );
                     if ( name )
                     {
                        GST_DEBUG( "found video parser: name (%s) sink pad %p", name, sinkPad);
                        g_free( name );
                        if ( sinkPad )
                        {
                           GstCaps *caps= gst_pad_get_current_caps(sinkPad);
                           if ( caps )
                           {
                              dataProbeProcessCaps( sink, caps );
                           }
                           sink->soc.dataProbePad= (GstPad*)gst_object_ref( sinkPad );
                           sink->soc.dataProbeId= gst_pad_add_probe( sinkPad,
                                                                     (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                                                                     dataProbe,
                                                                     sink,
                                                                     dataProbeDestroy );
                           GST_DEBUG("video data probeId %u", sink->soc.dataProbeId);
                        }
                     }
                     g_value_reset( &itemElement );
                     break;
                  }
               }
            }
            g_value_reset( &itemElement );
         }
         gst_iterator_free(iterElement);
      }
   }
   if ( src )
   {
      gst_object_unref( src );
      result= true;
   }
   return result;
}
#endif

static int sinkAcquireVideo( GstWesterosSink *sink )
{
   int result= 0;
   NEXUS_Error rc;
   NxClient_ConnectSettings connectSettings;

   GST_DEBUG("sinkAcquireVideo: enter");
   if ( sink->rm && sink->resAssignedId >= 0 )
   {
      if ( swIsSWDecode( sink ) )
      {
         result= 1;
         goto done;
      }
      #if (NEXUS_PLATFORM_VERSION_MAJOR>=16)
      if ( sink->resCurrCaps.capabilities & EssRMgrVidCap_limitedResolution )
      {
         sink->soc.usePip= TRUE;
      }
      #endif
   }

   sink->soc.videoWindow= NEXUS_SurfaceClient_AcquireVideoWindow( sink->soc.surfaceClient, 0);
   sink->soc.videoDecoderId= sink->soc.allocSurface.simpleVideoDecoder[0].id;
   sink->soc.videoDecoder= NEXUS_SimpleVideoDecoder_Acquire( sink->soc.videoDecoderId );

   NEXUS_StartCallbacks( sink->soc.videoDecoder );

   NEXUS_SimpleVideoDecoderClientSettings settings;
   NEXUS_SimpleVideoDecoder_GetClientSettings(sink->soc.videoDecoder, &settings);
   settings.resourceChanged.callback= resourceChangedCallback;
   settings.resourceChanged.context= sink;
   settings.resourceChanged.param= 0;;
   NEXUS_SimpleVideoDecoder_SetClientSettings(sink->soc.videoDecoder, &settings);

   /* Connect to the decoder */
   NxClient_GetDefaultConnectSettings(&connectSettings);
   connectSettings.simpleVideoDecoder[0].id= sink->soc.videoDecoderId;
   connectSettings.simpleVideoDecoder[0].surfaceClientId= sink->soc.surfaceClientId;
   connectSettings.simpleVideoDecoder[0].windowId= 0;
   connectSettings.simpleVideoDecoder[0].windowCapabilities.type= NxClient_VideoWindowType_eMain;
   connectSettings.simpleVideoDecoder[0].windowCapabilities.maxWidth= 1920;
   connectSettings.simpleVideoDecoder[0].windowCapabilities.maxHeight= 1080;
   #if (NEXUS_PLATFORM_VERSION_MAJOR>=16)
   connectSettings.simpleVideoDecoder[0].decoderCapabilities.secureVideo= (sink->soc.secureVideo ? true : false);

   NEXUS_VideoDecoderCapabilities videoDecoderCaps;
   NEXUS_GetVideoDecoderCapabilities(&videoDecoderCaps);
   if ( videoDecoderCaps.memory[0].maxFormat >= NEXUS_VideoFormat_e3840x2160p24hz )
   {
      printf("westerossink: supports 4K\n");
      if ( !sink->soc.usePip )
      {
         connectSettings.simpleVideoDecoder[0].decoderCapabilities.maxWidth= 3840;
         connectSettings.simpleVideoDecoder[0].decoderCapabilities.maxHeight= 2160;
         connectSettings.simpleVideoDecoder[0].decoderCapabilities.colorDepth= 10;
         connectSettings.simpleVideoDecoder[0].decoderCapabilities.feeder.colorDepth= 10;
      }
   }
   else
   {
   #endif
      connectSettings.simpleVideoDecoder[0].decoderCapabilities.maxWidth= 1920;
      connectSettings.simpleVideoDecoder[0].decoderCapabilities.maxHeight= 1080;
   #if (NEXUS_PLATFORM_VERSION_MAJOR>=16)
   }
   #endif
   if ( sink->soc.usePip )
   {
      // We are using PIP window. Restrict size.
      connectSettings.simpleVideoDecoder[0].windowCapabilities.type= NxClient_VideoWindowType_ePip;
      connectSettings.simpleVideoDecoder[0].windowCapabilities.maxWidth= MAX_PIP_WIDTH;
      connectSettings.simpleVideoDecoder[0].windowCapabilities.maxHeight= MAX_PIP_HEIGHT;
   }
   rc= NxClient_Connect(&connectSettings, &sink->soc.connectId);
   if ( rc == NEXUS_SUCCESS )
   {
      sink->soc.presentationStarted= FALSE;

      NEXUS_VideoDecoderSettings settings;
      NEXUS_VideoDecoderExtendedSettings ext_settings;
      NEXUS_SimpleVideoDecoder_GetSettings(sink->soc.videoDecoder, &settings);
      NEXUS_SimpleVideoDecoder_GetExtendedSettings(sink->soc.videoDecoder, &ext_settings);

      #if (NEXUS_PLATFORM_VERSION_MAJOR > 16) || ((NEXUS_PLATFORM_VERSION_MAJOR == 16) && (NEXUS_PLATFORM_VERSION_MINOR > 3))
      settings.scanMode= NEXUS_VideoDecoderScanMode_e1080p;
      #endif
      // Don't enable zeroDelayOutputMode since this combined with
      // NEXUS_VideoDecoderTimestampMode_eDisplay will cause the capture
      // to omit all out of order frames (ie. all B-Frames)
      settings.channelChangeMode= NEXUS_VideoDecoder_ChannelChangeMode_eMuteUntilFirstPicture;
      settings.ptsOffset= sink->soc.ptsOffset;
      settings.fifoEmpty.callback= underflowCallback;
      settings.fifoEmpty.context= sink;
      settings.firstPtsPassed.callback= firstPtsPassedCallback;
      settings.firstPtsPassed.context= sink;
      settings.ptsError.callback= ptsErrorCallback;
      settings.ptsError.context= sink;
      #if (NEXUS_PLATFORM_VERSION_MAJOR > 15) || ((NEXUS_PLATFORM_VERSION_MAJOR == 15) && (NEXUS_PLATFORM_VERSION_MINOR > 2))
      settings.streamChanged.callback= streamChangedCallback;
      settings.streamChanged.context= sink;
      #endif
      ext_settings.dataReadyCallback.callback= NULL;
      ext_settings.dataReadyCallback.context= NULL;
      ext_settings.zeroDelayOutputMode= false;
      if ( sink->soc.useLowDelay )
      {
         ext_settings.lowLatencySettings.mode= NEXUS_VideoDecoderLowLatencyMode_eAverage;
         ext_settings.lowLatencySettings.latency= sink->soc.latencyTarget;
         printf("westerossink: using low delay (target %d ms)\n", sink->soc.latencyTarget);
      }
      if ( sink->soc.useImmediateOutput )
      {
         ext_settings.zeroDelayOutputMode= true;
         ext_settings.ignoreDpbOutputDelaySyntax= true;
         printf("westerossink: using immediate output mode\n");
      }
      ext_settings.treatIFrameAsRap= true;
      ext_settings.ignoreNumReorderFramesEqZero= true;

      rc= NEXUS_SimpleVideoDecoder_SetSettings(sink->soc.videoDecoder, &settings);
      if ( rc != NEXUS_SUCCESS )
      {
         GST_WARNING("sinkAcquireVideo: NEXUS_SimpleVideoDecoder_SetSettings failed rc %d", rc);
      }
      rc= NEXUS_SimpleVideoDecoder_SetExtendedSettings(sink->soc.videoDecoder, &ext_settings);
      if ( rc != NEXUS_SUCCESS )
      {
         GST_WARNING("sinkAcquireVideo: NEXUS_SimpleVideoDecoder_SetExtendedSettings failed rc %d", rc);
      }

      if ( sink->soc.usePip )
      {
         gst_westeros_sink_soc_update_video_position( sink );
      }

      NEXUS_SurfaceComposition composition;
      NxClient_GetSurfaceClientComposition( sink->soc.surfaceClientId, &composition );
      composition.zorder= sink->zorder*MAX_ZORDER;
      rc= NxClient_SetSurfaceClientComposition( sink->soc.surfaceClientId, &composition );
      if ( rc != NEXUS_SUCCESS )
      {
         GST_WARNING("sinkAcquireVideo: NxClient_SetSurfaceClientComposition failed rc %d", rc);
      }

      NEXUS_SurfaceClientSettings clientSettings;
      NEXUS_SurfaceClient_GetSettings(sink->soc.videoWindow, &clientSettings);
      clientSettings.composition.contentMode= sink->soc.zoomMode;
      sink->visible= sink->show;
      clientSettings.composition.visible= sink->visible;
      NEXUS_SurfaceClient_SetSettings(sink->soc.videoWindow, &clientSettings);

      LOCK( sink );
      if ( sink->videoStarted )
      {
         GST_DEBUG("sinkAcquireVideo: calling start video");
         if ( !gst_westeros_sink_soc_start_video( sink ) )
         {
            GST_ERROR("sinkAcquireVideo: gst_westeros_sink_soc_start_video failed");
         }
      }
      sink->soc.haveHardware= TRUE;
      UNLOCK( sink );

      result= 1;
   }
   else
   {
      GST_ERROR("sinkAcquireVideo: NxClient_Connect failed: %d", rc);
   }

done:
   GST_DEBUG("sinkAcquireVideo: exit: %d", result);

   return result;
}

static void sinkReleaseVideo( GstWesterosSink *sink )
{
   GST_DEBUG("sinkReleaseVideo: enter");
   LOCK( sink );
   if ( sink->soc.videoDecoder )
   {
      NEXUS_SimpleVideoDecoderHandle videoDecoder= sink->soc.videoDecoder;
      UNLOCK( sink );
      NEXUS_StopCallbacks( videoDecoder );
      LOCK( sink );
   }
   if ( sink->soc.connectId != 0 )
   {
      if ( sink->soc.stcChannel )
      {
         NEXUS_Error rc;
         rc= NEXUS_SimpleStcChannel_Freeze(sink->soc.stcChannel, TRUE);
         GST_DEBUG("NEXUS_SimpleStcChannel_Freeze FALSE ");
         if ( rc != NEXUS_SUCCESS )
         {
            GST_ERROR("sinkReleaseVideo: NEXUS_SimpleStcChannel_Freeze FALSE failed: %d", (int)rc);
         }
      }

      if ( sink->videoStarted )
      {
         GST_DEBUG("sinkReleaseVideo: stop decoder");
         NEXUS_SimpleVideoDecoder_Stop(sink->soc.videoDecoder);
      }

      sink->soc.haveHardware= FALSE;
      sink->soc.usePip= FALSE;
      sink->soc.stcChannel= NULL;
      sink->soc.videoPidChannel= NULL;
      sink->soc.codec= bvideo_codec_unknown;

      GST_DEBUG("sinkReleaseVideo: disconnect");
      NxClient_Disconnect(sink->soc.connectId);
      sink->soc.connectId= 0;
      if ( sink->soc.videoDecoder )
      {
         GST_DEBUG("sinkReleaseVideo: release decoder");
         NEXUS_SimpleVideoDecoder_Release(sink->soc.videoDecoder);
         sink->soc.videoDecoder= 0;
      }
      if ( sink->soc.videoWindow )
      {
         GST_DEBUG("sinkReleaseVideo: release video window");
         NEXUS_SurfaceClient_ReleaseVideoWindow(sink->soc.videoWindow);
         sink->soc.videoWindow= 0;
      }
   }
   #ifdef ENABLE_SW_DECODE
   if ( sink->soc.dataProbeId )
   {
      GST_DEBUG("gst_pad_remove_probe dataProbeId %d\n", sink->soc.dataProbeId);
      gst_pad_remove_probe( sink->soc.dataProbePad, sink->soc.dataProbeId );
      gst_object_unref( sink->soc.dataProbePad );
      sink->soc.dataProbePad= 0;
      sink->soc.dataProbeId= 0;
   }
   if ( sink->soc.dataProbeCodecData )
   {
      free( sink->soc.dataProbeCodecData );
      sink->soc.dataProbeCodecData= 0;
   }
   #endif
   UNLOCK( sink );
   GST_DEBUG("sinkReleaseVideo: exit");
}

static int sinkAcquireResources( GstWesterosSink *sink )
{
   int result= 0;
   NEXUS_Error rc;
   NxClient_AllocSettings allocSettings;

   NxClient_GetDefaultAllocSettings(&allocSettings);
   allocSettings.surfaceClient= 1;
   allocSettings.simpleVideoDecoder= 1;
   rc= NxClient_Alloc(&allocSettings, &sink->soc.allocSurface);
   if ( rc == NEXUS_SUCCESS )
   {
      sink->soc.surfaceClientId= sink->soc.allocSurface.surfaceClient[0].id;
      sink->soc.surfaceClient= NEXUS_SurfaceClient_Acquire(sink->soc.surfaceClientId);

      result= sinkAcquireVideo( sink );
   }
   else
   {
      GST_ERROR("sinkAcquireResources: NxClient_Alloc failed %d", rc);
   }

   return result;
}

static void sinkReleaseResources( GstWesterosSink *sink )
{
   if ( sink->soc.surfaceClientId != 0 )
   {
      sinkReleaseVideo( sink );

      if ( sink->soc.surfaceClient )
      {
         NEXUS_SurfaceClient_Release(sink->soc.surfaceClient);
         sink->soc.surfaceClient= 0;
      }
      sink->soc.surfaceClientId= 0;
      NxClient_Free(&sink->soc.allocSurface);
   }
}
