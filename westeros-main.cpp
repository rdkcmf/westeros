#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>
#include <memory.h>
#include <assert.h>
#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>

#if !defined (WESTEROS_PLATFORM_EMBEDDED)
#include <GL/glew.h>
#include <GL/glut.h>
#include <GL/freeglut.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <X11/Xlib.h>
#endif

#include <vector>
#include <map>

#include "westeros-compositor.h"

typedef struct _InputCtx
{
   bool started;
   bool stopRequested;
   std::vector<pollfd> deviceFds;
   WstCompositor *wctx;
} InputCtx;

typedef struct _AppCtx
{
   WstCompositor *wctx;
   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   pthread_t inputThreadId;
   InputCtx *inputCtx;
   #else
   char title[32];
   void *nativeWindow;
   int glutWindowId;
   #endif
} AppCtx;

static bool g_running= false;
static std::map<int,AppCtx*> g_appCtxMap= std::map<int,AppCtx*>();

static void signalHandler(int signum)
{
   printf("signalHandler: signum %d\n", signum);
	g_running= false;
}

static void showUsage()
{
   printf("usage:\n");
   printf(" westeros [options]\n" );
   printf("where [options] are:\n" );
   printf("  --renderer <module> : renderer module to use\n" );
   printf("  --framerate <rate> : frame rate in fps\n" );
   printf("  --display <name> : name of wayland display created by compositor\n" );
   printf("  --repeater : operate as a repeating nested compositor\n" );
   printf("  --nested : operate as a nested compositor\n" );
   printf("  --nestedDisplay <name> : name of wayland display to connect to for nested composition\n" );
   printf("  --nestedInput : register nested input listeners\n" ); 
   printf("  --width <width> : width of nested composition surface\n" );
   printf("  --height <width> : height of nested composition surface\n" );
   printf("  -? : show usage\n" );
   printf("\n" );
}

#if defined (WESTEROS_PLATFORM_EMBEDDED)
static const char *inputByPath= "/dev/input/by-path/";
static const char *kbdDev= "event-kbd";
static const char *mouseDev= "event-mouse";

int openDevice( std::vector<pollfd> &deviceFds, const char *devPathName )
{
   int fd= open( devPathName, O_RDONLY | O_CLOEXEC );
   if ( fd < 0 )
   {
      printf( "error opening device: %s\n", devPathName );
   }
   else
   {
      pollfd pfd;
      printf( "opened device %s : fd %d\n", devPathName, fd );
      pfd.fd= fd;
      deviceFds.push_back( pfd );
   }
}

char *getDevice( const char *devType, const char *path, char *devName )
{
   int len, lenDev;
   char *devPathName= 0;
   
   len= strlen( devName );
   
   lenDev= strlen(devType);
   if ( len > lenDev )
   {
     if ( !strncmp( devName+len-lenDev, devType, lenDev) )
     {
        devPathName= (char *)malloc( strlen(path)+len+1);
        if ( devPathName )
        {
           strcpy( devPathName, path );
           strcat( devPathName, devName );
           
           printf( "found %s: %s\n", devType, devPathName );           
        }
     }
   }
   
   return devPathName;
}

void getDevices( std::vector<pollfd> &deviceFds )
{
   int maxName, buffSize;
   DIR * dir;
   struct dirent *entry= 0;
   struct dirent *result;
   char *devPathName;
   
   maxName= pathconf( inputByPath, _PC_NAME_MAX );
   if ( maxName < 0 ) maxName= 255;
   
   buffSize= offsetof(struct dirent, d_name) + maxName + 1;
   entry= (struct dirent*)malloc( buffSize );
   if ( entry )
   {
      dir= opendir( inputByPath );
      if ( dir )
      {
         while( !readdir_r( dir, entry, &result) )
         {
            if ( !result )
            {
               break;
            }
            
            devPathName= getDevice( kbdDev, inputByPath, result->d_name );
            if ( !devPathName )
            {
               devPathName= getDevice( mouseDev, inputByPath, result->d_name );
            }

            if ( devPathName )
            {
               openDevice( deviceFds, devPathName );
               free( devPathName );
            }
         }
         
         closedir( dir );
      }
      free( entry );
   }
}

void releaseDevices( std::vector<pollfd> &deviceFds )
{
   while( deviceFds.size() > 0 )
   {
      pollfd pfd= deviceFds[0];
      printf( "closing device fd: %d\n", pfd.fd );
      close( pfd.fd );
      deviceFds.erase( deviceFds.begin() );
   }
}

