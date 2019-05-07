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
#include <csetjmp>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include <vector>
#include <string>

#include "westeros-ut-em.h"

#include "westeros-compositor.h"

#include "soc-tests.h"
#include "test-egl.h"
#include "test-render.h"
#include "test-keyboard.h"
#include "test-pointer.h"
#include "test-touch.h"
#include "test-simpleshell.h"
#include "test-essos.h"

static bool invokeTestCase( TESTCASE testCase, std::string &detail );
static bool testCaseAPIDisplayName( EMCTX *ctx );
static bool testCaseAPIGetLastError( EMCTX *ctx );
static bool testCaseAPIFrameRate( EMCTX *ctx );
static bool testCaseAPINativeWindow( EMCTX *emctx );
static bool testCaseAPIRendererModule( EMCTX *emctx );
static bool testCaseAPIIsNested( EMCTX *emctx );
static bool testCaseAPIIsRepeater( EMCTX *emctx );
static bool testCaseAPIIsEmbedded( EMCTX *emctx );
static bool testCaseAPIVpcBridge( EMCTX *emctx );
static bool testCaseAPIOutputSize( EMCTX *emctx );
static bool testCaseAPINestedDisplayName( EMCTX *emctx );
static bool testCaseAPINestedSize( EMCTX *emctx );
static bool testCaseAPIAllowCursorModification( EMCTX *emctx );
static bool testCaseAPISetDefaultCursor( EMCTX *emctx );
static bool testCaseAPISetDefaultCursorEmbedded( EMCTX *emctx );
static bool testCaseAPIInvalidateScene( EMCTX *emctx );
static bool testCaseAPISetTerminatedCallback( EMCTX *emctx );
static bool testCaseAPISetDispatchCallback( EMCTX *emctx );
static bool testCaseAPISetInvalidateCallback( EMCTX *emctx );
static bool testCaseAPISetOutputNestedCallback( EMCTX *emctx );
static bool testCaseAPISetClientStatusCallback( EMCTX *emctx );
static bool testCaseAPILaunchClient( EMCTX *emctx );
static bool testCaseAPIAddModule( EMCTX *emctx );


TESTCASE genericTests[]=
{
   { "testAPIDisplayName",
     "Test compositor display name API paths",
     testCaseAPIDisplayName
   },
   { "testAPIGetLastError",
     "Test compositor get last error API paths",
     testCaseAPIGetLastError
   },
   { "testAPIFrameRate",
     "Test compositor framerate API paths",
     testCaseAPIFrameRate
   },
   { "testAPINativeWindow",
     "Test compositor native window API paths",
     testCaseAPINativeWindow
   },
   { "testAPIRendererModule",
     "Test compositor renderer module API paths",
     testCaseAPIRendererModule
   },
   { "testAPIIsNested",
     "Test compositor IsNested API paths",
     testCaseAPIIsNested
   },
   { "testAPIIsRepeater",
     "Test compositor IsRepeater API paths",
     testCaseAPIIsRepeater
   },
   { "testAPIIsEmbedded",
     "Test compositor IsEmbedded API paths",
     testCaseAPIIsEmbedded
   },
   { "testAPIVpcBridge",
     "Test compositor Vpc bridge API paths",
     testCaseAPIVpcBridge
   },
   { "testAPIOutputSize",
     "Test compositor output size API paths",
     testCaseAPIOutputSize
   },
   { "testAPINestedDisplayName",
     "Test compositor nested display name API paths",
     testCaseAPINestedDisplayName
   },
   { "testAPINestedSize",
     "Test compositor nested size API paths",
     testCaseAPINestedSize
   },
   { "testAPIAllowCursorModification",
     "Test compositor allow cursor modification API paths",
     testCaseAPIAllowCursorModification
   },
   { "testAPISetDefaultCursor",
     "Test compositor set default cursor API paths",
     testCaseAPISetDefaultCursor
   },
   { "testAPISetDefaultCursorEmbedded",
     "Test compositor set default cursor API paths with embedded composition",
     testCaseAPISetDefaultCursorEmbedded
   },
   { "testAPIInvalidateScene",
     "Test compositor invalidate scene API paths",
     testCaseAPIInvalidateScene
   },
   { "testAPISetTerminatedCallback",
     "Test compositor set terminated callback API paths",
     testCaseAPISetTerminatedCallback
   },
   { "testAPISetDispatchCallback",
     "Test compositor set dispatch callback API paths",
     testCaseAPISetDispatchCallback
   },
   { "testAPISetInvalidateCallback",
     "Test compositor set invalidate callback API paths",
     testCaseAPISetInvalidateCallback
   },
   { "testAPISetOutputNestedCallback",
     "Test compositor set output nested callback API paths",
     testCaseAPISetOutputNestedCallback
   },
   { "testAPISetClientStatusCallback",
     "Test compositor set client status callback API paths",
     testCaseAPISetClientStatusCallback
   },
   { "testAPILaunchClient",
     "Test compositor launch client API paths",
     testCaseAPILaunchClient
   },
   { "testAPIAddModule",
     "Test compositor add module API paths",
     testCaseAPIAddModule
   },
   { "testEssosAPIUseWayland",
     "Test Essos use wayland API paths",
     testCaseEssosUseWayland
   },
   { "testEssosAPIUseDirect",
     "Test Essos use direct API paths",
     testCaseEssosUseDirect
   },
   { "testEssosAPIName",
     "Test Essos name API paths",
     testCaseEssosName
   },
   { "testEssosAPIEGLSurfaceAttributes",
     "Test Essos EGL surface attributes API paths",
     testCaseEssosEGLSurfaceAttributes
   },
   { "testEssosAPIEGLContextAttributes",
     "Test Essos EGL context attributes API paths",
     testCaseEssosEGLContextAttributes
   },
   { "testEssosAPIInitialWindowSize",
     "Test Essos initial window size API paths",
     testCaseEssosInitialWindowSize
   },
   { "testEssosAPISwapInterval",
     "Test Essos swap interal API paths",
     testCaseEssosSwapInterval
   },
   { "testEssosAPIInit",
     "Test Essos init API paths",
     testCaseEssosInit
   },
   { "testEssosAPIGetEGLDisplayType",
     "Test Essos get EGL display type API paths",
     testCaseEssosGetEGLDisplayType
   },
   { "testEssosAPICreateNativeWindow",
     "Test Essos create native window API paths",
     testCaseEssosCreateNativeWindow
   },
   { "testEssosAPIGetWaylandDisplay",
     "Test Essos get wayland display API paths",
     testCaseEssosGetWaylandDisplay
   },
   { "testEssosAPIStart",
     "Test Essos start API paths",
     testCaseEssosStart
   },
   { "testEssosEventLoopThrottle",
     "Test Essos event loop throttle behaviour",
     testCaseEssosEventLoopThrottle
   },
   { "testEssosDisplaySizeChange",
     "Test Essos display size change notification",
     testCaseEssosDisplaySizeChange
   },
   { "testEssosDisplaySafeAreaChange",
     "Test Essos display safe area change notification",
     testCaseEssosDisplaySafeAreaChange
   },
   { "testEssosDisplaySizeChangeWayland",
     "Test Essos display size change notification with Wayland",
     testCaseEssosDisplaySizeChangeWayland
   },
   { "testEssosDisplaySafeAreaChangeWayland",
     "Test Essos display safe area change notification with Wayland",
     testCaseEssosDisplaySafeAreaChangeWayland
   },
   { "testEssosKeyboardBasicKeyInputWayland",
     "Test Essos key events with Wayland",
     testCaseEssosKeyboardBasicKeyInputWayland
   },
   { "testRenderBasicComposition",
     "Test compositor basic composition",
     testCaseRenderBasicComposition
   },
   { "testRenderBasicCompositionEmbedded",
     "Test embedded compositor basic composition",
     testCaseRenderBasicCompositionEmbedded
   },
   { "testRenderBasicCompositionNested",
     "Test nested compositor basic composition",
     testCaseRenderBasicCompositionNested
   },
   { "testRenderBasicCompositionRepeating",
     "Test repeating compositor basic composition",
     testCaseRenderBasicCompositionRepeating
   },
   { "testRenderWaylandThreading",
     "Test compositor for wayland threading issues",
     testCaseRenderWaylandThreading
   },
   { "testRenderWaylandThreadingEmbedded",
     "Test embedded compositor for wayland threading issues",
     testCaseRenderWaylandThreadingEmbedded
   },
   { "testKeyboardBasicKeyInput",
     "Test basic keyboard input",
     testCaseKeyboardBasicKeyInput
   },
   { "testKeyboardBasicKeyInputRepeater",
     "Test basic keyboard input with repeating compositor",
     testCaseKeyboardBasicKeyInputRepeater
   },
   { "testPointerEnterLeave",
     "Test pointer entering and leaving a surface",
     testCasePointerEnterLeave
   },
   { "testPointerBasicFocus",
     "Test changing focus with pointer",
     testCasePointerBasicFocus
   },
   { "testPointerBasicFocusRepeater",
     "Test changing focus with pointer with repeating composition",
     testCasePointerBasicFocusRepeater
   },
   { "testTouchBasicTouchInput",
     "Test basic touch input",
     testCaseTouchBasicTouchInput
   },
   { "testTouchBasicTouchInputRepeater",
     "Test basic touch input with repeating composition",
     testCaseTouchBasicTouchInputRepeater
   },
   { "testTouchBasicFocus",
     "Test changing focus with touch",
     testCaseTouchBasicFocus
   },
   { "testTouchBasicFocusRepeater",
     "Test changing focus with touch with repeating composition",
     testCaseTouchBasicFocusRepeater
   },
   { "testSimpleShellBasic",
     "Test basic simple shell paths",
     testCaseSimpleShellBasic
   },
   { "testSimpleShellBasicEmbedded",
     "Test basic simple shell paths with embedded composition",
     testCaseSimpleShellBasicEmbedded
   },
   { "testSimpleShellBasicRepeater",
     "Test basic simple shell paths with repeating composition",
     testCaseSimpleShellBasicRepeater
   },
   {
     "", "", (TESTCASEFUNC)0
   }
};

