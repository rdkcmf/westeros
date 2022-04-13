/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2018 RDK Management
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <gst/gst.h>

#include "soc-video-src.h"

static void emVideoSrcFinalize(GObject *object);
static GstStateChangeReturn emVideoSrcChangeState(GstElement *element, GstStateChange transition);
static gboolean emVideoSrcQuery(GstElement *element, GstQuery *query);
static gboolean emVideoSrcPadQuery(GstPad *pad, GstObject *parent, GstQuery *query);
static GstCaps *emVideoSrcGetCaps(GstBaseSrc *baseSrc, GstCaps *filter);
static gboolean emVideoSrcStart( GstBaseSrc *baseSrc );
static gboolean emVideoSrcStop( GstBaseSrc *baseSrc );
static gboolean emVideoSrcUnlock( GstBaseSrc *baseSrc );
static gboolean emVideoSrcUnlockStop( GstBaseSrc *baseSrc );
static gboolean emVideoSrcIsSeekable( GstBaseSrc *baseSrc );
static gboolean emVideoSrcDoSeek( GstBaseSrc *baseSrc, GstSegment *segment );
static void emVideoSrcLoop( GstPad *pad );

GST_DEBUG_CATEGORY_STATIC(EMVideoSrcDebug);
#define GST_CAT_DEFAULT EMVideoSrcDebug

#define em_video_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(EMVideoSrc, em_video_src, GST_TYPE_BASE_SRC, GST_DEBUG_CATEGORY_INIT(EMVideoSrcDebug, "emsrc", 0, "em video src"));

#define EM_SRC_CAPS \
           "video/x-h264;"

static GstStaticPadTemplate emVideoSrcPadTemplate=
  GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS(EM_SRC_CAPS));

static void em_video_src_class_init(EMVideoSrcClass* klass)
{
   GObjectClass *gobject_class= G_OBJECT_CLASS(klass);
   GstElementClass *gstelement_class= GST_ELEMENT_CLASS(klass);
   GstBaseSrcClass* basesrc_class = GST_BASE_SRC_CLASS(klass);

   gobject_class->finalize= emVideoSrcFinalize;

   gstelement_class->change_state= emVideoSrcChangeState;
   gstelement_class->query= emVideoSrcQuery;

   basesrc_class->get_caps= emVideoSrcGetCaps;
   basesrc_class->start= emVideoSrcStart;
   basesrc_class->stop= emVideoSrcStop;
   basesrc_class->unlock= emVideoSrcUnlock;
   basesrc_class->unlock_stop= emVideoSrcUnlockStop;
   basesrc_class->is_seekable= emVideoSrcIsSeekable;
   basesrc_class->do_seek= emVideoSrcDoSeek;

   gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&emVideoSrcPadTemplate));

   gst_element_class_set_metadata(gstelement_class, "em video sink", "Src/Video", "Westeros unit test framework video source", "Wannamaker, Jeff <jeff_wannamaker@cable.comcast.com>");
}

static void em_video_src_init(EMVideoSrc* src)
{
   src->defaultQueryFunc= GST_PAD_QUERYFUNC(GST_BASE_SRC_PAD(src));
   if ( src->defaultQueryFunc == NULL )
   {
      src->defaultQueryFunc= gst_pad_query_default;
   }

   gst_pad_set_query_function(GST_BASE_SRC_PAD(src), GST_DEBUG_FUNCPTR(emVideoSrcPadQuery));

   pthread_mutex_init( &src->mutex, 0 );
   src->paused= true;
   src->frameNumber= 0;
   src->needSegment= true;
   src->needStep= false;
   src->segRate= 1.0;
   src->segAppliedRate= 1.0;
   src->segStartTime= 0;
   src->segStopTime= -1;

   gst_base_src_set_format( GST_BASE_SRC(src), GST_FORMAT_TIME );
   gst_base_src_set_async( GST_BASE_SRC(src), TRUE );
}

static void emVideoSrcFinalize(GObject *object)
{
   GST_CALL_PARENT(G_OBJECT_CLASS, finalize, (object));
}

