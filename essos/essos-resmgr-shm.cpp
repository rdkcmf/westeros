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

#include <sys/mman.h>

#define ESSRMGR_NAME "essrmgr"

#define ESSRMGR_MAGIC (((('E')&0xFF) << 24)|((('S')&0xFF) << 16)|((('R')&0xFF) << 8)|(('M')&0xFF))
#define ESSRMGR_VERSION (0x010000)

#define ESSRMGR_FILE_SIZE (2*ESSRMGR_MAX_ITEMS*2048)

typedef struct _EssRMgrUserNotify
{
   EssRMgr *rm;
   sem_t *semNotify;
   sem_t *semConfirm;
   sem_t *semComplete;
   int event;
   int type;
   int priority;
   int resourceIdx;
   bool needNotification;
   bool needConfirmation;
   EssRMgrNotifyCB notifyCB;
   void *notifyUserData;
   EssRMgrRequest req;
} EssRMgrUserNotify;

typedef struct _EssRMgrHdr
{
   uint32_t magic;
   uint32_t formatVersion;
   uint32_t length;
   uint32_t version;
   uint32_t crc;
   int nextRequestId;
   bool requesterWinsPriorityTie;
} EssRMgrHdr;

typedef struct _EssRMgrResourceNotify
{
   int self;
   int next;
   int prev;
   int type;
   sem_t semNotify;
   sem_t semConfirm;
   sem_t semComplete;
   int pidUser;
   int priorityUser;
   EssRMgrUserNotify notify;
} EssRMgrResourceNotify;

typedef struct _EssRMgrResource
{
   int type;
   int capabilities;
   int criteriaMask;
   int requestIdOwner;
   int pidOwner;
   int priorityOwner;
   int usageOwner;
   int pendingNtfyIdx;
   EssRMgrUsageInfo usageInfo;
} EssRMgrResource;

typedef struct _EssRMgrResourceControl
{
   sem_t semRequest;
   EssRMgrResourceNotify revoke[ESSRMGR_MAX_ITEMS];
   EssRMgrResourceNotify pending[ESSRMGR_MAX_PENDING];
   int maxPoolItems;
   int pendingPoolIdx;
} EssRMgrResourceControl;

typedef struct _EssRMgrBase
{
   int numVideoDecoders;
   EssRMgrResource videoDecoder[ESSRMGR_MAX_ITEMS];
   int numAudioDecoders;
   EssRMgrResource audioDecoder[ESSRMGR_MAX_ITEMS];
   int numFrontEnds;
   EssRMgrResource frontEnd[ESSRMGR_MAX_ITEMS];
} EssRMgrBase;

typedef struct _EssRMgrState
{
   EssRMgrHdr hdr;
   EssRMgrBase base;
   EssRMgrResourceControl vidCtrl;
   EssRMgrResourceControl audCtrl;
   EssRMgrResourceControl feCtrl;
   char reserved[ESSRMGR_FILE_SIZE-(sizeof(hdr)+sizeof(vidCtrl)+sizeof(audCtrl)+sizeof(feCtrl)+sizeof(base))];
} EssRMgrState;

typedef struct _EssRMgr
{
   char ctrlFileName[PATH_MAX];
   int fdCtrlFile;
   int ctrlSize;
   void *ctrlMem;
   EssRMgrState *state;
} EssRMgr;


static int essRMSemWaitChecked( sem_t *sem );
static void essRMInitDefaultState( EssRMgr *rm );
static bool essRMReadConfigFile( EssRMgr *rm );
static bool essRMOpenCtrlFile( EssRMgr *rm );
static void essRMCloseCtrlFile( EssRMgr *rm );
static bool essRMLockCtrlFile( EssRMgr *rm );
static void essRMUnlockCtrlFile( EssRMgr *rm );
static bool essRMLockCtrlFileAndValidate( EssRMgr *rm );
static void essRMValidateState( EssRMgr *rm );
static bool essRMInitCtrlFile( EssRMgr *rm );
static EssRMgrResourceNotify* essRMGetPendingPoolItem( EssRMgr *rm, int type );
static void essRMPutPendingPoolItem( EssRMgr *rm, EssRMgrResourceNotify *notify );
static void essRMInsertPendingByPriority( EssRMgr *rm, int id, EssRMgrResourceNotify *item );
static void essRMRemovePending( EssRMgr *rm, int id, EssRMgrResourceNotify *item );
static bool essRMAssignResource( EssRMgr *rm, int id, EssRMgrRequest *req );
static bool essRMRevokeResource( EssRMgr *rm, int type, int id );
static bool essRMTransferResource( EssRMgr *rm, int id, EssRMgrResourceNotify *pending );
static bool essRMRequestResource( EssRMgr *rm, EssRMgrRequest *req );
static void essRMReleaseResource( EssRMgr *rm, int type, int id );
static bool essRMSetPriorityResource( EssRMgr *rm, int requestId, int type, int priority );
static bool essRMSetUsageResource( EssRMgr *rm, int requestId, int type, EssRMgrUsage *usage );
static int essRMFindSuitableResource( EssRMgr *rm, int type, int priority, EssRMgrUsage *usage, int& pendingIdx );
static void essRMCancelRequestResource( EssRMgr *rm, int requestId, int type );
static void* essRMNotifyThread( void *userData );


static bool gCrc32Ready= false;
static unsigned long gCrc32Constants[256];


static void initCRC32()
{
   unsigned int k, i, j;
   if ( gCrc32Ready ) return;
   for(i = 0; i < 256; i++)
   {
      k = 0;
      for(j = (i << 24) | 0x800000; j != 0x80000000; j <<= 1)
      {
         k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
      }
      gCrc32Constants[i] = k;
   }
   gCrc32Ready= true;
}

static unsigned long getCRC32(unsigned char *data, int size, int initial= 0xffffffff )
{
   int i;
   unsigned long int crc= initial;
   initCRC32();
   for(i= 0; i < size; i++)
   {
      crc= (crc << 8) ^ gCrc32Constants[((crc >> 24) ^ data[i])&0xFF];
   }
   return crc;
}

bool EssRMgrInit()
{
   // Nothing to do
   return true;
}

void EssRMgrTerm()
{
   // Nothing to do
}

EssRMgr* EssRMgrCreate()
{
   EssRMgr *rm= 0;
   bool error= true;
   bool createdCtrlState= false;
   int rc;

   char *env= getenv( "ESSRMGR_DEBUG" );
   if ( env )
   {
      gLogLevel= atoi( env );
   }

   rm= (EssRMgr*)calloc( 1, sizeof(EssRMgr) );
   if ( rm )
   {
      rm->fdCtrlFile= -1;

      if ( !essRMOpenCtrlFile( rm ) )
      {
         goto exit;
      }

      if ( essRMLockCtrlFile( rm ) )
      {
         struct stat fileStat;
         uint32_t crc;

         rm->ctrlSize= sizeof(EssRMgrState);

         if ( fstat( rm->fdCtrlFile, &fileStat ) != 0 )
         {
            ERROR("Error from fstat for control file: errno %d", errno);
            essRMUnlockCtrlFile( rm );
            goto exit;
         }

         if ( fileStat.st_size < rm->ctrlSize )
         {
            createdCtrlState= true;
            essRMInitCtrlFile( rm );
         }

         rm->ctrlMem= mmap( NULL, 
                            rm->ctrlSize,
                            PROT_READ|PROT_WRITE,
                            MAP_SHARED | MAP_POPULATE,
                            rm->fdCtrlFile,
                            0 //offset
                           );
         if ( rm->ctrlMem == MAP_FAILED )
         {
            ERROR("Error mapping control file: errno %d", errno);
            essRMUnlockCtrlFile( rm );
            goto exit;
         }

         rm->state= (EssRMgrState*)rm->ctrlMem;

         if ( createdCtrlState )
         {
            essRMInitDefaultState( rm );
         }

         essRMValidateState( rm );

         rc= sem_init( &rm->state->vidCtrl.semRequest, 1, 1 );
         if ( rc != 0 )
         {
            ERROR("error creating control file request semaphore: errno %d", errno);
         }

         rc= sem_init( &rm->state->audCtrl.semRequest, 1, 1 );
         if ( rc != 0 )
         {
            ERROR("error creating control file request semaphore: errno %d", errno);
         }

         rc= sem_init( &rm->state->feCtrl.semRequest, 1, 1 );
         if ( rc != 0 )
         {
            ERROR("error creating control file request semaphore: errno %d", errno);
         }

         essRMUnlockCtrlFile( rm );
      }

      error= false;
   }

exit:
   if ( error )
   {
      if ( rm )
      {
         EssRMgrDestroy( rm );
         rm= 0;
      }
   }

   return rm;
}

void EssRMgrDestroy( EssRMgr *rm )
{
   if ( rm )
   {
      if ( rm->ctrlMem )
      {
         munmap( rm->ctrlMem, rm->ctrlSize );
         rm->ctrlMem= 0;
      }
      essRMCloseCtrlFile( rm );
      free( rm );
   }
}

bool EssRMgrGetPolicyPriorityTie( EssRMgr *rm )
{
   bool policyValue= false;

   if ( rm )
   {
      policyValue= rm->state->hdr.requesterWinsPriorityTie;
   }

   return policyValue;
}

int EssRMgrResourceGetCount( EssRMgr *rm, int type )
{
   int count= 0;

   if ( rm )
   {
      if ( essRMLockCtrlFileAndValidate( rm ) )
      {
         switch( type )
         {
            case EssRMgrResType_videoDecoder:
               count= rm->state->base.numVideoDecoders;
               break;
            case EssRMgrResType_audioDecoder:
               count= rm->state->base.numAudioDecoders;
               break;
            case EssRMgrResType_frontEnd:
               count= rm->state->base.numFrontEnds;
               break;
            default:
               break;
         }
         essRMUnlockCtrlFile( rm );
      }
   }

   return count;
}

