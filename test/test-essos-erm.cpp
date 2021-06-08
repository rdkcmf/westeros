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
#include <limits.h>
#include <pthread.h>
#include <unistd.h>

#include "test-essos-erm.h"

#include "essos-resmgr.h"

static const char *configFile=
"policy: requester-wins-priority-tie\n \
video: hardware\n \
video: hardware,limitedResolution(1920,1080),limitedQuality\n \
video: hardware,limitedResolution(640,480),limitedQuality\n \
video: software,limitedPerformance\n \
audio: none\n \
frontend: none\n \
";

static bool initERM( EMCTX *emctx )
{
   bool result= false;
   FILE *pFileConfig= 0;
   size_t len;

   pFileConfig= fopen( "essrmgr.conf", "wt" );
   if ( !pFileConfig )
   {
      EMERROR("Unable to create ERM config file");
      goto exit;
   }

   len= fwrite( configFile, 1, strlen(configFile), pFileConfig );
   if ( len != strlen(configFile) )
   {
      EMERROR("Unable to populate ERM config file");
      goto exit;
   }

   fclose( pFileConfig );
   pFileConfig= 0;

   setenv( "ESSRMGR_CONFIG_FILE", "essrmgr.conf", 0 );

   result= EssRMgrInit();
   if ( !result )
   {
      EMERROR("EssRMgrInit failed");
      goto exit;
   }

exit:
   return result;
}

static void termERM( EMCTX *emctx )
{
   EssRMgrTerm();

   remove( "essrmgr.conf" );

   unsetenv( "ESSRMGR_CONFIG_FILE" );
}

typedef struct _TestCtx
{
   EMCTX *emctx;
   const char *name;
   int loop;
   bool async;
   int type;
   int delay;
   int priority;
   int priority2;
   int usage;
   int usage2;
   int maxWidth;
   int maxHeight;
   int assignedId;
   int grantCount;
   int revokeCount;
   int prevAssignedId;
} TestCtx;

static void notify( EssRMgr *rm, int event, int type, int id, void* userData )
{
   TestCtx *tctx= (TestCtx*)userData;
   printf("notify enter\n");
   switch( type )
   {
      case EssRMgrResType_videoDecoder:
      case EssRMgrResType_audioDecoder:
      case EssRMgrResType_frontEnd:
         switch( event )
         {
            case EssRMgrEvent_granted:
               ++tctx->grantCount;
               printf("%s granted resource type %d id %d grant count %d\n", tctx->name, type, id, tctx->grantCount );
               tctx->assignedId= id;
               break;
            case EssRMgrEvent_revoked:
               {
                  ++tctx->revokeCount;
                  tctx->prevAssignedId= tctx->assignedId;
                  tctx->assignedId= -1;
                  printf("%s revoked: releasing resource type %d id %d revoke count %d\n", tctx->name, type, id, tctx->revokeCount);
                  EssRMgrReleaseResource( rm, type, id );
                  printf("%s revoked: done releasing resource type %d id %d\n", tctx->name, type, id);
               }
               break;
            default:
               break;
         }
         break;
      default:
         break;
   }
   printf("notify exit\n");
}

