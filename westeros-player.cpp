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
#include <signal.h>
#include <gst/gst.h>

#include "wayland-client.h"

#define UNUSED( x ) ((void)(x))

typedef struct _AppCtx
{
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_output *output;
   GstElement *pipeline;
   GstElement *player;
   GstElement *westerossink;
   GstBus *bus;
   GMainLoop *loop;
   guint timerId;
   bool usePip;
   bool useCameraLatency;
   bool useLowDelay;
   int latencyTarget;
   bool useSecureVideo;
   gfloat rate;
   gfloat zorder;
   bool needToSetRate;
   const char *videoRectOverride;
   bool haveMode;
   int outputWidth;
   int outputHeight;
} AppCtx;

static void showUsage()
{
   printf("usage:\n");
   printf(" westeros_player [options] <uri>\n" );
   printf("  uri - URI of video asset to play\n" );
   printf("where [options] are:\n" );
   printf("  -r x,y,w,h : video rect\n" );
   printf("  -P : use PIP window\n" );
   printf("  -C : use camera latency mode\n" );
   printf("  -l : use low-delay\n" );
   printf("  -L <target> : use latency target <target> (in ms)\n" );
   printf("  -p : emit position logs\n" );
   printf("  -s : secure video\n" );
   printf("  -R <rate> : play with rate\n" );
   printf("  -z <zorder> : video z-order, 0.0 - 1.0\n");
   printf("  -? : show usage\n" );
   printf("\n" );   
}

static void outputHandleGeometry( void *data, 
                                  struct wl_output *output,
                                  int x,
                                  int y,
                                  int mmWidth,
                                  int mmHeight,
                                  int subPixel,
                                  const char *make,
                                  const char *model,
                                  int transform )
{
   UNUSED(data);
   UNUSED(output);
   UNUSED(x);
   UNUSED(y);
   UNUSED(mmWidth);
   UNUSED(mmHeight);
   UNUSED(subPixel);
   UNUSED(make);
   UNUSED(model);
   UNUSED(transform);
}

static void outputHandleMode( void *data,
                              struct wl_output *output,
                              uint32_t flags,
                              int width,
                              int height,
                              int refreshRate )
{
   AppCtx *ctx= (AppCtx*)data;
   char work[32];
   
   if ( flags & WL_OUTPUT_MODE_CURRENT )
   {
      ctx->haveMode= true;
      ctx->outputWidth= width;
      ctx->outputHeight= height;

      printf("outputMode: %dx%d flags %X\n", width, height, flags);
   
      if ( ctx->westerossink && !ctx->videoRectOverride )
      {
         sprintf( work, "%d,%d,%d,%d", 0, 0, width, height );
         g_object_set(G_OBJECT(ctx->westerossink), "window-set", work, NULL );
      }
   }
}

static void outputHandleDone( void *data,
                              struct wl_output *output )
{
   UNUSED(data);
   UNUSED(output);
}

static void outputHandleScale( void *data,
                               struct wl_output *output,
                               int32_t scale )
{
   UNUSED(data);
   UNUSED(output);
   UNUSED(scale);
}

static const struct wl_output_listener outputListener = {
   outputHandleGeometry,
   outputHandleMode,
   outputHandleDone,
   outputHandleScale
};

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version)
{
   AppCtx *ctx= (AppCtx*)data;
   int len;
  
   len= strlen(interface);
   if ( (len==9) && !strncmp(interface, "wl_output", len) ) {
      ctx->output= (struct wl_output*)wl_registry_bind(registry, id, &wl_output_interface, 2);
		wl_output_add_listener(ctx->output, &outputListener, ctx);
		wl_display_roundtrip( ctx->display );
   } 
}

static void registryHandleGlobalRemove(void *data, 
                                       struct wl_registry *registry,
			                              uint32_t name)
{
   UNUSED(data);
   UNUSED(registry);
   UNUSED(name);
}

static const struct wl_registry_listener registryListener = 
{
	registryHandleGlobal,
	registryHandleGlobalRemove
};

