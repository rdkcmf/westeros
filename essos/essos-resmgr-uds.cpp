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

#include <sys/socket.h>
#include <sys/un.h>
#include <linux/netlink.h>
#include <assert.h>
#include <algorithm>
#include <map>
#include <string>

#define ESSRMGR_SERVER_NAME "resource"

#define ESSRMGR_MAX_APPIDLEN (80)

#define ESSRMGR_NOT_AUTHORIZED (2)

typedef struct _EssRMgrResourceServerCtx EssRMgrResourceServerCtx;
typedef struct _EssRMgrResourceConnection EssRMgrResourceConnection;

typedef struct _EssRMgrUserNotify
{
   EssRMgrResourceServerCtx *server;
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

typedef struct _EssRMgrResourceNotify
{
   int self;
   int next;
   int prev;
   int type;
   EssRMgrResourceConnection *connUser;
   int priorityUser;
   EssRMgrUserNotify notify;
} EssRMgrResourceNotify;

typedef struct _EssRMgrResource
{
   int type;
   int capabilities;
   int criteriaMask;
   int requestIdOwner;
   EssRMgrResourceConnection *connOwner;
   int state;
   int priorityOwner;
   int usageOwner;
   int pendingNtfyIdx;
   EssRMgrUsageInfo usageInfo;
} EssRMgrResource;

typedef struct _EssRMgrBase
{
   bool requesterWinsPriorityTie;
   bool unauthorizedRequestsAbort;
   int timeoutMS;
   int numVideoDecoders;
   EssRMgrResource videoDecoder[ESSRMGR_MAX_ITEMS];
   int numAudioDecoders;
   EssRMgrResource audioDecoder[ESSRMGR_MAX_ITEMS];
   int numFrontEnds;
   EssRMgrResource frontEnd[ESSRMGR_MAX_ITEMS];
   int numSVPAllocators;
   EssRMgrResource svpAlloc[ESSRMGR_MAX_ITEMS];
} EssRMgrBase;

typedef struct _EssRMgrResourceControl
{
   EssRMgrResourceNotify pending[ESSRMGR_MAX_PENDING];
   int maxPoolItems;
   int pendingPoolIdx;
} EssRMgrResourceControl;

typedef struct _EssRMgrState
{
   pthread_mutex_t mutex;
   EssRMgrBase base;
   EssRMgrResourceControl vidCtrl;
   EssRMgrResourceControl audCtrl;
   EssRMgrResourceControl feCtrl;
   EssRMgrResourceControl svpaCtrl;
} EssRMgrState;

typedef struct _EssRMgrResourceConnection
{
   pthread_mutex_t mutex;
   EssRMgrResourceServerCtx *server;
   int socketFd;
   pthread_t threadId;
   bool threadStarted;
   bool threadStopRequested;
   sem_t semComplete;
   int clientId;
   char appId[ESSRMGR_MAX_APPIDLEN+1];
} 
EssRMgrResourceConnection;

#define MAX_SUN_PATH (80)
typedef struct _EssRMgrServerCtx
{
   pthread_mutex_t mutex;
   int refCnt;
   const char *name;
   struct sockaddr_un addr;
   char lock[MAX_SUN_PATH+6];
   int lockFd;
   int socketFd;
   pthread_t threadId;
   bool threadStarted;
   bool threadStopRequested;
} EssRMgrServerCtx;

typedef struct _EssRMgrResourceServerCtx
{
   EssRMgrServerCtx *server;
   std::vector<EssRMgrResourceConnection*> connections;
   EssRMgrState *state;
   int nextClientId;
   std::map<std::string,int> blackList;
} EssRMgrResourceServerCtx;

typedef struct _EssRMgrClientConnection
{
   EssRMgr *rm;
   pthread_mutex_t mutex;
   const char *name;
   struct sockaddr_un addr;
   int socketFd;
   pthread_t threadId;
   bool ready;
   bool threadStarted;
   bool threadStopRequested;
   bool unauthorizedRequestsAbort;
   int timeoutMS;
} EssRMgrClientConnection;

typedef enum _EssRMgrValue
{
   EssRMgrValue_count= 0,
   EssRMgrValue_owner= 1,
   EssRMgrValue_caps= 2,
   EssRMgrValue_state= 3,
   EssRMgrValue_aggregateState= 4,
   EssRMgrValue_policy_tie= 5,
   EssRMgrValue_policy_abortUnauthorized= 6,
   EssRMgrValue_policy_revokeTimeout= 7,
} EssRMgrValue;

typedef struct _EssRMgrRequestInfo
{
   sem_t semComplete;
   sem_t semConfirm;
   bool needConfirm;
   bool waitForever;
   int type;
   int requestId;
   int assignedId;
   int result;
   int value1;
   int value2;
   int value3;
   EssRMgr *rm;
   EssRMgrRequest req;
} EssRMgrRequestInfo;

typedef struct _EssRMgr
{
   pthread_mutex_t mutex;
   EssRMgrClientConnection *conn;
   int nextRequestId;
   std::vector<EssRMgrRequestInfo*> requests;
   std::vector<EssRMgrRequestInfo*> notifications;
} EssRMgr;

static void essRMInitDefaultState( EssRMgrResourceServerCtx *rm );
static bool essRMReadConfigFile( EssRMgrResourceServerCtx *rm );
static bool essRMgrAppIdAuthorized( EssRMgrResourceConnection *conn, char *appId );
static bool essRMgrUpdateBlackList( EssRMgrResourceConnection *conn, char *appId, bool add );
static bool essRMRequestResource( EssRMgrResourceConnection *conn, EssRMgrRequest *req );
static void essRMReleaseResource( EssRMgrResourceConnection *conn, int type, int id );
static bool essRMSetPriorityResource( EssRMgrResourceConnection *conn, int requestId, int type, int priority );
static bool essRMSetUsageResource( EssRMgrResourceConnection *conn, int requestId, int type, EssRMgrUsage *usage );
static int essRMFindSuitableResource( EssRMgrResourceConnection *conn, int type, int priority, EssRMgrUsage *usage, int& pendingIdx );
static void essRMCancelRequestResource( EssRMgrResourceConnection *conn, int requestId, int type );
static EssRMgrResourceNotify* essRMGetPendingPoolItem( EssRMgrResourceConnection *conn, int type );
static void essRMPutPendingPoolItem( EssRMgrResourceConnection *conn, EssRMgrResourceNotify *notify );
static void essRMInsertPendingByPriority( EssRMgrResourceConnection *conn, int id, EssRMgrResourceNotify *item );
static void essRMRemovePending( EssRMgrResourceConnection *conn, int id, EssRMgrResourceNotify *item );
static bool essRMAssignResource( EssRMgrResourceConnection *conn, int id, EssRMgrRequest *req );
static bool essRMRevokeResource( EssRMgrResourceConnection *conn, int type, int id, bool wait );
static bool essRMTransferResource( EssRMgrResourceConnection *conn, EssRMgrResourceNotify *pending );
static void essRMInvokeNotify( EssRMgr *rm, int event, EssRMgrRequestInfo *info );
static void* essRMNotifyThread( void *userData );
static void essRMDestroyResourceConnection( EssRMgrResourceConnection *conn );
static bool essRMSemWait( sem_t *sem, bool waitForever, int retry );

static EssRMgrResourceServerCtx *gCtx= 0;

static void essRMDumpMessage( unsigned char *p, int len)
{
   int i, c, col;

   col= 0;
   for( i= 0; i < len; ++i )
   {
      if ( col == 0 ) fprintf(stderr, "%04X: ", i);

      c= p[i];

      fprintf(stderr, "%02X ", c);

      if ( col == 7 ) fprintf( stderr, " - " );

      if ( col == 15 ) fprintf( stderr, "\n" );

      ++col;
      if ( col >= 16 ) col= 0;
   }

   if ( col > 0 ) fprintf(stderr, "\n");
}

static unsigned int getU32( unsigned char *p )
{
   unsigned n;

   n= (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|(p[3]);

   return n;
}

static int putU32( unsigned char *p, unsigned n )
{
   p[0]= (n>>24);
   p[1]= (n>>16);
   p[2]= (n>>8);
   p[3]= (n&0xFF);

   return 4;
}

static bool essRMSendResRevoke( EssRMgrResourceConnection *conn, int type, int id )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len;
      int sentLen;

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'V';
      mbody[len++]= type;
      len += putU32( &mbody[len], id );
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendResRevoke: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent res type %d revoke to conn %p", type, conn);
      }
   }
   return result;
}

static bool essRMSendResRequestResponse( EssRMgrResourceConnection *conn, EssRMgrRequest *req, int reqResult )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len;
      int sentLen;

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'R';
      mbody[len++]= reqResult;
      len += putU32( &mbody[len], req->requestId );
      len += putU32( &mbody[len], req->assignedId);
      len += putU32( &mbody[len], req->assignedCaps);
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendResRequestResponse: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent res request rsp: type %d to resource client", req->type);
      }
   }
   return result;
}

static bool essRMSendGetValueResponse( EssRMgrResourceConnection *conn, int requestId, int valueId, int value1, int value2, int value3 )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len;
      int sentLen;

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'T';
      len += putU32( &mbody[len], requestId );
      mbody[len++]= valueId;
      len += putU32( &mbody[len], value1 );
      len += putU32( &mbody[len], value2 );
      len += putU32( &mbody[len], value3 );
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendGetCountResponse: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent get value rsp: valueId %d to resource client", valueId);
      }
   }
   return result;
}

static bool essRMSendDumpStateResponse( EssRMgrResourceConnection *conn, int requestId, char *data )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[9+255];
      int len, datalen= 0;
      int sentLen;

      if ( data )
      {
         datalen= strlen(data);
         if ( datalen > (sizeof(mbody)-9) )
         {
            datalen= sizeof(mbody)-9;
         }
      }

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'D';
      len += putU32( &mbody[len], requestId );
      mbody[len++]= datalen;
      if ( datalen )
      {
         strncpy( (char*)&mbody[len], data, datalen );
         len += datalen;
      }
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendDumpStateResponse: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent dump state len %d datalen %d resource client", len, datalen);
      }
   }
   return result;
}

static void essRMDumpState( EssRMgrResourceConnection *conn, int requestId )
{
   char msg[256];
   EssRMgrState *state= conn->server->state;

   snprintf(msg, sizeof(msg), "requester wins priority tie: %d\n", state->base.requesterWinsPriorityTie);
   essRMSendDumpStateResponse( conn, requestId, msg );
   snprintf(msg, sizeof(msg), "unauthorized requests abort: %d\n", state->base.unauthorizedRequestsAbort);
   essRMSendDumpStateResponse( conn, requestId, msg );
   snprintf(msg, sizeof(msg), "revoke-timeout: %d ms\n", state->base.timeoutMS);
   essRMSendDumpStateResponse( conn, requestId, msg );
   for( int i= 0; i < state->base.numVideoDecoders; ++i )
   {
      snprintf(msg, sizeof(msg), "video decoder: %d caps %X owner %d priority %d\n",
               i,
               state->base.videoDecoder[i].capabilities,
               (state->base.videoDecoder[i].connOwner ? state->base.videoDecoder[i].connOwner->clientId : 0),
               state->base.videoDecoder[i].priorityOwner );
      essRMSendDumpStateResponse( conn, requestId, msg );
   }
   for( int i= 0; i < state->base.numAudioDecoders; ++i )
   {
      snprintf(msg, sizeof(msg), "audio decoder: %d caps %X owner %d priority %d\n",
               i,
               state->base.audioDecoder[i].capabilities,
               (state->base.audioDecoder[i].connOwner  ? state->base.audioDecoder[i].connOwner->clientId : 0),
               state->base.audioDecoder[i].priorityOwner );
      essRMSendDumpStateResponse( conn, requestId, msg );
   }
   for( int i= 0; i < state->base.numFrontEnds; ++i )
   {
      snprintf(msg, sizeof(msg), "frontend: %d caps %X owner %d priority %d\n",
               i,
               state->base.frontEnd[i].capabilities,
               (state->base.frontEnd[i].connOwner  ? state->base.frontEnd[i].connOwner->clientId : 0),
               state->base.frontEnd[i].priorityOwner );
      essRMSendDumpStateResponse( conn, requestId, msg );
   }
   for( int i= 0; i < state->base.numSVPAllocators; ++i )
   {
      snprintf(msg, sizeof(msg), "svpa: %d caps %X owner %d priority %d\n",
               i,
               state->base.svpAlloc[i].capabilities,
               (state->base.svpAlloc[i].connOwner  ? state->base.svpAlloc[i].connOwner->clientId : 0),
               state->base.frontEnd[i].priorityOwner );
      essRMSendDumpStateResponse( conn, requestId, msg );
   }
   if ( conn->server->blackList.size() )
   {
      for( std::map<std::string,int>::iterator it= conn->server->blackList.begin(); it != conn->server->blackList.end(); ++it )
      {
         snprintf(msg, sizeof(msg), "black list: (%s)\n", it->first.c_str() );
         essRMSendDumpStateResponse( conn, requestId, msg);
      }
   }
   essRMSendDumpStateResponse( conn, requestId, 0 );
}

static int essRMGetAggregateState( EssRMgrResourceConnection *conn )
{
   int avstate= EssRMgrRes_idle;
   EssRMgrState *state= conn->server->state;

   for( int i= 0; i < state->base.numVideoDecoders; ++i )
   {
      DEBUG("video %d state %d", i, state->base.videoDecoder[i].state);
      if ( state->base.videoDecoder[i].state > avstate )
      {
         avstate= state->base.videoDecoder[i].state;
         DEBUG("avstate %d", avstate);
      }
   }
   for( int i= 0; i < state->base.numAudioDecoders; ++i )
   {
      DEBUG("audio %d state %d", i, state->base.audioDecoder[i].state);
      if ( state->base.audioDecoder[i].state > avstate )
      {
         avstate= state->base.audioDecoder[i].state;
         DEBUG("avstate %d", avstate);
      }
   }
   DEBUG("aggregate avstate %d", avstate);

   return avstate;
}

