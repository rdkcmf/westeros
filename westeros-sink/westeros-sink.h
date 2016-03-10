#ifndef __WESTEROS_SINK_H__
#define __WESTEROS_SINK_H__

#include "wayland-client.h"
#include "simpleshell-client-protocol.h"

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

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

#include "westeros-sink-soc.h"

struct _GstWesterosSink
{
   GstBaseSink parent;
   GstPadEventFunction parentEventFunc;
   
   GstPad *peerPad; 
   gboolean rejectPrerollBuffers;
   
   #ifdef GLIB_VERSION_2_32 
   GMutex mutex;
   #else
   GMutex *mutex;
   #endif
   
   int srcWidth;
   int srcHeight;

   int windowX;
   int windowY;
   int windowWidth;
   int windowHeight;
   
   bool visible;
   float opacity;
   float zorder;

   gboolean sinkDispatching;
   gboolean videoStarted;
   gboolean startAfterLink;
   
   gint64 startPTS;
   gint64 firstPTS;
   gint64 currentPTS;
   gint64 position;
   
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_simple_shell *shell;
   struct wl_compositor *compositor;
   struct wl_event_queue *queue;
   struct wl_surface *surface;
   uint32_t surfaceId;
   
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

