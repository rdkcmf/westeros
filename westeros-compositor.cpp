#include "westeros-compositor.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <map>
#include <vector>

#include <xkbcommon/xkbcommon.h>

#include "wayland-server.h"
#include "westeros-nested.h"
#ifdef ENABLE_SBPROTOCOL
#include "westeros-simplebuffer.h"
#endif
#include "westeros-simpleshell.h"
#include "xdg-shell-server-protocol.h"

#define WST_MAX_ERROR_DETAIL (512)

#define MAX_NESTED_NAME_LEN (32)

#define DEFAULT_FRAME_RATE (60)
#define DEFAULT_OUTPUT_WIDTH (1280)
#define DEFAULT_OUTPUT_HEIGHT (720)
#define DEFAULT_NESTED_WIDTH (1280)
#define DEFAULT_NESTED_HEIGHT (720)

#define DEFAULT_KEY_REPEAT_DELAY (1000)
#define DEFAULT_KEY_REPEAT_RATE  (5)

#define WESTEROS_UNUSED(x) ((void)(x))

#define MIN(x,y) (((x) < (y)) ? (x) : (y))

#define INT_FATAL(FORMAT, ...)      printf("Westeros Fatal: " FORMAT "\n", ##__VA_ARGS__)
#define INT_ERROR(FORMAT, ...)      printf("Westeros Error: " FORMAT "\n", ##__VA_ARGS__)
#define INT_WARNING(FORMAT, ...)    printf("Westeros Warning: " FORMAT "\n",  ##__VA_ARGS__)
#define INT_INFO(FORMAT, ...)       printf("Westeros Info: " FORMAT "\n",  ##__VA_ARGS__)
#define INT_DEBUG(FORMAT, ...)      printf("Westeros Debug: " FORMAT "\n", ##__VA_ARGS__)
#define INT_TRACE(FORMAT, ...)

#define FATAL(FORMAT, ...)          INT_FATAL(FORMAT, ##__VA_ARGS__)
#define ERROR(FORMAT, ...)          INT_ERROR(FORMAT, ##__VA_ARGS__)
#define WARNING(FORMAT, ...)        INT_WARNING(FORMAT, ##__VA_ARGS__)
#define INFO(FORMAT, ...)           INT_INFO(FORMAT, ##__VA_ARGS__)
#define DEBUG(FORMAT, ...)          INT_DEBUG(FORMAT, ##__VA_ARGS__)
#define TRACE(FORMAT, ...)          INT_TRACE(FORMAT, ##__VA_ARGS__)

typedef struct _WstSurface WstSurface;
typedef struct _WstSeat WstSeat;

typedef struct _WstShellSurface
{
   struct wl_resource *resource;
   WstSurface *surface;
   const char* title;
   const char* className;
} WstShellSurface;

typedef struct _WstKeyboard
{
   WstSeat *seat;
   struct wl_list resourceList;
   struct wl_array keys;

   uint32_t currentModifiers;

   struct xkb_state *state;
   xkb_mod_index_t modShift;
   xkb_mod_index_t modAlt;
   xkb_mod_index_t modCtrl;
   xkb_mod_index_t modCaps;
} WstKeyboard;

typedef struct _WstPointer
{
   WstSeat *seat;
   struct wl_list resourceList;
   struct wl_list focusResourceList;
   bool entered;
   WstSurface *focus;
   WstSurface *pointerSurface;
   int32_t hotSpotX;
   int32_t hotSpotY;
   int32_t pointerX;
   int32_t pointerY;
} WstPointer;

typedef struct _WstSeat
{
   WstCompositor *compositor;
   struct wl_list resourceList;
   const char *seatName;
   int keyRepeatDelay;
   int keyRepeatRate;
   
   WstKeyboard *keyboard;
   WstPointer *pointer;
} WstSeat;

typedef enum _WstBufferType 
{
  WstBufferType_null,
  WstBufferType_shm,
  WstBufferType_sb
} WstBufferType;

typedef struct _WstRegion
{
   struct wl_resource *resource;

   WstCompositor *compositor;
} WstRegion;

typedef struct _WstSurfaceFrameCallback
{
   struct wl_resource *resource;
   struct wl_list link;
} WstSurfaceFrameCallback;

typedef struct _WstSurface
{
   struct wl_resource *resource;
   
   WstCompositor *compositor;
   int surfaceId;
	
	WstRenderer *renderer;
	WstRenderSurface *surface;
	std::vector<WstShellSurface*> shellSurface;
	const char *roleName;
	float zorder;

   const char *name;	
	int refCount;
   
   struct wl_resource *attachedBufferResource;
   WstBufferType attachedBufferType;
   
   struct wl_list frameCallbackList;
   
} WstSurface;

typedef struct _WstSurfaceInfo
{
   WstSurface *surface;
} WstSurfaceInfo;

typedef struct _WstClientInfo
{
   struct wl_resource *sbResource;
   WstSurface *surface;
} WstClientInfo;

typedef struct _WstCompositor
{
   const char *displayName;
   unsigned int frameRate;
   int framePeriodMillis;
   const char *rendererModule;
   bool isNested;
   bool isEmbedded;
   const char *nestedDisplayName;
   unsigned int nestedWidth;
   unsigned int nestedHeight;
   bool allowModifyCursor;
   int outputWidth;
   int outputHeight;

   void *terminatedUserData;
   WstTerminatedCallback terminatedCB;
   void *invalidateUserData;
   WstInvalidateSceneCallback invalidateCB;
   void *hidePointerUserData;
   WstHidePointerCallback hidePointerCB;
   void *clientStatusUserData;
   WstClientStatus clientStatusCB;
   
   bool running;
   bool stopRequested;
   bool compositorReady;
   bool compositorThreadStarted;
   pthread_t compositorThreadId;
   
   char lastErrorDetail[WST_MAX_ERROR_DETAIL];

   struct xkb_rule_names xkbNames;
   struct xkb_context *xkbCtx;
   struct xkb_keymap *xkbKeymap;
   uint32_t xkbKeymapFormat;
   int xkbKeymapSize;
   int xkbKeymapFd;
   char *xkbKeymapArea;
   
   pthread_mutex_t mutex;

   WstSeat *seat;
   WstRenderer *renderer;
   
   WstNestedConnection *nc;
   void *nestedListenerUserData;
   WstNestedConnectionListener nestedListener;
   
   void *keyboardNestedListenerUserData;
   WstKeyboardNestedListener *keyboardNestedListener;
   void *pointerNestedListenerUserData;
   WstPointerNestedListener *pointerNestedListener;

   struct wl_display *display;
   #ifdef ENABLE_SBPROTOCOL
   struct wl_sb *sb;
   #endif
   struct wl_simple_shell *simpleShell;
   struct wl_event_source *displayTimer;

   int nextSurfaceId;
   std::vector<WstSurface*> surfaces;
   std::map<int32_t, WstSurface*> surfaceMap;
   std::map<struct wl_client*, WstClientInfo*> clientInfoMap;
   std::map<struct wl_resource*, WstSurfaceInfo*> surfaceInfoMap;

   bool needRepaint;
} WstCompositor;

static const char* wstGetNextNestedDisplayName(void);
static void* wstCompositorThread( void *arg );
static long long wstGetCurrentTimeMillis(void);
static void wstCompositorComposeFrame( WstCompositor *ctx );
static int wstCompositorDisplayTimeOut( void *data );
static void wstCompositorScheduleRepaint( WstCompositor *ctx );
static void wstCompositorBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void wstDestroyCompositorCallback(struct wl_resource *resource);
static void wstICompositorCreateSurface( struct wl_client *client, struct wl_resource *resource, uint32_t id);
static void wstICompositorCreateRegion( struct wl_client *client, struct wl_resource *resource, uint32_t id);
static void wstDestroySurfaceCallback(struct wl_resource *resource);
static WstSurface* wstSurfaceCreate( WstCompositor *ctx);
static void wstSurfaceDestroy( WstSurface *surface );
static bool wstSurfaceSetRole( WstSurface *surface, const char *roleName, 
                               struct wl_resource *errorResource, uint32_t errorCode );
static void wstSurfaceInsertSurface( WstCompositor *ctx, WstSurface *surface );
static WstSurface* wstGetSurfaceFromSurfaceId( WstCompositor *ctx, int32_t surfaceId );
static WstSurfaceInfo* wstGetSurfaceInfo( WstCompositor *ctx, struct wl_resource *resource );
static void wstUpdateClientInfo( WstCompositor *ctx, struct wl_client *client, struct wl_resource *resource );
static void wstISurfaceDestroy(struct wl_client *client, struct wl_resource *resource);
static void wstISurfaceAttach(struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *buffer_resource, int32_t sx, int32_t sy);
static void wstISurfaceDamage(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t x, int32_t y, int32_t width, int32_t height);
static void wstISurfaceFrame(struct wl_client *client,
                             struct wl_resource *resource, uint32_t callback);
static void wstISurfaceSetOpaqueRegion(struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *region_resource);
static void wstISurfaceSetInputRegion(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *region_resource);
static void wstISurfaceCommit(struct wl_client *client, struct wl_resource *resource);
static void wstISurfaceSetBufferTransform(struct wl_client *client,
                                          struct wl_resource *resource, int transform);
static void wstISurfaceSetBufferScale(struct wl_client *client,
                                      struct wl_resource *resource,
                                      int32_t scale);
static WstRegion *wstRegionCreate( WstCompositor *ctx );
static void wstRegionDestroy( WstRegion *region );
static void wstDestroyRegionCallback(struct wl_resource *resource);
static void wstIRegionDestroy( struct wl_client *client, struct wl_resource *resource );
static void wstIRegionAdd( struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y, int32_t width, int32_t height );
static void wstIRegionSubtract( struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t x, int32_t y, int32_t width, int32_t height );
static void wstShellBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void wstIShellGetShellSurface(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     struct wl_resource *shell_resource );
static void wstDestroyShellSurfaceCallback(struct wl_resource *resource);
static void wstShellSurfaceDestroy( WstShellSurface *shellSurface );
static void wstIShellSurfacePong(struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t serial );
static void wstIShellSurfaceMove(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *seat_resource,
                                 uint32_t serial );
static void wstIShellSurfaceResize(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *seat_resource,
                                   uint32_t serial,
                                   uint32_t edges );
static void wstIShellSurfaceSetTopLevel(struct wl_client *client, 
                                        struct wl_resource *resource);
static void wstIShellSurfaceSetTransient(struct wl_client *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *parent_resource,
                                         int x, int y, uint32_t flags );
static void wstIShellSurfaceSetFullscreen(struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t method,
                                          uint32_t framerate,
                                          struct wl_resource *output_resource );
static void wstIShellSurfaceSetPopup(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *seat_resource,
                                     uint32_t serial,
                                     struct wl_resource *parent_resource,
                                     int x, int y, uint32_t flags );
static void wstIShellSurfaceSetMaximized(struct wl_client *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *output_resource );
static void wstIShellSurfaceSetTitle(struct wl_client *client,
                                     struct wl_resource *resource,
                                     const char *title );
static void wstIShellSurfaceSetClass(struct wl_client *client,
                                     struct wl_resource *resource,
                                     const char *className );
static void wstXdgShellBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void wstIXdgDestroy( struct wl_client *client, struct wl_resource *resource );
static void wstIXdgUseUnstableVersion( struct wl_client *client, struct wl_resource *resource, int32_t version );
static void wstIXdgGetXdgSurface( struct wl_client *client, 
                                  struct wl_resource *resource,
                                  uint32_t id,
                                  struct wl_resource *surface_resource );
static void  wstIXdgGetXdgPopup( struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t id,
                                 struct wl_resource *surface_resource,
                                 struct wl_resource *parent_resource,
                                 struct wl_resource *seat_resource,
                                 uint32_t serial,
                                 int32_t x,
                                 int32_t y
                                 #if defined ( USE_XDG_VERSION4 )
                                 ,
                                 uint32_t flags 
                                 #endif
                               );
static void wstIXdgPong( struct wl_client *client,
                         struct wl_resource *resource,
                         uint32_t serial );
static void wstIXdgShellSurfaceDestroy( struct wl_client *client,
                                        struct wl_resource *resource );
static void wstIXdgShellSurfaceSetParent( struct wl_client *client,
                                          struct wl_resource *resource,
                                          struct wl_resource *parent_resource );
static void wstIXdgShellSurfaceSetTitle( struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *title );
static void wstIXdgShellSurfaceSetAppId( struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *app_id );
static void wstIXdgShellSurfaceShowWindowMenu( struct wl_client *client,
                                               struct wl_resource *resource,
                                               struct wl_resource *seat_resource,
                                               uint32_t serial,
                                               int32_t x,
                                               int32_t y );
static void wstIXdgShellSurfaceMove( struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *seat_resource,
                                     uint32_t serial );
static void wstIXdgShellSurfaceResize( struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *seat_resource,
                                       uint32_t serial,
                                       uint32_t edges );
static void wstIXdgShellSurfaceAckConfigure( struct wl_client *client,
                                             struct wl_resource *resource,
                                             uint32_t serial );
static void wstIXdgShellSurfaceSetWindowGeometry( struct wl_client *client,
                                                  struct wl_resource *resource,
                                                  int32_t x,
                                                  int32_t y,
                                                  int32_t width,
                                                  int32_t height );
static void wstIXdgShellSurfaceSetMaximized( struct wl_client *client,
                                             struct wl_resource *resource );
static void wstIXdgShellSurfaceUnSetMaximized( struct wl_client *client,
                                               struct wl_resource *resource );
static void wstIXdgShellSurfaceSetFullscreen( struct wl_client *client,
                                              struct wl_resource *resource,
                                              struct wl_resource *output_resource );
static void wstIXdgShellSurfaceUnSetFullscreen( struct wl_client *client,
                                                struct wl_resource *resource );
static void wstIXdgShellSurfaceSetMinimized( struct wl_client *client,
                                             struct wl_resource *resource );
static void wstDefaultNestedConnectionEnded( void *userData );
static void wstDefaultNestedKeyboardHandleKeyMap( void *userData, uint32_t format, int fd, uint32_t size );
static void wstDefaultNestedKeyboardHandleEnter( void *userData, struct wl_array *keys );
static void wstDefaultNestedKeyboardHandleLeave( void *userData );
static void wstDefaultNestedKeyboardHandleKey( void *userData, uint32_t time, uint32_t key, uint32_t state );
static void wstDefaultNestedKeyboardHandleModifiers( void *userData, uint32_t mods_depressed, uint32_t mods_latched, 
                                                     uint32_t mods_locked, uint32_t group );
