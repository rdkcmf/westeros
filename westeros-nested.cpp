#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <pthread.h>
#include <unistd.h>

#include "westeros-nested.h"

#define WST_UNUSED(x) ((void)(x))

typedef struct _WstNestedConnection
{
   WstCompositor *ctx;
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_compositor *compositor;
   struct wl_surface *surface;
   struct wl_seat *seat;
   struct wl_keyboard *keyboard;
   struct wl_pointer *pointer;
   struct wl_touch *touch;
   int nestedWidth;
   int nestedHeight;
   void *nestedListenerUserData;
   WstNestedConnectionListener *nestedListener;
   bool started;
   bool stopRequested;
   pthread_t nestedThreadId;
} WstNestedConnection;


static void keyboardHandleKeymap( void *data, struct wl_keyboard *keyboard, 
                                  uint32_t format, int fd, uint32_t size )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->keyboardHandleKeyMap( nc->nestedListenerUserData,
                                                format, fd, size );
   }
}

static void keyboardHandleEnter( void *data, struct wl_keyboard *keyboard,
                                 uint32_t serial, struct wl_surface *surface, 
                                 struct wl_array *keys )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->keyboardHandleEnter( nc->nestedListenerUserData,
                                               keys );
   }
}

static void keyboardHandleLeave( void *data, struct wl_keyboard *keyboard,
                                 uint32_t serial, struct wl_surface *surface )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->keyboardHandleLeave( nc->nestedListenerUserData );
   }
}

static void keyboardHandleKey( void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t time, uint32_t key,
                               uint32_t state)
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->keyboardHandleKey( nc->nestedListenerUserData,
                                             time, key, state );
   }
}

static void keyboardHandleModifiers( void *data, struct wl_keyboard *keyboard,
                                     uint32_t serial, uint32_t mods_depressed,
                                     uint32_t mods_latched, uint32_t mods_locked,
                                     uint32_t group )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->keyboardHandleModifiers( nc->nestedListenerUserData,
                                                         mods_depressed, mods_latched,
                                                         mods_locked, group );
   }
}

static void keyboardHandleRepeatInfo( void *data, struct wl_keyboard *keyboard,
                                      int32_t rate, int32_t delay )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->keyboardHandleRepeatInfo( nc->nestedListenerUserData,
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
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->pointerHandleEnter( nc->nestedListenerUserData,
                                              sx, sy );
   }
}

static void pointerHandleLeave( void *data, struct wl_pointer *pointer,
                                uint32_t serial, struct wl_surface *surface )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->pointerHandleLeave( nc->nestedListenerUserData );
   }
}

static void pointerHandleMotion( void *data, struct wl_pointer *pointer, 
                                 uint32_t time, wl_fixed_t sx, wl_fixed_t sy )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->pointerHandleMotion( nc->nestedListenerUserData,
                                               time, sx, sy );
   }
}

static void pointerHandleButton( void *data, struct wl_pointer *pointer,
                                 uint32_t serial, uint32_t time, 
                                 uint32_t button, uint32_t state )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->pointerHandleButton( nc->nestedListenerUserData,
                                               time, button, state );
   }
}

static void pointerHandleAxis( void *data, struct wl_pointer *pointer,
                               uint32_t time, uint32_t axis, wl_fixed_t value )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->pointerHandleAxis( nc->nestedListenerUserData,
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
	WstNestedConnection *nc = (WstNestedConnection*)data;

   if ( capabilities & WL_SEAT_CAPABILITY_KEYBOARD )
   {
      nc->keyboard= wl_seat_get_keyboard( nc->seat );
      wl_keyboard_add_listener( nc->keyboard, &keyboardListener, nc );
   }
   if ( capabilities & WL_SEAT_CAPABILITY_POINTER )
   {
      nc->pointer= wl_seat_get_pointer( nc->seat );
      wl_pointer_add_listener( nc->pointer, &pointerListener, nc );
   }
   if ( capabilities & WL_SEAT_CAPABILITY_TOUCH )
   {
      nc->touch= wl_seat_get_touch( nc->seat );
   }   
   wl_display_roundtrip( nc->display );
}

static void seatName( void *data, struct wl_seat *seat, const char *name )
{
   WST_UNUSED(data);
   WST_UNUSED(seat);
   WST_UNUSED(name);
}

static const struct wl_seat_listener seatListener = {
   seatCapabilities,
   seatName 
};

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version)
{
	WstNestedConnection *nc = (WstNestedConnection*)data;
   int len;
  
   len= strlen(interface);
   if ( ((len==13) && !strncmp(interface, "wl_compositor", len) ) ) {
      nc->compositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
   }
   else if ( (len==7) && !strncmp(interface, "wl_seat", len) ) {
      nc->seat= (struct wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 4);
		wl_seat_add_listener(nc->seat, &seatListener, nc);
		wl_display_roundtrip( nc->display );
   } 
}

