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
#include <sys/time.h>
#include <dlfcn.h>

#include "libgdl.h"
#include "gdl_types.h"

#include "westeros-sink.h"

#define FRAME_POLL_TIME (8000)
#define EOS_DETECT_DELAY (500000)
#define EOS_DETECT_DELAY_AT_START (10000000)
#define UNDERFLOW_INTERVAL (5000)
#define DEFAULT_MODE_WIDTH (1280)
#define DEFAULT_MODE_HEIGHT (720)
#define DEFAULT_STREAM_TIME_OFFSET (120*GST_MSECOND)
#define GST_TO_ISMD_RATE(r) ((gint)(r*10000))
#define GST_TO_ISMD_TIME(t) (((t) == GST_CLOCK_TIME_NONE) ? ISMD_NO_PTS : ((((t)*90)+500000)/1000000))
#define ISMD_TO_GST_TIME(t) (((t) == ISMD_NO_PTS) ? GST_CLOCK_TIME_NONE : ((((t)*1000000)+45)/90))

#define SYSTEM_STRIDE (2048)

GST_DEBUG_CATEGORY_EXTERN (gst_westeros_sink_debug);
#define GST_CAT_DEFAULT gst_westeros_sink_debug

enum
{
  PROP_VIDEO_PLANE= PROP_SOC_BASE,
  PROP_GDL_PLANE,
  PROP_SCALE_MODE,
  PROP_ENABLE_CC_PASSTHRU,
  PROP_MUTE,
  PROP_CURRENT_PTS,
  PROP_DEINTERLACING_POLICY,
  PROP_STREAM_TIME_OFFSET,
  PROP_USE_VIRTUAL_COORDS
};

enum
{
   SIGNAL_FIRSTFRAME,
   SIGNAL_UNDERFLOW,
   MAX_SIGNAL
};

enum
{
   ISMD_DEMUX,
   ISMD_VIDDEC,
   ISMD_VIDPPROC,
   ISMD_VIDSINK,
   ISMD_VIDREND,
   ISMD_AUDSINK
};

static guint g_signals[MAX_SIGNAL]= {0, 0};

static void processFrame( GstWesterosSink *sink, ismd_buffer_handle_t ismdBuffer );
static gpointer monitorThread(gpointer data);
static void setupGDL( GstWesterosSink *sink );
static void termGDL( GstWesterosSink *sink );
static void setupCCPlane( GstWesterosSink *sink );

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

gboolean initIsmdGst( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   const char *moduleName= "libgstfluismd.so";
   const char *checkAPIName= "_ismd_gst_buffer_check_type";
   const char *getBufferAPIName= "ismd_gst_buffer_get_handle";
   const char *registerDeviceAPIName= "ismd_gst_register_device_handle";
   const char *getDefaultClockAPIName= "ismd_gst_clock_get_default_clock";
   const char *markBaseTimeAPIName= "ismd_gst_clock_mark_basetime";
   const char *clearBaseTimeAPIName= "ismd_gst_clock_clear_basetime";
   
   void *module= 0;

   module= dlopen( moduleName, RTLD_NOW );
   if ( !module )
   {
      GST_ERROR("westeros-sink-soc:: failed to load module (%s)", moduleName);
      GST_ERROR("  detail: %s", dlerror() );
      goto exit;
   }
   
   sink->soc.isIsmdBuffer= (PISMDGSTBUFFERCHECKTYPE)dlsym( module, checkAPIName );
   if ( !sink->soc.isIsmdBuffer )
   {
      GST_ERROR("westeros-sink-soc: failed to find module (%s) method (%s)", moduleName, checkAPIName );
      GST_ERROR("  detail: %s", dlerror() );
      goto exit;
   }
   
   sink->soc.getIsmdBufferHandle= (PISMDGSTBUFFERGETHANDLE)dlsym( module, getBufferAPIName );
   if ( !sink->soc.getIsmdBufferHandle )
   {
      GST_ERROR("westeros-sink-soc: failed to find module (%s) method (%s)", moduleName, getBufferAPIName );
      GST_ERROR("  detail: %s", dlerror() );
      goto exit;
   }
   
   sink->soc.registerDeviceHandle= (PISMDGSTREGISTERDEVICEHANDLE)dlsym( module, registerDeviceAPIName );
   if ( !sink->soc.registerDeviceHandle )
   {
      GST_ERROR("westeros-sink-soc: failed to find module (%s) method (%s)", moduleName, registerDeviceAPIName );
      GST_ERROR("  detail: %s", dlerror() );
      goto exit;
   }

   sink->soc.getDefaultClock= (PISMDGSTGETDEFAULTCLOCK)dlsym( module, getDefaultClockAPIName );
   if ( !sink->soc.getDefaultClock )
   {
      GST_ERROR("westeros-sink-soc: failed to find module (%s) method (%s)", moduleName, getDefaultClockAPIName );
      GST_ERROR("  detail: %s", dlerror() );
      goto exit;
   }

   sink->soc.markBaseTime= (PISMDGSTMARKBASETIME)dlsym( module, markBaseTimeAPIName );
   if ( !sink->soc.markBaseTime )
   {
      GST_WARNING("westeros-sink-soc: failed to find module (%s) method (%s)", moduleName, markBaseTimeAPIName );
      GST_WARNING("  detail: %s", dlerror() );
   }

   sink->soc.clearBaseTime= (PISMDGSTCLEARBASETIME)dlsym( module, clearBaseTimeAPIName );
   if ( !sink->soc.clearBaseTime )
   {
      GST_WARNING("westeros-sink-soc: failed to find module (%s) method (%s)", moduleName, clearBaseTimeAPIName );
      GST_WARNING("  detail: %s", dlerror() );
   }

   result= TRUE;
   
exit:

   if ( !result )
   {
      if ( module )
      {
         dlclose( module );
      }
   }
   
   return result;   
}

static void icegdlFormat(void *data, struct wl_icegdl *icegdl, uint32_t format)
{
   WESTEROS_UNUSED(icegdl);
   GstWesterosSink *sink= (GstWesterosSink*)data;
   if ( sink )
   {
      GST_INFO_OBJECT(sink, "westeros-sink-soc: registry: icegdlFormat: %X", format);
   }
}

static void icegdlPlane(void *data, struct wl_icegdl *icegdl, uint32_t plane )
{
   WESTEROS_UNUSED(icegdl);

   GstWesterosSink *sink= (GstWesterosSink*)data;
   if ( sink )
   {
      GST_INFO_OBJECT(sink, "westeros-sink-soc: icegdlPlane: sink %p using plane %d", sink, plane);
   }
}

struct wl_icegdl_listener icegdlListener = {
	icegdlPlane,
	icegdlFormat
};