static void wstDefaultNestedKeyboardHandleRepeatInfo( void *userData, int32_t rate, int32_t delay );
static void wstDefaultNestedPointerHandleEnter( void *userData, wl_fixed_t sx, wl_fixed_t sy );
static void wstDefaultNestedPointerHandleLeave( void *userData );
static void wstDefaultNestedPointerHandleMotion( void *userData, uint32_t time, wl_fixed_t sx, wl_fixed_t sy );
static void wstDefaultNestedPointerHandleButton( void *userData, uint32_t time, uint32_t button, uint32_t state );
static void wstDefaultNestedPointerHandleAxis( void *userData, uint32_t time, uint32_t axis, wl_fixed_t value );
static void wstSetDefaultNestedListener( WstCompositor *ctx );
static bool wstSeatInit( WstCompositor *ctx );
static void wstSeatTerm( WstCompositor *ctx );
static void wstResourceUnBindCallback( struct wl_resource *resource );
static void wstSeatBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void wstISeatGetPointer( struct wl_client *client, struct wl_resource *resource, uint32_t id );
static void wstISeatGetKeyboard( struct wl_client *client, struct wl_resource *resource, uint32_t id );
static void wstISeatGetTouch( struct wl_client *client, struct wl_resource *resource, uint32_t id );
static void wstIKeyboardRelease( struct wl_client *client, struct wl_resource *resource );
static void wstIPointerSetCursor( struct wl_client *client, 
                                  struct wl_resource *resource,
                                  uint32_t serial,
                                  struct wl_resource *surface_resource,
                                  int32_t hotspot_x,
                                  int32_t hotspot_y );                                  
static void wstIPointerRelease( struct wl_client *client, struct wl_resource *resource );
static bool wstInitializeKeymap( WstCompositor *ctx );
static void wstTerminateKeymap( WstCompositor *ctx );
static void wstProcessKeyEvent( WstKeyboard *keyboard, uint32_t keyCode, uint32_t keyState, uint32_t modifiers );
static void wstKeyboardSendModifiers( WstKeyboard *keyboard, struct wl_resource *resource );
static void wstProcessPointerEnter( WstPointer *pointer );
static void wstProcessPointerLeave( WstPointer *pointer );
static void wstProcessPointerMoveEvent( WstPointer *pointer, int32_t x, int32_t y );
static void wstProcessPointerButtonEvent( WstPointer *pointer, uint32_t button, uint32_t buttonState );
static void wstPointerCheckFocus( WstPointer *pointer, int32_t x, int32_t y );
static void wstPointerSetPointer( WstPointer *pointer, WstSurface *surface );
static void wstPointerUpdatePosition( WstPointer *pointer );
static void wstPointerSetFocus( WstPointer *pointer, WstSurface *surface, wl_fixed_t x, wl_fixed_t y );
static void wstPointerMoveFocusToClient( WstPointer *pointer, struct wl_client *client );

extern char **environ;
static pthread_mutex_t g_mutex= PTHREAD_MUTEX_INITIALIZER;
static int g_pid= 0;
static int g_nextNestedId= 0;


WstCompositor* WstCompositorCreate()
{
   WstCompositor *ctx= 0;
   
   ctx= (WstCompositor*)calloc( 1, sizeof(WstCompositor) );
   if ( ctx )
   {
      pthread_mutex_init( &ctx->mutex, 0 );
      
      ctx->frameRate= DEFAULT_FRAME_RATE;
      ctx->framePeriodMillis= (1000/ctx->frameRate);
      
      ctx->nestedWidth= DEFAULT_NESTED_WIDTH;
      ctx->nestedHeight= DEFAULT_NESTED_HEIGHT;
      
      ctx->outputWidth= DEFAULT_OUTPUT_WIDTH;
      ctx->outputHeight= DEFAULT_OUTPUT_HEIGHT;
      
      ctx->surfaceMap= std::map<int32_t, WstSurface*>();
      ctx->clientInfoMap= std::map<struct wl_client*, WstClientInfo*>();
      ctx->surfaceInfoMap= std::map<struct wl_resource*, WstSurfaceInfo*>();
      
      ctx->xkbNames.rules= strdup("evdev");
      ctx->xkbNames.model= strdup("pc105");
      ctx->xkbNames.layout= strdup("us");
      ctx->xkbKeymapFd= -1;

      wstSetDefaultNestedListener( ctx );
   }
   
   return ctx;
}

void WstCompositorDestroy( WstCompositor *ctx )
{
   if ( ctx )
   {
      if ( ctx->running )
      {
         WstCompositorStop( ctx );
      }
      
      if ( ctx->displayName )
      {
         free( (void*)ctx->displayName );
         ctx->displayName= 0;
      }
      
      if ( ctx->rendererModule )
      {
         free( (void*)ctx->rendererModule );
         ctx->rendererModule= 0;
      }
      
      if ( ctx->nestedDisplayName )
      {
         free( (void*)ctx->nestedDisplayName );
         ctx->nestedDisplayName= 0;
      }
      
      if ( ctx->xkbNames.rules )
      {
         free( (void*)ctx->xkbNames.rules );
         ctx->xkbNames.rules= 0;
      }

      if ( ctx->xkbNames.model )
      {
         free( (void*)ctx->xkbNames.model );
         ctx->xkbNames.model= 0;
      }

      if ( ctx->xkbNames.layout )
      {
         free( (void*)ctx->xkbNames.layout );
         ctx->xkbNames.layout= 0;
      }

      if ( ctx->xkbNames.variant )
      {
         free( (void*)ctx->xkbNames.variant );
         ctx->xkbNames.variant= 0;
      }

      if ( ctx->xkbNames.options )
      {
         free( (void*)ctx->xkbNames.options );
         ctx->xkbNames.options= 0;
      }
      
      pthread_mutex_destroy( &ctx->mutex );
      
      free( ctx );
   }
}

const char *WstCompositorGetLastErrorDetail( WstCompositor *ctx )
{
   const char *detail= 0;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      
      detail= ctx->lastErrorDetail;
      
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return detail;
}

bool WstCompositorSetDisplayName( WstCompositor *ctx, const char *displayName )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set display name while compositor is running" );
         goto exit;
      }
      
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->displayName )
      {
         free( (void*)ctx->displayName );
      }
      
      ctx->displayName= strdup( displayName );

      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorSetFrameRate( WstCompositor *ctx, unsigned int frameRate )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( frameRate == 0 )
      {
         sprintf( ctx->lastErrorDetail,
                  "Invalid argument.  The frameRate (%u) must be greater than 0 fps", frameRate );
         goto exit;      
      }

      pthread_mutex_lock( &ctx->mutex );
      
      ctx->frameRate= frameRate;
      ctx->framePeriodMillis= (1000/frameRate);
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorSetRendererModule( WstCompositor *ctx, const char *rendererModule )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set renderer module while compositor is running" );
         goto exit;
      }
               
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->rendererModule )
      {
         free( (void*)ctx->rendererModule );
      }
      
      ctx->rendererModule= strdup( rendererModule );
               
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }
   
exit:

   return result;
}

bool WstCompositorSetIsNested( WstCompositor *ctx, bool isNested )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set isNested while compositor is running" );
         goto exit;
      }
                     
      pthread_mutex_lock( &ctx->mutex );
      
      ctx->isNested= isNested;
               
      pthread_mutex_unlock( &ctx->mutex );
            
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorSetIsEmbedded( WstCompositor *ctx, bool isEmbedded )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set isEmbedded while compositor is running" );
         goto exit;
      }
                     
      pthread_mutex_lock( &ctx->mutex );
      
      ctx->isEmbedded= isEmbedded;
               
      pthread_mutex_unlock( &ctx->mutex );
            
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorSetNestedDisplayName( WstCompositor *ctx, const char *nestedDisplayName )
{
   bool result= false;
   int len;
   const char *name= 0;
   
   if ( ctx )
   {
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set nested display name while compositor is running" );
         goto exit;
      }
      
      if ( nestedDisplayName )
      {
         len= strlen(nestedDisplayName);
         
         if ( (len == 0) || (len > MAX_NESTED_NAME_LEN) )
         {
            sprintf( ctx->lastErrorDetail,
                     "Invalid argument.  The nested name length (%u) must be > 0 and < %u in length", 
                     len, MAX_NESTED_NAME_LEN );
            goto exit;      
         }
         
         name= strdup( nestedDisplayName );
      }
                     
      pthread_mutex_lock( &ctx->mutex );
      
      if ( ctx->nestedDisplayName )
      {
         free( (void*)ctx->nestedDisplayName );
      }
      
      ctx->nestedDisplayName= name;
               
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorSetNestedSize( WstCompositor *ctx, unsigned int width, unsigned int height )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set nested size while compositor is running" );
         goto exit;
      }      

      if ( width == 0 )
      {
         sprintf( ctx->lastErrorDetail,
                  "Invalid argument.  The nested width (%u) must be greater than zero", width );
         goto exit;      
      }

      if ( height == 0 )
      {
         sprintf( ctx->lastErrorDetail,
                  "Invalid argument.  The nested height (%u) must be greater than zero", height );
         goto exit;      
      }
               
      pthread_mutex_lock( &ctx->mutex );
      
      ctx->nestedWidth= width;
      ctx->nestedHeight= height;
               
      pthread_mutex_unlock( &ctx->mutex );
            
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorSetAllowCursorModification( WstCompositor *ctx, bool allow )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set allow cursor modification while compositor is running" );
         goto exit;
      }      

      pthread_mutex_lock( &ctx->mutex );
      
      ctx->allowModifyCursor= allow;
               
      pthread_mutex_unlock( &ctx->mutex );
            
      result= true;
   }

exit:
   
   return result;
}

void WstCompositorGetOutputDimensions( WstCompositor *ctx, unsigned int *width, unsigned int *height )
{
   int outputWidth= 0;
   int outputHeight= 0;
   
   if ( ctx )
   {               
      pthread_mutex_lock( &ctx->mutex );
      
      outputWidth= ctx->outputWidth;
      outputHeight= ctx->outputHeight;
               
      pthread_mutex_unlock( &ctx->mutex );
   }

   if ( width )
   {
      *width= outputWidth;
   }
   if ( height )
   {
      *height= outputHeight;
   }
}

