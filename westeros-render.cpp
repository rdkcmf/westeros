#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <pthread.h>
#include <unistd.h>

#include <dlfcn.h>

#include "westeros-render.h"

#define DEFAULT_OUTPUT_WIDTH (1280)
#define DEFAULT_OUTPUT_HEIGHT (720)

#define WESTEROS_UNUSED(x) ((void)(x))

static void keyboardHandleKeymap( void *data, struct wl_keyboard *keyboard, 
                                  uint32_t format, int fd, uint32_t size )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->keyboardHandleKeyMap( renderer->nestedListenerUserData,
                                                      format, fd, size );
   }
}

static void keyboardHandleEnter( void *data, struct wl_keyboard *keyboard,
                                 uint32_t serial, struct wl_surface *surface, 
                                 struct wl_array *keys )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->keyboardHandleEnter( renderer->nestedListenerUserData,
                                                     keys );
   }
}

static void keyboardHandleLeave( void *data, struct wl_keyboard *keyboard,
                                 uint32_t serial, struct wl_surface *surface )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->keyboardHandleLeave( renderer->nestedListenerUserData );
   }
}

static void keyboardHandleKey( void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t time, uint32_t key,
                               uint32_t state)
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->keyboardHandleKey( renderer->nestedListenerUserData,
                                                   time, key, state );
   }
}

static void keyboardHandleModifiers( void *data, struct wl_keyboard *keyboard,
                                     uint32_t serial, uint32_t mods_depressed,
                                     uint32_t mods_latched, uint32_t mods_locked,
                                     uint32_t group )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->keyboardHandleModifiers( renderer->nestedListenerUserData,
                                                         mods_depressed, mods_latched,
                                                         mods_locked, group );
   }
}

static void keyboardHandleRepeatInfo( void *data, struct wl_keyboard *keyboard,
                                      int32_t rate, int32_t delay )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->keyboardHandleRepeatInfo( renderer->nestedListenerUserData,
                                                          rate, delay );
   }
}

static const struct wl_keyboard_listener keyboardListener= {
   keyboardHandleKeymap,
   keyboardHandleEnter,
   keyboardHandleLeave,
   keyboardHandleKey,
   keyboardHandleModifiers,
   keyboardHandleRepeatInfo
};

static void pointerHandleEnter( void *data, struct wl_pointer *pointer,
                                uint32_t serial, struct wl_surface *surface,
                                wl_fixed_t sx, wl_fixed_t sy )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->pointerHandleEnter( renderer->nestedListenerUserData,
                                                    sx, sy );
   }
}

static void pointerHandleLeave( void *data, struct wl_pointer *pointer,
                                uint32_t serial, struct wl_surface *surface )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->pointerHandleLeave( renderer->nestedListenerUserData );
   }
}

static void pointerHandleMotion( void *data, struct wl_pointer *pointer, 
                                 uint32_t time, wl_fixed_t sx, wl_fixed_t sy )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->pointerHandleMotion( renderer->nestedListenerUserData,
                                                     time, sx, sy );
   }
}

static void pointerHandleButton( void *data, struct wl_pointer *pointer,
                                 uint32_t serial, uint32_t time, 
                                 uint32_t button, uint32_t state )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->pointerHandleButton( renderer->nestedListenerUserData,
                                                     time, button, state );
   }
}

static void pointerHandleAxis( void *data, struct wl_pointer *pointer,
                               uint32_t time, uint32_t axis, wl_fixed_t value )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   if ( renderer->nestedListener )
   {
      renderer->nestedListener->pointerHandleAxis( renderer->nestedListenerUserData,
                                                   time, axis, value );
   }
}

static const struct wl_pointer_listener pointerListener= {
   pointerHandleEnter,
   pointerHandleLeave,
   pointerHandleMotion,
   pointerHandleButton,
   pointerHandleAxis
};

static void seatCapabilities( void *data, struct wl_seat *seat, uint32_t capabilities )
{
	WstRenderer *renderer = (WstRenderer*)data;

   if ( capabilities & WL_SEAT_CAPABILITY_KEYBOARD )
   {
      renderer->keyboard= wl_seat_get_keyboard( renderer->seat );
      wl_keyboard_add_listener( renderer->keyboard, &keyboardListener, renderer );
   }
   if ( capabilities & WL_SEAT_CAPABILITY_POINTER )
   {
      renderer->pointer= wl_seat_get_pointer( renderer->seat );
      wl_pointer_add_listener( renderer->pointer, &pointerListener, renderer );
   }
   if ( capabilities & WL_SEAT_CAPABILITY_TOUCH )
   {
      renderer->touch= wl_seat_get_touch( renderer->seat );
   }   
   wl_display_roundtrip( renderer->display );
}

