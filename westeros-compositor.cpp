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

#if defined (WESTEROS_HAVE_WAYLAND_EGL)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined (WESTEROS_PLATFORM_RPI)
#include <bcm_host.h>
#endif
#include "wayland-egl.h"
#endif

#include "wayland-server.h"
#include "westeros-nested.h"
#ifdef ENABLE_SBPROTOCOL
#include "westeros-simplebuffer.h"
#endif
#include "westeros-simpleshell.h"
#include "xdg-shell-server-protocol.h"
#include "vpc-server-protocol.h"

#define WST_MAX_ERROR_DETAIL (512)

#define MAX_NESTED_NAME_LEN (32)

#define DEFAULT_FRAME_RATE (60)
#define DEFAULT_OUTPUT_WIDTH (1280)
#define DEFAULT_OUTPUT_HEIGHT (720)
#define DEFAULT_NESTED_WIDTH (1280)
#define DEFAULT_NESTED_HEIGHT (720)

#define DEFAULT_KEY_REPEAT_DELAY (1000)
#define DEFAULT_KEY_REPEAT_RATE  (5)

#if !defined (XKB_KEYMAP_COMPILE_NO_FLAGS)
#define XKB_KEYMAP_COMPILE_NO_FLAGS XKB_MAP_COMPILE_NO_FLAGS
#endif

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

#define WST_EVENT_QUEUE_SIZE (64)

typedef enum _WstEventType
{
   WstEventType_key,
   WstEventType_keyCode,
   WstEventType_keyModifiers,
   WstEventType_pointerEnter,
   WstEventType_pointerLeave,
   WstEventType_pointerMove,
   WstEventType_pointerButton,
} WstEventType;

typedef struct _WstEvent
{
   WstEventType type;
   void *p1;
   unsigned int v1;
   unsigned int v2;
   unsigned int v3;
   unsigned int v4;
} WstEvent;

typedef struct _WstSurface WstSurface;
typedef struct _WstSeat WstSeat;

typedef struct _WstShellSurface
{
   struct wl_resource *resource;
   WstSurface *surface;
   const char* title;
   const char* className;
} WstShellSurface;

typedef struct _WstVpcSurface
{
   struct wl_resource *resource;
   WstSurface *surface;
   WstCompositor *compositor;
   struct wl_vpc_surface *vpcSurfaceNested;
   bool useHWPath;
   bool useHWPathNext;
   bool pathTransitionPending;
   int xTrans;
   int yTrans;
   int xScaleNum;
   int xScaleDenom;
   int yScaleNum;
   int yScaleDenom;
} WstVpcSurface;

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

typedef struct _WstShm WstShm;
typedef struct _WstShmPool WstShmPool;

typedef struct _WstShmBuffer
{
   WstShmPool *pool;
   struct wl_resource *bufferResource;
   struct wl_buffer *bufferNested;
   int32_t width;
   int32_t height;
   int32_t stride;
   uint32_t format;
} WstShmBuffer;

typedef struct _WstShmPool
{
   WstShm *shm;
   int32_t refCount;
   struct wl_resource *poolResource;
   struct wl_shm_pool *poolNested;
} WstShmPool;

typedef struct _WstShm
{
   WstCompositor *compositor;
   struct wl_list resourceList;
} WstShm;

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
   WstVpcSurface *vpcSurface;
   struct wl_surface *surfaceNested;
   const char *roleName;
   
   bool visible;
   int x;
   int y;
   int width;
   int height;
   float opacity;
   float zorder;

   const char *name;   
   int refCount;
   
   struct wl_resource *attachedBufferResource;
   int attachedX;
   int attachedY;
   
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

typedef struct _WstOutput
{
   WstCompositor *compositor;
   struct wl_list resourceList;
   int x;
   int y;
   int width;
   int height;
   int refreshRate;
   int mmWidth;
   int mmHeight;
   int subPixel;
   const char *make;
   const char *model;
   int transform;
   int currentScale;
   
} WstOutput;

typedef struct _WstCompositor
{
   const char *displayName;
   unsigned int frameRate;
   int framePeriodMillis;
   const char *rendererModule;
   bool isNested;
   bool isRepeater;
   bool isEmbedded;
   const char *nestedDisplayName;
   unsigned int nestedWidth;
   unsigned int nestedHeight;
   bool allowModifyCursor;
   void *nativeWindow;
   int outputWidth;
   int outputHeight;
   
   int eventIndex;   
   WstEvent eventQueue[WST_EVENT_QUEUE_SIZE];

   void *terminatedUserData;
   WstTerminatedCallback terminatedCB;
   void *dispatchUserData;
   WstDispatchCallback dispatchCB;
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

   WstOutput *output;

   WstSeat *seat;
   WstRenderer *renderer;

   WstShm *shm;   
   WstNestedConnection *nc;
   void *nestedListenerUserData;
   WstNestedConnectionListener nestedListener;
   bool hasEmbeddedMaster;
   
   void *outputNestedListenerUserData;
   WstOutputNestedListener *outputNestedListener;
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
   std::vector<WstVpcSurface*> vpcSurfaces;
   std::map<int32_t, WstSurface*> surfaceMap;
   std::map<struct wl_client*, WstClientInfo*> clientInfoMap;
   std::map<struct wl_resource*, WstSurfaceInfo*> surfaceInfoMap;

   bool needRepaint;
   bool outputSizeChanged;
   
   struct wl_display *dcDisplay;
   struct wl_registry *dcRegistry;
   struct wl_shm *dcShm;
   struct wl_compositor *dcCompositor;
   struct wl_seat *dcSeat;
   struct wl_pointer *dcPointer;
   struct wl_shm_pool *dcPool;
   struct wl_surface *dcCursorSurface;
   struct wl_client *dcClient;
   void *dcPoolData;
   int dcPoolSize;
   int dcPoolFd;
   int dcPid;
   bool dcDefaultCursor;
   
} WstCompositor;

static const char* wstGetNextNestedDisplayName(void);
static void* wstCompositorThread( void *arg );
static long long wstGetCurrentTimeMillis(void);
static void wstCompositorProcessEvents( WstCompositor *ctx );
static void wstCompositorComposeFrame( WstCompositor *ctx, uint32_t frameTime );
static int wstCompositorDisplayTimeOut( void *data );
static void wstCompositorScheduleRepaint( WstCompositor *ctx );
static void wstShmBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static bool wstShmInit( WstCompositor *ctx );
static void wstShmTerm( WstCompositor *ctx );
static void wstIShmBufferDestroy(struct wl_client *client, struct wl_resource *resource);
static void wstIShmPoolCreateBuffer( struct wl_client *client, struct wl_resource *resource,
                                     uint32_t id, int32_t offset,
                                     int32_t width, int32_t height,
                                     int32_t stride, uint32_t format);
static void wstShmBufferDestroy( struct wl_resource *resource );
static void wstIShmPoolDestroy( struct wl_client *client, struct wl_resource *resource );
static void wstIShmPoolResize( struct wl_client *client, struct wl_resource *resource, int32_t size );
static void wstIShmCreatePool( struct wl_client *client, struct wl_resource *resource,
                               uint32_t id, int fd, int32_t size );
void wstShmDestroyPool( struct wl_resource *resource );  
void wstShmPoolUnRef( WstShmPool *pool );                            
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
                              struct wl_resource *bufferResource, int32_t sx, int32_t sy);
static void wstISurfaceDamage(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t x, int32_t y, int32_t width, int32_t height);
static void wstISurfaceFrame(struct wl_client *client,
                             struct wl_resource *resource, uint32_t callback);
static void wstISurfaceSetOpaqueRegion(struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *regionResource);
static void wstISurfaceSetInputRegion(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *regionResource);
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
static bool wstOutputInit( WstCompositor *ctx );
static void wstOutputTerm( WstCompositor *ctx );                                
static void wstOutputBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);                                
static void wstOutputChangeSize( WstCompositor *ctx );
static void wstShellBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void wstIShellGetShellSurface(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     struct wl_resource *surfaceResource );
static void wstDestroyShellSurfaceCallback(struct wl_resource *resource);
static void wstShellSurfaceDestroy( WstShellSurface *shellSurface );
static void wstIShellSurfacePong(struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t serial );
static void wstIShellSurfaceMove(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *seatResource,
                                 uint32_t serial );
static void wstIShellSurfaceResize(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *seatResource,
                                   uint32_t serial,
                                   uint32_t edges );
static void wstIShellSurfaceSetTopLevel(struct wl_client *client, 
                                        struct wl_resource *resource);
static void wstIShellSurfaceSetTransient(struct wl_client *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *parentResource,
                                         int x, int y, uint32_t flags );
static void wstIShellSurfaceSetFullscreen(struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t method,
                                          uint32_t framerate,
                                          struct wl_resource *outputResource );
static void wstIShellSurfaceSetPopup(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *seatResource,
                                     uint32_t serial,
                                     struct wl_resource *parentResource,
                                     int x, int y, uint32_t flags );
static void wstIShellSurfaceSetMaximized(struct wl_client *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *outputResource );
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
                                  struct wl_resource *surfaceResource );
static void  wstIXdgGetXdgPopup( struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t id,
                                 struct wl_resource *surfaceResource,
                                 struct wl_resource *parentResource,
                                 struct wl_resource *seatResource,
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
                                          struct wl_resource *parentResource );
static void wstIXdgShellSurfaceSetTitle( struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *title );
static void wstIXdgShellSurfaceSetAppId( struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *app_id );
static void wstIXdgShellSurfaceShowWindowMenu( struct wl_client *client,
                                               struct wl_resource *resource,
                                               struct wl_resource *seatResource,
                                               uint32_t serial,
                                               int32_t x,
                                               int32_t y );
static void wstIXdgShellSurfaceMove( struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *seatResource,
                                     uint32_t serial );
static void wstIXdgShellSurfaceResize( struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *seatResource,
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
                                              struct wl_resource *outputResource );
static void wstIXdgShellSurfaceUnSetFullscreen( struct wl_client *client,
                                                struct wl_resource *resource );
static void wstIXdgShellSurfaceSetMinimized( struct wl_client *client,
                                             struct wl_resource *resource );
static void wstXdgSurfaceSendConfigure( WstCompositor *ctx, WstSurface *surface, uint32_t state );
static void wstDefaultNestedConnectionEnded( void *userData );
static void wstDefaultNestedOutputHandleGeometry( void *userData, int32_t x, int32_t y, int32_t mmWidth,
                                                  int32_t mmHeight, int32_t subPixel, const char *make, 
                                                  const char *model, int32_t transform );
static void wstDefaultNestedOutputHandleMode( void* userData, uint32_t flags, int32_t width, 
                                              int32_t height, int32_t refreshRate );
static void wstDefaultNestedOutputHandleDone( void *userData );
static void wstDefaultNestedOutputHandleScale( void *userData, int32_t scale );                                              
static void wstDefaultNestedKeyboardHandleKeyMap( void *userData, uint32_t format, int fd, uint32_t size );
static void wstDefaultNestedKeyboardHandleEnter( void *userData, struct wl_array *keys );
static void wstDefaultNestedKeyboardHandleLeave( void *userData );
static void wstDefaultNestedKeyboardHandleKey( void *userData, uint32_t time, uint32_t key, uint32_t state );
static void wstDefaultNestedKeyboardHandleModifiers( void *userData, uint32_t mods_depressed, uint32_t mods_latched, 
                                                     uint32_t mods_locked, uint32_t group );
