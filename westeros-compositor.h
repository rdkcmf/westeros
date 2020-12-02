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
#ifndef _WESTEROS_COMPOSITOR_H
#define _WESTEROS_COMPOSITOR_H

#include "westeros-render.h"

typedef struct _WstCompositor WstCompositor;

typedef enum _WstKeyboard_keyState
{
   WstKeyboard_keyState_released,
   WstKeyboard_keyState_depressed,
   WstKeyboard_keyState_none
} WstKeyboard_keyState;

typedef enum _WstKeyboad_modifiers
{
   WstKeyboard_shift= (1<<0),
   WstKeyboard_alt=   (1<<1),
   WstKeyboard_ctrl=  (1<<2),
   WstKeyboard_caps=  (1<<3),
   WstKeyboard_meta= (1<<4)
} WstKeyboard_modifiers;

typedef enum _WstPointer_buttonState
{
   WstPointer_buttonState_released,
   WstPointer_buttonState_depressed
} WstPointer_buttonState;

typedef struct _WstTouchInfo
{
   int id;
   int x;
   int y;
   bool valid;
   bool starting;
   bool stopping;
   bool moved;
} WstTouchInfo;

#define WST_MAX_TOUCH (10)
typedef struct _WstTouchSet
{
   WstTouchInfo touch[WST_MAX_TOUCH];
} WstTouchSet;

typedef enum _WstClient_status
{
   WstClient_started,
   WstClient_stoppedNormal,
   WstClient_stoppedAbnormal,
   WstClient_connected,
   WstClient_disconnected,
   WstClient_firstFrame
} WstClient_status;

typedef void (*WstTerminatedCallback)( WstCompositor *wctx, void *userData );
typedef void (*WstDispatchCallback)( WstCompositor *wctx, void *userData );
typedef void (*WstInvalidateSceneCallback)( WstCompositor *wctx, void *userData );
typedef void (*WstHidePointerCallback)( WstCompositor *wctx, bool hidePointer, void *userData );
typedef void (*WstClientStatus)( WstCompositor *wctx, int status, int clientPID, int detail, void *userData );
typedef void (*WstVirtEmbUnBoundClient)( WstCompositor *wctx, int clientPID, void *userData );

typedef void (*WstOutputHandleGeometryCallback)( void *userData, int32_t x, int32_t y, int32_t mmWidth, int32_t mmHeight,
                                                 int32_t subPixel, const char *make, const char *model, int32_t transform );
typedef void (*WstOutputHandleModeCallback)( void *userData, uint32_t flags, int32_t width, int32_t height, int32_t refreshRate );
typedef void (*WstOutputHandleDoneCallback)( void *UserData );
typedef void (*WstOutputHandleScaleCallback)( void *UserData, int32_t scale );

typedef void (*WstKeyboardHandleKeyMapCallback)( void *userData, uint32_t format, int fd, uint32_t size );
typedef void (*WstKeyboardHandleEnterCallback)( void *userData, struct wl_array *keys );
typedef void (*WstKeyboardHandleLeaveCallback)( void *userData );
typedef void (*WstKeyboardHandleKeyCallback)( void *userData, uint32_t time, uint32_t key, uint32_t state );
typedef void (*WstKeyboardHandleModifiersCallback)( void *userData, uint32_t mods_depressed, uint32_t mods_latched,
                                                    uint32_t mods_locked, uint32_t group );
typedef void (*WstKeyboardHandleRepeatInfoCallback)( void *userData, int32_t rate, int32_t delay );

typedef void (*WstPointerHandleEnterCallback)( void *userData, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WstPointerHandleLeaveCallback)( void *userData );
typedef void (*WstPointerHandleMotionCallback)( void *userData, uint32_t time, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WstPointerHandleButtonCallback)( void *userData, uint32_t time, uint32_t button, uint32_t state );
typedef void (*WstPointerHandleAxisCallback)( void *userData, uint32_t time, uint32_t axis, wl_fixed_t value );

typedef void (*WstTouchHandleDownCallback)( void *userData, uint32_t time, int32_t id, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WstTouchHandleUpCallback)( void *userData, uint32_t time, int32_t id );
typedef void (*WstTouchHandleMotionCallback)( void *userData, uint32_t time, int32_t id, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WstTouchHandleFrameCallback)( void *userData );