static void seatName( void *data, struct wl_seat *seat, const char *name )
{
	WstRenderer *renderer = (WstRenderer*)data;
}

static const struct wl_seat_listener seatListener = {
   seatCapabilities,
   seatName 
};

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version)
{
	WstRenderer *renderer = (WstRenderer*)data;
   int len;
  
   len= strlen(interface);
   if ( ((len==13) && !strncmp(interface, "wl_compositor", len) ) ) {
      renderer->compositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
   }
   else if ( (len==7) && !strncmp(interface, "wl_seat", len) ) {
      renderer->seat= (struct wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 4);
		wl_seat_add_listener(renderer->seat, &seatListener, renderer);
		wl_display_roundtrip( renderer->display );
   } 
}

static void registryHandleGlobalRemove(void *data, 
                                       struct wl_registry *registry,
			                              uint32_t name)
{
}

static const struct wl_registry_listener registryListener = 
{
	registryHandleGlobal,
	registryHandleGlobalRemove
};

static void* wstNestedThread( void *data )
{
   WstRenderer *renderer= (WstRenderer*)data;
   
   renderer->started= true;
   
   while ( !renderer->stopRequested )
   {
      if ( wl_display_dispatch( renderer->display ) == -1 )
      {
         break;
      }
      
      usleep( 50000 );
   }
   
   renderer->started= false;
}

WstRenderer* WstRendererCreate( const char *moduleName, int argc, char **argv, WstRenderNestedListener *listener, void *userData )
{
   bool error= false;
   void *module= 0, *init;
   
   WstRenderer *renderer= (WstRenderer*)calloc( 1, sizeof(WstRenderer) );
   if ( renderer )
   {
      int rc;
      const char *displayName= 0;
      int i= 0;
      int len, value;
      int width= DEFAULT_OUTPUT_WIDTH;
      int height= DEFAULT_OUTPUT_HEIGHT;
      
      while ( i < argc )
      {
         len= strlen(argv[i]);
         if ( (len == 7) && (strncmp( argv[i], "--width", len) == 0) )
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
         if ( (len == 8) && (strncmp( argv[i], "--height", len) == 0) )
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
         if ( (len == 9) && (strncmp( argv[i], "--display", len) == 0) )
         {
            if ( i+1 < argc )
            {
               ++i;
               displayName= argv[i];
            }
         }
         ++i;
      }
      
      // If a display name was provided we are doing nested composition.  This means we are 
      // compositing the surfaces of our clients onto a wayland surface of another wayland
      // compositor.  Below we connect to the specified wayland display, create a compositor
      // and a surface.  These are stored in the WstRenderer instance.  If populated they
      // signal to the underlying renderer module that nested composition is being done and
      // may cause the renderer to configure itself differently.
      if ( displayName )
      {
         renderer->nestedListenerUserData= userData;
         renderer->nestedListener= listener;

         renderer->display= wl_display_connect( displayName );
         if ( !renderer->display )
         {
            printf("WstRendererCreate: failed to connect to wayland display: %s\n", displayName );
            error= true;
            goto exit;
         }
         
         renderer->registry= wl_display_get_registry(renderer->display);
         if ( !renderer->registry )
         {
            printf("WstRendererCreate: failed to obtain registry from wayland display: %s\n", displayName );
            error= true;
            goto exit;
         }

         wl_registry_add_listener(renderer->registry, &registryListener, renderer);   
         wl_display_roundtrip(renderer->display);
         
         if ( !renderer->compositor )
         {
            printf("WstRendererCreate: failed to obtain compositor from wayland display: %s\n", displayName );
            error= true;
            goto exit;
         }
         
         renderer->surface= wl_compositor_create_surface(renderer->compositor);
         if ( !renderer->surface )
         {
            printf("WstRendererCreate: failed to create compositor surface from wayland display: %s\n", displayName );
            error= true;
            goto exit;
         }
         
         renderer->started= false;
         renderer->stopRequested= false;
         rc= pthread_create( &renderer->nestedThreadId, NULL, wstNestedThread, renderer );
         if ( rc )
         {
            printf("WstRendererCreate: failed to start thread for nested compositor\n" );
            error= true;
            goto exit;
         }
      
         renderer->nestedWidth= width;
         renderer->nestedHeight= height;
      }
      
      module= dlopen( moduleName, RTLD_NOW );
      if ( !module )
      {
         printf("WstRendererCreate: failed to load module (%s)\n", moduleName);
         printf("  detail: %s\n", dlerror() );
         error= true;
         goto exit;
      }
      
      init= dlsym( module, RENDERER_MODULE_INIT );
      if ( !init )
      {
         printf("WstRendererCreate: failed to find module (%s) method (%s)\n", moduleName, RENDERER_MODULE_INIT );
         printf("  detail: %s\n", dlerror() );
         error= true;
         goto exit;
      }

      renderer->outputWidth= DEFAULT_OUTPUT_WIDTH;
      renderer->outputHeight= DEFAULT_OUTPUT_HEIGHT;
      
      rc= ((WSTMethodRenderInit)init)( renderer, argc, argv );
      if ( rc )
      {
         printf("WstRendererCreate: module (%s) init failed: %d\n", moduleName, rc );
         error= true;
         goto exit;
      }
      
      printf("WstRendererCreate: module (%s) loaded and intialized\n", moduleName );
   }
   
exit:

   if ( error )
   {
      if ( renderer )
      {
         WstRendererDestroy( renderer );
      }
      if ( module )
      {
         dlclose( module );
      }
   }
   
   return renderer;
}

