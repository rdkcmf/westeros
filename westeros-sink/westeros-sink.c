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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include "westeros-sink.h"

#define DEFAULT_WINDOW_X (0)
#define DEFAULT_WINDOW_Y (0)
#define DEFAULT_WINDOW_WIDTH (1280)
#define DEFAULT_WINDOW_HEIGHT (720)

#define GST_PACKAGE_ORIGIN "http://gstreamer.net/"

#define WESTEROS_UNUSED(x) ((void)(x))

static GstStaticPadTemplate gst_westeros_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(WESTEROS_SINK_CAPS));

GST_DEBUG_CATEGORY_STATIC (gst_westeros_sink_debug);
#define GST_CAT_DEFAULT gst_westeros_sink_debug

enum
{
  PROP_0,
  PROP_WINDOW_SET,
  PROP_ZORDER,
  PROP_OPACITY
};

#ifdef USE_GST1
#define gst_westeros_sink_parent_class parent_class
G_DEFINE_TYPE (GstWesterosSink, gst_westeros_sink, GST_TYPE_BASE_SINK)
#else
GST_BOILERPLATE (GstWesterosSink, gst_westeros_sink, GstBaseSink, GST_TYPE_BASE_SINK)
#endif

static void gst_westeros_sink_term(GstWesterosSink *sink); 
static void gst_westeros_sink_finalize(GObject *object); 
static void gst_westeros_sink_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_westeros_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_westeros_sink_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_westeros_sink_query(GstElement *element, GstQuery *query);
static gboolean gst_westeros_sink_start(GstBaseSink *base_sink);
static gboolean gst_westeros_sink_stop(GstBaseSink *base_sink);
static gboolean gst_westeros_sink_unlock(GstBaseSink *base_sink);
static gboolean gst_westeros_sink_unlock_stop(GstBaseSink *base_sink);
static gboolean gst_westeros_sink_check_caps(GstWesterosSink *sink, GstPad *peer);
#ifdef USE_GST1
static gboolean gst_westeros_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static GstPadLinkReturn gst_westeros_sink_link(GstPad *pad, GstObject *parent, GstPad *peer);
static void gst_westeros_sink_unlink(GstPad *pad, GstObject *parent);
#else
static gboolean gst_westeros_sink_event(GstPad *pad, GstEvent *event);
static GstPadLinkReturn gst_westeros_sink_link(GstPad *pad, GstPad *peer);
static void gst_westeros_sink_unlink(GstPad *pad);
#endif
static GstFlowReturn gst_westeros_sink_render(GstBaseSink *base_sink, GstBuffer *buffer);
static GstFlowReturn gst_westeros_sink_preroll(GstBaseSink *base_sink, GstBuffer *buffer);


static void shellSurfaceId(void *data,
                           struct wl_simple_shell *wl_simple_shell,
                           struct wl_surface *surface,
                           uint32_t surfaceId)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   sink->surfaceId= surfaceId;
   char name[32];
   wl_fixed_t z, op;
   WESTEROS_UNUSED(wl_simple_shell);
   WESTEROS_UNUSED(surface);

	sprintf( name, "westeros-sink-surface-%x", surfaceId );	
	wl_simple_shell_set_name( sink->shell, surfaceId, name );
   if ( (sink->windowWidth == 0) || (sink->windowHeight == 0) )
   {
      wl_simple_shell_set_visible( sink->shell, sink->surfaceId, false);
   }
   else
   {
      wl_simple_shell_set_geometry( sink->shell, surfaceId, sink->windowX, sink->windowY, sink->windowWidth, sink->windowHeight );
      z= wl_fixed_from_double(sink->zorder);
      wl_simple_shell_set_zorder( sink->shell, sink->surfaceId, z);
      op= wl_fixed_from_double(sink->opacity);
      wl_simple_shell_set_opacity( sink->shell, sink->surfaceId, op);
      wl_simple_shell_get_status( sink->shell, sink->surfaceId );
   }
   wl_display_flush(sink->display);
}                           