const char *WstCompositorGetDisplayName( WstCompositor *ctx )
{
   const char *displayName= 0;
   
   if ( ctx )
   {
               
      pthread_mutex_lock( &ctx->mutex );
      
      // If no display name was provided, then generate a name.
      if ( !ctx->displayName )
      {
         ctx->displayName= wstGetNextNestedDisplayName();
      }

      displayName= ctx->displayName;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return displayName;
}

unsigned int WstCompositorGetFrameRate( WstCompositor *ctx )
{
   unsigned int frameRate= 0;
   
   if ( ctx )
   {               
      pthread_mutex_lock( &ctx->mutex );
      
      frameRate= ctx->frameRate;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return frameRate;
}

const char *WstCompositorGetRendererModule( WstCompositor *ctx )
{
   const char *rendererModule= 0;
   
   if ( ctx )
   {               
      pthread_mutex_lock( &ctx->mutex );
      
      rendererModule= ctx->rendererModule;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return rendererModule;
}

bool WstCompositorGetIsNested( WstCompositor *ctx )
{
   bool isNested= false;
   
   if ( ctx )
   {               
      pthread_mutex_lock( &ctx->mutex );
      
      isNested= ctx->isNested;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return isNested;
}

bool WstCompositorGetIsEmbedded( WstCompositor *ctx )
{
   bool isEmbedded= false;
   
   if ( ctx )
   {               
      pthread_mutex_lock( &ctx->mutex );
      
      isEmbedded= ctx->isEmbedded;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return isEmbedded;
}

const char *WstCompositorGetNestedDisplayName( WstCompositor *ctx )
{
   const char *nestedDisplayName= 0;
   
   if ( ctx )
   {               
      pthread_mutex_lock( &ctx->mutex );
      
      nestedDisplayName= ctx->nestedDisplayName;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return nestedDisplayName;
}

void WstCompositorGetNestedSize( WstCompositor *ctx, unsigned int *width, unsigned int *height )
{
   int nestedWidth= 0;
   int nestedHeight= 0;
   
   if ( ctx )
   {               
      pthread_mutex_lock( &ctx->mutex );
      
      nestedWidth= ctx->nestedWidth;
      nestedHeight= ctx->nestedHeight;
               
      pthread_mutex_unlock( &ctx->mutex );
   }

   if ( width )
   {
      *width= nestedWidth;
   }
   if ( height )
   {
      *height= nestedHeight;
   }
}

bool WstCompositorGetAllowCursorModification( WstCompositor *ctx )
{
   bool allow= false;
   
   if ( ctx )
   {               
      pthread_mutex_lock( &ctx->mutex );
      
      allow= ctx->allowModifyCursor;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return allow;
}

bool WstCompositorSetTerminatedCallback( WstCompositor *ctx, WstTerminatedCallback cb, void *userData )
{
   bool result= false;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->terminatedUserData= userData;
      ctx->terminatedCB= cb;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetInvalidateCallback( WstCompositor *ctx, WstInvalidateSceneCallback cb, void *userData )
{
   bool result= false;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isEmbedded )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is not embedded" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      ctx->invalidateUserData= userData;
      ctx->invalidateCB= cb;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetHidePointerCallback( WstCompositor *ctx, WstHidePointerCallback cb, void *userData )
{
   bool result= false;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isEmbedded )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is not embedded" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      ctx->hidePointerUserData= userData;
      ctx->hidePointerCB= cb;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetClientStatusCallback( WstCompositor *ctx, WstClientStatus cb, void *userData )
{
  bool result= false;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isEmbedded )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is not embedded" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      ctx->clientStatusUserData= userData;
      ctx->clientStatusCB= cb;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetKeyboardNestedListener( WstCompositor *ctx, WstKeyboardNestedListener *listener, void *userData )
{
  bool result= false;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set keyboard nested listener while compositor is running" );
         goto exit;
      }      

      if ( !ctx->isNested )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is not nested" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      ctx->keyboardNestedListenerUserData= userData;
      ctx->keyboardNestedListener= listener;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetPointerNestedListener( WstCompositor *ctx, WstPointerNestedListener *listener, void *userData )
{
  bool result= false;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set pointer nested listener while compositor is running" );
         goto exit;
      }      

      if ( !ctx->isNested )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is not nested" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      ctx->pointerNestedListenerUserData= userData;
      ctx->pointerNestedListener= listener;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorComposeEmbedded( WstCompositor *ctx, int width, int height, int resW, int resH, float *matrix, float alpha )
{
   bool result= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isEmbedded )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is not embedded" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      if ( !ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is not running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
   
      if ( ctx->compositorReady )
      {   
         ctx->renderer->outputWidth= width;
         ctx->renderer->outputHeight= height;
         ctx->renderer->resW= resW;
         ctx->renderer->resH= resH;
         ctx->renderer->matrix= matrix;
         ctx->renderer->alpha= alpha;
         
         WstRendererUpdateScene( ctx->renderer );
      }
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorStart( WstCompositor *ctx )
{
   bool result= false;
   int rc;
                  
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is already running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      if ( !ctx->rendererModule && ctx->isEmbedded )
      {
         ctx->rendererModule= strdup("libwesteros_render_embedded.so.0");
      }
      
      if ( !ctx->rendererModule )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  A renderer module must be supplied" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;      
      }
      
      if ( ctx->isNested )
      {
         // If we are operating as a nested compostitor the name
         // of the wayland display we are to pass our composited output
         // to must be provided
         if ( !ctx->nestedDisplayName )
         {
            char *var= getenv("WAYLAND_DISPLAY");
            if ( var )
            {
               ctx->nestedDisplayName= strdup(var);
            }
         }
         if ( !ctx->nestedDisplayName )
         {
            sprintf( ctx->lastErrorDetail,
                     "Error.  Nested composition requested but no target display name provided" );
            pthread_mutex_unlock( &ctx->mutex );
            goto exit;      
         }
      }
         
      // If no display name was provided, then generate a name.
      if ( !ctx->displayName )
      {
         ctx->displayName= wstGetNextNestedDisplayName();
      }

      if ( !ctx->isNested )
      {
         // Setup key map
         result= wstInitializeKeymap( ctx );
         if ( !result )
         {
            pthread_mutex_unlock( &ctx->mutex );
            goto exit;      
         }
      }
      
      rc= pthread_create( &ctx->compositorThreadId, NULL, wstCompositorThread, ctx );
      if ( rc )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Failed to start compositor main thread: %d", rc );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;      
      }

      ctx->running= true;

      result= true;      

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:
   
   return result;
}

void WstCompositorStop( WstCompositor *ctx )
{
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->running )
      {
         ctx->running= false;
         
         if ( ctx->compositorThreadStarted )
         {
            pthread_t threadId= ctx->compositorThreadId;
            ctx->stopRequested= true;
            if ( ctx->display )
            {
               wl_display_terminate(ctx->display);
            }
            pthread_mutex_unlock( &ctx->mutex );
            pthread_join( ctx->compositorThreadId, NULL );
            pthread_mutex_lock( &ctx->mutex );
         }

         if ( !ctx->isNested )
         {
            wstTerminateKeymap( ctx );         
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorKeyEvent( WstCompositor *ctx, int keyCode, unsigned int keyState, unsigned int modifiers )
{
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         WstKeyboard *keyboard= ctx->seat->keyboard;
         
         if ( keyboard )
         {
            wstProcessKeyEvent( keyboard, keyCode, keyState, modifiers );
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorPointerEnter( WstCompositor *ctx )
{
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         WstPointer *pointer= ctx->seat->pointer;
         
         if ( pointer )
         {
            wstProcessPointerEnter( pointer );
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorPointerLeave( WstCompositor *ctx )
{
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         WstPointer *pointer= ctx->seat->pointer;
         
         if ( pointer )
         {
            wstProcessPointerLeave( pointer );
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorPointerMoveEvent( WstCompositor *ctx, int x, int y )
{
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         WstPointer *pointer= ctx->seat->pointer;
         
         if ( pointer )
         {
            wstProcessPointerMoveEvent( pointer, x, y );
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorPointerButtonEvent( WstCompositor *ctx, unsigned int button, unsigned int buttonState )
{
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         WstPointer *pointer= ctx->seat->pointer;
         
         if ( pointer )
         {
            wstProcessPointerButtonEvent( pointer, button, buttonState );
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

bool WstCompositorLaunchClient( WstCompositor *ctx, const char *cmd )
{
   bool result= false;
   int rc;
   int i, len, numArgs, numEnvVar;
   char work[256];
   char *p1, *p2;
   char **args= 0;
   char **env= 0;
   char *envDisplay= 0;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      
      if ( !ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is not running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      i= (cmd ? strlen(cmd) : 0);
      if ( !cmd || (i > 255) )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad argument.  cmd (%p len %d) rejected", cmd, i );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }      
      
      // Build argument set for the client
      numArgs= 0;
      p1= (char *)cmd;
      do
      {
         p2= strpbrk( p1, " " );
         if ( !p2 )
         {
            if ( strlen( p1 ) > 0 )
            {
               ++numArgs;
            }
         }
         else
         {
            ++numArgs;
            p1= p2+1;
         }
      }
      while( p2 );
      
      printf( "numArgs= %d\n", numArgs );
      
      args= (char**)calloc( 1, (numArgs+1)*sizeof(char*) );
      if ( !args )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Unable to allocate memory for client arguments (%d args)", numArgs );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      i= 0;
      p1= (char *)cmd;
      do
      {
         p2= strpbrk( p1, " " );
         if ( !p2 )
         {
            if ( strlen( p1 ) > 0 )
            {
               args[i]= strdup(p1);
               ++i;
            }
         }
         else
         {
            args[i]= strndup( p1, (p2-p1) );
            p1= p2+1;
            ++i;
         }
      }
      while( p2 );
      
      for( i= 0; i < numArgs; ++i )
      {
         if ( args[i] == 0 )
         {
            sprintf( ctx->lastErrorDetail,
                     "Error.  Unable to allocate memory for client argument %d", i );
            pthread_mutex_unlock( &ctx->mutex );
            goto exit;
         }
         printf( "arg[%d]= %s\n", i, args[i] );
      }

      // Build environment for client
      if ( environ )
      {
         int i= numEnvVar= 0;
         for( ; ; )
         {
            char *var= environ[i];
            if ( var == NULL )
            {
               break;
            }
            len= strlen(var);
            if ( (len >= 16) && !strncmp( "WAYLAND_DISPLAY=", var, 16) )
            {
               //skip this var
            }
            else
            {
               ++numEnvVar;
            }
            ++i;
         }
      }
      
      env= (char**)calloc( 1, (numEnvVar+2)*sizeof(char*) );
      if ( !env )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Unable to allocate memory for client environment (%d vars)", numEnvVar );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      for( int i= 0, j= 0; j < numEnvVar; ++i )
      {
         char *var= environ[i];
         len= strlen(var);
         if ( (len >= 16) && !strncmp( "WAYLAND_DISPLAY=", var, 16) )
         {
            //skip this var
         }
         else
         {
            env[j++]= environ[i];
         }
      }
      sprintf( work, "WAYLAND_DISPLAY=%s", ctx->displayName );
      envDisplay= strdup( work );
      if ( !envDisplay )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Unable to allocate memory for client diplay name" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      env[numEnvVar]= envDisplay;
      ++numEnvVar;
      env[numEnvVar-1]= envDisplay;
      for( int i= 0; i < numEnvVar; ++i )
      {
         printf( "env[%d]= %s\n", i, env[i] );
      }
      
      pthread_mutex_unlock( &ctx->mutex );
      
      // Launch client
      int pid= fork();
      if ( pid == 0 )
      {
         rc= execvpe( args[0], args, env );
         if ( rc < 0 )
         {
            printf("execvpe: errno %d\n", errno );
         }
         exit(0);
      }
      else if ( pid < 0 )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Unable to fork process" );
         goto exit;
      }
      else
      {
         int pidChild, status;

         pidChild= waitpid( pid, &status, 0 );
         if ( pidChild != 0 )
         {
            int clientStatus, detail= 0;
            
            if ( WIFSIGNALED(status) )
            {
               int signo= WTERMSIG(status);
               clientStatus= WstClient_stoppedAbnormal;
               detail= signo;
            }
            else if ( WIFEXITED(status) )
            {
               int exitCode= WEXITSTATUS(status);
               clientStatus= WstClient_stoppedNormal;
               detail= exitCode;
            }
            else
            {
               clientStatus= WstClient_stoppedNormal;
               detail= 0;
            }
            
            if ( ctx->clientStatusCB )
            {
               INFO("clientStatus: status %d pid %d detail %d", clientStatus, pidChild, detail); 
               ctx->clientStatusCB( ctx, clientStatus, pidChild, detail, ctx->clientStatusUserData );
            }
         }
      }
      
      result= true;
   }
   
exit:

   if ( envDisplay )
   {
      free( envDisplay );
   }
   
   if ( env )
   {
      free( env );
   }
   
   if ( args )
   {
      for( i= 0; i < numArgs; ++i )
      {
         if ( args[i] )
         {
            free( args[i] );
         }
      }
      free( args );
   }

   return result;
}



/*
 * ----------------- Internal methods --------------------------------------------------------------
 */

static const char* wstGetNextNestedDisplayName(void)
{
   char *name= 0;
   char work[32];
   int id;
   
   pthread_mutex_lock( &g_mutex );
   
   if ( g_pid == 0 )
   {
      g_pid= getpid();
   }
   
   id= g_nextNestedId;
   
   ++g_nextNestedId;
   
   sprintf( work, "westeros-%u-%u", g_pid, id );
   
   pthread_mutex_unlock( &g_mutex );
   
   name= strdup(work);
   
   return name;
}

#ifdef ENABLE_SBPROTOCOL
static void sbBind( void *user_data, struct wl_client *client, struct wl_resource *resource)
{
   WstCompositor *ctx= (WstCompositor*)user_data;
      
   wstUpdateClientInfo( ctx, client, resource );
}

static void sbReferenceBuffer(void *userData, struct wl_client *client, uint32_t name, struct wl_sb_buffer *buffer)
{
   WESTEROS_UNUSED(userData);
   WESTEROS_UNUSED(client);
   
   // The value of 'native_handle' is the address or id of the low level buffer that is valid across process boundaries
   buffer->driverBuffer= (void*)name;
}

static void sbReleaseBuffer(void *userData, struct wl_sb_buffer *buffer)
{
   WstCompositor *ctx= (WstCompositor*)userData;

   for ( int i= 0; i < ctx->surfaces.size(); ++i )
   {      
      WstSurface *surface= ctx->surfaces[i];
      struct wl_resource *resource= buffer->resource;
      if ( surface->attachedBufferResource == resource )
      {
         surface->attachedBufferResource= 0;
         surface->attachedBufferType= WstBufferType_null;
         break;
      }
   }
}

static struct wayland_sb_callbacks sbCallbacks= {
   sbBind,
   sbReferenceBuffer,
   sbReleaseBuffer
};
#endif

static void simpleShellSetName( void* userData, uint32_t surfaceId, const char *name )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      surface->name= strdup(name);
      DEBUG("set surfaceId %x name to %s", surfaceId, name );
   }
}

static void simpleShellGetName( void* userData, uint32_t surfaceId, const char **name )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      *name= surface->name;
   }
}

static void simpleShellSetVisible( void* userData, uint32_t surfaceId, bool visible )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      WstRendererSurfaceSetVisible( ctx->renderer, surface->surface, visible );
   }
}

static void simpleShellGetVisible( void* userData, uint32_t surfaceId, bool *visible )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      WstRendererSurfaceGetVisible( ctx->renderer, surface->surface, visible );
   }
}

static void simpleShellSetGeometry( void* userData, uint32_t surfaceId, int x, int y, int width, int height )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      WstRendererSurfaceSetGeometry( ctx->renderer, surface->surface, x, y, width, height );
   }
}

static void simpleShellGetGeometry( void* userData, uint32_t surfaceId, int *x, int *y, int *width, int *height )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      WstRendererSurfaceGetGeometry( ctx->renderer, surface->surface, x, y, width, height );
   }
}

static void simpleShellSetOpacity( void* userData, uint32_t surfaceId, float opacity )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      WstRendererSurfaceSetOpacity( ctx->renderer, surface->surface, opacity );
   }
}

static void simpleShellGetOpacity( void* userData, uint32_t surfaceId, float *opacity )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      WstRendererSurfaceGetOpacity( ctx->renderer, surface->surface, opacity );
   }
}

static void simpleShellSetZOrder( void* userData, uint32_t surfaceId, float zorder )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      WstRendererSurfaceSetZOrder( ctx->renderer, surface->surface, zorder );
      
      surface->zorder= zorder;
      wstSurfaceInsertSurface( ctx, surface );
   }
}

static void simpleShellGetZOrder( void* userData, uint32_t surfaceId, float *zorder )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      WstRendererSurfaceGetZOrder( ctx->renderer, surface->surface, zorder );
   }
}

struct wayland_simple_shell_callbacks simpleShellCallbacks= {
   simpleShellSetName,
   simpleShellGetName,
   simpleShellSetVisible,
   simpleShellGetVisible,
   simpleShellSetGeometry,
   simpleShellGetGeometry,
   simpleShellSetOpacity,
   simpleShellGetOpacity,
   simpleShellSetZOrder,
   simpleShellGetZOrder
};

