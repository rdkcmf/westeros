#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <sys/time.h>

#include <vector>

#include "westeros-simpleshell.h"

#include "wayland-server.h"
#include "simpleshell-server-protocol.h"

#define WST_UNUSED( n ) ((void)n)

#define MIN(x,y) (((x) < (y)) ? (x) : (y))

#define DEFAULT_NAME "noname"
#define BROADCAST_DELAY (10)

static void destroy_shell(struct wl_resource *resource);

typedef struct _ShellInfo
{
   struct wl_client *client;
   struct wl_resource *resource;
} ShellInfo;

typedef struct _PendingBroadcastInfo
{
   uint32_t surfaceId;
   long long creationTime;
} PendingBroadcastInfo;

struct wl_simple_shell 
{
   struct wl_display *display;   
   struct wl_global *wl_simple_shell_global;   
   struct wayland_simple_shell_callbacks *callbacks;
   WstRenderer *renderer;
   void *userData;
   struct wl_event_source *delayTimer;
   std::vector<ShellInfo> shells;
   std::vector<uint32_t> surfaces;
   std::vector<PendingBroadcastInfo> pendingCreateBroadcast;
};

static long long getCurrentTimeMillis()
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static void wstISimpleShellSetName(struct wl_client *client, struct wl_resource *resource, 
                                      uint32_t surfaceId, const char *name);
static void wstISimpleShellSetVisible(struct wl_client *client, struct wl_resource *resource, 
                                      uint32_t surfaceId, uint32_t visible);
static void wstISimpleShellSetGeometry(struct wl_client *client, struct wl_resource *resource,
                                       uint32_t surfaceId, int32_t x, int32_t y, int32_t width, int32_t height);
static void wstISimpleShellSetOpacity(struct wl_client *client, struct wl_resource *resource, 
                                      uint32_t surfaceId, wl_fixed_t opacity);
static void wstISimpleShellSetZOrder(struct wl_client *client, struct wl_resource *resource, 
                                     uint32_t surfaceId, wl_fixed_t zorder);
static void wstISimpleShellGetStatus(struct wl_client *client, struct wl_resource *resource, uint32_t surface);
static void wstISimpleShellGetSurfaces(struct wl_client *client, struct wl_resource *resource);

const static struct wl_simple_shell_interface simple_shell_interface = {
   wstISimpleShellSetName,
   wstISimpleShellSetVisible,
   wstISimpleShellSetGeometry,
   wstISimpleShellSetOpacity,
   wstISimpleShellSetZOrder,
   wstISimpleShellGetStatus,
   wstISimpleShellGetSurfaces
};

static void wstISimpleShellSetName(struct wl_client *client, struct wl_resource *resource, 
                                      uint32_t surfaceId, const char *name)
{
	struct wl_simple_shell *shell= (struct wl_simple_shell*)wl_resource_get_user_data(resource);

   shell->callbacks->set_name( shell->userData, surfaceId, name );
}

static void wstISimpleShellSetVisible(struct wl_client *client, struct wl_resource *resource, 
                                      uint32_t surfaceId, uint32_t visible)
{
	struct wl_simple_shell *shell= (struct wl_simple_shell*)wl_resource_get_user_data(resource);

   shell->callbacks->set_visible( shell->userData, surfaceId, (visible != 0) );
}

static void wstISimpleShellSetGeometry(struct wl_client *client, struct wl_resource *resource, 
                                       uint32_t surfaceId, int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct wl_simple_shell *shell= (struct wl_simple_shell*)wl_resource_get_user_data(resource);

   shell->callbacks->set_geometry( shell->userData, surfaceId, x, y, width, height );
}

static void wstISimpleShellSetOpacity(struct wl_client *client, struct wl_resource *resource, 
                                      uint32_t surfaceId, wl_fixed_t opacity)
{
	struct wl_simple_shell *shell= (struct wl_simple_shell*)wl_resource_get_user_data(resource);
   float opacityLevel= wl_fixed_to_double( opacity );

   if ( opacityLevel < 0.0 ) opacityLevel= 0.0;
   if ( opacityLevel > 1.0 ) opacityLevel= 1.0;

   shell->callbacks->set_opacity( shell->userData, surfaceId, opacityLevel );
}

