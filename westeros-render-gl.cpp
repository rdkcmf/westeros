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
#include <sys/time.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined (USE_MESA)
#include <EGL/eglmesaext.h>
#endif

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

#define DEFAULT_SURFACE_WIDTH (0)
#define DEFAULT_SURFACE_HEIGHT (0)

#ifndef DRM_FORMAT_R8
#define DRM_FORMAT_R8 (0x20203852)
#endif

#ifndef DRM_FORMAT_GR88
#define DRM_FORMAT_GR88 (0x38385247)
#endif

#ifndef DRM_FORMAT_RG88
#define DRM_FORMAT_RG88 (0x38384752)
#endif

#ifndef DRM_FORMAT_NV12
#define DRM_FORMAT_NV12 (0x3231564E)
#endif

#ifndef DRM_FORMAT_NV21
#define DRM_FORMAT_NV21 (0x3132564E)
#endif

static const char *fShaderText =
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "uniform sampler2D texture;\n"
  "uniform float alpha;\n"
  "varying vec2 txv;\n"
  "void main()\n"
  "{\n"
  "  gl_FragColor= texture2D(texture, txv) * alpha;\n"
  "}\n";

static const char *vShaderText =
  "uniform vec2 resolution;\n"
  "uniform mat4 matrix;\n"
  "attribute vec2 pos;\n"
  "attribute vec2 texcoord;\n"
  "varying vec2 txv;\n"
  "void main()\n"
  "{\n"
  "  vec4 p= matrix * vec4(pos, 0, 1);\n"
  "  vec4 zeroToOne= p / vec4(resolution, resolution.x, 1);\n"
  "  vec4 zeroToTwo= zeroToOne * vec4(2.0, 2.0, 1, 1);\n"
  "  vec4 clipSpace= zeroToTwo - vec4(1.0, 1.0, 0, 0);\n"
  "  clipSpace.w= 1.0+clipSpace.z;\n"
  "  gl_Position=  clipSpace * vec4(1, -1, 1, 1);\n"
  "  txv= texcoord;\n"
  "}\n";

static const char *fShaderTextYUV =
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "uniform sampler2D texture;\n"
  "uniform sampler2D textureuv;\n"
  "const vec3 cc_r = vec3(1.0, -0.8604, 1.59580);\n"
  "const vec4 cc_g = vec4(1.0, 0.539815, -0.39173, -0.81290);\n"
  "const vec3 cc_b = vec3(1.0, -1.071, 2.01700);\n"
  "uniform float alpha;\n"
  "varying vec2 txv;\n"
  "varying vec2 txvuv;\n"
  "void main()\n"
  "{\n"
  "   vec4 y_vec= texture2D(texture, txv);\n"
  "   vec4 c_vec= texture2D(textureuv, txvuv);\n"
  "   vec4 temp_vec= vec4(y_vec.a, 1.0, c_vec.b, c_vec.a);\n"
  "   gl_FragColor= vec4( dot(cc_r,temp_vec.xyw), dot(cc_g,temp_vec), dot(cc_b,temp_vec.xyz), alpha );\n"
  "}\n";

static const char *vShaderTextYUV =
  "uniform vec2 resolution;\n"
  "uniform mat4 matrix;\n"
  "attribute vec2 pos;\n"
  "attribute vec2 texcoord;\n"
  "attribute vec2 texcoorduv;\n"
  "varying vec2 txv;\n"
  "varying vec2 txvuv;\n"
  "void main()\n"
  "{\n"
  "  vec4 p= matrix * vec4(pos, 0, 1);\n"
  "  vec4 zeroToOne= p / vec4(resolution, resolution.x, 1);\n"
  "  vec4 zeroToTwo= zeroToOne * vec4(2.0, 2.0, 1, 1);\n"
  "  vec4 clipSpace= zeroToTwo - vec4(1.0, 1.0, 0, 0);\n"
  "  clipSpace.w= 1.0+clipSpace.z;\n"
  "  gl_Position=  clipSpace * vec4(1, -1, 1, 1);\n"
  "  txv= texcoord;\n"
  "  txvuv= texcoorduv;\n"
  "}\n";

