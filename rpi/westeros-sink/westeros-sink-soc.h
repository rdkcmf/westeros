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
#ifndef __WESTEROS_SINK_SOC_H__
#define __WESTEROS_SINK_SOC_H__

#include <stdlib.h>
#include <semaphore.h>

#include "simplebuffer-client-protocol.h"

#include "IL/OMX_Core.h"
#include "IL/OMX_Broadcom.h"

#define WESTEROS_SINK_CAPS \
            "video/x-h264, " \
      "parsed=(boolean) true, " \
      "alignment=(string) au, " \
      "stream-format=(string) byte-stream, " \
      "width=(int) [1,MAX], " "height=(int) [1,MAX]" 

typedef void (*BcmHostInit_t)(void);
typedef void (*BcmHostDeinit_t)(void);

typedef OMX_ERRORTYPE (*OMX_Init_t)(void);
typedef OMX_ERRORTYPE (*OMX_Deinit_t)(void);
typedef OMX_ERRORTYPE (*OMX_GetHandle_t)(OMX_HANDLETYPE *handle, OMX_STRING name, OMX_PTR data, OMX_CALLBACKTYPE *callbacks);
typedef OMX_ERRORTYPE (*OMX_FreeHandle_t)(OMX_HANDLETYPE handle);
typedef OMX_ERRORTYPE (*OMX_SetupTunnel_t)(OMX_HANDLETYPE output, OMX_U32 outport, OMX_HANDLETYPE input, OMX_U32 inport );

typedef struct _OMX_AsyncResult
{
   bool done;
   OMX_EVENTTYPE eEvent;
   OMX_U32 nData1;
   OMX_U32 nData2;
} OMX_AsyncResult;

typedef struct _WstOmxComponent
{
   bool isOpen;
   const char *name;
   OMX_HANDLETYPE hComp;
   OMX_VERSIONTYPE specVersion;
   OMX_VERSIONTYPE compVersion;
   OMX_U32 vidInPort;
   OMX_U32 vidOutPort;
   OMX_U32 otherInPort;
   OMX_U32 otherOutPort;
   OMX_AsyncResult asyncVidIn;
   OMX_AsyncResult asyncVidOut;
   OMX_AsyncResult asyncOtherIn;
   OMX_AsyncResult asyncOtherOut;
} WstOmxComponent;

struct _GstWesterosSinkSoc
{
   struct wl_sb *sb;
   int activeBuffers;
   
   void *moduleBcmHost;
   bool bcmHostIsInit;
   BcmHostInit_t bcm_host_init;
   BcmHostDeinit_t bcm_host_deinit;
   
   void *moduleOpenMax;
   bool omxIsInit;
   OMX_Init_t OMX_Init;
   OMX_Deinit_t OMX_Deinit;
   OMX_GetHandle_t OMX_GetHandle;
   OMX_FreeHandle_t OMX_FreeHandle;
   OMX_SetupTunnel_t OMX_SetupTunnel;

   OMX_AsyncResult asyncStateSet;
   OMX_AsyncResult asyncError;

   WstOmxComponent vidDec;
   WstOmxComponent vidSched;
   WstOmxComponent clock;
   WstOmxComponent vidRend;
   WstOmxComponent eglRend;
   WstOmxComponent *rend;
   
   bool tunnelActiveClock;
   bool tunnelActiveVidDec;
   bool tunnelActiveVidSched;
   
   bool firstBuffer;
   bool decoderReady;
   bool schedReady;
   bool playingVideo;
   bool useGfxPath;
   
   bool semInputActive;
   sem_t semInputBuffers;
   unsigned int capacityInputBuffers;
   unsigned int countInputBuffers;
   OMX_BUFFERHEADERTYPE** inputBuffers;
   
   OMX_BUFFERHEADERTYPE *buffCurrent;

   #ifdef GLIB_VERSION_2_32 
   GMutex mutex;
   #else
   GMutex *mutex;
   #endif
   gboolean quitCaptureThread;
   GThread *captureThread;
};

void gst_westeros_sink_soc_class_init(GstWesterosSinkClass *klass);
gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink );
void gst_westeros_sink_soc_term( GstWesterosSink *sink );
void gst_westeros_sink_soc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
void gst_westeros_sink_soc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
gboolean gst_westeros_sink_soc_null_to_ready( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_paused_to_ready( GstWesterosSink *sink, gboolean *passToDefault );
gboolean gst_westeros_sink_soc_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault );
void gst_westeros_sink_soc_registryHandleGlobal( GstWesterosSink *sink, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version);
void gst_westeros_sink_soc_registryHandleGlobalRemove(GstWesterosSink *sink,
                                 struct wl_registry *registry,
			                        uint32_t name);
gboolean gst_westeros_sink_soc_accept_caps( GstWesterosSink *sink, GstCaps *caps );
void gst_westeros_sink_soc_set_startPTS( GstWesterosSink *sink, gint64 pts );
void gst_westeros_sink_soc_render( GstWesterosSink *sink, GstBuffer *buffer );
void gst_westeros_sink_soc_flush( GstWesterosSink *sink );
gboolean gst_westeros_sink_soc_start_video( GstWesterosSink *sink );
void gst_westeros_sink_soc_eos_event( GstWesterosSink *sink );
void gst_westeros_sink_soc_set_video_path( GstWesterosSink *sink, bool useGfxPath );
void gst_westeros_sink_soc_update_video_position( GstWesterosSink *sink );
gboolean gst_westeros_sink_soc_query( GstWesterosSink *sink, GstQuery *query );

#endif