void WstRendererDestroy( WstRenderer *renderer )
{
   if ( renderer )
   {
      bool threadStarted= renderer->started;
      if ( renderer->touch )
      {
         wl_touch_destroy( renderer->touch );
      }
      if ( renderer->pointer )
      {
         wl_pointer_destroy( renderer->pointer );
      }
      if ( renderer->keyboard )
      {
         wl_keyboard_destroy( renderer->keyboard );
      }
      if ( renderer->renderer )
      {
         renderer->renderTerm( renderer );
         renderer->renderer= 0;
      }
      if ( renderer->surface )
      {
         wl_surface_destroy( renderer->surface );
         renderer->surface= 0;
      }
      if ( renderer->compositor )
      {
         wl_compositor_destroy( renderer->compositor );
         renderer->compositor= 0;
      }
      if ( renderer->registry )
      {
         wl_registry_destroy( renderer->registry );
         renderer->registry= 0;
      }
      if ( renderer->display )
      {
         wl_display_flush( renderer->display );
         if ( threadStarted )
         {
            renderer->stopRequested= true;
         }
         wl_display_roundtrip( renderer->display );
         wl_display_disconnect( renderer->display );
         renderer->display= 0;
      }
      if ( threadStarted )
      {
         pthread_join( renderer->nestedThreadId, NULL );
      }
      free( renderer );      
   }
}

void WstRendererUpdateScene( WstRenderer *renderer )
{
   renderer->updateScene( renderer );
}

WstRenderSurface* WstRendererSurfaceCreate( WstRenderer *renderer )
{
   return renderer->surfaceCreate( renderer );
}

void WstRendererSurfaceDestroy( WstRenderer *renderer, WstRenderSurface *surface )
{
   renderer->surfaceDestroy( renderer, surface );
}

void WstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, void *buffer )
{
   renderer->surfaceCommit( renderer, surface, buffer );
}

void WstRendererSurfaceCommitMemory( WstRenderer *renderer, WstRenderSurface *surface,
                                     void *data, int width, int height, int format, int stride )
{
   renderer->surfaceCommitMemory( renderer, surface, data, width, height, format, stride );
}

void WstRendererSurfaceSetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool visible )
{
   renderer->surfaceSetVisible( renderer, surface, visible );
}

bool WstRendererSurfaceGetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool *visible )
{
   return renderer->surfaceGetVisible( renderer, surface, visible );
}

void WstRendererSurfaceSetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height )
{
   renderer->surfaceSetGeometry( renderer, surface, x, y, width, height );
}

void WstRendererSurfaceGetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height )
{
   renderer->surfaceGetGeometry( renderer, surface, x, y, width, height );
}

void WstRendererSurfaceSetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float opacity )
{
   renderer->surfaceSetOpacity( renderer, surface, opacity );
}

float WstRendererSurfaceGetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float *opacity )
{
   return renderer->surfaceGetOpacity( renderer, surface, opacity );
}

void WstRendererSurfaceSetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float z )
{
   renderer->surfaceSetZOrder( renderer, surface, z );
}

float WstRendererSurfaceGetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float *z )
{
   return renderer->surfaceGetZOrder( renderer, surface, z );
}