static void shellSurfaceCreated(void *data,
                                struct wl_simple_shell *wl_simple_shell,
                                uint32_t surfaceId,
                                const char *name)
{
   WESTEROS_UNUSED(data);
   WESTEROS_UNUSED(wl_simple_shell);
   WESTEROS_UNUSED(surfaceId);
   WESTEROS_UNUSED(name);
}
                                
static void shellSurfaceDestroyed(void *data,
                                  struct wl_simple_shell *wl_simple_shell,
                                  uint32_t surfaceId,
                                  const char *name)
{
   WESTEROS_UNUSED(data);
   WESTEROS_UNUSED(wl_simple_shell);
   WESTEROS_UNUSED(surfaceId);
   WESTEROS_UNUSED(name);
}
                                  
static void shellSurfaceStatus(void *data,
                               struct wl_simple_shell *wl_simple_shell,
                               uint32_t surfaceId,
                               const char *name,
                               uint32_t visible,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height,
                               wl_fixed_t opacity,
                               wl_fixed_t zorder)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   WESTEROS_UNUSED(wl_simple_shell);
   WESTEROS_UNUSED(surfaceId);
   WESTEROS_UNUSED(name);
   sink->visible= visible;
   sink->windowX= x;
   sink->windowY= y;
   sink->windowWidth= width;
   sink->windowHeight= height;
   sink->opacity= opacity;
   sink->zorder= zorder;
}

static void shellGetSurfacesDone(void *data, struct wl_simple_shell *wl_simple_shell )
{
   WESTEROS_UNUSED(data);
   WESTEROS_UNUSED(wl_simple_shell);
}

static const struct wl_simple_shell_listener shellListener = 
{
   shellSurfaceId,
   shellSurfaceCreated,
   shellSurfaceDestroyed,
   shellSurfaceStatus,
   shellGetSurfacesDone
};

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version);
static void registryHandleGlobalRemove(void *data, 
                                       struct wl_registry *registry,
			                              uint32_t name);

static const struct wl_registry_listener registryListener = 
{
	registryHandleGlobal,
	registryHandleGlobalRemove
};

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
   int len;

   printf("westeros-sink: registry: id %d interface (%s) version %d\n", id, interface, version );
   
   len= strlen(interface);
   if ((len==13) && (strncmp(interface, "wl_compositor",len) == 0)) 
   {
      sink->compositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
      printf("westeros-sink: compositor %p\n", (void*)sink->compositor);
      wl_proxy_set_queue((struct wl_proxy*)sink->compositor, sink->queue);
   }
   else if ((len==15) && (strncmp(interface, "wl_simple_shell",len) == 0)) 
   {
      sink->shell= (struct wl_simple_shell*)wl_registry_bind(registry, id, &wl_simple_shell_interface, 1);
      printf("westeros-sink: shell %p\n", (void*)sink->shell);
      wl_proxy_set_queue((struct wl_proxy*)sink->shell, sink->queue);
      wl_simple_shell_add_listener(sink->shell, &shellListener, sink);
   }
   
   gst_westeros_sink_soc_registryHandleGlobal( sink, registry, id, interface, version );

   wl_display_flush(sink->display);
}

static void registryHandleGlobalRemove(void *data, 
                                       struct wl_registry *registry,
			                              uint32_t name)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;

   gst_westeros_sink_soc_registryHandleGlobalRemove( sink, registry, name );
}


#ifndef USE_GST1
static void gst_westeros_sink_base_init(gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_westeros_sink_debug, "westerossink", 0, "westerossink element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_westeros_sink_pad_template));
  gst_element_class_set_details_simple (gstelement_class, "Westeros Sink",
      "Sink",
      "Writes buffers to the westeros wayland compositor",
      "Comcast");
}
#endif