void gst_westeros_sink_soc_class_init(GstWesterosSinkClass *klass)
{
   GObjectClass *gobject_class= (GObjectClass *) klass;
   GstElementClass *gstelement_class;
   
   g_object_class_install_property (gobject_class, PROP_VIDEO_PLANE,
     g_param_spec_string (
        "video-plane", 
        "upp plane to use for video",
        "upp plane (one of UPP_A, UPP_B, UPP_C, UPP_C)",
        "",
        (GParamFlags)G_PARAM_READWRITE ));

   g_object_class_install_property (gobject_class, PROP_GDL_PLANE,
      g_param_spec_uint (
          "gdl-plane",
          "upp plane used for video",
          "upp plane (one of GDL_PLANE_ID_NONE, GDL_PLANE_ID_UPP_A, GDL_PLANE_ID_UPP_B, GDL_PLANE_ID_UPP_C, or GDL_PLANE_ID_UPP_D)",
          GDL_PLANE_ID_UNDEFINED, GDL_PLANE_ID_UPP_D, GDL_PLANE_ID_UPP_A, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_SCALE_MODE,
      g_param_spec_uint (
          "scale-mode",
          "scale mode used for video",
          "scale mode (one of VPP_NO_SCALING, SCALE_TO_FIT, ZOOM_TO_FIT, ZOOM_TO_FILL)",
          ISMD_VIDPPROC_SCALING_POLICY_SCALE_TO_FIT,
          ISMD_VIDPPROC_SCALING_POLICY_ZOOM_TO_FIT,
          ISMD_VIDPPROC_SCALING_POLICY_SCALE_TO_FIT,
          G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_ENABLE_CC_PASSTHRU,
      g_param_spec_boolean (
          "cc-passthrough",
          "cc-passthrough",
          "Closed caption passthrough",
          TRUE, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_uint (
          "mute",
          "mute video",
          "mute video",
          0,1,1, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_CURRENT_PTS,
      g_param_spec_ulong (
          "currentPTS",
          "currentPTS",
          "Current PTS value",
          0, G_MAXULONG, 0, G_PARAM_READABLE));

   g_object_class_install_property (gobject_class, PROP_DEINTERLACING_POLICY,
      g_param_spec_uint (
          "deinterlacing-policy",
          "deinterlacing-policy",
          "Set deinterlacing policy",
          ISMD_VIDPPROC_DI_POLICY_NONE, ISMD_VIDPPROC_DI_POLICY_NEVER, ISMD_VIDPPROC_DI_POLICY_VIDEO, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_STREAM_TIME_OFFSET,
      g_param_spec_uint64 (
          "stream-time-offset",
          "stream time offset",
          "Set an offset in ns to clock synchronization",
          0, G_MAXUINT64, DEFAULT_STREAM_TIME_OFFSET, G_PARAM_READWRITE));

   g_object_class_install_property (gobject_class, PROP_USE_VIRTUAL_COORDS,
      g_param_spec_boolean (
          "use-virtual-coords",
          "use virtual coords",
          "use virtual coordinates based on a 1280x720 screen",
          TRUE, G_PARAM_READWRITE));

   gstelement_class= GST_ELEMENT_CLASS(klass);        
   g_signals[SIGNAL_FIRSTFRAME]= g_signal_new( "firstframe-callback",
                                               G_TYPE_FROM_CLASS(gstelement_class),
                                               (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
                                               0,    // class offset
                                               NULL, // accumulator
                                               NULL, // accu data                                               
                                               g_cclosure_marshal_VOID__INT,
                                               G_TYPE_NONE,
                                               1,
                                               G_TYPE_INT );
   g_signals[SIGNAL_UNDERFLOW]= g_signal_new( "vidsink-underflow-callback",
                                              G_TYPE_FROM_CLASS(gstelement_class),
                                              (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
                                              0,    // class offset
                                              NULL, // accumulator
                                              NULL, // accu data                                               
                                              g_cclosure_marshal_VOID__INT,
                                              G_TYPE_NONE,
                                              1,
                                              G_TYPE_INT );
}

gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink )
{
   gboolean result= FALSE;

   sink->soc.vidrend= ISMD_DEV_HANDLE_INVALID;
   sink->soc.vidpproc= ISMD_DEV_HANDLE_INVALID;
   sink->soc.vidsink= ISMD_DEV_HANDLE_INVALID;
   sink->soc.inputPort= ISMD_PORT_HANDLE_INVALID;
   sink->soc.outputPort= ISMD_PORT_HANDLE_INVALID;
   sink->soc.clock= 0;
   sink->soc.clockDevice= ISMD_CLOCK_HANDLE_INVALID;
   sink->soc.baseTime= ISMD_NO_PTS;
   sink->soc.pauseTime= ISMD_NO_PTS;
   sink->soc.underflowEvent= ISMD_EVENT_HANDLE_INVALID;
   sink->soc.quitEvent= ISMD_EVENT_HANDLE_INVALID;
   sink->soc.frameCount= 0;
   sink->soc.sinkReady= true;
   sink->soc.gdlReady= false;
   sink->soc.ismdReady= false;
   sink->soc.hwVideoPlane= GDL_PLANE_ID_UPP_A;
   sink->soc.scaleMode= ISMD_VIDPPROC_SCALING_POLICY_SCALE_TO_FIT;
   sink->soc.enableCCPassthru= TRUE;
   sink->soc.mute= 0;
   sink->soc.deinterlacePolicy= ISMD_VIDPPROC_DI_POLICY_VIDEO;
   sink->soc.streamTimeOffset= DEFAULT_STREAM_TIME_OFFSET;
   sink->soc.useVirtualCoords= true;
   sink->soc.modeWidth= DEFAULT_MODE_WIDTH;
   sink->soc.modeHeight= DEFAULT_MODE_HEIGHT;
   sink->soc.needToClearGfx= false;
   sink->soc.useGfxPath= false;
   sink->soc.contRate= 0;
   sink->soc.firstFrameSignalled= false;
   sink->soc.lastUnderflowTime= -1LL;
   sink->soc.quitMonitorThread= TRUE;
   sink->soc.monitorThread= NULL;
   sink->soc.icegdl= 0;
   result= initIsmdGst( sink );
   
   return result;
}

void gst_westeros_sink_soc_term( GstWesterosSink *sink )
{
   sink->soc.sinkReady= false;
}

void gst_westeros_sink_soc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(object);

   WESTEROS_UNUSED(pspec);

   if ( !sink->soc.sinkReady ) return;

   switch (prop_id) 
   {
      case PROP_VIDEO_PLANE:
      {
         const gchar *name= g_value_get_string(value);
         int len= strlen(name);
         if ( len==5 )
         {
            int plane= -1;
            if ( !strncasecmp( "UPP_A", name, len ) )
            {
               plane= GDL_PLANE_ID_UPP_A;
            }
            else if ( !strncasecmp( "UPP_B", name, len ) )
            {
               plane= GDL_PLANE_ID_UPP_B;
            }
            else if ( !strncasecmp( "UPP_C", name, len ) )
            {
               plane= GDL_PLANE_ID_UPP_C;
            }
            else if ( !strncasecmp( "UPP_D", name, len ) )
            {
               plane= GDL_PLANE_ID_UPP_D;
            }
            if ( plane > 0 )
            {
               GST_INFO_OBJECT(sink, "westeros-sink: video-plane %s (%d)", name, plane);            
               LOCK(sink)
               sink->soc.hwVideoPlane= plane;
               UNLOCK(sink);
            }
         }
         break;
      }
      case PROP_GDL_PLANE:
      {
         const char *name= 0;
         int plane= g_value_get_uint(value);
         switch( plane )
         {
            case GDL_PLANE_ID_UNDEFINED:
               name= "None";
               break;
            case GDL_PLANE_ID_UPP_A:
               name= "UPP_A";
               break;
            case GDL_PLANE_ID_UPP_B:
               name= "UPP_B";
               break;
            case GDL_PLANE_ID_UPP_C:
               name= "UPP_C";
               break;
            case GDL_PLANE_ID_UPP_D:
               name= "UPP_D";
               break;
            default:
               break;
         }
         if ( name )
         {
            GST_INFO_OBJECT(sink, "westeros-sink: gdl-plane %s (%d)", name, plane);
            LOCK(sink)
            sink->soc.hwVideoPlane= plane;
            UNLOCK(sink);
         }
         break;
      }
      case PROP_SCALE_MODE:
      {
         int scaleMode= g_value_get_uint(value);
         switch( scaleMode )
         {
            case ISMD_VIDPPROC_SCALING_POLICY_NO_SCALING:
            case ISMD_VIDPPROC_SCALING_POLICY_SCALE_TO_FIT:
            case ISMD_VIDPPROC_SCALING_POLICY_ZOOM_TO_FIT:
            case ISMD_VIDPPROC_SCALING_POLICY_ZOOM_TO_FILL:
               break;
            default:
               scaleMode= -1;
               break;
         }
         if ( scaleMode >= 0 )
         {
            if ( scaleMode != sink->soc.scaleMode )
            {
               LOCK(sink);
               sink->soc.scaleMode= scaleMode;
               if ( sink->soc.vidsink != ISMD_DEV_HANDLE_INVALID )
               {
                  gst_westeros_sink_soc_update_video_position(sink);
               }
               UNLOCK(sink);
            }
         }
         break;
      }
      case PROP_ENABLE_CC_PASSTHRU:
      {
         gboolean enable= g_value_get_boolean(value);

         LOCK(sink);
         if ( enable != sink->soc.enableCCPassthru )
         {
            sink->soc.enableCCPassthru= g_value_get_boolean(value);
         }
         UNLOCK(sink);
         setupCCPlane(sink);
         break;
      }
      case PROP_MUTE:
      {
         int mute= g_value_get_uint(value);

         LOCK(sink);
         if ( mute != sink->soc.mute )
         {
            sink->soc.mute= mute;
            if ( sink->soc.vidrend != ISMD_DEV_HANDLE_INVALID )
            {
               ismd_vidrend_mute( sink->soc.vidrend,
                                  (sink->soc.mute == 1)
                                  ? ISMD_VIDREND_MUTE_DISPLAY_BLACK_FRAME
                                  : ISMD_VIDREND_MUTE_NONE );
            }
         }
         UNLOCK(sink);
         break;
      }
      case PROP_DEINTERLACING_POLICY:
      {
         int policy= g_value_get_uint(value);

         LOCK(sink);
         if ( policy != sink->soc.deinterlacePolicy )
         {
            sink->soc.deinterlacePolicy= policy;

            if ( sink->soc.vidpproc != ISMD_DEV_HANDLE_INVALID )
            {
               ismd_result_t ismdrc;

               ismdrc= ismd_vidpproc_set_deinterlace_policy( sink->soc.vidpproc,
                                                             (ismd_vidpproc_deinterlace_policy_t)policy );
               if ( ismdrc != ISMD_SUCCESS )
               {
                  GST_ERROR("ismd_vidpproc_set_deinterlace_policy failed: %d", ismdrc );
               }
            }
         }
         UNLOCK(sink);
         break;
      }
      case PROP_STREAM_TIME_OFFSET:
      {
         guint64 streamTimeOffset= g_value_get_uint64(value);

         LOCK(sink);
         sink->soc.streamTimeOffset= streamTimeOffset;
         UNLOCK(sink);
         break;
      }
      case PROP_USE_VIRTUAL_COORDS:
      {
         gboolean useVirtual= g_value_get_boolean(value);

         g_print("westeros-sink: setting use-virtual-coords to %d\n", useVirtual);
         LOCK(sink);
         sink->soc.useVirtualCoords= useVirtual;
         if ( sink->soc.vidsink != ISMD_DEV_HANDLE_INVALID )
         {
            gst_westeros_sink_soc_update_video_position(sink);
         }
         UNLOCK(sink);
         break;
      }
      default:
         G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
         break;
   }
}

void gst_westeros_sink_soc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(object);

   WESTEROS_UNUSED(pspec);

   if ( !sink->soc.sinkReady ) return;

   switch (prop_id) 
   {
      case PROP_VIDEO_PLANE:
      {
         int plane;
         const gchar *name;
         
         LOCK(sink);
         plane= sink->soc.hwVideoPlane;
         UNLOCK(sink);
         
         switch( plane )
         {
            case GDL_PLANE_ID_UNDEFINED:
               name= "None";
               break;
            case GDL_PLANE_ID_UPP_A:
               name= "UPP_A";
               break;
            case GDL_PLANE_ID_UPP_B:
               name= "UPP_B";
               break;
            case GDL_PLANE_ID_UPP_C:
               name= "UPP_C";
               break;
            case GDL_PLANE_ID_UPP_D:
               name= "UPP_D";
               break;
            default:
               name= "UNKNOWN";
               break;
         }
         
         g_value_set_string(value, name);
         break;
      }
      case PROP_GDL_PLANE:
      {
         int plane;

         LOCK(sink);
         plane= sink->soc.hwVideoPlane;
         UNLOCK(sink);

         g_value_set_uint(value, (unsigned int)plane);
         break;
      }
      case PROP_SCALE_MODE:
      {
         int scaleMode;

         LOCK(sink);
         scaleMode= sink->soc.scaleMode;
         UNLOCK(sink);

         g_value_set_uint(value, (unsigned int)scaleMode);
         break;
      }
      case PROP_ENABLE_CC_PASSTHRU:
      {
         gboolean enable;

         LOCK(sink);
         enable= sink->soc.enableCCPassthru;
         UNLOCK(sink);

         g_value_set_boolean(value, enable);
         break;
      }
      case PROP_MUTE:
      {
         int mute;

         LOCK(sink);
         mute= sink->soc.mute;
         UNLOCK(sink);

         g_value_set_uint(value, (unsigned int)mute);
         break;
      }
      case PROP_CURRENT_PTS:
      {
         long long pts;

         LOCK(sink);
         pts= sink->currentPTS;
         UNLOCK(sink);

         g_value_set_ulong(value, pts);
         break;
      }
      case PROP_DEINTERLACING_POLICY:
      {
         int policy;

         LOCK(sink);
         policy= sink->soc.deinterlacePolicy;
         UNLOCK(sink);

         g_value_set_uint(value, (unsigned int)policy);
         break;
      }
      case PROP_STREAM_TIME_OFFSET:
      {
         guint64 offset;

         LOCK(sink);
         offset= sink->soc.streamTimeOffset;
         UNLOCK(sink);

         g_value_set_uint64(value, offset);
         break;
      }
      case PROP_USE_VIRTUAL_COORDS:
      {
         gboolean useVirtual;

         LOCK(sink);
         useVirtual= sink->soc.useVirtualCoords;
         UNLOCK(sink);

         g_value_set_boolean(value, useVirtual);
         break;
      }
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

   if ( (len==9) && !strncmp(interface, "wl_icegdl", len) ) {
      sink->soc.icegdl= (struct wl_icegdl*)wl_registry_bind(registry, id, &wl_icegdl_interface, 1);
      GST_INFO_OBJECT(sink, "westeros-sink: registry: icegdl %p", (void*)sink->soc.icegdl);
      wl_proxy_set_queue((struct wl_proxy*)sink->soc.icegdl, sink->queue);
		wl_icegdl_add_listener(sink->soc.icegdl, &icegdlListener, sink);
		GST_INFO_OBJECT(sink, "westeros-sink: registry: done add icegdl listener");
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

static void buffer_release( void *data, struct wl_buffer *buffer )
{
   GstWesterosSink *sink= (GstWesterosSink*)data;

   --sink->soc.activeBuffers;

   wl_buffer_destroy( buffer );   
}

static struct wl_buffer_listener wl_buffer_listener= 
{
   buffer_release
};

gboolean gst_westeros_sink_soc_null_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   gboolean result= FALSE;
   ismd_result_t ismdrc;
   void *ismdClock;

   WESTEROS_UNUSED(passToDefault);

   ismdrc= ismd_vidrend_open( &sink->soc.vidrend );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("unable to open vidrend: %d", ismdrc);
      goto exit;
   }
   
   if ( sink->soc.registerDeviceHandle )
   {
      sink->soc.registerDeviceHandle( sink->soc.vidrend, ISMD_VIDREND );
   }

   ismdrc= ismd_vidpproc_open( &sink->soc.vidpproc );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("unable to open vidpproc: %d", ismdrc);
      goto exit;
   }
   
   ismdrc= ismd_vidsink_open( &sink->soc.vidsink );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("unable to open vidsink: %d", ismdrc);
      goto exit;
   }
   
   ismdrc= ismd_vidsink_set_smd_handles( sink->soc.vidsink, sink->soc.vidpproc, sink->soc.vidrend );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("failure setting vidsink handles: %d", ismdrc);
      goto exit;
   }
   
   ismdrc= ismd_vidsink_get_input_port( sink->soc.vidsink, &sink->soc.inputPort );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("unable to get vidsink input port: %d", ismdrc);
      goto exit;
   }   

   ismdrc= ismd_vidrend_enable_port_output( sink->soc.vidrend, 0, 1, &sink->soc.outputPort );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("unable to get vidrend output port: %d", ismdrc);
      goto exit;
   }   
   
   ismdrc= ismd_vidrend_get_underflow_event( sink->soc.vidrend, &sink->soc.underflowEvent );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("unable to get vidrend underflow event: %d", ismdrc);
      goto exit;
   }   
   
   ismdrc= ismd_event_alloc( &sink->soc.quitEvent );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("unable to alloc quit event: %d", ismdrc);
      goto exit;
   }

   ismdClock= sink->soc.getDefaultClock();
   sink->soc.clockDevice= *((gint*)(((GstSystemClock*)ismdClock)+1));
   sink->soc.clock= GST_CLOCK(ismdClock);

   ismdrc= ismd_vidsink_set_clock( sink->soc.vidsink, sink->soc.clockDevice );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("failure setting vidsink clock: %d", ismdrc);
      goto exit;
   }

   ismdrc= ismd_vidrend_set_video_plane( sink->soc.vidrend, sink->soc.hwVideoPlane );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("failure setting vidrend video plane: %d", ismdrc);
      goto exit;
   }   
   
   sink->soc.ismdReady= true;

   setupGDL( sink );

   result= TRUE;

