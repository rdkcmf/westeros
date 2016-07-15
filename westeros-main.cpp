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
#include <stddef.h>
#include <ctype.h>
#include <memory.h>
#include <assert.h>
#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <linux/input.h>

#if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#if !defined (WESTEROS_PLATFORM_EMBEDDED)
#include <GL/glew.h>
#include <GL/glut.h>
#include <GL/freeglut.h>
#include <X11/Xlib.h>
#elif defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h> 
#endif

#if defined (WESTEROS_PLATFORM_EMBEDDED)
  #include "westeros-gl.h"
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
   bool isEmbedded;
   float matrix[16];
   float alpha;
   int x, y, width, height;
   std::vector<WstRect> rects;
   int hints;
   int tickCount;
   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   EGLDisplay eglDisplay;
   EGLConfig eglConfig;
   EGLContext eglContext;   
   EGLSurface eglSurface;
   GLuint fboId;
   GLuint fboTextureId;
   GLuint fboProgram;
   GLuint fboFrag;
   GLuint fboVert;
   GLint fboPosLoc;
   GLint fboUVLoc;
   GLint fboResLoc;
   GLint fboMatrixLoc;
   GLint fboAlphaLoc;
   GLint fboTextureLoc;
   bool enableAnimation;
   bool animationRunning;
   float scale;
   float startScale;
   float targetScale;
   int transX;
   int startTransX;
   int targetTransX;
   int transY;
   int startTransY;
   int targetTransY;
   long long animationStartTime;
   long long animationDuration;
   #endif
   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   bool showCursor;
   bool cursorReady;
   pthread_t inputThreadId;
   InputCtx *inputCtx;
   WstGLCtx *glCtx;
   #else
   char title[32];
   int glutWindowId;
   #endif
   void *nativeWindow;
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
   printf("  --embedded : operate as an embedded compositor\n" );
   printf("  --repeater : operate as a repeating nested compositor\n" );
   printf("  --nested : operate as a nested compositor\n" );
   printf("  --nestedDisplay <name> : name of wayland display to connect to for nested composition\n" );
   printf("  --nestedInput : register nested input listeners\n" ); 
   printf("  --width <width> : width of nested composition surface\n" );
   printf("  --height <width> : height of nested composition surface\n" );
   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   printf("  --animate : enable animation (use with --embedded)\n" );
   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   printf("  --enableCursor : display default pointer cursor\n" );
   #endif
   #endif
   printf("  -? : show usage\n" );
   printf("\n" );
}

#if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)

#define RED_SIZE (8)
#define GREEN_SIZE (8)
#define BLUE_SIZE (8)
#define ALPHA_SIZE (8)
#define DEPTH_SIZE (0)