static const char *fShaderText_Y_UV =
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "uniform sampler2D texture;\n"
  "uniform sampler2D textureuv;\n"
  "const vec3 cc_r = vec3(1.0, -0.8604, 1.59580);\n"
  "const vec4 cc_g = vec4(1.0, 0.539815, -0.39173, -0.81290);\n"
  "const vec3 cc_b = vec3(1.0, -1.071, 2.01700);\n"
  "varying vec2 txv;\n"
  "varying vec2 txvuv;\n"
  "void main()\n"
  "{\n"
  "   vec4 y_vec= texture2D(texture, txv);\n"
  "   vec4 c_vec= texture2D(textureuv, txvuv);\n"
  "   vec4 temp_vec= vec4(y_vec.r, 1.0, c_vec.r, c_vec.g);\n"
  "   gl_FragColor= vec4( dot(cc_r,temp_vec.xyw), dot(cc_g,temp_vec), dot(cc_b,temp_vec.xyz), 1 );\n"
  "}\n";

static const char *fShaderTextExternal =
  "#extension GL_OES_EGL_image_external : require\n"
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "uniform samplerExternalOES texture;\n"
  "varying vec2 txv;\n"
  "void main()\n"
  "{\n"
  "  gl_FragColor= texture2D(texture, txv);\n"
  "}\n";

typedef enum _WstShaderType
{
   WstShaderType_rgb,
   WstShaderType_yuv,
   WstShaderType_external
} WstShaderType;

typedef struct _WstShader
{
   bool isYUV;
   GLuint fragShader;
   GLuint vertShader;
   GLuint program;
   GLuint attrPos;
   GLuint attrTexcoord;
   GLuint attrTexcoorduv;
   GLint uniRes;
   GLint uniMatrix;
   GLint uniAlpha;
   GLint uniTexture;
   GLint uniTextureuv;
} WstShader;

static char message[1024];

#define MAX_TEXTURES (2)

struct _WstRenderSurface
{
   void *nativePixmap;
   int textureCount;
   bool externalImage;
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

   bool sizeOverride;
   bool invertedY;
};

typedef struct _WstRendererGL
{
   WstRenderer *renderer;
   int outputWidth;
   int outputHeight;
   WstShader *textureShader;
   WstShader *textureShaderYUV;
   WstShader *textureShaderExternal;

   void *nativeWindow;

   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   WstGLCtx *glCtx;
   #endif
   
   EGLDisplay eglDisplay;
   EGLConfig eglConfig;
   EGLContext eglContext;   
   EGLSurface eglSurface;
   #ifdef _WESTEROS_GL_ICEGDL_
   EGLSurface eglSurfaceSave;
   #endif

   PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
   PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
   #endif

   bool haveDmaBufImport;
   bool haveExternalImage;

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
static WstRenderSurface *wstRendererGLCreateSurface(WstRendererGL *renderer);
static void wstRendererGLDestroySurface( WstRendererGL *renderer, WstRenderSurface *surface );
static void wstRendererGLFlushSurface( WstRendererGL *renderer, WstRenderSurface *surface );
static void wstRendererGLPrepareResource( WstRendererGL *renderer, WstRenderSurface *surface, struct wl_resource *resource);
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
static void wstRendererGLRenderSurface( WstRendererGL *renderer, WstRenderSurface *surface );

static bool wstRendererGLSetupEGL( WstRendererGL *renderer );
static void wstRendererGLDestroyShader( WstShader *shader );
static WstShader* wstRendererGLCreateShader( WstRendererGL *renderer, int shaderType );
static void wstRendererGLShaderDraw( WstShader *shader,
                                      int width, int height, float* matrix, float alpha,
                                      GLuint textureId, GLuint textureUVId,
                                      int count, const float* vc, const float* txc );

static bool emitFPS= false;

static WstRendererGL* wstRendererGLCreate( WstRenderer *renderer )
{
   WstRendererGL *rendererGL= 0;
   
   rendererGL= (WstRendererGL*)calloc(1, sizeof(WstRendererGL) );
   if ( rendererGL )
   {
      if ( getenv("WESTEROS_RENDER_GL_FPS" ) )
      {
         emitFPS= true;
      }

      rendererGL->outputWidth= renderer->outputWidth;
      rendererGL->outputHeight= renderer->outputHeight;

      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      rendererGL->glCtx= WstGLInit();
      #endif
      
      rendererGL->renderer= renderer;
      wstRendererGLSetupEGL( rendererGL );

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
         if ( strstr( extensions, "EGL_EXT_image_dma_buf_import" ) )
         {
            rendererGL->haveDmaBufImport= true;
         }
      }
      extensions= (const char *)glGetString(GL_EXTENSIONS);
      if ( extensions )
      {
         #ifdef GL_OES_EGL_image_external
         if ( strstr( extensions, "GL_OES_EGL_image_external" ) )
         {
            rendererGL->haveExternalImage= true;
         }
         #endif
      }
      printf("have wayland-egl: %d\n", rendererGL->haveWaylandEGL );
      printf("have dmabuf import: %d\n", rendererGL->haveDmaBufImport );
      printf("have external image: %d\n", rendererGL->haveExternalImage );
      #endif
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

      if ( renderer->textureShader )
      {
         wstRendererGLDestroyShader( renderer->textureShader );
         renderer->textureShader= 0;
      }
      if ( renderer->textureShaderYUV )
      {
         wstRendererGLDestroyShader( renderer->textureShaderYUV );
         renderer->textureShaderYUV= 0;
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
         #endif
      }

      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      if ( renderer->glCtx )
      {
         WstGLTerm( renderer->glCtx );
         renderer->glCtx= 0;
      }
      #endif

      free( renderer );
   }
}