static void gst_westeros_sink_class_init(GstWesterosSinkClass *klass)
{
   GObjectClass *gobject_class= (GObjectClass *) klass;
   GstElementClass *gstelement_class= (GstElementClass *) klass;
   GstBaseSinkClass *gstbasesink_class= (GstBaseSinkClass *) klass;
   
   gobject_class->finalize= gst_westeros_sink_finalize;
   gobject_class->set_property= gst_westeros_sink_set_property;
   gobject_class->get_property= gst_westeros_sink_get_property;
   
   gstelement_class->change_state= gst_westeros_sink_change_state;
   gstelement_class->query= gst_westeros_sink_query;
   
   gstbasesink_class->start= GST_DEBUG_FUNCPTR (gst_westeros_sink_start);
   gstbasesink_class->stop= GST_DEBUG_FUNCPTR (gst_westeros_sink_stop);
   gstbasesink_class->unlock= GST_DEBUG_FUNCPTR (gst_westeros_sink_unlock);
   gstbasesink_class->unlock_stop= GST_DEBUG_FUNCPTR (gst_westeros_sink_unlock_stop);
   gstbasesink_class->render= GST_DEBUG_FUNCPTR (gst_westeros_sink_render);
   gstbasesink_class->preroll= GST_DEBUG_FUNCPTR (gst_westeros_sink_preroll);   

   g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WINDOW_SET,
       g_param_spec_string ("window_set", "window set",
           "Window Set Format: x,y,width,height",
           NULL, G_PARAM_WRITABLE));

   g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WINDOW_SET,
       g_param_spec_string ("rectangle", "rectangle",
           "Window Set Format: x,y,width,height",
           NULL, G_PARAM_WRITABLE));

   g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ZORDER,
       g_param_spec_float ("zorder", "zorder",
           "zorder from 0.0 (lowest) to 1.0 (highest)",
           0.0, 1.0, 0.0, G_PARAM_WRITABLE));

   g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_OPACITY,
       g_param_spec_float ("opacity", "opacity",
           "opacity from 0.0 (transparent) to 1.0 (opaque)",
           0.0, 1.0, 1.0, G_PARAM_WRITABLE));

#ifdef USE_GST1
  GST_DEBUG_CATEGORY_INIT (gst_westeros_sink_debug, "westerossink", 0, "westerossink element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_westeros_sink_pad_template));
  gst_element_class_set_details_simple (gstelement_class, "Westeros Sink",
      "Sink",
      "Writes buffers to the westeros wayland compositor",
      "Comcast");
#endif
}