void* inputThread( void *data )
{
   InputCtx *inCtx= (InputCtx*)data;
   int deviceCount= inCtx->deviceFds.size();
   int i, n;
   input_event e;
   unsigned int keyModifiers= 0;
   int mouseAccel= 1;
   int mouseX= 0;
   int mouseY= 0;
   unsigned int outputWidth, outputHeight;
   bool mouseEnterSent= false;
   bool mouseMoved= false;
   
   inCtx->started= true;
      
   while( !inCtx->stopRequested )
   {
      for( i= 0; i < deviceCount; ++i )
      {
         inCtx->deviceFds[i].events= POLLIN | POLLERR;
         inCtx->deviceFds[i].revents= 0;
      }
      
      n= poll(&inCtx->deviceFds[0], deviceCount, 400);
      if ( n >= 0 )
      {
         for( i= 0; i < deviceCount; ++i )
         {
            if ( inCtx->deviceFds[i].revents & POLLIN )
            {
               n= read( inCtx->deviceFds[i].fd, &e, sizeof(input_event) );
               if ( n > 0 )
               {
                  switch( e.type )
                  {
                     case EV_KEY:
                        switch( e.code )
                        {
                           case BTN_LEFT:
                           case BTN_RIGHT:
                           case BTN_MIDDLE:
                           case BTN_SIDE:
                           case BTN_EXTRA:
                              {
                                 unsigned int keyCode= e.code;
                                 unsigned int keyState;
                                 
                                 if ( !mouseEnterSent )
                                 {
                                    WstCompositorPointerEnter( inCtx->wctx );
                                    mouseEnterSent= true;
                                 }
                                 switch ( e.value )
                                 {
                                    case 0:
                                       keyState= WstKeyboard_keyState_released;
                                       break;
                                    case 1:
                                       keyState= WstKeyboard_keyState_depressed;
                                       break;
                                    default:
                                       keyState= WstKeyboard_keyState_none;
                                       break;
                                 }

                                 if ( keyState != WstKeyboard_keyState_none )
                                 {
                                    WstCompositorPointerButtonEvent( inCtx->wctx, keyCode, keyState );
                                 }
                              }
                              break;
                           default:
                              {
                                 int keyCode= e.code;
                                 unsigned int keyState;

                                 switch( keyCode )
                                 {
                                    case KEY_LEFTSHIFT:
                                    case KEY_RIGHTSHIFT:
                                       if ( e.value )
                                          keyModifiers |= WstKeyboard_shift;
                                       else
                                          keyModifiers &= ~WstKeyboard_shift;
                                       break;
                                       
                                    case KEY_LEFTCTRL:
                                    case KEY_RIGHTCTRL:
                                       if ( e.value )
                                          keyModifiers |= WstKeyboard_ctrl;
                                       else
                                          keyModifiers &= ~WstKeyboard_ctrl;
                                       break;

                                    case KEY_LEFTALT:
                                    case KEY_RIGHTALT:
                                       if ( e.value )
                                          keyModifiers |= WstKeyboard_alt;
                                       else
                                          keyModifiers &= ~WstKeyboard_alt;
                                       break;
                                     default:
                                        {
                                          switch ( e.value )
                                          {
                                             case 0:
                                                keyState= WstKeyboard_keyState_released;
                                                break;
                                             case 1:
                                                keyState= WstKeyboard_keyState_depressed;
                                                break;
                                             default:
                                                keyState= WstKeyboard_keyState_none;
                                                break;
                                          }

                                          if ( keyState != WstKeyboard_keyState_none )
                                          {
                                             WstCompositorKeyEvent( inCtx->wctx,
                                                                    keyCode,
                                                                    keyState,
                                                                    keyModifiers );
                                          }                                          
                                        }
                                        break;
                                 }                                 
                              }
                              break;
                        }
                        break;
                     case EV_REL:
                        if ( !outputWidth || !outputHeight )
                        {
                           WstCompositorGetOutputSize( inCtx->wctx, &outputWidth, &outputHeight );
                        }
                        switch( e.code )
                        {
                           case REL_X:
                              mouseX= mouseX + e.value * mouseAccel;
                              if ( mouseX < 0 ) mouseX= 0;
                              if ( mouseX > outputWidth ) mouseX= outputWidth;
                              mouseMoved= true;
                              break;
                           case REL_Y:
                              mouseY= mouseY + e.value * mouseAccel;
                              if ( mouseY < 0 ) mouseY= 0;
                              if ( mouseY > outputHeight ) mouseY= outputHeight;
                              mouseMoved= true;
                              break;
                           default:
                              break;
                        }
                        break;
                     case EV_SYN:
                        {
                           if ( mouseMoved )
                           {
                              if ( !mouseEnterSent )
                              {
                                 WstCompositorPointerEnter( inCtx->wctx );
                                 mouseEnterSent= true;
                              }

                              WstCompositorPointerMoveEvent( inCtx->wctx, mouseX, mouseY );
                              
                              mouseMoved= false;
                           }
                        }
                        break;
                     default:
                        break;
                  }
               }
            }
         }
      }
   }
   
   return NULL;
}

