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

      vfm->sync= av_sync_create( sessionId, /* session_id */
                                 vfm->conn->syncType,
                                 3, /* start threshold */
                                 2, /* delay */
                                 vsyncInterval );
      if ( !vfm->sync )
      {
         ERROR("Failed to create vfm sync module");
      }
      DEBUG("Created sync module: %p vsyncInterval %d", vfm->sync, vsyncInterval );
   }
}

static void wstAVSyncTerm( VideoFrameManager *vfm )
{
   if ( vfm->sync )
   {
      av_sync_destroy( vfm->sync );
      vfm->sync= 0;
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
            fCheck->vf= 0;
            vfm->dropFrameCount += 1;
            wstFreeVideoFrameResources( fCheck );
            wstVideoServerSendBufferRelease( vfm->conn, fCheck->bufferId );
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
      free( vf );
   }
}

static void wstAVSyncSetSyncType( VideoFrameManager *vfm, int type )
{
   if ( vfm->sync )
   {
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
            av_sync_update_vsync_interval( vfm->sync, vsyncInterval );
         }
         vfm->vblankIntervalPrev= vfm->vblankInterval;
      }

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
   }
   return f;
}

static void wstAVSyncPause( VideoFrameManager *vfm, bool pause )
{
   if ( vfm->sync )
   {
      av_sync_pause( vfm->sync, pause );
   }
}

