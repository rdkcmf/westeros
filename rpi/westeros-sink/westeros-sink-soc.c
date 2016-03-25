#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include "westeros-sink.h"

#define WESTEROS_UNUSED(x) ((void)(x))

#define FRAME_POLL_TIME (8000)
#define EOS_DETECT_DELAY (500000)
#define EOS_DETECT_DELAY_AT_START (10000000)

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


gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink )
{
   gboolean result= FALSE;

   //TBD
   
   return result;
}

void gst_westeros_sink_soc_term( GstWesterosSink *sink )
{
   if ( sink->soc.sb )
   {
      wl_sb_destroy( sink->soc.sb );
      sink->soc.sb= 0;
   }

   //TBD
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
   WESTEROS_UNUSED(passToDefault);
   
   //TBD

   return TRUE;
}

gboolean gst_westeros_sink_soc_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);
   
   //TBD
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(passToDefault);
   
   //TBD
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   LOCK( sink );
   sink->videoStarted= FALSE;
   UNLOCK( sink );
   
   //TBD
   
   *passToDefault= false;
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_paused_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   LOCK( sink );
   sink->videoStarted= FALSE;
   UNLOCK( sink );
   
   //TBD

   *passToDefault= false;
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(sink);
   
   //TBD

   *passToDefault= false;
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_accept_caps( GstWesterosSink *sink, GstCaps *caps )
{
   bool result= FALSE;
   
   //TBD

   return result;   
}

void gst_westeros_sink_soc_set_startPTS( GstWesterosSink *sink, gint64 pts )
{
   unsigned int pts45k= (unsigned int)( pts / 2 );
   
   //TBD
}

void gst_westeros_sink_soc_flush( GstWesterosSink *sink )
{
}

gboolean gst_westeros_sink_soc_start_video( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   
   //TBD
 
   return result;   
}