exit:
   
   return result;
}

gboolean gst_westeros_sink_soc_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   gboolean result= FALSE;

   WESTEROS_UNUSED(passToDefault);

   gst_westeros_sink_soc_update_video_position(sink);

   result= TRUE;

   return result;
}

gboolean gst_westeros_sink_soc_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);
   
   GST_WARNING_OBJECT(sink, "gst_westeros_sink_soc_paused_to_playing called: vidsink %d", sink->soc.vidsink);
   gst_westeros_sink_soc_start_video(sink);
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   gboolean result= FALSE;
   ismd_result_t ismdrc;
   
   GST_WARNING_OBJECT(sink, "gst_westeros_sink_soc_playing_to_paused called: vidsink %d", sink->soc.vidsink);
   LOCK( sink );
   sink->videoStarted= FALSE;
   UNLOCK( sink );

   *passToDefault= false;

   sink->soc.streamTimeOffset= DEFAULT_STREAM_TIME_OFFSET;

   ismdrc= ismd_clock_get_time( sink->soc.clockDevice, &sink->soc.pauseTime );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("failure getting ismd clock time: %d", ismdrc);
   }   
   
   ismdrc= ismd_vidsink_set_state( sink->soc.vidsink, ISMD_DEV_STATE_PAUSE );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("failure setting vidsink to PAUSE: %d", ismdrc);
      goto exit;
   }
   
   result= TRUE;