#include "soc-tests.h"

static int gTestRunCount= 0;
static int gTestFailCount= 0;
static bool gAbortTests= false;
static pthread_t gWatchDogThreadId= 0;
static bool gWatchDogReset= false;
static bool gWatchDogStopRequested= false;
static bool gWatchDogViolation= false;
static long long gWatchDogLimitMillis= 30000;
static jmp_buf gEnv;

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long currentTimeMillis;

   gettimeofday(&tv,0);
   currentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return currentTimeMillis;
}

static void signalHandler(int signum)
{
   longjmp( gEnv, signum );
}

void* watchDogThread( void *arg )
{
   long long baseTime, now;

   baseTime= getCurrentTimeMillis();

   while( !gWatchDogStopRequested )
   {
      if ( gWatchDogReset )
      {
         gWatchDogReset= false;
         baseTime= getCurrentTimeMillis();         
      }

      now= getCurrentTimeMillis();
      if ( now-baseTime >= gWatchDogLimitMillis )
      {
         gWatchDogViolation= true;
         printf("Watchdog limit exceeded\n");
         abort();
      }

      usleep( 1000000 );
   }

   return NULL;
}

bool startWatchDog( void )
{
   bool result= false;
   int rc;

   rc= pthread_create( &gWatchDogThreadId, NULL, watchDogThread, NULL );
   if ( rc )
   {
      printf("Error: unable to start watchdog thread\n");
      goto exit;
   }

   result= true;

exit:
   return result;
}

void resetWatchDog( void )
{
   gWatchDogReset= true;
}

void stopWatchDog( void )
{
   if ( gWatchDogThreadId )
   {
      gWatchDogStopRequested= true;
      pthread_join( gWatchDogThreadId, NULL );
      gWatchDogThreadId= 0;
   }
}


static void showUsage( void )
{
   printf("Usage:\n");
   printf(" westeros-unittest <options> [<testname>]\n");
   printf("  testname - name of test to run, otherwise run all tests\n");
   printf("where: options are:\n");
   printf(" -? : show usage\n");
   printf("\n");
}

