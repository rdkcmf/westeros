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

#include "../avsync.h"
#include "aml_avsync.h"
#include "aml_avsync_log.h"

#ifdef USE_AMLOGIC_MESON_MSYNC
#define INVALID_SESSION_ID (16)
#endif

static void wstAVSyncInit( VideoFrameManager *vfm, int sessionId )
{
   const char *env= 0;

   env= getenv("WESTEROS_GL_USE_AMLOGIC_AVSYNC");
   if ( env )
   {
      int refreshRate;
      int level;
      switch( g_activeLevel )
      {
         case 0:
         default:
            level= LOG_ERROR;
            break;
         case 1:
            level= LOG_WARN;
            break;
         case 2:
            level= LOG_INFO;
            break;
         case 3:
         case 4:
         case 5:
            level= LOG_DEBUG;
            break;
            break;
         case 6:
            level= LOG_TRACE;
            break;
      }
      log_set_level( level );

      refreshRate= 60;
      if ( gCtx->modeInfo )
      {
         refreshRate= gCtx->modeInfo->vrefresh;
      }
      pts90K vsyncInterval= 90000LL/refreshRate;

      if ( !vfm->conn->videoPlane->frameRateMatchingPlane
      #ifdef AV_SYNC_SESSION_V_MONO
           && (vfm->conn->syncType != AV_SYNC_MODE_VIDEO_MONO)
      #endif
         )
      {
         INFO("force vmaster mode for non-primary video plane");
         vfm->conn->syncType= 0;
      }

      #ifdef USE_AMLOGIC_MESON_MSYNC
      INFO("msync enabled");
      if ( sessionId == INVALID_SESSION_ID )
      {
         int session= av_sync_open_session(&sessionId);
         if ( session < 0 )
         {
            ERROR("Failed to alloc avsync session");
            return;
         }
         vfm->syncSession= session;
      }

      vfm->sync= av_sync_create( sessionId, /* session_id */
                                 vfm->conn->syncType,
                                 AV_SYNC_TYPE_VIDEO,
                                 1 /* start threshold */
                               );
      if ( vfm->sync )
      {
         struct video_config config;

         memset(&config, 0, sizeof(struct video_config));
         config.delay= 2;
         av_sync_video_config( vfm->sync, &config);
      }
      else
      {
         ERROR("Failed to create vfm sync module");
      }
      #else
      vfm->sync= av_sync_create( sessionId, /* session_id */
                                 vfm->conn->syncType,
                                 1, /* start threshold */
                                 2, /* delay */
                                 vsyncInterval );
      if ( !vfm->sync )
      {
         ERROR("Failed to create vfm sync module");
      }
      #endif
      DEBUG("Created sync module: %p vsyncInterval %d", vfm->sync, vsyncInterval );
      if ( vfm->sync && (vfm->rate != 1.0) )
      {
         INFO("set av_sync speed %f", vfm->rate);
         av_sync_set_speed( vfm->sync, vfm->rate );
      }
   }
}

static void wstAVSyncTerm( VideoFrameManager *vfm )
{
   if ( vfm->sync )
   {
      av_sync_destroy( vfm->sync );
      vfm->sync= 0;
      #ifdef USE_AMLOGIC_MESON_MSYNC
      if (vfm->syncSession != -1)
      {
         av_sync_close_session( vfm->syncSession );
         vfm->syncSession = -1;
      }
      #endif
      vfm->syncInit= false;
   }
}

void *sync_free_frame( struct vframe *vf )
{
   VideoFrameManager *vfm= (VideoFrameManager*)vf->private;
   int i;
   FRAME("sync_free_frame: %p pts %d", vf, vf->pts);
   for( i= 0; i < vfm->queueSize; ++i )
   {
      VideoFrame *fCheck= &vfm->queue[i];
      if ( fCheck->vf == vf )
      {
         if ( fCheck->canExpire )
         {
            avProgLog( fCheck->frameTime*1000LL, vfm->conn->videoResourceId, "WtoD", "drop");
            fCheck->vf= 0;
            vfm->dropFrameCount += 1;
            wstOffloadFreeVideoFrameResources( fCheck );
            wstOffloadSendBufferRelease( vfm->conn, fCheck->bufferId );
            if ( i+1 < vfm->queueSize )
            {
               memmove( &vfm->queue[i], &vfm->queue[i+1], (vfm->queueSize-i-1)*sizeof(VideoFrame) );
            }
            --vfm->queueSize;
         }
         else
         {
            /* canExpire of false means this frame has
             * been forced to display via frame advance so it
             * will be freed when released by the display */
            vf= 0;
         }
         break;
      }
   }
   if ( vf )
   {
      wstOffloadFreeVf( vf );
   }
}

