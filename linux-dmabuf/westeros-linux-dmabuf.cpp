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
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <assert.h>

#include "westeros-linux-dmabuf.h"

#include "wayland-server.h"
#include "linux-dmabuf-unstable-v1-server-protocol.h"

#define MIN(x,y) (((x) < (y)) ? (x) : (y))

static const uint32_t gLDBFormats[]= {
   DRM_FORMAT_XRGB8888,
   DRM_FORMAT_XBGR8888,
   DRM_FORMAT_RGBX8888,
   DRM_FORMAT_BGRX8888,
   DRM_FORMAT_ARGB8888,
   DRM_FORMAT_RGBA8888,
   DRM_FORMAT_ABGR8888,
   DRM_FORMAT_BGRA8888,
   0
};


struct wl_ldb
{
   struct wl_display *display;
   struct wl_global *wl_ldb_global;

   void *userData;
   struct wayland_ldb_callbacks *callbacks;
   WstRenderer *renderer;
};

static void wstLDBBufferDestroy(struct wl_ldb_buffer *buffer)
{
   int i;

   for (i= 0; i < buffer->info.planeCount; i++)
   {
      if ( buffer->info.fd[i] >= 0 )
      {
         close(buffer->info.fd[i]);
      }
      buffer->info.fd[i]= -1;
   }

   buffer->info.planeCount= 0;
   free(buffer);
}