typedef struct _WstOutputNestedListener
{
   WstOutputHandleGeometryCallback outputHandleGeometry;
   WstOutputHandleModeCallback outputHandleMode;
   WstOutputHandleDoneCallback outputHandleDone;
   WstOutputHandleScaleCallback outputHandleScale;
   
} WstOutputNestedListener;

typedef struct _WstKeyboardNestedListener
{
   WstKeyboardHandleKeyMapCallback keyboardHandleKeyMap;
   WstKeyboardHandleEnterCallback keyboardHandleEnter;
   WstKeyboardHandleLeaveCallback keyboardHandleLeave;
   WstKeyboardHandleKeyCallback keyboardHandleKey;
   WstKeyboardHandleModifiersCallback keyboardHandleModifiers;
   WstKeyboardHandleRepeatInfoCallback keyboardHandleRepeatInfo;
} WstKeyboardNestedListener;

typedef struct _WstPointerNestedListener
{
   WstPointerHandleEnterCallback pointerHandleEnter;
   WstPointerHandleLeaveCallback pointerHandleLeave;
   WstPointerHandleMotionCallback pointerHandleMotion;
   WstPointerHandleButtonCallback pointerHandleButton;
   WstPointerHandleAxisCallback pointerHandleAxis;
} WstPointerNestedListener;

typedef struct _WstTouchNestedListener
{
   WstTouchHandleDownCallback touchHandleDown;
   WstTouchHandleUpCallback touchHandleUp;
   WstTouchHandleMotionCallback touchHandleMotion;
   WstTouchHandleFrameCallback touchHandleFrame;
} WstTouchNestedListener;

/**
 * WestCompositorCreate
 *
 * Create a new compositor instance.  The caller should configure
 * the instance with WstCompositorSet* calls and then start the
 * compositor operation by calling WstCompositorStart.
 */
WstCompositor* WstCompositorCreate();

/** 
 * WstCompositorDestroy
 *
 * Destroy a compositor instance.  If the compositor is running
 * it will be stopped, and then all resources will be freed.
 */
void WstCompositorDestroy( WstCompositor *wctx );

/**
 * WstCompositorGetMasterEmbedded
 *
 * Atomically get or create the master embedded compositor.  This master
 * embedded compositor will be the sole embedded compositor instance
 * for the process and can be used to create multiple virtual embedded
 * compositor instances with WstCompositorCreateVirtualEmbedded.
 */
WstCompositor* WstCompositorGetMasterEmbedded();

/**
 * WstCompositorCreateVirtualEmbedded
 *
 * Create a virtual embedded compositor from an existing embedded compositor.
 */
WstCompositor* WstCompositorCreateVirtualEmbedded( WstCompositor *wctx );

/**
 * WstCompositorGetLastErrorDetail
 *
 * Returns a null terminated string giving information about the
 * last error that has occurred.
 */
const char *WstCompositorGetLastErrorDetail( WstCompositor *wctx );

/**
 * WstCompositorSetDisplayName
 *
 * Specify the name of the wayland display that this instance will
 * create.  This must be called prior to WstCompositorStart.  If not
 * called, the behaviour is as follows: for a nested compositor a
 * display name will be generated, for a non-nested compositor the
 * default display name of 'wayland-0' will be used.  The display
 * name of a compositor can be obtained using WstCompositorGetDisplayName.
 */
bool WstCompositorSetDisplayName( WstCompositor *wctx, const char *displayName );

/**
 * WstCompositorSetFrameRate
 *
 * Specity the rate in frames per second (fps) that the compositor should
 * generate each new composited output frame.  This can be called at any time.
 */
bool WstCompositorSetFrameRate( WstCompositor *wctx, unsigned int frameRate );

/**
 * WstCompositorSetNativeWindow
 *
 * Specify the native window to be used by the compositor render module
 * in creating its rendering context.
 */
bool WstCompositorSetNativeWindow( WstCompositor *wctx, void *nativeWindow );

/**
 * WstCompositorSetRendererModule
 *
 * Specify the name of the module the compositor will use for rendering.  This
 * will be a shared library file name without path.  An example module 
 * name might be libwesteros_render_gl.so.0.  This must be called prior
 * to WstCompositorStart.
 */