bool EssRMgrResourceGetOwner( EssRMgr *rm, int type, int id, int *pid, int *priority )
{
   bool result= false;

   if ( rm )
   {
      if ( essRMLockCtrlFileAndValidate( rm ) )
      {
         switch( type )
         {
            case EssRMgrResType_videoDecoder:
               if ( (id >= 0) && (id < rm->state->base.numVideoDecoders) )
               {
                  if ( pid )
                  {
                     *pid= rm->state->base.videoDecoder[id].pidOwner;
                  }
                  if ( priority )
                  {
                     *priority= rm->state->base.videoDecoder[id].priorityOwner;
                  }
                  result= true;
               }
               break;
            case EssRMgrResType_audioDecoder:
               if ( (id >= 0) && (id < rm->state->base.numAudioDecoders) )
               {
                  if ( pid )
                  {
                     *pid= rm->state->base.audioDecoder[id].pidOwner;
                  }
                  if ( priority )
                  {
                     *priority= rm->state->base.audioDecoder[id].priorityOwner;
                  }
                  result= true;
               }
               break;
            case EssRMgrResType_frontEnd:
               if ( (id >= 0) && (id < rm->state->base.numFrontEnds) )
               {
                  if ( pid )
                  {
                     *pid= rm->state->base.frontEnd[id].pidOwner;
                  }
                  if ( priority )
                  {
                     *priority= rm->state->base.frontEnd[id].priorityOwner;
                  }
                  result= true;
               }
               break;
            default:
               break;
         }
         essRMUnlockCtrlFile( rm );
      }
   }

   return result;
}

bool EssRMgrResourceGetCaps( EssRMgr *rm, int type, int id, EssRMgrCaps *caps )
{
   bool result= false;

   if ( rm && caps )
   {
      if ( essRMLockCtrlFileAndValidate( rm ) )
      {
         switch( type )
         {
            case EssRMgrResType_videoDecoder:
               if ( (id >= 0) && (id < rm->state->base.numVideoDecoders) )
               {
                  caps->capabilities= rm->state->base.videoDecoder[id].capabilities;
                  if ( caps->capabilities & EssRMgrVidCap_limitedResolution )
                  {
                     caps->info.video.maxWidth= rm->state->base.videoDecoder[id].usageInfo.video.maxWidth;
                     caps->info.video.maxHeight= rm->state->base.videoDecoder[id].usageInfo.video.maxHeight;
                  }
                  result= true;
               }
               break;
            case EssRMgrResType_audioDecoder:
               if ( (id >= 0) && (id < rm->state->base.numAudioDecoders) )
               {
                  caps->capabilities= rm->state->base.audioDecoder[id].capabilities;
                  result= true;
               }
               break;
            case EssRMgrResType_frontEnd:
               if ( (id >= 0) && (id < rm->state->base.numFrontEnds) )
               {
                  caps->capabilities= rm->state->base.frontEnd[id].capabilities;
                  result= true;
               }
               break;
            default:
               break;
         }
         essRMUnlockCtrlFile( rm );
      }
   }

   return result;
}

bool EssRMgrRequestResource( EssRMgr *rm, int type, EssRMgrRequest *req )
{
   bool result= false;
   bool haveSemRequest= false;
   sem_t *sem= 0;
   int rc;

   TRACE2("EssRMgrRequestResource: enter: rm %p type %d", rm, type );

   if ( rm && req )
   {
      if ( type != req->type )
      {
         ERROR("mismatched resource types in request");
         goto exit;
      }

      if ( !req->notifyCB )
      {
         ERROR("must supply notification callback with reqeust");
         goto exit;
      }

      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            sem= &rm->state->vidCtrl.semRequest;
            break;
         case EssRMgrResType_audioDecoder:
            sem= &rm->state->audCtrl.semRequest;
            break;
         case EssRMgrResType_frontEnd:
            sem= &rm->state->feCtrl.semRequest;
            break;
         default:
            ERROR("unsupported resource type: %d", type);
            goto exit;
      }
      if ( sem )
      {
         rc= essRMSemWaitChecked( sem );
         if ( rc != 0 )
         {
            ERROR("sem_wait failed for semRequest: errno %d", errno);
            goto exit;
         }
         haveSemRequest= true;
      }
      
      if ( essRMLockCtrlFileAndValidate( rm ) )
      {
         req->requestId= rm->state->hdr.nextRequestId++;

         switch( type )
         {
            case EssRMgrResType_videoDecoder:
            case EssRMgrResType_audioDecoder:
            case EssRMgrResType_frontEnd:
               result= essRMRequestResource( rm, req );
               break;
            default:
               ERROR("unsupported resource type: %d", type);
               break;
         }
         essRMUnlockCtrlFile( rm );
      }
      else
      {
         ERROR("error locking control file: errno %d", errno);
      }
   }

exit:

   if ( haveSemRequest )
   {
      sem_post( sem );
   }

   TRACE2("EssRMgrRequestResource: exit: rm %p type %d result %d", rm, type, result );

   return result;
}

void EssRMgrReleaseResource( EssRMgr *rm, int type, int id )
{
   TRACE2("EssRMReleaseResource: enter: rm %p type %d id %d", rm, type, id);

   if ( rm )
   {
      if ( essRMLockCtrlFileAndValidate( rm ) )
      {
         switch( type )
         {
            case EssRMgrResType_videoDecoder:
            case EssRMgrResType_audioDecoder:
            case EssRMgrResType_frontEnd:
               essRMReleaseResource( rm, type, id );
               break;
            default:
               ERROR("unsupported resource type: %d", type);
               break;
         }
         essRMUnlockCtrlFile( rm );
      }
   }

   TRACE2("EssRMReleaseResource: exit: rm %p type %d id %d", rm, type, id);
}

bool EssRMgrRequestSetPriority( EssRMgr *rm, int type, int requestId, int priority )
{
   bool result= false;

   if ( rm )
   {
      if ( essRMLockCtrlFileAndValidate( rm ) )
      {
         switch( type )
         {
            case EssRMgrResType_videoDecoder:
            case EssRMgrResType_audioDecoder:
            case EssRMgrResType_frontEnd:
               result= essRMSetPriorityResource( rm, requestId, type, priority );
               break;
            default:
               break;
         }
         essRMUnlockCtrlFile( rm );
      }
   }

   return result;
}

bool EssRMgrRequestSetUsage( EssRMgr *rm, int type, int requestId, EssRMgrUsage *usage )
{
   bool result= false;

   if ( rm )
   {
      if ( essRMLockCtrlFileAndValidate( rm ) )
      {
         switch( type )
         {
            case EssRMgrResType_videoDecoder:
            case EssRMgrResType_audioDecoder:
            case EssRMgrResType_frontEnd:
               result= essRMSetUsageResource( rm, requestId, type, usage );
               break;
            default:
               break;
         }
         essRMUnlockCtrlFile( rm );
      }
   }

   return result;
}


void EssRMgrRequestCancel( EssRMgr *rm, int type, int requestId )
{
   if ( rm )
   {
      if ( essRMLockCtrlFileAndValidate( rm ) )
      {
         switch( type )
         {
            case EssRMgrResType_videoDecoder:
            case EssRMgrResType_audioDecoder:
            case EssRMgrResType_frontEnd:
               essRMCancelRequestResource( rm, requestId, type );
               break;
            default:
               break;
         }
         essRMUnlockCtrlFile( rm );
      }
   }
}


void EssRMgrDumpState( EssRMgr *rm )
{
   DEBUG("reserved size: %d", sizeof(rm->state->reserved));
   if ( essRMLockCtrlFileAndValidate( rm ) )
   {
      printf("requester wins priority tie: %d\n", rm->state->hdr.requesterWinsPriorityTie);
      for( int i= 0; i < rm->state->base.numVideoDecoders; ++i )
      {
         printf("video decoder: %d caps %X owner %d priority %d\n",
                i,
                rm->state->base.videoDecoder[i].capabilities,
                rm->state->base.videoDecoder[i].pidOwner,
                rm->state->base.videoDecoder[i].priorityOwner );
      }
      for( int i= 0; i < rm->state->base.numAudioDecoders; ++i )
      {
         printf("audio decoder: %d caps %X owner %d priority %d\n",
                i,
                rm->state->base.audioDecoder[i].capabilities,
                rm->state->base.audioDecoder[i].pidOwner,
                rm->state->base.audioDecoder[i].priorityOwner );
      }
      for( int i= 0; i < rm->state->base.numFrontEnds; ++i )
      {
         printf("frontend: %d caps %X owner %d priority %d\n",
                i,
                rm->state->base.frontEnd[i].capabilities,
                rm->state->base.frontEnd[i].pidOwner,
                rm->state->base.frontEnd[i].priorityOwner );
      }
      essRMUnlockCtrlFile( rm );
   }
}

