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
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <memory.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include "essos-resmgr.h"


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

#define ESSRMGR_DEFAULT_CONFIG_FILE "/etc/default/essrmgr.conf"

#define ESSRMGR_MAX_ITEMS (16)
#define ESSRMGR_MAX_PENDING (ESSRMGR_MAX_ITEMS*3)

#define ESSRMGR_CRITERIA_MASK_VIDEO (0x0007)
#define ESSRMGR_CRITERIA_MASK_AUDIO (0x0000)
#define ESSRMGR_CRITERIA_MASK_FE    (0x0000)
#define ESSRMGR_CRITERIA_MASK_SVPA  (0x0000)

#define DEFAULT_TIMEOUT_MS (3000)

static int gLogLevel= 2;

static void essrm_printf( int level, const char *fmt, ... )
{
   if ( level <= gLogLevel )
   {
      va_list argptr;
      va_start( argptr, fmt );
      vfprintf( stderr, fmt, argptr );
      va_end( argptr );
   }
}

#if !defined(USE_ESSRMGR_SHM_IMPL) && !defined(USE_ESSRMGR_UDS_IMPL)
#define USE_ESSRMGR_SHM_IMPL
#endif

#ifdef USE_ESSRMGR_SHM_IMPL
#include "essos-resmgr-shm.cpp"
#endif

#ifdef USE_ESSRMGR_UDS_IMPL
#include "essos-resmgr-uds.cpp"
#endif

