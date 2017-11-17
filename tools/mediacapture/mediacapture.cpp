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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <gst/gst.h>
#include "gst/app/gstappsrc.h"
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <vector>

#include <curl/curl.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// Uncomment to capture each ES buffer in its own file
//#define MEDIACAPTURE_CAPTURE_ES_BUFFERS

// Uncomment to auto start capture based on the prescence of a trigger file
//#define MEDIACAPTURE_USE_TRIGGER_FILE

#ifdef MEDIACAPTURE_USE_RTREMOTE
#include <rtRemote.h>

class rtMediaCaptureObject;

#define RTREMOTE_NAME "media-%d-%d"
#define RTREMOTE_SERVERNAME "mediacaptureregistry"
#endif

#define DEFAULT_PACKET_SIZE (188)
#define VIDEO_ACCUM_SIZE (10000*DEFAULT_PACKET_SIZE)
#define AUDIO_ACCUM_SIZE (2000*DEFAULT_PACKET_SIZE)
#define VIDEO_PTS_PCR_OFFSET (90000)
#define AUDIO_PTS_PCR_OFFSET (20000)
#define MAX_90K (0x1FFFFFFFFLL)
#define TICKS_90K (90000LL)
#define TICKS_90K_PER_MILLISECOND (90LL)
#define NANO_SECONDS (1.0e9)
#define PTS_OFFSET VIDEO_PTS_PCR_OFFSET
#define DECODE_TIME (10000)
#define POST_TIMEOUT (4000)
#define POST_CHUNK_SIZE (16*1024)
#define EMIT_BUFFER_SIZE (12*POST_CHUNK_SIZE)

#define min( a, b ) ( ((a) <= (b)) ? (a) : (b) )
#define max( a, b ) ( ((a) >= (b)) ? (a) : (b) )

typedef struct _MediaCapContext MediaCapContext;

typedef struct _SrcInfo
{
   MediaCapContext *ctx;
   GstElement *element;
   GstPad *pad;
   gboolean isDemux;
   gchar *mimeType;
   gint packetSize;
   gint mpegversion;
   gchar *codecData;
   gulong probeId;
   bool isVideo;
   bool isByteStream;
   bool needEmitSPSPPS;
   int bufferCount;
   int pid;
   int streamType;
   int streamId;
   int continuityCount;
   long long firstPTS;
   long long pts;
   long long dts;
   int spsppsLen;
   unsigned char *spspps;
   int audioPESHdrLen;
   unsigned char *audioPESHdr;
   int packetOffset;
   unsigned char packet[DEFAULT_PACKET_SIZE];
   long long accumTestPTS;
   long long accumFirstPTS;
   int firstBlockLen;
   long long accumLastPTS;
   int accumOffset;
   int accumSize;
   unsigned char *accumulator;
} SrcInfo;

typedef struct _MediaCapContext
{
   pthread_mutex_t mutex;
   GstElement *element;
   std::vector<SrcInfo*> srcList;
   bool captureToFile;
   FILE *pCaptureFile;
   long long captureDuration;
   long long captureStartTime;
   long long captureStopTime;
   bool prepared;
   bool canCapture;
   bool captureActive;
   bool needTSEncapsulation;
   bool needEmitPATPMT;
   bool needEmitPCR;
   int packetSize;
   int pmtPid;
   int pcrPid;
   int videoPid;
   long long ptsPcrOffset;
   unsigned char *patPacket;
   unsigned char *pmtPacket;
   int nextESPid;
   int nextVideoStreamId;
   int nextAudioStreamId;
   #ifdef MEDIACAPTURE_USE_RTREMOTE
   pthread_t apiThreadId;
   bool reportProgress;
   long long progressInterval;
   bool apiThreadStarted;
   bool apiThreadStopRequested;
   rtRemoteEnvironment *rtEnv;
   rtObjectRef apiObj;
   char rtName[32];
   #endif
   char *emitBuffer;
   int emitCapacity;
   int emitHead; // write pos
   int emitTail; // read pos
   int emitCount;
   long long totalBytesEmitted;
   long long totalBytesPosted;
   long long lastReportedPostedBytes;
   long long durationCaptured;
   long long lastEmittedPTS;
   pthread_mutex_t emitMutex;
   pthread_mutex_t emitNotEmptyMutex;
   pthread_cond_t emitNotEmptyCond;
   pthread_mutex_t emitNotFullMutex;
   pthread_cond_t emitNotFullCond;
   CURL *curl;
   int curlfd;
   char errorBuffer[CURL_ERROR_SIZE];
   pthread_t postThreadId;
   bool postThreadStarted;
   bool postThreadStopRequested;
   bool postThreadAborted;
   bool foundStartPoint;
   bool hitStopPoint;
   bool goodCapture;
   bool captureCompleteSent;
} MediaCapContext;

static void iprintf( int level, const char *fmt, ... );
static void initCRCTable();
static unsigned long getCRC(unsigned char *data, int size, int initial= 0xFFFFFFFF );
static void generatePAT( MediaCapContext *ctx );
static void generatePMT( MediaCapContext *ctx );
static unsigned char* getBinaryCodecData( gchar *codecData, int& dataLen );
static void generateSPSandPPS( MediaCapContext *ctx, SrcInfo *si );
static void updateAudioAACPESHeader( MediaCapContext *ctx, SrcInfo *si, int bufferSize );
static void generateAudioAACPESHeader( MediaCapContext *ctx, SrcInfo *si );
static long long getCurrentTimeMillis(void);
static void releaseSourceResources( SrcInfo *si );
static void freeSource(MediaCapContext *ctx, SrcInfo *si);
static void freeSources(MediaCapContext *ctx);
static void findSources(MediaCapContext *ctx);
static void prepareForCapture( MediaCapContext *ctx );
static void captureProbeDestroy( gpointer userData );
static GstPadProbeReturn captureProbe( GstPad *pad, GstPadProbeInfo *info, gpointer userData );
static bool prepareEndpoint( MediaCapContext *ctx, const char *endPoint );
static void startCapture( MediaCapContext *ctx, bool toFile, const char *dest, int duration );
static void stopCapture(MediaCapContext *ctx);
static void processCaptureData( MediaCapContext *ctx, SrcInfo *si, unsigned char *data, int len );
static bool readTimeStamp( unsigned char *p, long long& timestamp );
static int writeTimeStamp( unsigned char *p, long long pts, long long dts );
static int writePCR( unsigned char *p, long long pcr );
static bool discardCaptureData( MediaCapContext *ctx, long long startPTS );
static void flushCaptureData( MediaCapContext *ctx );
static void flushCaptureDataByTime( MediaCapContext *ctx, long long ptsLinit, long long nextPCR );
static void checkBufferLevels( MediaCapContext *ctx, long long pcr );
static void flushPacket( MediaCapContext *ctx, SrcInfo *si );
static void performEncapsulation( MediaCapContext *ctx, SrcInfo *si, unsigned char *data, int len );
static void addToPacket( MediaCapContext *ctx, SrcInfo *si, unsigned char *data, int len );
static void emitCaptureData( MediaCapContext *ctx, unsigned char *data, int len );

#define LEVEL_ALWAYS  (0)
#define LEVEL_FATAL   (0)
#define LEVEL_ERROR   (1)
#define LEVEL_WARNING (2)
#define LEVEL_INFO    (3)
#define LEVEL_DEBUG   (4)
#define LEVEL_TRACE1  (5)
#define LEVEL_TRACE2  (6)

#define INT_FATAL(FORMAT, ...)      iprintf(1, "Mediacapture Fatal: " FORMAT "\n", ##__VA_ARGS__); fflush(stdout)
#define INT_ERROR(FORMAT, ...)      iprintf(1, "Mediacapture Error: " FORMAT "\n", ##__VA_ARGS__); fflush(stdout)
#define INT_WARNING(FORMAT, ...)    iprintf(2, "Mediacapture Warning: " FORMAT "\n",  ##__VA_ARGS__); fflush(stdout)
#define INT_INFO(FORMAT, ...)       iprintf(3, "Mediacapture Info: " FORMAT "\n",  ##__VA_ARGS__); fflush(stdout)
#define INT_DEBUG(FORMAT, ...)      iprintf(4, "Mediacapture Debug: " FORMAT "\n", ##__VA_ARGS__)
#define INT_TRACE1(FORMAT, ...)     iprintf(5, "Mediacapture Trace: " FORMAT "\n", ##__VA_ARGS__)
#define INT_TRACE2(FORMAT, ...)     iprintf(6, "Mediacapture Trace: " FORMAT "\n", ##__VA_ARGS__)

#define FATAL(FORMAT, ...)          INT_FATAL(FORMAT, ##__VA_ARGS__)
#define ERROR(FORMAT, ...)          INT_ERROR(FORMAT, ##__VA_ARGS__)
#define WARNING(FORMAT, ...)        INT_WARNING(FORMAT, ##__VA_ARGS__)
#define INFO(FORMAT, ...)           INT_INFO(FORMAT, ##__VA_ARGS__)
#define DEBUG(FORMAT, ...)          INT_DEBUG(FORMAT, ##__VA_ARGS__)
#define TRACE1(FORMAT, ...)         INT_TRACE1(FORMAT, ##__VA_ARGS__)
#define TRACE2(FORMAT, ...)         INT_TRACE2(FORMAT, ##__VA_ARGS__)

static int gDebugLevel= 1;
static int gNextId= 0;
static pthread_mutex_t gMutex= PTHREAD_MUTEX_INITIALIZER;
#ifdef MEDIACAPTURE_USE_RTREMOTE
static bool gIsPrimaryStatusKnown= false;
static bool gIsPrimary= false;
static rtObjectRef gServerObj;
static std::vector<MediaCapContext*> gContextList= std::vector<MediaCapContext*>();
#endif

static void iprintf( int level, const char *fmt, ... )
{
   va_list argptr;

   if ( level <= gDebugLevel )
   {
      va_start( argptr, fmt );
      vfprintf( stdout, fmt, argptr );
      va_end( argptr );
   }
}

#ifdef MEDIACAPTURE_USE_RTREMOTE

static bool isPrimaryRtRemoteUser()
{
   bool isPrimary;

   // We need a way to ensure rtRemote event dispatching happens.  In cases
   // where multiple entities in a single process use rtRemote, only one can
   // perform the dispatching.  Here we use a hack of doing a one-time check
   // to see if anything else in our process is already doing dispatching
   // by looking for the unix domain socket file that rtRemote creates.
   if ( !gIsPrimaryStatusKnown )
   {
      char work[64];
      struct stat fileinfo;
      int rc;
      int pid= getpid();

      INFO("isPrimaryRtRemote: pid %d", pid);

      sprintf( work, "/tmp/rt_remote_soc.%d", pid );
      rc= stat( work, &fileinfo );
      if ( rc )
      {
         gIsPrimary= true;
      }
      else
      {
         gIsPrimary= false;
      }
      gIsPrimaryStatusKnown= true;
   }

   isPrimary= gIsPrimary;
   INFO("isPrimaryRtRemote: primary %d", isPrimary);
   
   return isPrimary;
}

class rtMediaCaptureObject : public rtObject
{
   public:
     rtMediaCaptureObject( MediaCapContext *ctx)
       : m_ctx( ctx )
     {
        DEBUG("rtMediaCaptureObject ctor: ctx %p", m_ctx);
     }
     virtual ~rtMediaCaptureObject()
     {
        DEBUG("rtMediaCaptureObject dtor: ctx %p", m_ctx );
     }

     rtDeclareObject(rtMediaCaptureObject, rtObject);     

     rtMethod3ArgAndNoReturn("captureMediaSample", captureMediaSample, rtString, int32_t, rtFunctionRef);

     rtMethod3ArgAndNoReturn("captureMediaStart", captureMediaStart, rtString, int32_t, rtFunctionRef);

     rtMethod1ArgAndNoReturn("captureMediaStop", captureMediaStop, rtFunctionRef);

     rtError captureMediaSample( rtString file, int32_t duration, rtFunctionRef f );

     rtError captureMediaStart( rtString endPoint, int32_t duration, rtFunctionRef f );

     rtError captureMediaStop( rtFunctionRef f);

     void reportCaptureStart();

     void reportCaptureProgress(long long bytes, long long totalBytes);

     void reportCaptureComplete();

   private:
      MediaCapContext *m_ctx;
      rtFunctionRef m_func;
};
rtDefineObject(rtMediaCaptureObject, rtObject);
rtDefineMethod(rtMediaCaptureObject, captureMediaSample);
rtDefineMethod(rtMediaCaptureObject, captureMediaStart);
rtDefineMethod(rtMediaCaptureObject, captureMediaStop);