static WstRenderSurface *wstRendererGLCreateSurface(WstRendererGL *renderer)
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
    }
   
    return surface;
}

static void wstRendererGLDestroySurface( WstRendererGL *renderer, WstRenderSurface *surface )
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

static void wstRendererGLPrepareResource( WstRendererGL *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   if ( surface && resource )
   {
      EGLImageKHR eglImage= 0;

      #ifdef ENABLE_SBPROTOCOL
      struct wl_sb_buffer *sbBuffer;
      sbBuffer= WstSBBufferGet( resource );
      if ( sbBuffer )
      {
         #ifdef EGL_LINUX_DMA_BUF_EXT
         if ( renderer->haveDmaBufImport )
         {
            if ( WstSBBufferGetFd( sbBuffer ) >= 0 )
            {
               int i;
               uint32_t frameFormat, frameWidth, frameHeight;
               int fd[MAX_TEXTURES];
               int32_t offset[MAX_TEXTURES], stride[MAX_TEXTURES];
               EGLint attr[28];

               frameFormat= WstSBBufferGetFormat( sbBuffer );
               frameWidth= WstSBBufferGetWidth( sbBuffer );
               frameHeight= WstSBBufferGetHeight( sbBuffer );

               for( i= 0; i < MAX_TEXTURES; ++i )
               {
                  fd[i]= WstSBBufferGetPlaneFd( sbBuffer, i );
                  WstSBBufferGetPlaneOffsetAndStride( sbBuffer, i, &offset[i], &stride[i] );
               }

               if ( (surface->bufferWidth != frameWidth) || (surface->bufferHeight != frameHeight) )
               {
                  surface->bufferWidth= frameWidth;
                  surface->bufferHeight= frameHeight;
               }

               for( i= 0; i < MAX_TEXTURES; ++i )
               {
                  if ( surface->eglImage[i] )
                  {
                     renderer->eglDestroyImageKHR( renderer->eglDisplay,
                                                   surface->eglImage[i] );
                     surface->eglImage[i]= 0;
                  }
               }

               switch( frameFormat )
               {
                  case WL_SB_FORMAT_NV12:
                  case WL_SB_FORMAT_NV21:
                     if ( renderer->haveExternalImage )
                     {
                        if ( fd[1] == -1 )
                        {
                           fd[1]= fd[0];
                        }

                        i= 0;
                        attr[i++]= EGL_WIDTH;
                        attr[i++]= frameWidth;
                        attr[i++]= EGL_HEIGHT;
                        attr[i++]= frameHeight;
                        attr[i++]= EGL_LINUX_DRM_FOURCC_EXT;
                        attr[i++]= (frameFormat == WL_SB_FORMAT_NV12 ? DRM_FORMAT_NV12 : DRM_FORMAT_NV21);
                        attr[i++]= EGL_DMA_BUF_PLANE0_FD_EXT;
                        attr[i++]= fd[0];
                        attr[i++]= EGL_DMA_BUF_PLANE0_OFFSET_EXT;
                        attr[i++]= offset[0];
                        attr[i++]= EGL_DMA_BUF_PLANE0_PITCH_EXT;
                        attr[i++]= stride[0];
                        attr[i++]= EGL_DMA_BUF_PLANE1_FD_EXT;
                        attr[i++]= fd[1];
                        attr[i++]= EGL_DMA_BUF_PLANE1_OFFSET_EXT;
                        attr[i++]= offset[1];
                        attr[i++]= EGL_DMA_BUF_PLANE1_PITCH_EXT;
                        attr[i++]= stride[1];
                        attr[i++]= EGL_YUV_COLOR_SPACE_HINT_EXT;
                        attr[i++]= EGL_ITU_REC709_EXT;
                        attr[i++]= EGL_SAMPLE_RANGE_HINT_EXT;
                        attr[i++]= EGL_YUV_FULL_RANGE_EXT;
                        attr[i++]= EGL_NONE;

                        eglImage= renderer->eglCreateImageKHR( renderer->eglDisplay,
                                                               EGL_NO_CONTEXT,
                                                               EGL_LINUX_DMA_BUF_EXT,
                                                               (EGLClientBuffer)NULL,
                                                               attr );
                        if ( eglImage )
                        {
                           surface->eglImage[0]= eglImage;
                           if ( surface->textureId[0] != GL_NONE )
                           {
                              glDeleteTextures( 1, &surface->textureId[0] );
                           }
                           surface->textureId[0]= GL_NONE;
                        }
                        else
                        {
                           printf("wstRendererGLPrepareResource: eglCreateImageKHR failed for fd %d, DRM_FORMAT_NV12: errno %X\n", fd[0], eglGetError());
                        }

                        surface->textureCount= 1;
                        surface->externalImage= true;
                     }
                     else
                     {
                        if ( fd[1] == -1 )
                        {
                           fd[1]= fd[0];
                        }

                        i= 0;
                        attr[i++]= EGL_WIDTH;
                        attr[i++]= frameWidth;
                        attr[i++]= EGL_HEIGHT;
                        attr[i++]= frameHeight;
                        attr[i++]= EGL_LINUX_DRM_FOURCC_EXT;
                        attr[i++]= DRM_FORMAT_R8;
                        attr[i++]= EGL_DMA_BUF_PLANE0_FD_EXT;
                        attr[i++]= fd[0];
                        attr[i++]= EGL_DMA_BUF_PLANE0_OFFSET_EXT;
                        attr[i++]= offset[0];
                        attr[i++]= EGL_DMA_BUF_PLANE0_PITCH_EXT;
                        attr[i++]= stride[0];
                        attr[i++]= EGL_NONE;

                        eglImage= renderer->eglCreateImageKHR( renderer->eglDisplay,
                                                               EGL_NO_CONTEXT,
                                                               EGL_LINUX_DMA_BUF_EXT,
                                                               (EGLClientBuffer)NULL,
                                                               attr );
                        if ( eglImage )
                        {
                           surface->eglImage[0]= eglImage;
                           if ( surface->textureId[0] != GL_NONE )
                           {
                              glDeleteTextures( 1, &surface->textureId[0] );
                           }
                           surface->textureId[0]= GL_NONE;
                        }
                        else
                        {
                           printf("wstRendererGLPrepareResource: eglCreateImageKHR failed for fd %d, DRM_FORMAT_R8: errno %X\n", fd[0], eglGetError());
                        }

                        i= 0;
                        attr[i++]= EGL_WIDTH;
                        attr[i++]= frameWidth/2;
                        attr[i++]= EGL_HEIGHT;
                        attr[i++]= frameHeight/2;
                        attr[i++]= EGL_LINUX_DRM_FOURCC_EXT;
                        attr[i++]= (frameFormat == WL_SB_FORMAT_NV12 ? DRM_FORMAT_GR88 : DRM_FORMAT_RG88);
                        attr[i++]= EGL_DMA_BUF_PLANE0_FD_EXT;
                        attr[i++]= fd[1];
                        attr[i++]= EGL_DMA_BUF_PLANE0_OFFSET_EXT;
                        attr[i++]= offset[1];
                        attr[i++]= EGL_DMA_BUF_PLANE0_PITCH_EXT;
                        attr[i++]= stride[1];
                        attr[i++]= EGL_NONE;

                        eglImage= renderer->eglCreateImageKHR( renderer->eglDisplay,
                                                               EGL_NO_CONTEXT,
                                                               EGL_LINUX_DMA_BUF_EXT,
                                                               (EGLClientBuffer)NULL,
                                                               attr );
                        if ( eglImage )
                        {
                           surface->eglImage[1]= eglImage;
                           if ( surface->textureId[1] != GL_NONE )
                           {
                              glDeleteTextures( 1, &surface->textureId[1] );
                           }
                           surface->textureId[1]= GL_NONE;
                        }
                        else
                        {
                           printf("wstRendererGLPrepareResource: eglCreateImageKHR failed for fd %d, DRM_FORMAT_GR88: errno %X\n", fd[1], eglGetError());
                        }

                        surface->textureCount= 2;
                     }
                     break;
                  default:
                     printf("wstRendererGLPrepareResource: unsuppprted texture format: %x\n", frameFormat );
                     break;
               }
            }
         }
         #endif
      }
      #endif
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
      #ifdef EGL_LINUX_DMA_BUF_EXT
      else if ( rendererGL->haveDmaBufImport )
      {
         int fd= WstSBBufferGetFd( sbBuffer );
         if ( fd >= 0 )
         {
            //surface->resource= resource;
            wstRendererGLPrepareResource( rendererGL, surface, resource );
         }
      }
      #endif
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

static void wstRendererGLRenderSurface( WstRendererGL *renderer, WstRenderSurface *surface )
{
   if ( (surface->textureId[0] == GL_NONE) || surface->memDirty || surface->externalImage )
   {
      for ( int i= 0; i < surface->textureCount; ++i )
      {
         if ( surface->textureId[i] == GL_NONE )
         {
            glGenTextures(1, &surface->textureId[i] );
         }
       
         /* Bind the egl image as a texture */
         glActiveTexture(GL_TEXTURE1+i);
         glBindTexture(GL_TEXTURE_2D, surface->textureId[i] );
         #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
         if ( surface->eglImage[i] && renderer->eglContext )
         {
            #ifdef GL_OES_EGL_image_external
            if ( surface->externalImage )
            {
               renderer->glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, surface->eglImage[i]);
            }
            else
            {
            #endif
               renderer->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, surface->eglImage[i]);
            #ifdef GL_OES_EGL_image_external
            }
            #endif
         }
         else
         #endif
         if ( i == 0 )
         {
            if ( surface->mem )
            {
               glTexImage2D( GL_TEXTURE_2D,
                             0, //level
                             surface->memFormatGL, //internalFormat
                             surface->memWidth,
                             surface->memHeight,
                             0, // border
                             surface->memFormatGL, //format
                             surface->memType,
                             surface->mem );
               surface->memDirty= false;
            }
         }
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      }
   }

   if ( !surface->sizeOverride )
   {
      surface->width= surface->bufferWidth;
      surface->height= surface->bufferHeight;
   }

   const float verts[4][2] =
   {
      { float(surface->x), float(surface->y) },
      { float(surface->x+surface->width), float(surface->y) },
      { float(surface->x), float(surface->y+surface->height) },
      { float(surface->x+surface->width), float(surface->y+surface->height) }
   };

   const float uvNormal[4][2] =
   {
      { 0,  0 },
      { 1,  0 },
      { 0,  1 },
      { 1,  1 }
   };

   const float uvYInverted[4][2] =
   {
      { 0,  1 },
      { 1,  1 },
      { 0,  0 },
      { 1,  0 }
   };

   const float identityMatrix[4][4] =
   {
      {1, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {0, 0, 0, 1}
   };

   const float *uv= surface->invertedY ? (const float*)uvYInverted : (const float*)uvNormal;

   float *matrix= (float*)identityMatrix;

   int resW, resH;
   GLint viewport[4];

   resW= renderer->renderer->outputWidth;
   resH= renderer->renderer->outputHeight;

   if ( surface->textureCount == 1 )
   {
      wstRendererGLShaderDraw( surface->externalImage ? renderer->textureShaderExternal : renderer->textureShader,
                               resW,
                               resH,
                               (float*)matrix,
                               surface->opacity,
                               surface->textureId[0],
                               GL_NONE,
                               4,
                               (const float*)verts,
                               (const float*)uv );
   }
   else
   {
      wstRendererGLShaderDraw( renderer->textureShaderYUV,
                               resW,
                               resH,
                               (float*)matrix,
                               surface->opacity,
                               surface->textureId[0],
                               surface->textureId[1],
                               4,
                               (const float*)verts,
                               (const float*)uv );
   }
}

#define RED_SIZE (8)
#define GREEN_SIZE (8)
#define BLUE_SIZE (8)
#define ALPHA_SIZE (8)
#define DEPTH_SIZE (0)

static bool wstRendererGLSetupEGL( WstRendererGL *renderer )
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
   printf("wstRendererGLSetupEGL: eglDisplay=%p\n", renderer->eglDisplay );
   if ( renderer->eglDisplay == EGL_NO_DISPLAY )
   {
      printf("wstRendererGLSetupEGL: EGL not available\n" );
      goto exit;
   }

   // Initialize display
   b= eglInitialize( renderer->eglDisplay, &major, &minor );
   if ( !b )
   {
      printf("wstRendererGLSetupEGL: unable to initialize EGL display\n" );
      goto exit;
   }
   printf("wstRendererGLSetupEGL: eglInitiialize: major: %d minor: %d\n", major, minor );

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
      printf("wstRendererGLSetupEGL: unable to alloc memory for EGL configurations\n");
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
           (depthSize >= DEPTH_SIZE) )
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

   #ifndef WESTEROS_PLATFORM_QEMUX86
   if ( renderer->nativeWindow )
   #endif
   {
      // Create an EGL window surface
      renderer->eglSurface= eglCreateWindowSurface( renderer->eglDisplay, 
                                                    renderer->eglConfig, 
                                                    (EGLNativeWindowType)renderer->nativeWindow,
                                                    NULL );
      printf("wstRendererGLSetupEGL: eglSurface %p\n", renderer->eglSurface );
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
      free( eglConfigs );
      eglConfigs= 0;
   }

   return result;
}

