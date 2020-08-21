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

#ifndef __ESSOS_SYSTEM__
#define __ESSOS_SYSTEM__

#ifndef __cplusplus
#include <stdbool.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#include <linux/input.h>

/**
 * EssContextSupportWayland
 *
 * Returns true if the context supports running as a
 * Wayland application.  To configure the application to
 * run as a Wayland application call EssContextSetUseWayland.
 */
bool EssContextSupportWayland( EssCtx *ctx );

/**
 * EssContextSupportDirect
 *
 * Returns true if the context supports running as a
 * normal native EGL applicaiton.  By default a newly
 * created context will be configurd to run as a direct
 * EGL application.
 */
bool EssContextSupportDirect( EssCtx *ctx );

/**
 * EssContextSetUseWayland
 *
 * Configure an application context to run as a Wayland
 * application.  This must be called before initializing or 
 * starting the application.
 */
bool EssContextSetUseWayland( EssCtx *ctx, bool useWayland );

/**
 * EssContextGetUseWayland
 *
 * Returns true if the application context is configured to
 * run as a Wayland application.
 */
bool EssContextGetUseWayland( EssCtx *ctx );

/**
 * EssContextSetUseDirect
 *
 * Configure an application context to run as a normal direct
 * EGL application.  This must be called before initializing or 
 * starting the application.
 */
bool EssContextSetUseDirect( EssCtx *ctx, bool useDirect );

/**
 * EssContextGetUseDirect
 *
 * Returns true if the application context is configured to
 * run as a normal direct application.
 */
bool EssContextGetUseDirect( EssCtx *ctx );

/**
 * EssContextSetDisplayMode
 *
 * Available only on devices where westeros-gl supports mode setting.
 *
 * Set the active display mode. The mode is specified by a string
 * with the format "[wx]h[p|i][[x]r]".  For example:
 * 1920x1080i60
 * 720p
 * 1080i
 * 3840x2160
 * 1920x1080x24
 */
bool EssContextSetDisplayMode( EssCtx *ctx, const char *mode );

/**
 * EssContextGetWaylandDisplay
 *
 * If the context is initialized and configured to run as a Wayland app, this call
 * will return the wayland display handle.
 */
void* EssContextGetWaylandDisplay( EssCtx *ctx );

/**
 * EssContextSetDisplaySize
 *
 * Used to inform Essos of the display size for a direct EGL application,  For a Wayland application
 * the source of the display size is the compositor and this call will be ignored,
 */
bool EssContextSetDisplaySize( EssCtx *ctx, int width, int height );

/**
 * EssContextSetInitialWindowSize
 *
 * Specifies the window size to use when the application starts
 */
bool EssContextSetInitialWindowSize( EssCtx *ctx, int width, int height );

/**
 * EssContextSetWindowPosition
 *
 * Sets the window position.  For a Wayland application, this will
 * set the window position if the application is not a fullscreen
 * application (ie. connected to a Westeros embedded compositor).  For a
 * direct Linux application this call will be ignored.
 */
bool EssContextSetWindowPosition( EssCtx *ctx, int x, int y );

/**
 * EssContextSetKeyRepeatInitialDelay
 *
 * Set the initial delay in milliseconds when a key is pressed until repeats start.
 */
 bool EssContextSetKeyRepeatInitialDelay( EssCtx *ctx, int delay );

/**
 * EssContextSetKeyRepeatPeriod
 *
 * Set the period in milliseconds between key repeats.
 */
 bool EssContextSetKeyRepeatPeriod( EssCtx *ctx, int period );

/**
 * EssContextSetSwapInterval
 *
 * Sets the EGL swap interval used by the context.  The default interval is 1.
 */
bool EssContextSetSwapInterval( EssCtx *ctx, EGLint swapInterval );

/**
 * EssContextSetEGLConfigAttributes
 *
 * Specifies a set of EGL surface attributes to be used as constraints
 * when choosing an EGL config.  This call can be made to replace the default
 * attributes used by Essos.
 */
bool EssContextSetEGLConfigAttributes( EssCtx *ctx, EGLint *attrs, EGLint size );

/**
 * EssContextGetEGLConfigAttributes
 *
 * Returns the current set of attributes the context is configured to use
 * when choosing EGL config.
 */
bool EssContextGetEGLConfigAttributes( EssCtx *ctx, EGLint **attrs, EGLint *size );

/**
 * EssContextSetEGLSurfaceAttributes
 *
 * Specifies a set of EGL surface attributes to be used when creating
 * an EGL surface.  This call can be made to replace the default
 * attributes used by Essos.
 */
bool EssContextSetEGLSurfaceAttributes( EssCtx *ctx, EGLint *attrs, EGLint size );

/**
 * EssContextGetEGLSurfaceAttributes
 *
 * Returns the current set of attributes the context is configured to use
 * when creating EGL surfaces.
 */
bool EssContextGetEGLSurfaceAttributes( EssCtx *ctx, EGLint **attrs, EGLint *size );

/**
 * EssContextSetEGLContextAttributes
 *
 * Specifies a set of EGL context attributes to be used when creating
 * an EGL context.  This call can be made to replace the default
 * attributes used by Essos.
 */
bool EssContextSetEGLContextAttributes( EssCtx *ctx, EGLint *attrs, EGLint size );

/**
 * EssContextSetEGLContectAttributes
 *
 * Returns the current set of attributes the context is configured to use
 * when creating an EGL context..
 */
bool EssContextGetEGLContextAttributes( EssCtx *ctx, EGLint **attrs, EGLint *size );

typedef struct _EssInputDeviceMetadata
{
    dev_t deviceNumber;
    char * devicePhysicalAddress;
    input_id id;
    uint8_t filterCode;
} EssInputDeviceMetadata;

typedef struct _EssKeyAndMetadataListener
{
   void (*keyPressed)( void *userData, unsigned int key, EssInputDeviceMetadata *metadata );
   void (*keyReleased)( void *userData, unsigned int key, EssInputDeviceMetadata *metadata );
   void (*keyRepeat)( void *userData, unsigned int key, EssInputDeviceMetadata *metadata );
} EssKeyAndMetadataListener;


/**
 * EssContextSetKeyAndMetadataListener
 *
 * Set a key listener (see EssKeyListener) to receive key event callbacks. Key
 * codes are Linux codes defined by linux/input.h.
 * Metadata contain an additional information specific for a source of key input
 */
bool EssContextSetKeyAndMetadataListener( EssCtx *ctx, void *userData, EssKeyAndMetadataListener *listener, EssInputDeviceMetadata *metadata );


#if defined(__cplusplus)
} //extern "C"
#endif
#endif

