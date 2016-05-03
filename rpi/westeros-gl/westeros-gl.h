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

typedef struct _WstGLCtx WstGLCtx;

WstGLCtx* WstGLInit();
void WstGLTerm( WstGLCtx *ctx );
void* WstGLCreateNativeWindow( WstGLCtx *ctx, int x, int y, int width, int height );
void WstGLDestroyNativeWindow( WstGLCtx *ctx, void *nativeWindow );
bool WstGLGetNativePixmap( WstGLCtx *ctx, void *nativeBuffer, void **nativePixmap );
void WstGLGetNativePixmapDimensions( WstGLCtx *ctx, void *nativePixmap, int *width, int *height );
void WstGLReleaseNativePixmap( WstGLCtx *ctx, void *nativePixmap );
void* WstGLGetEGLNativePixmap( WstGLCtx *ctx, void *nativePixmap );

#endif

