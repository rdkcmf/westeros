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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/input.h>

#include "test-keyboard.h"
#include "test-egl.h"

#include "westeros-compositor.h"
#include "wayland-client.h"
#include "wayland-egl.h"
#include <xkbcommon/xkbcommon.h>

namespace Keyboard
{

typedef struct _TestCtx
{
   TestEGLCtx eglCtx;
   struct wl_display *display;
   struct wl_compositor *compositor;
   struct wl_seat *seat;
   struct wl_pointer *pointer;
   struct wl_keyboard *keyboard;
   struct wl_surface *surface;
   struct wl_egl_window *wlEglWindow;
   int windowWidth;
   int windowHeight;
   struct xkb_context *xkbCtx;
   struct xkb_keymap *xkbKeymap;
   struct xkb_state *xkbState;
   xkb_mod_index_t modAlt;
   xkb_mod_index_t modCtrl;
   xkb_mod_index_t modShift;
   xkb_mod_index_t modCaps;
   unsigned int modMask;
   bool keyboardMap;
   bool keyboardEnter;
   bool keyboardLeave;
   bool keyboardRepeatInfo;
   bool keyAlt;
   bool keyCtrl;
   bool keyShift;
   bool keyCaps;
   int keyPressed;
} TestCtx;

static void pointerEnter( void* data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy )
{
   TestCtx *ctx= (TestCtx*)data;
}

static void pointerLeave( void* data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface )
{
   TestCtx *ctx= (TestCtx*)data;
}

static void pointerMotion( void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy )
{
}

static void pointerButton( void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state )
{
}

static void pointerAxis( void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value )
{
}

static const struct wl_pointer_listener pointerListener = {
   pointerEnter,
   pointerLeave,
   pointerMotion,
   pointerButton,
   pointerAxis
};

static void keyboardKeymap( void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size )
{
   TestCtx *ctx= (TestCtx*)data;

   if ( format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 )
   {
      void *map= mmap( 0, size, PROT_READ, MAP_SHARED, fd, 0 );
      if ( map != MAP_FAILED )
      {
         if ( !ctx->xkbCtx )
         {
            ctx->xkbCtx= xkb_context_new( XKB_CONTEXT_NO_FLAGS );
         }
         else
         {
            printf("error: xkb_context_new failed\n");
         }
         if ( ctx->xkbCtx )
         {
            if ( ctx->xkbKeymap )
            {
               xkb_keymap_unref( ctx->xkbKeymap );
               ctx->xkbKeymap= 0;
            }
            ctx->xkbKeymap= xkb_keymap_new_from_string( ctx->xkbCtx, (char*)map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            if ( !ctx->xkbKeymap )
            {
               printf("error: xkb_keymap_new_from_string failed\n");
            }
            if ( ctx->xkbState )
            {
               xkb_state_unref( ctx->xkbState );
               ctx->xkbState= 0;
            }
            ctx->xkbState= xkb_state_new( ctx->xkbKeymap );
            if ( !ctx->xkbState )
            {
               printf("error: xkb_state_new failed\n");
            }
            if ( ctx->xkbKeymap )
            {
               ctx->modAlt= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_ALT );
               ctx->modCtrl= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CTRL );
               ctx->modShift= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_SHIFT );
               ctx->modCaps= xkb_keymap_mod_get_index( ctx->xkbKeymap, XKB_MOD_NAME_CAPS );
            }
            munmap( map, size );

            ctx->keyboardMap= true;
         }
      }
   }

   close( fd );
}
static void keyboardEnter( void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys )
{
   TestCtx *ctx= (TestCtx*)data;
   printf("keyboard enter\n");
   ctx->keyboardEnter= true;
}

static void keyboardLeave( void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface )
{
   TestCtx *ctx= (TestCtx*)data;
   printf("keyboard leave\n");
   ctx->keyboardLeave= true;
}

static void keyboardKey( void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state )
{
   TestCtx *ctx= (TestCtx*)data;
   if ( state == WL_KEYBOARD_KEY_STATE_PRESSED )
   {
      ctx->keyPressed= key;
   }
   else if ( state == WL_KEYBOARD_KEY_STATE_RELEASED )
   {
      ctx->keyPressed= 0;
   }
}