exit:
 
   return result;   
}

gboolean gst_westeros_sink_soc_paused_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);

   LOCK( sink );
   sink->soc.ismdReady= false;
   sink->soc.streamTimeOffset= DEFAULT_STREAM_TIME_OFFSET;
   if ( sink->soc.monitorThread )
   {
      sink->soc.quitMonitorThread= TRUE;
      ismd_event_set( sink->soc.quitEvent );
      UNLOCK( sink );
      if ( sink->soc.monitorThread )
      {
         g_thread_join( sink->soc.monitorThread );
      }
      LOCK( sink );
   }
   UNLOCK( sink );

   if ( sink->soc.icegdl )
   {
      wl_icegdl_destroy( sink->soc.icegdl );
      sink->soc.icegdl= 0;
   }

   return TRUE;
}

gboolean gst_westeros_sink_soc_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault )
{
   *passToDefault= false;

   LOCK( sink );
   sink->soc.gdlReady= false;
   if ( sink->soc.quitEvent != ISMD_EVENT_HANDLE_INVALID )
   {
      ismd_event_free( sink->soc.quitEvent );
      sink->soc.quitEvent= ISMD_EVENT_HANDLE_INVALID;
   }
   if ( (sink->soc.vidrend != ISMD_DEV_HANDLE_INVALID) && 
        (sink->soc.outputPort != ISMD_PORT_HANDLE_INVALID) )
   {
      ismd_vidrend_disable_port_output( sink->soc.vidrend );
      sink->soc.outputPort= ISMD_PORT_HANDLE_INVALID;
   }
   if ( sink->soc.vidsink != ISMD_DEV_HANDLE_INVALID )
   {
      ismd_vidsink_close( sink->soc.vidsink );
      sink->soc.vidsink= ISMD_DEV_HANDLE_INVALID;
   }
   if ( sink->soc.vidpproc != ISMD_DEV_HANDLE_INVALID )
   {
      ismd_dev_close( sink->soc.vidpproc );
      sink->soc.vidpproc= ISMD_DEV_HANDLE_INVALID;
   }
   if ( sink->soc.vidrend != ISMD_DEV_HANDLE_INVALID )
   {
      ismd_dev_close( sink->soc.vidrend );
      sink->soc.vidrend= ISMD_DEV_HANDLE_INVALID;
   }
   if ( sink->soc.clockDevice != ISMD_CLOCK_HANDLE_INVALID )
   {
      gst_object_unref( sink->soc.clock );
      sink->soc.clockDevice= ISMD_CLOCK_HANDLE_INVALID;
   }
   sink->videoStarted= FALSE;
   sink->soc.firstFrameSignalled= false;
   sink->soc.lastUnderflowTime= -1LL;

   termGDL(sink);

   UNLOCK( sink );

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
      if (strcmp("video/x-decoded-ismd", mime) == 0)
      {
         result= TRUE;
      }
   }

   return result;   
}

void gst_westeros_sink_soc_set_startPTS( GstWesterosSink *sink, gint64 pts )
{
   GST_WARNING_OBJECT(sink, "gst_westeros_sink_soc_set_startPTS called: pts %lld", pts);
   unsigned int pts45k= (unsigned int)( pts / 2 );
   sink->soc.frameCount= 0;
   sink->soc.firstFrameSignalled= false;
   sink->soc.lastUnderflowTime= -1LL;
   sink->positionSegmentStart= 0LL;
}