static void setupEGL( AppCtx *appCtx )
{
   EGLBoolean b;
   EGLint major, minor;
   EGLint configCount;
   EGLConfig *eglConfigs= 0;
   EGLint attr[32];
   EGLint redSize, greenSize, blueSize, alphaSize, depthSize;
   EGLint ctxAttrib[3];
   int i;

   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   appCtx->glCtx= WstGLInit();
   if ( !appCtx->glCtx )
   {
      printf("Unable to create GL context\n");
      goto exit;
   }
   #endif

   appCtx->eglDisplay= eglGetDisplay( EGL_DEFAULT_DISPLAY );
   if ( appCtx->eglDisplay == EGL_NO_DISPLAY )
   {
      printf("Unable to open default EGL display\n");
      goto exit;
   }
   
   b= eglInitialize( appCtx->eglDisplay, &major, &minor );
   if ( !b )
   {
      printf("Unable to initialize EGL display\n");
      goto exit;
   }
   printf("Initialized EGL display: major %d minor: %d\n", major, minor );

   b= eglGetConfigs( appCtx->eglDisplay, NULL, 0, &configCount );
   if ( !b )
   {
      printf("Unable to get count of EGL configurations: %X\n", eglGetError() );
      goto exit;
   }
   printf("Number of EGL configurations: %d\n", configCount );
    
   eglConfigs= (EGLConfig*)malloc( configCount*sizeof(EGLConfig) );
   if ( !eglConfigs )
   {
      printf("Unable to alloc memory for EGL configurations\n");
      goto exit;
   }

   i= 0;
   attr[i++]= EGL_RED_SIZE;
   attr[i++]= RED_SIZE;
   attr[i++]= EGL_GREEN_SIZE;
   attr[i++]= GREEN_SIZE;
   attr[i++]= EGL_BLUE_SIZE;
   attr[i++]= BLUE_SIZE;
   attr[i++]= EGL_DEPTH_SIZE;
   attr[i++]= DEPTH_SIZE;
   attr[i++]= EGL_STENCIL_SIZE;
   attr[i++]= 0;
   attr[i++]= EGL_SURFACE_TYPE;
   attr[i++]= EGL_WINDOW_BIT;
   attr[i++]= EGL_RENDERABLE_TYPE;
   attr[i++]= EGL_OPENGL_ES2_BIT;
   attr[i++]= EGL_NONE;
    
   // Get a list of configurations that meet or exceed our requirements
   b= eglChooseConfig( appCtx->eglDisplay, attr, eglConfigs, configCount, &configCount );
   if ( !b )
   {
      printf("eglChooseConfig failed: %X\n", eglGetError() );
      goto exit;
   }
   printf("eglChooseConfig: matching configurations: %d\n", configCount );

   // Choose a suitable configuration
   for( i= 0; i < configCount; ++i )
   {
      eglGetConfigAttrib( appCtx->eglDisplay, eglConfigs[i], EGL_RED_SIZE, &redSize );
      eglGetConfigAttrib( appCtx->eglDisplay, eglConfigs[i], EGL_GREEN_SIZE, &greenSize );
      eglGetConfigAttrib( appCtx->eglDisplay, eglConfigs[i], EGL_BLUE_SIZE, &blueSize );
      eglGetConfigAttrib( appCtx->eglDisplay, eglConfigs[i], EGL_ALPHA_SIZE, &alphaSize );
      eglGetConfigAttrib( appCtx->eglDisplay, eglConfigs[i], EGL_DEPTH_SIZE, &depthSize );

      printf("config %d: red: %d green: %d blue: %d alpha: %d depth: %d\n",
              i, redSize, greenSize, blueSize, alphaSize, depthSize );
      if ( (redSize == RED_SIZE) &&
           (greenSize == GREEN_SIZE) &&
           (blueSize == BLUE_SIZE) &&
           (alphaSize == ALPHA_SIZE) &&
           (depthSize == DEPTH_SIZE) )
      {
         printf( "choosing config %d\n", i);
         break;
      }
   }
   if ( i == configCount )
   {
      printf("No suitable configuration available\n");
      goto exit;
   }
   appCtx->eglConfig= eglConfigs[i];

   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   appCtx->nativeWindow= WstGLCreateNativeWindow( appCtx->glCtx, appCtx->x, appCtx->y, appCtx->width, appCtx->height );
   #endif
   printf("nativeWindow %p\n", appCtx->nativeWindow );
   if ( !appCtx->nativeWindow )
   {
      goto exit;
   }

   // Create an EGL window surface
   appCtx->eglSurface= eglCreateWindowSurface( appCtx->eglDisplay, 
                                               appCtx->eglConfig, 
                                               (EGLNativeWindowType)appCtx->nativeWindow,
                                               NULL );
   printf("eglSurface %p\n", appCtx->eglSurface );
   if ( !appCtx->eglSurface )
   {
      goto exit;
   }

   ctxAttrib[0]= EGL_CONTEXT_CLIENT_VERSION;
   ctxAttrib[1]= 2; // ES2
   ctxAttrib[2]= EGL_NONE;

   // Create an EGL context
   appCtx->eglContext= eglCreateContext( appCtx->eglDisplay, appCtx->eglConfig, EGL_NO_CONTEXT, ctxAttrib );
   if ( appCtx->eglContext == EGL_NO_CONTEXT )
   {
      printf( "Unable to create EGL context: %X\n", eglGetError() );
      goto exit;
   }
   printf("eglContext %p\n", appCtx->eglContext );

   eglMakeCurrent( appCtx->eglDisplay, appCtx->eglSurface, appCtx->eglSurface, appCtx->eglContext );
   
   eglSwapInterval( appCtx->eglDisplay, 1 );
   
exit:
   
   return;
}

