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
#include <memory.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/time.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "wayland-client.h"
#include "wayland-egl.h"
#include "simpleshell-client-protocol.h"

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version);
static void registryHandleGlobalRemove(void *data, 
                                       struct wl_registry *registry,
			                              uint32_t name);

static const struct wl_registry_listener registryListener = 
{
	registryHandleGlobal,
	registryHandleGlobalRemove
};

static void shellSurfaceId(void *data,
                           struct wl_simple_shell *wl_simple_shell,
                           struct wl_surface *surface,
                           uint32_t surfaceId);
static void shellSurfaceCreated(void *data,
                                struct wl_simple_shell *wl_simple_shell,
                                uint32_t surfaceId,
                                const char *name);
static void shellSurfaceDestroyed(void *data,
                                  struct wl_simple_shell *wl_simple_shell,
                                  uint32_t surfaceId,
                                  const char *name);
static void shellSurfaceStatus(void *data,
                               struct wl_simple_shell *wl_simple_shell,
                               uint32_t surfaceId,
                               const char *name,
                               uint32_t visible,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height,
                               wl_fixed_t opacity,
                               wl_fixed_t zorder);
static void shellGetSurfacesDone(void *data,
                                 struct wl_simple_shell *wl_simple_shell);

static const struct wl_simple_shell_listener shellListener = 
{
   shellSurfaceId,
   shellSurfaceCreated,
   shellSurfaceDestroyed,
   shellSurfaceStatus,
   shellGetSurfacesDone
};

typedef enum _InputState
{
   InputState_main,
   InputState_attribute,
} InputState;

typedef enum _Attribute
{
   Attribute_position,
   Attribute_size,
   Attribute_visibility,
   Attribute_opacity,
   Attribute_zorder
} Attribute;

typedef struct _AppCtx
{
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_shm *shm;
   struct wl_compositor *compositor;
   struct wl_simple_shell *shell;
   struct wl_seat *seat;
   struct wl_keyboard *keyboard;
   struct wl_pointer *pointer;
   struct wl_touch *touch;
   struct wl_surface *surface;
   struct wl_egl_window *native;
   struct wl_callback *frameCallback;

   EGLDisplay eglDisplay;
   EGLConfig eglConfig;
   EGLSurface eglSurfaceWindow;
   EGLContext eglContext;   
   EGLImageKHR eglImage;
   EGLNativePixmapType eglPixmap;

   bool getShell;
   InputState inputState;
   Attribute attribute;
   
   int planeX;
   int planeY;
   int planeWidth;
   int planeHeight;

   uint32_t surfaceIdOther;
   uint32_t surfaceIdCurrent;
   float surfaceOpacity;
   float surfaceZOrder;
   bool surfaceVisible;   
   int surfaceX;
   int surfaceY;
   int surfaceWidth;
   int surfaceHeight;
   
   int surfaceDX;
   int surfaceDY;
   int surfaceDWidth;
   int surfaceDHeight;

	struct 
	{
		GLuint rotation_uniform;
		GLuint pos;
		GLuint col;
	} gl;
	long long startTime;
	long long currTime;
	bool needRedraw;
} AppCtx;


static void drawFrame( AppCtx *ctx );
static bool setupEGL( AppCtx *ctx );
static void termEGL( AppCtx *ctx );
static bool createSurface( AppCtx *ctx );
static void resizeSurface( AppCtx *ctx, int dx, int dy, int width, int height );
static void destroySurface( AppCtx *ctx );
static bool setupGL( AppCtx *ctx );
static bool renderGL( AppCtx *ctx );

int g_running= 0;
int g_log= 0;

static void signalHandler(int signum)
{
   printf("signalHandler: signum %d\n", signum);
	g_running = 0;
}

static long long currentTimeMillis()
{
   long long timeMillis;
   struct timeval tv;   

   gettimeofday(&tv, NULL);
   timeMillis = tv.tv_sec * 1000 + tv.tv_usec / 1000;
   
   return timeMillis;
}

