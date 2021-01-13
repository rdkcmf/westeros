/*
 * Copyright (C) 2020 RDK Management
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
#ifndef __WESTEROS_SINK_RAW_H__
#define __WESTEROS_SINK_RAW_H__

#include <stdlib.h>
#include <stdint.h>

#include <semaphore.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "simplebuffer-client-protocol.h"

#define WESTEROS_SINK_CAPS \
      "video/x-raw, " \
      "format=(string) { NV12, I420, YU12 }"

typedef struct _WstVideoClientConnection
{
   GstWesterosSink *sink;
   const char *name;
   struct sockaddr_un addr;
   int socketFd;
   int serverRefreshRate;
   gint64 serverRefreshPeriod;
} WstVideoClientConnection;

#define WST_NUM_DRM_BUFFERS (20)
#define WST_MAX_PLANE (2)
typedef struct _WstDrmBuffer
{
   int width;
   int height;
   int fd[WST_MAX_PLANE];
   int handle[WST_MAX_PLANE];
   gsize size[WST_MAX_PLANE];
   gsize offset[WST_MAX_PLANE];
   gsize pitch[WST_MAX_PLANE];
   gint64 frameTime; /* in microseconds */
   int buffIndex;
   int frameNumber;
   int bufferId;
   bool locked;
   int lockCount;
   bool localAlloc;
   GstBuffer *gstbuf;
} WstDrmBuffer;

struct _GstWesterosSinkSoc
{
   struct wl_sb *sb;
   double frameRate;
   double pixelAspectRatio;
   gboolean havePixelAspectRatio;
   gboolean pixelAspectRatioChanged;
   gboolean showChanged;
   int frameWidth;
   int frameHeight;
   uint32_t frameFormatStream;
   uint32_t frameFormatOut;
   int frameInCount;
   int frameOutCount;
   int frameDisplayCount;
   uint32_t numDropped;
   gint64 currentInputPTS;

   gboolean updateSession;
   int syncType;
   int sessionId;

   int nextFrameFd;
   int prevFrame1Fd;
   int prevFrame2Fd;
   int resubFd;

   gboolean videoPlaying;
   gboolean videoPaused;
   gboolean quitEOSDetectionThread;
   GThread *eosDetectionThread;
   gboolean quitDispatchThread;
   GThread *dispatchThread;

   gboolean emitFirstFrameSignal;
   gboolean useCaptureOnly;
   gboolean captureEnabled;
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

   gboolean enableTextureSignal;
   gboolean forceAspectRatio;

   int drmFd;
   int nextDrmBuffer;
   bool haveDrmBuffSem;
   sem_t drmBuffSem;
   GThread *firstFrameThread;
   WstDrmBuffer drmBuffer[WST_NUM_DRM_BUFFERS];

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

