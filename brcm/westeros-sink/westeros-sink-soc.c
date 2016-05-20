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
#include <sys/time.h>

#include "westeros-sink.h"

#include "bmedia_types.h"

#define WESTEROS_UNUSED(x) ((void)(x))

#define FRAME_POLL_TIME (8000)
#define EOS_DETECT_DELAY (500000)
#define EOS_DETECT_DELAY_AT_START (10000000)

static void freeCaptureSurfaces( GstWesterosSink *sink );
static gboolean allocCaptureSurfaces( GstWesterosSink *sink );
static gboolean queryPeerHandles(GstWesterosSink *sink);
static gpointer captureThread(gpointer data);
static void setVideoPath( GstWesterosSink *sink, bool useGfxPath );
static void processFrame( GstWesterosSink *sink );
static void updateVideoStatus( GstWesterosSink *sink );
static void updateVideoPosition( GstWesterosSink *sink );
static NEXUS_VideoCodec convertVideoCodecToNexus(bvideo_codec codec);
static long long getCurrentTimeMillis(void);

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

static void vpcVideoPathChange(void *data,
                               struct wl_vpc_surface *wl_vpc_surface,
                               uint32_t new_pathway )
{
   WESTEROS_UNUSED(wl_vpc_surface);
   GstWesterosSink *sink= (GstWesterosSink*)data;
   printf("westeros-sink-soc: new pathway: %d\n", new_pathway);
   setVideoPath( sink, (new_pathway == WL_VPC_SURFACE_PATHWAY_GRAPHICS) );
}                               

static void vpcVideoXformChange(void *data,
                                struct wl_vpc_surface *wl_vpc_surface,
                                int32_t x_translation,
                                int32_t y_translation,
                                uint32_t x_scale_num,
                                uint32_t x_scale_denom,
                                uint32_t y_scale_num,
                                uint32_t y_scale_denom)
{                                
   WESTEROS_UNUSED(wl_vpc_surface);
   GstWesterosSink *sink= (GstWesterosSink*)data;
      
   sink->soc.transX= x_translation;
   sink->soc.transY= y_translation;
   if ( x_scale_denom )
   {
      sink->soc.scaleXNum= x_scale_num;
      sink->soc.scaleXDenom= x_scale_denom;
   }
   if ( y_scale_denom )
   {
      sink->soc.scaleYNum= y_scale_num;
      sink->soc.scaleYDenom= y_scale_denom;
   }
   
   LOCK( sink );
   updateVideoPosition( sink );
   UNLOCK( sink );
}

static const struct wl_vpc_surface_listener vpcListener= {
   vpcVideoPathChange,
   vpcVideoXformChange
};

gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   NEXUS_Error rc;
   NxClient_AllocSettings allocSettings;

   sink->soc.captureWidth= -1;
   sink->soc.captureHeight= -1;
   for( int i= 0; i < NUM_CAPTURE_SURFACES; ++i )
   {
      sink->soc.captureSurface[i]= NULL;
   }
   sink->soc.transX= 0;
   sink->soc.transY= 0;
   sink->soc.scaleXNum= 1;
   sink->soc.scaleXDenom= 1;
   sink->soc.scaleYNum= 1;
   sink->soc.scaleYDenom= 1;
   sink->soc.quitCaptureThread= TRUE;
   sink->soc.captureThread= NULL;
   sink->soc.captureCount= 0;
   sink->soc.frameCount= 0;
   sink->soc.noFrameCount= 0;
   sink->soc.sb= 0;
   sink->soc.activeBuffers= 0;
   sink->soc.vpc= 0;
   sink->soc.vpcSurface= 0;
   sink->soc.captureEnabled= FALSE;
   
   rc= NxClient_Join(NULL);
   if ( rc == NEXUS_SUCCESS )
   {
      NxClient_GetDefaultAllocSettings(&allocSettings);
      allocSettings.surfaceClient= 1;
      allocSettings.simpleVideoDecoder= 1;
      rc= NxClient_Alloc(&allocSettings, &sink->soc.allocSurface);
      if ( rc == NEXUS_SUCCESS )
      {
         sink->soc.surfaceClientId= sink->soc.allocSurface.surfaceClient[0].id;    
         sink->soc.surfaceClient= NEXUS_SurfaceClient_Acquire(sink->soc.surfaceClientId);
         sink->soc.videoWindow= NEXUS_SurfaceClient_AcquireVideoWindow( sink->soc.surfaceClient, 0);
         sink->soc.videoDecoderId= sink->soc.allocSurface.simpleVideoDecoder[0].id;
         sink->soc.videoDecoder= NEXUS_SimpleVideoDecoder_Acquire( sink->soc.videoDecoderId );

         sink->soc.stcChannel= NULL;
         sink->soc.videoPidChannel= NULL;
         sink->soc.codec= bvideo_codec_unknown;
         
         result= TRUE;
      }
      else
      {
         GST_ERROR("gst_westeros_sink_soc_init: NxClient_Alloc failed %d\n", rc);
      }
   }
   else
   {
      GST_ERROR("gst_westeros_sink_soc_init: NxClient_Join failed %d\n", rc);
   }
   
   return result;
}