static void essRMReleaseConnectionResources( EssRMgrResourceConnection *conn )
{
   EssRMgrState *state= conn->server->state;
   
   for( int i= 0; i < state->base.numVideoDecoders; ++i )
   {
      if ( state->base.videoDecoder[i].connOwner == conn )
      {
         DEBUG("removing dead owner conn %p vid decoder %d", conn, i );
         state->base.videoDecoder[i].requestIdOwner= -1;
         state->base.videoDecoder[i].connOwner= 0;
         state->base.videoDecoder[i].priorityOwner= 0;
         state->base.videoDecoder[i].usageOwner= 0;
         state->base.videoDecoder[i].state= EssRMgrRes_idle;
      }
   }

   for( int i= 0; i < state->base.numAudioDecoders; ++i )
   {
      if ( state->base.audioDecoder[i].connOwner == conn )
      {
         DEBUG("removing dead owner conn %p aud decoder %d", conn, i );
         state->base.audioDecoder[i].requestIdOwner= -1;
         state->base.audioDecoder[i].connOwner= 0;
         state->base.audioDecoder[i].priorityOwner= 0;
         state->base.audioDecoder[i].usageOwner= 0;
         state->base.audioDecoder[i].state= EssRMgrRes_idle;
      }
   }

   for( int i= 0; i < state->base.numFrontEnds; ++i )
   {
      if ( state->base.frontEnd[i].connOwner == conn )
      {
         DEBUG("removing dead owner conn %p frontend %d", conn, i );
         state->base.frontEnd[i].requestIdOwner= -1;
         state->base.frontEnd[i].connOwner= 0;
         state->base.frontEnd[i].priorityOwner= 0;
         state->base.frontEnd[i].usageOwner= 0;
         state->base.frontEnd[i].state= EssRMgrRes_idle;
      }
   }

   for( int i= 0; i < state->base.numSVPAllocators; ++i )
   {
      if ( state->base.svpAlloc[i].connOwner == conn )
      {
         DEBUG("removing dead owner conn %p svpa %d", conn, i );
         state->base.svpAlloc[i].requestIdOwner= -1;
         state->base.svpAlloc[i].connOwner= 0;
         state->base.svpAlloc[i].priorityOwner= 0;
         state->base.svpAlloc[i].usageOwner= 0;
         state->base.svpAlloc[i].state= EssRMgrRes_idle;
      }
   }
}

static void *essRMResourceConnectionThread( void *arg )
{
   EssRMgrResourceConnection *conn= (EssRMgrResourceConnection*)arg;
   EssRMgrResourceServerCtx *server= conn->server;
   struct msghdr msg;
   struct iovec iov[1];
   unsigned char mbody[64+ESSRMGR_MAX_APPIDLEN];
   int moff= 0, len, i, rc;

   DEBUG("essRMResourceConnectionThread: enter");

   essRMSendGetValueResponse( conn, -1, EssRMgrValue_policy_abortUnauthorized, server->state->base.unauthorizedRequestsAbort, 0, 0);

   essRMSendGetValueResponse( conn, -1, EssRMgrValue_policy_revokeTimeout, server->state->base.timeoutMS, 0, 0);

   conn->threadStarted= true;
   while( !conn->threadStopRequested )
   {
      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= 4;

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      do
      {
         len= recvmsg( conn->socketFd, &msg, 0 );
      }
      while ( (len < 0) && (errno == EINTR));

      if ( len > 0 )
      {
         unsigned char *m= mbody;
         if ( gLogLevel >= 7 )
         {
            essRMDumpMessage( mbody, len );
         }
         if ( (m[0] == 'R') && (m[1] == 'S') )
         {
            int mlen, id;
            mlen= m[2];
            id= m[3];
            if ( mlen > sizeof(mbody)-4 )
            {
               ERROR("bad client message length: %d : truncating");
               mlen= sizeof(mbody)-4;
            }
            if ( mlen > 1 )
            {
               iov[0].iov_base= (char*)mbody+4;
               iov[0].iov_len= mlen-1;

               msg.msg_name= NULL;
               msg.msg_namelen= 0;
               msg.msg_iov= iov;
               msg.msg_iovlen= 1;
               msg.msg_control= 0;
               msg.msg_controllen= 0;
               msg.msg_flags= 0;

               do
               {
                  len= recvmsg( conn->socketFd, &msg, 0 );
               }
               while ( (len < 0) && (errno == EINTR));
            }

            if ( len > 0 )
            {
               len += 4;
               if ( gLogLevel >= 7 )
               {
                  essRMDumpMessage( mbody, len );
               }
               switch( id )
               {
                  case 'R':
                     if ( mlen >= 14 )
                     {
                        bool result= 0;
                        int reqresult= 0;
                        int appIdLen;
                        char appId[ESSRMGR_MAX_APPIDLEN+1];
                        int infolen;
                        int offset;
                        EssRMgrRequest req;
                        memset( &req, 0, sizeof(EssRMgrRequest));
                        req.type= m[4];
                        req.asyncEnable= m[5];
                        req.requestId= getU32( &m[6] );
                        req.usage= getU32( &m[10] );
                        req.priority= getU32( &m[14] );
                        req.assignedId= -1;
                        appIdLen= getU32( &m[18] );
                        offset= 22;
                        if ( appIdLen > 0 )
                        {
                           offset += appIdLen;
                           if ( appIdLen > ESSRMGR_MAX_APPIDLEN ) appIdLen= ESSRMGR_MAX_APPIDLEN;
                           memcpy( appId, &m[22], appIdLen );
                        }
                        appId[appIdLen]= '\0';
                        infolen= getU32( &m[offset] );
                        DEBUG("got res req res type %d", req.type);
                        pthread_mutex_lock( &server->state->mutex );
                        if ( essRMgrAppIdAuthorized( conn, appId ) )
                        {
                           strncpy( conn->appId, appId, ESSRMGR_MAX_APPIDLEN);
                           switch( req.type )
                           {
                              case EssRMgrResType_videoDecoder:
                                 if ( infolen >= 8 )
                                 {
                                    req.info.video.maxWidth= getU32( &m[offset] );
                                    req.info.video.maxHeight= getU32( &m[offset+4] );
                                 }
                                 result= essRMRequestResource( conn, &req );
                                 break;
                              case EssRMgrResType_audioDecoder:
                              case EssRMgrResType_frontEnd:
                              case EssRMgrResType_svpAllocator:
                                 result= essRMRequestResource( conn, &req );
                                 break;
                              default:
                                 ERROR("unsupported resource type: %d", req.type);
                                 break;
                           }
                        }
                        else
                        {
                           reqresult= ESSRMGR_NOT_AUTHORIZED;
                        }
                        essRMSendResRequestResponse(conn, &req, reqresult);
                        pthread_mutex_unlock( &server->state->mutex );
                     }
                     break;
                  case 'L':
                     if ( mlen >= 6 )
                     {
                        int type= m[4];
                        int assignedId= getU32( &m[5] );
                        DEBUG("got res release res type %d assignedId %d", type, assignedId);
                        pthread_mutex_lock( &server->state->mutex );
                        switch( type )
                        {
                           case EssRMgrResType_videoDecoder:
                           case EssRMgrResType_audioDecoder:
                           case EssRMgrResType_frontEnd:
                           case EssRMgrResType_svpAllocator:
                              essRMReleaseResource( conn, type, assignedId );
                              break;
                           default:
                              ERROR("unsupported resource type: %d", type);
                              break;
                        }
                        pthread_mutex_unlock( &server->state->mutex );
                     }
                     break;
                  case 'P':
                     if ( mlen >= 10 )
                     {
                        int type= m[4];
                        int requestId= getU32( &m[5] );
                        int priority= getU32( &m[9] );
                        DEBUG("got set priority type %d requestId %d priority %d", type, requestId, priority);
                        pthread_mutex_lock( &server->state->mutex );
                        essRMSetPriorityResource( conn, requestId, type, priority );
                        pthread_mutex_unlock( &server->state->mutex );
                     }
                     break;
                  case 'S':
                     if ( mlen >= 7 )
                     {
                        int type= m[4];
                        int id= getU32( &m[5] );
                        int state= m[9];
                        DEBUG("got set state type %d id %d state %d", type, id, state);
                        if ( state < EssRMgrRes_max )
                        {
                           switch( type )
                           {
                              case EssRMgrResType_videoDecoder:
                                 if ( (id < server->state->base.numVideoDecoders) &&
                                      (conn ==  server->state->base.videoDecoder[id].connOwner) )
                                 {
                                    server->state->base.videoDecoder[id].state= state;
                                 }
                                 break;
                              case EssRMgrResType_audioDecoder:
                                 if ( (id < server->state->base.numAudioDecoders) &&
                                      (conn ==  server->state->base.audioDecoder[id].connOwner) )
                                 {
                                    server->state->base.audioDecoder[id].state= state;
                                 }
                                 break;
                              case EssRMgrResType_frontEnd:
                                 if ( (id < server->state->base.numFrontEnds) &&
                                      (conn ==  server->state->base.frontEnd[id].connOwner) )
                                 {
                                    server->state->base.frontEnd[id].state= state;
                                 }
                                 break;
                              case EssRMgrResType_svpAllocator:
                                 if ( (id < server->state->base.numSVPAllocators) &&
                                      (conn ==  server->state->base.svpAlloc[id].connOwner) )
                                 {
                                    server->state->base.svpAlloc[id].state= state;
                                 }
                                 break;
                              default:
                                 ERROR("unsupported resource type: %d", type);
                                 break;
                           }
                        }
                        else
                        {
                           ERROR("bad state: %d", state);
                        }
                     }
                     break;
                  case 'U':
                     if ( mlen >= 14 )
                     {
                        EssRMgrUsage usage;
                        int type= m[4];
                        int requestId= getU32( &m[5] );
                        int value= getU32( &m[9] );
                        int infolen= getU32( &m[13] );
                        usage.usage= value;
                        DEBUG("got set usage type %d requestId %d usage %d", type, requestId, usage);
                        pthread_mutex_lock( &server->state->mutex );
                        switch( type )
                        {
                           case EssRMgrResType_videoDecoder:
                              if ( infolen >= 8 )
                              {
                                 usage.info.video.maxWidth= getU32( &m[17] );
                                 usage.info.video.maxHeight= getU32( &m[21] );
                              }
                              essRMSetUsageResource( conn, requestId, type, &usage );
                              break;
                           case EssRMgrResType_audioDecoder:
                           case EssRMgrResType_frontEnd:
                           case EssRMgrResType_svpAllocator:
                              essRMSetUsageResource( conn, requestId, type, &usage );
                              break;
                           default:
                              ERROR("unsupported resource type: %d", type);
                              break;
                        }
                        pthread_mutex_unlock( &server->state->mutex );
                     }
                     break;
                  case 'C':
                     if ( mlen >= 6 )
                     {
                        int type= m[4];
                        int requestId= getU32( &m[5] );
                        DEBUG("got cancel type %d requestId %d", type, requestId);
                        pthread_mutex_lock( &server->state->mutex );
                        essRMCancelRequestResource( conn, requestId, type );                        
                        pthread_mutex_unlock( &server->state->mutex );
                     }
                     break;
                  case 'T':
                     if ( mlen >= 6 )
                     {
                        int requestId= getU32( &m[4] );
                        int valueId= m[8];
                        int value1, value2, value3;
                        value1= value2= value3= 0;
                        DEBUG("got get value valueId %d requestId %d", valueId, requestId);
                        switch( valueId )
                        {
                           case EssRMgrValue_count:
                              {
                                 int type;
                                 type= m[9];
                                 pthread_mutex_lock( &server->state->mutex );
                                 switch( type )
                                 {
                                    case EssRMgrResType_videoDecoder:
                                       value1= server->state->base.numVideoDecoders;
                                       break;
                                    case EssRMgrResType_audioDecoder:
                                       value1= server->state->base.numAudioDecoders;
                                       break;
                                    case EssRMgrResType_frontEnd:
                                       value1= server->state->base.numFrontEnds;
                                       break;
                                    case EssRMgrResType_svpAllocator:
                                       value1= server->state->base.numSVPAllocators;
                                       break;
                                    default:
                                       ERROR("unsupported resource type: %d", type);
                                       break;
                                 }
                                 pthread_mutex_unlock( &server->state->mutex );
                              }
                              break;
                           case EssRMgrValue_owner:
                              {
                                 int type, id;
                                 type= m[9];
                                 id= getU32( &m[10] );
                                 pthread_mutex_lock( &server->state->mutex );
                                 switch( type )
                                 {
                                    case EssRMgrResType_videoDecoder:
                                       if ( id < server->state->base.numVideoDecoders )
                                       {
                                          if ( server->state->base.videoDecoder[id].connOwner )
                                          {
                                             value1= server->state->base.videoDecoder[id].connOwner->clientId;
                                             value2= server->state->base.videoDecoder[id].priorityOwner;
                                          }
                                       }
                                       break;
                                    case EssRMgrResType_audioDecoder:
                                       if ( id < server->state->base.numAudioDecoders )
                                       {
                                          if ( server->state->base.audioDecoder[id].connOwner )
                                          {
                                             value1= server->state->base.audioDecoder[id].connOwner->clientId;
                                             value2= server->state->base.audioDecoder[id].priorityOwner;
                                          }
                                       }
                                       break;
                                    case EssRMgrResType_frontEnd:
                                       if ( id < server->state->base.numFrontEnds )
                                       {
                                          if ( server->state->base.frontEnd[id].connOwner )
                                          {
                                             value1= server->state->base.frontEnd[id].connOwner->clientId;
                                             value2= server->state->base.frontEnd[id].priorityOwner;
                                          }
                                       }
                                       break;
                                    case EssRMgrResType_svpAllocator:
                                       if ( id < server->state->base.numSVPAllocators )
                                       {
                                          if ( server->state->base.svpAlloc[id].connOwner )
                                          {
                                             value1= server->state->base.svpAlloc[id].connOwner->clientId;
                                             value2= server->state->base.svpAlloc[id].priorityOwner;
                                          }
                                       }
                                       break;
                                    default:
                                       ERROR("unsupported resource type: %d", type);
                                       break;
                                 }
                                 pthread_mutex_unlock( &server->state->mutex );
                              }
                              break;
                           case EssRMgrValue_caps:
                              {
                                 int type, id;
                                 type= m[9];
                                 id= getU32( &m[10] );
                                 pthread_mutex_lock( &server->state->mutex );
                                 switch( type )
                                 {
                                    case EssRMgrResType_videoDecoder:
                                       if ( id < server->state->base.numVideoDecoders )
                                       {
                                          value1= server->state->base.videoDecoder[id].capabilities;
                                          value2= server->state->base.videoDecoder[id].usageInfo.video.maxWidth;
                                          value3= server->state->base.videoDecoder[id].usageInfo.video.maxHeight;
                                       }
                                       break;
                                    case EssRMgrResType_audioDecoder:
                                       if ( id < server->state->base.numAudioDecoders )
                                       {
                                          value1= server->state->base.audioDecoder[id].capabilities;
                                       }
                                       break;
                                    case EssRMgrResType_frontEnd:
                                       if ( id < server->state->base.numFrontEnds )
                                       {
                                          value1= server->state->base.frontEnd[id].capabilities;
                                       }
                                       break;
                                    case EssRMgrResType_svpAllocator:
                                       if ( id < server->state->base.numSVPAllocators )
                                       {
                                          value1= server->state->base.svpAlloc[id].capabilities;
                                       }
                                       break;
                                    default:
                                       ERROR("unsupported resource type: %d", type);
                                       break;
                                 }
                                 pthread_mutex_unlock( &server->state->mutex );
                              }
                              break;
                           case EssRMgrValue_state:
                              {
                                 int type, id;
                                 type= m[9];
                                 id= getU32( &m[10] );
                                 pthread_mutex_lock( &server->state->mutex );
                                 switch( type )
                                 {
                                    case EssRMgrResType_videoDecoder:
                                       if ( id < server->state->base.numVideoDecoders )
                                       {
                                          value1= server->state->base.videoDecoder[id].state;
                                       }
                                       break;
                                    case EssRMgrResType_audioDecoder:
                                       if ( id < server->state->base.numAudioDecoders )
                                       {
                                          value1= server->state->base.audioDecoder[id].state;
                                       }
                                       break;
                                    case EssRMgrResType_frontEnd:
                                       if ( id < server->state->base.numFrontEnds )
                                       {
                                          value1= server->state->base.frontEnd[id].state;
                                       }
                                       break;
                                    case EssRMgrResType_svpAllocator:
                                       if ( id < server->state->base.numSVPAllocators )
                                       {
                                          value1= server->state->base.svpAlloc[id].state;
                                       }
                                       break;
                                    default:
                                       ERROR("unsupported resource type: %d", type);
                                       break;
                                 }
                                 pthread_mutex_unlock( &server->state->mutex );
                              }
                              break;
                           case EssRMgrValue_aggregateState:
                              {
                                 pthread_mutex_lock( &server->state->mutex );
                                 value1= essRMGetAggregateState( conn );
                                 pthread_mutex_unlock( &server->state->mutex );
                              }
                              break;
                           case EssRMgrValue_policy_tie:
                              {
                                 value1= server->state->base.requesterWinsPriorityTie;
                              }
                              break;
                           case EssRMgrValue_policy_revokeTimeout:
                              {
                                 value1= server->state->base.timeoutMS;
                              }
                              break;
                        }
                        essRMSendGetValueResponse( conn, requestId, valueId, value1, value2, value3);
                     }
                     break;
                  case 'B':
                     if ( mlen >= 6 )
                     {
                        bool add= ((m[4] != 0) ? true : false);
                        char appId[ESSRMGR_MAX_APPIDLEN+1];
                        int appIdLen= getU32( &m[5] );
                        if ( appIdLen > ESSRMGR_MAX_APPIDLEN )
                        {
                           appIdLen= ESSRMGR_MAX_APPIDLEN;
                        }
                        appId[0]= '\0';
                        if ( appIdLen )
                        {
                           strncpy( appId, (char*)&m[9], appIdLen );
                           appId[appIdLen]= '\0';
                        }
                        DEBUG("got BL add/remove %d appid (%s)", add, appId);
                        pthread_mutex_lock( &server->state->mutex );
                        essRMgrUpdateBlackList( conn, appId, add );
                        pthread_mutex_unlock( &server->state->mutex );
                     }
                     break;
                  case 'D':
                     if ( mlen >= 5 )
                     {
                        int requestId;
                        DEBUG("got dump state req");
                        requestId= getU32( &m[4] );
                        pthread_mutex_lock( &server->state->mutex );
                        essRMDumpState( conn, requestId );
                        pthread_mutex_unlock( &server->state->mutex );
                     }
                     break;
                  default:
                     ERROR("got unknown resource client message: mlen %d", mlen);
                     essRMDumpMessage( mbody, mlen+3 );
                     break;
               }
            }
         }
         else
         {
            ERROR("client msg bad header");
            essRMDumpMessage( mbody, len );
            len= 0;
         }
      }
      else
      {
         DEBUG("resource client disconnected");
         essRMReleaseConnectionResources( conn );
         break;
      }
   }

   essRMDestroyResourceConnection(conn);

   DEBUG("essRMResourceConnectionThread: exit");

   return 0;
}