static void* wstCompositorThread( void *arg )
{
   WstCompositor *ctx= (WstCompositor*)arg;
   int rc;
   struct wl_display *display= 0;
	struct wl_event_loop *loop= 0;
	int argc;
	char arg0[MAX_NESTED_NAME_LEN+1];
	char arg1[MAX_NESTED_NAME_LEN+1];
	char arg2[MAX_NESTED_NAME_LEN+1];
	char arg3[MAX_NESTED_NAME_LEN+1];
	char *argv[4]= { arg0, arg1, arg2, arg3 };

   ctx->compositorThreadStarted= true;

   DEBUG("calling wl_display_create");
   display= wl_display_create();
   DEBUG("wl_display=%p", display);
   if ( !display )
   {
      ERROR("unable to create primary display");
      goto exit;
   }

   ctx->display= display;

	wl_display_init_shm(ctx->display);

	if (!wl_global_create(ctx->display, &wl_compositor_interface, 3, ctx, wstCompositorBind))
	{
      ERROR("unable to create wl_compositor interface");
      goto exit;
   }

	if (!wl_global_create(ctx->display, &wl_shell_interface, 1, ctx, wstShellBind))
	{
      ERROR("unable to create wl_shell interface");
      goto exit;
   }

	if (!wl_global_create(ctx->display, &xdg_shell_interface, 1, ctx, wstXdgShellBind))
	{
      ERROR("unable to create xdg-shell interface");
      goto exit;
   }

   if ( !wstSeatInit( ctx ) )
   {
      ERROR("unable to intialize seat");
      goto exit;
   }
   
	if (!wl_global_create(ctx->display, &wl_seat_interface, 4, ctx->seat, wstSeatBind))
	{
      ERROR("unable to create wl_seat interface");
      goto exit;
   }

	loop= wl_display_get_event_loop(ctx->display);
	if ( !loop )
	{
      ERROR("unable to get wayland event loop");
      goto exit;
	}
   
   if ( ctx->displayName )
   {
      rc= wl_display_add_socket( ctx->display, ctx->displayName );
      if ( rc )
      {
         ERROR("unable to create socket name (%s)", ctx->displayName );
         goto exit;
      }
   }
   else
   {
     const char *name= wl_display_add_socket_auto(ctx->display);
     if ( !name )
     {
        ERROR("unable to create socket");
        goto exit;
     }
     ctx->displayName= strdup(name);
   }
   DEBUG("wl_display=%p displayName=%s", ctx->display, ctx->displayName );

   if ( ctx->isNested )
   {
      ctx->nc= WstNestedConnectionCreate( ctx, 
                                          ctx->nestedDisplayName, 
                                          ctx->nestedWidth, 
                                          ctx->nestedHeight,
                                          &ctx->nestedListener,
                                          ctx );
      if ( !ctx->nc )
      {
         ERROR( "Unable to create nested connection to display %s", ctx->nestedDisplayName );
         goto exit;
      }
      
      argc= 4;
      strcpy( arg0, "--width" );
      sprintf( arg1, "%u", ctx->outputWidth );
      strcpy( arg2, "--height" );
      sprintf( arg3, "%u", ctx->outputHeight );
   }
   else
   {
      argc= 0;
   }
   
   ctx->renderer= WstRendererCreate( ctx->rendererModule, argc, (char **)argv, ctx->nc );
   if ( !ctx->renderer )
   {
      ERROR("unable to initialize renderer module");
      goto exit;
   }

   #ifdef ENABLE_SBPROTOCOL
   ctx->sb= WstSBInit( ctx->display, &sbCallbacks, ctx );
   if ( !ctx->sb )
   {
      ERROR("unable to create wl_sb interface");
      goto exit;
   }
   #endif
   
   ctx->simpleShell= WstSimpleShellInit( ctx->display, &simpleShellCallbacks, ctx );
   if ( !ctx->simpleShell )
   {
      ERROR("unable to create wl_simple_shell interface");
      goto exit;
   }

   ctx->displayTimer= wl_event_loop_add_timer( loop, wstCompositorDisplayTimeOut, ctx );
   pthread_mutex_lock( &ctx->mutex );
   wl_event_source_timer_update( ctx->displayTimer, ctx->framePeriodMillis );
   pthread_mutex_unlock( &ctx->mutex );
   
   ctx->compositorReady= true;   
   DEBUG("calling wl_display_run for display: %s", ctx->displayName );
   wl_display_run(ctx->display);
   DEBUG("done calling wl_display_run for display: %s", ctx->displayName );

exit:

   ctx->compositorReady= false;
     
   DEBUG("display: %s terminating...", ctx->displayName );
   
   ctx->compositorThreadStarted= false;

   if ( ctx->displayTimer )
   {
      wl_event_source_remove( ctx->displayTimer );
      ctx->displayTimer= 0;
   }
      
   if ( ctx->simpleShell )
   {
      WstSimpleShellUninit( ctx->simpleShell );
      ctx->simpleShell= 0;
   }
   
   #ifdef ENABLE_SBPROTOCOL
   if ( ctx->sb )
   {
      WstSBUninit( ctx->sb );
      ctx->sb= 0;
   }
   #endif

   if ( ctx->nc )
   {
      WstNestedConnectionDisconnect( ctx->nc );
      ctx->nc= 0;
   }
   
   if ( ctx->renderer )
   {
      WstRendererDestroy( ctx->renderer );
      ctx->renderer= 0;
   }
   
   if ( ctx->nc )
   {
      WstNestedConnectionDestroy( ctx->nc );
      ctx->nc= 0;
   }
   
   if ( ctx->seat )
   {
      wstSeatTerm( ctx );
   }
   
   if ( ctx->display )
   {
      wl_display_destroy(ctx->display);
      ctx->display= 0;      
   }
   DEBUG("display: %s terminated", ctx->displayName );

   ctx->surfaceMap.clear();
   
   while( ctx->surfaceInfoMap.size() >  0 )
   {
      std::map<struct wl_resource*,WstSurfaceInfo*>::iterator it= ctx->surfaceInfoMap.begin();
      WstSurfaceInfo *surfaceInfo= it->second;
      ctx->surfaceInfoMap.erase( it );
      free( surfaceInfo );
   }   
   
   while( ctx->clientInfoMap.size() >  0 )
   {
      std::map<struct wl_client*,WstClientInfo*>::iterator it= ctx->clientInfoMap.begin();
      WstClientInfo *clientInfo= it->second;
      ctx->clientInfoMap.erase( it );
      free( clientInfo );
   }
   
   return NULL;
}

static long long wstGetCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static void wstCompositorComposeFrame( WstCompositor *ctx )
{
   uint32_t frameTime;
   
   frameTime= (uint32_t)wstGetCurrentTimeMillis();
   
   pthread_mutex_lock( &ctx->mutex );

   ctx->needRepaint= false;

   if ( !ctx->isEmbedded )
   {
      WstRendererUpdateScene( ctx->renderer );
   }
   
   for( std::map<struct wl_resource*, WstSurfaceInfo*>::iterator it= ctx->surfaceInfoMap.begin(); it != ctx->surfaceInfoMap.end(); ++it )
   {
      WstSurfaceFrameCallback *fcb;
      WstSurface *surface;
      WstSurfaceInfo *surfaceInfo= it->second;
      
      surface= surfaceInfo->surface;
      while( !wl_list_empty( &surface->frameCallbackList ) )
      {
         fcb= wl_container_of( surface->frameCallbackList.next, fcb, link);
         wl_list_remove( surface->frameCallbackList.next );
         wl_callback_send_done( fcb->resource, frameTime );
         wl_resource_destroy( fcb->resource );
         free(fcb);
      }
   }
   
   pthread_mutex_unlock( &ctx->mutex );
}

static int wstCompositorDisplayTimeOut( void *data )
{
   WstCompositor *ctx= (WstCompositor*)data;
   
   if ( ctx->needRepaint )
   {
      wstCompositorComposeFrame( ctx );
      
      if ( ctx->invalidateCB )
      {
         ctx->invalidateCB( ctx, ctx->invalidateUserData );
      }
   }

   pthread_mutex_lock( &ctx->mutex );
   wl_event_source_timer_update( ctx->displayTimer, ctx->framePeriodMillis );
   pthread_mutex_unlock( &ctx->mutex );
   
   return 0;
}

static void wstCompositorScheduleRepaint( WstCompositor *ctx )
{
   if ( !ctx->needRepaint )
   {
      ctx->needRepaint= true;
   }
}

static const struct wl_compositor_interface compositor_interface= 
{
   wstICompositorCreateSurface,
	wstICompositorCreateRegion
};

static const struct wl_surface_interface surface_interface= 
{
   wstISurfaceDestroy,
   wstISurfaceAttach,
   wstISurfaceDamage,
   wstISurfaceFrame,
   wstISurfaceSetOpaqueRegion,
   wstISurfaceSetInputRegion,
   wstISurfaceCommit,
   wstISurfaceSetBufferTransform,
   wstISurfaceSetBufferScale
};

static const struct wl_region_interface region_interface=
{
   wstIRegionDestroy,
   wstIRegionAdd,
   wstIRegionSubtract
};

static const struct wl_shell_interface shell_interface=
{
   wstIShellGetShellSurface
};

static const struct wl_shell_surface_interface shell_surface_interface=
{
   wstIShellSurfacePong,
   wstIShellSurfaceMove,
   wstIShellSurfaceResize,
   wstIShellSurfaceSetTopLevel,
   wstIShellSurfaceSetTransient,
   wstIShellSurfaceSetFullscreen,
   wstIShellSurfaceSetPopup,
   wstIShellSurfaceSetMaximized,
   wstIShellSurfaceSetTitle,
   wstIShellSurfaceSetClass
};

#if defined ( USE_XDG_VERSION4 )
static const struct xdg_shell_interface xdg_shell_interface_impl=
{
   wstIXdgUseUnstableVersion,
   wstIXdgGetXdgSurface,
   wstIXdgGetXdgPopup,
   wstIXdgPong
};
#elif defined ( USE_XDG_VERSION5 )
static const struct xdg_shell_interface xdg_shell_interface_impl=
{
   wstIXdgDestroy,
   wstIXdgUseUnstableVersion,
   wstIXdgGetXdgSurface,
   wstIXdgGetXdgPopup,
   wstIXdgPong
};
#else

#error "No supported version of xdg_shell protocol specified"

#endif

static const struct xdg_surface_interface xdg_surface_interface_impl=
{
   wstIXdgShellSurfaceDestroy,
   wstIXdgShellSurfaceSetParent,
   wstIXdgShellSurfaceSetTitle,
   wstIXdgShellSurfaceSetAppId,
   wstIXdgShellSurfaceShowWindowMenu,
   wstIXdgShellSurfaceMove,
   wstIXdgShellSurfaceResize,
   wstIXdgShellSurfaceAckConfigure,
   wstIXdgShellSurfaceSetWindowGeometry,
   wstIXdgShellSurfaceSetMaximized,
   wstIXdgShellSurfaceUnSetMaximized,
   wstIXdgShellSurfaceSetFullscreen,
   wstIXdgShellSurfaceUnSetFullscreen,
   wstIXdgShellSurfaceSetMinimized
};

static const struct wl_seat_interface seat_interface=
{
   wstISeatGetPointer,
   wstISeatGetKeyboard,
   wstISeatGetTouch   
};

static const struct wl_keyboard_interface keyboard_interface=
{
   wstIKeyboardRelease
};

static const struct wl_pointer_interface pointer_interface=
{
   wstIPointerSetCursor,
   wstIPointerRelease
};

static void wstCompositorBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstCompositor *ctx= (WstCompositor*)data;
   struct wl_resource *resource;

   DEBUG("wstCompositorBind: client %p data %p version %d id %d", client, data, version, id );
   
   resource= wl_resource_create(client, 
                                &wl_compositor_interface,
                                MIN(version, 3), 
                                id);
   if (!resource) 
   {
      wl_client_post_no_memory(client);
      return;
   }

   wl_resource_set_implementation(resource, &compositor_interface, ctx, wstDestroyCompositorCallback);
   
   int pid= 0;
   wl_client_get_credentials( client, &pid, NULL, NULL );
   INFO("display %s client pid %d connected", ctx->displayName, pid );
   if ( ctx->clientStatusCB )
   {
      ctx->clientStatusCB( ctx, WstClient_connected, pid, 0, ctx->clientStatusUserData );
   }
}

static void wstDestroyCompositorCallback(struct wl_resource *resource)
{
   WstCompositor *ctx= (WstCompositor*)wl_resource_get_user_data(resource);
   
   int pid= 0;
   struct wl_client *client= wl_resource_get_client(resource);
   wl_client_get_credentials( client, &pid, NULL, NULL );
   INFO("display %s client pid %d disconnected", ctx->displayName, pid );
   if ( ctx->clientStatusCB )
   {
      ctx->clientStatusCB( ctx, WstClient_disconnected, pid, 0, ctx->clientStatusUserData );
   }
}

static void wstICompositorCreateSurface( struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   WstCompositor *ctx= (WstCompositor*)wl_resource_get_user_data(resource);
   WstSurface *surface;
   WstSurfaceInfo *surfaceInfo;
   int clientId;
   
   surface= wstSurfaceCreate(ctx);
   if (!surface) 
   {
      wl_resource_post_no_memory(resource);
      return;
   }

   surface->resource= wl_resource_create(client, &wl_surface_interface,
                                         wl_resource_get_version(resource), id);
   if (!surface->resource)
   {
      wstSurfaceDestroy(surface);
      wl_resource_post_no_memory(resource);
      return;
   }
   surfaceInfo= wstGetSurfaceInfo( ctx, surface->resource );
   if ( !surfaceInfo )
   {
      wstSurfaceDestroy(surface);
      wl_resource_post_no_memory(resource);
      return;
   }
   wl_resource_set_implementation(surface->resource, &surface_interface, surface, wstDestroySurfaceCallback);

   surfaceInfo->surface= surface;
   
   wstUpdateClientInfo( ctx, client, 0 );
   ctx->clientInfoMap[client]->surface= surface;
      
   DEBUG("wstICompositorCreateSurface: client %p resource %p id %d : surface resource %p", client, resource, id, surface->resource );
   
   if ( ctx->simpleShell )
   {
      WstSimpleShellNotifySurfaceCreated( ctx->simpleShell, client, surface->surfaceId );
   }
}

static void wstICompositorCreateRegion(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   WstCompositor *ctx= (WstCompositor*)wl_resource_get_user_data(resource);
   WstRegion *region;

   WARNING("wstICompositorCreateRegion: wl_region is currently a stub impl");
   
   region= wstRegionCreate( ctx );
   if ( !region )
   {
      wl_resource_post_no_memory(resource);
      return;
   }

   region->resource= wl_resource_create(client, &wl_region_interface,
                                        wl_resource_get_version(resource), id);
   if (!region->resource)
   {
      wstRegionDestroy(region);
      wl_resource_post_no_memory(resource);
      return;
   }

   wl_resource_set_implementation(region->resource, &region_interface, region, wstDestroyRegionCallback);
}

static void wstDestroySurfaceCallback(struct wl_resource *resource)
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);

   assert(surface);

   WstCompositor *ctx= surface->compositor;

   if ( ctx->simpleShell )
   {
      WstSimpleShellNotifySurfaceDestroyed( ctx->simpleShell, wl_resource_get_client(resource), surface->surfaceId );
   }

   std::map<struct wl_resource*,WstSurfaceInfo*>::iterator it= ctx->surfaceInfoMap.find( resource );
   if ( it != ctx->surfaceInfoMap.end() )
   {
      WstSurfaceInfo *surfaceInfo= it->second;
      DEBUG("wstDestroySurfaceCallback resource %p free surfaceInfo", resource );
      ctx->surfaceInfoMap.erase( it );
      free( surfaceInfo );
   }

   surface->resource= NULL;
   wstSurfaceDestroy(surface);
}

static WstSurface* wstSurfaceCreate( WstCompositor *ctx)
{
   WstSurface *surface= 0;
   
   surface= (WstSurface*)calloc( 1, sizeof(WstSurface) );
   if ( surface )
   {
      surface->compositor= ctx;
      surface->refCount= 1;
      
      surface->surfaceId= ctx->nextSurfaceId++;
      ctx->surfaceMap.insert( std::pair<int32_t,WstSurface*>( surface->surfaceId, surface ) );
      
      surface->renderer= ctx->renderer;
      
      wl_list_init(&surface->frameCallbackList);
      
      surface->surface= WstRendererSurfaceCreate( ctx->renderer );
      if ( !surface->surface )
      {
         free( surface );
         surface= 0;
      }

      WstRendererSurfaceGetZOrder( ctx->renderer, surface->surface, &surface->zorder );
      wstSurfaceInsertSurface( ctx, surface );
   }
   
   return surface;
}

