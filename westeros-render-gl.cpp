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
#include <assert.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
  #include <GLES2/gl2.h>
  #include <GLES2/gl2ext.h>
#else  
  #include <GL/glew.h>
  #include <GL/gl.h>
#endif

#if defined (WESTEROS_PLATFORM_EMBEDDED)
  #include "westeros-gl.h"
#endif

#if defined (WESTEROS_PLATFORM_RPI)
  #include <bcm_host.h>
#endif

#include "westeros-render.h"
#include "wayland-server.h"
#include "wayland-client.h"
#include "wayland-egl.h"

#ifdef ENABLE_SBPROTOCOL
#include "westeros-simplebuffer.h"
#endif

#include <vector>

//#define WST_DEBUG

#ifdef WST_DEBUG
#define INT_TRACE(FORMAT,...) printf( FORMAT "\n", __VA_ARGS__)
#else
#define INT_TRACE(FORMAT,...) 
#endif

#define WST_TRACE(...)  INT_TRACE(__VA_ARGS__, "")

#define WST_UNUSED( n ) ((void)n)

#define ZNEAR        (0.0f)
#define ZFAR         (1000000.0f)
#define ZSTART       (0.1f)
#define ZINC         (0.1f)
#define MAX_LOG_SIZE (1024)

#define TCM11 (0)
#define TCM12 (1)
#define TCM21 (2)
#define TCM22 (3)
#define TCMDX (4)
#define TCMDY (5)

#define TCM_MAPX( x, y, t ) ((x)*(t)[TCM11]+(y)*(t)[TCM21]+(t)[TCMDX])
#define TCM_MAPY( x, y, t ) ((x)*(t)[TCM12]+(y)*(t)[TCM22]+(t)[TCMDY])

#define DEFAULT_SURFACE_WIDTH (0)
#define DEFAULT_SURFACE_HEIGHT (0)


typedef enum _WstShaderType
{
   WstShaderType_texture,
   WstShaderType_textureYUV,
   WstShaderType_fill,
   WstShaderType_alphaText,
} WstShaderType;

typedef struct _WstShaderClassBits
{
   unsigned int type:4;
   unsigned int opacity:1;
   unsigned int unused:3;
   int maskCount:24;
} WstShaderClassBits;

typedef union _WstShaderClass
{
   WstShaderClassBits bits;
   int id;
} WstShaderClass;

#define MAX_TEXTURES (2)

#define WST_MAX_MASK 16
typedef struct _WstShader
{
   WstShaderClass shaderClass;
   GLuint programId;
   GLuint vertexShaderId;
   GLuint fragmentShaderId;
   GLuint mvpLocation;
   GLuint vertexLocation;
   GLuint textureLocation[MAX_TEXTURES];
   GLuint fillColorLocation;
   GLuint opacityLocation;
   GLuint textureUnit0;
   GLuint textureUnit1;
   GLuint alphaTextColorLocation;
   GLuint maskAttribLocation[WST_MAX_MASK];   
   GLuint maskUniformLocation[WST_MAX_MASK];   
} WstShader;

typedef struct _WstRenderContext
{
    float opacity;
    float z;
    const float *transform;
    int cpuClip;
    GLint clip[4];
} WstRenderContext;

struct _WstRenderSurface
{
   void *nativePixmap;
   int textureCount;
   GLuint textureId[MAX_TEXTURES];
   EGLImageKHR eglImage[MAX_TEXTURES];

   int bufferWidth;
   int bufferHeight;

   unsigned char *mem;
   bool memDirty;
   int memWidth;
   int memHeight;
   GLint memFormatGL;
   GLenum memType;

   int x;
   int y;
   int width;
   int height;

   bool visible;
   float opacity;
   float zorder;

   int onScreen;

   int vertexLocation;
   int textureLocation[MAX_TEXTURES];

   float transform[6];

   int clip[4];

   int vertexCoordsDirty;
   int texCoordsDirty;
   bool invertedY;

   GLfloat vertexCoords[4*2];
   GLfloat textureCoords[4*2];
     
};

typedef struct _WstRendererGL
{
   WstRenderer *renderer;
   int outputWidth;
   int outputHeight;
   int noScreenClip;
   GLfloat projection[16];
   GLfloat modelview[16];
   GLfloat mvp[16];
   WstShader nullShader;
   int currentShaderClass;
   WstShader *currentShader;
   int shaderCacheSize;
   GLint maxTextureUnits;
   GLuint *activeTextureId;
   WstShader **shaderCache;
   WstRenderContext renderCtx;

   void *nativeWindow;

   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   WstGLCtx *glCtx;
   #endif
   
   EGLDisplay eglDisplay;
   EGLConfig eglConfig;
   EGLContext eglContext;   
   EGLSurface eglSurface;

   PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
   PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
   #endif

   #if defined (WESTEROS_HAVE_WAYLAND_EGL)
   bool haveWaylandEGL;
   PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
   PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
   PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
   #endif
   
   std::vector<WstRenderSurface*> surfaces;
} WstRendererGL;

static WstRendererGL* wstRendererGLCreate( int width, int height );
static void wstRendererGLDestroy( WstRendererGL *renderer );
static WstRenderSurface *wstRenderGLCreateSurface(WstRendererGL *renderer);
static void wstRenderGLDestroySurface( WstRendererGL *renderer, WstRenderSurface *surface );
static void wstRendererGLFlushSurface( WstRendererGL *renderer, WstRenderSurface *surface );
static void wstRendererGLCommitShm( WstRendererGL *rendererGL, WstRenderSurface *surface, struct wl_resource *resource );
#if defined (WESTEROS_HAVE_WAYLAND_EGL)
static void wstRendererGLCommitWaylandEGL( WstRendererGL *rendererGL, WstRenderSurface *surface, 
                                           struct wl_resource *resource, EGLint format );
#endif
#ifdef ENABLE_SBPROTOCOL
static void wstRendererGLCommitSB( WstRendererGL *rendererGL, WstRenderSurface *surface, struct wl_resource *resource );
#endif
#if defined (WESTEROS_PLATFORM_RPI)
static void wstRendererGLCommitDispmanx( WstRendererGL *rendererGL, WstRenderSurface *surface, 
                                         DISPMANX_RESOURCE_HANDLE_T dispResource,
                                         EGLint format, int bufferWidth, int bufferHeight );
#endif                                         
static void wstRenderGLPrepareSurface( WstRendererGL *renderer, WstRenderContext *rctx, WstRenderSurface *surf );
static void wstRenderGLRenderSurface( WstRendererGL *renderer, WstRenderContext *rctx, WstRenderSurface *surf );

static bool wstRenderGLSetupEGL( WstRendererGL *renderer );
static void wstMatrixIdentity(float matrix[16]);
static void wstMatrixMultMatrix(float matrix[16], float in1[16], float in2[16]);
static void wstMatrixTranslate(float matrix[16], float tx, float ty, float tz);
static void wstMatrixOrtho(float matrix[16], float left, float right, float bottom, float top, float near, float far);
static void wstSetCurrentShader( WstRendererGL *renderer, int shaderClassId );
static WstShader* wstGetShaderForClass( WstRendererGL *renderer, int shaderClassId );
static WstShader* wstCreateShaderForClass( WstShaderClass shaderClass );
static void wstDestroyShader( WstRendererGL *renderer, WstShader *shader );
static unsigned int wstCreateGLShader(const char *pShaderSource, bool isVertexShader );

