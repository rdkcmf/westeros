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
#include <stdint.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <linux/videodev2.h>
#include <drm/drm_fourcc.h>

#include "simplebuffer-client-protocol.h"

#define WESTEROS_SINK_CAPS \
      "video/x-h264, " \
      "parsed=(boolean) true, " \
      "alignment=(string) au, " \
      "stream-format=(string) byte-stream, " \
      "width=(int) [1,MAX], " "height=(int) [1,MAX] ; " \
      "video/mpeg, " \
      "parsed=(boolean) true, " \
      "systemstream = (boolean) false, " \
      "width=(int) [1,MAX], " "height=(int) [1,MAX]" 

typedef struct _WstVideoClientConnection
{
   GstWesterosSink *sink;
   const char *name;
   struct sockaddr_un addr;
   int socketFd;
   int serverRefreshRate;
   gint64 serverRefreshPeriod;
   gint64 lastSendTime;
} WstVideoClientConnection;

typedef struct _WstPlaneInfo
{
   int fd;
   void *start;
   int capacity;
} WstPlaneInfo;

#define WST_MAX_PLANES (3)
typedef struct _WstGemBuffer
{
   uint32_t width;
   uint32_t height;
   int planeCount;
   unsigned int handle[WST_MAX_PLANES];
   unsigned int stride[WST_MAX_PLANES];
   unsigned int offset[WST_MAX_PLANES];
   uint32_t size[WST_MAX_PLANES];
   int fd[WST_MAX_PLANES];
} WstGemBuffer;

typedef struct _WstBufferInfo
{
   struct v4l2_buffer buf;
   struct v4l2_plane planes[WST_MAX_PLANES];
   WstPlaneInfo planeInfo[WST_MAX_PLANES];
   WstGemBuffer gemBuf;
   GstBuffer *gstbuf;
   int bufferId;
   bool locked;
   int planeCount;
   int fd;
   void *start;
   int capacity;
   gint64 frameTime;
   bool drop;
} WstBufferInfo;

struct _GstWesterosSinkSoc
{
   struct wl_sb *sb;
   int activeBuffers;
   double frameRate;
   int frameWidth;
   int frameHeight;
   int frameInCount;
   int frameOutCount;
   uint32_t numDropped;
   uint32_t inputFormat;
   uint32_t outputFormat;

   char *devname;
   gboolean enableTextureSignal;
   int v4l2Fd;
   struct v4l2_capability caps;
   uint32_t deviceCaps;
   gboolean isMultiPlane;
   gboolean preferNV12M;
   uint32_t inputMemMode;
   uint32_t outputMemMode;
   int numInputFormats;
   struct v4l2_fmtdesc *inputFormats;
   int numOutputFormats;
   struct v4l2_fmtdesc *outputFormats;
   struct v4l2_format fmtIn;
   struct v4l2_format fmtOut;
   gboolean formatsSet;
   int bufferCohort;
   uint32_t minBuffersIn;
   uint32_t minBuffersOut;
   int numBuffersIn;
   WstBufferInfo *inBuffers;
   int numBuffersOut;
   int bufferIdOutBase;
   WstBufferInfo *outBuffers;

   int nextFrameFd;
   int prevFrame1Fd;
   int prevFrame2Fd;
   int resubFd;

   gboolean videoPlaying;
   gboolean videoPaused;
   gboolean hasEvents;
   gboolean needCaptureRestart;
   gboolean quitVideoOutputThread;
   GThread *videoOutputThread;
   gboolean quitEOSDetectionThread;
   GThread *eosDetectionThread;
   gboolean quitDispatchThread;
   GThread *dispatchThread;

   gboolean useCaptureOnly;
   gboolean captureEnabled;
   int framesBeforeHideVideo;
   gint64 prevFrameTimeGfx;
   gint64 prevFramePTSGfx;
   WstVideoClientConnection *conn;
   int videoX;
   int videoY;
   int videoWidth;
   int videoHeight;

   gboolean frameStepOnPreroll;

   gboolean secureVideo;
   gboolean useDmabufOutput;
   int dwMode;
   int drmFd;

   #ifdef USE_GST1
   GstPadChainFunction chainOrg;
   #endif

   #ifdef GLIB_VERSION_2_32 
   GMutex mutex;
   #else
   GMutex *mutex;
   #endif
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

