/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2017 RDK Management
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


#include <gst/gstinfo.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <stdbool.h>
#include "westeros-sink.h"

#define SIZE 1024
#define SHM_FILE_PATH "/tmp/westeros-shm"

static void gst_westeros_buffer_pool_finalize (GObject * object);
#ifdef USE_GST1
static gboolean gst_westeros_soc_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
#else
static gboolean gst_westeros_soc_sink_event(GstPad *pad, GstEvent *event)
#endif
#define gst_westeros_buffer_pool_parent_class parent_class

G_DEFINE_TYPE (GstWesterosBufferPool,gst_westeros_buffer_pool, GST_TYPE_BUFFER_POOL);

#ifdef USE_GST1
static gboolean gst_westeros_soc_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(parent);
#else
static gboolean gst_westeros_soc_sink_event(GstPad *pad, GstEvent *event)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(gst_pad_get_parent(pad));
#endif
   gboolean result= TRUE;
   gboolean passToDefault= FALSE;

   switch (GST_EVENT_TYPE(event))
   {
      case GST_EVENT_CAPS:
      case GST_EVENT_FLUSH_START:
      case GST_EVENT_EOS:
      #ifdef USE_GST1
      case GST_EVENT_SEGMENT:
      #else
      case GST_EVENT_NEWSEGMENT:
      #endif
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

void gst_westeros_sink_soc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

void gst_westeros_sink_soc_get_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

static void destroy_shared_pool (SharedPool *sharedPool)
{
   munmap (sharedPool->buff, sharedPool->size);
   wl_shm_pool_destroy (sharedPool->shm_pool);
   free (sharedPool);
}

void gst_westeros_sink_soc_term (GstBaseSink *base_sink)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(base_sink);

   if(sink->soc.wos_shm)
      wl_shm_destroy(sink->soc.wos_shm);
   if(sink->soc.shared_pool) 
      destroy_shared_pool (sink->soc.shared_pool);
}
void gst_westeros_sink_soc_update_video_position(GstWesterosSink *sink)
{
}
void gst_westeros_sink_soc_set_video_path( GstWesterosSink *sink, uint32_t new_pathway)
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(new_pathway);
}
bool gst_westeros_sink_soc_null_to_ready(GstWesterosSink *sink, gboolean *passToDefault)
{
   return true;
}

bool gst_westeros_sink_soc_ready_to_paused(GstWesterosSink *sink, gboolean *passToDefault)
{
   return true;
}

bool gst_westeros_sink_soc_paused_to_playing (GstWesterosSink *sink, gboolean *passToDefault)
{
   wl_simple_shell_set_visible(sink->shell, sink->surfaceId, true);
   wl_simple_shell_set_geometry(sink->shell, sink->surfaceId,sink->windowX, sink->windowY,sink->windowWidth, sink->windowHeight);
   wl_display_flush( sink->display );
   return true;
}

bool gst_westeros_sink_soc_playing_to_paused(GstWesterosSink *sink, gboolean *passToDefault)
{
   return true;
}

bool gst_westeros_sink_soc_paused_to_ready (GstWesterosSink *sink, gboolean *passToDefault)
{
   return true;
}

bool gst_westeros_sink_soc_ready_to_null (GstWesterosSink *sink, gboolean *passToDefault)
{
   return true;
}


void gst_westeros_sink_soc_class_init(GstWesterosSinkClass *klass)
{
   GstBaseSinkClass *gstbasesink_class= (GstBaseSinkClass *) klass;

   gstbasesink_class->propose_allocation = GST_DEBUG_FUNCPTR (gst_westeros_sink_propose_allocation);
   gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_westeros_sink_set_caps);
   gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_westeros_video_sink_get_caps);


}
gboolean gst_westeros_sink_soc_query(GstWesterosSink *sink,GstQuery *query)
{
   return FALSE;
}

void gst_westeros_sink_soc_start_video(GstWesterosSink *sink)
{
}


void gst_westeros_sink_soc_flush(GstWesterosSink *sink)
{
}

gboolean gst_westeros_sink_soc_accept_caps(GstWesterosSink *sink,GstCaps *cap)
{
   return TRUE;
}


void gst_westeros_sink_soc_set_startPTS( GstWesterosSink sink,gint64  startPTS )
{
}