gboolean stateChangeTimerTimeout( gpointer userData )
{
   AppCtx *ctx= (AppCtx*)userData;

   if ( ctx->pipeline )
   {
      GstState stateCurrent, statePending;

      gst_element_get_state( ctx->pipeline, &stateCurrent, &statePending, 0 );

      if ( (stateCurrent == GST_STATE_PAUSED) && (statePending == GST_STATE_VOID_PENDING) )
      {
         if ( !gst_element_seek( ctx->pipeline,
                                 ctx->rate,
                                 GST_FORMAT_TIME,
                                 GST_SEEK_FLAG_FLUSH,
                                 GST_SEEK_TYPE_SET, //start type
                                 0, //start
                                 GST_SEEK_TYPE_NONE, //stop type
                                 GST_CLOCK_TIME_NONE //stop
                                ) )
         {
            g_print("Error: unable to set rate\n");
            g_main_loop_quit( ctx->loop );
         }
         else
         {
            g_object_set( ctx->player, "mute", TRUE, NULL );

            if ( GST_STATE_CHANGE_FAILURE == gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING) )
            {
               g_print("Error: unable to move to PLAYING\n");
               g_main_loop_quit( ctx->loop );
            }
         }
      }
      else
      {
         return G_SOURCE_CONTINUE;
      }
   }

   return G_SOURCE_REMOVE;
}

static gboolean busCallback(GstBus *bus, GstMessage *message, gpointer data)
{
   AppCtx *ctx= (AppCtx*)data;
   
   switch ( GST_MESSAGE_TYPE(message) ) 
   {
      case GST_MESSAGE_STATE_CHANGED:
         {
            GstState oldState, newState, pendingState;
            gst_message_parse_state_changed( message, &oldState, &newState, &pendingState );
            if ( (oldState == GST_STATE_READY) &&
                 (newState == GST_STATE_PAUSED) &&
                 (pendingState == GST_STATE_VOID_PENDING) &&
                 ctx->needToSetRate  )
            {
               ctx->needToSetRate= false;

               g_timeout_add( 500, stateChangeTimerTimeout, ctx );
            }
         }
         break;
      case GST_MESSAGE_ERROR: 
         {
            GError *error;
            gchar *debug;
            
            gst_message_parse_error(message, &error, &debug);
            g_print("Error: %s\n", error->message);
            if ( debug )
            {
               g_print("Debug info: %s\n", debug);
            }
            g_error_free(error);
            g_free(debug);
            g_main_loop_quit( ctx->loop );
         }
         break;
     case GST_MESSAGE_EOS:
         g_print( "EOS ctx %p\n", ctx );
         g_main_loop_quit( ctx->loop );
         break;
     default:
         break;
    }
    return TRUE;
}

