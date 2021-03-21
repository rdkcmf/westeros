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
   int lockCount;
   int planeCount;
   int fd;
   void *start;
   int capacity;
   int frameNumber;
   gint64 frameTime;
   bool drop;
   bool queued;
} WstBufferInfo;

#ifdef ENABLE_SW_DECODE
#define WST_NUM_SW_BUFFERS (4)
typedef struct _WstSWBuffer
{
   int width;
   int height;
   int fd0;
   int fd1;
   int handle0;
   int handle1;
   int size0;
   int size1;
   int offset0;
   int offset1;
   int pitch0;
   int pitch1;
} WstSWBuffer;
#endif

struct _GstWesterosSinkSoc
{
   struct wl_sb *sb;
   int activeBuffers;
   double frameRate;
   int frameRateFractionNum;
   int frameRateFractionDenom;
   gboolean frameRateChanged;
   double pixelAspectRatio;
   gboolean havePixelAspectRatio;
   gboolean pixelAspectRatioChanged;
   gboolean showChanged;
   gboolean zoomModeUser;
   int zoomMode;
   int overscanSize;
   int frameWidth;
   int frameHeight;
   int frameWidthStream;
   int frameHeightStream;
   int frameInCount;
   int frameOutCount;
   int frameDecodeCount;
   int frameDisplayCount;
   uint32_t numDropped;
   uint32_t inputFormat;
   uint32_t outputFormat;
   gboolean interlaced;
   gint64 prevDecodedTimestamp;
   gint64 currentInputPTS;
   gint64 videoStartTime;

   char *devname;
   gboolean enableTextureSignal;
   gboolean enableDecodeErrorSignal;
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
   gboolean updateSession;
   int syncType;
   int sessionId;
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
   gboolean emitFirstFrameSignal;
   gboolean emitUnderflowSignal;
   gboolean decodeError;
   gboolean quitVideoOutputThread;
   GThread *videoOutputThread;
   gboolean quitEOSDetectionThread;
   GThread *eosDetectionThread;
   gboolean quitDispatchThread;
   GThread *dispatchThread;

   gboolean useCaptureOnly;
   gboolean captureEnabled;
   gboolean frameAdvance;
   gboolean pauseException;
   gboolean pauseGetGfxFrame;
   gboolean useGfxSync;
   int pauseGfxBuffIndex;
   int hideVideoFramesDelay;
   int hideGfxFramesDelay;
   int framesBeforeHideVideo;
   int framesBeforeHideGfx;
   gint64 prevFrameTimeGfx;
   gint64 prevFramePTSGfx;
   WstVideoClientConnection *conn;
   int videoX;
   int videoY;
   int videoWidth;
   int videoHeight;

   gboolean haveColorimetry;
   int hdrColorimetry[4];
   gboolean haveMasteringDisplay;
   float hdrMasteringDisplay[10];
   gboolean haveContentLightLevel;
   int hdrContentLightLevel[2];

   GstBuffer *prerollBuffer;
   gboolean frameStepOnPreroll;
   gboolean forceAspectRatio;

   gboolean lowMemoryMode;
   gboolean secureVideo;
   gboolean useDmabufOutput;
   int dwMode;
   int drmFd;

   #ifdef USE_GST1
   GstPadChainFunction chainOrg;
   #endif

   #ifdef ENABLE_SW_DECODE
   GThread *firstFrameThread;
   int nextSWBuffer;
   WstSWBuffer swBuffer[WST_NUM_SW_BUFFERS];
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