static void wstISimpleShellSetZOrder(struct wl_client *client, struct wl_resource *resource, 
                                     uint32_t surfaceId, wl_fixed_t zorder)
{
	struct wl_simple_shell *shell= (struct wl_simple_shell*)wl_resource_get_user_data(resource);
   float zOrderLevel= wl_fixed_to_double( zorder );

   if ( zOrderLevel < 0.0 ) zOrderLevel= 0.0;
   if ( zOrderLevel > 1.0 ) zOrderLevel= 1.0;

   shell->callbacks->set_zorder( shell->userData, surfaceId, zOrderLevel );
}

static void wstISimpleShellGetStatus(struct wl_client *client, struct wl_resource *resource, uint32_t surfaceId )
{
	struct wl_simple_shell *shell= (struct wl_simple_shell*)wl_resource_get_user_data(resource);
	const char *name= 0;
   bool visible;
   int x, y, width, height;
   float opacity, zorder;
   wl_fixed_t fixedOpacity, fixedZOrder;

   shell->callbacks->get_name( shell->userData, surfaceId, &name );
   if ( !name )
   {
      name= (const char *)DEFAULT_NAME;
   }   

   shell->callbacks->get_status( shell->userData, surfaceId,
                                 &visible,
                                 &x, &y, &width, &height,
                                 &opacity, &zorder );
   
   fixedOpacity= wl_fixed_from_double( (double)opacity );
   fixedZOrder= wl_fixed_from_double( (double)zorder );
   
   wl_simple_shell_send_surface_status( resource, surfaceId, 
                                        name, (visible ? 1 : 0),
                                        x, y, width, height, fixedOpacity, fixedZOrder );
}

static void wstISimpleShellGetSurfaces(struct wl_client *client, struct wl_resource *resource)
{
	struct wl_simple_shell *shell= (struct wl_simple_shell*)wl_resource_get_user_data(resource);

   for( std::vector<uint32_t>::iterator it= shell->surfaces.begin();
        it != shell->surfaces.end();
        ++it )
   {
      uint32_t surfaceId= (*it);
      wstISimpleShellGetStatus(client, resource, surfaceId );
   }
}

static void destroy_shell(struct wl_resource *resource)
{
	struct wl_simple_shell *shell= (struct wl_simple_shell*)wl_resource_get_user_data(resource);
   
   for ( std::vector<ShellInfo>::iterator it= shell->shells.begin(); 
         it != shell->shells.end();
         ++it )
   {
      if ( (*it).resource == resource )
      {
         shell->shells.erase(it);
         break;   
      }
   }
}

static void wstSimpleShellBind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_simple_shell *shell= (struct wl_simple_shell*)data;
   struct wl_resource *resource;
   ShellInfo info;

	printf("westeros-simpleshell: wstSimpleShellBind: enter: client %p data %p version %d id %d\n", client, data, version, id);

   resource= wl_resource_create(client, &wl_simple_shell_interface, MIN(version, 1), id);
   if (!resource) 
   {
      wl_client_post_no_memory(client);
      return;
   }

   wl_resource_set_implementation(resource, &simple_shell_interface, shell, destroy_shell);
   
   info.client= client;
   info.resource= resource;
   shell->shells.push_back( info );
}

static void wstSimpleShellBroadcastCreation( struct wl_simple_shell *shell, uint32_t surfaceId )
{
	const char *name= 0;

   // Get any name the creator may have assigned the surface
   shell->callbacks->get_name( shell->userData, surfaceId, &name );
   if ( !name )
   {
      name= (const char *)DEFAULT_NAME;
   }
   printf("broadcast for surfaceId %x name %s\n", surfaceId, name); //JRW

   // Broadcast the surface creation announcement
   for( std::vector<ShellInfo>::iterator it= shell->shells.begin(); 
        it != shell->shells.end();
        ++it )
   {
      struct wl_resource *shell_resource= (*it).resource;

      wl_simple_shell_send_surface_created( shell_resource, surfaceId, name );
   }
}

static int wstSimpleShellTimeOut( void *data )
{
   bool more= true;
   long long now;
   long long delay;
   PendingBroadcastInfo pendingInfo;
	struct wl_simple_shell *shell= (struct wl_simple_shell*)data;

   while( more )
   {
      if ( shell->pendingCreateBroadcast.size() > 0 )
      {
         pendingInfo= shell->pendingCreateBroadcast.front();
         shell->pendingCreateBroadcast.erase( shell->pendingCreateBroadcast.begin() );

         wstSimpleShellBroadcastCreation( shell, pendingInfo.surfaceId );

         if ( shell->pendingCreateBroadcast.size() > 0 )
         {
            pendingInfo= shell->pendingCreateBroadcast.front();
            now= getCurrentTimeMillis();
            delay= now-pendingInfo.creationTime;
            
            if ( delay >= BROADCAST_DELAY )
            {
               continue;
            }
            else
            {
               delay= BROADCAST_DELAY-delay;
               wl_event_source_timer_update( shell->delayTimer, delay );
               more= false;
            }
         }
      }
      else
      {
         break;
      }
   }
   
   return 0;   
}

