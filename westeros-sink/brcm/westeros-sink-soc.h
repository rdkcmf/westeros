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
#ifndef __WESTEROS_SINK_SOC_H__
#define __WESTEROS_SINK_SOC_H__

#include "nexus_config.h"
#include "nexus_platform.h"
#include "default_nexus.h"
#include "nxclient.h"
#include "nexus_surface_client.h"
#include "nexus_core_utils.h"
#include "nexus_stc_channel.h"
#include "nexus_simple_video_decoder.h"

#include "simplebuffer-client-protocol.h"

#define WESTEROS_SINK_CAPS \
           "video/x-brcm-avd;"


#define NUM_CAPTURE_SURFACES (NEXUS_SIMPLE_DECODER_MAX_SURFACES)

struct _GstWesterosSinkSoc
{
   int captureWidth;
   int captureHeight;
   int captureWidthNext;
   int captureHeightNext;
   bool secureGraphics;
   NEXUS_SurfaceHandle captureSurface[NUM_CAPTURE_SURFACES];

   NxClient_AllocResults allocSurface;
   int surfaceClientId;
   NEXUS_SurfaceClientHandle surfaceClient;
   NEXUS_SurfaceClientHandle videoWindow;

   gboolean forceAspectRatio;
   gboolean hideVideoDuringCapture;
   gboolean useImmediateOutput;
   gboolean usePip;
   gboolean useLowDelay;
   gboolean frameStepOnPreroll;
   gboolean enableTextureSignal;
   gint latencyTarget;
   
   unsigned int connectId;

   int videoDecoderId;
   NEXUS_SimpleVideoDecoderHandle videoDecoder;

   NEXUS_SimpleStcChannelHandle stcChannel;
   NEXUS_PidChannelHandle videoPidChannel;

   gboolean haveResources;
   long long timeResourcesLost;
   gint64 positionResourcesLost;

   int codec;
   gboolean quitCaptureThread;
   GThread *captureThread;
   long long startTime;
   int captureCount;
   int frameCount;
   int noFrameCount;
   guint32 numDecoded;
   guint32 numDropped;
   guint32 numDroppedOutOfSegment;
   gboolean ignoreDiscontinuity;
   gboolean checkForEOS;
   gboolean emitEOS;
   gboolean emitUnderflow;
   gboolean emitPTSError;
   gboolean emitResourceChange;
   guint prevQueueDepth;
   guint prevFifoDepth;
   guint prevNumDecoded;
   gboolean captureEnabled;
   gboolean videoPlaying;
   int framesBeforeHideVideo;

   gboolean presentationStarted;
   unsigned int ptsOffset;
   gboolean zoomSet;
   NEXUS_VideoWindowContentMode zoomMode;
   gboolean enableCCPassthru;
   NEXUS_VideoFormat outputFormat;
   gfloat serverPlaySpeed;
   gfloat clientPlaySpeed;
   gboolean stoppedForPlaySpeedChange;
   gboolean secureVideo;
   gboolean haveHardware;
   #ifdef ENABLE_SW_DECODE
   GstPad *dataProbePad;
   gboolean dataProbeNeedStartCodes;
   gulong dataProbeId;
   bool removeDataProbe;
   int dataProbeCodecDataLen;
   unsigned char *dataProbeCodecData;
   bool swPrerolled;
   NEXUS_SurfaceHandle swWorkSurface;
   int swNextCaptureSurface;
   NEXUS_Graphics2DHandle g2d;
   bool g2dEventCreated;
   BKNI_EventHandle g2dEvent;
   int frameWidth;
   int frameHeight;
   #endif

   #if ((NEXUS_PLATFORM_VERSION_MAJOR >= 18) || (NEXUS_PLATFORM_VERSION_MAJOR >= 17 && NEXUS_PLATFORM_VERSION_MINOR >= 3))
   NEXUS_VideoEotf eotf;
   NEXUS_ContentLightLevel contentLightLevel;
   NEXUS_MasteringDisplayColorVolume masteringDisplayColorVolume;
   #endif

   int videoX;
   int videoY;
   int videoWidth;
   int videoHeight;

   struct wl_sb *sb;
   int activeBuffers;
};

void gst_westeros_sink_soc_class_init(GstWesterosSinkClass *klass);
gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink );
void gst_westeros_sink_soc_term( GstWesterosSink *sink );
void gst_westeros_sink_soc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
void gst_westeros_sink_soc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
gboolean gst_westeros_sink_soc_null_to_ready( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_paused_to_ready( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault );
void gst_westeros_sink_soc_registryHandleGlobal( GstWesterosSink *sink, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version);
void gst_westeros_sink_soc_registryHandleGlobalRemove(GstWesterosSink *sink,
                                 struct wl_registry *registry,
			                        uint32_t name);
gboolean gst_westeros_sink_soc_accept_caps( GstWesterosSink *sink, GstCaps *caps );
void gst_westeros_sink_soc_set_startPTS( GstWesterosSink *sink, gint64 pts );
void gst_westeros_sink_soc_render( GstWesterosSink *sink, GstBuffer *buffer );
void gst_westeros_sink_soc_flush( GstWesterosSink *sink );
gboolean gst_westeros_sink_soc_start_video( GstWesterosSink *sink );
void gst_westeros_sink_soc_eos_event( GstWesterosSink *sink );
void gst_westeros_sink_soc_set_video_path( GstWesterosSink *sink, bool useGfxPath );
void gst_westeros_sink_soc_update_video_position( GstWesterosSink *sink );
gboolean gst_westeros_sink_soc_query( GstWesterosSink *sink, GstQuery *query );

#endif
