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
#include <string.h>
#include <limits.h>
#include <memory.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <dirent.h>
#include <dlfcn.h>
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
#include "vpc-client-protocol.h"
#include "vpc-server-protocol.h"

#include "westeros-version.h"

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

#define VPCBRIDGE_SIGNAL (INT_MIN)

typedef void* (*PFNGETDEVICEBUFFERFROMRESOURCE)( struct wl_resource *resource );
typedef bool (*PFNREMOTEBEGIN)( struct wl_display *dspsrc, struct wl_display *dspdst );
typedef void (*PFNREMOTEEND)( struct wl_display *dspsrc, struct wl_display *dspdst );
typedef struct wl_buffer* (*PFNREMOTECLONEBUFFERFROMRESOURCE)( struct wl_display *dspsrc,
                                                               struct wl_resource *resource,
                                                               struct wl_display *dspdst,
                                                               int *width,
                                                               int *height );

typedef enum _WstEventType
{
   WstEventType_key,
   WstEventType_keyCode,
   WstEventType_keyModifiers,
   WstEventType_pointerEnter,
   WstEventType_pointerLeave,
   WstEventType_pointerMove,
   WstEventType_pointerButton,
   WstEventType_touchDown,
   WstEventType_touchUp,
   WstEventType_touchMotion,
   WstEventType_touchFrame,
   WstEventType_resolutionChangeBegin,
   WstEventType_resolutionChangeEnd
} WstEventType;

typedef enum
{
   WstPipeDescriptor_ParentRead = 0,
   WstPipeDescriptor_ChildWrite = 1
} WstPipeDescriptor;

typedef struct _WstEvent
{
   WstEventType type;
   void *p1;
   unsigned int v1;
   unsigned int v2;
   unsigned int v3;
   unsigned int v4;
} WstEvent;

typedef struct _WstContext WstContext;
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
   bool videoPathSet;
   bool useHWPath;
   bool useHWPathNext;
   bool pathTransitionPending;
   bool sizeOverride;
   int xTrans;
   int yTrans;
   int xScaleNum;
   int xScaleDenom;
   int yScaleNum;
   int yScaleDenom;
   int outputWidth;
   int outputHeight;
   int hwX;
   int hwY;
   int hwWidth;
   int hwHeight;

   bool useHWPathVpcBridge;
   int xTransVpcBridge;
   int yTransVpcBridge;
   int xScaleNumVpcBridge;
   int xScaleDenomVpcBridge;
   int yScaleNumVpcBridge;
   int yScaleDenomVpcBridge;
   int outputWidthVpcBridge;
   int outputHeightVpcBridge;
} WstVpcSurface;

typedef struct _WstKeyboard
{
   WstSeat *seat;
   WstCompositor *compositor;
   struct wl_list resourceList;
   struct wl_list focusResourceList;
   struct wl_array keys;
   WstSurface *focus;

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
   WstCompositor *compositor;
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

typedef struct _WstTouch
{
   WstSeat *seat;
   WstCompositor *compositor;
   struct wl_list resourceList;
   struct wl_list focusResourceList;
   WstSurface *focus;
} WstTouch;

typedef struct _WstSeat
{
   WstContext *ctx;
   struct wl_list resourceList;
   const char *seatName;
   int keyRepeatDelay;
   int keyRepeatRate;
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
   WstContext *ctx;
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
   bool tempVisible;

   const char *name;   
   int refCount;
   
   struct wl_resource *attachedBufferResource;
   struct wl_resource *detachedBufferResource;
   int attachedX;
   int attachedY;
   bool vpcBridgeSignal;
   
   struct wl_list frameCallbackList;
   struct wl_listener attachedBufferDestroyListener;
   struct wl_listener detachedBufferDestroyListener;
   
} WstSurface;

typedef struct _WstSurfaceInfo
{
   WstSurface *surface;
} WstSurfaceInfo;

typedef struct _WstClientInfo
{
   struct wl_resource *sbResource;
   WstSurface *surface;
   WstCompositor *wctx;
   bool usesXdgShell;
} WstClientInfo;

typedef struct _WstOutput
{
   WstContext *ctx;
   struct wl_list resourceList;
   int x;
   int y;
   int refreshRate;
   int mmWidth;
   int mmHeight;
   int subPixel;
   const char *make;
   const char *model;
   int transform;
   int currentScale;
   
} WstOutput;

typedef bool (*WstModuleInit)( WstCompositor *wctx, struct wl_display* );
typedef void (*WstModuleTerm)( WstCompositor *wctx );

typedef struct _WstModule
{
   const char *moduleName;
   void *module;
   WstModuleInit initEntryPoint;
   WstModuleTerm termEntryPoint;
   bool isInitialized;
} WstModule;

typedef struct _WstContext
{
   const char *displayName;
   unsigned int frameRate;
   int framePeriodMillis;
   const char *rendererModule;
   bool isNested;
   bool isRepeater;
   bool isEmbedded;
   bool mustInitRendererModule;
   bool hasVpcBridge;
   const char *nestedDisplayName;
   unsigned int nestedWidth;
   unsigned int nestedHeight;
   bool allowModifyCursor;
   void *nativeWindow;
   std::vector<WstModule*> modules;
   
   void *terminatedUserData;
   WstTerminatedCallback terminatedCB;
   
   bool running;
   bool compositorReady;
   bool compositorAborted;
   bool compositorThreadStarted;
   pthread_t compositorThreadId;

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
   struct wl_display *ncDisplay;
   pthread_mutex_t ncStartedMutex;
   pthread_cond_t ncStartedCond;
   void *nestedListenerUserData;
   WstNestedConnectionListener nestedListener;
   bool hasEmbeddedMaster;
   
   void *outputNestedListenerUserData;
   WstOutputNestedListener *outputNestedListener;
   void *keyboardNestedListenerUserData;
   WstKeyboardNestedListener *keyboardNestedListener;
   void *pointerNestedListenerUserData;
   WstPointerNestedListener *pointerNestedListener;
   void *touchNestedListenerUserData;
   WstTouchNestedListener *touchNestedListener;

   struct wl_display *display;
   #ifdef ENABLE_SBPROTOCOL
   struct wl_sb *sb;
   #endif
   struct wl_simple_shell *simpleShell;
   struct wl_event_source *displayTimer;

   #if defined (WESTEROS_HAVE_WAYLAND_EGL)
   EGLDisplay eglDisplay;
   PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
   PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
   PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
   PFNGETDEVICEBUFFERFROMRESOURCE getDeviceBufferFromResource;
   bool canRemoteClone;
   PFNREMOTEBEGIN remoteBegin;
   PFNREMOTEEND remoteEnd;
   PFNREMOTECLONEBUFFERFROMRESOURCE remoteCloneBufferFromResource;
   #endif

   int nextSurfaceId;
   std::vector<WstSurface*> surfaces;
   std::vector<WstVpcSurface*> vpcSurfaces;
   std::map<int32_t, WstSurface*> surfaceMap;
   std::map<struct wl_client*, WstClientInfo*> clientInfoMap;
   std::map<struct wl_resource*, WstSurfaceInfo*> surfaceInfoMap;

   bool needRepaint;
   bool allowImmediateRepaint;
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

   WstCompositor *wctx;
   std::vector<WstCompositor*> virt;
   
} WstContext;

typedef struct _WstCompositor
{
   bool isVirtual;
   WstContext *ctx;
   int clientPid;

   char lastErrorDetail[WST_MAX_ERROR_DETAIL];

   void *dispatchUserData;
   WstDispatchCallback dispatchCB;
   void *invalidateUserData;
   WstInvalidateSceneCallback invalidateCB;
   void *hidePointerUserData;
   WstHidePointerCallback hidePointerCB;
   void *clientStatusUserData;
   WstClientStatus clientStatusCB;

   bool outputSizeChanged;
   int outputWidth;
   int outputHeight;

   WstKeyboard *keyboard;
   WstPointer *pointer;
   WstTouch *touch;

   bool destroyed;

   int eventIndex;
   WstEvent eventQueue[WST_EVENT_QUEUE_SIZE];
} WstCompositor;

static const char* wstGetNextNestedDisplayName(void);
static bool wstCompositorCreateRenderer( WstContext *ctx );
static void wstCompositorReleaseResources( WstContext *ctx );
static void* wstCompositorThread( void *arg );
static long long wstGetCurrentTimeMillis(void);
static bool wstCompositorCheckForRepeaterSupport( WstContext *ctx );
static void wstCompositorDestroyVirtual( WstCompositor *wctx );
static void wstCompositorProcessEvents( WstCompositor *wctx );
static void wstContextProcessEvents( WstContext *ctx );
static void wstCompositorComposeFrame( WstContext *ctx, uint32_t frameTime );
static void wstContextInvokeDispatchCB( WstContext *ctx );
static void wstContextInvokeInvalidateCB( WstContext *ctx );
static void wstContextInvokeHidePointerCB( WstContext *ctx, bool hidePointer );
static int wstCompositorDisplayTimeOut( void *data );
static void wstCompositorScheduleRepaint( WstContext *ctx );
static void wstCompositorReleaseDetachedBuffers( WstContext *ctx );
static void wstShmBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static bool wstShmInit( WstContext *ctx );
static void wstShmTerm( WstContext *ctx );
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
static void wstShmDestroyPool( struct wl_resource *resource );
static void wstShmPoolUnRef( WstShmPool *pool );
static WstCompositor *wstGetCompositorFromPid( WstContext *ctx, struct wl_client *client, int pid );
static WstCompositor* wstGetCompositorFromClient( WstContext *ctx, struct wl_client *client );
static void wstCompositorBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void wstDestroyCompositorCallback(struct wl_resource *resource);
static void wstICompositorCreateSurface( struct wl_client *client, struct wl_resource *resource, uint32_t id);
static void wstICompositorCreateRegion( struct wl_client *client, struct wl_resource *resource, uint32_t id);
static void wstDestroySurfaceCallback(struct wl_resource *resource);
static WstSurface* wstSurfaceCreate( WstCompositor *wctx);
static void wstSurfaceDestroy( WstSurface *surface );
static bool wstSurfaceSetRole( WstSurface *surface, const char *roleName, 
                               struct wl_resource *errorResource, uint32_t errorCode );
static void wstSurfaceInsertSurface( WstContext *ctx, WstSurface *surface );
static WstSurface* wstGetSurfaceFromSurfaceId( WstContext *ctx, int32_t surfaceId );
static WstSurface* wstGetSurfaceFromPoint( WstCompositor *wctx, int x, int y );
static WstSurfaceInfo* wstGetSurfaceInfo( WstContext *ctx, struct wl_resource *resource );
static void wstUpdateClientInfo( WstContext *ctx, struct wl_client *client, struct wl_resource *resource );
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
static WstRegion *wstRegionCreate( WstCompositor *wctx );
static void wstRegionDestroy( WstRegion *region );
static void wstDestroyRegionCallback(struct wl_resource *resource);
static void wstIRegionDestroy( struct wl_client *client, struct wl_resource *resource );
static void wstIRegionAdd( struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y, int32_t width, int32_t height );
static void wstIRegionSubtract( struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t x, int32_t y, int32_t width, int32_t height );
static bool wstOutputInit( WstContext *ctx );
static void wstOutputTerm( WstContext *ctx );
static void wstOutputBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);                                
static void wstOutputChangeSize( WstCompositor *wctx );
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
static void wstXdgSurfaceSendConfigure( WstCompositor *wctx, WstSurface *surface, uint32_t state );
static void wstDefaultNestedConnectionStarted( void *userData );
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
static void wstDefaultNestedTouchHandleDown( void *userData, struct wl_surface *surfaceNested, uint32_t time, int32_t id, wl_fixed_t sx, wl_fixed_t sy );
static void wstDefaultNestedTouchHandleUp( void *userData, uint32_t time, int32_t id );
static void wstDefaultNestedTouchHandleMotion( void *userData, uint32_t time, int32_t id, wl_fixed_t sx, wl_fixed_t sy );
static void wstDefaultNestedTouchHandleFrame( void *userData );
static void wstDefaultNestedShmFormat( void *userData, uint32_t format );
static void wstDefaultNestedVpcVideoPathChange( void *userData, struct wl_surface *surfaceNested, uint32_t newVideoPath );
static void wstDefaultNestedVpcVideoXformChange( void *userData,
                                                 struct wl_surface *surfaceNested,
                                                 int32_t x_translation,
                                                 int32_t y_translation,
                                                 uint32_t x_scale_num,
                                                 uint32_t x_scale_denom,
                                                 uint32_t y_scale_num,
                                                 uint32_t y_scale_denom,
                                                 uint32_t output_width,
                                                 uint32_t output_height );
static void wstDefaultNestedSurfaceStatus( void *userData, struct wl_surface *surface,
                                           const char *name,
                                           uint32_t visible,
                                           int32_t x,
                                           int32_t y,
                                           int32_t width,
                                           int32_t height,
                                           wl_fixed_t opacity,
                                           wl_fixed_t zorder);
static void wstSetDefaultNestedListener( WstContext *ctx );
static bool wstSeatInit( WstContext *ctx );
static void wstSeatItemTerm( WstCompositor *wctx );
static void wstSeatTerm( WstContext *ctx );
static void wstSeatCreateDevices( WstCompositor *wctx );
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
static void wstITouchRelease( struct wl_client *client, struct wl_resource *resource );
static void wstVpcBind( struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void wstIVpcGetVpcSurface( struct wl_client *client, struct wl_resource *resource, 
                                  uint32_t id, struct wl_resource *surfaceResource);
static void wstDestroyVpcSurfaceCallback(struct wl_resource *resource);
static void wstVpcSurfaceDestroy( WstVpcSurface *vpcSurface );
static void wstIVpcSurfaceSetGeometry( struct wl_client *client, struct wl_resource *resource,
                                       int32_t x, int32_t y, int32_t width, int32_t height );
static void wstUpdateVPCSurfaces( WstCompositor *wctx, std::vector<WstRect> &rects );
static bool wstInitializeKeymap( WstCompositor *wctx );
static void wstTerminateKeymap( WstCompositor *wctx );
static void wstProcessKeyEvent( WstKeyboard *keyboard, uint32_t keyCode, uint32_t keyState, uint32_t modifiers );
static void wstKeyboardSendModifiers( WstKeyboard *keyboard, struct wl_resource *resource );
static void wstKeyboardCheckFocus( WstKeyboard *keyboard, WstSurface *surface );
static void wstKeyboardSetFocus( WstKeyboard *keyboard, WstSurface *surface );
static void wstKeyboardMoveFocusToClient( WstKeyboard *keyboard, struct wl_client *client );
static void wstProcessPointerEnter( WstPointer *pointer, int x, int y, struct wl_surface *surfaceNested );
static void wstProcessPointerLeave( WstPointer *pointer, struct wl_surface *surfaceNested );
static void wstProcessPointerMoveEvent( WstPointer *pointer, int32_t x, int32_t y );
static void wstProcessPointerButtonEvent( WstPointer *pointer, uint32_t button, uint32_t buttonState, uint32_t time );
static void wstPointerCheckFocus( WstPointer *pointer, int32_t x, int32_t y );
static void wstPointerSetPointer( WstPointer *pointer, WstSurface *surface );
static void wstPointerUpdatePosition( WstPointer *pointer );
static void wstPointerSetFocus( WstPointer *pointer, WstSurface *surface, wl_fixed_t x, wl_fixed_t y );
static void wstPointerMoveFocusToClient( WstPointer *pointer, struct wl_client *client );
static void wstProcessTouchDownEvent( WstTouch *touch, uint32_t time, int id, int x, int y, struct wl_surface *surfaceNested );
static void wstProcessTouchUpEvent( WstTouch *touch, uint32_t time, int id );
static void wstProcessTouchMotionEvent( WstTouch *touch, uint32_t time, int id, int x, int y );
static void wstProcessTouchFrameEvent( WstTouch *touch );
static void wstTouchCheckFocus( WstTouch *pointer, int32_t x, int32_t y );
static void wstTouchMoveFocusToClient( WstTouch *touch, struct wl_client *client );
static void wstRemoveTempFile( int fd );
static void wstPruneOrphanFiles( WstContext *ctx );
static bool wstInitializeDefaultCursor( WstCompositor *compositor, 
                                        unsigned char *imgData, int width, int height,
                                        int hotspotX, int hotspotY  );
static void wstTerminateDefaultCursor( WstCompositor *compositor );
static void wstForwardChildProcessStdout( int descriptors[2] );
static void wstMonitorChildProcessStdout( int descriptors[2] );


extern char **environ;
static pthread_mutex_t g_mutex= PTHREAD_MUTEX_INITIALIZER;
static int g_pid= 0;
static int g_nextNestedId= 0;
static pthread_mutex_t g_mutexMasterEmbedded= PTHREAD_MUTEX_INITIALIZER;
static WstCompositor *g_masterEmbedded= 0;


WstCompositor* WstCompositorCreate()
{
   WstCompositor *wctx= 0;

   INFO("westeros (core) version " WESTEROS_VERSION_FMT, WESTEROS_VERSION );
   
   wctx= (WstCompositor*)calloc( 1, sizeof(WstCompositor) );
   if ( wctx )
   {
      WstContext *ctx= 0;

      wctx->outputWidth= DEFAULT_OUTPUT_WIDTH;
      wctx->outputHeight= DEFAULT_OUTPUT_HEIGHT;

      ctx= (WstContext*)calloc( 1, sizeof(WstContext) );
      if ( ctx )
      {
         pthread_mutex_init( &ctx->mutex, 0 );

         ctx->frameRate= DEFAULT_FRAME_RATE;
         ctx->framePeriodMillis= (1000/ctx->frameRate);

         ctx->nestedWidth= DEFAULT_NESTED_WIDTH;
         ctx->nestedHeight= DEFAULT_NESTED_HEIGHT;

         #if defined (WESTEROS_HAVE_WAYLAND_EGL)
         ctx->eglDisplay= EGL_NO_DISPLAY;
         #endif

         ctx->nextSurfaceId= 1;

         ctx->surfaceMap= std::map<int32_t, WstSurface*>();
         ctx->clientInfoMap= std::map<struct wl_client*, WstClientInfo*>();
         ctx->surfaceInfoMap= std::map<struct wl_resource*, WstSurfaceInfo*>();
         ctx->vpcSurfaces= std::vector<WstVpcSurface*>();
         ctx->modules= std::vector<WstModule*>();

         ctx->xkbNames.rules= strdup("evdev");
         ctx->xkbNames.model= strdup("pc105");
         ctx->xkbNames.layout= strdup("us");
         ctx->xkbKeymapFd= -1;

         ctx->dcPoolFd= -1;

         wstSetDefaultNestedListener( ctx );

         ctx->wctx= wctx;
         wctx->ctx= ctx;
      }
      else
      {
         free( wctx );
         wctx= 0;
      }
   }
   
   return wctx;
}

void WstCompositorDestroy( WstCompositor *wctx )
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         pthread_mutex_lock( &ctx->mutex );
         wctx->destroyed= true;
         pthread_mutex_unlock( &ctx->mutex );
         return;
      }

      pthread_mutex_lock( &g_mutexMasterEmbedded );
      if ( g_masterEmbedded == wctx )
      {
         g_masterEmbedded= 0;
      }
      pthread_mutex_unlock( &g_mutexMasterEmbedded );

      pthread_mutex_lock( &ctx->mutex );
      for ( std::vector<WstCompositor*>::iterator it= ctx->virt.begin();
            it != ctx->virt.end();
            ++it )
      {
         WstCompositor *wctx= (*it);
         wctx->destroyed= true;
      }
      pthread_mutex_unlock( &ctx->mutex );
      
      if ( ctx->running || ctx->compositorThreadStarted )
      {
         WstCompositorStop( wctx );
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

      while( !ctx->modules.empty() )
      {
         WstModule* module= ctx->modules.back();
         ctx->modules.pop_back();
         if ( module )
         {
            if ( module->moduleName )
            {
               free( (void*)module->moduleName );
            }
            if ( module->module )
            {
               dlclose( module->module );
            }
            free( module );
         }
      }

      pthread_mutex_destroy( &ctx->mutex );
      
      free( ctx );

      free( wctx );
   }
}

WstCompositor* WstCompositorGetMasterEmbedded()
{
   WstCompositor *wctx= 0;

   pthread_mutex_lock( &g_mutexMasterEmbedded );
   if ( !g_masterEmbedded )
   {
      wctx= WstCompositorCreate();
      if ( wctx )
      {
         bool error= false;
         if ( !WstCompositorSetRendererModule( wctx, "libwesteros_render_embedded.so.0.0.0" ) )
         {
            ERROR("WstCompositorGetMasterEmbedded: WstCompositorSetRendererModule failed");
            error= true;
         }
         if ( !WstCompositorSetIsEmbedded( wctx, true ) )
         {
            ERROR("WstCompositorGetMasterEmbedded: WstCompositorSetIsEmbedded failed");
            error= true;
         }
         if ( !WstCompositorStart( wctx ) )
         {
            ERROR("WstCompositorGetMasterEmbedded: WstCompositorStart failed");
            error= true;
         }
         if ( error )
         {
            WstCompositorDestroy( wctx );
            wctx= 0;
         }
         else
         {
            g_masterEmbedded= wctx;
         }
      }
      else
      {
         ERROR("WstCompositorGetMasterEmbedded: WstCompositorCreate failed");
      }
   }
   wctx= g_masterEmbedded;
   pthread_mutex_unlock( &g_mutexMasterEmbedded );

   return wctx;
}