static WstShader* wstRendererGLCreateShader( WstRendererGL *renderer, int shaderType )
{
   WstShader *shaderNew= 0;
   GLuint type;
   const char *typeName= 0, *src= 0;
   GLint shader, status, len;
   bool yuv= (shaderType == WstShaderType_yuv);
   bool noalpha;

   shaderNew= (WstShader*)calloc( 1, sizeof(WstShader));
   if ( !shaderNew )
   {
      printf("wstRendererGLCreateShader: failed to allocate WstShader\n");
      goto exit;
   }

   shaderNew->isYUV= yuv;
   shaderNew->program= GL_NONE;
   shaderNew->fragShader= GL_NONE;
   shaderNew->vertShader= GL_NONE;
   shaderNew->uniRes= -1;
   shaderNew->uniMatrix= -1;
   shaderNew->uniAlpha= -1;
   shaderNew->uniTexture= -1;
   shaderNew->uniTextureuv= -1;

   for( int i= 0; i < 2; ++i )
   {
      if ( i == 0 )
      {
         type= GL_FRAGMENT_SHADER;
         typeName= "fragment";
         noalpha= true;
         if ( yuv )
         {
            src= (renderer->haveDmaBufImport ? fShaderText_Y_UV : fShaderTextYUV);
         }
         else if ( shaderType == WstShaderType_external )
         {
            src= fShaderTextExternal;
         }
         else
         {
            src= fShaderText;
            noalpha= false;
         }
      }
      else
      {
         type= GL_VERTEX_SHADER;
         typeName= "vertex";
         src= ( yuv ? vShaderTextYUV : vShaderText );
      }
      shader= glCreateShader(type);
      if ( !shader )
      {
         printf("wstRendererGLCreateShader: glCreateShader (%s) error: %d\n", typeName, glGetError());
         goto exit;
      }
      glShaderSource(shader, 1, &src, NULL );
      glCompileShader(shader);
      glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
      if ( !status )
      {
         glGetShaderInfoLog(shader, sizeof(message), &len, message);
         printf("wstRendererGLCreateShader: %s shader compile error: (%s)\n", typeName, message);
         goto exit;
      }
      if ( i == 0 )
         shaderNew->fragShader= shader;
      else
         shaderNew->vertShader= shader;
   }

   shaderNew->program= glCreateProgram();
   if ( shaderNew->program == GL_NONE )
   {
      printf("wstRendererGLCreateShader: glCreateProgram error %d\n", glGetError());
      goto exit;
   }

   glAttachShader(shaderNew->program, shaderNew->fragShader);
   glAttachShader(shaderNew->program, shaderNew->vertShader);

   shaderNew->attrPos= 0;
   glBindAttribLocation(shaderNew->program, shaderNew->attrPos, "pos");
   shaderNew->attrTexcoord= 1;
   glBindAttribLocation(shaderNew->program, shaderNew->attrTexcoord, "texcoord");
   if ( yuv )
   {
      shaderNew->attrTexcoorduv= 2;
      glBindAttribLocation(shaderNew->program, shaderNew->attrTexcoorduv, "texcoorduv");
   }

   glLinkProgram(shaderNew->program);
   glGetProgramiv(shaderNew->program, GL_LINK_STATUS, &status);
   if ( !status )
   {
      glGetProgramInfoLog(shaderNew->program, sizeof(message), &len, message);
      printf("wstRendererGLCreateShader: %s shader link error: (%s)\n", typeName, message);
      goto exit;
   }

   shaderNew->uniRes= glGetUniformLocation(shaderNew->program, "resolution");
   if ( shaderNew->uniRes == -1 )
   {
      printf("wstRendererGLCreateShader: uniformn 'resolution' location error\n");
      goto exit;
   }

   shaderNew->uniMatrix= glGetUniformLocation(shaderNew->program, "matrix");
   if ( shaderNew->uniMatrix == -1 )
   {
      printf("wstRendererGLCreateShader: uniformn 'matrix' location error\n");
      goto exit;
   }

   shaderNew->uniAlpha= glGetUniformLocation(shaderNew->program, "alpha");
   if ( (shaderNew->uniAlpha == -1) && !noalpha )
   {
      printf("wstRendererGLCreateShader: uniformn 'alpha' location error\n");
      goto exit;
   }

   shaderNew->uniTexture= glGetUniformLocation(shaderNew->program, "texture");
   if ( shaderNew->uniTexture == -1 )
   {
      printf("wstRendererGLCreateShader: uniformn 'texture' location error\n");
      goto exit;
   }

   if ( yuv )
   {
      shaderNew->uniTextureuv= glGetUniformLocation(shaderNew->program, "textureuv");
      if ( shaderNew->uniTextureuv == -1 )
      {
         printf("wstRendererGLCreateShader: uniformn 'textureuv' location error\n");
         goto exit;
      }
   }

exit:

   return shaderNew;
}

