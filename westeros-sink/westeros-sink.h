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
#ifndef __WESTEROS_SINK_H__
#define __WESTEROS_SINK_H__

#include "wayland-client.h"
#include "simpleshell-client-protocol.h"
#include "vpc-client-protocol.h"
#include "essos-resmgr.h"

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#define DEFAULT_WINDOW_X (0)
#define DEFAULT_WINDOW_Y (0)
#define DEFAULT_WINDOW_WIDTH (1280)
#define DEFAULT_WINDOW_HEIGHT (720)

#define WESTEROS_UNUSED(x) ((void)(x))

#ifdef USE_RAW_SINK
typedef struct _GstWesterosRawSink GstWesterosRawSink;
typedef struct _GstWesterosRawSinkClass GstWesterosRawSinkClass;
#define _GstWesterosSink _GstWesterosRawSink
#define GstWesterosSink GstWesterosRawSink
#define _GstWesterosSinkSoc _GstWesterosRawSinkSoc
#define GstWesterosSinkSoc GstWesterosRawSinkSoc
#define _GstWesterosSinkClass _GstWesterosRawSinkClass
#endif


G_BEGIN_DECLS

#define GST_TYPE_WESTEROS_SINK \
  (gst_westeros_sink_get_type())
#define GST_WESTEROS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WESTEROS_SINK,GstWesterosSink))
#define GST_WESTEROS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WESTEROS_SINK,GstWesterosSinkClass))
#define GST_WESTEROS_SINK_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_WESTEROS_SINK, GstWesterosSinkClass))
#define GST_IS_WESTEROS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WESTEROS_SINK))
#define GST_IS_WESTEROS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WESTEROS_SINK))

typedef struct _GstWesterosSink GstWesterosSink;
typedef struct _GstWesterosSinkClass GstWesterosSinkClass;

typedef gboolean (*ProcessPadEvent)(GstWesterosSink *sink, GstPad *pad, GstEvent *event, gboolean *passToDefault);

typedef int (*SinkAcquireResources)( GstWesterosSink *sink );
typedef void (*SinkReleaseResources)( GstWesterosSink *sink );

#ifdef ENABLE_SW_DECODE
#include "../../westeros-sink/westeros-sink-sw.h"

typedef bool (*SinkSWInit)( GstWesterosSink *sink );
typedef void (*SinkSWTerm)( GstWesterosSink *sink );
typedef void (*SinkSWLink)( GstWesterosSink *sink );
typedef void (*SinkSWUnLink)( GstWesterosSink *sink );
typedef void (*SinkSWEvent)( GstWesterosSink *sink, int id, int p1, void *p2 );
typedef void (*SinkSWDisplay)( GstWesterosSink *sink, SWFrame *frame );
#endif

typedef void (*SinkTimeCodePresent)( GstWesterosSink *sink, guint64 pts, guint signnal );

typedef void* (*MediaCaptureCreateContext)( GstElement *element );
typedef void (*MediaCaptureDestroyContext)( void *context );

#define PROP_SOC_BASE (100)

typedef struct _WstSinkResReqInfo
{
   GstWesterosSink *sink;
   EssRMgrRequest resReq;
} WstSinkResReqInfo;

typedef struct _WstSinkTimeCode
{
   guint64 position;
   guint hours;
   guint minutes;
   guint seconds;
} WstSinkTimeCode;

#include "westeros-sink-soc.h"

struct _GstWesterosSink
{
   GstBaseSink parent;
   GstPadEventFunction parentEventFunc;
   GstPadQueryFunction defaultQueryFunc;
   
   GstPad *peerPad; 
   gboolean rejectPrerollBuffers;
   
   gboolean initialized;
   #ifdef GLIB_VERSION_2_32 
   GMutex mutex;
   #else
   GMutex *mutex;
   #endif
   
   int srcWidth;
   int srcHeight;
   int maxWidth;
   int maxHeight;

   int windowX;
   int windowY;
   int windowWidth;
   int windowHeight;
   bool show;
   bool windowChange;
   bool windowSet;
   bool windowSizeOverride;

   int displayWidth;
   int displayHeight;

   bool visible;
   float opacity;
   float zorder;
   gfloat playbackRate;
   
   int transX;
   int transY;
   int scaleXNum;
   int scaleXDenom;
   int scaleYNum;
   int scaleYDenom;
   int outputWidth;
   int outputHeight;

   gboolean videoStarted;
   gboolean startAfterLink;
   gboolean startAfterCaps;
   gboolean flushStarted;
   gboolean passCaps;
   
   gboolean eosEventSeen;
   gboolean eosDetected;
   gint64 startPTS;
   gint64 firstPTS;
   gint64 currentPTS;
   gint64 position;
   gint64 positionSegmentStart;
   gint64 prevPositionSegmentStart;
   gboolean queryPositionFromPeer;
   const GstSegment* currentSegment;
   gboolean useSegmentPosition;
   GstSegment segment;

   unsigned segmentNumber;
   
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_simple_shell *shell;
   struct wl_compositor *compositor;
   struct wl_event_queue *queue;
   struct wl_surface *surface;
   uint32_t surfaceId;
   struct wl_vpc *vpc;
   struct wl_vpc_surface *vpcSurface;
   struct wl_output *output;

   EssRMgr *rm;
   guint resPriority;
   guint resUsage;
   int resAssignedId;
   EssRMgrCaps resCurrCaps;
   WstSinkResReqInfo resReqPrimary;
   WstSinkResReqInfo resReqSecondary;
   SinkAcquireResources acquireResources;
   SinkReleaseResources releaseResources;
   #ifdef ENABLE_SW_DECODE
   void *swCtx;
   SinkSWInit swInit;
   SinkSWTerm swTerm;
   SinkSWLink swLink;
   SinkSWLink swUnLink;
   SinkSWEvent swEvent;
   SinkSWDisplay swDisplay;
   #endif

   ProcessPadEvent processPadEvent;

   void *mediaCaptureModule;
   MediaCaptureDestroyContext mediaCaptureDestroyContext;
   void *mediaCaptureContext;

   gboolean enableTimeCodeSignal;
   int timeCodeCapacity;
   int timeCodeCount;
   WstSinkTimeCode timeCodeActive;
   WstSinkTimeCode *timeCodes;
   SinkTimeCodePresent timeCodePresent;

   struct _GstWesterosSinkSoc soc;
};

struct _GstWesterosSinkClass
{
   GstBaseSinkClass parent_class;
   int canUseResMgr;

};

GType gst_westeros_sink_get_type (void);

G_END_DECLS

#ifdef GLIB_VERSION_2_32
  #define LOCK( sink ) g_mutex_lock( &((sink)->mutex) );
  #define UNLOCK( sink ) g_mutex_unlock( &((sink)->mutex) );
#else
  #define LOCK( sink ) g_mutex_lock( (sink)->mutex );
  #define UNLOCK( sink ) g_mutex_unlock( (sink)->mutex );
#endif

void gst_westeros_sink_eos_detected( GstWesterosSink *sink );

#endif

