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
#include <unistd.h>
#include <linux/input.h>

#include "test-essos.h"

#include "essos.h"
#include "westeros-compositor.h"

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

bool testCaseEssosUseWayland( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   bool useWayland;
   bool value;

   useWayland= true;
   result= EssContextSetUseWayland( (EssCtx*)0, useWayland );
   if ( result )
   {
      EMERROR("EssContextSetUseWayland did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, useWayland );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   value= EssContextGetUseWayland( ctx );
   if ( value != useWayland )
   {
      EMERROR("EssContextGetUseWayland reports unexpected value: expected(%d) actual(%d)", useWayland, value );
      goto exit;
   }

   useWayland= false;
   result= EssContextSetUseWayland( ctx, useWayland );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   value= EssContextGetUseWayland( ctx );
   if ( value != useWayland )
   {
      EMERROR("EssContextGetUseWayland reports unexpected value: expected(%d) actual(%d)", useWayland, value );
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result )
   {
      EMERROR("EssContextSetUseWayland did not fail on initialized context");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:
   return testResult;
}

bool testCaseEssosUseDirect( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   bool useDirect;
   bool value;

   useDirect= false;
   result= EssContextSetUseDirect( (EssCtx*)0, useDirect );
   if ( result )
   {
      EMERROR("EssContextSetUseDirect did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseDirect( ctx, useDirect );
   if ( result == false )
   {
      EMERROR("EssContextSetUseDirect failed");
      goto exit;
   }

   value= EssContextGetUseDirect( ctx );
   if ( value != useDirect )
   {
      EMERROR("EssContextGetUseDirect reports unexpected value: expected(%d) actual(%d)", useDirect, value );
      goto exit;
   }

   useDirect= true;
   result= EssContextSetUseDirect( ctx, useDirect );
   if ( result == false )
   {
      EMERROR("EssContextSetUseDirect failed");
      goto exit;
   }

   value= EssContextGetUseDirect( ctx );
   if ( value != useDirect )
   {
      EMERROR("EssContextGetUseDirect reports unexpected value: expected(%d) actual(%d)", useDirect, value );
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextSetUseDirect( ctx, false );
   if ( result )
   {
      EMERROR("EssContextSetUseDirect did not fail on initialized context");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:
   return testResult;
}

bool testCaseEssosName( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   const char *name= "testapp";
   const char *name2= "foo";

   result= EssContextSetName( (EssCtx*)0, name );
   if ( result )
   {
      EMERROR("EssContextSetName did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetName( ctx, 0 );
   if ( result )
   {
      EMERROR("EssContextSetName did not fail with null name");
      goto exit;
   }

   result= EssContextSetName( ctx, name );
   if ( result == false )
   {
      EMERROR("EssContextSetName failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextSetName( ctx, name2 );
   if ( result )
   {
      EMERROR("EssContextSetName did not fail with initialized context");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:

   return testResult;
}

bool testCaseEssosGetLastErrorDetail( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   const char *detail;

   detail= EssContextGetLastErrorDetail( (EssCtx*)0 );
   if ( detail )
   {
      EMERROR("EssContextGetLastErrorDetail did not reuturn null with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   detail= EssContextGetLastErrorDetail( ctx );
   if ( detail == 0 )
   {
      EMERROR("EssContextGetLastErrorDetail failed");
      goto exit;
   }

   result= EssContextSetName( ctx, 0 );
   if ( result )
   {
      EMERROR("EssContextSetName did not fail with null name");
      goto exit;
   }

   detail= EssContextGetLastErrorDetail( ctx );
   if ( detail == 0 )
   {
      EMERROR("EssContextGetLastErrorDetail failed");
      goto exit;
   }
   if ( strlen(detail) <= 0 )
   {
      EMERROR("EssContextGetLastErrorDetail returned unexpected zero length message");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:
   return testResult;
}

bool testCaseEssosSetKeyRepeatInitialDelay( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   int delay;

   delay= 500;
   result= EssContextSetKeyRepeatInitialDelay( (EssCtx*)0, delay );
   if ( result )
   {
      EMERROR("EssContextSetKeyRepeatInitialDelay did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetKeyRepeatInitialDelay( ctx, delay );
   if ( result == false )
   {
      EMERROR("EssContextSetKeyRepeatInitialDelay failed");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:
   return testResult;
}

bool testCaseEssosSetKeyRepeatPeriod( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   int period;

   period= 200;
   result= EssContextSetKeyRepeatPeriod( (EssCtx*)0, period );
   if ( result )
   {
      EMERROR("EssContextSetKeyRepeatPeriod did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetKeyRepeatPeriod( ctx, period );
   if ( result == false )
   {
      EMERROR("EssContextSetKeyRepeatPeriod failed");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:
   return testResult;
}

bool testCaseEssosEGLConfigAttributes( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   EGLint attr[3];
   EGLint attrSize;
   EGLint *valueAttr, *defaultValue;
   EGLint valueSize, defaultSize;

   attr[0]= EGL_DEPTH_SIZE;
   attr[1]= 24;
   attr[2]= EGL_NONE;

   attrSize= 3;

   result= EssContextSetEGLConfigAttributes( (EssCtx*)0, attr, attrSize );
   if ( result )
   {
      EMERROR("EssContextSetEGLConfigAttributes did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextGetEGLConfigAttributes( ctx, &defaultValue, &defaultSize );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLConfigAttributes failed");
      goto exit;
   }

   result= EssContextSetEGLConfigAttributes( ctx, attr, attrSize );
   if ( result == false )
   {
      EMERROR("EssContextSetEGLConfigAttributes failed");
      goto exit;
   }

   result= EssContextGetEGLConfigAttributes( ctx, 0, &valueSize );
   if ( result )
   {
      EMERROR("EssContextSetEGLConfigAttributes did not fail with null attr pointer");
      goto exit;
   }

   result= EssContextGetEGLConfigAttributes( ctx, &valueAttr, 0 );
   if ( result )
   {
      EMERROR("EssContextSetEGLConfigAttributes did not fail with null size pointer");
      goto exit;
   }

   result= EssContextGetEGLConfigAttributes( ctx, 0, 0 );
   if ( result )
   {
      EMERROR("EssContextSetEGLConfigAttributes did not fail with null attr and size pointers");
      goto exit;
   }

   result= EssContextGetEGLConfigAttributes( ctx, &valueAttr, &valueSize );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLConfigAttributes failed");
      goto exit;
   }

   if ( (valueSize != attrSize) && (memcmp( valueAttr, attr, attrSize*sizeof(EGLint) ) != 0 ) )
   {
      EMERROR("EssContextGetEGLConfigAttributes return unexpected values");
      goto exit;
   }

   result= EssContextSetEGLConfigAttributes( ctx, 0, 0 );
   if ( result == false )
   {
      EMERROR("EssContextSetEGLConfigAttributes failed");
      goto exit;
   }

   result= EssContextGetEGLConfigAttributes( ctx, &valueAttr, &valueSize );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLConfigAttributes failed");
      goto exit;
   }

   if ( (valueSize != defaultSize) && (memcmp( valueAttr, defaultValue, defaultSize*sizeof(EGLint) ) != 0 ) )
   {
      EMERROR("EssContextGetEGLConfigAttributes return unexpected values");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:

   return testResult;
}

bool testCaseEssosEGLSurfaceAttributes( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   EGLint attr[3];
   EGLint attrSize;
   EGLint *valueAttr, *defaultValue;
   EGLint valueSize, defaultSize;

   attr[0]= EGL_SWAP_BEHAVIOR;
   attr[1]= EGL_BUFFER_PRESERVED;
   attr[2]= EGL_NONE;

   attrSize= 3;

   result= EssContextSetEGLSurfaceAttributes( (EssCtx*)0, attr, attrSize );
   if ( result )
   {
      EMERROR("EssContextSetEGLSurfaceAttributes did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextGetEGLSurfaceAttributes( ctx, &defaultValue, &defaultSize );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLSurfaceAttributes failed");
      goto exit;
   }

   result= EssContextSetEGLSurfaceAttributes( ctx, attr, attrSize );
   if ( result == false )
   {
      EMERROR("EssContextSetEGLSurfaceAttributes failed");
      goto exit;
   }

   result= EssContextGetEGLSurfaceAttributes( ctx, 0, &valueSize );
   if ( result )
   {
      EMERROR("EssContextSetEGLSurfaceAttributes did not fail with null attr pointer");
      goto exit;
   }

   result= EssContextGetEGLSurfaceAttributes( ctx, &valueAttr, 0 );
   if ( result )
   {
      EMERROR("EssContextSetEGLSurfaceAttributes did not fail with null size pointer");
      goto exit;
   }

   result= EssContextGetEGLSurfaceAttributes( ctx, 0, 0 );
   if ( result )
   {
      EMERROR("EssContextSetEGLSurfaceAttributes did not fail with null attr and size pointers");
      goto exit;
   }

   result= EssContextGetEGLSurfaceAttributes( ctx, &valueAttr, &valueSize );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLSurfaceAttributes failed");
      goto exit;
   }

   if ( (valueSize != attrSize) && (memcmp( valueAttr, attr, attrSize*sizeof(EGLint) ) != 0 ) )
   {
      EMERROR("EssContextGetEGLSurfaceAttributes return unexpected values");
      goto exit;
   }

   result= EssContextSetEGLSurfaceAttributes( ctx, 0, 0 );
   if ( result == false )
   {
      EMERROR("EssContextSetEGLSurfaceAttributes failed");
      goto exit;
   }

   result= EssContextGetEGLSurfaceAttributes( ctx, &valueAttr, &valueSize );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLSurfaceAttributes failed");
      goto exit;
   }

   if ( (valueSize != defaultSize) && (memcmp( valueAttr, defaultValue, defaultSize*sizeof(EGLint) ) != 0 ) )
   {
      EMERROR("EssContextGetEGLSurfaceAttributes return unexpected values");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:

   return testResult;
}

bool testCaseEssosEGLContextAttributes( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   EGLint attr[1];
   EGLint attrSize;
   EGLint *valueAttr, *defaultValue;
   EGLint valueSize, defaultSize;

   attr[0]= EGL_NONE;

   attrSize= 1;

   result= EssContextSetEGLContextAttributes( (EssCtx*)0, attr, attrSize );
   if ( result )
   {
      EMERROR("EssContextSetEGLContextAttributes did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextGetEGLContextAttributes( ctx, &defaultValue, &defaultSize );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLContextAttributes failed");
      goto exit;
   }

   result= EssContextSetEGLContextAttributes( ctx, attr, attrSize );
   if ( result == false )
   {
      EMERROR("EssContextSetEGLContextAttributes failed");
      goto exit;
   }

   result= EssContextGetEGLContextAttributes( ctx, 0, &valueSize );
   if ( result )
   {
      EMERROR("EssContextSetEGLContextAttributes did not fail with null attr pointer");
      goto exit;
   }

   result= EssContextGetEGLContextAttributes( ctx, &valueAttr, 0 );
   if ( result )
   {
      EMERROR("EssContextSetEGLContextAttributes did not fail with null size pointer");
      goto exit;
   }

   result= EssContextGetEGLContextAttributes( ctx, 0, 0 );
   if ( result )
   {
      EMERROR("EssContextSetEGLContextAttributes did not fail with null attr and size pointers");
      goto exit;
   }

   result= EssContextGetEGLContextAttributes( ctx, &valueAttr, &valueSize );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLContextAttributes failed");
      goto exit;
   }

   if ( (valueSize != attrSize) && (memcmp( valueAttr, attr, attrSize*sizeof(EGLint) ) != 0 ) )
   {
      EMERROR("EssContextGetEGLContextAttributes return unexpected values");
      goto exit;
   }

   result= EssContextSetEGLContextAttributes( ctx, 0, 0 );
   if ( result == false )
   {
      EMERROR("EssContextSetEGLContextAttributes failed");
      goto exit;
   }

   result= EssContextGetEGLContextAttributes( ctx, &valueAttr, &valueSize );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLContextAttributes failed");
      goto exit;
   }

   if ( (valueSize != defaultSize) && (memcmp( valueAttr, defaultValue, defaultSize*sizeof(EGLint) ) != 0 ) )
   {
      EMERROR("EssContextGetEGLContextAttributes return unexpected values");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:

   return testResult;
}

bool testCaseEssosInitialWindowSize( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   int windowWidth, windowHeight;

   windowWidth= 640;
   windowHeight= 480;

   result= EssContextSetInitialWindowSize( (EssCtx*)0, windowWidth, windowHeight );
   if ( result )
   {
      EMERROR("EssContextSetInitialWindowSize did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetInitialWindowSize( ctx, windowWidth, windowHeight );
   if ( result == false )
   {
      EMERROR("EssContextSetInitialWindowSize failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   usleep( 17000 );

   result= EssContextSetInitialWindowSize( ctx, 1920, 1080 );
   if ( !result  )
   {
      EMERROR("EssContextSetInitialWindowSize failed on running context");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:

   return testResult;
}

bool testCaseEssosSwapInterval( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   int swapInterval;

   swapInterval= 2;

   result= EssContextSetSwapInterval( (EssCtx*)0, swapInterval );
   if ( result )
   {
      EMERROR("EssContextSetSwapInterval did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetSwapInterval( ctx, swapInterval );
   if ( result == false )
   {
      EMERROR("EssContextSetSwapInterval failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   usleep( 17000 );

   result= EssContextSetSwapInterval( ctx, 3 );
   if ( result  )
   {
      EMERROR("EssContextSetSwapInterval did not fail on running context");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:

   return testResult;
}

bool testCaseEssosInit( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;

   result= EssContextInit( (EssCtx*)0 );
   if ( result )
   {
      EMERROR("EssContextInit did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   usleep( 17000 );

   result= EssContextInit( ctx );
   if ( result )
   {
      EMERROR("EssContextInit did not fail with running context");
      goto exit;
   }

   EssContextDestroy( ctx );

   testResult= true;

exit:

   return testResult;
}

bool testCaseEssosGetEGLDisplayType( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   NativeDisplayType displayType;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;

   result= EssContextGetEGLDisplayType( (EssCtx*)0, &displayType );
   if ( result )
   {
      EMERROR("EssContextGetEGLDisplayType did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextGetEGLDisplayType( ctx, &displayType );
   if ( result )
   {
      EMERROR("EssContextGetEGLDisplayType did not fail with uninitialized context");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextGetEGLDisplayType( ctx, &displayType );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLDisplayType failed");
      goto exit;
   }

   EssContextDestroy( ctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextGetEGLDisplayType( ctx, &displayType );
   if ( result == false )
   {
      EMERROR("EssContextGetEGLDisplayType failed");
      goto exit;
   }

   unsetenv( "WAYLAND_DISPLAY" );

   testResult= true;

exit:

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseEssosCreateNativeWindow( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   NativeWindowType windowType;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;
   int windowWidth, windowHeight;

   windowWidth= WINDOW_WIDTH;
   windowHeight= WINDOW_HEIGHT;

   result= EssContextCreateNativeWindow( (EssCtx*)0, windowWidth, windowHeight, &windowType );
   if ( result )
   {
      EMERROR("EssContextCreateNativeWindow did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextCreateNativeWindow( ctx, windowWidth, windowHeight, &windowType );
   if ( result )
   {
      EMERROR("EssContextCreateNativeWindow did not fail with uninitialized context");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextCreateNativeWindow( ctx, windowWidth, windowHeight, &windowType );
   if ( result == false )
   {
      EMERROR("EssContextCreateNativeWindow failed");
      goto exit;
   }

   EssContextDestroy( ctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextCreateNativeWindow( ctx, windowWidth, windowHeight, &windowType );
   if ( result == false )
   {
      EMERROR("EssContextCreateNativeWindow failed");
      goto exit;
   }

   unsetenv( "WAYLAND_DISPLAY" );

   testResult= true;

exit:

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseEssosDestroyNativeWindow( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   NativeWindowType windowType;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;
   int windowWidth, windowHeight;

   windowWidth= WINDOW_WIDTH;
   windowHeight= WINDOW_HEIGHT;

   result= EssContextDestroyNativeWindow( (EssCtx*)0, &windowType );
   if ( result )
   {
      EMERROR("EssContextDestroyNativeWindow did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextDestroyNativeWindow( ctx, windowType );
   if ( result )
   {
      EMERROR("EssContextDestroyNativeWindow did not fail with uninitialized context");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextCreateNativeWindow( ctx, windowWidth, windowHeight, &windowType );
   if ( result == false )
   {
      EMERROR("EssContextCreateNativeWindow failed");
      goto exit;
   }

   result= EssContextDestroyNativeWindow( ctx, windowType );
   if ( result == false )
   {
      EMERROR("EssContextDestroyNativeWindow failed");
      goto exit;
   }

   EssContextDestroy( ctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   result= EssContextCreateNativeWindow( ctx, windowWidth, windowHeight, &windowType );
   if ( result == false )
   {
      EMERROR("EssContextCreateNativeWindow failed");
      goto exit;
   }

   result= EssContextDestroyNativeWindow( ctx, windowType );
   if ( result == false )
   {
      EMERROR("EssContextDestroyNativeWindow failed");
      goto exit;
   }

   unsetenv( "WAYLAND_DISPLAY" );

   testResult= true;

exit:

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseEssosGetWaylandDisplay( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;
   struct wl_display *wldisplay= 0;

   wldisplay= (struct wl_display*)EssContextGetWaylandDisplay( (EssCtx*)0 );
   if ( wldisplay )
   {
      EMERROR("EssContextGetWaylandDisplay failed to return null wayland display using null context");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   wldisplay= (struct wl_display*)EssContextGetWaylandDisplay( ctx );
   if ( wldisplay )
   {
      EMERROR("EssContextGetWaylandDisplay failed to return null wayland display using uninitialized context");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   wldisplay= (struct wl_display*)EssContextGetWaylandDisplay( ctx );
   if ( wldisplay )
   {
      EMERROR("EssContextGetWaylandDisplay failed to return null wayland display using non-wayland context");
      goto exit;
   }

   EssContextDestroy( ctx );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   wldisplay= (struct wl_display*)EssContextGetWaylandDisplay( ctx );
   if ( !wldisplay )
   {
      EMERROR("EssContextGetWaylandDisplay failed");
      goto exit;
   }

   unsetenv( "WAYLAND_DISPLAY" );

   testResult= true;

exit:

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseEssosStart( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;

   result= EssContextStart( (EssCtx*)0 );
   if ( result )
   {
      EMERROR("EssContextStart did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   usleep( 17000 );

   result= EssContextStart( ctx );
   if ( result  )
   {
      EMERROR("EssContextStart did not fail when already running");
      goto exit;
   }

   EssContextDestroy( ctx );
   ctx= 0;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   usleep( 17000 );

   result= EssContextStart( ctx );
   if ( result  )
   {
      EMERROR("EssContextStart did not fail when already running");
      goto exit;
   }

   EssContextDestroy( ctx );
   ctx= 0;

   unsetenv( "WAYLAND_DISPLAY" );

   testResult= true;

exit:

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseEssosEventLoopThrottle( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   int iterationCount, totalIterations;
   long long time1, time2, diff, total, mean;

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   totalIterations= 10;

   // Ensure throttling when calls to EssContextRunEventLoopOnce are made
   // immediately one after the other
   total= 0;
   for( iterationCount= 0; iterationCount < totalIterations; ++iterationCount )
   {
      time1= EMGetCurrentTimeMicro();
      EssContextRunEventLoopOnce( ctx );
      time2= EMGetCurrentTimeMicro();
      diff= time2-time1;
      total += diff;
   }
   mean= total/totalIterations;

   if ( mean < 8000 )
   {
      EMERROR("Ineffective event loop throttle: mean period: %lld us", mean );
      goto exit;
   }


   // Ensure no throttling when calls to EssContextRunEventLoopOnce are separated
   // by 16 ms intervals
   total= 0;
   for( iterationCount= 0; iterationCount < totalIterations; ++iterationCount )
   {
      time1= EMGetCurrentTimeMicro();
      EssContextRunEventLoopOnce( ctx );
      time2= EMGetCurrentTimeMicro();
      diff= time2-time1;
      total += diff;
      usleep( 16667 );
   }
   mean= total/totalIterations;

   if ( mean > 8000 )
   {
      EMERROR("Unexpected event loop throttle: mean period: %lld us", mean );
      goto exit;
   }

   EssContextDestroy( ctx );
   ctx= 0;

   usleep( 30000 );



   setenv( "ESSOS_NO_EVENT_LOOP_THROTTLE", "1", 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   // Ensuren no throttling when it is disabled by env var
   total= 0;
   for( iterationCount= 0; iterationCount < totalIterations; ++iterationCount )
   {
      time1= EMGetCurrentTimeMicro();
      EssContextRunEventLoopOnce( ctx );
      time2= EMGetCurrentTimeMicro();
      diff= time2-time1;
      total += diff;
   }
   mean= total/totalIterations;

   if ( mean > 8000 )
   {
      EMERROR("Unexpected event loop throttle: mean period: %lld us", mean );
      goto exit;
   }

   EssContextDestroy( ctx );
   ctx= 0;

   unsetenv( "ESSOS_NO_EVENT_LOOP_THROTTLE" );

   testResult= true;

exit:

   if ( ctx )
   {
      EssContextDestroy( ctx );
   }

   unsetenv( "ESSOS_NO_EVENT_LOOP_THROTTLE" );

   return testResult;
}

bool testCaseEssosDisplaySize( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   int displayWidth= 0;
   int displayHeight= 0;
   int width, height;

   result= EssContextSetDisplaySize( (EssCtx*)0, displayWidth, displayHeight );
   if ( result )
   {
      EMERROR("EssContextSetDisplaySize did not fail with null handle");
      goto exit;
   }

   result= EssContextGetDisplaySize( (EssCtx*)0, &displayWidth, &displayHeight );
   if ( result )
   {
      EMERROR("EssContextGetDisplaySize did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextSetDisplaySize( ctx, displayWidth, displayHeight );
   if ( result )
   {
      EMERROR("EssContextSetDisplaySize did not fail when called before init");
      goto exit;
   }

   result= EssContextGetDisplaySize( ctx, &displayWidth, &displayHeight );
   if ( result )
   {
      EMERROR("EssContextGetDisplaySize did not fail when called before init");
      goto exit;
   }

   result= EssContextInit( ctx );
   if ( result == false )
   {
      EMERROR("EssContextInit failed");
      goto exit;
   }

   displayWidth= 1000;
   displayHeight= 800;
   result= EssContextSetDisplaySize( ctx, displayWidth, displayHeight );
   if ( !result )
   {
      EMERROR("EssContextSetDisplaySize failed");
      goto exit;
   }

   result= EssContextGetDisplaySize( ctx, &width, &height );
   if ( !result )
   {
      EMERROR("EssContextGetDisplaySize failed");
      goto exit;
   }

   if ( (width != displayWidth) || (height != displayHeight) )
   {
      EMERROR("EssContextGetDisplaySize returned unexpected size: actual %dx%d expected %dx%d", width, height, displayWidth, displayHeight);
      goto exit;
   }

   testResult= true;

exit:

   if ( ctx )
   {
      EssContextDestroy( ctx );
   }

   return testResult;
}

namespace DisplaySizeChange
{

typedef struct _SettingsInfo
{
   bool wasCalled;
   int width;
   int height;
} SettingsInfo;

static void displaySize( void *userData, int width, int height )
{
   SettingsInfo *si= (SettingsInfo*)userData;
   si->wasCalled= true;
   si->width= width;
   si->height= height;
}

EssSettingsListener settingsListener=
{
   displaySize,
   0
};

}; // namespace DisplaySizeChange

bool testCaseEssosDisplaySizeChange( EMCTX *emctx )
{
   using namespace DisplaySizeChange;

   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   SettingsInfo settingsInfo;
   int displayWidth;
   int displayHeight;
   int targetIndex;
   int targetWidth[]= { 1920, 800, 1280, 0 };
   int targetHeight[]= { 1080, 400, 720, 0 };
   char work[256];

   displayWidth= 640;
   displayHeight= 480;
   EMSetDisplaySize( emctx, displayWidth, displayHeight );

   EMStart( emctx );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   memset( &settingsInfo, 0, sizeof(settingsInfo) );

   result= EssContextSetSettingsListener( ctx, &settingsInfo, &settingsListener );
   if ( result == false )
   {
      EMERROR("EssContextSetSettingsListener failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      if ( settingsInfo.wasCalled ) break;
      usleep(2000);
   }

   if ( !settingsInfo.wasCalled )
   {
      EMERROR("EssSettingsListener not called on startup");
      goto exit;
   }
   settingsInfo.wasCalled= false;

   if ( (settingsInfo.width != displayWidth) ||
        (settingsInfo.height != displayHeight) )
   {
      EMERROR("Unexpected display size on startup: expected (%d,%d) actual (%d, %d)",
              displayWidth, displayHeight, settingsInfo.width, settingsInfo.height );
      goto exit;
   }

   targetIndex= 0;
   for( ; ; )
   {
      memset( &settingsInfo, 0, sizeof(settingsInfo) );

      displayWidth= targetWidth[targetIndex];
      displayHeight= targetHeight[targetIndex];

      if ( !displayWidth || !displayHeight ) break;

      // Use EMSetDisplay size to emulate mode changing on non-KMS devices
      EMSetDisplaySize( emctx, displayWidth, displayHeight );

      // Use EssContextSetDisplayMode for mode changing on KMS devices
      sprintf(work,"%dx%d", displayWidth, displayHeight );
      EssContextSetDisplayMode( ctx, work );

      EssContextUpdateDisplay( ctx );

      for( int i= 0; i < 16; ++i )
      {
         EssContextRunEventLoopOnce( ctx );
         if ( settingsInfo.wasCalled ) break;
         usleep(2000);
      }

      if ( !settingsInfo.wasCalled )
      {
         EMERROR("EssSettingsListener not called");
         goto exit;
      }
      settingsInfo.wasCalled= false;

      if ( (settingsInfo.width != displayWidth) ||
           (settingsInfo.height != displayHeight) )
      {
         EMERROR("Unexpected display size: expected (%d,%d) actual (%d, %d)",
                 displayWidth, displayHeight, settingsInfo.width, settingsInfo.height );
         goto exit;
      }

      ++targetIndex;
   }

   testResult= true;

exit:

   EssContextDestroy( ctx );

   return testResult;
}

namespace DisplaySafeAreaChange
{

#define DEFAULT_PLANE_SAFE_BORDER_PERCENT (5)

typedef struct _SettingsInfo
{
   bool wasCalled;
   int safeX;
   int safeY;
   int safeW;
   int safeH;
} SettingsInfo;

static void displaySafeArea( void *userData, int x, int y, int width, int height )
{
   SettingsInfo *si= (SettingsInfo*)userData;
   si->wasCalled= true;
   si->safeX= x;
   si->safeY= y;
   si->safeW= width;
   si->safeH= height;
}

EssSettingsListener settingsListener=
{
   0,
   displaySafeArea
};

}; // namespace DisplaySafeAreaChange

bool testCaseEssosDisplaySafeAreaChange( EMCTX *emctx )
{
   using namespace DisplaySafeAreaChange;

   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   SettingsInfo settingsInfo;
   int displayWidth;
   int displayHeight;
   int targetIndex;
   int targetWidth[]= { 1920, 800, 1280, 0 };
   int targetHeight[]= { 1080, 400, 720, 0 };
   int safeX, safeY, safeW, safeH;
   char work[256];

   displayWidth= 640;
   displayHeight= 480;
   EMSetDisplaySize( emctx, displayWidth, displayHeight );

   EMStart( emctx );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, false );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   memset( &settingsInfo, 0, sizeof(settingsInfo) );

   result= EssContextSetSettingsListener( ctx, &settingsInfo, &settingsListener );
   if ( result == false )
   {
      EMERROR("EssContextSetSettingsListener failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      if ( settingsInfo.wasCalled ) break;
      usleep(2000);
   }

   if ( !settingsInfo.wasCalled )
   {
      EMERROR("EssSettingsListener not called on startup");
      goto exit;
   }
   settingsInfo.wasCalled= false;

   safeX= displayWidth*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
   safeY= displayHeight*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
   safeW= displayWidth-2*safeX;
   safeH= displayHeight-2*safeY;

   if ( (settingsInfo.safeX != safeX) ||
        (settingsInfo.safeY != safeY) ||
        (settingsInfo.safeW != safeW) ||
        (settingsInfo.safeH != safeH) )
   {
      EMERROR("Unexpected display safe area on startup: expected (%d,%d,%d.%d) actual (%d,%d,%d,%d)",
              safeX, safeY, safeW, safeH, settingsInfo.safeX, settingsInfo.safeY, settingsInfo.safeW, settingsInfo.safeH );
      goto exit;
   }

   targetIndex= 0;
   for( ; ; )
   {
      memset( &settingsInfo, 0, sizeof(settingsInfo) );

      displayWidth= targetWidth[targetIndex];
      displayHeight= targetHeight[targetIndex];

      if ( !displayWidth || !displayHeight ) break;

      // Use EMSetDisplay size to emulate mode changing on non-KMS devices
      EMSetDisplaySize( emctx, displayWidth, displayHeight );

      // Use EssContextSetDisplayMode for mode changing on KMS devices
      sprintf(work,"%dx%d", displayWidth, displayHeight );
      EssContextSetDisplayMode( ctx, work );

      EssContextUpdateDisplay( ctx );

      for( int i= 0; i < 16; ++i )
      {
         EssContextRunEventLoopOnce( ctx );
         if ( settingsInfo.wasCalled ) break;
         usleep(2000);
      }

      if ( !settingsInfo.wasCalled )
      {
         EMERROR("EssSettingsListener not called");
         goto exit;
      }
      settingsInfo.wasCalled= false;

      safeX= displayWidth*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
      safeY= displayHeight*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
      safeW= displayWidth-2*safeX;
      safeH= displayHeight-2*safeY;

      if ( (settingsInfo.safeX != safeX) ||
           (settingsInfo.safeY != safeY) ||
           (settingsInfo.safeW != safeW) ||
           (settingsInfo.safeH != safeH) )
      {
         EMERROR("Unexpected display safe area: expected (%d,%d,%d.%d) actual (%d,%d,%d,%d)",
                 safeX, safeY, safeW, safeH, settingsInfo.safeX, settingsInfo.safeY, settingsInfo.safeW, settingsInfo.safeH );
         goto exit;
      }

      ++targetIndex;
   }

   testResult= true;

exit:

   EssContextDestroy( ctx );

   return testResult;
}

bool testCaseEssosDisplaySizeChangeWayland( EMCTX *emctx )
{
   using namespace DisplaySizeChange;

   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;
   SettingsInfo settingsInfo;
   int displayWidth;
   int displayHeight;
   int targetIndex;
   int targetWidth[]= { 1920, 800, 1280, 0 };
   int targetHeight[]= { 1080, 400, 720, 0 };

   EMStart( emctx );

   displayWidth= 640;
   displayHeight= 480;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   WstCompositorResolutionChangeBegin( wctx );

   usleep( 17000 );

   EMSetDisplaySize( emctx, displayWidth, displayHeight );
   WstCompositorResolutionChangeEnd( wctx, displayWidth, displayHeight );

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   memset( &settingsInfo, 0, sizeof(settingsInfo) );

   result= EssContextSetSettingsListener( ctx, &settingsInfo, &settingsListener );
   if ( result == false )
   {
      EMERROR("EssContextSetSettingsListener failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      if ( settingsInfo.wasCalled ) break;
      usleep(2000);
   }

   if ( !settingsInfo.wasCalled )
   {
      EMERROR("EssSettingsListener not called on startup");
      goto exit;
   }
   settingsInfo.wasCalled= false;

   if ( (settingsInfo.width != displayWidth) ||
        (settingsInfo.height != displayHeight) )
   {
      EMERROR("Unexpected display size on startup: expected (%d,%d) actual (%d, %d)",
              displayWidth, displayHeight, settingsInfo.width, settingsInfo.height );
      goto exit;
   }

   targetIndex= 0;
   for( ; ; )
   {
      memset( &settingsInfo, 0, sizeof(settingsInfo) );

      displayWidth= targetWidth[targetIndex];
      displayHeight= targetHeight[targetIndex];

      if ( !displayWidth || !displayHeight ) break;

      WstCompositorResolutionChangeBegin( wctx );

      usleep( 17000 );

      EMSetDisplaySize( emctx, displayWidth, displayHeight );
      WstCompositorResolutionChangeEnd( wctx, displayWidth, displayHeight );

      for( int i= 0; i < 16; ++i )
      {
         EssContextRunEventLoopOnce( ctx );
         if ( settingsInfo.wasCalled ) break;
         usleep(2000);
      }

      if ( !settingsInfo.wasCalled )
      {
         EMERROR("EssSettingsListener not called");
         goto exit;
      }
      settingsInfo.wasCalled= false;

      if ( (settingsInfo.width != displayWidth) ||
           (settingsInfo.height != displayHeight) )
      {
         EMERROR("Unexpected display size: expected (%d,%d) actual (%d, %d)",
                 displayWidth, displayHeight, settingsInfo.width, settingsInfo.height );
         goto exit;
      }

      ++targetIndex;
   }

   testResult= true;

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseEssosDisplaySafeAreaChangeWayland( EMCTX *emctx )
{
   using namespace DisplaySafeAreaChange;

   bool testResult= false;
   bool result;
   EssCtx *ctx= 0;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;
   SettingsInfo settingsInfo;
   int displayWidth;
   int displayHeight;
   int targetIndex;
   int targetWidth[]= { 1920, 800, 1280, 0 };
   int targetHeight[]= { 1080, 400, 720, 0 };
   int safeX, safeY, safeW, safeH;

   EMStart( emctx );

   displayWidth= 640;
   displayHeight= 480;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   WstCompositorResolutionChangeBegin( wctx );

   usleep( 17000 );

   EMSetDisplaySize( emctx, displayWidth, displayHeight );
   WstCompositorResolutionChangeEnd( wctx, displayWidth, displayHeight );

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   memset( &settingsInfo, 0, sizeof(settingsInfo) );

   result= EssContextSetSettingsListener( ctx, &settingsInfo, &settingsListener );
   if ( result == false )
   {
      EMERROR("EssContextSetSettingsListener failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      if ( settingsInfo.wasCalled ) break;
      usleep(2000);
   }

   if ( !settingsInfo.wasCalled )
   {
      EMERROR("EssSettingsListener not called on startup");
      goto exit;
   }
   settingsInfo.wasCalled= false;

   safeX= displayWidth*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
   safeY= displayHeight*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
   safeW= displayWidth-2*safeX;
   safeH= displayHeight-2*safeY;

   if ( (settingsInfo.safeX != safeX) ||
        (settingsInfo.safeY != safeY) ||
        (settingsInfo.safeW != safeW) ||
        (settingsInfo.safeH != safeH) )
   {
      EMERROR("Unexpected display safe area on startup: expected (%d,%d,%d.%d) actual (%d,%d,%d,%d)",
              safeX, safeY, safeW, safeH, settingsInfo.safeX, settingsInfo.safeY, settingsInfo.safeW, settingsInfo.safeH );
      goto exit;
   }

   targetIndex= 0;
   for( ; ; )
   {
      memset( &settingsInfo, 0, sizeof(settingsInfo) );

      displayWidth= targetWidth[targetIndex];
      displayHeight= targetHeight[targetIndex];

      if ( !displayWidth || !displayHeight ) break;

      WstCompositorResolutionChangeBegin( wctx );

      usleep( 17000 );

      EMSetDisplaySize( emctx, displayWidth, displayHeight );
      WstCompositorResolutionChangeEnd( wctx, displayWidth, displayHeight );

      for( int i= 0; i < 16; ++i )
      {
         EssContextRunEventLoopOnce( ctx );
         if ( settingsInfo.wasCalled ) break;
         usleep(2000);
      }

      if ( !settingsInfo.wasCalled )
      {
         EMERROR("EssSettingsListener not called");
         goto exit;
      }
      settingsInfo.wasCalled= false;

      safeX= displayWidth*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
      safeY= displayHeight*DEFAULT_PLANE_SAFE_BORDER_PERCENT/100;
      safeW= displayWidth-2*safeX;
      safeH= displayHeight-2*safeY;

      if ( (settingsInfo.safeX != safeX) ||
           (settingsInfo.safeY != safeY) ||
           (settingsInfo.safeW != safeW) ||
           (settingsInfo.safeH != safeH) )
      {
         EMERROR("Unexpected display safe area: expected (%d,%d,%d.%d) actual (%d,%d,%d,%d)",
                 safeX, safeY, safeW, safeH, settingsInfo.safeX, settingsInfo.safeY, settingsInfo.safeW, settingsInfo.safeH );
         goto exit;
      }

      ++targetIndex;
   }

   testResult= true;

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

namespace BasicKeyInput
{

typedef struct _TestCtx
{
   bool wasCalled;
   bool keyAlt;
   bool keyCtrl;
   bool keyShift;
   bool keyCaps;
   int keyPressed;
} TestCtx;

static void keyPressed( void *userData, unsigned int key )
{
   TestCtx *testCtx= (TestCtx*)userData;
   testCtx->wasCalled= true;
   testCtx->keyPressed= key;
   switch( key )
   {
      case KEY_RIGHTSHIFT:
      case KEY_LEFTSHIFT:
         testCtx->keyShift= true;
         break;
      case KEY_RIGHTALT:
      case KEY_LEFTALT:
         testCtx->keyAlt= true;
         break;
      case KEY_RIGHTCTRL:
      case KEY_LEFTCTRL:
         testCtx->keyCtrl= true;
         break;
      case KEY_CAPSLOCK:
         testCtx->keyCaps= !testCtx->keyCaps;
         break;
      default:
         break;
   }
}

static void keyReleased( void *userData, unsigned int key )
{
   TestCtx *testCtx= (TestCtx*)userData;
   testCtx->wasCalled= true;
   testCtx->keyPressed= 0;
   switch( key )
   {
      case KEY_RIGHTSHIFT:
      case KEY_LEFTSHIFT:
         testCtx->keyShift= false;
         break;
      case KEY_RIGHTALT:
      case KEY_LEFTALT:
         testCtx->keyAlt= false;
         break;
      case KEY_RIGHTCTRL:
      case KEY_LEFTCTRL:
         testCtx->keyCtrl= false;
         break;
      case KEY_CAPSLOCK:
         // Nothing to do
         break;
      default:
         break;
   }
}

static EssKeyListener keyListener=
{
   keyPressed,
   keyReleased
};

};

bool testCaseEssosKeyboardBasicKeyInputWayland( EMCTX *emctx )
{
   using namespace BasicKeyInput;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   EssCtx *ctx= 0;
   TestCtx tCtx;
   TestCtx *testCtx= &tCtx;

   EMStart( emctx );

   memset( testCtx, 0, sizeof(TestCtx) );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextSetKeyListener( ctx, testCtx, &keyListener );
   if ( result == false )
   {
      EMERROR("EssContextSetKeyListener failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_A, WstKeyboard_keyState_depressed, 0 );

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssKeyListener not called");
      goto exit;
   }
   testCtx->wasCalled= false;

   if ( (testCtx->keyPressed != KEY_A) ||
        (testCtx->keyAlt || testCtx->keyCtrl || testCtx->keyShift || testCtx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 0/%d ctrl 0/%d shift 0/%d caps 0/%d",
              KEY_A, testCtx->keyPressed, testCtx->keyAlt, testCtx->keyCtrl, testCtx->keyShift, testCtx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_A, WstKeyboard_keyState_released, 0 );
   WstCompositorKeyEvent( wctx,  KEY_B, WstKeyboard_keyState_depressed, WstKeyboard_shift );

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssKeyListener not called");
      goto exit;
   }
   testCtx->wasCalled= false;

   if ( (testCtx->keyPressed != KEY_B) ||
        (testCtx->keyAlt || testCtx->keyCtrl || !testCtx->keyShift || testCtx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 0/%d ctrl 0/%d shift 1/%d caps 0/%d",
              KEY_B, testCtx->keyPressed, testCtx->keyAlt, testCtx->keyCtrl, testCtx->keyShift, testCtx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_B, WstKeyboard_keyState_released, 0 );
   WstCompositorKeyEvent( wctx,  KEY_C, WstKeyboard_keyState_depressed, WstKeyboard_alt );

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssKeyListener not called");
      goto exit;
   }
   testCtx->wasCalled= false;

   if ( (testCtx->keyPressed != KEY_C) ||
        (!testCtx->keyAlt || testCtx->keyCtrl || testCtx->keyShift || testCtx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 1/%d ctrl 0/%d shift 0/%d caps 0/%d",
              KEY_C, testCtx->keyPressed, testCtx->keyAlt, testCtx->keyCtrl, testCtx->keyShift, testCtx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_C, WstKeyboard_keyState_released, 0 );
   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_depressed, WstKeyboard_ctrl|WstKeyboard_caps );

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssKeyListener not called");
      goto exit;
   }
   testCtx->wasCalled= false;

   if ( (testCtx->keyPressed != KEY_D) ||
        (testCtx->keyAlt || !testCtx->keyCtrl || testCtx->keyShift || !testCtx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 0/%d ctrl 1/%d shift 0/%d caps 1/%d",
              KEY_D, testCtx->keyPressed, testCtx->keyAlt, testCtx->keyCtrl, testCtx->keyShift, testCtx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_released, 0 );

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssKeyListener not called");
      goto exit;
   }
   testCtx->wasCalled= false;

   testResult= true;

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

namespace RepeatKeyInput
{

typedef struct _TestCtx
{
   bool wasCalled;
   bool keyAlt;
   bool keyCtrl;
   bool keyShift;
   bool keyCaps;
   int keyPressed;
   int repeatCount;
} TestCtx;

static void keyPressed( void *userData, unsigned int key )
{
   TestCtx *testCtx= (TestCtx*)userData;
   testCtx->wasCalled= true;
   testCtx->keyPressed= key;
   switch( key )
   {
      case KEY_RIGHTSHIFT:
      case KEY_LEFTSHIFT:
         testCtx->keyShift= true;
         break;
      case KEY_RIGHTALT:
      case KEY_LEFTALT:
         testCtx->keyAlt= true;
         break;
      case KEY_RIGHTCTRL:
      case KEY_LEFTCTRL:
         testCtx->keyCtrl= true;
         break;
      case KEY_CAPSLOCK:
         testCtx->keyCaps= !testCtx->keyCaps;
         break;
      default:
         break;
   }
}

static void keyReleased( void *userData, unsigned int key )
{
   TestCtx *testCtx= (TestCtx*)userData;
   testCtx->wasCalled= true;
   testCtx->keyPressed= 0;
   switch( key )
   {
      case KEY_RIGHTSHIFT:
      case KEY_LEFTSHIFT:
         testCtx->keyShift= false;
         break;
      case KEY_RIGHTALT:
      case KEY_LEFTALT:
         testCtx->keyAlt= false;
         break;
      case KEY_RIGHTCTRL:
      case KEY_LEFTCTRL:
         testCtx->keyCtrl= false;
         break;
      case KEY_CAPSLOCK:
         // Nothing to do
         break;
      default:
         break;
   }
}

static void keyRepeat( void *userData, unsigned int key )
{
   TestCtx *testCtx= (TestCtx*)userData;
   testCtx->wasCalled= true;
   ++testCtx->repeatCount;
}

static EssKeyListener keyListener=
{
   keyPressed,
   keyReleased,
   keyRepeat
};

};

bool testCaseEssosKeyboardRepeatKeyInputWayland( EMCTX *emctx )
{
   using namespace RepeatKeyInput;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   EssCtx *ctx= 0;
   TestCtx tCtx;
   TestCtx *testCtx= &tCtx;

   EMStart( emctx );

   memset( testCtx, 0, sizeof(TestCtx) );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextSetKeyListener( ctx, testCtx, &keyListener );
   if ( result == false )
   {
      EMERROR("EssContextSetKeyListener failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_A, WstKeyboard_keyState_depressed, 0 );

   for( int i= 0; i < 100; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssKeyListener not called");
      goto exit;
   }
   if ( testCtx->repeatCount < 4 )
   {
      EMERROR("EssKeyListener keyRepeat not called: expected 4, actual %d", testCtx->repeatCount);
      goto exit;
   }
   testCtx->wasCalled= false;
   testCtx->repeatCount= 0;

   testResult= true;

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

namespace BasicPointerInput
{

typedef struct _TestCtx
{
   bool wasCalled;
   int x;
   int y;
   bool motion;
   int button;
   bool pressed;
   bool released;
} TestCtx;

void pointerMotion( void *userData, int x, int y )
{
   TestCtx *testCtx= (TestCtx*)userData;
   testCtx->wasCalled= true;
   testCtx->motion= true;
   testCtx->pressed= false;
   testCtx->released= false;
   testCtx->x= x;
   testCtx->y= y;
   testCtx->button= 0;
}

void pointerButtonPressed( void *userData, int button, int x, int y )
{
   TestCtx *testCtx= (TestCtx*)userData;
   testCtx->wasCalled= true;
   testCtx->motion= false;
   testCtx->pressed= true;
   testCtx->released= false;
   testCtx->x= x;
   testCtx->y= y;
   testCtx->button= button;
}

void pointerButtonReleased( void *userData, int button, int x, int y )
{
   TestCtx *testCtx= (TestCtx*)userData;
   testCtx->wasCalled= true;
   testCtx->motion= false;
   testCtx->pressed= false;
   testCtx->released= true;
   testCtx->x= x;
   testCtx->y= y;
   testCtx->button= button;
}

static EssPointerListener pointerListener=
{
   pointerMotion,
   pointerButtonPressed,
   pointerButtonReleased
};

};

bool testCaseEssosPointerBasicPointerInputWayland( EMCTX *emctx )
{
   using namespace BasicPointerInput;

   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   EssCtx *ctx= 0;
   TestCtx tCtx;
   TestCtx *testCtx= &tCtx;

   EMStart( emctx );

   memset( testCtx, 0, sizeof(TestCtx) );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextSetPointerListener( ctx, testCtx, &pointerListener );
   if ( result == false )
   {
      EMERROR("EssContextSetPointerListener failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   EssContextUpdateDisplay( ctx );

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }



   WstCompositorPointerEnter( wctx );
   WstCompositorPointerMoveEvent( wctx, 200, 200 );

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssPointerListener not called");
      goto exit;
   }

   if ( !testCtx->motion || testCtx->pressed || testCtx->released ||
        (testCtx->x != 200) || (testCtx->y != 200) )
   {
      EMERROR("Did not get expected event: expected/actual motion %d/%d pressed %d/%d released %d/%d x %d/%d y %d/%d button %d/%d",
              testCtx->motion, true, testCtx->pressed, false, testCtx->released, false,
               testCtx->x, 200, testCtx->y, 200, testCtx->button, 0);
      goto exit;
   }
   memset( testCtx, 0, sizeof(TestCtx) );



   WstCompositorPointerButtonEvent( wctx, BTN_LEFT, WstPointer_buttonState_depressed );

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssPointerListener not called");
      goto exit;
   }

   if ( testCtx->motion || !testCtx->pressed || testCtx->released ||
        (testCtx->x != 200) || (testCtx->y != 200) || (testCtx->button != BTN_LEFT) )
   {
      EMERROR("Did not get expected event: expected/actual motion %d/%d pressed %d/%d released %d/%d x %d/%d y %d/%d button %d/%d",
              testCtx->motion, false, testCtx->pressed, true, testCtx->released, false,
               testCtx->x, 200, testCtx->y, 200, testCtx->button, BTN_LEFT);
      goto exit;
   }
   memset( testCtx, 0, sizeof(TestCtx) );




   WstCompositorPointerButtonEvent( wctx, BTN_LEFT, WstPointer_buttonState_released );

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssPointerListener not called");
      goto exit;
   }

   if ( testCtx->motion || testCtx->pressed || !testCtx->released ||
        (testCtx->x != 200) || (testCtx->y != 200) || (testCtx->button != BTN_LEFT) )
   {
      EMERROR("Did not get expected event: expected/actual motion %d/%d pressed %d/%d released %d/%d x %d/%d y %d/%d button %d/%d",
              testCtx->motion, false, testCtx->pressed, false, testCtx->released, true,
               testCtx->x, 200, testCtx->y, 200, testCtx->button, BTN_LEFT);
      goto exit;
   }
   memset( testCtx, 0, sizeof(TestCtx) );

   testResult= true;

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   EssContextDestroy( ctx );

   WstCompositorDestroy( wctx );

   return testResult;
}

namespace BasicTerminate
{

typedef struct _TestCtx
{
   bool wasCalled;
} TestCtx;

void terminated( void *userData )
{
   TestCtx *testCtx= (TestCtx*)userData;

   testCtx->wasCalled= true;
}

static EssTerminateListener terminateListener=
{
   terminated
};

};

bool testCaseEssosTerminateListener( EMCTX *emctx )
{
   using namespace BasicTerminate;

   bool testResult= false;
   bool result;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;
   EssCtx *ctx= 0;
   TestCtx tCtx;
   TestCtx *testCtx= &tCtx;

   EMStart( emctx );

   memset( testCtx, 0, sizeof(TestCtx) );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   setenv( "WAYLAND_DISPLAY", displayName, 0 );

   result= EssContextSetTerminateListener( (EssCtx*)0, testCtx, &terminateListener );
   if ( result )
   {
      EMERROR("EssContextSetTerminateListener did not fail with null handle");
      goto exit;
   }

   ctx= EssContextCreate();
   if ( !ctx )
   {
      EMERROR("EssContextCreate failed");
      goto exit;
   }

   result= EssContextSetTerminateListener( ctx, testCtx, &terminateListener );
   if ( result == false )
   {
      EMERROR("EssContextSetTerminateListener failed");
      goto exit;
   }

   result= EssContextSetUseWayland( ctx, true );
   if ( result == false )
   {
      EMERROR("EssContextSetUseWayland failed");
      goto exit;
   }

   result= EssContextStart( ctx );
   if ( result == false )
   {
      EMERROR("EssContextStart failed");
      goto exit;
   }

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      usleep(2000);
   }

   WstCompositorDestroy( wctx );
   wctx= 0;

   for( int i= 0; i < 16; ++i )
   {
      EssContextRunEventLoopOnce( ctx );
      if ( testCtx->wasCalled )
      {
         break;
      }
      usleep(2000);
   }

   if ( !testCtx->wasCalled )
   {
      EMERROR("EssTerminateListener not called");
      goto exit;
   }

   testResult= true;

exit:

   unsetenv( "WAYLAND_DISPLAY" );

   EssContextDestroy( ctx );

   if ( wctx )
   {
      WstCompositorDestroy( wctx );
   }

   return testResult;
}

