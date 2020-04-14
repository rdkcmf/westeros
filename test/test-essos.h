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
#ifndef _TEST_ESSOS_H
#define _TEST_ESSOS_H

#include "westeros-ut-em.h"

bool testCaseEssosUseWayland( EMCTX *emctx );
bool testCaseEssosUseDirect( EMCTX *emctx );
bool testCaseEssosName( EMCTX *emctx );
bool testCaseEssosGetLastErrorDetail( EMCTX *emctx );
bool testCaseEssosSetKeyRepeatInitialDelay( EMCTX *emctx );
bool testCaseEssosSetKeyRepeatPeriod( EMCTX *emctx );
bool testCaseEssosEGLConfigAttributes( EMCTX *emctx );
bool testCaseEssosEGLSurfaceAttributes( EMCTX *emctx );
bool testCaseEssosEGLContextAttributes( EMCTX *emctx );
bool testCaseEssosInitialWindowSize( EMCTX *emctx );
bool testCaseEssosSetWindowPosition( EMCTX *emctx );
bool testCaseEssosSwapInterval( EMCTX *emctx );
bool testCaseEssosInit( EMCTX *emctx );
bool testCaseEssosGetEGLDisplayType( EMCTX *emctx );
bool testCaseEssosCreateNativeWindow( EMCTX *emctx );
bool testCaseEssosDestroyNativeWindow( EMCTX *emctx );
bool testCaseEssosGetWaylandDisplay( EMCTX *emctx );
bool testCaseEssosStart( EMCTX *emctx );
bool testCaseEssosEventLoopThrottle( EMCTX *emctx );
bool testCaseEssosDisplaySize( EMCTX *emctx );
bool testCaseEssosDisplaySizeChange( EMCTX *emctx );
bool testCaseEssosDisplaySafeAreaChange( EMCTX *emctx );
bool testCaseEssosDisplaySizeChangeWayland( EMCTX *emctx );
bool testCaseEssosDisplaySafeAreaChangeWayland( EMCTX *emctx );
bool testCaseEssosKeyboardBasicKeyInputWayland( EMCTX *emctx );
bool testCaseEssosKeyboardRepeatKeyInputWayland( EMCTX *emctx );
bool testCaseEssosPointerBasicPointerInputWayland( EMCTX *emctx );
bool testCaseEssosTerminateListener( EMCTX *emctx );
bool testCaseEssosGamepadBasic( EMCTX *emctx );

#endif

