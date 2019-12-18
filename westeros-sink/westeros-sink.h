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
#ifndef __WESTEROS_SINK_H__
#define __WESTEROS_SINK_H__

#include "wayland-client.h"
#include "simpleshell-client-protocol.h"
#include "vpc-client-protocol.h"

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#define DEFAULT_WINDOW_X (0)
#define DEFAULT_WINDOW_Y (0)
#define DEFAULT_WINDOW_WIDTH (1280)
#define DEFAULT_WINDOW_HEIGHT (720)

#define WESTEROS_UNUSED(x) ((void)(x))

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

typedef void* (*MediaCaptureCreateContext)( GstElement *element );
typedef void (*MediaCaptureDestroyContext)( void *context );

#define PROP_SOC_BASE (100)

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
   bool windowChange;
   bool windowSizeOverride;
   
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

   ProcessPadEvent processPadEvent;

   void *mediaCaptureModule;
   MediaCaptureDestroyContext mediaCaptureDestroyContext;
   void *mediaCaptureContext;

   struct _GstWesterosSinkSoc soc;
};

struct _GstWesterosSinkClass
{
   GstBaseSinkClass parent_class;

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