void gst_westeros_sink_soc_eos_event(GstWesterosSink *sink)
{
}

GstCaps *gst_westeros_video_sink_get_caps (GstBaseSink * bsink,
      GstCaps * filter)
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(bsink);
   GstCaps *sink_caps = gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD (sink));
   if (filter)
   {
      GstCaps *intersectCaps = gst_caps_intersect_full (filter, sink_caps, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (sink_caps);
      sink_caps = intersectCaps;
   }
   return sink_caps;
}

gboolean gst_westeros_sink_set_caps (GstBaseSink * bsink,
      GstCaps * caps)
{
   GstWesterosSink *sink = GST_WESTEROS_SINK(bsink);
   GstBufferPool *newbufferpool, *oldbufferpool;
   static GstAllocationParams params = { 0, 0, 0, 15, };
   GstVideoInfo gst_videoInfo;
   GstStructure *gst_structure;
   guint size;

   GST_LOG_OBJECT (sink, "set caps %" GST_PTR_FORMAT, caps);

   if (!gst_video_info_from_caps (&gst_videoInfo, caps))
      goto invalid_video_format;

   sink->srcWidth = gst_videoInfo.width;
   sink->srcHeight = gst_videoInfo.height;

   sink->soc.width = gst_videoInfo.width;
   sink->soc.height = gst_videoInfo.height;

   size = gst_videoInfo.size;


   if( !gst_westeros_sink_soc_setformat(&(sink->soc.format),caps) )
      goto invalid_video_format;

#ifdef revisit
   if (!(display->wos_formats & (1 << sink->format)))
   {
      GST_DEBUG_OBJECT (sink, "%s not available", gst_westeros_video_format_to_string (sink->format));
      return FALSE;
   }
#endif

   /* create a new pool for the new configuration */
   newbufferpool = gst_westeros_buffer_pool_new (sink);

   if (newbufferpool == NULL)
   {
      GST_DEBUG_OBJECT (sink, "Failed to create new pool");
      return FALSE;
   }
   gst_structure = gst_buffer_pool_get_config (newbufferpool);
   gst_buffer_pool_config_set_params (gst_structure, caps, size, 2, 0);
   gst_buffer_pool_config_set_allocator (gst_structure, NULL, &params);
   if (!gst_buffer_pool_set_config (newbufferpool, gst_structure))
      goto config_failed;

   oldbufferpool = sink->soc.wos_gstPool;
   sink->soc.wos_gstPool = newbufferpool;
   if (oldbufferpool)
      gst_object_unref (oldbufferpool);

   return TRUE;

config_failed:
   {
      GST_DEBUG_OBJECT (bsink, "failed to set config");
      return FALSE;
   }
invalid_video_format:
   {
      GST_DEBUG_OBJECT (sink,"Could not find video format from caps %" GST_PTR_FORMAT, caps);
      return FALSE;
   }

}

