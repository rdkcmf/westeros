/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2017 RDK Management
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

#include "rtString.h"
#include "rtRemote.h"
#include <pthread.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <string>
#include <vector>

static pthread_mutex_t gMutex= PTHREAD_MUTEX_INITIALIZER;
static std::vector<std::string> gAvailablePipelines= std::vector<std::string>();
static bool gCaptureComplete= true;
static bool gRunning= false;
static int gSrcActive= -1;

static void signalHandler(int signum)
{
   printf("signalHandler: signum %d\n", signum);
	gRunning= false;
}

void showUsage()
{
   printf("mediacapture-test [file <filename>]|[endpoint <url>] duration\n");
}

static rtError captureCallback(int /*argc*/, rtValue const* argv, rtValue* /*result*/, void* /*argp*/)
{
  rtString s = argv[0].toString();
  
  printf("capture result: %s\n", s.cString());
  gCaptureComplete= true;

  return RT_OK;
}

#define NON_BLOCKING_ENABLED (0)
#define NON_BLOCKING_DISABLED (1)

static void setBlockingMode(int blockingState )  
{  
   struct termios ttystate;
   int mask, bits;  
 
   mask= (blockingState == NON_BLOCKING_ENABLED) ? ~(ICANON|ECHO) : -1;
   bits= (blockingState == NON_BLOCKING_ENABLED) ? 0 : (ICANON|ECHO);

   tcgetattr(STDIN_FILENO, &ttystate);  

   ttystate.c_lflag= ((ttystate.c_lflag & mask) | bits);  
 
   if (blockingState == NON_BLOCKING_ENABLED)  
   {  
       ttystate.c_cc[VMIN]= 1;  
   }  

   tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);   
}

static bool isKeyHit()
{
   bool keyHit= false;
   fd_set fdset;
   struct timeval tval;

   tval.tv_sec= 0;
   tval.tv_usec= 0;
   FD_ZERO(&fdset);
   FD_SET(STDIN_FILENO, &fdset);
   select(STDIN_FILENO+1, &fdset, NULL, NULL, &tval);

   keyHit= FD_ISSET(STDIN_FILENO, &fdset);

   return keyHit;
}

void startCaptureSourceToEndpoint( int src, const char *endpointName, int duration, rtRemoteEnvironment *env )
{
   rtError rc;
   std::string srcName;

   printf("start capture src %d to endpoint\n", src);

   pthread_mutex_lock( &gMutex );
   if ( src < gAvailablePipelines.size() )
   {
      srcName= gAvailablePipelines[src];
   }
   pthread_mutex_unlock( &gMutex );

   if ( srcName.length() > 0 )
   {
      rtObjectRef server;
      rc= rtRemoteLocateObject(env, srcName.c_str(), server);
      if ( rc == RT_OK )
      {
         rtString endpoint= endpointName;

         gSrcActive= src;
         gCaptureComplete= false;
         rc= server.send("captureMediaStart", endpoint, duration, new rtFunctionCallback(captureCallback) );
         if ( rc != RT_OK )
         {
            gSrcActive= -1;
            gCaptureComplete= true;
            printf("error invoking captureMediaStart: %d\n", rc);
         }
      }
      else
      {
         printf("unable to locate src %d\n", src);
      }
   }
   else
   {
      printf("src %d is not available\n", src);
   }
}

void stopCaptureSourceToEndpoint( int src, rtRemoteEnvironment *env )
{
   rtError rc;
   std::string srcName;

   printf("stop capture src %d\n", src);

   pthread_mutex_lock( &gMutex );
   if ( src < gAvailablePipelines.size() )
   {
      srcName= gAvailablePipelines[src];
   }
   pthread_mutex_unlock( &gMutex );

   if ( srcName.length() > 0 )
   {
      rtObjectRef server;
      rc= rtRemoteLocateObject(env, srcName.c_str(), server);
      if ( rc == RT_OK )
      {
         rc= server.send("captureMediaStop", new rtFunctionCallback(captureCallback) );
         if ( rc != RT_OK )
         {
            printf("error invoking captureMediaStop: %d\n", rc);
         }
      }
      else
      {
         printf("unable to locate src %d\n", src);
      }
   }
   else
   {
      printf("src %d is not available\n", src);
   }
}

void captureSourceToFile( int src, const char *fileName, int duration, rtRemoteEnvironment *env )
{
   rtError rc;
   std::string srcName;

   printf("capture src %d to file %s for %d ms\n", src, fileName, duration);

   pthread_mutex_lock( &gMutex );
   if ( src < gAvailablePipelines.size() )
   {
      srcName= gAvailablePipelines[src];
   }
   pthread_mutex_unlock( &gMutex );

   if ( srcName.length() > 0 )
   {
      rtObjectRef server;
      rc= rtRemoteLocateObject(env, srcName.c_str(), server);
      if ( rc == RT_OK )
      {
         rtString file= fileName;

         gCaptureComplete= false;         
         rc= server.send("captureMediaSample", file, duration, new rtFunctionCallback(captureCallback) );
         if ( rc == RT_OK )
         {
            while( !gCaptureComplete )
            {
               rtRemoteProcessSingleItem( env );
               usleep(10000);
            }
         }
      }
      else
      {
         printf("unable to locate src %d\n", src);
      }
   }
   else
   {
      printf("src %d is not available\n", src);
   }
}