static void wstAVSyncSetSyncType( VideoFrameManager *vfm, int type )
{
   if ( vfm->sync )
   {
      if ( !vfm->conn->videoPlane->frameRateMatchingPlane )
      {
         INFO("force vmaster mode for non-primary video plane");
         vfm->conn->syncType= type= 0;
      }
      DEBUG("calling av_sync_change_mode new mode %d\n", type);
      int rc= av_sync_change_mode( vfm->sync, type );
      if ( rc )
      {
         ERROR("Failed to change sync mode: %d", rc);
      }
   }
}

static void wstAVSyncPush( VideoFrameManager *vfm, VideoFrame *f )
{
   if ( vfm->sync )
   {
      struct vframe *nf= (struct vframe*)malloc( sizeof(struct vframe) );
      if ( nf )
      {
         int rc;
         nf->private= vfm;
         nf->pts= ((f->frameTime / 100) * 9);
         nf->duration= 0;
         nf->free= sync_free_frame;
         FRAME("push frame %p pts %d to sync", nf, nf->pts);
         rc= av_sync_push_frame( vfm->sync, nf );
         if ( !rc )
         {
            f->vf= nf;
         }
         else
         {
            ERROR("sync push frame failed: rc %d", rc);
            free( nf );
         }
      }
      else
      {
         ERROR("Failed to allocate vframe");
      }
   }
}

static VideoFrame *wstAVSyncPop( VideoFrameManager *vfm )
{
   VideoFrame *f= 0;
   if ( vfm->sync )
   {
      struct vframe *vf;

      if ( vfm->vblankIntervalPrev != vfm->vblankInterval )
      {
         if ( vfm->vblankIntervalPrev )
         {
            pts90K vsyncInterval= (90000LL*vfm->vblankInterval+500000LL)/1000000LL;
            INFO("updating vblankInterval to %d (%lld us)", vsyncInterval, vfm->vblankInterval);
            #ifndef USE_AMLOGIC_MESON_MSYNC
            av_sync_update_vsync_interval( vfm->sync, vsyncInterval );
            #endif
         }
         vfm->vblankIntervalPrev= vfm->vblankInterval;
      }

      pthread_mutex_lock( &vfm->mutex);
      vf= av_sync_pop_frame( vfm->sync );
      if ( vf )
      {
         int i;
         for( i= 0; i < vfm->queueSize; ++i )
         {
            VideoFrame *fCheck= &vfm->queue[i];
            if ( fCheck->vf == vf )
            {
               if ( i > 0 )
               {
                  if ( i != 1 )
                  {
                     ERROR("bad sync pop: item %d", i);
                  }
                  memmove( &vfm->queue[0], &vfm->queue[1], (vfm->queueSize-1)*sizeof(VideoFrame) );
                  --vfm->queueSize;
               }
               f= &vfm->queue[0];
               break;
            }
         }
         if ( !f )
         {
            ERROR("Unable to identify sync popped frame: pts %d", vf->pts);
         }
      }
      pthread_mutex_unlock( &vfm->mutex);
   }
   return f;
}

static void wstAVSyncPause( VideoFrameManager *vfm, bool pause )
{
   if ( vfm->sync )
   {
      INFO("pause: %d speed %f", pause, vfm->rate);
      if ( !pause )
      {
         INFO("set av_sync speed %f sync mode %d", vfm->rate, vfm->conn->syncType);
         av_sync_set_speed( vfm->sync, vfm->rate );
      }
      av_sync_pause( vfm->sync, pause );
   }
}

