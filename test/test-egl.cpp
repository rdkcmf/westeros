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

#include "test-egl.h"

#define RED_SIZE (8)
#define GREEN_SIZE (8)
#define BLUE_SIZE (8)
#define ALPHA_SIZE (8)
#define DEPTH_SIZE (0)

bool testSetupEGL( TestEGLCtx *ctx, void *nativeDisplay )
{
   bool result= false;
   EGLint major, minor;
   EGLBoolean b;
   EGLint configCount;
   EGLConfig *eglConfigs= 0;
   EGLint attr[32];
   EGLint redSize, greenSize, blueSize, alphaSize, depthSize;
   EGLint ctxAttrib[3];
   int i;

   if ( nativeDisplay )
   {
      ctx->eglDisplay = eglGetDisplay((NativeDisplayType)nativeDisplay);
   }
   else
   {
      ctx->eglDisplay= eglGetDisplay(EGL_DEFAULT_DISPLAY);
   }
   printf("eglDisplay=%p\n", ctx->eglDisplay );
   if ( ctx->eglDisplay == EGL_NO_DISPLAY )
   {
      printf("error: EGL not available\n" );
      goto exit;
   }

   b= eglInitialize( ctx->eglDisplay, &major, &minor );
   if ( !b )
   {
      printf("error: unable to initialize EGL display\n" );
      goto exit;
   }
   printf("eglInitiialize: major: %d minor: %d\n", major, minor );

   b= eglGetConfigs( ctx->eglDisplay, NULL, 0, &configCount );
   if ( !b )
   {
      printf("error: unable to get count of EGL configurations: %X\n", eglGetError() );
      goto exit;
   }
   printf("Number of EGL configurations: %d\n", configCount );
    
   eglConfigs= (EGLConfig*)malloc( configCount*sizeof(EGLConfig) );
   if ( !eglConfigs )
   {
      printf("error: unable to alloc memory for EGL configurations\n");
      goto exit;
   }

   i= 0;
   attr[i++]= EGL_RED_SIZE;
   attr[i++]= RED_SIZE;
   attr[i++]= EGL_GREEN_SIZE;
   attr[i++]= GREEN_SIZE;
   attr[i++]= EGL_BLUE_SIZE;
   attr[i++]= BLUE_SIZE;
   attr[i++]= EGL_DEPTH_SIZE;
   attr[i++]= DEPTH_SIZE;
   attr[i++]= EGL_STENCIL_SIZE;
   attr[i++]= 0;
   attr[i++]= EGL_SURFACE_TYPE;
   attr[i++]= EGL_WINDOW_BIT;
   attr[i++]= EGL_RENDERABLE_TYPE;
   attr[i++]= EGL_OPENGL_ES2_BIT;
   attr[i++]= EGL_NONE;
    
   b= eglChooseConfig( ctx->eglDisplay, attr, eglConfigs, configCount, &configCount );
   if ( !b )
   {
      printf("error: eglChooseConfig failed: %X\n", eglGetError() );
      goto exit;
   }
   printf("eglChooseConfig: matching configurations: %d\n", configCount );

   for( i= 0; i < configCount; ++i )
   {
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_RED_SIZE, &redSize );
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_GREEN_SIZE, &greenSize );
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_BLUE_SIZE, &blueSize );
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_ALPHA_SIZE, &alphaSize );
      eglGetConfigAttrib( ctx->eglDisplay, eglConfigs[i], EGL_DEPTH_SIZE, &depthSize );

      printf("config %d: red: %d green: %d blue: %d alpha: %d depth: %d\n",
              i, redSize, greenSize, blueSize, alphaSize, depthSize );
      if ( (redSize == RED_SIZE) &&
           (greenSize == GREEN_SIZE) &&
           (blueSize == BLUE_SIZE) &&
           (alphaSize == ALPHA_SIZE) &&
           (depthSize >= DEPTH_SIZE) )
      {
         printf( "choosing config %d\n", i);
         break;
      }
   }
   if ( i == configCount )
   {
      printf("error: no suitable configuration available\n");
      goto exit;
   }
   ctx->eglConfig= eglConfigs[i];

   ctxAttrib[0]= EGL_CONTEXT_CLIENT_VERSION;
   ctxAttrib[1]= 2; // ES2
   ctxAttrib[2]= EGL_NONE;
    
   /*
    * Create an EGL context
    */
   ctx->eglContext= eglCreateContext( ctx->eglDisplay, ctx->eglConfig, EGL_NO_CONTEXT, ctxAttrib );
   if ( ctx->eglContext == EGL_NO_CONTEXT )
   {
      printf( "eglCreateContext failed: %X\n", eglGetError() );
      goto exit;
   }
   printf("eglCreateContext: eglContext %p\n", ctx->eglContext );

   ctx->initialized= true;

   result= true;

exit:

   if ( eglConfigs )
   {
      free( eglConfigs );
      eglConfigs= 0;
   }

   return result;
}

void testTermEGL( TestEGLCtx *ctx )
{
   if ( ctx->initialized )
   {
      eglMakeCurrent( ctx->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
      
      eglTerminate( ctx->eglDisplay );
      eglReleaseThread();

      ctx->initialized= false;
   }
}


