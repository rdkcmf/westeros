#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>

#include "westeros-compositor.h"

static bool g_running= false;

static void signalHandler(int signum)
{
   printf("signalHandler: signum %d\n", signum);
	g_running= false;
}

static void showUsage()
{
   printf("usage:\n");
   printf(" westeros [options]\n" );
   printf("where [options] are:\n" );
   printf("  --renderer <module> : renderer module to use\n" );
   printf("  --framerate <rate> : frame rate in fps\n" );
   printf("  --display <name> : name of wayland display created by compositor\n" );
   printf("  --nested : operate as a nested compositor\n" );
   printf("  --nestedDisplay <name> : name of wayland display to connect to for nested composition\n" );
   printf("  --width <width> : width of nested composition surface\n" );
   printf("  --height <width> : height of nested composition surface\n" );
   printf("  -? : show usage\n" );
   printf("\n" );
}

int main( int argc, char** argv)
{
   int nRC= 0;
	struct sigaction sigint;
   const char *rendererModule= 0;
   const char *displayName= 0;
   const char *nestedDisplayName= 0;
   bool error= false;
   int len, value, width=-1, height=-1;
   WstCompositor *wctx;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      printf("unable to create compositor instance\n");
      nRC= -1;
      goto exit;
   }

   for( int i= 1; i < argc; ++i )
   {
      len= strlen(argv[i]);
      if ( (len == 10) && !strncmp( (const char*)argv[i], "--renderer", len) )
      {
         if ( i < argc-1 )
         {
            ++i;
            rendererModule= argv[i];
            
            if ( !WstCompositorSetRendererModule( wctx, rendererModule) )
            {
               error= true;
               break;
            }
         }
      }
      else
      if ( (len == 11) && !strncmp( (const char*)argv[i], "--framerate", len) )
      {
         if ( i < argc-1 )
         {
            int frameRate;
            
            ++i;
            frameRate= atoi(argv[i]);
            if ( frameRate > 0 )
            {
               if ( !WstCompositorSetFrameRate( wctx, frameRate ) )
               {
                  error= true;
                  break;
               }
            }
         }
      }
      else
      if ( (len == 9) && !strncmp( (const char*)argv[i], "--display", len) )
      {
         if ( i < argc-1)
         {
            ++i;
            displayName= argv[i];

            if ( !WstCompositorSetDisplayName( wctx, displayName) )
            {
               error= true;
               break;
            }
         }
      }
      if ( (len == 8) && !strncmp( (const char*)argv[i], "--nested", len) )
      {
         if ( !WstCompositorSetIsNested( wctx, true) )
         {
            error= true;
            break;
         }
      }
      else
      if ( (len == 15) && !strncmp( (const char*)argv[i], "--nestedDisplay", len) )
      {
         if ( i < argc-1)
         {
            ++i;
            nestedDisplayName= argv[i];

            if ( !WstCompositorSetNestedDisplayName( wctx, nestedDisplayName) )
            {
               error= true;
               break;
            }
         }
      }
      else
      if ( (len == 7) && !strncmp( argv[i], "--width", len) )
      {
         if ( i+1 < argc )
         {
            ++i;
            value= atoi(argv[i]);
            if ( value > 0 )
            {
               width= value;
            }
         }
      }
      else
      if ( (len == 8) && !strncmp( argv[i], "--height", len) )
      {
         if ( i+1 < argc )
         {
            ++i;
            value= atoi(argv[i]);
            if ( value > 0 )
            {
               height= value;
            }
         }
      }
      else
      if ( (len == 2) && !strncmp( (const char*)argv[i], "-?", len) )
      {
         showUsage();
         goto exit;
      }
   }
   
   if ( !error )
   {
      if ( (width > 0) && (height > 0) )
      {
         if ( !WstCompositorSetNestedSize( wctx, width, height) )
         {
            error= true;
         }
      }

      if ( !rendererModule )
      {
         printf("missing renderer module: use --renderer <module>\n");
         nRC= -1;
         goto exit;
      }
      
      if ( !error )
      {
         if ( !(error= !WstCompositorStart( wctx )) )
         {
	         sigint.sa_handler = signalHandler;
	         sigemptyset(&sigint.sa_mask);
	         sigint.sa_flags = SA_RESETHAND;
	         sigaction(SIGINT, &sigint, NULL);

            g_running= true;
            while( g_running )
            {
               usleep( 10000 );
            }
            
            WstCompositorStop( wctx );
         }
      }
   }
      
exit:

   if ( wctx )
   {
      if ( error )
      {
         const char *detail= WstCompositorGetLastErrorDetail( wctx );
         printf("Compositor error: (%s)\n", detail );
      }
      
      WstCompositorDestroy( wctx );
   }

   return nRC;   
}