rtError rtMediaCaptureObject::captureMediaSample( rtString file, int32_t duration, rtFunctionRef f )
{
   rtError rc;
   rtString result;
   bool validCtx= false;
   char work[256];

   INFO("captureMediaSample: m_ctx %p", m_ctx);

   pthread_mutex_lock( &gMutex );
   for ( std::vector<MediaCapContext*>::iterator it= gContextList.begin();
         it != gContextList.end();
         ++it )
   {
      MediaCapContext *ctxIter= (*it);
      if ( ctxIter == m_ctx )
      {
         validCtx= true;
         break;
      }
   }
   pthread_mutex_unlock( &gMutex );

   if ( validCtx )
   {
      if ( m_ctx->captureActive )
      {
         result= "";
         snprintf( work, sizeof(work), "failure: (%s) busy", m_ctx->rtName );
         result.append(work);
         rc= f.send(result);
         if( rc != RT_OK )
         {
            ERROR("captureMediaSample: send (busy) rc %d", rc);
         }
      }
      else
      {
         prepareForCapture( m_ctx );

         if ( m_ctx->canCapture )
         {
            m_func= f;
            startCapture( m_ctx, true, file.cString(), duration );
         }
         else
         {
            result= "";
            snprintf( work, sizeof(work), "failure: (%s) cannot capture current media", m_ctx->rtName );
            result.append(work);
            if( rc != RT_OK )
            {
               ERROR("captureMediaSample: send (cannot capture) rc %d", rc);
            }
         }
      }
   }
   else
   {
      result= "failure: source no longer available";
      rc= f.send(result);
      if( rc != RT_OK )
      {
         ERROR("captureMediaSample: send (no longer available) rc %d", rc);
      }
   }
   
   return RT_OK;
}

rtError rtMediaCaptureObject::captureMediaStart( rtString endPoint, int32_t duration, rtFunctionRef f )
{
   rtError rc;
   rtString result;
   bool validCtx= false;
   char work[256];

   INFO("captureMediaStart: m_ctx %p", m_ctx);

   pthread_mutex_lock( &gMutex );
   for ( std::vector<MediaCapContext*>::iterator it= gContextList.begin();
         it != gContextList.end();
         ++it )
   {
      MediaCapContext *ctxIter= (*it);
      if ( ctxIter == m_ctx )
      {
         validCtx= true;
         break;
      }
   }
   pthread_mutex_unlock( &gMutex );

   if ( validCtx )
   {
      if ( m_ctx->captureActive )
      {
         result= "";
         snprintf( work, sizeof(work), "failure: (%s) busy", m_ctx->rtName );
         result.append(work);
         rc= f.send(result);
         if( rc != RT_OK )
         {
            ERROR("captureMediaStart: send rc (busy) %d", rc);
         }
      }
      else
      {
         prepareForCapture( m_ctx );

         if ( m_ctx->canCapture )
         {
            m_ctx->totalBytesEmitted= 0;

            startCapture( m_ctx, false, endPoint.cString(), duration );
            if ( m_ctx->captureActive )
            {
               m_func= f;
               reportCaptureStart();
            }
            else
            {
               result= "";
               snprintf( work, sizeof(work), "failure: (%s) failed to start", m_ctx->rtName );
               result.append(work);
               rc= f.send(result);
               if( rc != RT_OK )
               {
                  ERROR("captureMediaStart: send (failed to start) rc %d", rc);
               }
            }
         }
         else
         {
            result= "";
            snprintf( work, sizeof(work), "failure: (%s) cannot capture current media", m_ctx->rtName );
            result.append(work);
            rc= f.send(result);
            if( rc != RT_OK )
            {
               ERROR("captureMediaStart: send (cannot capture) rc %d", rc);
            }
         }
      }
   }
   else
   {
      result= "failure: source no longer available";
      rc= f.send(result);
      if( rc != RT_OK )
      {
         ERROR("captureMediaStart: send (no longer available) rc %d", rc);
      }
   }
   return RT_OK;
}

rtError rtMediaCaptureObject::captureMediaStop( rtFunctionRef f )
{
   rtError rc;
   rtString result;
   bool validCtx= false;
   char work[256];

   INFO("captureMediaStop: m_ctx %p", m_ctx);

   pthread_mutex_lock( &gMutex );
   for ( std::vector<MediaCapContext*>::iterator it= gContextList.begin();
         it != gContextList.end();
         ++it )
   {
      MediaCapContext *ctxIter= (*it);
      if ( ctxIter == m_ctx )
      {
         validCtx= true;
         break;
      }
   }
   pthread_mutex_unlock( &gMutex );

   if ( validCtx )
   {
      if ( m_ctx->captureActive )
      {
         if ( m_ctx->postThreadStarted )
         {
            if ( m_ctx->captureActive )
            {
               m_ctx->goodCapture= true;
               stopCapture( m_ctx );
            }
         }
      }
      else
      {
         result= "";
         snprintf( work, sizeof(work), "failure: (%s) nothing to stop", m_ctx->rtName );
         result.append(work);
         rc= f.send(result);
         if( rc != RT_OK )
         {
            ERROR("captureMediaStop: send rc (nothing to stop) %d", rc);
         }
      }
   }
   else
   {
      result= "failure: source no longer available";
      rc= f.send(result);
      if( rc != RT_OK )
      {
         ERROR("captureMediaStop: send (no longer available) rc %d", rc);
      }
   }

   return RT_OK;
}

void rtMediaCaptureObject::reportCaptureStart()
{
   rtError rc;
   if ( m_func.ptr() )
   {
      if ( !m_ctx->captureToFile )
      {
         char work[64];
         rtString result= "";
         snprintf( work, sizeof(work), "started: (%s)", m_ctx->rtName );
         result.append(work);
         rc= m_func.send(result);
         if ( rc != RT_OK )
         {
            ERROR("rtMediaCaptureObject::reportCaptureStart: send rc %d", rc);
         }
      }
   }
}

void rtMediaCaptureObject::reportCaptureProgress( long long bytes, long long totalBytes )
{
   rtError rc;
   if ( m_func.ptr() )
   {
      if ( !m_ctx->captureToFile )
      {
         char work[256];
         rtString result= "";

         snprintf( work, sizeof(work), "progress: (%s) bytes %lld total bytes %lld", m_ctx->rtName, bytes, totalBytes);
         result.append(work);

         rc= m_func.send(result);
         if ( rc != RT_OK )
         {
            ERROR("rtMediaCaptureObject::reportCaptureProgress: send rc %d", rc);
         }
      }
   }
}

void rtMediaCaptureObject::reportCaptureComplete()
{
   rtError rc;
   INFO("reportCaptureComplete: goodCapture %d", m_ctx->goodCapture);
   if ( m_func.ptr() )
   {
      if ( m_ctx->captureToFile )
      {
         rtString result= "complete";
         rc= m_func.send(result);
      }
      else
      {
         char work[256];
         rtString result= "";

         snprintf( work, sizeof(work), "complete: (%s) bytes %lld duration %lld ms", m_ctx->rtName, m_ctx->totalBytesPosted, m_ctx->durationCaptured);
         result.append(work);
         rc= m_func.send(result);
      }
      INFO("rtMediaCaptureObject::reportCaptureComplete: send rc %d", rc);
   }
}

static bool registerRtRemote( MediaCapContext *ctx )
{
   bool result= false;
   rtError rc;

   ctx->rtEnv= rtEnvironmentGetGlobal();

   rc= rtRemoteInit(ctx->rtEnv);
   INFO("registerRtRemote: rtRemoteInit: rc %d", rc);
   if ( rc == RT_OK )
   {
      ctx->apiObj= new rtMediaCaptureObject( ctx );
      if ( ctx->apiObj )
      {
         int id;
         
         pthread_mutex_lock( &gMutex );
         id= gNextId++;
         pthread_mutex_unlock( &gMutex );
         
         snprintf( ctx->rtName, sizeof(ctx->rtName), RTREMOTE_NAME, getpid(), id );
         rc= rtRemoteRegisterObject( ctx->rtEnv, ctx->rtName, ctx->apiObj );
         INFO("registerRtRemote: rtRemoteRegisterObject: (%s) rc %d", ctx->rtName, rc);
         if ( rc == RT_OK )
         {
            if ( !gServerObj.ptr() )
            {
               rc= rtRemoteLocateObject(ctx->rtEnv, RTREMOTE_SERVERNAME, gServerObj);
               if ( rc == RT_OK )
               {
                  INFO("registerRtRemote: serverObj ptr %p\n", gServerObj.ptr());
               }
            }
            if ( gServerObj.ptr() )
            {
               rtString pipeLineName= ctx->rtName;
               rc= gServerObj.send( "registerPipeline", pipeLineName );
               INFO("registerRtRemote: register pipeline (%s) rc %d", ctx->rtName, rc); 
            }
            else
            {
               ERROR("registerRtRemote: failed to register pipeline (%s) rc %d", ctx->rtName, rc);
            }
            
            result= true;
         }
      }
      else
      {
         ERROR("registerRtRemote: unable to create apiObj instance");
      }
   }
   
   return result;
}

static void unregisterRtRemote( MediaCapContext *ctx )
{
   DEBUG("unregisterRtRemote: enter");
   if ( ctx )
   {
      rtError rc;

      if ( !isPrimaryRtRemoteUser() )
      {
         if ( ctx->apiObj )
         {
            DEBUG("unregisterRtRemote: calling rtRemoteUnregisterObject for (%s)", ctx->rtName);
            rc= rtRemoteUnregisterObject( ctx->rtEnv, ctx->rtName );
            DEBUG("unregisterRtRemote: rtRemoteUnregisterObject rc %d",rc);
            DEBUG("unregisterRtRemote: releasing apiObj");
            ctx->apiObj= NULL;
            DEBUG("unregisterRtRemote: apiObj gone");
         }

         if ( ctx->rtEnv )
         {
            DEBUG("unregisterRtRemote: calling rtShutdown");
            rtRemoteShutdown(ctx->rtEnv);
            ctx->rtEnv= 0;
         }
      }

      if ( gServerObj.ptr() )
      {
         rtString pipeLineName= ctx->rtName;
         rc= gServerObj.send( "unregisterPipeline", pipeLineName );
         INFO("registerRtRemote: unregister pipeline (%s) rc %d", ctx->rtName, rc); 
      }
   }
   DEBUG("unregisterRtRemote: exit");
}

static void* apiThread( void *arg )
{
   MediaCapContext *ctx= (MediaCapContext*)arg;
   rtError rc;

   INFO("apiThread: enter");
   ctx->apiThreadStarted= true;

   if ( registerRtRemote(ctx) )
   {
      for( ; ; )
      {
         rtRemoteProcessSingleItem( ctx->rtEnv );
         if ( ctx->apiThreadStopRequested )
         {
            break;
         }
         usleep( 1000 );
      }
   }

   ctx->apiThreadStarted= false;

   INFO("apiThread: ending");

   unregisterRtRemote(ctx);

   return NULL;
}

static void createRtRemoteAPI( MediaCapContext *ctx )
{
   if ( ctx )
   {
      int rc;

      if ( isPrimaryRtRemoteUser() )
      {
         INFO("createRtRemoteAPI: before pthread_create: apiThreadId %d", ctx->apiThreadId);
         rc= pthread_create( &ctx->apiThreadId, NULL, apiThread, ctx );
         INFO("createRtRemoteAPI: after pthread_create: apiThreadId %d", ctx->apiThreadId);
         if ( rc )
         {
            ERROR("createRtRemopteAPI failed to start apiThread");
         }
      }
      else
      {
         registerRtRemote( ctx );
      }
   }
}

static void destroyRtRemoteAPI( MediaCapContext *ctx )
{
   DEBUG("destroyRtRemoteAPI: enter");
   if ( ctx )
   {
      if ( ctx->apiThreadStarted )
      {
         ctx->apiThreadStopRequested= true;
         pthread_join( ctx->apiThreadId, NULL );
      }
      else
      {
         unregisterRtRemote(ctx);
      }
   }
   DEBUG("destroyRtRemoteAPI: exit");
}
#endif

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static unsigned long gCRCTable[256];

static void initCRCTable()
{
	unsigned int k, i, j;
	for(i = 0; i < 256; i++)
	{
		k = 0;
		for(j = (i << 24) | 0x800000; j != 0x80000000; j <<= 1)
		{
			k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
		}
		gCRCTable[i] = k;
	}
}

static unsigned long getCRC(unsigned char *data, int size, int initial )
{
	int i;
	unsigned long int crc= initial;
	for(i = 0; i < size; i++)
	{
		crc= (crc << 8) ^ gCRCTable[0xFF&((crc >> 24) ^ data[i])];
	}
	return crc;
}

static void dumpPacket( unsigned char *packet, int packetSize )
{
   if ( LEVEL_TRACE2 <= gDebugLevel )
   {
      int i;
      char buff[1024];   
      
      int col= 0;
      int buffPos= 0;
      buffPos += sprintf(&buff[buffPos], "\n" );
      for( i= 0; i < packetSize; ++i )
      {
          buffPos += snprintf(&buff[buffPos], (sizeof (buff) - buffPos), "%02X ", packet[i] );
          ++col;
          if ( col == 8 )
          {
             strcat( buff, " " );
             buffPos += 1;
          }
          if ( col == 16 )
          {
             buffPos += snprintf(&buff[buffPos], (sizeof (buff) - buffPos), "\n" );
             col= 0;
          }
      }
      if ( col > 0 )
      {
         buffPos += snprintf(&buff[buffPos], (sizeof (buff) - buffPos), "\n" );
         col= 0;
      }
      TRACE2("%s", buff );
   }
}