static void wstILDBBufferDestroy(struct wl_client *client,
                                 struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct wl_buffer_interface linux_dmabuf_buffer_interface = {
   wstILDBBufferDestroy
};

static void wstLDBDestroyBuffer(struct wl_resource *resource)
{
   struct wl_ldb_buffer *buffer;

   buffer= (struct wl_ldb_buffer *)wl_resource_get_user_data(resource);
   if ( buffer )
   {
      wstLDBBufferDestroy(buffer);
   }
}

static void wstILDBParamsDestroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void wstILDBParamsAdd(struct wl_client *client,
                            struct wl_resource *resourceParams,
                            int32_t name_fd,
                            uint32_t plane_idx,
                            uint32_t offset,
                            uint32_t stride,
                            uint32_t modifier_hi,
                            uint32_t modifier_lo)
{
   struct wl_ldb_buffer *buffer;
   int version;

   buffer= (struct wl_ldb_buffer*)wl_resource_get_user_data(resourceParams);

   if (!buffer)
   {
      wl_resource_post_error( resourceParams,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                              "a wl_buffer has previously been created from this ldb parameters object" );
      if ( name_fd >= 0 )
      {
         close(name_fd);
      }
      return;
   }

   version= wl_resource_get_version(resourceParams);

   if (plane_idx >= WST_LDB_MAX_PLANES)
   {
      wl_resource_post_error( resourceParams,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                              "the plane plane index is out of bounds: %u",
                              plane_idx );
      if ( name_fd >= 0 )
      {
         close(name_fd);
      }
      return;
   }

   if (buffer->info.fd[plane_idx] != -1)
   {
      wl_resource_post_error( resourceParams,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                              "plane %u has already been assigned a dmabuf fd %d",
                              plane_idx,
                              buffer->info.fd[plane_idx] );
      if ( name_fd >= 0 )
      {
         close(name_fd);
      }
      return;
   }

   buffer->info.fd[plane_idx]= name_fd;
   buffer->info.offset[plane_idx]= offset;
   buffer->info.stride[plane_idx]= stride;
   if ( version < ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION )
   {
      buffer->info.modifier[plane_idx]= DRM_FORMAT_MOD_INVALID;
   }
   else
   {
      buffer->info.modifier[plane_idx]= ((((uint64_t)modifier_hi)<<32)|((uint64_t)modifier_lo));
   }

   buffer->info.planeCount++;
}

static void wstLDBParamsCreate(struct wl_client *client,
                               struct wl_resource *resourceParams,
                               uint32_t buffer_id,
                               int32_t width,
                               int32_t height,
                               uint32_t format,
                               uint32_t flags)
{
   struct wl_ldb_buffer *buffer;
   int i;

   buffer= (struct wl_ldb_buffer*)wl_resource_get_user_data(resourceParams);
   if ( !buffer )
   {
      wl_resource_post_error( resourceParams,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                              "a wl_buffer was previously created from this ldb parameters object" );
      return;
   }

   if ( (width < 0) || (height < 0) ) 
   {
      wl_resource_post_error( resourceParams,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS,
                              "bad dimensions: %dx%d",
                              width,
                              height );
      wstLDBBufferDestroy(buffer);
      return;
   }

   if ( buffer->info.planeCount == 0 ) 
   {
      wl_resource_post_error( resourceParams,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                              "no planes have been added to this ldb parameters object" );
      wstLDBBufferDestroy(buffer);
      return;
   }

   for (i= 0; i < buffer->info.planeCount; i++) 
   {
      if ( buffer->info.fd[i] == -1 )
      {
         wl_resource_post_error( resourceParams,
                                 ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                                 "plane %i has been assigned no parameters",
                                 i );
         wstLDBBufferDestroy(buffer);
         return;
      }
   }

   buffer->info.width= width;
   buffer->info.height= height;
   buffer->info.format= format;
   buffer->info.flags= flags;

   buffer->bufferResource= wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
   if ( !buffer->bufferResource ) 
   {
      wl_resource_post_no_memory(resourceParams);
      if (buffer_id == 0)
      {
         zwp_linux_buffer_params_v1_send_failed(resourceParams);
      }
      else
      {
         wl_resource_post_error( resourceParams,
                                ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
                                "buffer creation failed");
      }
      wstLDBBufferDestroy(buffer);
      return;
   }

   wl_resource_set_implementation(buffer->bufferResource,
                                  &linux_dmabuf_buffer_interface,
                                  buffer,
                                  wstLDBDestroyBuffer );

   if (buffer_id == 0)
   {
      // If there is no buffer id send an immediate created event
      zwp_linux_buffer_params_v1_send_created(resourceParams, buffer->bufferResource);
   }

   // Transfer ownership of wl_ldb_buffer to buffer resource
   wl_resource_set_user_data(buffer->paramsResource, NULL);

   return;
}

static void wstILDBParamsCreate(struct wl_client *client,
                               struct wl_resource *resourceParams,
                               int32_t width, 
                               int32_t height,
                               uint32_t format, 
                               uint32_t flags)
{
   wstLDBParamsCreate(client, resourceParams, 0, width, height, format, flags);
}

static void  wstILDBParamsCreateImmed(struct wl_client *client,
                                     struct wl_resource *resourceParams,
                                     uint32_t buffer_id,
                                     int32_t width,
                                     int32_t height,
                                     uint32_t format,
                                     uint32_t flags)
{
   wstLDBParamsCreate(client, resourceParams, buffer_id, width, height, format, flags);
}

static const struct zwp_linux_buffer_params_v1_interface zwp_linux_buffer_params_interface = {
   wstILDBParamsDestroy,
   wstILDBParamsAdd,
   wstILDBParamsCreate,
   wstILDBParamsCreateImmed
};

static void wstLDBDestroyParams(struct wl_resource *resourceParams)
{
   struct wl_ldb_buffer *buffer;

   buffer= (struct wl_ldb_buffer *)wl_resource_get_user_data(resourceParams);
   if ( buffer )
   {
      wstLDBBufferDestroy(buffer);
   }
}

static void wstILDBDestroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void wstILDBCreateParams(struct wl_client *client,
                                struct wl_resource *linux_dmabuf_resource,
                                uint32_t params_id)
{
   struct wl_ldb *ldb;
   struct wl_ldb_buffer *buffer;
   uint32_t version;
   int i;

   version= wl_resource_get_version(linux_dmabuf_resource);
   ldb= (struct wl_ldb*)wl_resource_get_user_data(linux_dmabuf_resource);

   buffer= (struct wl_ldb_buffer *)calloc(1, sizeof *buffer);
   if (!buffer)
   {
      wl_resource_post_no_memory(linux_dmabuf_resource);
      return;
   }

   for (i= 0; i < WST_LDB_MAX_PLANES; i++)
   {
      buffer->info.fd[i]= -1;
      buffer->info.modifier[i]= DRM_FORMAT_MOD_INVALID;
   }

   buffer->ldb= ldb;
   buffer->paramsResource= wl_resource_create(client,
                                              &zwp_linux_buffer_params_v1_interface,
                                              version,
                                              params_id);
   if (!buffer->paramsResource)
   {
      wl_resource_post_no_memory(linux_dmabuf_resource);
      free( buffer );
      return;
   }

   wl_resource_set_implementation(buffer->paramsResource,
                                  &zwp_linux_buffer_params_interface,
                                  buffer,
                                  wstLDBDestroyParams);

   return;
}

static const struct zwp_linux_dmabuf_v1_interface linux_dmabuf_interface = {
   wstILDBDestroy,
   wstILDBCreateParams
};

static void wstLDBBind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_ldb *ldb= (struct wl_ldb*)data;
   struct wl_resource *resource;
   int i;
   bool formatsSent= false;

   printf("westeros-ldb: wstLDBind: enter: client %p data %p version %d id %d\n", client, data, version, id);

   resource= wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, MIN(version, 3), id);
   if (!resource)
   {
      wl_client_post_no_memory(client);
      return;
   }

   ldb->callbacks->bind( ldb->userData, client, resource);

   wl_resource_set_implementation(resource, &linux_dmabuf_interface, data, NULL);

   if ( ldb->renderer )
   {
      int *formats= 0;
      int numFormats= 0;
      uint64_t *modifiers= 0;
      int numModifiers= 0;
      uint64_t modifierInvalid= DRM_FORMAT_MOD_INVALID;
      int i, j;
      WstRendererQueryDmabufFormats( ldb->renderer, &formats, &numFormats );
      printf("westeros-ldb: found %d dmabuf formats\n", numFormats);
      for( i= 0; i < numFormats; ++i )
      {
         WstRendererQueryDmabufModifiers( ldb->renderer, formats[i], &modifiers, &numModifiers);
         if (numModifiers == 0)
         {
            numModifiers= 1;
            modifiers= &modifierInvalid;
         }
         for( j= 0; j < numModifiers; ++j )
         {
            if ( version >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION )
            {
               uint32_t modifier_lo= modifiers[j] & 0xFFFFFFFF;
               uint32_t modifier_hi= modifiers[j] >> 32;
               zwp_linux_dmabuf_v1_send_modifier(resource, formats[i], modifier_hi, modifier_lo);
            }
            else if ( (modifiers[j] == DRM_FORMAT_MOD_LINEAR) || (modifiers == &modifierInvalid) )
            {
               zwp_linux_dmabuf_v1_send_format(resource, formats[i]);
            }
         }
         if ( modifiers && (modifiers != &modifierInvalid) )
         {
            free(modifiers);
            modifiers= 0;
         }
      }
      if ( formats )
      {
         free( formats );
      }
      if ( numFormats )
      {
         formatsSent= true;
      }
   }

   if ( !formatsSent )
   {
      i= 0;
      while( gLDBFormats[i] != 0 )
      {
         zwp_linux_dmabuf_v1_send_format(resource, gLDBFormats[i]);
         ++i;
      }
   }

   printf("westeros-ldb: wstLDBBind: exit: client %p id %d\n", client, id);
}