bool WstCompositorSetRendererModule( WstCompositor *wctx, const char *rendererModule );

/**
 * WstCompositorSetIsNested
 *
 * Specify if the compositor is to act as a nested compositor.  When acting
 * as a nested compositor, the compositor will create a wayland display that
 * clients can connect and render to, but the compositor will act as a client
 * to another compositor and its output frames will be drawn to a surface
 * of the second compositor.
 */
bool WstCompositorSetIsNested( WstCompositor *wctx, bool isNested );

/**
 * WstCompositorSetIsRepeater
 *
 * Specify if the compositor is to act as a repeating nested compositor.  A 
 * normal nested compositor will compose client surfaces to produce an output
 * surface which is then sent to a second compositor for display.  A repeating 
 * nested compositor will not perform any composition rendering but instead 
 * will forward surface buffers from its clients to the wayland display to 
 * which it is connected.  Enabling repeating will also enable nested 
 * composition.
 */
bool WstCompositorSetIsRepeater( WstCompositor *wctx, bool isRepeater );

/**
 * WstCompositorSetIsEmbedded
 *
 * Specify if the compositor is to act as an embedded compositor.  When acting
 * as an embedded compositor, the compositor will create a wayland display that
 * clients can connect and render to, but the compositor will only compose
 * its scene when WstCompositorComposeEmbedded is called.  An embedded
 * compositor should use libwesteros_render_embedded.so.0 as its
 * renderer module (or some other module that supports embedded composition).
 * Note that multi-threaded applications that use embedded composition must
 * call WstCompositorStart and WstCompositorComposeEmbedded on the same thread.
 */
bool WstCompositorSetIsEmbedded( WstCompositor *wctx, bool isEmbedded );

/**
 * WstCompositorSetVpcBridge
 *
 * Specify if the embedded compositor instance should establish a VPC
 * (Video Path Control) bridge with another compositor instance.  A
 * VPC bridge will allow control over video path and positioning to be
 * extended to higher level compositors from a nested ebedded compositor.
 */
bool WstCompositorSetVpcBridge( WstCompositor *wctx, char *displayName );

/**
 * WstCompositorSetOutputSize
 *
 * Specify the size of the output surface for the compositor.  This may
 * be called at any time.
 */
bool WstCompositorSetOutputSize( WstCompositor *wctx, int width, int height );

/**
 * WstCompositorSetNestedDisplayName
 *
 * Specify the wayland display name that this compositor instance should connect
 * and render to as a nested compositor.  This must be called prior to 
 * WstCompositorStart.
 */
bool WstCompositorSetNestedDisplayName( WstCompositor *wctx, const char *nestedDisplayName );

/**
 * WstCompositorSetNestedSize
 *
 * Specify the size of the surface which should be created on the display
 * specified with WstCompositorSetNestedDisplayName in which to display the
 * composited output.  This must be called prior to WstCompositorStart.
 */
bool WstCompositorSetNestedSize( WstCompositor *wctx, unsigned int width, unsigned int height );

/**
 * WstCompositorSetAllowCursorModification
 *
 * Specify whether compositor clients are permitted to modify the pointer cursor
 * image.  This must be called prior to WstCompositorStart.
 */
bool WstCompositorSetAllowCursorModification( WstCompositor *wctx, bool allow );

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
                                    int width, int height, int hotSpotX, int hotSpotY );

/**
 * WstCompositorAddModule
 *
 * Specify the name of a module the compositor will load.  This
 * will be a shared library file name without path.  Modules can be used
 * to add new protocols or other functionality to the compositor instance.
 * The module name will be added to a list of modules to load and initialize
 * when the comnpositor starts. The module needs to supply module
 * initialization and termination entry points:
 *
 * bool moduleInit( WstCompositor *wctx, struct wl_display );
 * void moduleTerm( WstCompositor *wctx );
 *
 * This API must be called prior to WstCompositorStart.
 *
 */
bool WstCompositorAddModule( WstCompositor *wctx, const char *moduleName );

/**
 * WstCompositorResolutionChangeBegin
 *
 * Signal the compositor that a display resolution change is about to happen
 */