static void shmFormat(void *data, struct wl_shm *wl_shm, uint32_t format)
{
   AppCtx *ctx = (AppCtx*)data;

   printf("shm format: %X\n", format);
}

struct wl_shm_listener shmListener = {
	shmFormat
};

static void seatCapabilities( void *data, struct wl_seat *seat, uint32_t capabilities )
{
	AppCtx *ctx = (AppCtx*)data;

   printf("seat %p caps: %X\n", seat, capabilities );
   
   if ( capabilities & WL_SEAT_CAPABILITY_KEYBOARD )
   {
      printf("  seat has keyboard\n");
      ctx->keyboard= wl_seat_get_keyboard( ctx->seat );
      printf("  keyboard %p\n", ctx->keyboard );
   }
   if ( capabilities & WL_SEAT_CAPABILITY_POINTER )
   {
      printf("  seat has pointer\n");
      ctx->pointer= wl_seat_get_pointer( ctx->seat );
      printf("  pointer %p\n", ctx->pointer );
   }
   if ( capabilities & WL_SEAT_CAPABILITY_TOUCH )
   {
      printf("  seat has touch\n");
      ctx->touch= wl_seat_get_touch( ctx->seat );
      printf("  touch %p\n", ctx->touch );
   }   
}

static void seatName( void *data, struct wl_seat *seat, const char *name )
{
	AppCtx *ctx = (AppCtx*)data;
   printf("seat %p name: %s\n", seat, name);
}

static const struct wl_seat_listener seatListener = {
   seatCapabilities,
   seatName 
};

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version)
{
	AppCtx *ctx = (AppCtx*)data;
	int len;

   printf("westeros-test: registry: id %d interface (%s) version %d\n", id, interface, version );

   len= strlen(interface);
   if ( (len==6) && !strncmp(interface, "wl_shm", len)) {
      ctx->shm= (struct wl_shm*)wl_registry_bind(registry, id, &wl_shm_interface, 1);
      printf("shm %p\n", ctx->shm);
		wl_shm_add_listener(ctx->shm, &shmListener, ctx);
   }
   else if ( (len==13) && !strncmp(interface, "wl_compositor", len) ) {
      ctx->compositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
      printf("compositor %p\n", ctx->compositor);
   } 
   else if ( (len==7) && !strncmp(interface, "wl_seat", len) ) {
      ctx->seat= (struct wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 4);
      printf("seat %p\n", ctx->seat);
		wl_seat_add_listener(ctx->seat, &seatListener, ctx);
   } 
   else if ( (len==15) && !strncmp(interface, "wl_simple_shell", len) ) {
      if ( ctx->getShell ) {
         ctx->shell= (struct wl_simple_shell*)wl_registry_bind(registry, id, &wl_simple_shell_interface, 1);      
         printf("shell %p\n", ctx->shell );
         wl_simple_shell_add_listener(ctx->shell, &shellListener, ctx);
      }
   }
}

static void registryHandleGlobalRemove(void *data, 
                                       struct wl_registry *registry,
			                              uint32_t name)
{
}

static void shellSurfaceId(void *data,
                           struct wl_simple_shell *wl_simple_shell,
                           struct wl_surface *surface,
                           uint32_t surfaceId)
{
	AppCtx *ctx = (AppCtx*)data;
   char name[32];
  
	sprintf( name, "westeros-test-surface-%x", surfaceId );
   printf("shell: surface created: %p id %x\n", surface, surfaceId);
	wl_simple_shell_set_name( ctx->shell, surfaceId, name );
   wl_simple_shell_set_geometry( ctx->shell, surfaceId, ctx->surfaceX, ctx->surfaceY, ctx->surfaceWidth, ctx->surfaceHeight );

}
                           
static void shellSurfaceCreated(void *data,
                                struct wl_simple_shell *wl_simple_shell,
                                uint32_t surfaceId,
                                const char *name)
{
	AppCtx *ctx = (AppCtx*)data;

   printf("shell: surface created: %x name: %s\n", surfaceId, name);
   ctx->surfaceIdOther= ctx->surfaceIdCurrent;
   ctx->surfaceIdCurrent= surfaceId;   
   wl_simple_shell_get_status( ctx->shell, ctx->surfaceIdCurrent );
   printf("shell: surfaceCurrent: %x surfaceOther: %x\n", ctx->surfaceIdCurrent, ctx->surfaceIdOther);
}