static EssRMgrResourceConnection *essRMCreateResourceConnection( EssRMgrResourceServerCtx *server, int fd )
{
   EssRMgrResourceConnection *conn= 0;
   int rc;
   bool error= false;

   conn= (EssRMgrResourceConnection*)calloc( 1, sizeof(EssRMgrResourceConnection) );
   if ( conn )
   {
      pthread_attr_t attr;
      pthread_mutex_init( &conn->mutex, 0 );
      conn->socketFd= fd;
      conn->server= server;
      conn->clientId= ++server->nextClientId;

      rc= sem_init( &conn->semComplete, 0, 0 );
      if ( rc != 0 )
      {
         ERROR("Error creating semaphore semComplete for resource connection: errno %d", errno );
         goto exit;
      }

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

      rc= pthread_create( &conn->threadId, &attr, essRMResourceConnectionThread, conn );
      if ( rc )
      {
         ERROR("unable to start resource connection thread: rc %d errno %d", rc, errno);
         error= true;
         goto exit;
      }
      DEBUG("new connection %p client id %d", conn, conn->clientId);
   }

exit:

   if ( error )
   {
      if ( conn )
      {
         pthread_mutex_destroy( &conn->mutex );
         free( conn );
         conn= 0;
      }
   }

   return conn;
}

static void essRMDestroyResourceConnection( EssRMgrResourceConnection *conn )
{
   if ( conn )
   {
      pthread_mutex_lock( &conn->server->server->mutex );
      std::vector<EssRMgrResourceConnection*>& connections = conn->server->connections;
      connections.erase( std::remove(connections.begin(), connections.end(), conn), connections.end() );
      pthread_mutex_unlock( &conn->server->server->mutex );

      if ( conn->socketFd >= 0 )
      {
         shutdown( conn->socketFd, SHUT_RDWR );
      }

      if ( conn->socketFd >= 0 )
      {
         close( conn->socketFd );
         conn->socketFd= -1;
      }

      pthread_mutex_destroy( &conn->mutex );

      free( conn );
   }
}

static void *essRMResoureServerThread( void *arg )
{
   int rc;
   EssRMgrResourceServerCtx *server= (EssRMgrResourceServerCtx*)arg;

   DEBUG("essRMResoureServerThread: enter");

   essRMInitDefaultState( server );

   while( !server->server->threadStopRequested )
   {
      int fd;
      struct sockaddr_un addr;
      socklen_t addrLen= sizeof(addr);

      DEBUG("waiting for connections...");
      fd= accept4( server->server->socketFd, (struct sockaddr *)&addr, &addrLen, SOCK_CLOEXEC );
      if ( fd >= 0 )
      {
         if ( !server->server->threadStopRequested )
         {
            EssRMgrResourceConnection *conn= 0;

            DEBUG("resource server received connection: fd %d", fd);

            conn= essRMCreateResourceConnection( server, fd );
            if ( conn )
            {
               DEBUG("created resource connection %p for fd %d", conn, fd );
               pthread_mutex_lock( &server->server->mutex );
               server->connections.push_back( conn );
               pthread_mutex_unlock( &server->server->mutex );
            }
            else
            {
               ERROR("failed to create resource connection for fd %d", fd);
               close( fd );
            }
         }
         else
         {
            close( fd );
         }
      }
      else
      {
         usleep( 10000 );
      }
   }

exit:
   server->server->threadStarted= false;
   DEBUG("essRMResoureServerThread: exit");

   return 0;
}