static bool is_buffer_in_segment( const GstSegment *segment, GstBuffer *buf )
{
   GstClockTime duration, pts;
   gboolean     buffer_is_valid;
   GstClockTime clip_start;
   GstClockTime clip_stop;

   duration = GST_BUFFER_DURATION ( buf );
   pts      = GST_BUFFER_TIMESTAMP( buf );

   if ( GST_CLOCK_TIME_IS_VALID ( pts ) )
   {
      GST_LOG ( "check for clipping on buffer %p with pts %" GST_TIME_FORMAT " and duration %" GST_TIME_FORMAT,
                buf, GST_TIME_ARGS ( pts ), GST_TIME_ARGS ( duration ) );

      clip_start = clip_stop = pts;
      if ( GST_CLOCK_TIME_IS_VALID ( duration ) )
      {
         clip_stop += duration;
      }

      buffer_is_valid = gst_segment_clip ( segment, GST_FORMAT_TIME,
                                           clip_start, clip_stop,
                                           &clip_start, &clip_stop );

      if ( G_UNLIKELY( !buffer_is_valid) )
      {
         GST_INFO( "buffer %p with pts %" GST_TIME_FORMAT " and duration %" GST_TIME_FORMAT " is not in segment",
                      buf, GST_TIME_ARGS (pts), GST_TIME_ARGS (duration) );
         return FALSE;
      }
      else
      {
         GST_LOG( "buffer was in the segment" );
      }

   }
   else
   {
      GST_WARNING( "buffer clock time is not valid" );
   }

   return TRUE;
}

static void tag_new_segment( GstWesterosSink *sink, ismd_buffer_handle_t ismdBuffer )
{
   ismd_newsegment_tag_t ismdNewSegment;

   if ( sink->currentSegment )
   {
      ismdNewSegment.rate_valid= TRUE;
      ismdNewSegment.requested_rate= GST_TO_ISMD_RATE( sink->currentSegment->rate );
      ismdNewSegment.applied_rate= GST_TO_ISMD_RATE( sink->currentSegment->applied_rate);
      if ( sink->currentSegment->format == GST_FORMAT_TIME )
      {
         ismdNewSegment.start= GST_TO_ISMD_TIME(sink->currentSegment->start);
         ismdNewSegment.stop= GST_TO_ISMD_TIME(sink->currentSegment->stop);
         ismdNewSegment.linear_start= 0;
         ismdNewSegment.segment_position= GST_TO_ISMD_TIME(sink->currentSegment->time);
      }
      else
      {
         ismdNewSegment.start= ISMD_NO_PTS;
         ismdNewSegment.stop= ISMD_NO_PTS;
         ismdNewSegment.linear_start= 0;
         ismdNewSegment.segment_position= ISMD_NO_PTS;
      }

      GST_DEBUG_OBJECT(sink, "tag_new_segment: start %lld stop %lld linear_start %lld segment_position %lld rate %d applied_rate %d",
                         ismdNewSegment.start, ismdNewSegment.stop, ismdNewSegment.linear_start, ismdNewSegment.segment_position,
                         ismdNewSegment.requested_rate, ismdNewSegment.applied_rate );
      ismd_tag_set_newsegment( ismdBuffer, ismdNewSegment );
   }
}

void gst_westeros_sink_soc_render( GstWesterosSink *sink, GstBuffer *buffer )
{
   /* Keep generic sink code ready to act on an EOS event */
   gst_westeros_sink_eos_detected( sink );

   LOCK( sink );
   if ( !is_buffer_in_segment( sink->currentSegment, buffer ) )
   {
      UNLOCK( sink );
      return;
   }
   sink->position= buffer->pts;
   UNLOCK( sink );

   ismd_buffer_handle_t ismdBuffer;
   
   if ( sink->soc.needToClearGfx )
   {
      if ( sink->soc.clearGfxCount-- <= 0 )
      {
         /* Send a null buffer to remove any previous surface */
         wl_surface_attach( sink->surface, 0, sink->windowX, sink->windowY );
         wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
         wl_surface_commit( sink->surface );
         wl_display_flush(sink->display);
         wl_display_dispatch_queue_pending(sink->display, sink->queue);      

         sink->soc.needToClearGfx= false;
      }      
   }
   
   if ( sink->soc.ismdReady )
   {
      if ( sink->soc.isIsmdBuffer(buffer) )
      {
         ismdBuffer= sink->soc.getIsmdBufferHandle(buffer);
         if ( ismdBuffer )
         {
            bool windowChange;
            LOCK(sink);
            windowChange= sink->windowChange;
            UNLOCK(sink);
            if ( windowChange )
            {
               gst_westeros_sink_soc_update_video_position( sink );
            }

            if ( !sink->flushStarted )
            {
               processFrame( sink, ismdBuffer );
            }
         }
      }
   }
}

void gst_westeros_sink_soc_flush( GstWesterosSink *sink )
{
   ismd_result_t ismdrc;
      
   GST_WARNING_OBJECT(sink, "gst_westeros_sink_soc_flush called: vidsink %d", sink->soc.vidsink);

   if ( sink->soc.clearBaseTime )
   {
      sink->soc.clearBaseTime( sink->soc.clock );
   }

   if ( sink->soc.vidsink != ISMD_DEV_HANDLE_INVALID )
   {
      sink->soc.baseTime= ISMD_NO_PTS;
      sink->soc.pauseTime= ISMD_NO_PTS;
      
      if ( sink->videoStarted )
      {
         ismdrc= ismd_vidsink_set_state( sink->soc.vidsink, ISMD_DEV_STATE_PAUSE );
         if ( ismdrc != ISMD_SUCCESS )
         {
            GST_ERROR("failure setting vidsink to PAUSE: %d", ismdrc);
         }
         GST_WARNING_OBJECT(sink, "gst_westeros_sink_soc_flush called: vidsink %d now PAUSE", sink->soc.vidsink);
      }
      ismdrc= ismd_vidsink_flush( sink->soc.vidsink );
   }
   sink->soc.firstFrameSignalled= false;
   sink->soc.lastUnderflowTime= -1LL;
   LOCK(sink);
   sink->soc.frameCount= 0;
   UNLOCK(sink);
}