static void essRMInitDefaultState( EssRMgr *rm )
{
   memset( rm->state, 0, sizeof(EssRMgrState) );

   rm->state->hdr.magic= ESSRMGR_MAGIC;
   rm->state->hdr.formatVersion= ESSRMGR_VERSION;
   rm->state->hdr.length= sizeof(EssRMgrState);
   rm->state->hdr.version= 0;

   if ( !essRMReadConfigFile(rm) )
   {
      ERROR("Error processing config file: using default config");
      rm->state->base.numVideoDecoders= ESSRMGR_MAX_ITEMS;
      for( int i= 0; i < ESSRMGR_MAX_ITEMS; ++i )
      {
         rm->state->base.videoDecoder[i].requestIdOwner= 0;
         rm->state->base.videoDecoder[i].pidOwner= 0;
         switch ( i )
         {
            case 0:
               rm->state->base.videoDecoder[i].capabilities= EssRMgrVidCap_hardware;
               break;
            default:
               rm->state->base.videoDecoder[i].capabilities= (EssRMgrVidCap_software|EssRMgrVidCap_limitedPerformance);
               break;
         }
      }
      rm->state->base.numAudioDecoders= ESSRMGR_MAX_ITEMS;
      for( int i= 0; i < ESSRMGR_MAX_ITEMS; ++i )
      {
         rm->state->base.audioDecoder[i].requestIdOwner= 0;
         rm->state->base.audioDecoder[i].pidOwner= 0;
         rm->state->base.audioDecoder[i].capabilities= EssRMgrAudCap_none;
      }
      rm->state->base.numFrontEnds= ESSRMGR_MAX_ITEMS;
      for( int i= 0; i < ESSRMGR_MAX_ITEMS; ++i )
      {
         rm->state->base.frontEnd[i].requestIdOwner= 0;
         rm->state->base.frontEnd[i].pidOwner= 0;
         rm->state->base.frontEnd[i].capabilities= EssRMgrFECap_none;
      }
   }

   for( int i= 0; i < rm->state->base.numVideoDecoders; ++i )
   {
      int rc= sem_init( &rm->state->vidCtrl.revoke[i].semNotify, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semNotify for video decoder %d: errno %d", i, errno );
      }
      rc= sem_init( &rm->state->vidCtrl.revoke[i].semConfirm, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semConfirm for video decoder %d: errno %d", i, errno );
      }
      rc= sem_init( &rm->state->vidCtrl.revoke[i].semComplete, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semComplete for video decoder %d: errno %d", i, errno );
      }
      rm->state->base.videoDecoder[i].type= EssRMgrResType_videoDecoder;
      rm->state->base.videoDecoder[i].criteriaMask= ESSRMGR_CRITERIA_MASK_VIDEO;
      rm->state->base.videoDecoder[i].pendingNtfyIdx= -1;
   }

   int maxPending= 3*rm->state->base.numVideoDecoders;
   DEBUG("vid pendingPool: max pending %d", maxPending);
   for( int i= 0; i < maxPending; ++i )
   {
      EssRMgrResourceNotify *pending= &rm->state->vidCtrl.pending[i];
      int rc= sem_init( &pending->semNotify, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semNotify for vid pending pool entry %d: errno %d", i, errno );
      }
      rc= sem_init( &pending->semConfirm, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semConfirm for vid pending pool entry %d: errno %d", i, errno );
      }
      rc= sem_init( &pending->semComplete, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semComplete for vid pending pool entry %d: errno %d", i, errno );
      }
      pending->type= EssRMgrResType_videoDecoder;
      pending->self= i;
      pending->next= ((i+1 < maxPending) ? i+1 : -1);
      pending->prev= ((i > 0) ? i-1 : -1);
      DEBUG("vid pendingPool: item %d next %d prev %d", i, pending->next, pending->prev);
   }
   rm->state->vidCtrl.pendingPoolIdx= 0;
   rm->state->vidCtrl.maxPoolItems= maxPending;

   for( int i= 0; i < rm->state->base.numAudioDecoders; ++i )
   {
      int rc= sem_init( &rm->state->audCtrl.revoke[i].semNotify, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semNotify for audio decoder %d: errno %d", i, errno );
      }
      rc= sem_init( &rm->state->audCtrl.revoke[i].semConfirm, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semConfirm for audio decoder %d: errno %d", i, errno );
      }
      rc= sem_init( &rm->state->audCtrl.revoke[i].semComplete, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semComplete for audio decoder %d: errno %d", i, errno );
      }
      rm->state->base.audioDecoder[i].type= EssRMgrResType_audioDecoder;
      rm->state->base.audioDecoder[i].criteriaMask= ESSRMGR_CRITERIA_MASK_AUDIO;
      rm->state->base.audioDecoder[i].pendingNtfyIdx= -1;
   }

   maxPending= 3*rm->state->base.numAudioDecoders;
   DEBUG("aud pendingPool: max pending %d", maxPending);
   for( int i= 0; i < maxPending; ++i )
   {
      EssRMgrResourceNotify *pending= &rm->state->audCtrl.pending[i];
      int rc= sem_init( &pending->semNotify, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semNotify for aud pending pool entry %d: errno %d", i, errno );
      }
      rc= sem_init( &pending->semConfirm, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semConfirm for aud pending pool entry %d: errno %d", i, errno );
      }
      rc= sem_init( &pending->semComplete, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semComplete for aud pending pool entry %d: errno %d", i, errno );
      }
      pending->type= EssRMgrResType_audioDecoder;
      pending->self= i;
      pending->next= ((i+1 < maxPending) ? i+1 : -1);
      pending->prev= ((i > 0) ? i-1 : -1);
      DEBUG("aud pendingPool: item %d next %d prev %d", i, pending->next, pending->prev);
   }
   rm->state->audCtrl.pendingPoolIdx= 0;
   rm->state->audCtrl.maxPoolItems= maxPending;

   for( int i= 0; i < rm->state->base.numFrontEnds; ++i )
   {
      int rc= sem_init( &rm->state->feCtrl.revoke[i].semNotify, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semNotify for front end %d: errno %d", i, errno );
      }
      rc= sem_init( &rm->state->feCtrl.revoke[i].semConfirm, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semConfirm for front end %d: errno %d", i, errno );
      }
      rc= sem_init( &rm->state->feCtrl.revoke[i].semComplete, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semComplete for front end %d: errno %d", i, errno );
      }
      rm->state->base.frontEnd[i].type= EssRMgrResType_frontEnd;
      rm->state->base.frontEnd[i].criteriaMask= ESSRMGR_CRITERIA_MASK_FE;
      rm->state->base.frontEnd[i].pendingNtfyIdx= -1;
   }

   maxPending= 3*rm->state->base.numFrontEnds;
   DEBUG("fe pendingPool: max pending %d", maxPending);
   for( int i= 0; i < maxPending; ++i )
   {
      EssRMgrResourceNotify *pending= &rm->state->feCtrl.pending[i];
      int rc= sem_init( &pending->semNotify, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semNotify for fe pending pool entry %d: errno %d", i, errno );
      }
      rc= sem_init( &pending->semConfirm, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semConfirm for fe pending pool entry %d: errno %d", i, errno );
      }
      rc= sem_init( &pending->semComplete, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semComplete for fe pending pool entry %d: errno %d", i, errno );
      }
      pending->type= EssRMgrResType_frontEnd;
      pending->self= i;
      pending->next= ((i+1 < maxPending) ? i+1 : -1);
      pending->prev= ((i > 0) ? i-1 : -1);
      DEBUG("fe pendingPool: item %d next %d prev %d", i, pending->next, pending->prev);
   }
   rm->state->feCtrl.pendingPoolIdx= 0;
   rm->state->feCtrl.maxPoolItems= maxPending;

   rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
}

