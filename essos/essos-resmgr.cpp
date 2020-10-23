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
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <memory.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include "essos-resmgr.h"

#define ESSRMGR_NAME "essrmgr"

#define INT_FATAL(FORMAT, ...)      essrm_printf(0, "Essos Fatal: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_ERROR(FORMAT, ...)      essrm_printf(0, "Essos Error: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_WARNING(FORMAT, ...)    essrm_printf(1, "Essos Warnig: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_INFO(FORMAT, ...)       essrm_printf(2, "Essos Info: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_DEBUG(FORMAT, ...)      essrm_printf(3, "Essos Debug: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE1(FORMAT, ...)     essrm_printf(4, "Essos Trace: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE2(FORMAT, ...)     essrm_printf(5, "Essos Trace: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE3(FORMAT, ...)     essrm_printf(6, "Essos Trace: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)

#define FATAL(...)                  INT_FATAL(__VA_ARGS__, "")
#define ERROR(...)                  INT_ERROR(__VA_ARGS__, "")
#define WARNING(...)                INT_WARNING(__VA_ARGS__, "")
#define INFO(...)                   INT_INFO(__VA_ARGS__, "")
#define DEBUG(...)                  INT_DEBUG(__VA_ARGS__, "")
#define TRACE1(...)                 INT_TRACE1(__VA_ARGS__, "")
#define TRACE2(...)                 INT_TRACE2(__VA_ARGS__, "")
#define TRACE3(...)                 INT_TRACE3(__VA_ARGS__, "")

#define ESSRMGR_MAGIC (((('E')&0xFF) << 24)|((('S')&0xFF) << 16)|((('R')&0xFF) << 8)|(('M')&0xFF))
#define ESSRMGR_VERSION (0x010000)

#define ESSRMGR_DEFAULT_CONFIG_FILE "/etc/essrmgr.conf"

#define ESSRMGR_CRITERIA_MASK (0x0007)

#define ESSRMGR_MAX_DECODERS (16)
#define ESSRMGR_MAX_PENDING (ESSRMGR_MAX_DECODERS*3)
#define ESSRMGR_FILE_SIZE (ESSRMGR_MAX_DECODERS*1024)

typedef struct _EssRMgrUserNotify
{
   EssRMgr *rm;
   sem_t *semNotify;
   sem_t *semConfirm;
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
} EssRMgrHdr;

typedef struct _EssRMgrVideoDecoderNotify
{
   int self;
   int next;
   int prev;
   sem_t semNotify;
   sem_t semConfirm;
   int pidUser;
   int priorityUser;
   EssRMgrUserNotify notify;
} EssRMgrVideoDecoderNotify;

typedef struct _EssRMgrVideoDecoder
{
   int capabilities;
   int maxWidth;
   int maxHeight;
   int requestIdOwner;
   int pidOwner;
   int priorityOwner;
   int usageOwner;
   int pendingNtfyIdx;
} EssRMgrVideoDecoder;

typedef struct _EssRMgrBase
{
   int numVideoDecoders;
   EssRMgrVideoDecoder videoDecoder[ESSRMGR_MAX_DECODERS];
} EssRMgrBase;

typedef struct _EssRMgrVidControl
{
   sem_t semRequest;
   EssRMgrVideoDecoderNotify revoke[ESSRMGR_MAX_DECODERS];
   EssRMgrVideoDecoderNotify pending[ESSRMGR_MAX_PENDING];
   int maxPoolItems;
   int pendingPoolIdx;
} EssRMgrVidControl;