#else

AppCtx* appCtxFromWindowId( int id )
{
   AppCtx *appCtx= 0;

   std::map<int,AppCtx*>::iterator it= g_appCtxMap.find( id );
   if ( it != g_appCtxMap.end() )
   {
      appCtx= it->second;
   }
   
   return appCtx;
}

void onGlutClose()
{
   g_running= false;
}

void onGlutDisplay()
{
   // Nothing to do
}

void onGlutReshape( int width, int height )
{
   AppCtx *appCtx= appCtxFromWindowId( glutGetWindow() );
   if ( appCtx )
   {
      WstCompositorSetOutputSize( appCtx->wctx, width, height );
   }
}

void onGlutMotion( int x, int y )
{
   AppCtx *appCtx= appCtxFromWindowId( glutGetWindow() );
   if ( appCtx )
   {
      WstCompositorPointerMoveEvent( appCtx->wctx, x, y );
   }
}

void onGlutMouse( int button, int state, int x, int y )
{
   bool haveButton= true;
   unsigned int keyCode;
   unsigned int keyState;

   AppCtx *appCtx= appCtxFromWindowId( glutGetWindow() );
   if ( appCtx )
   {
      keyState= (state == GLUT_DOWN) 
                ?
                  WstKeyboard_keyState_depressed
                :
                  WstKeyboard_keyState_released
                ;   
      
      switch( button )
      {
         case GLUT_LEFT_BUTTON:
            keyCode= BTN_LEFT;
            break;
         case GLUT_RIGHT_BUTTON:
            keyCode= BTN_RIGHT;
            break;
         default:
            haveButton= false;
            break;
      }
      
      if ( haveButton )
      {
         WstCompositorPointerButtonEvent( appCtx->wctx, keyCode, keyState );
      }
   }   
}

int keyGlutToLinux[]=
{
  0x00, 0x00, 0x00, KEY_C, 0x00, 0x00, 0x00, 0x00, 
  KEY_BACKSPACE, 0x00, 0x00, 0x00, 0x00, KEY_ENTER, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, KEY_ESC, 0x00, 0x00, 0x00, 0x00, 
  KEY_SPACE, KEY_1, KEY_APOSTROPHE, KEY_3, KEY_4, KEY_5, KEY_7, KEY_APOSTROPHE,
  KEY_KPLEFTPAREN, KEY_KPRIGHTPAREN, KEY_8, KEY_EQUAL, KEY_COMMA, KEY_MINUS, KEY_DOT, KEY_SLASH,
  KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
  KEY_8, KEY_9, KEY_SEMICOLON, KEY_SEMICOLON, KEY_COMMA, KEY_EQUAL, KEY_DOT, KEY_SLASH,
  KEY_2, KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
  KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
  KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W,
  KEY_X, KEY_Y, KEY_Z, KEY_LEFTBRACE, KEY_BACKSLASH, KEY_RIGHTBRACE, KEY_6, KEY_MINUS,
  KEY_GRAVE, KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
  KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
  KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W,
  KEY_X, KEY_Y, KEY_Z, KEY_LEFTBRACE, KEY_BACKSLASH, KEY_RIGHTBRACE, KEY_GRAVE, KEY_DELETE
};