static void 
#ifdef USE_GST1
gst_westeros_sink_init(GstWesterosSink *sink)
{
#else
gst_westeros_sink_init(GstWesterosSink *sink, GstWesterosSinkClass *gclass) 
{
   WESTEROS_UNUSED(gclass);
#endif
   
   sink->peerPad= NULL;
   
   sink->parentEventFunc = GST_PAD_EVENTFUNC(GST_BASE_SINK_PAD(sink));

   gst_pad_set_event_function(GST_BASE_SINK_PAD(sink), GST_DEBUG_FUNCPTR(gst_westeros_sink_event));
   gst_pad_set_link_function(GST_BASE_SINK_PAD(sink), GST_DEBUG_FUNCPTR(gst_westeros_sink_link));
   gst_pad_set_unlink_function(GST_BASE_SINK_PAD(sink), GST_DEBUG_FUNCPTR(gst_westeros_sink_unlink));
    
   gst_base_sink_set_sync(GST_BASE_SINK(sink), FALSE);
   gst_base_sink_set_async_enabled(GST_BASE_SINK(sink), FALSE);

   sink->initialized= TRUE;
   
   #ifdef GLIB_VERSION_2_32 
   g_mutex_init( &sink->mutex );
   #else
   sink->mutex= g_mutex_new();
   #endif

   sink->videoStarted= FALSE;
   sink->startAfterLink= FALSE;	
   sink->rejectPrerollBuffers= FALSE;
   
   sink->srcWidth= 1280;
   sink->srcHeight= 720;
    
   sink->windowX= DEFAULT_WINDOW_X;
   sink->windowY= DEFAULT_WINDOW_Y;
   sink->windowWidth= DEFAULT_WINDOW_WIDTH;
   sink->windowHeight= DEFAULT_WINDOW_HEIGHT;
   
   sink->visible= false;
   
   sink->opacity= 1.0;
   sink->zorder= 0.0;
   
   sink->eosDetected= FALSE;
   sink->startPTS= 0;
   sink->firstPTS= 0;
   sink->currentPTS= 0;
   sink->position= 0;

   if ( gst_westeros_sink_soc_init( sink ) == TRUE )
   {
      sink->display= 0;
      sink->registry= 0;
      sink->shell= 0;
      sink->compositor= 0;
      sink->surfaceId= 0;

      sink->display= wl_display_connect(NULL);

      if ( sink->display )
      {
         sink->queue= wl_display_create_queue(sink->display);
         if ( sink->queue )
         {
            sink->registry= wl_display_get_registry( sink->display );
            if ( sink->registry )
            {
               wl_proxy_set_queue((struct wl_proxy*)sink->registry, sink->queue);
               wl_registry_add_listener(sink->registry, &registryListener, sink);   
               wl_display_roundtrip_queue(sink->display,sink->queue);
               
               sink->surface= wl_compositor_create_surface(sink->compositor);
               printf("gst_westeros_sink_init: surface=%p\n", (void*)sink->surface);
               wl_proxy_set_queue((struct wl_proxy*)sink->surface, sink->queue);
               
               wl_display_flush( sink->display );
            }
            else
            {
               GST_ERROR("gst_westeros_sink_init: unable to get display registry\n");
            }
         }
         else
         {
            GST_ERROR("gst_westeros_sink_init: unable to create queue\n");
         }
      }
      else
      {
         GST_ERROR("gst_westeros_sink_init: unable to create display\n");
      }
   }
   else
   {
      GST_ERROR("gst_westeros_sink_init: soc_init failed\n");
   }
}

static void gst_westeros_sink_term(GstWesterosSink *sink)
{
   sink->initialized= FALSE;
   
   gst_westeros_sink_soc_term( sink );
   
   if ( sink->surface )
   {
      wl_surface_destroy( sink->surface );
      sink->surface= 0;
   }
   if ( sink->compositor )
   {
      wl_compositor_destroy( sink->compositor );
      sink->compositor= 0;
   }
   if ( sink->shell )
   {
      wl_simple_shell_destroy( sink->shell );
      sink->shell= 0;
   }
   if ( sink->registry )
   {
      wl_registry_destroy(sink->registry);
      sink->registry= 0;
   }
   if ( sink->queue )
   {
      wl_event_queue_destroy( sink->queue );
      sink->queue= 0;
   }
   if ( sink->display )
   {
      printf("gst_westeros_sink_finalize: wl_display_disconnect: display=%p\n", (void*)sink->display);
      wl_display_disconnect(sink->display);
      sink->display= 0;
   }
   
   #ifdef GLIB_VERSION_2_32 
   g_mutex_clear( &sink->mutex );
   #else
   g_mutex_free( sink->mutex );
   #endif  
}

static void gst_westeros_sink_finalize(GObject *object) 
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(object);

   if ( sink->initialized )
   {
      gst_westeros_sink_term( sink );
   }

   GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void gst_westeros_sink_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(object);
  
   WESTEROS_UNUSED(pspec);
   WESTEROS_UNUSED(value);
   WESTEROS_UNUSED(sink);
    
   switch (prop_id) 
   {
      case PROP_WINDOW_SET:
      {
         const gchar *str= g_value_get_string(value);
         gchar **parts= g_strsplit(str, ",", 4);
         
         if ( !parts[0] || !parts[1] || !parts[2] || !parts[3] )
         {
            GST_ERROR( "Bad window properties string" );
         }
         else
         {         
            sink->windowX= atoi( parts[0] );
            sink->windowY= atoi( parts[1] );
            sink->windowWidth= atoi( parts[2] );
            sink->windowHeight= atoi( parts[3] );
            
            if ( sink->shell && sink->surfaceId )
            {
               wl_simple_shell_set_geometry( sink->shell, sink->surfaceId, 
                                             sink->windowX, sink->windowY, 
                                             sink->windowWidth, sink->windowHeight );
                                             
               printf("gst_westeros_sink_set_property set window rect (%d,%d,%d,%d)\n", 
                       sink->windowX, sink->windowY, sink->windowWidth, sink->windowHeight );
                       
               if ( (sink->windowWidth > 0) && (sink->windowHeight > 0 ) )
               {
                  wl_simple_shell_set_visible( sink->shell, sink->surfaceId, true);
                  
                  wl_simple_shell_get_status( sink->shell, sink->surfaceId);
                  
                  wl_display_flush( sink->display );
               }
            }            
                    
            sink->srcWidth= sink->windowWidth;
            sink->srcHeight= sink->windowHeight;
         }

         g_strfreev(parts);
         break;
      }
      
      case PROP_ZORDER:
      {
         sink->zorder= g_value_get_float(value);
         if ( sink->shell )
         {
            wl_fixed_t z= wl_fixed_from_double(sink->zorder);
            wl_simple_shell_set_zorder( sink->shell, sink->surfaceId, z);
         }
         break;
      }
      
      case PROP_OPACITY:
      {
         sink->opacity= g_value_get_float(value);
         if ( sink->shell )
         {
            wl_fixed_t op= wl_fixed_from_double(sink->opacity);
            wl_simple_shell_set_opacity( sink->shell, sink->surfaceId, op);
         }
         break;
      }
   }
}