static void wstDefaultNestedKeyboardHandleRepeatInfo( void *userData, int32_t rate, int32_t delay );
static void wstDefaultNestedPointerHandleEnter( void *userData, struct wl_surface *surfaceNested, wl_fixed_t sx, wl_fixed_t sy );
static void wstDefaultNestedPointerHandleLeave( void *userData, struct wl_surface *surfaceNested );
static void wstDefaultNestedPointerHandleMotion( void *userData, uint32_t time, wl_fixed_t sx, wl_fixed_t sy );
static void wstDefaultNestedPointerHandleButton( void *userData, uint32_t time, uint32_t button, uint32_t state );
static void wstDefaultNestedPointerHandleAxis( void *userData, uint32_t time, uint32_t axis, wl_fixed_t value );
static void wstDefaultNestedShmFormat( void *userData, uint32_t format );
static void wstDefaultNestedVpcVideoPathChange( void *userData, struct wl_surface *surfaceNested, uint32_t newVideoPath );
static void wstDefaultNestedVpcVideoXformChange( void *userData,
                                                 struct wl_surface *surfaceNested,
                                                 int32_t x_translation,
                                                 int32_t y_translation,
                                                 uint32_t x_scale_num,
                                                 uint32_t x_scale_denom,
                                                 uint32_t y_scale_num,
                                                 uint32_t y_scale_denom );
static void wstDefaultNestedSurfaceStatus( void *userData, struct wl_surface *surface,
                                           const char *name,
                                           uint32_t visible,
                                           int32_t x,
                                           int32_t y,
                                           int32_t width,
                                           int32_t height,
                                           wl_fixed_t opacity,
                                           wl_fixed_t zorder);
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
                                  struct wl_resource *surfaceResource,
                                  int32_t hotspot_x,
                                  int32_t hotspot_y );                                  
static void wstIPointerRelease( struct wl_client *client, struct wl_resource *resource );
static void wstVpcBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void wstIVpcGetVpcSurface( struct wl_client *client, struct wl_resource *resource, 
                                  uint32_t id, struct wl_resource *surfaceResource);
static void wstDestroyVpcSurfaceCallback(struct wl_resource *resource);
static void wstVpcSurfaceDestroy( WstVpcSurface *vpcSurface );
static void wstUpdateVPCSurfaces( WstCompositor *ctx, std::vector<WstRect> &rects );
static bool wstInitializeKeymap( WstCompositor *ctx );
static void wstTerminateKeymap( WstCompositor *ctx );
static void wstProcessKeyEvent( WstKeyboard *keyboard, uint32_t keyCode, uint32_t keyState, uint32_t modifiers );
static void wstKeyboardSendModifiers( WstKeyboard *keyboard, struct wl_resource *resource );
static void wstProcessPointerEnter( WstPointer *pointer, int x, int y, struct wl_surface *surfaceNested );
static void wstProcessPointerLeave( WstPointer *pointer, struct wl_surface *surfaceNested );
static void wstProcessPointerMoveEvent( WstPointer *pointer, int32_t x, int32_t y );
static void wstProcessPointerButtonEvent( WstPointer *pointer, uint32_t button, uint32_t buttonState, uint32_t time );
static void wstPointerCheckFocus( WstPointer *pointer, int32_t x, int32_t y );
static void wstPointerSetPointer( WstPointer *pointer, WstSurface *surface );
static void wstPointerUpdatePosition( WstPointer *pointer );
static void wstPointerSetFocus( WstPointer *pointer, WstSurface *surface, wl_fixed_t x, wl_fixed_t y );
static void wstPointerMoveFocusToClient( WstPointer *pointer, struct wl_client *client );
static void wstRemoveTempFile( int fd );
static bool wstInitializeDefaultCursor( WstCompositor *compositor, 
                                        unsigned char *imgData, int width, int height,
                                        int hotspotX, int hotspotY  );
static void wstTerminateDefaultCursor( WstCompositor *compositor );

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
      
      ctx->nextSurfaceId= 1;
      
      ctx->surfaceMap= std::map<int32_t, WstSurface*>();
      ctx->clientInfoMap= std::map<struct wl_client*, WstClientInfo*>();
      ctx->surfaceInfoMap= std::map<struct wl_resource*, WstSurfaceInfo*>();
      ctx->vpcSurfaces= std::vector<WstVpcSurface*>();
      
      ctx->xkbNames.rules= strdup("evdev");
      ctx->xkbNames.model= strdup("pc105");
      ctx->xkbNames.layout= strdup("us");
      ctx->xkbKeymapFd= -1;

      ctx->dcPoolFd= -1;

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

bool WstCompositorSetNativeWindow( WstCompositor *ctx, void *nativeWindow )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set native window while compositor is running" );
         goto exit;
      }

      pthread_mutex_lock( &ctx->mutex );
      
      ctx->nativeWindow= nativeWindow;
      
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

bool WstCompositorSetIsRepeater( WstCompositor *ctx, bool isRepeater )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set isRepeater while compositor is running" );
         goto exit;
      }
                     
      pthread_mutex_lock( &ctx->mutex );
      
      ctx->isRepeater= isRepeater;
      if ( isRepeater )
      {
         ctx->isNested= true;
         #if defined (WESTEROS_HAVE_WAYLAND_EGL) && !defined(WESTEROS_PLATFORM_RPI) && !defined(WESTEROS_PLATFORM_NEXUS)
         // We can't do renderless composition with some wayland-egl.  Ignore the
         // request and configure for nested composition with gl renderer
         ctx->isRepeater= false;
         ctx->rendererModule= strdup("libwesteros_render_gl.so.0");
         WARNING("WstCompositorSetIsRepeater: cannot repeat with wayland-egl: configuring nested with gl renderer");
         #endif
      }
               
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

bool WstCompositorSetOutputSize( WstCompositor *ctx, int width, int height )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( width == 0 )
      {
         sprintf( ctx->lastErrorDetail,
                  "Invalid argument.  The output width (%u) must be greater than zero", width );
         goto exit;      
      }

      if ( height == 0 )
      {
         sprintf( ctx->lastErrorDetail,
                  "Invalid argument.  The output height (%u) must be greater than zero", height );
         goto exit;      
      }
               
      pthread_mutex_lock( &ctx->mutex );
      
      ctx->outputWidth= width;
      ctx->outputHeight= height;
      
      if ( ctx->running )
      {
         ctx->outputSizeChanged= true;
      }
               
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

/**
 * WstCompositorSetDefaultCursor
 *
 * Supplies a default pointer cursor image for the compositor to display.  The
 * data should be supplied in ARGB888 format as an array of 32 bit ARGB samples
 * containing width*height*4 bytes.  To remove a previously set curosr, call
 * with imgData set to NULL.  This should only be called while the 
 * conpositor is running.
 */
bool WstCompositorSetDefaultCursor( WstCompositor *ctx, unsigned char *imgData,
                                    int width, int height, int hotSpotX, int hotSpotY )
{
   bool result= false;
   
   if ( ctx )
   {
      if ( !ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set default cursor unless the compositor is running" );
         goto exit;
      }

      if ( ctx->isRepeater )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Cannot set default cursor when operating as a repeating nested compositor" );
         goto exit;
      }

      if ( ctx->isNested )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Cannot set default cursor when operating as a nested compositor" );
         goto exit;
      }

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->dcClient )
      {
         wl_client_destroy( ctx->dcClient );
         ctx->dcClient= 0;
         ctx->dcPid= 0;
         ctx->dcDefaultCursor= false;
         pthread_mutex_unlock( &ctx->mutex );
         usleep( 100000 );
         pthread_mutex_lock( &ctx->mutex );
      }
      else
      {
         wstTerminateDefaultCursor( ctx );
      }
      
      if ( imgData )
      {
         if ( !wstInitializeDefaultCursor( ctx, imgData, width, height, hotSpotX, hotSpotY ) )
         {
            sprintf( ctx->lastErrorDetail,
                     "Error.  Unable to set default cursor" );
            pthread_mutex_unlock( &ctx->mutex );
            goto exit;
         }
      }
               
      pthread_mutex_unlock( &ctx->mutex );
            
      result= true;
   }

exit:
   
   return result;
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