static WstRendererGL* wstRendererGLCreate( WstRenderer *renderer )
{
   WstRendererGL *rendererGL= 0;
   
   rendererGL= (WstRendererGL*)calloc(1, sizeof(WstRendererGL) );
   if ( rendererGL )
   {
      rendererGL->outputWidth= renderer->outputWidth;
      rendererGL->outputHeight= renderer->outputHeight;
      
      rendererGL->currentShaderClass= -1;

      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      rendererGL->glCtx= WstGLInit();
      #endif
      
      rendererGL->renderer= renderer;
      wstRenderGLSetupEGL( rendererGL );

      rendererGL->eglCreateImageKHR= (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
      printf( "eglCreateImageKHR %p\n", rendererGL->eglCreateImageKHR);

      rendererGL->eglDestroyImageKHR= (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
      printf( "eglDestroyImageKHR %p\n", rendererGL->eglDestroyImageKHR);

      #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
      rendererGL->glEGLImageTargetTexture2DOES= (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
      printf( "glEGLImageTargetTexture2DOES %p\n", rendererGL->glEGLImageTargetTexture2DOES);
      #endif
   
      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      const char *extensions= eglQueryString( rendererGL->eglDisplay, EGL_EXTENSIONS );
      if ( extensions )
      {
         if ( !strstr( extensions, "EGL_WL_bind_wayland_display" ) )
         {
            printf("wayland-egl support expected, but not advertised by eglQueryString(eglDisplay,EGL_EXTENSIONS): not attempting to use\n" );
         }
         else
         {
            printf("wayland-egl support expected, and is advertised by eglQueryString(eglDisplay,EGL_EXTENSIONS): proceeding to use \n" );
         
            rendererGL->eglBindWaylandDisplayWL= (PFNEGLBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglBindWaylandDisplayWL");
            printf( "eglBindWaylandDisplayWL %p\n", rendererGL->eglBindWaylandDisplayWL );

            rendererGL->eglUnbindWaylandDisplayWL= (PFNEGLUNBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglUnbindWaylandDisplayWL");
            printf( "eglUnbindWaylandDisplayWL %p\n", rendererGL->eglUnbindWaylandDisplayWL );

            rendererGL->eglQueryWaylandBufferWL= (PFNEGLQUERYWAYLANDBUFFERWL)eglGetProcAddress("eglQueryWaylandBufferWL");
            printf( "eglQueryWaylandBufferWL %p\n", rendererGL->eglQueryWaylandBufferWL );
            
            if ( rendererGL->eglBindWaylandDisplayWL &&
                 rendererGL->eglUnbindWaylandDisplayWL &&
                 rendererGL->eglQueryWaylandBufferWL )
            {
               printf("calling eglBindWaylandDisplayWL with eglDisplay %p and wayland display %p\n", rendererGL->eglDisplay, renderer->display );
               EGLBoolean rc= rendererGL->eglBindWaylandDisplayWL( rendererGL->eglDisplay, renderer->display );
               if ( rc )
               {
                  rendererGL->haveWaylandEGL= true;
               }
               else
               {
                  printf("eglBindWaylandDisplayWL failed: %x\n", eglGetError() );
               }
            }
            else
            {
               printf("wayland-egl support expected, and advertised, but methods are missing: no wayland-egl\n" );
            }
         }
      }
      printf("have wayland-egl: %d\n", rendererGL->haveWaylandEGL );
      #endif
    
      // Get the number of texture units
      glGetIntegerv( GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &rendererGL->maxTextureUnits );
      WST_TRACE( "WstRendererGL: WstRendererGLCreate: maxTextureUnits=%d\n", rendererGL->maxTextureUnits );

      rendererGL->shaderCacheSize= 4 *                // texture + textureYUV + fill + alphaText
                                   2 *                // opacity on/off                        
                                   rendererGL->maxTextureUnits;
      rendererGL->shaderCache= (WstShader**)calloc( 1, rendererGL->shaderCacheSize*sizeof(WstShader*) );
      WST_TRACE( "WstRendererGL: WstRendererGLCreate: allocate cache for %d shader classes\n", rendererGL->shaderCacheSize );
    
      // Initialize null shader
      rendererGL->nullShader.shaderClass.id= -1;
      rendererGL->nullShader.programId= GL_NONE;
      rendererGL->nullShader.vertexShaderId= GL_NONE;
      rendererGL->nullShader.fragmentShaderId= GL_NONE;
      rendererGL->nullShader.mvpLocation= -1;
      rendererGL->nullShader.vertexLocation= -1;
      rendererGL->nullShader.textureLocation[0]= -1;
      rendererGL->nullShader.textureLocation[1]= -1;
      rendererGL->nullShader.fillColorLocation= -1;
      rendererGL->nullShader.opacityLocation= -1;
      for( int i= 0; i < WST_MAX_MASK; ++i )
      {
         rendererGL->nullShader.maskAttribLocation[i]= -1;
         rendererGL->nullShader.maskUniformLocation[i]= -1;
      }

      rendererGL->activeTextureId= (GLuint*)malloc( rendererGL->maxTextureUnits*sizeof(GLuint) );
      for( int i= 0; i < rendererGL->maxTextureUnits; ++i )
      {
         rendererGL->activeTextureId[i]= GL_NONE;
      }

      rendererGL->currentShader= &rendererGL->nullShader;

      wstMatrixIdentity(rendererGL->modelview);
      wstMatrixTranslate(rendererGL->modelview, -rendererGL->outputWidth, -rendererGL->outputHeight, 0);
      wstMatrixIdentity(rendererGL->projection);
      wstMatrixOrtho( rendererGL->projection, 
                      0,                          //left
                      rendererGL->outputWidth,    //right
                      rendererGL->outputHeight,   //bottom
                      0,                          //top
                      ZNEAR,                      //near
                      ZFAR );                     //far

      wstMatrixMultMatrix(rendererGL->mvp, rendererGL->projection, rendererGL->modelview);
   }
   
   return rendererGL;
}

static void wstRendererGLDestroy( WstRendererGL *renderer )
{
   if ( renderer )
   {
      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      if ( renderer->haveWaylandEGL )
      {
         renderer->eglUnbindWaylandDisplayWL( renderer->eglDisplay, renderer->renderer->display );
         renderer->haveWaylandEGL= false;
      }
      #endif
      
      if ( renderer->shaderCache )
      {
         WstShader *shader;

         for( int i= 0; i < renderer->shaderCacheSize; ++i )
         {
            shader= renderer->shaderCache[i];
            if ( shader )
            {
               wstDestroyShader( renderer, shader );
               renderer->shaderCache[i]= 0;
            }
       
         }
         free( renderer->shaderCache );
         renderer->shaderCache= 0;
      }
      if (renderer->activeTextureId)
      {
         free(renderer->activeTextureId);
         renderer->activeTextureId = 0;
      }
      
      if ( renderer->eglSurface )
      {
         eglDestroySurface( renderer->eglDisplay, renderer->eglSurface );
         renderer->eglSurface= 0;
      }
      
      // If we are doing nested composition destroy the wayland egl window
      // otherwise destroy the native egl window
      if ( renderer->renderer->displayNested )
      {
         if ( renderer->nativeWindow )
         {
            wl_egl_window_destroy( (struct wl_egl_window *)renderer->nativeWindow );
            renderer->nativeWindow= 0;
         }
      }
      else
      {
         #if defined (WESTEROS_PLATFORM_EMBEDDED)
         if ( renderer->nativeWindow )
         {
            WstGLDestroyNativeWindow( renderer->glCtx, renderer->nativeWindow );
            renderer->nativeWindow= 0;
         }
         
         WstGLTerm( renderer->glCtx );
         renderer->glCtx= 0;
         #endif
      }

      free( renderer );
   }
}

static WstRenderSurface *wstRenderGLCreateSurface(WstRendererGL *renderer)
{
    WstRenderSurface *surface= 0;

    WST_UNUSED(renderer);   
    surface= (WstRenderSurface*)calloc( 1, sizeof(WstRenderSurface) );
    if ( surface )
    {
        surface->textureCount= 1;
        surface->textureId[0]= GL_NONE;

        surface->width= DEFAULT_SURFACE_WIDTH;
        surface->height= DEFAULT_SURFACE_HEIGHT;
        surface->x= 0;
        surface->y= 0;
        surface->visible= true;
        surface->opacity= 1.0;
        surface->zorder= 0.5;
        
        wstMatrixIdentity(surface->transform);
      
        surface->vertexCoordsDirty= 1;
        surface->texCoordsDirty= 1;
    }
   
    return surface;
}

static void wstRenderGLDestroySurface( WstRendererGL *renderer, WstRenderSurface *surface )
{
    WST_UNUSED(renderer);    
    if ( surface )
    {
        wstRendererGLFlushSurface( renderer, surface );
        free( surface );
    }
}

static void wstRendererGLFlushSurface( WstRendererGL *renderer, WstRenderSurface *surface )
{
    if ( surface )
    {
        for( int i= 0; i < MAX_TEXTURES; ++i )
        {
           if ( surface->textureId[i] )
           {
              glDeleteTextures( 1, &surface->textureId[i] );
              surface->textureId[i]= GL_NONE;
           }
           if ( surface->eglImage[i] )
           {
               renderer->eglDestroyImageKHR( renderer->eglDisplay,
                                             surface->eglImage[i] );
               surface->eglImage[i]= 0;
           }
        }
        #if defined (WESTEROS_PLATFORM_EMBEDDED)
        if ( surface->nativePixmap )
        {
           WstGLReleaseNativePixmap( renderer->glCtx, surface->nativePixmap );
           surface->nativePixmap= 0;
        }
        #endif
        if ( surface->mem )
        {
           free( surface->mem );
        }
    }
}

static void wstRendererGLCommitShm( WstRendererGL *rendererGL, WstRenderSurface *surface, struct wl_resource *resource )
{
   struct wl_shm_buffer *shmBuffer;
   int width, height, stride;
   GLint formatGL;
   GLenum type;
   bool transformPixelsA= false;
   bool transformPixelsB= false;
   bool fillAlpha= false;
   void *data;

   shmBuffer= wl_shm_buffer_get( resource );
   if ( shmBuffer )
   {
      width= wl_shm_buffer_get_width(shmBuffer);
      height= wl_shm_buffer_get_height(shmBuffer);
      stride= wl_shm_buffer_get_stride(shmBuffer);
      
      // The SHM formats describe the structure of the color channels for a pixel as
      // they would appear in a machine register not the byte order in memory.  For 
      // example WL_SHM_FORMAT_ARGB8888 is a 32 bit pixel with alpha in the 8 most significant
      // bits and blue in the 8 list significant bits.  On a little endian machine the
      // byte order in memory would be B, G, R, A.
      switch( wl_shm_buffer_get_format(shmBuffer) )
      {
         case WL_SHM_FORMAT_ARGB8888:
            #ifdef BIG_ENDIAN_CPU
               formatGL= GL_RGBA;
               transformPixelsA= true;
            #else
               #if defined (WESTEROS_HAVE_WAYLAND_EGL)
               if ( rendererGL->haveWaylandEGL )
               {
                  formatGL= GL_BGRA_EXT;
               }
               else
               {
                  formatGL= GL_RGBA;
                  transformPixelsB= true;
               }
               #elif defined (WESTEROS_PLATFORM_EMBEDDED)
               formatGL= GL_BGRA_EXT;
               #else
               formatGL= GL_RGBA;
               transformPixelsB= true;
               #endif
            #endif
            type= GL_UNSIGNED_BYTE;
            break;
         case WL_SHM_FORMAT_XRGB8888:
            #ifdef BIG_ENDIAN_CPU
               formatGL= GL_RGBA;
               transformPixelsA= true;
            #else
               #if defined (WESTEROS_HAVE_WAYLAND_EGL)
               if ( rendererGL->haveWaylandEGL )
               {
                  formatGL= GL_BGRA_EXT;
               }
               else
               {
                  formatGL= GL_RGBA;
                  transformPixelsB= true;
               }
               #elif defined (WESTEROS_PLATFORM_EMBEDDED)
               formatGL= GL_BGRA_EXT;
               #else
               formatGL= GL_RGBA;
               transformPixelsB= true;
               #endif
            #endif
            type= GL_UNSIGNED_BYTE;
            fillAlpha= true;
            break;
         case WL_SHM_FORMAT_BGRA8888:
            #ifdef BIG_ENDIAN_CPU
               #if defined (WESTEROS_HAVE_WAYLAND_EGL)
               if ( rendererGL->haveWaylandEGL )
               {
                  formatGL= GL_BGRA_EXT;
               }
               else
               {
                  formatGL= GL_RGBA;
                  transformPixelsB= true;
               }
               #elif defined (WESTEROS_PLATFORM_EMBEDDED)
               formatGL= GL_BGRA_EXT;
               #else
               formatGL= GL_RGBA;
               transformPixelsB= true;
               #endif
            #else
               formatGL= GL_RGBA;
               transformPixelsA= true;
            #endif
            type= GL_UNSIGNED_BYTE;
            break;
         case WL_SHM_FORMAT_BGRX8888:
            #ifdef BIG_ENDIAN_CPU
               #if defined (WESTEROS_HAVE_WAYLAND_EGL)
               if ( rendererGL->haveWaylandEGL )
               {
                  formatGL= GL_BGRA_EXT;
               }
               else
               {
                  formatGL= GL_RGBA;
                  transformPixelsB= true;
               }
               #elif defined (WESTEROS_PLATFORM_EMBEDDED)
               formatGL= GL_BGRA_EXT;
               #else
               formatGL= GL_RGBA;
               transformPixelsB= true;
               #endif
            #else
               formatGL= GL_RGBA;
               transformPixelsA= true;
            #endif
            type= GL_UNSIGNED_BYTE;
            fillAlpha= true;
            break;
         case WL_SHM_FORMAT_RGB565:
            formatGL= GL_RGB;
            type= GL_UNSIGNED_SHORT_5_6_5;
            break;
         case WL_SHM_FORMAT_ARGB4444:
            formatGL= GL_RGBA;
            type= GL_UNSIGNED_SHORT_4_4_4_4;
            break;
         default:
            formatGL= GL_NONE;
            break;
      }

      if ( formatGL != GL_NONE )
      {
         wl_shm_buffer_begin_access(shmBuffer);
         data= wl_shm_buffer_get_data(shmBuffer);
         
         if ( surface->mem &&
              (
                (surface->memWidth != width) ||
                (surface->memHeight != height) ||
                (surface->memFormatGL != formatGL) ||
                (surface->memType != type)
              )
            )
         {
            free( surface->mem );
            surface->mem= 0;
            surface->vertexCoordsDirty= 1;
         }
         if ( !surface->mem )
         {
            surface->mem= (unsigned char*)malloc( stride*height );
         }
         if ( surface->mem )
         {
            memcpy( surface->mem, data, stride*height );
            
            if ( transformPixelsA )
            {
               // transform ARGB to RGBA
               unsigned int pixel, alpha;
               unsigned int *pixdata= (unsigned int*)surface->mem;
               for( int y= 0; y < height; ++y )
               {
                  for( int x= 0; x < width; ++x )
                  {
                     pixel= pixdata[y*width+x];
                     alpha= (fillAlpha ? 0xFF : (pixel>>24));
                     pixel= (pixel<<8)|alpha;
                     pixdata[y*width+x]= pixel;
                  }
               }
            }
            else if ( transformPixelsB )
            {
               // transform BGRA to RGBA
               unsigned char *pixdata= (unsigned char*)surface->mem;
               for( int y= 0; y < height; ++y )
               {
                  for( int x= 0; x < width; ++x )
                  {
                     if ( fillAlpha )
                     {
                        pixdata[y*width*4 + x*4 +3]= 0xFF;
                     }
                     unsigned char temp= pixdata[y*width*4 + x*4 +2];
                     pixdata[y*width*4 + x*4 +2]= pixdata[y*width*4 + x*4 +0];
                     pixdata[y*width*4 + x*4 +0]= temp;
                  }
               }
            }
            else if ( fillAlpha )
            {
               if ( fillAlpha )
               {
                  unsigned char *pixdata= (unsigned char*)surface->mem;
                  for( int y= 0; y < height; ++y )
                  {
                     for( int x= 0; x < width; ++x )
                     {
                        pixdata[y*width*4 + x*4 +3]= 0xFF;
                     }
                  }
               }
            }
            
            surface->bufferWidth= width;
            surface->bufferHeight= height;
            surface->memWidth= width;
            surface->memHeight= height;
            surface->memFormatGL= formatGL;
            surface->memType= type;
            surface->memDirty= true;
         }      
         
         wl_shm_buffer_end_access(shmBuffer);
      }
   }
}

#if defined (WESTEROS_HAVE_WAYLAND_EGL)
static void wstRendererGLCommitWaylandEGL( WstRendererGL *rendererGL, WstRenderSurface *surface, 
                                           struct wl_resource *resource, EGLint format )
{
   EGLImageKHR eglImage= 0;
   EGLint value;
   EGLint attrList[3];
   int bufferWidth= 0, bufferHeight= 0;

   if (EGL_TRUE == rendererGL->eglQueryWaylandBufferWL( rendererGL->eglDisplay,
                                                        resource,
                                                        EGL_WIDTH,
                                                        &value ) )
   {
      bufferWidth= value;
   }                                                        

   if (EGL_TRUE == rendererGL->eglQueryWaylandBufferWL( rendererGL->eglDisplay,
                                                        resource,
                                                        EGL_HEIGHT,
                                                        &value ) )
   {
      bufferHeight= value;
   }                                                        
   
   if ( (surface->bufferWidth != bufferWidth) || (surface->bufferHeight != bufferHeight) )
   {
      surface->bufferWidth= bufferWidth;
      surface->bufferHeight= bufferHeight;
      surface->vertexCoordsDirty= 1;
   }

   for( int i= 0; i < MAX_TEXTURES; ++i )
   {
      if ( surface->eglImage[i] )
      {
         rendererGL->eglDestroyImageKHR( rendererGL->eglDisplay,
                                         surface->eglImage[i] );
         surface->eglImage[i]= 0;
      }
   }

   #if defined (WESTEROS_PLATFORM_RPI)
   /* 
    * The Userland wayland-egl implementation used on RPI isn't complete in that it does not
    * support the use of eglCreateImageKHR using the wl_buffer resource and target EGL_WAYLAND_BUFFER_WL.
    * For that reason we need to supply a different path for handling buffers received via
    * wayland-egl on RPI
    */
   {
      DISPMANX_RESOURCE_HANDLE_T dispResource= vc_dispmanx_get_handle_from_wl_buffer(resource);
      if ( dispResource != DISPMANX_NO_HANDLE )
      {
         wstRendererGLCommitDispmanx( rendererGL, surface, dispResource, format, bufferWidth, bufferHeight );
      }
   }
   #else
   switch ( format )
   {
      case EGL_TEXTURE_RGB:
      case EGL_TEXTURE_RGBA:
         eglImage= rendererGL->eglCreateImageKHR( rendererGL->eglDisplay,
                                                  EGL_NO_CONTEXT,
                                                  EGL_WAYLAND_BUFFER_WL,
                                                  resource,
                                                  NULL // EGLInt attrList[]
                                                 );
         if ( eglImage )
         {
            /*
             * We have a new eglImage.  Mark the surface as having no texture to
             * trigger texture creation during the next scene render
             */
            surface->eglImage[0]= eglImage;
            if ( surface->textureId[0] != GL_NONE )
            {
               glDeleteTextures( 1, &surface->textureId[0] );
            }
            surface->textureId[0]= GL_NONE;
            surface->textureCount= 1;
         }
         break;
      
      case EGL_TEXTURE_Y_U_V_WL:
         printf("wstRendererGLCommitWaylandEGL: EGL_TEXTURE_Y_U_V_WL not supported\n" );
         break;
       
      case EGL_TEXTURE_Y_UV_WL:
         attrList[0]= EGL_WAYLAND_PLANE_WL;
         attrList[2]= EGL_NONE;
         for( int i= 0; i < 2; ++i )
         {
            attrList[1]= i;
            
            eglImage= rendererGL->eglCreateImageKHR( rendererGL->eglDisplay,
                                                     EGL_NO_CONTEXT,
                                                     EGL_WAYLAND_BUFFER_WL,
                                                     resource,
                                                     attrList
                                                    );
            if ( eglImage )
            {
               surface->eglImage[i]= eglImage;
               if ( surface->textureId[i] != GL_NONE )
               {
                  glDeleteTextures( 1, &surface->textureId[i] );
               }
               surface->textureId[i]= GL_NONE;
            }
         }
         surface->textureCount= 2;
         break;
         
      case EGL_TEXTURE_Y_XUXV_WL:
         printf("wstRendererGLCommitWaylandEGL: EGL_TEXTURE_Y_XUXV_WL not supported\n" );
         break;
         
      default:
         printf("wstRendererGLCommitWaylandEGL: unknown texture format: %x\n", format );
         break;
   }
   #endif
   #if WESTEROS_INVERTED_Y
   surface->invertedY= true;
   #endif
}
#endif

#ifdef ENABLE_SBPROTOCOL
static void wstRendererGLCommitSB( WstRendererGL *rendererGL, WstRenderSurface *surface, struct wl_resource *resource )
{
   struct wl_sb_buffer *sbBuffer;
   void *deviceBuffer;
   int bufferWidth, bufferHeight;
   
   #if defined WESTEROS_PLATFORM_RPI
   int sbFormat;
   EGLint format= EGL_NONE;
   
   sbBuffer= WstSBBufferGet( resource );
   if ( sbBuffer )
   {
      deviceBuffer= WstSBBufferGetBuffer( sbBuffer );
      if ( deviceBuffer )
      {
         DISPMANX_RESOURCE_HANDLE_T dispResource= (DISPMANX_RESOURCE_HANDLE_T)deviceBuffer;
         
         bufferWidth= WstSBBufferGetWidth( sbBuffer );
         bufferHeight= WstSBBufferGetHeight( sbBuffer );
         sbFormat= WstSBBufferGetFormat( sbBuffer );
         switch( sbFormat )
         {
            case WL_SB_FORMAT_ARGB8888:
            case WL_SB_FORMAT_XRGB8888:
               format= EGL_TEXTURE_RGBA;
               break;
         }
         
         if ( format != EGL_NONE )
         {
            wstRendererGLCommitDispmanx( rendererGL, surface, dispResource, format, bufferWidth, bufferHeight );
         }
      }
   }
   #else
   EGLNativePixmapType eglPixmap= 0;
   EGLImageKHR eglImage= 0;
   bool resize= false;
   
   sbBuffer= WstSBBufferGet( resource );
   if ( sbBuffer )
   {
      deviceBuffer= WstSBBufferGetBuffer( sbBuffer );
      if ( deviceBuffer )
      {
         if ( surface->nativePixmap )
         {
            eglPixmap = (EGLNativePixmapType) WstGLGetEGLNativePixmap(rendererGL->glCtx, surface->nativePixmap);
         }
         if ( WstGLGetNativePixmap( rendererGL->glCtx, deviceBuffer, &surface->nativePixmap ) )
         {
            WstGLGetNativePixmapDimensions( rendererGL->glCtx, surface->nativePixmap, &bufferWidth, &bufferHeight );
            if ( (surface->bufferWidth != bufferWidth) || (surface->bufferHeight != bufferHeight) )
            {
               surface->bufferWidth= bufferWidth;
               surface->bufferHeight= bufferHeight;
               surface->vertexCoordsDirty= 1;
               resize= true;
            }
            
            if ( resize || (eglPixmap != (EGLNativePixmapType) WstGLGetEGLNativePixmap(rendererGL->glCtx, surface->nativePixmap)) )
            {
               /*
                * If the eglPixmap contained by the surface WstGLNativePixmap changed
                * (because the attached buffer dimensions changed, for example) then we
                * need to create a new texture
                */
               if ( surface->eglImage[0] )
               {
                  rendererGL->eglDestroyImageKHR( rendererGL->eglDisplay,
                                                  surface->eglImage[0] );
                  surface->eglImage[0]= 0;
               }
               eglPixmap = (EGLNativePixmapType) WstGLGetEGLNativePixmap(rendererGL->glCtx, surface->nativePixmap);
            }
            if ( !surface->eglImage[0] )
            {
               eglImage= rendererGL->eglCreateImageKHR( rendererGL->eglDisplay,
                                                        EGL_NO_CONTEXT,
                                                        EGL_NATIVE_PIXMAP_KHR,
                                                        (EGLClientBuffer) eglPixmap,
                                                        NULL // EGLInt attrList[]
                                                       );
               if ( eglImage )
               {
                  /*
                   * We have a new eglImage.  Mark the surface as having no texture to
                   * trigger texture creation during the next scene render
                   */
                  surface->eglImage[0]= eglImage;
                  if ( surface->textureId[0] != GL_NONE )
                  {
                     glDeleteTextures( 1, &surface->textureId[0] );
                  }
                  surface->textureId[0]= GL_NONE;
               }
            }
         }
      }
   }
   #endif
   #if WESTEROS_INVERTED_Y
   surface->invertedY= true;
   #endif
}
#endif

#if defined (WESTEROS_PLATFORM_RPI)
static void wstRendererGLCommitDispmanx( WstRendererGL *rendererGL, WstRenderSurface *surface, 
                                         DISPMANX_RESOURCE_HANDLE_T dispResource,
                                         EGLint format, int bufferWidth, int bufferHeight )
{
   int stride;
   GLint formatGL;
   GLenum type;

   if ( dispResource != DISPMANX_NO_HANDLE )
   {
      switch ( format )
      {
         case EGL_TEXTURE_RGB:
         case EGL_TEXTURE_RGBA:
            {
               stride= 4*bufferWidth;
               formatGL= GL_RGBA;
               type= GL_UNSIGNED_BYTE;

               if ( surface->mem &&
                    (
                      (surface->memWidth != bufferWidth) ||
                      (surface->memHeight != bufferHeight) ||
                      (surface->memFormatGL != formatGL) ||
                      (surface->memType != type)
                    )
                  )
               {
                  free( surface->mem );
                  surface->mem= 0;
                  surface->vertexCoordsDirty= 1;
               }
               if ( !surface->mem )
               {
                  surface->mem= (unsigned char*)malloc( stride*bufferHeight );
               }
               if ( surface->mem )
               {
                  VC_RECT_T rect;
                  int result;
                  
                  rect.x= 0;
                  rect.y= 0;
                  rect.width= bufferWidth;
                  rect.height= bufferHeight;

                  result= vc_dispmanx_resource_read_data( dispResource,
                                                          &rect,
                                                          surface->mem,
                                                          stride );
                  if ( result >= 0 )
                  {
                     surface->bufferWidth= bufferWidth;
                     surface->bufferHeight= bufferHeight;
                     surface->memWidth= bufferWidth;
                     surface->memHeight= bufferHeight;
                     surface->memFormatGL= formatGL;
                     surface->memType= type;
                     surface->memDirty= true;
                  }
               }            
            }
            break;
         
         case EGL_TEXTURE_Y_U_V_WL:
            printf("wstRendererGLCommitDispmanx: EGL_TEXTURE_Y_U_V_WL not supported\n" );
            break;
          
         case EGL_TEXTURE_Y_UV_WL:
            printf("wstRendererGLCommitDispmanx: EGL_TEXTURE_Y_UV_WL not supported\n" );
            break;
            
         case EGL_TEXTURE_Y_XUXV_WL:
            printf("wstRendererGLCommitDispmanx: EGL_TEXTURE_Y_XUXV_WL not supported\n" );
            break;
            
         default:
            printf("wstRendererGLCommitDispmanx: unknown texture format: %x\n", format );
            break;
      }
   }
}
#endif

static void wstRenderGLPrepareSurface( WstRendererGL *renderer, WstRenderContext *rctx, WstRenderSurface *surf )
{
    GLfloat *vertexCoords;
    const float *transform= rctx->transform;
    float *zlevel= &rctx->z;
    float clipLeft, clipRight, clipTop, clipBottom;
    float txmin, txmax, tymin, tymax;
    int vcupdate= 0;
    int tcupdate= 0;
    float z= *zlevel;
    WstShaderClass itemShaderClass;

    if ( surf->visible )
    {
       *zlevel= z + ZINC;

       clipLeft=  rctx->clip[0];
       clipRight= clipLeft+rctx->clip[2];
       clipTop= rctx->clip[1];
       clipBottom= clipTop+rctx->clip[3];

       if ( (surf->clip[0] != rctx->clip[0]) ||
            (surf->clip[1] != rctx->clip[1]) ||
            (surf->clip[2] != rctx->clip[2]) ||
            (surf->clip[3] != rctx->clip[3]) )
       {
          surf->clip[0]= rctx->clip[0];
          surf->clip[1]= rctx->clip[1];
          surf->clip[2]= rctx->clip[2];
          surf->clip[3]= rctx->clip[3];
          
          vcupdate= 1;
          tcupdate= 1;
       }
       
       if ( (surf->textureId[0] == GL_NONE) || surf->memDirty )
       {
           for ( int i= 0; i < surf->textureCount; ++i )
           {
              if ( surf->textureId[i] == GL_NONE )
              {
                 glGenTextures(1, &surf->textureId[i] );
                 WST_TRACE("WstPrepareSurface: surf %p surface textureId=%d\n", surf, surf->textureId[i] );
              }
             
              /* Bind the egl image as a texture */
              glActiveTexture(GL_TEXTURE0+i);
              glBindTexture(GL_TEXTURE_2D, surf->textureId[i] );
              #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
              if ( surf->eglImage[i] )
              {
                 renderer->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, surf->eglImage[i]);
              }
              else
              #endif
              if ( i == 0 )
              {
                 if ( surf->mem )
                 {
                    glTexImage2D( GL_TEXTURE_2D,
                                  0, //level
                                  surf->memFormatGL, //internalFormat
                                  surf->memWidth,
                                  surf->memHeight,
                                  0, // border
                                  surf->memFormatGL, //format
                                  surf->memType,
                                  surf->mem );
                    surf->memDirty= false;
                 }
              }
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
              glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
              glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
              WST_TRACE("WstPrepareSurface: after binding texture: glGetError=%X\n", glGetError() );
           }
       }

       if ( vcupdate || tcupdate || surf->vertexCoordsDirty )
       {
           surf->width= surf->bufferWidth;
           surf->height= surf->bufferHeight;
                      
           itemShaderClass.bits.type= (surf->textureCount == 1) ? WstShaderType_texture : WstShaderType_textureYUV;
           itemShaderClass.bits.opacity= 0;
           itemShaderClass.bits.unused= 0;
           itemShaderClass.bits.maskCount= 0;

           if ( renderer->currentShaderClass != itemShaderClass.id )
           {
               wstSetCurrentShader( renderer, itemShaderClass.id );
           }
           
           surf->vertexLocation= renderer->currentShader->vertexLocation;
           WST_TRACE("surface vertexLocation %d\n", surf->vertexLocation);
       }
       
       vertexCoords= surf->vertexCoords;

       if ( surf->vertexCoordsDirty ) 
       {
           vcupdate= 1;
           tcupdate= 1;
       }

       txmin= tymin= 0.0f;
       txmax= tymax= 1.0f;            

       if ( vcupdate )
       {
           float x1, x2, x3, x4, y1, y2, y3, y4, x5, y5;

           x1= TCM_MAPX(surf->x,surf->y,transform);
           y1= TCM_MAPY(surf->x,surf->y,transform);
           x2= TCM_MAPX(surf->x+surf->width,surf->y,transform);
           y2= TCM_MAPY(surf->x+surf->width,surf->y,transform);
           x3= TCM_MAPX(surf->x+surf->width,surf->y+surf->height,transform);
           y3= TCM_MAPY(surf->x+surf->width,surf->y+surf->height,transform);
           x4= TCM_MAPX(surf->x,surf->y+surf->height,transform);
           y4= TCM_MAPY(surf->x,surf->y+surf->height,transform);

           x5 = TCM_MAPX(surf->x + surf->width / 2,surf->y+surf->height / 2,transform);
           y5 = TCM_MAPY(surf->x + surf->width / 2,surf->y+surf->height / 2,transform);

           bool isOnScreen = false;

           if ( rctx->cpuClip )
           {
               if ( rctx->clip[0] > clipLeft ) clipLeft= rctx->clip[0];
               if ( rctx->clip[1] > clipTop ) clipTop= rctx->clip[1];
               if ( rctx->clip[0]+rctx->clip[2] < clipRight ) clipRight= rctx->clip[0]+rctx->clip[2];
               if ( rctx->clip[1]+rctx->clip[3] < clipBottom ) clipBottom= rctx->clip[1]+rctx->clip[3];
           }

           if (renderer->noScreenClip)
           {
               isOnScreen = true;
           }
           else
           {                                    
               if ((int)x1 == (int)x4 && (int)x2 == (int)x3)//rectangle
               {
                   float left = x1;
                   float right = x3;
                   float top = y1;
                   float bottom = y3;

                   if(right < clipLeft || left > clipRight || bottom < clipTop || top > clipBottom)
                       isOnScreen = false;
                   else 
                       isOnScreen = true;
               }	
               else 
               {
                   isOnScreen = (
                          (((x1 >= clipLeft) && (y1 >= clipTop)) && ((x1 < clipRight) && (y1 < clipBottom))) ||
                          (((x2 >= clipLeft) && (y2 >= clipTop)) && ((x2 < clipRight) && (y2 < clipBottom))) ||
                          (((x3 >= clipLeft) && (y3 >= clipTop)) && ((x3 < clipRight) && (y3 < clipBottom))) ||
                          (((x4 >= clipLeft) && (y4 >= clipTop)) && ((x4 < clipRight) && (y4 < clipBottom))) ||
                          (((x5 >= clipLeft) && (y5 >= clipTop)) && ((x5 < clipRight) && (y5 < clipBottom)))
                          );
               }
           }

           if (isOnScreen)
           {
               surf->onScreen= 1;
                     
               if ( rctx->cpuClip )
               {
                   float ox1= x1;
                   float oy1= y1;
                   float ow= x3-x1;
                   float oh= y3-y1;

                   if ( x1 < clipLeft )
                   {
                       if ( ow ) txmin= (clipLeft-ox1)/ow;
                       x1= clipLeft;
                   }
                   if ( x2 > clipRight ) x2= clipRight;                      
                   if ( x3 > clipRight )
                   {
                       if ( ow ) txmax= (clipRight-ox1)/ow;
                       x3= clipRight;
                   }
                   if ( x4 < clipLeft ) x4= clipLeft;                      
                   if ( y1 < clipTop )
                   {
                       if ( oh ) tymin= (clipTop-oy1)/oh;
                       y1= clipTop;
                   }
                   if ( y2 < clipTop ) y2= clipTop;                      
                   if ( y3 > clipBottom )
                   {
                       if ( oh ) tymax= (clipBottom-oy1)/oh;
                       y3= clipBottom;
                   }
                   if ( y4 > clipBottom ) y4= clipBottom;
               }

               vertexCoords[0]= x1;
               vertexCoords[1]= y1;
               vertexCoords[2]= x2;
               vertexCoords[3]= y2;
               vertexCoords[4]= x3;
               vertexCoords[5]= y3;
               vertexCoords[6]= x4;
               vertexCoords[7]= y4;

               surf->vertexCoordsDirty= 0;
           }
           else
           {
               surf->onScreen= 0;
           }
       }
       
       if ( tcupdate )
       {
           GLfloat *textureCoords;

           for ( int i= 0; i < surf->textureCount; ++i )
           {
              surf->textureLocation[i]= renderer->currentShader->textureLocation[i];
              WST_TRACE("surf textureLocation[%d] %d\n", i, surf->textureLocation[i]);
           }

           textureCoords= surf->textureCoords;

           if ( surf->invertedY )
           {
              tymin= 1.0f-tymin;
              tymax= 1.0f-tymax;
           }
           
           textureCoords[0]= txmin;
           textureCoords[1]= tymin;
           textureCoords[2]= txmax;
           textureCoords[3]= tymin;
           textureCoords[4]= txmax;
           textureCoords[5]= tymax;
           textureCoords[6]= txmin;	
           textureCoords[7]= tymax;
       }
    }
}

static void wstRenderGLRenderSurface( WstRendererGL *renderer, WstRenderContext *rctx, WstRenderSurface *surf )
{
    WstShaderClass itemShaderClass;
    float opacity= rctx->opacity * surf->opacity;
    const float *transform= rctx->transform;
    int enableBlend= 1;

    if ( surf->visible )
    {
       WST_TRACE( "WstRenderSurface: surface %p onScreen %d", surf, surf->onScreen );
       if ( surf->onScreen || renderer->noScreenClip )
       {
           itemShaderClass.bits.type= (surf->textureCount == 1) ? WstShaderType_texture : WstShaderType_textureYUV;
           itemShaderClass.bits.opacity= (opacity < 0.99f) ? 1 : 0;
           itemShaderClass.bits.unused= 0;
           itemShaderClass.bits.maskCount= 0;

           if ( renderer->currentShaderClass != itemShaderClass.id )
           {
              WST_TRACE("WstRenderSurface: set current shader to class %d", itemShaderClass.id);
              wstSetCurrentShader( renderer, itemShaderClass.id );
           }
           if ( itemShaderClass.bits.opacity )
           {
              enableBlend= 1;
              WST_TRACE("WstRenderSurface: setting shader opacity to %f", opacity);
              glUniform1f( renderer->currentShader->opacityLocation, opacity );
           }
           WST_TRACE("WstRenderSurface: vertices: (%f,%f), (%f,%f), (%f,%f), (%f,%f)",
                      surf->vertexCoords[0], surf->vertexCoords[1], 
                      surf->vertexCoords[2], surf->vertexCoords[3], 
                      surf->vertexCoords[4], surf->vertexCoords[5], 
                      surf->vertexCoords[6], surf->vertexCoords[7] );
           glVertexAttribPointer(surf->vertexLocation, 2, GL_FLOAT, GL_FALSE, 0, surf->vertexCoords);
           WST_TRACE("WstRenderSurface: enable vertex attrib array %d", surf->vertexLocation);
           glEnableVertexAttribArray(surf->vertexLocation);
           WST_TRACE("WstRenderSurface: texcoords: (%f,%f), (%f,%f), (%f,%f), (%f,%f)",
                      surf->textureCoords[0], surf->textureCoords[1], 
                      surf->textureCoords[2], surf->textureCoords[3], 
                      surf->textureCoords[4], surf->textureCoords[5], 
                      surf->textureCoords[6], surf->textureCoords[7] );
           glVertexAttribPointer(surf->textureLocation[0], 2, GL_FLOAT, GL_FALSE, 0, surf->textureCoords);
           WST_TRACE("WstRenderSurface: enable texture attrib array %d", surf->textureLocation[0]);
           glEnableVertexAttribArray(surf->textureLocation[0]);

           glActiveTexture(GL_TEXTURE0);
           glUniform1i( renderer->currentShader->textureUnit0, 0 );
           if ( renderer->activeTextureId[0] != surf->textureId[0] )
           {
              renderer->activeTextureId[0]= surf->textureId[0];
              WST_TRACE("WstRenderSurface: calling glBindTexture textureId %d", surf->textureId[0]);
              glBindTexture(GL_TEXTURE_2D, surf->textureId[0]);
           }
           if ( surf->textureCount > 1 )
           {
              glVertexAttribPointer(surf->textureLocation[1], 2, GL_FLOAT, GL_FALSE, 0, surf->textureCoords);
              WST_TRACE("WstRenderSurface: enable texture attrib array %d", surf->textureLocation[1]);
              glEnableVertexAttribArray(surf->textureLocation[1]);

              glActiveTexture(GL_TEXTURE1);
              glUniform1i( renderer->currentShader->textureUnit1, 1 );
              if ( renderer->activeTextureId[1] != surf->textureId[1] )
              {
                 renderer->activeTextureId[1]= surf->textureId[1];
                 WST_TRACE("WstRenderSurface: calling glBindTexture textureId %d", surf->textureId[1]);
                 glBindTexture(GL_TEXTURE_2D, surf->textureId[1]);
              }
           }
           if ( enableBlend )
           {
              glEnable(GL_BLEND);
           }
           WST_TRACE("WstRenderSurface: calling glDrawArrays");
           glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
           if ( enableBlend )
           {
              glDisable(GL_BLEND);
           }
           glBindTexture(GL_TEXTURE_2D, GL_NONE);
           renderer->activeTextureId[0]= GL_NONE;
           renderer->activeTextureId[1]= GL_NONE;
       }
    }
}


#define RED_SIZE (8)
#define GREEN_SIZE (8)
#define BLUE_SIZE (8)
#define ALPHA_SIZE (8)
#define DEPTH_SIZE (0)

static bool wstRenderGLSetupEGL( WstRendererGL *renderer )
{
   bool result= false;
   EGLint major, minor;
   EGLBoolean b;
   EGLint configCount;
   EGLConfig *eglConfigs= 0;
   EGLint attr[32];
   EGLint redSize, greenSize, blueSize, alphaSize, depthSize;
   EGLint ctxAttrib[3];
   int i;
   
   if ( renderer->renderer->displayNested )
   {
      // Get EGL display from wayland display
      renderer->eglDisplay= eglGetDisplay( (EGLNativeDisplayType)renderer->renderer->displayNested );
   }
   else
   {
      // Get default EGL display
      renderer->eglDisplay= eglGetDisplay(EGL_DEFAULT_DISPLAY);
   }
   printf("wstRenderGLSetupEGL: eglDisplay=%p\n", renderer->eglDisplay );
   if ( renderer->eglDisplay == EGL_NO_DISPLAY )
   {
      printf("wstRenderGLSetupEGL: EGL not available\n" );
      goto exit;
   }

   // Initialize display
   b= eglInitialize( renderer->eglDisplay, &major, &minor );
   if ( !b )
   {
      printf("wstRenderGLSetupEGL: unable to initialize EGL display\n" );
      goto exit;
   }
   printf("wstRenderGLSetupEGL: eglInitiialize: major: %d minor: %d\n", major, minor );

   // Get number of available configurations
   b= eglGetConfigs( renderer->eglDisplay, NULL, 0, &configCount );
   if ( !b )
   {
      printf("wstRendererGLSetupEGL: unable to get count of EGL configurations: %X\n", eglGetError() );
      goto exit;
   }
   printf("wstRendererGLSetupEGL: Number of EGL configurations: %d\n", configCount );
    
   eglConfigs= (EGLConfig*)malloc( configCount*sizeof(EGLConfig) );
   if ( !eglConfigs )
   {
      printf("wstRenderGLSetupEGL: unable to alloc memory for EGL configurations\n");
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
   b= eglChooseConfig( renderer->eglDisplay, attr, eglConfigs, configCount, &configCount );
   if ( !b )
   {
      printf("wstRendererGLSetupEGL: eglChooseConfig failed: %X\n", eglGetError() );
      goto exit;
   }
   printf("wstRendererGLSetupEGL: eglChooseConfig: matching configurations: %d\n", configCount );
    
   // Choose a suitable configuration
   for( i= 0; i < configCount; ++i )
   {
      eglGetConfigAttrib( renderer->eglDisplay, eglConfigs[i], EGL_RED_SIZE, &redSize );
      eglGetConfigAttrib( renderer->eglDisplay, eglConfigs[i], EGL_GREEN_SIZE, &greenSize );
      eglGetConfigAttrib( renderer->eglDisplay, eglConfigs[i], EGL_BLUE_SIZE, &blueSize );
      eglGetConfigAttrib( renderer->eglDisplay, eglConfigs[i], EGL_ALPHA_SIZE, &alphaSize );
      eglGetConfigAttrib( renderer->eglDisplay, eglConfigs[i], EGL_DEPTH_SIZE, &depthSize );

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
      printf("wstRendererGLSetupEGL: no suitable configuration available\n");
      goto exit;
   }
   renderer->eglConfig= eglConfigs[i];

   // If we are doing nested composition, create a wayland egl window otherwise
   // create a native egl window.
   if ( renderer->renderer->displayNested )
   {
      renderer->nativeWindow= wl_egl_window_create(renderer->renderer->surfaceNested, renderer->outputWidth, renderer->outputHeight);         
   }
   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   else
   {
      renderer->nativeWindow= WstGLCreateNativeWindow( renderer->glCtx, 0, 0, renderer->outputWidth, renderer->outputHeight );
   }
   #else
   else
   {
      renderer->nativeWindow= renderer->renderer->nativeWindow;
   }
   #endif
   printf("nativeWindow %p\n", renderer->nativeWindow );

   if ( renderer->nativeWindow )
   {
      // Create an EGL window surface
      renderer->eglSurface= eglCreateWindowSurface( renderer->eglDisplay, 
                                                    renderer->eglConfig, 
                                                    (EGLNativeWindowType)renderer->nativeWindow,
                                                    NULL );
      printf("wstRenderGLSetupEGL: eglSurface %p\n", renderer->eglSurface );
   }

   ctxAttrib[0]= EGL_CONTEXT_CLIENT_VERSION;
   ctxAttrib[1]= 2; // ES2
   ctxAttrib[2]= EGL_NONE;

   // Create an EGL context
   renderer->eglContext= eglCreateContext( renderer->eglDisplay, renderer->eglConfig, EGL_NO_CONTEXT, ctxAttrib );
   if ( renderer->eglContext == EGL_NO_CONTEXT )
   {
      printf( "wstRendererGLSetupEGL: eglCreateContext failed: %X\n", eglGetError() );
      goto exit;
   }
   printf("wstRendererGLSetupEGL: eglContext %p\n", renderer->eglContext );

   eglMakeCurrent( renderer->eglDisplay, renderer->eglSurface, renderer->eglSurface, renderer->eglContext );
   
   eglSwapInterval( renderer->eglDisplay, 1 );
   
   result= true;

exit:

   if ( eglConfigs )
   {
      //Crashes in free sometimes.  Why?
      //free( eglConfigs );
      eglConfigs= 0;
   }

   return result;
}


// ----------------------------------------------------------------------------
// Initialize the 4x4 matrix m[16] to the identity matrix
// ----------------------------------------------------------------------------
static void wstMatrixIdentity(float matrix[16])
{
    if (matrix)
    {
        memset( matrix, 0, 16*sizeof(float) );
    
        matrix[0]= 1.0f;
        matrix[5]= 1.0f; 
        matrix[10]= 1.0f;
        matrix[15]= 1.0f;
    }
}

// ----------------------------------------------------------------------------
// Multiply two 4 x 4 matrices
// ----------------------------------------------------------------------------
static void wstMatrixMultMatrix(float matrix[16], float in1[16], float in2[16])
{
    int i= 0;

    /* in1 or in2 also could be the result matrix so a, 
       working matrix temp[16] is used to store the result */
    float temp[16];

    if ( matrix && in1 && in2 )
    {
        temp[0]= in1[0] * in2[0] + in1[4] * in2[1] + in1[8] * in2[2] + in1[12] * in2[3];
        temp[1]= in1[1] * in2[0] + in1[5] * in2[1] + in1[9] * in2[2] + in1[13] * in2[3];
        temp[2]= in1[2] * in2[0] + in1[6] * in2[1] + in1[10] * in2[2] + in1[14] * in2[3];
        temp[3]= in1[3] * in2[0] + in1[7] * in2[1] + in1[11] * in2[2] + in1[15] * in2[3];
        temp[4]= in1[0] * in2[4] + in1[4] * in2[5] + in1[8] * in2[6] + in1[12] * in2[7];
        temp[5]= in1[1] * in2[4] + in1[5] * in2[5] + in1[9] * in2[6] + in1[13] * in2[7];
        temp[6]= in1[2] * in2[4] + in1[6] * in2[5] + in1[10] * in2[6] + in1[14] * in2[7];
        temp[7]= in1[3] * in2[4] + in1[7] * in2[5] + in1[11] * in2[6] + in1[15] * in2[7];
        temp[8]= in1[0] * in2[8] + in1[4] * in2[9] + in1[8] * in2[10] + in1[12] * in2[11];
        temp[9]= in1[1] * in2[8] + in1[5] * in2[9] + in1[9] * in2[10] + in1[13] * in2[11];
        temp[10]= in1[2] * in2[8] + in1[6] * in2[9] + in1[10] * in2[10] + in1[14] * in2[11];
        temp[11]= in1[3] * in2[8] + in1[7] * in2[9] + in1[11] * in2[10] + in1[15] * in2[11];
        temp[12]= in1[0] * in2[12] + in1[4] * in2[13] + in1[8] * in2[14] + in1[12] * in2[15];
        temp[13]= in1[1] * in2[12] + in1[5] * in2[13] + in1[9] * in2[14] + in1[13] * in2[15];
        temp[14]= in1[2] * in2[12] + in1[6] * in2[13] + in1[10] * in2[14] + in1[14] * in2[15];
        temp[15]= in1[3] * in2[12] + in1[7] * in2[13] + in1[11] * in2[14] + in1[15] * in2[15];

        for (i=0; i<16; i++)
        {
            matrix[i]= temp[i];
        }
    }
}
// ----------------------------------------------------------------------------
// Apply translation to 4x4 matrix
// ----------------------------------------------------------------------------
static void wstMatrixTranslate(float matrix[16], float dx, float dy, float dz)
{
    if ( matrix )
    {
        matrix[12] += (dx * matrix[0] + dy * matrix[4] + dz * matrix[8]);
        matrix[13] += (dx * matrix[1] + dy * matrix[5] + dz * matrix[9]);
        matrix[14] += (dx * matrix[2] + dy * matrix[6] + dz * matrix[10]);
        matrix[15] += (dx * matrix[3] + dy * matrix[7] + dz * matrix[11]);
    }
}

// ----------------------------------------------------------------------------
// Establish an orthogonal projection matrix
// ----------------------------------------------------------------------------
static void wstMatrixOrtho(float matrix[16], float left, float right, float bottom, float top, float near, float far)
{
    // working 4x4 matrix for intermediate values
    float temp[16] = { 1.0f, 0.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f, 0.0f,
                       0.0f, 0.0f, 1.0f, 0.0f,
                       0.0f, 0.0f, 0.0f, 1.0f };                
   
    if (matrix)
    { 
        if ( (left != right) && 
             (bottom != top) && 
             (near != far) )
        {
            temp[12]= ((right + left) / (right - left));
            temp[13]= ((top + bottom) / (top - bottom));
            temp[14]= ((far + near) / (far - near));
            temp[0]= (2.0f / (right - left));
            temp[5]= (2.0f / (top - bottom));
            temp[10]= (2.0f / (near - far));

            wstMatrixMultMatrix(matrix, matrix, temp);
        }
    }
}

static void wstSetCurrentShader( WstRendererGL *renderer, int shaderClassId )
{
   WstShader *shader= wstGetShaderForClass(renderer, shaderClassId);

   if ( shader )
   {
      if ( shader->shaderClass.id != renderer->currentShaderClass )
      {
         renderer->currentShaderClass= shader->shaderClass.id;
         glUseProgram(shader->programId);
         WST_TRACE("use program %d\n", shader->programId );

         glUniformMatrix4fv(shader->mvpLocation, 1, GL_FALSE, renderer->mvp);
         for( int i= 0; i < shader->shaderClass.bits.maskCount; ++i )
         {
            glUniform1i(shader->maskUniformLocation[i], i+1);
         }
      }
   }

   renderer->currentShader= (shader != 0) ? shader : &renderer->nullShader;
}

// ----------------------------------------------------------------------------
// Get a shader program for the specified shader class
// ----------------------------------------------------------------------------
static WstShader *wstGetShaderForClass( WstRendererGL *renderer, int shaderClassId )
{
   WstShader *shader= 0;
   WstShaderClass shaderClass;
   int cacheIndex;

   shaderClass.id= shaderClassId;
   cacheIndex= shaderClass.bits.type + shaderClass.bits.opacity*4 + shaderClass.bits.maskCount*8;
   WST_TRACE("shaderClass %X cacheIndex %d shaderCacheSize %d\n", shaderClass.id, cacheIndex, renderer->shaderCacheSize );
   assert( cacheIndex < renderer->shaderCacheSize );
   shader= renderer->shaderCache[cacheIndex];
   if ( !shader )
   {
      shader= wstCreateShaderForClass( shaderClass );
      renderer->shaderCache[cacheIndex]= shader;
   }

   return shader;   
}

#define WST_MAX_SHADER_SOURCE_SIZE 1024
#define EMIT(s) \
  assert( (strnlen(shaderSource, WST_MAX_SHADER_SOURCE_SIZE) + strnlen(s, WST_MAX_SHADER_SOURCE_SIZE)) \
          < \
          WST_MAX_SHADER_SOURCE_SIZE); \
  strncat(shaderSource, s, strnlen(s, WST_MAX_SHADER_SOURCE_SIZE))

#if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   #define WST_SHADER_PRECISION "mediump"
#else
   #define WST_SHADER_PRECISION " "
#endif   

// ----------------------------------------------------------------------------
// Create a shader program for the specified shader class
// ----------------------------------------------------------------------------
static WstShader* wstCreateShaderForClass( WstShaderClass shaderClass )
{
   int error= 0;
   int i;
   char name[32];
   WstShader *shaderNew= 0;
   char *shaderSource= 0;
   GLuint vertexShaderId= GL_NONE, fragmentShaderId= GL_NONE;
   GLuint programId= GL_NONE;
   GLuint attribIndex;
   int shaderStatus = GL_FALSE; 
   int infoLen;
   char infoLog[MAX_LOG_SIZE+1];


   shaderNew= (WstShader*)calloc( 1, sizeof(WstShader) );
   if ( !shaderNew )
   {
      WST_TRACE("Not enough memory to allocate shader for class %X\n", shaderClass.id );
      return shaderNew;
   } 

   // Generate shader programs
   shaderSource= (char*)malloc( WST_MAX_SHADER_SOURCE_SIZE );
   if ( shaderSource )
   {
      // Generate vertex shader source
      shaderSource[0]= '\0';
      EMIT( "uniform mat4 mvp;\n" );
      switch ( shaderClass.bits.type )
      {
         case WstShaderType_texture:
            EMIT( "varying " WST_SHADER_PRECISION " vec2 texcoord;\n" );
            break;
         case WstShaderType_textureYUV:
            EMIT( "varying " WST_SHADER_PRECISION " vec2 texcoord0;\n" );
            EMIT( "varying " WST_SHADER_PRECISION " vec2 texcoord1;\n" );
            break;
         case WstShaderType_fill:
            EMIT( "varying " WST_SHADER_PRECISION " vec4 basecolor;\n" );
            break;
         case WstShaderType_alphaText:
            EMIT( "varying " WST_SHADER_PRECISION " vec2 texcoord;\n" );
            break;
         default:
            assert(0);
            break;
      }

      for( i= 0; i < shaderClass.bits.maskCount; ++i )
      {
         snprintf( name, sizeof(name)-1, "maskcoord%d", i );
         EMIT( "varying " WST_SHADER_PRECISION " vec2 " );
         EMIT( name );
         EMIT( ";\n" );
      }

      EMIT( "attribute vec4 position;\n" );
      EMIT( "attribute vec2 inputtexture0;\n" );
      EMIT( "attribute vec2 inputtexture1;\n" );
      EMIT( "attribute vec4 inputcolor;\n" );

      for( i= 0; i < shaderClass.bits.maskCount; ++i )
      {
         snprintf( name, sizeof(name)-1, "inputmask%d", i );
         EMIT( "attribute vec2 " );
         EMIT( name );
         EMIT( ";\n" );
      }

      EMIT( "void main(void) {\n" );
      switch ( shaderClass.bits.type )
      {
         case WstShaderType_texture:
            EMIT( "   texcoord = inputtexture0;\n" );
            break;
         case WstShaderType_textureYUV:
            EMIT( "   texcoord0 = inputtexture0;\n" );
            EMIT( "   texcoord1 = inputtexture1;\n" );
            break;
         case WstShaderType_fill:
            EMIT( "   basecolor = inputcolor;\n" );
            break;
         case WstShaderType_alphaText:
            EMIT( "   texcoord = inputtexture0;\n" );
            break;
         default:
            assert(0);
            break;
      }

      for( i= 0; i < shaderClass.bits.maskCount; ++i )
      {
         snprintf( name, sizeof(name)-1, "maskcoord%d", i );
         EMIT( "   " );
         EMIT( name );
         EMIT( " = " );
         snprintf( name, sizeof(name)-1, "inputmask%d", i );
         EMIT( name );
         EMIT( ";\n" );
      }

      EMIT( "   gl_Position = mvp * position;\n" );
      EMIT( "}\n" );

      WST_TRACE( "vertex source:\n(\n%s\n)\n", shaderSource );

      // Create vertex shader from source
      vertexShaderId = wstCreateGLShader(shaderSource, true);
      WST_TRACE( "shader class %X vertexShaderId=%d\n", shaderClass.id, vertexShaderId );


      // Generate fragment shader source
      shaderSource[0]= '\0';
      switch ( shaderClass.bits.type )
      {
         case WstShaderType_texture:
            EMIT( "varying " WST_SHADER_PRECISION " vec2 texcoord;\n" );
            break;
         case WstShaderType_textureYUV:
            EMIT( "varying " WST_SHADER_PRECISION " vec2 texcoord0;\n" );
            EMIT( "varying " WST_SHADER_PRECISION " vec2 texcoord1;\n" );
            break;
         case WstShaderType_fill:
            EMIT( "varying " WST_SHADER_PRECISION " vec4 basecolor;\n" );
            break;
         case WstShaderType_alphaText:
            EMIT( "varying " WST_SHADER_PRECISION " vec2 texcoord;\n" );
            break;
         default:
            assert(0);
            break;
      }
      for( i= 0; i < shaderClass.bits.maskCount; ++i )
      {
         EMIT( "varying " WST_SHADER_PRECISION " vec2 " );
         snprintf( name, sizeof(name)-1, "maskcoord%d", i );
         EMIT( name );
         EMIT( ";\n" );
      }

      if ( (shaderClass.bits.type == WstShaderType_texture) || 
           (shaderClass.bits.type == WstShaderType_alphaText) )
      {
         EMIT( "uniform sampler2D texture;\n" );
      }
      else if ( shaderClass.bits.type == WstShaderType_textureYUV )
      {
         EMIT( "uniform sampler2D textureY;\n" );
         EMIT( "uniform sampler2D textureUV;\n" );
         EMIT( "const " WST_SHADER_PRECISION " vec3 cc_r = vec3(1.0, -0.8604, 1.59580);\n" );
         EMIT( "const " WST_SHADER_PRECISION " vec4 cc_g = vec4(1.0, 0.539815, -0.39173, -0.81290);\n" );
         EMIT( "const " WST_SHADER_PRECISION " vec3 cc_b = vec3(1.0, -1.071, 2.01700);\n" );
      }
      for( i= 0; i < shaderClass.bits.maskCount; ++i )
      {
         snprintf( name, sizeof(name)-1, "mask%d", i );
         EMIT( "uniform sampler2D " );
         EMIT( name );
         EMIT( ";\n" );
      }
      if ( shaderClass.bits.type == WstShaderType_alphaText )
      {
         EMIT( "uniform " WST_SHADER_PRECISION " vec4 alphaTextColor;\n" );
      }
      if ( shaderClass.bits.opacity )
      {
         EMIT( "uniform " WST_SHADER_PRECISION " float opacity;\n" );
      }

      EMIT( "void main(void) {\n" );

      if ( (shaderClass.bits.type == WstShaderType_texture) || 
           (shaderClass.bits.type == WstShaderType_alphaText) )
      {
         EMIT( "   " WST_SHADER_PRECISION " vec4 texcolor = texture2D(texture, texcoord);\n" );
      }
      else if ( shaderClass.bits.type == WstShaderType_textureYUV )
      {
         EMIT( "   " WST_SHADER_PRECISION " vec4 y_vec = texture2D(textureY, texcoord0);\n" );
         EMIT( "   " WST_SHADER_PRECISION " vec4 c_vec = texture2D(textureUV, texcoord1);\n" );
         EMIT( "   " WST_SHADER_PRECISION " vec4 temp_vec = vec4(y_vec.a, 1.0, c_vec.b, c_vec.a);\n" );
      }
      for( i= 0; i < shaderClass.bits.maskCount; ++i )
      {
         EMIT( "   " WST_SHADER_PRECISION " vec4 " );
         snprintf( name, sizeof(name)-1, "maskcolor%d", i );
         EMIT( name );
         EMIT( " = texture2D(" );
         snprintf( name, sizeof(name)-1, "mask%d", i );
         EMIT( name );
         EMIT( ", " );
         snprintf( name, sizeof(name)-1, "maskcoord%d", i );
         EMIT( name );
         EMIT( ");\n" );
      }

      EMIT( "   gl_FragColor = " );
      if ( shaderClass.bits.opacity )
      {
         EMIT( "opacity*" );
      }
      for( i= 0; i < shaderClass.bits.maskCount; ++i )
      {
         snprintf( name, sizeof(name)-1, "maskcolor%d", i );
         EMIT( name );
         EMIT( ".a*" );
      }
      switch ( shaderClass.bits.type )
      {
         case WstShaderType_texture:
            EMIT( "texcolor;\n" );
            break;
         case WstShaderType_textureYUV:
            EMIT( "vec4( dot(cc_r,temp_vec.xyw), dot(cc_g,temp_vec), dot(cc_b,temp_vec.xyz), 1.0 );\n" );
            break;
         case WstShaderType_fill:
            EMIT( "vec4( basecolor.a*basecolor.r, basecolor.a*basecolor.g, basecolor.a*basecolor.b, basecolor.a );\n" );
            break;
         case WstShaderType_alphaText:
            EMIT( "texcolor.a*alphaTextColor;\n" );
            break;
         default:
            assert(0);
            break;
      }
      EMIT( "}\n" );

      WST_TRACE( "fragment source:\n(\n%s\n)\n", shaderSource );

      // Create fragment shader from source
      fragmentShaderId = wstCreateGLShader(shaderSource, false);
      WST_TRACE( "shader class %X fragmentShaderId=%d\n", shaderClass.id, fragmentShaderId );

      free( shaderSource );
   }

   if ( (vertexShaderId != GL_NONE) && (fragmentShaderId != GL_NONE) )
   {
       // Create Program
       programId = glCreateProgram();
       WST_TRACE("shader class %X programId=%d vertexShaderId=%d fragmentShaderId=%d\n", 
                   shaderClass.id, programId, vertexShaderId, fragmentShaderId );

       glAttachShader(programId, vertexShaderId);
       glAttachShader(programId, fragmentShaderId);

       attribIndex= 0;
       glBindAttribLocation(programId, attribIndex++, "position");
       glBindAttribLocation(programId, attribIndex++, "inputtexture0" );
       glBindAttribLocation(programId, attribIndex++, "inputtexture1" );
       glBindAttribLocation(programId, attribIndex++, "inputcolor" );
       for( i= 0; i < shaderClass.bits.maskCount; ++i )
       {
          snprintf( name, sizeof(name)-1, "inputmask%d", i );
          glBindAttribLocation(programId, attribIndex++, name );
       }

       glLinkProgram(programId);

       glGetProgramiv(programId, GL_LINK_STATUS, &shaderStatus);
       if (shaderStatus != GL_TRUE)
       {
           fprintf(stderr,"Error: Failed to link GLSL program\n");
           glGetProgramInfoLog(programId, MAX_LOG_SIZE, &infoLen, infoLog);
           if (infoLen > MAX_LOG_SIZE)
           {
               infoLog[MAX_LOG_SIZE] = '\0';
           }
           fprintf(stderr,"%s\n",infoLog);
           error= 1;
       }
       else
       {
           glValidateProgram(programId); 
           glGetProgramiv(programId, GL_VALIDATE_STATUS, &shaderStatus);
           if (shaderStatus != GL_TRUE)
           {
               fprintf(stderr,"Error: Failed to validate GLSL program\n");
               glGetProgramInfoLog(programId, MAX_LOG_SIZE, &infoLen, infoLog);
               if (infoLen > MAX_LOG_SIZE)
               {
                   infoLog[MAX_LOG_SIZE] = '\0';
               }
               fprintf(stderr,"%s\n",infoLog);
               error= 1;
           }
       }

       if (shaderStatus == GL_TRUE)
       {
           shaderNew->shaderClass= shaderClass;
           shaderNew->programId= programId;
           shaderNew->vertexShaderId= vertexShaderId;
           shaderNew->fragmentShaderId= fragmentShaderId;

           // Get attribute locations
           shaderNew->vertexLocation= glGetAttribLocation(programId, "position");
           shaderNew->textureLocation[0]= glGetAttribLocation(programId, "inputtexture0");
           shaderNew->textureLocation[1]= glGetAttribLocation(programId, "inputtexture1");
           shaderNew->fillColorLocation= glGetAttribLocation(programId, "inputcolor");
           for( i= 0; i < shaderClass.bits.maskCount; ++i )
           {
              snprintf( name, sizeof(name)-1, "inputmask%d", i );
              shaderNew->maskAttribLocation[i]= glGetAttribLocation(programId, name );
           }

           // Get uniform locations
           shaderNew->mvpLocation= glGetUniformLocation(programId, "mvp");
           if ( shaderClass.bits.type == WstShaderType_alphaText )
           {
              shaderNew->alphaTextColorLocation= glGetUniformLocation(programId, "alphaTextColor");
           }
           if ( shaderClass.bits.opacity )
           {
              shaderNew->opacityLocation = glGetUniformLocation(programId, "opacity");
           }
           for( i= 0; i < shaderClass.bits.maskCount; ++i )
           {
              snprintf( name, sizeof(name)-1, "mask%d", i );
              shaderNew->maskUniformLocation[i] = glGetUniformLocation(programId, name);
           }
           if ( shaderClass.bits.type == WstShaderType_texture )
           {
              shaderNew->textureUnit0= glGetUniformLocation(programId,"texture");
           }
           else if ( shaderClass.bits.type == WstShaderType_textureYUV )
           {
              shaderNew->textureUnit0= glGetUniformLocation(programId,"textureY");
              shaderNew->textureUnit1= glGetUniformLocation(programId,"textureUV");
           }

           #ifdef WST_DEBUG
           WST_TRACE("shader %p created for class %X:", shaderNew, shaderNew->shaderClass.id );
           WST_TRACE("programId=%d vertexShaderId=%d fragmentShaderId=%d", 
                     shaderNew->programId, shaderNew->vertexShaderId, shaderNew->fragmentShaderId );
           WST_TRACE("vertexLocation= %d textureLocation[0]=%d textureLocation[1]=%d fillColorLocation=%d mvpLocation= %d", 
                     shaderNew->vertexLocation, shaderNew->textureLocation[0], shaderNew->textureLocation[1],
                     shaderNew->fillColorLocation, shaderNew->mvpLocation );
           if ( shaderNew->shaderClass.bits.opacity )
           {
              WST_TRACE("opacityLocation= %d", shaderNew->opacityLocation );
           }
           if ( shaderClass.bits.type == WstShaderType_alphaText )
           {
              WST_TRACE("alphaTextColorLocation= %d", shaderNew->alphaTextColorLocation );
           }
           for( i= 0; i < shaderClass.bits.maskCount; ++i )
           {
              WST_TRACE("mask %d attribLocation %d uniformLocation %d", i, shaderNew->maskAttribLocation[i], shaderNew->maskUniformLocation[i] );
           }
           #endif
       }
   }
   else
   {
       error= 1;
   }

   if ( error )
   {
      if (programId)
      {
         if (vertexShaderId)
         {
            glDetachShader(programId, vertexShaderId);
            glDeleteShader(vertexShaderId);
         }
         if (fragmentShaderId)
         {
            glDetachShader(programId, fragmentShaderId);
            glDeleteShader(fragmentShaderId);
         }
         glDeleteProgram(programId);
      }
   }

   return shaderNew;
}

// ----------------------------------------------------------------------------
// Destroy a shader program created for a specified shader class
// ----------------------------------------------------------------------------
static void wstDestroyShader( WstRendererGL *renderer, WstShader *shader )
{
   if ( shader )
   {
      assert( shader != &renderer->nullShader );

      if ( shader == renderer->currentShader )
      {
         renderer->currentShader= &renderer->nullShader;
      }

      if (shader->programId)
      {
         if (shader->vertexShaderId)
         {
            glDetachShader(shader->programId, shader->vertexShaderId);
            glDeleteShader(shader->vertexShaderId);
         }
         if (shader->fragmentShaderId)
         {
            glDetachShader(shader->programId, shader->fragmentShaderId);
            glDeleteShader(shader->fragmentShaderId);
         }
         glDeleteProgram(shader->programId);
      }

      free( shader );
   }
}

// ----------------------------------------------------------------------------
// Create a shader from source code
// ----------------------------------------------------------------------------
static unsigned int wstCreateGLShader(const char *pShaderSource, bool isVertexShader)
{
    GLuint shaderId= 0;
    int status, loglen, shaderSourceLen= -1;
    char log[MAX_LOG_SIZE+1];

    if ( pShaderSource )
    {
        shaderId= glCreateShader( isVertexShader ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER );
        if ( !shaderId )
        {
            fprintf(stderr,"Error: Failed to create %s Shader\n", isVertexShader ? "vertex" : "fragment");
            return 0;
        }
        
        glShaderSource(shaderId, 1, (const char **)&pShaderSource, &shaderSourceLen);
        glCompileShader(shaderId);

        glGetShaderiv( shaderId, GL_COMPILE_STATUS, &status);
        if (GL_TRUE != status )
        {
            fprintf(stderr,"Error: Failed to compile GL %s Shader\n", isVertexShader ? "vertex" : "fragment" );
            glGetShaderInfoLog( shaderId, MAX_LOG_SIZE, &loglen, log);
            if (loglen > MAX_LOG_SIZE)
            {
                 log[MAX_LOG_SIZE]= '\0';
            }
            else
            {
                log[loglen]= '\0';
            }
            fprintf(stderr, "%s\n",log);
        }
    }
    
    return shaderId;
}

static void wstRendererTerm( WstRenderer *renderer )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   if ( rendererGL )
   {
      wstRendererGLDestroy( rendererGL );
      renderer->renderer= 0;
   }
}

static void wstRendererUpdateScene( WstRenderer *renderer )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   GLfloat *mvp;
   WstRenderContext *rctx;

   if ( (renderer->outputWidth != rendererGL->outputWidth) ||
        (renderer->outputHeight != rendererGL->outputHeight) )
   {
      rendererGL->outputWidth= renderer->outputWidth;
      rendererGL->outputHeight= renderer->outputHeight;
      
      wstMatrixIdentity(rendererGL->modelview);
      wstMatrixTranslate(rendererGL->modelview, -rendererGL->outputWidth, -rendererGL->outputHeight, 0);
      wstMatrixIdentity(rendererGL->projection);
      wstMatrixOrtho( rendererGL->projection, 
                      0,                          //left
                      rendererGL->outputWidth,    //right
                      rendererGL->outputHeight,   //bottom
                      0,                          //top
                      ZNEAR,                      //near
                      ZFAR );                     //far

      wstMatrixMultMatrix(rendererGL->mvp, rendererGL->projection, rendererGL->modelview);
   }

   eglMakeCurrent( rendererGL->eglDisplay, 
                   rendererGL->eglSurface, 
                   rendererGL->eglSurface, 
                   rendererGL->eglContext );
   
   mvp= rendererGL->mvp;
   
   glViewport( 0, 0, renderer->outputWidth, renderer->outputHeight );
   glClearColor( 0.0, 0.0, 0.0, 0.0 );
   glClear( GL_COLOR_BUFFER_BIT );
   
   glDisable(GL_BLEND);
   glBlendColor(0,0,0,0);
   glActiveTexture(GL_TEXTURE0);
   glDisable(GL_STENCIL_TEST);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE);
   glDisable(GL_SCISSOR_TEST);
   glBlendFuncSeparate( GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE );
   
   glUseProgram(rendererGL->currentShader->programId);
   
   glUniformMatrix4fv(rendererGL->currentShader->mvpLocation, 1, GL_FALSE, mvp);
   
   for( int i= 0; i < rendererGL->maxTextureUnits; ++i )
   {
      rendererGL->activeTextureId[i]= GL_NONE;
   }
   
   rctx= &rendererGL->renderCtx;
   
   rctx->opacity= 1.0f;
   rctx->z= ZSTART+ZINC;
   rctx->clip[0]= 0;
   rctx->clip[1]= 0;
   rctx->clip[2]= renderer->outputWidth;
   rctx->clip[3]= renderer->outputHeight;
   float transform[6];
   rctx->transform= transform;
   transform[TCM11]= 1.0f;
   transform[TCM22]= 1.0f;
   transform[TCM12]= transform[TCM21]= transform[TCMDX]= transform[TCMDY]= 0.0;   
   
   rctx->cpuClip= ( (transform[TCM12] == 0.0) && (transform[TCM21] == 0.0) ) ? 1 : 0;
   
   /*
    * Render surfaces from bottom to top
    */   
   int imax= rendererGL->surfaces.size();
   for( int i= 0; i < imax; ++i )
   {
      WstRenderSurface *surface= rendererGL->surfaces[i];
      
      if ( surface->visible && 
          (
            #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
            surface->eglImage[0] ||
            #endif
            surface->memDirty ||
            (surface->textureId[0] != GL_NONE)
          )
        )
      {
         wstRenderGLPrepareSurface( rendererGL, rctx, surface );
         wstRenderGLRenderSurface( rendererGL, rctx, surface );
      }
   }
 
   glFlush();
   glFinish();

   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   eglSwapBuffers(rendererGL->eglDisplay, rendererGL->eglSurface);
   #endif
}

static WstRenderSurface* wstRendererSurfaceCreate( WstRenderer *renderer )
{
   WstRenderSurface *surface;
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;

   surface= wstRenderGLCreateSurface(rendererGL);
   
   std::vector<WstRenderSurface*>::iterator it= rendererGL->surfaces.begin();
   while ( it != rendererGL->surfaces.end() )
   {
      if ( surface->zorder < (*it)->zorder )
      {
         break;
      }
      ++it;
   }
   rendererGL->surfaces.insert(it,surface);
   
   return surface; 
}

static void wstRendererSurfaceDestroy( WstRenderer *renderer, WstRenderSurface *surface )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;

   for ( std::vector<WstRenderSurface*>::iterator it= rendererGL->surfaces.begin(); 
         it != rendererGL->surfaces.end();
         ++it )
   {
      if ( (*it) == surface )
      {
         rendererGL->surfaces.erase(it);
         break;   
      }
   }   
   
   wstRenderGLDestroySurface( rendererGL, surface );
}

static void wstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   EGLint value;

   if ( resource )
   {
      if ( wl_shm_buffer_get( resource ) )
      {
         wstRendererGLCommitShm( rendererGL, surface, resource );
      }
      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      else if ( rendererGL->haveWaylandEGL && 
                (EGL_TRUE == rendererGL->eglQueryWaylandBufferWL( rendererGL->eglDisplay,
                                                                  resource,
                                                                  EGL_TEXTURE_FORMAT,
                                                                  &value ) ) )
      {
         wstRendererGLCommitWaylandEGL( rendererGL, surface, resource, value );
      }
      #endif
      #ifdef ENABLE_SBPROTOCOL
      else if ( WstSBBufferGet( resource ) )
      {
         wstRendererGLCommitSB( rendererGL, surface, resource );
      }
      #endif
      else
      {
         printf("wstRenderSurfaceCommit: unsupported buffer type\n");
      }
   }
   else
   {
      wstRendererGLFlushSurface( rendererGL, surface );
   }
}