gboolean gst_westeros_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{

   GstWesterosSink *sink= GST_WESTEROS_SINK(bsink);
   GstBufferPool *gst_bufferpool;
   GstStructure *gst_config;
   GstCaps *caps;
   guint buffer_size;
   gboolean pool_wanted;

   gst_query_parse_allocation (query, &caps, &pool_wanted);

   if (caps == NULL)
      goto no_caps;

   LOCK ( sink );

   if ((gst_bufferpool = sink->soc.wos_gstPool))
      gst_object_ref (gst_bufferpool);

   UNLOCK ( sink );

   if (gst_bufferpool != NULL) 
   {
      GstCaps *pcaps;

      gst_config = gst_buffer_pool_get_config (gst_bufferpool);
      gst_buffer_pool_config_get_params (gst_config, &pcaps, &buffer_size, NULL, NULL);

      /*check whether different caps*/
      if (!gst_caps_is_equal (caps, pcaps)) 
      {
         gst_object_unref (gst_bufferpool);
         gst_bufferpool = NULL;
      }
      gst_structure_free (gst_config);
   }

   if (gst_bufferpool == NULL && pool_wanted) 
   {
      GST_DEBUG_OBJECT (sink, "create new pool");
      GstVideoInfo gst_Videoinfo;

      if (!gst_video_info_from_caps (&gst_Videoinfo, caps))
      {
         GST_DEBUG_OBJECT (sink, "Invalid Capabilities");
         goto invalid_caps;
      }
      gst_bufferpool = gst_westeros_buffer_pool_new (sink);
      buffer_size = gst_Videoinfo.size;

      gst_config = gst_buffer_pool_get_config (gst_bufferpool);
      gst_buffer_pool_config_set_params (gst_config, caps, buffer_size, 2, 0);
      if (!gst_buffer_pool_set_config (gst_bufferpool, gst_config))
      {
         GST_DEBUG_OBJECT (sink, "Setting Config Failed");
         goto configuration_failed;
      }
   }
   if (gst_bufferpool)
   {
      gst_query_add_allocation_pool (query, gst_bufferpool, buffer_size, 2, 0);
      gst_object_unref (gst_bufferpool);
   }

   return TRUE;
configuration_failed:
   {
      GST_DEBUG_OBJECT (bsink, "failed setting config");
      gst_object_unref (gst_bufferpool);
      return FALSE;
   }
no_caps:
   {
      GST_DEBUG_OBJECT (bsink, "no caps specified");
      return FALSE;
   }
invalid_caps:
   {
      GST_DEBUG_OBJECT (bsink, "invalid caps specified");
      return FALSE;
   }

}



void gst_westeros_sink_soc_registryHandleGlobalRemove(void *data,
      struct wl_registry *registry,
      uint32_t name)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;
}

static void shm_format (void *data,
      struct wl_shm *wos_shm,
      uint32_t gst_format)
{
   GstWesterosSink *sink =(GstWesterosSink*) data;
   sink->soc.format |= (1 << gst_format);
}

struct wl_shm_listener shm_listenter = { shm_format };

void gst_westeros_sink_soc_registryHandleGlobal(void *data,
      struct wl_registry *registry, uint32_t id,
      const char *interface, uint32_t version)
{
   GstWesterosSink *sink= (GstWesterosSink*) data;
   GstWesterosSinkSoc *soc = &sink->soc;
   if(strcmp (interface, "wl_shm") == 0)
   {
      soc->wos_shm = wl_registry_bind (registry, id, &wl_shm_interface, 1);
      wl_shm_add_listener (soc->wos_shm, &shm_listenter, sink);
   }
}

typedef struct
{
   uint32_t wos_format;
   GstVideoFormat gst_format;
} VideoFormat;

static const VideoFormat formats[] = {
#if G_BYTE_ORDER == G_BIG_ENDIAN
   {WL_SHM_FORMAT_XRGB8888, GST_VIDEO_FORMAT_xRGB},
   {WL_SHM_FORMAT_ARGB8888, GST_VIDEO_FORMAT_ARGB},
#else
   {WL_SHM_FORMAT_XRGB8888, GST_VIDEO_FORMAT_BGRx},
   {WL_SHM_FORMAT_ARGB8888, GST_VIDEO_FORMAT_BGRA},
#endif
};

static uint32_t gst_video_format_to_wos_format (GstVideoFormat gstFormat)
{
   guint index;

   for (index = 0; index < G_N_ELEMENTS (formats); index++)
      if (formats[index].gst_format == gstFormat)
         return formats[index].wos_format;

   GST_WARNING ("westeros video format not found");
   return -1;
}




gboolean gst_westeros_sink_soc_setformat(uint32_t * gst_format,
      GstCaps * caps)
{
   GstStructure *structure;
   GstVideoFormat gstFormat = GST_VIDEO_FORMAT_UNKNOWN;
   const gchar *format = NULL;

   structure = gst_caps_get_structure (caps, 0);
   format = gst_structure_get_string (structure, "format");
   gstFormat = gst_video_format_from_string (format);

   *gst_format = gst_video_format_to_wos_format (gstFormat);
   printf("gst_format = %u\n",*gst_format);

   return (*gst_format != -1);
}

gboolean gst_westeros_sink_soc_init(GstBaseSink *base_sink)
{

   GstWesterosSink *sink= GST_WESTEROS_SINK(base_sink);
   if(sink) 
   {
      sink->soc.width = sink->srcWidth;
      sink->soc.height= sink->srcHeight;
      sink->soc.geometrySet = false;
      gst_pad_set_event_function(GST_BASE_SINK_PAD(sink), GST_DEBUG_FUNCPTR(gst_westeros_soc_sink_event));
      return TRUE;
   }
   return FALSE;
}