static bool essRMInitServiceServer( const char *name, EssRMgrServerCtx **newServer )
{
   bool result= false;
   const char *workingDir;
   int rc, pathNameLen, addressSize;
   EssRMgrServerCtx *server= 0;

   server= (EssRMgrServerCtx*)calloc( 1, sizeof(EssRMgrServerCtx) );
   if ( !server )
   {
      ERROR("No memory for server name (%s)", name);
      goto exit;
   }

   pthread_mutex_init( &server->mutex, 0 );
   server->socketFd= -1;
   server->lockFd= -1;
   server->name= name;

   ++server->refCnt;

   workingDir= getenv("XDG_RUNTIME_DIR");
   if ( !workingDir )
   {
      ERROR("essRMInitServiceServer: XDG_RUNTIME_DIR is not set");
      goto exit;
   }

   pathNameLen= strlen(workingDir)+strlen("/")+strlen(server->name)+1;
   if ( pathNameLen > (int)sizeof(server->addr.sun_path) )
   {
      ERROR("essRMInitServiceServer: name for server unix domain socket is too long: %d versus max %d",
             pathNameLen, (int)sizeof(server->addr.sun_path) );
      goto exit;
   }

   server->addr.sun_family= AF_LOCAL;
   strcpy( server->addr.sun_path, workingDir );
   strcat( server->addr.sun_path, "/" );
   strcat( server->addr.sun_path, server->name );

   strcpy( server->lock, server->addr.sun_path );
   strcat( server->lock, ".lock" );

   server->lockFd= open(server->lock,
                        O_CREAT|O_CLOEXEC,
                        S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
   if ( server->lockFd < 0 )
   {
      ERROR("essRMInitServiceServer: failed to create lock file (%s) errno %d", server->lock, errno );
      goto exit;
   }

   rc= flock(server->lockFd, LOCK_NB|LOCK_EX );
   if ( rc < 0 )
   {
      ERROR("essRMInitServiceServer: failed to lock.  Is another server running with name %s ?", server->name );
      goto exit;
   }

   (void)unlink(server->addr.sun_path);

   server->socketFd= socket( PF_LOCAL, SOCK_STREAM|SOCK_CLOEXEC, 0 );
   if ( server->socketFd < 0 )
   {
      ERROR("essRMInitServiceServer: unable to open socket: errno %d", errno );
      goto exit;
   }

   addressSize= pathNameLen + offsetof(struct sockaddr_un, sun_path);

   rc= bind(server->socketFd, (struct sockaddr *)&server->addr, addressSize );
   if ( rc < 0 )
   {
      ERROR("essRMInitServiceServer: Error: bind failed for socket: errno %d", errno );
      goto exit;
   }

   rc= listen(server->socketFd, 1);
   if ( rc < 0 )
   {
      ERROR("essRMInitServiceServer: Error: listen failed for socket: errno %d", errno );
      goto exit;
   }

   *newServer= server;

   result= true;

exit:

   if ( !result )
   {
      server->addr.sun_path[0]= '\0';
      server->lock[0]= '\0';
   }

   return result;
}

static void essRMTermServiceServer( EssRMgrServerCtx *server )
{
   if ( server )
   {
      int i;

      pthread_mutex_lock( &server->mutex );
      if ( --server->refCnt > 0 )
      {
         pthread_mutex_unlock( &server->mutex );
         return;
      }

      if ( server->socketFd >= 0 )
      {
         shutdown( server->socketFd, SHUT_RDWR );
      }

      if ( server->threadStarted )
      {
         server->threadStopRequested= true;
         pthread_mutex_unlock( &server->mutex );
         pthread_join( server->threadId, NULL );
         pthread_mutex_lock( &server->mutex );
      }

      if ( server->socketFd >= 0 )
      {
         close(server->socketFd);
         server->socketFd= -1;
      }

      if ( server->addr.sun_path )
      {
         (void)unlink( server->addr.sun_path );
         server->addr.sun_path[0]= '\0';
      }

      if ( server->lockFd >= 0 )
      {
         close(server->lockFd);
         server->lockFd= -1;
      }

      if ( server->lock[0] != '\0' )
      {
         (void)unlink( server->lock );
         server->lock[0]= '\0';
      }

      pthread_mutex_unlock( &server->mutex );
      pthread_mutex_destroy( &server->mutex );

      free( server );
   }
}

static bool essRMInitResourceServer( EssRMgrResourceServerCtx *server )
{
   bool result= false;
   int rc;

   if ( !essRMInitServiceServer( ESSRMGR_SERVER_NAME, &server->server ) )
   {
      ERROR("essRMgrInitResourceServer: Error: unable to start service server");
      goto exit;
   }

   server->blackList= std::map<std::string,int>();

   rc= pthread_create( &server->server->threadId, NULL, essRMResoureServerThread, server );
   if ( rc )
   {
      ERROR("essRMgrInitResourceServer: Error: unable to start server thread: rc %d errno %d", rc, errno);
      goto exit;
   }
   server->server->threadStarted= true;

   result= true;

exit:
   return result;
}

static void essRMTermResourceServer( EssRMgrResourceServerCtx *server )
{
   if ( server )
   {
      server->blackList.clear();
      essRMTermServiceServer( server->server );
      server->server= 0;
      if ( server->state )
      {
         pthread_mutex_destroy( &server->state->mutex );
         free( server->state );
         server->state= 0;
      }
      free( server );
   }
}

static void essRMInitDefaultState( EssRMgrResourceServerCtx *server )
{
   memset( server->state, 0, sizeof(EssRMgrState) );

   pthread_mutex_init( &server->state->mutex, 0 );

   if ( !essRMReadConfigFile(server) )
   {
      ERROR("Error processing config file: using default config");
      server->state->base.numVideoDecoders= ESSRMGR_MAX_ITEMS;
      for( int i= 0; i < ESSRMGR_MAX_ITEMS; ++i )
      {
         server->state->base.videoDecoder[i].requestIdOwner= 0;
         server->state->base.videoDecoder[i].connOwner= 0;
         switch ( i )
         {
            case 0:
               server->state->base.videoDecoder[i].capabilities= EssRMgrVidCap_hardware;
               break;
            default:
               server->state->base.videoDecoder[i].capabilities= (EssRMgrVidCap_software|EssRMgrVidCap_limitedPerformance);
               break;
         }
      }
      server->state->base.numAudioDecoders= ESSRMGR_MAX_ITEMS;
      for( int i= 0; i < ESSRMGR_MAX_ITEMS; ++i )
      {
         server->state->base.audioDecoder[i].requestIdOwner= 0;
         server->state->base.audioDecoder[i].connOwner= 0;
         server->state->base.audioDecoder[i].capabilities= EssRMgrAudCap_none;
      }
      server->state->base.numFrontEnds= ESSRMGR_MAX_ITEMS;
      for( int i= 0; i < ESSRMGR_MAX_ITEMS; ++i )
      {
         server->state->base.frontEnd[i].requestIdOwner= 0;
         server->state->base.frontEnd[i].connOwner= 0;
         server->state->base.frontEnd[i].capabilities= EssRMgrFECap_none;
      }
      server->state->base.numSVPAllocators= ESSRMGR_MAX_ITEMS;
      for( int i= 0; i < ESSRMGR_MAX_ITEMS; ++i )
      {
         server->state->base.svpAlloc[i].requestIdOwner= 0;
         server->state->base.svpAlloc[i].connOwner= 0;
         server->state->base.svpAlloc[i].capabilities= EssRMgrSVPACap_none;
      }
   }

   for( int i= 0; i < server->state->base.numVideoDecoders; ++i )
   {
      server->state->base.videoDecoder[i].type= EssRMgrResType_videoDecoder;
      server->state->base.videoDecoder[i].criteriaMask= ESSRMGR_CRITERIA_MASK_VIDEO;
      server->state->base.videoDecoder[i].state= EssRMgrRes_idle;
      server->state->base.videoDecoder[i].pendingNtfyIdx= -1;
   }

   int maxPending= 3*server->state->base.numVideoDecoders;
   DEBUG("vid pendingPool: max pending %d", maxPending);
   for( int i= 0; i < maxPending; ++i )
   {
      EssRMgrResourceNotify *pending= &server->state->vidCtrl.pending[i];
      pending->type= EssRMgrResType_videoDecoder;
      pending->self= i;
      pending->next= ((i+1 < maxPending) ? i+1 : -1);
      pending->prev= ((i > 0) ? i-1 : -1);
      DEBUG("vid pendingPool: item %d next %d prev %d", i, pending->next, pending->prev);
   }
   server->state->vidCtrl.pendingPoolIdx= 0;
   server->state->vidCtrl.maxPoolItems= maxPending;

   for( int i= 0; i < server->state->base.numAudioDecoders; ++i )
   {
      server->state->base.audioDecoder[i].type= EssRMgrResType_audioDecoder;
      server->state->base.audioDecoder[i].criteriaMask= ESSRMGR_CRITERIA_MASK_AUDIO;
      server->state->base.audioDecoder[i].state= EssRMgrRes_idle;
      server->state->base.audioDecoder[i].pendingNtfyIdx= -1;
   }

   maxPending= 3*server->state->base.numAudioDecoders;
   DEBUG("aud pendingPool: max pending %d", maxPending);
   for( int i= 0; i < maxPending; ++i )
   {
      EssRMgrResourceNotify *pending= &server->state->audCtrl.pending[i];
      pending->type= EssRMgrResType_audioDecoder;
      pending->self= i;
      pending->next= ((i+1 < maxPending) ? i+1 : -1);
      pending->prev= ((i > 0) ? i-1 : -1);
      DEBUG("aud pendingPool: item %d next %d prev %d", i, pending->next, pending->prev);
   }
   server->state->audCtrl.pendingPoolIdx= 0;
   server->state->audCtrl.maxPoolItems= maxPending;

   for( int i= 0; i < server->state->base.numFrontEnds; ++i )
   {
      server->state->base.frontEnd[i].type= EssRMgrResType_frontEnd;
      server->state->base.frontEnd[i].criteriaMask= ESSRMGR_CRITERIA_MASK_FE;
      server->state->base.frontEnd[i].state= EssRMgrRes_idle;
      server->state->base.frontEnd[i].pendingNtfyIdx= -1;
   }

   maxPending= 3*server->state->base.numFrontEnds;
   DEBUG("fe pendingPool: max pending %d", maxPending);
   for( int i= 0; i < maxPending; ++i )
   {
      EssRMgrResourceNotify *pending= &server->state->feCtrl.pending[i];
      pending->type= EssRMgrResType_frontEnd;
      pending->self= i;
      pending->next= ((i+1 < maxPending) ? i+1 : -1);
      pending->prev= ((i > 0) ? i-1 : -1);
      DEBUG("fe pendingPool: item %d next %d prev %d", i, pending->next, pending->prev);
   }

   for( int i= 0; i < server->state->base.numSVPAllocators; ++i )
   {
      server->state->base.svpAlloc[i].type= EssRMgrResType_svpAllocator;
      server->state->base.svpAlloc[i].criteriaMask= ESSRMGR_CRITERIA_MASK_SVPA;
      server->state->base.svpAlloc[i].state= EssRMgrRes_idle;
      server->state->base.svpAlloc[i].pendingNtfyIdx= -1;
   }

   maxPending= 3*server->state->base.numSVPAllocators;
   DEBUG("svpa pendingPool: max pending %d", maxPending);
   for( int i= 0; i < maxPending; ++i )
   {
      EssRMgrResourceNotify *pending= &server->state->svpaCtrl.pending[i];
      pending->type= EssRMgrResType_svpAllocator;
      pending->self= i;
      pending->next= ((i+1 < maxPending) ? i+1 : -1);
      pending->prev= ((i > 0) ? i-1 : -1);
      DEBUG("svpa pendingPool: item %d next %d prev %d", i, pending->next, pending->prev);
   }
   server->state->feCtrl.pendingPoolIdx= 0;
   server->state->feCtrl.maxPoolItems= maxPending;
}

static EssRMgrRequestInfo *essRMFindRequestByRequestIdUnlocked( EssRMgr *rm, int requestId, bool remove )
{
   EssRMgrRequestInfo *info= 0;
   for ( std::vector<EssRMgrRequestInfo*>::iterator it= rm->requests.begin(); 
          it != rm->requests.end(); ++it )
   {
      if ( (*it)->requestId == requestId )
      {
         info= (*it);
         if ( remove )
         {
            rm->requests.erase( it );
         }
         break;
      }
   }
   return info;
}

static EssRMgrRequestInfo *essRMFindRequestByRequestId( EssRMgr *rm, int requestId, bool remove )
{
   EssRMgrRequestInfo *info= 0;
   pthread_mutex_lock( &rm->mutex );
   info= essRMFindRequestByRequestIdUnlocked( rm, requestId, remove );
   pthread_mutex_unlock( &rm->mutex );
   return info;
}

static EssRMgrRequestInfo *essRMFindRequestByResourceUnlocked( EssRMgr *rm, int type, int assignedId, bool remove )
{
   EssRMgrRequestInfo *info= 0;
   for ( std::vector<EssRMgrRequestInfo*>::iterator it= rm->requests.begin(); 
          it != rm->requests.end(); ++it )
   {
      if ( ((*it)->type == type) &&
           ((*it)->assignedId == assignedId) )
      {
         info= (*it);
         if ( remove )
         {
            rm->requests.erase( it );
         }
         break;
      }
   }
   return info;
}

static EssRMgrRequestInfo *essRMFindRequestByResource( EssRMgr *rm, int type, int assignedId, bool remove )
{
   EssRMgrRequestInfo *info= 0;
   pthread_mutex_lock( &rm->mutex );
   info= essRMFindRequestByResourceUnlocked( rm, type, assignedId, remove );
   pthread_mutex_unlock( &rm->mutex );
   return info;
}

static void *essRMClientConnectionThread( void *userData )
{
   EssRMgr *rm= (EssRMgr*)userData;
   EssRMgrClientConnection *conn= rm->conn;
   struct msghdr msg;
   struct iovec iov[1];
   unsigned char mbody[4+256];
   int moff= 0, len, i, rc;

   conn->threadStarted= true;
   while( !conn->threadStopRequested )
   {
      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= 4;

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      do
      {
         len= recvmsg( conn->socketFd, &msg, 0 );
      }
      while ( (len < 0) && (errno == EINTR));

      if ( len > 0 )
      {
         unsigned char *m= mbody;
         if ( gLogLevel >= 7 )
         {
            essRMDumpMessage( mbody, len );
         }
         if ( (m[0] == 'R') && (m[1] == 'S') )
         {
            int mlen, id;
            mlen= m[2];
            id= m[3];
            if ( mlen > sizeof(mbody)-4 )
            {
               ERROR("bad server message length: %d : truncating");
               mlen= sizeof(mbody)-4;
            }
            if ( mlen > 1 )
            {
               iov[0].iov_base= (char*)mbody+4;
               iov[0].iov_len= mlen-1;

               msg.msg_name= NULL;
               msg.msg_namelen= 0;
               msg.msg_iov= iov;
               msg.msg_iovlen= 1;
               msg.msg_control= 0;
               msg.msg_controllen= 0;
               msg.msg_flags= 0;

               do
               {
                  len= recvmsg( conn->socketFd, &msg, 0 );
               }
               while ( (len < 0) && (errno == EINTR));
            }

            if ( len > 0 )
            {
               len += 4;
               if ( gLogLevel >= 7 )
               {
                  essRMDumpMessage( mbody, len );
               }
               switch( id )
               {
                  case 'R':
                     if ( mlen >= 14 )
                     {
                        bool needConfirm= false;
                        EssRMgrRequestInfo *info= 0;
                        int result= m[4];
                        int requestId= getU32( &m[5] );
                        int assignedId= getU32( &m[9] );
                        int assignedCaps= getU32( &m[13] );
                        DEBUG("got res req rsp: result %d, requestId %d assignedId %d assignedCaps %X", result,
                             requestId, assignedId, assignedCaps );
                        pthread_mutex_lock( &rm->mutex );
                        info= essRMFindRequestByRequestIdUnlocked( rm, requestId, false );
                        if ( info )
                        {
                           needConfirm= info->needConfirm;
                           info->result= result;
                           info->assignedId= assignedId;
                           info->req.assignedId= assignedId;
                           info->req.assignedCaps= assignedCaps;
                           if ( assignedId >= 0 )
                           {
                              if ( info->req.asyncEnable )
                              {
                                 essRMInvokeNotify( rm, EssRMgrEvent_granted, info );
                              }
                           }
                           sem_post( &info->semComplete );
                        }
                        else
                        {
                           ERROR("no match for requestId %d", requestId);
                        }
                        pthread_mutex_unlock( &rm->mutex );
                        if ( needConfirm )
                        {
                           essRMSemWait( &info->semConfirm, false, conn->timeoutMS );
                        }
                     }
                     break;
                  case 'V':
                     if ( mlen >= 6 )
                     {
                        EssRMgrRequestInfo *info= 0;
                        int type, assignedId;
                        type= m[4];
                        assignedId= getU32( &m[5] );
                        DEBUG("got res revoke: type %d assignedId %d", type, assignedId);
                        pthread_mutex_lock( &rm->mutex );
                        info= essRMFindRequestByResourceUnlocked( rm, type, assignedId, false );
                        if ( info )
                        {
                           essRMInvokeNotify( rm, EssRMgrEvent_revoked, info );
                        }
                        else
                        {
                           ERROR("no match for resource type %d assignedId %d", type, assignedId);
                        }
                        pthread_mutex_unlock( &rm->mutex );
                     }
                     break;
                  case 'T':
                     if ( mlen >= 18 )
                     {
                        EssRMgrRequestInfo *info= 0;
                        int valueId, requestId, value1, value2, value3;
                        requestId= getU32( &m[4] );
                        valueId= m[8];
                        value1= getU32( &m[9] );
                        value2= getU32( &m[13] );
                        value3= getU32( &m[17] );
                        DEBUG("got get value: valueId %d v1 %d v2 %d v3 %d", valueId, value1, value2, value3);
                        info= essRMFindRequestByRequestId( rm, requestId, true );
                        if ( info )
                        {
                           info->value1= value1;
                           info->value2= value2;
                           info->value3= value3;
                           sem_post( &info->semComplete );
                        }
                        else
                        {
                           if ( (requestId == -1) && (valueId == EssRMgrValue_policy_abortUnauthorized) )
                           {
                              DEBUG("set unauthorizedRequestsAbort to %d ms", value1);
                              rm->conn->unauthorizedRequestsAbort= (bool)value1;
                           }
                           else if ( (requestId == -1) && (valueId == EssRMgrValue_policy_revokeTimeout) )
                           {
                              DEBUG("set timeout to %d ms", value1);
                              rm->conn->timeoutMS= value1;
                              rm->conn->ready= true;
                           }
                           else
                           {
                              ERROR("no match for requestId %d", requestId);
                           }
                        }
                     }
                     break;
                  case 'D':
                     if ( mlen >= 6 )
                     {
                        EssRMgrRequestInfo *info= 0;
                        int requestId= getU32( &m[4] );
                        int msglen= m[8];
                        bool last= (msglen == 0);
                        info= essRMFindRequestByRequestId( rm, requestId, last );
                        if ( info )
                        {
                           if ( msglen > 0 )
                           {
                              printf("%.*s", msglen, &m[9] );
                           }
                           else
                           {
                              sem_post( &info->semComplete );
                           }
                        }
                        else
                        {
                           ERROR("no match for requestId %d", requestId);
                        }
                     }
                     break;
                  default:
                     ERROR("got unknown resource server message: mlen %d", mlen);
                     essRMDumpMessage( mbody, mlen+3 );
                     break;
               }
            }
         }
         else
         {
            ERROR("server msg bad header");
            essRMDumpMessage( mbody, len );
            len= 0;
         }
      }
      else
      {
         DEBUG("resource server disconnected");
         break;
      }
   }

   conn->threadStarted= false;
   return 0;
}

static void essRMDestroyClientConnection( EssRMgrClientConnection *conn )
{
   std::vector<EssRMgrRequestInfo*> requests;
   std::vector<EssRMgrRequestInfo*> notifications;

   if ( conn )
   {
      pthread_mutex_lock( &conn->mutex );
      conn->addr.sun_path[0]= '\0';

      if ( conn->threadStarted )
      {
         conn->threadStopRequested= true;
         shutdown( conn->socketFd, SHUT_RDWR );
         pthread_join( conn->threadId, NULL );
      }

      if ( conn->socketFd >= 0 )
      {
         close( conn->socketFd );
         conn->socketFd= -1;
      }
      pthread_mutex_unlock( &conn->mutex );

      pthread_mutex_lock( &conn->rm->mutex );
      requests.swap( conn->rm->requests );
      notifications.swap( conn->rm->notifications );
      pthread_mutex_unlock( &conn->rm->mutex );

      for ( std::vector<EssRMgrRequestInfo*>::iterator it= requests.begin();
            it != requests.end(); ++it )
      {
         EssRMgrRequestInfo* info= (*it);
         if ( info->waitForever )
         {
            sem_post( &info->semComplete );
         }
      }

      for ( std::vector<EssRMgrRequestInfo*>::iterator it= notifications.begin();
            it != notifications.end(); ++it )
      {
         EssRMgrRequestInfo* info= (*it);
         essRMSemWait( &info->semComplete, false, conn->timeoutMS );
         sem_destroy( &info->semComplete );
         free( info );
      }

      pthread_mutex_destroy( &conn->mutex );

      free( conn );
   }
}

static EssRMgrClientConnection *essRMCreateClientConnection( EssRMgr *rm )
{
   EssRMgrClientConnection *conn= 0;
   int rc;
   bool error= true;
   const char *workingDir;
   int pathNameLen, addressSize;
   bool started;

   conn= (EssRMgrClientConnection*)calloc( 1, sizeof(EssRMgrClientConnection));
   if ( conn )
   {
      rm->conn= conn;
      conn->rm= rm;
      conn->socketFd= -1;
      conn->name= ESSRMGR_SERVER_NAME;
      conn->timeoutMS= DEFAULT_TIMEOUT_MS;
      pthread_mutex_init( &conn->mutex, 0 );

      workingDir= getenv("XDG_RUNTIME_DIR");
      if ( !workingDir )
      {
         ERROR("essRMCreateClientConnection: XDG_RUNTIME_DIR is not set");
         goto exit;
      }

      pathNameLen= strlen(workingDir)+strlen("/")+strlen(conn->name)+1;
      if ( pathNameLen > (int)sizeof(conn->addr.sun_path) )
      {
         ERROR("essRMCreateClientConnection: name for server unix domain socket is too long: %d versus max %d",
                pathNameLen, (int)sizeof(conn->addr.sun_path) );
         goto exit;
      }

      conn->addr.sun_family= AF_LOCAL;
      strcpy( conn->addr.sun_path, workingDir );
      strcat( conn->addr.sun_path, "/" );
      strcat( conn->addr.sun_path, conn->name );

      conn->socketFd= socket( PF_LOCAL, SOCK_STREAM|SOCK_CLOEXEC, 0 );
      if ( conn->socketFd < 0 )
      {
         ERROR("essRMCreateClientConnection: unable to open socket: errno %d", errno );
         goto exit;
      }

      addressSize= pathNameLen + offsetof(struct sockaddr_un, sun_path);

      rc= connect(conn->socketFd, (struct sockaddr *)&conn->addr, addressSize );
      if ( rc < 0 )
      {
         ERROR("essRMCreateClientConnection: connect failed for socket: errno %d", errno );
         goto exit;
      }

      rm->conn->threadStopRequested= false;
      rc= pthread_create( &conn->threadId, NULL, essRMClientConnectionThread, rm );
      if ( rc )
      {
         ERROR("unable to start client connection thread: rc %d", rc);
         goto exit;
      }

      error= false;

      started= false;
      for( ; ; )
      {
         if ( rm->conn->threadStarted )
         {
            started= true;
         }
         if ( !rm->conn->threadStarted && started )
         {
            error= true;
            break;
         }
         if ( rm->conn->ready )
         {
            DEBUG("connection ready");
            break;
         }
         usleep( 10000 );
      }
   }

exit:

   if ( error )
   {
      essRMDestroyClientConnection( conn );
      rm->conn= 0;
      conn= 0;
   }

   return conn;
}

static bool essRMWaitResponseClientConnection( EssRMgrClientConnection *conn, int timeoutMS, EssRMgrRequestInfo *info )
{
   int retry= timeoutMS;
   return essRMSemWait( &info->semComplete, info->waitForever, retry );
}

static bool essRMSendResRequestClientConnection( EssRMgrClientConnection *conn, EssRMgrRequestInfo *info )
{
   bool result= false;
   if ( conn )
   {
      EssRMgrRequest *req= &info->req;
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64+ESSRMGR_MAX_APPIDLEN];
      int len, infolen;
      int sentLen;
      const char *appId= getenv("ESSRMGR_APPID");
      int appIdLen= 0;

      if ( !appId )
      {
         appId= getenv("CLIENT_IDENTIFIER");
      }
      if ( appId )
      {
         appIdLen= strlen(appId);
         if ( appIdLen > ESSRMGR_MAX_APPIDLEN )
         {
            appIdLen= ESSRMGR_MAX_APPIDLEN;
         }
      }

      pthread_mutex_lock( &conn->mutex );

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      infolen= 0;
      if ( req->type == EssRMgrResType_videoDecoder )
      {
         infolen= 2*sizeof(int);
      }
      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'R';
      mbody[len++]= (req->type&0xFF);
      mbody[len++]= (req->asyncEnable&0xFF);
      len += putU32( &mbody[len], info->requestId );
      len += putU32( &mbody[len], req->usage );
      len += putU32( &mbody[len], req->priority );
      len += putU32( &mbody[len], appIdLen );
      if ( appIdLen )
      {
         memcpy( &mbody[len], appId, appIdLen );
         len += appIdLen;
      }
      len += putU32( &mbody[len], infolen );
      if ( req->type == EssRMgrResType_videoDecoder )
      {
         len += putU32( &mbody[len], req->info.video.maxWidth );
         len += putU32( &mbody[len], req->info.video.maxHeight );
      }
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendResRequestClientConnection: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent res request: type %d to resource server", req->type);
      }

      pthread_mutex_unlock( &conn->mutex );
   }
   return result;
}