static void wstRendererSurfaceSetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool visible )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;

   if ( surface )
   {
      surface->visible= visible;
   }
}

static bool wstRendererSurfaceGetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool *visible )
{
   bool isVisible= false;
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;

   if ( surface )
   {
      isVisible= surface->visible;
      
      *visible= isVisible;
   }
   
   return isVisible;   
}

static void wstRendererSurfaceSetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   
   if ( surface )
   {
      surface->x= x;
      surface->y= y;
      surface->width= width;
      surface->height= height;
      surface->vertexCoordsDirty= 1;
   }
}

void wstRendererSurfaceGetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;

   if ( surface )
   {
      *x= surface->x;
      *y= surface->y;
      *width= surface->width;
      *height= surface->height;
   }
}

static void wstRendererSurfaceSetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float opacity )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   
   if ( surface )
   {
      surface->opacity= opacity;
   }
}

static float wstRendererSurfaceGetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float *opacity )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   float opacityLevel= 1.0;
   
   if ( surface )
   {
      opacityLevel= surface->opacity;
      
      *opacity= opacityLevel;
   }
   
   return opacityLevel;
}

static void wstRendererSurfaceSetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float z )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   
   if ( surface )
   {
      surface->zorder= z;

      // Remove from surface list
      for ( std::vector<WstRenderSurface*>::iterator it= rendererGL->surfaces.begin(); 
            it != rendererGL->surfaces.end();
            ++it )
      {
         if ( (*it) == surface )
         {
            rendererGL->surfaces.erase(it);
            break;   
         }
      }   

      // Re-insert in surface list based on new z-order
      std::vector<WstRenderSurface*>::iterator it= rendererGL->surfaces.begin();
      while ( it != rendererGL->surfaces.end() )
      {
         if ( surface->zorder < (*it)->zorder )
         {
            break;
         }
         ++it;
      }
      rendererGL->surfaces.insert(it,surface);
   }
}

