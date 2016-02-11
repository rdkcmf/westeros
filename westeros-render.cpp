#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <dlfcn.h>

#include "westeros-render.h"

#define DEFAULT_OUTPUT_WIDTH (1280)
#define DEFAULT_OUTPUT_HEIGHT (720)

#define WESTEROS_UNUSED(x) ((void)(x))

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version)
{
	WstRenderer *renderer = (WstRenderer*)data;

   if (strcmp(interface, "wl_compositor") == 0 ) {
      renderer->compositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
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

WstRenderer* WstRendererCreate( const char *moduleName, int argc, char **argv )
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
         if ( renderer->registry )
         {
            wl_registry_destroy( renderer->registry );
         }
         if ( renderer->display )
         {
            wl_display_disconnect( renderer->display );
         }
         free( renderer );
         renderer= 0;
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
         wl_display_disconnect( renderer->display );
         renderer->display= 0;
      }
      free( renderer );      
   }
}

void WstRendererUpdateScene( WstRenderer *renderer )
{
   renderer->updateScene( renderer );
   
   if ( renderer->display )
   {
      // For nested, keep connection active
      wl_display_roundtrip( renderer->display );
   }
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