typedef struct _EssRMgrState
{
   EssRMgrHdr hdr;
   EssRMgrBase base;
   EssRMgrVidControl vidCtrl;
   char reserved[ESSRMGR_FILE_SIZE-(sizeof(hdr)+sizeof(vidCtrl)+sizeof(base))];
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
static bool essRMOpenCtrlFile( EssRMgr *rm );
static void essRMCloseCtrlFile( EssRMgr *rm );
static bool essRMLockCtrlFile( EssRMgr *rm );
static void essRMUnlockCtrlFile( EssRMgr *rm );
static bool essRMLockCtrlFileAndValidate( EssRMgr *rm );
static void essRMValidateState( EssRMgr *rm );
static bool essRMInitCtrlFile( EssRMgr *rm );
static int essRMVidFindLowestPriorityPreemption( EssRMgr *rm, std::vector<int>& decoders, int priority );
static EssRMgrVideoDecoderNotify* essRMVidGetPendingPoolItem( EssRMgr *rm );
static void essRMVidPutPendingPoolItem( EssRMgr *rm, EssRMgrVideoDecoderNotify *notify );
static void essRMVidInsertPendingByPriority( EssRMgr *rm, int decoderIdx, EssRMgrVideoDecoderNotify *item );
static void essRMVidRemovePending( EssRMgr *rm, int decoderIdx, EssRMgrVideoDecoderNotify *item );
static bool essRMAssignVideoDecoder( EssRMgr *rm, int decoderIdx, EssRMgrRequest *req );
static bool essRMRevokeVideoDecoder( EssRMgr *rm, int decoderIdx );
static bool essRMTransferVideoDecoder( EssRMgr *rm, int decoderIdx, EssRMgrVideoDecoderNotify *pending );
static bool essRMRequestVideoDecoder( EssRMgr *rm, EssRMgrRequest *req );
static void essRMReleaseVideoDecoder( EssRMgr *rm, int id );
static bool essRMSetPriorityVideoDecoder( EssRMgr *rm, int requestId, int priority );
static bool essRMSetUsageVideoDecoder( EssRMgr *rm, int requestId, EssRMgrUsage *usage );
static int essRMFindSuitableVideoDecoder( EssRMgr *rm, int priority, EssRMgrUsage *usage, int& pendingIdx );
static void essRMCancelRequestVideoDecoder( EssRMgr *rm, int requestId );
static bool essRMReadConfigFile( EssRMgr *rm );
static void* essRMNotifyThread( void *userData );


static int gLogLevel= 2;
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
		crc= (crc << 8) ^ gCrc32Constants[(crc >> 24) ^ data[i]];
	}
	return crc;
}