WstCompositor* WstCompositorCreateVirtualEmbedded( WstCompositor *wctx )
{
   WstCompositor *virt= 0;

   if ( !wctx )
   {
      wctx= WstCompositorGetMasterEmbedded();
   }

   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( !ctx->isEmbedded )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set create virtual embedded from a non-embedded compositor" );
         goto exit;
      }

      virt= (WstCompositor*)calloc( 1, sizeof(WstCompositor) );
      if ( !virt )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  No memory to create virtual embedded compositor" );
         goto exit;
      }

      pthread_mutex_lock( &ctx->mutex );

      virt->isVirtual= true;
      virt->ctx= ctx;
      ctx->virt.push_back( virt );

      pthread_mutex_unlock( &ctx->mutex );
   }

exit:
   return virt;
}

const char *WstCompositorGetLastErrorDetail( WstCompositor *wctx )
{
   const char *detail= 0;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      detail= wctx->lastErrorDetail;
      
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return detail;
}

bool WstCompositorSetDisplayName( WstCompositor *wctx, const char *displayName )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set display name of virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
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

bool WstCompositorSetFrameRate( WstCompositor *wctx, unsigned int frameRate )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set frame rate of virtual embedded compositor" );
         goto exit;
      }

      if ( frameRate == 0 )
      {
         sprintf( wctx->lastErrorDetail,
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

bool WstCompositorSetNativeWindow( WstCompositor *wctx, void *nativeWindow )
{
   bool result= false;
   
   if ( wctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set native window of virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
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

bool WstCompositorSetRendererModule( WstCompositor *wctx, const char *rendererModule )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set render module of virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
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

bool WstCompositorSetIsNested( WstCompositor *wctx, bool isNested )
{
   bool result= false;

   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set nested for virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set isNested while compositor is running" );
         goto exit;
      }
                     
      pthread_mutex_lock( &ctx->mutex );
      
      wstCompositorCheckForRepeaterSupport( ctx );

      ctx->isNested= isNested;
               
      pthread_mutex_unlock( &ctx->mutex );
            
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorSetIsRepeater( WstCompositor *wctx, bool isRepeater )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set repeater for virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set isRepeater while compositor is running" );
         goto exit;
      }
                     
      pthread_mutex_lock( &ctx->mutex );
      
      ctx->isRepeater= isRepeater;
      if ( isRepeater )
      {
         ctx->isNested= true;

         bool repeaterSupported= wstCompositorCheckForRepeaterSupport( ctx );
         if ( !repeaterSupported )
         {
            // We can't do renderless composition with some wayland-egl.  Ignore the
            // request and configure for nested composition with gl renderer
            ctx->isRepeater= false;
            if ( ctx->rendererModule )
            {
               free( (void*)ctx->rendererModule );
               ctx->rendererModule= 0;
            }
            ctx->rendererModule= strdup("libwesteros_render_gl.so.0");
            WARNING("WstCompositorSetIsRepeater: cannot repeat with this wayland-egl: configuring nested with gl renderer");
         }
      }
               
      pthread_mutex_unlock( &ctx->mutex );
            
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorSetIsEmbedded( WstCompositor *wctx, bool isEmbedded )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual && isEmbedded )
      {
         result= true;
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set isEmbedded while compositor is running" );
         goto exit;
      }
                     
      pthread_mutex_lock( &ctx->mutex );
      
      ctx->isEmbedded= isEmbedded;

      if ( ctx->isEmbedded )
      {
         char *var= getenv("WESTEROS_VPC_BRIDGE");
         if ( var )
         {
            if ( ctx->nestedDisplayName )
            {
               free( (void*)ctx->nestedDisplayName );
               ctx->nestedDisplayName= 0;
            }
            ctx->nestedDisplayName= strdup(var);
            ctx->hasVpcBridge= true;
         }
      }
               
      pthread_mutex_unlock( &ctx->mutex );
            
      result= true;
   }

exit:
   
   return result;
}

/**
 * WstCompositorSetVpcBridge
 *
 * Specify if the embedded compositor instance should establish a VPC
 * (Video Path Control) bridge with another compositor instance.  A
 * VPC bridge will allow control over video path and positioning to be
 * extended to higher level compositors from a nested ebedded compositor.
 */
bool WstCompositorSetVpcBridge( WstCompositor *wctx, char *displayName )
{
   bool result= false;

   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set vpc bridge for virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set VPC bridge while compositor is running" );
         goto exit;
      }

      if ( !ctx->isEmbedded )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Cannot set VPC bridge on non-embedded compositor" );
         goto exit;
      }

      pthread_mutex_lock( &ctx->mutex );

      ctx->hasVpcBridge= (displayName != 0);

      if ( ctx->hasVpcBridge )
      {
         ctx->nestedDisplayName= strdup(displayName);
      }

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

exit:

   return result;
}

bool WstCompositorSetOutputSize( WstCompositor *wctx, int width, int height )
{
   bool result= false;

   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( width <= 0 )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  The output width (%u) must be greater than zero", width );
         goto exit;      
      }

      if ( height <= 0 )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  The output height (%u) must be greater than zero", height );
         goto exit;      
      }
               
      pthread_mutex_lock( &ctx->mutex );
      
      wctx->outputWidth= width;
      wctx->outputHeight= height;
      
      if ( ctx->running )
      {
         wctx->outputSizeChanged= true;
      }
               
      pthread_mutex_unlock( &ctx->mutex );
            
      result= true;
   }

exit:
   
   return result;
}

bool WstCompositorSetNestedDisplayName( WstCompositor *wctx, const char *nestedDisplayName )
{
   bool result= false;
   int len;
   const char *name= 0;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set nested display name for virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set nested display name while compositor is running" );
         goto exit;
      }
      
      if ( nestedDisplayName )
      {
         len= strlen(nestedDisplayName);
         
         if ( (len == 0) || (len > MAX_NESTED_NAME_LEN) )
         {
            sprintf( wctx->lastErrorDetail,
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

bool WstCompositorSetNestedSize( WstCompositor *wctx, unsigned int width, unsigned int height )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set nested size for virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set nested size while compositor is running" );
         goto exit;
      }      

      if ( width == 0 )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  The nested width (%u) must be greater than zero", width );
         goto exit;      
      }

      if ( height == 0 )
      {
         sprintf( wctx->lastErrorDetail,
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

bool WstCompositorSetAllowCursorModification( WstCompositor *wctx, bool allow )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set allow cursor modification for virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
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
bool WstCompositorSetDefaultCursor( WstCompositor *wctx, unsigned char *imgData,
                                    int width, int height, int hotSpotX, int hotSpotY )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set default cursor for virtual embedded compositor" );
         goto exit;
      }

      if ( !ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set default cursor unless the compositor is running" );
         goto exit;
      }

      if ( ctx->isRepeater )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Cannot set default cursor when operating as a repeating nested compositor" );
         goto exit;
      }

      if ( ctx->isNested )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Cannot set default cursor when operating as a nested compositor" );
         goto exit;
      }

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->dcClient )
      {
         pthread_mutex_unlock( &ctx->mutex );
         wl_client_destroy( ctx->dcClient );
         usleep( 100000 );
         pthread_mutex_lock( &ctx->mutex );
         ctx->dcClient= 0;
         ctx->dcPid= 0;
         ctx->dcDefaultCursor= false;
      }
      else
      {
         wstTerminateDefaultCursor( wctx );
      }
      
      if ( imgData )
      {
         if ( !wstInitializeDefaultCursor( wctx, imgData, width, height, hotSpotX, hotSpotY ) )
         {
            sprintf( wctx->lastErrorDetail,
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

bool WstCompositorAddModule( WstCompositor *wctx, const char *moduleName )
{
   bool result= false;
   WstModule *moduleNew= 0;

   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot add module to virtual embedded compositor" );
         goto exit;
      }

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot add module while compositor is running" );
         goto exit;
      }

      moduleNew= (WstModule*)calloc( 1, sizeof(WstModule) );
      if ( !moduleNew )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Unable to allocate memory for new module" );
         goto exit;
      }

      moduleNew->moduleName= strdup(moduleName);
      if ( !moduleNew->moduleName )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Unable to allocate memory for new module name" );
         goto exit;
      }

      moduleNew->module= dlopen( moduleName, RTLD_NOW );
      if ( !moduleNew->module )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Unable to open new module (%.*s)", (WST_MAX_ERROR_DETAIL-64), dlerror() );
         goto exit;
      }

      moduleNew->initEntryPoint= (WstModuleInit)dlsym( moduleNew->module, "moduleInit" );
      if ( !moduleNew->initEntryPoint )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Module missing moduleInit entry point (%.*s)", (WST_MAX_ERROR_DETAIL-64), dlerror() );
         goto exit;
      }

      moduleNew->termEntryPoint= (WstModuleTerm)dlsym( moduleNew->module, "moduleTerm" );
      if ( !moduleNew->termEntryPoint )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Module missing moduleTerm entry point (%.*s)", (WST_MAX_ERROR_DETAIL-64), dlerror() );
         goto exit;
      }

      pthread_mutex_lock( &ctx->mutex );

      ctx->modules.push_back( moduleNew );

      pthread_mutex_unlock( &ctx->mutex );

      result= true;
   }

exit:

   if ( !result )
   {
      if ( moduleNew )
      {
         if ( moduleNew->moduleName )
         {
            free( (void*)moduleNew->moduleName );
         }

         if ( moduleNew->module )
         {
            dlclose( moduleNew->module );
         }

         free( moduleNew );
      }
   }

   return result;
}

void WstCompositorResolutionChangeBegin( WstCompositor *wctx )
{
   DEBUG("WstCompositorResolutionChangeBegin");
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isNested && !ctx->isEmbedded )
      {
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_resolutionChangeBegin;

         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );

         ctx->allowImmediateRepaint= true;
         wstCompositorScheduleRepaint( ctx );
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
   DEBUG("WstCompositorResolutionChangeBegin: exit");
}

void WstCompositorResolutionChangeEnd( WstCompositor *wctx, int width, int height )
{
   DEBUG("WstCompositorResolutionChangeEnd: (%d x %d)", width, height);
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      WstCompositorSetOutputSize( wctx, width, height );
      ctx->outputSizeChanged= true;

      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isNested && !ctx->isEmbedded )
      {
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_resolutionChangeEnd;
         wctx->eventQueue[eventIndex].v1= width;
         wctx->eventQueue[eventIndex].v2= height;

         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );

         ctx->allowImmediateRepaint= true;
         wstCompositorScheduleRepaint( ctx );
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
   DEBUG("WstCompositorResolutionChangeEnd: exit");
}