static void wstSurfaceDestroy( WstSurface *surface )
{
   WstCompositor *ctx= surface->compositor;
   WstSurfaceFrameCallback *fcb;
   
   DEBUG("wstSurfaceDestroy: surface %p refCount %d", surface, surface->refCount );
   
   if (--surface->refCount > 0)
      return;

   // Remove from pointer focus
   if ( ctx->seat->pointer &&
        (ctx->seat->pointer->focus == surface) )
   {
      ctx->seat->pointer->focus= 0;
      wstPointerSetPointer( ctx->seat->pointer, 0 );
   }
   
   // Remove as pointer surface
   if ( ctx->seat->pointer &&
        (ctx->seat->pointer->pointerSurface == surface) )
   {
      wstPointerSetPointer( ctx->seat->pointer, 0 );
   }     

   // Remove from surface list
   for ( std::vector<WstSurface*>::iterator it= ctx->surfaces.begin(); 
         it != ctx->surfaces.end();
         ++it )
   {
      if ( surface == (*it) )
      {
         ctx->surfaces.erase(it);
         break;
      }
   }

   // Remove from surface map
   for( std::map<int32_t, WstSurface*>::iterator it= ctx->surfaceMap.begin(); it != ctx->surfaceMap.end(); ++it )
   {
      if ( it->second == surface )
      {
         ctx->surfaceMap.erase(it);
         break;
      }
   }

   // Cleanup renderer surface
   if ( surface->renderer )
   {
      if ( surface->surface )
      {
         WstRendererSurfaceDestroy( surface->renderer, surface->surface );
         surface->surface= 0;
      }
   }
   
   // Cleanup shell surfaces
   for ( std::vector<WstShellSurface*>::iterator it= surface->shellSurface.begin(); 
         it != surface->shellSurface.end();
         ++it )
   {
      WstShellSurface *shellSurface= (*it);
      
      if ( shellSurface->resource )
      {
         shellSurface->surface= 0;
         wl_resource_destroy( shellSurface->resource );
      }
   }

   // Cleanup client info for this surface
   for( std::map<struct wl_client*,WstClientInfo*>::iterator it= ctx->clientInfoMap.begin(); it != ctx->clientInfoMap.end(); ++it )
   {
      WstClientInfo *clientInfo= (WstClientInfo*)it->second;
      if ( clientInfo->surface == surface )
      {
         DEBUG("wstDestroySurface: free clientInfo for surface %p", surface );
         ctx->clientInfoMap.erase( it );
         free( clientInfo );
         break;
      }
   }
   
   // Cleanup any pending frame callbacks for this surface
   while( !wl_list_empty( &surface->frameCallbackList ) )
   {
      fcb= wl_container_of( surface->frameCallbackList.next, fcb, link);
      wl_list_remove( surface->frameCallbackList.next );
      wl_resource_destroy(fcb->resource);
      free(fcb);      
   }

   assert(surface->resource == NULL);
   
   free(surface);
   
   // Update composited output to reflect removeal of surface
   wstCompositorScheduleRepaint( ctx );
}

static bool wstSurfaceSetRole( WstSurface *surface, const char *roleName, 
                               struct wl_resource *errorResource, uint32_t errorCode )
{
   bool result= false;
   int lenCur, lenNew;
      
   lenCur= (surface->roleName ? strlen(surface->roleName) : 0 );
   lenNew= strlen(roleName);
   
   if ( !surface->roleName ||
        (surface->roleName == roleName) ||
        ((lenCur == lenNew) && !strncmp( surface->roleName, roleName, lenCur )) )
   {
      surface->roleName= roleName;
      result= true;
   }
   else
   {
      wl_resource_post_error( errorResource,
                              errorCode,
                              "Cannot assign wl_surface@%d role %s: already has role %s",
                              wl_resource_get_id(surface->resource),
                              roleName,
                              surface->roleName );
   }
   
   return result;
}

static void wstSurfaceInsertSurface( WstCompositor *ctx, WstSurface *surface )
{
   // Remove from surface list
   for ( std::vector<WstSurface*>::iterator it= ctx->surfaces.begin(); 
         it != ctx->surfaces.end();
         ++it )
   {
      if ( (*it) == surface )
      {
         ctx->surfaces.erase(it);
         break;   
      }
   }   

   // Re-insert in surface list based on z-order
   std::vector<WstSurface*>::iterator it= ctx->surfaces.begin();
   while ( it != ctx->surfaces.end() )
   {
      if ( surface->zorder < (*it)->zorder )
      {
         break;
      }
      ++it;
   }
   ctx->surfaces.insert(it,surface);
}

static WstSurface* wstGetSurfaceFromSurfaceId( WstCompositor *ctx, int32_t surfaceId )
{
   WstSurface *surface= 0;
   
   std::map<int32_t,WstSurface*>::iterator it= ctx->surfaceMap.find( surfaceId );
   if ( it != ctx->surfaceMap.end() )
   {
      surface= it->second;
   }
   
   return surface;
}

static WstSurfaceInfo* wstGetSurfaceInfo( WstCompositor *ctx, struct wl_resource *resource )
{
   WstSurfaceInfo *surfaceInfo= 0;

   std::map<struct wl_resource*,WstSurfaceInfo*>::iterator it= ctx->surfaceInfoMap.find( resource );
   if ( it != ctx->surfaceInfoMap.end() )
   {
      surfaceInfo= it->second;
      DEBUG("wstGetSurfaceInfo: found surfaceInfo for resource %p", resource );
   }
   else
   {
      surfaceInfo= (WstSurfaceInfo*)calloc( 1, sizeof(WstSurfaceInfo) );
      if ( surfaceInfo )
      {
         DEBUG("wstGetSurfaceInfo: create surfaceInfo for resource %p", resource );
         ctx->surfaceInfoMap.insert( std::pair<struct wl_resource*,WstSurfaceInfo*>(resource, surfaceInfo) );
      }
   }
   
   return surfaceInfo;
}

static void wstUpdateClientInfo( WstCompositor *ctx, struct wl_client *client, struct wl_resource *resource )
{
   WstClientInfo *clientInfo= 0;
   
   std::map<struct wl_client*,WstClientInfo*>::iterator it= ctx->clientInfoMap.find( client );
   if ( it != ctx->clientInfoMap.end() )
   {
      clientInfo= it->second;
      DEBUG("wstUpdateClientInfo: found clientInfo for client %p", client );
   }
   else
   {
      clientInfo= (WstClientInfo*)calloc( 1, sizeof(WstClientInfo) );
      if ( clientInfo )
      {
         DEBUG("wstUpdateClientInfo: create clientInfo for client %p", client );
         ctx->clientInfoMap.insert( std::pair<struct wl_client*,WstClientInfo*>(client, clientInfo) );
      }
   }   
   if ( clientInfo )
   {
      if ( resource )
      {
         DEBUG("wstUpdateClientInfo: client %p update resource %p", client, resource );
         clientInfo->sbResource= resource;
      }
   }
}

static void wstISurfaceDestroy(struct wl_client *client, struct wl_resource *resource)
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);
   WstCompositor *ctx= surface->compositor;

   DEBUG("wstISurfaceDestroy resource %p", resource );
   wl_resource_destroy(resource);

   std::map<struct wl_client*,WstClientInfo*>::iterator it= ctx->clientInfoMap.find( client );
   if ( it != ctx->clientInfoMap.end() )
   {
      WstClientInfo *clientInfo= it->second;
      DEBUG("destroy_surface client %p free clientInfo", client );
      ctx->clientInfoMap.erase( it );
      free( clientInfo );
   }
}

static void wstISurfaceAttach(struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *buffer_resource, int32_t sx, int32_t sy)
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);

   if ( buffer_resource )
   {
      surface->attachedBufferResource= buffer_resource;
      if ( wl_shm_buffer_get( buffer_resource ) )
      {
         surface->attachedBufferType= WstBufferType_shm;
      }
      #ifdef ENABLE_SBPROTOCOL
      else if ( WstSBBufferGet( surface->compositor->sb, buffer_resource ) )
      {
         surface->attachedBufferType= WstBufferType_sb;
      }
      #endif
      else
      {
         surface->attachedBufferType= WstBufferType_null;
         ERROR("wstISurfaceAttach: buffer type: unknown");
      }
   }
}

static void wstISurfaceDamage(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t x, int32_t y, int32_t width, int32_t height)
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   WESTEROS_UNUSED(width);
   WESTEROS_UNUSED(height);

   // Ignored for now - assume damage includes entire surface
}

static void wstISurfaceFrame(struct wl_client *client,
                             struct wl_resource *resource, uint32_t callback)
{
   WstSurfaceFrameCallback *fcb= 0;
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);
   
   fcb= (WstSurfaceFrameCallback*)malloc( sizeof(WstSurfaceFrameCallback) );
   if ( !fcb )
   {
      wl_resource_post_no_memory(resource);
      return;
   }
   
   fcb->resource= wl_resource_create( client, &wl_callback_interface, 1, callback );
   if ( !fcb->resource )
   {
      wl_resource_post_no_memory(resource);
      free(fcb);
      return;
   }
   
   wl_list_insert( surface->frameCallbackList.prev, &fcb->link );
}

static void wstISurfaceSetOpaqueRegion(struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *region_resource)
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(region_resource);
   TRACE("wstISurfaceSetOpaqueRegion: not supported");
}

static void wstISurfaceSetInputRegion(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *region_resource)
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(region_resource);
   WARNING("wstISurfaceSetInputRegion: not supported");
}

static void wstISurfaceCommit(struct wl_client *client, struct wl_resource *resource)
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);

   if ( surface->attachedBufferResource )
   {
      switch( surface->attachedBufferType )
      {
         case WstBufferType_shm:
            {
               struct wl_shm_buffer *shmBuffer;
               
               shmBuffer= wl_shm_buffer_get( surface->attachedBufferResource );
               if ( shmBuffer )
               {
                  int width, height, stride, format;
                  void *data;
                  
                  width= wl_shm_buffer_get_width(shmBuffer);
                  height= wl_shm_buffer_get_height(shmBuffer);
                  stride= wl_shm_buffer_get_stride(shmBuffer);
                  switch( wl_shm_buffer_get_format(shmBuffer) )
                  {
                     case WL_SHM_FORMAT_ARGB8888:
                        format= WstRenderer_format_ARGB8888;
                        break;
                     case WL_SHM_FORMAT_XRGB8888:
                        format= WstRenderer_format_XRGB8888;
                        break;
                     case WL_SHM_FORMAT_BGRA8888:
                        format= WstRenderer_format_BGRA8888;
                        break;
                     case WL_SHM_FORMAT_BGRX8888:
                        format= WstRenderer_format_BGRX8888;
                        break;
                     case WL_SHM_FORMAT_RGB565:
                        format= WstRenderer_format_RGB565;
                        break;
                     case WL_SHM_FORMAT_ARGB4444:
                        format= WstRenderer_format_ARGB4444;
                        break;
                     default:
                        WARNING("unsupported pixel format: %d", wl_shm_buffer_get_format(shmBuffer));
                        format= WstRenderer_format_unknown;
                        break;
                  }

                  if ( format != WstRenderer_format_unknown )
                  {
                     wl_shm_buffer_begin_access(shmBuffer);
                     data= wl_shm_buffer_get_data(shmBuffer);
                     
                     pthread_mutex_lock( &surface->compositor->mutex );
                     WstRendererSurfaceCommitMemory( surface->renderer,
                                                     surface->surface,
                                                     data,
                                                     width,
                                                     height,
                                                     format,
                                                     stride );      
                     pthread_mutex_unlock( &surface->compositor->mutex );
                     
                     wl_shm_buffer_end_access(shmBuffer);

                     wstCompositorScheduleRepaint( surface->compositor );
                  }
               }
            }
            break;
         #ifdef ENABLE_SBPROTOCOL
         case WstBufferType_sb:
            {
               struct wl_sb_buffer *sbBuffer;
               void *deviceBuffer;
               
               sbBuffer= WstSBBufferGet( surface->compositor->sb, surface->attachedBufferResource );
               if ( sbBuffer )
               {
                  deviceBuffer= WstSBBufferGetBuffer( sbBuffer );
                  if ( deviceBuffer )
                  {
                     pthread_mutex_lock( &surface->compositor->mutex );
                     WstRendererSurfaceCommit( surface->renderer, surface->surface, deviceBuffer );
                     pthread_mutex_unlock( &surface->compositor->mutex );
                     
                     wstCompositorScheduleRepaint( surface->compositor );
                  }
               }
            }
            break;
         #endif
         default:
            WARNING("wstISurfaceCommit: unsupported buffer type");
            break;
      }

      wl_buffer_send_release( surface->attachedBufferResource );
      surface->attachedBufferResource= 0;
      surface->attachedBufferType= WstBufferType_null;
   }
}

static void wstISurfaceSetBufferTransform(struct wl_client *client,
                                          struct wl_resource *resource, int transform)
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(transform);
   WARNING("wstISurfaceSetBufferTransform: not supported");
}

static void wstISurfaceSetBufferScale(struct wl_client *client,
                                      struct wl_resource *resource,
                                      int32_t scale)
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(scale);
   WARNING("wstISurfaceSetBufferScale: not supported");
}

static WstRegion *wstRegionCreate( WstCompositor *ctx )
{
   WstRegion *region= 0;

   region= (WstRegion*)calloc( 1, sizeof(WstRegion) );
   if ( region )
   {
      region->compositor= ctx;
   }
   
   return region;
}

static void wstRegionDestroy( WstRegion *region )
{
   assert(region->resource == NULL);
   free( region );
}

static void wstDestroyRegionCallback(struct wl_resource *resource)
{
   WstRegion *region= (WstRegion*)wl_resource_get_user_data(resource);

   assert(region);

   region->resource= NULL;
   wstRegionDestroy(region);   
}

static void wstIRegionDestroy( struct wl_client *client, struct wl_resource *resource )
{
   wl_resource_destroy( resource) ;
}

static void wstIRegionAdd( struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y, int32_t width, int32_t height )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   WESTEROS_UNUSED(width);
   WESTEROS_UNUSED(height);
}
                           
static void wstIRegionSubtract( struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t x, int32_t y, int32_t width, int32_t height )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   WESTEROS_UNUSED(width);
   WESTEROS_UNUSED(height);
}

static void wstShellBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstCompositor *ctx= (WstCompositor*)data;
   struct wl_resource *resource;

   DEBUG("wstShellBind: client %p data %p version %d id %d", client, data, version, id );
   
   resource= wl_resource_create(client, 
                                &wl_shell_interface,
                                MIN(version, 1), 
                                id);
   if (!resource) 
   {
      wl_client_post_no_memory(client);
      return;
   }

   wl_resource_set_implementation(resource, &shell_interface, ctx, NULL);
}

static void wstIShellGetShellSurface(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     struct wl_resource *surface_resource )
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(surface_resource);
   WstShellSurface *shellSurface= 0;
   
   if ( !wstSurfaceSetRole( surface, "wl_shell_surface", 
                            resource, WL_DISPLAY_ERROR_INVALID_OBJECT ) )
   {
      return;
   }

   shellSurface= (WstShellSurface*)calloc(1,sizeof(WstShellSurface));
   if ( !shellSurface )
   {
      wl_resource_post_no_memory(surface_resource);
      return;
   }
   
   shellSurface->resource= wl_resource_create(client,
                                              &wl_shell_surface_interface, 1, id);
   if (!shellSurface->resource) 
   {
      free(shellSurface);
      wl_client_post_no_memory(client);
      return;
   }
   
   wl_resource_set_implementation(shellSurface->resource,
                                  &shell_surface_interface,
                                  shellSurface, wstDestroyShellSurfaceCallback );
   
   shellSurface->surface= surface;
   surface->shellSurface.push_back(shellSurface);   
}
                                     