static void keyboardModifiers( void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group )
{
   TestCtx *ctx= (TestCtx*)data;
   int wasActive, nowActive;
   if ( ctx->xkbState )
   {
      xkb_state_update_mask( ctx->xkbState, mods_depressed, mods_latched, mods_locked, 0, 0, group );

      wasActive= (ctx->modMask & (1<<ctx->modAlt));
      nowActive= (mods_depressed & (1<<ctx->modAlt));
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modAlt);
         ctx->keyAlt= nowActive;
      }

      wasActive= (ctx->modMask & (1<<ctx->modCtrl));
      nowActive= (mods_depressed & (1<<ctx->modCtrl));
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modCtrl);
         ctx->keyCtrl= nowActive;
      }

      wasActive= (ctx->modMask & (1<<ctx->modShift));
      nowActive= (mods_depressed & (1<<ctx->modShift));
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modShift);
         ctx->keyShift= nowActive;
      }

      wasActive= (ctx->modMask & (1<<ctx->modCaps));
      nowActive= (mods_locked & (1<<ctx->modCaps));
      if ( nowActive != wasActive )
      {
         ctx->modMask ^= (1<<ctx->modCaps);
         ctx->keyCaps= nowActive;
      }
   }
}

static void keyboardRepeatInfo( void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay )
{
   TestCtx *ctx= (TestCtx*)data;
   ctx->keyboardRepeatInfo= true;
}

static const struct wl_keyboard_listener keyboardListener= {
   keyboardKeymap,
   keyboardEnter,
   keyboardLeave,
   keyboardKey,
   keyboardModifiers,
   keyboardRepeatInfo
};

static void seatCapabilities( void *data, struct wl_seat *seat, uint32_t capabilities )
{
   TestCtx *ctx= (TestCtx*)data;
   if ( capabilities & WL_SEAT_CAPABILITY_KEYBOARD )
   {
      ctx->keyboard= wl_seat_get_keyboard( ctx->seat );
      printf("keyboard %p\n", ctx->keyboard );
      wl_keyboard_add_listener( ctx->keyboard, &keyboardListener, ctx );
      wl_display_roundtrip(ctx->display);
   }
   if ( capabilities & WL_SEAT_CAPABILITY_POINTER )
   {
      ctx->pointer= wl_seat_get_pointer( ctx->seat );
      printf("pointer %p\n", ctx->pointer );
      wl_pointer_add_listener( ctx->pointer, &pointerListener, ctx );
      wl_display_roundtrip(ctx->display);
   }
}

static void seatName( void *data, struct wl_seat *seat, const char *name )
{
}

static const struct wl_seat_listener seatListener = {
   seatCapabilities,
   seatName
};

static void registryHandleGlobal(void *data, 
                                 struct wl_registry *registry, uint32_t id,
                                 const char *interface, uint32_t version)
{
   TestCtx *ctx= (TestCtx*)data;
   int len;

   len= strlen(interface);

   if ( (len==13) && !strncmp(interface, "wl_compositor", len) ) {
      ctx->compositor= (struct wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
      printf("compositor %p\n", ctx->compositor);
   }
   else if ( (len==7) && !strncmp(interface, "wl_seat", len) ) {
      ctx->seat= (struct wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 4);
      printf("seat %p\n", ctx->seat);
      wl_seat_add_listener(ctx->seat, &seatListener, ctx);
      wl_display_roundtrip(ctx->display);
   }
}

static void registryHandleGlobalRemove(void *data, 
                                      struct wl_registry *registry,
                                      uint32_t name)
{
}

static const struct wl_registry_listener registryListener = 
{
   registryHandleGlobal,
   registryHandleGlobalRemove
};

} // namespace Keyboard

using namespace Keyboard;

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

