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
#ifndef _WESTEROS_SIMPLEBUFFER_H
#define _WESTEROS_SIMPLEBUFFER_H

#include "wayland-server.h"

#ifndef WL_SB_FORMAT_ENUM
#define WL_SB_FORMAT_ENUM
enum wl_sb_format {
        WL_SB_FORMAT_C8 = 0x20203843,
        WL_SB_FORMAT_RGB332 = 0x38424752,
        WL_SB_FORMAT_BGR233 = 0x38524742,
        WL_SB_FORMAT_XRGB4444 = 0x32315258,
        WL_SB_FORMAT_XBGR4444 = 0x32314258,
        WL_SB_FORMAT_RGBX4444 = 0x32315852,
        WL_SB_FORMAT_BGRX4444 = 0x32315842,
        WL_SB_FORMAT_ARGB4444 = 0x32315241,
        WL_SB_FORMAT_ABGR4444 = 0x32314241,
        WL_SB_FORMAT_RGBA4444 = 0x32314152,
        WL_SB_FORMAT_BGRA4444 = 0x32314142,
        WL_SB_FORMAT_XRGB1555 = 0x35315258,
        WL_SB_FORMAT_XBGR1555 = 0x35314258,
        WL_SB_FORMAT_RGBX5551 = 0x35315852,
        WL_SB_FORMAT_BGRX5551 = 0x35315842,
        WL_SB_FORMAT_ARGB1555 = 0x35315241,
        WL_SB_FORMAT_ABGR1555 = 0x35314241,
        WL_SB_FORMAT_RGBA5551 = 0x35314152,
        WL_SB_FORMAT_BGRA5551 = 0x35314142,
        WL_SB_FORMAT_RGB565 = 0x36314752,
        WL_SB_FORMAT_BGR565 = 0x36314742,
        WL_SB_FORMAT_RGB888 = 0x34324752,
        WL_SB_FORMAT_BGR888 = 0x34324742,
        WL_SB_FORMAT_XRGB8888 = 0x34325258,
        WL_SB_FORMAT_XBGR8888 = 0x34324258,
        WL_SB_FORMAT_RGBX8888 = 0x34325852,
        WL_SB_FORMAT_BGRX8888 = 0x34325842,
        WL_SB_FORMAT_ARGB8888 = 0x34325241,
        WL_SB_FORMAT_ABGR8888 = 0x34324241,
        WL_SB_FORMAT_RGBA8888 = 0x34324152,
        WL_SB_FORMAT_BGRA8888 = 0x34324142,
        WL_SB_FORMAT_XRGB2101010 = 0x30335258,
        WL_SB_FORMAT_XBGR2101010 = 0x30334258,
        WL_SB_FORMAT_RGBX1010102 = 0x30335852,
        WL_SB_FORMAT_BGRX1010102 = 0x30335842,
        WL_SB_FORMAT_ARGB2101010 = 0x30335241,
        WL_SB_FORMAT_ABGR2101010 = 0x30334241,
        WL_SB_FORMAT_RGBA1010102 = 0x30334152,
        WL_SB_FORMAT_BGRA1010102 = 0x30334142,
        WL_SB_FORMAT_YUYV = 0x56595559,
        WL_SB_FORMAT_YVYU = 0x55595659,
        WL_SB_FORMAT_UYVY = 0x59565955,
        WL_SB_FORMAT_VYUY = 0x59555956,
        WL_SB_FORMAT_AYUV = 0x56555941,
        WL_SB_FORMAT_NV12 = 0x3231564e,
        WL_SB_FORMAT_NV21 = 0x3132564e,
        WL_SB_FORMAT_NV16 = 0x3631564e,
        WL_SB_FORMAT_NV61 = 0x3136564e,
        WL_SB_FORMAT_YUV410 = 0x39565559,
        WL_SB_FORMAT_YVU410 = 0x39555659,
        WL_SB_FORMAT_YUV411 = 0x31315559,
        WL_SB_FORMAT_YVU411 = 0x31315659,
        WL_SB_FORMAT_YUV420 = 0x32315559,
        WL_SB_FORMAT_YVU420 = 0x32315659,
        WL_SB_FORMAT_YUV422 = 0x36315559,
        WL_SB_FORMAT_YVU422 = 0x36315659,
        WL_SB_FORMAT_YUV444 = 0x34325559,
        WL_SB_FORMAT_YVU444 = 0x34325659,
};
#endif /* WL_SB_FORMAT_ENUM */

struct wl_sb;

struct wl_sb_buffer 
{
   struct wl_resource *resource;
   struct wl_sb *sb;
   int32_t width, height;
   uint32_t format;
   const void *driverFormat;
   int32_t offset[3];
   int32_t stride[3];
   int fd[3];
   uint32_t native_handle;
   void *driverBuffer;
};

struct wayland_sb_callbacks {
   void (*bind)(void *user_data, struct wl_client *client, struct wl_resource *resource);
   
   void (*reference_buffer)(void *user_data, struct wl_client *client, uint32_t native_handle, struct wl_sb_buffer *buffer);

   void (*release_buffer)(void *user_data, struct wl_sb_buffer *buffer);
};

wl_sb* WstSBInit( struct wl_display *display, struct wayland_sb_callbacks *callbacks, void *userData );
void WstSBUninit( struct wl_sb *sb );
struct wl_sb_buffer *WstSBBufferGet( struct wl_resource *resource );
uint32_t WstSBBufferGetFormat(struct wl_sb_buffer *buffer);
int32_t WstSBBufferGetWidth(struct wl_sb_buffer *buffer);
int32_t WstSBBufferGetHeight(struct wl_sb_buffer *buffer);
int32_t WstSBBufferGetStride(struct wl_sb_buffer *buffer);
void WstSBBufferGetPlaneOffsetAndStride(struct wl_sb_buffer *buffer, int plane, int32_t *offset, int32_t *stride );
void *WstSBBufferGetBuffer(struct wl_sb_buffer *buffer);
int WstSBBufferGetFd(struct wl_sb_buffer *buffer);
int WstSBBufferGetPlaneFd(struct wl_sb_buffer *buffer, int plane);

#endif