static void registryHandleGlobalRemove(void *data, 
                                       struct wl_registry *registry,
			                              uint32_t name)
{
   WST_UNUSED(data);
   WST_UNUSED(registry);
   WST_UNUSED(name);
}

static const struct wl_registry_listener registryListener = 
{
	registryHandleGlobal,
	registryHandleGlobalRemove
};

static void* wstNestedThread( void *data )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   nc->started= true;
   
   while ( !nc->stopRequested )
   {
      if ( wl_display_dispatch( nc->display ) == -1 )
      {
         break;
      }
      
      usleep( 50000 );
   }
 
   nc->started= false;
   if ( !nc->stopRequested )
   {
      nc->nestedListener->connectionEnded( nc->nestedListenerUserData );
   }
}

WstNestedConnection* WstNestedConnectionCreate( WstCompositor *wctx, 
                                                const char *displayName, 
                                                int width, 
                                                int height,
                                                WstNestedConnectionListener *listener,
                                                void *userData )
{
   WstNestedConnection *nc= 0;
   bool error= false;
   int rc;
   
   nc= (WstNestedConnection*)calloc( 1, sizeof(WstNestedConnection) );
   if ( nc )
   {
      nc->ctx= wctx;
      nc->nestedListenerUserData= userData;
      nc->nestedListener= listener;

      nc->display= wl_display_connect( displayName );
      if ( !nc->display )
      {
         printf("WstNestedConnectionCreate: failed to connect to wayland display: %s\n", displayName );
         error= true;
         goto exit;
      }
      
      nc->registry= wl_display_get_registry(nc->display);
      if ( !nc->registry )
      {
         printf("WstNestedConnectionCreate: failed to obtain registry from wayland display: %s\n", displayName );
         error= true;
         goto exit;
      }

      wl_registry_add_listener(nc->registry, &registryListener, nc);   
      wl_display_roundtrip(nc->display);
      
      if ( !nc->compositor )
      {
         printf("WstNestedConnectionCreate: failed to obtain compositor from wayland display: %s\n", displayName );
         error= true;
         goto exit;
      }
      
      nc->surface= wl_compositor_create_surface(nc->compositor);
      if ( !nc->surface )
      {
         printf("WstNestedConnectionCreate: failed to create compositor surface from wayland display: %s\n", displayName );
         error= true;
         goto exit;
      }
      wl_display_roundtrip(nc->display);
      
      nc->started= false;
      nc->stopRequested= false;
      rc= pthread_create( &nc->nestedThreadId, NULL, wstNestedThread, nc );
      if ( rc )
      {
         printf("WstNestedConnectionCreate: failed to start thread for nested compositor\n" );
         error= true;
         goto exit;
      }
   
      nc->nestedWidth= width;
      nc->nestedHeight= height;
   }

exit:

   if ( error )
   {
      if ( nc )
      {
         WstNestedConnectionDestroy( nc );
         nc= 0;
      }
   }
   
   return nc;   
}

void WstNestedConnectionDisconnect( WstNestedConnection *nc )
{
   if ( nc )
   {
      if ( nc->started )
      {
         nc->stopRequested= true;
         wl_display_flush( nc->display );
         wl_display_roundtrip( nc->display );
         pthread_join( nc->nestedThreadId, NULL );
      }
   }
}

void WstNestedConnectionDestroy( WstNestedConnection *nc )
{
   if ( nc )
   {
      bool threadStarted= nc->started;
      if ( threadStarted )
      {
         nc->stopRequested= true;
         wl_display_flush( nc->display );
         wl_display_roundtrip( nc->display );
         pthread_join( nc->nestedThreadId, NULL );
      }
      if ( nc->touch )
      {
         wl_touch_destroy( nc->touch );
         nc->touch= 0;
      }
      if ( nc->pointer )
      {
         wl_pointer_destroy( nc->pointer );
         nc->pointer= 0;
      }
      if ( nc->keyboard )
      {
         wl_keyboard_destroy( nc->keyboard );
         nc->keyboard= 0;
      }
      if ( nc->surface )
      {
         wl_surface_destroy( nc->surface );
         nc->surface= 0;
      }
      if ( nc->compositor )
      {
         wl_compositor_destroy( nc->compositor );
         nc->compositor= 0;
      }
      if ( nc->registry )
      {
         wl_registry_destroy( nc->registry );
         nc->registry= 0;
      }
      if ( nc->display )
      {
         wl_display_flush( nc->display );
         wl_display_roundtrip( nc->display );
         wl_display_disconnect( nc->display );
         nc->display= 0;
      }
      free( nc );
   }
}

wl_display* WstNestedConnectionGetDisplay( WstNestedConnection *nc )
{
   wl_display *display= 0;
   
   if ( nc )
   {
      display= nc->display;
   }
   
   return display;
}

wl_surface* WstNestedConnectionGetSurface( WstNestedConnection *nc )
{
   wl_surface *surface= 0;
   
   if ( nc )
   {
      surface= nc->surface;
   }
   
   return surface;
}