static void essrm_printf( int level, const char *fmt, ... )
{
   if ( level <= gLogLevel )
   {
      va_list argptr;
      va_start( argptr, fmt );
      vprintf( fmt, argptr );
      va_end( argptr );
   }
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
                     caps->info.video.maxWidth= rm->state->base.videoDecoder[id].maxWidth;
                     caps->info.video.maxHeight= rm->state->base.videoDecoder[id].maxHeight;
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

bool EssRMgrRequestResource( EssRMgr *rm, int type, EssRMgrRequest *req )
{
   bool result= false;
   bool haveSemRequest= false;
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

      rc= essRMSemWaitChecked( &rm->state->vidCtrl.semRequest );
      if ( rc != 0 )
      {
         ERROR("sem_wait failed for semRequest: errno %d", errno);
         goto exit;
      }
      haveSemRequest= true;
      
      if ( essRMLockCtrlFileAndValidate( rm ) )
      {
         req->requestId= rm->state->hdr.nextRequestId++;

         switch( type )
         {
            case EssRMgrResType_videoDecoder:
               result= essRMRequestVideoDecoder( rm, req );
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
      sem_post( &rm->state->vidCtrl.semRequest );
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
               essRMReleaseVideoDecoder( rm, id );
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
               result= essRMSetPriorityVideoDecoder( rm, requestId, priority );
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
               result= essRMSetUsageVideoDecoder( rm, requestId, usage );
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
               essRMCancelRequestVideoDecoder( rm, requestId );
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
      for( int i= 0; i < rm->state->base.numVideoDecoders; ++i )
      {
         printf("video decoder: %d caps %X owner %d priority %d\n",
                i,
                rm->state->base.videoDecoder[i].capabilities,
                rm->state->base.videoDecoder[i].pidOwner,
                rm->state->base.videoDecoder[i].priorityOwner );
                
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
      rm->state->base.numVideoDecoders= ESSRMGR_MAX_DECODERS;
      for( int i= 0; i < ESSRMGR_MAX_DECODERS; ++i )
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
   }

   for( int i= 0; i < rm->state->base.numVideoDecoders; ++i )
   {
      int rc= sem_init( &rm->state->vidCtrl.revoke[i].semNotify, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semNotify for decoder %d: errno %d", i, errno );
      }
      rc= sem_init( &rm->state->vidCtrl.revoke[i].semConfirm, 1, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semConfirm for decoder %d: errno %d", i, errno );
      }
      rm->state->base.videoDecoder[i].pendingNtfyIdx= -1;
   }

   int maxPending= 3*rm->state->base.numVideoDecoders;
   DEBUG("vid pendingPool: max pending %d", maxPending);
   for( int i= 0; i < maxPending; ++i )
   {
      EssRMgrVideoDecoderNotify *pending= &rm->state->vidCtrl.pending[i];
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
      pending->self= i;
      pending->next= ((i+1 < maxPending) ? i+1 : -1);
      pending->prev= ((i > 0) ? i-1 : -1);
      DEBUG("vid pendingPool: item %d next %d prev %d", i, pending->next, pending->prev);
   }
   rm->state->vidCtrl.pendingPoolIdx= 0;
   rm->state->vidCtrl.maxPoolItems= maxPending;

   rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
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
   EssRMgrVideoDecoderNotify *iter;
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

static int essRMVidFindLowestPriorityPreemption( EssRMgr *rm, std::vector<int>& decoders, int priority )
{
   int i, idx, preemptIdx= -1;
   int preemptPriority= 0;

   for( i= 0; i < decoders.size(); ++i )
   {
      idx= decoders[i];
      if ( (rm->state->base.videoDecoder[idx].pidOwner != 0) &&
           (rm->state->base.videoDecoder[idx].priorityOwner > priority) &&
           (rm->state->base.videoDecoder[idx].priorityOwner > preemptPriority) )
      {
         preemptIdx= idx;
         preemptPriority= rm->state->base.videoDecoder[idx].priorityOwner;
      }
   }

   return preemptIdx;
}

static EssRMgrVideoDecoderNotify* essRMVidGetPendingPoolItem( EssRMgr *rm )
{
   EssRMgrVideoDecoderNotify *notify= 0;

   DEBUG("essRMVidGetPendingPoolItem: state %p pendingPool %d", rm->state, rm->state->vidCtrl.pendingPoolIdx);
   if ( rm->state->vidCtrl.pendingPoolIdx >= 0 )
   {
      notify= &rm->state->vidCtrl.pending[rm->state->vidCtrl.pendingPoolIdx];
      DEBUG("essRMVidGetPendingPoolItem: notify %d notify->next %d notify->prev %d", notify->self, notify->next, notify->prev );
      rm->state->vidCtrl.pendingPoolIdx= notify->next;
      if ( notify->next >= 0 )
      {
         rm->state->vidCtrl.pending[notify->next].prev= -1;
      }
      notify->next= -1;
   }

   return notify;
}

static void essRMVidPutPendingPoolItem( EssRMgr *rm, EssRMgrVideoDecoderNotify *notify )
{
   notify->next= rm->state->vidCtrl.pendingPoolIdx;
   if ( notify->next >= 0 )
   {
      rm->state->vidCtrl.pending[notify->next].prev= notify->self;
   }
   rm->state->vidCtrl.pendingPoolIdx= notify->self;
   notify->prev= -1;
}

static void essRMVidInsertPendingByPriority( EssRMgr *rm, int decoderIdx, EssRMgrVideoDecoderNotify *item )
{
   EssRMgrVideoDecoderNotify *insertAfter= 0;
   EssRMgrVideoDecoderNotify *iter= 0;
   int *list= &rm->state->base.videoDecoder[decoderIdx].pendingNtfyIdx;
   
   if ( *list >= 0 ) iter= &rm->state->vidCtrl.pending[*list];

   while ( iter )
   {
      if ( item->priorityUser < iter->priorityUser )
      {
         break;
      }
      insertAfter= iter;
      iter= ((iter->next >= 0) ? &rm->state->vidCtrl.pending[iter->next] : 0);
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
      rm->state->vidCtrl.pending[item->next].prev= item->self;
   }
}

static void essRMVidRemovePending( EssRMgr *rm, int decoderIdx, EssRMgrVideoDecoderNotify *item )
{
   int *list= &rm->state->base.videoDecoder[decoderIdx].pendingNtfyIdx;

   if ( item->next >= 0 )
      rm->state->vidCtrl.pending[item->next].prev= item->prev;
   if ( item->prev >= 0 )
      rm->state->vidCtrl.pending[item->prev].next= item->next;
   else
      *list= -1;
   item->next= item->prev= -1;
}

static bool essRMAssignVideoDecoder( EssRMgr *rm, int decoderIdx, EssRMgrRequest *req )
{
   bool result= false;
   pthread_t threadId;
   pthread_attr_t attr;
   int rc;
   int pid= getpid();

   rm->state->vidCtrl.revoke[decoderIdx].notify.needNotification= true;
   rm->state->vidCtrl.revoke[decoderIdx].notify.rm= rm;
   rm->state->vidCtrl.revoke[decoderIdx].notify.notifyCB= req->notifyCB;
   rm->state->vidCtrl.revoke[decoderIdx].notify.notifyUserData= req->notifyUserData;
   rm->state->vidCtrl.revoke[decoderIdx].notify.semNotify= &rm->state->vidCtrl.revoke[decoderIdx].semNotify;
   rm->state->vidCtrl.revoke[decoderIdx].notify.semConfirm= &rm->state->vidCtrl.revoke[decoderIdx].semConfirm;
   rm->state->vidCtrl.revoke[decoderIdx].notify.event= EssRMgrEvent_revoked;
   rm->state->vidCtrl.revoke[decoderIdx].notify.type= EssRMgrResType_videoDecoder;
   rm->state->vidCtrl.revoke[decoderIdx].notify.priority= req->priority;
   rm->state->vidCtrl.revoke[decoderIdx].notify.resourceIdx= decoderIdx;
   rm->state->vidCtrl.revoke[decoderIdx].notify.req= *req;
   rm->state->vidCtrl.revoke[decoderIdx].pidUser= pid;
   rm->state->vidCtrl.revoke[decoderIdx].priorityUser= req->priority;

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

   rc= pthread_create( &threadId, &attr, essRMNotifyThread, &rm->state->vidCtrl.revoke[decoderIdx].notify );
   if ( rc == 0 )
   {
      rm->state->base.videoDecoder[decoderIdx].requestIdOwner= req->requestId;
      rm->state->base.videoDecoder[decoderIdx].pidOwner= pid;
      rm->state->base.videoDecoder[decoderIdx].priorityOwner= req->priority;
      rm->state->base.videoDecoder[decoderIdx].usageOwner= req->usage;

      result= true;
   }
   else
   {
      ERROR("error starting notification thread: errno %d", errno );
   }

   return result;
}

static bool essRMRevokeVideoDecoder( EssRMgr *rm, int decoderIdx )
{
   bool result= false;

   if ( rm )
   {
      int rc;
      int pidPreempt= rm->state->base.videoDecoder[decoderIdx].pidOwner;

      // preempt current owner
      DEBUG("preempting pid %d to revoke decoder %d", pidPreempt, decoderIdx );

      rm->state->vidCtrl.revoke[decoderIdx].notify.needConfirmation= true;

      rc= sem_post( &rm->state->vidCtrl.revoke[decoderIdx].semNotify );
      if ( rc == 0 )
      {
         int retry= 300;

         essRMUnlockCtrlFile( rm );
         for( ; ; )
         {
            rc= sem_trywait( &rm->state->vidCtrl.revoke[decoderIdx].semConfirm );
            if ( rc == 0 )
            {
               DEBUG("preemption of pid %d to revoke decoder %d successful", pidPreempt, decoderIdx );
               break;
            }
            if ( --retry == 0 )
            {
               INFO("preemption timeout waiting for pid %d to release decoder %d", pidPreempt, decoderIdx );
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

exit:
   return result;
}

static bool essRMTransferVideoDecoder( EssRMgr *rm, int decoderIdx, EssRMgrVideoDecoderNotify *pending )
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
               DEBUG("transfer of video decoder %d to pid %d successful",
                      decoderIdx,
                      pending->pidUser );
               break;
            }
            if ( --retry == 0 )
            {
               INFO("timeout waiting to tranfer video decoder %d to pid %d",
                    decoderIdx,
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

      essRMVidPutPendingPoolItem( rm, pending );
   }

exit:
   return result;
}

static bool essRMRequestVideoDecoder( EssRMgr *rm, EssRMgrRequest *req )
{
   bool result= false;
   bool madeAssignment= false;
   int rc;

   TRACE2("EssRMgrRequestVideoDecoder: enter: rm %p requestId %d", rm, req->requestId );

   if ( rm && req )
   {
      int assignIdx= -1, pendingIdx= -1;
      EssRMgrUsage usage;
      int pid= getpid();
      
      usage.usage= req->usage;
      usage.info= req->info;
      assignIdx= essRMFindSuitableVideoDecoder( rm, req->priority, &usage, pendingIdx );

      if ( assignIdx >= 0 )
      {
         pthread_t threadId;

         if ( rm->state->base.videoDecoder[assignIdx].pidOwner != 0 )
         {
            if ( !essRMRevokeVideoDecoder( rm, assignIdx ) )
            {
               ERROR("failed to revoke decoder %d", assignIdx);
               goto exit;
            }
         }

         if ( essRMAssignVideoDecoder( rm, assignIdx, req ) )
         {
            req->assignedId= assignIdx;
            req->assignedCaps= rm->state->base.videoDecoder[assignIdx].capabilities;
            if ( req->assignedCaps & EssRMgrVidCap_limitedResolution )
            {
               req->info.video.maxWidth= rm->state->base.videoDecoder[assignIdx].maxWidth;
               req->info.video.maxHeight= rm->state->base.videoDecoder[assignIdx].maxHeight;
            }
            result= true;
         }

         rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
      }
      else if ( (pendingIdx >= 0 ) && req->asyncEnable )
      {
         EssRMgrVideoDecoderNotify *pending= essRMVidGetPendingPoolItem( rm );
         if ( pending )
         {
            pthread_t threadId;
            pthread_attr_t attr;

            DEBUG("request %d entering pending state for decoder %d pid %d", req->requestId, pendingIdx, pid );
            pending->notify.needNotification= true;
            pending->notify.rm= rm;
            pending->notify.notifyCB= req->notifyCB;
            pending->notify.notifyUserData= req->notifyUserData;
            pending->notify.semNotify= &pending->semNotify;
            pending->notify.semConfirm= &pending->semConfirm;
            pending->notify.event= EssRMgrEvent_granted;
            pending->notify.type= EssRMgrResType_videoDecoder;
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
               essRMVidInsertPendingByPriority( rm, pendingIdx, pending );

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

exit:

   return result;
}

static void essRMReleaseVideoDecoder( EssRMgr *rm, int id )
{
   if ( rm )
   {
      if ( (id >= 0) && (id < rm->state->base.numVideoDecoders) )
      {
         int pid= getpid();
         if ( pid == rm->state->base.videoDecoder[id].pidOwner )
         {
            DEBUG("pid %d releasing video decoder %d", pid, id);
            rm->state->base.videoDecoder[id].requestIdOwner= -1;
            rm->state->base.videoDecoder[id].pidOwner= 0;
            rm->state->base.videoDecoder[id].priorityOwner= 0;
            rm->state->base.videoDecoder[id].usageOwner= 0;

            rm->state->vidCtrl.revoke[id].notify.notifyCB= 0;
            rm->state->vidCtrl.revoke[id].notify.notifyUserData= 0;
            if ( rm->state->vidCtrl.revoke[id].notify.needNotification )
            {
               rm->state->vidCtrl.revoke[id].notify.needNotification= false;
               sem_post( &rm->state->vidCtrl.revoke[id].semNotify );
            }

            if ( rm->state->vidCtrl.revoke[id].notify.needConfirmation )
            {
               rm->state->vidCtrl.revoke[id].notify.needConfirmation= false;
               sem_post( &rm->state->vidCtrl.revoke[id].semConfirm );
            }

            if ( rm->state->base.videoDecoder[id].pendingNtfyIdx >= 0 )
            {
               EssRMgrVideoDecoderNotify *pending= &rm->state->vidCtrl.pending[rm->state->base.videoDecoder[id].pendingNtfyIdx];
               rm->state->base.videoDecoder[id].pendingNtfyIdx= pending->next;
               if ( pending->next >= 0 )
               {
                  rm->state->vidCtrl.pending[pending->next].prev= -1;
               }
               rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );

               essRMTransferVideoDecoder( rm, id, pending );
            }

            rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
         }
         else
         {
            ERROR("pid %d attempting to release decoder %d owned by pid %d", 
                   pid, id, rm->state->base.videoDecoder[id].pidOwner );
         }
      }
   }
}

static bool essRMSetPriorityVideoDecoder( EssRMgr *rm, int requestId, int priority )
{
   bool result= false;

   if ( rm )
   {
      if ( (requestId >= 0) && (requestId < rm->state->hdr.nextRequestId) )
      {
         int id;
         bool found= false;
         int pendingNtfyIdx= -1;
         int pid= getpid();
         for( id= 0; id < rm->state->base.numVideoDecoders; ++id )
         {
            if ( (rm->state->base.videoDecoder[id].requestIdOwner == requestId) &&
                 (rm->state->base.videoDecoder[id].pidOwner == pid) )
            {
               DEBUG("found request %d as owner of decoder %d", requestId, id);
               found= true;
               rm->state->base.videoDecoder[id].priorityOwner= priority;

               pendingNtfyIdx= rm->state->base.videoDecoder[id].pendingNtfyIdx;
               if ( pendingNtfyIdx >= 0 )
               {
                  EssRMgrVideoDecoderNotify *pending= &rm->state->vidCtrl.pending[pendingNtfyIdx];
                  if ( priority <= pending->priorityUser )
                  {
                     pendingNtfyIdx= -1;
                     result= true;
                  }
                  else
                  {
                     essRMVidRemovePending( rm, id, pending );
                  }
               }
            }
            else
            {
               pendingNtfyIdx= rm->state->base.videoDecoder[id].pendingNtfyIdx;
               while ( pendingNtfyIdx >= 0 )
               {
                  EssRMgrVideoDecoderNotify *pending= &rm->state->vidCtrl.pending[pendingNtfyIdx];
                  if ( (pending->notify.req.requestId == requestId) && (pending->pidUser == pid) )
                  {
                     DEBUG("found request %d in decoder %d pending list: change priority from %d to %d owner priority %d", 
                           requestId, id, pending->priorityUser, priority, rm->state->base.videoDecoder[id].priorityOwner );
                     found= true;
                     pending->priorityUser= pending->notify.req.priority= priority;
                     essRMVidRemovePending( rm, id, pending );
                     if ( pending->priorityUser >= rm->state->base.videoDecoder[id].priorityOwner )
                     {
                        essRMVidInsertPendingByPriority( rm, id, pending );
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
            if ( essRMRevokeVideoDecoder( rm, id ) )
            {
               EssRMgrVideoDecoderNotify *pending= &rm->state->vidCtrl.pending[pendingNtfyIdx];

               result= essRMTransferVideoDecoder( rm, id, pending );

               rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );
            }
         }
      }
   }

exit:
   return result;
}

static bool essRMSetUsageVideoDecoder( EssRMgr *rm, int requestId, EssRMgrUsage *usage )
{
   bool result= false;

   if ( rm )
   {
      if ( (requestId >= 0) && (requestId < rm->state->hdr.nextRequestId) )
      {
         int id;
         int pendingNtfyIdx= -1;
         int pid= getpid();
         EssRMgrVideoDecoderNotify *pending= 0;

         for( id= 0; id < rm->state->base.numVideoDecoders; ++id )
         {
            if ( (rm->state->base.videoDecoder[id].requestIdOwner == requestId) &&
                 (rm->state->base.videoDecoder[id].pidOwner == pid) )
            {
               int usageOrg;
               int testResult, testResultOrg;

               usageOrg= rm->state->base.videoDecoder[id].usageOwner;

               // update owner's usage
               rm->state->base.videoDecoder[id].usageOwner= usage->usage;
               rm->state->hdr.crc= getCRC32( (unsigned char *)&rm->state->base, sizeof(EssRMgrBase) );

               pendingNtfyIdx= rm->state->base.videoDecoder[id].pendingNtfyIdx;

               testResult= (usage->usage & rm->state->base.videoDecoder[id].capabilities) & ESSRMGR_CRITERIA_MASK;
               if ( testResult )
               {
                  // owned decoder is no longer eligible for new usage
                  essRMRevokeVideoDecoder( rm, id );

                  result= true;
                  goto exit;
               }

               testResultOrg= (~(usageOrg ^ (rm->state->base.videoDecoder[id].capabilities)) & ESSRMGR_CRITERIA_MASK);
               testResult= (~(usage->usage ^ (rm->state->base.videoDecoder[id].capabilities)) & ESSRMGR_CRITERIA_MASK);
               if (
                    (testResult != testResultOrg) &&
                    (testResult != 0) &&
                    (pendingNtfyIdx >= 0)
                  )
               {
                  EssRMgrVideoDecoderNotify *pending= &rm->state->vidCtrl.pending[pendingNtfyIdx];
                  testResult= (~(pending->notify.req.usage ^ (rm->state->base.videoDecoder[id].capabilities)) & ESSRMGR_CRITERIA_MASK);
                  if ( testResult == 0 )
                  {
                     // owned decoder is now more suitable for a pending request
                     if ( pendingNtfyIdx >= 0 )
                     {
                        essRMRevokeVideoDecoder( rm, id );
                     }
                  }
               }

               result= true;
               goto exit;
            }
            else
            {
               pendingNtfyIdx= rm->state->base.videoDecoder[id].pendingNtfyIdx;
               while ( pendingNtfyIdx >= 0 )
               {
                  pending= &rm->state->vidCtrl.pending[pendingNtfyIdx];
                  if ( (pending->notify.req.requestId == requestId) && (pending->pidUser == pid) )
                  {
                     EssRMgrRequest req;

                     // remove pending request and then re-issue with new usage
                     essRMVidRemovePending( rm, id, pending );

                     req= pending->notify.req;
                     req.usage= usage->usage;
                     req.info= usage->info;

                     essRMVidPutPendingPoolItem( rm, pending);

                     result= essRMRequestVideoDecoder( rm, &req);
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

static int essRMFindSuitableVideoDecoder( EssRMgr *rm, int priority, EssRMgrUsage *usage, int& pendingIdx )
{
   int suitableIdx= -1;

   pendingIdx= -1;

   if ( rm )
   {
      int i, j, testResult, idx;
      std::vector<int> eligibleDecoders, bestDecoders;

      // Deterimine set of defined decoders that are do not violate usage constraints
      for( i= 0; i < rm->state->base.numVideoDecoders; ++i )
      {
         testResult= (usage->usage & rm->state->base.videoDecoder[i].capabilities) & ESSRMGR_CRITERIA_MASK;
         if ( testResult == 0 )
         {
            DEBUG("decoder %d caps 0x%X eligible", i, rm->state->base.videoDecoder[i].capabilities);
            eligibleDecoders.push_back( i );
         }
      }
      if ( eligibleDecoders.size() )
      {
         int currMaxSize= -1;
         int maxSize;
         std::vector<int> *group;

         // Check eligible decoders for one that meets constraints but does not provide
         // unnecessary capabilities
         for( i= 0; i < eligibleDecoders.size(); ++i )
         {
            idx= eligibleDecoders[i];
            testResult= (~(usage->usage ^ (rm->state->base.videoDecoder[idx].capabilities)) & ESSRMGR_CRITERIA_MASK);
            if ( testResult == 0 )
            {
               DEBUG("decoder %d caps 0x%X ideal", i, rm->state->base.videoDecoder[idx].capabilities);
               bestDecoders.push_back( idx );
            }
         }

         // Look for the most suitable decoder
         for( i= 0; i < 2; i++ )
         {
            group= ((i == 0) ? &bestDecoders : &eligibleDecoders);
            for( j= 0; j < group->size(); ++j )
            {
               idx= group->at(j);
               maxSize= -1;
               if ( rm->state->base.videoDecoder[idx].capabilities & EssRMgrVidCap_limitedResolution )
               {
                  maxSize= rm->state->base.videoDecoder[idx].maxWidth*rm->state->base.videoDecoder[idx].maxHeight;
                  if ( 
                       ((usage->info.video.maxWidth > 0) &&
                        (usage->info.video.maxWidth > rm->state->base.videoDecoder[idx].maxWidth)) ||
                       ((usage->info.video.maxHeight > 0) &&
                        (usage->info.video.maxHeight > rm->state->base.videoDecoder[idx].maxHeight))
                     )
                  {
                     TRACE1("decoder %d disqualified: size constraints: target (%dx%d) limit (%dx%d)",
                           idx,
                           usage->info.video.maxWidth, usage->info.video.maxHeight,
                           rm->state->base.videoDecoder[idx].maxWidth,
                           rm->state->base.videoDecoder[idx].maxHeight );
                     continue;
                  }
               }
               if ( 
                    (rm->state->base.videoDecoder[idx].pidOwner != 0) &&
                    (rm->state->base.videoDecoder[idx].priorityOwner == priority) &&
                    (rm->state->base.videoDecoder[idx].usageOwner == usage->usage)
                  )
               {
                  TRACE1("decoder %d disqualified: same pri,same usage: owner pid %d pri %d usage 0x%X",
                         idx,
                         rm->state->base.videoDecoder[idx].pidOwner,
                         priority,
                         usage->usage );
                  if ( pendingIdx < 0 )
                  {
                     pendingIdx= idx;
                  }
                  continue;
               }
               if ( 
                    (rm->state->base.videoDecoder[idx].pidOwner != 0) &&
                    (rm->state->base.videoDecoder[idx].priorityOwner < priority)
                  )
               {
                  TRACE1("decoder %d disqualified: priorty: owner pid %d pri %d req pri %d",
                         idx,
                         rm->state->base.videoDecoder[idx].pidOwner,
                         rm->state->base.videoDecoder[idx].priorityOwner,
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
                    (rm->state->base.videoDecoder[suitableIdx].pidOwner != 0) &&
                    (rm->state->base.videoDecoder[idx].pidOwner != 0) &&
                    (rm->state->base.videoDecoder[suitableIdx].priorityOwner > rm->state->base.videoDecoder[idx].priorityOwner)
                  )
               {
                  TRACE1("decoder %d disqualified: curr candidate for preemption has lower priority: %d vs %d",
                         rm->state->base.videoDecoder[suitableIdx].priorityOwner,
                         rm->state->base.videoDecoder[idx].priorityOwner );
                  continue;
               }
               else 
               if (
                    (currMaxSize != -1) &&
                    (maxSize == -1) || (maxSize < currMaxSize)
                  )
               {
                  if ( 
                       (rm->state->base.videoDecoder[suitableIdx].pidOwner > 0) ||
                       (rm->state->base.videoDecoder[idx].pidOwner == 0)
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
      }
   }

   return suitableIdx;
}

static void essRMCancelRequestVideoDecoder( EssRMgr *rm, int requestId )
{
   if ( rm )
   {
      if ( (requestId >= 0) && (requestId < rm->state->hdr.nextRequestId) )
      {
         int id;
         bool found= false;
         int pendingNtfyIdx= -1;
         int pid= getpid();

         for( id= 0; id < rm->state->base.numVideoDecoders; ++id )
         {
            if ( (rm->state->base.videoDecoder[id].requestIdOwner == requestId) &&
                 (rm->state->base.videoDecoder[id].pidOwner == pid) )
            {
               DEBUG("found request %d as owner of decoder %d", requestId, id);
               found= true;
               essRMReleaseVideoDecoder( rm, id );
            }
            else   
            {
               pendingNtfyIdx= rm->state->base.videoDecoder[id].pendingNtfyIdx;
               while ( pendingNtfyIdx >= 0 )
               {
                  EssRMgrVideoDecoderNotify *pending= &rm->state->vidCtrl.pending[pendingNtfyIdx];
                  if ( (pending->notify.req.requestId == requestId) && (pending->pidUser == pid) )
                  {
                     DEBUG("found request %d in decoder %d pending list", requestId, id );
                     found= true;
                     essRMVidRemovePending( rm, id, pending );
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
                     rm->state->base.videoDecoder[videoIndex].maxWidth= maxWidth;
                     rm->state->base.videoDecoder[videoIndex].maxHeight= maxHeight;
                     INFO("  max res: %dx%d", maxWidth, maxHeight);
                     maxWidth= -1;
                     maxHeight= -1;
                  }
                  ++videoIndex;
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

   INFO("config file defines %d video decoders", rm->state->base.numVideoDecoders);

   result= true;

exit:
   return result;
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
                  if ( essRMAssignVideoDecoder( notify->rm, notify->resourceIdx, &notify->req ) )
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
   DEBUG("notify thread exit");
   return NULL;
}