static void *requestThread( void *userData )
{
   TestCtx *tctx= (TestCtx*)userData;
   EMCTX *emctx= tctx->emctx;
   EssRMgr *rm= 0;
   EssRMgrRequest req;
   bool result;
   int loopIdx;

   memset( &req, 0, sizeof(EssRMgrRequest) );

   rm= EssRMgrCreate();
   if ( !rm )
   {
      EMERROR("EssRMgrCreate failed for client (%s)", tctx->name);
      goto exit;
   }

   if ( tctx->loop == 0 ) tctx->loop= 1;
   for( loopIdx= 0; loopIdx < tctx->loop; ++loopIdx )
   {
      req.type= tctx->type;
      if ( req.type == EssRMgrResType_videoDecoder )
      {
         req.info.video.maxWidth= tctx->maxWidth;
         req.info.video.maxHeight= tctx->maxHeight;
      }
      req.assignedId= -1;
      req.requestId= -1;
      req.usage= tctx->usage;
      req.priority= tctx->priority;
      req.asyncEnable= tctx->async;
      req.notifyCB= notify;
      req.notifyUserData= tctx;

      result= EssRMgrRequestResource( rm, tctx->type, &req );
      if ( result )
      {
         if ( req.assignedId >= 0 )
         {
            printf("  %s assigned id %d caps %X\n", tctx->name, req.assignedId, req.assignedCaps );
         }
         else
         {
            printf("  %s async grant pending, request id %d\n", tctx->name, req.requestId );
         }
         if ( tctx->priority2 != 0 )
         {
            usleep( tctx->delay/2 );
            if ( req.requestId >= 0 )
            {
               printf("%s changing priority from %d to %d\n", tctx->name, tctx->priority, tctx->priority2 );
               EssRMgrRequestSetPriority( rm, tctx->type, req.requestId, tctx->priority2 );
            }
            usleep( tctx->delay/2 );
         }
         else if ( tctx->usage2 != 0 )
         {
            usleep( tctx->delay/2 );
            if ( req.requestId >= 0 )
            {
               EssRMgrUsage usageNew;
               usageNew.usage= tctx->usage2;
               usageNew.info= req.info;
               printf("%s changing usage from %d to %d\n", tctx->name, tctx->usage, tctx->usage2 );
               EssRMgrRequestSetUsage( rm, tctx->type, req.requestId, &usageNew );
            }
            usleep( tctx->delay/2 );
         }
         else
         {
            usleep( tctx->delay );
         }
         if ( tctx->assignedId >= 0 )
         {
            printf("%s voluntarily releasing id %d\n", tctx->name, tctx->assignedId);
            tctx->prevAssignedId= tctx->assignedId;
            EssRMgrReleaseResource( rm, tctx->type, tctx->assignedId );
         }
         usleep( tctx->delay );
      }
   }

exit:

   if ( rm )
   {
      EssRMgrDestroy( rm );
   }

   return 0;
}

bool testCaseERMBasicRequestVideo( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool error= false;
   int rc;
   pthread_t threadIdA= 0;
   pthread_t threadIdB= 0;
   TestCtx ctxA;
   TestCtx ctxB;

   result= initERM( emctx );
   if ( !result )
   {
      EMERROR("initERM failed");
      goto exit;
   }

   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_videoDecoder;
   ctxA.async= true;
   ctxA.loop= 1;
   ctxA.priority= 2;
   ctxA.usage= 7;
   ctxA.delay= 60000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   usleep( 10000 );

   memset( &ctxB, 0, sizeof(ctxA) );
   ctxB.emctx= emctx;
   ctxB.name= "B";
   ctxB.type= EssRMgrResType_videoDecoder;
   ctxB.async= true;
   ctxB.loop= 1;
   ctxB.priority= 1;
   ctxB.usage= 7;
   ctxB.delay= 60000;
   ctxB.assignedId= -1;
   ctxB.prevAssignedId= -1;
   rc= pthread_create( &threadIdB, NULL, requestThread, &ctxB );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );
   pthread_join( threadIdB, NULL );

   if ( ctxA.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for A: expected 1 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 1 )
   {
      EMERROR("Unexpected revoke count for A: expected 1 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxB.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for B: expected 1 actual %d", ctxB.grantCount );
      error= true;
   }
   if ( ctxB.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for B: expected 0 actual %d", ctxB.revokeCount );
      error= true;
   }
   if ( error ) goto exit;

   testResult= true;

exit:
   termERM( emctx );

   return testResult;
}

bool testCaseERMBasicRequestAudio( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool error= false;
   int rc;
   pthread_t threadIdA= 0;
   pthread_t threadIdB= 0;
   TestCtx ctxA;
   TestCtx ctxB;

   result= initERM( emctx );
   if ( !result )
   {
      EMERROR("initERM failed");
      goto exit;
   }

   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_audioDecoder;
   ctxA.async= true;
   ctxA.loop= 1;
   ctxA.priority= 2;
   ctxA.usage= 7;
   ctxA.delay= 60000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   usleep( 10000 );

   memset( &ctxB, 0, sizeof(ctxA) );
   ctxB.emctx= emctx;
   ctxB.name= "B";
   ctxB.type= EssRMgrResType_audioDecoder;
   ctxB.async= true;
   ctxB.loop= 1;
   ctxB.priority= 1;
   ctxB.usage= 7;
   ctxB.delay= 60000;
   ctxB.assignedId= -1;
   ctxB.prevAssignedId= -1;
   rc= pthread_create( &threadIdB, NULL, requestThread, &ctxB );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );
   pthread_join( threadIdB, NULL );

   if ( ctxA.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for A: expected 1 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 1 )
   {
      EMERROR("Unexpected revoke count for A: expected 1 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxB.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for B: expected 1 actual %d", ctxB.grantCount );
      error= true;
   }
   if ( ctxB.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for B: expected 0 actual %d", ctxB.revokeCount );
      error= true;
   }
   if ( error ) goto exit;

   testResult= true;