static void generatePAT( MediaCapContext *ctx )
{
   int progNum= 1;
   int pmtVersion= 1;
   int pmtSectionLen= 0;
   int pmtPid, pcrPid= -1;
   int i, j, temp, crc;
   unsigned char *packet= 0;

   assert( ctx->packetSize == DEFAULT_PACKET_SIZE);
   
   pmtPid= ctx->pmtPid;
   if ( ctx->videoPid )
   {
      pcrPid= ctx->videoPid;
      ctx->ptsPcrOffset= VIDEO_PTS_PCR_OFFSET;
   }
   else
   {
      pcrPid= ctx->srcList[0]->pid;
      ctx->ptsPcrOffset= AUDIO_PTS_PCR_OFFSET;
   }
   ctx->pcrPid= pcrPid;
   
   packet= (unsigned char *)malloc( DEFAULT_PACKET_SIZE );
   if ( packet )
   {
      memset( packet, 0xFF, DEFAULT_PACKET_SIZE );

      i= 0;
      // Sync byte
      packet[i+0]= 0x47;
      // TEI=no ; Payload start=yes; priority=0; 5 bits pid=0
      packet[i+1]= 0x60;
      // 8 bits LSB pid = 0
      packet[i+2]= 0x00;
      // 2 bits scrambling ctrl= no; 2 bits adaptation field = no adaptation; 4 bits continuity counter
      packet[i+3]= 0x10;
      // Payload start=yes, so pointer field giving offset to first section 
      packet[i+4]= 0x00;
      // Start of section, table id= 0 (PAT)
      packet[i+5]= 0x00;
      // 4 bits fixed = 1011; 4 bits MSB section length
      packet[i+6]= 0xB0;
      // 8 bits LSB section length (remaining bytes following this field including CRC)
      packet[i+7]= 0x0D;
      // TSID - NA
      packet[i+8]= 0x00;
      // TSID - NA
      packet[i+9]= 0x01;
      // Masking 2 ms bits and ls bit : 0011 1110 (3E)
      temp= pmtVersion << 1;
      temp= temp & 0x3E;
      // C1: 1100 0001 : reserved bits, current_next_indicator as 1
      packet[i+10]= 0xC1 | temp;
      // Section number
      packet[i+11]= 0x00;
      // Last section number
      packet[i+12]= 0x00;
      // 16 bit program number
      packet[i+13]= (progNum >> 8) & 0xFF;
      packet[i+14]= progNum & 0xFF;
      // PMT Pid with 3 ms bits reserved
      packet[i+15]= 0xE0;
      packet[i+15] |= (unsigned char)((pmtPid >> 8) & 0x1F);
      packet[i+16]= (unsigned char)(0xFF & pmtPid);
    
       // CRC
      crc= getCRC(&packet[i+5], 12);
      packet[i+17]= (crc >> 24) & 0xFF;
      packet[i+18]= (crc >> 16) & 0xFF;
      packet[i+19]= (crc >> 8) & 0xFF;
      packet[i+20]= crc & 0xFF;

      ctx->patPacket= packet;
      dumpPacket(packet, DEFAULT_PACKET_SIZE);
   }
   else
   {
      ERROR( "generatePAT: unable to allocate memory for PAT packet");
   }
}

static void generatePMT( MediaCapContext *ctx )
{
   int i, temp, crc;
   int pmtPid, pcrPid;
   int pmtSize, sectionLen;
   int progNum= 1;
   int pmtVersion= 1;
   unsigned char *packet= 0;

   assert( ctx->packetSize == DEFAULT_PACKET_SIZE);
   packet= (unsigned char *)malloc( DEFAULT_PACKET_SIZE );
   if ( packet )
   {
      memset( packet, 0xFF, DEFAULT_PACKET_SIZE );

      pmtPid= ctx->pmtPid;
      pcrPid= ctx->pcrPid;

      packet[0]= 0x47;
      packet[1]= 0x60;
      packet[1] |= (unsigned char) ((pmtPid >> 8) & 0x1F);
      packet[2]= (unsigned char) (0xFF & pmtPid);
      packet[3]= 0x10;

      pmtSize= 17 + ctx->srcList.size()*5 + 4;

      sectionLen= pmtSize - 8;      
      packet[4]= 0x00;
      packet[5]= 0x02;
      packet[6]= (0xB0 | ((sectionLen>>8)&0xF));
      packet[7]= (sectionLen & 0xFF);

      packet[8]= (progNum >> 8) & 0xFF;
      packet[9]= progNum & 0xFF;

      temp= pmtVersion << 1;
      temp= temp & 0x3E;
      packet[10]= 0xC1 | temp;

      packet[11]= 0x00;
      packet[12]= 0x00;
      packet[13]= 0xE0;
      packet[13] |= (unsigned char) ((pcrPid >> 8) & 0x1F);
      packet[14]= (unsigned char) (0xFF & pcrPid);
      packet[15] = 0xF0;
      packet[16] = 0x00;

      i= 17;
      for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
            it != ctx->srcList.end();
            ++it )
      {
         SrcInfo *si= (*it);
         packet[i]= si->streamType;
         packet[i+1]= (0xE0 | ((si->pid >> 8) & 0x1F));
         packet[i+2]= (si->pid & 0xFF);
         packet[i+3]= 0xF0;
         packet[i+4]= 0x00;
         i += 5;
      }

      crc= getCRC( &packet[5], pmtSize-5-4 );
      packet[i]= ((crc>>24)&0xFF);
      packet[i+1]= ((crc>>16)&0xFF);
      packet[i+2]= ((crc>>8)&0xFF);
      packet[i+3]= (crc&0xFF);

      ctx->pmtPacket= packet;
      dumpPacket(packet, DEFAULT_PACKET_SIZE);
   }
   else
   {
      ERROR("generatePMT: unable to allocate memory for PMT packet");
   }
}

static unsigned char* getBinaryCodecData( gchar *codecData, int& dataLen )
{
   unsigned char *data= 0;
   int len, i, c, b;

   len= strlen(codecData);
   data= (unsigned char*)malloc( len/2 );
   if ( data )
   {
      dataLen= len/2;
      for( i= 0; i < len; ++i )
      {            
         c= codecData[i];
         if ( (c >= '0') && (c <= '9') )
         {
            c= c-'0';
         }
         else if ( (c >= 'a') && (c <= 'f') )
         {
            c= (c-'a')+10;
         }
         else if ( (c >= 'A') && (c <= 'F') )
         {
            c= (c-'A')+10;
         }
         else
         {
            ERROR("getBinaryCodecData: bad character (%02X) in codec_data", c);
            c= 0;
         }
         if ( i % 2 == 0 )
         {
            b= c;
         }
         else
         {
            b= (b << 4)+c;
            data[i/2]= b;
         }
      }
   }
   
   return data;
}

static void generateSPSandPPS( MediaCapContext *ctx, SrcInfo *si )
{
   unsigned char *data= 0;
   int len, i;
   int spsLen, ppsLen, neededLen;
   unsigned char *spspps= 0;
   
   if ( ctx && si && si->codecData )
   {
      data= getBinaryCodecData( si->codecData, len );
      if ( data )
      {
         if ( len > 10 )
         {
            i= 6;
            spsLen= (data[i] << 8)|data[i+1];
            i += (spsLen + 2 +1);
            ppsLen= (data[i] << 8)|data[i+1];
            neededLen= 3 + spsLen + 3 + ppsLen;
            spspps= (unsigned char*)calloc( 1, neededLen );
            if ( spspps )
            {
               spspps[2]= 1;
               memcpy( spspps+3, data+8, spsLen );
               spspps[3+spsLen+2]= 1;
               memcpy( spspps+3+spsLen+3, data+8+spsLen+3, ppsLen );
               
               dumpPacket( spspps, neededLen );
               si->spspps= spspps;
               si->spsppsLen= neededLen;
            }
            else
            {
               ERROR("generateSPSandPPS: unble to allocate memory for sps/pps buffer");
               ctx->canCapture= false;
            }
         }
         
         free( data );
      }
      else
      {
         ERROR("mediacapture: generateSPSandPPS: unble to allocate memory for binary codec_data");
         ctx->canCapture= false;
      }
   }
}

static void updateAudioAACPESHeader( MediaCapContext *ctx, SrcInfo *si, int bufferSize )
{
   if ( si->audioPESHdr )
   {
      unsigned char *pesHdr= si->audioPESHdr;
      pesHdr[3]= ((pesHdr[3] & 0xFC) | (((7 + bufferSize) & 0x1800) >> 11));
      pesHdr[4]= (((7 + bufferSize) & 0x1FF8) >> 3);
      pesHdr[5]= ((pesHdr[5] & 0x1F) | (((7 + bufferSize) & 0x07) << 5));
   }
}

#define AOT_SBR (5)
#define AOT_PS (29)
static void generateAudioAACPESHeader( MediaCapContext *ctx, SrcInfo *si )
{
   unsigned char *data= 0;
   int len, i;
   unsigned char *pesHdr= 0;
   int aot, rateIdx, chan;
   int bufferSize= 0;

   if ( ctx && si && si->codecData )
   {
      data= getBinaryCodecData( si->codecData, len );
      if ( data )
      {
         aot= (data[0] >> 3);
         if ( (aot == AOT_SBR) || (aot == AOT_PS) )
         {
            if ( len > 2 )
            {
               aot= ((data[2] & 0x7C) >> 2);
            }
         }
         rateIdx= ((data[0] & 0x03) <<1);
         rateIdx |= ((data[1] & 0x80) >>7);
         chan= ((data[1]&0x78)>>3);

         if ( (aot < 1) || (aot > 4) )
         {
            ERROR("generateAudioAACPESHeader: audio codec m4a.40.%d not supported", aot);
            ctx->canCapture= false;
         }
         else
         {
            pesHdr= (unsigned char*)malloc( 7 );
            if ( pesHdr )
            {
               pesHdr[0]= 0xFF;
               pesHdr[1]= 0xF1;
               pesHdr[2]= ((aot-1) << 6);
               pesHdr[2] |= (rateIdx << 2);
               pesHdr[2] |= ((chan & 0x04) >> 2);
               pesHdr[3]= ((chan & 0x03) << 6);
               pesHdr[3] |= (((7 + bufferSize) & 0x1800) >> 11);
               pesHdr[4]= (((7 + bufferSize) & 0x1FF8) >> 3);
               pesHdr[5]= (((7 + bufferSize) & 0x07) << 5);
               pesHdr[5] |= 0x1F;
               pesHdr[6]= 0xFC;
               
               si->audioPESHdrLen= 7;
               si->audioPESHdr= pesHdr;
            }
            else
            {
               ERROR("generateAudioAACPESHeader: unble to allocate memory for pes header");
               ctx->canCapture= false;
            }
         }
         
         free( data );
      }
      else
      {
         ERROR("generateAudioAACPESHeader: unble to allocate memory for binary codec_data");
         ctx->canCapture= false;
      }
   }
}

static void releaseSourceResources( SrcInfo *si )
{
   DEBUG("releaseSourceResources: enter: si %p", si);
   if ( si )
   {
      if ( si->pad )
      {
         DEBUG("releaseSourceResources: unref pad: si %p pad %p", si, si->pad);
         gst_object_unref(si->pad);
         DEBUG("releaseSourceResources: done unref pad: si %p pad %p", si, si->pad);
         si->pad= 0;
      }
      if ( si->element )
      {
         DEBUG("releaseSourceResources: unref element: si %p element %p", si, si->element);
         gst_object_unref(si->element);
         DEBUG("releaseSourceResources: done unref element: si %p element %p", si, si->element);
         si->element= 0;
      }
      if ( si->mimeType )
      {
         g_free(si->mimeType);
         si->mimeType= 0;
      }
      if ( si->codecData )
      {
         g_free(si->codecData);
         si->codecData= 0;
      }
      if ( si->spspps )
      {
         free( si->spspps );
         si->spspps= 0;
      }
      if ( si->audioPESHdr )
      {
         free( si->audioPESHdr );
         si->audioPESHdr= 0;
      }
      if ( si->accumulator )
      {
         free( si->accumulator );
         si->accumulator= 0;
      }
   }
   DEBUG("releaseSourceResources: exit: si %p", si);
}

static void freeSource(MediaCapContext *ctx, SrcInfo *si)
{
   for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
         it != ctx->srcList.end();
         ++it )
   {
      SrcInfo *siIter= (*it);
      if ( siIter == si )
      {
         releaseSourceResources(si);
         ctx->srcList.erase(it);
         free( si );         
         break;
      }
   }
}