#define ESSRMGR_MAX_NAMELEN (255)
static bool essRMReadConfigFile( EssRMgr *rm )
{
   bool result= false;
   const char *configFileName;
   struct stat fileStat;
   FILE *pFile= 0;
   char work[ESSRMGR_MAX_NAMELEN+1];
   int c, i, capabilities;
   bool haveIdentifier, truncation;
   int videoIndex= 0;
   int audioIndex= 0;
   int feIndex= 0;
   int maxWidth, maxHeight;

   configFileName= getenv("ESSRMGR_CONFIG_FILE");
   if ( !configFileName || (stat( configFileName, &fileStat ) != 0) )
   {
      configFileName= ESSRMGR_DEFAULT_CONFIG_FILE;
   }

   pFile= fopen( configFileName, "rt" );
   if ( !pFile )
   {
      ERROR("Error opening config file (%s)", configFileName );
      goto exit;
   }

   i= 0;
   haveIdentifier= false;
   truncation= false;
   for( ; ; )
   {
      c= fgetc(pFile);
      if ( c == EOF )
      {
         break;
      }
      else if ( (c == ' ') || (c == '\t') || (c == ':') || (c == '\n') )
      {
         if ( i > 0 )
         {
            work[i]= '\0';
            haveIdentifier= true;
         }
      }
      else
      {
         if ( i < ESSRMGR_MAX_NAMELEN )
         {
            work[i++]= c;
         }
         else
         {
            truncation= true;
         }
      }
      if ( haveIdentifier )
      {
         if ( (i == 6) && !strncmp( work, "policy", i) )
         {
            i= 0;
            haveIdentifier= false;
            for( ; ; )
            {
               c= fgetc(pFile);
               if ( (c == ' ') || (c == '\t') || (c == ',') || (c == '(') || (c == '\n') || (c == EOF) )
               {
                  if ( i > 0 )
                  {
                     work[i]= '\0';
                     haveIdentifier= true;
                  }
               }
               else
               {
                  if ( i < ESSRMGR_MAX_NAMELEN )
                  {
                     work[i++]= c;
                  }
                  else
                  {
                     truncation= true;
                  }
               }
               if ( haveIdentifier )
               {
                  if ( (i == 27) && !strncmp( work, "requester-wins-priority-tie", i ) )
                  {
                     INFO("policy: requester-wins-priority-tie");
                     rm->state->hdr.requesterWinsPriorityTie= true;
                  }
               }
               if ( (c == '\n') || (c == EOF) )
               {
                  break;
               }
            }
         }
         else
         if ( (i == 5) && !strncmp( work, "video", i ) )
         {
            i= 0;
            haveIdentifier= false;
            capabilities= 0;
            maxWidth= -1;
            maxHeight= -1;
            for( ; ; )
            {
               c= fgetc(pFile);
               if ( (c == ' ') || (c == '\t') || (c == ',') || (c == '(') || (c == '\n') || (c == EOF) )
               {
                  if ( i > 0 )
                  {
                     work[i]= '\0';
                     haveIdentifier= true;
                  }
               }
               else
               {
                  if ( i < ESSRMGR_MAX_NAMELEN )
                  {
                     work[i++]= c;
                  }
                  else
                  {
                     truncation= true;
                  }
               }
               if ( haveIdentifier )
               {
                  if ( (i == 4) && !strncmp( work, "none", i ) )
                  {
                     capabilities |= EssRMgrVidCap_none;
                  }
                  else if ( (i == 17) && !strncmp( work, "limitedResolution", i ) )
                  {
                     bool haveValue;
                     capabilities |= EssRMgrVidCap_limitedResolution;
                     if ( c == '(' )
                     {
                        i= 0;
                        haveValue= false;
                        for( ; ; )
                        {
                           c= fgetc( pFile );
                           if ( (c == ',') || (c == ' ') || (c == ')') || (c == '\t') || (c == '\n') || (c == EOF) )
                           {
                              if ( i > 0 )
                              {
                                 work[i]= '\0';
                                 haveValue= true;
                              }
                           }
                           else
                           {
                              if ( i < ESSRMGR_MAX_NAMELEN )
                              {
                                 work[i++]= c;
                              }
                              else
                              {
                                 truncation= true;
                              }
                           }
                           if ( haveValue )
                           {
                              if ( maxWidth == -1 )
                              {
                                 maxWidth= atoi( work );
                                 if ( maxWidth < 0 ) maxWidth= 0;
                              }
                              else
                              {
                                 maxHeight= atoi( work );
                                 if ( maxHeight < 0 ) maxHeight= 0;
                              }
                              i= 0;
                              haveValue= false;
                              if ( (maxWidth != -1) && (maxHeight != -1) )
                              {
                                 if ( (maxWidth == 0) || (maxHeight == 0 ) )
                                 {
                                    ERROR("bad resolution limit: using default");
                                    maxWidth= -1;
                                    maxHeight= -1;
                                 }
                                 if ( c != ')' )
                                 {
                                    ERROR("syntax error with resolution limit");
                                 }
                                 break;
                              }
                           }
                        }
                     }
                  }
                  else if ( (i == 14) && !strncmp( work, "limitedQuality", i ) )
                  {
                     capabilities |= EssRMgrVidCap_limitedQuality;
                  }
                  else if ( (i == 18) && !strncmp( work, "limitedPerformance", i ) )
                  {
                     capabilities |= EssRMgrVidCap_limitedPerformance;
                  }
                  else if ( (i == 8) && !strncmp( work, "hardware", i ) )
                  {
                     capabilities |= EssRMgrVidCap_hardware;
                  }
                  else if ( (i == 8) && !strncmp( work, "software", i ) )
                  {
                     capabilities |= EssRMgrVidCap_software;
                  }
                  else
                  {
                     WARNING("unknown attribute: (%s)", work );
                  }
                  i= 0;
                  haveIdentifier= false;
               }
               if ( (c == '\n') || (c == EOF) )
               {
                  INFO("adding video %d caps %lX", videoIndex, capabilities);
                  rm->state->base.videoDecoder[videoIndex].capabilities= capabilities;
                  if ( capabilities & EssRMgrVidCap_limitedResolution )
                  {
                     if ( maxWidth == -1 ) maxWidth= 640;
                     if ( maxHeight == -1 ) maxHeight= 480;
                     rm->state->base.videoDecoder[videoIndex].usageInfo.video.maxWidth= maxWidth;
                     rm->state->base.videoDecoder[videoIndex].usageInfo.video.maxHeight= maxHeight;
                     INFO("  max res: %dx%d", maxWidth, maxHeight);
                     maxWidth= -1;
                     maxHeight= -1;
                  }
                  ++videoIndex;
                  break;
               }
            }
         }
         else
         if ( (i == 5) && !strncmp( work, "audio", i ) )
         {
            i= 0;
            haveIdentifier= false;
            capabilities= 0;
            for( ; ; )
            {
               c= fgetc(pFile);
               if ( (c == ' ') || (c == '\t') || (c == ',') || (c == '(') || (c == '\n') || (c == EOF) )
               {
                  if ( i > 0 )
                  {
                     work[i]= '\0';
                     haveIdentifier= true;
                  }
               }
               else
               {
                  if ( i < ESSRMGR_MAX_NAMELEN )
                  {
                     work[i++]= c;
                  }
                  else
                  {
                     truncation= true;
                  }
               }
               if ( haveIdentifier )
               {
                  if ( (i == 4) && !strncmp( work, "none", i ) )
                  {
                     capabilities |= EssRMgrAudCap_none;
                  }
                  else
                  {
                     WARNING("unknown attribute: (%s)", work );
                  }
                  i= 0;
                  haveIdentifier= false;
               }
               if ( (c == '\n') || (c == EOF) )
               {
                  INFO("adding audio %d caps %lX", audioIndex, capabilities);
                  rm->state->base.audioDecoder[audioIndex].capabilities= capabilities;
                  ++audioIndex;
                  break;
               }
            }
         }
         else
         if ( (i == 8) && !strncmp( work, "frontend", i ) )
         {
            i= 0;
            haveIdentifier= false;
            capabilities= 0;
            for( ; ; )
            {
               c= fgetc(pFile);
               if ( (c == ' ') || (c == '\t') || (c == ',') || (c == '(') || (c == '\n') || (c == EOF) )
               {
                  if ( i > 0 )
                  {
                     work[i]= '\0';
                     haveIdentifier= true;
                  }
               }
               else
               {
                  if ( i < ESSRMGR_MAX_NAMELEN )
                  {
                     work[i++]= c;
                  }
                  else
                  {
                     truncation= true;
                  }
               }
               if ( haveIdentifier )
               {
                  if ( (i == 4) && !strncmp( work, "none", i ) )
                  {
                     capabilities |= EssRMgrAudCap_none;
                  }
                  else
                  {
                     WARNING("unknown attribute: (%s)", work );
                  }
                  i= 0;
                  haveIdentifier= false;
               }
               if ( (c == '\n') || (c == EOF) )
               {
                  INFO("adding frontend %d caps %lX", feIndex, capabilities);
                  rm->state->base.frontEnd[feIndex].capabilities= capabilities;
                  ++feIndex;
                  break;
               }
            }
         }
         i= 0;
         haveIdentifier= false;
         if ( truncation )
         {
            WARNING("identifier(s) exceeded max len (%d) and were truncated", ESSRMGR_MAX_NAMELEN);
            truncation= false;
         }
      }
   }

   rm->state->base.numVideoDecoders= videoIndex;
   rm->state->base.numAudioDecoders= audioIndex;
   rm->state->base.numFrontEnds= feIndex;

   INFO("config file defines %d video decoders %d audio decoders %d frontends",
        rm->state->base.numVideoDecoders,
        rm->state->base.numAudioDecoders,
        rm->state->base.numFrontEnds);

   result= true;

exit:
   return result;
}

static int essRMSemWaitChecked( sem_t *sem )
{
   int rc;
   while ( true )
   {
      rc= sem_wait(sem);
      if ( (rc == 0) || (errno != EINTR) ) break;
   }
   return rc;
}

static bool essRMOpenCtrlFile( EssRMgr *rm )
{
   bool result= false;
   const char *path;
   int rc, len;

   path= getenv("XDG_RUNTIME_DIR");
   if ( !path )
   {
      ERROR("XDG_RUNTIME_DIR is not set");
      goto exit;
   }

   len= snprintf( rm->ctrlFileName, PATH_MAX, "%s/%s", path, ESSRMGR_NAME ) + 1;
   if ( len < 0 )
   {
      ERROR("error building control file name");
      goto exit;
   }

   if ( len > PATH_MAX )
   {
      ERROR("control file name length exceeds max length %d", PATH_MAX );
      goto exit;
   }

   rm->fdCtrlFile= open( rm->ctrlFileName,
                         (O_CREAT|O_CLOEXEC|O_RDWR),
                         (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) );
   if ( rm->fdCtrlFile < 0 )
   {
      ERROR("error creating control file (%s) errno %d", rm->ctrlFileName, errno);
      goto exit;
   }

   result= true;

exit:

   return result;
}

static void essRMCloseCtrlFile( EssRMgr *rm )
{
   if ( rm && (rm->fdCtrlFile >= 0) )
   {
      close( rm->fdCtrlFile );
      rm->fdCtrlFile= -1;
   }
}

static bool essRMLockCtrlFile( EssRMgr *rm )
{
   bool result= false;
   int rc;

   if ( rm && (rm->fdCtrlFile >= 0) )
   {
      rc= flock( rm->fdCtrlFile, LOCK_EX );
      if ( rc < 0 )
      {
         ERROR("error obtaining control file lock: errno %d", errno);
      }

      result= true;
   }

   return result;
}

static void essRMUnlockCtrlFile( EssRMgr *rm )
{
   int rc;

   if ( rm && (rm->fdCtrlFile >= 0) )
   {
      rc= flock( rm->fdCtrlFile, LOCK_UN );
      if ( rc < 0 )
      {
         ERROR("error releasing control file lock: errno %d", errno);
      }
   }
}