static void frame_redraw_handler (void *data, struct wl_callback *callback, uint32_t time)
{

   GstWesterosSink *sink= (GstWesterosSink*)data;

   sink->soc.redraw_check = FALSE;
   wl_callback_destroy (callback);
}

static const struct wl_callback_listener frame_callback_listener = {
   frame_redraw_handler
};


void gst_westeros_sink_soc_render(GstBaseSink *base_sink, GstBuffer *gst_buffer)
{
   GstWesterosSink *sink= GST_WESTEROS_SINK(base_sink);

   GstVideoRectangle source, dest, final;
   GstBuffer *to_render;
   GstWosMetaData *metadata;
   GstFlowReturn ret;
   GType type;


   type = gst_metadata_get_type ();
   metadata = (GstWosMetaData*)gst_buffer_get_meta(gst_buffer,type);

   if (sink->soc.redraw_check) {
      wl_display_dispatch_queue_pending (sink->display,sink->queue);
      wl_display_flush( sink->display );
   }

   if (metadata && metadata->sink == sink) 
   {
      GST_LOG_OBJECT (sink, "buffer %p from our pool, writing directly", gst_buffer);
      to_render = gst_buffer;
   } 
   else 
   {
      if (!sink->soc.wos_gstPool)
         goto pool_unavailable;

      GstMapInfo src;
      GST_LOG_OBJECT (sink, "buffer %p not from our pool, copying", gst_buffer);



      if (!gst_buffer_pool_set_active (sink->soc.wos_gstPool, TRUE))
         goto buffer_activate_failed;

      if((ret = gst_buffer_pool_acquire_buffer (sink->soc.wos_gstPool, &to_render, NULL)) != GST_FLOW_OK);
      goto buffer_unavailable;

      gst_buffer_map (gst_buffer, &src, GST_MAP_READ);
      gst_buffer_fill (to_render, 0, src.data, src.size);
      gst_buffer_unmap (gst_buffer, &src);

      metadata = gst_buffer_get_metadata(to_render);//note
   }
   dest.w = sink->windowWidth;
   dest.h = sink->windowHeight;
   source.w = sink->srcWidth;
   source.h = sink->srcHeight;

   wl_surface_attach (sink->surface, metadata->wos_buffer, 0, 0);
   gst_video_sink_center_rect (source, dest, &final, TRUE);
   wl_surface_damage (sink->surface, 0, 0, final.w, final.h);

   sink->soc.redraw_check = TRUE;
   sink->soc.callback = wl_surface_frame (sink->surface);
   wl_callback_add_listener (sink->soc.callback, &frame_callback_listener, sink);
   wl_surface_commit (sink->surface);
   wl_display_dispatch_queue_pending (sink->display,sink->queue);
   wl_display_flush( sink->display );
   wl_display_roundtrip_queue(sink->display,sink->queue);
   if (gst_buffer != to_render)
      gst_buffer_unref (to_render);
   return GST_FLOW_OK;

buffer_unavailable:
   {
      GST_WARNING_OBJECT (sink, "could not create image");
      return ;
   }
pool_unavailable:
   {
      GST_ERROR_OBJECT (sink,"We don't have a bufferpool negotiated");
      return ;
   }
buffer_activate_failed:
   {
      GST_ERROR_OBJECT (sink, "failed to activate bufferpool.");
      return ;
   }
}

GType  gst_metadata_get_type ()
{
   static volatile GType g_type;
   static const gchar *api_tags[] =
   { "memory", 
      "size", 
      "colorspace", 
      "orientation", 
      NULL 
   };

   if (g_once_init_enter (&g_type)) {
      GType _type = gst_meta_api_type_register ("GstWosMetaDataAPI", api_tags);
      g_once_init_leave (&g_type, _type);
   }
   return g_type;
}

GstWosMetaData*  gst_buffer_get_metadata(GstBuffer *gst_buffer)
{
   GType type = gst_metadata_get_type ();
   GstWosMetaData *metadata = (GstWosMetaData*)gst_buffer_get_meta(gst_buffer,type);
   return metadata;
}

