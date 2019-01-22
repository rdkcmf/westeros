/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2018 RDK Management
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
/*
* Copyright © 2011 Kristian Høgsberg
* Copyright © 2011 Benjamin Franzke
*
* Permission to use, copy, modify, distribute, and sell this software and its
* documentation for any purpose is hereby granted without fee, provided that
* the above copyright notice appear in all copies and that both that copyright
* notice and this permission notice appear in supporting documentation, and
* that the name of the copyright holders not be used in advertising or
* publicity pertaining to distribution of the software without specific,
* written prior permission.  The copyright holders make no representations
* about the suitability of this software for any purpose.  It is provided "as
* is" without express or implied warranty.
*
* THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
* INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
* EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
* CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
* DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
* TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
* OF THIS SOFTWARE.
*/
#ifndef _wayland_egl_h
#define _wayland_egl_h

#include "EGL/egl.h"
#include "EGL/eglext.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct wl_egl_window *wl_egl_window_create(struct wl_surface *surface, int width, int height);
void wl_egl_window_destroy(struct wl_egl_window *egl_window);
void wl_egl_window_resize(struct wl_egl_window *egl_window, int width, int height, int dx, int dy);
void wl_egl_window_get_attached_size(struct wl_egl_window *egl_window, int *width, int *height);
void *wl_egl_get_device_buffer(struct wl_resource *resource);

#ifdef  __cplusplus
}
#endif

#endif