static void shellSurfaceDestroyed(void *data,
                                  struct wl_simple_shell *wl_simple_shell,
                                  uint32_t surfaceId,
                                  const char *name)
{
	AppCtx *ctx = (AppCtx*)data;
	
   printf("shell: surface destroyed: %x name: %s\n", surfaceId, name);
   
   if ( ctx->surfaceIdCurrent == surfaceId )
   {
      ctx->surfaceIdCurrent= ctx->surfaceIdOther;
      ctx->surfaceIdOther= 0;
      wl_simple_shell_get_status( ctx->shell, ctx->surfaceIdCurrent );
   }
   if ( ctx->surfaceIdOther == surfaceId )
   {
      ctx->surfaceIdOther= 0;
   }
   printf("shell: surfaceCurrent: %x surfaceOther: %x\n", ctx->surfaceIdCurrent, ctx->surfaceIdOther);
}
                                  
static void shellSurfaceStatus(void *data,
                               struct wl_simple_shell *wl_simple_shell,
                               uint32_t surfaceId,
                               const char *name,
                               uint32_t visible,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height,
                               wl_fixed_t opacity,
                               wl_fixed_t zorder)
{
	AppCtx *ctx = (AppCtx*)data;

   printf("shell: surface: %x name: %s\n", surfaceId, name);
   printf("shell: position (%d,%d,%d,%d) visible %d opacity %f zorder %f\n",
           x, y, width, height, visible, wl_fixed_to_double(opacity), wl_fixed_to_double(zorder) );

   ctx->surfaceVisible= visible;
   ctx->surfaceX= x;
   ctx->surfaceY= y;
   ctx->surfaceWidth= width;
   ctx->surfaceHeight= height;
   ctx->surfaceOpacity= wl_fixed_to_double(opacity);
   ctx->surfaceZOrder= wl_fixed_to_double(zorder);   
}                               

static void shellGetSurfacesDone(void *data,
                                 struct wl_simple_shell *wl_simple_shell)
{
	AppCtx *ctx = (AppCtx*)data;
	printf("shell: get all surfaces done\n");
}                                        

#define NON_BLOCKING_ENABLED (0)
#define NON_BLOCKING_DISABLED (1)

static void setBlockingMode(int blockingState )  
{  
   struct termios ttystate;
   int mask, bits;  
 
   mask= (blockingState == NON_BLOCKING_ENABLED) ? ~(ICANON|ECHO) : -1;
   bits= (blockingState == NON_BLOCKING_ENABLED) ? 0 : (ICANON|ECHO);

   // Obtain the current terminal state and alter the attributes to achieve 
   // the requested blocking behaviour
   tcgetattr(STDIN_FILENO, &ttystate);  

   ttystate.c_lflag= ((ttystate.c_lflag & mask) | bits);  
 
   if (blockingState == NON_BLOCKING_ENABLED)  
   {  
       ttystate.c_cc[VMIN]= 1;  
   }  

   tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);   
}

static int kbhit()  
{  
   struct timeval tv;  
   fd_set fds;  
   tv.tv_sec = 0;  
   tv.tv_usec = 0;  
   FD_ZERO(&fds);  
   FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0  
   select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);  
   return FD_ISSET(STDIN_FILENO, &fds);  
}