static void gst_metadata_free (GstWosMetaData * metadata, GstBuffer *gst_buffer)
{
   gst_object_unref (metadata->sink);
   munmap (metadata->data, metadata->buffer_size);
   wl_buffer_destroy (metadata->wos_buffer);
}

const GstMetaInfo *gst_metadata_get_info ()
{
   static const GstMetaInfo *metainfo = NULL;
   if (g_once_init_enter (&metainfo)) 
   {
      const GstMetaInfo *gst_meta =
         gst_meta_register (GST_WOS_METADATA_GET_TYPE, "GstWosMetaData",sizeof (GstWosMetaData), (GstMetaInitFunction) NULL,
               (GstMetaFreeFunction) gst_metadata_free,
               (GstMetaTransformFunction) NULL);

      g_once_init_leave (&metainfo, gst_meta);
   }
   return metainfo;
}

static gboolean westeros_buffer_pool_set_config (GstBufferPool * gst_bufferpool, 
      GstStructure * gst_config)
{
   GstVideoInfo gst_videoInfo ;
   GstCaps *gst_caps;
   GstWesterosBufferPool *bufferpool = GST_WESTEROS_BUFFER_POOL_CAST (gst_bufferpool);

   GstWesterosSink *sink  = bufferpool->sink;

   if (!gst_buffer_pool_config_get_params (gst_config, &gst_caps, NULL, NULL, NULL))
      goto config_failed;

   if (gst_caps == NULL)
      goto caps_unavailable;

   if (!gst_video_info_from_caps (&gst_videoInfo, gst_caps))
      goto invalid_caps;

   GST_LOG_OBJECT (gst_bufferpool, "%dx%d, caps %" GST_PTR_FORMAT, gst_videoInfo.width, gst_videoInfo.height,gst_caps);


   bufferpool->gst_caps = gst_caps_ref (gst_caps);
   bufferpool->gst_VideoInfo = gst_videoInfo;
   bufferpool->VWidth = gst_videoInfo.width;
   bufferpool->VHeight = gst_videoInfo.height;

   return GST_BUFFER_POOL_CLASS (parent_class)->set_config (gst_bufferpool, gst_config);

   /* ERRORS */
config_failed:
   {
      GST_WARNING_OBJECT (gst_bufferpool, "invalid config");
      return FALSE;
   }
caps_unavailable:
   {
      GST_WARNING_OBJECT (gst_bufferpool, "no caps in config");
      return FALSE;
   }
invalid_caps:
   {
      GST_WARNING_OBJECT (gst_bufferpool,
            "failed getting geometry from caps %" GST_PTR_FORMAT, gst_caps);
      return FALSE;
   }
}

static struct wl_shm_pool* create_shm_pool (GstWesterosSink *sink, 
      int size,
      void **data)
{
   struct wl_shm_pool *shm_pool;
   char shm_filename[SIZE];
   static int count = 0;
   int fd;

   // Create temp uniq file
   snprintf (shm_filename, SIZE, "%s-%d-%s", SHM_FILE_PATH, count++, "XXXXXX");

   if( (fd = mkstemp (shm_filename)) < 0) 
   {
      GST_ERROR ("File  %s creation failed:", shm_filename);
      return NULL;
   }
   if (ftruncate (fd, size) < 0) 
   {
      GST_ERROR ("file %s truncate failed",shm_filename);
      close (fd);
      return NULL;
   }

   *data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); //map device to memory
   if (*data == MAP_FAILED) 
   {
      GST_ERROR ("mmap failed: ");
      close (fd);
      return NULL;
   }

   shm_pool = wl_shm_create_pool (sink->soc.wos_shm, fd, size);
   close (fd);
   return shm_pool;
}

SharedPool* createSharedPool (GstWesterosSink *sink,size_t buffer_size)
{
   SharedPool *shared_pool = (SharedPool*) malloc (sizeof(SharedPool));

   if (shared_pool == NULL)
      return NULL;

   shared_pool->shm_pool = create_shm_pool (sink, buffer_size, &shared_pool->buff);
   if (shared_pool->shm_pool == NULL) 
   {
      free (shared_pool);
      return NULL;
   }
   shared_pool->size = buffer_size;
   shared_pool->count = 0;

   return shared_pool;
}

