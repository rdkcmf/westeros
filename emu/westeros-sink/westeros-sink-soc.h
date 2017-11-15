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



#ifndef __GST_WESTEROS_BUFFER_POOL_H__
#define __GST_WESTEROS_BUFFER_POOL_H__

#include "westeros-sink.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/gstvideometa.h>
#if G_BYTE_ORDER == G_BIG_ENDIAN
#define SINK_CAPS "{xRGB, ARGB}"
#else
#define SINK_CAPS "{BGRx, BGRA}"
#endif


#define WESTEROS_SINK_CAPS GST_VIDEO_CAPS_MAKE(SINK_CAPS)

G_BEGIN_DECLS
#define GST_IS_WESTEROSVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WESTEROSVIDEOSINK))

/* buffer pool functions */
#define GST_TYPE_WESTEROS_BUFFER_POOL_TYPE      (gst_westeros_buffer_pool_get_type())
#define GST_IS_WESTEROS_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WESTEROS_BUFFER_POOL))
#define GST_WESTEROS_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WESTEROS_BUFFER_POOL, GstWesterosBufferPool))
#define GST_WESTEROS_BUFFER_POOL_CAST(obj) ((GstWesterosBufferPool*)(obj))

#define GST_WOS_METADATA_GET_TYPE  (gst_metadata_get_type())
#define GST_WOS_METADATA_INFO  (gst_metadata_get_info())

#define SIZE 1024
#define SHM_FILE_PATH "/tmp/westeros-shm"





struct _GstWesterosBufferPool
{
  GstBufferPool gst_bufferpool;
  GstWesterosSink *sink;
  GstCaps *gst_caps;
  GstVideoInfo gst_VideoInfo;
  guint VWidth;
  guint VHeight;
};

struct _GstWesterosBufferPoolClass
{
  GstBufferPoolClass parent_class;
};


struct _GstWosMetaData {
  GstMeta gst_metadata;
  GstWesterosSink *sink;
  struct wl_buffer *wos_buffer;
  size_t buffer_size;
  void *data;
};

struct _SharedPool {
  struct wl_shm_pool *shm_pool;
  size_t size;
  size_t count;
  void *buff;
};


typedef struct _SharedPool SharedPool;
typedef struct _GstWosMetaData GstWosMetaData;
typedef struct _GstWesterosBufferPool GstWesterosBufferPool;
typedef struct _GstWesterosBufferPoolClass GstWesterosBufferPoolClass;
typedef struct _GstWesterosSinkSoc GstWesterosSinkSoc;

struct _GstWesterosSinkSoc
{
   struct _GstWosMetaData GstWosMetaData;
   struct _GstWesterosBufferPool GstWesterosBufferPool;	
   struct _GstWesterosBufferPoolClass GstWesterosBufferPoolClass;

   GstBufferPool *wos_gstPool;
   SharedPool *shared_pool;
   struct wl_shm *wos_shm;
   struct wl_callback *callback;
   guint redraw_check :1;
   uint32_t format;
   int width;
   int height;
   bool geometrySet;
};


GType gst_metadata_get_type (void);
GType gst_westeros_buffer_pool_get_type (void);
GstBufferPool *gst_wayland_buffer_pool_new (GstWesterosSink * sink);
const GstMetaInfo * gst_wos_metadata_get_info (void);
GType  gst_metadata_get_type ();
GstWosMetaData*  gst_buffer_get_metadata(GstBuffer *gst_buffer);
const GstMetaInfo *gst_metadata_get_info ();

gboolean gst_westeros_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query);
gboolean gst_westeros_sink_set_caps (GstBaseSink * bsink,GstCaps * caps);
GstBufferPool *gst_westeros_buffer_pool_new (GstWesterosSink* sink);

GstCaps* gst_westeros_video_sink_get_caps (GstBaseSink * bsink,
                                  GstCaps * filter);
G_END_DECLS

#endif /*__GST_WESTEROS_BUFFER_POOL_H__*/