static void freeSources(MediaCapContext *ctx)
{
   DEBUG("freeSources: enter");
   while( ctx->srcList.size() > 0 )
   {
      SrcInfo *si= ctx->srcList[ctx->srcList.size()-1];
      releaseSourceResources(si);
      ctx->srcList.pop_back();
      free( si );
   }
   DEBUG("freeSources: exit");
}

static void findSources(MediaCapContext *ctx)
{
   GstElement *pipeline= 0;
   GstElement *element, *elementPrev= 0;
   GstElement *elementDemux= 0;
   GstIterator *iterator;

   INFO("findSources: enter: ctx %p", ctx);

   freeSources(ctx);

   element= ctx->element;
   do
   {
      if ( elementPrev )
      {
         gst_object_unref( elementPrev );
      }
      element= GST_ELEMENT_CAST(gst_element_get_parent( element ));
      if ( element )
      {
         elementPrev= pipeline;
         pipeline= element;
      }
   }
   while( element != 0 );
   
   DEBUG("pipeline %p", pipeline );
   if ( pipeline )
   {
      GstIterator *iterElement= gst_bin_iterate_recurse( GST_BIN(pipeline) );
      if ( iterElement )
      {
         GValue itemElement= G_VALUE_INIT;
         while( gst_iterator_next( iterElement, &itemElement ) == GST_ITERATOR_OK )
         {
            element= (GstElement*)g_value_get_object( &itemElement );
            if ( element && !GST_IS_BIN(element) )
            {
               int numSinkPads= 0;

               GstIterator *iterPad= gst_element_iterate_sink_pads( element );
               if ( iterPad )
               {
                  GValue itemPad= G_VALUE_INIT;
                  while( gst_iterator_next( iterPad, &itemPad ) == GST_ITERATOR_OK )
                  {
                     GstPad *pad= (GstPad*)g_value_get_object( &itemPad );
                     if ( pad )
                     {
                        ++numSinkPads;
                     }
                     g_value_reset( &itemPad );
                  }
                  if ( numSinkPads )
                  {
                     GstElementClass *ec= GST_ELEMENT_GET_CLASS(element);
                     if ( ec )
                     {
                        const gchar *meta= gst_element_class_get_metadata( ec, GST_ELEMENT_METADATA_KLASS);
                        DEBUG("element name (%s) Klass (%s)", gst_element_get_name(element), meta);
                        if ( meta && strstr(meta, "Demuxer") )
                        {
                           elementDemux= element;
                           GstIterator *iterPad= gst_element_iterate_sink_pads( element );
                           if ( iterPad )
                           {
                              GValue itemPad= G_VALUE_INIT;
                              while( gst_iterator_next( iterPad, &itemPad ) == GST_ITERATOR_OK )
                              {
                                 GstPad *pad= (GstPad*)g_value_get_object( &itemPad );
                                 if ( pad )
                                 {
                                    SrcInfo *si= (SrcInfo*)calloc( 1, sizeof(SrcInfo));
                                    if ( si )
                                    {             
                                       gst_object_ref(element);
                                       gst_object_ref(pad);
                                       si->ctx= ctx;
                                       si->element= element;
                                       si->pad= pad;
                                       si->isDemux= TRUE;
                                       si->mimeType= 0;
                                       si->packetSize= -1;
                                       si->probeId= 0;
                                       si->firstPTS= -1LL;
                                       ctx->srcList.push_back(si);
                                    }
                                    g_value_reset( &itemPad );
                                    break;
                                 }
                                 g_value_reset( &itemPad );
                              }
                              gst_iterator_free(iterPad);
                           }
                        }
                     }
                  }
                  gst_iterator_free(iterPad);
               }

               if ( numSinkPads == 0 )
               {
                  DEBUG("source element name (%s)", gst_element_get_name(element));

                  GstIterator *iterPad= gst_element_iterate_src_pads( element );
                  if ( iterPad )
                  {
                     GValue itemPad= G_VALUE_INIT;
                     while( gst_iterator_next( iterPad, &itemPad ) == GST_ITERATOR_OK )
                     {
                        GstPad *pad= (GstPad*)g_value_get_object( &itemPad );
                        if ( pad )
                        {
                           SrcInfo *si;

                           gchar *name= gst_pad_get_name(pad);
                           if ( name )
                           {
                              DEBUG("    pad name (%s)", name);
                              g_free(name);
                           }
       
                           si= (SrcInfo*)calloc( 1, sizeof(SrcInfo));
                           if ( si )
                           {
                              gst_object_ref(element);
                              gst_object_ref(pad);
                              si->ctx= ctx;
                              si->element= element;
                              si->pad= pad;
                              si->isDemux= FALSE;
                              si->mimeType= 0;
                              si->packetSize= -1;
                              si->probeId= 0;
                              si->firstPTS= -1LL;
                              ctx->srcList.push_back(si);
                           }
                        }
                        g_value_reset( &itemPad );
                     }
                     gst_iterator_free(iterPad);
                  }
               }
            }
            g_value_reset( &itemElement );
         }
         gst_iterator_free(iterElement);
      }
      
      gst_object_unref(pipeline);
   }

   gboolean haveDemux= FALSE;
   for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
         it != ctx->srcList.end();
         ++it )
   {
      SrcInfo *si= (*it);
      if ( si->isDemux )
      {
         haveDemux= TRUE;
         break;
      }
   }
   if ( haveDemux )
   {
      bool needAnotherPass= true;
      while( needAnotherPass )
      {
         needAnotherPass= false;
         for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
               it != ctx->srcList.end();
               ++it )
         {
            SrcInfo *si= (*it);
            if ( !si->isDemux )
            {
               needAnotherPass= true;
               freeSource(ctx, si);
               break;
            }
         }
      }
   }

   for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
         it != ctx->srcList.end();
         ++it )
   {
      SrcInfo *si= (*it);
      GstPad *pad;      

      element= si->element;
      pad= si->pad;
      
      GstCaps *caps= gst_pad_get_current_caps(pad);
      if ( !caps && GST_IS_APP_SRC(element) )
      {
         caps= gst_app_src_get_caps( GST_APP_SRC(element) );
         DEBUG("    app src caps %p", caps);
         if ( !caps )
         {
            GstElement *parent= GST_ELEMENT_CAST(gst_element_get_parent( element ));
            if ( parent )
            {
               GstIterator *iterParentPad= gst_element_iterate_src_pads( parent );
               if ( iterParentPad )
               {
                  GValue itemParentPad= G_VALUE_INIT;
                  while( gst_iterator_next( iterParentPad, &itemParentPad ) == GST_ITERATOR_OK )
                  {
                     GstPad *padParent= (GstPad*)g_value_get_object( &itemParentPad );
                     if ( padParent )
                     {
                        gchar *namePadParent= gst_pad_get_name(padParent);
                        if ( namePadParent )
                        {
                           DEBUG("    parent pad name (%s)", namePadParent);
                           g_free(namePadParent);
                        }
                        caps= gst_pad_get_current_caps(padParent);
                        DEBUG("    parent pad caps %p", caps);
                        g_value_reset( &itemParentPad );
                        break;
                     }
                     g_value_reset( &itemParentPad );
                  }
                  gst_iterator_free(iterParentPad);
               }
            }
         }
      }

      if ( caps )
      {
         gchar *str= gst_caps_to_string(caps);
         if ( str )
         {
            DEBUG("    caps (%s)", str);
            g_free(str);
         }
         GstStructure *structure= gst_caps_get_structure(caps, 0);
         if(structure )
         {
            gint packetSize= -1;
            gint mpegversion= -1;
            const GValue *cdata;
            const gchar *mime= gst_structure_get_name(structure);
            DEBUG("    pad mimeType (%s)", mime);
            if ( mime )
            {
               si->mimeType= g_strdup(mime);
            }
            if ( !gst_structure_get_int(structure, "packetsize", &packetSize) )
            {
               packetSize= -1;
            }
            si->packetSize= packetSize;
            if ( !gst_structure_get_int(structure, "mpegversion", &mpegversion) )
            {
               mpegversion= 1;
            }
            si->mpegversion= mpegversion;
            cdata= gst_structure_get_value(structure, "codec_data");
            if ( cdata )
            {
               gchar *codecData= gst_value_serialize(cdata);
               if ( codecData )
               {
                  si->codecData= g_strdup(codecData);
                  if ( si->codecData )
                  {
                     DEBUG("    codecData: (%s)", si->codecData);
                  }
                  g_free(codecData);
               }
            }
         }
      }
   }

   INFO("findSources: exit: ctx %p have %d sources", ctx, ctx->srcList.size());
}

static void prepareForCapture( MediaCapContext *ctx )
{
   DEBUG("prepareForCapture: enter");   
   if ( ctx )
   {
      if ( !ctx->prepared )
      {
         if ( ctx->srcList.size() > 0 )
         {
            ctx->canCapture= true;

            if ( ctx->srcList.size() > 1 )
            {
               ctx->packetSize= DEFAULT_PACKET_SIZE;
               ctx->needTSEncapsulation= true;
            }
            else
            {
                SrcInfo *si= ctx->srcList[0];
                
                int len= si->mimeType ? strlen(si->mimeType) : 0;
                if ( (len == 23) && !strncmp( "video/vnd.dlna.mpeg-tts", si->mimeType, len) )
                {
                   ctx->packetSize= 192;
                }
                else 
                if ( (len == 12) && !strncmp( "video/mpegts", si->mimeType, len) )
                {
                   ctx->packetSize= ((si->packetSize != -1) ? si->packetSize : DEFAULT_PACKET_SIZE);
                }
                else
                {
                   ctx->packetSize= DEFAULT_PACKET_SIZE;
                   ctx->needTSEncapsulation= true;
                }
            }

            if ( ctx->needTSEncapsulation )
            {
               for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
                     it != ctx->srcList.end();
                     ++it )
               {
                  SrcInfo *si= (*it);

                  int len= si->mimeType ? strlen(si->mimeType) : 0;
                  if ( (len == 12) && !strncmp( "video/x-h264", si->mimeType, len) )
                  {
                     si->isVideo= true;
                     si->streamType= 0x1B;
                     si->streamId= ctx->nextVideoStreamId++;                  
                     si->pid= ctx->nextESPid++;
                     ctx->videoPid= si->pid;
                     if ( si->codecData )
                     {
                        si->isByteStream= false;
                        si->needEmitSPSPPS= true;
                        generateSPSandPPS( ctx, si );
                     }
                     else
                     {
                        si->isByteStream= true;
                     }
                     DEBUG("prepareForCapture: video isByteStream: %d", si->isByteStream);
                  }
                  else
                  if ( (len == 12) && !strncmp( "audio/x-eac3", si->mimeType, len) )
                  {
                     si->streamType= 0x87;
                     si->streamId= ctx->nextAudioStreamId++;
                     si->pid= ctx->nextESPid++;
                  }
                  else
                  if ( (len == 10) && !strncmp( "audio/mpeg", si->mimeType, len) )
                  {
                     switch( si->mpegversion )
                     {
                        case 1:
                           si->streamType= 0x03;
                           break;
                        default:
                        case 2:
                           si->streamType= 0x04;
                           break;
                        case 4:
                           si->streamType= 0x0F;
                           if ( si->codecData )
                           {
                              generateAudioAACPESHeader( ctx, si );
                           }
                           break;
                     }
                     si->streamId= ctx->nextAudioStreamId++;                  
                     si->pid= ctx->nextESPid++;
                  }
                  else
                  {
                     ERROR("prepareForCapture: unknown media format: mimeType(%s)", si->mimeType);
                     ctx->needTSEncapsulation= false;
                     ctx->canCapture= false;
                  }
               }
               
               if ( ctx->needTSEncapsulation )
               {
                  for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
                        it != ctx->srcList.end();
                        ++it )
                  {
                     SrcInfo *si= (*it);
                     int accumSize= ( si->isVideo ? VIDEO_ACCUM_SIZE : AUDIO_ACCUM_SIZE );
                     si->accumulator= (unsigned char*)malloc( accumSize );
                     if ( si->accumulator )
                     {
                        si->accumSize= accumSize;
                     }
                     else
                     {
                        ERROR("prepareForCapture: unable to allocate memory for stream (%s) accumulator", si->mimeType);
                        ctx->canCapture= false;
                     }
                  }
                  if ( ctx->canCapture )
                  {
                     DEBUG("prepareForCapture: calling generatePAT");
                     generatePAT( ctx );               
                     DEBUG("prepareForCapture: done calling generatePAT");
                     DEBUG("prepareForCapture: calling generatePMT");
                     generatePMT( ctx );
                     DEBUG("prepareForCapture: done calling generatePMT");
                     ctx->needEmitPATPMT= true;
                     ctx->needEmitPCR= true;
                  }
               }
            }
         }
         ctx->prepared= true;
      }
   }
   INFO("prepareForCapture: exit: ctx %p have %d sources canCapture %d needTSEncapsulation %d", 
         ctx, ctx->srcList.size(), ctx->canCapture, ctx->needTSEncapsulation);

   DEBUG("prepareForCapture: exit");
}