exit:
   termERM( emctx );

   return testResult;
}

bool testCaseERMBasicRequestFrontEnd( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool error= false;
   int rc;
   pthread_t threadIdA= 0;
   pthread_t threadIdB= 0;
   TestCtx ctxA;
   TestCtx ctxB;

   result= initERM( emctx );
   if ( !result )
   {
      EMERROR("initERM failed");
      goto exit;
   }

   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_frontEnd;
   ctxA.async= true;
   ctxA.loop= 1;
   ctxA.priority= 2;
   ctxA.usage= 7;
   ctxA.delay= 60000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   usleep( 10000 );

   memset( &ctxB, 0, sizeof(ctxA) );
   ctxB.emctx= emctx;
   ctxB.name= "B";
   ctxB.type= EssRMgrResType_frontEnd;
   ctxB.async= true;
   ctxB.loop= 1;
   ctxB.priority= 1;
   ctxB.usage= 7;
   ctxB.delay= 60000;
   ctxB.assignedId= -1;
   ctxB.prevAssignedId= -1;
   rc= pthread_create( &threadIdB, NULL, requestThread, &ctxB );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );
   pthread_join( threadIdB, NULL );

   if ( ctxA.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for A: expected 1 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 1 )
   {
      EMERROR("Unexpected revoke count for A: expected 1 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxB.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for B: expected 1 actual %d", ctxB.grantCount );
      error= true;
   }
   if ( ctxB.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for B: expected 0 actual %d", ctxB.revokeCount );
      error= true;
   }
   if ( error ) goto exit;

   testResult= true;

exit:
   termERM( emctx );

   return testResult;
}

