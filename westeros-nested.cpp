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
#include <pthread.h>
#include <unistd.h>

#include <map>
#include <vector>

#include "westeros-nested.h"
#include "simpleshell-client-protocol.h"
#include "vpc-client-protocol.h"

#ifdef ENABLE_SBPROTOCOL
#include "simplebuffer-client-protocol.h"
#endif

#define WST_UNUSED(x) ((void)(x))

typedef struct _WstNestedSurfaceInfo
{
   struct wl_surface *surface;
   struct wl_buffer *buffer;
} WstNestedSurfaceInfo;

typedef struct _WstNestedBufferInfo
{
   struct wl_surface *surface;
   struct wl_resource *bufferRemote;
} WstNestedBufferInfo;

typedef struct _WstNestedConnection
{
   WstCompositor *ctx;
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_compositor *compositor;
   struct wl_output *output;
   struct wl_shm *shm;
   struct wl_vpc *vpc;
   struct wl_simple_shell *simpleShell;
   #ifdef ENABLE_SBPROTOCOL
   struct wl_sb *sb;
   #endif
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
   uint32_t pointerEnterSerial;
   std::vector<WstNestedBufferInfo> buffersToRelease;
   std::map<struct wl_surface*, int32_t> surfaceMap;
   std::map<struct wl_surface*, WstNestedSurfaceInfo*> surfaceInfoMap;
   std::map<struct wl_vpc_surface*, struct wl_surface*> vpcSurfaceMap;
} WstNestedConnection;

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
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->outputHandleGeometry( nc->nestedListenerUserData,
                                                x,
                                                y,
                                                mmWidth,
                                                mmHeight,
                                                subPixel,
                                                make,
                                                model,
                                                transform );
   }
}

static void outputHandleMode( void *data,
                              struct wl_output *output,
                              uint32_t flags,
                              int width,
                              int height,
                              int refreshRate )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->outputHandleMode( nc->nestedListenerUserData,
                                            flags,
                                            width,
                                            height,
                                            refreshRate );
   }
}

static void outputHandleDone( void *data,
                              struct wl_output *output )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->outputHandleDone( nc->nestedListenerUserData );
   }   
}

static void outputHandleScale( void *data,
                               struct wl_output *output,
                               int32_t scale )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->outputHandleScale( nc->nestedListenerUserData, scale );
   }   
}

static const struct wl_output_listener outputListener = {
   outputHandleGeometry,
   outputHandleMode,
   outputHandleDone,
   outputHandleScale
};

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
      nc->pointerEnterSerial= serial;
      nc->nestedListener->pointerHandleEnter( nc->nestedListenerUserData,
                                              surface, sx, sy );
   }
}