static void captureProbeDestroy( gpointer userData )
{
   SrcInfo *si= (SrcInfo*)userData;
   if ( si )
   {
      MediaCapContext *ctx= si->ctx;
      if ( ctx )
      {
         DEBUG("captureProbeDestroy: probeId %u", si->probeId);
         si->probeId= 0;
         DEBUG("captureProbeDestroy: probeId %u done", si->probeId);
      }
   }
}

static GstPadProbeReturn captureProbe( GstPad *pad, GstPadProbeInfo *info, gpointer userData )
{
   SrcInfo *si= (SrcInfo*)userData;
   if ( si )
   {
      if ( !si->ctx->captureActive )
      {
         return GST_PAD_PROBE_REMOVE;
      }
      
      if ( info->type & GST_PAD_PROBE_TYPE_BUFFER )
      {
         GstBuffer *buffer= (GstBuffer*)info->data;
         #ifdef USE_GST1
         GstMapInfo map;
         #endif
         int inSize= 0;
         unsigned char *inData= 0;
         long long pts= -1LL, dts= -1LL;

         #ifdef USE_GST1
         gst_buffer_map(buffer, &map, (GstMapFlags)GST_MAP_READ);
         inSize= map.size;
         inData= map.data;
         #else
         inSize= (int)GST_BUFFER_SIZE(buffer);
         inData= GST_BUFFER_DATA(buffer);
         #endif

         if ( GST_BUFFER_PTS_IS_VALID(buffer) )
         {
            pts= GST_BUFFER_PTS(buffer);
         }
         if ( GST_BUFFER_DTS_IS_VALID(buffer) )
         {
            dts= GST_BUFFER_DTS(buffer);
         }

         if ( dts == pts )
         {
            dts= -1LL;
         }
         
         si->pts= ((pts != -1LL) ? ((pts/NANO_SECONDS)*TICKS_90K + PTS_OFFSET) : -1LL);
         si->dts= ((dts != -1LL) ? ((dts/NANO_SECONDS)*TICKS_90K + PTS_OFFSET) : -1LL);
         if ( (si->dts != -1LL) && (si->pts-si->dts < DECODE_TIME) )
         {
            si->dts= ((dts != -1LL) ? (si->pts-DECODE_TIME) : -1LL);
         }

         if ( si->ctx->needTSEncapsulation && (si->firstPTS == -1LL) )
         {
            si->firstPTS= si->pts;
            INFO("captureProbe: pid %x sets firstPTS to %lld", si->pid, si->pts);
         }
         if ( si->ctx->captureStartTime == -1LL )
         {
            if ( si->pts != -1LL )
            {
               if ( si->ctx->needTSEncapsulation )
               {
                  if ( (si->ctx->videoPid == -1) || (si->pid == si->ctx->videoPid) )
                  {
                     si->ctx->captureStartTime= si->pts;
                     INFO("captureProbe: pid %x (video pid %x) sets captureStartTime to %lld", si->pid, si->ctx->videoPid, si->pts);
                  }
               }
               else
               {
                  si->ctx->captureStartTime= si->pts;
                  INFO("captureProbe: sets captureStartTime to %lld", si->pts);
               }
            }
            else
            {
               si->ctx->captureStartTime= getCurrentTimeMillis()*90LL;
            }
            if ( si->ctx->captureDuration )
            {
               si->ctx->captureStopTime= si->ctx->captureStartTime + si->ctx->captureDuration;
            }
            else
            {
               si->ctx->captureStopTime= -1LL;
            }
         }

         TRACE1("probeId %u (%s): type %d buffer %p : size %d data %p pts %lld dts %lld",
                 si->probeId, si->mimeType, info->type, info->data, inSize, inData, pts, dts);
         if ( inSize && inData )
         {
            if ( LEVEL_TRACE2 <= gDebugLevel )
            {
               int i, imax= 192;
               if ( imax > inSize ) imax= inSize;
               for( i= 0; i < imax; ++i )
               {           
                  iprintf(LEVEL_TRACE2, " %02X ", inData[i] );
                  if ( (i % 16) == 7 ) iprintf(LEVEL_TRACE2, " - ");
                  if ( (i % 16) == 15 ) iprintf(LEVEL_TRACE2, "\n");
               }
               iprintf(LEVEL_TRACE2,"\n");
            }
            processCaptureData( si->ctx, si, inData, inSize );
         }

         #ifdef USE_GST1
         gst_buffer_unmap( buffer, &map);
         #endif

         ++si->bufferCount;

         if ( !si->ctx->needTSEncapsulation )
         {
            long long now= getCurrentTimeMillis()*90LL;
            if ( (si->ctx->captureStopTime != -1LL) && (now >= si->ctx->captureStopTime) )
            {
               si->ctx->hitStopPoint= true;

               INFO("captureProbe: calling stopCapture: now %lld startTime %lld stopTime %lld",
                    now, si->ctx->captureStartTime, si->ctx->captureStopTime);
            }
         }
         else if ( si->ctx->hitStopPoint )
         {
            INFO("captureProbe: calling stopCapture: pid %x now %lld startTime %lld stopTime %lld",
                 si->pid, si->pts, si->ctx->captureStartTime, si->ctx->captureStopTime);
         }
         if ( si->ctx->hitStopPoint )
         {
            si->probeId= 0;

            si->ctx->goodCapture= true;

            stopCapture( si->ctx );

            return GST_PAD_PROBE_REMOVE;
         }
      }
      else if ( info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM )
      {
         GstEvent *event= (GstEvent*)info->data;
         if ( event )
         {
            DEBUG("captureProbe: pid %X event %d", si->pid, GST_EVENT_TYPE(event));
            switch (GST_EVENT_TYPE(event))
            {
               case GST_EVENT_EOS:
               {
                  INFO("captureProbe: pid %X EOS", si->pid);
                  si->probeId= 0;
                  si->ctx->goodCapture= true;
                  stopCapture( si->ctx );
                  return GST_PAD_PROBE_REMOVE;
               }
            }
         }
      }
   }

   return GST_PAD_PROBE_OK;
}

static int postSocketOpenedCallback(void *clientp, curl_socket_t curlfd, curlsocktype purpose)
{
   MediaCapContext *ctx= (MediaCapContext*)clientp;
   DEBUG("postSocketOpenedCallback: curlfd %d purpose %d", curlfd, purpose);
   if ( ctx )
   {
      ctx->curlfd= curlfd;
   }
   return CURL_SOCKOPT_OK;
}

static size_t postReadCallback(char *buffer, size_t size, size_t nitems, void *userData)
{
   size_t ret= 0;
   MediaCapContext *ctx= (MediaCapContext*)userData;
   TRACE1("postReadCallback: data %p size %d, nitems %d", buffer, size, nitems);
   if ( ctx )
   {
      int lenToRead= size*nitems;
      int offset= 0;

      if ( ctx->postThreadStopRequested )
      {
         ret= 0;
      }
      else
      {
         bool isFull;

         pthread_mutex_lock( &ctx->emitMutex );

         isFull= (ctx->emitCount >= ctx->emitCapacity);

         TRACE1("postReadCallback: isFull %d lenToRead %d", isFull, lenToRead);
         for( ; ; )
         {
            int avail= ctx->emitCount;
            int consume= lenToRead-offset;
            if ( consume > avail )
            {
               consume= avail;
            }
            TRACE1("postReadCallback: cap %d count %d avail %d consume %d", ctx->emitCapacity, ctx->emitCount, avail, consume);
            
            while( consume )
            {
               int copylen= (ctx->emitHead > ctx->emitTail) ? ctx->emitHead-ctx->emitTail : ctx->emitCapacity-ctx->emitTail;
               if ( copylen > consume )
               {
                  copylen= consume;
               }         
               TRACE1("postReadCallback: consume %d copylen %d head %d tail %d", consume, copylen, ctx->emitHead, ctx->emitTail);
               if ( copylen )
               {
                  memcpy( &buffer[offset], &ctx->emitBuffer[ctx->emitTail], copylen );
                  offset += copylen;
                  consume -= copylen;
                  ctx->emitTail += copylen;
                  ctx->emitCount -= copylen;
                  if ( ctx->emitTail >= ctx->emitCapacity ) ctx->emitTail= 0;
               }
               else
               {
                  break;
               }
            }
            isFull= (ctx->emitCount >= ctx->emitCapacity);
            pthread_mutex_unlock( &ctx->emitMutex );

            TRACE1("postReadCallback: offset %d dataLen %d", offset, lenToRead);
            if ( offset < lenToRead )
            {
               // Wait for more data
               TRACE1("postReadCallback: wait till not empty...");
               pthread_mutex_lock( &ctx->emitMutex );
               pthread_mutex_lock( &ctx->emitNotEmptyMutex );
               pthread_mutex_unlock( &ctx->emitMutex );
               pthread_cond_wait( &ctx->emitNotEmptyCond, &ctx->emitNotEmptyMutex );
               pthread_mutex_unlock( &ctx->emitNotEmptyMutex );
               TRACE1("postReadCallback: done wait till not empty");
            }
            else
            {
               break;
            }
            if ( ctx->postThreadStopRequested )
            {
               offset= 0;
               break;
            }
            pthread_mutex_lock( &ctx->emitMutex );
         }

         if ( !isFull )
         {
            // Signal not full
            TRACE1("postReadCallback: signal not full: count %d capacity %d", ctx->emitCount, ctx->emitCapacity);
            pthread_mutex_lock( &ctx->emitMutex );
            pthread_mutex_lock( &ctx->emitNotFullMutex );
            pthread_mutex_unlock( &ctx->emitMutex );
            pthread_cond_signal( &ctx->emitNotFullCond );
            pthread_mutex_unlock( &ctx->emitNotFullMutex );
         }
         TRACE1("postReadCallback: done");

         ctx->totalBytesPosted += offset;
         long long intervalBytes= ctx->totalBytesPosted-ctx->lastReportedPostedBytes;
         if ( ctx->reportProgress && (intervalBytes >= ctx->progressInterval) )
         {
            if ( ctx->apiObj.ptr() )
            {
               ((rtMediaCaptureObject*)ctx->apiObj.ptr())->reportCaptureProgress( intervalBytes, ctx->totalBytesPosted );
            }
            ctx->lastReportedPostedBytes= ctx->totalBytesPosted;
         }

         ret= offset;
      }
   }
   TRACE1("postReadCallback: exit %d", ret);
   return ret;
}

static void* postThread( void *arg )
{
   MediaCapContext *ctx= (MediaCapContext*)arg;
   CURLcode res;

   INFO("postThread: enter");
   ctx->postThreadStarted= true;

   res= curl_easy_perform(ctx->curl);
   if ( res != CURLE_OK )
   {
      ERROR("postThread: curl error %d from curl_easy_perform", res );
      ctx->postThreadAborted= true;

      // ensure awaken from wait not full
      pthread_mutex_lock( &ctx->emitMutex );
      pthread_mutex_lock( &ctx->emitNotFullMutex );
      pthread_mutex_unlock( &ctx->emitMutex );
      pthread_cond_signal( &ctx->emitNotFullCond );
      pthread_mutex_unlock( &ctx->emitNotFullMutex );
   }

   ctx->postThreadStarted= false;

   INFO("postThread: ending");

   return NULL;
}

