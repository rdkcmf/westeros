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
} AppCtx;

static void showUsage()
{
   printf("usage:\n");
   printf(" westeros_player [options] <uri>\n" );
   printf("  uri - URI of video asset to play\n" );
   printf("where [options] are:\n" );
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
      printf("outputMode: %dx%d flags %X\n", width, height, flags);
   
      if ( ctx->westerossink )
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

static gboolean busCallback(GstBus *bus, GstMessage *message, gpointer data)
{
   AppCtx *ctx= (AppCtx*)data;
   
   switch ( GST_MESSAGE_TYPE(message) ) 
   {
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
   
   g_object_set(G_OBJECT(ctx->player), "video-sink", ctx->westerossink, NULL );
   
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

int main( int argc, char **argv )
{
   int result= -1;
   int argidx;
   const char *uri= 0;
   AppCtx *ctx= 0;
   
   printf("westeros_test: v1.0\n\n" );
   
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
         g_object_set(G_OBJECT(ctx->player), "uri", uri, NULL );
         
         if ( GST_STATE_CHANGE_FAILURE != gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING) )
         {
            g_main_loop_run( ctx->loop );
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

