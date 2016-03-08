#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include "westeros-simplebuffer.h"

#include "wayland-server.h"
#include "simplebuffer-server-protocol.h"

#define MIN(x,y) (((x) < (y)) ? (x) : (y))

struct wl_sb 
{
   struct wl_display *display;
   struct wl_global *wl_sb_global;
	
	void *userData;
   struct wayland_sb_callbacks *callbacks;
};

static void wstISimpleBufferDestroy(struct wl_client *client, struct wl_resource *resource);

const static struct wl_buffer_interface bufferInterface {
   wstISimpleBufferDestroy
};

static void wstISBCreateBuffer(struct wl_client *client, struct wl_resource *resource,
                               uint32_t id, uint32_t native_handle, int32_t width, int32_t height,
                               uint32_t stride, uint32_t format);
static void wstISBCreatePlanarBuffer(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id, uint32_t native_handle,
                                     int32_t width, int32_t height, uint32_t format,
                                     int32_t offset0, int32_t offset1, int32_t offset2, 
                                     int32_t stride0, int32_t stride1, int32_t stride2);
static void wstSBCreateBuffer(struct wl_client *client, struct wl_resource *resource,
                              uint32_t id, uint32_t native_handle, int32_t w, int32_t h,
                              uint32_t fmt, int32_t off0, int32_t off1, int32_t off2,
                              int32_t strd0, int32_t strd1, int32_t strd2);

static void wstISimpleBufferDestroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void wstSBDestroyBuffer(struct wl_resource *resource)
{
   struct wl_sb_buffer *buffer = (struct wl_sb_buffer*)resource->data;
   struct wl_sb *sb = buffer->sb;

   sb->callbacks->release_buffer(sb->userData, buffer);
   free(buffer);
}

const static struct wl_sb_interface sb_interface = {
   wstISBCreateBuffer,
   wstISBCreatePlanarBuffer
};

static void wstSBBind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_sb *sb= (struct wl_sb*)data;
   struct wl_resource *resource;
   uint32_t capabilities;
	
	printf("westeros-sb: wstSBBind: enter: client %p data %p version %d id %d\n", client, data, version, id);

   resource= wl_resource_create(client, &wl_sb_interface, MIN(version, 1), id);
   if (!resource) 
   {
      wl_client_post_no_memory(client);
      return;
   }

   sb->callbacks->bind( sb->userData, client, resource);

   wl_resource_set_implementation(resource, &sb_interface, data, NULL);

   wl_resource_post_event(resource, WL_SB_FORMAT, WL_SB_FORMAT_ARGB8888);
	
	printf("westeros-sb: wstSBBind: exit: client %p id %d\n", client, id);
}

static void wstISBCreateBuffer(struct wl_client *client, struct wl_resource *resource,
                                uint32_t id, uint32_t native_handle, int32_t width, int32_t height,
                                uint32_t stride, uint32_t format)
{
   switch (format) 
   {
      case WL_SB_FORMAT_ARGB8888:
      case WL_SB_FORMAT_XRGB8888:
      case WL_SB_FORMAT_YUYV:
      case WL_SB_FORMAT_RGB565:
         break;
      default:
         wl_resource_post_error(resource, WL_SB_ERROR_INVALID_FORMAT, "invalid format");
         return;
   }

   wstSBCreateBuffer(client, resource, id, native_handle, width, height, 
                     format, 0, 0, 0, stride, 0, 0);
}

static void wstISBCreatePlanarBuffer(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id, uint32_t native_handle,
                                     int32_t width, int32_t height, uint32_t format,
                                     int32_t offset0, int32_t offset1, int32_t offset2, 
                                     int32_t stride0, int32_t stride1, int32_t stride2)
{
   switch (format) 
   {
      case WL_SB_FORMAT_YUV410:
      case WL_SB_FORMAT_YUV411:
      case WL_SB_FORMAT_YUV420:
      case WL_SB_FORMAT_YUV422:
      case WL_SB_FORMAT_YUV444:
      case WL_SB_FORMAT_NV12:
      case WL_SB_FORMAT_NV16:
         break;
      default:
         wl_resource_post_error(resource, WL_SB_ERROR_INVALID_FORMAT, "invalid format");
         return;
   }

   wstSBCreateBuffer(client, resource, id, native_handle, width, height, 
                     format, offset0, offset1, offset2, stride0, stride1, stride2);
}