static bool essRMSendResReleaseClientConnection( EssRMgrClientConnection *conn, EssRMgrRequestInfo *info )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len;
      int sentLen;

      pthread_mutex_lock( &conn->mutex );

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'L';
      mbody[len++]= (info->type&0xFF);
      len += putU32( &mbody[len], info->assignedId );
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendResReleaseClientConnection: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent res releaase: type %d assignedId %d to resource server", info->type, info->assignedId);
      }

      pthread_mutex_unlock( &conn->mutex );
   }
   return result;
}

static bool essRMSendResSetStateClientConnection( EssRMgrClientConnection *conn, EssRMgrRequestInfo *info, int state )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len;
      int sentLen;

      pthread_mutex_lock( &conn->mutex );

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'S';
      mbody[len++]= (info->type&0xFF);
      len += putU32( &mbody[len], info->assignedId );
      mbody[len++]= (state&0xFF);
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendResSetStateClientConnection: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent res set state: type %d assignedId %d state %d to resource server", info->type, info->assignedId, state);
      }

      pthread_mutex_unlock( &conn->mutex );
   }
   return result;
}

static bool essRMSendSetPriorityClientConnection( EssRMgrClientConnection *conn, EssRMgrRequestInfo *info )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len;
      int sentLen;

      pthread_mutex_lock( &conn->mutex );

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'P';
      mbody[len++]= (info->type&0xFF);
      len += putU32( &mbody[len], info->requestId );
      len += putU32( &mbody[len], info->req.priority );
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendSetPriorityClientConnection: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent set priority: type %d requestId %d to resource server", info->type, info->req.requestId);
      }

      pthread_mutex_unlock( &conn->mutex );
   }
   return result;
}

static bool essRMSendSetUsageClientConnection( EssRMgrClientConnection *conn, EssRMgrRequestInfo *info )
{
   bool result= false;
   if ( conn )
   {
      EssRMgrRequest *req= &info->req;
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len, infolen;
      int sentLen;

      pthread_mutex_lock( &conn->mutex );

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      infolen= 0;
      if ( req->type == EssRMgrResType_videoDecoder )
      {
         infolen= 2*sizeof(int);
      }
      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'U';
      mbody[len++]= (info->type&0xFF);
      len += putU32( &mbody[len], info->requestId );
      len += putU32( &mbody[len], req->usage );
      len += putU32( &mbody[len], infolen );
      if ( req->type == EssRMgrResType_videoDecoder )
      {
         len += putU32( &mbody[len], req->info.video.maxWidth );
         len += putU32( &mbody[len], req->info.video.maxHeight );
      }
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendSetUsageClientConnection: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent set usage: type %d requestId %d to resource server", info->type, info->req.requestId);
      }

      pthread_mutex_unlock( &conn->mutex );
   }
   return result;
}

static bool essRMSendCancelClientConnection( EssRMgrClientConnection *conn, EssRMgrRequestInfo *info )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len;
      int sentLen;

      pthread_mutex_lock( &conn->mutex );

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'C';
      mbody[len++]= (info->type&0xFF);
      len += putU32( &mbody[len], info->requestId );
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendCancelClientConnection: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent cancel: type %d requestId %d to resource server", info->type, info->req.requestId);
      }

      pthread_mutex_unlock( &conn->mutex );
   }
   return result;
}

static bool essRMSendBlackListClientConnection( EssRMgrClientConnection *conn, const char *appId, bool add )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[20+ESSRMGR_MAX_APPIDLEN];
      int len;
      int sentLen;
      int appIdLen= 0;

      if ( appId )
      {
         appIdLen= strlen(appId);
         if ( appIdLen > ESSRMGR_MAX_APPIDLEN )
         {
            appIdLen= ESSRMGR_MAX_APPIDLEN;
         }
      }

      if ( appIdLen > 0 )
      {
         pthread_mutex_lock( &conn->mutex );

         msg.msg_name= NULL;
         msg.msg_namelen= 0;
         msg.msg_iov= iov;
         msg.msg_iovlen= 1;
         msg.msg_control= 0;
         msg.msg_controllen= 0;
         msg.msg_flags= 0;

         len= 0;
         mbody[len++]= 'R';
         mbody[len++]= 'S';
         mbody[len++]= 0;
         mbody[len++]= 'B';
         mbody[len++]= (add ? 1 : 0);
         len += putU32( &mbody[len], appIdLen );
         if ( appIdLen )
         {
            memcpy( &mbody[len], appId, appIdLen );
            len += appIdLen;
         }
         if( len > sizeof(mbody) )
         {
            ERROR("essRMSendAddBLClientConnection: msg too big");
         }
         mbody[2]= (len-3);

         iov[0].iov_base= (char*)mbody;
         iov[0].iov_len= len;

         do
         {
            sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
            TRACE1("sentLen %d len %d", sentLen, len);
         }
         while ( (sentLen < 0) && (errno == EINTR));

         if ( sentLen == len )
         {
            result= true;
            DEBUG("sent add BL: appId (%s) to resource server", appId);
         }

         pthread_mutex_unlock( &conn->mutex );
      }
   }

   return result;
}

static bool essRMSendGetValueClientConnection( EssRMgrClientConnection *conn, EssRMgrRequestInfo *info, int valueId )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len;
      int sentLen;

      pthread_mutex_lock( &conn->mutex );

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'T';
      len += putU32( &mbody[len], info->requestId );
      mbody[len++]= valueId;
      switch( valueId )
      {
         case EssRMgrValue_count:
            mbody[len++]= (info->type&0xFF);
            break;
         case EssRMgrValue_owner:
            mbody[len++]= (info->type&0xFF);
            len += putU32( &mbody[len], info->assignedId );
            break;
         case EssRMgrValue_caps:
            mbody[len++]= (info->type&0xFF);
            len += putU32( &mbody[len], info->assignedId );
            break;
         case EssRMgrValue_state:
            mbody[len++]= (info->type&0xFF);
            len += putU32( &mbody[len], info->assignedId );
            break;
         case EssRMgrValue_aggregateState:
         case EssRMgrValue_policy_tie:
            break;
         default:
            ERROR("bad value id: %d", valueId);
            break;
      }
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendGetValueClientConnection: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent get value: valueId %d requestId %d to resource server", valueId, info->requestId);
      }

      pthread_mutex_unlock( &conn->mutex );
   }
   return result;
}

static bool essRMSendDumpStateClientConnection( EssRMgrClientConnection *conn, EssRMgrRequestInfo *info )
{
   bool result= false;
   if ( conn )
   {
      struct msghdr msg;
      struct iovec iov[1];
      unsigned char mbody[64];
      int len;
      int sentLen;

      pthread_mutex_lock( &conn->mutex );

      msg.msg_name= NULL;
      msg.msg_namelen= 0;
      msg.msg_iov= iov;
      msg.msg_iovlen= 1;
      msg.msg_control= 0;
      msg.msg_controllen= 0;
      msg.msg_flags= 0;

      len= 0;
      mbody[len++]= 'R';
      mbody[len++]= 'S';
      mbody[len++]= 0;
      mbody[len++]= 'D';
      len += putU32( &mbody[len], info->requestId );
      if( len > sizeof(mbody) )
      {
         ERROR("essRMSendDumpStateClientConnection: msg too big");
      }
      mbody[2]= (len-3);

      iov[0].iov_base= (char*)mbody;
      iov[0].iov_len= len;

      do
      {
         sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
         TRACE1("sentLen %d len %d", sentLen, len);
      }
      while ( (sentLen < 0) && (errno == EINTR));

      if ( sentLen == len )
      {
         result= true;
         DEBUG("sent dump state to resource server");
      }

      pthread_mutex_unlock( &conn->mutex );
   }
   return result;
}

static bool essRMSemWait( sem_t *sem, bool waitForever, int retry )
{
   bool result= false;
   int rc;
   int timeout= retry;

   for( ; ; )
   {
      rc= sem_trywait( sem );
      if ( rc == 0 )
      {
         DEBUG("request completed" );
         result= true;
         break;
      }
      if ( !waitForever )
      {
         if ( --retry == 0 )
         {
            INFO("request timeout: timeout %d ms", timeout );
            break;
         }
      }
      usleep( 1000 );
   }

   return result;
}

bool EssRMgrInit()
{
   bool result= false;

   char *env= getenv( "ESSRMGR_DEBUG" );
   if ( env )
   {
      gLogLevel= atoi( env );
   }

   gCtx= (EssRMgrResourceServerCtx*)calloc( 1, sizeof(EssRMgrResourceServerCtx) );
   if ( gCtx )
   {
      gCtx->state= (EssRMgrState*)calloc( 1, sizeof(EssRMgrState) );
      if ( gCtx->state )
      {
         result= essRMInitResourceServer( gCtx );
      }
      else
      {
         ERROR("No memory for EssRMgrState");
      }
   }
   else
   {
      ERROR("No memory for EssRMgrResourceServerCtx");
   }

   return result;
}

void EssRMgrTerm()
{
   if ( gCtx )
   {
      essRMTermResourceServer( gCtx );
      gCtx= 0;
   }
}

EssRMgr* EssRMgrCreate()
{
   EssRMgr *rm= 0;
   bool error= true;

   char *env= getenv( "ESSRMGR_DEBUG" );
   if ( env )
   {
      gLogLevel= atoi( env );
   }

   rm= (EssRMgr*)calloc( 1, sizeof(EssRMgr) );
   if ( rm )
   {
      pthread_mutex_init( &rm->mutex, 0 );
      rm->conn= essRMCreateClientConnection( rm );
      if ( !rm->conn )
      {
         ERROR("essRMCreateClientConnection failed to create connection");
         goto exit;
      }
      rm->requests= std::vector<EssRMgrRequestInfo*>();
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
      if ( rm->conn )
      {
         EssRMgrClientConnection *conn= rm->conn;
         rm->conn= 0;
         essRMDestroyClientConnection( conn );
      }

      pthread_mutex_destroy( &rm->mutex );
      free( rm );
   }
}