static void termEGL( AppCtx *appCtx )
{
   if ( appCtx->eglSurface )
   {
      eglDestroySurface( appCtx->eglDisplay, appCtx->eglSurface );
      appCtx->eglSurface= 0;
   }
   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   if ( appCtx->nativeWindow )
   {
      WstGLDestroyNativeWindow( appCtx->glCtx, appCtx->nativeWindow );
      appCtx->nativeWindow= 0;
   }
   if ( appCtx->glCtx )
   {
      WstGLTerm( appCtx->glCtx );
      appCtx->glCtx= 0;
   }
   #endif
}

static const char *fragShaderText =
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "uniform sampler2D s_texture;\n"
  "uniform float u_alpha;\n"
  "varying vec2 v_uv;\n"
  "void main()\n"
  "{\n"
  "  gl_FragColor = texture2D(s_texture, v_uv) * u_alpha;\n"
  "}\n";

static const char *vertexShaderText =
  "uniform vec2 u_resolution;\n"
  "uniform mat4 amymatrix;\n"
  "attribute vec2 pos;\n"
  "attribute vec2 uv;\n"
  "varying vec2 v_uv;\n"
  "void main()\n"
  "{\n"
  "  vec4 p = amymatrix * vec4(pos, 0, 1);\n"
  "  vec4 zeroToOne = p / vec4(u_resolution, u_resolution.x, 1);\n"
  "  vec4 zeroToTwo = zeroToOne * vec4(2.0, 2.0, 1, 1);\n"
  "  vec4 clipSpace = zeroToTwo - vec4(1.0, 1.0, 0, 0);\n"
  "  clipSpace.w = 1.0+clipSpace.z;\n"
  "  gl_Position =  clipSpace * vec4(1, -1, 1, 1);\n"
  "  v_uv = uv;\n"
  "}\n";

static GLuint createShader(AppCtx *appCtx, GLenum shaderType, const char *shaderSource )
{
   GLuint shader= 0;
   GLint shaderStatus;
   GLsizei length;
   char logText[1000];
   
   shader= glCreateShader( shaderType );
   if ( shader )
   {
      glShaderSource( shader, 1, (const char **)&shaderSource, NULL );
      glCompileShader( shader );
      glGetShaderiv( shader, GL_COMPILE_STATUS, &shaderStatus );
      if ( !shaderStatus )
      {
         glGetShaderInfoLog( shader, sizeof(logText), &length, logText );
         printf("Error compiling %s shader: %*s\n",
                ((shaderType == GL_VERTEX_SHADER) ? "vertex" : "fragment"),
                length,
                logText );
      }
   }
   
   return shader;
}

static void createFBO( AppCtx *appCtx )
{
   GLenum statusFBO;
	GLuint frag, vert;
	GLuint program;
	GLint statusShader;

   glGenFramebuffers( 1, &appCtx->fboId );
   glGenTextures( 1, &appCtx->fboTextureId );
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, appCtx->fboTextureId);
   glTexImage2D( GL_TEXTURE_2D,
                 0, //level
                 GL_RGBA, //internalFormat
                 appCtx->width,
                 appCtx->height,
                 0, // border
                 GL_RGBA, //format
                 GL_UNSIGNED_BYTE,
                 NULL );
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindFramebuffer( GL_FRAMEBUFFER, appCtx->fboId );
   glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, appCtx->fboTextureId, 0 );
   statusFBO= glCheckFramebufferStatus( GL_FRAMEBUFFER );
   if ( statusFBO != GL_FRAMEBUFFER_COMPLETE )
   {
      printf("Error: bad fbo status: %d\n", statusFBO );
   }
   glBindFramebuffer( GL_FRAMEBUFFER, 0 );   


	frag= createShader(appCtx, GL_FRAGMENT_SHADER, fragShaderText);
	vert= createShader(appCtx, GL_VERTEX_SHADER, vertexShaderText);

	program= glCreateProgram();
	glAttachShader(program, frag);
	glAttachShader(program, vert);

   appCtx->fboPosLoc= 0;
   appCtx->fboUVLoc= 1;
   glBindAttribLocation(program, appCtx->fboPosLoc, "pos");
   glBindAttribLocation(program, appCtx->fboUVLoc, "uv");
   
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &statusShader);
	if (!statusShader) 
	{
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
	}

   appCtx->fboResLoc= glGetUniformLocation(program,"u_resolution");
   appCtx->fboMatrixLoc= glGetUniformLocation(program,"amymatrix");
   appCtx->fboAlphaLoc= glGetUniformLocation(program,"u_alpha");
   appCtx->fboTextureLoc= glGetUniformLocation(program,"s_texture");
   
   appCtx->fboProgram= program;
   appCtx->fboVert= vert;
   appCtx->fboFrag= frag;
}