static bool prepareEndpoint( MediaCapContext *ctx, const char *endPoint )
{
   bool result= false;
   int rc;
   CURLcode res;
   CURL *curl= 0;
   struct curl_slist *httpHeaderItems= NULL;

   INFO("prepareEndpoint: endpoint (%s)", endPoint);

   curl= curl_easy_init();
   if ( !curl )
   {
      ERROR("prepareEndpoint: unable to create curl session");
      goto exit;
   }

   curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, ctx->errorBuffer);
   curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
   curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
   curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
   curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, POST_TIMEOUT);
   curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100);
   
   res= curl_easy_setopt(curl, CURLOPT_URL, endPoint );
   if ( res != CURLE_OK )
   {
      ERROR("prepareEndpoint: curl error %d setting CURLOPT_URL to (%s)\n", res, endPoint );
      goto exit;
   }
   
   res= curl_easy_setopt(curl, CURLOPT_POST, 1 );
   if ( res != CURLE_OK )
   {
      ERROR("prepareEndpoint: curl error %d setting CURLOPT_POST to (%s)\n", res, endPoint );
      goto exit;
   }

   httpHeaderItems= curl_slist_append( httpHeaderItems, "Content-Type: video/mpeg" );
   if( httpHeaderItems == NULL )
   {
      ERROR("prepareEndpoint: Unable to append CURLOPT_HTTPHEADER Content-Type attribute");
      goto exit;
   }

   httpHeaderItems= curl_slist_append( httpHeaderItems, "Transfer-Encoding: chunked" );
   if( httpHeaderItems == NULL )
   {
      ERROR("prepareEndpoint: Unable to append CURLOPT_HTTPHEADER Transfer-Encoding attribute");
      goto exit;
   }

   res= curl_easy_setopt(curl, CURLOPT_HTTPHEADER, httpHeaderItems);
   if ( res != CURLE_OK )
   {
      ERROR("prepareEndpoint: curl error %d setting CURLOPT_HTTPHEADER", res );
      goto exit;
   }

   res= curl_easy_setopt(curl, CURLOPT_READFUNCTION, postReadCallback);
   if ( res != CURLE_OK )
   {
      ERROR("prepareEndpoint: curl error %d setting CURLOPT_READFUNCTION", res );
      goto exit;
   }

   res= curl_easy_setopt(curl, CURLOPT_READDATA, ctx);
   if ( res != CURLE_OK )
   {
      ERROR("prepareEndpoint: curl error %d setting CURLOPT_READDATA", res );
      goto exit;
   }

   res= curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, postSocketOpenedCallback);
   if ( res != CURLE_OK )
   {
      ERROR("prepareEndpoint: curl error %d setting CURLOPT_SOCKOPTFUNCTION", res );
      goto exit;
   }

   res= curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA, ctx);
   if ( res != CURLE_OK )
   {
      ERROR("prepareEndpoint: curl error %d setting CURLOPT_SOCKOPTDATA", res );
      goto exit;
   }

   ctx->emitBuffer= (char *)malloc( EMIT_BUFFER_SIZE );
   if ( !ctx->emitBuffer )
   {
      ERROR("prepareEndpoint: unable to alloc memory for post emit buffer");
      goto exit;
   }
   ctx->emitCapacity= EMIT_BUFFER_SIZE;
   ctx->emitHead= 0;
   ctx->emitTail= 0;
   ctx->emitCount= 0;

   pthread_mutex_init( &ctx->emitMutex, 0 );
   pthread_mutex_init( &ctx->emitNotEmptyMutex, 0 );
   pthread_cond_init( &ctx->emitNotEmptyCond, 0 );
   pthread_mutex_init( &ctx->emitNotFullMutex, 0 );
   pthread_cond_init( &ctx->emitNotFullCond, 0 );

   ctx->curl= curl;
   ctx->postThreadStopRequested= false;
   ctx->postThreadAborted= false;

   rc= pthread_create( &ctx->postThreadId, NULL, postThread, ctx );
   if ( rc )
   {
      ERROR("prepareEndpoint failed to start postThread");
      ctx->curl= 0;
      goto exit;
   }

   result= true;

exit:

   if ( !result )
   {
      if ( curl )
      {
         curl_easy_cleanup( curl );
      }
      if ( httpHeaderItems )
      {
         curl_slist_free_all( httpHeaderItems );
      }
   }

   return result;
}

static void startCapture( MediaCapContext *ctx, bool toFile, const char *dest, int duration )
{
   bool okToStart= true;

   INFO("startCapture: enter: ctx %p", ctx);
   
   if ( ctx )
   {
      ctx->goodCapture= false;
      ctx->hitStopPoint= false;
      ctx->captureCompleteSent= false;
      ctx->totalBytesPosted= 0;
      ctx->lastReportedPostedBytes= 0;

      if ( toFile )
      {
         ctx->captureToFile= true;

         ctx->pCaptureFile= fopen( dest, "wb" );
         if ( !ctx->pCaptureFile )
         {
            ERROR("startCapture: failed to open capture file (%s) errno %d", dest, errno);
            okToStart= false;
         }
      }
      else
      {
         ctx->captureToFile= false;

         if ( !prepareEndpoint( ctx, dest ) )
         {
            ERROR("startCapture: failed to access endpoint (%s)", dest);
            okToStart= false;
         }
      }

      if ( okToStart )
      {
         ctx->foundStartPoint= false;
         ctx->durationCaptured= 0LL;
         ctx->lastEmittedPTS= -1LL;
         ctx->captureStartTime= -1LL;
         ctx->captureDuration= (duration*TICKS_90K_PER_MILLISECOND);

         if ( ctx->needTSEncapsulation )
         {
            ctx->needEmitPATPMT= true;
            ctx->needEmitPCR= true;
         }

         // Add non-video probe
         for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
               it != ctx->srcList.end();
               ++it )
         {
            SrcInfo *si= (*it);

            if ( !si->isVideo )
            {
               si->bufferCount= 0;
               si->firstPTS= -1LL;
               si->accumFirstPTS= -1LL;
               si->accumLastPTS= -1LL;
               si->accumTestPTS= -1LL;
               si->accumOffset= 0;
               si->probeId= gst_pad_add_probe( si->pad,
                                               (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                                               captureProbe,
                                               si,
                                               captureProbeDestroy );
               INFO("src (%s) non-video probeId %u", si->mimeType, si->probeId);
            }
         }
            
         // Add video probe
         for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
               it != ctx->srcList.end();
               ++it )
         {
            SrcInfo *si= (*it);

            if ( si->isVideo )
            {
               si->bufferCount= 0;
               si->firstPTS= -1LL;
               si->accumFirstPTS= -1LL;
               si->accumLastPTS= -1LL;
               si->accumTestPTS= -1LL;
               si->accumOffset= 0;
               si->probeId= gst_pad_add_probe( si->pad,
                                               (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER|(int)GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                                               captureProbe,
                                               si,
                                               captureProbeDestroy );
               INFO("src (%s) video probeId %u", si->mimeType, si->probeId);
            }
         }

         ctx->captureActive= true;
      }
   }
   INFO("startCapture: exit: ctx %p", ctx);
}

static void stopCapture( MediaCapContext *ctx )
{
   INFO("stopCapture: enter: ctx %p", ctx);
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->captureActive= false;

      for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
            it != ctx->srcList.end();
            ++it )
      {
         SrcInfo *si= (*it);
         if ( si->probeId )
         {
            gulong probeId= si->probeId;
            INFO("stopCapture: remove probe %d for pad %p", si->probeId, si->pad);
            si->probeId= 0;
            gst_pad_remove_probe( si->pad, probeId );
         }
      }

      if ( ctx->captureToFile )
      {
         if ( ctx->pCaptureFile )
         {
            fclose( ctx->pCaptureFile );
            ctx->pCaptureFile= 0;
         }

         #ifdef MEDIACAPTURE_USE_RTREMOTE
         if ( ctx->apiObj.ptr() )
         {
            ((rtMediaCaptureObject*)ctx->apiObj.ptr())->reportCaptureComplete();
         }
         #endif
      }
      else
      {
         if ( ctx->postThreadStarted )
         {
            ctx->postThreadStopRequested= true;

            // ensure awaken from wait not full
            pthread_mutex_lock( &ctx->emitMutex );
            pthread_mutex_lock( &ctx->emitNotFullMutex );
            pthread_mutex_unlock( &ctx->emitMutex );
            pthread_cond_signal( &ctx->emitNotFullCond );
            pthread_mutex_unlock( &ctx->emitNotFullMutex );

            // ensure awaken from waiting not empty
            pthread_mutex_lock( &ctx->emitMutex );
            pthread_mutex_lock( &ctx->emitNotEmptyMutex );
            pthread_mutex_unlock( &ctx->emitMutex );
            pthread_cond_signal( &ctx->emitNotEmptyCond );
            pthread_mutex_unlock( &ctx->emitNotEmptyMutex );

            DEBUG("stopCapture: calling pthread_join");
            pthread_join( ctx->postThreadId, NULL );
            DEBUG("stopCapture: done calling pthread_join");            
         }

         if ( !ctx->captureCompleteSent )
         {
            if ( ctx->needTSEncapsulation )
            {
               if ( ctx->lastEmittedPTS != -1LL )
               {
                  ctx->durationCaptured= (ctx->lastEmittedPTS - ctx->captureStartTime)/TICKS_90K_PER_MILLISECOND;
               }
               INFO("lastEmittedPTS %lld captureStartTime %lld durationCaptured %lld", ctx->lastEmittedPTS, ctx->captureStartTime, ctx->durationCaptured);
            }
            else
            {
               long long now= getCurrentTimeMillis()*TICKS_90K_PER_MILLISECOND;
               ctx->durationCaptured= (now-ctx->captureStartTime)/TICKS_90K_PER_MILLISECOND;
               INFO("now %lld captureStartTime %lld durationCaptured %lld", now, ctx->captureStartTime, ctx->durationCaptured);
            }
            #ifdef MEDIACAPTURE_USE_RTREMOTE
            if ( ctx->apiObj.ptr() )
            {
               ((rtMediaCaptureObject*)ctx->apiObj.ptr())->reportCaptureComplete();
            }
            #endif
            ctx->captureCompleteSent= true;
         }

         if ( ctx->curl )
         {
            curl_easy_cleanup( ctx->curl );

            pthread_mutex_destroy( &ctx->emitMutex );
            pthread_mutex_destroy( &ctx->emitNotEmptyMutex );
            pthread_mutex_destroy( &ctx->emitNotFullMutex );
            pthread_cond_destroy( &ctx->emitNotEmptyCond );
            pthread_cond_destroy( &ctx->emitNotFullCond );
            ctx->curlfd= -1;
            ctx->curl= 0;
         }
         if ( ctx->emitBuffer )
         {
            free( ctx->emitBuffer );
            ctx->emitBuffer= 0;
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
   INFO("stopCapture: exit: ctx %p", ctx);
}

static void processCaptureData( MediaCapContext *ctx, SrcInfo *si, unsigned char *data, int len )
{
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      
      if ( !ctx->captureToFile || ctx->pCaptureFile )
      {
         if ( ctx->needTSEncapsulation )
         {
            #ifdef MEDIACAPTURE_CAPTURE_ES_BUFFERS
            char name[256];
            FILE *pFile;
            
            sprintf( name, "/opt/mediacapture-pid%04X-%d.dat", si->pid, si->bufferCount );
            pFile= fopen( name, "wb");
            if ( pFile )
            {
               fwrite( data, 1, len, pFile );
               fclose( pFile );
            }
            #endif
            performEncapsulation( ctx, si, data, len );
         }
         else
         {
            TRACE1("processCaptureData: calling emitCaptureData: data %p len %d", data, len);
            emitCaptureData( ctx, data, len );
            TRACE1("processCaptureData: done calling emitCaptureData");
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

static bool readTimeStamp( unsigned char *p, long long& timestamp )
{
   bool result= true;
   
   if ( ((p[0] & 0x01) != 1) || ((p[2] & 0x01) != 1) || ((p[4] & 0x01) != 1) )
   {
      result= false;
   }
   switch ( (p[0] & 0xF0) >> 4 )
   {
      case 1:
      case 2:
      case 3:
         break;
      default:
         result= false;
         break;
   }
   
   timestamp= ((((long long)(p[0]&0x0E))<<30)>>1) |
              (( (long long)(p[1]&0xFF))<<22) |
              ((((long long)(p[2]&0xFE))<<15)>>1) |
              (( (long long)(p[3]&0xFF))<<7) |
              (( (long long)(p[4]&0xFE))>>1);
   
   return result;
}

static int writeTimeStamp( unsigned char *p, long long pts, long long dts )
{
   int len= 0;
   int prefix;
   if ( pts != -1LL )
   {
      len += 5;
      prefix= 0x02;
      if ( dts != -1LL )
      {
         prefix |= 0x01;
      }
      p[0]= (((prefix&0xF)<<4)|(((pts>>30)&0x7)<<1)|0x01);
      p[1]= ((pts>>22)&0xFF);
      p[2]= ((((pts>>15)&0x7F)<<1)|0x01);
      p[3]= ((pts>>7)&0xFF);
      p[4]= ((((pts)&0x7F)<<1)|0x01);
      if ( dts != -1LL )
      {
         len += 5;
         prefix= 0x01;
         p[5]= (((prefix&0xF)<<4)|(((dts>>30)&0x7)<<1)|0x01);
         p[6]= ((dts>>22)&0xFF);
         p[7]= ((((dts>>15)&0x7F)<<1)|0x01);
         p[8]= ((dts>>7)&0xFF);
         p[9]= ((((dts)&0x7F)<<1)|0x01);
      }
   }
   return len;
}

static int writePCR( unsigned char *p, long long pcr )
{
   p[0]= ((pcr>>(33-8))&0xFF);
   p[1]= ((pcr>>(33-16))&0xFF);
   p[2]= ((pcr>>(33-24))&0xFF);
   p[3]= ((pcr>>(33-32))&0xFF);
   p[4]= ((pcr&0x01)<<7)|(0x7E);
   p[5]= 0x00;
   return 6;
}

static bool discardCaptureData( MediaCapContext *ctx, long long startPTS )
{
   bool noDataLeft= false;
   INFO("discardCaptureData: discard data prior to PTS %lld", startPTS );
   for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
         it != ctx->srcList.end();
         ++it )
   {
      SrcInfo *si= (*it);
      unsigned char *packet;
      long long ptsFirst= -1LL;
      long long pts= -1LL;
      int firstBlockOffset= -1;
      bool discardingData= false;

      for( int i= 0; i < si->accumOffset; i += DEFAULT_PACKET_SIZE )
      {
         packet= si->accumulator+i;
         if ( packet[1] & 0x40 )
         {
            int payloadOffset= 4;
            if ( packet[3] & 0x20 )
            {
               payloadOffset += (1+packet[4]);
            }
            if ( (packet[payloadOffset] == 0x00) && (packet[payloadOffset+1] == 0x00) && (packet[payloadOffset+2] == 0x01) )
            {
               if ( packet[payloadOffset+7] & 0x80 )
               {
                  bool validPTS= readTimeStamp( &packet[payloadOffset+9], pts );
                  if ( validPTS )
                  {
                     if ( ptsFirst < 0 )
                     {
                        ptsFirst= pts;
                     }
                     if ( pts < startPTS )
                     {
                        discardingData= true;
                     }
                     else
                     {
                        firstBlockOffset= i;
                        break;
                     }
                  }
               }
            }
         }
      }
      if ( firstBlockOffset == -1 )
      {
         DEBUG("discardCaptureData: pid %d discarding pts %lld to %lld inclusive", si->pid, ptsFirst, pts);
         noDataLeft= true;
         si->accumOffset= 0;
      }
      else if ( discardingData && (firstBlockOffset > 0) )
      {
         DEBUG("discardCaptureData: pid %d discarding pts %lld to %lld", si->pid, ptsFirst, pts);
         int dataRemaining= si->accumOffset-firstBlockOffset;
         if ( dataRemaining )
         {
            memmove( si->accumulator, si->accumulator+firstBlockOffset, dataRemaining );
         }
         si->accumOffset= dataRemaining;
      }
   }
   return noDataLeft;
}

static void flushCaptureData( MediaCapContext *ctx )
{
   for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
         it != ctx->srcList.end();
         ++it )
   {
      SrcInfo *si= (*it);

      flushPacket( ctx, si );
      if ( si->accumOffset )
      {
         if ( (ctx->pcrPid == si->pid) && (si->accumLastPTS >= 0) )
         {
            ctx->lastEmittedPTS= si->accumLastPTS;
         }
         emitCaptureData( ctx, si->accumulator, si->accumOffset );
         si->accumOffset= 0;
      }
   }
}

static void flushCaptureDataByTime( MediaCapContext *ctx, long long ptsLimit, long long nextPCR )
{   
   long long nextPTSToEmit;

   DEBUG("flushCaptureDataByTime: ptsLimit %llx", ptsLimit);
   for( ; ; )
   {
      nextPTSToEmit= -1LL;

      for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
            it != ctx->srcList.end();
            ++it )
      {
         SrcInfo *si= (*it);
         unsigned char *packet;
         long long firstPTS= -1LL;
         int firstBlockLen= 0;
         long long ptsTest;

         for( int i= 0; i < si->accumOffset; i += DEFAULT_PACKET_SIZE )
         {
            packet= si->accumulator+i;
            if ( packet[1] & 0x40 )
            {
               int payloadOffset= 4;
               if ( packet[3] & 0x20 )
               {
                  payloadOffset += (1+packet[4]);
               }
               if ( (packet[payloadOffset] == 0x00) && (packet[payloadOffset+1] == 0x00) && (packet[payloadOffset+2] == 0x01) )
               {
                  if ( packet[payloadOffset+7] & 0x80 )
                  {
                     long long pts;
                     bool validPTS= readTimeStamp( &packet[payloadOffset+9], pts );
                     if ( validPTS )
                     {
                        if ( firstPTS == -1L )
                        {
                           firstPTS= pts;
                        }
                        else
                        {
                           firstBlockLen= i;
                           break;
                        }
                     }
                  }
               }
            }
         }

         ptsTest= (si->isVideo ? firstPTS : ((firstPTS + (VIDEO_PTS_PCR_OFFSET-AUDIO_PTS_PCR_OFFSET))&MAX_90K));
         if ( (ptsTest < ptsLimit) || (!si->isVideo && (firstPTS < nextPCR) && (firstBlockLen > 0)) )
         {
            si->accumTestPTS= ptsTest;
            si->accumFirstPTS= firstPTS;
            si->firstBlockLen= firstBlockLen;
            if ( (nextPTSToEmit == -1LL) || (ptsTest < nextPTSToEmit) )
            {
               nextPTSToEmit= ptsTest;
            }
         }
      }

      if ( nextPTSToEmit != -1LL )
      {
         for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
               it != ctx->srcList.end();
               ++it )
         {
            SrcInfo *si= (*it);

            if ( si->pid != ctx->pcrPid )
            {
               if ( nextPTSToEmit == si->accumTestPTS )
               {
                  int dataRemaining= si->accumOffset-si->firstBlockLen;
                  DEBUG("flushCaptureDataByTime: calling emitCaptureData: pid %X pts %llx len %d", si->pid, si->accumFirstPTS, si->firstBlockLen);
                  emitCaptureData( ctx, si->accumulator, si->firstBlockLen );
                  if ( dataRemaining )
                  {
                     memmove( si->accumulator, si->accumulator+si->firstBlockLen, dataRemaining );
                  }
                  si->accumOffset= dataRemaining;
               }
            }
         }
         for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
               it != ctx->srcList.end();
               ++it )
         {
            SrcInfo *si= (*it);

            if ( si->pid == ctx->pcrPid )
            {
               if ( nextPTSToEmit == si->accumTestPTS )
               {
                  int dataRemaining= si->accumOffset-si->firstBlockLen;
                  DEBUG("flushCaptureDataByTime: calling emitCaptureData: pid %X pts %llx len %d", si->pid, si->accumFirstPTS, si->firstBlockLen);
                  ctx->lastEmittedPTS= nextPTSToEmit;
                  emitCaptureData( ctx, si->accumulator, si->firstBlockLen );
                  if ( dataRemaining )
                  {
                     memmove( si->accumulator, si->accumulator+si->firstBlockLen, dataRemaining );
                  }
                  si->accumOffset= dataRemaining;
               }
            }
         }

         if ( (ctx->captureStopTime > 0) && (ctx->lastEmittedPTS >= ctx->captureStopTime) )
         {
            INFO("flushCaptureDataByTime: lastEmittedPTS %lld captureStopTime %lld", ctx->lastEmittedPTS, ctx->captureStopTime);
            ctx->hitStopPoint= true;
            break;
         }

         nextPTSToEmit= -1LL;
      }
      else
      {
         break;
      }
   }
}

