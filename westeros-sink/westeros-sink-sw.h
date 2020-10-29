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
#ifndef __WESTEROS_SINK_SW_H__
#define __WESTEROS_SINK_SW_H__

/*
 * Events that may be passed to a swEvent method
 */
typedef enum _SWEvt
{
   SWEvt_none= 0,
   SWEvt_pause= 1, /* p1: 0 unpause, 1 pause, p2: unused */
   SWEvt_max= 2
} SWEvt;

/*
 * Decoded frame information
 */
typedef struct _SWFrame
{
   int width;
   int height;
   unsigned char *Y;
   unsigned char *U;
   unsigned char *V;
   int Ystride;
   int Ustride;
   int Vstride;
   int frameNumber;
   long long pts;
} SWFrame;

void wstsw_process_caps( GstWesterosSink *sink, GstCaps *caps );
void wstsw_set_codec_init_data( GstWesterosSink *sink, int initDataLen, uint8_t *initData );
bool wstsw_render( GstWesterosSink *sink, GstBuffer *buffer );
static gboolean wstsw_null_to_ready( GstWesterosSink *sink, gboolean *passToDefault );
static gboolean wstsw_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault );
static gboolean wstsw_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault );
static gboolean wstsw_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault );
static gboolean wstsw_paused_to_ready( GstWesterosSink *sink, gboolean *passToDefault );
static gboolean wstsw_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault );

#endif