static void destroyFBO( AppCtx *appCtx )
{
   if ( appCtx->fboVert )
   {
      glDeleteShader( appCtx->fboVert );
      appCtx->fboVert= 0;
   }
   if ( appCtx->fboFrag )
   {
      glDeleteShader( appCtx->fboFrag );
      appCtx->fboFrag= 0;
   }
   if ( appCtx->fboProgram )
   {
      glDeleteProgram( appCtx->fboProgram );
      appCtx->fboProgram= 0;
   }
   if ( appCtx->fboId )
   {
      glBindFramebuffer( GL_FRAMEBUFFER, appCtx->fboId );
      glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
      glBindFramebuffer( GL_FRAMEBUFFER, 0 );
      if ( appCtx->fboTextureId )
      {
         glDeleteTextures( 1, &appCtx->fboTextureId );
         appCtx->fboTextureId= 0;
      }
      glDeleteFramebuffers( 1, &appCtx->fboId );
      appCtx->fboId= 0;
   }
}

static void drawFBO ( AppCtx *appCtx )
{
   int x, y, w, h;
   
   x= appCtx->x;
   y= appCtx->y;
   w= appCtx->width;
   h= appCtx->height;
      
   const float verts[4][2] = 
   {
      { x, y },
      { x+w, y },
      { x,  y+h },
      { x+w, y+h }
   };
 
   const float uv[4][2] = 
   {
      { 0,  1 },
      { 1,  1 },
      { 0,  0 },
      { 1,  0 }
   };
   
   glUseProgram(appCtx->fboProgram);
   glUniform2f(appCtx->fboResLoc, appCtx->width, appCtx->height);
   glUniformMatrix4fv(appCtx->fboMatrixLoc, 1, GL_FALSE, (GLfloat*)appCtx->matrix);
   glUniform1f(appCtx->fboAlphaLoc, 1.0f);

   glActiveTexture(GL_TEXTURE0); 
   glBindTexture(GL_TEXTURE_2D, appCtx->fboTextureId);
   glUniform1i(appCtx->fboTextureLoc, 0);
   glVertexAttribPointer(appCtx->fboPosLoc, 2, GL_FLOAT, GL_FALSE, 0, verts);
   glVertexAttribPointer(appCtx->fboUVLoc, 2, GL_FLOAT, GL_FALSE, 0, uv);
   glEnableVertexAttribArray(appCtx->fboPosLoc);
   glEnableVertexAttribArray(appCtx->fboUVLoc);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   glDisableVertexAttribArray(appCtx->fboPosLoc);
   glDisableVertexAttribArray(appCtx->fboUVLoc);

   glFlush();
   glFinish();
}

static bool isRotated( AppCtx *appCtx )
{
   float *f= appCtx->matrix;
   const float e= 1.0e-2;
   
   if ( (fabsf(f[1]) > e) ||
        (fabsf(f[2]) > e) ||
        (fabsf(f[4]) > e) ||
        (fabsf(f[6]) > e) ||
        (fabsf(f[8]) > e) ||
        (fabsf(f[9]) > e) )
   {
      return true;
   }
   
   return false;
}

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}
#endif

#if defined (WESTEROS_PLATFORM_EMBEDDED)
static const char *inputPath= "/dev/input/";

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
   return fd;
}

char *getDevice( const char *path, char *devName )
{
   int len;
   char *devicePathName= 0;
   struct stat buffer;
   
   if ( !devName )
      return devicePathName; 
      
   len= strlen( devName );
   
   devicePathName= (char *)malloc( strlen(path)+len+1);
   if ( devicePathName )
   {
      strcpy( devicePathName, path );
      strcat( devicePathName, devName );     
   }
   
   if ( !stat(devicePathName, &buffer) )
   {
      printf( "found %s\n", devicePathName );           
   }
   else
   {
      free( devicePathName );
      devicePathName= 0;
   }
   
   return devicePathName;
}