static bool essRMLockCtrlFileAndValidate( EssRMgr *rm )
{
   bool result= false;
   result= essRMLockCtrlFile( rm );
   if ( result )
   {
      essRMValidateState( rm );
   }
   return result;
}

static void essRMValidateState( EssRMgr *rm )
{
   EssRMgrState *state;
   EssRMgrResourceNotify *iter;
   bool error= false;
   bool updateCRC= false;
   uint32_t crc;

   state= (EssRMgrState*)rm->ctrlMem;

   if ( state->hdr.magic != ESSRMGR_MAGIC )
   {
      ERROR("Bad magic in control file - resetting");
      error= true;
      goto exit;
   }

   crc= getCRC32( (unsigned char *)&state->base, sizeof(EssRMgrBase) );
   if ( state->hdr.crc != crc )
   {
      ERROR("Bad CRC in control file - resetting");
      error= true;
      goto exit;
   }

   TRACE1("vid pendingPool: max pending %d", state->vidCtrl.maxPoolItems);
   for ( int i= 0; i < state->vidCtrl.maxPoolItems; ++i )
   {
      bool itemGood, nextGood, prevGood;

      itemGood= nextGood= prevGood= false;

      iter= &state->vidCtrl.pending[i];
      TRACE3("vid pendingPool: item %d next %d prev %d", iter->self, iter->next, iter->prev);
      if ( iter->self == i ) itemGood= true;
      for( int j= 0; j < state->vidCtrl.maxPoolItems; ++j )
      {
         if ( (iter->next == -1) || ((iter->next >= 0) && (iter->next < state->vidCtrl.maxPoolItems)) ) nextGood= true;
         if ( (iter->prev == -1) || ((iter->prev >= 0) && (iter->prev < state->vidCtrl.maxPoolItems)) ) prevGood= true;
      }
      if ( !itemGood || !nextGood || !prevGood )
      {
         ERROR("Bad pool item: self %d (%d) next %d (%d) prev %d (%d)", iter->self, itemGood, iter->next, nextGood, iter->prev, prevGood);
      }
   }

   for( int i= 0; i < state->base.numVideoDecoders; ++i )
   {
      if ( state->base.videoDecoder[i].pidOwner != 0 )
      {
         int rc= kill( state->base.videoDecoder[i].pidOwner, 0 );
         if ( rc != 0 )
         {
            DEBUG("removing dead owner pid %d vid decoder %d", state->base.videoDecoder[i].pidOwner, i );
            state->base.videoDecoder[i].requestIdOwner= -1;
            state->base.videoDecoder[i].pidOwner= 0;
            state->base.videoDecoder[i].priorityOwner= 0;
            state->base.videoDecoder[i].usageOwner= 0;
            updateCRC= true;
         }
      }
   }

   for( int i= 0; i < state->base.numAudioDecoders; ++i )
   {
      if ( state->base.audioDecoder[i].pidOwner != 0 )
      {
         int rc= kill( state->base.audioDecoder[i].pidOwner, 0 );
         if ( rc != 0 )
         {
            DEBUG("removing dead owner pid %d aud decoder %d", state->base.audioDecoder[i].pidOwner, i );
            state->base.audioDecoder[i].requestIdOwner= -1;
            state->base.audioDecoder[i].pidOwner= 0;
            state->base.audioDecoder[i].priorityOwner= 0;
            state->base.audioDecoder[i].usageOwner= 0;
            updateCRC= true;
         }
      }
   }

   if ( updateCRC )
   {
      state->hdr.crc= getCRC32( (unsigned char *)&state->base, sizeof(EssRMgrBase) );
   }

exit:
   if ( error )
   {
      essRMInitDefaultState( rm );
   }
}

static bool essRMInitCtrlFile( EssRMgr *rm )
{
   bool result= false;
   int rc;
   EssRMgrState state;

   INFO("Initializing control file");

   memset( &state, 0, sizeof(EssRMgrState) );

   rc= write( rm->fdCtrlFile, &state, sizeof(EssRMgrState) );
   if ( rc < 0 )
   {
      ERROR("Error writing control file: errno %d", errno);
      goto exit;
   }

   result= true;

exit:
   return result;
}

static EssRMgrResourceNotify* essRMGetPendingPoolItem( EssRMgr *rm, int type )
{
   EssRMgrResourceNotify *notify= 0;
   EssRMgrResourceControl *ctrl= 0;
   switch( type )
   {
      case EssRMgrResType_videoDecoder:
         ctrl= &rm->state->vidCtrl;
         break;
      case EssRMgrResType_audioDecoder:
         ctrl= &rm->state->audCtrl;
         break;
      case EssRMgrResType_frontEnd:
         ctrl= &rm->state->feCtrl;
         break;
      default:
         ERROR("Bad resource type: %d", type);
         break;
   }
   if ( ctrl )
   {
      DEBUG("essRMGetPendingPoolItem: type %d state %p pendingPool %d", type, rm->state, ctrl->pendingPoolIdx);
      if ( ctrl->pendingPoolIdx >= 0 )
      {
         notify= &ctrl->pending[ctrl->pendingPoolIdx];
         DEBUG("essRMGetPendingPoolItem: type %d notify %d notify->next %d notify->prev %d", type, notify->self, notify->next, notify->prev );
         ctrl->pendingPoolIdx= notify->next;
         if ( notify->next >= 0 )
         {
            ctrl->pending[notify->next].prev= -1;
         }
         notify->next= -1;
      }
   }

   return notify;
}

static void essRMPutPendingPoolItem( EssRMgr *rm, EssRMgrResourceNotify *notify )
{
   EssRMgrResourceControl *ctrl= 0;
   switch( notify->type )
   {
      case EssRMgrResType_videoDecoder:
         ctrl= &rm->state->vidCtrl;
         break;
      case EssRMgrResType_audioDecoder:
         ctrl= &rm->state->audCtrl;
         break;
      case EssRMgrResType_frontEnd:
         ctrl= &rm->state->feCtrl;
         break;
      default:
         ERROR("Bad resource type: %d", notify->type);
         break;
   }
   if ( ctrl )
   {
      notify->next= ctrl->pendingPoolIdx;
      if ( notify->next >= 0 )
      {
         ctrl->pending[notify->next].prev= notify->self;
      }
      ctrl->pendingPoolIdx= notify->self;
   }
   notify->prev= -1;
}

static void essRMInsertPendingByPriority( EssRMgr *rm, int id, EssRMgrResourceNotify *item )
{
   EssRMgrResourceNotify *insertAfter= 0;
   EssRMgrResourceNotify *iter= 0;
   EssRMgrResource *res= 0;
   EssRMgrResourceControl *ctrl= 0;
   switch( item->type )
   {
      case EssRMgrResType_videoDecoder:
         res= rm->state->base.videoDecoder;
         ctrl= &rm->state->vidCtrl;
         break;
      case EssRMgrResType_audioDecoder:
         res= rm->state->base.audioDecoder;
         ctrl= &rm->state->audCtrl;
         break;
      case EssRMgrResType_frontEnd:
         res= rm->state->base.frontEnd;
         ctrl= &rm->state->feCtrl;
         break;
      default:
         ERROR("Bad resource type: %d", item->type);
         break;
   }
   if ( res && ctrl )
   {
      int *list= &res[id].pendingNtfyIdx;
      
      if ( *list >= 0 ) iter= &ctrl->pending[*list];

      while ( iter )
      {
         if ( item->priorityUser < iter->priorityUser )
         {
            break;
         }
         insertAfter= iter;
         iter= ((iter->next >= 0) ? &ctrl->pending[iter->next] : 0);
      }
      if ( insertAfter )
      {
         item->prev= insertAfter->self;
         item->next= insertAfter->next;
         insertAfter->next= item->self;
      }
      else
      {
         item->next= *list;
         item->prev= -1;
         *list= item->self;
      }
      if ( item->next >= 0 )
      {
         ctrl->pending[item->next].prev= item->self;
      }
   }
}

static void essRMRemovePending( EssRMgr *rm, int id, EssRMgrResourceNotify *item )
{
   EssRMgrResource *res= 0;
   EssRMgrResourceControl *ctrl= 0;
   switch( item->type )
   {
      case EssRMgrResType_videoDecoder:
         res= rm->state->base.videoDecoder;
         ctrl= &rm->state->vidCtrl;
         break;
      case EssRMgrResType_audioDecoder:
         res= rm->state->base.audioDecoder;
         ctrl= &rm->state->audCtrl;
         break;
      case EssRMgrResType_frontEnd:
         res= rm->state->base.frontEnd;
         ctrl= &rm->state->feCtrl;
         break;
      default:
         ERROR("Bad resource type: %d", item->type);
         break;
   }
   if ( res && ctrl )
   {
      int *list= &res[id].pendingNtfyIdx;

      if ( item->next >= 0 )
         ctrl->pending[item->next].prev= item->prev;
      if ( item->prev >= 0 )
         ctrl->pending[item->prev].next= item->next;
      else
         *list= -1;
   }
   item->next= item->prev= -1;
}