static void adjustAttribute( AppCtx *ctx, int c )
{
   switch( ctx->attribute )
   {
      case Attribute_position:
         switch( c )
         {
            case 0x41: //UP
               --ctx->surfaceDY;
               break;
            case 0x42: //DOWN
               ++ctx->surfaceDY;
               break;
            case 0x43: //RIGHT
               ++ctx->surfaceDX;
               break;
            case 0x44: //LEFT
               --ctx->surfaceDX;
               break;
         }
         break;
      case Attribute_size:
         switch( c )
         {
            case 0x41: //UP
               --ctx->surfaceDHeight;
               break;
            case 0x42: //DOWN
               ++ctx->surfaceDHeight;
               break;
            case 0x43: //RIGHT
               ++ctx->surfaceDWidth;
               break;
            case 0x44: //LEFT
               --ctx->surfaceDWidth;
               break;
         }
         break;
      case Attribute_visibility:
         switch( c )
         {
            case 0x41: //UP
            case 0x43: //RIGHT
            case 0x42: //DOWN
            case 0x44: //LEFT
               ctx->surfaceVisible= !ctx->surfaceVisible;
               wl_simple_shell_set_visible( ctx->shell, ctx->surfaceIdCurrent, (ctx->surfaceVisible ? 1 : 0) );
               break;
         }
         break;
      case Attribute_opacity:
         switch( c )
         {
            case 0x41: //UP
            case 0x43: //RIGHT
               ctx->surfaceOpacity += 0.1;
               if ( ctx->surfaceOpacity > 1.0 )
               {
                  ctx->surfaceOpacity= 1.0;
               }
               wl_simple_shell_set_opacity( ctx->shell, ctx->surfaceIdCurrent, wl_fixed_from_double(ctx->surfaceOpacity) );
               break;
            case 0x42: //DOWN
            case 0x44: //LEFT
               ctx->surfaceOpacity -= 0.1;
               if ( ctx->surfaceOpacity < 0.0 )
               {
                  ctx->surfaceOpacity= 0.0;
               }
               wl_simple_shell_set_opacity( ctx->shell, ctx->surfaceIdCurrent, wl_fixed_from_double(ctx->surfaceOpacity) );
               break;
         }
         break;
      case Attribute_zorder:
         switch( c )
         {
            case 0x41: //UP
            case 0x43: //RIGHT
               ctx->surfaceZOrder += 0.1;
               if ( ctx->surfaceZOrder > 1.0 )
               {
                  ctx->surfaceZOrder= 1.0;
               }
               wl_simple_shell_set_zorder( ctx->shell, ctx->surfaceIdCurrent, wl_fixed_from_double(ctx->surfaceZOrder) );
               break;
            case 0x42: //DOWN
            case 0x44: //LEFT
               ctx->surfaceZOrder -= 0.1;
               if ( ctx->surfaceZOrder < 0.0 )
               {
                  ctx->surfaceZOrder= 0.0;
               }
               wl_simple_shell_set_zorder( ctx->shell, ctx->surfaceIdCurrent, wl_fixed_from_double(ctx->surfaceZOrder) );
               break;
         }
         break;
   }
}

static void processInputMain( AppCtx *ctx, int c )
{
   switch( c )
   {
      case 0x1B:
         c= fgetc(stdin);
         if ( c == 0x5B )
         {
            c= fgetc(stdin);
            switch( c )
            {
               case 0x41: //UP
               case 0x42: //DOWN
               case 0x43: //RIGHT
               case 0x44: // LEFT
                  if ( ctx->surfaceIdCurrent )
                  {
                     adjustAttribute( ctx, c );
                  }
                  break;
            }
         }
         break;
      case 'a':
         ctx->inputState= InputState_attribute;
         printf("attribute: (p) osition, (s) ize, (v) isible, (o) pacity, (z) order (x) back to main\n");
         break;
      case 's':
         if ( ctx->surfaceIdCurrent )
         {
            wl_simple_shell_get_status( ctx->shell, ctx->surfaceIdCurrent );
         }
         break;
      case 'n':
         if ( ctx->surfaceIdOther )
         {
            uint32_t temp= ctx->surfaceIdCurrent;
            ctx->surfaceIdCurrent= ctx->surfaceIdOther;
            ctx->surfaceIdOther= temp;
            printf("shell: surfaceCurrent: %x surfaceOther: %x\n", ctx->surfaceIdCurrent, ctx->surfaceIdOther);
            wl_simple_shell_get_status( ctx->shell, ctx->surfaceIdCurrent );
         }
         break;
      case 'l':
         printf("get all surfaces:\n");
         wl_simple_shell_get_surfaces( ctx->shell );
         break;
      case 'r':
         ctx->planeWidth= (ctx->planeWidth == 1280) ? 640 : 1280;
         ctx->planeHeight= (ctx->planeHeight == 720) ? 360 : 720;
         printf("resize egl window to (%d,%d)\n", ctx->planeWidth, ctx->planeHeight );
         resizeSurface( ctx, 0, 0, ctx->planeWidth, ctx->planeHeight);
         break;
   }
}