bool createPipeline( AppCtx *ctx )
{
   bool result= false;
   int argc= 0;
   char **argv= 0;

   gst_init( &argc, &argv );

   ctx->pipeline= gst_pipeline_new("pipeline");
   if ( !ctx->pipeline )
   {
      printf("Error: unable to create pipeline instance\n" );
      goto exit;
   }

   ctx->bus= gst_pipeline_get_bus( GST_PIPELINE(ctx->pipeline) );
   if ( !ctx->bus )
   {
      printf("Error: unable to get pipeline bus\n");
      goto exit;
   }
   gst_bus_add_watch( ctx->bus, busCallback, ctx );
   
   ctx->player= gst_element_factory_make( "playbin", "player" );
   if ( !ctx->player )
   {
      printf("Error: unable to create playbin instance\n" );
      goto exit;
   }
   gst_object_ref( ctx->player );

   ctx->westerossink= gst_element_factory_make( "westerossink", "vsink" );
   if ( !ctx->westerossink )
   {
      printf("Error: unable to create westerossink instance\n" );
      goto exit;
   }
   gst_object_ref( ctx->westerossink );

   if ( ctx->usePip )
   {
      g_object_set(G_OBJECT(ctx->westerossink), "pip", TRUE, NULL );
   }

   if ( ctx->useCameraLatency )
   {
      g_object_set(G_OBJECT(ctx->westerossink), "camera-latency", TRUE, NULL );
   }

   if ( ctx->useLowDelay )
   {
      g_object_set(G_OBJECT(ctx->westerossink), "low-delay", TRUE, NULL );
      if ( ctx->latencyTarget )
      {
         g_object_set(G_OBJECT(ctx->westerossink), "latency-target", ctx->latencyTarget, NULL );
      }
   }

   if ( ctx->useSecureVideo )
   {
      g_object_set(G_OBJECT(ctx->westerossink), "secure-video", TRUE, NULL );
   }

   g_object_set(G_OBJECT(ctx->westerossink), "zorder", ctx->zorder, NULL );

   g_object_set(G_OBJECT(ctx->player), "video-sink", ctx->westerossink, NULL );

   if ( ctx->videoRectOverride )
   {
      g_object_set(G_OBJECT(ctx->westerossink), "window-set", ctx->videoRectOverride, NULL );
   }
   else if ( ctx->haveMode )
   {
      char work[32];
      sprintf( work, "%d,%d,%d,%d", 0, 0, ctx->outputWidth, ctx->outputHeight );
      g_object_set(G_OBJECT(ctx->westerossink), "window-set", work, NULL );
   }

   if ( !gst_bin_add( GST_BIN(ctx->pipeline), ctx->player) )
   {
      printf("Error: unable to add playbin to pipeline\n");
      goto exit;
   }
   
   result= true;   

exit:
   
   return result;
}

void destroyPipeline( AppCtx *ctx )
{
   if ( ctx->pipeline )
   {
      gst_element_set_state(ctx->pipeline, GST_STATE_NULL);
   }
   if ( ctx->westerossink )
   {
      gst_object_unref( ctx->westerossink );
      ctx->westerossink= 0;
   }
   if ( ctx->player )
   {
      gst_object_unref( ctx->player );
      ctx->player= 0;
   }
   if ( ctx->bus )
   {
      gst_object_unref( ctx->bus );
      ctx->bus= 0;
   }
   if ( ctx->pipeline )
   {
      gst_object_unref( GST_OBJECT(ctx->pipeline) );
      ctx->pipeline= 0;
   }
}

static AppCtx *g_ctx= 0;

static void signalHandler(int signum)
{
   printf("signalHandler: signum %d\n", signum);
   if ( g_ctx )
   {
	   g_main_loop_quit( g_ctx->loop );
	   g_ctx= 0;
	}
}