void onGlutKeyboard( unsigned char key, int x, int y )
{
   AppCtx *appCtx= appCtxFromWindowId( glutGetWindow() );
   if ( appCtx )
   {
      int keyCode= 0;
      unsigned int keyModifiers= 0;
      
      key= tolower(key);
      int glutModifiers= glutGetModifiers();
      
      if ( glutModifiers & GLUT_ACTIVE_SHIFT )
      {
         keyModifiers |= WstKeyboard_shift;
      }
      if ( glutModifiers & GLUT_ACTIVE_CTRL )
      {
         keyModifiers |= WstKeyboard_ctrl;
      }
      if ( glutModifiers & GLUT_ACTIVE_ALT )
      {
         keyModifiers |= WstKeyboard_alt;
      }

      if ( key < 128 )
      {
         keyCode= keyGlutToLinux[key];
      }      
      
      if ( keyCode )
      {
         WstCompositorKeyEvent( appCtx->wctx,
                                keyCode,
                                WstKeyboard_keyState_depressed,
                                keyModifiers );

         WstCompositorKeyEvent( appCtx->wctx,
                                keyCode,
                                WstKeyboard_keyState_released,
                                keyModifiers );
      }
   }
}

void onGlutSpecial( int key, int x, int y )
{
   AppCtx *appCtx= appCtxFromWindowId( glutGetWindow() );
   if ( appCtx )
   {
      int keyCode= 0;
      unsigned int keyModifiers= 0;
      
      key= tolower(key);
      int glutModifiers= glutGetModifiers();
      
      if ( glutModifiers & GLUT_ACTIVE_SHIFT )
      {
         keyModifiers |= WstKeyboard_shift;
      }
      if ( glutModifiers & GLUT_ACTIVE_CTRL )
      {
         keyModifiers |= WstKeyboard_ctrl;
      }
      if ( glutModifiers & GLUT_ACTIVE_ALT )
      {
         keyModifiers |= WstKeyboard_alt;
      }
      
      switch( key )
      {
         case GLUT_KEY_F1:
            keyCode= KEY_F1;
            break;
         case GLUT_KEY_F2:
            keyCode= KEY_F2;
            break;
         case GLUT_KEY_F3:
            keyCode= KEY_F3;
            break;
         case GLUT_KEY_F4:
            keyCode= KEY_F4;
            break;
         case GLUT_KEY_F5:
            keyCode= KEY_F5;
            break;
         case GLUT_KEY_F6:
            keyCode= KEY_F6;
            break;
         case GLUT_KEY_F7:
            keyCode= KEY_F7;
            break;
         case GLUT_KEY_F8:
            keyCode= KEY_F8;
            break;
         case GLUT_KEY_F9:
            keyCode= KEY_F9;
            break;
         case GLUT_KEY_F10:
            keyCode= KEY_F10;
            break;
         case GLUT_KEY_F11:
            keyCode= KEY_F11;
            break;
         case GLUT_KEY_F12:
            keyCode= KEY_F2;
            break;
         case GLUT_KEY_LEFT:
            keyCode= KEY_LEFT;
            break;
         case GLUT_KEY_UP:
            keyCode= KEY_UP;
            break;
         case GLUT_KEY_RIGHT:
            keyCode= KEY_RIGHT;
            break;
         case GLUT_KEY_DOWN:
            keyCode= KEY_DOWN;
            break;
         case GLUT_KEY_PAGE_UP:
            keyCode= KEY_PAGEUP;
            break;
         case GLUT_KEY_PAGE_DOWN:
            keyCode= KEY_PAGEDOWN;
            break;
         case GLUT_KEY_HOME:
            keyCode= KEY_HOME;
            break;
         case GLUT_KEY_END:
            keyCode= KEY_END;
            break;
         case GLUT_KEY_INSERT:
            keyCode= KEY_INSERT;
            break;
      }
      
      if ( keyCode )
      {
         WstCompositorKeyEvent( appCtx->wctx,
                                keyCode,
                                WstKeyboard_keyState_depressed,
                                keyModifiers );

         WstCompositorKeyEvent( appCtx->wctx,
                                keyCode,
                                WstKeyboard_keyState_released,
                                keyModifiers );
      }
   }   
}

void *getNativeWindowFromName( Display *display, Window window, char *name )
{
   void *nativeWindow= 0;
   Window dummy;
   Window *children;
   unsigned int numberOfChildren;
   char *windowName;
   int rc, i;
   
   rc= XQueryTree( display, window, &dummy, &dummy, &children, &numberOfChildren );
   if ( rc )
   {
      for( i= 0; i < numberOfChildren; ++i )
      {
         if ( XFetchName( display, children[i], &windowName ) )
         {
            if ( !strcmp( windowName, name ) )
            {
               nativeWindow= (void*)children[i];
            }
            XFree( windowName );
         }
         else
         {
            nativeWindow= (void *)getNativeWindowFromName( display, children[i], name);
         }
         
         if ( nativeWindow )
         {
            break;
         }
      }
   }
   
   return nativeWindow;
}