static void wstRendererGLDestroyShader( WstShader *shader )
{
   if ( shader )
   {
      if ( shader->program != GL_NONE )
      {
         if ( shader->fragShader != GL_NONE )
         {
            glDetachShader( shader->program, shader->fragShader );
            glDeleteShader( shader->fragShader );
            shader->fragShader= GL_NONE;
         }
         if ( shader->vertShader != GL_NONE )
         {
            glDetachShader( shader->program, shader->vertShader );
            glDeleteShader( shader->vertShader );
            shader->vertShader= GL_NONE;
         }
         glDeleteProgram( shader->program );
         shader->program= GL_NONE;
      }
      free( shader );
   }
}

static void wstRendererGLShaderDraw( WstShader *shader,
                                      int width, int height, float* matrix, float alpha,
                                      GLuint textureId, GLuint textureUVId,
                                      int count, const float* vc, const float* txc )
{
    glUseProgram( shader->program );
    glUniformMatrix4fv( shader->uniMatrix, 1, GL_FALSE, matrix );
    glUniform2f( shader->uniRes, width, height );
    if ( shader->uniAlpha != -1 )
    {
       glUniform1f( shader->uniAlpha, alpha );
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture( GL_TEXTURE_2D, textureId );
    glUniform1i( shader->uniTexture, 1 );
    if ( shader->isYUV )
    {
       glActiveTexture(GL_TEXTURE2);
       glBindTexture( GL_TEXTURE_2D, textureUVId );
       glUniform1i( shader->uniTextureuv, 2 );
    }
    glVertexAttribPointer( shader->attrPos, 2, GL_FLOAT, GL_FALSE, 0, vc );
    glVertexAttribPointer( shader->attrTexcoord, 2, GL_FLOAT, GL_FALSE, 0, txc );
    if ( shader->isYUV )
    {
       glVertexAttribPointer( shader->attrTexcoorduv, 2, GL_FLOAT, GL_FALSE, 0, txc );
    }
    glEnableVertexAttribArray( shader->attrPos );
    glEnableVertexAttribArray( shader->attrTexcoord );
    if ( shader->isYUV )
    {
       glEnableVertexAttribArray( shader->attrTexcoorduv );
    }
    glDrawArrays( GL_TRIANGLE_STRIP, 0, count );
    glDisableVertexAttribArray( shader->attrPos );
    glDisableVertexAttribArray( shader->attrTexcoord );
    if ( shader->isYUV )
    {
       glDisableVertexAttribArray( shader->attrTexcoorduv );
    }
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

   if ( emitFPS )
   {
      static int frameCount= 0;
      static long long lastReportTime= -1LL;
      struct timeval tv;
      long long now;
      gettimeofday(&tv,0);
      now= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);
      ++frameCount;
      if ( lastReportTime == -1LL ) lastReportTime= now;
      if ( now-lastReportTime > 5000 )
      {
         double fps= ((double)frameCount*1000)/((double)(now-lastReportTime));
         printf("westeros-render-gl: fps %f\n", fps);
         lastReportTime= now;
         frameCount= 0;
      }
   }

   if ( rendererGL->eglSurface == EGL_NO_SURFACE ) return;

   if ( (renderer->outputWidth != rendererGL->outputWidth) ||
        (renderer->outputHeight != rendererGL->outputHeight) )
   {
      rendererGL->outputWidth= renderer->outputWidth;
      rendererGL->outputHeight= renderer->outputHeight;
   }

   eglMakeCurrent( rendererGL->eglDisplay, 
                   rendererGL->eglSurface, 
                   rendererGL->eglSurface, 
                   rendererGL->eglContext );

   if ( !rendererGL->textureShader )
   {
      rendererGL->textureShader= wstRendererGLCreateShader( rendererGL, WstShaderType_rgb );
      rendererGL->textureShaderYUV= wstRendererGLCreateShader( rendererGL, WstShaderType_yuv );
      if ( rendererGL->haveExternalImage )
      {
         rendererGL->textureShaderExternal= wstRendererGLCreateShader( rendererGL, WstShaderType_external );
      }
      rendererGL->eglContext= eglGetCurrentContext();
   }

   glViewport( 0, 0, renderer->outputWidth, renderer->outputHeight );
   glClearColor( 0.0, 0.0, 0.0, 0.0 );
   glClear( GL_COLOR_BUFFER_BIT );
   
   glEnable(GL_BLEND);
   glBlendColor(0,0,0,0);
   glDisable(GL_STENCIL_TEST);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE);
   glDisable(GL_SCISSOR_TEST);
   glBlendFuncSeparate( GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE );

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
         wstRendererGLRenderSurface( rendererGL, surface );
      }
   }
 
   #if defined (WESTEROS_PLATFORM_NEXUS )
   {
      static bool needFinish= (getenv("WAYLAND_EGL_BNXS_ZEROCOPY") == NULL);
      // The calls to glFlush/glFinish are not required except on the Broadcom Nexus platform
      // when older versions of wayland-egl-bnxs are being used.  This code will be removed
      // in the near future.
      if ( needFinish )
      {
         glFlush();
         glFinish();
      }
   }
   #endif

   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   eglSwapBuffers(rendererGL->eglDisplay, rendererGL->eglSurface);
   #endif
}

