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

#ifndef __ESSOS_RESMGR__
#define __ESSOS_RESMGR__

#ifndef __cplusplus
#include <stdbool.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _EssRMgr EssRMgr;

typedef enum _EssRMgrResType
{
   EssRMgrResType_videoDecoder,
   EssRMgrResType_audioDecoder,
   EssRMgrResType_frontEnd
} EssRMgrResType;

typedef enum _EssRMgrVideoDecoderCaps
{
   EssRMgrVidCap_none=                (0),
   EssRMgrVidCap_limitedResolution=   (1<<0),
   EssRMgrVidCap_limitedQuality=      (1<<1),
   EssRMgrVidCap_limitedPerformance=  (1<<2),
   EssRMgrVidCap_hardware=            (1<<16),
   EssRMgrVidCap_software=            (1<<17),

} EssRMgrVideoDecoderCaps;

typedef enum _EssRMgrVideoUsage
{
   EssRMgrVidUse_none=                (0),
   EssRMgrVidUse_fullResolution=      (1<<0),
   EssRMgrVidUse_fullQuality=         (1<<1),
   EssRMgrVidUse_fullPerformance=     (1<<2),

} EssRMgrVideoUsage;

typedef enum _EssRMgrAudioDecoderCaps
{
   EssRMgrAudCap_none=                (0),
} EssRMgrAudioDecoderCaps;

typedef enum _EssRMgrAudioUsage
{
   EssRMgrAudUse_none=                (0),
} EssRMgrAudioUsage;

typedef enum _EssRMgrFrontEndCaps
{
   EssRMgrFECap_none=                (0),
} EssRMgrFrontEndCaps;

typedef enum _EssRMgrFrontEndUsage
{
   EssRMgrFEUse_none=                (0),
} EssRMgrFontEndUsage;

typedef enum _EssRMgrEvent
{
   /*
    * Received when a resource is granted asynchronusly.
    */
   EssRMgrEvent_granted,

   /*
    * Received when a resource is being revoked.  When received,
    * the application should release the resource and then call
    * EssRMgrReleaseResource.
    */
   EssRMgrEvent_revoked
} EssRMgrEvent;

typedef void (*EssRMgrNotifyCB)( EssRMgr *rm, int event, int type, int id, void* userData );

typedef struct _EssRMgrVideoInfo
{
   int maxWidth;
   int maxHeight;
} EssRMgrVideoInfo;

typedef struct _EssRMgrAudioInfo
{
} EssRMgrAudioInfo;

typedef struct _EssRMgrFEInfo
{
} EssRMgrFEInfo;

typedef union EssRMgrUsageInfo
{
   EssRMgrVideoInfo video;
   EssRMgrAudioInfo audio;
   EssRMgrFEInfo frontEnd;
} EssRMgrUsageInfo;

typedef struct _EssRMgrRequest
{
   int type;
   int usage;
   int priority;
   bool asyncEnable;
   EssRMgrNotifyCB notifyCB;
   void *notifyUserData;
   EssRMgrUsageInfo info;
   int requestId;
   int assignedId;
   int assignedCaps;
} EssRMgrRequest;

typedef struct _EssRMgrCaps
{
   int capabilities;
   EssRMgrUsageInfo info;
} EssRMgrCaps;

typedef struct _EssRMgrUsage
{
   int usage;   
   EssRMgrUsageInfo info;
} EssRMgrUsage;



/**
 * EssRMgrInit
 *
 * Initialize resource manager
 */
bool EssRMgrInit();

/**
 * EssRMgrTerm
 *
 * Terminate resource manager
 */
void EssRMgrTerm();

/**
 * EssRMgrCreate
 *
 * Create an Essos resource manager context.
 */
EssRMgr* EssRMgrCreate();

/**
 * EssRMgrDestroy
 *
 * Destroy an Essos resource manager context.
 */
void EssRMgrDestroy( EssRMgr *rm );

/**
 * EssRMgrGetPolicyPriorityTie
 *
 * Get the policy for priority ties.  Returns true if requester
 * wins priority tie, false otherwise
 */
bool EssRMgrGetPolicyPriorityTie( EssRMgr *rm );

/**
 * EssRMgrResourceGetCount
 *
 * Get number of instances of specified resource type
 */
int EssRMgrResourceGetCount( EssRMgr *rm, int type );

/**
 * EssRMgrResourceGetOwner
 *
 * Get current owner of specified resource
 */
bool EssRMgrResourceGetOwner( EssRMgr *rm, int type, int id, int *client, int *priority );

/**
 * EssRMgrResourceGetCaps
 *
 * Get capabilities info for specified resource
 */
bool EssRMgrResourceGetCaps( EssRMgr *rm, int type, int id, EssRMgrCaps *caps );

/**
 * EssRMgrRequestResource
 *
 * Request ownership of an instance of the specified resource.
 *
 * The caller provides details of the request in the EssRMgrRequest structure including the intended usage constraints,
 * request priority, if the request can be asynchronous, and a notification callback.  This call back will be
 * invoked for asynchronous grant and revocation events. Prior to returning, the requestId field of the supplied EssRMgrRequest
 * structure is assigned a value.  This value can be used in subsequent calls requireing a requestId until the
 * id is invalidated by calling EssRMgrReleaseReource or EssRMgrRequestCancel.
 */
bool EssRMgrRequestResource( EssRMgr *rm, int type, EssRMgrRequest *req );

/**
 * EssRMgrReleaseResource
 *
 * Release ownership of a resource.
 */
void EssRMgrReleaseResource( EssRMgr *rm, int type, int id );

/**
 * EssRMgrUsageSetPriority
 *
 * Update the priority of a resource request.  This may result
 * in a change of resource ownership.
 */
bool EssRMgrRequestSetPriority( EssRMgr *rm, int type, int requestId, int priority );

/**
 * EssRMgrRequestSetUsage
 *
 * Update the usage of a resource request.  This may result
 * in a change or resource ownership.
 */
bool EssRMgrRequestSetUsage( EssRMgr *rm, int type, int requestId, EssRMgrUsage *usage );

/**
 * EssRMgrRequestCancel
 *
 * Cancel a resource request.
 */
void EssRMgrRequestCancel( EssRMgr *rm, int type, int requestId );

/**
 * EssRMgrDumpState
 *
 * Emit state data to log output
 */
void EssRMgrDumpState( EssRMgr *rm );

#if defined(__cplusplus)
} //extern "C"
#endif

#endif