static void processInputAttribute( AppCtx *ctx, int c )
{
   switch( c )
   {
      case 'p':
         ctx->attribute= Attribute_position;
         break;
      case 's':
         ctx->attribute= Attribute_size;
         break;
      case 'v':
         ctx->attribute= Attribute_visibility;
         break;
      case 'o':
         ctx->attribute= Attribute_opacity;
         break;
      case 'z':
         ctx->attribute= Attribute_zorder;
         break;
      default:
      case 'x':
         break;
   }
   ctx->inputState= InputState_main;
}

static void processInput( AppCtx *ctx, int c )
{
   switch( ctx->inputState )
   {
      case InputState_main:
         processInputMain( ctx, c );
         break;
      case InputState_attribute:
         processInputAttribute( ctx, c );
         break;
   }
}

static void showUsage()
{
   printf("usage:\n");
   printf(" westeros_test [options]\n" );
   printf("where [options] are:\n" );
   printf("  --delay <delay> : render loop delay\n" );
   printf("  --shell : use wl_simple_shell protocol\n" );
   printf("  --display <name> : wayland display to connect to\n" );
   printf("  --noframe : don't pace rendering with frame requests\n" );
   printf("  -? : show usage\n" );
   printf("\n" );
}

static void redraw( void *data, struct wl_callback *callback, uint32_t time )
{
   AppCtx *ctx= (AppCtx*)data;

   if ( g_log ) printf("redraw: time %d\n", time);
   wl_callback_destroy( callback );

   ctx->needRedraw= true;
}

static struct wl_callback_listener frameListener=
{
   redraw
};

static void drawFrame( AppCtx *ctx )
{
   if ( ctx->surfaceDX || ctx->surfaceDY || ctx->surfaceDWidth || ctx->surfaceDHeight )
   {
      ctx->surfaceX += ctx->surfaceDX;
      ctx->surfaceY += ctx->surfaceDY;
      ctx->surfaceWidth += ctx->surfaceDWidth;
      ctx->surfaceHeight += ctx->surfaceDHeight;
      
      wl_simple_shell_set_geometry( ctx->shell, ctx->surfaceIdCurrent, 
                                    ctx->surfaceX, ctx->surfaceY, ctx->surfaceWidth, ctx->surfaceHeight );
   }
   
   renderGL(ctx);
   
   ctx->frameCallback= wl_surface_frame( ctx->surface );
   wl_callback_add_listener( ctx->frameCallback, &frameListener, ctx );
   
   eglSwapBuffers(ctx->eglDisplay, ctx->eglSurfaceWindow);
}