static void getAvailablePipelines(rtRemoteEnvironment *env, rtObjectRef &registry)
{
   rtError rc;
   rtString list;

   pthread_mutex_lock( &gMutex );

   rc= registry.sendReturns("getAvailablePipelines", list );
   if ( rc == RT_OK )
   {
      char *l= strdup( list.cString() );
      if ( l )
      {
         char *item;

         gAvailablePipelines.clear();

         item= strtok( l, ",");
         if ( item )
         {
            int n= atoi(item);
            if ( n > 0 )
            {
               int i= 0;
               do
               {
                  item= strtok( NULL, ",");
                  if ( item )
                  {
                     gAvailablePipelines.push_back(item);
                  }
                  ++i;
               }
               while( item && (i < n) );
            }
         }

         free(l);
      }
   }
   else
   {
      printf("error: unable to get pipeline list from registry: %d\n", rc);
   }

   pthread_mutex_unlock( &gMutex );
}

static void displayPipelineList(rtRemoteEnvironment *env, rtObjectRef &registry)
{
   getAvailablePipelines( env, registry );

   printf("pipelines\n{\n");
   pthread_mutex_lock( &gMutex );
   int count= gAvailablePipelines.size();
   for( int i= 0; i < count; ++i)
   {
      printf("  %d - %s\n", i, gAvailablePipelines[i].c_str());
   }
   pthread_mutex_unlock( &gMutex );
   printf("}\n");
}

static void listActions()
{
   printf("=======================================\n");
   printf("Press\n");
   printf(" 0 - 9 to capture from that source\n");
   printf(" s to stop any active capture\n");
   printf(" l to list available sources\n");
   printf(" q to exit\n");
   printf("---------------------------------------\n");
}

int main( int argc, const char **argv )
{
   rtError rc;
   rtRemoteEnvironment *env= 0;
   const char* srcName= 0;
   const char* fileName= 0;
   const char* endpointName= 0;
   bool toFile= false;
   bool toEndpoint= false;
   int duration= 0;
   int len;

   printf("mediacapture-test v1.0\n");

   if ( argc > 4 )
   {
      showUsage();
   }
   else
   {
      if ( argc > 1 )
      {
         len= strlen(argv[1]);
         if ( (len == 4) && !strncmp( argv[1], "file", len) )
         {
            toFile= true;
         }
         else if ( (len == 8) && !strncmp( argv[1], "endpoint", len) )
         {
            toEndpoint= true;
         }
      }
      if ( argc > 2 )
      {
         if ( toFile )
         {
            fileName= argv[2];
         }
         else
         if ( toEndpoint )
         {
            endpointName= argv[2];
         }
      }
      if ( argc > 3 )
      {
         duration= atoi(argv[3]);
      }
      if ( !toFile && !toEndpoint )
      {
         showUsage();
         exit(0);
      }
      if ( duration <= 0 )
      {
         duration= 30000;
      }

      printf("will capture to %s (%s)\n", (toFile?"file":"endpoint"), (toFile?fileName:endpointName));

      env= rtEnvironmentGetGlobal();

      rc= rtRemoteInit(env);
      if ( rc == RT_OK )
      {
         rtObjectRef registry;
         struct sigaction sigint;

         rc= rtRemoteLocateObject(env, "mediacaptureregistry", registry);
         if ( rc == RT_OK )
         {
            sigint.sa_handler= signalHandler;
            sigemptyset(&sigint.sa_mask);
	         sigint.sa_flags= SA_RESETHAND;
            sigaction(SIGINT, &sigint, NULL);

            setBlockingMode(NON_BLOCKING_ENABLED);

            listActions();
            gRunning= true;
            while( gRunning )
            {
               rtRemoteProcessSingleItem( env );
               if ( isKeyHit() )
               {
                  int src= -1;
                  int c= fgetc(stdin);
                  switch( c )
                  {
                     case '0':
                     case '1':
                     case '2':
                     case '3':
                     case '4':
                     case '5':
                     case '6':
                     case '7':
                     case '8':
                     case '9':
                        src= c-'0';
                        getAvailablePipelines(env, registry);
                        if ( toFile )
                        {
                           captureSourceToFile( src, fileName, duration, env );
                        }
                        else
                        {
                           startCaptureSourceToEndpoint( src, endpointName, duration, env );
                        }
                        break;
                     case 's':
                        if ( !toFile )
                        {
                           if ( gSrcActive != -1 )
                           {
                              stopCaptureSourceToEndpoint( gSrcActive, env );
                           }
                        }
                        break;
                     case 'l':
                        displayPipelineList(env, registry);
                        break;
                     case 'q':
                        gRunning= false;
                        break;
                     default:
                        listActions();
                        break;
                  }
               }
               usleep( 10000 );
            }

            setBlockingMode(NON_BLOCKING_DISABLED);
         }
         else
         {
            printf("error: unable to locate registry: %d", rc);
         }

      }
      else
      {
         printf("error: rtRemoteInit rc %d\n", rc);
      }

   }

   return 0;
}