bool testCaseERMBasicRequestLoop( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool error= false;
   int rc;
   pthread_t threadIdA= 0;
   pthread_t threadIdB= 0;
   TestCtx ctxA;
   TestCtx ctxB;

   result= initERM( emctx );
   if ( !result )
   {
      EMERROR("initERM failed");
      goto exit;
   }

   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_videoDecoder;
   ctxA.async= true;
   ctxA.loop= 10;
   ctxA.priority= 2;
   ctxA.usage= 7;
   ctxA.delay= 120000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   usleep( 10000 );

   memset( &ctxB, 0, sizeof(ctxA) );
   ctxB.emctx= emctx;
   ctxB.name= "B";
   ctxB.type= EssRMgrResType_videoDecoder;
   ctxB.async= true;
   ctxB.loop= 10;
   ctxB.priority= 1;
   ctxB.usage= 7;
   ctxB.delay= 120000;
   ctxB.assignedId= -1;
   ctxB.prevAssignedId= -1;
   rc= pthread_create( &threadIdB, NULL, requestThread, &ctxB );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );
   pthread_join( threadIdB, NULL );

   if ( ctxA.grantCount != 10 )
   {
      EMERROR("Unexpected grant count for A: expected 10 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 10 )
   {
      EMERROR("Unexpected revoke count for A: expected 10 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxB.grantCount != 10 )
   {
      EMERROR("Unexpected grant count for B: expected 10 actual %d", ctxB.grantCount );
      error= true;
   }
   if ( ctxB.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for B: expected 0 actual %d", ctxB.revokeCount );
      error= true;
   }
   if ( error ) goto exit;

   testResult= true;

exit:
   termERM( emctx );

   return testResult;
}

bool testCaseERMBasicRequestAsync( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool error= false;
   int rc;
   pthread_t threadIdA= 0;
   pthread_t threadIdB= 0;
   TestCtx ctxA;
   TestCtx ctxB;

   result= initERM( emctx );
   if ( !result )
   {
      EMERROR("initERM failed");
      goto exit;
   }

   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_videoDecoder;
   ctxA.async= true;
   ctxA.loop= 1;
   ctxA.priority= 1;
   ctxA.usage= 7;
   ctxA.delay= 60000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   usleep( 10000 );

   memset( &ctxB, 0, sizeof(ctxA) );
   ctxB.emctx= emctx;
   ctxB.name= "B";
   ctxB.type= EssRMgrResType_videoDecoder;
   ctxB.async= true;
   ctxB.loop= 1;
   ctxB.priority= 10;
   ctxB.usage= 7;
   ctxB.delay= 100000;
   ctxB.assignedId= -1;
   ctxB.prevAssignedId= -1;
   rc= pthread_create( &threadIdB, NULL, requestThread, &ctxB );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );
   pthread_join( threadIdB, NULL );

   if ( ctxA.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for A: expected 1 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for A: expected 0 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxB.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for B: expected 1 actual %d", ctxB.grantCount );
      error= true;
   }
   if ( ctxB.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for B: expected 0 actual %d", ctxB.revokeCount );
      error= true;
   }
   if ( error ) goto exit;

   testResult= true;

exit:
   termERM( emctx );

   return testResult;
}

bool testCaseERMVideoSizeConstraint( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool error= false;
   int rc;
   pthread_t threadIdA= 0;
   TestCtx ctxA;

   result= initERM( emctx );
   if ( !result )
   {
      EMERROR("initERM failed");
      goto exit;
   }

   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_videoDecoder;
   ctxA.async= true;
   ctxA.loop= 1;
   ctxA.priority= 10;
   ctxA.usage= 4;
   ctxA.maxWidth=320;
   ctxA.maxHeight=240;
   ctxA.delay= 10000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );

   if ( ctxA.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for A: expected 1 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for A: expected 0 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxA.prevAssignedId != 2 )
   {
      EMERROR("Unexpected prev id for A: expected 2 actual %d", ctxA.prevAssignedId );
      error= true;
   }
   if ( error ) goto exit;


   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_videoDecoder;
   ctxA.async= true;
   ctxA.loop= 1;
   ctxA.priority= 10;
   ctxA.usage= 4;
   ctxA.maxWidth=800;
   ctxA.maxHeight=600;
   ctxA.delay= 10000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );

   if ( ctxA.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for A: expected 1 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for A: expected 0 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxA.prevAssignedId != 1 )
   {
      EMERROR("Unexpected prev id for A: expected 1 actual %d", ctxA.prevAssignedId );
      error= true;
   }
   if ( error ) goto exit;


   testResult= true;

exit:
   termERM( emctx );

   return testResult;
}

bool testCaseERMRequesterIncreasePriority( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool error= false;
   int rc;
   pthread_t threadIdA= 0;
   pthread_t threadIdB= 0;
   TestCtx ctxA;
   TestCtx ctxB;

   result= initERM( emctx );
   if ( !result )
   {
      EMERROR("initERM failed");
      goto exit;
   }

   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_videoDecoder;
   ctxA.async= true;
   ctxA.loop= 1;
   ctxA.priority= 2;
   ctxA.usage= 7;
   ctxA.delay= 200000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   usleep( 10000 );

   memset( &ctxB, 0, sizeof(ctxA) );
   ctxB.emctx= emctx;
   ctxB.name= "B";
   ctxB.type= EssRMgrResType_videoDecoder;
   ctxB.async= true;
   ctxB.loop= 1;
   ctxB.priority= 3;
   ctxB.priority2= 1;
   ctxB.usage= 7;
   ctxB.delay= 100000;
   ctxB.assignedId= -1;
   ctxB.prevAssignedId= -1;
   rc= pthread_create( &threadIdB, NULL, requestThread, &ctxB );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );
   pthread_join( threadIdB, NULL );

   if ( ctxA.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for A: expected 1 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 1 )
   {
      EMERROR("Unexpected revoke count for A: expected 1 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxB.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for B: expected 1 actual %d", ctxB.grantCount );
      error= true;
   }
   if ( ctxB.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for B: expected 0 actual %d", ctxB.revokeCount );
      error= true;
   }
   if ( error ) goto exit;

   testResult= true;