bool WstCompositorGetIsRepeater( WstCompositor *ctx )
{
   bool isRepeater= false;
   
   if ( ctx )
   {               
      pthread_mutex_lock( &ctx->mutex );
      
      isRepeater= ctx->isRepeater;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return isRepeater;
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

void WstCompositorGetOutputSize( WstCompositor *ctx, unsigned int *width, unsigned int *height )
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

bool WstCompositorSetDispatchCallback( WstCompositor *ctx, WstDispatchCallback cb, void *userData )
{
   bool result= false;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      ctx->dispatchUserData= userData;
      ctx->dispatchCB= cb;
      
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

bool WstCompositorSetOutputNestedListener( WstCompositor *ctx, WstOutputNestedListener *listener, void *userData )
{
  bool result= false;
   
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->running )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Cannot set output nested listener while compositor is running" );
         goto exit;
      }      

      if ( !ctx->isNested )
      {
         sprintf( ctx->lastErrorDetail,
                  "Bad state.  Compositor is not nested" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      ctx->outputNestedListenerUserData= userData;
      ctx->outputNestedListener= listener;
      
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

bool WstCompositorComposeEmbedded( WstCompositor *ctx, 
                                   int x, int y, int width, int height,
                                   float *matrix, float alpha, 
                                   unsigned int hints, 
                                   bool *needHolePunch, std::vector<WstRect> &rects )
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
         ctx->renderer->outputX= x;
         ctx->renderer->outputY= y;
         ctx->renderer->matrix= matrix;
         ctx->renderer->alpha= alpha;
         ctx->renderer->fastHint= (hints & WstHints_noRotation);
         ctx->renderer->needHolePunch= false;
         ctx->renderer->rects.clear();
         
         if ( ctx->vpcSurfaces.size() )
         {
            wstUpdateVPCSurfaces( ctx, rects );
         }
         
         WstRendererUpdateScene( ctx->renderer );
         
         if ( ctx->renderer->rects.size() )
         {
            *needHolePunch= ctx->renderer->needHolePunch;
            rects= ctx->renderer->rects;
         }
         else if ( rects.size() )
         {
            *needHolePunch= true;
         }
      }
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:
   
   return result;
}

void WstCompositorInvalidateScene( WstCompositor *ctx )
{
   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );

      wstCompositorScheduleRepaint( ctx );

      pthread_mutex_unlock( &ctx->mutex );
   }
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

      if ( !ctx->rendererModule && ctx->isRepeater )
      {
         ctx->rendererModule= strdup("libwesteros_render_gl.so.0");      
      }
      
      if ( !ctx->rendererModule && !ctx->isRepeater )
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

      pthread_mutex_unlock( &ctx->mutex );
      
      for( int i= 0; i < 500; ++i )
      {
         bool ready;
         
         pthread_mutex_lock( &ctx->mutex );
         ready= ctx->compositorReady;
         pthread_mutex_unlock( &ctx->mutex );
         
         if ( ready )
         {
            break;
         }
         
         usleep( 10000 );
      }

      if ( !ctx->compositorReady )
      {
         sprintf( ctx->lastErrorDetail,
                  "Error.  Compositor thread failed to create display" );
         goto exit;      
      }

      ctx->running= true;

      result= true;      

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
         int eventIndex= ctx->eventIndex;
         ctx->eventQueue[eventIndex].type= WstEventType_key;
         ctx->eventQueue[eventIndex].v1= keyCode;
         ctx->eventQueue[eventIndex].v2= keyState;
         ctx->eventQueue[eventIndex].v3= modifiers;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
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
         int eventIndex= ctx->eventIndex;
         ctx->eventQueue[eventIndex].type= WstEventType_pointerEnter;
         ctx->eventQueue[eventIndex].v1= 0;
         ctx->eventQueue[eventIndex].v2= 0;
         ctx->eventQueue[eventIndex].p1= 0;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
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
         int eventIndex= ctx->eventIndex;
         ctx->eventQueue[eventIndex].type= WstEventType_pointerLeave;
         ctx->eventQueue[eventIndex].p1= 0;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
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
         int eventIndex= ctx->eventIndex;
         ctx->eventQueue[eventIndex].type= WstEventType_pointerMove;
         ctx->eventQueue[eventIndex].v1= x;
         ctx->eventQueue[eventIndex].v2= y;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
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
         int eventIndex= ctx->eventIndex;
         ctx->eventQueue[eventIndex].type= WstEventType_pointerButton;
         ctx->eventQueue[eventIndex].v1= button;
         ctx->eventQueue[eventIndex].v2= buttonState;
         ctx->eventQueue[eventIndex].v3= 0; //no time
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
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

      FILE *pClientLog= 0;
      char *clientLogName= getenv( "WESTEROS_CAPTURE_CLIENT_STDOUT" );
      if ( clientLogName )
      {
         pClientLog= fopen( clientLogName, "w" );
         printf("capturing stdout for client %s to file %s\n", args[0], clientLogName );
      }
      
      // Launch client
      int pid= fork();
      if ( pid == 0 )
      {
         if ( pClientLog )
         {
            dup2( fileno(pClientLog), STDOUT_FILENO );
         }

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

         if(ctx->clientStatusCB)
         {
             INFO("clientStatus: status %d pid %d", WstClient_started, pid);
             ctx->clientStatusCB( ctx, WstClient_started, pid, 0, ctx->clientStatusUserData );
         }

         pidChild= waitpid( pid, &status, 0 );
         if ( pidChild != 0 )
         {
            int clientStatus, detail= 0;

            if ( pClientLog )
            {
               fclose( pClientLog );
            }
            
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

static void simpleShellSetVisible( void* userData, uint32_t surfaceId, bool visible )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      surface->visible= visible;
      if ( surface->compositor->isRepeater )
      {
         WstNestedConnectionSurfaceSetVisible( ctx->nc, surface->surfaceNested, visible );
      }
      else
      {
         WstRendererSurfaceSetVisible( ctx->renderer, surface->surface, visible );
      }
   }
}

static void simpleShellSetGeometry( void* userData, uint32_t surfaceId, int x, int y, int width, int height )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      surface->x= x;
      surface->y= y;
      surface->width= width;
      surface->height= height;
      if ( surface->compositor->isRepeater )
      {
         WstNestedConnectionSurfaceSetGeometry( ctx->nc, surface->surfaceNested, x, y, width, height );
      }
      else
      {
         WstRendererSurfaceSetGeometry( ctx->renderer, surface->surface, x, y, width, height );
      }
   }
}

static void simpleShellSetOpacity( void* userData, uint32_t surfaceId, float opacity )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      surface->opacity= opacity;
      if ( surface->compositor->isRepeater )
      {
         WstNestedConnectionSurfaceSetOpacity( ctx->nc, surface->surfaceNested, opacity );
      }
      else
      {
         WstRendererSurfaceSetOpacity( ctx->renderer, surface->surface, opacity );
      }
   }
}

static void simpleShellSetZOrder( void* userData, uint32_t surfaceId, float zorder )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      surface->zorder= zorder;
      if ( surface->compositor->isRepeater )
      {
         WstNestedConnectionSurfaceSetZOrder( ctx->nc, surface->surfaceNested, zorder );
      }
      else
      {
         WstRendererSurfaceSetZOrder( ctx->renderer, surface->surface, zorder );
         
         wstSurfaceInsertSurface( ctx, surface );
      }
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

static void simpleShellGetStatus( void* userData, uint32_t surfaceId, bool *visible,
                                  int *x, int *y, int *width, int *height,
                                  float *opacity, float *zorder )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      if ( surface->compositor->isRepeater )
      {
         *visible= surface->visible;
         *x= surface->x;
         *y= surface->y;
         *width= surface->width;
         *height= surface->height;
         *opacity= surface->opacity;
         *zorder= surface->zorder;
      }
      else
      {
         WstRendererSurfaceGetVisible( ctx->renderer, surface->surface, visible );
         WstRendererSurfaceGetGeometry( ctx->renderer, surface->surface, x, y, width, height );
         WstRendererSurfaceGetOpacity( ctx->renderer, surface->surface, opacity );
         WstRendererSurfaceGetZOrder( ctx->renderer, surface->surface, zorder );
      }
   }
}

struct wayland_simple_shell_callbacks simpleShellCallbacks= {
   simpleShellSetName,
   simpleShellSetVisible,
   simpleShellSetGeometry,
   simpleShellSetOpacity,
   simpleShellSetZOrder,
   simpleShellGetName,
   simpleShellGetStatus
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

   if ( !wstShmInit(ctx) )
   {
      ERROR("unable to create wl_shm interface");
      goto exit;
   }

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
   
   if (!wl_global_create(ctx->display, &wl_vpc_interface, 1, ctx, wstVpcBind ))
   {
      ERROR("unable to create wl_vpc interface");
      goto exit;
   }
   
   if ( !wstOutputInit(ctx) )
   {
      ERROR("unable to intialize output");
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
      int width= ctx->nestedWidth;
      int height= ctx->nestedHeight;
      
      if ( ctx->isRepeater )
      {
         width= 0;
         height= 0;
      }
      ctx->nc= WstNestedConnectionCreate( ctx, 
                                          ctx->nestedDisplayName, 
                                          width, 
                                          height,
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
      if ( ctx->nativeWindow )
      {
         argc += 2;
         strcpy( arg0, "--nativeWindow" );
         sprintf( arg1, "%p", ctx->nativeWindow );
      }
   }

   #if !( defined (WESTEROS_HAVE_WAYLAND_EGL) && \
         (defined (WESTEROS_PLATFORM_RPI) || defined (WESTEROS_PLATFORM_NEXUS)) \
       )
   // We normally don't need a renderer when acting as a nested repeater, but for platforms
   // using wayland-egl that support renderless nested composition, we need to
   // initialize the renderer so that the wayland-egl impl can add its protocols
   if ( !ctx->isRepeater )
   #endif
   {
      ctx->renderer= WstRendererCreate( ctx->rendererModule, argc, (char **)argv, ctx->display, ctx->nc );
      if ( !ctx->renderer )
      {
         ERROR("unable to initialize renderer module");
         goto exit;
      }
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
   ctx->needRepaint= true;
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
   
   if ( ctx->shm )
   {
      wstShmTerm( ctx );
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
   
   if ( ctx->output )
   {
      wstOutputTerm( ctx );
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

static void wstCompositorProcessEvents( WstCompositor *ctx )
{
   int i;
   
   pthread_mutex_lock( &ctx->mutex );
   
   if ( ctx->nc )
   {
      WstNestedConnectionReleaseRemoteBuffers( ctx->nc );
   }
   
   for( i= 0; i < ctx->eventIndex; ++i )
   {
      switch( ctx->eventQueue[i].type )
      {
         case WstEventType_key:
            {
               WstKeyboard *keyboard= ctx->seat->keyboard;
               
               if ( keyboard )
               {
                  wstProcessKeyEvent( keyboard, 
                                      ctx->eventQueue[i].v1, //keyCode
                                      ctx->eventQueue[i].v2, //keyState
                                      ctx->eventQueue[i].v3  //modifiers
                                    );
               }
            }
            break;
         case WstEventType_keyCode:
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
                                              ctx->eventQueue[i].v1,  //time
                                              ctx->eventQueue[i].v2,  //key
                                              ctx->eventQueue[i].v3   //state
                                            );
                     }   
                  }
               }
            }
            break;
         case WstEventType_keyModifiers:
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
                                                    ctx->eventQueue[i].v1, // mod depressed
                                                    ctx->eventQueue[i].v2, // mod latched
                                                    ctx->eventQueue[i].v3, // mod locked
                                                    ctx->eventQueue[i].v4  // mod group
                                                  );
                     }   
                  }
               }
            }
            break;
         case WstEventType_pointerEnter:
            {
               WstPointer *pointer= ctx->seat->pointer;
               
               if ( pointer )
               {
                  wstProcessPointerEnter( pointer,
                                          ctx->eventQueue[i].v1, //x
                                          ctx->eventQueue[i].v2, //y
                                          (struct wl_surface*)ctx->eventQueue[i].p1  //surfaceNested
                                        );
               }
            }
            break;
         case WstEventType_pointerLeave:
            {
               WstPointer *pointer= ctx->seat->pointer;
               
               if ( pointer )
               {
                  wstProcessPointerLeave( pointer,
                                          (struct wl_surface*)ctx->eventQueue[i].p1  //surfaceNested
                                        );
               }
            }
            break;
         case WstEventType_pointerMove:
            {
               WstPointer *pointer= ctx->seat->pointer;
               
               if ( pointer )
               {
                  wstProcessPointerMoveEvent( pointer, 
                                              ctx->eventQueue[i].v1, //x
                                              ctx->eventQueue[i].v2  //y
                                            );
               }
            }
            break;
         case WstEventType_pointerButton:
            {
               WstPointer *pointer= ctx->seat->pointer;
               
               uint32_t time;
               
               if ( ctx->eventQueue[i].v3 )
               {
                  time= ctx->eventQueue[i].v4;
               }
               else
               {
                  time= (uint32_t)wstGetCurrentTimeMillis();
               }
               
               if ( pointer )
               {
                  wstProcessPointerButtonEvent( pointer, 
                                                ctx->eventQueue[i].v1, //button
                                                ctx->eventQueue[i].v2, //buttonState
                                                time
                                               );
               }
            }
            break;
         default:
            WARNING("wstCompositorProcessEvents: unknown event type %d", ctx->eventQueue[i].type );
            break;
      }
   }
   ctx->eventIndex= 0;
   pthread_mutex_unlock( &ctx->mutex );
}

