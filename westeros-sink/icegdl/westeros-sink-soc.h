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
