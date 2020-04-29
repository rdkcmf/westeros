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

#ifndef __WESTEROS_SINK_SOC_SVP_UTIL_H__
#define __WESTEROS_SINK_SOC_SVP_UTIL_H__

#define WESTEROS_SINK_SVP

static void wstSVPSetInputMemMode( GstWesterosSink *sink, int mode );
static void wstSVPSetOutputMemMode( GstWesterosSink *sink, int mode );
static void wstSVPDecoderConfig( GstWesterosSink *sink );
static bool wstSVPSetupOutputBuffersDmabuf( GstWesterosSink *sink );
static void wstSVPTearDownOutputBuffersDmabuf( GstWesterosSink *sink );

#endif
