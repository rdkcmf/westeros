/*
 * Copyright (C) 2019 RDK Management
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

#include "westeros-sink-sw.h"

#if defined(__cplusplus)
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

#if defined(__cplusplus)
} // extern "C"
#endif

#include <unistd.h>

GST_DEBUG_CATEGORY_EXTERN (gst_westeros_sink_debug);
#define GST_CAT_DEFAULT gst_westeros_sink_debug

typedef struct _SWCtx
{
   AVCodec* codec;
   AVCodecContext* codecCtx;
   bool contextOpen;
   AVCodecParserContext* parserCtx;
   AVPacket *packet;
   AVFrame *frame;
   uint8_t *initData;
   int initDataLen;
   bool needInitData;
   bool active;
   bool paused;
   double frameRate;
   int outputFrameCount;
   gint64 prevFrameTime;
} SWCtx;

static bool initSWDecoder( GstWesterosSink *sink );
static void termSWDecoder( SWCtx *swCtx );


static bool initSWDecoder( GstWesterosSink *sink )
{
   bool result= false;
   int rc;
   SWCtx *swCtx= 0;

   swCtx= (SWCtx*)calloc( 1, sizeof(SWCtx) );
   if ( !swCtx )
   {
      GST_ERROR("initSWDecoder: no memory for SWCtx");
      goto exit;
   }
   swCtx->prevFrameTime= -1LL;
   swCtx->frameRate= 60.0;

   avcodec_register_all();
   swCtx->codec= avcodec_find_decoder(AV_CODEC_ID_H264);
   if ( !swCtx->codec )
   {
      GST_ERROR("initSWDecoder: unable to find decoder" );
      goto exit;
   }

   GST_DEBUG("name: %s\n", swCtx->codec->name );
   GST_DEBUG("long name: %s\n", swCtx->codec->long_name );
   GST_DEBUG("capabilities: %X\n", swCtx->codec->capabilities );

   swCtx->codecCtx= avcodec_alloc_context3( swCtx->codec );
   if ( !swCtx->codecCtx )
   {
      GST_ERROR("initSWDecoder: unable to allocate decoder context" );
      goto exit;
   }

   rc= avcodec_open2( swCtx->codecCtx, swCtx->codec, (AVDictionary**)NULL );
   if ( rc != 0 )
   {
      GST_ERROR("initSWDecoder: error opening decoder: rc %d", rc );
      goto exit;
   }
   swCtx->contextOpen= true;

   swCtx->parserCtx= av_parser_init( swCtx->codec->id );
   if ( !swCtx->parserCtx )
   {
      GST_ERROR("initSWDecoder: unable to allocate decode parser context" );
      goto exit;
   }

   swCtx->packet= av_packet_alloc();
   if ( !swCtx->packet )
   {
      GST_ERROR("initSWDecoder: unable to allocate decode packet" );
      goto exit;
   }

   swCtx->frame= av_frame_alloc();
   if ( !swCtx->frame )
   {
      GST_ERROR("initSWDecoder: unable to allocate decode frame" );
      goto exit;
   }

   swCtx->needInitData= true;

   sink->swCtx= swCtx;

   result= true;

exit:

   if ( !result )
   {
      if ( swCtx )
      {
         termSWDecoder( swCtx );
      }
   }
   return result;
}

static void termSWDecoder( SWCtx *swCtx )
{
   if ( swCtx->parserCtx )
   {
      av_parser_close( swCtx->parserCtx );
      swCtx->parserCtx= 0;
   }
   if ( swCtx->contextOpen )
   {
      avcodec_close( swCtx->codecCtx );
      swCtx->contextOpen= false;
   }
   if ( swCtx->codecCtx )
   {
      av_free( swCtx->codecCtx );
      swCtx->codecCtx= 0;
   }
   if ( swCtx->frame )
   {
      av_frame_free( &swCtx->frame );
      swCtx->frame= 0;
   }
   if ( swCtx->packet )
   {
      av_packet_free( &swCtx->packet );
      swCtx->packet= 0;
   }
   free( swCtx );
}

void wstsw_process_caps( GstWesterosSink *sink, GstCaps *caps )
{
   SWCtx *swCtx= (SWCtx*)sink->swCtx;
   if ( swCtx )
   {
      GstStructure *structure;
      gint num, denom;

      gchar *str= gst_caps_to_string(caps);
      g_print("westeros-sink: caps: (%s)\n", str);
      g_free( str );

      structure= gst_caps_get_structure(caps, 0);
      if( structure )
      {
         if ( gst_structure_get_fraction( structure, "framerate", &num, &denom ) )
         {
            if ( denom == 0 ) denom= 1;
            swCtx->frameRate= (double)num/(double)denom;
            if ( swCtx->frameRate <= 0.0 )
            {
               g_print("westeros-sink: caps have framerate of 0 - assume 60\n");
               swCtx->frameRate= 60.0;
            }
         }
         sink->soc.pixelAspectRatio= 1.0;
         if ( gst_structure_get_fraction( structure, "pixel-aspect-ratio", &num, &denom ) )
         {
            if ( (num <= 0) || (denom <= 0))
            {
               num= denom= 1;
            }
            sink->soc.pixelAspectRatio= (double)num/(double)denom;
            sink->soc.havePixelAspectRatio= TRUE;
            sink->soc.pixelAspectRatioChanged= TRUE;
         }
      }
   }
}

void wstsw_set_codec_init_data( GstWesterosSink *sink, int initDataLen, uint8_t *initData )
{
   SWCtx *swCtx= (SWCtx*)sink->swCtx;
   if ( swCtx )
   {
      GST_DEBUG("wstsw_set_codec_init_data: data %p len %d", initData, initDataLen);
      swCtx->initData= initData;
      swCtx->initDataLen= initDataLen;
   }
}

bool wstsw_render( GstWesterosSink *sink, GstBuffer *buffer )
{
   bool result= true;
   SWCtx *swCtx= (SWCtx*)sink->swCtx;

   GST_LOG("wstsw_render: buffer %p", buffer );

   while( swCtx->paused )
   {
      usleep( 1000 );
      if ( !swCtx->active )
      {      
         goto exit;
      }
   }

   if ( swCtx )
   {
      int rc;
      int inputLen, parsedLen, consumed;
      uint8_t *inputData, *parsedData;
      #ifdef USE_GST1
      GstMapInfo map;
      #endif
      int inSize= 0;
      unsigned char *inData= 0;
      int64_t pts= AV_NOPTS_VALUE;
      int64_t dts= AV_NOPTS_VALUE;

      if ( GST_BUFFER_PTS_IS_VALID(buffer) )
      {
         pts= GST_BUFFER_PTS(buffer);
      }
      if ( GST_BUFFER_DTS_IS_VALID(buffer) )
      {
         dts= GST_BUFFER_DTS(buffer);
      }
      #ifdef USE_GST1
      gst_buffer_map(buffer, &map, (GstMapFlags)GST_MAP_READ);
      inSize= map.size;
      inData= map.data;
      #else
      inSize= (int)GST_BUFFER_SIZE(buffer);
      inData= GST_BUFFER_DATA(buffer);
      #endif

      while( inSize > 0 )
      {
         if ( swCtx->needInitData && swCtx->initDataLen && swCtx->initData )
         {
            inputData= swCtx->initData;
            inputLen= swCtx->initDataLen;
            swCtx->needInitData= false;
         }
         else
         {
            if ( swCtx->needInitData )
            {
               bool foundSPS= false;
               int i;
               for ( i= 0; i < inSize-5; ++i )
               {
                  if ( (inData[i+0] == 0) &&
                       (inData[i+1] == 0) &&
                       (inData[i+2] == 0) &&
                       (inData[i+3] == 1) &&
                       (inData[i+4] == 0x67) )
                  {
                    GST_DEBUG("wstsw_render: found sps at offset %d", i);
                    swCtx->needInitData= false;
                    foundSPS= true;
                    inData= inData+i;
                    inSize= inSize-i;
                    break;
                  }
               }
               if ( !foundSPS )
               {
                  GST_DEBUG("wstsw_render: skipping data until sps");
                  break;
               }
            }

            inputData= (uint8_t*)inData;
            inputLen= inSize;
            inSize= 0;
         }

         while( inputLen > 0 )
         {
            parsedData= 0;
            parsedLen= 0;
            consumed= av_parser_parse2( swCtx->parserCtx,
                                        swCtx->codecCtx,
                                        &parsedData,
                                        &parsedLen,
                                        inputData,
                                        inputLen,
                                        pts,
                                        dts,
                                        0 );
            if ( consumed < 0 )
            {
               GST_ERROR("wstsw_render: av_parser_parse2 error: rc %d", consumed);
               goto exit;
            }

            inputData += consumed;
            inputLen -= consumed;

            if ( parsedLen )
            {
               swCtx->packet->data= parsedData;
               swCtx->packet->size= parsedLen;

               rc= avcodec_send_packet( swCtx->codecCtx, swCtx->packet );
               if ( rc != 0 )
               {      
                  GST_ERROR("wstsw_render: avdecode_send_packet error: rc %d", rc);
                  goto exit;
               }
               while ( rc >= 0 )
               {
                  SWFrame swFrame;

                  rc= avcodec_receive_frame( swCtx->codecCtx, swCtx->frame );
                  if ( (rc == AVERROR(EAGAIN)) || (rc == AVERROR_EOF) )
                  {
                     break;
                  }
                  else if ( rc < 0 )
                  {
                     GST_ERROR("wstsw_render: avdecode_receive_frame error: rc %d", rc);
                     goto exit;                  
                  }
                  GST_LOG("wst_render: frame %d %dx%d : format %d key_frame %d ptype %d coded_picture_number %d data: %p, %p, %p, %p",
                          swCtx->outputFrameCount,
                          swCtx->frame->width,
                          swCtx->frame->height,
                          swCtx->frame->format,
                          swCtx->frame->key_frame,
                          swCtx->frame->pict_type,
                          swCtx->frame->coded_picture_number,
                          swCtx->frame->data[0],
                          swCtx->frame->data[1],
                          swCtx->frame->data[2],
                          swCtx->frame->data[3]
                         );

                  if ( sink->swDisplay )
                  {
                     gint64 currFrameTime, currFramePTS;

                     swFrame.width= swCtx->frame->width;
                     swFrame.height= swCtx->frame->height;
                     swFrame.Y= swCtx->frame->data[0];
                     swFrame.Ystride= swCtx->frame->linesize[0];
                     swFrame.U= swCtx->frame->data[1];
                     swFrame.Ustride= swCtx->frame->linesize[1];
                     swFrame.V= swCtx->frame->data[2];
                     swFrame.Vstride= swCtx->frame->linesize[2];
                     swFrame.frameNumber= swCtx->outputFrameCount;
                     swFrame.pts= 0;

                     currFrameTime= g_get_monotonic_time();
                     if ( swCtx->prevFrameTime != -1LL )
                     {
                        gint64 framePeriod= currFrameTime-swCtx->prevFrameTime;
                        gint64 nominalFramePeriod= 1000000LL / swCtx->frameRate;
                        gint64 delay= (nominalFramePeriod-framePeriod);
                        GST_LOG("wstsw_render: time %lld prev_time %lld delay %lld", currFrameTime, swCtx->prevFrameTime, delay ); 
                        if ( (delay > 2) && (delay <= nominalFramePeriod) )
                        {
                           usleep( delay );
                           currFrameTime= g_get_monotonic_time();
                        }
                     }
                     swCtx->prevFrameTime= currFrameTime;

                     sink->position= sink->positionSegmentStart + ((swCtx->outputFrameCount * GST_SECOND) / swCtx->frameRate);
                     sink->currentPTS= sink->position / (GST_SECOND/90000LL);
                     GST_LOG("wstsw_render: POSITION: %" GST_TIME_FORMAT, GST_TIME_ARGS (sink->position));

                     sink->swDisplay( sink, &swFrame );
                  }

                  swCtx->outputFrameCount++;
               }
            }
         }
      }
   }
exit:
   if ( !swCtx->active )
   {
      result= false;
   }
   return result;
}

static gboolean wstsw_null_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   if ( initSWDecoder( sink ) )
   {
      if ( sink->swInit )
      {
         gst_base_sink_set_sync(GST_BASE_SINK(sink), TRUE);

         if ( sink->swInit( sink ) )
         {
            g_print("westerossink: using sw decode\n");
         }
         else
         {
            GST_ERROR("Failed to initialize sw decoding");
         }
      }
   }
   else
   {
      GST_ERROR("Failed to initialize sw decoder");
   }

   return TRUE;
}

static gboolean wstsw_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   SWCtx *swCtx= (SWCtx*)sink->swCtx;

   if ( sink->swLink )
   {
      sink->swLink( sink );
   }
   swCtx->active= true;

   return TRUE;
}

static gboolean wstsw_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault )
{
   SWCtx *swCtx= (SWCtx*)sink->swCtx;

   swCtx->paused= false;
   if ( sink->swEvent )
   {
      sink->swEvent( sink, SWEvt_pause, (int)swCtx->paused, 0 );
   }

   return TRUE;
}

static gboolean wstsw_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   SWCtx *swCtx= (SWCtx*)sink->swCtx;

   swCtx->paused= true;   
   if ( sink->swEvent )
   {
      sink->swEvent( sink, SWEvt_pause, (int)swCtx->paused, 0 );
   }

   return TRUE;
}

static gboolean wstsw_paused_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   SWCtx *swCtx= (SWCtx*)sink->swCtx;

   swCtx->active= false;
   if ( sink->swUnLink )
   {
      sink->swUnLink( sink );
   }

   return TRUE;
}

static gboolean wstsw_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault )
{
   if ( sink->swTerm )
   {
      sink->swTerm( sink );
   }
   if ( sink->swCtx )
   {
      SWCtx *swCtx= (SWCtx*)sink->swCtx;
      termSWDecoder( swCtx );
      sink->swCtx= 0;
   }

   return TRUE;
}