static void wstDestroyShellSurfaceCallback(struct wl_resource *resource)
{
   WstShellSurface *shellSurface= (WstShellSurface*)wl_resource_get_user_data(resource);

   shellSurface->resource= NULL;
   wstShellSurfaceDestroy(shellSurface);
}

static void wstShellSurfaceDestroy( WstShellSurface *shellSurface )
{
   if ( shellSurface->surface )
   {
      // Remove destroyed shell surface from surface's list
      WstSurface *surface= shellSurface->surface;
      for ( std::vector<WstShellSurface*>::iterator it= surface->shellSurface.begin(); 
            it != surface->shellSurface.end();
            ++it )
      {
         if ( (*it) == shellSurface )
         {
            surface->shellSurface.erase(it);
            break;   
         }
      }   
   }
   
   if ( shellSurface->title )
   {
      free((void*)shellSurface->title);
      shellSurface->title= 0;
   }

   if ( shellSurface->className )
   {
      free((void*)shellSurface->className);
      shellSurface->className= 0;
   }
   
   assert(shellSurface->resource == NULL);
   
   free( shellSurface );
}

static void wstIShellSurfacePong(struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t serial )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(serial);
}
                                 
static void wstIShellSurfaceMove(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *seat_resource,
                                 uint32_t serial )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seat_resource);
   WESTEROS_UNUSED(serial);
}
                                 
static void wstIShellSurfaceResize(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *seat_resource,
                                   uint32_t serial,
                                   uint32_t edges )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seat_resource);
   WESTEROS_UNUSED(serial);
   WESTEROS_UNUSED(edges);
}                                   

static void wstIShellSurfaceSetTopLevel(struct wl_client *client, 
                                        struct wl_resource *resource)
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
}

static void wstIShellSurfaceSetTransient(struct wl_client *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *parent_resource,
                                         int x, int y, uint32_t flags )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(parent_resource);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   WESTEROS_UNUSED(flags);
}
                                         
static void wstIShellSurfaceSetFullscreen(struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t method,
                                          uint32_t framerate,
                                          struct wl_resource *output_resource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(method);
   WESTEROS_UNUSED(framerate);
   WESTEROS_UNUSED(output_resource);
}
                                          
static void wstIShellSurfaceSetPopup(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *seat_resource,
                                     uint32_t serial,
                                     struct wl_resource *parent_resource,
                                     int x, int y, uint32_t flags )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seat_resource);
   WESTEROS_UNUSED(serial);
   WESTEROS_UNUSED(parent_resource);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   WESTEROS_UNUSED(flags);
}
                                     
static void wstIShellSurfaceSetMaximized(struct wl_client *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *output_resource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(output_resource);
}
                                         
static void wstIShellSurfaceSetTitle(struct wl_client *client,
                                     struct wl_resource *resource,
                                     const char *title )
{
   WstShellSurface *shellSurface= (WstShellSurface*)wl_resource_get_user_data(resource);
   
   if ( shellSurface->title )
   {
      free((void*)shellSurface->title);
      shellSurface->title= 0;
   }
   
   if ( title )
   {
      shellSurface->title= strdup(title);
   }
}
                                     
static void wstIShellSurfaceSetClass(struct wl_client *client,
                                     struct wl_resource *resource,
                                     const char *className )
{
   WstShellSurface *shellSurface= (WstShellSurface*)wl_resource_get_user_data(resource);
   
   if ( shellSurface->className )
   {
      free((void*)shellSurface->className);
      shellSurface->className= 0;
   }
   
   if ( className )
   {
      shellSurface->className= strdup(className);
   }
}

static void wstXdgShellBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstCompositor *ctx= (WstCompositor*)data;
   struct wl_resource *resource;

   WARNING("wstXdgShellBind: xdg-shell is currently a stub impl");
   
   resource= wl_resource_create(client, 
                                &xdg_shell_interface,
                                MIN(version, 1), 
                                id);
   if (!resource) 
   {
      wl_client_post_no_memory(client);
      return;
   }

   wl_resource_set_implementation(resource, &xdg_shell_interface_impl, ctx, NULL);
}

static void wstIXdgDestroy( struct wl_client *client, struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);
   wl_resource_destroy( resource );
}

static void wstIXdgUseUnstableVersion( struct wl_client *client, struct wl_resource *resource, int32_t version )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(version);
   
   // Xdg Shell is just stubbed out to make Weston client apps happy
}

static void wstIXdgGetXdgSurface( struct wl_client *client, 
                                  struct wl_resource *resource,
                                  uint32_t id,
                                  struct wl_resource *surface_resource )
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(surface_resource);
   WstShellSurface *shellSurface= 0;

   if ( !wstSurfaceSetRole( surface, "xdg_surface", 
                            resource, WL_DISPLAY_ERROR_INVALID_OBJECT ) )
   {
      return;
   }
   
   WARNING("wstIXdgGetXdgSurface: xdg_surface is currently a stub impl");
   shellSurface= (WstShellSurface*)calloc(1,sizeof(WstShellSurface));
   if ( !shellSurface )
   {
      wl_resource_post_no_memory(surface_resource);
      return;
   }
   
   shellSurface->resource= wl_resource_create(client,
                                              &xdg_surface_interface, 1, id);
   if (!shellSurface->resource) 
   {
      free(shellSurface);
      wl_client_post_no_memory(client);
      return;
   }
   
   wl_resource_set_implementation(shellSurface->resource,
                                  &xdg_surface_interface_impl,
                                  shellSurface, wstDestroyShellSurfaceCallback );
   
   shellSurface->surface= surface;
   surface->shellSurface.push_back(shellSurface);   
}

static void  wstIXdgGetXdgPopup( struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t id,
                                 struct wl_resource *surface_resource,
                                 struct wl_resource *parent_resource,
                                 struct wl_resource *seat_resource,
                                 uint32_t serial,
                                 int32_t x,
                                 int32_t y
                                 #if defined ( USE_XDG_VERSION4 )
                                 ,
                                 uint32_t flags 
                                 #endif
                                )
{
   WESTEROS_UNUSED(parent_resource);
   WESTEROS_UNUSED(seat_resource);
   WESTEROS_UNUSED(serial);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   #if defined ( USE_XDG_VERSION4 )
   WESTEROS_UNUSED(flags);
   #endif

   WARNING("Xdg Popup not supported");
   
   wstIXdgGetXdgSurface( client, resource, id, surface_resource );
}

static void wstIXdgPong( struct wl_client *client,
                         struct wl_resource *resource,
                         uint32_t serial )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(serial);
}                         

static void wstIXdgShellSurfaceDestroy( struct wl_client *client,
                                        struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);
   wl_resource_destroy( resource );
}
      
static void wstIXdgShellSurfaceSetParent( struct wl_client *client,
                                          struct wl_resource *resource,
                                          struct wl_resource *parent_resource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(parent_resource);
}
                                          
static void wstIXdgShellSurfaceSetTitle( struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *title )
{
   WESTEROS_UNUSED(client);
   WstShellSurface *shellSurface= (WstShellSurface*)wl_resource_get_user_data(resource);

   if ( shellSurface->title )
   {
      free((void*)shellSurface->title);
      shellSurface->title= 0;
   }
   
   if ( title )
   {
      shellSurface->title= strdup(title);
   }
}
                                         
static void wstIXdgShellSurfaceSetAppId( struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *app_id )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(app_id);
}                                         
                                         
static void wstIXdgShellSurfaceShowWindowMenu( struct wl_client *client,
                                               struct wl_resource *resource,
                                               struct wl_resource *seat_resource,
                                               uint32_t serial,
                                               int32_t x,
                                               int32_t y )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seat_resource);
   WESTEROS_UNUSED(serial);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
}
                                               
static void wstIXdgShellSurfaceMove( struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *seat_resource,
                                     uint32_t serial )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seat_resource);
   WESTEROS_UNUSED(serial);
}
                                     
static void wstIXdgShellSurfaceResize( struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *seat_resource,
                                       uint32_t serial,
                                       uint32_t edges )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seat_resource);
   WESTEROS_UNUSED(serial);
   WESTEROS_UNUSED(edges);
}

static void wstIXdgShellSurfaceAckConfigure( struct wl_client *client,
                                             struct wl_resource *resource,
                                             uint32_t serial )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(serial);
}

static void wstIXdgShellSurfaceSetWindowGeometry( struct wl_client *client,
                                                  struct wl_resource *resource,
                                                  int32_t x,
                                                  int32_t y,
                                                  int32_t width,
                                                  int32_t height )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   WESTEROS_UNUSED(width);
   WESTEROS_UNUSED(height);
}
                                                  
static void wstIXdgShellSurfaceSetMaximized( struct wl_client *client,
                                             struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
}
                                             
static void wstIXdgShellSurfaceUnSetMaximized( struct wl_client *client,
                                               struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
}
                                               
static void wstIXdgShellSurfaceSetFullscreen( struct wl_client *client,
                                              struct wl_resource *resource,
                                              struct wl_resource *output_resource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(output_resource);
}
                                              
static void wstIXdgShellSurfaceUnSetFullscreen( struct wl_client *client,
                                                struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
}
                                                
static void wstIXdgShellSurfaceSetMinimized( struct wl_client *client,
                                             struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
}                                             

static void wstDefaultNestedConnectionEnded( void *userData )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   if ( ctx )
   {
      if ( ctx->display )
      {
         wl_display_terminate(ctx->display);
      }
      if ( ctx->terminatedCB )
      {
         ctx->terminatedCB( ctx, ctx->terminatedUserData );
      }
   }
}

static void wstDefaultNestedKeyboardHandleKeyMap( void *userData, uint32_t format, int fd, uint32_t size )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   if ( ctx )
   {
      ctx->xkbKeymapFormat= format;
      ctx->xkbKeymapFd= fd;
      ctx->xkbKeymapSize= size;

      if ( ctx->keyboardNestedListener )
      {
         ctx->keyboardNestedListener->keyboardHandleKeyMap( ctx->keyboardNestedListenerUserData,
                                                            format, fd, size );
      }
   }   
} 

static void wstDefaultNestedKeyboardHandleEnter( void *userData, struct wl_array *keys )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   if ( ctx )
   {
      if ( ctx->keyboardNestedListener )
      {
         ctx->keyboardNestedListener->keyboardHandleEnter( ctx->keyboardNestedListenerUserData,
                                                           keys );
      }
      else
      {
         if ( ctx->seat )
         {
            wl_array_copy( &ctx->seat->keyboard->keys, keys );
         }
      }
   }   
}

static void wstDefaultNestedKeyboardHandleLeave( void *userData )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   if ( ctx )
   {
      if ( ctx->keyboardNestedListener )
      {
         ctx->keyboardNestedListener->keyboardHandleLeave( ctx->keyboardNestedListenerUserData );
      }
      else
      {
         // Nothing to do.
         // Keyboard focus is controlled by the master compositor.  All clients
         // will remain in a keyboard entered state but will only get keys
         // when the master compositor passes them to this client.
      }
   }
}

static void wstDefaultNestedKeyboardHandleKey( void *userData, uint32_t time, uint32_t key, uint32_t state )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   if ( ctx )
   {
      if ( ctx->keyboardNestedListener )
      {
         ctx->keyboardNestedListener->keyboardHandleKey( ctx->keyboardNestedListenerUserData,
                                                         time, key, state );
      }
      else
      {
         if ( ctx->seat )
         {
            WstKeyboard *keyboard= ctx->seat->keyboard;
            
            if ( keyboard )
            {
               uint32_t serial;
               struct wl_resource *resource;
               
               serial= wl_display_next_serial( ctx->display );
               wl_resource_for_each( resource, &keyboard->resourceList )
               {
                  wl_keyboard_send_key( resource, 
                                        serial,
                                        time,
                                        key,
                                        state );
               }   
            }
         }
      }
   }   
}

static void wstDefaultNestedKeyboardHandleModifiers( void *userData, uint32_t mods_depressed, uint32_t mods_latched, 
                                                     uint32_t mods_locked, uint32_t group )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   
   if ( ctx )
   {
      if ( ctx->keyboardNestedListener )
      {
         ctx->keyboardNestedListener->keyboardHandleModifiers( ctx->keyboardNestedListenerUserData,
                                                               mods_depressed, mods_latched, mods_locked, group );
      }
      else
      {
         if ( ctx->seat )
         {
            WstKeyboard *keyboard= ctx->seat->keyboard;
            
            if ( keyboard )
            {
               uint32_t serial;
               struct wl_resource *resource;
               
               serial= wl_display_next_serial( ctx->display );
               wl_resource_for_each( resource, &keyboard->resourceList )
               {
                  wl_keyboard_send_modifiers( resource,
                                              serial,
                                              mods_depressed, // mod depressed
                                              mods_latched,   // mod latched
                                              mods_locked,    // mod locked
                                              group           // mod group
                                            );
               }   
            }
         }
      }
   }
}

static void wstDefaultNestedKeyboardHandleRepeatInfo( void *userData, int32_t rate, int32_t delay )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   
   if ( ctx )
   {
      if ( ctx->keyboardNestedListener )
      {
         ctx->keyboardNestedListener->keyboardHandleRepeatInfo( ctx->keyboardNestedListenerUserData,
                                                                rate, delay );
      }
      else
      {
         if ( ctx->seat )
         {
            ctx->seat->keyRepeatDelay;
            ctx->seat->keyRepeatRate;
         }
      }
   }                       
}

static void wstDefaultNestedPointerHandleEnter( void *userData, wl_fixed_t sx, wl_fixed_t sy )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   
   if ( ctx )
   {
      if ( ctx->pointerNestedListener )
      {
         ctx->pointerNestedListener->pointerHandleEnter( ctx->pointerNestedListenerUserData,
                                                         sx, sy );
      }
      else
      {
         if ( ctx->seat )
         {
            WstPointer *pointer= ctx->seat->pointer;
            if (  pointer )
            {
               int x, y;
               
               x= wl_fixed_to_int( sx );
               y= wl_fixed_to_int( sy );
               
               pointer->entered= true;
               
               pointer->pointerX= x;
               pointer->pointerY= y;
               
               wstPointerCheckFocus( pointer, x, y );
            }
         }
      }
   }
}

static void wstDefaultNestedPointerHandleLeave( void *userData )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   
   if ( ctx )
   {
      if ( ctx->pointerNestedListener )
      {
         ctx->pointerNestedListener->pointerHandleLeave( ctx->pointerNestedListenerUserData );
      }
      else
      {
         if ( ctx->seat )
         {
            WstPointer *pointer= ctx->seat->pointer;
            if (  pointer )
            {
               wstProcessPointerLeave( pointer );
            }
         }
      }
   }
}

