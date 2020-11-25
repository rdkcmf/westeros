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

#ifndef _WESTEROS_LINUX_DMABUF_H
#define _WESTEROS_LINUX_DMABUF_H

#include <drm_fourcc.h>
#include "wayland-server.h"
#include "westeros-render.h"

#define WST_LDB_MAX_PLANES 4

#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL<<56) - 1)
#endif

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif

struct wl_ldb;

struct wstLDBInfo
{
   uint32_t format;
   uint32_t flags;
   int32_t width;
   int32_t height;
   int planeCount;
   int fd[WST_LDB_MAX_PLANES];
   uint32_t offset[WST_LDB_MAX_PLANES];
   uint32_t stride[WST_LDB_MAX_PLANES];
   uint64_t modifier[WST_LDB_MAX_PLANES];
};

struct wl_ldb_buffer
{
   struct wl_resource *paramsResource;
   struct wl_resource *bufferResource;
   struct wl_ldb *ldb;
   struct wstLDBInfo info;
};

struct wayland_ldb_callbacks {
   void (*bind)(void *user_data, struct wl_client *client, struct wl_resource *resource);
};

wl_ldb* WstLDBInit( struct wl_display *display, struct wayland_ldb_callbacks *callbacks, void *userData );
void WstLDBUninit( struct wl_ldb *ldb );
void WstLDBSetRenderer( struct wl_ldb *ldb, WstRenderer *renderer );
struct wl_ldb_buffer *WstLDBBufferGet( struct wl_resource *resource );
uint32_t WstLDBBufferGetFormat(struct wl_ldb_buffer *buffer);
int32_t WstLDBBufferGetWidth(struct wl_ldb_buffer *buffer);
int32_t WstLDBBufferGetHeight(struct wl_ldb_buffer *buffer);
int32_t WstLDBBufferGetStride(struct wl_ldb_buffer *buffer);
void WstLDBBufferGetPlaneOffsetAndStride(struct wl_ldb_buffer *buffer, int plane, int32_t *offset, int32_t *stride );
void *WstLDBBufferGetBuffer(struct wl_ldb_buffer *buffer);
int WstLDBBufferGetFd(struct wl_ldb_buffer *buffer);
int WstLDBBufferGetPlaneFd(struct wl_ldb_buffer *buffer, int plane);
uint64_t WstLDBBufferGetPlaneModifier(struct wl_ldb_buffer *buffer, int plane);

#endif