static void wstSBCreateBuffer(struct wl_client *client, struct wl_resource *resource,
                              uint32_t id, uint32_t native_handle, int32_t width, int32_t height,
                              uint32_t fmt, int32_t off0, int32_t off1, int32_t off2,
                              int32_t strd0, int32_t strd1, int32_t strd2)
{
   struct wl_sb *sb= (struct wl_sb*)resource->data;
   struct wl_sb_buffer *buff;
   buff= (wl_sb_buffer*)calloc(1, sizeof *buff);
   if (!buff) 
   {
      wl_resource_post_no_memory(resource);
      return;
   }

   sb->callbacks->reference_buffer(sb->userData, client, native_handle, buff);
   if ( !buff->driverBuffer ) 
   {
      wl_resource_post_error(resource, WL_SB_ERROR_INVALID_NATIVE_HANDLE, "invalid native_handle");
      return;
   }

   buff->resource= wl_resource_create(client, &wl_buffer_interface, 1, id);
   if (!buff->resource) 
   {
      wl_resource_post_no_memory(resource);
      free(buff);
      return;
   }
   
   buff->sb= sb;
   buff->width= width;
   buff->height= height;
   buff->format= fmt;
   buff->offset[0]= off0;
   buff->offset[1]= off1;
   buff->offset[2]= off2;
   buff->stride[0]= strd0;
   buff->stride[1]= strd1;
   buff->stride[2]= strd2;

   wl_resource_set_implementation(buff->resource,
                                 (void (**)(void)) &bufferInterface,
                                 buff, wstSBDestroyBuffer);
}

wl_sb* WstSBInit( struct wl_display *display, struct wayland_sb_callbacks *callbacks, void *userData )
{
   struct wl_sb *sb= 0;
   
	printf("westeros-sb: WstSBInit: enter: display %p\n", display);
   sb= (struct wl_sb*)calloc( 1, sizeof(struct wl_sb) );
   if ( !sb )
   {
      goto exit;
   }
   
   sb->display= display;
   sb->callbacks= callbacks;
   sb->userData= userData;
  
   sb->wl_sb_global= wl_global_create(display, &wl_sb_interface, 1, sb, wstSBBind );

exit:
	printf("westeros-sb: WstSBInit: exit: display %p sb %p\n", display, sb);

   return sb;
}

void WstSBUninit( struct wl_sb *sb )
{
   if ( sb )
   {
      free( sb );
   }
}

struct wl_sb_buffer *WstSBBufferGet( struct wl_resource *resource )
{
   if( resource == NULL )
      return NULL;

   if( wl_resource_instance_of( resource, &wl_buffer_interface, &bufferInterface ) ) 
   {
      return (wl_sb_buffer *)wl_resource_get_user_data( (wl_resource*)resource );
   }
   else
      return NULL;
}

uint32_t WstSBBufferGetFormat(struct wl_sb_buffer *buffer)
{
   return buffer->format;
}

int32_t WstSBBufferGetWidth(struct wl_sb_buffer *buffer)
{
   return buffer->width;
}

int32_t WstSBBufferGetHeight(struct wl_sb_buffer *buffer)
{
   return buffer->height;
}

int32_t WstSBBufferGetStride(struct wl_sb_buffer *buffer)
{
   return buffer->stride[0];
}

void WstSBBufferGetPlaneOffsetAndStride(struct wl_sb_buffer *buffer, int plane, int32_t *offset, int32_t *stride )
{
   if ( (plane >=0 ) && (plane <= 2) )
   {
      *offset= buffer->offset[plane];
      *stride= buffer->stride[plane];
   }
}

void *WstSBBufferGetBuffer(struct wl_sb_buffer *buffer)
{
   return buffer->driverBuffer;
}