static void checkBufferLevels( MediaCapContext *ctx, long long pcr )
{
   long long firstCommonPTS= -1LL, lastCommonPTS= -1LL;
   
   for ( std::vector<SrcInfo*>::iterator it= ctx->srcList.begin();
         it != ctx->srcList.end();
         ++it )
   {
      SrcInfo *si= (*it);
      unsigned char *packet;
      long long firstPTS= -1LL, lastPTS= -1LL;

      flushPacket( ctx, si );
      for( int i= 0; i < si->accumOffset; i += DEFAULT_PACKET_SIZE )
      {
         packet= si->accumulator+i;

         if ( packet[1] & 0x40 )
         {
            int payloadOffset= 4;
            if ( packet[3] & 0x20 )
            {
               payloadOffset += (1+packet[4]);
            }
            if ( (packet[payloadOffset] == 0x00) && (packet[payloadOffset+1] == 0x00) && (packet[payloadOffset+2] == 0x01) )
            {
               if ( packet[payloadOffset+7] & 0x80 )
               {
                  long long pts;
                  bool validPTS= readTimeStamp( &packet[payloadOffset+9], pts );
                  if ( validPTS )
                  {
                     if ( firstPTS == -1LL )
                     {
                        firstPTS= pts;
                     }
                     lastPTS= pts;
                  }
               }
            }
         }
      }

      DEBUG("pid %X accumulator contains pts %llx to %llx next pcr %llx", si->pid, firstPTS, lastPTS, pcr);
      si->accumFirstPTS= firstPTS;
      si->accumLastPTS= lastPTS;

      if ( firstPTS != -1LL )
      {
         switch( firstCommonPTS )
         {
            case -1LL:
               firstCommonPTS= firstPTS;
               break;
            case -2LL:
               firstCommonPTS= -2LL;
               break;
            default:
               if ( ctx->foundStartPoint )
               {
                  firstCommonPTS= min( firstCommonPTS, firstPTS );
               }
               else
               {
                  firstCommonPTS= max( firstCommonPTS, firstPTS );
               }
               break;
         }
      }
      else
      {
         firstCommonPTS= -2LL;
      }
      if ( lastPTS != -1LL )
      {
         switch( lastCommonPTS )
         {
            case -1LL:
               lastCommonPTS= lastPTS;
               break;
            case -2LL:
               lastCommonPTS= -2LL;
               break;
            default:
               lastCommonPTS= min( lastCommonPTS, lastPTS );
               break;
         }
      }
      else
      {
         lastCommonPTS= -2LL;
      }
   }
   
   DEBUG("firstCommonPTS %llx lastCommonPTS %llx", firstCommonPTS, lastCommonPTS);
   if ( (firstCommonPTS > 0) && (lastCommonPTS > firstCommonPTS) )
   {
      if ( !ctx->foundStartPoint )
      {
         ctx->foundStartPoint= !discardCaptureData( ctx, firstCommonPTS );
         if ( ctx->foundStartPoint )
         {
            ctx->captureStartTime= firstCommonPTS;
            if ( ctx->captureDuration )
            {
               ctx->captureStopTime= ctx->captureStartTime + ctx->captureDuration;
            }
         }
      }
      if ( ctx->foundStartPoint && !ctx->hitStopPoint )
      {
         flushCaptureDataByTime( ctx, lastCommonPTS, pcr );
      }
   }
}

static void flushPacket( MediaCapContext *ctx, SrcInfo *si )
{
   unsigned char *packet= si->packet;
   int lenAvail= DEFAULT_PACKET_SIZE-si->packetOffset;
   if ( lenAvail < (DEFAULT_PACKET_SIZE-4) )
   {
      if ( lenAvail != 0 )
      {
         // Pad partially filled packet using adaptation fill bytes
         if ( si->packet[3] & 0x20 )
         {
            int adaptationLen= si->packet[4];
            memmove( si->packet+4+(adaptationLen+1)+lenAvail, si->packet+4+(adaptationLen+1), DEFAULT_PACKET_SIZE-4-(adaptationLen+1)-lenAvail );
            memset( si->packet+4+(adaptationLen+1), 0xFF, lenAvail );
            adaptationLen += lenAvail;
            si->packet[4]= adaptationLen;
         }
         else
         {
            si->packet[3] |= 0x20;
            memmove( si->packet+4+lenAvail, si->packet+4, DEFAULT_PACKET_SIZE-4-lenAvail );
            si->packet[4]= lenAvail-1;
            if ( lenAvail > 1 )
            {
               si->packet[5]= 0x00;
               if ( lenAvail > 2 )
               {
                  memset( si->packet+6, 0xFF, lenAvail-2 );
               }
            }
         }
         si->packetOffset += lenAvail;
      }
      memcpy( si->accumulator+si->accumOffset, si->packet, DEFAULT_PACKET_SIZE );
      si->accumOffset += DEFAULT_PACKET_SIZE;
      if ( si->accumOffset == si->accumSize )
      {
         int accumSizeNew= si->accumSize*2;
         unsigned char *accumulatorNew= (unsigned char*)malloc( accumSizeNew );
         if ( accumulatorNew )
         {
            INFO("flushPacket: grow pid %d accumulator to %d bytes", si->pid, accumSizeNew);
            memcpy( accumulatorNew, si->accumulator, si->accumSize );
            free( si->accumulator );
            si->accumulator= accumulatorNew;
            si->accumSize= accumSizeNew;
         }
         else
         {
            ERROR("flushPacket: unable to grow pid %d accumulator - capture will be missing some ES data for other pids", si->pid);

            DEBUG("flushPacket: calling emitCaptureData: pid %X len %d", si->pid, si->accumOffset);
            if ( (ctx->pcrPid == si->pid) && (si->accumLastPTS >= 0) )
            {
               ctx->lastEmittedPTS= si->accumLastPTS;
            }
            emitCaptureData( ctx, si->accumulator, si->accumOffset );
            si->accumOffset= 0;
         }
      }
      si->packetOffset= 0;
      lenAvail= DEFAULT_PACKET_SIZE;
   }
   if ( lenAvail == DEFAULT_PACKET_SIZE )
   {
      packet[0]= 0x47;
      packet[1]= (0x00 | (si->pid >> 8));
      packet[2]= (si->pid & 0xFF);
      packet[3]= (0x10 | (si->continuityCount&0x0F));
      si->continuityCount= ((si->continuityCount+1)&0x0F);
      si->packetOffset= 4;
   }
}

static void addToPacket( MediaCapContext *ctx, SrcInfo *si, unsigned char *data, int len )
{
   int lenToCopy;
   int lenAvail;
   
   while( len )
   {
      assert( (si->packetOffset >= 0) && (si->packetOffset <= DEFAULT_PACKET_SIZE) );
      lenAvail= DEFAULT_PACKET_SIZE-si->packetOffset;
      if ( lenAvail == 0 )
      {
         flushPacket( ctx, si );
         lenAvail= DEFAULT_PACKET_SIZE-si->packetOffset;
      }
      lenToCopy= len;
      if ( lenToCopy > lenAvail )
      {
         lenToCopy= lenAvail;
      }
      memcpy( si->packet+si->packetOffset, data, lenToCopy );
      data += lenToCopy;
      len -= lenToCopy;
      si->packetOffset += lenToCopy;
   }
}