int main( int argc, const char **argv )
{
   int rc= -1;
   int testIndex;
   int argidx;
   const char *testName= 0;
   struct sigaction sigint;
   std::vector<std::string> failedTests;

   printf("Westeros Unit Test\n\n");

   argidx= 1;
   while( argidx < argc )
   {
      if ( argv[argidx][0] == '-' )
      {
         switch( argv[argidx][1] )
         {
            case '?':
               showUsage();
               goto exit;
            default:
               printf( "unknown option %s\n\n", argv[argidx] );
               exit( -1 );
               break;
         }
      }
      else
      {
         if ( !testName )
         {
            testName= argv[argidx];
         }
         else
         {
            printf( "ignoring extra argument: %s\n", argv[argidx] );
         }
      }
      ++argidx;
   }

   sigint.sa_handler= signalHandler;
   sigemptyset(&sigint.sa_mask);
   sigint.sa_flags= SA_RESETHAND;
   sigaction(SIGINT, &sigint, NULL);
   sigaction(SIGABRT, &sigint, NULL);
   sigaction(SIGSEGV, &sigint, NULL);
   sigaction(SIGFPE, &sigint, NULL);
   sigaction(SIGILL, &sigint, NULL);
   sigaction(SIGBUS, &sigint, NULL);


   startWatchDog();

   // Run generic tests
   testIndex= 0;
   TESTCASE testCase;
   while ( genericTests[testIndex].func )
   {
      testCase= genericTests[testIndex];
      if ( !testName || !strcmp( testName, testCase.name ) )
      {
         std::string detail;
         if ( !invokeTestCase( testCase, detail ) )
         {
            failedTests.push_back( testCase.name + std::string(": ") + detail );
         }
      }
      ++testIndex;
      if ( gAbortTests ) goto done_tests;
   }

   // Run Soc specific tests
   testIndex= 0;
   do
   {
      testCase= getSocTest( testIndex );
      if ( testCase.func )
      {
         if ( !testName || !strcmp( testName, testCase.name ) )
         {
            std::string detail;
            if ( !invokeTestCase( testCase, detail ) )
            {
               failedTests.push_back( testCase.name + std::string(": ") + detail );
            }
         }
      }      
      ++testIndex;
      if ( gAbortTests ) goto done_tests;
   }
   while( testCase.func );

   stopWatchDog();

done_tests:
   rc= 0;
   printf("=============================================================================\n");
   printf("Tests run: %d\n", gTestRunCount );
   printf("Tests failed: %d\n", gTestFailCount );
   if ( failedTests.size() )
   {
      rc= -1;
      for( int i= 0; i < failedTests.size(); ++i )
      {
         printf("  %s\n", failedTests[i].c_str() );
      }
   }
   printf("=============================================================================\n");

exit:

   return rc;
}

static bool invokeTestCase( TESTCASE testCase, std::string &detail )
{
   bool result= false;
   EMCTX *emctx= 0;
   int n;

   resetWatchDog();

   emctx= EMCreateContext();
   if ( emctx )
   {
      ++gTestRunCount;

      printf("-----------------------------------------------------------------------------\n");
      printf("Running test: %s...\n", testCase.name);
      n= setjmp( gEnv );
      if ( n == 0 )
      {
         result= testCase.func( emctx );
      }
      else
      {
         result= false;
         if ( gWatchDogViolation )
         {
            gWatchDogViolation= false;
            EMERROR("Watchdog violation");
         }
         else
         {
            EMERROR("Crash: signum: %d", n);
         }
         gAbortTests= true;
      }
      if( !result )
      {
         printf("-----------------------------------------------------------------------------\n");
         printf("Test %d (%s) failed\nTest description:\n%s\nMessage:\n%s\n", gTestRunCount, testCase.name, testCase.desc, EMGetError( emctx ));
         printf("-----------------------------------------------------------------------------\n");
         detail += EMGetError( emctx );
         ++gTestFailCount;
      }

      EMDestroyContext( emctx );
   }

   return result;
}