#define NUM_EVENTS (20)
int main( int argc, char** argv)
{
   int nRC= 0;
   AppCtx ctx;
	struct sigaction sigint;
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   int count;
   int delay= 16667;
   const char *display_name= 0;
   bool paceRendering= true;
   EGLBoolean swapok;

   printf("westeros_test: v1.0\n" );

   memset( &ctx, 0, sizeof(AppCtx) );

   for( int i= 1; i < argc; ++i )
   {
      if ( !strcmp( (const char*)argv[i], "--delay") )
      {
         printf("got delay: i %d argc %d\n", i, argc );
         if ( i+1 < argc )
         {
            int v= atoi(argv[++i]);
            printf("v=%d\n", v);
            if ( v > 0 )
            {
               delay= v;
               printf("using delay=%d\n", delay );
            }
         }
      }
      else if (!strcmp( (const char*)argv[i], "--shell" ) )
      {
         ctx.getShell= true;
      }
      else if ( !strcmp( (const char*)argv[i], "--display") )
      {
         if ( i+1 < argc )
         {
            ++i;
            display_name= argv[i];
         }
      }
      else if (!strcmp( (const char*)argv[i], "--noframe" ) )
      {
         paceRendering= false;
      }
      else if (!strcmp( (const char*)argv[i], "--log" ) )
      {
         g_log= true;
      }
      else if ( !strcmp( (const char*)argv[i], "-?" ) )
      {
         showUsage();
         goto exit;
      }
   }

   ctx.startTime= currentTimeMillis();
   
   if ( display_name )
   {
      printf("calling wl_display_connect for display name %s\n", display_name);
   }
   else
   {
      printf("calling wl_display_connect for default display\n");
   }
   display= wl_display_connect(display_name);
   printf("wl_display_connect: display=%p\n", display);
   if ( !display )
   {
      printf("error: unable to connect to primary display\n");
      nRC= -1;
      goto exit;
   }

   printf("calling wl_display_get_registry\n");
   registry= wl_display_get_registry(display);
   printf("wl_display_get_registry: registry=%p\n", registry);
   if ( !registry )
   {
      printf("error: unable to get display registry\n");
      nRC= -2;
      goto exit;
   }

   ctx.display= display;
   ctx.registry= registry;
   ctx.planeX= 0;
   ctx.planeY= 0;
   ctx.planeWidth= 1280;
   ctx.planeHeight= 720;
   wl_registry_add_listener(registry, &registryListener, &ctx);
   
   wl_display_roundtrip(ctx.display);
   
   setupEGL(&ctx);

   ctx.surfaceWidth= 640;
   ctx.surfaceHeight= 360;
   ctx.surfaceX= 0;
   ctx.surfaceY= 0;

   createSurface(&ctx);
   
   setupGL(&ctx);
   
   if ( paceRendering )
   {
      drawFrame(&ctx);
   }
  
	sigint.sa_handler = signalHandler;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

   setBlockingMode(NON_BLOCKING_ENABLED);

   ctx.inputState= InputState_main;
   ctx.attribute= Attribute_position;
   
   g_running= 1;
   while( g_running )
   {
      if ( wl_display_dispatch( ctx.display ) == -1 )
      {
         break;
      }

      if ( delay > 0 )
      {
         usleep(delay);
      }
      
      if ( !paceRendering )
      {
         renderGL(&ctx);
         eglSwapBuffers(ctx.eglDisplay, ctx.eglSurfaceWindow);
      }
      else if ( ctx.needRedraw )
      {
         ctx.needRedraw= false;
         drawFrame(&ctx);
      }

      if ( ctx.getShell )
      {      
         if ( kbhit() )
         {
            int c= fgetc(stdin);
            processInput(&ctx, c);
            
            // Prevent keys from building up while held down
            tcflush(STDIN_FILENO,TCIFLUSH);
         }
         else
         {
            if ( ctx.surfaceDX || ctx.surfaceDY || ctx.surfaceDWidth || ctx.surfaceDHeight )
            {
              // Key has been released - reset deltas
              ctx.surfaceDX= ctx.surfaceDY= ctx.surfaceDWidth= ctx.surfaceDHeight= 0;         
            }
         }
      }
   }   

exit:

   printf("westeros_test: exiting...\n");

   setBlockingMode(NON_BLOCKING_DISABLED);
   
   if ( ctx.compositor )
   {
      wl_compositor_destroy( ctx.compositor );
      ctx.compositor= 0;
   }
   
   if ( ctx.shell )
   {
      wl_simple_shell_destroy( ctx.shell );
      ctx.shell= 0;
   }
   
   termEGL(&ctx);
 
   if ( registry )
   {
      wl_registry_destroy(registry);
      registry= 0;
   }
   
   if ( display )
   {
      wl_display_disconnect(display);
      display= 0;
   }
   
   printf("westeros_test: exit\n");
      
   return nRC;
}

#define RED_SIZE (8)
#define GREEN_SIZE (8)
#define BLUE_SIZE (8)
#define ALPHA_SIZE (8)
#define DEPTH_SIZE (0)