static bool essRMAssignResource( EssRMgr *rm, int id, EssRMgrRequest *req )
{
   bool result= false;
   pthread_t threadId;
   pthread_attr_t attr;
   EssRMgrResource *res= 0;
   EssRMgrResourceControl *ctrl= 0;
   const char *typeName= 0;
   int rc;
   int pid= getpid();

   switch( req->type )
   {
      case EssRMgrResType_videoDecoder:
         res= rm->state->base.videoDecoder;
         ctrl= &rm->state->vidCtrl;
         typeName= "video decoder";
         break;
      case EssRMgrResType_audioDecoder:
         res= rm->state->base.audioDecoder;
         ctrl= &rm->state->audCtrl;
         typeName= "audio decoder";
         break;
      case EssRMgrResType_frontEnd:
         res= rm->state->base.frontEnd;
         ctrl= &rm->state->feCtrl;
         typeName= "frontend";
         break;
      default:
         ERROR("Bad resource type: %d", req->type);
         break;
   }
   if ( res && typeName )
   {
      ctrl->revoke[id].notify.needNotification= true;
      ctrl->revoke[id].notify.rm= rm;
      ctrl->revoke[id].notify.notifyCB= req->notifyCB;
      ctrl->revoke[id].notify.notifyUserData= req->notifyUserData;
      ctrl->revoke[id].notify.semNotify= &ctrl->revoke[id].semNotify;
      ctrl->revoke[id].notify.semConfirm= &ctrl->revoke[id].semConfirm;
      ctrl->revoke[id].notify.semComplete= 0;
      ctrl->revoke[id].notify.event= EssRMgrEvent_revoked;
      ctrl->revoke[id].notify.type= req->type;
      ctrl->revoke[id].notify.priority= req->priority;
      ctrl->revoke[id].notify.resourceIdx= id;
      ctrl->revoke[id].notify.req= *req;
      ctrl->revoke[id].pidUser= pid;
      ctrl->revoke[id].priorityUser= req->priority;

      rc= pthread_attr_init( &attr );
      if ( rc )
      {
         ERROR("unable to init pthread attr: errno %d", errno);
      }

      rc= pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED);
      if ( rc )
      {
         ERROR("unable to set pthread attr detached: errno %d", errno);
      }

      rc= pthread_create( &threadId, &attr, essRMNotifyThread, &ctrl->revoke[id].notify );
      if ( rc == 0 )
      {
         INFO("%s %d assigned to pid %d", typeName, id, pid);
         res[id].requestIdOwner= req->requestId;
         res[id].pidOwner= pid;
         res[id].priorityOwner= req->priority;
         res[id].usageOwner= req->usage;

         result= true;
      }
      else
      {
         ERROR("error starting notification thread: errno %d", errno );
      }
   }

   return result;
}

static bool essRMRevokeResource( EssRMgr *rm, int type, int id )
{
   bool result= false;
   EssRMgrResource *res= 0;
   EssRMgrResourceControl *ctrl= 0;

   if ( rm )
   {
      int rc;

      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            res= rm->state->base.videoDecoder;
            ctrl= &rm->state->vidCtrl;
            break;
         case EssRMgrResType_audioDecoder:
            res= rm->state->base.audioDecoder;
            ctrl= &rm->state->audCtrl;
            break;
         case EssRMgrResType_frontEnd:
            res= rm->state->base.frontEnd;
            ctrl= &rm->state->feCtrl;
            break;
         default:
            ERROR("Bad resource type: %d", type);
            break;
      }
      if ( res && ctrl )
      {
         int pidPreempt= res[id].pidOwner;

         // preempt current owner
         DEBUG("preempting pid %d to revoke res type %d id %d", pidPreempt, type, id );

         ctrl->revoke[id].notify.needConfirmation= true;

         rc= sem_post( &ctrl->revoke[id].semNotify );
         if ( rc == 0 )
         {
            int retry= 300;

            essRMUnlockCtrlFile( rm );
            for( ; ; )
            {
               rc= sem_trywait( &ctrl->revoke[id].semConfirm );
               if ( rc == 0 )
               {
                  DEBUG("preemption of pid %d to revoke res type %d id %d successful", pidPreempt, type, id );
                  break;
               }
               if ( --retry == 0 )
               {
                  INFO("preemption timeout waiting for pid %d to release res type %d id %d", pidPreempt, type, id );
                  break;
               }
               usleep( 10000 );
            }
            if ( !essRMLockCtrlFileAndValidate( rm ) )
            {
               ERROR("error locking control file: errno %d", errno);
            }
         }
         else
         {
            ERROR("sem_post failed errno %d for semNotify)");
            goto exit;
         }

         result= true;
      }
   }

exit:
   return result;
}

static bool essRMTransferResource( EssRMgr *rm, int id, EssRMgrResourceNotify *pending )
{
   bool result= false;

   if ( rm )
   {
      int pid= getpid();

      pending->notify.needConfirmation= true;

      int rc= sem_post( &pending->semNotify );
      if ( rc == 0 )
      {
         int retry= 300;

         essRMUnlockCtrlFile( rm );
         for( ; ; )
         {
            rc= sem_trywait( &pending->semConfirm );
            if ( rc == 0 )
            {
               DEBUG("transfer of res type %d id %d to pid %d successful",
                      pending->type,
                      id,
                      pending->pidUser );
               break;
            }
            if ( --retry == 0 )
            {
               INFO("timeout waiting to tranfer res type %d id %d to pid %d",
                    pending->type,
                    id,
                    pending->pidUser );
               break;
            }
            usleep( 10000 );
         }
         if ( essRMLockCtrlFileAndValidate( rm ) )
         {
            result= true;
         }
         else
         {
            ERROR("error locking control file: errno %d", errno);
         }
      }
      else
      {
         ERROR("sem_post failed errno %d for semNotify)");
      }

      essRMPutPendingPoolItem( rm, pending );
   }

exit:
   return result;
}

static bool essRMRequestResource( EssRMgr *rm, EssRMgrRequest *req )
{
   bool result= false;
   bool madeAssignment= false;
   int rc;

   TRACE2("essRMgrRequestResource: enter: rm %p requestId %d", rm, req->requestId );

   if ( rm && req )
   {
      int assignIdx= -1, pendingIdx= -1;
      EssRMgrUsage usage;
      int pid= getpid();
      EssRMgrResource *res= 0;
      switch( req->type )
      {
         case EssRMgrResType_videoDecoder:
            res= rm->state->base.videoDecoder;
            break;
         case EssRMgrResType_audioDecoder:
            res= rm->state->base.audioDecoder;
            break;
         case EssRMgrResType_frontEnd:
            res= rm->state->base.frontEnd;
            break;
         default:
            ERROR("Bad resource type: %d", req->type);
            break;
      }
      
      if ( res )
      {      
         usage.usage= req->usage;
         usage.info= req->info;
         assignIdx= essRMFindSuitableResource( rm, req->type, req->priority, &usage, pendingIdx );

         if ( assignIdx >= 0 )
         {
            pthread_t threadId;

            if ( res[assignIdx].pidOwner != 0 )
            {
               if ( !essRMRevokeResource( rm, req->type, assignIdx ) )
               {
                  ERROR("failed to revoke res type %d id %d", req->type, assignIdx);
                  goto exit;
               }
            }

            if ( essRMAssignResource( rm, assignIdx, req ) )
            {
               req->assignedId= assignIdx;
               req->assignedCaps= res[assignIdx].capabilities;
               if ( (req->type == EssRMgrResType_videoDecoder) && (req->assignedCaps & EssRMgrVidCap_limitedResolution) )
               {
                  req->info.video.maxWidth= res[assignIdx].usageInfo.video.maxWidth;
                  req->info.video.maxHeight= res[assignIdx].usageInfo.video.maxHeight;
               }
               result= true;
            }

            rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
         }
         else if ( (pendingIdx >= 0 ) && req->asyncEnable )
         {
            EssRMgrResourceNotify *pending= essRMGetPendingPoolItem( rm, req->type );
            if ( pending )
            {
               pthread_t threadId;
               pthread_attr_t attr;

               DEBUG("request %d entering pending state for res type %d id %d pid %d", req->requestId, req->type, pendingIdx, pid );
               pending->notify.needNotification= true;
               pending->notify.rm= rm;
               pending->notify.notifyCB= req->notifyCB;
               pending->notify.notifyUserData= req->notifyUserData;
               pending->notify.semNotify= &pending->semNotify;
               pending->notify.semConfirm= &pending->semConfirm;
               pending->notify.semComplete= 0;
               pending->notify.event= EssRMgrEvent_granted;
               pending->notify.type= req->type;
               pending->notify.priority= req->priority;
               pending->notify.resourceIdx= pendingIdx;
               pending->notify.req= *req;
               pending->pidUser= pid;
               pending->priorityUser= req->priority;

               rc= pthread_attr_init( &attr );
               if ( rc )
               {
                  ERROR("unable to init pthread attr: errno %d", errno);
               }

               rc= pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED);
               if ( rc )
               {
                  ERROR("unable to set pthread attr detached: errno %d", errno);
               }

               rc= pthread_create( &threadId, &attr, essRMNotifyThread, &pending->notify );
               if ( rc == 0 )
               {
                  essRMInsertPendingByPriority( rm, pendingIdx, pending );

                  req->assignedId= -1;
                  
                  result= true;
               }
               else
               {
                  ERROR("error starting notification thread: errno %d", errno );
               }

               rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
            }
            else
            {
               ERROR("pending pool empty, must deny request");
            }
         }
      }
   }

exit:

   return result;
}