static GstStateChangeReturn emVideoSrcChangeState(GstElement *element, GstStateChange transition)
{
   GstStateChangeReturn result= GST_STATE_CHANGE_SUCCESS;
   EMVideoSrc *src= EM_VIDEO_SRC(element);

   GST_DEBUG_OBJECT(element, "em-src: change state from %s to %s\n", 
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

   switch (transition)
   {
      case GST_STATE_CHANGE_READY_TO_PAUSED:
         pthread_mutex_lock( &src->mutex );
         src->frameNumber= 0;
         src->needSegment= true;
         pthread_mutex_unlock( &src->mutex );
         break;
      case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
         pthread_mutex_lock( &src->mutex );
         src->paused= false;
         pthread_mutex_unlock( &src->mutex );
         break;
      case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
         pthread_mutex_lock( &src->mutex );
         src->paused= true;
         pthread_mutex_unlock( &src->mutex );
         break;
      default:
         break;
   }

   result= GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

   return result;
}

static gboolean emVideoSrcQuery(GstElement *element, GstQuery *query)
{
   gboolean rv= FALSE;

   switch (GST_QUERY_TYPE(query)) 
   {
      default:
         return GST_ELEMENT_CLASS(parent_class)->query(element, query);
   }
   return rv;
}

static gboolean emVideoSrcPadQuery(GstPad *pad, GstObject *parent, GstQuery *query)
{
   gboolean rv= FALSE;
   EMVideoSrc *src= EM_VIDEO_SRC(parent);

   switch (GST_QUERY_TYPE(query)) 
   {
      case GST_QUERY_CUSTOM:
         {
            // TBD
            #if 0
            GValue val= {0, };
            GstStructure *query_structure= (GstStructure*)gst_query_get_structure(query);
            const gchar *struct_name= gst_structure_get_name(query_structure);
            if (!strcasecmp(struct_name, "get_stc_channel_handle"))
            {
               g_value_init(&val, G_TYPE_POINTER);
               g_value_set_pointer(&val, (gpointer)EMGetStcChannel(src->ctx));

               gst_structure_set_value(query_structure, "stc_channel", &val);

               rv = TRUE;
            }
            else if (!strcasecmp(struct_name, "get_video_codec"))
            {
               g_value_init(&val, G_TYPE_INT);
               g_value_set_int(&val, EMGetVideoCodec(src->ctx));

               gst_structure_set_value(query_structure, "video_codec", &val);

               rv = TRUE;
            }
            else if (!strcasecmp(struct_name, "get_video_pid_channel"))
            {
               g_value_init(&val, G_TYPE_POINTER);
               g_value_set_pointer(&val, (gpointer)EMGetVideoPidChannel(src->ctx));

               gst_structure_set_value(query_structure, "video_pid_channel", &val);

               rv = TRUE;
            }
            else if (!strcasecmp(struct_name, "get_hdr_metadata"))
            {
               const char *value;
               value= EMSimpleVideoDecoderGetColorimetry(src->dec);
               g_value_init(&val, G_TYPE_STRING);
               if ( value && value[0] )
               {
                  g_value_set_string(&val, value);
                  gst_structure_set_value(query_structure, "colorimetry", &val);
                  rv = TRUE;
               }
               value= EMSimpleVideoDecoderGetMasteringMeta(src->dec);
               if ( value && value[0] )
               {
                  g_value_set_string(&val, value);
                  gst_structure_set_value(query_structure, "mastering_display_metadata", &val);
                  rv = TRUE;
               }
               value= EMSimpleVideoDecoderGetContentLight(src->dec);
               if ( value && value[0] )
               {
                  g_value_set_string(&val, value);
                  gst_structure_set_value(query_structure, "content_light_level", &val);
                  rv = TRUE;
               }
            }
            #endif
         }
         break;
      default:
         rv= src->defaultQueryFunc(pad, parent, query);
         break;
   }

   return rv;
}

static GstCaps *emVideoSrcGetCaps(GstBaseSrc *baseSrc, GstCaps *filter)
{
   GstCaps *caps= 0;
   EMVideoSrc *src= EM_VIDEO_SRC(baseSrc);
   float rate= 0;
   int width= 0, height= 0;
   int rate_num= 24, rate_denom= 1;

   GST_DEBUG("getcaps");

   rate= EMSimpleVideoDecoderGetFrameRate( src->dec );
   EMSimpleVideoDecoderGetVideoSize( src->dec, &width, &height );

   rate_num= (int)(rate*10.0);
   rate_denom= 10;

   caps= gst_caps_new_simple( "video/x-h264",
                              "width", G_TYPE_INT, width,
                              "height", G_TYPE_INT, height,
                              "framerate", GST_TYPE_FRACTION, rate_num, rate_denom,
                              "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
                               NULL );
   if ( caps )
   {
      if ( filter )
      {
         GstCaps *intersection= gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
         gst_caps_unref( caps );
         caps= intersection;
      }
   }

   return caps;
}

static gboolean emVideoSrcStart( GstBaseSrc *baseSrc )
{
   gboolean rv= TRUE;
   EMVideoSrc *src= EM_VIDEO_SRC(baseSrc);
   GstPad *pad= 0;

   GST_DEBUG("start");
   pad= GST_BASE_SRC_PAD(src);

   GST_PAD_STREAM_LOCK(pad);
   rv= gst_pad_start_task( pad, (GstTaskFunction)emVideoSrcLoop, pad, NULL );
   GST_PAD_STREAM_UNLOCK(pad);

   return rv;
}

static gboolean emVideoSrcStop( GstBaseSrc *baseSrc )
{
   gboolean rv= TRUE;
   EMVideoSrc *src= EM_VIDEO_SRC(baseSrc);
   GstPad *pad= 0;

   GST_DEBUG("stop");
   pad= GST_BASE_SRC_PAD(src);

   rv= gst_pad_stop_task( pad );

   return rv;
}

static gboolean emVideoSrcUnlock( GstBaseSrc *baseSrc )
{
   gboolean rv;
   EMVideoSrc *src= EM_VIDEO_SRC(baseSrc);
   GstPad *pad= 0;

   pad= GST_BASE_SRC_PAD(src);
   
   rv= gst_pad_stop_task( pad );

   return rv;
}


static gboolean emVideoSrcUnlockStop( GstBaseSrc *baseSrc )
{
   gboolean rv;
   EMVideoSrc *src= EM_VIDEO_SRC(baseSrc);
   GstPad *pad= 0;

   pad= GST_BASE_SRC_PAD(src);
   
   GST_PAD_STREAM_LOCK(pad);
   rv= gst_pad_start_task( pad, (GstTaskFunction)emVideoSrcLoop, pad, NULL );
   GST_PAD_STREAM_UNLOCK(pad);

   return rv;
}

static gboolean emVideoSrcIsSeekable( GstBaseSrc *baseSrc )
{
   return TRUE;
}

static gboolean emVideoSrcDoSeek( GstBaseSrc *baseSrc, GstSegment *segment )
{
   EMVideoSrc *src= EM_VIDEO_SRC(baseSrc);

   GST_DEBUG("doseek");
   pthread_mutex_lock( &src->mutex );
   src->needSegment= true;
   src->segRate= segment->rate;
   src->segAppliedRate= segment->applied_rate;
   src->segStartTime= segment->start;
   src->segStopTime= segment->stop;
   src->frameNumber= 0;
   EMSimpleVideoDecoderSetFrameNumber( src->dec, src->frameNumber );
   pthread_mutex_unlock( &src->mutex );

   return TRUE;
}

#define DATA_INTERVAL (16667)

static void emVideoSrcLoop( GstPad *pad )
{
   EMVideoSrc *src= EM_VIDEO_SRC(gst_pad_get_parent(pad));
   GstBuffer *buffer= 0;
   GstFlowReturn rv;
   int bufferSize;
   float frameRate;
   float bitRate;
   long long nanoTime;
   bool needStep= false;

   pthread_mutex_lock( &src->mutex );
   if ( src->paused  )
   {
      needStep= src->needStep;
      if ( needStep )
      {
         ++src->frameNumber;
      }
      src->needStep= false;
   }
   if ( !src->paused || src->needSegment || needStep )
   {
      frameRate= EMSimpleVideoDecoderGetFrameRate( src->dec );
      bitRate= EMSimpleVideoDecoderGetBitRate( src->dec );
      EMSimpleVideoDecoderSetFrameNumber( src->dec, src->frameNumber );
      pthread_mutex_unlock( &src->mutex );

      bufferSize= (DATA_INTERVAL*bitRate)/8;

      buffer= gst_buffer_new_allocate( 0, // default allocator
                                       bufferSize,
                                       0 ); // no allocation parameters
      if ( buffer )
      {
         GstMapInfo map;
         int width, height;

         EMSimpleVideoDecoderGetVideoSize( src->dec, &width, &height );
         gst_buffer_map(buffer, &map, (GstMapFlags)GST_MAP_READWRITE);
         if ( map.data && (map.size >= 8) )
         {
            map.data[0]= ((width>>24)&0xFF);
            map.data[1]= ((width>>16)&0xFF);
            map.data[2]= ((width>>8)&0xFF);
            map.data[3]= (width&0xFF);

            map.data[4]= ((height>>24)&0xFF);
            map.data[5]= ((height>>16)&0xFF);
            map.data[6]= ((height>>8)&0xFF);
            map.data[7]= (height&0xFF);
         }
         gst_buffer_unmap(buffer, &map);

         if ( src->needSegment )
         {
            GstSegment segment;
            GstEvent *newSegEvent;

            gst_segment_init( &segment, GST_FORMAT_TIME );
            segment.rate= src->segRate;
            segment.applied_rate= src->segAppliedRate;
            segment.start= src->segStartTime+(EMSimpleVideoDecoderGetBasePTS(src->dec)/90000.0)*GST_SECOND;
            segment.stop= src->segStopTime;
            segment.position= src->segStartTime;

            newSegEvent= gst_event_new_segment(&segment);

            GST_LOG("push segment");
            gst_pad_push_event( pad, newSegEvent );

            src->needSegment= false;
         }

         nanoTime= src->segStartTime+((EMSimpleVideoDecoderGetBasePTS(src->dec)/90000.0)+(src->frameNumber/frameRate))*GST_SECOND;

         GST_BUFFER_PTS(buffer)= nanoTime;

         GST_LOG("push buffer for frame %d", src->frameNumber);
         rv= gst_pad_push( pad, buffer );
         GST_LOG("done push buffer for frame %d rc %d", src->frameNumber, rv);
         if ( (rv != GST_FLOW_OK) && (rv != GST_FLOW_FLUSHING) )
         {
            g_print("Error: unable to push buffer: flow %d\n", rv);
            gst_buffer_unref( buffer );
         }

         GST_PAD_STREAM_UNLOCK(pad);
         usleep( DATA_INTERVAL );
         GST_PAD_STREAM_LOCK(pad);
      }
      else
      {
         g_print("Error: unable to allocate gstreamer buffer size %d\n", bufferSize);
      }   

      pthread_mutex_lock( &src->mutex );
      if ( !src->paused )
      {
         ++src->frameNumber;
      }
   }
   pthread_mutex_unlock( &src->mutex );
}

GstElement* createVideoSrc(EMCTX *emctx, EMSimpleVideoDecoder *dec)
{
   GstElement *element= 0;

   element= (GstElement*)g_object_new(EM_TYPE_VIDEO_SRC, NULL);
   if ( element )
   {
      EMVideoSrc *src= EM_VIDEO_SRC(element);
      src->ctx= emctx;
      src->dec= dec;
   }

   return element;
}

int videoSrcGetFrameNumber( GstElement *element )
{
   int frameNumber;
   EMVideoSrc *src= EM_VIDEO_SRC(element);

   pthread_mutex_lock( &src->mutex );

   frameNumber= src->frameNumber;

   pthread_mutex_unlock( &src->mutex );

   GST_DEBUG("query frame number: %d", frameNumber);

   return frameNumber;
}

void videoSrcSetFrameSize( GstElement *element, int width, int height )
{
   EMVideoSrc *src= EM_VIDEO_SRC(element);
   GstCaps *caps= 0;
   GstPad *pad= 0;
   GstEvent *event= 0;
   float rate= 0;
   int rate_num= 24, rate_denom= 1;

   GST_DEBUG("set frame size: %dx%d", width, height);

   rate= EMSimpleVideoDecoderGetFrameRate( src->dec );
   EMSimpleVideoDecoderGetVideoSize( src->dec, &width, &height );

   rate_num= (int)(rate*10.0);
   rate_denom= 10;

   pad= GST_BASE_SRC_PAD(src);

   caps= gst_caps_new_simple( "video/x-h264",
                              "width", G_TYPE_INT, width,
                              "height", G_TYPE_INT, height,
                              "framerate", GST_TYPE_FRACTION, rate_num, rate_denom,
                               NULL );
   if ( caps )
   {
      event= gst_event_new_caps( caps );
      if ( event )
      {
         GST_PAD_STREAM_LOCK(pad);
         gst_pad_push_event( pad, gst_event_new_flush_start() );
         gst_pad_push_event( pad, gst_event_new_flush_stop(TRUE) );
         gst_pad_push_event( pad, event );
         src->frameNumber= -1;
         src->needSegment= true;
         GST_PAD_STREAM_UNLOCK(pad);
      }
   }
}

void videoSrcDoStep( GstElement *element )
{
   EMVideoSrc *src= EM_VIDEO_SRC(element);

   pthread_mutex_lock( &src->mutex );

   GST_DEBUG("need step");
   src->needStep= true;

   pthread_mutex_unlock( &src->mutex );
}