static void wstCompositorComposeFrame( WstCompositor *ctx, uint32_t frameTime )
{
   pthread_mutex_lock( &ctx->mutex );

   ctx->needRepaint= false;

   if ( !ctx->isEmbedded && !ctx->isRepeater )
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
   long long frameTime, now;
   int nextFrameDelay;
   
   frameTime= wstGetCurrentTimeMillis();   
   
   wstCompositorProcessEvents( ctx );
   
   if ( ctx->outputSizeChanged )
   {
      wstOutputChangeSize( ctx );
   }

   if ( ctx->dispatchCB )
   {
      ctx->dispatchCB( ctx, ctx->dispatchUserData );
   }
   
   if ( ctx->needRepaint )
   {
      wstCompositorComposeFrame( ctx, (uint32_t)frameTime );
      
      if ( ctx->invalidateCB )
      {
         ctx->invalidateCB( ctx, ctx->invalidateUserData );
      }
   }

   now= wstGetCurrentTimeMillis();
   nextFrameDelay= (ctx->framePeriodMillis-(now-frameTime));
   if ( nextFrameDelay < 1 ) nextFrameDelay= 1;

   pthread_mutex_lock( &ctx->mutex );
   wl_event_source_timer_update( ctx->displayTimer, nextFrameDelay );
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

static const struct wl_shm_interface shm_interface=
{
   wstIShmCreatePool
};

static const struct wl_shm_pool_interface shm_pool_interface= {
   wstIShmPoolCreateBuffer,
   wstIShmPoolDestroy,
   wstIShmPoolResize
};

static const struct wl_buffer_interface shm_buffer_interface = {
   wstIShmBufferDestroy
};

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

static const struct wl_vpc_interface vpc_interface_impl=
{
   wstIVpcGetVpcSurface
};

static void wstShmBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstShm *shm= (WstShm*)data;
   struct wl_resource *resource;

   DEBUG("wstShmBind: client %p data %p version %d id %d", client, data, version, id );

   resource= wl_resource_create(client, 
                                &wl_shm_interface,
                                MIN(version, 1), 
                                id);
   if (!resource) 
   {
      wl_client_post_no_memory(client);
      return;
   }

   wl_list_insert( &shm->resourceList, wl_resource_get_link(resource) );
   wl_resource_set_implementation(resource, &shm_interface, shm, wstResourceUnBindCallback);

   wl_shm_send_format(resource, WL_SHM_FORMAT_ARGB8888);
   wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
}

static bool wstShmInit( WstCompositor *ctx )
{
   bool result= false;
   int rc;
   
   /*
    * Normally we use the shm services in wayland directly.  For a repeating
    * compositor, however, we register our own shm service so we have
    * access to the shared memory fd sent by the the client which allows
    * us to forward the shared memory operations to the target compositor
    * we are connected to.
    */
   if ( ctx->isRepeater )
   {
      ctx->shm= (WstShm*)calloc( 1, sizeof(WstShm) );
      if ( ctx->shm )
      {
         ctx->shm->compositor= ctx;
         wl_list_init( &ctx->shm->resourceList );
         
         if ( wl_global_create(ctx->display, &wl_shm_interface, 1, ctx->shm, wstShmBind) )
         {
            result= true;
         }
      }
      else
      {
         ERROR("no memory to allocate compositor shm ctx");
      }
   }
   else
   {
      rc= wl_display_init_shm(ctx->display);
      if ( !rc )
      {
         result= true;
      }
   }
   
   return result;
}

static void wstShmTerm( WstCompositor *ctx )
{
   if ( ctx->isRepeater )
   {
      if ( ctx->shm )
      {
         struct wl_resource *resource;
         WstShm *shm= ctx->shm;
         
         while( !wl_list_empty( &shm->resourceList ) )
         {
            resource= wl_container_of( shm->resourceList.next, resource, link);
            wl_resource_destroy(resource);
         }
         
         free( ctx->shm );
         ctx->shm= 0;
      }
   }
}

static void wstIShmBufferDestroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void wstIShmPoolCreateBuffer( struct wl_client *client, struct wl_resource *resource,
                                     uint32_t id, int32_t offset,
                                     int32_t width, int32_t height,
                                     int32_t stride, uint32_t format)
{
   WstShmPool *pool= (WstShmPool*)wl_resource_get_user_data(resource);
   
   if ( pool )
   {
      WstShmBuffer *buffer= (WstShmBuffer*)calloc( 1, sizeof(WstShmBuffer) );
      if ( !buffer )
      {
         wl_client_post_no_memory(client);
         return;
      }
      
      buffer->pool= pool;
      buffer->bufferResource= wl_resource_create(client, &wl_buffer_interface, 1, id);
      if ( !buffer->bufferResource )
      {
         wl_client_post_no_memory(client);
         free( buffer );
         return;
      }
      
      buffer->pool->refCount++;
      
      wl_resource_set_implementation(buffer->bufferResource,
                                     &shm_buffer_interface,
                                     buffer, wstShmBufferDestroy);
      
      
      buffer->width= width;
      buffer->height= height;
      buffer->stride= stride;
      buffer->format= format;
      buffer->bufferNested= WstNestedConnectionShmPoolCreateBuffer( pool->shm->compositor->nc,
                                                                    pool->poolNested,
                                                                    offset,
                                                                    width,
                                                                    height,
                                                                    stride,
                                                                    format );
   }
}                                

void wstShmBufferDestroy( struct wl_resource *resource )
{
   WstShmBuffer *buffer= (WstShmBuffer*)wl_resource_get_user_data(resource);
   if ( buffer )
   {
      if ( buffer->bufferNested )
      {
         WstNestedConnectionShmBufferPoolDestroy( buffer->pool->shm->compositor->nc, buffer->pool->poolNested, buffer->bufferNested );
      }
      if ( buffer->pool )
      {
         wstShmPoolUnRef( buffer->pool );
      }
      free( buffer );
   }
}

static void wstIShmPoolDestroy( struct wl_client *client, struct wl_resource *resource )
{
   wl_resource_destroy(resource);
}

static void wstIShmPoolResize( struct wl_client *client, struct wl_resource *resource, int32_t size )
{
   WstShmPool *pool= (WstShmPool*)wl_resource_get_user_data(resource);
   
   if ( pool )
   {
      WstShm *shm= pool->shm;

      WstNestedConnectionShmPoolResize( shm->compositor->nc, pool->poolNested, size );         
   }
}

static void wstIShmCreatePool( struct wl_client *client, struct wl_resource *resource,
                              uint32_t id, int fd, int32_t size )
{
   WstShm *shm= (WstShm*)wl_resource_get_user_data(resource);
   
   if ( shm->compositor )
   {
      WstNestedConnection *nc;
      
      nc= shm->compositor->nc;
      if ( nc )
      {
         WstShmPool *pool= 0;
         
         pool= (WstShmPool*)calloc( 1, sizeof(WstShmPool) );
         if ( !pool )
         {
            wl_resource_post_no_memory(resource);
            return;
         }
   
         pool->shm= shm;      
         pool->refCount= 1;
         
         pool->poolResource= wl_resource_create(client, &wl_shm_pool_interface, 1, id);
         if ( !pool->poolResource )
         {
            wl_resource_post_no_memory(resource);
            free( pool );
            return;
         }

         wl_resource_set_implementation(pool->poolResource,
                                        &shm_pool_interface,
                                        pool, wstShmDestroyPool);
         
         pool->poolNested= WstNestedConnnectionShmCreatePool( nc, fd, size );
         if ( !pool->poolNested )
         {
            wl_resource_destroy( pool->poolResource );
            wl_resource_post_no_memory(resource);
            free( pool );
            return;
         }

         // Close the fd.  Each fd sent via wayland gets duplicated.         
         close( fd );
      }   
   }   
}

void wstShmDestroyPool( struct wl_resource *resource )
{
   WstShmPool *pool= (WstShmPool*)wl_resource_get_user_data(resource);
   
   if ( pool )
   {
      WstShm *shm= pool->shm;
      
      wstShmPoolUnRef( pool );
   }
}

void wstShmPoolUnRef( WstShmPool *pool )
{
   if ( pool )
   {
      --pool->refCount;
      if ( !pool->refCount )
      {
         WstShm *shm= pool->shm;
         if ( pool->poolNested )
         {
            WstNestedConnectionShmDestroyPool( shm->compositor->nc, pool->poolNested );
         }
         free( pool );
      }
   }
}
                              
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
      WstSimpleShellNotifySurfaceCreated( ctx->simpleShell, client, surface->resource, surface->surfaceId );
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

      wl_list_init(&surface->frameCallbackList);

      surface->visible= true;
      surface->opacity= 1.0;
      surface->zorder= 0.5;
      if ( ctx->isRepeater )
      {
         surface->surfaceNested= WstNestedConnectionCreateSurface( ctx->nc );
         if ( !surface->surfaceNested )
         {
            free( surface );
            surface= 0;
         }
      }
      else
      {
         surface->renderer= ctx->renderer;
         
         surface->surface= WstRendererSurfaceCreate( ctx->renderer );
         if ( !surface->surface )
         {
            free( surface );
            surface= 0;
         }

         WstRendererSurfaceGetZOrder( ctx->renderer, surface->surface, &surface->zorder );
      }
      
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

   // Release any attached buffer
   if ( surface->attachedBufferResource )
   {
      wl_buffer_send_release( surface->attachedBufferResource );
      surface->attachedBufferResource= 0;
   }

   // Remove from pointer focus
   if ( ctx->seat->pointer &&
        (ctx->seat->pointer->focus == surface) )
   {
      ctx->seat->pointer->focus= 0;
      if ( !ctx->dcDefaultCursor )
      {
         wstPointerSetPointer( ctx->seat->pointer, 0 );
      }
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

   // Cleanup any nested connection surface
   if ( surface->surfaceNested )
   {
      WstNestedConnectionDestroySurface( ctx->nc, surface->surfaceNested );
      surface->surfaceNested= 0;
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
   
   // Cleanup vpc surface
   if ( surface->vpcSurface )
   {
      if ( surface->vpcSurface->resource )
      {
         surface->vpcSurface->surface= 0;
         wl_resource_destroy( surface->vpcSurface->resource );
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
                              struct wl_resource *bufferResource, int32_t sx, int32_t sy)
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);

   if ( surface->attachedBufferResource != bufferResource )
   {
      if ( surface->attachedBufferResource )
      {
         wl_buffer_send_release( surface->attachedBufferResource );
         surface->attachedBufferResource= 0;
      }
   }
   if ( bufferResource )
   {
      surface->attachedBufferResource= bufferResource;
      surface->attachedX= sx;
      surface->attachedY= sy;
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
                                       struct wl_resource *regionResource)
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(regionResource);
   TRACE("wstISurfaceSetOpaqueRegion: not supported");
}

static void wstISurfaceSetInputRegion(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *regionResource)
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(regionResource);
   WARNING("wstISurfaceSetInputRegion: not supported");
}