static void essRMReleaseResource( EssRMgr *rm, int type, int id )
{
   if ( rm )
   {
      int maxId= 0;
      EssRMgrResource *res= 0;
      EssRMgrResourceControl *ctrl= 0;
      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            maxId= rm->state->base.numVideoDecoders;
            res= rm->state->base.videoDecoder;
            ctrl= &rm->state->vidCtrl;
            break;
         case EssRMgrResType_audioDecoder:
            maxId= rm->state->base.numAudioDecoders;
            res= rm->state->base.audioDecoder;
            ctrl= &rm->state->audCtrl;
            break;
         case EssRMgrResType_frontEnd:
            maxId= rm->state->base.numFrontEnds;
            res= rm->state->base.frontEnd;
            ctrl= &rm->state->feCtrl;
            break;
         default:
            ERROR("Bad resource type: %d", type);
            break;
      }
      if ( res && ctrl )
      {
         if ( (id >= 0) && (id < maxId) )
         {
            int pid= getpid();
            if ( pid == res[id].pidOwner )
            {
               DEBUG("pid %d releasing res type %d id %d", pid, type, id);
               res[id].requestIdOwner= -1;
               res[id].pidOwner= 0;
               res[id].priorityOwner= 0;
               res[id].usageOwner= 0;

               ctrl->revoke[id].notify.notifyCB= 0;
               ctrl->revoke[id].notify.notifyUserData= 0;
               if ( ctrl->revoke[id].notify.needNotification )
               {
                  ctrl->revoke[id].notify.needNotification= false;
                  ctrl->revoke[id].notify.semComplete= &ctrl->revoke[id].semComplete;
                  sem_post( &ctrl->revoke[id].semNotify );
                  essRMSemWaitChecked( &ctrl->revoke[id].semComplete );
                  ctrl->revoke[id].notify.semComplete= 0;
               }

               if ( ctrl->revoke[id].notify.needConfirmation )
               {
                  ctrl->revoke[id].notify.needConfirmation= false;
                  sem_post( &ctrl->revoke[id].semConfirm );
               }

               if ( res[id].pendingNtfyIdx >= 0 )
               {
                  EssRMgrResourceNotify *pending= &ctrl->pending[res[id].pendingNtfyIdx];
                  res[id].pendingNtfyIdx= pending->next;
                  if ( pending->next >= 0 )
                  {
                     ctrl->pending[pending->next].prev= -1;
                  }
                  rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );

                  essRMTransferResource( rm, id, pending );
               }

               rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
            }
            else
            {
               ERROR("pid %d attempting to release res type %d id %d owned by pid %d",
                      pid, type, id, res[id].pidOwner );
            }
         }
      }
   }
}

static bool essRMSetPriorityResource( EssRMgr *rm, int requestId, int type, int priority )
{
   bool result= false;

   if ( rm )
   {
      int maxId= 0;
      EssRMgrResource *res= 0;
      EssRMgrResourceControl *ctrl= 0;
      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            maxId= rm->state->base.numVideoDecoders;
            res= rm->state->base.videoDecoder;
            ctrl= &rm->state->vidCtrl;
            break;
         case EssRMgrResType_audioDecoder:
            maxId= rm->state->base.numAudioDecoders;
            res= rm->state->base.audioDecoder;
            ctrl= &rm->state->audCtrl;
            break;
         case EssRMgrResType_frontEnd:
            maxId= rm->state->base.numFrontEnds;
            res= rm->state->base.frontEnd;
            ctrl= &rm->state->feCtrl;
            break;
         default:
            ERROR("Bad resource type: %d", type);
            break;
      }
      if ( res && ctrl && (requestId >= 0) && (requestId < rm->state->hdr.nextRequestId) )
      {
         int id;
         bool found= false;
         int pendingNtfyIdx= -1;
         int pid= getpid();
         for( id= 0; id < maxId; ++id )
         {
            if ( (res[id].requestIdOwner == requestId) &&
                 (res[id].pidOwner == pid) )
            {
               DEBUG("found request %d as owner of res type %d id %d", requestId, type, id);
               found= true;
               res[id].priorityOwner= priority;

               pendingNtfyIdx= res[id].pendingNtfyIdx;
               if ( pendingNtfyIdx >= 0 )
               {
                  EssRMgrResourceNotify *pending= &ctrl->pending[pendingNtfyIdx];
                  if ( priority <= pending->priorityUser )
                  {
                     pendingNtfyIdx= -1;
                     result= true;
                  }
                  else
                  {
                     essRMRemovePending( rm, id, pending );
                  }
               }
            }
            else
            {
               pendingNtfyIdx= res[id].pendingNtfyIdx;
               while ( pendingNtfyIdx >= 0 )
               {
                  EssRMgrResourceNotify *pending= &ctrl->pending[pendingNtfyIdx];
                  if ( (pending->notify.req.requestId == requestId) && (pending->pidUser == pid) )
                  {
                     DEBUG("found request %d in res type %d id %d pending list: change priority from %d to %d owner priority %d",
                           requestId, type, id, pending->priorityUser, priority, res[id].priorityOwner );
                     found= true;
                     pending->priorityUser= pending->notify.req.priority= priority;
                     essRMRemovePending( rm, id, pending );
                     if (
                           (pending->priorityUser > res[id].priorityOwner) ||
                           (
                             !rm->state->hdr.requesterWinsPriorityTie &&
                             (pending->priorityUser == res[id].priorityOwner)
                           )
                        )
                     {
                        essRMInsertPendingByPriority( rm, id, pending );
                        rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
                        result= true;
                        goto exit;
                     }
                     break;
                  }
                  pendingNtfyIdx= pending->next;
               }
            }
            if ( found )
            {
               break;
            }
         }

         if ( !found )
         {
            ERROR("requestId %d not found", requestId );
         }

         rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );

         if ( pendingNtfyIdx >= 0 )
         {
            if ( essRMRevokeResource( rm, type, id ) )
            {
               EssRMgrResourceNotify *pending= &ctrl->pending[pendingNtfyIdx];

               result= essRMTransferResource( rm, id, pending );

               rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
            }
         }
      }
   }

exit:
   return result;
}

static bool essRMSetUsageResource( EssRMgr *rm, int requestId, int type, EssRMgrUsage *usage )
{
   bool result= false;

   if ( rm )
   {
      int maxId= 0;
      EssRMgrResource *res= 0;
      EssRMgrResourceControl *ctrl= 0;
      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            maxId= rm->state->base.numVideoDecoders;
            res= rm->state->base.videoDecoder;
            ctrl= &rm->state->vidCtrl;
            break;
         case EssRMgrResType_audioDecoder:
            maxId= rm->state->base.numAudioDecoders;
            res= rm->state->base.audioDecoder;
            ctrl= &rm->state->audCtrl;
            break;
         case EssRMgrResType_frontEnd:
            maxId= rm->state->base.numFrontEnds;
            res= rm->state->base.frontEnd;
            ctrl= &rm->state->feCtrl;
            break;
         default:
            ERROR("Bad resource type: %d", type);
            break;
      }
      if ( res && ctrl && (requestId >= 0) && (requestId < rm->state->hdr.nextRequestId) )
      {
         int id;
         int pendingNtfyIdx= -1;
         int pid= getpid();
         EssRMgrResourceNotify *pending= 0;

         for( id= 0; id < maxId; ++id )
         {
            if ( (res[id].requestIdOwner == requestId) &&
                 (res[id].pidOwner == pid) )
            {
               int usageOrg;
               int testResult, testResultOrg;

               usageOrg= res[id].usageOwner;

               // update owner's usage
               res[id].usageOwner= usage->usage;
               rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );

               pendingNtfyIdx= res[id].pendingNtfyIdx;

               testResult= (usage->usage & res[id].capabilities) & res[id].criteriaMask;
               if ( testResult )
               {
                  // owned item is no longer eligible for new usage
                  essRMRevokeResource( rm, type, id );

                  result= true;
                  goto exit;
               }

               testResultOrg= (~(usageOrg ^ (res[id].capabilities)) & res[id].criteriaMask);
               testResult= (~(usage->usage ^ (res[id].capabilities)) & res[id].criteriaMask);
               if (
                    (testResult != testResultOrg) &&
                    (testResult != 0) &&
                    (pendingNtfyIdx >= 0)
                  )
               {
                  EssRMgrResourceNotify *pending= &ctrl->pending[pendingNtfyIdx];
                  testResult= (~(pending->notify.req.usage ^ (res[id].capabilities)) & res[id].criteriaMask);
                  if ( testResult == 0 )
                  {
                     // owned item is now more suitable for a pending request
                     if ( pendingNtfyIdx >= 0 )
                     {
                        essRMRevokeResource( rm, type, id );
                     }
                  }
               }

               result= true;
               goto exit;
            }
            else
            {
               pendingNtfyIdx= res[id].pendingNtfyIdx;
               while ( pendingNtfyIdx >= 0 )
               {
                  pending= &ctrl->pending[pendingNtfyIdx];
                  if ( (pending->notify.req.requestId == requestId) && (pending->pidUser == pid) )
                  {
                     EssRMgrRequest req;

                     // remove pending request and then re-issue with new usage
                     essRMRemovePending( rm, id, pending );

                     req= pending->notify.req;
                     req.usage= usage->usage;
                     req.info= usage->info;

                     essRMPutPendingPoolItem( rm, pending);

                     result= essRMRequestResource( rm, &req);
                     goto exit;
                  }
                  pendingNtfyIdx= pending->next;
               }
            }
         }
      }
   }

exit:

   return result;
}