void gst_westeros_sink_soc_term( GstWesterosSink *sink )
{
   if ( sink->soc.vpc )
   {
      wl_vpc_destroy( sink->soc.vpc );
      sink->soc.vpc= 0;
   }
   
   if ( sink->soc.sb )
   {
      wl_sb_destroy( sink->soc.sb );
      sink->soc.sb= 0;
   }

   freeCaptureSurfaces(sink);

   NxClient_Disconnect(sink->soc.connectId);
   NEXUS_SimpleVideoDecoder_Release(sink->soc.videoDecoder);
   NEXUS_SurfaceClient_ReleaseVideoWindow(sink->soc.videoWindow);
   NEXUS_SurfaceClient_Release(sink->soc.surfaceClient);
   NxClient_Free(&sink->soc.allocSurface);
   NxClient_Uninit(); 
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
   }
   else
   if ((len==6) && (strncmp(interface, "wl_vpc", len) ==0))
   {
      sink->soc.vpc= (struct wl_vpc*)wl_registry_bind(registry, id, &wl_vpc_interface, 1);
      printf("westeros-sink-soc: registry: vpc %p\n", (void*)sink->soc.vpc);
      wl_proxy_set_queue((struct wl_proxy*)sink->soc.vpc, sink->queue);
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
   
   NEXUS_VideoDecoderSettings settings;
   NEXUS_VideoDecoderExtendedSettings ext_settings;
   NEXUS_SimpleVideoDecoder_GetSettings(sink->soc.videoDecoder, &settings);
   NEXUS_SimpleVideoDecoder_GetExtendedSettings(sink->soc.videoDecoder, &ext_settings);

   // Don't enable zeroDelayOutputMode since this combined with 
   // NEXUS_VideoDecoderTimestampMode_eDisplay will cause the capture
   // to omit all out of order frames (ie. all B-Frames)
   settings.channelChangeMode= NEXUS_VideoDecoder_ChannelChangeMode_eMuteUntilFirstPicture;
   ext_settings.dataReadyCallback.callback= NULL;
   ext_settings.dataReadyCallback.context= NULL;
   ext_settings.zeroDelayOutputMode= false;
   ext_settings.treatIFrameAsRap= true;
   ext_settings.ignoreNumReorderFramesEqZero= true;

   NEXUS_SimpleVideoDecoder_SetSettings(sink->soc.videoDecoder, &settings);
   NEXUS_SimpleVideoDecoder_SetExtendedSettings(sink->soc.videoDecoder, &ext_settings);
   
   if ( sink->soc.vpc && sink->surface )
   {
      sink->soc.vpcSurface= wl_vpc_get_vpc_surface( sink->soc.vpc, sink->surface );
      if ( sink->soc.vpcSurface )
      {
         wl_vpc_surface_add_listener( sink->soc.vpcSurface, &vpcListener, sink );
         wl_proxy_set_queue((struct wl_proxy*)sink->soc.vpcSurface, sink->queue);
         wl_display_flush( sink->display );
         printf("westeros-sink-soc: null_to_ready: done add vpcSurface listener\n");
         
         result= TRUE;
      }
      else
      {
         GST_ERROR("gst_westeros_sink_soc_null_to_ready: failed to create vpcSurface\n");
      }
   }
   else
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: can't create vpc surface: vpc %p surface %p\n",
                sink->soc.vpc, sink->surface);
   }
   
   return result;
}