static void gst_westeros_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(object);
  
   WESTEROS_UNUSED(pspec); 
   WESTEROS_UNUSED(value);
   WESTEROS_UNUSED(sink);
    
   switch (prop_id) 
   {
   }
}

static GstStateChangeReturn gst_westeros_sink_change_state(GstElement *element, GstStateChange transition)
{
   GstStateChangeReturn result= GST_STATE_CHANGE_SUCCESS;
   GstWesterosSink *sink= GST_WESTEROS_SINK(element);
   gboolean passToDefault= true;

   GST_DEBUG_OBJECT(element, "westeros-sink: change state from %s to %s\n", 
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

   sink->rejectPrerollBuffers= false;

   if (GST_STATE_TRANSITION_CURRENT(transition) == GST_STATE_TRANSITION_NEXT(transition))
   {
      return GST_STATE_CHANGE_SUCCESS;
   }

   switch (transition) 
   {
      case GST_STATE_CHANGE_NULL_TO_READY:
      {
         sink->position= 0;         
         sink->eosDetected= FALSE;
         if ( !gst_westeros_sink_soc_null_to_ready(sink, &passToDefault) )
         {
            result= GST_STATE_CHANGE_FAILURE;
         }         
         break;
      }

      case GST_STATE_CHANGE_READY_TO_PAUSED:
      {
         if ( gst_westeros_sink_soc_ready_to_paused(sink, &passToDefault) )
         {
            sink->rejectPrerollBuffers = !gst_base_sink_is_async_enabled(GST_BASE_SINK(sink));
         }
         else
         {
            result= GST_STATE_CHANGE_FAILURE;
         }
         break;
      }

      case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      {
         if ( !gst_westeros_sink_soc_paused_to_playing( sink, &passToDefault) )
         {
            result= GST_STATE_CHANGE_FAILURE;
         }
         break;
	   }

      case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      {
         if ( gst_westeros_sink_soc_playing_to_paused( sink, &passToDefault ) )
         {
            sink->rejectPrerollBuffers = !gst_base_sink_is_async_enabled(GST_BASE_SINK(sink));
         }
         break;
      }

      case GST_STATE_CHANGE_PAUSED_TO_READY:
      {
         sink->eosDetected= FALSE;
         if ( gst_westeros_sink_soc_paused_to_ready( sink, &passToDefault ) )
         {
            sink->rejectPrerollBuffers = !gst_base_sink_is_async_enabled(GST_BASE_SINK(sink));
         }
         break;
      }

      case GST_STATE_CHANGE_READY_TO_NULL:
      {
         if ( sink->initialized )
         {
            gst_westeros_sink_term( sink );
            if ( !gst_westeros_sink_soc_ready_to_null( sink, &passToDefault ) )
            {
               result= GST_STATE_CHANGE_FAILURE;
            }
         }
         break;
      }

      default:
         break;
   }
  
   if (result == GST_STATE_CHANGE_FAILURE)
   {
      return result;
   }
   
   if ( passToDefault )
   {
      result= GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
   }
 
   return result;
}

static gboolean gst_westeros_sink_query(GstElement *element, GstQuery *query)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(element);

   switch (GST_QUERY_TYPE(query)) 
   {
      case GST_QUERY_LATENCY:
         gst_query_set_latency(query, FALSE, 0, 10*1000*1000);
         return TRUE;
   
      case GST_QUERY_POSITION:
         {
            GstFormat format;
            
            gst_query_parse_position(query, &format, NULL);
            
            if ( GST_FORMAT_BYTES == format )
            {
               return GST_ELEMENT_CLASS(parent_class)->query(element, query);
            }
            else
            {
               LOCK( sink );
               gst_query_set_position(query, GST_FORMAT_TIME, sink->position);
               UNLOCK( sink );
               return TRUE;
            }
         }
         break;
         
      case GST_QUERY_CUSTOM:
      case GST_QUERY_DURATION:
      case GST_QUERY_SEEKING:
      case GST_QUERY_RATE:
         if (sink->peerPad)
         {
            return gst_pad_query(sink->peerPad, query);
         }
              
      default:
         return GST_ELEMENT_CLASS(parent_class)->query (element, query);
   }
}