static bool setupEGL( AppCtx *ctx )
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

   /*
    * Get default EGL display
    */
   ctx->eglDisplay = eglGetDisplay((NativeDisplayType)ctx->display);
   printf("eglDisplay=%p\n", ctx->eglDisplay );
   if ( ctx->eglDisplay == EGL_NO_DISPLAY )
   {
      printf("error: EGL not available\n" );
      goto exit;
   }
    
   /*
    * Initialize display
    */
   b= eglInitialize( ctx->eglDisplay, &major, &minor );
   if ( !b )
   {
      printf("error: unable to initialize EGL display\n" );
      goto exit;
   }
   printf("eglInitiialize: major: %d minor: %d\n", major, minor );
    
   /*
    * Get number of available configurations
    */
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
    
   /*
    * Get a list of configurations that meet or exceed our requirements
    */
   b= eglChooseConfig( ctx->eglDisplay, attr, eglConfigs, configCount, &configCount );
   if ( !b )
   {
      printf("error: eglChooseConfig failed: %X\n", eglGetError() );
      goto exit;
   }
   printf("eglChooseConfig: matching configurations: %d\n", configCount );
    
   /*
    * Choose a suitable configuration
    */
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
           (depthSize == DEPTH_SIZE) )
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

   result= true;
    
exit:

   if ( eglConfigs )
   {
      free( eglConfigs );
      eglConfigs= 0;
   }

   return result;       
}

static void termEGL( AppCtx *ctx )
{
   if ( ctx->display )
   {
      eglMakeCurrent( ctx->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
      
      destroySurface( ctx );
      
      eglTerminate( ctx->eglDisplay );
      eglReleaseThread();
   }
}

static bool createSurface( AppCtx *ctx )
{
   bool result= false;
   EGLBoolean b;

   ctx->surface= wl_compositor_create_surface(ctx->compositor);
   printf("surface=%p\n", ctx->surface);   
   if ( !ctx->surface )
   {
      printf("error: unable to create wayland surface\n");
      goto exit;
   }

   ctx->native= wl_egl_window_create(ctx->surface, ctx->planeWidth, ctx->planeHeight);
   if ( !ctx->native )
   {
      printf("error: unable to create wl_egl_window\n");
      goto exit;
   }
   printf("wl_egl_window %p\n", ctx->native);
   
   /*
    * Create a window surface
    */
   ctx->eglSurfaceWindow= eglCreateWindowSurface( ctx->eglDisplay,
                                                  ctx->eglConfig,
                                                  (EGLNativeWindowType)ctx->native,
                                                  NULL );
   if ( ctx->eglSurfaceWindow == EGL_NO_SURFACE )
   {
      printf("eglCreateWindowSurface: A: error %X\n", eglGetError() );
      ctx->eglSurfaceWindow= eglCreateWindowSurface( ctx->eglDisplay,
                                                     ctx->eglConfig,
                                                     (EGLNativeWindowType)NULL,
                                                     NULL );
      if ( ctx->eglSurfaceWindow == EGL_NO_SURFACE )
      {
         printf("eglCreateWindowSurface: B: error %X\n", eglGetError() );
         goto exit;
      }
   }
   printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", ctx->eglSurfaceWindow );                                         

   /*
    * Establish EGL context for this thread
    */
   b= eglMakeCurrent( ctx->eglDisplay, ctx->eglSurfaceWindow, ctx->eglSurfaceWindow, ctx->eglContext );
   if ( !b )
   {
      printf("error: eglMakeCurrent failed: %X\n", eglGetError() );
      goto exit;
   }
    
   eglSwapInterval( ctx->eglDisplay, 1 );

exit:

   return result;
}

static void destroySurface( AppCtx *ctx )
{
   if ( ctx->eglSurfaceWindow )
   {
      eglDestroySurface( ctx->eglDisplay, ctx->eglSurfaceWindow );
      
      wl_egl_window_destroy( ctx->native );   
   }
   if ( ctx->surface )
   {
      wl_surface_destroy( ctx->surface );
      ctx->surface= 0;
   }
}

static void resizeSurface( AppCtx *ctx, int dx, int dy, int width, int height )
{
   wl_egl_window_resize( ctx->native, width, height, dx, dy );
}

static const char *vert_shader_text =
	"uniform mat4 rotation;\n"
	"attribute vec4 pos;\n"
	"attribute vec4 color;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_Position = rotation * pos;\n"
	"  v_color = color;\n"
	"}\n";

static const char *frag_shader_text =
	"precision mediump float;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_FragColor = v_color;\n"
	"}\n";

static GLuint createShader(AppCtx *ctx, GLenum shaderType, const char *shaderSource )
{
   GLuint shader= 0;
   GLint shaderStatus;
   GLsizei length;
   char logText[1000];
   
   shader= glCreateShader( shaderType );
   if ( shader )
   {
      glShaderSource( shader, 1, (const char **)&shaderSource, NULL );
      glCompileShader( shader );
      glGetShaderiv( shader, GL_COMPILE_STATUS, &shaderStatus );
      if ( !shaderStatus )
      {
         glGetShaderInfoLog( shader, sizeof(logText), &length, logText );
         printf("Error compiling %s shader: %*s\n",
                ((shaderType == GL_VERTEX_SHADER) ? "vertex" : "fragment"),
                length,
                logText );
      }
   }
   
   return shader;
}

static bool setupGL( AppCtx *ctx )
{
   bool result= false;
	GLuint frag, vert;
	GLuint program;
	GLint status;

	frag= createShader(ctx, GL_FRAGMENT_SHADER, frag_shader_text);
	vert= createShader(ctx, GL_VERTEX_SHADER, vert_shader_text);

	program= glCreateProgram();
	glAttachShader(program, frag);
	glAttachShader(program, vert);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) 
	{
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
		goto exit;
	}

	glUseProgram(program);

	ctx->gl.pos= 0;
	ctx->gl.col= 1;

	glBindAttribLocation(program, ctx->gl.pos, "pos");
	glBindAttribLocation(program, ctx->gl.col, "color");
	glLinkProgram(program);

	ctx->gl.rotation_uniform= glGetUniformLocation(program, "rotation");
		
exit:
   return result;		
}