void *getNativeWindow( AppCtx *appCtx )
{
   void *nativeWindow= 0;
   Display *display= 0;
   
   display= XOpenDisplay( NULL );
   if ( display )
   {
      Window root= DefaultRootWindow(display);
      
      nativeWindow= getNativeWindowFromName( display, root, appCtx->title );
      
      XCloseDisplay( display );
   }
   
   return nativeWindow;
}

#endif

AppCtx* initApp()
{
   AppCtx *appCtx= 0;
   
   appCtx= (AppCtx*)calloc( 1, sizeof(AppCtx) );
   if ( appCtx )
   {
      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      appCtx->inputCtx= (InputCtx*)calloc( 1, sizeof(InputCtx) );
      if ( !appCtx->inputCtx )
      {
         free( appCtx );
         appCtx= 0;
         goto exit;
      }

      appCtx->inputCtx->started= false;
      appCtx->inputCtx->stopRequested= false;
      
      #else

      int argc= 0;
      char **argv= 0;
      
      glutInit(&argc,argv);
      glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA );
      glutInitWindowPosition(0,0);
      glutInitWindowSize(1280, 720);
      
      sprintf( appCtx->title, "Westeros-%d", getpid() );
      appCtx->glutWindowId= glutCreateWindow(appCtx->title);
      
      g_appCtxMap.insert( std::pair<int,AppCtx*>( appCtx->glutWindowId, appCtx ) );
      
      glutSetOption(GLUT_RENDERING_CONTEXT, GLUT_USE_CURRENT_CONTEXT);
      
      glewInit();
            
      glutWMCloseFunc( onGlutClose );
      glutDisplayFunc( onGlutDisplay );
      glutReshapeFunc( onGlutReshape );
      glutMotionFunc( onGlutMotion );
      glutPassiveMotionFunc( onGlutMotion );
      glutMouseFunc(onGlutMouse);
      glutKeyboardFunc( onGlutKeyboard );
      glutSpecialFunc( onGlutSpecial );
      
      glClearColor( 0, 0, 0, 1 );
      
      appCtx->nativeWindow= getNativeWindow( appCtx );
      printf("nativeWindow= %p\n", appCtx->nativeWindow );
      #endif
   }

exit:   
   return appCtx;
}

bool startApp( AppCtx *appCtx, WstCompositor *wctx )
{
   bool result= false;
   
   if ( appCtx )
   {
      appCtx->wctx= wctx;
      
      #if !defined (WESTEROS_PLATFORM_EMBEDDED)
      WstCompositorSetNativeWindow( appCtx->wctx, appCtx->nativeWindow );
      #endif
      
      if ( !WstCompositorGetIsNested( appCtx->wctx ) )
      {
         #if defined (WESTEROS_PLATFORM_EMBEDDED)
         InputCtx *inputCtx= appCtx->inputCtx;
         
         getDevices( inputCtx->deviceFds );
         if ( inputCtx->deviceFds.size() > 0 )
         {
            inputCtx->wctx= wctx;
            int rc= pthread_create( &appCtx->inputThreadId, NULL, inputThread, inputCtx );
            if ( rc )
            {
               printf("unable to start input thread: error %d\n", rc );
            }
         }
         #else
         WstCompositorPointerEnter( appCtx->wctx );
         #endif
      }
      
      result= true;
   }
   
   return result;
}

void termApp( AppCtx *appCtx )
{
   if ( appCtx )
   {
      if ( !WstCompositorGetIsNested( appCtx->wctx ) )
      {
         #if defined (WESTEROS_PLATFORM_EMBEDDED)
         if ( appCtx->inputCtx )
         {
            if ( appCtx->inputCtx->started )
            {
               appCtx->inputCtx->stopRequested= true;
               pthread_join( appCtx->inputThreadId, NULL );
            }
            releaseDevices( appCtx->inputCtx->deviceFds );
            
            free( appCtx->inputCtx );
            appCtx->inputCtx= 0;
         }
         #else
         if ( appCtx->glutWindowId )
         {
            glutDestroyWindow( appCtx->glutWindowId );
         }
         #endif
      }
      
      free( appCtx );
   }
}