static WstRenderSurface* wstRendererSurfaceCreate( WstRenderer *renderer )
{
   WstRenderSurface *surface;
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;

   surface= wstRendererGLCreateSurface(rendererGL);
   
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
   
   wstRendererGLDestroySurface( rendererGL, surface );
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
      if ( (width != surface->width) || (height != surface->height) )
      {
         surface->sizeOverride= true;
      }
      surface->x= x;
      surface->y= y;
      surface->width= width;
      surface->height= height;
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

#ifndef WESTEROS_PLATFORM_QEMUX86
static void wstRendererResolutionChangeBegin( WstRenderer *renderer )
{
   if ( !renderer->displayNested )
   {
      WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;

      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      #ifdef _WESTEROS_GL_ICEGDL_
      rendererGL->eglSurfaceSave= rendererGL->eglSurface;
      rendererGL->eglSurface= EGL_NO_SURFACE;
      #else
      eglMakeCurrent( rendererGL->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
      if ( rendererGL->eglSurface )
      {
         eglDestroySurface( rendererGL->eglDisplay, rendererGL->eglSurface );
         rendererGL->eglSurface= EGL_NO_SURFACE;
      }
      #endif
      if ( rendererGL->nativeWindow )
      {
         WstGLDestroyNativeWindow( rendererGL->glCtx, rendererGL->nativeWindow );
         rendererGL->nativeWindow= 0;
      }
      #endif
   }
}

static void wstRendererResolutionChangeEnd( WstRenderer *renderer )
{
   if ( !renderer->displayNested )
   {
      WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;

      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      rendererGL->nativeWindow= WstGLCreateNativeWindow( rendererGL->glCtx, 0, 0, renderer->outputWidth, renderer->outputHeight );
      #ifdef _WESTEROS_GL_ICEGDL_
      rendererGL->eglSurface= rendererGL->eglSurfaceSave;
      rendererGL->eglSurfaceSave= EGL_NO_SURFACE;
      #else
      if ( rendererGL->nativeWindow )
      {
         // Create an EGL window surface
         rendererGL->eglSurface= eglCreateWindowSurface( rendererGL->eglDisplay,
                                                         rendererGL->eglConfig,
                                                         (EGLNativeWindowType)rendererGL->nativeWindow,
                                                         NULL );
      }
      #endif
      #endif
   }
}
#endif

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
      #ifndef WESTEROS_PLATFORM_QEMUX86
      renderer->resolutionChangeBegin= wstRendererResolutionChangeBegin;
      renderer->resolutionChangeEnd= wstRendererResolutionChangeEnd;
      #endif
   }
   else
   {
      rc= -1;
   }

exit:
   
   return 0;
}

}

