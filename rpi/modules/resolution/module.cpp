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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wayland-server.h"
#include "westeros-compositor.h"

#include <bcm_host.h>

static WstCompositor *compositor= 0;

void determineInitialResolution( void )
{
   int displayId;
   uint32_t width, height;

   displayId= DISPMANX_ID_MAIN_LCD;
   int32_t result= graphics_get_display_size( displayId,
                                              &width,
                                              &height );

   if ( result == 0 )
   {
      printf("module: display %dx%d\n", width, height);
      WstCompositorResolutionChangeBegin( compositor );
      WstCompositorResolutionChangeEnd( compositor, width, height );
   }
}

extern "C"
{

bool moduleInit( WstCompositor *ctx, struct wl_display *display )
{
   bool result= false;

   compositor= ctx;

   bcm_host_init();
   determineInitialResolution();

   result= true;

   return result;
}

void moduleTerm( WstCompositor *ctx )
{
   bcm_host_deinit();
   compositor= 0;
}

}