static void wstISurfaceCommit(struct wl_client *client, struct wl_resource *resource)
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);
   struct wl_resource *committedBufferResource;

   pthread_mutex_lock( &surface->compositor->mutex );

   committedBufferResource= surface->attachedBufferResource;
   if ( surface->attachedBufferResource )
   {      
      if ( surface->compositor->isRepeater )
      {
         if ( wl_resource_instance_of( surface->attachedBufferResource, &wl_buffer_interface, &shm_buffer_interface ) )
         {
            WstShmBuffer *buffer= (WstShmBuffer*)wl_resource_get_user_data(surface->attachedBufferResource);
            if ( buffer )
            {
               int width, height;
               
               width= buffer->width;
               height= buffer->height;
               
               WstNestedConnectionAttachAndCommit( surface->compositor->nc,
                                                   surface->surfaceNested,
                                                   buffer->bufferNested,
                                                   0,
                                                   0,
                                                   width, 
                                                   height );
            }
         }
         #ifdef ENABLE_SBPROTOCOL
         else if ( WstSBBufferGet( surface->attachedBufferResource ) )
         {
            struct wl_sb_buffer *sbBuffer;
            void *deviceBuffer;
            
            sbBuffer= WstSBBufferGet( surface->attachedBufferResource );
            if ( sbBuffer )
            {
               struct wl_buffer *buffer;
               int width, height, stride;
               uint32_t format;

               deviceBuffer= WstSBBufferGetBuffer( sbBuffer );
               
               width= WstSBBufferGetWidth( sbBuffer );
               height= WstSBBufferGetHeight( sbBuffer );
               format= WstSBBufferGetFormat( sbBuffer );
               stride= WstSBBufferGetStride( sbBuffer );
               
               WstNestedConnectionAttachAndCommitDevice( surface->compositor->nc,
                                                   surface->surfaceNested,
                                                   0,
                                                   deviceBuffer,
                                                   format,
                                                   stride,
                                                   0,
                                                   0,
                                                   width, 
                                                   height );
            }
         }
         #endif
         #if defined (WESTEROS_HAVE_WAYLAND_EGL) && \
             (defined (WESTEROS_PLATFORM_RPI) || defined (WESTEROS_PLATFORM_NEXUS))
         #if defined (WESTEROS_PLATFORM_RPI)
         else if ( vc_dispmanx_get_handle_from_wl_buffer( surface->attachedBufferResource ) )
         #elif defined (WESTEROS_PLATFORM_NEXUS)
         else if ( wl_egl_get_device_buffer( surface->attachedBufferResource ) )
         #endif
         {
            static PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL= 0;
            static EGLDisplay eglDisplay= EGL_NO_DISPLAY;
            
            if ( !eglQueryWaylandBufferWL )
            {
               eglQueryWaylandBufferWL= (PFNEGLQUERYWAYLANDBUFFERWL)eglGetProcAddress("eglQueryWaylandBufferWL");
               eglDisplay= eglGetDisplay( EGL_DEFAULT_DISPLAY );
            }
            
            if ( eglQueryWaylandBufferWL && eglDisplay )
            {
               EGLint value;
               uint32_t format= 0;
               int stride, bufferWidth= 0, bufferHeight= 0;
               void *deviceBuffer;

               if (EGL_TRUE == eglQueryWaylandBufferWL( eglDisplay,
                                                        surface->attachedBufferResource,
                                                        EGL_WIDTH,
                                                        &value ) )
               {
                  bufferWidth= value;
               }

               if (EGL_TRUE == eglQueryWaylandBufferWL( eglDisplay,
                                                        surface->attachedBufferResource,
                                                        EGL_HEIGHT,
                                                        &value ) )
               {
                  bufferHeight= value;
               }

               if (EGL_TRUE == eglQueryWaylandBufferWL( eglDisplay,
                                                        surface->attachedBufferResource,
                                                        EGL_TEXTURE_FORMAT,
                                                        &value ) )
               {
                  switch ( value )
                  {
                     case EGL_TEXTURE_RGB:
                        format= WL_SB_FORMAT_XRGB8888;
                        stride= bufferWidth*4;
                        break;
                     case EGL_TEXTURE_RGBA:
                        format= WL_SB_FORMAT_ARGB8888;
                        stride= bufferWidth*4;
                        break;
                  }
               }
            
               #if defined (WESTEROS_PLATFORM_RPI)
               deviceBuffer= (void*)vc_dispmanx_get_handle_from_wl_buffer( surface->attachedBufferResource );
               #elif defined (WESTEROS_PLATFORM_NEXUS)
               deviceBuffer= (void*)wl_egl_get_device_buffer( surface->attachedBufferResource );
               #endif
               if ( deviceBuffer &&
                    (format != 0) &&
                    (bufferWidth != 0) &&
                    (bufferHeight != 0) )
               {
                  WstNestedConnectionAttachAndCommitDevice( surface->compositor->nc,
                                                      surface->surfaceNested,
                                                      surface->attachedBufferResource,
                                                      deviceBuffer,
                                                      format,
                                                      stride,
                                                      0,
                                                      0,
                                                      bufferWidth, 
                                                      bufferHeight );

                  surface->attachedBufferResource= 0;
               }
            }
         }
         #endif
      }
      else
      {
         WstRendererSurfaceCommit( surface->renderer, surface->surface, surface->attachedBufferResource );
      }      
   }
   else
   {
      if ( surface->compositor->isRepeater )
      {
         WstNestedConnectionAttachAndCommit( surface->compositor->nc,
                                             surface->surfaceNested,
                                             0, // null buffer
                                             0,
                                             0,
                                             0, //width
                                             0  //height
                                           );
      }
      else
      {
         WstRendererSurfaceCommit( surface->renderer, surface->surface, 0 );
      }
   }

   if ( surface->vpcSurface && surface->vpcSurface->pathTransitionPending )
   {
      if ( (committedBufferResource && !surface->vpcSurface->useHWPathNext) ||
           (!committedBufferResource && surface->vpcSurface->useHWPathNext) )
      {
         surface->vpcSurface->useHWPath= surface->vpcSurface->useHWPathNext;
         surface->vpcSurface->pathTransitionPending= false;
      }
   }

   wstCompositorScheduleRepaint( surface->compositor );

   if ( surface->attachedBufferResource )
   {      
      wl_buffer_send_release( surface->attachedBufferResource );
      surface->attachedBufferResource= 0;
   }

   pthread_mutex_unlock( &surface->compositor->mutex );
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

static bool wstOutputInit( WstCompositor *ctx )
{
   bool result= false;
   WstOutput *output= 0;
   
   ctx->output= (WstOutput*)calloc( 1, sizeof(WstOutput) );
   if ( !ctx->output )
   {
      ERROR("no memory to allocate output");
      goto exit;
   }
   output= ctx->output;

   wl_list_init( &output->resourceList );
   output->compositor= ctx;
   
   output->x= 0;
   output->y= 0;
   output->width= ctx->outputWidth;
   output->height= ctx->outputHeight;
   output->refreshRate= ctx->frameRate;
   output->mmWidth= ctx->outputWidth;
   output->mmHeight= ctx->outputHeight;
   output->subPixel= WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB;
   output->make= strdup("Westeros");
   if ( ctx->isEmbedded )
   {
      output->model= strdup("Westeros-embedded");
   }
   else
   {
      output->model= strdup("Westeros");
   }
   output->transform= WL_OUTPUT_TRANSFORM_NORMAL;
   output->currentScale= 1;
   
   if (!wl_global_create(ctx->display, &wl_output_interface, 2, output, wstOutputBind))
   {
      ERROR("unable to create wl_output interface");
      goto exit;
   }
   
   result= true;
   
exit:

   if ( !result )
   {
      wstOutputTerm( ctx );
   }
   
   return result;
}

static void wstOutputTerm( WstCompositor *ctx )
{
   if ( ctx->output )
   {
      WstOutput *output= ctx->output;
      struct wl_resource *resource;

      while( !wl_list_empty( &output->resourceList ) )
      {
         resource= wl_container_of( output->resourceList.next, resource, link);
         wl_resource_destroy(resource);
      }
      
      if ( output->make )
      {
         free( (void*)output->make );
         output->make= 0;
      }
      
      if ( output->model )
      {
         free( (void*)output->model );
         output->model= 0;
      }
      
      free( output );
      ctx->output= 0;
   }
}

static void wstOutputBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstOutput *output= (WstOutput*)data;
   struct wl_resource *resource;
   
   DEBUG("wstOutputBind: client %p data %p version %d id %d", client, data, version, id );
   
   resource= wl_resource_create(client,
                                &wl_output_interface,
                                MIN(version,2),
                                id);                                
   if (!resource) 
   {
      wl_client_post_no_memory(client);
      return;
   }

   wl_list_insert( &output->resourceList, wl_resource_get_link(resource) );
   wl_resource_set_implementation(resource, NULL, output, wstResourceUnBindCallback);
   
   wl_output_send_geometry( resource,
                            output->x,
                            output->y,
                            output->mmWidth,
                            output->mmHeight,
                            output->subPixel,
                            output->make,
                            output->model,
                            output->transform );
                  
   if ( version >= WL_OUTPUT_SCALE_SINCE_VERSION )
   {
      wl_output_send_scale( resource, output->currentScale );
   }

   wl_output_send_mode( resource,
                        WL_OUTPUT_MODE_CURRENT,
                        output->width,
                        output->height,
                        output->refreshRate );

   if ( version >= WL_OUTPUT_DONE_SINCE_VERSION )
   {
      wl_output_send_done( resource );   
   }
}

static void wstOutputChangeSize( WstCompositor *ctx )
{
   WstOutput *output= ctx->output;
   struct wl_resource *resource;
   
   ctx->outputSizeChanged= false;
   
   if ( ctx->renderer )
   {
      wstCompositorScheduleRepaint(ctx);
      ctx->renderer->outputWidth= ctx->outputWidth;
      ctx->renderer->outputHeight= ctx->outputHeight;
   }
   output->width= ctx->outputWidth;
   output->height= ctx->outputHeight;

   wl_resource_for_each( resource, &output->resourceList )
   {
      wl_output_send_mode( resource,
                           WL_OUTPUT_MODE_CURRENT,
                           output->width,
                           output->height,
                           output->refreshRate );
   }
   
   if ( ctx->isEmbedded || ctx->hasEmbeddedMaster )
   {
      for ( std::vector<WstSurface*>::iterator it= ctx->surfaces.begin(); 
            it != ctx->surfaces.end();
            ++it )
      {
         WstSurface *surface= (*it);
         
         if ( surface->roleName )
         {
            int len= strlen( surface->roleName );
            if ( (len == 11) && !strncmp( "xdg_surface", surface->roleName, len) )
            {
               wstXdgSurfaceSendConfigure( ctx, surface, XDG_SURFACE_STATE_FULLSCREEN );
            }
         }
      }
   }
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
                                     struct wl_resource *surfaceResource )
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(surfaceResource);
   WstShellSurface *shellSurface= 0;
   
   if ( !wstSurfaceSetRole( surface, "wl_shell_surface", 
                            resource, WL_DISPLAY_ERROR_INVALID_OBJECT ) )
   {
      return;
   }

   shellSurface= (WstShellSurface*)calloc(1,sizeof(WstShellSurface));
   if ( !shellSurface )
   {
      wl_resource_post_no_memory(surfaceResource);
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
                                 struct wl_resource *seatResource,
                                 uint32_t serial )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seatResource);
   WESTEROS_UNUSED(serial);
}
                                 
static void wstIShellSurfaceResize(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *seatResource,
                                   uint32_t serial,
                                   uint32_t edges )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seatResource);
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
                                         struct wl_resource *parentResource,
                                         int x, int y, uint32_t flags )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(parentResource);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   WESTEROS_UNUSED(flags);
}
                                         
static void wstIShellSurfaceSetFullscreen(struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t method,
                                          uint32_t framerate,
                                          struct wl_resource *outputResource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(method);
   WESTEROS_UNUSED(framerate);
   WESTEROS_UNUSED(outputResource);
}
                                          
static void wstIShellSurfaceSetPopup(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *seatResource,
                                     uint32_t serial,
                                     struct wl_resource *parentResource,
                                     int x, int y, uint32_t flags )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seatResource);
   WESTEROS_UNUSED(serial);
   WESTEROS_UNUSED(parentResource);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   WESTEROS_UNUSED(flags);
}
                                     