static int essRMFindSuitableResource( EssRMgr *rm, int type, int priority, EssRMgrUsage *usage, int& pendingIdx )
{
   int suitableIdx= -1;

   pendingIdx= -1;

   if ( rm )
   {
      int i, j, testResult, idx;
      std::vector<int> eligibleItems, bestItems;
      int maxId= 0;
      EssRMgrResource *res= 0;
      EssRMgrResourceControl *ctrl= 0;
      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            maxId= rm->state->base.numVideoDecoders;
            res= rm->state->base.videoDecoder;
            ctrl= &rm->state->vidCtrl;
            break;
         case EssRMgrResType_audioDecoder:
            maxId= rm->state->base.numAudioDecoders;
            res= rm->state->base.audioDecoder;
            ctrl= &rm->state->audCtrl;
            break;
         case EssRMgrResType_frontEnd:
            maxId= rm->state->base.numFrontEnds;
            res= rm->state->base.frontEnd;
            ctrl= &rm->state->feCtrl;
            break;
         default:
            ERROR("Bad resource type: %d", type);
            break;
      }
      if ( res && ctrl )
      {
         // Deterimine set of defined resources that are do not violate usage constraints
         for( i= 0; i < maxId; ++i )
         {
            testResult= (usage->usage & res[i].capabilities) & res[i].criteriaMask;
            if ( testResult == 0 )
            {
               DEBUG("res type %d id %d caps 0x%X eligible", type, i, res[i].capabilities);
               eligibleItems.push_back( i );
            }
         }
         if ( eligibleItems.size() )
         {
            int currMaxSize= -1;
            int maxSize;
            int preemptIdx= -1;
            std::vector<int> *group;

            // Check eligible items for one that meets constraints but does not provide
            // unnecessary capabilities
            for( i= 0; i < eligibleItems.size(); ++i )
            {
               idx= eligibleItems[i];
               testResult= (~(usage->usage ^ (res[idx].capabilities)) & res[idx].criteriaMask);
               if ( testResult == 0 )
               {
                  DEBUG("res type %d id %d caps 0x%X ideal", type, i, res[idx].capabilities);
                  bestItems.push_back( idx );
               }
            }

            // Look for the most suitable item
            for( i= 0; i < 2; i++ )
            {
               group= ((i == 0) ? &bestItems : &eligibleItems);
               for( j= 0; j < group->size(); ++j )
               {
                  idx= group->at(j);
                  maxSize= -1;
                  if ( (type == EssRMgrResType_videoDecoder) && (res[idx].capabilities & EssRMgrVidCap_limitedResolution) )
                  {
                     maxSize= res[idx].usageInfo.video.maxWidth*res[idx].usageInfo.video.maxHeight;
                     if ( 
                          ((usage->info.video.maxWidth > 0) &&
                           (usage->info.video.maxWidth > res[idx].usageInfo.video.maxWidth)) ||
                          ((usage->info.video.maxHeight > 0) &&
                           (usage->info.video.maxHeight > res[idx].usageInfo.video.maxHeight))
                        )
                     {
                        TRACE1("video decoder %d disqualified: size constraints: target (%dx%d) limit (%dx%d)",
                              idx,
                              usage->info.video.maxWidth, usage->info.video.maxHeight,
                              res[idx].usageInfo.video.maxWidth,
                              res[idx].usageInfo.video.maxHeight );
                        continue;
                     }
                  }
                  if ( 
                       (res[idx].pidOwner != 0) &&
                       (res[idx].priorityOwner == priority) &&
                       (res[idx].usageOwner == usage->usage)
                     )
                  {
                     if ( rm->state->hdr.requesterWinsPriorityTie )
                     {
                        TRACE1("res type %d id %d in use but possible preemption target", type, idx);
                        preemptIdx= idx;
                     }
                     else
                     {
                        TRACE1("res type %d id %d disqualified: same pri,same usage: owner pid %d pri %d usage 0x%X",
                               type,
                               idx,
                               res[idx].pidOwner,
                               priority,
                               usage->usage );
                        if ( pendingIdx < 0 )
                        {
                           pendingIdx= idx;
                        }
                     }
                     continue;
                  }
                  if ( 
                       (res[idx].pidOwner != 0) &&
                       (res[idx].priorityOwner < priority)
                     )
                  {
                     TRACE1("res type %d id %d disqualified: priorty: owner pid %d pri %d req pri %d",
                            type,
                            idx,
                            res[idx].pidOwner,
                            res[idx].priorityOwner,
                            priority );
                     if ( pendingIdx < 0 )
                     {
                        pendingIdx= idx;
                     }
                     continue;
                  }
                  if ( suitableIdx == -1 )
                  {
                     suitableIdx= idx;
                     currMaxSize= maxSize;
                  }
                  else
                  if ( 
                       (res[suitableIdx].pidOwner != 0) &&
                       (res[idx].pidOwner != 0) &&
                       (res[suitableIdx].priorityOwner > res[idx].priorityOwner)
                     )
                  {
                     TRACE1("res type %d id %d disqualified: curr candidate for preemption has lower priority: %d vs %d",
                            type,
                            suitableIdx,
                            res[suitableIdx].priorityOwner,
                            res[idx].priorityOwner );
                     continue;
                  }
                  else 
                  if (
                       (type == EssRMgrResType_videoDecoder) &&
                       (currMaxSize != -1) &&
                       (maxSize == -1) || (maxSize < currMaxSize)
                     )
                  {
                     if ( 
                          (res[suitableIdx].pidOwner > 0) ||
                          (res[idx].pidOwner == 0)
                        )
                     {
                        suitableIdx= idx;
                        currMaxSize= maxSize;
                     }
                  }
               }
               if ( suitableIdx >= 0 )
               {
                  break;
               }
            }
            if ( (suitableIdx < 0) && (preemptIdx >= 0) )
            {
               TRACE1("no suitable free - preempting res type %d id %d", type, preemptIdx);
               suitableIdx= preemptIdx;
            }
         }
      }
   }

   return suitableIdx;
}

static void essRMCancelRequestResource( EssRMgr *rm, int requestId, int type )
{
   if ( rm )
   {
      if ( (requestId >= 0) && (requestId < rm->state->hdr.nextRequestId) )
      {
         int id;
         bool found= false;
         int pendingNtfyIdx= -1;
         int pid= getpid();
         EssRMgrResource *res= 0;
         EssRMgrResourceControl *ctrl= 0;
         int maxId= 0;
         switch( type )
         {
            case EssRMgrResType_videoDecoder:
               maxId= rm->state->base.numVideoDecoders;
               res= rm->state->base.videoDecoder;
               ctrl= &rm->state->vidCtrl;
               break;
            case EssRMgrResType_audioDecoder:
               maxId= rm->state->base.numAudioDecoders;
               res= rm->state->base.audioDecoder;
               ctrl= &rm->state->audCtrl;
               break;
            case EssRMgrResType_frontEnd:
               maxId= rm->state->base.numFrontEnds;
               res= rm->state->base.frontEnd;
               ctrl= &rm->state->feCtrl;
               break;
            default:
               ERROR("Bad resource type: %d", type);
               break;
         }
         if ( res && ctrl )
         {
            for( id= 0; id < maxId; ++id )
            {
               if ( (res[id].requestIdOwner == requestId) &&
                    (res[id].pidOwner == pid) )
               {
                  DEBUG("found request %d as owner of res type %d id %d", requestId, type, id);
                  found= true;
                  essRMReleaseResource( rm, type, id );
               }
               else   
               {
                  pendingNtfyIdx= res[id].pendingNtfyIdx;
                  while ( pendingNtfyIdx >= 0 )
                  {
                     EssRMgrResourceNotify *pending= &ctrl->pending[pendingNtfyIdx];
                     if ( (pending->notify.req.requestId == requestId) && (pending->pidUser == pid) )
                     {
                        DEBUG("found request %d in res type %d id %d pending list", requestId, type, id );
                        found= true;
                        essRMRemovePending( rm, id, pending );
                        break;
                     }
                     pendingNtfyIdx= pending->next;
                  }
               }
               if ( found )
               {
                  break;
               }
            }
         }
      }
   }
}

static void* essRMNotifyThread( void *userData )
{
   int rc;
   EssRMgrUserNotify *notify= (EssRMgrUserNotify*)userData;

   DEBUG("notify thread started");
   rc= essRMSemWaitChecked( notify->semNotify );
   DEBUG("notify thread notified");
   if ( rc != 0 )
   {
      ERROR("unexpected error from sem_wait: rc %d errno %d", rc, errno);
   }

   if ( notify->needNotification )
   {
      notify->needNotification= false;
      if ( notify->notifyCB )
      {
         bool invokeCallback= false;

         switch( notify->event )
         {
            case EssRMgrEvent_granted:
               if ( essRMLockCtrlFileAndValidate( notify->rm ) )
               {
                  bool result;
                  switch( notify->type )
                  {
                     case EssRMgrResType_videoDecoder:
                     case EssRMgrResType_audioDecoder:
                     case EssRMgrResType_frontEnd:
                        result= essRMAssignResource( notify->rm, notify->resourceIdx, &notify->req );
                        break;
                     default:
                        result= false;
                        break;
                  }
                  if ( result )
                  {
                     invokeCallback= true;
                     notify->rm->state->hdr.crc= getCRC32( (unsigned char *)&notify->rm->state->base, sizeof(EssRMgrBase) );

                     if ( notify->needConfirmation )
                     {
                        notify->needConfirmation= false;
                        sem_post( notify->semConfirm );
                     }
                  }
                  essRMUnlockCtrlFile( notify->rm );
               }
               break;
            case EssRMgrEvent_revoked:
               invokeCallback= true;
               break;
            default:
               ERROR("Unknown event type: %d", notify->event);
               break;
         }

         if ( invokeCallback )
         {
            DEBUG("calling notify callback");
            notify->notifyCB( notify->rm, notify->event, notify->type, notify->resourceIdx, notify->notifyUserData );
            DEBUG("done calling notify callback");
         }
      }
   }
   if ( notify->semComplete )
   {
      DEBUG("notify thread post complete");
      sem_post( notify->semComplete );
   }
   DEBUG("notify thread exit");
   return NULL;
}