static gboolean gst_westeros_sink_start(GstBaseSink *base_sink)
{
   WESTEROS_UNUSED(base_sink);

   return TRUE;
}

static gboolean gst_westeros_sink_stop(GstBaseSink *base_sink)
{
   WESTEROS_UNUSED(base_sink);

   return TRUE;
}

static gboolean gst_westeros_sink_unlock(GstBaseSink *base_sink)
{
   WESTEROS_UNUSED(base_sink);
  
   return TRUE;
}

static gboolean gst_westeros_sink_unlock_stop(GstBaseSink *base_sink)
{
   WESTEROS_UNUSED(base_sink);

   return TRUE;
}

#ifdef USE_GST1
static gboolean gst_westeros_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(parent);
#else
static gboolean gst_westeros_sink_event(GstPad *pad, GstEvent *event)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(gst_pad_get_parent(pad));
#endif
   gboolean result= TRUE;
   gboolean passToDefault= FALSE;

   switch (GST_EVENT_TYPE(event))
   {
      case GST_EVENT_FLUSH_START:
         sink->position= 0;
         gst_westeros_sink_soc_flush( sink );
         passToDefault= TRUE;
         break;
          
      case GST_EVENT_EOS:
         gst_element_post_message (GST_ELEMENT_CAST(sink), gst_message_new_eos(GST_OBJECT_CAST(sink)));
         break;
         
      #ifdef USE_GST1
      case GST_EVENT_SEGMENT:
      #else
      case GST_EVENT_NEWSEGMENT:
      #endif
         {
            gint64 segmentStart, segmentPosition;
            GstFormat segmentFormat;

            #ifdef USE_GST1
            const GstSegment *dataSegment;
            gst_event_parse_segment(event, &dataSegment);
            segmentFormat= dataSegment->format;
            segmentStart= dataSegment->start;
            segmentPosition= dataSegment->position;
            #else
            gst_event_parse_new_segment(event, NULL, NULL, 
                                        &segmentFormat, &segmentStart, 
                                        NULL, &segmentPosition);
            #endif
            
            GST_LOG_OBJECT(sink, 
                           "segment: start %" GST_TIME_FORMAT ", stop %" GST_TIME_FORMAT,
                            GST_TIME_ARGS(segmentStart), GST_TIME_ARGS(segmentPosition));

            if ( 
                 (segmentFormat == GST_FORMAT_TIME) && 
                 ( (segmentStart != 0) || (sink->startPTS != 0) )
               ) 
            {
               sink->startPTS= (GST_TIME_AS_MSECONDS(segmentStart)*90LL);
               sink->position= GST_TIME_AS_NSECONDS(segmentStart);
               gst_westeros_sink_soc_set_startPTS( sink, sink->startPTS );
            }
         }
         break;
       default:
         passToDefault= TRUE;
         break;
   }

   if (passToDefault && sink->parentEventFunc)
   {
      #ifdef USE_GST1
      result= sink->parentEventFunc(pad, parent, event);
      #else
      result= sink->parentEventFunc(pad, event);
      #endif
   }
   else
   {
      gst_event_ref(event);
   }

   #ifndef USE_GST1
   gst_object_unref(sink);
   #endif
  
   return result;
}

