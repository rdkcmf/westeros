/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2017 RDK Management
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

#ifndef __ESSOS_APP__
#define __ESSOS_APP__

/* ----------------------------------------------------------------------------
 * Essos apps are single windowed portable applications that render graphics
 * using OpenGLES2.
 *
 * The API flow for a minimal app is:
 * EssContextCreate()
 * EssContextInit()
 * EssContextSetKeyListener()
 * EssContextSetPointerListener()
 * EssContextSetSettingsListener()
 * EssContextSetTerminateListener()
 * doEGLSetup() including:
 *   EssContextGetEGLDisplayType()
 *   EssContextCreateNativeWindow()
 * EssContextStart()
 *
 * then execute a main loop that includes
 *
 *   EssContextRunEventLoopOnce()
 *   doOpenGLES2Rendering()
 *   EssContextUpdateDisplay();
 *---------------------------------------------------------------------------- */
#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _EssCtx EssCtx;

typedef struct _EssKeyListener
{
   void (*keyPressed)( void *userData, unsigned int key ); 
   void (*keyReleased)( void *userData, unsigned int key ); 
   void (*keyRepeat)( void *userData, unsigned int key ); 
} EssKeyListener;

typedef struct _EssPointerListener
{
   void (*pointerMotion)( void *userData, int x, int y );
   void (*pointerButtonPressed)( void *userData, int button, int x, int y );
   void (*pointerButtonReleased)( void *userData, int button, int x, int y );
} EssPointerListener;

typedef struct _EssTouchListener
{
   void (*touchDown)( void *userData, int id, int x, int y );
   void (*touchUp)( void *userData, int id );
   void (*touchMotion)( void *userData, int id, int x, int y );
   void (*touchFrame)( void *userData );
} EssTouchListener;

typedef struct _EssSettingsListener
{
   void (*displaySize)( void *userData, int width, int height );
   /*
    * If content rendered to the edges of the display are not guaranteed to be
    * visible, this callback will provide information on the region of the
    * application display surface that are guaranteed to be visible.  An application
    * can optionally use this information to, for example, draw a background image
    * that goes to the display edges but position important information within
    * the specified safe area. */
   void (*displaySafeArea)( void *userData, int x, int y, int width, int height );
} EssSettingsListener;

typedef struct _EssTerminateListener
{
   /*
    * Called to notify the application that it should shutdown. If this
    * callback is invoked it means some abnormal condition has occurred and
    * the application should shutdown.
    */
   void (*terminated)( void *userData );
} EssTerminateListener;


/**
 * EssContextCreate
 *
 * Create an Essos application context.
 */
EssCtx* EssContextCreate();

/**
 * EssContextDestroy
 *
 * Destroy and application instance.  If the application is running
 * it will be stopped.  All resources will be freed.
 */
void EssContextDestroy( EssCtx *ctx );

/**
 * EssContextGetLastErrorDetail
 *
 * Returns a null terminated string giving information about the
 * last error that has occurred.  If any Essos API fails, this method
 * can be used to obtain a string that should be logged.
 */

const char *EssContextGetLastErrorDetail( EssCtx *ctx );

/**
 * EssContextInit
 *
 * Initialize an application context.  Inititialization will be performed
 * by EssContextStart but for use cases where it is not desired to start
 * an application context, EssContextInit must be called before methods
 * such as EssContextGetEGLDisplayType or ESSContextCreateNativeWindow
 * can be called.
 */
bool EssContextInit( EssCtx *ctx );

/**
 * EssContextGetEGLDisplayType
 *
 * Returns a NativeDisplayType value that can be used in an eglGetDisplay call.  This
 * API is for applications that wish to create their EGL environment rather than allowing Essos
 * to do so automatically.
 */
bool EssContextGetEGLDisplayType( EssCtx *ctx, NativeDisplayType *displayType );

/**
 * EssContextCreateNativeWindow
 *
 * Creates a NativeWindowType value that can be used in an eglCreateWindowSurface call.  Passing
 * a NULL value for pointer to nativeWindow causes the EGL environment to be automtically
 * setup during the call to EssContextStart.
 */