static bool testCaseAPIDisplayName( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   size_t len;
   const char *value;
   const char *minAutoName= "westeros-";
   const char *displayName= "display0";

   value= WstCompositorGetDisplayName( (WstCompositor*)0 );
   if ( value != 0 )
   {
      EMERROR( "WstCompositorGetDisplayName did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( value == 0 )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return auto-generated name" );
      goto exit;
   }

   len= strlen( value );
   if ( len <= strlen(minAutoName) )
   {
      EMERROR( "WstCompositorGetDisplayName returned malformed (length) auto-generated name: len %d", len );
      goto exit;
   }

   if ( strncmp( value, minAutoName, strlen(minAutoName)) != 0 )
   {
      EMERROR( "WstCompositorGetDisplayName returned malformed (format) auto-generated name: (%s)", value );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   value= WstCompositorGetDisplayName( wctx );
   if ( 
         (value == 0) ||
         (strcmp( value, displayName) != 0 )
      )
   {
      EMERROR( "WstCompositorGetDisplayName failed to return expected name: expected (%s) actual (%s)", displayName, value );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result != false )
   {
      EMERROR( "WstCompositorSetDisplayName while running did not fail" );
      goto exit;
   }

   WstCompositorDestroy( wctx );


   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIGetLastError( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *detail= 0;

   detail= WstCompositorGetLastErrorDetail( (WstCompositor*)0 );
   if ( detail )
   {
      EMERROR("WstCompositorGetLastErrorDetail did not fail with null handle");
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, "foo" );
   if ( result != false )
   {
      EMERROR( "WstCompositorSetDisplayName while running did not fail" );
      goto exit;
   }

   detail= WstCompositorGetLastErrorDetail( wctx );
   if ( !detail || (strlen(detail) == 0) )
   {
      EMERROR("Failed to return error detail");
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIFrameRate( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   int value;
   int frameRate= 51;

   value= WstCompositorGetFrameRate( (WstCompositor*)0 );
   if ( value != 0 )
   {
      EMERROR( "WstCompositorGetFrameRate did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }
   
   result= WstCompositorSetFrameRate( wctx, 0 );
   if ( result != false )
   {
      EMERROR( "WstCompositorSetFrameRate did not fail for frame rate zero" );
      goto exit;
   }
   
   result= WstCompositorSetFrameRate( wctx, frameRate );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetFrameRate failed" );
      goto exit;
   }

   value= WstCompositorGetFrameRate( wctx );
   if ( value != frameRate )
   {
      EMERROR( "WstCompositorGetFrameRate did not return expected rate: expected %d actual %d", frameRate, value );
      goto exit;
   }   

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPINativeWindow( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   void *nativeWindow= 0;

   result= WstCompositorSetNativeWindow( (WstCompositor*)0, nativeWindow );
   if ( result != false )
   {
      EMERROR( "WstCompositorSetNativeWindow did not fail with null handle" );
      goto exit;
   }
   
   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetNativeWindow( wctx, nativeWindow );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetNativeWindow failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRenderedModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   result= WstCompositorSetNativeWindow( wctx, nativeWindow );
   if ( result != false )
   {
      EMERROR( "WstCompositorSetNativeWindow while running did not fail" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIRendererModule( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *rendererName1= "libwesteros_render_gl.so.0.0.0";
   const char *rendererName2= "libwesteros_render_embedded.so.0.0.0";
   const char *value;

   result= WstCompositorSetRendererModule( (WstCompositor*)0, rendererName1 );
   if ( result != false )
   {
      EMERROR( "WstCompositorSetRendererModule did not fail with null handle" );
      goto exit;
   }
   
   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, rendererName2 );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, rendererName1 );
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

   result= WstCompositorSetRendererModule( wctx, rendererName2 );
   if ( result != false )
   {
      EMERROR( "WstCompositorSetRendererModule while running did not fail" );
      goto exit;
   }

   value= WstCompositorGetRendererModule( wctx );
   if ( 
        !value ||
        (strcmp( value, rendererName1 ) != 0 )
      )
   {
      EMERROR( "WstCompositorGetRendererModule failed to return expected render module name: expected (%s) actual ( %s)", rendererName1, value );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIIsNested( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool isNested;
   WstCompositor *wctx= 0;

   result= WstCompositorSetIsNested( (WstCompositor*)0, true );
   if ( result )
   {
      EMERROR( "WstCompositorSetIsNested did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetIsNested( wctx, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   isNested= WstCompositorGetIsNested( wctx );
   if ( isNested != true )
   {
      EMERROR( "WstCompositorGetIsNested returned unexpected result: expected (%d) actual (%d)", isNested, true );
      goto exit;
   }

   result= WstCompositorSetIsNested( wctx, false );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   isNested= WstCompositorGetIsNested( wctx );
   if ( isNested != false )
   {
      EMERROR( "WstCompositorGetIsNested returned unexpected result: expected (%d) actual (%d)", isNested, false );
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

   result= WstCompositorSetIsNested( wctx, true );
   if ( result )
   {
      EMERROR( "WstCompositorSetIsNested while running did not fail" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIIsRepeater( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool isRepeater;
   WstCompositor *wctx= 0;

   result= WstCompositorSetIsRepeater( (WstCompositor*)0, true );
   if ( result )
   {
      EMERROR( "WstCompositorSetIsRepeater did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctx, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsRepeater failed" );
      goto exit;
   }

   isRepeater= WstCompositorGetIsNested( wctx );
   if ( isRepeater != true )
   {
      EMERROR( "WstCompositorGetIsRepeater returned unexpected result: expected (%d) actual (%d)", isRepeater, true );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctx, false );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsRepeater failed" );
      goto exit;
   }

   result= WstCompositorSetIsNested( wctx, false );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   isRepeater= WstCompositorGetIsRepeater( wctx );
   if ( isRepeater != false )
   {
      EMERROR( "WstCompositorGetIsRepeater returned unexpected result: expected (%d) actual (%d)", isRepeater, false );
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

   result= WstCompositorSetIsRepeater( wctx, true );
   if ( result )
   {
      EMERROR( "WstCompositorSetIsRepeater while running did not fail" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIIsEmbedded( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool isEmbedded;
   WstCompositor *wctx= 0;

   result= WstCompositorSetIsEmbedded( (WstCompositor*)0, true );
   if ( result )
   {
      EMERROR( "WstCompositorSetIsEmbedded did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
      goto exit;
   }

   isEmbedded= WstCompositorGetIsEmbedded( wctx );
   if ( isEmbedded != true )
   {
      EMERROR( "WstCompositorGetIsEmbedded returned unexpected result: expected (%d) actual (%d)", isEmbedded, true );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, false );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
      goto exit;
   }

   isEmbedded= WstCompositorGetIsEmbedded( wctx );
   if ( isEmbedded != false )
   {
      EMERROR( "WstCompositorGetIsEmbedded returned unexpected result: expected (%d) actual (%d)", isEmbedded, false );
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

   result= WstCompositorSetIsEmbedded( wctx, true );
   if ( result )
   {
      EMERROR( "WstCompositorSetIsEmbedded while running did not fail" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIVpcBridge( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   const char *value;
   WstCompositor *wctx= 0;
   char *bridgeToDisplayName= "outer0";

   result= WstCompositorSetVpcBridge( (WstCompositor*)0, bridgeToDisplayName );
   if ( result )
   {
      EMERROR( "WstCompositorSetVpcBridge did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetVpcBridge( wctx, bridgeToDisplayName );
   if ( result )
   {
      EMERROR( "WstCompositorSetVpcBridge did not fail with non-embedded compositor" );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
      goto exit;
   }

   result= WstCompositorSetVpcBridge( wctx, bridgeToDisplayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetVpcBridge failed" );
      goto exit;
   }

   value= WstCompositorGetVpcBridge( wctx );
   if ( 
         (value == 0) ||
         (strcmp( value, bridgeToDisplayName) != 0 )
      )
   {
      EMERROR( "WstCompositorGetVpcBridge failed to return expected name: expected (%s) actual (%s)", bridgeToDisplayName, value );
      goto exit;
   }

   result= WstCompositorSetVpcBridge( wctx, 0 );
   if ( !result )
   {
      EMERROR( "WstCompositorSetVpcBridge failed" );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, false );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
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

   result= WstCompositorSetVpcBridge( wctx, bridgeToDisplayName );
   if ( result )
   {
      EMERROR( "WstCompositorSetVpcBridge while running did not fail" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIOutputSize( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   int width, height, valueWidth, valueHeight;
   WstCompositor *wctx= 0;

   width= 800;
   height= 400;
   result= WstCompositorSetOutputSize( (WstCompositor*)0, width, height );
   if ( result )
   {
      EMERROR( "WstCompositorSetOutputSize did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetOutputSize( wctx, width, height );
   if ( !result )
   {
      EMERROR( "WstCompositorSetOutputSize failed" );
      goto exit;
   }

   result= WstCompositorSetOutputSize( wctx, 0, height );
   if ( result )
   {
      EMERROR( "WstCompositorSetOutputSize did not fail with zero width" );
      goto exit;
   }

   result= WstCompositorSetOutputSize( wctx, width, 0 );
   if ( result )
   {
      EMERROR( "WstCompositorSetOutputSize did not fail with zero height" );
      goto exit;
   }

   result= WstCompositorSetOutputSize( wctx, -1, -2 );
   if ( result )
   {
      EMERROR( "WstCompositorSetOutputSize did not fail with negative dimensions" );
      goto exit;
   }

   WstCompositorGetOutputSize( wctx, &valueWidth, &valueHeight );
   if ( (valueWidth != width) || (valueHeight != height) )
   {
      EMERROR( "WstCompositorGetOutputSize failed to return expected values: expected (%dx%d) actual (%dx%d)", width, height, valueWidth, valueHeight );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPINestedDisplayName( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *nestedDisplayName= "";
   const char *value;

   result= WstCompositorSetNestedDisplayName( (WstCompositor*)0, nestedDisplayName );
   if ( result )
   {
      EMERROR( "WstCompositorSetNestedDisplayName did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctx, nestedDisplayName );
   if ( result )
   {
      EMERROR( "WstCompositorSetNestedDisplayName did not fail with zero length name" );
      goto exit;
   }

   nestedDisplayName= "012345678901234567890123456789012";
   result= WstCompositorSetNestedDisplayName( wctx, nestedDisplayName );
   if ( result )
   {
      EMERROR( "WstCompositorSetNestedDisplayName did not fail with name that exceeds max length" );
      goto exit;
   }

   nestedDisplayName= "foo";
   result= WstCompositorSetNestedDisplayName( wctx, nestedDisplayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetNestedDisplayName failed" );
      goto exit;
   }

   nestedDisplayName= "nested0";
   result= WstCompositorSetNestedDisplayName( wctx, nestedDisplayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetNestedDisplayName failed" );
      goto exit;
   }

   value= WstCompositorGetNestedDisplayName( wctx );
   if ( 
         (value == 0) ||
         (strcmp( value, nestedDisplayName) != 0 )
      )
   {
      EMERROR( "WstCompositorGetNestedDisplayName failed to return expected name: expected (%s) actual (%s)", nestedDisplayName, value );
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

   nestedDisplayName= "foo";
   result= WstCompositorSetNestedDisplayName( wctx, nestedDisplayName );
   if ( result )
   {
      EMERROR( "WstCompositorSetNestedDisplayName while running did not fail" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPINestedSize( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   unsigned width, height, valueWidth, valueHeight;
   WstCompositor *wctx= 0;

   width= 800;
   height= 400;
   result= WstCompositorSetNestedSize( (WstCompositor*)0, width, height );
   if ( result )
   {
      EMERROR( "WstCompositorSetNestedSize did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetNestedSize( wctx, width, height );
   if ( !result )
   {
      EMERROR( "WstCompositorSetNestedSize failed" );
      goto exit;
   }

   result= WstCompositorSetNestedSize( wctx, 0, height );
   if ( result )
   {
      EMERROR( "WstCompositorSetNestedSize did not fail with zero width" );
      goto exit;
   }

   result= WstCompositorSetNestedSize( wctx, width, 0 );
   if ( result )
   {
      EMERROR( "WstCompositorSetNestedSize did not fail with zero height" );
      goto exit;
   }

   result= WstCompositorSetNestedSize( wctx, 0, 0 );
   if ( result )
   {
      EMERROR( "WstCompositorSetNestedSize did not fail with all zero dimensions" );
      goto exit;
   }

   WstCompositorGetNestedSize( wctx, &valueWidth, &valueHeight );
   if ( (valueWidth != width) || (valueHeight != height) )
   {
      EMERROR( "WstCompositorGetNestedSize failed to return expected values: expected (%ux%u) actual (%ux%u)", width, height, valueWidth, valueHeight );
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

   width= 800;
   height= 400;
   result= WstCompositorSetNestedSize( wctx, width, height );
   if ( result )
   {
      EMERROR( "WstCompositorSetNestedSize while running did not fail" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIInvalidateScene( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;

   WstCompositorInvalidateScene( (WstCompositor*)0 );

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
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

   usleep( 33000 );
   
   WstCompositorInvalidateScene( wctx );

   usleep( 33000 );

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPIAllowCursorModification( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   bool value;
   WstCompositor *wctx= 0;

   result= WstCompositorSetAllowCursorModification( (WstCompositor*)0, true );
   if ( result )
   {
      EMERROR( "WstCompositorSetAllowCursorModification did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   value= WstCompositorGetAllowCursorModification( wctx );
   if ( value != false )
   {
      EMERROR( "WstCompositorGetAllowCursorModification failed to return expected value: expected (%d) actual (%d)", false, value );
      goto exit;
   }

   result= WstCompositorSetAllowCursorModification( wctx, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetAllowCursorModification failed" );
      goto exit;
   }

   value= WstCompositorGetAllowCursorModification( wctx );
   if ( value != true )
   {
      EMERROR( "WstCompositorGetAllowCursorModification failed to return expected value: expected (%d) actual (%d)", true, value );
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

   result= WstCompositorSetAllowCursorModification( wctx, true );
   if ( result )
   {
      EMERROR( "WstCompositorSetAllowCursorModification while running did not fail" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   return testResult;
}

static bool testCaseAPISetDefaultCursor( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   unsigned char *imgData= 0;
   int cursorWidth, cursorHeight;
   int hotSpotX, hotSpotY;
   int imgDataSize;
   const char *displayName= "display0";
   const char *displayName2= "display1";
   WstCompositor *wctx= 0;
   WstCompositor *wctx2= 0;

   hotSpotX= 0;
   hotSpotY= 0;
   cursorWidth= 32;
   cursorHeight= 32;
   imgDataSize= cursorWidth*cursorHeight*4;

   imgData= (unsigned char*)malloc( imgDataSize );
   if ( !imgData )
   {
      EMERROR("No memory for image data");
      goto exit;
   }

   result= WstCompositorSetDefaultCursor( (WstCompositor*)0, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( result )
   {
      EMERROR( "WstCompositorSetDefaultCursor did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetDefaultCursor( wctx, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( result )
   {
      EMERROR( "WstCompositorSetDefaultCursor did not fail with non-running compositor" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   wctx2= WstCompositorCreate();
   if ( !wctx2 )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx2, displayName2 );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetIsNested( wctx2, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctx2, displayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx2, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx2 );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   result= WstCompositorSetDefaultCursor( wctx2, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( result )
   {
      EMERROR( "WstCompositorSetDefaultCursor did not fail with nested compositor" );
      goto exit;
   }

   WstCompositorDestroy( wctx2 );

   wctx2= WstCompositorCreate();
   if ( !wctx2 )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx2, displayName2 );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx2, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctx2, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsRepeater failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctx2, displayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx2 );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   result= WstCompositorSetDefaultCursor( wctx2, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( result )
   {
      EMERROR( "WstCompositorSetDefaultCursor did not fail with repeating compositor" );
      goto exit;
   }

   WstCompositorDestroy( wctx2 );

   result= WstCompositorSetDefaultCursor( wctx, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( !result )
   {
      EMERROR( "WstCompositorSetDefaultCursor failed" );
      goto exit;
   }

   // Allow default cursor to become active
   usleep( 100000 );

   result= WstCompositorSetDefaultCursor( wctx, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( !result )
   {
      EMERROR( "WstCompositorSetDefaultCursor failed when called a second time" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   if ( imgData )
   {
      free( imgData );
   }

   return testResult;
}

static bool testCaseAPISetDefaultCursorEmbedded( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   unsigned char *imgData= 0;
   int cursorWidth, cursorHeight;
   int hotSpotX, hotSpotY;
   int imgDataSize;
   const char *displayName= "display0";
   const char *displayName2= "display1";
   WstCompositor *wctx= 0;
   WstCompositor *wctx2= 0;

   hotSpotX= 0;
   hotSpotY= 0;
   cursorWidth= 32;
   cursorHeight= 32;
   imgDataSize= cursorWidth*cursorHeight*4;

   imgData= (unsigned char*)malloc( imgDataSize );
   if ( !imgData )
   {
      EMERROR("No memory for image data");
      goto exit;
   }

   result= WstCompositorSetDefaultCursor( (WstCompositor*)0, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( result )
   {
      EMERROR( "WstCompositorSetDefaultCursor did not fail with null handle" );
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_embedded.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
      goto exit;
   }

   result= WstCompositorSetDefaultCursor( wctx, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( result )
   {
      EMERROR( "WstCompositorSetDefaultCursor did not fail with non-running compositor" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   wctx2= WstCompositorCreate();
   if ( !wctx2 )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx2, displayName2 );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetIsNested( wctx2, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctx2, displayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx2, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx2 );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   result= WstCompositorSetDefaultCursor( wctx2, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( result )
   {
      EMERROR( "WstCompositorSetDefaultCursor did not fail with nested compositor" );
      goto exit;
   }

   WstCompositorDestroy( wctx2 );

   wctx2= WstCompositorCreate();
   if ( !wctx2 )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx2, displayName2 );
   if ( !result )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx2, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctx2, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsRepeater failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctx2, displayName );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx2 );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   result= WstCompositorSetDefaultCursor( wctx2, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( result )
   {
      EMERROR( "WstCompositorSetDefaultCursor did not fail with repeating compositor" );
      goto exit;
   }

   WstCompositorDestroy( wctx2 );

   result= WstCompositorSetDefaultCursor( wctx, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( !result )
   {
      EMERROR( "WstCompositorSetDefaultCursor failed" );
      goto exit;
   }

   // Allow default cursor to become active
   usleep( 100000 );

   result= WstCompositorSetDefaultCursor( wctx, imgData, cursorWidth, cursorHeight, hotSpotX, hotSpotY );
   if ( !result )
   {
      EMERROR( "WstCompositorSetDefaultCursor failed when called a second time" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   testResult= true;

exit:

   if ( imgData )
   {
      free( imgData );
   }

   return testResult;
}

static void terminatedCallback( WstCompositor *ctx, void *userData )
{
   bool *wasCalled= (bool*)userData;

   *wasCalled= true;
}

static bool testCaseAPISetTerminatedCallback( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   WstCompositor *wctxRepeater= 0;
   const char *displayName= "test0";
   bool callbackCalled= false;

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

   wctxRepeater= WstCompositorCreate();
   if ( !wctxRepeater )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctxRepeater, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctxRepeater, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetTerminatedCallback( wctxRepeater, terminatedCallback, &callbackCalled );
   if ( !result )
   {
      EMERROR("WstCompositorSetTerminatedCallback failed");
      goto exit;
   }

   result= WstCompositorStart( wctxRepeater );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   WstCompositorDestroy( wctx );

   usleep( 20000 );

   if ( !callbackCalled )
   {
      EMERROR("Terminated callback was not called");
      goto exit;
   }

   WstCompositorDestroy( wctxRepeater );

   testResult= true;

exit:

   return testResult;
}

static void dispatchCallback( WstCompositor *ctx, void *userData )
{
   int *callbackCount= (int*)userData;

   ++(*callbackCount);
}

static bool testCaseAPISetDispatchCallback( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   int callbackCount= 0;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetDispatchCallback( wctx, dispatchCallback, &callbackCount );
   if ( !result )
   {
      EMERROR("WstCompositorSetDispatchCallback failed");
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   usleep( 35000 );

   WstCompositorDestroy( wctx );

   if ( callbackCount < 2 )
   {
      EMERROR("Dispatch callback call count error: expected (%d) actual (%d)", 2, callbackCount);
      goto exit;
   }

   testResult= true;

exit:

   return testResult;
}

static void invalidateCallback( WstCompositor *ctx, void *userData )
{
   int *callbackCount= (int*)userData;

   ++(*callbackCount);
}

static bool testCaseAPISetInvalidateCallback( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   int callbackCount= 0;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetInvalidateCallback( wctx, invalidateCallback, &callbackCount );
   if ( !result )
   {
      EMERROR("WstCompositorSetInvalidateCallback failed");
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   usleep( 35000 );

   WstCompositorDestroy( wctx );

   if ( callbackCount != 1 )
   {
      EMERROR("Invalidate callback call count error: expected (%d) actual (%d)", 1, callbackCount);
      goto exit;
   }

   testResult= true;

exit:

   return testResult;
}

typedef struct _OutputNestedData
{
   bool geometryCalled;
   bool modeCalled;
   bool doneCalled;
   bool scaleCalled;
   int32_t width;
   int32_t height;
} OutputNestedData;

static void outputNestedHandleGeometry( void *userData, int32_t x, int32_t y, 
                                        int32_t mmWidth, int32_t mmHeight, int32_t subPixel, 
                                        const char *make, const char *model, int32_t transform )
{
   OutputNestedData *outputData= (OutputNestedData*)userData;
   outputData->geometryCalled= true;
}

static void outputNestedHandleMode( void *userData, uint32_t flags, int32_t width, int32_t height, int32_t refreshRate )
{
   OutputNestedData *outputData= (OutputNestedData*)userData;

   outputData->modeCalled= true;
   outputData->width= width;
   outputData->height= height;
}

static void outputNestedHandleDone( void *userData )
{
   OutputNestedData *outputData= (OutputNestedData*)userData;
   outputData->doneCalled= true;
}

static void outputNestedHandleScale( void *userData, int32_t scale )
{
   OutputNestedData *outputData= (OutputNestedData*)userData;
   outputData->scaleCalled= true;
}

WstOutputNestedListener outputNestedListener=
{
   outputNestedHandleGeometry,
   outputNestedHandleMode,
   outputNestedHandleDone,
   outputNestedHandleScale
};

static bool testCaseAPISetOutputNestedCallback( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   WstCompositor *wctxNested= 0;
   const char *displayName= "test0";
   const char *displayNameNested= "nested0";
   int width, height;
   OutputNestedData outputData;

   memset( &outputData, 0, sizeof(outputData) );

   result= WstCompositorSetOutputNestedListener( (WstCompositor*)0, &outputNestedListener, &outputData );
   if ( result )
   {
      EMERROR( "WstCompositorSetOutputNestedListener did not fail for null handle" );
      goto exit;
   }

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

   result= WstCompositorSetOutputNestedListener( wctx, &outputNestedListener, &outputData );
   if ( result )
   {
      EMERROR( "WstCompositorSetOutputNestedListener did not fail for non-nested compositor" );
      goto exit;
   }

   width= 800;
   height= 400;
   result= WstCompositorSetOutputSize( wctx, width, height );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetOutputSize did not fail with null handle" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   wctxNested= WstCompositorCreate();
   if ( !wctxNested )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctxNested, displayNameNested );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctxNested, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorSetIsNested( wctxNested, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctxNested, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetOutputNestedListener( wctxNested, &outputNestedListener, &outputData );
   if ( !result )
   {
      EMERROR( "WstCompositorSetOutputNestedListener failed" );
      goto exit;
   }

   result= WstCompositorStart( wctxNested );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   if ( !outputData.geometryCalled )
   {
      EMERROR( "Output nested geometry callback not called" );
      goto exit;
   }

   if ( !outputData.modeCalled )
   {
      EMERROR( "Output nested mode callback not called" );
      goto exit;
   }

   if ( !outputData.doneCalled )
   {
      EMERROR( "Output nested done callback not called" );
      goto exit;
   }

   if ( !outputData.scaleCalled )
   {
      EMERROR( "Output scale mode callback not called" );
      goto exit;
   }

   if ( (outputData.width != width) || (outputData.height != height) )
   {
      EMERROR( "Output nested callback got nexpected output size: expected(%dx%d) actual(%dx%d)", width, height, outputData.width, outputData.height );
      goto exit;
   }

   result= WstCompositorSetOutputNestedListener( wctx, &outputNestedListener, &outputData );
   if ( result )
   {
      EMERROR( "WstCompositorSetOutputNestedListener did not fail for running compositor" );
      goto exit;
   }

   testResult= true;

   WstCompositorDestroy( wctxNested );

   WstCompositorDestroy( wctx );

exit:

   return testResult;
}

void clientStatusDummy( WstCompositor *ctx, int status, int clientPID, int detail, void *userData )
{
}

static bool testCaseAPISetClientStatusCallback( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   const char *displayName= "test0";
   WstCompositor *wctx= 0;
   int value;

   result= WstCompositorSetClientStatusCallback( (WstCompositor*)0, clientStatusDummy, (void*)&value );
   if ( result )
   {      
      EMERROR( "WstCompositorSetClientStatusCallback did not fail for null handle" );
      goto exit;
   }

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

   result= WstCompositorSetClientStatusCallback( wctx, clientStatusDummy, (void*)&value );
   if ( result )
   {      
      EMERROR( "WstCompositorSetClientStatusCallback did not fail for with non-embedded compositor" );
      goto exit;
   }

   result= WstCompositorSetIsEmbedded( wctx, true );
   if ( !result )
   {
      EMERROR( "WstCompositorSetIsEmbedded failed" );
      goto exit;
   }

   result= WstCompositorSetClientStatusCallback( wctx, clientStatusDummy, (void*)&value );
   if ( !result )
   {      
      EMERROR( "WstCompositorSetClientStatusCallback failed" );
      goto exit;
   }

   testResult= true;

   WstCompositorDestroy( wctx );

exit:

   return testResult;
}

typedef struct _ClientStatusCtx
{
   int clientStatus;
   int clientPid;
   int detail;
   bool started;
   bool connected;
   bool disconnected;
   bool stoppedNormal;
   bool stoppedAbnormal;
} ClientStatusCtx;

void clientStatus( WstCompositor *ctx, int status, int clientPID, int detail, void *userData )
{
   ClientStatusCtx *csctx= (ClientStatusCtx*)userData;

   csctx->clientStatus= status;
   csctx->clientPid= clientPID;
   switch( status )
   {
      case WstClient_started:
         csctx->started= true;
         break;
      case WstClient_stoppedNormal:
         csctx->stoppedNormal= true;
         break;
      case WstClient_stoppedAbnormal:
         csctx->stoppedAbnormal= true;
         csctx->detail= detail;
         break;
      case WstClient_connected:
         csctx->connected= true;
         break;
      case WstClient_disconnected:
         csctx->disconnected= true;
         break;
   }
}

typedef struct _LaunchCtx
{
   EMCTX *emctx;
   WstCompositor *wctx;
   bool launchThreadStarted;
   bool launchError;
} LaunchCtx;

void* clientLaunchThread( void *arg )
{
   LaunchCtx *lctx= (LaunchCtx*)arg;
   EMCTX *emctx= lctx->emctx;
   const char *launchCmd= "./westeros_test --shell";
   bool result;

   lctx->launchThreadStarted= true;

   result= WstCompositorLaunchClient( lctx->wctx, launchCmd );
   if ( result == false )
   {
      printf("===JRW: setting launchError= true\n");
      lctx->launchError= true;
      EMERROR( "WstCompositorLaunchClient failed" );
      goto exit;
   }

exit:
   lctx->launchThreadStarted= false;

   return 0;
}

static bool testCaseAPILaunchClient( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   const char *displayName= "test0";
   const char *launchCmd= "./westeros_test --shell";
   WstCompositor *wctx= 0;
   int rc;
   pthread_t clientLaunchThreadId =0;
   ClientStatusCtx csctx;
   LaunchCtx lctx;
   int retryCount;
   int iteration;

   result= WstCompositorLaunchClient( (WstCompositor*)0, launchCmd );
   if ( result )
   {      
      EMERROR( "WstCompositorLaunchClient did not fail for null handle" );
      goto exit;
   }

   for( iteration= 0; iteration < 2; ++iteration )
   {
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

      result= WstCompositorSetRendererModule( wctx, "libwesteros_render_embedded.so.0.0.0" );
      if ( result == false )
      {
         EMERROR( "WstCompositorSetRendererModule failed" );
         goto exit;
      }

      result= WstCompositorSetIsEmbedded( wctx, true );
      if ( !result )
      {
         EMERROR( "WstCompositorSetIsEmbedded failed" );
         goto exit;
      }

      memset( &csctx, 0, sizeof(csctx) );
      result= WstCompositorSetClientStatusCallback( wctx, clientStatus, (void*)&csctx );
      if ( !result )
      {      
         EMERROR( "WstCompositorSetClientStatusCallback failed" );
         goto exit;
      }

      result= WstCompositorStart( wctx );
      if ( result == false )
      {
         EMERROR( "WstCompositorStart failed" );
         goto exit;
      }

      memset( &lctx, 0, sizeof(lctx) );
      lctx.emctx= emctx;
      lctx.wctx= wctx;

      rc= pthread_create( &clientLaunchThreadId, NULL, clientLaunchThread, &lctx );
      if ( rc )
      {
         EMERROR("unable to start client launch thread");
         goto exit;
      }

      retryCount= 0;
      while( !lctx.launchThreadStarted )
      {
         usleep( 300000 );

         if ( lctx.launchError )
         {
            goto exit;
         }

         ++retryCount;
         if ( retryCount > 50 )
         {
            EMERROR("launch thread failed to start");
            goto exit;
         }
      }

      retryCount= 0;
      while( !csctx.started )
      {
         usleep( 300000 );
         ++retryCount;
         if ( retryCount > 50 )
         {
            EMERROR("Client failed to start");
            goto exit;
         }
      }

      retryCount= 0;
      while( !csctx.connected )
      {
         usleep( 300000 );
         ++retryCount;
         if ( retryCount > 50 )
         {
            EMERROR("Client failed to connect");
            goto exit;
         }
      }

      if ( csctx.clientPid == 0 )
      {
         EMERROR("Bad client pid %d", csctx.clientPid );
         goto exit;
      }

      usleep( 300000 );

      if ( iteration == 1 )
      {
         WstCompositorStop( wctx );

         retryCount= 0;
         while( !csctx.stoppedNormal )
         {
            usleep( 300000 );
            ++retryCount;
            if ( retryCount > 50 )
            {
               EMERROR("Client failed to stop normally");
               goto exit;
            }
         }
      }
      else
      {
         kill( csctx.clientPid, 11 );

         retryCount= 0;
         while( !csctx.stoppedAbnormal )
         {
            usleep( 300000 );
            ++retryCount;
            if ( retryCount > 50 )
            {
               EMERROR("Client failed to stop abnormally");
               goto exit;
            }
         }

         if ( csctx.detail != 11 )
         {
            EMERROR("Client pid %d ended abnormally but with unexpected detail: expected (11) actual(%d)", csctx.clientPid, csctx.detail );
            goto exit;
         }
      }

      if ( !csctx.disconnected )
      {
         EMERROR("Did not get disconnect event from client pid %d", csctx.clientPid );
         goto exit;
      }

      WstCompositorDestroy( wctx );
      wctx= 0;
   }

   testResult= true;

exit:

   if ( wctx )
   {
      WstCompositorDestroy( wctx );
   }

   return testResult;
}

static bool testCaseAPIAddModule( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   const char *moduleNameBad1= "libfoo.so";
   const char *moduleNameBad2= "libwayland-client.so";
   const char *moduleNameGood= "libwesteros-ut-em.so";
   WstCompositor *wctx= 0;
   bool wasCalled;

   result= WstCompositorAddModule( (WstCompositor*)0, moduleNameGood );
   if ( result )
   {
      EMERROR("WstCompositorAddModule did not fail with hull handle");
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorAddModule( wctx, moduleNameBad1 );
   if ( result )
   {
      EMERROR("WstCompositorAddModule did not fail with non-existant module");
      goto exit;
   }

   result= WstCompositorAddModule( wctx, moduleNameBad2 );
   if ( result )
   {
      EMERROR("WstCompositorAddModule did not fail with module missing entry points");
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   result= WstCompositorAddModule( wctx, moduleNameGood );
   if ( result )
   {
      EMERROR("WstCompositorAddModule did not fail with running compositor");
      goto exit;
   }

   usleep( 17000 );

   WstCompositorDestroy( wctx );

   for( int i= 0; i < 2; ++i )
   {
      wctx= WstCompositorCreate();
      if ( !wctx )
      {
         EMERROR( "WstCompositorCreate failed" );
         goto exit;
      }

      result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
      if ( result == false )
      {
         EMERROR( "WstCompositorSetRendererModule failed" );
         goto exit;
      }

      result= WstCompositorAddModule( wctx, moduleNameGood );
      if ( !result )
      {
         EMERROR("WstCompositorAddModule failed");
         goto exit;
      }

      EMSetWesterosModuleIntFail( emctx, (i == 0 ? true : false) );

      result= WstCompositorStart( wctx );
      if ( result == false )
      {
         EMERROR( "WstCompositorStart failed" );
         goto exit;
      }

      usleep( 17000 );

      wasCalled= EMGetWesterosModuleInitCalled( emctx );
      if ( !wasCalled )
      {
         EMERROR("module modInit not called");
         goto exit;
      }

      WstCompositorDestroy( wctx );

      wasCalled= EMGetWesterosModuleTermCalled( emctx );
      if ( wasCalled && (i == 0) )
      {
         EMERROR("module modTerm was called unexpectedly");
         goto exit;
      }

      if ( !wasCalled && (i == 1) )
      {
         EMERROR("module modTerm was not called");
         goto exit;
      }
   }

   testResult= true;

exit:

   return testResult;
}