static void wstIShellSurfaceSetMaximized(struct wl_client *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *outputResource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(outputResource);
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
                                  struct wl_resource *surfaceResource )
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(surfaceResource);
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
      wl_resource_post_no_memory(surfaceResource);
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
   
   if ( surface->compositor->isEmbedded || surface->compositor->hasEmbeddedMaster )
   {
      WstCompositor *compositor= surface->compositor;
      struct wl_array states;
      uint32_t serial;
      uint32_t *entry;
      
      wl_array_init( &states );
      entry= (uint32_t*)wl_array_add( &states, sizeof(uint32_t) );
      *entry= XDG_SURFACE_STATE_FULLSCREEN;
      serial= wl_display_next_serial( compositor->display );
      xdg_surface_send_configure( shellSurface->resource,
                                  compositor->output->width,
                                  compositor->output->height,
                                  &states,
                                  serial );
                                  
      wl_array_release( &states );
   }
}

static void  wstIXdgGetXdgPopup( struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t id,
                                 struct wl_resource *surfaceResource,
                                 struct wl_resource *parentResource,
                                 struct wl_resource *seatResource,
                                 uint32_t serial,
                                 int32_t x,
                                 int32_t y
                                 #if defined ( USE_XDG_VERSION4 )
                                 ,
                                 uint32_t flags 
                                 #endif
                                )
{
   WESTEROS_UNUSED(parentResource);
   WESTEROS_UNUSED(seatResource);
   WESTEROS_UNUSED(serial);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
   #if defined ( USE_XDG_VERSION4 )
   WESTEROS_UNUSED(flags);
   #endif

   WARNING("Xdg Popup not supported");
   
   wstIXdgGetXdgSurface( client, resource, id, surfaceResource );
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
                                          struct wl_resource *parentResource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(parentResource);
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
                                               struct wl_resource *seatResource,
                                               uint32_t serial,
                                               int32_t x,
                                               int32_t y )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seatResource);
   WESTEROS_UNUSED(serial);
   WESTEROS_UNUSED(x);
   WESTEROS_UNUSED(y);
}
                                               
static void wstIXdgShellSurfaceMove( struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *seatResource,
                                     uint32_t serial )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seatResource);
   WESTEROS_UNUSED(serial);
}
                                     
static void wstIXdgShellSurfaceResize( struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *seatResource,
                                       uint32_t serial,
                                       uint32_t edges )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(seatResource);
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
                                              struct wl_resource *outputResource )
{
   WESTEROS_UNUSED(client);
   WESTEROS_UNUSED(resource);
   WESTEROS_UNUSED(outputResource);
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

static void wstXdgSurfaceSendConfigure( WstCompositor *ctx, WstSurface *surface, uint32_t state )
{
   struct wl_array states;
   uint32_t serial;
   uint32_t *entry;
   
   wl_array_init( &states );
   entry= (uint32_t*)wl_array_add( &states, sizeof(uint32_t) );
   *entry= state;
   serial= wl_display_next_serial( ctx->display );

   for ( std::vector<WstShellSurface*>::iterator it= surface->shellSurface.begin(); 
         it != surface->shellSurface.end();
         ++it )
   {
      WstShellSurface *shellSurface= (*it);
      
      xdg_surface_send_configure( shellSurface->resource,
                                  ctx->output->width,
                                  ctx->output->height,
                                  &states,
                                  serial );
   }
                               
   wl_array_release( &states );               
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

static void wstDefaultNestedOutputHandleGeometry( void *userData, int32_t x, int32_t y, int32_t mmWidth,
                                                  int32_t mmHeight, int32_t subPixel, const char *make, 
                                                  const char *model, int32_t transform )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   
   if ( ctx )
   {
      if ( ctx->outputNestedListener )
      {
         ctx->outputNestedListener->outputHandleGeometry( ctx->outputNestedListenerUserData,
                                                          x, y, mmWidth, mmHeight, subPixel,
                                                          make, model, transform );
      }
      else
      {
         if ( ctx->output )
         {
            WstOutput *output= ctx->output;
            
            pthread_mutex_lock( &ctx->mutex );
            output->x= x;
            output->y= y;
            output->mmWidth= mmWidth;
            output->mmHeight= mmHeight;
            output->subPixel= subPixel;
            if ( output->make )
            {
               free( (void*)output->make );         
            }
            output->make= strdup(make);
            if ( output->model )
            {
               free( (void*)output->model );         
            }
            output->model= strdup(model);
            output->transform= transform;
            
            if ( strstr( output->model, "embedded" ) )
            {
               ctx->hasEmbeddedMaster= true;
            }
            pthread_mutex_unlock( &ctx->mutex );
         }
      }
   }
}

static void wstDefaultNestedOutputHandleMode( void* userData, uint32_t flags, int32_t width, 
                                              int32_t height, int32_t refreshRate )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   
   if ( ctx )
   {
      if ( ctx->outputNestedListener )
      {
         ctx->outputNestedListener->outputHandleMode( ctx->outputNestedListenerUserData,
                                                      flags, width, height, refreshRate );
      }
      else
      {
         if ( ctx->output )
         {
            WstOutput *output= ctx->output;
            
            pthread_mutex_lock( &ctx->mutex );
            ctx->outputWidth= width;
            ctx->outputHeight= height;
            
            output->width= width;
            output->height= height;
            output->refreshRate= refreshRate;
            
            ctx->outputSizeChanged= true;
            pthread_mutex_unlock( &ctx->mutex );
         }
      }
   }
}

static void wstDefaultNestedOutputHandleDone( void *userData )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   if ( ctx )
   {
      if ( ctx->outputNestedListener )
      {
         ctx->outputNestedListener->outputHandleDone( ctx->outputNestedListenerUserData );
      }
      else
      {
         // Nothing to do
      }
   }
}

static void wstDefaultNestedOutputHandleScale( void *userData, int32_t scale )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   
   if ( ctx )
   {
      if ( ctx->outputNestedListener )
      {
         ctx->outputNestedListener->outputHandleScale( ctx->outputNestedListenerUserData, scale );
      }
      else
      {
         if ( ctx->output )
         {
            WstOutput *output= ctx->output;
            
            pthread_mutex_lock( &ctx->mutex );
            output->currentScale= scale;
            pthread_mutex_unlock( &ctx->mutex );
         }
      }
   }
}

static void wstDefaultNestedKeyboardHandleKeyMap( void *userData, uint32_t format, int fd, uint32_t size )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      ctx->xkbKeymapFormat= format;
      ctx->xkbKeymapFd= fd;
      ctx->xkbKeymapSize= size;
      pthread_mutex_unlock( &ctx->mutex );

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
            pthread_mutex_lock( &ctx->mutex );
            wl_array_copy( &ctx->seat->keyboard->keys, keys );
            pthread_mutex_unlock( &ctx->mutex );
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
         int eventIndex= ctx->eventIndex;
         pthread_mutex_lock( &ctx->mutex );
         ctx->eventQueue[eventIndex].type= WstEventType_keyCode;
         ctx->eventQueue[eventIndex].v1= time;
         ctx->eventQueue[eventIndex].v2= key;
         ctx->eventQueue[eventIndex].v3= state;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );         
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
         int eventIndex= ctx->eventIndex;
         pthread_mutex_lock( &ctx->mutex );
         ctx->eventQueue[eventIndex].type= WstEventType_keyCode;
         ctx->eventQueue[eventIndex].v1= mods_depressed;
         ctx->eventQueue[eventIndex].v2= mods_latched;
         ctx->eventQueue[eventIndex].v3= mods_locked;
         ctx->eventQueue[eventIndex].v4= group;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
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
            pthread_mutex_lock( &ctx->mutex );
            ctx->seat->keyRepeatDelay;
            ctx->seat->keyRepeatRate;
            pthread_mutex_unlock( &ctx->mutex );
         }
      }
   }                       
}

static void wstDefaultNestedPointerHandleEnter( void *userData, struct wl_surface *surfaceNested, wl_fixed_t sx, wl_fixed_t sy )
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
         int x, y;
         int eventIndex= ctx->eventIndex;
         
         x= wl_fixed_to_int( sx );
         y= wl_fixed_to_int( sy );

         pthread_mutex_lock( &ctx->mutex );
         ctx->eventQueue[eventIndex].type= WstEventType_pointerEnter;
         ctx->eventQueue[eventIndex].v1= x;
         ctx->eventQueue[eventIndex].v2= y;
         ctx->eventQueue[eventIndex].p1= surfaceNested;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedPointerHandleLeave( void *userData, struct wl_surface *surfaceNested )
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
         int eventIndex= ctx->eventIndex;
         pthread_mutex_lock( &ctx->mutex );
         ctx->eventQueue[eventIndex].type= WstEventType_pointerLeave;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
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
         int x, y;
         int eventIndex= ctx->eventIndex;
         
         x= wl_fixed_to_int( sx );
         y= wl_fixed_to_int( sy );

         pthread_mutex_lock( &ctx->mutex );
         ctx->eventQueue[eventIndex].type= WstEventType_pointerMove;
         ctx->eventQueue[eventIndex].v1= x;
         ctx->eventQueue[eventIndex].v2= y;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
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
         int eventIndex= ctx->eventIndex;
         pthread_mutex_lock( &ctx->mutex );
         ctx->eventQueue[eventIndex].type= WstEventType_pointerButton;
         ctx->eventQueue[eventIndex].v1= button;
         ctx->eventQueue[eventIndex].v2= state;
         ctx->eventQueue[eventIndex].v3= 1; // have time
         ctx->eventQueue[eventIndex].v4= time;
         
         ++ctx->eventIndex;
         assert( ctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
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

static void wstDefaultNestedShmFormat( void *userData, uint32_t format )
{
   WstCompositor *ctx= (WstCompositor*)userData;

   if ( ctx && ctx->shm )
   {
      struct wl_resource *resource;
      
      wl_resource_for_each( resource, &ctx->shm->resourceList )
      {
         wl_shm_send_format(resource, format);
      }   
   }
}

static void wstDefaultNestedVpcVideoPathChange( void *userData, struct wl_surface *surfaceNested, uint32_t newVideoPath )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   WstSurface *surface= 0;
   
   for( int i= 0; i < ctx->surfaces.size(); ++i )
   {
      WstSurface *surface= ctx->surfaces[i];
      if ( surface->surfaceNested == surfaceNested )
      {
         WstVpcSurface *vpcSurface= surface->vpcSurface;
         if ( vpcSurface )
         {
            wl_vpc_surface_send_video_path_change( vpcSurface->resource, newVideoPath ); 
         }
         break;
      }
   }
}

static void wstDefaultNestedVpcVideoXformChange( void *userData, 
                                                 struct wl_surface *surfaceNested,
                                                 int32_t x_translation,
                                                 int32_t y_translation,
                                                 uint32_t x_scale_num,
                                                 uint32_t x_scale_denom,
                                                 uint32_t y_scale_num,
                                                 uint32_t y_scale_denom )
{
   WstCompositor *ctx= (WstCompositor*)userData;
   WstSurface *surface= 0;
   
   for( int i= 0; i < ctx->surfaces.size(); ++i )
   {
      WstSurface *surface= ctx->surfaces[i];
      if ( surface->surfaceNested == surfaceNested )
      {
         WstVpcSurface *vpcSurface= surface->vpcSurface;
         if ( vpcSurface )
         {
            wl_vpc_surface_send_video_xform_change( vpcSurface->resource,
                                                    x_translation,
                                                    y_translation,
                                                    x_scale_num,
                                                    x_scale_denom,
                                                    y_scale_num,
                                                    y_scale_denom );                                                 
         }
         break;
      }
   }
}                                                 