gboolean gst_westeros_sink_soc_start_video( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   ismd_result_t ismdrc;
   long long now;
   
   GST_WARNING_OBJECT(sink, "gst_westeros_sink_soc_start_video called: vidsink %d", sink->soc.vidsink);

   if ( sink->soc.baseTime == ISMD_NO_PTS )
   {
      if ( sink->soc.markBaseTime )
      {
         GstClockTime bt= sink->soc.markBaseTime( sink->soc.clock, sink->soc.streamTimeOffset );
         sink->soc.baseTime= GST_TO_ISMD_TIME(bt);
      }
      else
      {
         ismdrc= ismd_clock_get_time( sink->soc.clockDevice, &sink->soc.baseTime );
         if ( ismdrc != ISMD_SUCCESS )
         {
            GST_ERROR("failure getting ismd clock time for baseTime: %d", ismdrc);
         }   
      }
   }
   if ( sink->soc.pauseTime != ISMD_NO_PTS )
   {
      ismd_time_t currTime;
      ismdrc= ismd_clock_get_time( sink->soc.clockDevice, &currTime );
      if ( ismdrc != ISMD_SUCCESS )
      {
         GST_ERROR("failure getting ismd clock time to adjust baseTime: %d", ismdrc);
      } 
      sink->soc.baseTime += (currTime - sink->soc.pauseTime);
      sink->soc.pauseTime= ISMD_NO_PTS;
   }
   if ( !sink->soc.markBaseTime )
   {
      if ( sink->soc.streamTimeOffset != GST_CLOCK_TIME_NONE )
      {
         sink->soc.baseTime += ((sink->soc.streamTimeOffset * 90LL)/GST_MSECOND);
      }
   }
   ismd_vidsink_set_base_time( sink->soc.vidsink, sink->soc.baseTime );
   
   ismdrc= ismd_vidsink_set_state( sink->soc.vidsink, ISMD_DEV_STATE_PLAY );
   if ( ismdrc != ISMD_SUCCESS )
   {
      GST_ERROR("failure setting vidsink to PLAY: %d", ismdrc);
      goto exit;
   }

   if ( sink->soc.vidrend != ISMD_DEV_HANDLE_INVALID )
   {
      ismd_vidrend_mute( sink->soc.vidrend,
                         (sink->soc.mute == 1)
                         ? ISMD_VIDREND_MUTE_DISPLAY_BLACK_FRAME
                         : ISMD_VIDREND_MUTE_NONE );
   }

   sink->soc.quitMonitorThread= FALSE;
   if ( sink->soc.monitorThread == NULL ) 
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: starting westeros_sink_monitor thread");
      sink->soc.monitorThread= g_thread_new("westeros_sink_monitor", monitorThread, sink);        
   }

   LOCK( sink );
   now= getCurrentTimeMillis();
   sink->soc.firstFrameTime= now-((sink->soc.frameCount*sink->soc.contRate)/90);
   sink->videoStarted= TRUE;
   UNLOCK( sink );
   
   result= TRUE;

exit:
 
   return result;   
}

void gst_westeros_sink_soc_eos_event( GstWesterosSink *sink )
{
   WESTEROS_UNUSED(sink);
}

static void processFrame( GstWesterosSink *sink, ismd_buffer_handle_t ismdBuffer )
{
   ismd_result_t ismdrc;
   ismd_buffer_descriptor_t desc;
   ismd_frame_attributes_t *attr= 0;
   struct wl_buffer *wlBuff= 0;
   int frameWidth, frameHeight, frameStride;
   long long now;
   long long targetFrameTime;
   int frameDelay;
   bool flushing;
   int segmentNumber, segmentNumberNew;

   if ( !sink->soc.gdlReady ) return;

   LOCK(sink);
   if ( sink->soc.ismdReady )
   {
      ismdrc= ismd_buffer_read_desc(ismdBuffer, &desc);
      if (ismdrc == ISMD_SUCCESS)
      {
         if ( desc.buffer_type == ISMD_BUFFER_TYPE_VIDEO_FRAME )
         {
            if ( !sink->soc.firstFrameSignalled )
            {
               UNLOCK(sink);
               g_print("Emitting Video FIRSTFRAME signal\n");
               g_signal_emit( G_OBJECT(sink), g_signals[SIGNAL_FIRSTFRAME], 0, SIGNAL_FIRSTFRAME );
               sink->soc.firstFrameSignalled= true;
               LOCK(sink);
            }

            attr= (ismd_frame_attributes_t *)desc.attributes;
         
            now= getCurrentTimeMillis();

            sink->currentPTS= (gint64)attr->local_pts;
            if ( sink->soc.frameCount == 0 )
            {
               sink->firstPTS= sink->currentPTS;
               sink->soc.firstFrameTime= now;
               GST_WARNING_OBJECT(sink, "set firstPTS: %lld", sink->firstPTS);

               tag_new_segment( sink, ismdBuffer );
            }

            sink->soc.contRate= attr->local_cont_rate;
            targetFrameTime= sink->soc.firstFrameTime+((sink->soc.frameCount*attr->local_cont_rate)/90);
            frameDelay= targetFrameTime-now;
            if ( sink->soc.useGfxPath )
            {
               UNLOCK(sink);
               if ( frameDelay > 0 )
               {
                  usleep( frameDelay*1000 );
               }
               LOCK(sink);
            }

            if ( sink->soc.ismdReady )
            {            
               ++sink->soc.frameCount;
               
               GST_LOG( "%lld: startPTS %lld firstPTS %lld currPTS %lld (%lld) frame %d cont_rate: %u local_cont_rate: %u time_code: %08X fmdcd %d fmdfi %d",
                        (now-sink->soc.firstFrameTime), sink->startPTS, sink->firstPTS, sink->currentPTS, attr->original_pts, sink->soc.frameCount,
                        attr->cont_rate, attr->local_cont_rate, attr->time_code, attr->fmd_cadence_type, attr->fmd_frame_index );

               frameStride= attr->scanline_stride;
               if ( frameStride == 0 )
               {
                  frameStride= SYSTEM_STRIDE;
               }
               frameWidth= attr->cont_size.width;
               frameHeight= attr->cont_size.height;

               ismd_buffer_add_reference( ismdBuffer );

               if ( sink->soc.useGfxPath )
               {
                  if ( sink->soc.icegdl )
                  {
                     wlBuff= wl_icegdl_create_planar_buffer( sink->soc.icegdl,
                                                             ismdBuffer,
                                                             sink->windowX,
                                                             sink->windowY,
                                                             sink->windowWidth,
                                                             sink->windowHeight,
                                                             frameStride );
                     if ( wlBuff )
                     {
                        ++sink->soc.activeBuffers;
                        wl_buffer_add_listener( wlBuff, &wl_buffer_listener, sink );

                        wl_surface_attach( sink->surface, wlBuff, 0, 0 );
                        wl_surface_damage( sink->surface, 0, 0, frameWidth, frameHeight);
                        wl_surface_commit( sink->surface );

                        wl_display_flush( sink->display );
                     }
                  }
               }

               ismd_result_t ismdrc = ismd_port_write( sink->soc.inputPort, ismdBuffer );
               if ( ismdrc != ISMD_SUCCESS )
               {
                  segmentNumber= sink->segmentNumber;

                  while ( ismdrc == ISMD_ERROR_NO_SPACE_AVAILABLE )
                  {
                     UNLOCK(sink);
                     usleep(1000);
                     LOCK(sink);

                     segmentNumberNew= sink->segmentNumber;
                     flushing= sink->flushStarted;

                     if ( flushing || !sink->soc.ismdReady || (segmentNumber != segmentNumberNew) )
                     {
                        ismd_buffer_dereference( ismdBuffer );
                        ismdrc= ISMD_SUCCESS;
                        break;
                     }

                     ismdrc = ismd_port_write( sink->soc.inputPort, ismdBuffer );
                  }

                  if ( ismdrc != ISMD_SUCCESS )
                  {
                     GST_ERROR("write buffer 0x%x to port %d rc %d", ismdBuffer, sink->soc.inputPort, ismdrc);
                     ismd_buffer_dereference( ismdBuffer );
                  }
               }
            }
         }
      }
   }
   UNLOCK( sink );

   if ( sink->display )
   {
      wl_display_roundtrip_queue(sink->display,sink->queue);
   }
}