static void wstDefaultNestedPointerHandleMotion( void *userData, uint32_t time, wl_fixed_t sx, wl_fixed_t sy )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   
   if ( ctx )
   {
      if ( ctx->pointerNestedListener )
      {
         ctx->pointerNestedListener->pointerHandleMotion( ctx->pointerNestedListenerUserData,
                                                          time, sx, sy );
      }
      else
      {
         if ( ctx->seat )
         {
            WstPointer *pointer= ctx->seat->pointer;
            if (  pointer )
            {
               int x, y;
               
               x= wl_fixed_to_int( sx );
               y= wl_fixed_to_int( sy );

               wstProcessPointerMoveEvent( pointer, x, y );
            }
         }
      }
   }
}

static void wstDefaultNestedPointerHandleButton( void *userData, uint32_t time, uint32_t button, uint32_t state )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   
   if ( ctx )
   {
      if ( ctx->pointerNestedListener )
      {
         ctx->pointerNestedListener->pointerHandleButton( ctx->pointerNestedListenerUserData,
                                                          time, button, state );
      }
      else
      {
         if ( ctx->seat )
         {
            WstPointer *pointer= ctx->seat->pointer;
            if (  pointer )
            {
               wstProcessPointerButtonEvent( pointer, button, state );
            }
         }
      }
   }
}

static void wstDefaultNestedPointerHandleAxis( void *userData, uint32_t time, uint32_t axis, wl_fixed_t value )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   if ( ctx )
   {
      if ( ctx->pointerNestedListener )
      {
         ctx->pointerNestedListener->pointerHandleAxis( ctx->pointerNestedListenerUserData,
                                                        time, axis, value );
      }
      else
      {
         // Not supported
      }
   }
}

static void wstSetDefaultNestedListener( WstCompositor *ctx )
{
   ctx->nestedListenerUserData= ctx;
   ctx->nestedListener.connectionEnded= wstDefaultNestedConnectionEnded;
   ctx->nestedListener.keyboardHandleKeyMap= wstDefaultNestedKeyboardHandleKeyMap;
   ctx->nestedListener.keyboardHandleEnter= wstDefaultNestedKeyboardHandleEnter;
   ctx->nestedListener.keyboardHandleLeave= wstDefaultNestedKeyboardHandleLeave;
   ctx->nestedListener.keyboardHandleKey= wstDefaultNestedKeyboardHandleKey;
   ctx->nestedListener.keyboardHandleModifiers= wstDefaultNestedKeyboardHandleModifiers;
   ctx->nestedListener.keyboardHandleRepeatInfo= wstDefaultNestedKeyboardHandleRepeatInfo;
   ctx->nestedListener.pointerHandleEnter= wstDefaultNestedPointerHandleEnter;
   ctx->nestedListener.pointerHandleLeave= wstDefaultNestedPointerHandleLeave;
   ctx->nestedListener.pointerHandleMotion= wstDefaultNestedPointerHandleMotion;
   ctx->nestedListener.pointerHandleButton= wstDefaultNestedPointerHandleButton;
   ctx->nestedListener.pointerHandleAxis= wstDefaultNestedPointerHandleAxis;   
}

static bool wstSeatInit( WstCompositor *ctx )
{
   bool result= false;
   WstSeat *seat= 0;
   WstKeyboard *keyboard= 0;
   WstPointer *pointer= 0;

   // Create seat
   ctx->seat= (WstSeat*)calloc( 1, sizeof(WstSeat) );
   if ( !ctx->seat )
   {
      ERROR("no memory to allocate seat");
      goto exit;
   }
   seat= ctx->seat;

   wl_list_init( &seat->resourceList );
   seat->compositor= ctx;
   seat->seatName= strdup("primary-seat");   
   seat->keyRepeatDelay= DEFAULT_KEY_REPEAT_DELAY;
   seat->keyRepeatRate= DEFAULT_KEY_REPEAT_RATE;
   
   // Create keyboard
   seat->keyboard= (WstKeyboard*)calloc( 1, sizeof(WstKeyboard) );
   if ( !seat->keyboard )
   {
      ERROR("no memory to allocate keyboard");
      goto exit;
   }
   
   keyboard= seat->keyboard;
   keyboard->seat= seat;
   wl_list_init( &keyboard->resourceList );
   wl_array_init( &keyboard->keys );
   
   if ( !ctx->isNested )
   {
      keyboard->state= xkb_state_new( ctx->xkbKeymap );
      if ( !keyboard->state )
      {
         ERROR("unable to create key state");
         goto exit;
      }
      
      keyboard->modShift= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_SHIFT );
      keyboard->modAlt= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_ALT );
      keyboard->modCtrl= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CTRL );
      keyboard->modCaps= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CAPS );
   }

   // Create pointer
   seat->pointer= (WstPointer*)calloc( 1, sizeof(WstPointer) );
   if ( !seat->pointer )
   {
      ERROR("no memory to allocate pointer");
      goto exit;
   }
   
   pointer= seat->pointer;
   pointer->seat= seat;
   wl_list_init( &pointer->resourceList );
   wl_list_init( &pointer->focusResourceList );
   
   result= true;
   
exit:
   
   if ( !result )
   {
      wstSeatTerm( ctx );
   }
   
   return result;
}

static void wstSeatTerm( WstCompositor *ctx )
{
   if ( ctx->seat )
   {
      WstSeat *seat= ctx->seat;
      struct wl_resource *resource;

      if ( seat->keyboard )
      {
         WstKeyboard *keyboard= seat->keyboard;
         
         while( !wl_list_empty( &keyboard->resourceList ) )
         {
            resource= wl_container_of( keyboard->resourceList.next, resource, link);
            wl_resource_destroy(resource);
         }
         
         wl_array_release( &keyboard->keys );
         
         if ( keyboard->state )
         {
            xkb_state_unref( keyboard->state );
            keyboard->state= 0;
         }

         free( keyboard );
         seat->keyboard= 0;
      }

      if ( seat->pointer )
      {
         WstPointer *pointer= seat->pointer;
         
         while( !wl_list_empty( &pointer->resourceList ) )
         {
            resource= wl_container_of( pointer->resourceList.next, resource, link);
            wl_resource_destroy(resource);
         }

         free( pointer );
         seat->pointer= 0;
      }

      while( !wl_list_empty( &seat->resourceList ) )
      {
         resource= wl_container_of( seat->resourceList.next, resource, link);
         wl_resource_destroy(resource);
      }
      
      if ( seat->seatName )
      {
         free( (void*)seat->seatName );
         seat->seatName= 0;
      }
      
      free( seat );
      ctx->seat= 0;
   }
}

static void wstResourceUnBindCallback( struct wl_resource *resource )
{
   wl_list_remove( wl_resource_get_link(resource) );
}

static void wstSeatBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstSeat *seat= (WstSeat*)data;
   struct wl_resource *resource;
   int caps= 0;

   DEBUG("wstSeatBind: client %p data %p version %d id %d", client, data, version, id );
   
   resource= wl_resource_create(client, 
                                &wl_seat_interface,
                                MIN(version, 4), 
                                id);
   if (!resource) 
   {
      wl_client_post_no_memory(client);
      return;
   }
   
   wl_list_insert( &seat->resourceList, wl_resource_get_link(resource) );

   wl_resource_set_implementation(resource, &seat_interface, seat, wstResourceUnBindCallback);
   
   if ( seat->keyboard )
   {
      caps |= WL_SEAT_CAPABILITY_KEYBOARD;
   }
   if ( seat->pointer )
   {
      caps |= WL_SEAT_CAPABILITY_POINTER;
   }
   wl_seat_send_capabilities(resource, caps);
   if ( wl_resource_get_version(resource) >= WL_SEAT_NAME_SINCE_VERSION )
   {
      wl_seat_send_name(resource, seat->seatName);
   }
}

static void wstISeatGetPointer( struct wl_client *client, struct wl_resource *resource, uint32_t id )
{
   WstSeat *seat= (WstSeat*)wl_resource_get_user_data(resource);
   WstPointer *pointer= seat->pointer;
   struct wl_resource *resourcePnt= 0;
   
   if ( !pointer )
   {
      return;
   }
   
   resourcePnt= wl_resource_create( client, 
                                    &wl_pointer_interface, 
                                    wl_resource_get_version(resource),
                                    id );
   if ( !resourcePnt )
   {
      wl_client_post_no_memory(client);
      return;
   }
   
   wl_list_insert( &pointer->resourceList, wl_resource_get_link(resourcePnt) );
   
   wl_resource_set_implementation( resourcePnt,
                                   &pointer_interface,
                                   pointer,
                                   wstResourceUnBindCallback );

   if ( pointer->entered )
   {
      wstPointerCheckFocus( pointer, pointer->pointerX, pointer->pointerY );
   }
}

static void wstISeatGetKeyboard( struct wl_client *client, struct wl_resource *resource, uint32_t id )
{
   WstSeat *seat= (WstSeat*)wl_resource_get_user_data(resource);
   WstKeyboard *keyboard= seat->keyboard;
   struct wl_resource *resourceKbd= 0;
   
   if ( !keyboard )
   {
      return;
   }
   
   resourceKbd= wl_resource_create( client, 
                                    &wl_keyboard_interface, 
                                    wl_resource_get_version(resource),
                                    id );
   if ( !resourceKbd )
   {
      wl_client_post_no_memory(client);
      return;
   }
   
   wl_list_insert( &keyboard->resourceList, wl_resource_get_link(resourceKbd) );
   
   wl_resource_set_implementation( resourceKbd,
                                   &keyboard_interface,
                                   keyboard,
                                   wstResourceUnBindCallback );

   if ( wl_resource_get_version(resourceKbd) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION )
   {
      wl_keyboard_send_repeat_info( resourceKbd, seat->keyRepeatRate, seat->keyRepeatDelay );
   }
   
   wl_keyboard_send_keymap( resourceKbd,
                            seat->compositor->xkbKeymapFormat,
                            seat->compositor->xkbKeymapFd,
                            seat->compositor->xkbKeymapSize );

   {
      struct wl_resource *surface= 0;
      
      if ( seat->compositor->clientInfoMap.size() > 0 )
      {
         WstCompositor *compositor= seat->compositor;
         for( std::map<struct wl_client*,WstClientInfo*>::iterator it= compositor->clientInfoMap.begin(); 
              it != compositor->clientInfoMap.end(); ++it )
         {
            if ( it->first == client )
            {
               uint32_t serial;
               
               serial= wl_display_next_serial( compositor->display );
               surface= compositor->clientInfoMap[client]->surface->resource;
               wl_keyboard_send_enter( resourceKbd,
                                       serial,
                                       surface,
                                       &keyboard->keys );

               if ( !compositor->isNested )
               {
                  wstKeyboardSendModifiers( keyboard, resourceKbd );
               }
               
               break;
            }
         }
      }                              
   }                               
}

static void wstISeatGetTouch( struct wl_client *client, struct wl_resource *resource, uint32_t id )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(id);
   
   // Not supporting for now.  We don't advertise touch capability so no client should
   // make this request, but if they do an error will be reported
   wl_resource_post_error( resource, 
                           WL_DISPLAY_ERROR_INVALID_METHOD,
                           "get_touch is not supported" );
}

static void wstIKeyboardRelease( struct wl_client *client, struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);
   
   wl_resource_destroy(resource);
}

static void wstIPointerSetCursor( struct wl_client *client, 
                                  struct wl_resource *resource,
                                  uint32_t serial,
                                  struct wl_resource *surface_resource,
                                  int32_t hotspot_x,
                                  int32_t hotspot_y )
{   
   WESTEROS_UNUSED(serial);
   
   WstPointer *pointer= (WstPointer*)wl_resource_get_user_data(resource);
   WstCompositor *compositor= pointer->seat->compositor;
   WstSurface *surface= 0;
   bool hidePointer= false;
   
   if ( surface_resource )
   {
      if ( pointer->focus &&
           (wl_resource_get_client( pointer->focus->resource ) != client) )
      {
         return;
      }
      surface= (WstSurface*)wl_resource_get_user_data(surface_resource);
   }
   
   if ( surface )
   {
      if ( !wstSurfaceSetRole( surface, "wl_pointer-cursor", 
                               resource, WL_DISPLAY_ERROR_INVALID_OBJECT ) )
      {
         return;
      }
   }
   
   if ( compositor->allowModifyCursor )
   {
      wstPointerSetPointer( pointer, surface );
      
      if ( pointer->pointerSurface )
      {
         pointer->hotSpotX= hotspot_x;
         pointer->hotSpotY= hotspot_y;
         
         wstPointerUpdatePosition( pointer );
      }

      if ( compositor->invalidateCB )
      {
         compositor->invalidateCB( compositor, compositor->invalidateUserData );
      }
   }
   else
   {
      if ( surface )
      {
         // Hide the client's cursor surface. We will continue to use default pointer image.
         WstRendererSurfaceSetVisible( compositor->renderer, surface->surface, false );
      }
   }
}
                                  
static void wstIPointerRelease( struct wl_client *client, struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);

   WstPointer *pointer= (WstPointer*)wl_resource_get_user_data(resource);
   WstCompositor *compositor= pointer->seat->compositor;

   wl_resource_destroy(resource);
}

#define TEMPFILE_PREFIX "/tmp/westeros-"
#define TEMPFILE_TEMPLATE TEMPFILE_PREFIX ## "XXXXXX"

static bool wstInitializeKeymap( WstCompositor *ctx )
{
   bool result= false;
   char *keymapStr= 0;
   char filename[32];
   int lenDidWrite;

   ctx->xkbCtx= xkb_context_new( XKB_CONTEXT_NO_FLAGS );
   if ( !ctx->xkbCtx )
   {
      sprintf( ctx->lastErrorDetail,
               "Error.  Unable to create xkb context" );
      goto exit;      
   }
   
   ctx->xkbKeymap= xkb_keymap_new_from_names( ctx->xkbCtx,
                                              &ctx->xkbNames,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS );
   if ( !ctx->xkbKeymap )
   {
      sprintf( ctx->lastErrorDetail,
               "Error.  Unable to create xkb keymap" );
      goto exit;      
   }
   
   keymapStr= xkb_keymap_get_as_string( ctx->xkbKeymap,
                                              XKB_KEYMAP_FORMAT_TEXT_V1 );
   if ( !keymapStr )
   {
      sprintf( ctx->lastErrorDetail,
               "Error.  Unable to get xkb keymap in string format" );
      goto exit;      
   }
   
   ctx->xkbKeymapFormat= WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1;
   ctx->xkbKeymapSize= strlen(keymapStr)+1;
   
   strcpy( filename, "/tmp/westeros-XXXXXX" );
   ctx->xkbKeymapFd= mkostemp( filename, O_CLOEXEC );
   if ( ctx->xkbKeymapFd < 0 )
   {
      sprintf( ctx->lastErrorDetail,
               "Error.  Unable to create temp file for xkb keymap string" );
      goto exit;      
   }
   
   lenDidWrite= write( ctx->xkbKeymapFd, keymapStr, ctx->xkbKeymapSize );
   if ( lenDidWrite != ctx->xkbKeymapSize )
   {
      sprintf( ctx->lastErrorDetail,
               "Error.  Unable to create write xkb keymap strint to temp file" );
      goto exit;      
   }

   ctx->xkbKeymapArea= (char*)mmap( NULL,
                                    ctx->xkbKeymapSize,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED | MAP_POPULATE,
                                    ctx->xkbKeymapFd,
                                    0  // offset
                                  );
   if ( ctx->xkbKeymapArea == MAP_FAILED )
   {
      sprintf( ctx->lastErrorDetail,
               "Error.  Unable to mmap temp file for xkb keymap string" );
      goto exit;      
   }

   result= true;
      
exit:

   if ( keymapStr )
   {
      free( keymapStr );
   }
   
   return result;
}

