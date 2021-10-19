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
#ifndef _TEST_ESSOS_ERM_H
#define _TEST_ESSOS_ERM_H

#include "westeros-ut-em.h"

bool testCaseERMBasicRequestVideo( EMCTX *emctx );
bool testCaseERMBasicRequestAudio( EMCTX *emctx );
bool testCaseERMBasicRequestFrontEnd( EMCTX *emctx );
bool testCaseERMBasicRequestSVPAllocator( EMCTX *emctx );
bool testCaseERMBasicRequestLoop( EMCTX *emctx );
bool testCaseERMBasicRequestAsync( EMCTX *emctx );
bool testCaseERMVideoSizeConstraint( EMCTX *emctx );
bool testCaseERMRequesterIncreasePriority( EMCTX *emctx );
bool testCaseERMOwnerDecreasePriority( EMCTX *emctx );
bool testCaseERMOwnerChangeUsage( EMCTX *emctx );
bool testCaseERMDualVideo1( EMCTX *emctx );
bool testCaseERMDualVideo2( EMCTX *emctx );
bool testCaseERMDualVideo3( EMCTX *emctx );

#endif

