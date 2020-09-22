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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include "westeros-sink.h"

#define FRAME_POLL_TIME (8000)
#define EOS_DETECT_DELAY (500000)
#define EOS_DETECT_DELAY_AT_START (10000000)

GST_DEBUG_CATEGORY_EXTERN (gst_westeros_sink_debug);
#define GST_CAT_DEFAULT gst_westeros_sink_debug

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

void gst_westeros_sink_soc_class_init(GstWesterosSinkClass *klass)
{
   WESTEROS_UNUSED(klass);   
}

gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink )
{
   gboolean result= FALSE;

   //TBD
   
   return result;
}

void gst_westeros_sink_soc_term( GstWesterosSink *sink )
{
   //TBD
}

void gst_westeros_sink_soc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
   WESTEROS_UNUSED(object);
   WESTEROS_UNUSED(prop_id);
   WESTEROS_UNUSED(value);
   WESTEROS_UNUSED(pspec);
}

void gst_westeros_sink_soc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
   WESTEROS_UNUSED(object);
   WESTEROS_UNUSED(prop_id);
   WESTEROS_UNUSED(value);
   WESTEROS_UNUSED(pspec);
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
   if ( sink->soc.sb )
   {
      wl_sb_destroy( sink->soc.sb );
      sink->soc.sb= 0;
   }

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
   //TBD
}

void gst_westeros_sink_soc_render( GstWesterosSink *sink, GstBuffer *buffer )
{
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

void gst_westeros_sink_soc_eos_event( GstWesterosSink *sink )
{
   WESTEROS_UNUSED(sink);
}

void gst_westeros_sink_soc_set_video_path( GstWesterosSink *sink, bool useGfxPath )
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(useGfxPath);
}

void gst_westeros_sink_soc_update_video_position( GstWesterosSink *sink )
{
   WESTEROS_UNUSED(sink);
}

gboolean gst_westeros_sink_soc_query( GstWesterosSink *sink, GstQuery *query )
{
   gboolean result = FALSE;

   // TBD

   return result;
}