static void wstTerminateKeymap( WstCompositor *ctx )
{
   if ( ctx->xkbKeymapArea )
   {
      munmap( ctx->xkbKeymapArea, ctx->xkbKeymapSize );
      ctx->xkbKeymapArea= 0;      
   }
   
   if ( ctx->xkbKeymapFd >= 0 )
   {
      int pid= getpid();
      int len, prefixlen;
      char path[32];
      char link[256];
      bool haveTempFilename= false;
      
      prefixlen= strlen(TEMPFILE_PREFIX);
      sprintf(path, "/proc/%d/fd/%d", pid, ctx->xkbKeymapFd );
      len= readlink( path, link, sizeof(link)-1 );
      if ( len > prefixlen )
      {
         link[len]= '\0';
         if ( strncmp( link, TEMPFILE_PREFIX, prefixlen ) == 0 )
         {
            haveTempFilename= true;
         }
      }
      
      close( ctx->xkbKeymapFd );
      
      if ( haveTempFilename )
      {
         DEBUG( "removing tempory file (%s)", link );
         remove( link );
      }
      
      ctx->xkbKeymapFd= -1;
   }
   
   if ( ctx->xkbKeymap )
   {
      xkb_keymap_unref( ctx->xkbKeymap );
      ctx->xkbKeymap= 0;
   }
   
   if ( ctx->xkbCtx )
   {
      xkb_context_unref( ctx->xkbCtx );
      ctx->xkbCtx= 0;
   }
}

static void wstProcessKeyEvent( WstKeyboard *keyboard, uint32_t keyCode, uint32_t keyState, uint32_t modifiers )
{
   WstCompositor *compositor= keyboard->seat->compositor;
   xkb_mod_mask_t changes, modDepressed, modLatched, modLocked, modGroup;
   uint32_t serial, time, state;
   struct wl_resource *resource;

   keyboard->currentModifiers= modifiers;
   
   modDepressed= 0;
   modLocked= 0;
   if ( keyboard->currentModifiers & WstKeyboard_shift )
   {
      modDepressed |= (1 << keyboard->modShift);
   }
   if ( keyboard->currentModifiers & WstKeyboard_alt )
   {
      modDepressed |= (1 << keyboard->modAlt);
   }
   if ( keyboard->currentModifiers & WstKeyboard_ctrl )
   {
      modDepressed |= (1 << keyboard->modCtrl);
   }
   if ( keyboard->currentModifiers & WstKeyboard_caps )
   {
      modLocked |= (1 << keyboard->modCaps);
   }
   
   changes= xkb_state_update_mask( keyboard->state,
                                   modDepressed,
                                   0, // modLatched
                                   modLocked,
                                   0, //depressed layout index
                                   0, //latched layout index
                                   0  //locked layout index
                                 );

   if ( changes )
   {
      wl_resource_for_each( resource, &keyboard->resourceList )
      {
         wstKeyboardSendModifiers( keyboard, resource );
      }
   }
   
   time= (uint32_t)wstGetCurrentTimeMillis();
   serial= wl_display_next_serial( compositor->display );
   state= (keyState == WstKeyboard_keyState_depressed) 
          ? WL_KEYBOARD_KEY_STATE_PRESSED
          : WL_KEYBOARD_KEY_STATE_RELEASED;
             
   wl_resource_for_each( resource, &keyboard->resourceList )
   {
      wl_keyboard_send_key( resource, 
                            serial,
                            time,
                            keyCode,
                            state );
   }   
}

static void wstKeyboardSendModifiers( WstKeyboard *keyboard, struct wl_resource *resource )
{
   WstCompositor *compositor= keyboard->seat->compositor;
   xkb_mod_mask_t modDepressed, modLatched, modLocked, modGroup;
   uint32_t serial;
   
   modDepressed= 0;
   modLocked= 0;
   if ( keyboard->currentModifiers & WstKeyboard_shift )
   {
      modDepressed |= (1 << keyboard->modShift);
   }
   if ( keyboard->currentModifiers & WstKeyboard_alt )
   {
      modDepressed |= (1 << keyboard->modAlt);
   }
   if ( keyboard->currentModifiers & WstKeyboard_ctrl )
   {
      modDepressed |= (1 << keyboard->modCtrl);
   }
   if ( keyboard->currentModifiers & WstKeyboard_caps )
   {
      modLocked |= (1 << keyboard->modCaps);
   }
   
   xkb_state_update_mask( keyboard->state,
                          modDepressed,
                          0, // modLatched
                          modLocked,
                          0, //depressed layout index
                          0, //latched layout index
                          0  //locked layout index
                        );

   modDepressed= xkb_state_serialize_mods( keyboard->state, (xkb_state_component)XKB_STATE_DEPRESSED );
   modLatched= xkb_state_serialize_mods( keyboard->state, (xkb_state_component)XKB_STATE_LATCHED );
   modLocked= xkb_state_serialize_mods( keyboard->state, (xkb_state_component)XKB_STATE_LOCKED );
   modGroup= xkb_state_serialize_group( keyboard->state, (xkb_state_component)XKB_STATE_EFFECTIVE );

   serial= wl_display_next_serial( compositor->display );
   
   wl_keyboard_send_modifiers( resource,
                               serial,
                               modDepressed, // mod depressed
                               modLatched,   // mod latched
                               modLocked,    // mod locked
                               modGroup      // mod group
                             );
}

static void wstProcessPointerEnter( WstPointer *pointer )
{
   pointer->entered= true;
}

static void wstProcessPointerLeave( WstPointer *pointer )
{
   WstCompositor *compositor= pointer->seat->compositor;

   if ( pointer->focus )
   {
      wstPointerSetFocus( pointer, 
                          0,    // surface 
                          0,    // x
                          0     // y
                        );
   }
   
   if ( pointer->pointerSurface )
   {
      wstPointerSetPointer( pointer, 0 );
   }   
   
   pointer->pointerX= 0;
   pointer->pointerY= 0;
   
   if ( compositor->invalidateCB )
   {
      compositor->invalidateCB( compositor, compositor->invalidateUserData );
   }

   pointer->entered= false;
}

static void wstProcessPointerMoveEvent( WstPointer *pointer, int32_t x, int32_t y )
{
   WstCompositor *compositor= pointer->seat->compositor;
   WstSurface *surface= 0;
   int sx, sy, sw, sh;
   uint32_t time;
   struct wl_resource *resource;

   pointer->pointerX= x;
   pointer->pointerY= y;
   
   wstPointerCheckFocus( pointer, x, y );
      
   if ( pointer->focus )
   {
      wl_fixed_t xFixed, yFixed;

      WstRendererSurfaceGetGeometry( compositor->renderer, pointer->focus->surface, &sx, &sy, &sw, &sh );
      
      if ( compositor->isEmbedded )
      {
         x= (x*compositor->renderer->resW/compositor->renderer->outputWidth);
         y= (y*compositor->renderer->resH/compositor->renderer->outputHeight);
      }
      
      xFixed= wl_fixed_from_int( x-sx );
      yFixed= wl_fixed_from_int( y-sy );
      
      time= (uint32_t)wstGetCurrentTimeMillis();
      wl_resource_for_each( resource, &pointer->focusResourceList )
      {
         wl_pointer_send_motion( resource, time, xFixed, yFixed );
      }
      
      if ( pointer->pointerSurface )
      {
         wstPointerUpdatePosition( pointer );
      }
      
      wstCompositorScheduleRepaint( compositor );
   }
}

static void wstProcessPointerButtonEvent( WstPointer *pointer, uint32_t button, uint32_t buttonState )
{
   WstCompositor *compositor= pointer->seat->compositor;
   uint32_t serial, time, btnState;
   struct wl_resource *resource;
   
   if ( pointer->focus )
   {
      serial= wl_display_next_serial( compositor->display );
      time= (uint32_t)wstGetCurrentTimeMillis();
      btnState= (buttonState == WstPointer_buttonState_depressed) 
                ? WL_POINTER_BUTTON_STATE_PRESSED 
                : WL_POINTER_BUTTON_STATE_RELEASED;
      wl_resource_for_each( resource, &pointer->focusResourceList )
      {
         wl_pointer_send_button( resource, serial, time, button, buttonState );
      }
   }
}

static void wstPointerCheckFocus( WstPointer *pointer, int32_t x, int32_t y )
{
   WstCompositor *compositor= pointer->seat->compositor;
   WstSurface *surface= 0;
   int sx, sy, sw, sh;
   wl_fixed_t xFixed, yFixed;
   bool haveRoles= false;
   WstSurface *surfaceNoRole= 0;

   // Identify top-most surface containing the pointer position
   for ( std::vector<WstSurface*>::reverse_iterator it= compositor->surfaces.rbegin(); 
         it != compositor->surfaces.rend();
         ++it )
   {
      surface= (*it);
      
      WstRendererSurfaceGetGeometry( compositor->renderer, surface->surface, &sx, &sy, &sw, &sh );
      
      // If this client is using surfaces with roles (eg xdg shell surfaces) then we only
      // want to assign focus to surfaces with appropriate roles.  However, we take note of
      // the best choice of surfaces with no role.  If we don't find a hit with a roled surface
      // and there was no use of roles, then we set focus on the best hit with  a surface
      // with no role.  This will happen if the client is a nested compositor instance.
      if ( surface->roleName )
      {
         haveRoles= true;
      }

      if ( (x >= sx) && (x < sx+sw) && (y >= sy) && (y < sy+sh) )
      {
         bool eligible= true;
         
         if ( surface->roleName )
         {
            int len= strlen(surface->roleName );
            if ( (len == 17) && !strncmp( surface->roleName, "wl_pointer-cursor", len ) )
            {
               eligible= false;
            } 
         }
         else
         {
            if ( !surfaceNoRole )
            {
               surfaceNoRole= surface;
            }
            eligible= false;
         }
         if ( eligible )
         {
            break;
         }
      }
      
      surface= 0;
   }
   
   if ( !surface && !haveRoles )
   {
      surface= surfaceNoRole;
   }
   
   if ( pointer->focus != surface )
   {
      xFixed= wl_fixed_from_int( x-sx );
      yFixed= wl_fixed_from_int( y-sy );

      wstPointerSetFocus( pointer, surface, xFixed, yFixed );
   }   
}

static void wstPointerSetPointer( WstPointer *pointer, WstSurface *surface )
{
   WstCompositor *compositor= pointer->seat->compositor;
   bool hidePointer;

   if ( surface )
   {
      WstRendererSurfaceSetZOrder( compositor->renderer, surface->surface, 1000000.0f );
      WstRendererSurfaceSetVisible( compositor->renderer, surface->surface, true );
      hidePointer= true;
   }
   else
   {
      if ( pointer->pointerSurface )
      {
         WstRendererSurfaceSetVisible( compositor->renderer, pointer->pointerSurface->surface, false );
      }
      if ( pointer->focus )
      {
         hidePointer= true;
      }
      else
      {
         hidePointer= false;
      }
   }

   pointer->pointerSurface= surface;
   
   if ( compositor->hidePointerCB )
   {
      compositor->hidePointerCB( compositor, hidePointer, compositor->hidePointerUserData );
   }   
}

static void wstPointerUpdatePosition( WstPointer *pointer )
{
   int px, py, pw, ph;
   WstCompositor *compositor= pointer->seat->compositor;
   WstSurface *pointerSurface= pointer->pointerSurface;

   WstRendererSurfaceGetGeometry( compositor->renderer, pointerSurface->surface, &px, &py, &pw, &ph );
   px= pointer->pointerX-pointer->hotSpotX;
   py= pointer->pointerY-pointer->hotSpotY;
   WstRendererSurfaceSetGeometry( compositor->renderer, pointerSurface->surface, px, py, pw, ph );
}

static void wstPointerSetFocus( WstPointer *pointer, WstSurface *surface, wl_fixed_t x, wl_fixed_t y )
{
   WstCompositor *compositor= pointer->seat->compositor;
   uint32_t serial, time;
   struct wl_client *surfaceClient;
   struct wl_resource *resource;

   if ( pointer->focus != surface )
   {
      if ( pointer->focus )
      {
         serial= wl_display_next_serial( compositor->display );
         surfaceClient= wl_resource_get_client( pointer->focus->resource );
         wl_resource_for_each( resource, &pointer->focusResourceList )
         {
            wl_pointer_send_leave( resource, serial, pointer->focus->resource );
         }
         pointer->focus= 0;
         if ( pointer->pointerSurface )
         {
            if ( wl_resource_get_client( pointer->pointerSurface->resource ) == surfaceClient )
            {
               wstPointerSetPointer( pointer, 0 );
            }
         }
      }
      
      pointer->focus= surface;
      
      if ( pointer->focus )
      {
         surfaceClient= wl_resource_get_client( pointer->focus->resource );
         wstPointerMoveFocusToClient( pointer, surfaceClient );

         serial= wl_display_next_serial( compositor->display );
         wl_resource_for_each( resource, &pointer->focusResourceList )
         {
            wl_pointer_send_enter( resource, serial, pointer->focus->resource, x, y );
         }
      }
      else
      {
         wstPointerSetPointer( pointer, 0 );
      }
   }
}

static void wstPointerMoveFocusToClient( WstPointer *pointer, struct wl_client *client )
{
   struct wl_resource *resource;
   struct wl_resource *temp;
   
   wl_list_insert_list( &pointer->resourceList, &pointer->focusResourceList );
   wl_list_init( &pointer->focusResourceList );
   
   wl_resource_for_each_safe( resource, temp, &pointer->resourceList )
   {
      if ( wl_resource_get_client( resource ) == client )
      {
         wl_list_remove( wl_resource_get_link( resource ) );
         wl_list_insert( &pointer->focusResourceList, wl_resource_get_link(resource) );
      }
   }
}