bool testCaseKeyboardBasicKeyInput( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   const char *displayName= "test0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   memset( &testCtx, 0, sizeof(TestCtx) );

   display= wl_display_connect(displayName);
   if ( !display )
   {
      EMERROR( "wl_display_connect failed" );
      goto exit;
   }
   ctx->display= display;

   registry= wl_display_get_registry(display);
   if ( !registry )
   {
      EMERROR( "wl_display_get_registrty failed" );
      goto exit;
   }

   wl_registry_add_listener(registry, &registryListener, ctx);

   wl_display_roundtrip(display);

   if ( !ctx->compositor || !ctx->seat || !ctx->keyboard || !ctx->pointer )
   {
      EMERROR("Failed to acquire needed compositor items");
      goto exit;
   }

   result= testSetupEGL( &ctx->eglCtx, display );
   if ( !result )
   {
      EMERROR("testSetupEGL failed");
      goto exit;
   }

   ctx->surface= wl_compositor_create_surface(ctx->compositor);
   printf("surface=%p\n", ctx->surface);   
   if ( !ctx->surface )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   ctx->windowWidth= WINDOW_WIDTH;
   ctx->windowHeight= WINDOW_HEIGHT;
   
   ctx->wlEglWindow= wl_egl_window_create(ctx->surface, ctx->windowWidth, ctx->windowHeight);
   if ( !ctx->wlEglWindow )
   {
      EMERROR("error: unable to create wl_egl_window");
      goto exit;
   }
   printf("wl_egl_window %p\n", ctx->wlEglWindow);

   ctx->eglCtx.eglSurfaceWindow= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                  ctx->eglCtx.eglConfig,
                                                  (EGLNativeWindowType)ctx->wlEglWindow,
                                                  NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", ctx->eglCtx.eglSurfaceWindow );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow, ctx->eglCtx.eglSurfaceWindow, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapInterval( ctx->eglCtx.eglDisplay, 1 );

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

   wl_display_roundtrip(display);

   usleep( 17000 );

   if ( !ctx->keyboardMap || !ctx->keyboardEnter || !ctx->keyboardRepeatInfo )
   {
      EMERROR("Did not get expected keyboard events: map %d entered %d repeatInfo %d", 
               ctx->keyboardMap, ctx->keyboardEnter, ctx->keyboardRepeatInfo );
   }   

   WstCompositorKeyEvent( wctx,  KEY_A, WstKeyboard_keyState_depressed, 0 );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   if ( (ctx->keyPressed != KEY_A) ||
        (ctx->keyAlt || ctx->keyCtrl || ctx->keyShift || ctx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 0/%d ctrl 0/%d shift 0/%d caps 0/%d",
              KEY_A, ctx->keyPressed, ctx->keyAlt, ctx->keyCtrl, ctx->keyShift, ctx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_A, WstKeyboard_keyState_released, 0 );
   WstCompositorKeyEvent( wctx,  KEY_B, WstKeyboard_keyState_depressed, WstKeyboard_shift );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   if ( (ctx->keyPressed != KEY_B) ||
        (ctx->keyAlt || ctx->keyCtrl || !ctx->keyShift || ctx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 0/%d ctrl 0/%d shift 1/%d caps 0/%d",
              KEY_B, ctx->keyPressed, ctx->keyAlt, ctx->keyCtrl, ctx->keyShift, ctx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_B, WstKeyboard_keyState_released, 0 );
   WstCompositorKeyEvent( wctx,  KEY_C, WstKeyboard_keyState_depressed, WstKeyboard_alt );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   if ( (ctx->keyPressed != KEY_C) ||
        (!ctx->keyAlt || ctx->keyCtrl || ctx->keyShift || ctx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 1/%d ctrl 0/%d shift 0/%d caps 0/%d",
              KEY_C, ctx->keyPressed, ctx->keyAlt, ctx->keyCtrl, ctx->keyShift, ctx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_C, WstKeyboard_keyState_released, 0 );
   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_depressed, WstKeyboard_ctrl|WstKeyboard_caps );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   if ( (ctx->keyPressed != KEY_D) ||
        (ctx->keyAlt || !ctx->keyCtrl || ctx->keyShift || !ctx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 0/%d ctrl 1/%d shift 0/%d caps 1/%d",
              KEY_D, ctx->keyPressed, ctx->keyAlt, ctx->keyCtrl, ctx->keyShift, ctx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_released, 0 );

   wl_display_roundtrip(display);

   usleep( 17000 );

   wl_display_roundtrip(display);

   testResult= true;

exit:

   if ( ctx->eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow );
      ctx->eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }

   if ( ctx->wlEglWindow )
   {
      wl_egl_window_destroy( ctx->wlEglWindow );
      ctx->wlEglWindow= 0;
   }

   if ( ctx->surface )
   {
      wl_surface_destroy( ctx->surface );
      ctx->surface= 0;
   }

   testTermEGL( &ctx->eglCtx );

   if ( ctx->keyboard )
   {
      wl_keyboard_destroy(ctx->keyboard);
      ctx->keyboard= 0;
   }

   if ( ctx->pointer )
   {
      wl_pointer_destroy(ctx->pointer);
      ctx->pointer= 0;
   }

   if ( ctx->seat )
   {
      wl_seat_destroy(ctx->seat);
      ctx->seat= 0;
   }

   if ( ctx->compositor )
   {
      wl_compositor_destroy( ctx->compositor );
      ctx->compositor= 0;
   }

   if ( registry )
   {
      wl_registry_destroy(registry);
      registry= 0;
   }

   if ( display )
   {
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctx );

   return testResult;
}

bool testCaseKeyboardBasicKeyInputRepeater( EMCTX *emctx )
{
   bool testResult= false;
   bool result;
   WstCompositor *wctx= 0;
   WstCompositor *wctxRepeater= 0;
   const char *displayName= "test0";
   const char *displayNameNested= "nested0";
   struct wl_display *display= 0;
   struct wl_registry *registry= 0;
   TestCtx testCtx;
   TestCtx *ctx= &testCtx;
   EGLBoolean b;

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctx, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetRendererModule( wctx, "libwesteros_render_gl.so.0.0.0" );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetRendererModule failed" );
      goto exit;
   }

   result= WstCompositorStart( wctx );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   wctxRepeater= WstCompositorCreate();
   if ( !wctxRepeater )
   {
      EMERROR( "WstCompositorCreate failed" );
      goto exit;
   }

   result= WstCompositorSetDisplayName( wctxRepeater, displayNameNested );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorSetIsRepeater( wctxRepeater, true );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetIsNested failed" );
      goto exit;
   }

   result= WstCompositorSetNestedDisplayName( wctxRepeater, displayName );
   if ( result == false )
   {
      EMERROR( "WstCompositorSetDisplayName failed" );
      goto exit;
   }

   result= WstCompositorStart( wctxRepeater );
   if ( result == false )
   {
      EMERROR( "WstCompositorStart failed" );
      goto exit;
   }

   memset( &testCtx, 0, sizeof(TestCtx) );

   display= wl_display_connect(displayNameNested);
   if ( !display )
   {
      EMERROR( "wl_display_connect failed" );
      goto exit;
   }
   ctx->display= display;

   registry= wl_display_get_registry(display);
   if ( !registry )
   {
      EMERROR( "wl_display_get_registrty failed" );
      goto exit;
   }

   wl_registry_add_listener(registry, &registryListener, ctx);

   wl_display_roundtrip(display);

   if ( !ctx->compositor || !ctx->seat || !ctx->keyboard || !ctx->pointer )
   {
      EMERROR("Failed to acquire needed compositor items");
      goto exit;
   }

   result= testSetupEGL( &ctx->eglCtx, display );
   if ( !result )
   {
      EMERROR("testSetupEGL failed");
      goto exit;
   }

   ctx->surface= wl_compositor_create_surface(ctx->compositor);
   printf("surface=%p\n", ctx->surface);   
   if ( !ctx->surface )
   {
      EMERROR("error: unable to create wayland surface");
      goto exit;
   }

   ctx->windowWidth= WINDOW_WIDTH;
   ctx->windowHeight= WINDOW_HEIGHT;
   
   ctx->wlEglWindow= wl_egl_window_create(ctx->surface, ctx->windowWidth, ctx->windowHeight);
   if ( !ctx->wlEglWindow )
   {
      EMERROR("error: unable to create wl_egl_window");
      goto exit;
   }
   printf("wl_egl_window %p\n", ctx->wlEglWindow);

   ctx->eglCtx.eglSurfaceWindow= eglCreateWindowSurface( ctx->eglCtx.eglDisplay,
                                                  ctx->eglCtx.eglConfig,
                                                  (EGLNativeWindowType)ctx->wlEglWindow,
                                                  NULL );
   printf("eglCreateWindowSurface: eglSurfaceWindow %p\n", ctx->eglCtx.eglSurfaceWindow );

   b= eglMakeCurrent( ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow, ctx->eglCtx.eglSurfaceWindow, ctx->eglCtx.eglContext );
   if ( !b )
   {
      EMERROR("error: eglMakeCurrent failed: %X", eglGetError() );
      goto exit;
   }

   eglSwapInterval( ctx->eglCtx.eglDisplay, 1 );

   eglSwapBuffers(ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow);

   wl_display_roundtrip(display);

   usleep( 35000 );

   if ( !ctx->keyboardMap || !ctx->keyboardEnter || !ctx->keyboardRepeatInfo )
   {
      EMERROR("Did not get expected keyboard events: map %d entered %d repeatInfo %d", 
               ctx->keyboardMap, ctx->keyboardEnter, ctx->keyboardRepeatInfo );
   }   

   WstCompositorKeyEvent( wctx,  KEY_A, WstKeyboard_keyState_depressed, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( (ctx->keyPressed != KEY_A) ||
        (ctx->keyAlt || ctx->keyCtrl || ctx->keyShift || ctx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 0/%d ctrl 0/%d shift 0/%d caps 0/%d",
              KEY_A, ctx->keyPressed, ctx->keyAlt, ctx->keyCtrl, ctx->keyShift, ctx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_A, WstKeyboard_keyState_released, 0 );
   WstCompositorKeyEvent( wctx,  KEY_B, WstKeyboard_keyState_depressed, WstKeyboard_shift );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( (ctx->keyPressed != KEY_B) ||
        (ctx->keyAlt || ctx->keyCtrl || !ctx->keyShift || ctx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 0/%d ctrl 0/%d shift 1/%d caps 0/%d",
              KEY_B, ctx->keyPressed, ctx->keyAlt, ctx->keyCtrl, ctx->keyShift, ctx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_B, WstKeyboard_keyState_released, 0 );
   WstCompositorKeyEvent( wctx,  KEY_C, WstKeyboard_keyState_depressed, WstKeyboard_alt );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( (ctx->keyPressed != KEY_C) ||
        (!ctx->keyAlt || ctx->keyCtrl || ctx->keyShift || ctx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 1/%d ctrl 0/%d shift 0/%d caps 0/%d",
              KEY_C, ctx->keyPressed, ctx->keyAlt, ctx->keyCtrl, ctx->keyShift, ctx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_C, WstKeyboard_keyState_released, 0 );
   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_depressed, WstKeyboard_ctrl|WstKeyboard_caps );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   if ( (ctx->keyPressed != KEY_D) ||
        (ctx->keyAlt || !ctx->keyCtrl || ctx->keyShift || !ctx->keyCaps )
      )
   {
      EMERROR("Did not get expected key event: expected/actual: key %d/%d alt 0/%d ctrl 1/%d shift 0/%d caps 1/%d",
              KEY_D, ctx->keyPressed, ctx->keyAlt, ctx->keyCtrl, ctx->keyShift, ctx->keyCaps);
      goto exit;
   }

   WstCompositorKeyEvent( wctx,  KEY_D, WstKeyboard_keyState_released, 0 );

   wl_display_roundtrip(display);

   usleep( 35000 );

   wl_display_roundtrip(display);

   testResult= true;

exit:

   if ( ctx->eglCtx.eglSurfaceWindow )
   {
      eglDestroySurface( ctx->eglCtx.eglDisplay, ctx->eglCtx.eglSurfaceWindow );
      ctx->eglCtx.eglSurfaceWindow= EGL_NO_SURFACE;
   }

   if ( ctx->wlEglWindow )
   {
      wl_egl_window_destroy( ctx->wlEglWindow );
      ctx->wlEglWindow= 0;
   }

   if ( ctx->surface )
   {
      wl_surface_destroy( ctx->surface );
      ctx->surface= 0;
   }

   testTermEGL( &ctx->eglCtx );

   if ( ctx->keyboard )
   {
      wl_keyboard_destroy(ctx->keyboard);
      ctx->keyboard= 0;
   }

   if ( ctx->pointer )
   {
      wl_pointer_destroy(ctx->pointer);
      ctx->pointer= 0;
   }

   if ( ctx->seat )
   {
      wl_seat_destroy(ctx->seat);
      ctx->seat= 0;
   }

   if ( ctx->compositor )
   {
      wl_compositor_destroy( ctx->compositor );
      ctx->compositor= 0;
   }

   if ( registry )
   {
      wl_registry_destroy(registry);
      registry= 0;
   }

   if ( display )
   {
      wl_display_disconnect(display);
      display= 0;
   }

   WstCompositorDestroy( wctxRepeater );

   WstCompositorDestroy( wctx );

   return testResult;
}