static void keyboardHandleKeyMap( void *userData, uint32_t format, int fd, uint32_t size )
{
   printf("keyboardHandleKeyMap: format %d fd %d size %d\n", format, fd, size );
}

static void keyboardHandleEnter( void *userData, struct wl_array *keys )
{
   printf("keyboardHandleEnter: keys %p\n", keys );
}

static void keyboardHandleLeave( void *userData )
{
   printf("keyboardHandleLeave\n" );
}

static void keyboardHandleKey( void *userData, uint32_t time, uint32_t key, uint32_t state )
{
   printf("keyboardHandleKey: time %u key %u state %u\n", time, key, state );
}

static void keyboardHandleModifiers( void *userData, uint32_t mods_depressed, uint32_t mods_latched, 
                                     uint32_t mods_locked, uint32_t group )
{
   printf("keyboardHandleModifiers: depressed %x latched %x locked %x group %u\n", 
          mods_depressed, mods_latched, mods_locked, group );
}

static void keyboardHandleRepeatInfo( void *userData, int32_t rate, int32_t delay )
{
   printf("keyboardHandleRepeatInfo: rate %d delay %d\n", rate, delay );
}

WstKeyboardNestedListener keyboardListener = {
   keyboardHandleKeyMap,
   keyboardHandleEnter,
   keyboardHandleLeave,
   keyboardHandleKey,
   keyboardHandleModifiers,
   keyboardHandleRepeatInfo
};

static void pointerHandleEnter( void *userData, wl_fixed_t sx, wl_fixed_t sy )
{
   printf("pointerHandleEnter: sx %x sy %x\n", sx, sy );
}

static void pointerHandleLeave( void *userData )
{
   printf("pointerHandleLeave\n");
}

static void pointerHandleMotion( void *userData, uint32_t time, wl_fixed_t sx, wl_fixed_t sy )
{
   printf("pointerHandleMotion: time %u sx %x sy %x\n", time, sx, sy );
}

static void pointerHandleButton( void *userData, uint32_t time, uint32_t button, uint32_t state )
{
   printf("pointerHandleButton: time %u button %u state %u\n", time, button, state );
}

static void pointerHandleAxis( void *userData, uint32_t time, uint32_t axis, wl_fixed_t value )
{
   printf("pointerHandleAxis: time %u axis %u value %x\n", time, axis, value );
}

WstPointerNestedListener pointerListener = {
   pointerHandleEnter,
   pointerHandleLeave,
   pointerHandleMotion,
   pointerHandleButton,
   pointerHandleAxis
};

void compositorTerminated( WstCompositor *wctx, void *userData )
{
   g_running= false;
}

void compositorInvalidate( WstCompositor *wctx, void *userData )
{
   #if !defined (WESTEROS_PLATFORM_EMBEDDED)
   glutSwapBuffers();
   #endif
}

void compositorDispatch( WstCompositor *wctx, void *userData )
{
   AppCtx *appCtx= (AppCtx*)userData;
   
   #if !defined (WESTEROS_PLATFORM_EMBEDDED)
   glutMainLoopEvent();
   #endif
}