exit:
   termERM( emctx );

   return testResult;
}

bool testCaseERMOwnerDecreasePriority( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool error= false;
   int rc;
   pthread_t threadIdA= 0;
   pthread_t threadIdB= 0;
   TestCtx ctxA;
   TestCtx ctxB;

   result= initERM( emctx );
   if ( !result )
   {
      EMERROR("initERM failed");
      goto exit;
   }

   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_videoDecoder;
   ctxA.async= true;
   ctxA.loop= 1;
   ctxA.priority= 1;
   ctxA.priority2= 5;
   ctxA.usage= 7;
   ctxA.delay= 100000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   usleep( 10000 );

   memset( &ctxB, 0, sizeof(ctxA) );
   ctxB.emctx= emctx;
   ctxB.name= "B";
   ctxB.type= EssRMgrResType_videoDecoder;
   ctxB.async= true;
   ctxB.loop= 1;
   ctxB.priority= 2;
   ctxB.usage= 7;
   ctxB.delay= 140000;
   ctxB.assignedId= -1;
   ctxB.prevAssignedId= -1;
   rc= pthread_create( &threadIdB, NULL, requestThread, &ctxB );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );
   pthread_join( threadIdB, NULL );

   if ( ctxA.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for A: expected 1 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 1 )
   {
      EMERROR("Unexpected revoke count for A: expected 1 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxB.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for B: expected 1 actual %d", ctxB.grantCount );
      error= true;
   }
   if ( ctxB.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for B: expected 0 actual %d", ctxB.revokeCount );
      error= true;
   }
   if ( error ) goto exit;

   testResult= true;

exit:
   termERM( emctx );

   return testResult;
}

bool testCaseERMOwnerChangeUsage( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool error= false;
   int rc;
   pthread_t threadIdA= 0;
   pthread_t threadIdB= 0;
   TestCtx ctxA;
   TestCtx ctxB;

   result= initERM( emctx );
   if ( !result )
   {
      EMERROR("initERM failed");
      goto exit;
   }

   memset( &ctxA, 0, sizeof(ctxA) );
   ctxA.emctx= emctx;
   ctxA.name= "A";
   ctxA.type= EssRMgrResType_videoDecoder;
   ctxA.async= true;
   ctxA.loop= 1;
   ctxA.priority= 1;
   ctxA.usage= 7;
   ctxA.usage2= 1;
   ctxA.delay= 100000;
   ctxA.assignedId= -1;
   ctxA.prevAssignedId= -1;
   rc= pthread_create( &threadIdA, NULL, requestThread, &ctxA );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   usleep( 10000 );

   memset( &ctxB, 0, sizeof(ctxA) );
   ctxB.emctx= emctx;
   ctxB.name= "B";
   ctxB.type= EssRMgrResType_videoDecoder;
   ctxB.async= true;
   ctxB.loop= 1;
   ctxB.priority= 2;
   ctxB.usage= 7;
   ctxB.delay= 140000;
   ctxB.assignedId= -1;
   ctxB.prevAssignedId= -1;
   rc= pthread_create( &threadIdB, NULL, requestThread, &ctxB );
   if ( rc )
   {
      EMERROR("Failed to created thread A");
      goto exit;
   }

   pthread_join( threadIdA, NULL );
   pthread_join( threadIdB, NULL );

   if ( ctxA.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for A: expected 1 actual %d", ctxA.grantCount );
      error= true;
   }
   if ( ctxA.revokeCount != 1 )
   {
      EMERROR("Unexpected revoke count for A: expected 1 actual %d", ctxA.revokeCount );
      error= true;
   }
   if ( ctxB.grantCount != 1 )
   {
      EMERROR("Unexpected grant count for B: expected 1 actual %d", ctxB.grantCount );
      error= true;
   }
   if ( ctxB.revokeCount != 0 )
   {
      EMERROR("Unexpected revoke count for B: expected 0 actual %d", ctxB.revokeCount );
      error= true;
   }
   if ( error ) goto exit;

   testResult= true;

exit:
   termERM( emctx );

   return testResult;
}