static void performEncapsulation( MediaCapContext *ctx, SrcInfo *si, unsigned char *data, int len )
{
   unsigned char *packet;
   int pesHdrDataLen= 0;
   static unsigned char startCode[3]= {0x00, 0x00, 0x01 };
   bool firstPCR= ctx->needEmitPATPMT;

   DEBUG("performEncapsulation: enter: pid %X pts %llx data %p len %d", si->pid, si->pts, data, len);
   if ( ctx->needEmitPATPMT )
   {
      emitCaptureData( ctx, ctx->patPacket, DEFAULT_PACKET_SIZE );
      emitCaptureData( ctx, ctx->pmtPacket, DEFAULT_PACKET_SIZE );
      ctx->needEmitPATPMT= false;
   }

   packet= si->packet;

   flushPacket( ctx, si );   

   if ( si->pid == ctx->pcrPid )
   {
      if ( (si->bufferCount % 10) == 9 )
      {
         ctx->needEmitPCR= true;
      }
   }

   if ( ctx->needEmitPCR )
   {
      long long pcr= 0;

      assert( si->packetOffset == 4 );
      if ( si->pts != -1L )
      {
         pcr= ((si->pts - ctx->ptsPcrOffset)&MAX_90K);
      }

      checkBufferLevels( ctx, pcr );

      packet[1]= (0x60 | (ctx->pcrPid >> 8));
      packet[2]= (ctx->pcrPid & 0xFF);
      si->continuityCount= ((si->continuityCount-1)&0x0F);
      packet[3]= (0x20 | si->continuityCount&0x0F);
      packet[4]= 0x07;
      packet[5]= 0x10;
      si->packetOffset += (2 + writePCR( packet+6, pcr ));

      flushPacket( ctx, si );
      ctx->needEmitPCR= false;

      if ( firstPCR )
      {
         flushCaptureData( ctx );
      }
   }

   assert( si->packetOffset == 4 );
   packet[1] |= 0x40;
   packet[4]= 0x00;
   packet[5]= 0x00;
   packet[6]= 0x01;
   packet[7]= si->streamId;
   if ( si->isVideo )
   {
      packet[8]= 0x00;
      packet[9]= 0x00;
   }
   else
   {
      int payloadLen= len;
      if ( si->audioPESHdr )
      {
         payloadLen += si->audioPESHdrLen;
      }
      packet[8]= ((payloadLen >> 8)&0xFF);
      packet[9]= (payloadLen&0xFF);
   }
   packet[10]= 0x80;
   packet[11]= 0x00;
   if ( si->pts != -1LL )
   {
      packet[11] |= 0x80;
      pesHdrDataLen += 5;
      if ( si->dts != -1LL )
      {
         packet[11] |= 0x40;
         pesHdrDataLen += 5;
      }
   }
   packet[12]= pesHdrDataLen;
   si->packetOffset += 9;
   writeTimeStamp( si->packet+si->packetOffset, si->pts, si->dts );
   si->packetOffset += pesHdrDataLen;
   if ( si->isVideo && !si->isByteStream )
   {
      if ( data[4] & 0x20 )
      {
         si->needEmitSPSPPS= true;
      }
      if ( si->needEmitSPSPPS )
      {
         addToPacket( ctx, si, si->spspps, si->spsppsLen );
         si->needEmitSPSPPS= false;
      }

      addToPacket( ctx, si, startCode, 3 );
      data += 4;
      len -= 4;
   }
   else if ( !si->isVideo && si->audioPESHdr )
   {
      if ( si->mpegversion == 4 )
      {
         updateAudioAACPESHeader( ctx, si, len );
      }
      addToPacket( ctx, si, si->audioPESHdr, si->audioPESHdrLen );
   }

   addToPacket( ctx, si, data, len );
   DEBUG("performEncapsulation: exit: pid %X pts %llx accumOffset %d", si->pid, si->pts, si->accumOffset);
}

static void emitCaptureData( MediaCapContext *ctx, unsigned char *data, int dataLen )
{
   if ( ctx && data && dataLen )
   {
      if ( ctx->captureToFile )
      {
         int lenDidWrite;
         
         lenDidWrite= fwrite( data, 1, dataLen, ctx->pCaptureFile );
         if ( lenDidWrite != dataLen )
         {
            ERROR("error writing to capture file: errono %d", errno);
         }

         ctx->totalBytesEmitted += lenDidWrite;
      }
      else if ( !ctx->postThreadStopRequested && !ctx->postThreadAborted )
      {
         int offset= 0;
         bool isEmpty= false;

         pthread_mutex_lock( &ctx->emitMutex );

         isEmpty= !ctx->emitCount;

         TRACE1("emitCaptureData: endpoint: isEmpty %d dataLen %d", isEmpty, dataLen);
         for( ; ; )
         {
            int avail= ctx->emitCapacity-ctx->emitCount;
            int consume= dataLen-offset;
            if ( consume > avail )
            {
               consume= avail;
            }
            TRACE1("emitCaptureData: endpoint: cap %d count %d avail %d consume %d", ctx->emitCapacity, ctx->emitCount, avail, consume);
            
            while( consume )
            {
               int copylen= (ctx->emitHead >= ctx->emitTail) ? ctx->emitCapacity-ctx->emitHead : ctx->emitTail-ctx->emitHead;
               if ( copylen > consume )
               {
                  copylen= consume;
               }         
               TRACE1("emitCaptureData: endpoint: consume %d copylen %d head %d tail %d", consume, copylen, ctx->emitHead, ctx->emitTail);
               if ( copylen )
               {
                  memcpy( &ctx->emitBuffer[ctx->emitHead], &data[offset], copylen );
                  offset += copylen;
                  consume -= copylen;
                  ctx->emitHead += copylen;
                  ctx->emitCount += copylen;
                  if ( ctx->emitHead >= ctx->emitCapacity ) ctx->emitHead= 0;
                  ctx->totalBytesEmitted += copylen;
               }
               else
               {
                  break;
               }
            }
            isEmpty= !ctx->emitCount;
            pthread_mutex_unlock( &ctx->emitMutex );

            TRACE1("emitCaptureData: endpoint: offset %d dataLen %d", offset, dataLen);
            if ( offset < dataLen )
            {
               int rc;
               struct timeval now;
               struct timespec timeout;
               int timelimit= POST_TIMEOUT;

               if ( !isEmpty )
               {
                  // Signal not empty
                  TRACE1("emitCaptureData: endpoint: signal not empty: count %d", ctx->emitCount);
                  pthread_mutex_lock( &ctx->emitMutex );
                  pthread_mutex_lock( &ctx->emitNotEmptyMutex );
                  pthread_mutex_unlock( &ctx->emitMutex );
                  pthread_cond_signal( &ctx->emitNotEmptyCond );
                  pthread_mutex_unlock( &ctx->emitNotEmptyMutex );
                  isEmpty= false;
               }

               gettimeofday(&now, 0);
               timeout.tv_nsec= now.tv_usec * 1000 + (timelimit % 1000) * 1000000;
               timeout.tv_sec= now.tv_sec + (timelimit / 1000);
               while (timeout.tv_nsec > 1000000000)
               {
                  timeout.tv_nsec -= 1000000000;
                  timeout.tv_sec++;
               }
               
               TRACE1("emitCaptureData: endpoint: wait till not full...");
               // Wait for more room
               pthread_mutex_lock( &ctx->emitMutex );
               pthread_mutex_lock( &ctx->emitNotFullMutex );
               pthread_mutex_unlock( &ctx->emitMutex );
               rc= pthread_cond_timedwait(&ctx->emitNotFullCond, &ctx->emitNotFullMutex, &timeout);
               pthread_mutex_unlock( &ctx->emitNotFullMutex );
               if ( rc == ETIMEDOUT )
               {
                  ERROR("emitCaptureData: endpoint: wait till not full timeout");
                  ctx->postThreadStopRequested= true;
                  if ( ctx->curlfd >= 0 )
                  {
                     DEBUG("emitCaptureData: shutdown curl fd");
                     shutdown( ctx->curlfd, SHUT_RDWR );
                  }
               }
               else
               {
                  TRACE1("emitCaptureData: endpoint: done wait till not full");
               }
            }
            else
            {
               break;
            }
            if ( ctx->postThreadStopRequested || ctx->postThreadAborted )
            {
               break;
            }

            pthread_mutex_lock( &ctx->emitMutex );
         }

         if ( !isEmpty )
         {
            // Signal not empty
            TRACE1("emitCaptureData: endpoint: signal not empty: count %d", ctx->emitCount);
            pthread_mutex_lock( &ctx->emitMutex );
            pthread_mutex_lock( &ctx->emitNotEmptyMutex );
            pthread_mutex_unlock( &ctx->emitMutex );
            pthread_cond_signal( &ctx->emitNotEmptyCond );
            pthread_mutex_unlock( &ctx->emitNotEmptyMutex );
         }
         TRACE1("emitCaptureData: endpoint: exit");
      }
   }
}


extern "C"
{

void* MediaCaptureCreateContext( GstElement *element )
{
   MediaCapContext *ctx= 0;
   const char *env;
   bool reportProgress= false;
   int progressInterval= 0;

   env= getenv("MEDIACAPTURE_DEBUG");
   if ( env )
   {
      int level= atoi(env);
      if ( level < 0 ) level= 0;
      if ( level > 10 ) level= 10;
      gDebugLevel= level;
      ERROR("setting log level to %d", gDebugLevel);
   }

   env= getenv("MEDIACAPTURE_REPORT_PROGRESS");
   if ( env )
   {
      progressInterval= atoi(env);
      if ( progressInterval > 0 )
      {
         reportProgress= true;
         INFO("setting progress interval to %d", progressInterval);
      }
   }

   INFO("MediaCaptureCreateContext: enter");
   ctx= (MediaCapContext*)calloc( 1, sizeof(MediaCapContext));
   if ( ctx )
   {
      pthread_mutex_init( &ctx->mutex, 0 );
      ctx->element= element;
      ctx->srcList= std::vector<SrcInfo*>();
      ctx->pmtPid= 0x40;
      ctx->nextESPid= 0x50;
      ctx->nextVideoStreamId= 0xE0;
      ctx->nextAudioStreamId= 0xD0;
      ctx->videoPid= -1;
      ctx->curlfd= -1;
      ctx->reportProgress= reportProgress;
      ctx->progressInterval= progressInterval*1000000LL;

      initCRCTable();

      pthread_mutex_lock( &gMutex );
      gContextList.push_back(ctx);
      pthread_mutex_unlock( &gMutex );

      findSources( ctx );

      #ifdef MEDIACAPTURE_USE_RTREMOTE
      createRtRemoteAPI( ctx );
      #else      
      #ifdef MEDIACAPTURE_USE_TRIGGER_FILE
      {
         FILE *pFile= fopen( "/opt/enable-capture", "rb" );
         if ( pFile )
         {
            fclose( pFile );
            remove( "/opt/enable-capture" );
         }
         else
         {
            ctx->canCapture= false;
         }
      }
      #endif
      if ( ctx->canCapture )
      {
         startCapture( ctx, "/opt/mediacapture.ts", 30000 );
      }
      #endif
   }
   INFO("MediaCaptureCreateContext: exit");

   return (void*)ctx;
}

void MediaCaptureDestroyContext( void *context )
{
   INFO("MediaCaptureDestroyContext: enter");
   MediaCapContext *ctx= (MediaCapContext*)context;
   if ( ctx )
   {
      pthread_mutex_lock( &gMutex );
      for ( std::vector<MediaCapContext*>::iterator it= gContextList.begin();
            it != gContextList.end();
            ++it )
      {
         MediaCapContext *ctxIter= (*it);
         if ( ctxIter == ctx )
         {
            gContextList.erase( it );
            break;
         }
      }
      pthread_mutex_unlock( &gMutex );

      if ( ctx->captureActive )
      {
         stopCapture( ctx );
      }
      freeSources( ctx );
      if ( ctx->patPacket )
      {
         free( ctx->patPacket );
      }
      if ( ctx->pmtPacket )
      {
         free( ctx->pmtPacket );
      }

      pthread_mutex_destroy( &ctx->mutex );

      #ifdef MEDIACAPTURE_USE_RTREMOTE
      destroyRtRemoteAPI( ctx );
      #endif

      free( ctx );
   }
   if ( ctx->totalBytesEmitted )
   {
      INFO("MediaCaptureDestroyContext: totalBytesEmitted %lld totalBytesPosted %lld", ctx->totalBytesEmitted, ctx->totalBytesPosted );
   }
   INFO("MediaCaptureDestroyContext: exit");
}

}