void WstCompositorResolutionChangeBegin( WstCompositor *wctx );

/**
 * WstCompositorResolutionChangeBegin
 *
 * Signal the compositor that a display resolution change has completed
 */
void WstCompositorResolutionChangeEnd( WstCompositor *wctx, int width, int height );

/**
 * WstCompositorGetDisplayName
 *
 * Obtain the display name used by this compositor instance.  This will 
 * be the name set prior to start via WstCompositorSetDisplayName or, for
 * a nested compositor for which no name was specified, the display name
 * that was automatically generated.  This can be called at any time.
 */
const char *WstCompositorGetDisplayName( WstCompositor *wctx );

/**
 * WstCompositorGetFrameRate
 *
 * Obtain the current output frame rate being used by the 
 * compositor instance.  The returned value will be in 
 * frames per second (fps).  This can be called at any time.
 */
unsigned int WstCompositorGetFrameRate( WstCompositor *wctx );

/**
 * WstCompositorGetRendererModule
 *
 * Obtain the name of the renderer module being used by
 * this compositor instance.  This can be called at any time.
 */
const char *WstCompositorGetRendererModule( WstCompositor *wctx );

/**
 * WstCompositorGetIsNested
 *
 * Determine if this compsitor instance is acting as a nested
 * compositor or not.  This may be called at any time.
 */
bool WstCompositorGetIsNested( WstCompositor *wctx );

/**
 * WstCompositorGetIsRepeater
 *
 * Determine if this compsitor instance is acting as a repeating 
 * nested compositor or not.  This may be called at any time.
 */
bool WstCompositorGetIsRepeater( WstCompositor *wctx );

/**
 * WstCompositorGetIsEmbedded
 *
 * Determine if this compsitor instance is acting as an embedded
 * compositor or not.  This may be called at any time.
 */
bool WstCompositorGetIsEmbedded( WstCompositor *wctx );

/**
 * WstCompositorGetIsVirtualEmbedded
 *
 * Determine if this compsitor instance is a virtual embedded
 * compositor or not.  This may be called at any time.
 */
bool WstCompositorGetIsVirtualEmbedded( WstCompositor *wctx );

/**
 * WstCompositorGetVpcBridge
 *
 * Determine the display, if any, with which this embedded compistor instance
 * will establish a VPC (Video Path Control) bridge.
 */
const char* WstCompositorGetVpcBridge( WstCompositor *wctx );

/**
 * WstCompositorGetOutputSize
 *
 * Obtain the width and height of the compositor output.
 */
void WstCompositorGetOutputSize( WstCompositor *wctx, unsigned int *width, unsigned int *height );

/**
 * WstCompositorGetNestedDisplayName
 *
 * Obtain the name of the wayland display that this compositor
 * instance will be, or is using to connect to as a nested
 * compositor.  This can be called at any time.
 */
const char *WstCompositorGetNestedDisplayName( WstCompositor *wctx );

/**
 * WstCompositorGetNestedSize
 *
 * Obtain the size of surface this compositor instance will create
 * or has created on another wayland display as a nested compositor.
 * This can be called at any time.
 */
void WstCompositorGetNestedSize( WstCompositor *wctx, unsigned int *width, unsigned int *height );

/**
 * WstCompositorGetAllowCursorModification
 *
 * Determine if this compsitor instance is configured to allow
 * compositor clients to modify the pointer cursor image.  
 * This may be called at any time.
 */
bool WstCompositorGetAllowCursorModification( WstCompositor *wctx );

/**
 * WstCompositorSetTerminatedCallback
 *
 * Specifies a callback for an embedded compositor to invoke to signal that it
 * has terminated.
 */
bool WstCompositorSetTerminatedCallback( WstCompositor *wctx, WstTerminatedCallback cb, void *userData );

/**
 * WstCompositorSetDispatchCallback
 *
 * Specifies a callback for a compositor to periodically invoke to give an opportunity for any required
 * implementatipn specific event dispatching or other 'main loop' type processing.
 */
bool WstCompositorSetDispatchCallback( WstCompositor *wctx, WstDispatchCallback cb, void *userData );

/**
 * WstCompositorSetInvalidateCallback
 *
 * Specifies a callback for an embedded compositor to invoke to signal that its
 * scene has become invalid and that WstCompositorComposeEmbedded should be called.
 */