static void wstSetDefaultNestedListener( WstCompositor *ctx )
{
   ctx->nestedListenerUserData= ctx;
   ctx->nestedListener.connectionEnded= wstDefaultNestedConnectionEnded;
   ctx->nestedListener.outputHandleGeometry= wstDefaultNestedOutputHandleGeometry;
   ctx->nestedListener.outputHandleMode= wstDefaultNestedOutputHandleMode;
   ctx->nestedListener.outputHandleDone= wstDefaultNestedOutputHandleDone;
   ctx->nestedListener.outputHandleScale= wstDefaultNestedOutputHandleScale;
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
   ctx->nestedListener.shmFormat= wstDefaultNestedShmFormat;
   ctx->nestedListener.vpcVideoPathChange= wstDefaultNestedVpcVideoPathChange;
   ctx->nestedListener.vpcVideoXformChange= wstDefaultNestedVpcVideoXformChange;
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
      struct wl_resource *surfaceResource= 0;
      
      if ( seat->compositor->clientInfoMap.size() > 0 )
      {
         WstCompositor *compositor= seat->compositor;
         for( std::map<struct wl_client*,WstClientInfo*>::iterator it= compositor->clientInfoMap.begin(); 
              it != compositor->clientInfoMap.end(); ++it )
         {
            if ( it->first == client )
            {
               WstSurface *surface;
               
               surface= compositor->clientInfoMap[client]->surface;
               if ( surface )
               {
                  uint32_t serial;
                  
                  serial= wl_display_next_serial( compositor->display );
                  surfaceResource= surface->resource;
                                 
                  wl_keyboard_send_enter( resourceKbd,
                                          serial,
                                          surfaceResource,
                                          &keyboard->keys );

                  if ( !compositor->isNested )
                  {
                     wstKeyboardSendModifiers( keyboard, resourceKbd );
                  }
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
                                  struct wl_resource *surfaceResource,
                                  int32_t hotspot_x,
                                  int32_t hotspot_y )
{   
   WESTEROS_UNUSED(serial);
   
   WstPointer *pointer= (WstPointer*)wl_resource_get_user_data(resource);
   WstCompositor *compositor= pointer->seat->compositor;
   WstSurface *surface= 0;
   bool hidePointer= false;
   int pid= 0;

   if ( surfaceResource )
   {
      if ( pointer->focus &&
           (wl_resource_get_client( pointer->focus->resource ) != client) )
      {
         return;
      }
      surface= (WstSurface*)wl_resource_get_user_data(surfaceResource);
   }
   
   if ( surface )
   {
      if ( !wstSurfaceSetRole( surface, "wl_pointer-cursor", 
                               resource, WL_DISPLAY_ERROR_INVALID_OBJECT ) )
      {
         return;
      }
   }
   
   if ( compositor->isRepeater )
   {
      WstNestedConnectionPointerSetCursor( compositor->nc, 
                                           surface ? surface->surfaceNested : NULL, 
                                           hotspot_x, hotspot_y );
   }
   else
   {
      wl_client_get_credentials( client, &pid, NULL, NULL );
   
      if ( compositor->allowModifyCursor || (pid == compositor->dcPid) )
      {
         if ( pid == compositor->dcPid )
         {
            compositor->dcClient= client;
            compositor->dcDefaultCursor= true;
         }
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
}
                                  
static void wstIPointerRelease( struct wl_client *client, struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);

   WstPointer *pointer= (WstPointer*)wl_resource_get_user_data(resource);
   WstCompositor *compositor= pointer->seat->compositor;

   wl_resource_destroy(resource);
}

static void wstVpcBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstCompositor *ctx= (WstCompositor*)data;
   struct wl_resource *resource;

   resource= wl_resource_create(client, 
                                &wl_vpc_interface,
                                MIN(version, 1), 
                                id);
   if (!resource)
   {
      wl_client_post_no_memory(client);
      return;
   }

   wl_resource_set_implementation(resource, &vpc_interface_impl, ctx, NULL);
}

static void wstIVpcGetVpcSurface( struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surfaceResource )
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(surfaceResource);
   WstVpcSurface *vpcSurface= 0;

   vpcSurface= (WstVpcSurface*)calloc(1,sizeof(WstVpcSurface));
   if ( !vpcSurface )
   {
      wl_resource_post_no_memory(surfaceResource);
      return;
   }
   
   vpcSurface->resource= wl_resource_create(client,
                                            &wl_vpc_surface_interface, 1, id);
   if (!vpcSurface->resource) 
   {
      free(vpcSurface);
      wl_client_post_no_memory(client);
      return;
   }
   
   wl_resource_set_implementation(vpcSurface->resource,
                                  NULL,
                                  vpcSurface, wstDestroyVpcSurfaceCallback );
   
   vpcSurface->surface= surface;
   vpcSurface->useHWPath= true;
   vpcSurface->useHWPathNext= true;
   vpcSurface->pathTransitionPending= false;
   vpcSurface->xScaleNum= 1;
   vpcSurface->xScaleDenom= 1;
   vpcSurface->yScaleNum= 1;
   vpcSurface->yScaleDenom= 1;
   vpcSurface->compositor= surface->compositor;
   surface->compositor->vpcSurfaces.push_back( vpcSurface );
   
   surface->vpcSurface= vpcSurface;
   
   if ( surface->compositor->isRepeater )
   {
      vpcSurface->vpcSurfaceNested= WstNestedConnectionGetVpcSurface( surface->compositor->nc,
                                                                      surface->surfaceNested );
   }
   wstCompositorScheduleRepaint( surface->compositor );
}

static void wstDestroyVpcSurfaceCallback(struct wl_resource *resource)
{
   WstVpcSurface *vpcSurface= (WstVpcSurface*)wl_resource_get_user_data(resource);

   vpcSurface->resource= NULL;
   wstVpcSurfaceDestroy(vpcSurface);
}

static void wstVpcSurfaceDestroy( WstVpcSurface *vpcSurface )
{
   if ( vpcSurface->compositor )
   {
      WstCompositor *ctx= vpcSurface->compositor;

      if ( vpcSurface->vpcSurfaceNested )
      {
         WstNestedConnectionDestroyVpcSurface( ctx->nc, vpcSurface->vpcSurfaceNested );
         vpcSurface->vpcSurfaceNested= 0;
      }
      
      // Remove from vpc surface list
      for ( std::vector<WstVpcSurface*>::iterator it= ctx->vpcSurfaces.begin(); 
            it != ctx->vpcSurfaces.end();
            ++it )
      {
         if ( vpcSurface == (*it) )
         {
            ctx->vpcSurfaces.erase(it);
            break;
         }
      }
   }

   if ( vpcSurface->surface )
   {
      vpcSurface->surface->vpcSurface= 0;
   }
   
   assert(vpcSurface->resource == NULL);
   
   free( vpcSurface );
}

static void wstUpdateVPCSurfaces( WstCompositor *ctx, std::vector<WstRect> &rects )
{
   bool useHWPath= ctx->renderer->fastHint;
   int scaleXNum= ctx->renderer->matrix[0]*1000000;
   int scaleXDenom= 1000000; 
   int scaleYNum= ctx->renderer->matrix[5]*1000000;
   int scaleYDenom= 1000000;
   int transX= (int)ctx->renderer->matrix[12];
   int transY= (int)ctx->renderer->matrix[13];
   
   for ( std::vector<WstVpcSurface*>::iterator it= ctx->vpcSurfaces.begin(); 
         it != ctx->vpcSurfaces.end();
         ++it )
   {
      WstVpcSurface *vpcSurface= (*it);
      WstRect rect;

      if ( (transX != vpcSurface->xTrans) ||
           (transY != vpcSurface->yTrans) ||
           (scaleXNum != vpcSurface->xScaleNum) ||
           (scaleXDenom != vpcSurface->xScaleDenom) ||
           (scaleYNum != vpcSurface->yScaleNum) ||
           (scaleYDenom != vpcSurface->yScaleDenom) )
      {
         vpcSurface->xTrans= transX;
         vpcSurface->yTrans= transY;
         vpcSurface->xScaleNum= scaleXNum;
         vpcSurface->xScaleDenom= scaleXDenom;
         vpcSurface->yScaleNum= scaleYNum;
         vpcSurface->yScaleDenom= scaleYDenom;
         
         wl_vpc_surface_send_video_xform_change( vpcSurface->resource,
                                                 vpcSurface->xTrans,
                                                 vpcSurface->yTrans,
                                                 vpcSurface->xScaleNum,
                                                 vpcSurface->xScaleDenom,
                                                 vpcSurface->yScaleNum,
                                                 vpcSurface->yScaleDenom );                                                 
      }
      
      if ( useHWPath != vpcSurface->useHWPath )
      {
         DEBUG("vpcSurface %p useHWPath %d\n", vpcSurface, useHWPath );
         vpcSurface->useHWPathNext= useHWPath;
         vpcSurface->pathTransitionPending= true;
         wl_vpc_surface_send_video_path_change( vpcSurface->resource, 
                                                useHWPath ? WL_VPC_SURFACE_PATHWAY_HARDWARE
                                                          : WL_VPC_SURFACE_PATHWAY_GRAPHICS );
      }

      if ( vpcSurface->useHWPath )
      {      
         rect.x= (ctx->output->x+vpcSurface->surface->x)*ctx->renderer->matrix[0]+(transX-ctx->output->x);
         rect.y= (ctx->output->y+vpcSurface->surface->y)*ctx->renderer->matrix[5]+(transY-ctx->output->y);
         rect.width= vpcSurface->surface->width*ctx->renderer->matrix[0];
         rect.height= vpcSurface->surface->height*ctx->renderer->matrix[5];
         rects.push_back(rect);
      }
   }
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
               "Error.  Unable to create write xkb keymap string to temp file" );
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
      wstRemoveTempFile( ctx->xkbKeymapFd );
      
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

static void wstProcessPointerEnter( WstPointer *pointer, int x, int y, struct wl_surface *surfaceNested )
{
   WstCompositor *ctx= pointer->seat->compositor;
   
   if ( ctx->isNested )
   {
      if ( ctx->seat )
      {
         if ( ctx->isRepeater )
         {
            WstSurface *surface= 0;
            
            for( int i= 0; i < ctx->surfaces.size(); ++i )
            {
               if ( ctx->surfaces[i]->surfaceNested == surfaceNested )
               {
                  surface= ctx->surfaces[i];
                  break;
               }
            }
            if ( surface )
            {
               uint32_t serial;
               struct wl_resource *resource;
               struct wl_client *surfaceClient;
               
               pointer->focus= surface;
               
               surfaceClient= wl_resource_get_client( pointer->focus->resource );
               wstPointerMoveFocusToClient( pointer, surfaceClient );

               serial= wl_display_next_serial( ctx->display );
               wl_resource_for_each( resource, &pointer->focusResourceList )
               {
                  wl_pointer_send_enter( resource, serial, pointer->focus->resource, x, y );
               }
            }
         }
         else
         {
            pointer->entered= true;
            
            pointer->pointerX= x;
            pointer->pointerY= y;
            
            wstPointerCheckFocus( pointer, x, y );
         }
      }
   }
   else
   {
      pointer->entered= true;
   }
}

static void wstProcessPointerLeave( WstPointer *pointer, struct wl_surface *surfaceNested )
{
   WstCompositor *ctx= pointer->seat->compositor;

   if ( ctx->isNested && ctx->isRepeater )
   {
      if ( ctx->seat )
      {
         WstSurface *surface= 0;
         
         for( int i= 0; i < ctx->surfaces.size(); ++i )
         {
            if ( ctx->surfaces[i]->surfaceNested == surfaceNested )
            {
               surface= ctx->surfaces[i];
               break;
            }
         }
         if ( surface )
         {
            uint32_t serial;
            struct wl_resource *resource;
            struct wl_client *surfaceClient;

            serial= wl_display_next_serial( ctx->display );
            surfaceClient= wl_resource_get_client( pointer->focus->resource );
            wl_resource_for_each( resource, &pointer->focusResourceList )
            {
               wl_pointer_send_leave( resource, serial, pointer->focus->resource );
            }
            pointer->focus= 0;
         }
      }
   }
   else if ( !ctx->dcDefaultCursor )
   {
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
      
      if ( ctx->invalidateCB )
      {
         ctx->invalidateCB( ctx, ctx->invalidateUserData );
      }

      pointer->entered= false;
   }
}

static void wstProcessPointerMoveEvent( WstPointer *pointer, int32_t x, int32_t y )
{
   WstCompositor *compositor= pointer->seat->compositor;
   WstSurface *surface= 0;
   int sx, sy, sw, sh;
   uint32_t time;
   struct wl_resource *resource;

   if ( compositor->isRepeater )
   {
      wl_fixed_t xFixed, yFixed;

      xFixed= wl_fixed_from_int( x );
      yFixed= wl_fixed_from_int( y );
      
      time= (uint32_t)wstGetCurrentTimeMillis();
      wl_resource_for_each( resource, &pointer->focusResourceList )
      {
         wl_pointer_send_motion( resource, time, xFixed, yFixed );
      }
   }
   else
   {
      pointer->pointerX= x;
      pointer->pointerY= y;
      
      wstPointerCheckFocus( pointer, x, y );
         
      if ( pointer->focus || compositor->dcDefaultCursor )
      {
         if ( pointer->focus )
         {
            wl_fixed_t xFixed, yFixed;

            WstRendererSurfaceGetGeometry( compositor->renderer, pointer->focus->surface, &sx, &sy, &sw, &sh );
            
            xFixed= wl_fixed_from_int( x-sx );
            yFixed= wl_fixed_from_int( y-sy );
            
            time= (uint32_t)wstGetCurrentTimeMillis();
            wl_resource_for_each( resource, &pointer->focusResourceList )
            {
               wl_pointer_send_motion( resource, time, xFixed, yFixed );
            }
         }
         
         if ( pointer->pointerSurface )
         {
            wstPointerUpdatePosition( pointer );
         }
         
         wstCompositorScheduleRepaint( compositor );
      }
   }
}

static void wstProcessPointerButtonEvent( WstPointer *pointer, uint32_t button, uint32_t buttonState, uint32_t time )
{
   WstCompositor *compositor= pointer->seat->compositor;
   uint32_t serial, btnState;
   struct wl_resource *resource;
   
   if ( pointer->focus )
   {
      serial= wl_display_next_serial( compositor->display );
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
   
   if ( !compositor->isRepeater )
   {
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
            int len= strlen(surface->roleName );
            if ( !((len == 17) && !strncmp( surface->roleName, "wl_pointer-cursor", len )) )
            {
               haveRoles= true;
            }
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
      else if ( !compositor->dcDefaultCursor )
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

static void wstRemoveTempFile( int fd )
{
   int pid= getpid();
   int len, prefixlen;
   char path[32];
   char link[256];
   bool haveTempFilename= false;
   
   prefixlen= strlen(TEMPFILE_PREFIX);
   sprintf(path, "/proc/%d/fd/%d", pid, fd );
   len= readlink( path, link, sizeof(link)-1 );
   if ( len > prefixlen )
   {
      link[len]= '\0';
      if ( strncmp( link, TEMPFILE_PREFIX, prefixlen ) == 0 )
      {
         haveTempFilename= true;
      }
   }
   
   close( fd );
   
   if ( haveTempFilename )
   {
      DEBUG( "removing tempory file (%s)", link );
      remove( link );
   }
}

static void dcSeatCapabilities( void *data, struct wl_seat *seat, uint32_t capabilities )
{
   WstCompositor *compositor= (WstCompositor*)data;

   printf("seat %p caps: %X\n", seat, capabilities );
   if ( capabilities & WL_SEAT_CAPABILITY_POINTER )
   {
      printf("  seat has pointer\n");
      compositor->dcPointer= wl_seat_get_pointer( compositor->dcSeat );
      printf("  pointer %p\n", compositor->dcPointer );
   }
}

static void dcSeatName( void *data, struct wl_seat *seat, const char *name )
{
   WESTEROS_UNUSED( data );
   WESTEROS_UNUSED( seat );
   WESTEROS_UNUSED( name );
}

static const struct wl_seat_listener dcSeatListener = {
   dcSeatCapabilities,
   dcSeatName 
};

static void dcRegistryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
                                 const char *interface, uint32_t version)
{
   WstCompositor *compositor= (WstCompositor*)data;
   int len;

   len= strlen(interface);
   if ( (len==6) && !strncmp(interface, "wl_shm", len)) {
      compositor->dcShm= (struct wl_shm*)wl_registry_bind(registry, id, &wl_shm_interface, 1);
   }
   else if ( (len==13) && !strncmp(interface, "wl_compositor", len) ) {
      compositor->dcCompositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
   } 
   else if ( (len==7) && !strncmp(interface, "wl_seat", len) ) {
      compositor->dcSeat= (struct wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 4);
      wl_seat_add_listener(compositor->dcSeat, &dcSeatListener, compositor);
   } 
}

static void dcRegistryHandleGlobalRemove(void *data, 
                                       struct wl_registry *registry,
                                       uint32_t name)
{
   WESTEROS_UNUSED(data);
   WESTEROS_UNUSED(registry);
   WESTEROS_UNUSED(name);
}

static const struct wl_registry_listener dcRegistryListener = 
{
   dcRegistryHandleGlobal,
   dcRegistryHandleGlobalRemove
};

static bool wstInitializeDefaultCursor( WstCompositor *compositor, 
                                        unsigned char *imgData, int width, int height,
                                        int hotspotX, int hotspotY  )
{
   bool result= false;

   int pid= fork();
   if ( pid == 0 )
   {
      struct wl_buffer *buffer= 0;
      int imgDataSize;
      char filename[32];
      int fd;
      int lenDidWrite;
      void *data;

      compositor->dcDisplay= wl_display_connect( compositor->displayName );
      if ( !compositor->display )
      {
         goto exit;
      }
      
      compositor->dcRegistry= wl_display_get_registry(compositor->dcDisplay);
      if ( !compositor->dcRegistry )
      {
         goto exit;
      }

      wl_registry_add_listener(compositor->dcRegistry, &dcRegistryListener, compositor);
      wl_display_roundtrip(compositor->dcDisplay);

      if ( !compositor->dcCompositor ||
           !compositor->dcShm ||
           !compositor->dcSeat )
      {
         goto exit;
      }
      
      wl_display_roundtrip(compositor->dcDisplay);
      
      if ( !compositor->dcPointer )
      {
         goto exit;
      }
      
      compositor->dcCursorSurface= wl_compositor_create_surface(compositor->dcCompositor);
      wl_display_roundtrip(compositor->dcDisplay);
      if ( !compositor->dcCursorSurface )
      {
         goto exit;
      }
      
      imgDataSize= 512+height*width*4;

      strcpy( filename, "/tmp/westeros-XXXXXX" );
      fd= mkostemp( filename, O_CLOEXEC );
      if ( fd < 0 )
      {
         goto exit;
      }

      lenDidWrite= write( fd, imgData, imgDataSize );
      if ( lenDidWrite != imgDataSize )
      {
         sprintf( compositor->lastErrorDetail,
                  "Error.  Unable to create write default cursor img data to temp file" );
         goto exit;      
      }

      data = mmap(NULL, imgDataSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if ( data == MAP_FAILED ) 
      {
         goto exit;
      }
   
      compositor->dcPoolData= data;
      compositor->dcPoolSize= imgDataSize;
      compositor->dcPoolFd= fd;
      memcpy( data, imgData, height*width*4 );
      
      compositor->dcPool= wl_shm_create_pool(compositor->dcShm, fd, imgDataSize);
      wl_display_roundtrip(compositor->dcDisplay);

      buffer= wl_shm_pool_create_buffer(compositor->dcPool, 
                                        0, //offset
                                        width,
                                        height,
                                        width*4, //stride
                                        WL_SHM_FORMAT_ARGB8888 );
      wl_display_roundtrip(compositor->dcDisplay);
      if ( !buffer )
      {
         goto exit;
      }
      
      wl_surface_attach( compositor->dcCursorSurface, buffer, 0, 0 );
      wl_surface_damage( compositor->dcCursorSurface, 0, 0, width, height);
      wl_surface_commit( compositor->dcCursorSurface );
      wl_pointer_set_cursor( compositor->dcPointer, 
                             0,
                             compositor->dcCursorSurface,
                             hotspotX, hotspotY );
      wl_display_roundtrip(compositor->dcDisplay);

      wl_display_dispatch( compositor->dcDisplay );         
      
   exit:

      if ( !result )
      {
         wstTerminateDefaultCursor( compositor );
      }
      
      exit(0);
   }
   else if ( pid < 0 )
   {
      sprintf( compositor->lastErrorDetail,
               "Error.  Unable to fork process" );
   }
   else
   {
      compositor->dcPid= pid;
      DEBUG("default cursor client spawned: pid %d\n", pid );
      result= true;
   }
   
   return result;
}

static void wstTerminateDefaultCursor( WstCompositor *compositor )
{
   if ( compositor->dcCursorSurface )
   {
      wl_pointer_set_cursor( compositor->dcPointer, 
                             0,
                             NULL,
                             0, 0 );
      wl_surface_destroy( compositor->dcCursorSurface );
      compositor->dcCursorSurface= 0;
   }
   
   if ( compositor->dcPool )
   {
      wl_shm_pool_destroy( compositor->dcPool);
      compositor->dcPool= 0;
   }
   
   if ( compositor->dcPoolData )
   {
      munmap( compositor->dcPoolData, compositor->dcPoolSize );
      compositor->dcPoolData= 0;
   }
   
   if ( compositor->dcPoolFd >= 0)
   {
      wstRemoveTempFile( compositor->dcPoolFd );
      compositor->dcPoolFd= -1;
   }
   
   if ( compositor->dcPointer )
   {
      wl_pointer_destroy( compositor->dcPointer );
      compositor->dcPointer= 0;
   }
   
   if ( compositor->dcShm )
   {
      wl_shm_destroy( compositor->dcShm );
      compositor->dcShm= 0;
   }
   
   if ( compositor->dcCompositor )
   {
      wl_compositor_destroy( compositor->dcCompositor );
      compositor->dcCompositor= 0;
   }

   if ( compositor->dcRegistry )
   {
      wl_registry_destroy( compositor->dcRegistry );
      compositor->dcRegistry= 0;
   }
   
   if ( compositor->dcDisplay )
   {
      wl_display_disconnect( compositor->dcDisplay );
      compositor->dcDisplay= 0;
   }
}

