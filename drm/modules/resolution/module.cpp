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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wayland-server.h"
#include "westeros-compositor.h"

#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define DEFAULT_CARD "/dev/dri/card0"

static WstCompositor *compositor= 0;

/*
 * Determine the resolution of the current display mode
 * and, if a desired mode is specified via the env
 * var WESTEROS_DRM_MODE, find a matching supported mode
 * Signal the determined mode to the compositor.
 */
void determineInitialResolution( void )
{
   int i, j;
   int drmFd= -1;
   drmModeRes *res = 0;
   drmModeConnector *conn = 0;
   drmModeEncoder *encoder = 0;
   drmModeCrtc *crtc = 0;
   const char *env;
   const char *cardName= 0;
   int modeWidth= 0, modeHeight= 0;
   int targetWidth= 0, targetHeight= 0;
   bool wantTarget= false;
   bool haveTarget= false;

   cardName= getenv("WESTEROS_DRM_CARD");
   if ( !cardName )
   {
      cardName= DEFAULT_CARD;
   }

   env= getenv("WESTEROS_DRM_MODE");
   if ( env )
   {
      if ( sscanf( env, "%dx%d", &targetWidth, &targetHeight ) == 2 )
      {
         printf("target mode: %dx%d\n", targetWidth, targetHeight);
         wantTarget= true;
      }
   }

   drmFd= open(cardName, O_RDWR);
   if ( drmFd < 0 )
   {
      printf( "failed to open drm card (%s)\n", cardName);
      goto exit;
   }

   res= drmModeGetResources( drmFd );
   if ( res )
   {
      for( i = 0; i < res->count_connectors; ++i )
      {
         conn= drmModeGetConnector( drmFd, res->connectors[i] );
         if ( conn )
         {
            if ( (conn->connection == DRM_MODE_CONNECTED) && (conn->count_modes > 0) )
            {
               for( j = 0; j < res->count_encoders; ++j )
               {
                  encoder= drmModeGetEncoder(drmFd, res->encoders[i]);
                  if ( encoder )
                  {
                     if ( encoder->encoder_id == conn->encoder_id )
                     {
                        crtc = drmModeGetCrtc(drmFd, encoder->crtc_id);
                        if ( crtc )
                        {
                           if ( crtc->mode_valid )
                           {
                              printf("current mode %dx%d@%d\n", crtc->mode.hdisplay, crtc->mode.vdisplay, crtc->mode.vrefresh );
                              modeWidth= crtc->mode.hdisplay;
                              modeHeight= crtc->mode.vdisplay;
                              break;
                           }
                        }
                     }
                     drmModeFreeEncoder( encoder );
                     encoder = 0;
                  }
               }

               if ( wantTarget )
               {
                  for( j= 0; j < conn->count_modes; ++j )
                  {
                     if ( !haveTarget &&
                          (conn->modes[j].hdisplay == targetWidth) &&
                          (conn->modes[j].vdisplay == targetHeight) &&
                          (conn->modes[j].type & DRM_MODE_TYPE_DRIVER) )
                     {
                        haveTarget= true;
                        break;
                     }
                  }

                  if ( haveTarget &&
                       (modeWidth != targetWidth) &&
                       (modeHeight != targetHeight) )
                  {
                     modeWidth= targetWidth;
                     modeHeight= targetHeight;
                  }
               }
            }
            drmModeFreeConnector(conn);
            conn= 0;
         }
         else
         {
            printf("unable to access a drm connector for card (%s)\n", cardName);
            goto exit;
         }
      }
   }
   else
   {
      printf("unable to access drm resources for card (%s)\n", cardName);
      goto exit;
   }

   if ( (modeWidth != 0) && (modeHeight != 0) )
   {
      printf("module: display %dx%d\n", modeWidth, modeHeight);
      WstCompositorResolutionChangeBegin( compositor );
      WstCompositorResolutionChangeEnd( compositor, modeWidth, modeHeight );
   }

exit:
   if ( encoder )
   {
      drmModeFreeEncoder( encoder );
   }

   if ( res )
   {
      drmModeFreeResources( res );
   }
   return;
}

extern "C"
{

bool moduleInit( WstCompositor *ctx, struct wl_display *display )
{
   bool result= false;

   compositor= ctx;

   determineInitialResolution();

   result= true;

   return result;
}

void moduleTerm( WstCompositor *ctx )
{
   compositor= 0;
}

}