wl_simple_shell* WstSimpleShellInit( struct wl_display *display,
                                     wayland_simple_shell_callbacks *callbacks, 
                                     void *userData )
{
   struct wl_simple_shell *shell= 0;
   struct wl_event_loop *loop= 0;
   
	printf("westeros-simpleshell: WstSimpleShellInit: enter: display %p\n", display );
   shell= (struct wl_simple_shell*)calloc( 1, sizeof(struct wl_simple_shell) );
   if ( !shell )
   {
      goto exit;
   }
   
   shell->display= display;
   shell->callbacks= callbacks;
   shell->userData= userData;

   loop= wl_display_get_event_loop(shell->display);
   if ( !loop )
   {
      free( shell );
      shell= 0;
      goto exit;
   }

   shell->delayTimer= wl_event_loop_add_timer( loop, wstSimpleShellTimeOut, shell );
   if ( !shell->delayTimer )
   {
      free( shell );
      shell= 0;
      goto exit;
   }
  
   shell->wl_simple_shell_global= wl_global_create(display, &wl_simple_shell_interface, 1, shell, wstSimpleShellBind );

exit:
	printf("westeros-simpleshell: WstSimpleShellInit: exit: display %p shell %p\n", display, shell);

   return shell;
}

void WstSimpleShellUninit( wl_simple_shell *shell )
{
   if ( shell )
   {
      if ( shell->delayTimer )
      {
         wl_event_source_remove( shell->delayTimer );
         shell->delayTimer= 0;
      }
      shell->pendingCreateBroadcast.clear();
      shell->surfaces.clear();
      shell->shells.clear();
      
      free( shell );
   }
}

void WstSimpleShellNotifySurfaceCreated( wl_simple_shell *shell, struct wl_client *client, 
                                         struct wl_resource *surface_resource, uint32_t surfaceId )
{
   bool creatorNotified= false;
      
   // Add surface to list
   shell->surfaces.push_back(surfaceId);
   
   // Provide surface creator with surfaceId
   for( std::vector<ShellInfo>::iterator it= shell->shells.begin(); 
        it != shell->shells.end();
        ++it )
   {
      if ( (*it).client == client )
      {
         long long now;
         PendingBroadcastInfo pendingInfo;
         struct wl_resource *shell_resource= (*it).resource;
         
         wl_simple_shell_send_surface_id( shell_resource, surface_resource, surfaceId );
         
         creatorNotified= true;
         
         // Perform the surface creation broadcast after an asynchronous
         // delay to give the surface creator time to assign a name
         now= getCurrentTimeMillis();
         pendingInfo.creationTime= now;
         pendingInfo.surfaceId= surfaceId;
         shell->pendingCreateBroadcast.push_back(pendingInfo);
         if ( shell->pendingCreateBroadcast.size() == 1 )
         {
            wl_event_source_timer_update( shell->delayTimer, BROADCAST_DELAY );
         }
         break;
      }
   }
   
   if ( !creatorNotified )
   {
      wstSimpleShellBroadcastCreation( shell, surfaceId );
   }
}

void WstSimpleShellNotifySurfaceDestroyed( wl_simple_shell *shell, struct wl_client *client, uint32_t surfaceId )
{
   const char *name;
   
   WST_UNUSED(client);

   // Get any name the creator may have assigned the surface
   shell->callbacks->get_name( shell->userData, surfaceId, &name );
   if ( !name )
   {
      name= (const char *)DEFAULT_NAME;
   }
   
   // Broadcast the surface destruction announcement
   for( std::vector<ShellInfo>::iterator it= shell->shells.begin(); 
        it != shell->shells.end();
        ++it )
   {
      struct wl_resource *shell_resource= (*it).resource;
      
      wl_simple_shell_send_surface_destroyed( shell_resource, surfaceId, name );
   }

   // Remove surface from list   
   for( std::vector<uint32_t>::iterator it= shell->surfaces.begin();
        it != shell->surfaces.end();
        ++it )
   {
      if ( (*it) == surfaceId )
      {
         shell->surfaces.erase(it);
         break;
      }
   }
}


