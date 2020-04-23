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
#ifndef __WESTEROS_SINK_SOC_H__
#define __WESTEROS_SINK_SOC_H__

#include <stdlib.h>

#include "ismd_core.h"
#include "ismd_vidrend.h"
#include "ismd_vidpproc.h"
#include "ismd_vidsink.h"

#include "icegdl-client-protocol.h"

typedef gboolean (*PISMDGSTBUFFERCHECKTYPE)(GstBuffer *buffer);
typedef ismd_buffer_handle_t (*PISMDGSTBUFFERGETHANDLE)(GstBuffer *buffer);
typedef guint (*PISMDGSTREGISTERDEVICEHANDLE)(ismd_dev_t dev_handle, int device_type);
typedef void* (*PISMDGSTGETDEFAULTCLOCK)(void);
typedef guint64 (*PISMDGSTMARKBASETIME)(void *clock, guint64 offset);
typedef void (*PISMDGSTCLEARBASETIME)(void *clock);

#define WESTEROS_SINK_CAPS \
           "video/x-decoded-ismd;"

struct _GstWesterosSinkSoc
{
   struct wl_icegdl *icegdl;
   int activeBuffers;
   int frameCount;
   long long firstFrameTime;
   PISMDGSTBUFFERCHECKTYPE isIsmdBuffer;
   PISMDGSTBUFFERGETHANDLE getIsmdBufferHandle;
   PISMDGSTREGISTERDEVICEHANDLE registerDeviceHandle;
   PISMDGSTGETDEFAULTCLOCK getDefaultClock;
   PISMDGSTMARKBASETIME markBaseTime;
   PISMDGSTCLEARBASETIME clearBaseTime;
   bool sinkReady;
   bool gdlReady;
   bool ismdReady;
   int hwVideoPlane;
   int scaleMode;
   gboolean enableCCPassthru;
   int mute;
   int deinterlacePolicy;
   guint64 streamTimeOffset;
   bool useVirtualCoords;
   ismd_dev_t vidrend;
   ismd_dev_t vidpproc;
   ismd_vidsink_dev_t vidsink;
   ismd_port_handle_t inputPort;
   ismd_port_handle_t outputPort;
   GstClock *clock;
   ismd_clock_t clockDevice;
   ismd_time_t baseTime;
   ismd_time_t pauseTime;
   ismd_event_t underflowEvent;
   ismd_event_t quitEvent;
   int modeWidth;
   int modeHeight;
   bool needToClearGfx;
   int clearGfxCount;
   bool useGfxPath;
   unsigned int contRate;
   
   bool firstFrameSignalled;
   long long lastUnderflowTime;
   gboolean quitMonitorThread;
   GThread *monitorThread;
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
void gst_westeros_sink_set_video_path( GstWesterosSink *sink, bool useGfxPath );
void gst_westeros_sink_soc_update_video_position( GstWesterosSink *sink );
gboolean gst_westeros_sink_soc_query( GstWesterosSink *sink, GstQuery *query );

#endif