gboolean gst_westeros_sink_soc_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);
   NEXUS_Error rc;
   
   queryPeerHandles(sink);

   if ( sink->soc.stcChannel )
   {
      rc= NEXUS_SimpleStcChannel_Freeze(sink->soc.stcChannel, TRUE);
      BDBG_ASSERT(!rc);
   }
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);
   queryPeerHandles(sink);	

   if( (sink->soc.stcChannel != NULL) && (sink->soc.codec != bvideo_codec_unknown) )
   {
      LOCK( sink );
      if ( !sink->soc.captureThread )
      {
         gst_westeros_sink_soc_start_video( sink );
      }
      else
      {
         sink->videoStarted= TRUE;
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
   LOCK( sink );
   sink->videoStarted= FALSE;
   UNLOCK( sink );
   
   *passToDefault= false;
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_paused_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);
   LOCK( sink );
   if ( sink->soc.captureThread )
   {
      sink->soc.quitCaptureThread= TRUE;
      UNLOCK( sink );
      g_thread_join( sink->soc.captureThread );  
      LOCK( sink );
   }

   if ( sink->videoStarted )
   {
      NEXUS_SimpleVideoDecoder_Stop(sink->soc.videoDecoder);
   }
   
   sink->videoStarted= FALSE;
   UNLOCK( sink );
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(sink);

   if ( sink->soc.vpcSurface )
   {
      wl_vpc_surface_destroy( sink->soc.vpcSurface );
   }

   *passToDefault= false;
   
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
   }

   return result;   
}

void gst_westeros_sink_soc_set_startPTS( GstWesterosSink *sink, gint64 pts )
{
   unsigned int pts45k= (unsigned int)( pts / 2 );
   NEXUS_SimpleVideoDecoder_SetStartPts( sink->soc.videoDecoder, pts45k );
}

void gst_westeros_sink_soc_render( GstWesterosSink *sink, GstBuffer *buffer )
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(buffer);
}

void gst_westeros_sink_soc_flush( GstWesterosSink *sink )
{
   NEXUS_SimpleVideoDecoder_Flush( sink->soc.videoDecoder );
   sink->soc.captureCount= 0;
   sink->soc.frameCount= 0;
   sink->soc.noFrameCount= 0;

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
}

gboolean gst_westeros_sink_soc_start_video( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   NEXUS_Error rc;
   NxClient_ConnectSettings connectSettings;
   NEXUS_SimpleVideoDecoderStartSettings startSettings;
     
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: enter");
   
   queryPeerHandles( sink );
   
   /* Connect to the decoder */
   NxClient_GetDefaultConnectSettings(&connectSettings);
   connectSettings.simpleVideoDecoder[0].id= sink->soc.videoDecoderId;
   connectSettings.simpleVideoDecoder[0].surfaceClientId= sink->soc.surfaceClientId;
   connectSettings.simpleVideoDecoder[0].windowId= 0;
   connectSettings.simpleVideoDecoder[0].windowCapabilities.type= NxClient_VideoWindowType_eMain;
   connectSettings.simpleVideoDecoder[0].windowCapabilities.maxWidth= 1920;
   connectSettings.simpleVideoDecoder[0].windowCapabilities.maxHeight= 1080;
   connectSettings.simpleVideoDecoder[0].decoderCapabilities.maxWidth= 0;
   connectSettings.simpleVideoDecoder[0].decoderCapabilities.maxHeight= 0;
   rc= NxClient_Connect(&connectSettings, &sink->soc.connectId);
   if ( rc != NEXUS_SUCCESS )
   {
      GST_ERROR("gst_westeros_sink_soc_start_video: NxClient_Connect failed: %d", rc);
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

   rc= NEXUS_SimpleVideoDecoder_SetStcChannel(sink->soc.videoDecoder, sink->soc.stcChannel);
   if ( rc != NEXUS_SUCCESS )
   {
      GST_ERROR("gst_westeros_sink_soc_start_video: NEXUS_SimpleVideoDecoder_SetStcChannel failed: %d", (int)rc);
      goto exit;
   }

   rc= NEXUS_SimpleVideoDecoder_Start(sink->soc.videoDecoder, &startSettings);
   if ( rc != NEXUS_SUCCESS )
   {
      GST_ERROR("gst_westeros_sink_soc_start_video: NEXUS_SimpleVideoDecoder_Start failed: %d", (int)rc);
      goto exit;
   }

   if ( sink->soc.stcChannel )
   {
      rc= NEXUS_SimpleStcChannel_Freeze(sink->soc.stcChannel, FALSE);
      if ( rc != NEXUS_SUCCESS )
      {
         goto exit;
      }
   }

   if ( sink->startPTS != 0 )
       gst_westeros_sink_soc_set_startPTS( sink, sink->startPTS );

   sink->soc.quitCaptureThread= FALSE;
   if ( sink->soc.captureThread == NULL ) 
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video: starting westeros_sink_capture thread");
      sink->soc.captureThread= g_thread_new("westeros_sink_capture", captureThread, sink);        
   }
 
   sink->videoStarted= TRUE;
   
   result= TRUE;

exit:

   return result;   
}

