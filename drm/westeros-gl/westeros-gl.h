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
#ifndef _WESTEROS_GL_H
#define _WESTEROS_GL_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Libraries using libdrm must be c libraries.
 * To ensure we can link with c++ code we use
 * extern "C" here.
 */
typedef struct _WstGLCtx WstGLCtx;

#define WESTEROS_GL_DISPLAY_CAPS

typedef enum _WstGLDisplayCapabilies
{
   WstGLDisplayCap_none = 0,
   WstGLDisplayCap_modeset = (1<<0)
} WstGLDisplayCapabilities;

typedef struct _WstGLDisplaySafeArea
{
   int x;
   int y;
   int w;
   int h;
} WstGLDisplaySafeArea;

typedef struct _WstGLDisplayInfo
{
   int width;
   int height;
   WstGLDisplaySafeArea safeArea;
} WstGLDisplayInfo;

typedef void (*WstGLDisplaySizeCallback)( void *userData, int width, int height );

WstGLCtx* WstGLInit();
void WstGLTerm( WstGLCtx *ctx );
bool WstGLGetDisplayCaps( WstGLCtx *ctx, unsigned int *caps );
bool WstGLSetDisplayMode( WstGLCtx *ctx, const char *mode );
bool WstGLGetDisplayInfo( WstGLCtx *ctx, WstGLDisplayInfo *displayInfo );
bool WstGLGetDisplaySafeArea( WstGLCtx *ctx, int *x, int *y, int *w, int *h );
bool WstGLAddDisplaySizeListener( WstGLCtx *ctx, void *userData, WstGLDisplaySizeCallback listener );
bool WstGLRemoveDisplaySizeListener( WstGLCtx *ctx, WstGLDisplaySizeCallback listener );
void* WstGLCreateNativeWindow( WstGLCtx *ctx, int x, int y, int width, int height );
void WstGLDestroyNativeWindow( WstGLCtx *ctx, void *nativeWindow );
bool WstGLGetNativePixmap( WstGLCtx *ctx, void *nativeBuffer, void **nativePixmap );
void WstGLGetNativePixmapDimensions( WstGLCtx *ctx, void *nativePixmap, int *width, int *height );
void WstGLReleaseNativePixmap( WstGLCtx *ctx, void *nativePixmap );
void* WstGLGetEGLNativePixmap( WstGLCtx *ctx, void *nativePixmap );

#if defined(__cplusplus)
} //extern "C"
#endif
#endif

