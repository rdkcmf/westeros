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
#ifndef _SOC_VIDEO_SRC_H
#define _SOC_VIDEO_SRC_H

#include <pthread.h>

#include "../westeros-ut-em.h"

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

#define EM_TYPE_VIDEO_SRC em_video_src_get_type()

#define EM_VIDEO_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), EM_TYPE_VIDEO_SRC, EMVideoSrc))
#define EM_VIDEO_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), EM_TYPE_VIDEO_SRC, EMVideoSrcClass))
#define EM_IS_VIDEO_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), EM_TYPE_VIDEO_SRC))
#define EM_IS_VIDEO_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), EM_TYPE_VIDEO_SRC))
#define EM_VIDEO_SRC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), EM_TYPE_VIDEO_SRC, EMVideoSrcClass))

typedef struct _EMVideoSrc EMVideoSrc;
typedef struct _EMVideoSrcClass EMVideoSrcClass;

struct _EMVideoSrc
{
   GstBaseSrc parent;
   GstPadQueryFunction defaultQueryFunc;
   EMCTX *ctx;
   EMSimpleVideoDecoder *dec;
   bool paused;
   int frameNumber;
   pthread_mutex_t mutex;
   gint64 segStartTime;
   gint64 segStopTime;
   gdouble segRate;
   gdouble segAppliedRate;
   bool needSegment;
};

struct _EMVideoSrcClass
{
   GstBaseSrcClass parent_class;
};

GType em_video_src_get_type() G_GNUC_CONST;

GstElement* createVideoSrc( EMCTX *emctx, EMSimpleVideoDecoder *dec );
int videoSrcGetFrameNumber( GstElement *element );
void videoSrcSetFrameSize( GstElement *element, int width, int height );

#endif