gboolean progressTimerTimeout( gpointer userData )
{
   AppCtx *ctx= (AppCtx*)userData;

   if ( ctx->pipeline )
   {
      gint64 pos= 0;
      if ( gst_element_query_position( ctx->pipeline, GST_FORMAT_TIME, &pos ) )
      {
         printf("westeros_player: pos: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pos));
      }
   }
   return G_SOURCE_CONTINUE;
}

int main( int argc, char **argv )
{
   int result= -1;
   int argidx;
   const char *uri= 0;
   bool usePip= false;
   bool useCameraLatency= false;
   bool useLowDelay= false;
   int latencyTarget= 0;
   bool emitPosition= false;
   bool useSecureVideo= false;
   gfloat rate= 1.0f;
   gfloat zorder= 0.0f;
   const char *videoRect= 0;
   AppCtx *ctx= 0;
   struct sigaction sigint;
   
   printf("westeros_player: v1.0\n\n" );
   
   if ( argc < 2 )
   {
      showUsage();
      goto exit;
   }

   argidx= 1;   
   while ( argidx < argc )
   {
      if ( argv[argidx][0] == '-' )
      {
         switch( argv[argidx][1] )
         {
            case 'r':
               if ( argidx+1 < argc )
               {
                  videoRect= argv[++argidx];
               }
               break;
            case 'P':
               usePip= true;
               break;
            case 'C':
               useCameraLatency= true;
               break;
            case 'l':
               useLowDelay= true;
               break;
            case 'L':
               if ( argidx+1 < argc )
               {
                  int t= atoi( argv[++argidx] );
                  if ( (t > 0) && (t <= 2000) )
                  {
                     latencyTarget= t;
                  }
               }
               break;
            case 'p':
               emitPosition= true;
               break;
            case 's':
               useSecureVideo= true;
               break;
            case 'R':
               if ( argidx+1 < argc )
               {
                  float r= atof( argv[++argidx] );
                  if ( r > 0.0 )
                  {
                     rate= r;
                  }
               }
               break;
            case 'z':
               if ( argidx+1 < argc )
               {
                  float z= atof( argv[++argidx] );
                  if ( z >= 0.0 )
                  {
                     zorder= z;
                  }
               }
               break;
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
         if ( !uri )
         {
            uri= argv[argidx];
         }
         else
         {
            printf( "ignoring extra argument: %s\n", argv[argidx] );
         }
      }
      
      ++argidx;
   }
   
   if ( !uri )
   {
      printf( "missing uri argument\n" );
      goto exit;
   }
         
   printf( "playing asset: %s\n", uri );

   ctx= (AppCtx*)calloc( 1, sizeof(AppCtx) );
   if ( !ctx )
   {
      printf("Error: unable to allocate application context\n");
      goto exit;
   }

   ctx->usePip= usePip;
   ctx->useCameraLatency= useCameraLatency;
   ctx->useLowDelay= useLowDelay;
   ctx->latencyTarget= latencyTarget;
   ctx->useSecureVideo= useSecureVideo;
   ctx->videoRectOverride= videoRect;
   ctx->rate= rate;
   ctx->zorder= zorder;
   if ( rate != 1.0f )
   {
      ctx->needToSetRate= true;
   }

   ctx->display= wl_display_connect( NULL );
   if ( !ctx->display )
   {
      printf("Error: unable to open default wayland display\n");
      goto exit;
   }
   
   ctx->registry= wl_display_get_registry(ctx->display);
   if ( !ctx->registry )
   {
      printf("Error: unable to get wayland registry\n");
      goto exit;
   }

   wl_registry_add_listener(ctx->registry, &registryListener, ctx);   
   wl_display_roundtrip(ctx->display);
   
   if ( createPipeline( ctx ) )
   {
      printf("pipeline created\n");
      ctx->loop= g_main_loop_new(NULL,FALSE);
      
      if ( ctx->loop )
      {
         if ( emitPosition )
         {
            ctx->timerId= g_timeout_add( 1000, progressTimerTimeout, ctx );
         }

         g_object_set(G_OBJECT(ctx->player), "uri", uri, NULL );
         
         if ( GST_STATE_CHANGE_FAILURE != gst_element_set_state(ctx->pipeline, ctx->needToSetRate ? GST_STATE_PAUSED : GST_STATE_PLAYING) )
         {
            sigint.sa_handler = signalHandler;
            sigemptyset(&sigint.sa_mask);
            sigint.sa_flags = SA_RESETHAND;
            sigaction(SIGINT, &sigint, NULL);

            g_ctx= ctx;

            g_main_loop_run( ctx->loop );
         }

         if ( ctx->timerId )
         {
            g_source_remove( ctx->timerId );
            ctx->timerId= 0;
         }
      }
      else
      {
         printf( "Error: unable to create main loop\n");
      }
   }
   else
   {
      printf( "Error: unable to create player pipeline\n" );
   }

   
   result= 0;
      
exit:

   if ( ctx )
   {
      if ( ctx->output )
      {
         wl_output_destroy( ctx->output );
         ctx->output= 0;
      }
      
      if ( ctx->registry )
      {
         wl_registry_destroy(ctx->registry);
         ctx->registry= 0;
      }
      
      if ( ctx->display )
      {
         wl_display_disconnect(ctx->display);
         ctx->display= 0;
      }
      
      destroyPipeline( ctx );
      
      if ( ctx->loop )
      {
         g_main_loop_unref(ctx->loop);
         ctx->loop= 0;
      }
      
      free( ctx );
   }

   return result;   
}

