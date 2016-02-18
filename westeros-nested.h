#ifndef _WESTEROS_NESTED_H
#define _WESTEROS_NESTED_H

#include <pthread.h>

#include "wayland-client.h"

typedef struct _WstCompositor WstCompositor;
typedef struct _WstNestedConnection WstNestedConnection;

typedef void (*WSTCallbackConnectionEnded)( void *userData );
typedef void (*WSTCallbackKeyboardHandleKeyMap)( void *userData, uint32_t format, int fd, uint32_t size );
typedef void (*WSTCallbackKeyboardHandleEnter)( void *userData, struct wl_array *keys );
typedef void (*WSTCallbackKeyboardHandleLeave)( void *userData );
typedef void (*WSTCallbackKeyboardHandleKey)( void *userData, uint32_t time, uint32_t key, uint32_t state );
typedef void (*WSTCallbackKeyboardHandleModifiers)( void *userData, uint32_t mods_depressed, uint32_t mods_latched,
                                                    uint32_t mods_locked, uint32_t group );
typedef void (*WSTCallbackKeyboardHandleRepeatInfo)( void *userData, int32_t rate, int32_t delay );

typedef void (*WSTCallbackPointerHandleEnter)( void *userData, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WSTCallbackPointerHandleLeave)( void *userData );
typedef void (*WSTCallbackPointerHandleMotion)( void *userData, uint32_t time, wl_fixed_t sx, wl_fixed_t sy );
typedef void (*WSTCallbackPointerHandleButton)( void *userData, uint32_t time, uint32_t button, uint32_t state );
typedef void (*WSTCallbackPointerHandleAxis)( void *userData, uint32_t time, uint32_t axis, wl_fixed_t value );

typedef struct _WstNestedConnectionListener
{
   WSTCallbackConnectionEnded connectionEnded;
   WSTCallbackKeyboardHandleKeyMap keyboardHandleKeyMap;
   WSTCallbackKeyboardHandleEnter keyboardHandleEnter;
   WSTCallbackKeyboardHandleLeave keyboardHandleLeave;
   WSTCallbackKeyboardHandleKey keyboardHandleKey;
   WSTCallbackKeyboardHandleModifiers keyboardHandleModifiers;
   WSTCallbackKeyboardHandleRepeatInfo keyboardHandleRepeatInfo;
   WSTCallbackPointerHandleEnter pointerHandleEnter;
   WSTCallbackPointerHandleLeave pointerHandleLeave;
   WSTCallbackPointerHandleMotion pointerHandleMotion;
   WSTCallbackPointerHandleButton pointerHandleButton;
   WSTCallbackPointerHandleAxis pointerHandleAxis;
} WstNestedConnectionListener;

WstNestedConnection* WstNestedConnectionCreate( WstCompositor *wctx, 
                                                const char *displayName, 
                                                int width, 
                                                int height,
                                                WstNestedConnectionListener *listener,
                                                void *userData );

void WstNestedConnectionDisconnect( WstNestedConnection *nc );

void WstNestedConnectionDestroy( WstNestedConnection *nc );

wl_display* WstNestedConnectionGetDisplay( WstNestedConnection *nc );

wl_surface* WstNestedConnectionGetSurface( WstNestedConnection *nc );

#endif