bool WstCompositorSetInvalidateCallback( WstCompositor *wctx, WstInvalidateSceneCallback cb, void *userData );

/**
 * WstCompositorSetHidePointerCallback
 *
 * Specifies a callback for an embedded compositor to invoke to signal that any
 * pointer image being displayed by the process embedding this compositor should be
 * hidden or shown.  The embedded compositor will request the host cursor be hidden
 * when a client requests a different pointer be used.
 */
bool WstCompositorSetHidePointerCallback( WstCompositor *wctx, WstHidePointerCallback cb, void *userData );

/**
 * WstCompositorSetClientStatusCallback
 *
 * Specifies a callback for an embedded compositor to invoke to signal the status of a
 * client process.  The callback will supply a status value from the WstClient_status
 * enum and the client pid.  If the status is WstClient_status_stoppedAbnormal the detail
 * value will be the signal that caused the client to terminate.
 */
bool WstCompositorSetClientStatusCallback( WstCompositor *wctx, WstClientStatus cb, void *userData );

/**
 * WstCompositorSetOutputNestedListener
 *
 * Specifies a set of callbacks to be invoked by a nested compositor for output events.  By default
 * the nested compositor will forward output events to a connected client.  When a listener is set
 * using WstCompositorSetOutputNestedListener the events will instead be passed to the caller
 * through the specified callback functions.  This allows the caller to handle the events outside
 * of Wayland.  This must be called prior to WstCompositorStart.
 */
bool WstCompositorSetOutputNestedListener( WstCompositor *wctx, WstOutputNestedListener *listener, void *userData );

/**
 * WstCompositorSetKeyboardNestedListener
 *
 * Specifies a set of callbacks to be invoked by a nested compositor for keyboard input.  By default
 * the nested compositor will forward keyboard events to a connected client.  When a listener is set
 * using WstCompositorSetKeyboardNestedListener the events will instead be passed to the caller
 * through the specified callback functions.  This allows the caller to route keyboard input outside
 * of Wayland.  This must be called prior to WstCompositorStart.
 */
bool WstCompositorSetKeyboardNestedListener( WstCompositor *wctx, WstKeyboardNestedListener *listener, void *userData );

/**
 * WstCompositorSetPointerNestedListener
 *
 * Specifies a set of callbacks to be invoked by a nested compositor for pointer input.  By default
 * the nested compositor will forward pointer events to a connected client.  When a listener is set
 * using WstCompositorSetPointerNestedListener the events will instead be passed to the caller
 * through the specified callback functions.  This allows the caller to route pointer input outside
 * of Wayland.  This must be called prior to WstCompositorStart.
 */
bool WstCompositorSetPointerNestedListener( WstCompositor *wctx, WstPointerNestedListener *listener, void *userData );

/**
 * WstCompositorSetVirtualEmbeddedUnBoundClientListener
 *
 * Specifies a callback for a virtual embedded master compositor to invoke to signal
 * that a new client process has connected to the display name shared by the virtual embedded
 * compositor instances created from this master and this client is not currently bound to
 * any virtual embedded compositor instance.  If a client process is started using WstCompositorLaunchClient
 * it will be automatically bound to the specified virtual embedded compositor instance.  This
 * listener provides a means for an externally launched client to be bound using
 * the WstCompositorVirtualEmbeddedBindClient API.
 */
bool WstCompositorSetVirtualEmbeddedUnBoundClientListener( WstCompositor *wctx, WstVirtEmbUnBoundClient listener, void *userData );

/**
 * WstCompositorVirtualEmbeddedBindClient
 *
 * Associate a client process specified by the supplied pid to a virtual embedded compositor instance.  If a
 * client process is started using WstCompositorLaunchClient it will be automatically bound to the specified
 * virtual embedded compositor instance.  This API allows binding an externally launched client detected
 * using a listener registered with WstCompositorSetVirtualEmbeddedUnBoundClientListener.
 */
bool WstCompositorVirtualEmbeddedBindClient( WstCompositor *wctx, int clientPid );