static float wstRendererSurfaceGetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float *z )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   float zLevel= 1.0;
   
   if ( surface )
   {
      zLevel= surface->zorder;
      
      *z= zLevel;
   }
   
   return zLevel;
}

extern "C"
{

int renderer_init( WstRenderer *renderer, int argc, char **argv )
{
   int rc= 0;
   WstRendererGL *rendererGL= 0;
   
   rendererGL= wstRendererGLCreate( renderer );
   if ( rendererGL )
   {
      renderer->renderer= rendererGL;
      renderer->renderTerm= wstRendererTerm;
      renderer->updateScene= wstRendererUpdateScene;
      renderer->surfaceCreate= wstRendererSurfaceCreate;
      renderer->surfaceDestroy= wstRendererSurfaceDestroy;
      renderer->surfaceCommit= wstRendererSurfaceCommit;
      renderer->surfaceSetVisible= wstRendererSurfaceSetVisible;
      renderer->surfaceGetVisible= wstRendererSurfaceGetVisible;
      renderer->surfaceSetGeometry= wstRendererSurfaceSetGeometry;
      renderer->surfaceGetGeometry= wstRendererSurfaceGetGeometry;
      renderer->surfaceSetOpacity= wstRendererSurfaceSetOpacity;
      renderer->surfaceGetOpacity= wstRendererSurfaceGetOpacity;
      renderer->surfaceSetZOrder= wstRendererSurfaceSetZOrder;
      renderer->surfaceGetZOrder= wstRendererSurfaceGetZOrder;
   }
   else
   {
      rc= -1;
   }

exit:
   
   return 0;
}

}