wl_ldb* WstLDBInit( struct wl_display *display, struct wayland_ldb_callbacks *callbacks, void *userData )
{
   struct wl_ldb *ldb= 0;

   printf("westeros-ldb: WstLDBInit: enter: display %p\n", display);
   ldb= (struct wl_ldb*)calloc( 1, sizeof(struct wl_ldb) );
   if ( !ldb )
   {
      goto exit;
   }

   ldb->display= display;
   ldb->callbacks= callbacks;
   ldb->userData= userData;

   ldb->wl_ldb_global= wl_global_create(display, &zwp_linux_dmabuf_v1_interface, 3, ldb, wstLDBBind );

exit:
   printf("westeros-ldb: WstLDBInit: exit: display %p sb %p\n", display, ldb);

   return ldb;
}

void WstLDBUninit( struct wl_ldb *ldb )
{
   if ( ldb )
   {
      free( ldb );
   }
}

void WstLDBSetRenderer( struct wl_ldb *ldb, WstRenderer *renderer )
{
   if ( ldb )
   {
      printf("westeros-ldb: set renderer %p\n", renderer);
      ldb->renderer= renderer;
   }
}

struct wl_ldb_buffer *WstLDBBufferGet( struct wl_resource *resource )
{
   if( resource == NULL )
   {
      return NULL;
   }

   if( wl_resource_instance_of( resource, &wl_buffer_interface, &linux_dmabuf_buffer_interface ) )
   {
      return (wl_ldb_buffer *)wl_resource_get_user_data( (wl_resource*)resource );
   }
   else
   {
      return NULL;
   }
}

uint32_t WstLDBBufferGetFormat(struct wl_ldb_buffer *buffer)
{
   return buffer->info.format;
}

int32_t WstLDBBufferGetWidth(struct wl_ldb_buffer *buffer)
{
   return buffer->info.width;
}

int32_t WstLDBBufferGetHeight(struct wl_ldb_buffer *buffer)
{
   return buffer->info.height;
}

int32_t WstLDBBufferGetStride(struct wl_ldb_buffer *buffer)
{
   return buffer->info.stride[0];
}

void WstLDBBufferGetPlaneOffsetAndStride(struct wl_ldb_buffer *buffer, int plane, int32_t *offset, int32_t *stride )
{
   if ( (plane >= 0 ) && (plane < WST_LDB_MAX_PLANES) )
   {
      *offset= buffer->info.offset[plane];
      *stride= buffer->info.stride[plane];
   }
}

int WstLDBBufferGetFd(struct wl_ldb_buffer *buffer)
{
   return buffer->info.fd[0];
}

int WstLDBBufferGetPlaneFd(struct wl_ldb_buffer *buffer, int plane)
{
   int fd= -1;
   if ( (plane >= 0 ) && (plane < WST_LDB_MAX_PLANES) )
   {
      fd= buffer->info.fd[plane];
   }
   return fd;
}

uint64_t WstLDBBufferGetPlaneModifier(struct wl_ldb_buffer *buffer, int plane)
{
   uint64_t modifier= DRM_FORMAT_MOD_INVALID;
   if ( (plane >= 0) && (plane < WST_LDB_MAX_PLANES) )
   {
      modifier= buffer->info.modifier[plane];
   }
   return modifier;
}