void getDevices( std::vector<pollfd> &deviceFds )
{
   DIR * dir;
   struct dirent *result;
   char *devPathName;
   if ( NULL != (dir = opendir( inputPath )) )
   {
      while( NULL != (result = readdir( dir )) )
      {
         if ( (result->d_type != DT_DIR) &&
             !strncmp(result->d_name, "event", 5) )
         {
            devPathName= getDevice( inputPath, result->d_name );
            if ( devPathName )
            {
               if (openDevice( deviceFds, devPathName ) >= 0 )
                  free( devPathName );
               else
                  printf("Could not open device %s\n", devPathName);
            }
         }
      }

      closedir( dir );
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
   int deviceCount;
   int i, n;
   input_event e;
   unsigned int keyModifiers= 0;
   int mouseAccel= 1;
   int mouseX= 0;
   int mouseY= 0;
   unsigned int outputWidth= 0, outputHeight= 0;
   bool mouseEnterSent= false;
   bool mouseMoved= false;
   int notifyFd= -1, watchFd=-1;
   char intfyEvent[512];
   pollfd pfd;
   
   inCtx->started= true;

   notifyFd= inotify_init();
   if ( notifyFd >= 0 )
   {
      pfd.fd= notifyFd;
      watchFd= inotify_add_watch( notifyFd, inputPath, IN_CREATE | IN_DELETE );
      inCtx->deviceFds.push_back( pfd );
   }

   deviceCount= inCtx->deviceFds.size();
      
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
               if ( inCtx->deviceFds[i].fd == notifyFd )
               {
                  // A hotplug event has occurred
                  n= read( notifyFd, &intfyEvent, sizeof(intfyEvent) );
                  if ( n >= sizeof(struct inotify_event) )
                  {
                     struct inotify_event *iev= (struct inotify_event*)intfyEvent;
                     if ( (iev->len >= 5) && !strncmp( iev->name, "event", 5 ) )
                     {
                        // Re-discover devices                        
                        printf("inotify: mask %x (%s) wd %d (%d)\n", iev->mask, iev->name, iev->wd, watchFd );
                        inCtx->deviceFds.pop_back();
                        releaseDevices( inCtx->deviceFds );
                        usleep( 10000 );
                        getDevices( inCtx->deviceFds );
                        inCtx->deviceFds.push_back( pfd );
                        deviceCount= inCtx->deviceFds.size();
                     }
                  }
               }
               else
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
   }
   
   if ( notifyFd )
   {
      if ( watchFd )
      {
         inotify_rm_watch( notifyFd, watchFd );
      }
      inCtx->deviceFds.pop_back();
      close( notifyFd );
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
         case GLUT_KEY_F1: keyCode= KEY_F1; break;
         case GLUT_KEY_F2: keyCode= KEY_F2; break;
         case GLUT_KEY_F3: keyCode= KEY_F3; break;
         case GLUT_KEY_F4: keyCode= KEY_F4; break;
         case GLUT_KEY_F5: keyCode= KEY_F5; break;
         case GLUT_KEY_F6: keyCode= KEY_F6; break;
         case GLUT_KEY_F7: keyCode= KEY_F7; break;
         case GLUT_KEY_F8: keyCode= KEY_F8; break;
         case GLUT_KEY_F9: keyCode= KEY_F9; break;
         case GLUT_KEY_F10: keyCode= KEY_F10; break;
         case GLUT_KEY_F11: keyCode= KEY_F11; break;
         case GLUT_KEY_F12: keyCode= KEY_F2; break;
         case GLUT_KEY_LEFT: keyCode= KEY_LEFT; break;
         case GLUT_KEY_UP: keyCode= KEY_UP; break;
         case GLUT_KEY_RIGHT: keyCode= KEY_RIGHT; break;
         case GLUT_KEY_DOWN: keyCode= KEY_DOWN; break;
         case GLUT_KEY_PAGE_UP: keyCode= KEY_PAGEUP; break;
         case GLUT_KEY_PAGE_DOWN: keyCode= KEY_PAGEDOWN; break;
         case GLUT_KEY_HOME: keyCode= KEY_HOME; break;
         case GLUT_KEY_END: keyCode= KEY_END; break;
         case GLUT_KEY_INSERT: keyCode= KEY_INSERT; break;
         default: break;
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
      
      if ( appCtx->isEmbedded )
      {
         EGLDisplay eglDisplay;
         EGLint major, minor;
         EGLBoolean b;
         eglDisplay= eglGetDisplay( EGL_DEFAULT_DISPLAY );
         if ( eglDisplay != EGL_NO_DISPLAY )
         {
            b= eglInitialize( eglDisplay, &major, &minor );
            if ( b )
            {
               printf("eglInitiialize: major: %d minor: %d\n", major, minor );
            }
            else
            {
               printf("unable to initialize EGL display\n" );
            }
         }
         else
         {
            printf("unable to open default EGL display\n");
         }

         appCtx->matrix[0]= 1.0f;
         appCtx->matrix[5]= 1.0f;
         appCtx->matrix[10]= 1.0f;
         appCtx->matrix[15]= 1.0f;
         appCtx->x= 0;
         appCtx->y= 0;
         appCtx->width= 1280;
         appCtx->height= 720;
         
         appCtx->alpha= 1.0f;
         appCtx->hints= WstHints_noRotation;
         appCtx->tickCount= 0;
         appCtx->animationRunning= false;
         appCtx->scale= 1.0;
         appCtx->targetScale= 1.0;
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

#if defined (WESTEROS_PLATFORM_EMBEDDED)
static void drawLine( unsigned char *data, 
                      int width, int height,
                      int x1, int y1,
                      int x2, int y2,
                      unsigned int color,
                      int thickness )
{
   unsigned int *p;
   unsigned int alpha;
   int x, y, ht;
   unsigned int c;
   
   // Draw vertical, horizontal, or 45 degree lines
   x= x1;
   y= y1;
   for( ; ; )
   {
      p= (unsigned int*)(data + (y*width+x)*4);
      for ( int i= 0; i < thickness; ++i )
      {        
         if ( (i == 0) || (i == thickness-1) )
            alpha= 0x7F;
         else
            alpha= 0xFF;
         
         c= ((color&0xFFFFFF)|(alpha<<24));
         *p= c;
         
         if ( y1 != y2 )
            p += 1;
         else
            p += width;
      }
      
      if ( (x == x2) && (y == y2) )
         break;
      
      if ( x1 == x2 )
         if (  y1 <= y2 )
            y= y+1;
         else
            y= y-1;
      else if ( y1 == y2 )
         if ( x1 <= x2 )
            x= x+1;
         else
            x= x-1;
      else
      {
         if (  y1 <= y2 )
            y= y+1;
         else
            y= y-1;
         if ( x1 <= x2 )
            x= x+1;
         else
            x= x-1;
      }
   }
}

static void fillShape( unsigned char *data, int width, int height, unsigned int color, int thickness )
{
   unsigned int *p, *f;
   bool inside;
   unsigned char prev;
   int xl, xr, t;
   
   for( int y= 0; y < height; y++ )
   {
      prev= 0;
      inside= false;
      xl= xr= -1;
      t= 0;
      p= (unsigned int *)(data+y*width*4);
      for( int x= 0; x < width; x++ )
      {
         if ( xl == -1 )
         {
            if ( *p )
            {
               ++t;
            }
            if ( (prev != 0) && (*p == 0 ) )
            {
               if ( !inside && (t == thickness) )
               {
                  inside= !inside;
               }
               if ( inside )
                  xl= x;
               t= 0;
            }
         }
         else
         {
            if ( *p != 0 )
            {
               if ( inside && (xl >= 0) && (xr == -1) )
               {
                  xr= x;
                  f= (unsigned int *)(data+(y*width+xl)*4);
                  for( int n= xl; n < xr; ++n )
                  {
                     *f= color;
                     ++f;
                  }
               }
               ++t;
               if ( t == thickness )
               {
                  t= 0;
                  xl= xr= -1;
                  if ( p[1] == 0 )
                    inside= !inside;
               }
            }
         }
         
         prev= *p;
         ++p;
      }
   }
}

static bool initCursor( AppCtx *appCtx )
{
   bool result= false;
   unsigned char *data= 0;
   int width, height;
   int allocSize;
   unsigned int edgeColor= 0xFFE6DF11;
   unsigned int fillColor= 0x70000000;
   
   // Create a default cursor image
   width= 64;
   height= 64;
   allocSize= width*4*height;
   
   data= (unsigned char *)calloc( 1, allocSize );
   if ( !data )
   {
      printf("Unable to allocate memory for default pointer cursor - cursor disabled\n");
      goto exit;
   }

   drawLine( data, width, height, 0, 0, 0, 43, edgeColor, 3 );   
   drawLine( data, width, height, 0, 0, 43, 0, edgeColor, 3 );      
   drawLine( data, width, height, 1, 43, 16, 28, edgeColor, 3 );
   drawLine( data, width, height, 43, 0, 28, 15, edgeColor, 3 );
   drawLine( data, width, height, 28, 16, 55, 43, edgeColor, 3 );
   drawLine( data, width, height, 17, 28, 44, 55, edgeColor, 3 );
   drawLine( data, width, height, 45, 54, 55, 44, edgeColor, 3 );
   
   fillShape( data, width, height, fillColor, 3 );
   
   drawLine( data, width, height, 0, 44, 2, 44, edgeColor, 1);
   drawLine( data, width, height, 0, 45, 1, 45, edgeColor, 1);

   if ( !WstCompositorSetDefaultCursor( appCtx->wctx, data, width, height, 0, 0 ) )
   {
      const char *detail= WstCompositorGetLastErrorDetail( appCtx->wctx );
      printf("Unable to set default cursor: error: (%s)\n", detail );
      goto exit;
   }
   
   appCtx->cursorReady= true;
   
   result= true;
   
exit:

   if ( data )
   {
      free( data );
   }
   
   return result;
}
#endif

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
   AppCtx *appCtx= (AppCtx*)userData;

   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   if ( appCtx->isEmbedded )
   {
      destroyFBO( appCtx );
      termEGL( appCtx );
   }
   #endif

   g_running= false;
}

void compositorInvalidate( WstCompositor *wctx, void *userData )
{
   AppCtx *appCtx= (AppCtx*)userData;

   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   if ( appCtx->isEmbedded )
   {
      bool needHolePunch= false;

      if ( appCtx->eglDisplay == EGL_NO_DISPLAY )
      {
         setupEGL( appCtx );
         createFBO( appCtx );
      }

      eglMakeCurrent( appCtx->eglDisplay, 
                      appCtx->eglSurface, 
                      appCtx->eglSurface, 
                      appCtx->eglContext );

      // Fill with opaque color to show that hole punch is working
      glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
      glClear( GL_COLOR_BUFFER_BIT );

      glBindFramebuffer( GL_FRAMEBUFFER, appCtx->fboId );

      GLfloat priorColor[4];
      glGetFloatv( GL_COLOR_CLEAR_VALUE, priorColor );
      glBlendFuncSeparate( GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE );
      glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
      glClear( GL_COLOR_BUFFER_BIT );
      glEnable(GL_BLEND);

      appCtx->rects.clear();
      WstCompositorComposeEmbedded( wctx, 
                                    appCtx->x,
                                    appCtx->y,
                                    appCtx->width,
                                    appCtx->height,
                                    appCtx->matrix,
                                    appCtx->alpha,
                                    appCtx->hints,
                                    &needHolePunch,
                                    appCtx->rects );

      glBindFramebuffer( GL_FRAMEBUFFER, 0 );

      if ( needHolePunch )
      {
         GLint priorBox[4];
         GLint viewport[4];
         bool wasEnabled= glIsEnabled(GL_SCISSOR_TEST);
         glGetIntegerv( GL_SCISSOR_BOX, priorBox );
         glGetIntegerv( GL_VIEWPORT, viewport );

         glEnable( GL_SCISSOR_TEST );
         glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
         for( unsigned int i= 0; i < appCtx->rects.size(); ++i )
         {
            WstRect r= appCtx->rects[i];
            if ( r.width && r.height )
            {
               glScissor( r.x, viewport[3]-(r.y+r.height), r.width, r.height );
               glClear( GL_COLOR_BUFFER_BIT );
            }
         }
        
         if ( wasEnabled )
         {
            glScissor( priorBox[0], priorBox[1], priorBox[2], priorBox[3] );
         }
         else
         {
            glDisable( GL_SCISSOR_TEST );
         }
      }

      drawFBO( appCtx );

      glClearColor( priorColor[0], priorColor[1], priorColor[2], priorColor[3] );
      
      eglSwapBuffers(appCtx->eglDisplay, appCtx->eglSurface);
   }   
   #endif

   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   if  ( appCtx->showCursor && !appCtx->cursorReady )
   {
      appCtx->showCursor= initCursor( appCtx );
   }
   #endif
   
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

   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   if ( appCtx->isEmbedded )
   {
      int hints;
      int tick;
      
      if ( appCtx->enableAnimation )
      {
         if ( !appCtx->animationRunning )
         {
            tick= ++appCtx->tickCount % 600;
            if ( tick < 599 )
            {
               hints= WstHints_noRotation;
            }
            else
            {
               appCtx->animationRunning= true;
               appCtx->animationStartTime= getCurrentTimeMillis();
               appCtx->animationDuration= 2000;
               appCtx->targetScale= (appCtx->targetScale == 1.0 ? 0.5 : 1.0);
               appCtx->startScale= appCtx->scale;
               appCtx->targetTransX= (appCtx->targetTransX == 0 ? 620 : 0);
               appCtx->startTransX= appCtx->transX;
               appCtx->targetTransY= (appCtx->targetTransY == 0 ? 340 : 0);
               appCtx->startTransY= appCtx->transY;
               hints= WstHints_none;
            }
         }
         else
         {
            long long now= getCurrentTimeMillis();
            long long timePos= now - appCtx->animationStartTime;
            hints= WstHints_none;
            if ( timePos >= appCtx->animationDuration )
            {            
               appCtx->animationRunning= false;
               appCtx->scale= appCtx->targetScale;
               appCtx->transX= appCtx->targetTransX;
               appCtx->transY= appCtx->targetTransY;
               appCtx->matrix[0]= appCtx->scale;
               appCtx->matrix[1]= 0.0f;
               appCtx->matrix[4]= 0.0f;
               appCtx->matrix[5]= appCtx->scale;
               appCtx->matrix[10]= appCtx->scale;
               appCtx->matrix[12]= appCtx->transX;
               appCtx->matrix[13]= appCtx->transY;
               hints= WstHints_noRotation;
               appCtx->tickCount= 0;
            }
            else
            {
               float sina, cosa;
               float pos= (float)timePos/(float)appCtx->animationDuration;
               float angle= pos*360.0*M_PI/180.0;
               sincosf(angle, &sina, &cosa);
               float cx= appCtx->width/2;
               float cy= appCtx->height/2;
               appCtx->scale= appCtx->startScale + pos*(appCtx->targetScale-appCtx->startScale);
               appCtx->transX= appCtx->startTransX + pos*(appCtx->targetTransX-appCtx->startTransX);
               appCtx->transY= appCtx->startTransY + pos*(appCtx->targetTransY-appCtx->startTransY);
               cx += appCtx->transX;
               cy += appCtx->transY;
               appCtx->matrix[0]= cosa*appCtx->scale;
               appCtx->matrix[1]= sina*appCtx->scale;
               appCtx->matrix[4]= -sina*appCtx->scale;
               appCtx->matrix[5]= cosa*appCtx->scale;
               appCtx->matrix[10]= appCtx->scale;
               appCtx->matrix[12]= cx-appCtx->scale*cx*cosa+appCtx->scale*cy*sina;
               appCtx->matrix[13]= cy-appCtx->scale*cx*sina-appCtx->scale*cy*cosa;
            }
         }

         if ( hints != appCtx->hints )
         {
            appCtx->hints= hints;
            WstCompositorInvalidateScene(appCtx->wctx);
         }
      }
   }
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
   
   if ( !WstCompositorSetTerminatedCallback( wctx, compositorTerminated, appCtx ) )
   {
      error= true;
      goto exit;
   }
   
   if ( !WstCompositorSetInvalidateCallback( wctx, compositorInvalidate, appCtx ) )
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
      if ( (len == 10) && !strncmp( (const char*)argv[i], "--embedded", len) )
      {
         if ( !WstCompositorSetIsEmbedded( wctx, true) )
         {
            error= true;
            break;
         }
         appCtx->isEmbedded= true;
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
      #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
      else
      if ( (len == 9) && !strncmp( argv[i], "--animate", len) )
      {
         appCtx->enableAnimation= true;
      }
      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      else
      if ( (len == 14) && !strncmp( argv[i], "--enableCursor", len) )
      {
         appCtx->showCursor= true;      
      }
      #endif
      #endif
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
      if  ( !appCtx->showCursor )
      {
         if ( !WstCompositorSetAllowCursorModification( wctx, true ) )
         {
            error= true;
            goto exit;
         }
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