const char *WstCompositorGetDisplayName( WstCompositor *wctx )
{
   const char *displayName= 0;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;
               
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

unsigned int WstCompositorGetFrameRate( WstCompositor *wctx )
{
   unsigned int frameRate= 0;
   
   if ( wctx && wctx->ctx )
   {               
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      frameRate= ctx->frameRate;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return frameRate;
}

const char *WstCompositorGetRendererModule( WstCompositor *wctx )
{
   const char *rendererModule= 0;
   
   if ( wctx && wctx->ctx )
   {               
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      rendererModule= ctx->rendererModule;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return rendererModule;
}

bool WstCompositorGetIsNested( WstCompositor *wctx )
{
   bool isNested= false;
   
   if ( wctx && wctx->ctx )
   {               
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      isNested= ctx->isNested;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return isNested;
}

bool WstCompositorGetIsRepeater( WstCompositor *wctx )
{
   bool isRepeater= false;
   
   if ( wctx && wctx->ctx )
   {               
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      isRepeater= ctx->isRepeater;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return isRepeater;
}

bool WstCompositorGetIsEmbedded( WstCompositor *wctx )
{
   bool isEmbedded= false;
   
   if ( wctx && wctx->ctx )
   {               
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      isEmbedded= ctx->isEmbedded;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return isEmbedded;
}

bool WstCompositorGetIsVirtualEmbedded( WstCompositor *wctx )
{
   bool isVirtualEmbedded= false;

   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      isVirtualEmbedded= (wctx->isVirtual && ctx->isEmbedded);

      pthread_mutex_unlock( &ctx->mutex );
   }

   return isVirtualEmbedded;
}

/**
 * WstCompositorGetVpcBridge
 *
 * Determine the display, if any, with which this embedded compistor instance
 * will establish a VPC (Video Path Control) bridge.
 */
const char* WstCompositorGetVpcBridge( WstCompositor *wctx )
{
   const char *displayName= 0;

   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->hasVpcBridge )
      {
         displayName= ctx->nestedDisplayName;
      }

      pthread_mutex_unlock( &ctx->mutex );
   }

   return displayName;
}

void WstCompositorGetOutputSize( WstCompositor *wctx, unsigned int *width, unsigned int *height )
{
   int outputWidth= 0;
   int outputHeight= 0;
   
   if ( wctx && wctx->ctx )
   {               
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      outputWidth= wctx->outputWidth;
      outputHeight= wctx->outputHeight;
               
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

const char *WstCompositorGetNestedDisplayName( WstCompositor *wctx )
{
   const char *nestedDisplayName= 0;
   
   if ( wctx && wctx->ctx )
   {               
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      nestedDisplayName= ctx->nestedDisplayName;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return nestedDisplayName;
}

void WstCompositorGetNestedSize( WstCompositor *wctx, unsigned int *width, unsigned int *height )
{
   int nestedWidth= 0;
   int nestedHeight= 0;
   
   if ( wctx && wctx->ctx )
   {               
      WstContext *ctx= wctx->ctx;

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

bool WstCompositorGetAllowCursorModification( WstCompositor *wctx )
{
   bool allow= false;
   
   if ( wctx && wctx->ctx )
   {               
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      allow= ctx->allowModifyCursor;
               
      pthread_mutex_unlock( &ctx->mutex );
   }
   
   return allow;
}

bool WstCompositorSetTerminatedCallback( WstCompositor *wctx, WstTerminatedCallback cb, void *userData )
{
   bool result= false;

   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         sprintf( wctx->lastErrorDetail,
                  "Invalid argument.  Cannot set terminated callback for virtual embedded compositor" );
         goto exit;
      }

      pthread_mutex_lock( &ctx->mutex );

      ctx->terminatedUserData= userData;
      ctx->terminatedCB= cb;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetDispatchCallback( WstCompositor *wctx, WstDispatchCallback cb, void *userData )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      wctx->dispatchUserData= userData;
      wctx->dispatchCB= cb;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetInvalidateCallback( WstCompositor *wctx, WstInvalidateSceneCallback cb, void *userData )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      wctx->invalidateUserData= userData;
      wctx->invalidateCB= cb;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetHidePointerCallback( WstCompositor *wctx, WstHidePointerCallback cb, void *userData )
{
   bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isEmbedded )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Compositor is not embedded" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      wctx->hidePointerUserData= userData;
      wctx->hidePointerCB= cb;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetClientStatusCallback( WstCompositor *wctx, WstClientStatus cb, void *userData )
{
  bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isEmbedded )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Compositor is not embedded" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      wctx->clientStatusUserData= userData;
      wctx->clientStatusCB= cb;
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:

   return result;   
}

bool WstCompositorSetOutputNestedListener( WstCompositor *wctx, WstOutputNestedListener *listener, void *userData )
{
  bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set output nested listener while compositor is running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }      

      if ( !ctx->isNested )
      {
         sprintf( wctx->lastErrorDetail,
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

bool WstCompositorSetKeyboardNestedListener( WstCompositor *wctx, WstKeyboardNestedListener *listener, void *userData )
{
  bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set keyboard nested listener while compositor is running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }      

      if ( !ctx->isNested )
      {
         sprintf( wctx->lastErrorDetail,
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

bool WstCompositorSetPointerNestedListener( WstCompositor *wctx, WstPointerNestedListener *listener, void *userData )
{
  bool result= false;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Cannot set pointer nested listener while compositor is running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }      

      if ( !ctx->isNested )
      {
         sprintf( wctx->lastErrorDetail,
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

bool WstCompositorComposeEmbedded( WstCompositor *wctx,
                                   int x, int y, int width, int height,
                                   float *matrix, float alpha, 
                                   unsigned int hints, 
                                   bool *needHolePunch, std::vector<WstRect> &rects )
{
   bool result= false;

   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( !ctx->isEmbedded )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Compositor is not embedded" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      if ( !ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
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
         ctx->renderer->hints= hints;
         ctx->renderer->needHolePunch= false;
         ctx->renderer->rects.clear();

         if ( wctx->isVirtual )
         {
            for (std::vector<WstSurface *>::iterator it = ctx->surfaces.begin(); it != ctx->surfaces.end(); ++it)
            {
               WstSurface *surface= (*it);
               if ( surface->compositor != wctx )
               {
                  WstRendererSurfaceGetVisible( ctx->renderer, surface->surface, &surface->tempVisible );
                  WstRendererSurfaceSetVisible( ctx->renderer, surface->surface, false );
               }
            }
         }
         
         if ( ctx->vpcSurfaces.size() )
         {
            wstUpdateVPCSurfaces( wctx, rects );
         }
         
         WstRendererUpdateScene( ctx->renderer );

         if ( wctx->isVirtual )
         {
            for (std::vector<WstSurface *>::iterator it = ctx->surfaces.begin(); it != ctx->surfaces.end(); ++it)
            {
               WstSurface *surface= (*it);
               if ( surface->compositor != wctx )
               {
                  WstRendererSurfaceSetVisible( ctx->renderer, surface->surface, surface->tempVisible );
               }
            }
         }

         if ( !(hints & WstHints_holePunch) )
         {
            if ( ctx->renderer->rects.size() )
            {
               for( int i= 0; i < ctx->renderer->rects.size(); ++i )
               {
                  if ( ctx->renderer->rects[i].width && ctx->renderer->rects[i].height )
                  {
                     rects.push_back( ctx->renderer->rects[i] );
                  }
               }
            }
         }

         *needHolePunch= ( rects.size() > 0 );
      }
      
      pthread_mutex_unlock( &ctx->mutex );
      
      result= true;
   }

exit:
   
   return result;
}

void WstCompositorInvalidateScene( WstCompositor *wctx )
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      wstCompositorScheduleRepaint( ctx );

      pthread_mutex_unlock( &ctx->mutex );
   }
}

bool WstCompositorStart( WstCompositor *wctx )
{
   bool result= false;
   int rc;
                  
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      if ( wctx->isVirtual )
      {
         if ( ctx->running )
         {
            result= true;
         }
         else
         {
            sprintf( wctx->lastErrorDetail,
                     "Invalid argument.  Cannot start virtual embedded compositor" );
         }
         goto exit;
      }

      pthread_mutex_lock( &ctx->mutex );
      
      if ( ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Compositor is already running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }
      
      if ( !ctx->rendererModule && ctx->isEmbedded )
      {
         ctx->rendererModule= strdup("libwesteros_render_embedded.so.0");
      }

      if ( !ctx->rendererModule && !ctx->isRepeater )
      {
         sprintf( wctx->lastErrorDetail,
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
            sprintf( wctx->lastErrorDetail,
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
         if ( !wstInitializeKeymap( wctx ) )
         {
            pthread_mutex_unlock( &ctx->mutex );
            goto exit;      
         }
      }

      ctx->compositorAborted= false;
      
      rc= pthread_create( &ctx->compositorThreadId, NULL, wstCompositorThread, wctx );
      if ( rc )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Failed to start compositor main thread: %d", rc );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;      
      }

      pthread_mutex_unlock( &ctx->mutex );
      
      INFO("waiting for compositor %s to start...", ctx->displayName);
      for( ; ; )
      {
         bool ready, aborted;

         pthread_mutex_lock( &ctx->mutex );
         ready= ctx->compositorReady;
         aborted= ctx->compositorAborted;
         pthread_mutex_unlock( &ctx->mutex );
         
         if ( ready || aborted )
         {
            if ( ready )
            {
               pthread_mutex_lock( &ctx->mutex );
               if ( ctx->isEmbedded )
               {
                  if ( !wstCompositorCreateRenderer( ctx ) )
                  {
                     sprintf( wctx->lastErrorDetail,
                              "Error.  Failed to initialize render module" );
                     pthread_mutex_unlock( &ctx->mutex );
                     goto exit;
                  }
               }
               pthread_mutex_unlock( &ctx->mutex );

               INFO("compositor %s is started", ctx->displayName);
            }
            if ( aborted )
            {
               INFO("start of compositor %s has failed", ctx->displayName);
            }
            break;
         }
         
         usleep( 10000 );
      }

      if ( !ctx->compositorReady )
      {
         sprintf( wctx->lastErrorDetail,
                  "Error.  Compositor thread failed to create display" );
         goto exit;      
      }

      ctx->running= true;

      result= true;      

   }

exit:
   
   return result;
}

void WstCompositorStop( WstCompositor *wctx )
{
   if ( wctx && wctx->ctx && !wctx->isVirtual )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->running || ctx->compositorThreadStarted )
      {
         ctx->running= false;

         ctx->outputNestedListener= 0;
         ctx->outputNestedListenerUserData= 0;
         ctx->keyboardNestedListener= 0;
         ctx->keyboardNestedListenerUserData= 0;
         ctx->pointerNestedListener= 0;
         ctx->pointerNestedListenerUserData= 0;

         if ( ctx->compositorThreadStarted && ctx->display )
         {
            wl_display_terminate( ctx->display );
         }

         pthread_mutex_unlock( &ctx->mutex );
         pthread_join( ctx->compositorThreadId, NULL );
         pthread_mutex_lock( &ctx->mutex );

         wstCompositorReleaseResources( ctx );

         if ( !ctx->isNested )
         {
            wstTerminateKeymap( wctx );
         }
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorKeyEvent( WstCompositor *wctx, int keyCode, unsigned int keyState, unsigned int modifiers )
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_key;
         wctx->eventQueue[eventIndex].v1= keyCode;
         wctx->eventQueue[eventIndex].v2= keyState;
         wctx->eventQueue[eventIndex].v3= modifiers;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorPointerEnter( WstCompositor *wctx )
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_pointerEnter;
         wctx->eventQueue[eventIndex].v1= 0;
         wctx->eventQueue[eventIndex].v2= 0;
         wctx->eventQueue[eventIndex].p1= 0;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorPointerLeave( WstCompositor *wctx )
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_pointerLeave;
         wctx->eventQueue[eventIndex].p1= 0;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorPointerMoveEvent( WstCompositor *wctx, int x, int y )
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_pointerMove;
         wctx->eventQueue[eventIndex].v1= x;
         wctx->eventQueue[eventIndex].v2= y;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorPointerButtonEvent( WstCompositor *wctx, unsigned int button, unsigned int buttonState )
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );

      if ( ctx->seat && !ctx->isNested )
      {
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_pointerButton;
         wctx->eventQueue[eventIndex].v1= button;
         wctx->eventQueue[eventIndex].v2= buttonState;
         wctx->eventQueue[eventIndex].v3= 0; //no time
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
      }

      pthread_mutex_unlock( &ctx->mutex );
   }
}

void WstCompositorTouchEvent( WstCompositor *wctx, WstTouchSet *touchSet )
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;
      bool queuedEvents= false;
      uint32_t time= (uint32_t)wstGetCurrentTimeMillis();

      pthread_mutex_lock( &ctx->mutex );

      int eventIndex= wctx->eventIndex;

      for( int i= 0; i < WST_MAX_TOUCH; ++i )
      {
         if ( touchSet->touch[i].valid )
         {
            if ( touchSet->touch[i].starting )
            {
               wctx->eventQueue[eventIndex].type= WstEventType_touchDown;
               wctx->eventQueue[eventIndex].v1= time;
               wctx->eventQueue[eventIndex].v2= touchSet->touch[i].id;
               wctx->eventQueue[eventIndex].v3= touchSet->touch[i].x;
               wctx->eventQueue[eventIndex].v4= touchSet->touch[i].y;
               wctx->eventQueue[eventIndex].p1= 0;
               ++eventIndex;
               assert( eventIndex < WST_EVENT_QUEUE_SIZE );
               queuedEvents= true;
            }
            else if ( touchSet->touch[i].stopping )
            {
               wctx->eventQueue[eventIndex].type= WstEventType_touchUp;
               wctx->eventQueue[eventIndex].v1= time;
               wctx->eventQueue[eventIndex].v2= touchSet->touch[i].id;
               ++eventIndex;
               assert( eventIndex < WST_EVENT_QUEUE_SIZE );
               queuedEvents= true;
            }
            else if ( touchSet->touch[i].moved )
            {
               wctx->eventQueue[eventIndex].type= WstEventType_touchMotion;
               wctx->eventQueue[eventIndex].v1= time;
               wctx->eventQueue[eventIndex].v2= touchSet->touch[i].id;
               wctx->eventQueue[eventIndex].v3= touchSet->touch[i].x;
               wctx->eventQueue[eventIndex].v4= touchSet->touch[i].y;
               ++eventIndex;
               assert( eventIndex < WST_EVENT_QUEUE_SIZE );
               queuedEvents= true;
            }
         }
      }

      if ( queuedEvents )
      {
         wctx->eventQueue[eventIndex].type= WstEventType_touchFrame;
         ++eventIndex;
         assert( eventIndex < WST_EVENT_QUEUE_SIZE );
      }

      wctx->eventIndex= eventIndex;

      pthread_mutex_unlock( &ctx->mutex );
   }
}

bool WstCompositorLaunchClient( WstCompositor *wctx, const char *cmd )
{
   bool result= false;
   int rc;
   int i, len, numArgs, numEnvVar;
   char work[256];
   char *p1, *p2;
   char **args= 0;
   char **env= 0;
   char *envDisplay= 0;
   FILE *pClientLog= 0;
   
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;

      pthread_mutex_lock( &ctx->mutex );
      
      if ( !ctx->running )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Compositor is not running" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      if ( wctx->clientPid )
      {
         sprintf( wctx->lastErrorDetail,
                  "Bad state.  Compositor already launched client" );
         pthread_mutex_unlock( &ctx->mutex );
         goto exit;
      }

      i= (cmd ? strlen(cmd) : 0);
      if ( !cmd || (i > 1024) )
      {
         sprintf( wctx->lastErrorDetail,
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
         sprintf( wctx->lastErrorDetail,
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
            sprintf( wctx->lastErrorDetail,
                     "Error.  Unable to allocate memory for client argument %d", i );
            pthread_mutex_unlock( &ctx->mutex );
            goto exit;
         }
         printf( "arg[%d]= %s\n", i, args[i] );
      }

      // Build environment for client
      numEnvVar= 0;
      if ( environ )
      {
         int i= 0;
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
         sprintf( wctx->lastErrorDetail,
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
      snprintf( work, sizeof(work), "WAYLAND_DISPLAY=%s", ctx->displayName );
      envDisplay= strdup( work );
      if ( !envDisplay )
      {
         sprintf( wctx->lastErrorDetail,
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

      char *clientLogName= getenv( "WESTEROS_CAPTURE_CLIENT_STDOUT" );
      if ( clientLogName )
      {
         pClientLog= fopen( clientLogName, "w" );
         printf("capturing stdout for client %s to file %s\n", args[0], clientLogName );
      }

      int filedes[2];
      bool forwardStdout = getenv( "WESTEROS_FORWARD_CLIENT_STDOUT" );

      if ( forwardStdout )
      {
         if ( pipe(filedes) == -1 )
         {
            perror("pipe failed");
            forwardStdout = false;
         }
      }

      // Launch client
      int pid= fork();
      if ( pid == 0 )
      {
         // CHILD PROCESS
         if ( pClientLog )
         {
            dup2( fileno(pClientLog), STDOUT_FILENO );
         }
         else if ( forwardStdout )
         {
            wstForwardChildProcessStdout( filedes );
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
         sprintf( wctx->lastErrorDetail,
                  "Error.  Unable to fork process" );
         goto exit;
      }
      else
      {
         // PARENT PROCESS
         int pidChild, status;

         wctx->clientPid= pid;

         if(wctx->clientStatusCB)
         {
             INFO("clientStatus: status %d pid %d", WstClient_started, pid);
             wctx->clientStatusCB( wctx, WstClient_started, pid, 0, wctx->clientStatusUserData );
         }

         if ( forwardStdout )
         {
            wstMonitorChildProcessStdout( filedes );
         }

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
            
            if ( wctx->clientStatusCB )
            {
               INFO("clientStatus: status %d pid %d detail %d", clientStatus, pidChild, detail); 
               wctx->clientStatusCB( wctx, clientStatus, pidChild, detail, wctx->clientStatusUserData );
            }
         }

         wctx->clientPid= 0;
      }
      
      result= true;
   }
   
exit:
   if ( pClientLog )
   {
      fclose( pClientLog );
   }

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

void WstCompositorFocusClientById( WstCompositor *wctx, const int id)
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;
      WstKeyboard *keyboard= wctx->keyboard;

      if (keyboard != 0)
      {
         pthread_mutex_lock( &ctx->mutex );
         for (std::vector<WstSurface *>::iterator it = ctx->surfaces.begin(); it != ctx->surfaces.end(); ++it)
         {
            WstSurface *surface= (*it);

            if (id == surface->surfaceId)
            {
               wstKeyboardSetFocus(keyboard, surface);
               break;
            }
         }
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

void WstCompositorFocusClientByName( WstCompositor *wctx, const char *name)
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;
      WstKeyboard *keyboard= wctx->keyboard;

      if (keyboard != 0)
      {
         pthread_mutex_lock( &ctx->mutex );
         for (std::vector<WstSurface *>::iterator it = ctx->surfaces.begin(); it != ctx->surfaces.end(); ++it)
         {
            WstSurface *surface= (*it);

            if (::strcmp(name, surface->name) == 0)
            {
               wstKeyboardSetFocus(keyboard, surface);
               break;
            }
         }
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
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
   WstContext *ctx= (WstContext*)user_data;
      
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
   WstContext *ctx= (WstContext*)userData;

   if ( ctx )
   {
      for ( int i= 0; i < ctx->surfaces.size(); ++i )
      {
         WstSurface *surface= ctx->surfaces[i];
         struct wl_resource *resource= buffer->resource;
         if ( surface->attachedBufferResource == resource )
         {
            wl_list_remove(&surface->attachedBufferDestroyListener.link);
            surface->attachedBufferResource= 0;
            break;
         }
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
   WstContext *ctx= (WstContext*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      surface->name= strdup(name);
      DEBUG("set surfaceId %x name to %s", surfaceId, name );
   }
}

static void simpleShellSetVisible( void* userData, uint32_t surfaceId, bool visible )
{
   WstContext *ctx= (WstContext*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      surface->visible= visible;
      if ( ctx->isRepeater )
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
   WstContext *ctx= (WstContext*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      if ( !surface->vpcSurface || (surface->vpcSurface && !surface->vpcSurface->sizeOverride) )
      {
         surface->x= x;
         surface->y= y;
         surface->width= width;
         surface->height= height;
         if ( ctx->isRepeater )
         {
            WstNestedConnectionSurfaceSetGeometry( ctx->nc, surface->surfaceNested, x, y, width, height );
         }
         else
         {
            WstRendererSurfaceSetGeometry( ctx->renderer, surface->surface, x, y, width, height );
         }
         if ( surface->vpcSurface && !surface->vpcSurface->sizeOverride )
         {
            if ( !ctx->isEmbedded && !ctx->hasEmbeddedMaster )
            {
               WstVpcSurface *vpcSurface= surface->vpcSurface;

               vpcSurface->hwX= x;
               vpcSurface->hwY= y;
               vpcSurface->hwWidth= width;
               vpcSurface->hwHeight= height;
               
               vpcSurface->xTrans= x;
               vpcSurface->yTrans= y;
               vpcSurface->xScaleNum= width*100000/DEFAULT_OUTPUT_WIDTH;
               vpcSurface->xScaleDenom= 100000;
               vpcSurface->yScaleNum= height*100000/DEFAULT_OUTPUT_HEIGHT;
               vpcSurface->yScaleDenom= 100000;
               vpcSurface->outputWidth= surface->compositor->outputWidth;
               vpcSurface->outputHeight= surface->compositor->outputHeight;
               
               wl_vpc_surface_send_video_xform_change( vpcSurface->resource,
                                                       vpcSurface->xTrans,
                                                       vpcSurface->yTrans,
                                                       vpcSurface->xScaleNum,
                                                       vpcSurface->xScaleDenom,
                                                       vpcSurface->yScaleNum,
                                                       vpcSurface->yScaleDenom,
                                                       vpcSurface->outputWidth,
                                                       vpcSurface->outputHeight );
            }
         }
      }
      pthread_mutex_lock( &ctx->mutex );
      wstCompositorScheduleRepaint( ctx );
      pthread_mutex_unlock( &ctx->mutex );
   }
}

static void simpleShellSetOpacity( void* userData, uint32_t surfaceId, float opacity )
{
   WstContext *ctx= (WstContext*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      surface->opacity= opacity;
      if ( ctx->isRepeater )
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
   WstContext *ctx= (WstContext*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      surface->zorder= zorder;
      if ( ctx->isRepeater )
      {
         WstNestedConnectionSurfaceSetZOrder( ctx->nc, surface->surfaceNested, zorder );
      }
      else
      {
         WstRendererSurfaceSetZOrder( ctx->renderer, surface->surface, zorder );

         pthread_mutex_lock( &ctx->mutex );
         wstSurfaceInsertSurface( ctx, surface );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void simpleShellSetFocus( void* userData, uint32_t surfaceId )
{
   WstContext *ctx= (WstContext*)userData;

   DEBUG("%s: surfaceId %x", __FUNCTION__, surfaceId);

   if ( ctx->seat )
   {
      WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
      if ( surface )
      {
         WstCompositor *wctx= surface->compositor;
         if ( wctx->keyboard )
         {
            wstKeyboardSetFocus( wctx->keyboard, surface );
         }
         else
         {
            ERROR("failed to set focus - missing keyboard");
         }
      }
      else
      {
         ERROR("failed to set focus - missing surface");
      }
   }
   else
   {
      ERROR("failed to set focus - missing seat");
   }
}

static void simpleShellGetName( void* userData, uint32_t surfaceId, const char **name )
{
   WstContext *ctx= (WstContext*)userData;

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
   WstContext *ctx= (WstContext*)userData;

   WstSurface *surface= wstGetSurfaceFromSurfaceId(ctx, surfaceId);
   if ( surface )
   {
      if ( ctx->isRepeater )
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
         if ( surface->vpcSurface )
         {
            /* Visibility of surface with a vpcSurface is controlled by whether the vpcSurface is using the
             * HW or graphics path.  We return true for visibility in shell status so that the sink will not
             * shut off video.
             */
            *visible= true;
         }
         else
         {
            WstRendererSurfaceGetVisible( ctx->renderer, surface->surface, visible );
         }
         if ( !surface->vpcSurface || (surface->vpcSurface && surface->vpcSurface->sizeOverride) )
         {
            WstRendererSurfaceGetGeometry( ctx->renderer, surface->surface, x, y, width, height );
         }
         else
         {
            *x= surface->vpcSurface->hwX;
            *y= surface->vpcSurface->hwY;
            *width= surface->vpcSurface->hwWidth;
            *height= surface->vpcSurface->hwHeight;
         }
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
   simpleShellGetStatus,
   simpleShellSetFocus
};

static void* wstCompositorThread( void *arg )
{
   WstCompositor *wctx= (WstCompositor*)arg;
   WstContext *ctx= wctx->ctx;
   int rc;
   struct wl_display *display= 0;
   struct wl_event_loop *loop= 0;
   bool startupAborted= true;

   ctx->compositorThreadStarted= true;

   DEBUG("calling wl_display_create");
   display= wl_display_create();
   DEBUG("wl_display=%p", display);
   if ( !display )
   {
      ERROR("unable to create primary display");
      goto exit;
   }

   wstPruneOrphanFiles( ctx );

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

   if ( ctx->isRepeater && !ctx->mustInitRendererModule )
   {
      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      ctx->eglDisplay= eglGetDisplay( EGL_DEFAULT_DISPLAY );
      if ( ctx->eglDisplay == EGL_NO_DISPLAY )
      {
         ERROR("unable to get EGL display");
         goto exit;
      }
      EGLBoolean rc= ctx->eglBindWaylandDisplayWL( ctx->eglDisplay, ctx->display );
      if ( !rc )
      {
         ERROR("unable to bind wayland display");
         goto exit;
      }
      #endif
   }
   else
   {
      if ( !ctx->isEmbedded && (!ctx->isNested || (ctx->isRepeater && ctx->mustInitRendererModule)) )
      {
         if ( !wstCompositorCreateRenderer( ctx ) )
         {
            ERROR("unable to initialize renderer module");
            goto exit;
         }
      }
   }

   if ( ctx->isNested || ctx->hasVpcBridge )
   {
      int width= ctx->nestedWidth;
      int height= ctx->nestedHeight;

      if ( ctx->hasVpcBridge )
      {
         INFO("embedded compositor %s will bridge vpc to display %s", ctx->displayName, ctx->nestedDisplayName);
      }
      if ( ctx->isRepeater || ctx->hasVpcBridge )
      {
         width= 0;
         height= 0;
      }
      pthread_mutex_init( &ctx->ncStartedMutex, 0);
      pthread_cond_init( &ctx->ncStartedCond, 0);
      pthread_mutex_lock( &ctx->ncStartedMutex );
      ctx->nc= WstNestedConnectionCreate( wctx,
                                          ctx->nestedDisplayName,
                                          width,
                                          height,
                                          &ctx->nestedListener,
                                          ctx );
      if ( !ctx->nc )
      {
         ERROR( "Unable to create nested connection to display %s", ctx->nestedDisplayName );
         pthread_mutex_unlock( &ctx->ncStartedMutex );
         pthread_mutex_destroy( &ctx->ncStartedMutex );
         pthread_cond_destroy( &ctx->ncStartedCond );
         goto exit;
      }
      INFO("waiting for nested connection to start");
      while( !ctx->ncDisplay )
      {
         pthread_cond_wait( &ctx->ncStartedCond, &ctx->ncStartedMutex );
      }
      pthread_mutex_unlock( &ctx->ncStartedMutex );
      pthread_mutex_destroy( &ctx->ncStartedMutex );
      pthread_cond_destroy( &ctx->ncStartedCond );
      INFO("nested connection started");
   }

   if ( ctx->isNested && !ctx->isRepeater && !ctx->isEmbedded )
   {
      if ( !wstCompositorCreateRenderer( ctx ) )
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

   for ( std::vector<WstModule*>::iterator it= ctx->modules.begin();
         it != ctx->modules.end();
         ++it )
   {
      bool result;
      WstModule *module= (*it);

      DEBUG("calling moduleInit for module (%s)...", module->moduleName );
      result= module->initEntryPoint( wctx, ctx->display );
      DEBUG("done calling moduleInit for module (%s) result %d", module->moduleName, result );
      if ( result )
      {
         module->isInitialized= true;
      }
   }
   
   startupAborted= false;
   ctx->compositorReady= true;   
   ctx->needRepaint= true;

   DEBUG("calling wl_display_run for display: %s", ctx->displayName );
   wl_display_run(ctx->display);
   DEBUG("done calling wl_display_run for display: %s", ctx->displayName );

   for ( std::vector<WstModule*>::iterator it= ctx->modules.begin();
         it != ctx->modules.end();
         ++it )
   {
      WstModule *module= (*it);

      if ( module->isInitialized )
      {
         DEBUG("calling moduleTerm for module (%s)...", module->moduleName );
         module->termEntryPoint( wctx );
         DEBUG("done calling moduleTerm for module (%s)", module->moduleName );
      }
   }

exit:

   if ( ctx->dcClient )
   {
      wl_client_destroy( ctx->dcClient );
      usleep( 100000 );
      ctx->dcClient= 0;
      ctx->dcPid= 0;
      ctx->dcDefaultCursor= false;
   }

   pthread_mutex_lock( &ctx->mutex );
   while( ctx->clientInfoMap.size() >  0 )
   {
      std::map<struct wl_client*,WstClientInfo*>::iterator it= ctx->clientInfoMap.begin();
      wl_client *client= it->first;
      WstClientInfo *clientInfo= it->second;
      ctx->clientInfoMap.erase( it );
      if ( client )
      {
         pthread_mutex_unlock( &ctx->mutex );
         wl_client_destroy( client );
         pthread_mutex_lock( &ctx->mutex );
      }
      free( clientInfo );
   }

   while( !ctx->surfaces.empty() )
   {
      WstSurface* surface= ctx->surfaces.back();
      ctx->surfaces.pop_back();
      if ( !surface || !surface->resource )
      {
         continue;
      }
      struct wl_client *client= wl_resource_get_client( surface->resource );
      if ( client )
      {
         pthread_mutex_unlock( &ctx->mutex );
         wl_client_destroy( client );
         pthread_mutex_lock( &ctx->mutex );
      }
   }
   pthread_mutex_unlock( &ctx->mutex );

   if ( startupAborted )
   {
      ctx->compositorAborted= true;
   }

   ctx->compositorReady= false;
     
   DEBUG("display: %s terminating...", ctx->displayName );

   if ( ctx->displayTimer )
   {
      wl_event_source_remove( ctx->displayTimer );
      ctx->displayTimer= 0;
   }
      
   return NULL;
}

static bool wstCompositorCreateRenderer( WstContext *ctx )
{
   bool result= false;
   int argc;
   char arg0[MAX_NESTED_NAME_LEN+1];
   char arg1[MAX_NESTED_NAME_LEN+1];
   char arg2[MAX_NESTED_NAME_LEN+1];
   char arg3[MAX_NESTED_NAME_LEN+1];
   char *argv[4]= { arg0, arg1, arg2, arg3 };

   if ( ctx->isNested || ctx->hasVpcBridge )
   {
      int width= ctx->nestedWidth;
      int height= ctx->nestedHeight;

      if ( ctx->isRepeater || ctx->hasVpcBridge )
      {
         width= 0;
         height= 0;
      }

      argc= 4;
      strcpy( arg0, "--width" );
      sprintf( arg1, "%u", ctx->wctx->outputWidth );
      strcpy( arg2, "--height" );
      sprintf( arg3, "%u", ctx->wctx->outputHeight );
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

   ctx->renderer= WstRendererCreate( ctx->rendererModule, argc, (char **)argv, ctx->display, ctx->nc );
   if ( !ctx->renderer )
   {
      ERROR("unable to initialize renderer module");
      goto exit;
   }

   result= true;

exit:

   return result;
}

static void wstCompositorReleaseResources( WstContext *ctx )
{
   if ( ctx->display )
   {
      wl_display_flush_clients(ctx->display);
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

   if ( ctx->canRemoteClone && ctx->ncDisplay )
   {
      ctx->remoteEnd( ctx->display, ctx->ncDisplay );
   }

   if ( ctx->nc )
   {
      WstNestedConnectionReleaseRemoteBuffers( ctx->nc );
      pthread_mutex_unlock( &ctx->mutex );
      WstNestedConnectionDisconnect( ctx->nc );
      pthread_mutex_lock( &ctx->mutex );
   }

   if ( ctx->isRepeater )
   {
      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      ctx->eglUnbindWaylandDisplayWL( ctx->eglDisplay, ctx->display );
      #endif
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
}

static bool wstCompositorCheckForRepeaterSupport( WstContext *ctx )
{
   bool supportsRepeater= false;

   #if defined (WESTEROS_HAVE_WAYLAND_EGL)
   ctx->eglBindWaylandDisplayWL= (PFNEGLBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglBindWaylandDisplayWL");
   ctx->eglUnbindWaylandDisplayWL= (PFNEGLUNBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglUnbindWaylandDisplayWL");
   ctx->eglQueryWaylandBufferWL= (PFNEGLQUERYWAYLANDBUFFERWL)eglGetProcAddress("eglQueryWaylandBufferWL");

   #if defined (WESTEROS_PLATFORM_RPI)
   ctx->mustInitRendererModule= true;
   if ( ctx->rendererModule )
   {
      free( (void*)ctx->rendererModule );
      ctx->rendererModule= 0;
   }
   ctx->rendererModule= strdup("libwesteros_render_gl.so.0");
   ctx->getDeviceBufferFromResource= (PFNGETDEVICEBUFFERFROMRESOURCE)vc_dispmanx_get_handle_from_wl_buffer;
   #else
   void *module;

   module= dlopen( "libwayland-egl.so.0", RTLD_NOW );
   if ( module )
   {
      ctx->getDeviceBufferFromResource= (PFNGETDEVICEBUFFERFROMRESOURCE)dlsym( module, "wl_egl_get_device_buffer" );
      ctx->remoteBegin= (PFNREMOTEBEGIN)dlsym( module, "wl_egl_remote_begin" );
      ctx->remoteEnd= (PFNREMOTEEND)dlsym( module, "wl_egl_remote_end" );
      ctx->remoteCloneBufferFromResource= (PFNREMOTECLONEBUFFERFROMRESOURCE)dlsym( module, "wl_egl_remote_buffer_clone" );

      if ( (ctx->remoteBegin != 0) &&
           (ctx->remoteEnd != 0) &&
           (ctx->remoteCloneBufferFromResource != 0) )
      {
         ctx->canRemoteClone= true;
      }
      dlclose( module );
   }
   #endif

   if ( ctx->mustInitRendererModule )
   {
      ctx->canRemoteClone= false;
   }

   if ( (ctx->eglBindWaylandDisplayWL != 0) &&
        (ctx->eglUnbindWaylandDisplayWL != 0) &&
        (ctx->eglQueryWaylandBufferWL != 0) &&
        ( (ctx->getDeviceBufferFromResource != 0) ||
          (ctx->canRemoteClone == true) )
      )
   {
      supportsRepeater= true;
   }

exit:
   #endif

   INFO("checking repeating composition supported: %s", (supportsRepeater ? "yes" : "no") );

   return supportsRepeater;
}

static long long wstGetCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static void wstCompositorDestroyVirtual( WstCompositor *wctx )
{
   WstContext *ctx= wctx->ctx;
   struct wl_resource *resource;
   struct wl_client *client;
   std::vector<struct wl_resource*> resources= std::vector<struct wl_resource*>();
   std::vector<struct wl_client*> clients= std::vector<struct wl_client*>();

   std::vector<WstSurface *>::iterator it = ctx->surfaces.begin();
   while( it != ctx->surfaces.end() )
   {
      WstSurface *surface= (*it);
      if ( surface->compositor == wctx )
      {
         it= ctx->surfaces.erase( it );

         if ( surface->resource )
         {
            bool found= false;
            client= wl_resource_get_client( surface->resource );
            for( int i= 0; i < clients.size(); ++i )
            {
               if ( clients[i] == client )
               {
                  found= true;
                  break;
               }
            }
            if ( !found )
            {
               clients.push_back( client );
            }
            resources.push_back( surface->resource );
         }
         else
         {
            wstSurfaceDestroy( surface );
         }
      }
      else
      {
         ++it;
      }
   }

   pthread_mutex_unlock( &ctx->mutex );
   while( resources.size() )
   {
      resource= resources.back();
      resources.pop_back();
      wl_resource_destroy(resource);
   }

   while( clients.size() )
   {
      client= clients.back();
      clients.pop_back();
      wl_client_destroy( client );
   }
   pthread_mutex_lock( &ctx->mutex );

   free( wctx );
}

static void wstCompositorProcessEvents( WstCompositor *wctx )
{
   int i;
   WstContext *ctx= wctx->ctx;

   if ( wctx->outputSizeChanged )
   {
      wstOutputChangeSize( wctx );
   }
   
   for( i= 0; i < wctx->eventIndex; ++i )
   {
      switch( wctx->eventQueue[i].type )
      {
         case WstEventType_key:
            {
               WstKeyboard *keyboard= wctx->keyboard;

               if ( keyboard )
               {
                  wstProcessKeyEvent( keyboard,
                                      wctx->eventQueue[i].v1, //keyCode
                                      wctx->eventQueue[i].v2, //keyState
                                      wctx->eventQueue[i].v3  //modifiers
                                    );
               }
            }
            break;
         case WstEventType_keyCode:
            {
               WstKeyboard *keyboard= wctx->keyboard;

               if ( keyboard )
               {
                  uint32_t serial;
                  struct wl_resource *resource;

                  serial= wl_display_next_serial( ctx->display );
                  wl_resource_for_each( resource, &keyboard->focusResourceList )
                  {
                     wl_keyboard_send_key( resource,
                                           serial,
                                           wctx->eventQueue[i].v1,  //time
                                           wctx->eventQueue[i].v2,  //key
                                           wctx->eventQueue[i].v3   //state
                                         );
                  }
               }
            }
            break;
         case WstEventType_keyModifiers:
            {
               WstKeyboard *keyboard= wctx->keyboard;

               if ( keyboard )
               {
                  uint32_t serial;
                  struct wl_resource *resource;
                  
                  serial= wl_display_next_serial( ctx->display );
                  wl_resource_for_each( resource, &keyboard->focusResourceList )
                  {
                     wl_keyboard_send_modifiers( resource,
                                                 serial,
                                                 wctx->eventQueue[i].v1, // mod depressed
                                                 wctx->eventQueue[i].v2, // mod latched
                                                 wctx->eventQueue[i].v3, // mod locked
                                                 wctx->eventQueue[i].v4  // mod group
                                               );
                  }
               }
            }
            break;
         case WstEventType_pointerEnter:
            {
               WstPointer *pointer= wctx->pointer;
               
               if ( pointer )
               {
                  wstProcessPointerEnter( pointer,
                                          wctx->eventQueue[i].v1, //x
                                          wctx->eventQueue[i].v2, //y
                                          (struct wl_surface*)wctx->eventQueue[i].p1  //surfaceNested
                                        );
               }
            }
            break;
         case WstEventType_pointerLeave:
            {
               WstPointer *pointer= wctx->pointer;
               
               if ( pointer )
               {
                  wstProcessPointerLeave( pointer,
                                          (struct wl_surface*)wctx->eventQueue[i].p1  //surfaceNested
                                        );
               }
            }
            break;
         case WstEventType_pointerMove:
            {
               WstPointer *pointer= wctx->pointer;
               
               if ( pointer )
               {
                  wstProcessPointerMoveEvent( pointer, 
                                              wctx->eventQueue[i].v1, //x
                                              wctx->eventQueue[i].v2  //y
                                            );
               }
            }
            break;
         case WstEventType_pointerButton:
            {
               WstPointer *pointer= wctx->pointer;
               
               uint32_t time;
               
               if ( wctx->eventQueue[i].v3 )
               {
                  time= wctx->eventQueue[i].v4;
               }
               else
               {
                  time= (uint32_t)wstGetCurrentTimeMillis();
               }
               
               if ( pointer )
               {
                  wstProcessPointerButtonEvent( pointer, 
                                                wctx->eventQueue[i].v1, //button
                                                wctx->eventQueue[i].v2, //buttonState
                                                time
                                               );
               }
            }
            break;
         case WstEventType_touchDown:
            {
               WstTouch *touch= wctx->touch;
               if ( touch )
               {
                  wstProcessTouchDownEvent( touch,
                                            wctx->eventQueue[i].v1, //time
                                            wctx->eventQueue[i].v2, //id
                                            wctx->eventQueue[i].v3, //x
                                            wctx->eventQueue[i].v4, //y
                                            (struct wl_surface*)wctx->eventQueue[i].p1  //surfaceNested
                                          );
               }
            }
            break;
         case WstEventType_touchUp:
            {
               WstTouch *touch= wctx->touch;
               if ( touch )
               {
                  wstProcessTouchUpEvent( touch,
                                          wctx->eventQueue[i].v1, //time
                                          wctx->eventQueue[i].v2  //id
                                          );
               }
            }
            break;
         case WstEventType_touchMotion:
            {
               WstTouch *touch= wctx->touch;
               if ( touch )
               {
                  wstProcessTouchMotionEvent( touch,
                                              wctx->eventQueue[i].v1, //time
                                              wctx->eventQueue[i].v2, //id
                                              wctx->eventQueue[i].v3, //x
                                              wctx->eventQueue[i].v4  //y
                                            );
               }
            }
            break;
         case WstEventType_touchFrame:
            {
               WstTouch *touch= wctx->touch;
               if ( touch )
               {
                  wstProcessTouchFrameEvent( touch );
               }
            }
            break;
         case WstEventType_resolutionChangeBegin:
            {
               if ( ctx->renderer )
               {
                  WstRendererResolutionChangeBegin( ctx->renderer );
               }
            }
            break;
         case WstEventType_resolutionChangeEnd:
            {
               int width, height;

               width= wctx->eventQueue[i].v1;
               height= wctx->eventQueue[i].v2;

               if ( !ctx->isNested && !ctx->isEmbedded )
               {
                  if ( ctx->renderer )
                  {
                     WstRendererResolutionChangeEnd( ctx->renderer );
                  }
               }
            }
            break;
         default:
            WARNING("wstCompositorProcessEvents: unknown event type %d", wctx->eventQueue[i].type );
            break;
      }
   }
   wctx->eventIndex= 0;
}

static void wstContextProcessEvents( WstContext *ctx )
{
   pthread_mutex_lock( &ctx->mutex );

   if ( ctx->nc )
   {
      WstNestedConnectionReleaseRemoteBuffers( ctx->nc );
   }

   std::vector<WstCompositor*>::iterator it= ctx->virt.begin();
   while ( it != ctx->virt.end() )
   {
      WstCompositor *wctx= (*it);
      if ( wctx->destroyed )
      {
         it= ctx->virt.erase( it );
         wstCompositorDestroyVirtual( wctx );
      }
      else
      {
         wstCompositorProcessEvents( wctx );
         ++it;
      }
   }

   wstCompositorProcessEvents( ctx->wctx );

   pthread_mutex_unlock( &ctx->mutex );
}

static void wstCompositorComposeFrame( WstContext *ctx, uint32_t frameTime )
{
   pthread_mutex_lock( &ctx->mutex );

   ctx->needRepaint= false;

   if ( !ctx->isEmbedded && !ctx->isRepeater )
   {
      WstRendererUpdateScene( ctx->renderer );
      wstCompositorReleaseDetachedBuffers( ctx );
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

static void wstContextInvokeDispatchCB( WstContext *ctx )
{
   WstCompositor *wctx;
   for ( std::vector<WstCompositor*>::iterator it= ctx->virt.begin();
         it != ctx->virt.end();
         ++it )
   {
      wctx= (*it);
      if ( wctx->dispatchCB )
      {
         wctx->dispatchCB( wctx, wctx->dispatchUserData );
      }
   }
   wctx= ctx->wctx;
   if ( wctx->dispatchCB )
   {
      wctx->dispatchCB( wctx, wctx->dispatchUserData );
   }
}

static void wstContextInvokeInvalidateCB( WstContext *ctx )
{
   WstCompositor *wctx;
   for ( std::vector<WstCompositor*>::iterator it= ctx->virt.begin();
         it != ctx->virt.end();
         ++it )
   {
      wctx= (*it);
      if ( wctx->invalidateCB )
      {
         wctx->invalidateCB( wctx, wctx->invalidateUserData );
      }
   }
   wctx= ctx->wctx;
   if ( wctx->invalidateCB )
   {
      wctx->invalidateCB( wctx, wctx->invalidateUserData );
   }
}

static void wstContextInvokeHidePointerCB( WstContext *ctx, bool hidePointer )
{
   WstCompositor *wctx;
   for ( std::vector<WstCompositor*>::iterator it= ctx->virt.begin();
         it != ctx->virt.end();
         ++it )
   {
      wctx= (*it);
      if ( wctx->hidePointerCB )
      {
         wctx->hidePointerCB( wctx, hidePointer, wctx->hidePointerUserData );
      }
   }
   wctx= ctx->wctx;
   if ( wctx->hidePointerCB )
   {
      wctx->hidePointerCB( wctx, hidePointer, wctx->hidePointerUserData );
   }
}

static int wstCompositorDisplayTimeOut( void *data )
{
   WstContext *ctx= (WstContext*)data;
   long long frameTime, now;
   long long nextFrameDelay;
   
   frameTime= wstGetCurrentTimeMillis();   
   
   wstContextProcessEvents( ctx );

   wstContextInvokeDispatchCB( ctx );
   
   if ( ctx->needRepaint )
   {
      ctx->allowImmediateRepaint= false;

      wstCompositorComposeFrame( ctx, (uint32_t)frameTime );
      
      wstContextInvokeInvalidateCB( ctx );
   }
   else
   {
      ctx->allowImmediateRepaint= true;
   }

   now= wstGetCurrentTimeMillis();
   nextFrameDelay= (ctx->framePeriodMillis-(now-frameTime));
   if ( nextFrameDelay < 1 ) nextFrameDelay= 1;
   if ( nextFrameDelay > ctx->framePeriodMillis ) nextFrameDelay= ctx->framePeriodMillis;

   pthread_mutex_lock( &ctx->mutex );
   wl_event_source_timer_update( ctx->displayTimer, nextFrameDelay );
   pthread_mutex_unlock( &ctx->mutex );
   
   return 0;
}

static void wstCompositorScheduleRepaint( WstContext *ctx )
{
   if ( !ctx->needRepaint )
   {
      ctx->needRepaint= true;
      if ( ctx->allowImmediateRepaint && ctx->displayTimer )
      {
         wl_event_source_timer_update( ctx->displayTimer, 1 );
      }
   }
}

static void wstCompositorReleaseDetachedBuffers( WstContext *ctx )
{
   for ( std::vector<WstSurface*>::iterator it= ctx->surfaces.begin();
         it != ctx->surfaces.end();
         ++it )
   {
      WstSurface *surface= (*it);
      if ( surface->detachedBufferResource )
      {
         wl_list_remove(&surface->detachedBufferDestroyListener.link);
         wl_buffer_send_release( surface->detachedBufferResource );
         surface->detachedBufferResource= 0;
      }
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

static const struct wl_touch_interface touch_interface=
{
   wstITouchRelease
};

static const struct wl_vpc_interface vpc_interface_impl=
{
   wstIVpcGetVpcSurface
};

static const struct wl_vpc_surface_interface vpc_surface_interface= 
{
   wstIVpcSurfaceSetGeometry
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

static bool wstShmInit( WstContext *ctx )
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
         ctx->shm->ctx= ctx;
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

static void wstShmTerm( WstContext *ctx )
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
      buffer->bufferNested= WstNestedConnectionShmPoolCreateBuffer( pool->shm->ctx->nc,
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
         WstNestedConnectionShmBufferPoolDestroy( buffer->pool->shm->ctx->nc, buffer->pool->poolNested, buffer->bufferNested );
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

      WstNestedConnectionShmPoolResize( shm->ctx->nc, pool->poolNested, size );
   }
}

static void wstIShmCreatePool( struct wl_client *client, struct wl_resource *resource,
                              uint32_t id, int fd, int32_t size )
{
   WstShm *shm= (WstShm*)wl_resource_get_user_data(resource);
   
   if ( shm->ctx )
   {
      WstNestedConnection *nc;
      
      nc= shm->ctx->nc;
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

static void wstShmDestroyPool( struct wl_resource *resource )
{
   WstShmPool *pool= (WstShmPool*)wl_resource_get_user_data(resource);
   
   if ( pool )
   {
      WstShm *shm= pool->shm;
      
      wstShmPoolUnRef( pool );
   }
}

static void wstShmPoolUnRef( WstShmPool *pool )
{
   if ( pool )
   {
      --pool->refCount;
      if ( !pool->refCount )
      {
         WstShm *shm= pool->shm;
         if ( pool->poolNested )
         {
            WstNestedConnectionShmDestroyPool( shm->ctx->nc, pool->poolNested );
         }
         free( pool );
      }
   }
}

static WstCompositor *wstGetCompositorFromPid( WstContext *ctx, struct wl_client *client, int pid )
{
   WstCompositor *wctx= ctx->wctx;
   if ( ctx->isEmbedded )
   {
      if ( ctx->virt.size() )
      {
         bool found= false;
         int pidNext= pid;
         INFO("searching for virtual embedded compositor for client %p pid %d", client, pid);
         while( !found )
         {
            for ( std::vector<WstCompositor*>::iterator it= ctx->virt.begin();
                  it != ctx->virt.end();
                  ++it )
            {
               WstCompositor *check= (*it);
               if ( check->clientPid == pidNext )
               {
                  found= true;
                  wctx= check;
                  INFO("found virtual embedded compositor %p for client %p pid %d", wctx, client, pid);
                  break;
               }
            }
            if ( !found )
            {
               FILE *pFile;
               char work[128];
               sprintf(work,"/proc/%d/stat",pidNext);
               pFile= fopen( work, "rt" );
               if ( pFile )
               {
                  int c, i= 0, spacecnt= 0;
                  pidNext= 1;
                  for( ; ; )
                  {
                     c= fgetc( pFile );
                     if ( c == ' ' )
                     {
                        ++spacecnt;
                        if ( spacecnt == 4 )
                        {
                           work[i]= 0;
                           pidNext= atoi( work );
                           break;
                        }
                     }
                     else if ( c == EOF )
                     {
                        break;
                     }
                     else if ( spacecnt == 3 )
                     {
                        work[i++]= c;
                     }
                  }
                  fclose( pFile );
               }
               else
               {
                  pidNext= 1;
               }
               if ( pidNext == 1 )
               {
                  break;
               }
            }
         }

         if ( !found )
         {
            INFO("virtual embedded compositor not found for client %p pid %d", client, pid);
         }
      }
   }

   return wctx;
}

static WstCompositor* wstGetCompositorFromClient( WstContext *ctx, struct wl_client *client )
{
   WstCompositor *wctx= 0;
   WstClientInfo *clientInfo= 0;
   int pid= 0;

   std::map<struct wl_client*,WstClientInfo*>::iterator it= ctx->clientInfoMap.find( client );
   if ( it != ctx->clientInfoMap.end() )
   {
      clientInfo= it->second;
      wctx= clientInfo->wctx;
   }

   if ( !wctx )
   {
      wl_client_get_credentials( client, &pid, NULL, NULL );
      wctx= wstGetCompositorFromPid( ctx, client, pid );
      if ( wctx && clientInfo )
      {
         clientInfo->wctx= wctx;
      }
   }

   return wctx;
}

static void wstCompositorBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstContext *ctx= (WstContext*)data;
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

   wstUpdateClientInfo( ctx, client, 0 );
   
   int pid= 0;
   wl_client_get_credentials( client, &pid, NULL, NULL );
   INFO("display %s client pid %d connected", ctx->displayName, pid );
   WstCompositor *wctx= wstGetCompositorFromClient( ctx, client );
   if ( wctx && wctx->clientStatusCB )
   {
      wctx->clientStatusCB( wctx, WstClient_connected, pid, 0, wctx->clientStatusUserData );
   }
}

static void wstDestroyCompositorCallback(struct wl_resource *resource)
{
   WstContext *ctx= (WstContext*)wl_resource_get_user_data(resource);
   if ( !ctx || !ctx->compositorReady )
      return;

   int pid= 0;
   struct wl_client *client= wl_resource_get_client(resource);
   wl_client_get_credentials( client, &pid, NULL, NULL );
   INFO("display %s client pid %d disconnected", ctx->displayName, pid );
   WstCompositor *wctx= wstGetCompositorFromClient( ctx, client );
   if ( wctx && wctx->clientStatusCB )
   {
      wctx->clientStatusCB( wctx, WstClient_disconnected, pid, 0, wctx->clientStatusUserData );
   }

   for( std::map<struct wl_client*,WstClientInfo*>::iterator it= ctx->clientInfoMap.begin(); it != ctx->clientInfoMap.end(); ++it )
   {
      if ( it->first == client )
      {
          WstClientInfo *clientInfo= (WstClientInfo*)it->second;
          ctx->clientInfoMap.erase( it );
          free( clientInfo );
          break;
      }
   }
}

static void wstICompositorCreateSurface( struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   WstContext *ctx= (WstContext*)wl_resource_get_user_data(resource);
   WstCompositor *wctx= 0;
   WstSurface *surface;
   WstSurfaceInfo *surfaceInfo;
   int clientId;
   
   pthread_mutex_lock( &ctx->mutex );

   wctx= wstGetCompositorFromClient( ctx, client );
   if ( !wctx )
   {
      wl_resource_post_no_memory(resource);
      pthread_mutex_unlock( &ctx->mutex );
      return;
   }

   surface= wstSurfaceCreate(wctx);
   if (!surface) 
   {
      wl_resource_post_no_memory(resource);
      pthread_mutex_unlock( &ctx->mutex );
      return;
   }

   surface->resource= wl_resource_create(client, &wl_surface_interface,
                                         wl_resource_get_version(resource), id);
   if (!surface->resource)
   {
      wstSurfaceDestroy(surface);
      wl_resource_post_no_memory(resource);
      pthread_mutex_unlock( &ctx->mutex );
      return;
   }
   surfaceInfo= wstGetSurfaceInfo( ctx, surface->resource );
   if ( !surfaceInfo )
   {
      wstSurfaceDestroy(surface);
      wl_resource_post_no_memory(resource);
      pthread_mutex_unlock( &ctx->mutex );
      return;
   }
   wl_resource_set_implementation(surface->resource, &surface_interface, surface, wstDestroySurfaceCallback);

   surfaceInfo->surface= surface;
   
   wstUpdateClientInfo( ctx, client, 0 );
   ctx->clientInfoMap[client]->surface= surface;
      
   DEBUG("wstICompositorCreateSurface: client %p resource %p id %d : surface resource %p", client, resource, id, surface->resource );

   if ( ctx->seat && wctx->keyboard )
   {
      wstKeyboardCheckFocus( wctx->keyboard, surface );
   }
   
   pthread_mutex_unlock( &ctx->mutex );

   if ( ctx->simpleShell )
   {
      WstSimpleShellNotifySurfaceCreated( ctx->simpleShell, client, surface->resource, surface->surfaceId );
   }
}

static void wstICompositorCreateRegion(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   WstContext *ctx= (WstContext*)wl_resource_get_user_data(resource);
   WstCompositor *wctx= 0;
   WstRegion *region;

   WARNING("wstICompositorCreateRegion: wl_region is currently a stub impl");

   wctx= wstGetCompositorFromClient( ctx, client );
   if ( !wctx )
   {
      wl_resource_post_no_memory(resource);
      return;
   }

   region= wstRegionCreate( wctx );
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

static void wstAttachedBufferDestroyCallback(struct wl_listener *listener, void *data )
{
   WstSurface *surface= wl_container_of(listener, surface, attachedBufferDestroyListener );
   surface->attachedBufferResource= 0;
}

static void wstDetachedBufferDestroyCallback(struct wl_listener *listener, void *data )
{
   WstSurface *surface= wl_container_of(listener, surface, detachedBufferDestroyListener );
   surface->detachedBufferResource= 0;
}

static void wstDestroySurfaceCallback(struct wl_resource *resource)
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);

   assert(surface);

   if ( surface->compositor && surface->compositor->ctx )
   {
      WstContext *ctx= surface->compositor->ctx;

      if ( ctx->simpleShell )
      {
         WstSimpleShellNotifySurfaceDestroyed( ctx->simpleShell, wl_resource_get_client(resource), surface->surfaceId );
      }

      pthread_mutex_lock( &ctx->mutex );

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

      pthread_mutex_unlock( &ctx->mutex );
   }
}

static WstSurface* wstSurfaceCreate( WstCompositor *wctx)
{
   WstSurface *surface= 0;
   WstContext *ctx= wctx->ctx;
   
   surface= (WstSurface*)calloc( 1, sizeof(WstSurface) );
   if ( surface )
   {
      surface->compositor= wctx;
      surface->refCount= 1;
      
      surface->surfaceId= ctx->nextSurfaceId++;
      ctx->surfaceMap.insert( std::pair<int32_t,WstSurface*>( surface->surfaceId, surface ) );

      wl_list_init(&surface->frameCallbackList);

      surface->attachedBufferDestroyListener.notify= wstAttachedBufferDestroyCallback;
      surface->detachedBufferDestroyListener.notify= wstDetachedBufferDestroyCallback;

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

         if ( surface )
         {
            WstRendererSurfaceGetZOrder( ctx->renderer, surface->surface, &surface->zorder );
         }
      }

      if ( surface )
      {
         wstSurfaceInsertSurface( ctx, surface );
      }
   }
   
   return surface;
}

static void wstSurfaceDestroy( WstSurface *surface )
{
   WstCompositor *wctx= surface->compositor;
   WstContext *ctx= wctx->ctx;
   WstSurfaceFrameCallback *fcb;
   
   DEBUG("wstSurfaceDestroy: surface %p refCount %d", surface, surface->refCount );
   
   if (--surface->refCount > 0)
      return;

   // Release any attached or detached buffer
   if ( surface->attachedBufferResource || surface->detachedBufferResource )
   {
      if ( surface->detachedBufferResource )
      {
         wl_list_remove(&surface->detachedBufferDestroyListener.link);
         wl_buffer_send_release( surface->detachedBufferResource );
      }
      if ( surface->attachedBufferResource )
      {
         wl_list_remove(&surface->attachedBufferDestroyListener.link);
         wl_buffer_send_release( surface->attachedBufferResource );
      }
      surface->attachedBufferResource= 0;
      surface->detachedBufferResource= 0;
   }

   // Remove from keyboard focus
   if ( wctx->keyboard &&
        (wctx->keyboard->focus == surface) )
   {
      wctx->keyboard->focus= 0;
   }

   // Remove from pointer focus
   if ( wctx->pointer &&
        (wctx->pointer->focus == surface) )
   {
      wctx->pointer->focus= 0;
      if ( !ctx->dcDefaultCursor )
      {
         wstPointerSetPointer( wctx->pointer, 0 );
      }
   }

   // Remove from touch focus
   if ( wctx->touch &&
        (wctx->touch->focus == surface) )
   {
      wctx->touch->focus= 0;
   }
   
   // Remove as pointer surface
   if ( wctx->pointer &&
        (wctx->pointer->pointerSurface == surface) )
   {
      wstPointerSetPointer( wctx->pointer, 0 );
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
         clientInfo->surface= 0;
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

   if ( wctx->pointer )
   {
      pthread_mutex_unlock( &ctx->mutex );
      wstPointerCheckFocus( wctx->pointer, wctx->pointer->pointerX, wctx->pointer->pointerY );
      pthread_mutex_lock( &ctx->mutex );
   }
   
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

static void wstSurfaceInsertSurface( WstContext *ctx, WstSurface *surface )
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

static WstSurface* wstGetSurfaceFromSurfaceId( WstContext *ctx, int32_t surfaceId )
{
   WstSurface *surface= 0;
   
   std::map<int32_t,WstSurface*>::iterator it= ctx->surfaceMap.find( surfaceId );
   if ( it != ctx->surfaceMap.end() )
   {
      surface= it->second;
   }
   
   return surface;
}

static WstSurface* wstGetSurfaceFromPoint( WstCompositor *wctx, int x, int y )
{
   WstSurface *surface= 0;
   int sx=0, sy=0, sw=0, sh=0;
   bool haveRoles= false;
   WstSurface *surfaceNoRole= 0;
   WstContext *ctx= wctx->ctx;

   // Identify top-most surface containing the pointer position
   for ( std::vector<WstSurface*>::reverse_iterator it= ctx->surfaces.rbegin();
         it != ctx->surfaces.rend();
         ++it )
   {
      surface= (*it);

      if ( surface->compositor != wctx ) continue;

      WstRendererSurfaceGetGeometry( ctx->renderer, surface->surface, &sx, &sy, &sw, &sh );

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
         else if ( surface->vpcSurface )
         {
            eligible= false;
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

   return surface;
}

static WstSurfaceInfo* wstGetSurfaceInfo( WstContext *ctx, struct wl_resource *resource )
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

static void wstUpdateClientInfo( WstContext *ctx, struct wl_client *client, struct wl_resource *resource )
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
   WstContext *ctx= surface->compositor->ctx;

   DEBUG("wstISurfaceDestroy resource %p", resource );
   wl_resource_destroy(resource);

   std::map<struct wl_client*,WstClientInfo*>::iterator it= ctx->clientInfoMap.find( client );
   if ( it != ctx->clientInfoMap.end() )
   {
      WstClientInfo *clientInfo= it->second;
      clientInfo->surface= 0;
   }
}

static void wstISurfaceAttach(struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *bufferResource, int32_t sx, int32_t sy)
{
   WstSurface *surface= (WstSurface*)wl_resource_get_user_data(resource);
   WstContext *ctx= surface->compositor->ctx;

   pthread_mutex_lock( &ctx->mutex );
   if ( surface->attachedBufferResource != bufferResource )
   {
      if ( surface->detachedBufferResource )
      {
         wl_list_remove(&surface->detachedBufferDestroyListener.link);
         wl_buffer_send_release( surface->detachedBufferResource );
      }
      if ( surface->attachedBufferResource )
      {
         wl_list_remove(&surface->attachedBufferDestroyListener.link);
      }
      surface->detachedBufferResource= surface->attachedBufferResource;
      if ( surface->detachedBufferResource )
      {
         wl_resource_add_destroy_listener( surface->detachedBufferResource, &surface->detachedBufferDestroyListener );
      }
      surface->attachedBufferResource= 0;
   }
   if ( bufferResource )
   {
      if ( surface->attachedBufferResource != bufferResource )
      {
         surface->attachedBufferResource= bufferResource;
         wl_resource_add_destroy_listener( surface->attachedBufferResource, &surface->attachedBufferDestroyListener );
      }
      surface->attachedX= sx;
      surface->attachedY= sy;
   }
   else
   {
      surface->vpcBridgeSignal= ((sx == VPCBRIDGE_SIGNAL) && (sy == VPCBRIDGE_SIGNAL));
   }
   pthread_mutex_unlock( &ctx->mutex );
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
   WstContext *ctx= surface->compositor->ctx;
   struct wl_resource *committedBufferResource;

   pthread_mutex_lock( &ctx->mutex );

   committedBufferResource= surface->attachedBufferResource;
   if ( surface->attachedBufferResource )
   {      
      if ( ctx->isRepeater )
      {
         int bufferWidth= 0, bufferHeight= 0;

         if ( wl_resource_instance_of( surface->attachedBufferResource, &wl_buffer_interface, &shm_buffer_interface ) )
         {
            WstShmBuffer *buffer= (WstShmBuffer*)wl_resource_get_user_data(surface->attachedBufferResource);
            if ( buffer )
            {
               bufferWidth= buffer->width;
               bufferHeight= buffer->height;
               
               WstNestedConnectionAttachAndCommit( ctx->nc,
                                                   surface->surfaceNested,
                                                   buffer->bufferNested,
                                                   0,
                                                   0,
                                                   bufferWidth,
                                                   bufferHeight );
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
               int stride;
               uint32_t format;

               deviceBuffer= WstSBBufferGetBuffer( sbBuffer );
               
               bufferWidth= WstSBBufferGetWidth( sbBuffer );
               bufferHeight= WstSBBufferGetHeight( sbBuffer );
               format= WstSBBufferGetFormat( sbBuffer );
               stride= WstSBBufferGetStride( sbBuffer );
               
               WstNestedConnectionAttachAndCommitDevice( ctx->nc,
                                                   surface->surfaceNested,
                                                   0,
                                                   deviceBuffer,
                                                   format,
                                                   stride,
                                                   0,
                                                   0,
                                                   bufferWidth,
                                                   bufferHeight );
            }
         }
         #endif
         #if defined (WESTEROS_HAVE_WAYLAND_EGL)
         if ( ctx->canRemoteClone )
         {
            struct wl_display *nestedDisplay;
            struct wl_buffer *clone;

            if ( ctx->ncDisplay )
            {
               clone= ctx->remoteCloneBufferFromResource( ctx->display,
                                                          surface->attachedBufferResource,
                                                          ctx->ncDisplay,
                                                          &bufferWidth,
                                                          &bufferHeight );
               if ( clone )
               {
                  WstNestedConnectionAttachAndCommitClone( ctx->nc,
                                                           surface->surfaceNested,
                                                           surface->attachedBufferResource,
                                                           clone,
                                                           0,
                                                           0,
                                                           bufferWidth,
                                                           bufferHeight );
                  wl_list_remove(&surface->attachedBufferDestroyListener.link);
                  surface->attachedBufferResource= 0;
               }
            }
         }
         #if defined (ENABLE_SBPROTOCOL)
         else
         if ( ctx->getDeviceBufferFromResource &&
              ctx->getDeviceBufferFromResource( surface->attachedBufferResource ) )
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
               int stride;
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
            
               deviceBuffer= (void*)ctx->getDeviceBufferFromResource( surface->attachedBufferResource );
               if ( deviceBuffer &&
                    (format != 0) &&
                    (bufferWidth != 0) &&
                    (bufferHeight != 0) )
               {
                  WstNestedConnectionAttachAndCommitDevice( ctx->nc,
                                                      surface->surfaceNested,
                                                      surface->attachedBufferResource,
                                                      deviceBuffer,
                                                      format,
                                                      stride,
                                                      0,
                                                      0,
                                                      bufferWidth, 
                                                      bufferHeight );

                  wl_list_remove(&surface->attachedBufferDestroyListener.link);
                  surface->attachedBufferResource= 0;
               }
            }
         }
         #endif
         #endif

         if ( (surface->width == 0) && (surface->height == 0) )
         {
            surface->width= bufferWidth;
            surface->height= bufferHeight;
         }
      }
      else
      {
         WstRendererSurfaceCommit( surface->renderer, surface->surface, surface->attachedBufferResource );
         if ( ctx->hasVpcBridge && surface->vpcSurface && surface->surfaceNested )
         {
            WstNestedConnectionAttachAndCommit( ctx->nc,
                                                surface->surfaceNested,
                                                0, // null buffer
                                                VPCBRIDGE_SIGNAL,
                                                VPCBRIDGE_SIGNAL,
                                                0, //width
                                                0  //height
                                              );
         }
      }      
   }
   else
   {
      int attachX, attachY;

      attachX= surface->vpcBridgeSignal ? VPCBRIDGE_SIGNAL : 0;
      attachY= surface->vpcBridgeSignal ? VPCBRIDGE_SIGNAL : 0;

      if ( ctx->isRepeater )
      {
         WstNestedConnectionAttachAndCommit( ctx->nc,
                                             surface->surfaceNested,
                                             0, // null buffer
                                             attachX,
                                             attachY,
                                             0, //width
                                             0  //height
                                           );
      }
      else
      {
         WstRendererSurfaceCommit( surface->renderer, surface->surface, 0 );
         if ( ctx->hasVpcBridge && surface->vpcSurface && surface->surfaceNested )
         {
            WstNestedConnectionAttachAndCommit( ctx->nc,
                                                surface->surfaceNested,
                                                0, // null buffer
                                                attachX,
                                                attachY,
                                                0, //width
                                                0  //height
                                              );
         }
      }
   }

   if ( surface->vpcSurface && surface->vpcSurface->pathTransitionPending )
   {
      if ( ((committedBufferResource || surface->vpcBridgeSignal) && !surface->vpcSurface->useHWPathNext) ||
           (!committedBufferResource && surface->vpcSurface->useHWPathNext) )
      {
         surface->vpcSurface->useHWPath= surface->vpcSurface->useHWPathNext;
         surface->vpcSurface->pathTransitionPending= false;
         if ( surface->renderer )
         {
            WstRendererSurfaceSetVisible( surface->renderer, surface->surface, !surface->vpcSurface->useHWPath );
         }
      }
   }

   wstCompositorScheduleRepaint( ctx );

   pthread_mutex_unlock( &ctx->mutex );
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

static WstRegion *wstRegionCreate( WstCompositor *wctx )
{
   WstRegion *region= 0;

   region= (WstRegion*)calloc( 1, sizeof(WstRegion) );
   if ( region )
   {
      region->compositor= wctx;
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

static bool wstOutputInit( WstContext *ctx )
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
   output->ctx= ctx;

   output->refreshRate= ctx->frameRate;
   output->mmWidth= ctx->wctx->outputWidth;
   output->mmHeight= ctx->wctx->outputHeight;
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

static void wstOutputTerm( WstContext *ctx )
{
   if ( ctx && ctx->output )
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
   WstCompositor *wctx;
   struct wl_resource *resource;
   
   DEBUG("wstOutputBind: client %p data %p version %d id %d", client, data, version, id );

   wctx= wstGetCompositorFromClient( output->ctx, client );
   if ( !wctx )
   {
      wl_client_post_no_memory(client);
      return;
   }

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
                        wctx->outputWidth,
                        wctx->outputHeight,
                        output->refreshRate );

   if ( version >= WL_OUTPUT_DONE_SINCE_VERSION )
   {
      wl_output_send_done( resource );   
   }
}

static void wstOutputChangeSize( WstCompositor *wctx )
{
   WstContext *ctx= wctx->ctx;
   WstOutput *output= ctx->output;
   struct wl_resource *resource;
   
   wctx->outputSizeChanged= false;
   
   if ( ctx->renderer )
   {
      wstCompositorScheduleRepaint(ctx);
      ctx->renderer->outputWidth= wctx->outputWidth;
      ctx->renderer->outputHeight= wctx->outputHeight;
   }

   wl_resource_for_each( resource, &output->resourceList )
   {
      wl_output_send_mode( resource,
                           WL_OUTPUT_MODE_CURRENT,
                           wctx->outputWidth,
                           wctx->outputHeight,
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
               wstXdgSurfaceSendConfigure( wctx, surface, XDG_SURFACE_STATE_FULLSCREEN );
            }
         }
      }
   }
   wl_display_flush_clients(ctx->display);
}

static void wstShellBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstContext *ctx= (WstContext*)data;
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
   WstContext *ctx= (WstContext*)data;
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

   wstUpdateClientInfo( ctx, client, 0 );
   ctx->clientInfoMap[client]->usesXdgShell= true;
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
   
   if ( surface->compositor->ctx->isEmbedded || surface->compositor->ctx->hasEmbeddedMaster )
   {
      WstCompositor *compositor= surface->compositor;
      struct wl_array states;
      uint32_t serial;
      uint32_t *entry;
      
      wl_array_init( &states );
      entry= (uint32_t*)wl_array_add( &states, sizeof(uint32_t) );
      *entry= XDG_SURFACE_STATE_FULLSCREEN;
      serial= wl_display_next_serial( compositor->ctx->display );
      xdg_surface_send_configure( shellSurface->resource,
                                  compositor->outputWidth,
                                  compositor->outputHeight,
                                  &states,
                                  serial );
                                  
      wl_array_release( &states );
   }

   WstCompositor *compositor= surface->compositor;
   if ( compositor )
   {
      pthread_mutex_lock( &compositor->ctx->mutex );
      if ( compositor->ctx->seat && compositor->keyboard )
      {
         wstKeyboardCheckFocus( compositor->keyboard, surface );
      }
      pthread_mutex_unlock( &compositor->ctx->mutex );
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

static void wstXdgSurfaceSendConfigure( WstCompositor *wctx, WstSurface *surface, uint32_t state )
{
   struct wl_array states;
   uint32_t serial;
   uint32_t *entry;
   
   wl_array_init( &states );
   entry= (uint32_t*)wl_array_add( &states, sizeof(uint32_t) );
   *entry= state;
   serial= wl_display_next_serial( wctx->ctx->display );

   for ( std::vector<WstShellSurface*>::iterator it= surface->shellSurface.begin(); 
         it != surface->shellSurface.end();
         ++it )
   {
      WstShellSurface *shellSurface= (*it);
      
      xdg_surface_send_configure( shellSurface->resource,
                                  wctx->outputWidth,
                                  wctx->outputHeight,
                                  &states,
                                  serial );
   }
                               
   wl_array_release( &states );               
}

static void wstDefaultNestedConnectionStarted( void *userData )
{
   WstContext *ctx= (WstContext*)userData;
   if ( ctx )
   {
      struct wl_display *ncDisplay;

      pthread_mutex_lock( &ctx->ncStartedMutex );

      ncDisplay= WstNestedConnectionGetDisplay( ctx->nc );

      if ( ctx->remoteBegin && ncDisplay )
      {
         if ( !ctx->remoteBegin( ctx->display, ncDisplay ) )
         {
            ERROR("remoteBegin failure");
            ctx->canRemoteClone= false;
         }
      }

      ctx->ncDisplay= ncDisplay;

      INFO("signal start of nested connection");

      pthread_cond_signal( &ctx->ncStartedCond );
      pthread_mutex_unlock( &ctx->ncStartedMutex );
   }
}

static void wstDefaultNestedConnectionEnded( void *userData )
{
   WstContext *ctx= (WstContext*)userData;
   if ( ctx )
   {
      if ( ctx->display )
      {
         wl_display_terminate(ctx->display);
      }
      if ( ctx->terminatedCB )
      {
         ctx->terminatedCB( ctx->wctx, ctx->terminatedUserData );
      }
   }
}

static void wstDefaultNestedOutputHandleGeometry( void *userData, int32_t x, int32_t y, int32_t mmWidth,
                                                  int32_t mmHeight, int32_t subPixel, const char *make, 
                                                  const char *model, int32_t transform )
{
   WstContext *ctx= (WstContext*)userData;
   
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
   WstContext *ctx= (WstContext*)userData;
   
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
            ctx->wctx->outputWidth= width;
            ctx->wctx->outputHeight= height;

            output->refreshRate= refreshRate;
            
            ctx->wctx->outputSizeChanged= true;
            pthread_mutex_unlock( &ctx->mutex );
         }
      }
   }
}

static void wstDefaultNestedOutputHandleDone( void *userData )
{
   WstContext *ctx= (WstContext*)userData;

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
   WstContext *ctx= (WstContext*)userData;
   
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
   WstContext *ctx= (WstContext*)userData;

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
   WstContext *ctx= (WstContext*)userData;

   if ( ctx )
   {
      if ( ctx->keyboardNestedListener )
      {
         ctx->keyboardNestedListener->keyboardHandleEnter( ctx->keyboardNestedListenerUserData,
                                                           keys );
      }
      else
      {
         if ( ctx->seat && ctx->wctx && ctx->wctx->keyboard )
         {
            pthread_mutex_lock( &ctx->mutex );
            wl_array_copy( &ctx->wctx->keyboard->keys, keys );
            pthread_mutex_unlock( &ctx->mutex );
         }
      }
   }   
}

static void wstDefaultNestedKeyboardHandleLeave( void *userData )
{
   WstContext *ctx= (WstContext*)userData;

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
   WstContext *ctx= (WstContext*)userData;

   if ( ctx )
   {
      if ( ctx->keyboardNestedListener )
      {
         ctx->keyboardNestedListener->keyboardHandleKey( ctx->keyboardNestedListenerUserData,
                                                         time, key, state );
      }
      else
      {
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         pthread_mutex_lock( &ctx->mutex );
         wctx->eventQueue[eventIndex].type= WstEventType_keyCode;
         wctx->eventQueue[eventIndex].v1= time;
         wctx->eventQueue[eventIndex].v2= key;
         wctx->eventQueue[eventIndex].v3= state;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );         
      }
   }   
}

static void wstDefaultNestedKeyboardHandleModifiers( void *userData, uint32_t mods_depressed, uint32_t mods_latched, 
                                                     uint32_t mods_locked, uint32_t group )
{
   WstContext *ctx= (WstContext*)userData;
   
   if ( ctx )
   {
      if ( ctx->keyboardNestedListener )
      {
         ctx->keyboardNestedListener->keyboardHandleModifiers( ctx->keyboardNestedListenerUserData,
                                                               mods_depressed, mods_latched, mods_locked, group );
      }
      else
      {
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         pthread_mutex_lock( &ctx->mutex );
         wctx->eventQueue[eventIndex].type= WstEventType_keyModifiers;
         wctx->eventQueue[eventIndex].v1= mods_depressed;
         wctx->eventQueue[eventIndex].v2= mods_latched;
         wctx->eventQueue[eventIndex].v3= mods_locked;
         wctx->eventQueue[eventIndex].v4= group;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedKeyboardHandleRepeatInfo( void *userData, int32_t rate, int32_t delay )
{
   WstContext *ctx= (WstContext*)userData;
   
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
   WstContext *ctx= (WstContext*)userData;
   
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
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         
         x= wl_fixed_to_int( sx );
         y= wl_fixed_to_int( sy );

         pthread_mutex_lock( &ctx->mutex );
         wctx->eventQueue[eventIndex].type= WstEventType_pointerEnter;
         wctx->eventQueue[eventIndex].v1= x;
         wctx->eventQueue[eventIndex].v2= y;
         wctx->eventQueue[eventIndex].p1= surfaceNested;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedPointerHandleLeave( void *userData, struct wl_surface *surfaceNested )
{
   WstContext *ctx= (WstContext*)userData;
   
   if ( ctx )
   {
      if ( ctx->pointerNestedListener )
      {
         ctx->pointerNestedListener->pointerHandleLeave( ctx->pointerNestedListenerUserData );
      }
      else
      {
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         pthread_mutex_lock( &ctx->mutex );
         wctx->eventQueue[eventIndex].type= WstEventType_pointerLeave;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedPointerHandleMotion( void *userData, uint32_t time, wl_fixed_t sx, wl_fixed_t sy )
{
   WstContext *ctx= (WstContext*)userData;
   
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
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         
         x= wl_fixed_to_int( sx );
         y= wl_fixed_to_int( sy );

         pthread_mutex_lock( &ctx->mutex );
         wctx->eventQueue[eventIndex].type= WstEventType_pointerMove;
         wctx->eventQueue[eventIndex].v1= x;
         wctx->eventQueue[eventIndex].v2= y;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedPointerHandleButton( void *userData, uint32_t time, uint32_t button, uint32_t state )
{
   WstContext *ctx= (WstContext*)userData;
   
   if ( ctx )
   {
      if ( ctx->pointerNestedListener )
      {
         ctx->pointerNestedListener->pointerHandleButton( ctx->pointerNestedListenerUserData,
                                                          time, button, state );
      }
      else
      {
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         pthread_mutex_lock( &ctx->mutex );
         wctx->eventQueue[eventIndex].type= WstEventType_pointerButton;
         wctx->eventQueue[eventIndex].v1= button;
         wctx->eventQueue[eventIndex].v2= state;
         wctx->eventQueue[eventIndex].v3= 1; // have time
         wctx->eventQueue[eventIndex].v4= time;
         
         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedPointerHandleAxis( void *userData, uint32_t time, uint32_t axis, wl_fixed_t value )
{
   WstContext *ctx= (WstContext*)userData;

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

static void wstDefaultNestedTouchHandleDown( void *userData, struct wl_surface *surfaceNested, uint32_t time, int32_t id, wl_fixed_t sx, wl_fixed_t sy )
{
   WstContext *ctx= (WstContext*)userData;

   if ( ctx )
   {
      if ( ctx->touchNestedListener )
      {
         ctx->touchNestedListener->touchHandleDown( ctx->touchNestedListenerUserData,
                                                    time, id, sx, sy );
      }
      else
      {
         int x, y;

         x= wl_fixed_to_int( sx );
         y= wl_fixed_to_int( sy );

         pthread_mutex_lock( &ctx->mutex );
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_touchDown;
         wctx->eventQueue[eventIndex].v1= time;
         wctx->eventQueue[eventIndex].v2= id;
         wctx->eventQueue[eventIndex].v3= x;
         wctx->eventQueue[eventIndex].v4= y;
         wctx->eventQueue[eventIndex].p1= surfaceNested;

         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedTouchHandleUp( void *userData, uint32_t time, int32_t id )
{
   WstContext *ctx= (WstContext*)userData;

   if ( ctx )
   {
      if ( ctx->touchNestedListener )
      {
         ctx->touchNestedListener->touchHandleUp( ctx->touchNestedListenerUserData,
                                                  time, id );
      }
      else
      {
         pthread_mutex_lock( &ctx->mutex );
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_touchUp;
         wctx->eventQueue[eventIndex].v1= time;
         wctx->eventQueue[eventIndex].v2= id;

         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedTouchHandleMotion( void *userData, uint32_t time, int32_t id, wl_fixed_t sx, wl_fixed_t sy )
{
   WstContext *ctx= (WstContext*)userData;

   if ( ctx )
   {
      if ( ctx->touchNestedListener )
      {
         ctx->touchNestedListener->touchHandleMotion( ctx->touchNestedListenerUserData,
                                                      time, id, sx, sy );
      }
      else
      {
         int x, y;

         x= wl_fixed_to_int( sx );
         y= wl_fixed_to_int( sy );

         pthread_mutex_lock( &ctx->mutex );
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_touchMotion;
         wctx->eventQueue[eventIndex].v1= time;
         wctx->eventQueue[eventIndex].v2= id;
         wctx->eventQueue[eventIndex].v3= x;
         wctx->eventQueue[eventIndex].v4= y;

         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedTouchHandleFrame( void *userData )
{
   WstContext *ctx= (WstContext*)userData;

   if ( ctx )
   {
      if ( ctx->touchNestedListener )
      {
         ctx->touchNestedListener->touchHandleFrame( ctx->touchNestedListenerUserData );
      }
      else
      {
         pthread_mutex_lock( &ctx->mutex );
         WstCompositor *wctx= ctx->wctx;
         int eventIndex= wctx->eventIndex;
         wctx->eventQueue[eventIndex].type= WstEventType_touchFrame;

         ++wctx->eventIndex;
         assert( wctx->eventIndex < WST_EVENT_QUEUE_SIZE );
         pthread_mutex_unlock( &ctx->mutex );
      }
   }
}

static void wstDefaultNestedShmFormat( void *userData, uint32_t format )
{
   WstContext *ctx= (WstContext*)userData;

   if ( ctx )
   {
      if ( ctx->shm )
      {
         struct wl_resource *resource;

         wl_resource_for_each( resource, &ctx->shm->resourceList )
         {
            wl_shm_send_format(resource, format);
         }
      }
   }
}

static void wstDefaultNestedVpcVideoPathChange( void *userData, struct wl_surface *surfaceNested, uint32_t newVideoPath )
{
   WstContext *ctx= (WstContext*)userData;
   WstSurface *surface= 0;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex );
      for( int i= 0; i < ctx->surfaces.size(); ++i )
      {
         WstSurface *surface= ctx->surfaces[i];
         if ( surface->surfaceNested == surfaceNested )
         {
            WstVpcSurface *vpcSurface= surface->vpcSurface;
            if ( vpcSurface )
            {
               if ( ctx->hasVpcBridge )
               {
                  vpcSurface->useHWPathVpcBridge= (newVideoPath == WL_VPC_SURFACE_PATHWAY_HARDWARE);
               }
               else
               {
                  wl_vpc_surface_send_video_path_change( vpcSurface->resource, newVideoPath );
               }
            }
            break;
         }
      }
      pthread_mutex_unlock( &ctx->mutex );
   }
}

static void wstDefaultNestedVpcVideoXformChange( void *userData, 
                                                 struct wl_surface *surfaceNested,
                                                 int32_t x_translation,
                                                 int32_t y_translation,
                                                 uint32_t x_scale_num,
                                                 uint32_t x_scale_denom,
                                                 uint32_t y_scale_num,
                                                 uint32_t y_scale_denom,
                                                 uint32_t output_width,
                                                 uint32_t output_height )
{
   WstContext *ctx= (WstContext*)userData;
   WstSurface *surface= 0;
   bool needRepaint= false;

   if ( ctx )
   {
      pthread_mutex_lock( &ctx->mutex);
      for( int i= 0; i < ctx->surfaces.size(); ++i )
      {
         WstSurface *surface= ctx->surfaces[i];
         if ( surface->surfaceNested == surfaceNested )
         {
            WstVpcSurface *vpcSurface= surface->vpcSurface;
            if ( vpcSurface )
            {
               if ( ctx->hasVpcBridge )
               {
                  vpcSurface->xTransVpcBridge= x_translation;
                  vpcSurface->yTransVpcBridge= y_translation;
                  vpcSurface->xScaleNumVpcBridge= x_scale_num;
                  vpcSurface->xScaleDenomVpcBridge= x_scale_denom;
                  vpcSurface->yScaleNumVpcBridge= y_scale_num;
                  vpcSurface->yScaleDenomVpcBridge= y_scale_denom;
                  vpcSurface->outputWidthVpcBridge= output_width;
                  vpcSurface->outputHeightVpcBridge= output_height;
                  needRepaint= true;
               }
               else
               {
                  wl_vpc_surface_send_video_xform_change( vpcSurface->resource,
                                                          x_translation,
                                                          y_translation,
                                                          x_scale_num,
                                                          x_scale_denom,
                                                          y_scale_num,
                                                          y_scale_denom,
                                                          output_width,
                                                          output_height );
               }
            }
            break;
         }
      }
      if ( needRepaint )
      {
         wstCompositorScheduleRepaint( ctx );
      }
      pthread_mutex_unlock( &ctx->mutex);
   }
}                                                 

static void wstSetDefaultNestedListener( WstContext *ctx )
{
   ctx->nestedListenerUserData= ctx;
   ctx->nestedListener.connectionStarted= wstDefaultNestedConnectionStarted;
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
   ctx->nestedListener.touchHandleDown= wstDefaultNestedTouchHandleDown;
   ctx->nestedListener.touchHandleUp= wstDefaultNestedTouchHandleUp;
   ctx->nestedListener.touchHandleMotion= wstDefaultNestedTouchHandleMotion;
   ctx->nestedListener.touchHandleFrame= wstDefaultNestedTouchHandleFrame;
   ctx->nestedListener.shmFormat= wstDefaultNestedShmFormat;
   ctx->nestedListener.vpcVideoPathChange= wstDefaultNestedVpcVideoPathChange;
   ctx->nestedListener.vpcVideoXformChange= wstDefaultNestedVpcVideoXformChange;
}

static bool wstSeatInit( WstContext *ctx )
{
   bool result= false;

   if ( ctx )
   {
      WstSeat *seat= 0;

      // Create seat
      ctx->seat= (WstSeat*)calloc( 1, sizeof(WstSeat) );
      if ( !ctx->seat )
      {
         ERROR("no memory to allocate seat");
         goto exit;
      }
      seat= ctx->seat;

      wl_list_init( &seat->resourceList );
      seat->ctx= ctx;
      seat->seatName= strdup("primary-seat");
      seat->keyRepeatDelay= DEFAULT_KEY_REPEAT_DELAY;
      seat->keyRepeatRate= DEFAULT_KEY_REPEAT_RATE;

      if ( !ctx->isEmbedded )
      {
         wstSeatCreateDevices( ctx->wctx );
      }

      result= true;
   }
   
exit:
   
   if ( !result )
   {
      wstSeatTerm( ctx );
   }
   
   return result;
}

static void wstSeatItemTerm( WstCompositor *wctx )
{
   if ( wctx )
   {
      struct wl_resource *resource;

      if ( wctx->keyboard )
      {
         WstKeyboard *keyboard= wctx->keyboard;
         
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
         wctx->keyboard= 0;
      }

      if ( wctx->pointer )
      {
         WstPointer *pointer= wctx->pointer;
         
         while( !wl_list_empty( &pointer->resourceList ) )
         {
            resource= wl_container_of( pointer->resourceList.next, resource, link);
            wl_resource_destroy(resource);
         }

         free( pointer );
         wctx->pointer= 0;
      }

      if ( wctx->touch )
      {
         WstTouch *touch= wctx->touch;

         while( !wl_list_empty( &touch->resourceList ) )
         {
            resource= wl_container_of( touch->resourceList.next, resource, link);
            wl_resource_destroy(resource);
         }

         free( touch );
         wctx->touch= 0;
      }
   }
}

static void wstSeatTerm( WstContext *ctx )
{
   if ( ctx )
   {
      if ( ctx->seat )
      {
         WstSeat *seat= ctx->seat;
         struct wl_resource *resource;

         for (std::vector<WstCompositor*>::iterator it = ctx->virt.begin(); it != ctx->virt.end(); ++it)
         {
            WstCompositor *wctx= (*it);
            wstSeatItemTerm( wctx );
         }
         wstSeatItemTerm( ctx->wctx );

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
}

static void wstSeatCreateDevices( WstCompositor *wctx )
{
   if ( wctx && wctx->ctx )
   {
      WstContext *ctx= wctx->ctx;
      if ( ctx->seat )
      {
         WstSeat *seat= ctx->seat;
         WstKeyboard *keyboard;
         WstPointer *pointer;
         WstTouch *touch;

         if ( !wctx->keyboard )
         {
            // Create keyboard
            wctx->keyboard= (WstKeyboard*)calloc( 1, sizeof(WstKeyboard) );
            if ( wctx->keyboard )
            {
               WstKeyboard *keyboard= wctx->keyboard;
               keyboard->seat= seat;
               keyboard->compositor= wctx;
               wl_list_init( &keyboard->resourceList );
               wl_list_init( &keyboard->focusResourceList );
               wl_array_init( &keyboard->keys );

               if ( !ctx->isNested )
               {
                  keyboard->state= xkb_state_new( ctx->xkbKeymap );
                  if ( keyboard->state )
                  {
                     keyboard->modShift= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_SHIFT );
                     keyboard->modAlt= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_ALT );
                     keyboard->modCtrl= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CTRL );
                     keyboard->modCaps= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CAPS );
                  }
                  else
                  {
                     ERROR("unable to create key state");
                  }
               }
            }
            else
            {
               ERROR("no memory to allocate keyboard");
            }
         }

         if ( !wctx->pointer )
         {
            // Create pointer
            wctx->pointer= (WstPointer*)calloc( 1, sizeof(WstPointer) );
            if ( wctx->pointer )
            {
               pointer= wctx->pointer;
               pointer->seat= seat;
               pointer->compositor= wctx;
               wl_list_init( &pointer->resourceList );
               wl_list_init( &pointer->focusResourceList );
            }
            else
            {
               ERROR("no memory to allocate pointer");
            }
         }

         if ( !wctx->touch )
         {
            // Create touch
            wctx->touch= (WstTouch*)calloc( 1, sizeof(WstTouch) );
            if ( wctx->touch )
            {
               touch= wctx->touch;
               touch->seat= seat;
               touch->compositor= wctx;
               wl_list_init( &touch->resourceList );
               wl_list_init( &touch->focusResourceList );
            }
            else
            {
               ERROR("no memory to allocate touch");
            }
         }
      }
   }
}

static void wstResourceUnBindCallback( struct wl_resource *resource )
{
   wl_list_remove( wl_resource_get_link(resource) );
}

static void wstSeatBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstSeat *seat= (WstSeat*)data;
   WstCompositor *wctx;
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

   wctx= wstGetCompositorFromClient( seat->ctx, client );
   if ( wctx )
   {
      wstSeatCreateDevices( wctx );

      if ( wctx->keyboard )
      {
         caps |= WL_SEAT_CAPABILITY_KEYBOARD;
      }
      if ( wctx->pointer )
      {
         caps |= WL_SEAT_CAPABILITY_POINTER;
      }
      if ( wctx->touch )
      {
         caps |= WL_SEAT_CAPABILITY_TOUCH;
      }
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
   WstPointer *pointer= 0;
   WstCompositor *wctx;
   struct wl_resource *resourcePnt= 0;
   
   wctx= wstGetCompositorFromClient( seat->ctx, client );
   if ( wctx )
   {
      pointer= wctx->pointer;
   }

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
   WstKeyboard *keyboard= 0;
   WstCompositor *wctx;
   struct wl_resource *resourceKbd= 0;
   struct wl_client *focusClient= 0;

   wctx= wstGetCompositorFromClient( seat->ctx, client );
   if ( wctx )
   {
      keyboard= wctx->keyboard;
   }
   
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

   if ( keyboard->focus ) focusClient= wl_resource_get_client(keyboard->focus->resource);

   if ( focusClient == client )
   {
      wl_list_insert( &keyboard->focusResourceList, wl_resource_get_link(resourceKbd) );
   }
   else
   {
      wl_list_insert( &keyboard->resourceList, wl_resource_get_link(resourceKbd) );
   }
   
   wl_resource_set_implementation( resourceKbd,
                                   &keyboard_interface,
                                   keyboard,
                                   wstResourceUnBindCallback );

   if ( wl_resource_get_version(resourceKbd) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION )
   {
      wl_keyboard_send_repeat_info( resourceKbd, seat->keyRepeatRate, seat->keyRepeatDelay );
   }
   
   wl_keyboard_send_keymap( resourceKbd,
                            seat->ctx->xkbKeymapFormat,
                            seat->ctx->xkbKeymapFd,
                            seat->ctx->xkbKeymapSize );

   {
      struct wl_resource *surfaceResource= 0;
      
      if ( seat->ctx->clientInfoMap.size() > 0 )
      {
         WstContext *ctx= seat->ctx;
         for( std::map<struct wl_client*,WstClientInfo*>::iterator it= ctx->clientInfoMap.begin();
              it != ctx->clientInfoMap.end(); ++it )
         {
            if ( it->first == client )
            {
               WstSurface *surface;
               
               surface= ctx->clientInfoMap[client]->surface;
               if ( surface )
               {
                  if ( !keyboard->focus || (focusClient == client) )
                  {
                     if ( keyboard->focus )
                     {
                        uint32_t serial;

                        serial= wl_display_next_serial( ctx->display );
                        surfaceResource= surface->resource;

                        wl_keyboard_send_enter( resourceKbd,
                                                serial,
                                                surfaceResource,
                                                &keyboard->keys );

                        if ( !ctx->isNested )
                        {
                           wstKeyboardSendModifiers( keyboard, resourceKbd );
                        }
                     }
                     else
                     {
                        wstKeyboardSetFocus( keyboard, surface );
                     }
                  }
               }
               
               break;
            }
         }
      }

      if ( !keyboard->focus )
      {
         // This is a workaround for apps that register keyboard listeners with one client
         // and create surfaces with different client
         if ( wl_list_empty(&keyboard->focusResourceList) && !wl_list_empty(&keyboard->resourceList) )
         {
            struct wl_client *client= 0;
            resource= wl_container_of( keyboard->resourceList.next, resource, link);
            if ( resource )
            {
               client= wl_resource_get_client( resource );
               if ( client )
               {
                  DEBUG("wstISeatGetKeyboard: move focus to client %p based on resource %p", client, resource);
                  wstKeyboardMoveFocusToClient( keyboard, client );
               }
            }
         }
      }
   }                               
}

static void wstISeatGetTouch( struct wl_client *client, struct wl_resource *resource, uint32_t id )
{
   WstSeat *seat= (WstSeat*)wl_resource_get_user_data(resource);
   WstCompositor *wctx;
   WstTouch *touch= 0;
   struct wl_resource *resourceTch= 0;

   wctx= wstGetCompositorFromClient( seat->ctx, client );
   if ( wctx )
   {
      touch= wctx->touch;
   }

   if ( !touch )
   {
      return;
   }

   resourceTch= wl_resource_create( client,
                                    &wl_touch_interface,
                                    wl_resource_get_version(resource),
                                    id );
   if ( !resourceTch )
   {
      wl_client_post_no_memory(client);
      return;
   }

   wl_list_insert( &touch->resourceList, wl_resource_get_link(resourceTch) );
   
   wl_resource_set_implementation( resourceTch,
                                   &touch_interface,
                                   touch,
                                   wstResourceUnBindCallback );
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
   WstCompositor *compositor= pointer->compositor;
   WstContext *ctx= compositor->ctx;
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
   
   if ( ctx->isRepeater )
   {
      WstNestedConnectionPointerSetCursor( ctx->nc,
                                           surface ? surface->surfaceNested : NULL, 
                                           hotspot_x, hotspot_y );
   }
   else
   {
      wl_client_get_credentials( client, &pid, NULL, NULL );
   
      if ( ctx->allowModifyCursor || (pid == ctx->dcPid) )
      {
         if ( pid == ctx->dcPid )
         {
            ctx->dcClient= client;
            ctx->dcDefaultCursor= true;
         }
         wstPointerSetPointer( pointer, surface );
         
         if ( pointer->pointerSurface )
         {
            pointer->hotSpotX= hotspot_x;
            pointer->hotSpotY= hotspot_y;
            
            wstPointerUpdatePosition( pointer );
         }

         wstContextInvokeInvalidateCB( ctx );
      }
      else
      {
         if ( surface )
         {
            // Hide the client's cursor surface. We will continue to use default pointer image.
            WstRendererSurfaceSetVisible( ctx->renderer, surface->surface, false );
         }
      }
   }
}
                                  
static void wstIPointerRelease( struct wl_client *client, struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);

   WstPointer *pointer= (WstPointer*)wl_resource_get_user_data(resource);
   WstCompositor *compositor= pointer->compositor;

   wl_resource_destroy(resource);
}

static void wstITouchRelease( struct wl_client *client, struct wl_resource *resource )
{
   WESTEROS_UNUSED(client);

   WstTouch *touch= (WstTouch*)wl_resource_get_user_data(resource);
   WstCompositor *compositor= touch->compositor;

   wl_resource_destroy(resource);
}

static void wstVpcBind( struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   WstContext *ctx= (WstContext*)data;
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
                                  &vpc_surface_interface,
                                  vpcSurface, wstDestroyVpcSurfaceCallback );
   
   WstCompositor *compositor= surface->compositor;
   vpcSurface->surface= surface;
   vpcSurface->videoPathSet= false;
   vpcSurface->useHWPath= true;
   vpcSurface->useHWPathNext= true;
   vpcSurface->pathTransitionPending= false;
   vpcSurface->xScaleNum= 1;
   vpcSurface->xScaleDenom= 1;
   vpcSurface->yScaleNum= 1;
   vpcSurface->yScaleDenom= 1;
   vpcSurface->outputWidth= compositor->outputWidth;
   vpcSurface->outputHeight= compositor->outputHeight;
   vpcSurface->compositor= compositor;
   vpcSurface->hwX= 0;
   vpcSurface->hwY= 0;
   vpcSurface->hwWidth= vpcSurface->outputWidth;
   vpcSurface->hwHeight= vpcSurface->outputHeight;
   vpcSurface->useHWPathVpcBridge= true;
   vpcSurface->xScaleNumVpcBridge= 1;
   vpcSurface->xScaleDenomVpcBridge= 1;
   vpcSurface->yScaleNumVpcBridge= 1;
   vpcSurface->yScaleDenomVpcBridge= 1;
   vpcSurface->outputWidthVpcBridge= compositor->outputWidth;
   vpcSurface->outputHeightVpcBridge= compositor->outputHeight;

   pthread_mutex_lock( &compositor->ctx->mutex );
   compositor->ctx->vpcSurfaces.push_back( vpcSurface );
   pthread_mutex_unlock( &compositor->ctx->mutex );
   if ( compositor->ctx->renderer && vpcSurface->useHWPath )
   {
      WstRendererSurfaceSetVisible( compositor->ctx->renderer, surface->surface, false );
   }
   
   surface->width= DEFAULT_OUTPUT_WIDTH;
   surface->height= DEFAULT_OUTPUT_HEIGHT;
   
   surface->vpcSurface= vpcSurface;

   if ( compositor->ctx->isNested || compositor->ctx->hasVpcBridge )
   {
      if ( !surface->surfaceNested )
      {
         surface->surfaceNested= WstNestedConnectionCreateSurface( compositor->ctx->nc );
      }
      if ( surface->surfaceNested )
      {
         vpcSurface->vpcSurfaceNested= WstNestedConnectionGetVpcSurface( compositor->ctx->nc,
                                                                         surface->surfaceNested );
      }
   }
   wl_vpc_surface_send_video_xform_change( vpcSurface->resource,
                                           vpcSurface->xTrans,
                                           vpcSurface->yTrans,
                                           vpcSurface->xScaleNum,
                                           vpcSurface->xScaleDenom,
                                           vpcSurface->yScaleNum,
                                           vpcSurface->yScaleDenom,
                                           vpcSurface->outputWidth,
                                           vpcSurface->outputHeight );
   pthread_mutex_lock( &compositor->ctx->mutex);
   wstCompositorScheduleRepaint( compositor->ctx );
   pthread_mutex_unlock( &compositor->ctx->mutex);
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
      WstContext *ctx= vpcSurface->compositor->ctx;
      // We should lock when client directly requests a vpc surface destruction
      // otherwise the parent surface is destroying us (i.e. vpcSurface->surface == 0)
      // and it already holds the lock in wstDestroySurfaceCallback()
      bool needLock= (vpcSurface->surface != 0);

      if ( vpcSurface->vpcSurfaceNested )
      {
         WstNestedConnectionDestroyVpcSurface( ctx->nc, vpcSurface->vpcSurfaceNested );
         vpcSurface->vpcSurfaceNested= 0;
      }
      
      if ( needLock )
      {
         pthread_mutex_lock(&ctx->mutex);
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

      if ( needLock )
      {
         pthread_mutex_unlock(&ctx->mutex);
      }
   }

   if ( vpcSurface->surface )
   {
      vpcSurface->surface->vpcSurface= 0;
   }
   
   assert(vpcSurface->resource == NULL);
   
   free( vpcSurface );
}

static void wstIVpcSurfaceSetGeometry( struct wl_client *client, struct wl_resource *resource,
                                       int32_t x, int32_t y, int32_t width, int32_t height )
{
   WstVpcSurface *vpcSurface= (WstVpcSurface*)wl_resource_get_user_data(resource);

   if ( vpcSurface )
   {
      vpcSurface->sizeOverride= true;
      vpcSurface->hwX= x;
      vpcSurface->hwY= y;
      vpcSurface->hwWidth= width;
      vpcSurface->hwHeight= height;

      WstSurface *surface= vpcSurface->surface;
      if ( surface )
      {
         surface->x= x;
         surface->y= y;
         surface->width= width;
         surface->height= height;
         if ( vpcSurface->vpcSurfaceNested )
         {
            wl_vpc_surface_set_geometry( vpcSurface->vpcSurfaceNested, x, y, width, height );
         }
         else
         {
            WstRendererSurfaceSetGeometry( surface->compositor->ctx->renderer, surface->surface, x, y, width, height );
         }
      }
   }
}

static void wstUpdateVPCSurfaces( WstCompositor *wctx, std::vector<WstRect> &rects )
{
   WstContext *ctx= wctx->ctx;
   bool useHWPath= (ctx->renderer->hints & WstHints_noRotation) && !(ctx->renderer->hints & WstHints_animating);
   bool isRotated= false;
   float scaleX, scaleY;

   float m12= ctx->renderer->matrix[1];
   float m21= ctx->renderer->matrix[4];
   float epsilon= 1.0e-2;
   if ( (fabs(m12) > epsilon) &&
        (fabs(m21) > epsilon) )
   {
      isRotated= true;
   }

   if ( isRotated )
   {
      // Calculate x and y scale by applying transfrom to x and y unit vectors
      float x1, x2, y1, y2;
      float x1t, x2t, y1t, y2t;
      float xdiff, ydiff;

      x1= 0;
      y1= 0;
      x2= 1;
      y2= 0;

      x1t= ctx->renderer->matrix[12];
      y1t= ctx->renderer->matrix[13];
      x2t= x2*ctx->renderer->matrix[0]+ctx->renderer->matrix[12];
      y2t= x2*ctx->renderer->matrix[1]+ctx->renderer->matrix[13];

      xdiff= x2t-x1t;
      ydiff= y2t-y1t;
      scaleX= sqrt( xdiff*xdiff+ydiff*ydiff );

      x2= 0;
      y2= 1;

      x1t= ctx->renderer->matrix[12];
      y1t= ctx->renderer->matrix[13];
      x2t= y2*ctx->renderer->matrix[4]+ctx->renderer->matrix[12];
      y2t= y2*ctx->renderer->matrix[5]+ctx->renderer->matrix[13];

      xdiff= x2t-x1t;
      ydiff= y2t-y1t;
      scaleY= sqrt( xdiff*xdiff+ydiff*ydiff );
   }
   else
   {
      scaleX= ctx->renderer->matrix[0];
      scaleY= ctx->renderer->matrix[5];
   }
   
   int scaleXNum= scaleX*100000;
   int scaleXDenom= 100000;
   int scaleYNum= scaleY*100000;
   int scaleYDenom= 100000;
   int transX= (int)ctx->renderer->matrix[12];
   int transY= (int)ctx->renderer->matrix[13];
   int outputWidth= wctx->outputWidth;
   int outputHeight= wctx->outputHeight;
   
   for ( std::vector<WstVpcSurface*>::iterator it= ctx->vpcSurfaces.begin(); 
         it != ctx->vpcSurfaces.end();
         ++it )
   {
      WstVpcSurface *vpcSurface= (*it);
      if ( vpcSurface->surface->compositor == wctx )
      {
         WstRect rect;

         bool useHWPathEffective= (useHWPath && vpcSurface->useHWPathVpcBridge);
         int scaleXNumEffective= (int)scaleXNum*((double)vpcSurface->xScaleNumVpcBridge/(double)vpcSurface->xScaleDenomVpcBridge);
         int scaleXDenomEffective= scaleXDenom;
         int scaleYNumEffective= (int)scaleYNum*((double)vpcSurface->yScaleNumVpcBridge/(double)vpcSurface->yScaleDenomVpcBridge);
         int scaleYDenomEffective= scaleYDenom;
         int transXEffective= (int)(transX*(double)vpcSurface->xScaleNumVpcBridge/(double)vpcSurface->xScaleDenomVpcBridge) + vpcSurface->xTransVpcBridge;
         int transYEffective= (int)(transY*(double)vpcSurface->yScaleNumVpcBridge/(double)vpcSurface->yScaleDenomVpcBridge) + vpcSurface->yTransVpcBridge;
         int outputWidthEffective= outputWidth;
         int outputHeightEffective= outputHeight;

         if ( !vpcSurface->videoPathSet || (useHWPathEffective != vpcSurface->useHWPath) )
         {
            DEBUG("vpcSurface %p useHWPath %d", vpcSurface, useHWPathEffective );
            vpcSurface->useHWPathNext= useHWPathEffective;
            if ( vpcSurface->videoPathSet )
            {
               vpcSurface->pathTransitionPending= true;
            }
            else
            {
               vpcSurface->useHWPath= useHWPathEffective;
            }
            wl_vpc_surface_send_video_path_change( vpcSurface->resource,
                                                   useHWPathEffective ? WL_VPC_SURFACE_PATHWAY_HARDWARE
                                                                      : WL_VPC_SURFACE_PATHWAY_GRAPHICS );
            vpcSurface->videoPathSet= true;
         }

         if ( (transXEffective != vpcSurface->xTrans) ||
              (transYEffective != vpcSurface->yTrans) ||
              (scaleXNumEffective != vpcSurface->xScaleNum) ||
              (scaleXDenomEffective != vpcSurface->xScaleDenom) ||
              (scaleYNumEffective != vpcSurface->yScaleNum) ||
              (scaleYDenomEffective != vpcSurface->yScaleDenom) ||
              (outputWidthEffective != vpcSurface->outputWidth) ||
              (outputHeightEffective != vpcSurface->outputHeight) )
         {
            vpcSurface->xTrans= transXEffective;
            vpcSurface->yTrans= transYEffective;
            vpcSurface->xScaleNum= scaleXNumEffective;
            vpcSurface->xScaleDenom= scaleXDenomEffective;
            vpcSurface->yScaleNum= scaleYNumEffective;
            vpcSurface->yScaleDenom= scaleYDenomEffective;
            vpcSurface->outputWidth= outputWidthEffective;
            vpcSurface->outputHeight= outputHeightEffective;

            wl_vpc_surface_send_video_xform_change( vpcSurface->resource,
                                                    vpcSurface->xTrans,
                                                    vpcSurface->yTrans,
                                                    vpcSurface->xScaleNum,
                                                    vpcSurface->xScaleDenom,
                                                    vpcSurface->yScaleNum,
                                                    vpcSurface->yScaleDenom,
                                                    vpcSurface->outputWidth,
                                                    vpcSurface->outputHeight );
         }

         if ( vpcSurface->useHWPath || vpcSurface->useHWPathNext )
         {
            int vx, vy, vw, vh;
            vx= vpcSurface->surface->x;
            vy= vpcSurface->surface->y;
            vw= vpcSurface->surface->width;
            vh= vpcSurface->surface->height;
            if ( vx+vw > outputWidth )
            {
               vw= outputWidth-vx;
            }
            if ( vy+vh > outputHeight )
            {
               vh= outputHeight-vy;
            }
            // Don't update hardware video rect under certain conditions to avoid visual
            // artifacts.  If we are rotating, we will be presenting video with the graphics
            // path and the transformed rect is not suitable for hardware positioning.  On non-rotation
            // transitions from HW presentation to Gfx don't update the HW rect so the hole
            // punch doesn't move before the first graphics surface arrives.
            if ( !isRotated && (!vpcSurface->pathTransitionPending || vpcSurface->useHWPathNext) )
            {
               vpcSurface->hwX= transX+vx*scaleX;
               vpcSurface->hwY= transY+vy*scaleY;
               vpcSurface->hwWidth= vw*scaleX;
               vpcSurface->hwHeight= vh*scaleY;
            }
            rect.x= vpcSurface->hwX;
            rect.y= vpcSurface->hwY;
            rect.width= vpcSurface->hwWidth;
            rect.height= vpcSurface->hwHeight;
            if ( ctx->renderer->hints & WstHints_holePunch )
            {
               ctx->renderer->holePunch( ctx->renderer, rect.x, rect.y, rect.width, rect.height );
            }
            else
            {
               rects.push_back(rect);
            }
            if ( ctx->hasVpcBridge && (vpcSurface->vpcSurfaceNested) )
            {
               wl_vpc_surface_set_geometry( vpcSurface->vpcSurfaceNested, rect.x, rect.y, rect.width, rect.height );
            }
         }
         else if ( !vpcSurface->sizeOverride )
         {
            WstSurface *surface= vpcSurface->surface;
            double sizeFactorX, sizeFactorY;

            sizeFactorX= (((double)outputWidth)/DEFAULT_OUTPUT_WIDTH);
            sizeFactorY= (((double)outputHeight)/DEFAULT_OUTPUT_HEIGHT);

            WstRendererSurfaceSetGeometry( ctx->renderer, surface->surface,
                                           surface->x,
                                           surface->y,
                                           surface->width*sizeFactorX,
                                           surface->height*sizeFactorY );
         }
      }
   }
}

#define TEMPFILE_PREFIX "westeros-"
#define TEMPFILE_TEMPLATE "/tmp/" TEMPFILE_PREFIX "%d-XXXXXX"

static int wstConvertToReadOnlyFile( int fd )
{
   int readOnlyFd= -1;
   int pid= getpid();
   int len, prefixlen;
   char path[34];
   char link[256];

   prefixlen= strlen(TEMPFILE_PREFIX);
   sprintf(path, "/proc/%d/fd/%d", pid, fd );
   len= readlink( path, link, sizeof(link)-1 );
   if ( len > prefixlen )
   {
      link[len]= '\0';
      if ( strstr( link, TEMPFILE_PREFIX ) )
      {
         readOnlyFd= open( link, O_RDONLY );
         if ( readOnlyFd < 0 )
         {
            ERROR( "unable to obtain a readonly fd for fd %d", fd);
         }
      }
   }

   return readOnlyFd;
}

static bool wstInitializeKeymap( WstCompositor *wctx )
{
   bool result= false;
   char *keymapStr= 0;
   char filename[34];
   int lenDidWrite;
   int readOnlyFd;
   WstContext *ctx= wctx->ctx;

   ctx->xkbCtx= xkb_context_new( XKB_CONTEXT_NO_FLAGS );
   if ( !ctx->xkbCtx )
   {
      sprintf( wctx->lastErrorDetail,
               "Error.  Unable to create xkb context" );
      goto exit;      
   }
   
   ctx->xkbKeymap= xkb_keymap_new_from_names( ctx->xkbCtx,
                                              &ctx->xkbNames,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS );
   if ( !ctx->xkbKeymap )
   {
      sprintf( wctx->lastErrorDetail,
               "Error.  Unable to create xkb keymap" );
      goto exit;      
   }
   
   keymapStr= xkb_keymap_get_as_string( ctx->xkbKeymap,
                                              XKB_KEYMAP_FORMAT_TEXT_V1 );
   if ( !keymapStr )
   {
      sprintf( wctx->lastErrorDetail,
               "Error.  Unable to get xkb keymap in string format" );
      goto exit;      
   }
   
   ctx->xkbKeymapFormat= WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1;
   ctx->xkbKeymapSize= strlen(keymapStr)+1;
   
   snprintf( filename, sizeof(filename), TEMPFILE_TEMPLATE, getpid() );
   ctx->xkbKeymapFd= mkostemp( filename, O_CLOEXEC );
   if ( ctx->xkbKeymapFd < 0 )
   {
      sprintf( wctx->lastErrorDetail,
               "Error.  Unable to create temp file for xkb keymap string" );
      goto exit;      
   }
   
   lenDidWrite= write( ctx->xkbKeymapFd, keymapStr, ctx->xkbKeymapSize );
   if ( lenDidWrite != ctx->xkbKeymapSize )
   {
      sprintf( wctx->lastErrorDetail,
               "Error.  Unable to create write xkb keymap string to temp file" );
      goto exit;      
   }

   readOnlyFd= wstConvertToReadOnlyFile( ctx->xkbKeymapFd );
   if ( readOnlyFd < 0 )
   {
      sprintf( wctx->lastErrorDetail,
               "Error.  Unable to create read-only fd for keymap temp file" );
      goto exit;
   }

   close( ctx->xkbKeymapFd );

   ctx->xkbKeymapFd= readOnlyFd;

   ctx->xkbKeymapArea= (char*)mmap( NULL,
                                    ctx->xkbKeymapSize,
                                    PROT_READ,
                                    MAP_SHARED | MAP_POPULATE,
                                    ctx->xkbKeymapFd,
                                    0  // offset
                                  );
   if ( ctx->xkbKeymapArea == MAP_FAILED )
   {
      sprintf( wctx->lastErrorDetail,
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

static void wstTerminateKeymap( WstCompositor *wctx )
{
   WstContext *ctx= wctx->ctx;
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
   WstCompositor *compositor= keyboard->compositor;
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
      wl_resource_for_each( resource, &keyboard->focusResourceList )
      {
         wstKeyboardSendModifiers( keyboard, resource );
      }
   }
   
   time= (uint32_t)wstGetCurrentTimeMillis();
   serial= wl_display_next_serial( compositor->ctx->display );
   state= (keyState == WstKeyboard_keyState_depressed) 
          ? WL_KEYBOARD_KEY_STATE_PRESSED
          : WL_KEYBOARD_KEY_STATE_RELEASED;
             
   wl_resource_for_each( resource, &keyboard->focusResourceList )
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
   WstCompositor *compositor= keyboard->compositor;
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

   serial= wl_display_next_serial( compositor->ctx->display );
   
   wl_keyboard_send_modifiers( resource,
                               serial,
                               modDepressed, // mod depressed
                               modLatched,   // mod latched
                               modLocked,    // mod locked
                               modGroup      // mod group
                             );
}

static void wstKeyboardCheckFocus( WstKeyboard *keyboard, WstSurface *surface )
{
   WstCompositor *compositor= keyboard->compositor;
   struct wl_client *surfaceClient;
   struct wl_resource *resource;

   if ( !keyboard->focus )
   {
      bool giveFocus= false;

      surfaceClient= wl_resource_get_client( surface->resource );
      wstUpdateClientInfo( compositor->ctx, surfaceClient, 0 );
      if ( compositor->ctx->clientInfoMap[surfaceClient]->usesXdgShell )
      {
         if ( surface->roleName )
         {
            int len= strlen(surface->roleName );
            if ( (len == 11) && !strncmp( surface->roleName, "xdg_surface", len ) )
            {
               giveFocus= true;
            }
         }
      }
      else
      {
         giveFocus= true;
      }
      if ( giveFocus )
      {
         // If nothing has keyboard focus yet, give it to this surface if the client has listeners.
         // This is for the case of a client that gets a keyboard object before creating any surfaces.
         if ( !compositor->keyboard->focus )
         {
            struct wl_resource *temp;
            WstKeyboard *keyboard= compositor->keyboard;

            DEBUG("wstKeyboardCheckFocus: no key focus yet");
            bool clientHasListeners= false;
            wl_resource_for_each_safe( resource, temp, &keyboard->resourceList )
            {
               if ( wl_resource_get_client( resource ) == surfaceClient )
               {
                  clientHasListeners= true;
               }
            }
            if ( !clientHasListeners )
            {
               wl_resource_for_each_safe( resource, temp, &keyboard->focusResourceList )
               {
                  if ( wl_resource_get_client( resource ) == surfaceClient )
                  {
                     // Move focus to null client first since we have acted on the workaround for apps that register
                     // keyboard listeners with one client and create surfaces with different client
                     clientHasListeners= true;
                     wstKeyboardMoveFocusToClient( keyboard, 0 );
                  }
               }
            }
            DEBUG("wstKeyboardCheckFocus: no key focus yet: client %p has listeners %d", surfaceClient, clientHasListeners);
            if ( clientHasListeners )
            {
               DEBUG("wstKeyboardCheckFocus: give key focus to surface %p resource %p", surface, surface->resource);
               wstKeyboardSetFocus( keyboard, surface );
            }
         }
      }
   }
}

static void wstKeyboardSetFocus( WstKeyboard *keyboard, WstSurface *surface )
{
   WstCompositor *compositor= keyboard->compositor;
   uint32_t serial;
   struct wl_client *surfaceClient;
   struct wl_resource *resource;

   if ( keyboard->focus != surface )
   {
      if ( surface )
      {
         bool clientHasFocus= false;
         struct wl_resource *temp;
         surfaceClient= wl_resource_get_client( surface->resource );
         wl_resource_for_each_safe( resource, temp, &keyboard->focusResourceList )
         {
            if ( wl_resource_get_client( resource ) == surfaceClient )
            {
               // Focus is already on client
               clientHasFocus= true;
            }
         }

         if ( !clientHasFocus )
         {
            // This is a workaround for apps that register keyboard listeners with one client
            // and create surfaces with different client
            bool clientHasListeners= false;
            wl_resource_for_each_safe( resource, temp, &keyboard->resourceList )
            {
               if ( wl_resource_get_client( resource ) == surfaceClient )
               {
                  clientHasListeners= true;
               }
            }

            if ( !clientHasListeners && !wl_list_empty(&keyboard->focusResourceList) )
            {
               DEBUG("Don't move focus to client with no listeners");
               return;
            }
         }
      }

      if ( keyboard->focus )
      {
         serial= wl_display_next_serial( compositor->ctx->display );
         surfaceClient= wl_resource_get_client( keyboard->focus->resource );
         wl_resource_for_each( resource, &keyboard->focusResourceList )
         {
            wl_keyboard_send_leave( resource, serial, keyboard->focus->resource );
         }
         keyboard->focus= 0;
      }

      keyboard->focus= surface;

      if ( keyboard->focus )
      {
         surfaceClient= wl_resource_get_client( keyboard->focus->resource );
         wstKeyboardMoveFocusToClient( keyboard, surfaceClient );

         serial= wl_display_next_serial( compositor->ctx->display );
         wl_resource_for_each( resource, &keyboard->focusResourceList )
         {
            wl_keyboard_send_enter( resource, serial, keyboard->focus->resource, &keyboard->keys );
            if ( !compositor->ctx->isNested )
            {
               wstKeyboardSendModifiers( keyboard, resource );
            }
         }
      }
      else
      {
         wstKeyboardMoveFocusToClient( keyboard, 0 );
      }
   }
}

static void wstKeyboardMoveFocusToClient( WstKeyboard *keyboard, struct wl_client *client )
{
   struct wl_resource *resource;
   struct wl_resource *temp;

   wl_list_insert_list( &keyboard->resourceList, &keyboard->focusResourceList );
   wl_list_init( &keyboard->focusResourceList );

   wl_resource_for_each_safe( resource, temp, &keyboard->resourceList )
   {
      if ( wl_resource_get_client( resource ) == client )
      {
         wl_list_remove( wl_resource_get_link( resource ) );
         wl_list_insert( &keyboard->focusResourceList, wl_resource_get_link(resource) );
      }
   }
}

static void wstProcessPointerEnter( WstPointer *pointer, int x, int y, struct wl_surface *surfaceNested )
{
   WstCompositor *wctx= pointer->compositor;
   WstContext *ctx= wctx->ctx;
   
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
   WstCompositor *wctx= pointer->compositor;
   WstContext *ctx= wctx->ctx;

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
      
      wstContextInvokeInvalidateCB( ctx );

      pointer->entered= false;
   }
}

static void wstProcessPointerMoveEvent( WstPointer *pointer, int32_t x, int32_t y )
{
   WstCompositor *compositor= pointer->compositor;
   WstContext *ctx= compositor->ctx;
   WstSurface *surface= 0;
   int sx=0, sy=0, sw=0, sh=0;
   uint32_t time;
   struct wl_resource *resource;

   if ( ctx->isRepeater )
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
         
      if ( pointer->focus || ctx->dcDefaultCursor )
      {
         if ( pointer->focus )
         {
            wl_fixed_t xFixed, yFixed;

            WstRendererSurfaceGetGeometry( ctx->renderer, pointer->focus->surface, &sx, &sy, &sw, &sh );
            
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
            wstCompositorScheduleRepaint( ctx );
         }
      }
   }
}

static void wstProcessPointerButtonEvent( WstPointer *pointer, uint32_t button, uint32_t buttonState, uint32_t time )
{
   WstCompositor *compositor= pointer->compositor;
   uint32_t serial, btnState;
   struct wl_resource *resource;
   
   if ( pointer->focus )
   {
      serial= wl_display_next_serial( compositor->ctx->display );
      btnState= (buttonState == WstPointer_buttonState_depressed) 
                ? WL_POINTER_BUTTON_STATE_PRESSED 
                : WL_POINTER_BUTTON_STATE_RELEASED;
      wl_resource_for_each( resource, &pointer->focusResourceList )
      {
         wl_pointer_send_button( resource, serial, time, button, buttonState );
      }

      if ( buttonState == WstPointer_buttonState_depressed )
      {
         WstKeyboard *keyboard= compositor->keyboard;
         if ( keyboard )
         {
            wstKeyboardSetFocus( keyboard, pointer->focus );
         }
      }
   }
}

static void wstPointerCheckFocus( WstPointer *pointer, int32_t x, int32_t y )
{
   WstCompositor *compositor= pointer->compositor;
   
   if ( !compositor->ctx->isRepeater )
   {
      WstSurface *surface= 0;

      surface= wstGetSurfaceFromPoint( compositor, x, y );
      
      if ( pointer->focus != surface )
      {
         int sx= 0, sy= 0, sw, sh;
         wl_fixed_t xFixed, yFixed;

         if ( surface )
         {
            WstRendererSurfaceGetGeometry( compositor->ctx->renderer, surface->surface, &sx, &sy, &sw, &sh );
         }

         xFixed= wl_fixed_from_int( x-sx );
         yFixed= wl_fixed_from_int( y-sy );

         wstPointerSetFocus( pointer, surface, xFixed, yFixed );
      }
   }
}

static void wstPointerSetPointer( WstPointer *pointer, WstSurface *surface )
{
   WstCompositor *compositor= pointer->compositor;
   bool hidePointer;

   if ( surface )
   {
      WstRendererSurfaceSetZOrder( compositor->ctx->renderer, surface->surface, 1000000.0f );
      WstRendererSurfaceSetVisible( compositor->ctx->renderer, surface->surface, true );
      hidePointer= true;
   }
   else
   {
      if ( pointer->pointerSurface )
      {
         WstRendererSurfaceSetVisible( compositor->ctx->renderer, pointer->pointerSurface->surface, false );
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

   wstContextInvokeHidePointerCB( compositor->ctx, hidePointer );
}

static void wstPointerUpdatePosition( WstPointer *pointer )
{
   int px=0, py=0, pw=0, ph=0;
   WstCompositor *compositor= pointer->compositor;
   WstSurface *pointerSurface= pointer->pointerSurface;

   WstRendererSurfaceGetGeometry( compositor->ctx->renderer, pointerSurface->surface, &px, &py, &pw, &ph );
   px= pointer->pointerX-pointer->hotSpotX;
   py= pointer->pointerY-pointer->hotSpotY;
   WstRendererSurfaceSetGeometry( compositor->ctx->renderer, pointerSurface->surface, px, py, pw, ph );
}

static void wstPointerSetFocus( WstPointer *pointer, WstSurface *surface, wl_fixed_t x, wl_fixed_t y )
{
   WstCompositor *compositor= pointer->compositor;
   uint32_t serial, time;
   struct wl_client *surfaceClient;
   struct wl_resource *resource;

   if ( pointer->focus != surface )
   {
      if ( pointer->focus )
      {
         serial= wl_display_next_serial( compositor->ctx->display );
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

         serial= wl_display_next_serial( compositor->ctx->display );
         wl_resource_for_each( resource, &pointer->focusResourceList )
         {
            wl_pointer_send_enter( resource, serial, pointer->focus->resource, x, y );
         }
      }
      else if ( !compositor->ctx->dcDefaultCursor )
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

static void wstProcessTouchDownEvent( WstTouch *touch, uint32_t time, int id, int x, int y, struct wl_surface *surfaceNested )
{
   WstContext *ctx= touch->compositor->ctx;
   uint32_t serial;
   struct wl_resource *resource;
   int sx=0, sy=0, sw=0, sh=0;

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
               struct wl_client *surfaceClient;

               touch->focus= surface;

               surfaceClient= wl_resource_get_client( touch->focus->resource );
               wstTouchMoveFocusToClient( touch, surfaceClient );
            }
         }
         else
         {
            wstTouchCheckFocus( touch, x, y );
         }
      }
   }
   else
   {
      wstTouchCheckFocus( touch, x, y );
   }

   if ( touch->focus )
   {
      wl_fixed_t xFixed, yFixed;

      if ( ctx->isRepeater )
      {
         sx= sy= 0;
      }
      else
      {
         WstRendererSurfaceGetGeometry( ctx->renderer, touch->focus->surface, &sx, &sy, &sw, &sh );
      }

      xFixed= wl_fixed_from_int( x-sx );
      yFixed= wl_fixed_from_int( y-sy );

      serial= wl_display_next_serial( ctx->display );
      wl_resource_for_each( resource, &touch->focusResourceList )
      {
         wl_touch_send_down( resource, serial, time, touch->focus->resource, id, xFixed, yFixed );
      }

      WstKeyboard *keyboard= touch->compositor->keyboard;
      if ( keyboard )
      {
         wstKeyboardSetFocus( keyboard, touch->focus );
      }
   }
}

static void wstProcessTouchUpEvent( WstTouch *touch, uint32_t time, int id )
{
   WstContext *ctx= touch->compositor->ctx;
   uint32_t serial;
   struct wl_resource *resource;

   if ( touch->focus )
   {
      serial= wl_display_next_serial( ctx->display );
      wl_resource_for_each( resource, &touch->focusResourceList )
      {
         wl_touch_send_up( resource, serial, time, id );
      }
   }
}

static void wstProcessTouchMotionEvent( WstTouch *touch, uint32_t time, int id, int x, int y )
{
   WstContext *ctx= touch->compositor->ctx;
   uint32_t serial;
   struct wl_resource *resource;
   int sx, sy, sw, sh;

   if ( touch->focus )
   {
      wl_fixed_t xFixed, yFixed;

      if ( ctx->isRepeater )
      {
         sx= sy= 0;
      }
      else
      {
         WstRendererSurfaceGetGeometry( ctx->renderer, touch->focus->surface, &sx, &sy, &sw, &sh );
      }

      xFixed= wl_fixed_from_int( x-sx );
      yFixed= wl_fixed_from_int( y-sy );

      wl_resource_for_each( resource, &touch->focusResourceList )
      {
         wl_touch_send_motion( resource, time, id, xFixed, yFixed );
      }
   }
}

static void wstProcessTouchFrameEvent( WstTouch *touch )
{
   struct wl_resource *resource;

   if ( touch->focus )
   {
      wl_resource_for_each( resource, &touch->focusResourceList )
      {
         wl_touch_send_frame( resource );
      }
   }
}

static void wstTouchCheckFocus( WstTouch *touch, int32_t x, int32_t y )
{
   WstCompositor *compositor= touch->compositor;
   WstSurface *surface= 0;

   surface= wstGetSurfaceFromPoint( compositor, x, y );
   if ( surface )
   {
      touch->focus= surface;

      if ( touch->focus )
      {
         struct wl_client *surfaceClient;
         surfaceClient= wl_resource_get_client( touch->focus->resource );
         wstTouchMoveFocusToClient( touch, surfaceClient );
      }
   }
}

static void wstTouchMoveFocusToClient( WstTouch *touch, struct wl_client *client )
{
   struct wl_resource *resource;
   struct wl_resource *temp;

   wl_list_insert_list( &touch->resourceList, &touch->focusResourceList );
   wl_list_init( &touch->focusResourceList );

   wl_resource_for_each_safe( resource, temp, &touch->resourceList )
   {
      if ( wl_resource_get_client( resource ) == client )
      {
         wl_list_remove( wl_resource_get_link( resource ) );
         wl_list_insert( &touch->focusResourceList, wl_resource_get_link(resource) );
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
      if ( strstr( link, TEMPFILE_PREFIX ) )
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

static void wstPruneOrphanFiles( WstContext *ctx )
{
   DIR *dir;
   struct dirent *result;
   struct stat fileinfo;
   int prefixLen;
   int pid, rc;
   char work[34];
   if ( NULL != (dir = opendir( "/tmp" )) )
   {
      prefixLen= strlen(TEMPFILE_PREFIX);
      while( NULL != (result = readdir( dir )) )
      {
         if ( (result->d_type != DT_DIR) &&
             !strncmp(result->d_name, TEMPFILE_PREFIX, prefixLen) )
         {
            snprintf( work, sizeof(work), "%s/%s", "/tmp", result->d_name);
            if ( sscanf( work, TEMPFILE_TEMPLATE, &pid ) == 1 )
            {
               // Check if the pid of this temp file is still valid
               snprintf(work, sizeof(work), "/proc/%d", pid);
               rc= stat( work, &fileinfo );
               if ( rc )
               {
                  // The pid is not valid, delete the file
                  snprintf( work, sizeof(work), "%s/%s", "/tmp", result->d_name);
                  INFO("removing temp file: %s", work);
                  remove( work );
               }
            }
         }
      }

      closedir( dir );
   }
}

static void dcSeatCapabilities( void *data, struct wl_seat *seat, uint32_t capabilities )
{
   WstContext *ctx= (WstContext*)data;

   printf("seat %p caps: %X\n", seat, capabilities );
   if ( capabilities & WL_SEAT_CAPABILITY_POINTER )
   {
      printf("  seat has pointer\n");
      ctx->dcPointer= wl_seat_get_pointer( ctx->dcSeat );
      printf("  pointer %p\n", ctx->dcPointer );
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
   WstContext *ctx= (WstContext*)data;
   int len;

   len= strlen(interface);
   if ( (len==6) && !strncmp(interface, "wl_shm", len)) {
      ctx->dcShm= (struct wl_shm*)wl_registry_bind(registry, id, &wl_shm_interface, 1);
   }
   else if ( (len==13) && !strncmp(interface, "wl_compositor", len) ) {
      ctx->dcCompositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
   } 
   else if ( (len==7) && !strncmp(interface, "wl_seat", len) ) {
      ctx->dcSeat= (struct wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 4);
      wl_seat_add_listener(ctx->dcSeat, &dcSeatListener, ctx);
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
   WstContext *ctx= compositor->ctx;

   int pid= fork();
   if ( pid == 0 )
   {
      struct wl_buffer *buffer= 0;
      int imgDataSize;
      char filename[32];
      int fd;
      int lenDidWrite;
      void *data;

      ctx->dcDisplay= wl_display_connect( ctx->displayName );
      if ( !ctx->dcDisplay )
      {
         goto exit;
      }
      
      ctx->dcRegistry= wl_display_get_registry(ctx->dcDisplay);
      if ( !ctx->dcRegistry )
      {
         goto exit;
      }

      wl_registry_add_listener(ctx->dcRegistry, &dcRegistryListener, ctx);
      wl_display_roundtrip(ctx->dcDisplay);

      if ( !ctx->dcCompositor ||
           !ctx->dcShm ||
           !ctx->dcSeat )
      {
         goto exit;
      }
      
      wl_display_roundtrip(ctx->dcDisplay);
      
      if ( !ctx->dcPointer )
      {
         goto exit;
      }
      
      ctx->dcCursorSurface= wl_compositor_create_surface(ctx->dcCompositor);
      wl_display_roundtrip(ctx->dcDisplay);
      if ( !ctx->dcCursorSurface )
      {
         goto exit;
      }
      
      imgDataSize= height*width*4;

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
   
      ctx->dcPoolData= data;
      ctx->dcPoolSize= imgDataSize;
      ctx->dcPoolFd= fd;
      memcpy( data, imgData, height*width*4 );
      
      ctx->dcPool= wl_shm_create_pool(ctx->dcShm, fd, imgDataSize);
      wl_display_roundtrip(ctx->dcDisplay);

      buffer= wl_shm_pool_create_buffer(ctx->dcPool,
                                        0, //offset
                                        width,
                                        height,
                                        width*4, //stride
                                        WL_SHM_FORMAT_ARGB8888 );
      wl_display_roundtrip(ctx->dcDisplay);
      if ( !buffer )
      {
         goto exit;
      }
      
      wl_surface_attach( ctx->dcCursorSurface, buffer, 0, 0 );
      wl_surface_damage( ctx->dcCursorSurface, 0, 0, width, height);
      wl_surface_commit( ctx->dcCursorSurface );
      wl_pointer_set_cursor( ctx->dcPointer,
                             0,
                             ctx->dcCursorSurface,
                             hotspotX, hotspotY );
      wl_display_roundtrip(ctx->dcDisplay);

      wl_display_dispatch( ctx->dcDisplay );
      
   exit:

      wstTerminateDefaultCursor( compositor );
      
      exit(0);
   }
   else if ( pid < 0 )
   {
      sprintf( compositor->lastErrorDetail,
               "Error.  Unable to fork process" );
   }
   else
   {
      ctx->dcPid= pid;
      DEBUG("default cursor client spawned: pid %d", pid );
      result= true;
   }
   
   return result;
}

static void wstTerminateDefaultCursor( WstCompositor *compositor )
{
   WstContext *ctx= compositor->ctx;
   if ( ctx->dcCursorSurface )
   {
      wl_pointer_set_cursor( ctx->dcPointer,
                             0,
                             NULL,
                             0, 0 );
      wl_surface_destroy( ctx->dcCursorSurface );
      ctx->dcCursorSurface= 0;
   }
   
   if ( ctx->dcPool )
   {
      wl_shm_pool_destroy( ctx->dcPool);
      ctx->dcPool= 0;
   }
   
   if ( ctx->dcPoolData )
   {
      munmap( ctx->dcPoolData, ctx->dcPoolSize );
      ctx->dcPoolData= 0;
   }
   
   if ( ctx->dcPoolFd >= 0)
   {
      wstRemoveTempFile( ctx->dcPoolFd );
      ctx->dcPoolFd= -1;
   }
   
   if ( ctx->dcPointer )
   {
      wl_pointer_destroy( ctx->dcPointer );
      ctx->dcPointer= 0;
   }
   
   if ( ctx->dcShm )
   {
      wl_shm_destroy( ctx->dcShm );
      ctx->dcShm= 0;
   }
   
   if ( ctx->dcCompositor )
   {
      wl_compositor_destroy( ctx->dcCompositor );
      ctx->dcCompositor= 0;
   }

   if ( ctx->dcRegistry )
   {
      wl_registry_destroy( ctx->dcRegistry );
      ctx->dcRegistry= 0;
   }
   
   if ( ctx->dcDisplay )
   {
      wl_display_disconnect( ctx->dcDisplay );
      ctx->dcDisplay= 0;
   }
}

static void wstForwardChildProcessStdout( int descriptors[2] )
{
   setbuf( stdout, 0 ); // disable buffering to ensure timely logging
   close( descriptors[WstPipeDescriptor_ParentRead] );
   while ( (dup2(descriptors[WstPipeDescriptor_ChildWrite], STDOUT_FILENO) == -1) && (errno == EINTR) ) {}
   close( descriptors[WstPipeDescriptor_ChildWrite] );
}

static void wstMonitorChildProcessStdout( int descriptors[2] )
{
   close( descriptors[WstPipeDescriptor_ChildWrite] );

   char tmp[4096];
   while ( 1 ) {
      ssize_t bytes = read( descriptors[WstPipeDescriptor_ParentRead], tmp, sizeof(tmp) );
      if ( bytes == -1 )
      {
         if ( errno == EINTR )
         {
            continue;
         }
         else
         {
            const char kErrorMsg[] = "read failed!";
            write( STDERR_FILENO, kErrorMsg, sizeof(kErrorMsg) );
            break;
         }
      }
      else if ( bytes == 0 )
      {
         break;
      }
      else
      {
         write( STDOUT_FILENO, tmp, bytes );
         fflush(stdout);
      }
   }
   close( descriptors[WstPipeDescriptor_ParentRead] );
}

