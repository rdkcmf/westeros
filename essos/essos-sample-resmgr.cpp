/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2019 RDK Management
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
#include <stdarg.h>
#include <memory.h>
#include <unistd.h>

#include "essos-resmgr.h"

void notify( EssRMgr *rm, int event, int type, int id, void* userData )
{
   EssRMgrRequest *req= (EssRMgrRequest*)userData;
   printf("notify enter\n");
   switch( type )
   {
      case EssRMgrResType_videoDecoder:
      case EssRMgrResType_audioDecoder:
      case EssRMgrResType_frontEnd:
         switch( event )
         {
            case EssRMgrEvent_granted:
               printf("granted resource type %d id %d\n", type, id );
               req->assignedId= id;
               break;
            case EssRMgrEvent_revoked:
               {
                  req->assignedId= -1;
                  printf("releasing resource type %d id %d\n", type, id);
                  EssRMgrReleaseResource( rm, type, id );
                  printf("done releasing resource type %d id %d\n", type, id);
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

static void showUsage()
{
   printf("usage:\n");
   printf(" essos-sample-resmgr [options]\n" );
   printf("where [options] are:\n" );
   printf("  --type <res type>\n" );
   printf("  --delay <delay in ms>\n");
   printf("  --priority <priority>\n");
   printf("  --priority2 <priority>\n");
   printf("  --async : allow async requests\n");
   printf("  --request <usage>\n");
   printf("  --usage2 <usage>\n");
   printf("  --size <width>x<height>\n");
   printf("  --donotrelease\n");
   printf("  --loop <count>\n" );
   printf("  --list : list current assignments\n");
   printf("  --lock : lock file\n");
   printf("  --get-policy-tie\n");
   printf("  --get-count <type>\n");
   printf("  --get-owner <type> <id>\n");
   printf("  --get-caps <type> <id>\n");
   printf("  -? : show usage\n" );
   printf("\n" );   
}

int main( int argc, const char **argv )
{
   int rc= 0;
   EssRMgr *rm= 0;
   int argidx;
   int resType= EssRMgrResType_videoDecoder;
   int priority= 100;
   int priority2= -1;
   int usage= 0;
   bool haveUsage2= false;
   int usage2= 0;
   EssRMgrRequest req;
   bool looping= false;
   int loopMax= 1, loopCount;
   long long delay= 10000000;
   bool asyncEnable= false;
   bool makeRequest= false;
   bool releaseRequest= true;
   bool makeLock= false;
   bool listAssignments= false;
   bool getPolicyTie= false;
   bool getCount= false;
   int getCountType= 0;
   bool getOwner= false;
   int getOwnerType= 0;
   int getOwnerId= 0;
   bool getCaps= false;
   int getCapsType= 0;
   int getCapsId= 0;

   printf("essos-sample-resmgr v1.0\n");

   memset( &req, 0, sizeof(EssRMgrRequest) );

   argidx= 1;   
   while ( argidx < argc )
   {
      if ( argv[argidx][0] == '-' )
      {
         int len= strlen( argv[argidx] );
         if ( (len == 2) && !strncmp( argv[argidx], "-?", len) )
         {
            showUsage();
            goto exit;
         }
         else if ( (len == 6) && !strncmp( argv[argidx], "--loop", len) )
         {
            ++argidx;
            if ( argidx < argc )
            {
               looping= true;
               loopMax= atoi( argv[argidx] );
            }
         }
         else if ( (len == 6) && !strncmp( argv[argidx], "--type", len) )
         {
            ++argidx;
            if ( argidx < argc )
            {
               resType= atoi( argv[argidx] );
            }
         }
         else if ( (len == 7) && !strncmp( argv[argidx], "--delay", len) )
         {
            ++argidx;
            if ( argidx < argc )
            {
               int value= atoi( argv[argidx] )*1000;
               if ( value )
               {
                  delay= value;
               }
            }
         }
         else if ( (len == 10) && !strncmp( argv[argidx], "--priority", len) )
         {
            ++argidx;
            if ( argidx < argc )
            {
               priority= atoi( argv[argidx] );
            }
         }
         else if ( (len == 11) && !strncmp( argv[argidx], "--priority2", len) )
         {
            ++argidx;
            if ( argidx < argc )
            {
               priority2= atoi( argv[argidx] );
            }
         }
         else if ( (len == 9) && !strncmp( argv[argidx], "--request", len) )
         {
            ++argidx;
            if ( argidx < argc )
            {
               if ( sscanf( argv[argidx], "%X", &usage ) == 1 )
               {
                  makeRequest= true;
               }
            }
         }
         else if ( (len == 8) && !strncmp( argv[argidx], "--usage2", len) )
         {
            int value;
            ++argidx;
            if ( argidx < argc )
            {
               if ( sscanf( argv[argidx], "%X", &usage ) == 1 )
               {
                  haveUsage2= true;
                  usage2= value;
               }
            }
         }
         else if ( (len == 6) && !strncmp( argv[argidx], "--size", len) )
         {
            ++argidx;
            if ( argidx < argc )
            {
               int w= 0, h= 0;
               if ( sscanf( argv[argidx], "%dx%d", &w, &h ) == 2 )
               {
                  req.info.video.maxWidth= w;
                  req.info.video.maxHeight= h;
               }
            }
         }
         else if ( (len == 7) && !strncmp( argv[argidx], "--async", len) )
         {
            asyncEnable= true;
         }
         else if ( (len == 14) && !strncmp( argv[argidx], "--donotrelease", len) )
         {
            releaseRequest= false;
         }
         else if ( (len == 6) && !strncmp( argv[argidx], "--lock", len) )
         {
            makeLock= true;
         }
         else if ( (len == 6) && !strncmp( argv[argidx], "--list", len) )
         {
            listAssignments= true;
         }
         else if ( (len == 16) && !strncmp( argv[argidx], "--get-policy-tie", len) )
         {
            getPolicyTie= true;
         }
         else if ( (len == 11) && !strncmp( argv[argidx], "--get-count", len) )
         {
            ++argidx;
            if ( argidx < argc )
            {
               getCount= true;
               getCountType= atoi( argv[argidx] );
            }
         }
         else if ( (len == 11) && !strncmp( argv[argidx], "--get-owner", len) )
         {
            ++argidx;
            if ( argidx+1 < argc )
            {
               getOwner= true;
               getOwnerType= atoi( argv[argidx] );
               ++argidx;
               getOwnerId= atoi( argv[argidx] );
            }
         }
         else if ( (len == 10) && !strncmp( argv[argidx], "--get-caps", len) )
         {
            ++argidx;
            if ( argidx+1 < argc )
            {
               getCaps= true;
               getCapsType= atoi( argv[argidx] );
               ++argidx;
               getCapsId= atoi( argv[argidx] );
            }
         }
         else
         {
            printf( "unknown option %s\n\n", argv[argidx] );
            exit( -1 );
         }
      }
      else
      {
         printf( "ignoring extra argument: %s\n", argv[argidx] );
      }
      
      ++argidx;
   }


   rm= EssRMgrCreate();
   printf( "created rm %p\n", rm);
   if ( !rm )
   {
      goto exit;
   }

   loopCount= 0;
   while( loopCount < loopMax )
   {
      if ( looping )
      {
         printf("---------------------------------------\n");
         printf("loop %d\n", loopCount);
      }
      ++loopCount;

      if ( makeRequest )
      {
         bool result;

         req.type= resType;
         req.assignedId= -1;
         req.requestId= -1;
         req.usage= usage;
         req.priority= priority;
         req.asyncEnable= asyncEnable;
         req.notifyCB= notify;
         req.notifyUserData= &req;

         result= EssRMgrRequestResource( rm, resType, &req );
         printf("EssRMgrRequestResource result %d\n", result);
         if ( result )
         {
            if ( req.assignedId >= 0 )
            {
               printf("  assigned id %d caps %X\n", req.assignedId, req.assignedCaps );
            }
            else
            {
               printf("  async grant pending\n" );
            }
            if ( priority2 >= 0 )
            {
               usleep( delay/2 );
               if ( req.requestId >= 0 )
               {
                  printf("changing priority from %d to %d\n", priority, priority2 );
                  EssRMgrRequestSetPriority( rm, resType, req.requestId, priority2 );
               }
               usleep( delay/2 );
            }
            else if ( haveUsage2 )
            {
               usleep( delay/2 );
               if ( req.requestId >= 0 )
               {
                  EssRMgrUsage usageNew;
                  usageNew.usage= usage2;
                  usageNew.info= req.info;
                  printf("changing usage from %x to %x\n", usage, usage2 );
                  EssRMgrRequestSetUsage( rm, resType, req.requestId, &usageNew );
               }
               usleep( delay/2 );
            }
            else
            {
               usleep( delay );
            }
            if ( releaseRequest )
            {
               if ( req.assignedId >= 0 )
               {
                  printf("EssRMgrReleaseResource\n");
                  EssRMgrReleaseResource( rm, resType, req.assignedId );
               }
               usleep( delay );
            }
         }
      }

      if ( listAssignments )
      {
         EssRMgrDumpState( rm );
      }

      if ( getPolicyTie )
      {
         bool result= EssRMgrGetPolicyPriorityTie( rm );
         printf("policy: requester-wins-priority-tie: %d\n", result);
      }

      if ( getCount )
      {
         int count= EssRMgrResourceGetCount( rm, getCountType );
         printf("count for resource type %d: %d\n", getCountType, count);
      }

      if ( getOwner )
      {
         int client= 0, priority= 0;
         bool result= EssRMgrResourceGetOwner( rm, getOwnerType, getOwnerId, &client, &priority );
         printf("owner for resource type %d id %d: result %d: client %d priority %d\n",
                 getOwnerType, getOwnerId, result, client, priority);
      }

      if ( getCaps )
      {
         EssRMgrCaps caps;
         bool result= EssRMgrResourceGetCaps( rm, getCapsType, getCapsId, &caps );
         printf("caps for resource type %d id %d: result %d: caps %X\n",
                getCapsType, getCapsId, result, caps.capabilities);
         if ( (getCapsType == EssRMgrResType_videoDecoder) && (caps.capabilities & EssRMgrVidCap_limitedResolution) )
         {
            printf("  (%dx%d)\n", caps.info.video.maxWidth, caps.info.video.maxHeight );
         }
      }
   }
 
 exit:
 
   if ( rm )
   {
      EssRMgrDestroy( rm );
   }
 
   return rc;
}