bool EssRMgrGetPolicyPriorityTie( EssRMgr *rm )
{
   EssRMgrRequestInfo *info= 0;
   bool value= false;
   int rc;
   bool result;
   int timeoutMS;

   if ( rm && rm->conn )
   {
      pthread_mutex_lock( &rm->mutex );
      if ( !rm->conn )
      {
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info= (EssRMgrRequestInfo*)calloc( 1, sizeof(EssRMgrRequestInfo));
      if ( !info )
      {
         ERROR("no memory for request");
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      rc= sem_init( &info->semComplete, 0, 0 );
      if ( rc )
      {
         ERROR("failed to create semComplete for request: %d error %d", rc, errno);
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info->waitForever= true;
      info->requestId= rm->nextRequestId++;
      rm->requests.push_back( info );
      timeoutMS= rm->conn->timeoutMS;

      result= essRMSendGetValueClientConnection( rm->conn, info, EssRMgrValue_policy_tie );
      pthread_mutex_unlock( &rm->mutex );
      if ( result )
      {
         essRMWaitResponseClientConnection( rm->conn, timeoutMS, info );

         value= info->value1;
      }
   }

exit:
   if ( info )
   {
      sem_destroy( &info->semComplete );
      free( info );
   }

   return value;
}

bool EssRMgrGetAVState( EssRMgr *rm, int *state )
{
   bool result= false;
   EssRMgrRequestInfo *info= 0;
   int rc;
   int timeoutMS;

   if ( rm && rm->conn )
   {
      pthread_mutex_lock( &rm->mutex );
      if ( !rm->conn )
      {
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info= (EssRMgrRequestInfo*)calloc( 1, sizeof(EssRMgrRequestInfo));
      if ( !info )
      {
         ERROR("no memory for request");
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      rc= sem_init( &info->semComplete, 0, 0 );
      if ( rc )
      {
         ERROR("failed to create semComplete for request: %d error %d", rc, errno);
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info->waitForever= true;
      info->requestId= rm->nextRequestId++;
      rm->requests.push_back( info );
      timeoutMS= rm->conn->timeoutMS;

      result= essRMSendGetValueClientConnection( rm->conn, info, EssRMgrValue_aggregateState );
      pthread_mutex_unlock( &rm->mutex );
      if ( result )
      {
         essRMWaitResponseClientConnection( rm->conn, timeoutMS, info );

         *state= info->value1;

         result= true;
      }
   }

exit:
   if ( info )
   {
      sem_destroy( &info->semComplete );
      free( info );
   }

   return result;
}

int EssRMgrResourceGetCount( EssRMgr *rm, int type )
{
   EssRMgrRequestInfo *info= 0;
   int count= 0;
   int rc;
   bool result;
   int timeoutMS;

   if ( rm && rm->conn )
   {
      pthread_mutex_lock( &rm->mutex );
      if ( !rm->conn )
      {
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info= (EssRMgrRequestInfo*)calloc( 1, sizeof(EssRMgrRequestInfo));
      if ( !info )
      {
         ERROR("no memory for request");
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      rc= sem_init( &info->semComplete, 0, 0 );
      if ( rc )
      {
         ERROR("failed to create semComplete for request: %d error %d", rc, errno);
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info->waitForever= true;
      info->type= type;
      info->requestId= rm->nextRequestId++;
      rm->requests.push_back( info );
      timeoutMS= rm->conn->timeoutMS;

      result= essRMSendGetValueClientConnection( rm->conn, info, EssRMgrValue_count );
      pthread_mutex_unlock( &rm->mutex );
      if ( result  )
      {
         essRMWaitResponseClientConnection( rm->conn, timeoutMS, info );

         count= info->value1;
      }
   }

exit:
   if ( info )
   {
      sem_destroy( &info->semComplete );
      free( info );
   }

   return count;
}

bool EssRMgrResourceGetOwner( EssRMgr *rm, int type, int id, int *client, int *priority )
{
   bool result= false;
   EssRMgrRequestInfo *info= 0;
   int rc;
   int timeoutMS;

   if ( rm && rm->conn )
   {
      pthread_mutex_lock( &rm->mutex );
      if ( !rm->conn )
      {
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info= (EssRMgrRequestInfo*)calloc( 1, sizeof(EssRMgrRequestInfo));
      if ( !info )
      {
         ERROR("no memory for request");
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      rc= sem_init( &info->semComplete, 0, 0 );
      if ( rc )
      {
         ERROR("failed to create semComplete for request: %d error %d", rc, errno);
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info->waitForever= true;
      info->type= type;
      info->assignedId= id;
      info->requestId= rm->nextRequestId++;
      rm->requests.push_back( info );
      timeoutMS= rm->conn->timeoutMS;

      result= essRMSendGetValueClientConnection( rm->conn, info, EssRMgrValue_owner );
      pthread_mutex_unlock( &rm->mutex );
      if ( result )
      {
         essRMWaitResponseClientConnection( rm->conn, timeoutMS, info );

         if ( client ) *client= info->value1;
         if ( priority ) *priority= info->value2;

         result= true;
      }
   }

exit:
   if ( info )
   {
      sem_destroy( &info->semComplete );
      free( info );
   }

   return result;
}

bool EssRMgrResourceGetCaps( EssRMgr *rm, int type, int id, EssRMgrCaps *caps )
{
   bool result= false;
   EssRMgrRequestInfo *info= 0;
   int rc;
   int timeoutMS;

   if ( rm && rm->conn )
   {
      pthread_mutex_lock( &rm->mutex );
      if ( !rm->conn )
      {
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info= (EssRMgrRequestInfo*)calloc( 1, sizeof(EssRMgrRequestInfo));
      if ( !info )
      {
         ERROR("no memory for request");
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      rc= sem_init( &info->semComplete, 0, 0 );
      if ( rc )
      {
         ERROR("failed to create semComplete for request: %d error %d", rc, errno);
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info->waitForever= true;
      info->type= type;
      info->assignedId= id;
      info->requestId= rm->nextRequestId++;
      rm->requests.push_back( info );
      timeoutMS= rm->conn->timeoutMS;

      result= essRMSendGetValueClientConnection( rm->conn, info, EssRMgrValue_caps );
      pthread_mutex_unlock( &rm->mutex );
      if ( result )
      {
         essRMWaitResponseClientConnection( rm->conn, timeoutMS, info );

         if ( caps )
         {
            caps->capabilities= info->value1;
            switch( type )
            {
               case EssRMgrResType_videoDecoder:
                  caps->info.video.maxWidth= info->value2;
                  caps->info.video.maxHeight= info->value3;
                  break;
               case EssRMgrResType_audioDecoder:
               case EssRMgrResType_frontEnd:
               case EssRMgrResType_svpAllocator:
                  break;
               default:
                  ERROR("Bad resource type: %d", type);
                  break;
            }
         }
         result= true;
      }
   }

exit:
   if ( info )
   {
      sem_destroy( &info->semComplete );
      free( info );
   }

   return result;
}

bool EssRMgrResourceGetState( EssRMgr *rm, int type, int id, int *state )
{
   bool result= false;
   EssRMgrRequestInfo *info= 0;
   int rc;
   int timeoutMS;

   if ( rm && rm->conn )
   {
      pthread_mutex_lock( &rm->mutex );
      if ( !rm->conn )
      {
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info= (EssRMgrRequestInfo*)calloc( 1, sizeof(EssRMgrRequestInfo));
      if ( !info )
      {
         ERROR("no memory for request");
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      rc= sem_init( &info->semComplete, 0, 0 );
      if ( rc )
      {
         ERROR("failed to create semComplete for request: %d error %d", rc, errno);
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info->waitForever= true;
      info->type= type;
      info->assignedId= id;
      info->requestId= rm->nextRequestId++;
      rm->requests.push_back( info );
      timeoutMS= rm->conn->timeoutMS;

      result= essRMSendGetValueClientConnection( rm->conn, info, EssRMgrValue_state );
      pthread_mutex_unlock( &rm->mutex );
      if ( result  )
      {
         essRMWaitResponseClientConnection( rm->conn, timeoutMS, info );

         if ( state )
         {
            *state= info->value1;
         }
         result= true;
      }
   }

exit:
   if ( info )
   {
      sem_destroy( &info->semComplete );
      free( info );
   }

   return result;
}

bool EssRMgrResourceSetState( EssRMgr *rm, int type, int id, int state )
{
   bool result= false;
   if ( rm && rm->conn )
   {
      EssRMgrRequestInfo *info= 0;
      info= essRMFindRequestByResource( rm, type, id, false );
      if ( info )
      {
         switch( state )
         {
            case EssRMgrRes_idle:
            case EssRMgrRes_paused:
            case EssRMgrRes_active:
               result= essRMSendResSetStateClientConnection( rm->conn, info, state );
               break;
            default:
               ERROR("bad state value %d", state);
               break;
         }
      }
      else
      {
         ERROR("no match for resource type %d id %d", type, id);
      }
   }

   return result;
}

bool EssRMgrRequestResource( EssRMgr *rm, int type, EssRMgrRequest *req )
{
   bool result= false;
   EssRMgrRequestInfo *info= 0;
   int rc;
   int timeoutMS;

   TRACE2("EssRMgrRequestResource: enter: rm %p type %d", rm, type );

   if ( rm && rm->conn && req )
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

      info= (EssRMgrRequestInfo*)calloc( 1, sizeof(EssRMgrRequestInfo));
      if ( !info )
      {
         ERROR("no memory for request");
         goto exit;
      }

      rc= sem_init( &info->semComplete, 0, 0 );
      if ( rc )
      {
         ERROR("failed to create semComplete for request: %d error %d", rc, errno);
         goto exit;
      }

      rc= sem_init( &info->semConfirm, 0, 0 );
      if ( rc )
      {
         ERROR("failed to create semComplete for request: %d error %d", rc, errno);
         goto exit;
      }
      info->needConfirm= true;

      pthread_mutex_lock( &rm->mutex );
      req->requestId= rm->nextRequestId++;

      info->type= type;
      info->requestId= req->requestId;
      memcpy(&info->req, req, sizeof(EssRMgrRequest));

      rm->requests.push_back( info );
      timeoutMS= rm->conn->timeoutMS;
      pthread_mutex_unlock( &rm->mutex );

      result= essRMSendResRequestClientConnection( rm->conn, info );
      if ( result )
      {
         result= essRMWaitResponseClientConnection( rm->conn, timeoutMS, info );
         if ( result )
         {
            pthread_mutex_lock( &rm->mutex );
            for ( std::vector<EssRMgrRequestInfo*>::iterator it= rm->requests.begin();
                  it != rm->requests.end(); ++it )
            {
               if ( (*it) == info )
               {
                  memcpy(req, &info->req, sizeof(EssRMgrRequest));
                  break;
               }
            }
            pthread_mutex_unlock( &rm->mutex );
            info->needConfirm= false;
            sem_post( &info->semConfirm );
            if ( info->result == ESSRMGR_NOT_AUTHORIZED )
            {
               if ( rm->conn->unauthorizedRequestsAbort )
               {
                  ERROR("erm resource request denied: not authorized: aborting");
                  assert( false );
               }
               ERROR("erm resource request denied: not authorized");
               result= false;
            }
         }
         info= 0;
      }
   }

exit:

   if ( info )
   {
      pthread_mutex_lock( &rm->mutex );
      for ( std::vector<EssRMgrRequestInfo*>::iterator it= rm->requests.begin(); 
             it != rm->requests.end(); ++it )
      {
         if ( (*it) == info )
         {
            rm->requests.erase(it);
            break;
         }
         sem_destroy( &info->semComplete );
      }
      pthread_mutex_unlock( &rm->mutex );
      free( info );
   }

   TRACE2("EssRMgrRequestResource: exit: rm %p type %d result %d", rm, type, result );

   return result;
}

void EssRMgrReleaseResource( EssRMgr *rm, int type, int id )
{
   TRACE2("EssRMReleaseResource: enter: rm %p type %d id %d", rm, type, id);

   if ( rm && rm->conn )
   {
      EssRMgrRequestInfo *info= 0;
      info= essRMFindRequestByResource( rm, type, id, true );
      if ( info )
      {
         essRMSendResReleaseClientConnection( rm->conn, info );
         sem_destroy( &info->semComplete );
         sem_destroy( &info->semConfirm );
         free( info );
      }
      else
      {
         ERROR("no match for resource type %d id %d", type, id);
      }
   }

   TRACE2("EssRMReleaseResource: exit: rm %p type %d id %d", rm, type, id);
}

bool EssRMgrRequestSetPriority( EssRMgr *rm, int type, int requestId, int priority )
{
   bool result= false;

   if ( rm && rm->conn )
   {
      EssRMgrRequestInfo *info= 0;
      info= essRMFindRequestByRequestId( rm, requestId, false );
      if ( info )
      {
        if ( (info->type == type) && (info->req.type == type) )
         {
            info->req.priority= priority;
            result= essRMSendSetPriorityClientConnection( rm->conn, info );
         }
         else
         {
            ERROR("bad request: type %d req %p", info->type, info->req);
         }
      }
      else
      {
         ERROR("no match for requestId %d", requestId);
      }
   }

   return result;
}

bool EssRMgrRequestSetUsage( EssRMgr *rm, int type, int requestId, EssRMgrUsage *usage )
{
   bool result= false;

   if ( rm && rm->conn )
   {
      EssRMgrRequestInfo *info= 0;
      info= essRMFindRequestByRequestId( rm, requestId, false );
      if ( info )
      {
         if ( (info->type == type) && (info->req.type == type) )
         {
            info->req.usage= usage->usage;
            info->req.info= usage->info;
            result= essRMSendSetUsageClientConnection( rm->conn, info );
         }
         else
         {
            ERROR("bad request: type %d req %p", info->type, info->req);
         }
      }
      else
      {
         ERROR("no match for requestId %d", requestId);
      }
   }

   return result;
}

void EssRMgrRequestCancel( EssRMgr *rm, int type, int requestId )
{
   if ( rm && rm->conn )
   {
      EssRMgrRequestInfo *info= 0;
      info= essRMFindRequestByRequestId( rm, requestId, false );
      if ( info )
      {
         if ( (info->type == type) && (info->req.type == type) )
         {
            essRMSendCancelClientConnection( rm->conn, info );
         }
         else
         {
            ERROR("bad request: type %d req %p", info->type, info->req);
         }
      }
      else
      {
         ERROR("no match for requestId %d", requestId);
      }
   }
}

bool EssRMgrAddToBlackList( EssRMgr *rm, const char *appId )
{
   bool result= false;

   if ( rm && rm->conn )
   {
      result= essRMSendBlackListClientConnection( rm->conn, appId, true );
   }

   return result;
}

bool EssRMgrRemoveFromBlackList( EssRMgr *rm, const char *appId )
{
   bool result= false;

   if ( rm && rm->conn )
   {
      result= essRMSendBlackListClientConnection( rm->conn, appId, false );
   }

   return result;
}

void EssRMgrDumpState( EssRMgr *rm )
{
   EssRMgrRequestInfo *info= 0;
   int rc;
   bool result;
   int timeoutMS;

   if ( rm && rm->conn )
   {
      pthread_mutex_lock( &rm->mutex );
      if ( !rm->conn )
      {
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info= (EssRMgrRequestInfo*)calloc( 1, sizeof(EssRMgrRequestInfo));
      if ( !info )
      {
         ERROR("no memory for request");
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      rc= sem_init( &info->semComplete, 0, 0 );
      if ( rc )
      {
         ERROR("failed to create semComplete for request: %d error %d", rc, errno);
         pthread_mutex_unlock( &rm->mutex );
         goto exit;
      }

      info->waitForever= true;
      info->requestId= rm->nextRequestId++;
      rm->requests.push_back( info );
      timeoutMS= rm->conn->timeoutMS;

      result= essRMSendDumpStateClientConnection( rm->conn, info );
      pthread_mutex_unlock( &rm->mutex );
      if ( result )
      {
         essRMWaitResponseClientConnection( rm->conn, timeoutMS, info );
      }
      info= 0;
   }

exit:
   if ( info )
   {
      free( info );
   }
}

static bool essRMgrAppIdAuthorized( EssRMgrResourceConnection *conn, char *appId )
{
   bool result= true;
   if ( conn && appId )
   {
      EssRMgrResourceServerCtx *server= conn->server;
      if ( server->blackList.size() )
      {
         std::map<std::string,int>::iterator it= server->blackList.find( appId );
         if ( it != server->blackList.end() )
         {
            result= false;
         }
      }
   }
   DEBUG("check appid(%s), authorized: %d", appId, result);
   return result;
}

static void essRMgrRevokeAll( EssRMgrResourceConnection *conn, char *appId )
{
   EssRMgrState *state= conn->server->state;

   DEBUG("start revoke all resources for appId (%s)", appId);
   for( int i= 0; i < state->base.numVideoDecoders; ++i )
   {
      if ( state->base.videoDecoder[i].connOwner &&
           !strcmp( state->base.videoDecoder[i].connOwner->appId, appId ) )
      {
         DEBUG("revoke video %d", i);
         essRMRevokeResource( conn, state->base.videoDecoder[i].type, i, false );
      }
   }
   for( int i= 0; i < state->base.numAudioDecoders; ++i )
   {
      if ( state->base.audioDecoder[i].connOwner &&
           !strcmp( state->base.audioDecoder[i].connOwner->appId, appId ) )
      {
         DEBUG("revoke audio %d", i);
         essRMRevokeResource( conn, state->base.audioDecoder[i].type, i, false );
      }
   }
   for( int i= 0; i < state->base.numFrontEnds; ++i )
   {
      if ( state->base.frontEnd[i].connOwner &&
           !strcmp( state->base.frontEnd[i].connOwner->appId, appId ) )
      {
         DEBUG("revoke front end %d", i);
         essRMRevokeResource( conn, state->base.videoDecoder[i].type, i, false );
      }
   }
   for( int i= 0; i < state->base.numSVPAllocators; ++i )
   {
      if ( state->base.svpAlloc[i].connOwner &&
           !strcmp( state->base.svpAlloc[i].connOwner->appId, appId ) )
      {
         DEBUG("revoke svpa %d", i);
         essRMRevokeResource( conn, state->base.videoDecoder[i].type, i, false );
      }
   }
   DEBUG("done revoke all resources for appId (%s)", appId);
}

static bool essRMgrUpdateBlackList( EssRMgrResourceConnection *conn, char *appId, bool add )
{
   bool result= false;
   if ( conn && appId )
   {
      EssRMgrResourceServerCtx *server= conn->server;
      std::map<std::string,int>::iterator it= server->blackList.find( appId );
      if ( add )
      {
         if ( it == server->blackList.end() )
         {
            server->blackList.insert( std::pair<std::string,int>( appId, 0 ) );
            essRMgrRevokeAll( conn, appId );
            result= true;
         }
      }
      else
      {
         if ( it != server->blackList.end() )
         {
            server->blackList.erase( it );
            result= true;
         }
      }
   }
   return result;
}

static bool essRMRequestResource( EssRMgrResourceConnection *conn, EssRMgrRequest *req )
{
   bool result= false;
   bool madeAssignment= false;
   int rc;

   TRACE2("essRMgrRequestResource: enter: conn %p requestId %d", conn, req->requestId );

   if ( conn && req )
   {
      int assignIdx= -1, pendingIdx= -1;
      EssRMgrResourceServerCtx *server= conn->server;
      EssRMgrUsage usage;
      EssRMgrResource *res= 0;
      switch( req->type )
      {
         case EssRMgrResType_videoDecoder:
            res= server->state->base.videoDecoder;
            break;
         case EssRMgrResType_audioDecoder:
            res= server->state->base.audioDecoder;
            break;
         case EssRMgrResType_frontEnd:
            res= server->state->base.frontEnd;
            break;
         case EssRMgrResType_svpAllocator:
            res= server->state->base.svpAlloc;
            break;
         default:
            ERROR("Bad resource type: %d", req->type);
            break;
      }
      
      if ( res )
      {
         usage.usage= req->usage;
         usage.info= req->info;
         assignIdx= essRMFindSuitableResource( conn, req->type, req->priority, &usage, pendingIdx );

         if ( assignIdx >= 0 )
         {
            pthread_t threadId;

            if ( res[assignIdx].connOwner != 0 )
            {
               if ( !essRMRevokeResource( conn, req->type, assignIdx, true ) )
               {
                  ERROR("failed to revoke resource type %d id %d", req->type, assignIdx);
                  req->assignedId= -1;
                  goto exit;
               }
            }

            if ( essRMAssignResource( conn, assignIdx, req ) )
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
         }
         else if ( (pendingIdx >= 0 ) && req->asyncEnable )
         {
            EssRMgrResourceNotify *pending= essRMGetPendingPoolItem( conn, req->type );
            if ( pending )
            {
               DEBUG("request %d entering pending state for res type %d id %d conn %p", req->type, req->requestId, pendingIdx, conn );
               pending->notify.needNotification= true;
               pending->notify.server= conn->server;
               pending->notify.notifyCB= req->notifyCB;
               pending->notify.notifyUserData= req->notifyUserData;
               pending->notify.event= EssRMgrEvent_granted;
               pending->notify.type= req->type;
               pending->notify.priority= req->priority;
               pending->notify.resourceIdx= pendingIdx;
               pending->notify.req= *req;
               pending->connUser= conn;
               pending->priorityUser= req->priority;

               essRMInsertPendingByPriority( conn, pendingIdx, pending );

               req->assignedId= -1;
               
               result= true;
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

static void essRMReleaseResource( EssRMgrResourceConnection *conn, int type, int id )
{
   if ( conn )
   {
      EssRMgrResourceServerCtx *server= conn->server;
      int maxId= 0;
      EssRMgrResource *res= 0;
      EssRMgrResourceControl *ctrl= 0;
      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            maxId= server->state->base.numVideoDecoders;
            res= server->state->base.videoDecoder;
            ctrl= &server->state->vidCtrl;
            break;
         case EssRMgrResType_audioDecoder:
            maxId= server->state->base.numAudioDecoders;
            res= server->state->base.audioDecoder;
            ctrl= &server->state->audCtrl;
            break;
         case EssRMgrResType_frontEnd:
            maxId= server->state->base.numFrontEnds;
            res= server->state->base.frontEnd;
            ctrl= &server->state->feCtrl;
            break;
         case EssRMgrResType_svpAllocator:
            maxId= server->state->base.numSVPAllocators;
            res= server->state->base.svpAlloc;
            ctrl= &server->state->svpaCtrl;
            break;
         default:
            ERROR("Bad resource type: %d", type);
            break;
      }
      if ( res && ctrl )
      {
         if ( (id >= 0) && (id < maxId) )
         {
            if ( conn == res[id].connOwner )
            {
               DEBUG("conn %p releasing res type %d id %d", conn, type, id);
               res[id].requestIdOwner= -1;
               res[id].connOwner= 0;
               res[id].priorityOwner= 0;
               res[id].usageOwner= 0;
               res[id].state= EssRMgrRes_idle;

               if ( res[id].pendingNtfyIdx >= 0 )
               {
                  EssRMgrResourceNotify *pending= &ctrl->pending[res[id].pendingNtfyIdx];
                  res[id].pendingNtfyIdx= pending->next;
                  if ( pending->next >= 0 )
                  {
                     ctrl->pending[pending->next].prev= -1;
                  }

                  if ( essRMAssignResource( pending->connUser, id, &pending->notify.req ) )
                  {
                     essRMTransferResource( pending->connUser, pending );
                  }
               }
            }
            else if ( res[id].connOwner )
            {
               ERROR("client %d attempting to release res type %d id %d owned by client %d",
                      conn->clientId, type, id, res[id].connOwner->clientId );
            }
            else
            {
               ERROR("client %d attempting to release unowned res type %d id %d",
                      conn->clientId, type, id );
            }
         }
      }
   }
}

static bool essRMSetPriorityResource( EssRMgrResourceConnection *conn, int requestId, int type, int priority )
{
   bool result= false;

   if ( conn )
   {
      EssRMgrResourceServerCtx *server= conn->server;
      int maxId= 0;
      EssRMgrResource *res= 0;
      EssRMgrResourceControl *ctrl= 0;
      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            maxId= server->state->base.numVideoDecoders;
            res= server->state->base.videoDecoder;
            ctrl= &server->state->vidCtrl;
            break;
         case EssRMgrResType_audioDecoder:
            maxId= server->state->base.numAudioDecoders;
            res= server->state->base.audioDecoder;
            ctrl= &server->state->audCtrl;
            break;
         case EssRMgrResType_frontEnd:
            maxId= server->state->base.numFrontEnds;
            res= server->state->base.frontEnd;
            ctrl= &server->state->feCtrl;
            break;
         case EssRMgrResType_svpAllocator:
            maxId= server->state->base.numSVPAllocators;
            res= server->state->base.svpAlloc;
            ctrl= &server->state->svpaCtrl;
            break;
         default:
            ERROR("Bad resource type: %d", type);
            break;
      }
      if ( res && ctrl && (requestId >= 0) )
      {
         int id;
         bool found= false;
         int pendingNtfyIdx= -1;
         for( id= 0; id < maxId; ++id )
         {
            if ( (res[id].requestIdOwner == requestId) &&
                 (res[id].connOwner == conn) )
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
               }
            }
            else
            {
               pendingNtfyIdx= res[id].pendingNtfyIdx;
               while ( pendingNtfyIdx >= 0 )
               {
                  EssRMgrResourceNotify *pending= &ctrl->pending[pendingNtfyIdx];
                  if ( (pending->notify.req.requestId == requestId) && (pending->connUser == conn) )
                  {
                     DEBUG("found request %d in res type %d id %d pending list: change priority from %d to %d owner priority %d",
                           requestId, type, id, pending->priorityUser, priority, res[id].priorityOwner );
                     found= true;
                     pending->priorityUser= pending->notify.req.priority= priority;
                     essRMRemovePending( conn, id, pending );
                     if (
                           (pending->priorityUser > res[id].priorityOwner) ||
                           (
                             !server->state->base.requesterWinsPriorityTie &&
                             (pending->priorityUser == res[id].priorityOwner)
                           )
                        )
                     {
                        essRMInsertPendingByPriority( conn, id, pending );
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

         if ( pendingNtfyIdx >= 0 )
         {
            bool preemptOther= (conn != res[id].connOwner);
            if ( essRMRevokeResource( conn, type, id, preemptOther ) )
            {
               if ( preemptOther )
               {
                  EssRMgrResourceNotify *pending= &ctrl->pending[pendingNtfyIdx];

                  result= essRMAssignResource( pending->connUser, id, &pending->notify.req );
                  if ( result )
                  {
                     result= essRMTransferResource( pending->connUser, pending );
                  }
               }
            }
         }
      }
   }

exit:
   return result;
}

static bool essRMSetUsageResource( EssRMgrResourceConnection *conn, int requestId, int type, EssRMgrUsage *usage )
{
   bool result= false;

   if ( conn )
   {
      EssRMgrResourceServerCtx *server= conn->server;
      int maxId= 0;
      EssRMgrResource *res= 0;
      EssRMgrResourceControl *ctrl= 0;
      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            maxId= server->state->base.numVideoDecoders;
            res= server->state->base.videoDecoder;
            ctrl= &server->state->vidCtrl;
            break;
         case EssRMgrResType_audioDecoder:
            maxId= server->state->base.numAudioDecoders;
            res= server->state->base.audioDecoder;
            ctrl= &server->state->audCtrl;
            break;
         case EssRMgrResType_frontEnd:
            maxId= server->state->base.numFrontEnds;
            res= server->state->base.frontEnd;
            ctrl= &server->state->feCtrl;
            break;
         case EssRMgrResType_svpAllocator:
            maxId= server->state->base.numSVPAllocators;
            res= server->state->base.svpAlloc;
            ctrl= &server->state->svpaCtrl;
            break;
         default:
            ERROR("Bad resource type: %d", type);
            break;
      }
      if ( res && ctrl && (requestId >= 0) )
      {
         int id;
         int pendingNtfyIdx= -1;
         EssRMgrResourceNotify *pending= 0;

         for( id= 0; id < maxId; ++id )
         {
            if ( (res[id].requestIdOwner == requestId) &&
                 (res[id].connOwner == conn) )
            {
               int usageOrg;
               int testResult, testResultOrg;

               usageOrg= res[id].usageOwner;

               // update owner's usage
               res[id].usageOwner= usage->usage;

               pendingNtfyIdx= res[id].pendingNtfyIdx;

               testResult= (usage->usage & res[id].capabilities) & res[id].criteriaMask;
               if ( testResult )
               {
                  // owned item is no longer eligible for new usage
                  essRMRevokeResource( conn, type, id, false );

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
                        essRMRevokeResource( conn, type, id, false );
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
                  if ( (pending->notify.req.requestId == requestId) && (pending->connUser == conn) )
                  {
                     EssRMgrRequest req;

                     // remove pending request and then re-issue with new usage
                     essRMRemovePending( conn, id, pending );

                     req= pending->notify.req;
                     req.usage= usage->usage;
                     req.info= usage->info;

                     essRMPutPendingPoolItem( conn, pending);

                     result= essRMRequestResource( conn, &req);
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

static int essRMFindSuitableResource( EssRMgrResourceConnection *conn, int type, int priority, EssRMgrUsage *usage, int& pendingIdx )
{
   int suitableIdx= -1;

   pendingIdx= -1;

   if ( conn )
   {
      int i, j, testResult, idx;
      std::vector<int> eligibleItems, bestItems;
      int maxId= 0;
      EssRMgrResourceServerCtx *server= conn->server;
      EssRMgrResource *res= 0;
      EssRMgrResourceControl *ctrl= 0;
      switch( type )
      {
         case EssRMgrResType_videoDecoder:
            maxId= server->state->base.numVideoDecoders;
            res= server->state->base.videoDecoder;
            ctrl= &server->state->vidCtrl;
            break;
         case EssRMgrResType_audioDecoder:
            maxId= server->state->base.numAudioDecoders;
            res= server->state->base.audioDecoder;
            ctrl= &server->state->audCtrl;
            break;
         case EssRMgrResType_frontEnd:
            maxId= server->state->base.numFrontEnds;
            res= server->state->base.frontEnd;
            ctrl= &server->state->feCtrl;
            break;
         case EssRMgrResType_svpAllocator:
            maxId= server->state->base.numSVPAllocators;
            res= server->state->base.svpAlloc;
            ctrl= &server->state->svpaCtrl;
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
                       (res[idx].connOwner != 0) &&
                       (res[idx].priorityOwner == priority) &&
                       (res[idx].usageOwner == usage->usage)
                     )
                  {
                     if ( conn->server->state->base.requesterWinsPriorityTie )
                     {
                        TRACE1("res type %d id %d in use but possible preemption target", type, idx);
                        preemptIdx= idx;
                     }
                     else
                     {
                        TRACE1("res type %d id %d disqualified: same pri,same usage: owner conn %p pri %d usage 0x%X",
                               type,
                               idx,
                               res[idx].connOwner,
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
                       (res[idx].connOwner != 0) &&
                       (res[idx].priorityOwner < priority)
                     )
                  {
                     TRACE1("res type %d id %d disqualified: priorty: owner conn %p pri %d req pri %d",
                            type,
                            idx,
                            res[idx].connOwner,
                            res[idx].priorityOwner,
                            priority );
                     if ( pendingIdx < 0 )
                     {
                        pendingIdx= idx;
                     }
                     continue;
                  }
                  if ( (suitableIdx != -1) &&
                       (res[suitableIdx].connOwner != 0) &&
                       (res[idx].connOwner == 0) )
                  {
                     // prefer unassigned resource
                     suitableIdx= idx;
                     currMaxSize= maxSize;
                  }
                  else if ( (suitableIdx != -1) &&
                            (res[idx].connOwner == 0) &&
                            ((res[idx].capabilities & res[idx].criteriaMask) > (res[suitableIdx].capabilities & res[suitableIdx].criteriaMask)) )
                  {
                     // prefer less capable resource
                     suitableIdx= idx;
                     currMaxSize= maxSize;
                  }
                  else if ( suitableIdx == -1 )
                  {
                     suitableIdx= idx;
                     currMaxSize= maxSize;
                  }
                  else
                  if ( 
                       (res[suitableIdx].connOwner != 0) &&
                       (res[idx].connOwner != 0) &&
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
                          (res[suitableIdx].connOwner > 0) ||
                          (res[idx].connOwner == 0)
                        )
                     {
                        suitableIdx= idx;
                        currMaxSize= maxSize;
                     }
                  }
               }
               if ( (i == 0) && (suitableIdx >= 0) )
               {
                  TRACE1("select ideal resource id %d", suitableIdx);
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

static void essRMCancelRequestResource( EssRMgrResourceConnection *conn, int requestId, int type )
{
   if ( conn )
   {
      EssRMgrResourceServerCtx *server= conn->server;
      if ( requestId >= 0 )
      {
         int id;
         bool found= false;
         int pendingNtfyIdx= -1;
         EssRMgrResource *res= 0;
         EssRMgrResourceControl *ctrl= 0;
         int maxId= 0;
         switch( type )
         {
            case EssRMgrResType_videoDecoder:
               maxId= server->state->base.numVideoDecoders;
               res= server->state->base.videoDecoder;
               ctrl= &server->state->vidCtrl;
               break;
            case EssRMgrResType_audioDecoder:
               maxId= server->state->base.numAudioDecoders;
               res= server->state->base.audioDecoder;
               ctrl= &server->state->audCtrl;
               break;
            case EssRMgrResType_frontEnd:
               maxId= server->state->base.numFrontEnds;
               res= server->state->base.frontEnd;
               ctrl= &server->state->feCtrl;
               break;
            case EssRMgrResType_svpAllocator:
               maxId= server->state->base.numSVPAllocators;
               res= server->state->base.svpAlloc;
               ctrl= &server->state->svpaCtrl;
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
                    (res[id].connOwner == conn) )
               {
                  DEBUG("found request %d as owner of res type %d id %d", requestId, type, id);
                  found= true;
                  essRMReleaseResource( conn, type, id );
               }
               else   
               {
                  pendingNtfyIdx= res[id].pendingNtfyIdx;
                  while ( pendingNtfyIdx >= 0 )
                  {
                     EssRMgrResourceNotify *pending= &ctrl->pending[pendingNtfyIdx];
                     if ( (pending->notify.req.requestId == requestId) && (pending->connUser == conn) )
                     {
                        DEBUG("found request %d in res type %d id %d pending list", requestId, type, id );
                        found= true;
                        essRMRemovePending( conn, id, pending );
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

static EssRMgrResourceNotify* essRMGetPendingPoolItem( EssRMgrResourceConnection *conn, int type )
{
   EssRMgrResourceNotify *notify= 0;
   EssRMgrResourceControl *ctrl= 0;
   switch( type )
   {
      case EssRMgrResType_videoDecoder:
         ctrl= &conn->server->state->vidCtrl;
         break;
      case EssRMgrResType_audioDecoder:
         ctrl= &conn->server->state->audCtrl;
         break;
      case EssRMgrResType_frontEnd:
         ctrl= &conn->server->state->feCtrl;
         break;
      case EssRMgrResType_svpAllocator:
         ctrl= &conn->server->state->svpaCtrl;
         break;
      default:
         ERROR("Bad resource type: %d", type);
         break;
   }
   if ( ctrl )
   {
      DEBUG("essRMGetPendingPoolItem: type %d state %p pendingPool %d", type, conn->server->state, ctrl->pendingPoolIdx);
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

static void essRMPutPendingPoolItem( EssRMgrResourceConnection *conn, EssRMgrResourceNotify *notify )
{
   EssRMgrResourceControl *ctrl= 0;
   switch( notify->type )
   {
      case EssRMgrResType_videoDecoder:
         ctrl= &conn->server->state->vidCtrl;
         break;
      case EssRMgrResType_audioDecoder:
         ctrl= &conn->server->state->audCtrl;
         break;
      case EssRMgrResType_frontEnd:
         ctrl= &conn->server->state->feCtrl;
         break;
      case EssRMgrResType_svpAllocator:
         ctrl= &conn->server->state->svpaCtrl;
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

static void essRMInsertPendingByPriority( EssRMgrResourceConnection *conn, int id, EssRMgrResourceNotify *item )
{
   EssRMgrResourceNotify *insertAfter= 0;
   EssRMgrResourceNotify *iter= 0;
   EssRMgrResource *res= 0;
   EssRMgrResourceControl *ctrl= 0;
   switch( item->type )
   {
      case EssRMgrResType_videoDecoder:
         res= conn->server->state->base.videoDecoder;
         ctrl= &conn->server->state->vidCtrl;
         break;
      case EssRMgrResType_audioDecoder:
         res= conn->server->state->base.audioDecoder;
         ctrl= &conn->server->state->audCtrl;
         break;
      case EssRMgrResType_frontEnd:
         res= conn->server->state->base.frontEnd;
         ctrl= &conn->server->state->feCtrl;
         break;
      case EssRMgrResType_svpAllocator:
         res= conn->server->state->base.svpAlloc;
         ctrl= &conn->server->state->svpaCtrl;
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

static void essRMRemovePending( EssRMgrResourceConnection *conn, int id, EssRMgrResourceNotify *item )
{
   EssRMgrResource *res= 0;
   EssRMgrResourceControl *ctrl= 0;
   switch( item->type )
   {
      case EssRMgrResType_videoDecoder:
         res= conn->server->state->base.videoDecoder;
         ctrl= &conn->server->state->vidCtrl;
         break;
      case EssRMgrResType_audioDecoder:
         res= conn->server->state->base.audioDecoder;
         ctrl= &conn->server->state->audCtrl;
         break;
      case EssRMgrResType_frontEnd:
         res= conn->server->state->base.frontEnd;
         ctrl= &conn->server->state->feCtrl;
         break;
      case EssRMgrResType_svpAllocator:
         res= conn->server->state->base.svpAlloc;
         ctrl= &conn->server->state->svpaCtrl;
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

static bool essRMAssignResource( EssRMgrResourceConnection *conn, int id, EssRMgrRequest *req )
{
   bool result= false;
   EssRMgrResource *res= 0;
   const char *typeName= 0;
   int rc;

   switch( req->type )
   {
      case EssRMgrResType_videoDecoder:
         res= conn->server->state->base.videoDecoder;
         typeName= "video decoder";
         break;
      case EssRMgrResType_audioDecoder:
         res= conn->server->state->base.audioDecoder;
         typeName= "audio decoder";
         break;
      case EssRMgrResType_frontEnd:
         res= conn->server->state->base.frontEnd;
         typeName= "frontend";
         break;
      case EssRMgrResType_svpAllocator:
         res= conn->server->state->base.svpAlloc;
         typeName= "svpa";
         break;
      default:
         ERROR("Bad resource type: %d", req->type);
         break;
   }
   if ( res && typeName )
   {
      INFO("%s id %d assigned to conn %p client %d", typeName, id, conn, conn->clientId);
      req->assignedId= id;
      res[id].requestIdOwner= req->requestId;
      res[id].connOwner= conn;
      res[id].priorityOwner= req->priority;
      res[id].usageOwner= req->usage;

      result= true;
   }

   return result;
}

static bool essRMRevokeResource( EssRMgrResourceConnection *conn, int type, int id, bool wait )
{
   bool result= false;
   bool error= false;
   EssRMgrResource *res= 0;

   switch( type )
   {
      case EssRMgrResType_videoDecoder:
         res= conn->server->state->base.videoDecoder;
         break;
      case EssRMgrResType_audioDecoder:
         res= conn->server->state->base.audioDecoder;
         break;
      case EssRMgrResType_frontEnd:
         res= conn->server->state->base.frontEnd;
         break;
      case EssRMgrResType_svpAllocator:
         res= conn->server->state->base.svpAlloc;
         break;
      default:
         ERROR("Bad resource type: %d", type);
   }

   if ( conn )
   {
      int rc;
      EssRMgrResourceConnection *connPreempt= res[id].connOwner;

      // preempt current owner
      DEBUG("preempting conn %p to revoke res type %d id %d", connPreempt, type, id );

      result= essRMSendResRevoke( connPreempt, type, id );
      if ( result && wait )
      {
         EssRMgrResourceServerCtx *server= conn->server;
         int retry= conn->server->state->base.timeoutMS/10;
         for( ; ; )
         {
            if ( res[id].connOwner == 0 )
            {
               break;
            }
            if ( --retry == 0 )
            {
               INFO("preemption timeout waiting for conn %p to release res type %d id %d (timeout %d ms)",
                    connPreempt, type, id, conn->server->state->base.timeoutMS );
               error= true;
               break;
            }
            pthread_mutex_unlock( &server->state->mutex );
            usleep( 10000 );
            pthread_mutex_lock( &server->state->mutex );
         }
      }

      result= !error;
   }

exit:
   return result;
}

static bool essRMTransferResource( EssRMgrResourceConnection *conn, EssRMgrResourceNotify *pending )
{
   bool result= false;

   if ( conn )
   {
      result= essRMSendResRequestResponse(conn, &pending->notify.req, result ? 1 : 0);

      essRMPutPendingPoolItem( conn, pending );
   }

exit:
   return result;
}

static void essRMInvokeNotify( EssRMgr *rm, int event, EssRMgrRequestInfo *info )
{
   int rc;
   pthread_t threadId;
   EssRMgrRequestInfo *copy= 0;
   pthread_attr_t attr;

   copy= (EssRMgrRequestInfo*)calloc( 1, sizeof(EssRMgrRequestInfo));
   if ( !copy )
   {
      ERROR("No memory for request info");
      goto exit;
   }

   rc= sem_init( &copy->semComplete, 0, 0 );
   if ( rc )
   {
      ERROR("failed to create semComplete for request: %d error %d", rc, errno);
      free( copy );
      copy= 0;
      goto exit;
   }

   memcpy(&copy->req, &info->req, sizeof(EssRMgrRequest));

   rc= pthread_attr_init( &attr );
   if ( rc )
   {
      ERROR("unable to init pthread attr: errno %d", errno);
      goto exit;
   }

   rc= pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED);
   if ( rc )
   {
      ERROR("unable to set pthread attr detached: errno %d", errno);
      goto exit;
   }

   copy->rm= rm;
   copy->value1= event;
   rc= pthread_create( &threadId, &attr, essRMNotifyThread, copy );
   if ( rc )
   {
      ERROR("unable to start notify thread for event %d res type %d id %d", event, copy->req.type, copy->req.assignedId);
      goto exit;
   }

   rm->notifications.push_back( copy );
   copy= 0;

exit:
   if ( copy )
   {
      sem_destroy( &copy->semComplete );
      free( copy );
   }

   return;
}

static void* essRMNotifyThread( void *userData )
{
   EssRMgrRequestInfo *info= (EssRMgrRequestInfo*)userData;
   if ( info )
   {
      EssRMgr *rm= info->rm;
      int event= info->value1;
      DEBUG("calling notify callback");
      info->req.notifyCB( rm, event, info->req.type, info->req.assignedId, info->req.notifyUserData );
      DEBUG("done calling notify callback");

      sem_post( &info->semComplete );
   }
   return NULL;
}

#define ESSRMGR_MAX_NAMELEN (255)
static bool essRMReadConfigFile( EssRMgrResourceServerCtx *server )
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
   int svpaIndex= 0;
   int maxWidth, maxHeight;
   int timeout= DEFAULT_TIMEOUT_MS;

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
                     server->state->base.requesterWinsPriorityTie= true;
                  }
                  else if ( (i == 27) && !strncmp( work, "unauthorized-requests-abort", i ) )
                  {
                     INFO("policy: unauthorized-requests-abort");
                     server->state->base.unauthorizedRequestsAbort= true;
                  }
                  else if ( (i == 14) && !strncmp( work, "revoke-timeout", i ) )
                  {
                     bool haveValue;
                     if ( c == '(' )
                     {
                        i= 0;
                        haveValue= false;
                        for( ; ; )
                        {
                           c= fgetc( pFile );
                           if ( (c == ' ') || (c == ')') || (c == '\t') || (c == '\n') || (c == EOF) )
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
                              int value= atoi( work );
                              if ( value <= 0 )
                              {
                                 ERROR("bad timeout: using default");
                                 value= DEFAULT_TIMEOUT_MS;;
                              }
                              timeout= value;
                              INFO("policy: revoke-timeout: %d", timeout);
                              haveValue= false;
                              if ( c != ')' )
                              {
                                 ERROR("syntax error with timeout");
                              }
                              break;
                           }
                        }
                     }
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
                  server->state->base.videoDecoder[videoIndex].capabilities= capabilities;
                  if ( capabilities & EssRMgrVidCap_limitedResolution )
                  {
                     if ( maxWidth == -1 ) maxWidth= 640;
                     if ( maxHeight == -1 ) maxHeight= 480;
                     server->state->base.videoDecoder[videoIndex].usageInfo.video.maxWidth= maxWidth;
                     server->state->base.videoDecoder[videoIndex].usageInfo.video.maxHeight= maxHeight;
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
                  server->state->base.audioDecoder[audioIndex].capabilities= capabilities;
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
                     capabilities |= EssRMgrFECap_none;
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
                  server->state->base.frontEnd[feIndex].capabilities= capabilities;
                  ++feIndex;
                  break;
               }
            }
         }
         else
         if ( (i == 4) && !strncmp( work, "svpa", i ) )
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
                     capabilities |= EssRMgrSVPACap_none;
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
                  INFO("adding svpa %d caps %lX", svpaIndex, capabilities);
                  server->state->base.svpAlloc[svpaIndex].capabilities= capabilities;
                  ++svpaIndex;
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

   server->state->base.numVideoDecoders= videoIndex;
   server->state->base.numAudioDecoders= audioIndex;
   server->state->base.numFrontEnds= feIndex;
   server->state->base.numSVPAllocators= svpaIndex;
   server->state->base.timeoutMS= timeout;

   INFO("config file defines %d video decoders %d audio decoders %d frontends %d svpa",
        server->state->base.numVideoDecoders,
        server->state->base.numAudioDecoders,
        server->state->base.numFrontEnds,
        server->state->base.numSVPAllocators);

   result= true;

exit:
   if ( pFile )
   {
      fclose( pFile );
   }
   return result;
}