static void pointerHandleLeave( void *data, struct wl_pointer *pointer,
                                uint32_t serial, struct wl_surface *surface )
{
   WstNestedConnection *nc= (WstNestedConnection*)data;
   
   if ( nc->nestedListener )
   {
      nc->nestedListener->pointerHandleLeave( nc->nestedListenerUserData, surface );
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

static void shmFormat( void *data, struct wl_shm *shm, uint32_t format )
{
	WstNestedConnection *nc = (WstNestedConnection*)data;
	
   if ( nc->nestedListener )
   {
      switch ( format )
      {
         case WL_SHM_FORMAT_ARGB8888:
         case WL_SHM_FORMAT_XRGB8888:
            // Nothing to do for required formats
            break;
         default:
            nc->nestedListener->shmFormat( nc->nestedListenerUserData, 
                                           format );
            break;
      }
   }
}

static const struct wl_shm_listener shmListener = {
   shmFormat
};
static void vpcVideoPathChange(void *data,
                               struct wl_vpc_surface *vpcSurface,
                               uint32_t new_pathway )
{
   WST_UNUSED(vpcSurface);
	WstNestedConnection *nc = (WstNestedConnection*)data;

   if ( nc->nestedListener )
   {
      std::map<struct wl_vpc_surface*,struct wl_surface*>::iterator it= nc->vpcSurfaceMap.find( vpcSurface );
      if ( it != nc->vpcSurfaceMap.end() )
      {
         struct wl_surface *surface= it->second;
         nc->nestedListener->vpcVideoPathChange( nc->nestedListenerUserData,
                                                 surface,
                                                 new_pathway );
      }
   }
}                               

static void vpcVideoXformChange(void *data,
                                struct wl_vpc_surface *vpcSurface,
                                int32_t x_translation,
                                int32_t y_translation,
                                uint32_t x_scale_num,
                                uint32_t x_scale_denom,
                                uint32_t y_scale_num,
                                uint32_t y_scale_denom)
{                                
   WST_UNUSED(vpcSurface);
	WstNestedConnection *nc = (WstNestedConnection*)data;

   if ( nc->nestedListener )
   {
      std::map<struct wl_vpc_surface*,struct wl_surface*>::iterator it= nc->vpcSurfaceMap.find( vpcSurface );
      if ( it != nc->vpcSurfaceMap.end() )
      {
         struct wl_surface *surface= it->second;
         nc->nestedListener->vpcVideoXformChange( nc->nestedListenerUserData,
                                                  surface,
                                                  x_translation,
                                                  y_translation,
                                                  x_scale_num,
                                                  x_scale_denom,
                                                  y_scale_num,
                                                  y_scale_denom );
      }
   }
}

static const struct wl_vpc_surface_listener vpcListener= {
   vpcVideoPathChange,
   vpcVideoXformChange
};

#ifdef ENABLE_SBPROTOCOL
static void sbFormat(void *data, struct wl_sb *wl_sb, uint32_t format)
{
   WST_UNUSED(data);
   WST_UNUSED(wl_sb);
   WST_UNUSED(format);
}

struct wl_sb_listener sbListener = {
	sbFormat
};
#endif

static void simpleShellSurfaceId(void *data,
                                 struct wl_simple_shell *wl_simple_shell,
                                 struct wl_surface *surface,
                                 uint32_t surfaceId)
{
   WST_UNUSED(wl_simple_shell);
	WstNestedConnection *nc = (WstNestedConnection*)data;

   nc->surfaceMap.insert( std::pair<struct wl_surface*,int32_t>( surface, surfaceId ) );     
}
                           
static void simpleShellSurfaceCreated(void *data,
                                      struct wl_simple_shell *wl_simple_shell,
                                      uint32_t surfaceId,
                                      const char *name)
{
   WST_UNUSED(data);
   WST_UNUSED(wl_simple_shell);
   WST_UNUSED(surfaceId);
   WST_UNUSED(name);
}

static void simpleShellSurfaceDestroyed(void *data,
                                        struct wl_simple_shell *wl_simple_shell,
                                        uint32_t surfaceId,
                                        const char *name)
{
   WST_UNUSED(data);
   WST_UNUSED(wl_simple_shell);
   WST_UNUSED(surfaceId);
   WST_UNUSED(name);
}
                                  
static void simpleShellSurfaceStatus(void *data,
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
   WST_UNUSED(data);
   WST_UNUSED(wl_simple_shell);
   WST_UNUSED(surfaceId);
   WST_UNUSED(name);
   WST_UNUSED(visible);
   WST_UNUSED(x);
   WST_UNUSED(y);
   WST_UNUSED(width);
   WST_UNUSED(height);
   WST_UNUSED(opacity);
   WST_UNUSED(zorder);
}                               

static void simpleShellGetSurfacesDone(void *data,
                                       struct wl_simple_shell *wl_simple_shell)
{
   WST_UNUSED(data);
   WST_UNUSED(wl_simple_shell);
}                                        

static const struct wl_simple_shell_listener simpleShellListener = 
{
   simpleShellSurfaceId,
   simpleShellSurfaceCreated,
   simpleShellSurfaceDestroyed,
   simpleShellSurfaceStatus,
   simpleShellGetSurfacesDone
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
   else if ( (len==9) && !strncmp(interface, "wl_output", len) ) {
      nc->output= (struct wl_output*)wl_registry_bind(registry, id, &wl_output_interface, 2);
		wl_output_add_listener(nc->output, &outputListener, nc);
		wl_display_roundtrip( nc->display );
   } 
   else if ( (len==7) && !strncmp(interface, "wl_seat", len) ) {
      nc->seat= (struct wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 4);
		wl_seat_add_listener(nc->seat, &seatListener, nc);
		wl_display_roundtrip( nc->display );
   } 
   else if ( (len==6) && !strncmp(interface, "wl_vpc", len) ) {
      nc->vpc= (struct wl_vpc*)wl_registry_bind(registry, id, &wl_vpc_interface, 1);
   }
   else if ( (len==6) && !strncmp(interface, "wl_shm", len) ) {
      nc->shm= (struct wl_shm*)wl_registry_bind(registry, id, &wl_shm_interface, 1);
      wl_shm_add_listener(nc->shm, &shmListener, nc);
   }
   else if ( (len==15) && !strncmp(interface, "wl_simple_shell", len) ) {
      nc->simpleShell= (struct wl_simple_shell*)wl_registry_bind(registry, id, &wl_simple_shell_interface, 1);      
      wl_simple_shell_add_listener(nc->simpleShell, &simpleShellListener, nc);
   }
   #ifdef ENABLE_SBPROTOCOL
   else if ( (len==5) && !strncmp(interface, "wl_sb", len) ) {
      nc->sb= (struct wl_sb*)wl_registry_bind(registry, id, &wl_sb_interface, 1);
      wl_sb_add_listener(nc->sb, &sbListener, nc);
   }
   #endif
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
      wl_display_flush( nc->display );
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
      
      nc->surfaceMap= std::map<struct wl_surface*, int32_t>();
      nc->surfaceInfoMap= std::map<struct wl_surface*, WstNestedSurfaceInfo*>();
      nc->vpcSurfaceMap= std::map<struct wl_vpc_surface*, struct wl_surface*>();
      nc->buffersToRelease= std::vector<WstNestedBufferInfo>();

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
    
      if ( (width != 0) && (height != 0) )
      {
         nc->surface= wl_compositor_create_surface(nc->compositor);
         if ( !nc->surface )
         {
            printf("WstNestedConnectionCreate: failed to create compositor surface from wayland display: %s\n", displayName );
            error= true;
            goto exit;
         }
         wl_display_roundtrip(nc->display);
      }
      
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
      if ( nc->vpc )
      {
         wl_vpc_destroy( nc->vpc );
         nc->vpc= 0;
      }
      if ( nc->shm )
      {
         wl_shm_destroy( nc->shm );
         nc->shm= 0;
      }
      #ifdef ENABLE_SBPROTOCOL
      if ( nc->sb )
      {
         wl_sb_destroy( nc->sb );
         nc->sb= 0;
      }
      #endif
      if ( nc->output )
      {
         wl_output_destroy( nc->output );
         nc->output= 0;
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
      nc->surfaceMap.clear();
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

wl_surface* WstNestedConnectionGetCompositionSurface( WstNestedConnection *nc )
{
   wl_surface *surface= 0;
   
   if ( nc )
   {
      surface= nc->surface;
   }
   
   return surface;
}

struct wl_surface* WstNestedConnectionCreateSurface( WstNestedConnection *nc )
{
   wl_surface *surface= 0;
   
   if ( nc && nc->compositor )
   {
      WstNestedSurfaceInfo *surfaceInfo;
      
      surface= wl_compositor_create_surface(nc->compositor);
      surfaceInfo= (WstNestedSurfaceInfo*)malloc( sizeof(WstNestedSurfaceInfo) );
      if ( surfaceInfo )
      {
         surfaceInfo->surface= surface;
         surfaceInfo->buffer= 0;
         nc->surfaceInfoMap.insert( std::pair<struct wl_surface*,WstNestedSurfaceInfo*>( surface, surfaceInfo ) );     
      }
      wl_display_flush( nc->display );      
   }
   
   return surface;
}

void WstNestedConnectionDestroySurface( WstNestedConnection *nc, struct wl_surface *surface )
{
   if ( surface )
   {
      {
         std::map<struct wl_surface*,int32_t>::iterator it= nc->surfaceMap.find( surface );
         if ( it != nc->surfaceMap.end() )
         {
            nc->surfaceMap.erase(it);
         }
      }
      {
         std::map<struct wl_surface*,WstNestedSurfaceInfo*>::iterator it= nc->surfaceInfoMap.find( surface );
         if ( it != nc->surfaceInfoMap.end() )
         {
            WstNestedSurfaceInfo *surfaceInfo= it->second;
            if ( surfaceInfo->buffer )
            {
               wl_buffer_destroy( surfaceInfo->buffer );
            }
            free( surfaceInfo );
            nc->surfaceInfoMap.erase(it);
         }
      }
      {
         for ( std::vector<WstNestedBufferInfo>::iterator it= nc->buffersToRelease.begin(); 
               it != nc->buffersToRelease.end();
               ++it )
         {
            if ( surface == (*it).surface )
            {
               it= nc->buffersToRelease.erase(it);
               if ( it == nc->buffersToRelease.end() ) break;
            }
         }         
      }
      wl_surface_destroy( surface );
      wl_display_flush( nc->display );      
   }
}

struct wl_vpc_surface* WstNestedConnectionGetVpcSurface( WstNestedConnection *nc, struct wl_surface *surface )
{
   struct wl_vpc_surface *vpcSurface= 0;
   
   if ( nc && nc->vpc )
   {
      vpcSurface= wl_vpc_get_vpc_surface( nc->vpc, surface );
      if ( vpcSurface )
      {
         wl_vpc_surface_add_listener( vpcSurface, &vpcListener, nc );
         
         nc->vpcSurfaceMap.insert( std::pair<struct wl_vpc_surface*,struct wl_surface*>( vpcSurface, surface ) );     
      }
   }
   
   return vpcSurface;
}

void WstNestedConnectionDestroyVpcSurface( WstNestedConnection *nc, struct wl_vpc_surface *vpcSurface )
{
   if ( vpcSurface )
   {
      std::map<struct wl_vpc_surface*,struct wl_surface*>::iterator it= nc->vpcSurfaceMap.find( vpcSurface );
      if ( it != nc->vpcSurfaceMap.end() )
      {
         nc->vpcSurfaceMap.erase(it);
      }
      wl_vpc_surface_destroy( vpcSurface );
      wl_display_flush( nc->display );      
   }
}

void WstNestedConnectionSurfaceSetVisible( WstNestedConnection *nc, 
                                           struct wl_surface *surface,
                                           bool visible )
{
   if ( surface )
   {
      std::map<struct wl_surface*,int32_t>::iterator it= nc->surfaceMap.find( surface );
      if ( it != nc->surfaceMap.end() )
      {
         int32_t surfaceId= it->second;
         
         wl_simple_shell_set_visible( nc->simpleShell, surfaceId, visible );
      }
   }
}

void WstNestedConnectionSurfaceSetGeometry( WstNestedConnection *nc, 
                                            struct wl_surface *surface,
                                            int x,
                                            int y,
                                            int width, 
                                            int height )
{
   if ( surface )
   {
      std::map<struct wl_surface*,int32_t>::iterator it= nc->surfaceMap.find( surface );
      if ( it != nc->surfaceMap.end() )
      {
         int32_t surfaceId= it->second;
         
         wl_simple_shell_set_geometry( nc->simpleShell, surfaceId, x, y, width, height );
      }
   }
}

void WstNestedConnectionSurfaceSetZOrder( WstNestedConnection *nc, 
                                          struct wl_surface *surface,
                                          float zorder )
{
   if ( surface )
   {
      std::map<struct wl_surface*,int32_t>::iterator it= nc->surfaceMap.find( surface );
      if ( it != nc->surfaceMap.end() )
      {
         int32_t surfaceId= it->second;
         wl_fixed_t z= wl_fixed_from_double(zorder);
         
         wl_simple_shell_set_zorder( nc->simpleShell, surfaceId, z );
      }
   }
}

void WstNestedConnectionSurfaceSetOpacity( WstNestedConnection *nc, 
                                           struct wl_surface *surface,
                                           float opacity )
{
   if ( surface )
   {
      std::map<struct wl_surface*,int32_t>::iterator it= nc->surfaceMap.find( surface );
      if ( it != nc->surfaceMap.end() )
      {
         int32_t surfaceId= it->second;
         wl_fixed_t op= wl_fixed_from_double(opacity);
         
         wl_simple_shell_set_opacity( nc->simpleShell, surfaceId, op );
      }
   }
}

void WstNestedConnectionAttachAndCommit( WstNestedConnection *nc,
                                          struct wl_surface *surface,
                                          struct wl_buffer *buffer,
                                          int x,
                                          int y,
                                          int width,
                                          int height )
{
   if ( nc )
   {
      wl_surface_attach( surface, buffer, 0, 0 );
      wl_surface_damage( surface, x, y, width, height);
      wl_surface_commit( surface );
      wl_display_flush( nc->display );      
   }
}                                          

typedef struct bufferInfo
{
   WstNestedConnection *nc;
   struct wl_surface *surface;
   struct wl_resource *bufferRemote;
} bufferInfo;

static void buffer_release( void *data, struct wl_buffer *buffer )
{
   bufferInfo *binfo= (bufferInfo*)data;
   
   wl_buffer_destroy( buffer );
   
   if ( binfo )
   {
      struct wl_resource *bufferRemote= binfo->bufferRemote;
      std::map<struct wl_surface*,WstNestedSurfaceInfo*>::iterator it= binfo->nc->surfaceInfoMap.find( binfo->surface );
      if ( it != binfo->nc->surfaceInfoMap.end() )
      {
         WstNestedSurfaceInfo *surfaceInfo= it->second;
         surfaceInfo->buffer= 0;
         if ( bufferRemote )
         {
            WstNestedBufferInfo bufferInfo;
            bufferInfo.surface= binfo->surface;
            bufferInfo.bufferRemote= bufferRemote;
            binfo->nc->buffersToRelease.push_back( bufferInfo );
         }
      }
      free(binfo);
   }
}

static struct wl_buffer_listener wl_buffer_listener= 
{
   buffer_release
};

void WstNestedConnectionAttachAndCommitDevice( WstNestedConnection *nc,
                                               struct wl_surface *surface,
                                               struct wl_resource *bufferRemote,
                                               void *deviceBuffer,
                                               uint32_t format,
                                               int32_t stride,
                                               int x,
                                               int y,
                                               int width,
                                               int height )
{
   if ( nc )
   {
      #ifdef ENABLE_SBPROTOCOL
      struct wl_buffer *buffer;
      
      buffer= wl_sb_create_buffer( nc->sb, 
                                   (uint32_t)deviceBuffer, 
                                   width, 
                                   height, 
                                   stride,
                                   format );
      if ( buffer )
      {
         if ( bufferRemote )
         {
            bufferInfo *binfo= (bufferInfo*)malloc( sizeof(bufferInfo) );
            if ( binfo )
            {
               binfo->nc= nc;
               binfo->surface= surface;
               binfo->bufferRemote= bufferRemote;
               wl_buffer_add_listener( buffer, &wl_buffer_listener, binfo );

               std::map<struct wl_surface*,WstNestedSurfaceInfo*>::iterator it= nc->surfaceInfoMap.find( surface );
               if ( it != nc->surfaceInfoMap.end() )
               {
                  WstNestedSurfaceInfo *surfaceInfo= it->second;
                  surfaceInfo->buffer= buffer;
               }
            }
         }
         wl_surface_attach( surface, buffer, 0, 0 );
         wl_surface_damage( surface, x, y, width, height);
         wl_surface_commit( surface );
         wl_display_flush( nc->display );
         if ( !bufferRemote )
         {
            wl_buffer_destroy( buffer );
         }
      }
      #endif
   }
}                                               

void WstNestedConnectionReleaseRemoteBuffers( WstNestedConnection *nc )
{
   while( nc->buffersToRelease.size() )
   {
      std::vector<WstNestedBufferInfo>::iterator it= nc->buffersToRelease.begin();
      struct wl_resource *bufferResource= (*it).bufferRemote;
      wl_buffer_send_release( bufferResource );
      nc->buffersToRelease.erase(it);
   }
}

void WstNestedConnectionPointerSetCursor( WstNestedConnection *nc, 
                                          struct wl_surface *surface, 
                                          int hotspotX, 
                                          int hotspotY )
{
   if ( nc )
   {
      wl_pointer_set_cursor( nc->pointer,
                             nc->pointerEnterSerial,
                             surface,
                             hotspotX,
                             hotspotY );
      wl_display_flush( nc->display );      
   }
}                                          

struct wl_shm_pool* WstNestedConnnectionShmCreatePool( WstNestedConnection *nc, int fd, int size )
{
   WST_UNUSED(nc);
   struct wl_shm_pool *pool= 0;
   
   if ( nc && nc->shm )
   {
      pool= wl_shm_create_pool( nc->shm, fd, size );
      wl_display_flush( nc->display );      
   }
   
   return pool;
}

void WstNestedConnectionShmDestroyPool( WstNestedConnection *nc, struct wl_shm_pool *pool )
{
   WST_UNUSED(nc);
   if ( pool )
   {
      wl_shm_pool_destroy( pool );
      wl_display_flush( nc->display );      
   }
}

void WstNestedConnectionShmPoolResize( WstNestedConnection *nc, struct wl_shm_pool *pool, int size )
{
   WST_UNUSED(nc);
   if ( pool )
   {
      wl_shm_pool_resize( pool, size );
      wl_display_flush( nc->display );      
   }
}

struct wl_buffer* WstNestedConnectionShmPoolCreateBuffer( WstNestedConnection *nc,
                                                          struct wl_shm_pool *pool,
                                                          int32_t offset,
                                                          int32_t width, 
                                                          int32_t height,
                                                          int32_t stride, 
                                                          uint32_t format)
{
   WST_UNUSED(nc);
   struct wl_buffer *buffer= 0;
   
   if ( pool )
   {
      buffer= wl_shm_pool_create_buffer( pool, offset, width, height, stride, format );
      wl_display_flush( nc->display );      
   }
   
   return buffer;
}                                                          

void WstNestedConnectionShmBufferPoolDestroy( WstNestedConnection *nc,
                                              struct wl_shm_pool *pool,
                                              struct wl_buffer *buffer )
{
   WST_UNUSED(nc);
   WST_UNUSED(pool);
   
   if ( buffer )
   {
      wl_buffer_destroy( buffer );
      wl_display_flush( nc->display );      
   }
}