static gpointer monitorThread(gpointer data) 
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   ismd_result_t ismdrc;
   ismd_event_list_t events;
   ismd_event_t event;
   
   GST_DEBUG_OBJECT(sink, "monitorThread: enter");

   events[0]= sink->soc.underflowEvent;
   events[1]= sink->soc.quitEvent;
   while( !sink->soc.quitMonitorThread )
   {
      ismdrc= ismd_event_wait_multiple( events, 
                                        2,
                                        ISMD_TIMEOUT_NONE,
                                        &event );
      if ( ismdrc == ISMD_SUCCESS )
      {
         if ( event == sink->soc.underflowEvent )
         {
            long long now= getCurrentTimeMillis();
            long long diff= UNDERFLOW_INTERVAL;
            if ( sink->soc.lastUnderflowTime != -1LL )
            {
               diff= now-sink->soc.lastUnderflowTime;
            }
            if ( diff >= UNDERFLOW_INTERVAL )
            {
               g_print("Emitting Video UNDERFLOW SIGNAL signal\n");
               g_signal_emit( G_OBJECT(sink), g_signals[SIGNAL_UNDERFLOW], 0, SIGNAL_UNDERFLOW );
               sink->soc.lastUnderflowTime= now;
            }
            ismdrc= ismd_event_reset( sink->soc.underflowEvent );
            if ( ismdrc != ISMD_SUCCESS )
            {
               GST_ERROR( "ismd_event_reset for underflowEvent failed: %d", ismdrc );
            }
         }
         ismd_event_acknowledge (event);
      }
   }
   
   GST_DEBUG_OBJECT(sink, "monitorThread: exit");
   
   LOCK( sink );
   sink->soc.monitorThread= NULL;
   UNLOCK( sink );
   
   g_thread_exit(NULL);
   
   return NULL;
}

static void setupGDL( GstWesterosSink *sink )
{
   gdl_ret_t gdlrc;
   gdl_display_info_t displayInfo;
   gdl_rectangle_t rect;
   int modeWidth, modeHeight;
   
   gdlrc= gdl_init(0);
   if ( gdlrc == GDL_SUCCESS )
   {
      memset( &displayInfo, 0, sizeof(displayInfo) );
      
      gdlrc= gdl_get_display_info( GDL_DISPLAY_ID_0, &displayInfo );
      if ( gdlrc == GDL_SUCCESS )
      {
         modeWidth= displayInfo.tvmode.width;
         modeHeight= displayInfo.tvmode.height;
         printf("westeros-sink-soc: plane %d mode %dx%d\n", sink->soc.hwVideoPlane, modeWidth, modeHeight);

         LOCK(sink);
         sink->soc.modeWidth= modeWidth;
         sink->soc.modeHeight= modeHeight;
         sink->windowWidth= DEFAULT_MODE_WIDTH;
         sink->windowHeight= DEFAULT_MODE_HEIGHT;
         UNLOCK(sink);
         
         gdlrc= gdl_plane_config_begin( (gdl_plane_id_t)sink->soc.hwVideoPlane );
         if ( gdlrc == GDL_SUCCESS )
         {
            rect.origin.x= 0;
            rect.origin.y= 0;
            rect.width= modeWidth;
            rect.height= modeHeight;

            gdlrc= gdl_plane_set_uint( GDL_PLANE_ALPHA_GLOBAL, 255 );
            if ( gdlrc != GDL_SUCCESS )
            {
               GST_ERROR("error setting GDL_PLANE_ALPHA_GLOBAL to 255: %d", gdlrc );
            }
            
            gdlrc= gdl_plane_set_attr( GDL_PLANE_VID_DST_RECT, &rect );
            if ( gdlrc != GDL_SUCCESS )
            {
               GST_ERROR("error setting GDL_PLANE_VID_DST_RECT to (0,0,%d,%d): %d", rect.width, rect.height, gdlrc );
            }
            
            gdlrc= gdl_plane_set_attr( GDL_PLANE_VID_SRC_RECT, &rect );
            if ( gdlrc != GDL_SUCCESS )
            {
               GST_ERROR("error setting GDL_PLANE_VID_SRC_RECT to (0,0,%d,%d): %d", rect.width, rect.height, gdlrc );
            }

            gdlrc= gdl_plane_set_uint( GDL_PLANE_VID_MISMATCH_POLICY, GDL_VID_POLICY_CONSTRAIN );
            if ( gdlrc != GDL_SUCCESS )
            {
               GST_ERROR("error setting GDL_PLANE_VID_MISMATCH policy to GDL_VID_POLICY_CONSTRAIN: %d", gdlrc );
            }
            
            gdlrc= gdl_plane_config_end( GDL_FALSE );
            if ( gdlrc != GDL_SUCCESS )
            {
               GST_ERROR("gdl_plane_config_end error: %d", gdlrc);
               termGDL(sink);
            }
            else
            {
               sink->soc.gdlReady= true;
            }
         }
         else
         {
            GST_ERROR("gdl_plane_config_begin error: %d", gdlrc);
         }         
      }
      else
      {
         GST_ERROR("unable to get display info: %d", gdlrc);
      }      
   }
   else
   {
      GST_ERROR("gdl_init error: %d", gdlrc);
   }
}

static void termGDL( GstWesterosSink *sink )
{
   if ( sink->soc.hwVideoPlane != GDL_PLANE_ID_UNDEFINED )
   {
      gdl_plane_reset( sink->soc.hwVideoPlane );
   }
   gdl_close();
}

static void setupCCPlane( GstWesterosSink *sink )
{
   gdl_ret_t gdlrc= GDL_SUCCESS;
   gdl_boolean_t enable;

   if ( !sink->soc.gdlReady )
   {
      return;
   }

   if ( sink->soc.hwVideoPlane != GDL_PLANE_ID_UNDEFINED )
   {
      enable= (sink->soc.enableCCPassthru ? GDL_TRUE : GDL_FALSE);

      gdlrc= gdl_port_set_attr( GDL_PD_ID_INTTVENC_COMPONENT,
                                GDL_PD_ATTR_ID_CC,
                                &enable );
      if ( gdlrc != GDL_SUCCESS )
      {
         GST_ERROR("gdl_port_set_attr failed for GDL_PD_ID_INTTVENC_COMPONENT GDL_PD_ATTR_ID_CC");
      }

      gdlrc= gdl_port_set_attr( GDL_PD_ID_INTTVENC,
                                GDL_PD_ATTR_ID_CC,
                                &enable );
      if ( gdlrc != GDL_SUCCESS )
      {
         GST_ERROR("gdl_port_set_attr failed for GDL_PD_ID_INTTVENC GDL_PD_ATTR_ID_CC");
      }

      if ( sink->soc.enableCCPassthru )
      {
         gdlrc= gdl_closed_caption_source( sink->soc.hwVideoPlane );
         if ( gdlrc != GDL_SUCCESS )
         {
            GST_ERROR("gdl_closed_capture_source with plane %d faild", sink->soc.hwVideoPlane);
         }
      }
   }
}

void gst_westeros_sink_soc_set_video_path( GstWesterosSink *sink, bool useGfxPath )
{
   LOCK(sink);
   if ( useGfxPath && !sink->soc.useGfxPath )
   {
      sink->soc.useGfxPath= true;
   }
   else if ( !useGfxPath && sink->soc.useGfxPath )
   {
      sink->soc.useGfxPath= false;

      gst_westeros_sink_soc_update_video_position(sink);

      sink->soc.needToClearGfx= true;
      sink->soc.clearGfxCount= 3;
   }
   UNLOCK(sink);
}