int main( int argc, char** argv)
{
   int nRC= 0;
	struct sigaction sigint;
   const char *rendererModule= 0;
   const char *displayName= 0;
   const char *nestedDisplayName= 0;
   bool error= false;
   int len, value, width=-1, height=-1;
   AppCtx *appCtx= 0;
   WstCompositor *wctx;

   appCtx= initApp();
   if ( !appCtx )
   {
      printf("unable to initialize app infrastructure\n");
      nRC= -1;
      goto exit;
   }

   wctx= WstCompositorCreate();
   if ( !wctx )
   {
      printf("unable to create compositor instance\n");
      nRC= -1;
      goto exit;
   }
   
   if ( !WstCompositorSetTerminatedCallback( wctx, compositorTerminated, NULL ) )
   {
      error= true;
      goto exit;
   }
   
   if ( !WstCompositorSetInvalidateCallback( wctx, compositorInvalidate, NULL ) )
   {
      error= true;
      goto exit;
   }
   
   if ( !WstCompositorSetDispatchCallback( wctx, compositorDispatch, appCtx ) )
   {
      error= true;
      goto exit;
   }

   for( int i= 1; i < argc; ++i )
   {
      len= strlen(argv[i]);
      if ( (len == 10) && !strncmp( (const char*)argv[i], "--renderer", len) )
      {
         if ( i < argc-1 )
         {
            ++i;
            rendererModule= argv[i];
            
            if ( !WstCompositorSetRendererModule( wctx, rendererModule) )
            {
               error= true;
               break;
            }
         }
      }
      else
      if ( (len == 11) && !strncmp( (const char*)argv[i], "--framerate", len) )
      {
         if ( i < argc-1 )
         {
            int frameRate;
            
            ++i;
            frameRate= atoi(argv[i]);
            if ( frameRate > 0 )
            {
               if ( !WstCompositorSetFrameRate( wctx, frameRate ) )
               {
                  error= true;
                  break;
               }
            }
         }
      }
      else
      if ( (len == 9) && !strncmp( (const char*)argv[i], "--display", len) )
      {
         if ( i < argc-1)
         {
            ++i;
            displayName= argv[i];

            if ( !WstCompositorSetDisplayName( wctx, displayName) )
            {
               error= true;
               break;
            }
         }
      }
      else
      if ( (len == 10) && !strncmp( (const char*)argv[i], "--repeater", len) )
      {
         if ( !WstCompositorSetIsRepeater( wctx, true) )
         {
            error= true;
            break;
         }
      }
      else
      if ( (len == 8) && !strncmp( (const char*)argv[i], "--nested", len) )
      {
         if ( !WstCompositorSetIsNested( wctx, true) )
         {
            error= true;
            break;
         }
      }
      else
      if ( (len == 15) && !strncmp( (const char*)argv[i], "--nestedDisplay", len) )
      {
         if ( i < argc-1)
         {
            ++i;
            nestedDisplayName= argv[i];

            if ( !WstCompositorSetNestedDisplayName( wctx, nestedDisplayName) )
            {
               error= true;
               break;
            }
         }
      }
      else
      if ( (len == 13) && !strncmp( (const char*)argv[i], "--nestedInput", len) )
      {
         if ( !WstCompositorSetKeyboardNestedListener( wctx, &keyboardListener, NULL) )
         {
            error= true;
            break;
         }
         if ( !WstCompositorSetPointerNestedListener( wctx, &pointerListener, NULL) )
         {
            error= true;
            break;
         }
      }
      else
      if ( (len == 7) && !strncmp( argv[i], "--width", len) )
      {
         if ( i+1 < argc )
         {
            ++i;
            value= atoi(argv[i]);
            if ( value > 0 )
            {
               width= value;
            }
         }
      }
      else
      if ( (len == 8) && !strncmp( argv[i], "--height", len) )
      {
         if ( i+1 < argc )
         {
            ++i;
            value= atoi(argv[i]);
            if ( value > 0 )
            {
               height= value;
            }
         }
      }
      else
      if ( (len == 2) && !strncmp( (const char*)argv[i], "-?", len) )
      {
         showUsage();
         goto exit;
      }
   }
   
   if ( !error )
   {
      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      if ( !WstCompositorSetAllowCursorModification( wctx, true ) )
      {
         error= true;
         goto exit;
      }
      #endif
      
      if ( (width > 0) && (height > 0) )
      {
         if ( !WstCompositorSetNestedSize( wctx, width, height) )
         {
            error= true;
            goto exit;
         }
      }

      if ( !rendererModule )
      {
         printf("missing renderer module: use --renderer <module>\n");
         nRC= -1;
         goto exit;
      }
      
      if ( !error )
      {
         if ( !startApp( appCtx, wctx ) )
         {
            printf("error starting application infrastructure, continuing but expect trouble\n" );
         }
      
         g_running= true;
         if ( !(error= !WstCompositorStart( wctx )) )
         {
	         sigint.sa_handler = signalHandler;
	         sigemptyset(&sigint.sa_mask);
	         sigint.sa_flags = SA_RESETHAND;
	         sigaction(SIGINT, &sigint, NULL);

            while( g_running )
            {
               usleep( 10000 );
            }
            
            WstCompositorStop( wctx );
         }
      }
   }
      
exit:

   if ( appCtx )
   {
      termApp( appCtx );
   }
   
   if ( wctx )
   {
      if ( error )
      {
         const char *detail= WstCompositorGetLastErrorDetail( wctx );
         printf("Compositor error: (%s)\n", detail );
      }
      
      WstCompositorDestroy( wctx );
   }

   return nRC;   
}