static bool renderGL( AppCtx *ctx )
{
   static const GLfloat verts[3][2] = {
      { -0.5, -0.5 },
      {  0.5, -0.5 },
      {  0,    0.5 }
   };
   static const GLfloat colors[3][4] = {
      { 1, 0, 0, 1.0 },
      { 0, 1, 0, 1.0 },
      { 0, 0, 1, 1.0 }
   };
   GLfloat angle;
   GLfloat rotation[4][4] = {
      { 1, 0, 0, 0 },
      { 0, 1, 0, 0 },
      { 0, 0, 1, 0 },
      { 0, 0, 0, 1 }
   };
   static const uint32_t speed_div = 5;
   EGLint rect[4];

   glViewport( 0, 0, ctx->planeWidth, ctx->planeHeight );
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT);

   ctx->currTime= currentTimeMillis();

   angle = ((ctx->currTime-ctx->startTime) / speed_div) % 360 * M_PI / 180.0;
   rotation[0][0] =  cos(angle);
   rotation[0][2] =  sin(angle);
   rotation[2][0] = -sin(angle);
   rotation[2][2] =  cos(angle);

   glUniformMatrix4fv(ctx->gl.rotation_uniform, 1, GL_FALSE, (GLfloat *) rotation);

   glVertexAttribPointer(ctx->gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
   glVertexAttribPointer(ctx->gl.col, 4, GL_FLOAT, GL_FALSE, 0, colors);
   glEnableVertexAttribArray(ctx->gl.pos);
   glEnableVertexAttribArray(ctx->gl.col);

   glDrawArrays(GL_TRIANGLES, 0, 3);

   glDisableVertexAttribArray(ctx->gl.pos);
   glDisableVertexAttribArray(ctx->gl.col);
   
   GLenum err= glGetError();
   if ( err != GL_NO_ERROR )
   {
      printf( "renderGL: glGetError() = %X\n", err );
   }
}