bool EssContextCreateNativeWindow( EssCtx *ctx, int width, int h, NativeWindowType *nativeWindow );

/**
 * EssContextDestroyNativeWindow
 *
 * Destroys a NativeWindowType value obtained from EssContextCreateNativeWindow.
 */
bool EssContextDestroyNativeWindow( EssCtx *ctx, NativeWindowType nativeWindow );

/**
 * EssContextSetKeyListener
 *
 * Set a key listener (see EssKeyListener) to receive key event callbacks. Key
 * codes are Linux codes defined by linux/input.h
 */
bool EssContextSetKeyListener( EssCtx *ctx, void *userData, EssKeyListener *listener );

/**
 * EssContextSetPointerListener
 *
 * Set a pointer listener (see EssPointerListener) to receive pointer event callbacks.
 * Button codes are Linux codes defined by linux/input.h
 */
bool EssContextSetPointerListener( EssCtx *ctx, void *userData, EssPointerListener *listener );

/**
 * EssContextSetTouchListener
 *
 * Set a touch listener (see EssTouchListener) to receive touch event callbacks.
 */
bool EssContextSetTouchListener( EssCtx *ctx, void *userData, EssTouchListener *listener );

/**
 * EssContextSetSettingsListener
 *
 * Set a settings listener (see EssSettingsListener) to receive settings event callbacks.
 */
bool EssContextSetSettingsListener( EssCtx *ctx, void *userData, EssSettingsListener *listener );

/**
 * EssContextSetTerminateListener
 *
 * Set a terminate listener (see EssTerminateListener) to receive a callback when the
 * application is being terminated.  The registered terminate listener will be invoked
 * if some abnormal condition required the application to shutdown.
 */
bool EssContextSetTerminateListener( EssCtx *ctx, void *userData, EssTerminateListener *listener );

/**
 * EssContextSetName
 *
 * Establish name of the application context.  This must be called
 * before initializing or starting the application
 */
bool EssContextSetName( EssCtx *ctx, const char *name );

/**
 * EssContextGetDisplaySize
 *
 * Returns the width and height of the display.
 */
bool EssContextGetDisplaySize( EssCtx *ctx, int *width, int *height );

/**
 * EssContextGetDisplaySafeArea
 *
 * Returns a rectangle giving the display safe area.  This is the region of the display
 * that is guaranteed to be visible to the user and not hidden by overscan.
 */
bool EssContextGetDisplaySafeArea( EssCtx *ctx, int *x, int *y, int *width, int *height );

/**
 * EssContextStart
 *
 * Start an application context running.  Context initialization will be performed by this call
 * if it has not already been done with EssContextInit. For applications that allow Essos to perform EGL
 * setup, the EGL environment will be active after calling this method. GLES2 rendering can then
 * be performed on this thread with buffer flips triggered by calls to EssContextUpdateDisplay. For
 * aoolications that manually perform EGL creation the EGL creation must be done between calling
 * EssContextInit and EssContextStart.  When manual EGL setup is done (EssContextCreateNativeWindow has
 * been called), EssContextStart will skip automatic EGL setup.  EssContextStart will also perform
 * setup required for user input and any other required setup.
 *
 * While running the EssContextRunEventLoop method must be regularly called.  
 */
bool EssContextStart( EssCtx *ctx );

/**
 * EssContextStop
 *
 * Stop an application context.
 */
void EssContextStop( EssCtx *ctx );

/**
 * EssContextResizeWindow
 *
 * Set a new window size.  This API may be called in response to a display size notification received
 * via an Essos settings listener.
 */
bool EssContextResizeWindow( EssCtx *ctx, int width, int height );

/**
 * EssContextRunEventLoopOnce
 *
 * Perform event processing.  This API will not block if no events are pending.
 * It must be called regularly while the aoplication is running.
 */
void EssContextRunEventLoopOnce( EssCtx *ctx);

/**
 * EssContextUpdateDisplay
 *
 * Perform a buffer flip operation.
 */
void EssContextUpdateDisplay( EssCtx *ctx );


#if defined(__cplusplus)
} //extern "C"
#endif
#endif