/**
 * WstCompositorComposeEmbedded
 *
 * Requests that the current scene be composed as part of the configured embedded environment.  This
 * should be called with the environment setup for offscreen rendering.  For example, if OpenGL is
 * being used for rendering, WstCompositorComposeEmbedded should be called with an FBO set as the
 * current render target.
 *
 * The x, y, width and height give the desired composition rectangle.  The matrix and alpha
 * values are what the caller intends to apply when the composited scene is subsequently rendered
 * from the offscreen target to the callers scene.  Based on the hinting provided, the compositor will
 * either render to the offscreen target or use an available fast path to render to a separate 
 * plane.  If no fast path is available it will render to the offscreen target without the transform
 * and alpha.  If it renders to a separate plane it will apply the provide transformation matrix and alpha
 * and will set needHolePunch to true.  Upon return, if needHolePunch is true, the caller should 
 * render a hole punch for each rectangle returned in rects, otherwise it should render the offscreen 
 * target to its scene while applying the transformation matrix and alpha.
 *
 * This should only be called while the compositor is running.
 */
bool WstCompositorComposeEmbedded( WstCompositor *wctx,
                                   int x, int y, int width, int height,
                                   float *matrix, float alpha, 
                                   unsigned int hints, 
                                   bool *needHolePunch, std::vector<WstRect> &rects );

/**
 * WstCompositorInvalidateScene
 *
 * Causes the compositor to invalidate its scene and schedule a repaint.  This only has an effect
 * when the compositor is running.
 *
 */
void WstCompositorInvalidateScene( WstCompositor *wctx );

/**
 * WstCompositorStart
 *
 * Start the compositor operating.  This will cause the compositor to create its
 * wayland display, connect to its target wayland display if acting as a nested 
 * compositor, and start processing events.  The function is not blocking and will
 * return as soon as the compositor is operating.
 */
bool WstCompositorStart( WstCompositor *wctx );

/**
 * WstCompositorStop
 *
 * Stops the operation of a compositor.  The compositor will halt all operation
 * and release all resources.
 */
void WstCompositorStop( WstCompositor *wctx );

/**
 * WstCompositorKeyEvent
 *
 * Pass a key event to the compositor.  The compositor will route the event
 * to an appropriate compositor client.
 */
void WstCompositorKeyEvent( WstCompositor *wctx, int keyCode, unsigned int keyState, unsigned int modifiers );

/**
 * WstCompositorPointerEnter
 *
 * Notifiy compositor that the pointer has entered its bounds.
 */
void WstCompositorPointerEnter( WstCompositor *wctx );

/**
 * WstCompositorPointerLeave
 *
 * Notifiy compositor that the pointer has exited its bounds.
 */
void WstCompositorPointerLeave( WstCompositor *wctx );

/**
 * WstCompositorPointerMoveEvent
 *
 * Pass a pointer move event to the compositor.  Th compositor will route the event
 * to an appropriate compositor client.
 */
void WstCompositorPointerMoveEvent( WstCompositor *wctx, int x, int y );

/**
 * WstCompositorPointerButtonEvent
 *
 * Pass a pointer button event to the compositor.  The compositor will route the event
 * to an appropriate compositor client.
 */
void WstCompositorPointerButtonEvent( WstCompositor *wctx, unsigned int button, unsigned int buttonState );

/**
 * WstCompositorTouchEvent
 *
 * Pass a touch event to the compositor.  The compositor will route the event to an
 * appropriate compositor client.
 */
void WstCompositorTouchEvent( WstCompositor *wctx, WstTouchSet *touchSet );

/**
 * WstCompositorLaunchClient
 *
 * Launch a named process intended to connect to the compositor as a client.  This should only be called
 * while the compositor is running.  The function is blocking and will not return until the client
 * process terminates or fails to launch.
 */
bool WstCompositorLaunchClient( WstCompositor *wctx, const char *cmd );

/**
 * WstCompositorFocusClientById
 *
 * Manually change the keyboard input focus to a client using it's id
 */
void WstCompositorFocusClientById( WstCompositor *wctx, const int id);

/**
 * WstCompositorFocusClientByName
 *
 * Manually change the keyboard input focus to a client using it's name. The name uniqueness is the responsibility of
 * the client. The first hit will be returned.
 */
void WstCompositorFocusClientByName( WstCompositor *wctx, const char *name);

#endif