static void *allocateSharedPool (SharedPool *sharedPool, 
      size_t size, 
      int *offset)
{
   void *data ;

   if ( (sharedPool->count + size) > sharedPool->size)
      return NULL;

   *offset = sharedPool->count;
   sharedPool->count += size;

   data = (char *) sharedPool->buff + *offset;
   return data;
}

static void shm_pool_reset (SharedPool *sharedPool)
{
   sharedPool->count = 0;
}

static GstWosMetaData *gst_buffer_add_metadata (GstBuffer * gst_buffer, GstWesterosBufferPool *pool)
{
   GstWesterosSink *sink  = pool->sink;
   SharedPool *shared_pool = sink->soc.shared_pool;

   GstWosMetaData *metadata = NULL;
   void *data;
   gint offset;
   guint stride,size;

   stride = pool->VWidth * 4;
   size =  pool->VHeight * stride;

   metadata = (GstWosMetaData *) gst_buffer_add_meta (gst_buffer, GST_WOS_METADATA_INFO, NULL);
   metadata->sink = gst_object_ref (sink);

   if (!shared_pool ) 
   {
      shared_pool = createSharedPool (sink, size * 15); 
      shm_pool_reset (shared_pool);
   }

   if (!shared_pool) 
      return NULL;

   sink->soc.shared_pool = shared_pool;
   data = allocateSharedPool (sink->soc.shared_pool, size, &offset); //revisit
   if (!data )
      return NULL;

   metadata->wos_buffer = wl_shm_pool_create_buffer (shared_pool->shm_pool, 
         offset,sink->srcWidth, sink->srcHeight, stride, sink->soc.format);
   metadata->data = data;
   metadata->buffer_size = size;

   gst_buffer_append_memory (gst_buffer,
         gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, data,
            size, 0, size, NULL, NULL));

   return metadata;
}

static GstFlowReturn westeros_buffer_pool_alloc (GstBufferPool * gst_pool, 
      GstBuffer ** gst_out_buffer, 
      GstBufferPoolAcquireParams * gst_bufferpoolParams)
{
   GstWesterosBufferPool *pool = GST_WESTEROS_BUFFER_POOL_CAST (gst_pool);
   GstWosMetaData *metadata ;

   GstBuffer *gst_buffer = gst_buffer_new ();
   metadata = gst_buffer_add_metadata (gst_buffer, pool);

   if (metadata == NULL) 
   {
      gst_buffer_unref (gst_buffer);
      goto buffer_unavailable;
   }
   *gst_out_buffer = gst_buffer;

   return GST_FLOW_OK;

buffer_unavailable:
   {
      GST_WARNING_OBJECT (gst_pool, "can't create buffer");
      return GST_FLOW_ERROR;
   }
}
static void gst_westeros_buffer_pool_finalize (GObject * object)
{
   GstWesterosBufferPool *pool = GST_WESTEROS_BUFFER_POOL_CAST (object);
   gst_object_unref (pool->sink);
   G_OBJECT_CLASS (gst_westeros_buffer_pool_parent_class)->finalize (object);
}



GstBufferPool *gst_westeros_buffer_pool_new (GstWesterosSink* sink)
{
   GstWesterosBufferPool *pool = NULL;
#ifdef revisit
   g_return_val_if_fail (GST_IS_WESTEROSVIDEOSINK (sink), NULL);
#endif
   pool = g_object_new (GST_TYPE_WESTEROS_BUFFER_POOL_TYPE, NULL);
   pool->sink = gst_object_ref (sink);
   return GST_BUFFER_POOL_CAST (pool);
}

static void gst_westeros_buffer_pool_class_init (GstWesterosBufferPoolClass * klass)
{
   GObjectClass *gobject_class             = (GObjectClass *) klass;
   GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

   gstbufferpool_class->alloc_buffer       = westeros_buffer_pool_alloc;
   gstbufferpool_class->set_config         = westeros_buffer_pool_set_config;
   gobject_class->finalize                 = gst_westeros_buffer_pool_finalize;
}

static void gst_westeros_buffer_pool_init (GstWesterosBufferPool * pool)
{
}