static gboolean gst_westeros_sink_check_caps(GstWesterosSink *sink, GstPad *peer)
{
   WESTEROS_UNUSED(sink);

   gboolean result= TRUE;
   GstCaps* caps= NULL;

#ifdef USE_GST1
   caps= gst_pad_query_caps(peer, NULL);
#else
   caps= gst_pad_get_caps(peer);
#endif
  
   if (gst_caps_get_size(caps) == 0)
   {
      result= TRUE;
      goto exit;
   }

   if ( !gst_westeros_sink_soc_accept_caps( sink, caps ) )  
   {
      result= FALSE;
      goto exit;
   }

exit:
   if ( caps )
   {
      gst_caps_unref(caps);
   }

   return result;
}

#ifdef USE_GST1
GstPadLinkReturn gst_westeros_sink_link(GstPad *pad, GstObject *parent, GstPad *peer)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(parent);
#else
static GstPadLinkReturn gst_westeros_sink_link(GstPad *pad, GstPad *peer)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(gst_pad_get_parent(pad));
#endif
   GstElement *pElement;

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_link: enter");
   
   if (gst_westeros_sink_check_caps(sink, peer) != TRUE)
   {
      GST_ERROR("Peer Caps is not supported\n");
   }

   gchar *peerName= gst_pad_get_name(peer);
   pElement= (GstElement*)gst_pad_get_parent_element(pad); 
   if (pElement)
   {
      gst_object_unref(pElement);
   }

   pElement= (GstElement*)gst_pad_get_parent_element(peer);
   if(pElement)
   {
      gst_object_unref(pElement);
   }

   sink->peerPad= peer;
   g_free(peerName);  
   
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_link: startAfterLink %d", sink->startAfterLink);
   if ( sink->startAfterLink )
   {
      sink->startAfterLink= FALSE;
      gst_westeros_sink_soc_start_video( sink );      
   }

   return GST_PAD_LINK_OK;
}

#ifdef USE_GST1
static void gst_westeros_sink_unlink(GstPad *pad, GstObject *parent)
{
   WESTEROS_UNUSED(pad);
   GstWesterosSink *sink= GST_WESTEROS_SINK(parent);
#else
static void gst_westeros_sink_unlink(GstPad *pad)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(gst_pad_get_parent(pad));
#endif

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_unlink");
   sink->peerPad= NULL;
   
   return;
}

static GstFlowReturn gst_westeros_sink_render(GstBaseSink *base_sink, GstBuffer *buffer)
{  
   GstWesterosSink *sink= GST_WESTEROS_SINK(base_sink);
   
   gst_westeros_sink_soc_render( sink, buffer );

   return GST_FLOW_OK;
}

static GstFlowReturn gst_westeros_sink_preroll(GstBaseSink *base_sink, GstBuffer *buffer)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(base_sink);

   WESTEROS_UNUSED(buffer);
   
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_preroll: enter: rejectPrerollBuffers: %d", sink->rejectPrerollBuffers);
   if (sink->rejectPrerollBuffers)
   {
      #ifdef USE_GST1
      return GST_FLOW_FLUSHING;
      #else
      return GST_FLOW_WRONG_STATE;
      #endif
   }

   return GST_FLOW_OK;
}

void gst_westeros_sink_eos_detected( GstWesterosSink *sink )
{
   sink->eosDetected= TRUE;
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_eos_detected: posting EOS");
   gst_element_post_message (GST_ELEMENT_CAST(sink), gst_message_new_eos(GST_OBJECT_CAST(sink)));   
}

static gboolean westeros_sink_init (GstPlugin * plugin)
{
   return gst_element_register (plugin, "westerossink", GST_RANK_NONE, gst_westeros_sink_get_type ());
}

#ifndef PACKAGE
#define PACKAGE "mywesterossink"
#endif

#ifdef USE_GST1
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    westerossink,
    "Writes buffers to the westeros wayland compositor",
    westeros_sink_init, 
    VERSION, 
    "LGPL", 
    PACKAGE_NAME,
    GST_PACKAGE_ORIGIN )
#else
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
   "westerossink",
    "Writes buffers to the westeros wayland compositor",
    westeros_sink_init, 
    VERSION, 
    "LGPL", 
    PACKAGE_NAME,
    GST_PACKAGE_ORIGIN )
#endif