void gst_westeros_sink_soc_update_video_position( GstWesterosSink *sink )
{
   ismd_result_t ismdrc;
   ismd_vidsink_scale_params_t scale;
   int wx, wy, ww, wh;
   int dwx, dwy, dww, dwh;
   int crop_hoff, crop_voff, crop_width, crop_height;
   int modeWidth, modeHeight;
   gdl_ret_t gdlrc;
   gdl_rectangle_t rect;
   gdl_display_info_t displayInfo;
   bool useGfxPath;

   if ( !sink->soc.gdlReady )
   {
      return;
   }
   
   wx= sink->windowX;
   wy= sink->windowY;
   ww= sink->windowWidth;
   wh= sink->windowHeight;
   modeWidth= sink->soc.modeWidth;
   modeHeight= sink->soc.modeHeight;
   sink->windowChange= false;
   useGfxPath= sink->soc.useGfxPath;

   if ( !useGfxPath )
   {
      memset( &displayInfo, 0, sizeof(displayInfo) );

      /*
       * Update current mode info
       */
      gdlrc= gdl_get_display_info( GDL_DISPLAY_ID_0, &displayInfo );
      if ( gdlrc == GDL_SUCCESS )
      {
         modeWidth= displayInfo.tvmode.width;
         modeHeight= displayInfo.tvmode.height;
         sink->soc.modeWidth= modeWidth;
         sink->soc.modeHeight= modeHeight;
      }

      /*
       * Apply any compositor transform to our window rect
       */
      double modeScaleX= 1.0;
      double modeScaleY= 1.0;
      if ( sink->soc.useVirtualCoords )
      {
         modeScaleX= (double)modeWidth/((double)DEFAULT_MODE_WIDTH);
         modeScaleY= (double)modeHeight/((double)DEFAULT_MODE_HEIGHT);
      }
      wx= (int)(((wx*sink->scaleXNum*modeScaleX)/sink->scaleXDenom) + sink->transX*modeScaleX);
      wy= (int)(((wy*sink->scaleYNum*modeScaleY)/sink->scaleYDenom) + sink->transY*modeScaleY);
      ww= (int)((ww*sink->scaleXNum*modeScaleX)/sink->scaleXDenom);
      wh= (int)((wh*sink->scaleYNum*modeScaleY)/sink->scaleYDenom);
      if ( !sink->windowSizeOverride )
      {
         double sizeXFactor= ((double)sink->outputWidth)/DEFAULT_WINDOW_WIDTH;
         double sizeYFactor= ((double)sink->outputHeight)/DEFAULT_WINDOW_HEIGHT;
         ww *= sizeXFactor;
         wh *= sizeYFactor;
      }

      /*
       * Use vidsink to perform scaling and use GDL to do cropping.  Scale the decoded frame
       * to the width and height specified by our window rectangle using the scaling policy
       * (currently zoom to fit) with top left of (0,0).  Then use GDL to crop as requried by the
       * x,y coordinates of our window rect to crop to just the porion of the decoded frame that
       * is visible on screen.  For this we set the GDL vid size mismatch policy to GDL_VID_POLICY_CONSTRAIN.
       * We set GDL_PLANE_VID_SRC_RECT (whose origin is the top left of the decoded frame) to the portion
       * of the frame that is on screen, then set GDL_PLANE_GID_DST_RECT (origin top left of UPP plane 
       * being used for video display) to position this visible portion appropriately.
       */
      crop_hoff= 0;
      crop_voff= 0;
      if ( (ww <= 0) || (ww > modeWidth) ) ww= modeWidth;
      if ( (wh <= 0) || (wh > modeHeight) ) wh= modeHeight;
      dwx= wx;
      dwy= wy;
      dww= ww;
      dwh= wh;
      if ( dwx < 0 )
      {
         crop_hoff= -wx;
         dww += wx;
         dwx= 0;
      }
      if ( dwy < 0 )
      {
         crop_voff= -wy;
         dwh += dwy;
         dwy= 0;
      }
      if ( dwy & 1 ) dwy += 1;
      if ( dwh & 1 ) dwh -= 1;
      if ( dwx+dww > modeWidth )
      {
         dww= ((modeWidth-dwx)&~3);
      }
      if ( dwy+dwh > modeHeight )
      {
         dwh= modeHeight-dwy;
      }
      crop_width= dww;
      crop_height= dwh;
      
      memset( &scale, 0, sizeof(scale) );
      scale.crop_enable= false;
      scale.crop_window.h_offset= crop_hoff;
      scale.crop_window.v_offset= crop_voff;
      scale.crop_window.width= crop_width;
      scale.crop_window.height= crop_height;
      scale.dest_window.x= 0;
      scale.dest_window.y= 0;
      scale.dest_window.width= ww; 
      scale.dest_window.height= wh;
      scale.aspect_ratio.numerator= 1; 
      scale.aspect_ratio.denominator= 1; 
      scale.scaling_policy= (ismd_vidpproc_scaling_policy_t)sink->soc.scaleMode;
      GST_INFO_OBJECT(sink, "crop_enable: %d h_offset %d v_offset %d crop_w %d crop_h %d, dest(%d, %d, %d, %d)",
         scale.crop_enable,
         scale.crop_window.h_offset,
         scale.crop_window.v_offset,
         scale.crop_window.width,
         scale.crop_window.height,
         scale.dest_window.x,
         scale.dest_window.y,
         scale.dest_window.width,
         scale.dest_window.height );
      
      ismdrc= ismd_vidsink_set_global_scaling_params( sink->soc.vidsink, scale );
      if ( ismdrc != ISMD_SUCCESS )
      {
         GST_ERROR( "ismd_vidsink_set_global_scaling_params error: %d", ismdrc );
      }

      gdlrc= gdl_plane_config_begin( (gdl_plane_id_t)sink->soc.hwVideoPlane );
      if ( gdlrc == GDL_SUCCESS )
      {
         rect.origin.x= crop_hoff;
         rect.origin.y= crop_voff;
         rect.width= crop_width;
         rect.height= crop_height;

         gdlrc= gdl_plane_set_attr( GDL_PLANE_VID_SRC_RECT, &rect );
         if ( gdlrc != GDL_SUCCESS )
         {
            GST_ERROR("error setting GDL_PLANE_VID_SRC_RECT to (%d,%d,%d,%d): %d", 
                      rect.origin.x, rect.origin.y, rect.width, rect.height, gdlrc );
         }

         rect.origin.x= dwx;
         rect.origin.y= dwy;
         rect.width= dww;
         rect.height= dwh;
         gdlrc= gdl_plane_set_attr( GDL_PLANE_VID_DST_RECT, &rect );
         if ( gdlrc != GDL_SUCCESS )
         {
            GST_ERROR("error setting GDL_PLANE_VID_DST_RECT to (%d,%d,%d,%d): %d", 
                      rect.origin.x, rect.origin.y, rect.width, rect.height, gdlrc );
         }

         gdlrc= gdl_plane_config_end( GDL_FALSE );
         if ( gdlrc != GDL_SUCCESS )
         {
            GST_ERROR("gdl_plane_config_end error: %d", gdlrc);
         }
      }
      else
      {
         GST_ERROR("gdl_plane_config_begin error: %d", gdlrc);
      }

      // Send a buffer to compositor to update hole punch geometry
      if ( sink->soc.icegdl )
      {
         struct wl_buffer *wlBuff;
         
         wlBuff= wl_icegdl_create_planar_buffer( sink->soc.icegdl, 
                                                 0,
                                                 sink->windowX,
                                                 sink->windowY,
                                                 sink->windowWidth,
                                                 sink->windowHeight,
                                                 sink->windowWidth*4 );
         if ( wlBuff )
         {
            wl_surface_attach( sink->surface, wlBuff, 0, 0 );
            wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight);
            wl_surface_commit( sink->surface );
            wl_display_flush( sink->display );
         }
      }
   }
}

gboolean gst_westeros_sink_soc_query( GstWesterosSink *sink, GstQuery *query )
{
   gboolean result = FALSE;

   // No supported queries

   return result;
}