static gboolean allocCaptureSurfaces( GstWesterosSink *sink )
{
   gboolean result= TRUE;
   NEXUS_SurfaceCreateSettings videoSurfaceCreateSettings;

   if ( sink->srcWidth < 16 ) sink->srcWidth= 16;
   if ( sink->srcHeight < 16 ) sink->srcHeight= 16;
   
   if ( (sink->soc.captureWidth != sink->srcWidth) || (sink->soc.captureHeight != sink->srcHeight) )
   {
      freeCaptureSurfaces(sink);
      
      sink->soc.captureWidth= sink->srcWidth;
      sink->soc.captureHeight= sink->srcHeight;

      /* Create a set of surfaces for decoded frame capture */
      NEXUS_Surface_GetDefaultCreateSettings(&videoSurfaceCreateSettings);
      videoSurfaceCreateSettings.width= sink->soc.captureWidth;
      videoSurfaceCreateSettings.height= sink->soc.captureHeight;
      videoSurfaceCreateSettings.pixelFormat= NEXUS_PixelFormat_eA8_R8_G8_B8;
      for( int i= 0; i < NUM_CAPTURE_SURFACES; ++i )
      {
         sink->soc.captureSurface[i]= NEXUS_Surface_Create(&videoSurfaceCreateSettings);
         GST_LOG("video capture surface %d: %p (%dx%d)\n", i, 
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
   for( int i= 0; i < NUM_CAPTURE_SURFACES; ++i )
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
         GST_ERROR("queryPeerHandles: pad query for stc_channel failed\n");
         gst_query_unref(query);
         return FALSE;
      }    
      structure2= gst_query_get_structure(query);
      val= gst_structure_get_value(structure2, "stc_channel");
      if (val == NULL) 
      {
         GST_ERROR("queryPeerHandles: struc value for stc_channel failed\n");
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
      GST_ERROR("queryPeerHandles: pad query for codec failed\n");
      gst_query_unref(query);
      return FALSE;
   }
   structure2= gst_query_get_structure(query);
   val= gst_structure_get_value(structure2, "video_codec");
   if (val == NULL) 
   {
      GST_ERROR("queryPeerHandles: struc value for codec failed\n");
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
      GST_ERROR("queryPeerHandles: pad query for video_pid_channel failed\n");
      gst_query_unref(query);
      return FALSE;
   }
   structure2= gst_query_get_structure(query);
   val= gst_structure_get_value(structure2, "video_pid_channel");
   if (val == NULL) 
   {
      GST_ERROR("queryPeerHandles: struc value for video_pid_channel failed\n");
      gst_query_unref(query);
      return FALSE;
   }
   sink->soc.videoPidChannel= (NEXUS_PidChannelHandle)g_value_get_pointer(val);
   gst_query_unref(query);

   allocCaptureSurfaces( sink );

   return TRUE;
}

static gpointer captureThread(gpointer data) 
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   
   GST_DEBUG_OBJECT(sink, "captureThread: enter");
   sink->soc.startTime= getCurrentTimeMillis();

   if ( getenv("WESTEROS_SINK_USE_GFX") )
   {
      GST_INFO_OBJECT(sink, "WESTEROS_SINK_USE_GFX defined - enabling capture\n");
      setVideoPath( sink, true );
   }

   /*
    * Check for new video frames at a rate that
    * can support video at up to 60 fps
    */
   while( !sink->soc.quitCaptureThread )
   {
      LOCK( sink );
      gboolean videoStarted= sink->videoStarted;
      gboolean eosDetected= sink->eosDetected;
      if ( sink->windowChange )
      {
         sink->windowChange= false;
         updateVideoPosition( sink );
      }
      UNLOCK( sink );
      
      if ( sink->soc.captureEnabled )
      {
         if ( videoStarted && sink->visible && !eosDetected )
         {
            processFrame( sink );
         }
      }
      else
      {      
         updateVideoStatus( sink );
      }

      if ( wl_display_dispatch_queue_pending(sink->display, sink->queue) == 0 )
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

static void setVideoPath( GstWesterosSink *sink, bool useGfxPath )
{
   if ( useGfxPath && !sink->soc.captureEnabled )
   {
      NEXUS_Error rc;
      NEXUS_SimpleVideoDecoderStartCaptureSettings captureSettings;
      NEXUS_SurfaceComposition composition;
      
      /* Start video frame capture */
      NEXUS_SimpleVideoDecoder_GetDefaultStartCaptureSettings(&captureSettings);
      captureSettings.displayEnabled= true;

      memcpy(&captureSettings.surface, &sink->soc.captureSurface, sizeof(sink->soc.captureSurface));
      rc= NEXUS_SimpleVideoDecoder_StartCapture(sink->soc.videoDecoder, &captureSettings);
      if (rc != 0)
      {
          GST_ERROR("Error NEXUS_SimpleVideoDecoder_StartCapture: %d", (int)rc);
      }
      sink->soc.captureEnabled= TRUE;

      /* Move HW path video off screen.  The natural inclination would be to suppress
       * its presentation by setting captureSettings.displayEnable to false, but doing
       * so seems to cause HW path video to never present again when capture is disabled.
       * Similarly, hiding the HW path video by setting its opacity to 0 seems to not work.
       */
      NxClient_GetSurfaceClientComposition(sink->soc.surfaceClientId, &composition);
      composition.position.y= -composition.position.height;
      NxClient_SetSurfaceClientComposition(sink->soc.surfaceClientId, &composition);
   }
   else if ( !useGfxPath && sink->soc.captureEnabled )
   {
      NEXUS_SurfaceComposition composition;

      /* Move HW path video back on-screen */
      NxClient_GetSurfaceClientComposition(sink->soc.surfaceClientId, &composition);
      composition.position.x= sink->soc.videoX;
      composition.position.y= sink->soc.videoY;
      composition.position.width= sink->soc.videoWidth;
      composition.position.height= sink->soc.videoHeight;
      NxClient_SetSurfaceClientComposition(sink->soc.surfaceClientId, &composition);

      /* Stop video frame capture */
      NEXUS_SimpleVideoDecoder_StopCapture(sink->soc.videoDecoder);
      sink->soc.captureEnabled= FALSE;
      
      wl_surface_attach( sink->surface, 0, sink->windowX, sink->windowY );
      wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
      wl_surface_commit( sink->surface );
      wl_display_flush(sink->display);
      wl_display_dispatch_queue_pending(sink->display, sink->queue);
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
   NEXUS_SimpleVideoDecoder_RecycleCapturedSurfaces(sink->soc.videoDecoder, &captureSurface, 1);                
   
   --sink->soc.activeBuffers;
   wl_buffer_destroy( buffer );
   
   free( binfo );
}

static struct wl_buffer_listener wl_buffer_listener= 
{
   buffer_release
};

static void processFrame( GstWesterosSink *sink )
{
   NEXUS_SimpleVideoDecoderCaptureStatus captureStatus;
   NEXUS_SurfaceHandle captureSurface= NULL;
   unsigned numReturned= 0;
   unsigned segmentNumber= 0;
   gboolean eosDetected= FALSE;
   
   GST_DEBUG_OBJECT(sink, "processFrame: enter");
   
   if ( (sink->soc.captureWidth != sink->srcWidth) || (sink->soc.captureHeight != sink->srcHeight) )
   {
      allocCaptureSurfaces( sink );
   }

   LOCK( sink );
   segmentNumber= sink->segmentNumber;
   eosDetected= sink->eosDetected;
   UNLOCK( sink );

   for( ; ; )
   {
      sink->soc.captureCount++;
      NEXUS_SimpleVideoDecoder_GetCapturedSurfaces(sink->soc.videoDecoder, &captureSurface, &captureStatus, 1, &numReturned);
      if ( numReturned > 0 )
      {
         bufferInfo *binfo= (bufferInfo*)malloc( sizeof(bufferInfo) );
         if ( binfo )
         {
            binfo->sink= sink;
            binfo->deviceBuffer= captureSurface;
            
            LOCK( sink );
            if ( sink->flushStarted || segmentNumber != sink->segmentNumber )
            {
                UNLOCK( sink );
                NEXUS_SimpleVideoDecoder_RecycleCapturedSurfaces(sink->soc.videoDecoder, &captureSurface, 1);
                free( binfo );
                break;
            }

            sink->currentPTS= ((gint64)captureStatus.pts)*2LL;
            if ( sink->soc.frameCount == 0 )
            {
               sink->firstPTS= sink->currentPTS;
            }
            sink->position= sink->positionSegmentStart + ((sink->currentPTS - sink->firstPTS) * GST_MSECOND) / 90LL;
            UNLOCK( sink );
            sink->soc.frameCount++;
            sink->soc.noFrameCount= 0;
            
            long long now= getCurrentTimeMillis();
            long long elapsed= now-sink->soc.startTime;
            GST_LOG("%lld.%03lld: cap surf %p: frame %d pts %u (%d) serial %u iter %d\n", 
                    elapsed/1000LL, elapsed%1000LL, (void*)captureSurface, sink->soc.frameCount, 
                    captureStatus.pts, captureStatus.ptsValid, captureStatus.serialNumber, sink->soc.captureCount );


            if ( sink->soc.sb )
            {
               struct wl_buffer *buff;
               
               buff= wl_sb_create_buffer( sink->soc.sb, 
                                          (uint32_t)captureSurface, 
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
      }
      else if ( !eosDetected )
      {
         int limit= (sink->currentPTS > sink->firstPTS) 
                    ? EOS_DETECT_DELAY
                    : EOS_DETECT_DELAY_AT_START;
         ++sink->soc.noFrameCount;
         if ( sink->soc.noFrameCount*FRAME_POLL_TIME > limit )
         {
            GST_INFO_OBJECT(sink, "processFrame: eos detected: firstPTS %lld currentPTS %lld\n", sink->firstPTS, sink->currentPTS);
            gst_westeros_sink_eos_detected( sink );
            sink->soc.noFrameCount= 0;
         }
         break;
      }
   }
   GST_DEBUG_OBJECT(sink, "processFrame: exit");
}

static void updateVideoStatus( GstWesterosSink *sink )
{
   NEXUS_VideoDecoderStatus videoStatus;
   gboolean noFrame= FALSE;
   gboolean eosDetected= FALSE;

   LOCK( sink );
   eosDetected= sink->eosDetected;
   UNLOCK( sink );
   
   if ( NEXUS_SUCCESS == NEXUS_SimpleVideoDecoder_GetStatus( sink->soc.videoDecoder, &videoStatus) )
   {
      LOCK( sink );
      if ( videoStatus.firstPtsPassed && (sink->currentPTS/2 != videoStatus.pts) )
      {
         sink->currentPTS= ((gint64)videoStatus.pts)*2LL;
         if ( sink->soc.frameCount == 0 )
         {
            sink->firstPTS= sink->currentPTS;
         }
         sink->position= sink->positionSegmentStart + ((sink->currentPTS - sink->firstPTS) * GST_MSECOND) / 90LL;
         sink->soc.frameCount++;
         sink->soc.noFrameCount= 0;
      }
      else
      {
         noFrame= TRUE;
      }
      UNLOCK( sink );
   }

   if ( noFrame && !eosDetected )
   {
      int limit= (sink->currentPTS > sink->firstPTS) 
                 ? EOS_DETECT_DELAY
                 : EOS_DETECT_DELAY_AT_START;
      ++sink->soc.noFrameCount;
      if ( sink->soc.noFrameCount*FRAME_POLL_TIME > limit )
      {
         GST_INFO_OBJECT(sink, "updateVideoStatus: eos detected: firstPTS %lld currentPTS %lld\n", sink->firstPTS, sink->currentPTS);
         gst_westeros_sink_eos_detected( sink );
         sink->soc.noFrameCount= 0;
      }
   }
}

static void updateVideoPosition( GstWesterosSink *sink )
{
   NEXUS_SurfaceComposition composition;
   
   sink->soc.videoX= ((sink->windowX*sink->soc.scaleXNum)/sink->soc.scaleXDenom) + sink->soc.transX;
   sink->soc.videoY= ((sink->windowY*sink->soc.scaleYNum)/sink->soc.scaleYDenom) + sink->soc.transY;
   sink->soc.videoWidth= ((sink->windowWidth)*sink->soc.scaleXNum)/sink->soc.scaleXDenom;
   sink->soc.videoHeight= ((sink->windowHeight)*sink->soc.scaleXNum)/sink->soc.scaleXDenom;

   if ( !sink->soc.captureEnabled )
   {
      NxClient_GetSurfaceClientComposition(sink->soc.surfaceClientId, &composition);
      composition.position.x= sink->soc.videoX;
      composition.position.y= sink->soc.videoY;
      composition.position.width= sink->soc.videoWidth;
      composition.position.height= sink->soc.videoHeight;
      NxClient_SetSurfaceClientComposition(sink->soc.surfaceClientId, &composition);
   }
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



