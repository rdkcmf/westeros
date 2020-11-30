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
#include <dlfcn.h>
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

#ifdef ENABLE_LDBPROTOCOL
#include "linux-dmabuf/westeros-linux-dmabuf.h"
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
static bool emitFPS= false;


#define MAX_TEXTURES (2)

struct _WstRenderSurface
{
   int textureCount;
   bool externalImage;
   GLuint textureId[MAX_TEXTURES];

   int bufferWidth;
   int bufferHeight;
   
   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   void *nativePixmap;
   EGLImageKHR eglImage[MAX_TEXTURES];
   #endif

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
   
   bool dirty;
   bool invertedY;
   
   WstRenderSurface *surfaceFast;
};

typedef struct _WstRendererEMB
{
   WstRenderer *renderer;
   int outputWidth;
   int outputHeight;

   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   WstGLCtx *glCtx;
   #endif

   void *nativeWindow;

   EGLDisplay eglDisplay;
   EGLContext eglContext;   

   PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
   PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
   #endif

   bool haveDmaBufImport;
   bool haveDmaBufImportModifiers;
   bool haveExternalImage;

   #if defined (WESTEROS_HAVE_WAYLAND_EGL)
   bool haveWaylandEGL;
   PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
   PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
   PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
   #endif
   #ifdef ENABLE_LDBPROTOCOL
   PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT;
   PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT;
   #endif

   WstShader *textureShader;
   WstShader *textureShaderYUV;
   WstShader *textureShaderExternal;
   
   std::vector<WstRenderSurface*> surfaces;
   std::vector<GLuint> deadTextures;

   bool fastPathActive;   
   WstRenderer *rendererFast;

} WstRendererEMB;


static WstRendererEMB* wstRendererEMBCreate( WstRenderer *renderer );
static void wstRendererEMBDestroy( WstRendererEMB *renderer );
static WstRenderSurface *wstRendererEMBCreateSurface(WstRendererEMB *renderer);
static void wstRendererEMBDestroySurface( WstRendererEMB *renderer, WstRenderSurface *surface );
static void wstRendererEMBFlushSurface( WstRendererEMB *renderer, WstRenderSurface *surface );
static void wstRendererEMBPrepareResource( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource);
static void wstRendererEMBCommitShm( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource );
#if defined (WESTEROS_HAVE_WAYLAND_EGL)
static void wstRendererEMBCommitWaylandEGL( WstRendererEMB *renderer, WstRenderSurface *surface, 
                                           struct wl_resource *resource, EGLint format );
#endif
#ifdef ENABLE_SBPROTOCOL
static void wstRendererEMBCommitSB( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource );
#endif
#ifdef ENABLE_LDBPROTOCOL
static void wstRendererEMBCommitLDB( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource );
#endif
#if defined (WESTEROS_PLATFORM_RPI)
static void wstRendererEMBCommitDispmanx( WstRendererEMB *renderer, WstRenderSurface *surface, 
                                         DISPMANX_RESOURCE_HANDLE_T dispResource,
                                         EGLint format, int bufferWidth, int bufferHeight );
#endif                                         
static void wstRendererEMBRenderSurface( WstRendererEMB *renderer, WstRenderSurface *surface );
static WstShader* wstRendererEMBCreateShader( WstRendererEMB *renderer, int shaderType );
static void wstRendererEMBDestroyShader( WstShader *shader );
static void wstRendererEMBShaderDraw( WstShader *shader,
                                      int width, int height, float* matrix, float alpha,
                                      GLuint textureId, GLuint textureUVId,
                                      int count, const float* vc, const float* txc );
static void wstRendererHolePunch( WstRenderer *renderer, int x, int y, int width, int height );
static void wstRendererInitFastPath( WstRendererEMB *renderer );
static bool wstRendererActivateFastPath( WstRendererEMB *renderer );
static void wstRendererDeactivateFastPath( WstRendererEMB *renderer );
static void wstRendererDeleteTexture( WstRendererEMB *renderer, GLuint textureId );
static void wstRendererProcessDeadTextures( WstRendererEMB *renderer );

#define RED_SIZE (8)
#define GREEN_SIZE (8)
#define BLUE_SIZE (8)
#define ALPHA_SIZE (8)
#define DEPTH_SIZE (0)

static WstRendererEMB* wstRendererEMBCreate( WstRenderer *renderer )
{
   WstRendererEMB *rendererEMB= 0;

   rendererEMB= (WstRendererEMB*)calloc(1, sizeof(WstRendererEMB) );
   if ( rendererEMB )
   {
      if ( getenv("WESTEROS_RENDER_EMBEDDED_FPS" ) )
      {
         emitFPS= true;
      }

      rendererEMB->outputWidth= renderer->outputWidth;
      rendererEMB->outputHeight= renderer->outputHeight;
      
      rendererEMB->renderer= renderer;
      rendererEMB->surfaces= std::vector<WstRenderSurface*>();
      rendererEMB->deadTextures= std::vector<GLuint>();
      
      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      rendererEMB->glCtx= WstGLInit();
      if ( !rendererEMB->glCtx )
      {
         free( rendererEMB );
         rendererEMB= 0;
         goto exit;
      }
      #endif

      rendererEMB->eglCreateImageKHR= (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
      WST_TRACE( "eglCreateImageKHR %p\n", rendererEMB->eglCreateImageKHR);

      rendererEMB->eglDestroyImageKHR= (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
      WST_TRACE( "eglDestroyImageKHR %p\n", rendererEMB->eglDestroyImageKHR);

      #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
      rendererEMB->glEGLImageTargetTexture2DOES= (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
      WST_TRACE( "glEGLImageTargetTexture2DOES %p\n", rendererEMB->glEGLImageTargetTexture2DOES);
      #endif

      rendererEMB->eglDisplay= eglGetCurrentDisplay();
      if ( rendererEMB->eglDisplay == EGL_NO_DISPLAY )
      {
         rendererEMB->eglDisplay= eglGetDisplay(EGL_DEFAULT_DISPLAY);
         fprintf(stderr,"no current eglDisplay, get eglDisplay %p\n", rendererEMB->eglDisplay);
      }

      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      const char *extensions= eglQueryString( rendererEMB->eglDisplay, EGL_EXTENSIONS );
      if ( extensions )
      {
         if ( !strstr( extensions, "EGL_WL_bind_wayland_display" ) )
         {
            printf("wayland-egl support expected, but not advertised by eglQueryString(eglDisplay,EGL_EXTENSIONS): not attempting to use\n" );
         }
         else
         {
            printf("wayland-egl support expected, and is advertised by eglQueryString(eglDisplay,EGL_EXTENSIONS): proceeding to use \n" );
         
            rendererEMB->eglBindWaylandDisplayWL= (PFNEGLBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglBindWaylandDisplayWL");
            printf( "eglBindWaylandDisplayWL %p\n", rendererEMB->eglBindWaylandDisplayWL );

            rendererEMB->eglUnbindWaylandDisplayWL= (PFNEGLUNBINDWAYLANDDISPLAYWL)eglGetProcAddress("eglUnbindWaylandDisplayWL");
            printf( "eglUnbindWaylandDisplayWL %p\n", rendererEMB->eglUnbindWaylandDisplayWL );

            rendererEMB->eglQueryWaylandBufferWL= (PFNEGLQUERYWAYLANDBUFFERWL)eglGetProcAddress("eglQueryWaylandBufferWL");
            printf( "eglQueryWaylandBufferWL %p\n", rendererEMB->eglQueryWaylandBufferWL );
            
            if ( rendererEMB->eglBindWaylandDisplayWL &&
                 rendererEMB->eglUnbindWaylandDisplayWL &&
                 rendererEMB->eglQueryWaylandBufferWL )
            {               
               printf("calling eglBindWaylandDisplayWL with eglDisplay %p and wayland display %p\n", rendererEMB->eglDisplay, renderer->display );
               EGLBoolean rc= rendererEMB->eglBindWaylandDisplayWL( rendererEMB->eglDisplay, renderer->display );
               if ( rc )
               {
                  rendererEMB->haveWaylandEGL= true;
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
            rendererEMB->haveDmaBufImport= true;
         }
         if ( strstr( extensions, "EGL_EXT_image_dma_buf_import_modifiers" ) )
         {
            rendererEMB->haveDmaBufImportModifiers= true;
            #ifdef ENABLE_LDBPROTOCOL
            rendererEMB->eglQueryDmaBufFormatsEXT = (PFNEGLQUERYDMABUFFORMATSEXTPROC)eglGetProcAddress("eglQueryDmaBufFormatsEXT");
            printf( "eglQueryDmaBufFormatsEXT %p\n", rendererEMB->eglQueryDmaBufFormatsEXT );
            rendererEMB->eglQueryDmaBufModifiersEXT = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
            printf( "eglQueryDmaBufModifiersEXT %p\n", rendererEMB->eglQueryDmaBufModifiersEXT );
            #endif
         }
      }
      extensions= (const char *)glGetString(GL_EXTENSIONS);
      if ( extensions )
      {
         #ifdef GL_OES_EGL_image_external
         if ( strstr( extensions, "GL_OES_EGL_image_external" ) )
         {
            rendererEMB->haveExternalImage= true;
         }
         #endif
      }
      printf("have wayland-egl: %d\n", rendererEMB->haveWaylandEGL );
      printf("have dmabuf import: %d\n", rendererEMB->haveDmaBufImport );
      printf("have dmabuf import modifiers: %d\n", rendererEMB->haveDmaBufImportModifiers );
      printf("have external image: %d\n", rendererEMB->haveExternalImage );
      #endif
   }

exit:
   
   return rendererEMB;
}

static void wstRendererEMBDestroy( WstRendererEMB *renderer )
{
   if ( renderer )
   {
      wstRendererProcessDeadTextures( renderer );
      
      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      if ( renderer->haveWaylandEGL )
      {
         renderer->eglUnbindWaylandDisplayWL( renderer->eglDisplay, renderer->renderer->display );
         renderer->haveWaylandEGL= false;
      }
      #endif

      if ( renderer->textureShader )
      {
         wstRendererEMBDestroyShader( renderer->textureShader );
         renderer->textureShader= 0;
      }
      if ( renderer->textureShaderYUV )
      {
         wstRendererEMBDestroyShader( renderer->textureShaderYUV );
         renderer->textureShaderYUV= 0;
      }
      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      if ( renderer->glCtx )
      {
         WstGLTerm( renderer->glCtx );
         renderer->glCtx= 0;
      }
      #endif
      if ( renderer->rendererFast )
      {
         renderer->rendererFast->renderTerm( renderer->rendererFast );
         free( renderer->rendererFast );
         renderer->rendererFast= 0;
      }
      free( renderer );
   }
}

static WstRenderSurface *wstRendererEMBCreateSurface(WstRendererEMB *renderer)
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
        
        surface->dirty= true;
        
        if ( renderer->fastPathActive )
        {
            surface->surfaceFast= renderer->rendererFast->surfaceCreate( renderer->rendererFast );
            if ( !surface->surfaceFast )
            {
               wstRendererDeactivateFastPath( renderer );
            }
        }
    }
   
    return surface;
}

static void wstRendererEMBDestroySurface( WstRendererEMB *renderer, WstRenderSurface *surface )
{
    if ( surface )
    {
        wstRendererEMBFlushSurface( renderer, surface );
        if ( renderer->rendererFast )
        {
            if ( surface->surfaceFast )
            {
               renderer->rendererFast->surfaceDestroy( renderer->rendererFast, surface->surfaceFast );
               surface->surfaceFast= 0;
            }
        }
        free( surface );
    }
}

static void wstRendererEMBFlushSurface( WstRendererEMB *renderer, WstRenderSurface *surface )
{
    if ( surface )
    {
        for( int i= 0; i < MAX_TEXTURES; ++i )
        {
           if ( surface->textureId[i] )
           {
              wstRendererDeleteTexture( renderer, surface->textureId[i] );
              surface->textureId[i]= GL_NONE;
           }
           #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
           if ( surface->eglImage[i] )
           {
               renderer->eglDestroyImageKHR( renderer->eglDisplay,
                                             surface->eglImage[i] );
               surface->eglImage[i]= 0;
           }
           #endif
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
           surface->mem= 0;
        }
    }
}

static void wstRendererEMBPrepareResource( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource )
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
                           printf("wstRendererEMBPrepareResource: eglCreateImageKHR failed for fd %d, DRM_FORMAT_NV12: errno %X\n", fd[0], eglGetError());
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
                              wstRendererDeleteTexture( renderer, surface->textureId[0] );
                           }
                           surface->textureId[0]= GL_NONE;
                        }
                        else
                        {
                           printf("wstRendererEMBPrepareResource: eglCreateImageKHR failed for fd %d, DRM_FORMAT_R8: errno %X\n", fd[0], eglGetError());
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
                              wstRendererDeleteTexture( renderer, surface->textureId[1] );
                           }
                           surface->textureId[1]= GL_NONE;
                        }
                        else
                        {
                           printf("wstRendererEMBPrepareResource: eglCreateImageKHR failed for fd %d, DRM_FORMAT_GR88: errno %X\n", fd[1], eglGetError());
                        }

                        surface->textureCount= 2;
                     }
                     break;
                  default:
                     printf("wstRendererEMBPrepareResource: unsuppprted texture format: %x\n", frameFormat );
                     break;
               }
            }
         }
         #endif
      }
      #endif
   }
}

static void wstRendererEMBCommitShm( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource )
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
               if ( renderer->haveWaylandEGL )
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
               if ( renderer->haveWaylandEGL )
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
               if ( renderer->haveWaylandEGL )
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
               if ( renderer->haveWaylandEGL )
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
static void wstRendererEMBCommitWaylandEGL( WstRendererEMB *renderer, WstRenderSurface *surface, 
                                           struct wl_resource *resource, EGLint format )
{
   EGLImageKHR eglImage= 0;
   EGLint value;
   EGLint attrList[3];
   int bufferWidth= 0, bufferHeight= 0;

   if (EGL_TRUE == renderer->eglQueryWaylandBufferWL( renderer->eglDisplay,
                                                      resource,
                                                      EGL_WIDTH,
                                                      &value ) )
   {
      bufferWidth= value;
   }                                                        

   if (EGL_TRUE == renderer->eglQueryWaylandBufferWL( renderer->eglDisplay,
                                                      resource,
                                                      EGL_HEIGHT,
                                                      &value ) )
   {
      bufferHeight= value;
   }                                                        
   
   #if defined (WESTEROS_PLATFORM_RPI)
   /* 
    * The Userland wayland-egl implementation used on RPI isn't complete in that it does not
    * support the use of eglCreateImageKHR using the wl_buffer resource and target EGL_WAYLAND_BUFFER_WL.
    * For that reason we need to supply a different method for handling buffers received via
    * wayland-egl on RPI
    */
   {
      DISPMANX_RESOURCE_HANDLE_T dispResource= vc_dispmanx_get_handle_from_wl_buffer(resource);
      if ( dispResource != DISPMANX_NO_HANDLE )
      {
         wstRendererEMBCommitDispmanx( renderer, surface, dispResource, format, bufferWidth, bufferHeight );
      }
   }
   #else
   if ( (surface->bufferWidth != bufferWidth) || (surface->bufferHeight != bufferHeight) )
   {
      surface->bufferWidth= bufferWidth;
      surface->bufferHeight= bufferHeight;
   }

   for( int i= 0; i < MAX_TEXTURES; ++i )
   {
      if ( surface->eglImage[i] )
      {
         renderer->eglDestroyImageKHR( renderer->eglDisplay,
                                       surface->eglImage[i] );
         surface->eglImage[i]= 0;
      }
   }

   switch ( format )
   {
      case EGL_TEXTURE_RGB:
      case EGL_TEXTURE_RGBA:
         eglImage= renderer->eglCreateImageKHR( renderer->eglDisplay,
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
               wstRendererDeleteTexture( renderer, surface->textureId[0] );
            }
            surface->textureId[0]= GL_NONE;
            surface->textureCount= 1;
         }
         break;
      
      case EGL_TEXTURE_Y_U_V_WL:
         printf("wstRendererEMBCommitWaylandEGL: EGL_TEXTURE_Y_U_V_WL not supported\n" );
         break;
       
      case EGL_TEXTURE_Y_UV_WL:
         attrList[0]= EGL_WAYLAND_PLANE_WL;
         attrList[2]= EGL_NONE;
         for( int i= 0; i < 2; ++i )
         {
            attrList[1]= i;
            
            eglImage= renderer->eglCreateImageKHR( renderer->eglDisplay,
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
                  wstRendererDeleteTexture( renderer, surface->textureId[i] );
               }
               surface->textureId[i]= GL_NONE;
            }
         }
         surface->textureCount= 2;
         break;
         
      case EGL_TEXTURE_Y_XUXV_WL:
         printf("wstRendererEMBCommitWaylandEGL: EGL_TEXTURE_Y_XUXV_WL not supported\n" );
         break;
         
      default:
         printf("wstRendererEMBCommitWaylandEGL: unknown texture format: %x\n", format );
         break;
   }
   #endif
   #if WESTEROS_INVERTED_Y
   surface->invertedY= true;
   #endif
}
#endif                                           

#ifdef ENABLE_SBPROTOCOL
static void wstRendererEMBCommitSB( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource )
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
            wstRendererEMBCommitDispmanx( renderer, surface, dispResource, format, bufferWidth, bufferHeight );
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
            eglPixmap = (EGLNativePixmapType) WstGLGetEGLNativePixmap(renderer->glCtx, surface->nativePixmap);
         }
         if ( WstGLGetNativePixmap( renderer->glCtx, deviceBuffer, &surface->nativePixmap ) )
         {
            WstGLGetNativePixmapDimensions( renderer->glCtx, surface->nativePixmap, &bufferWidth, &bufferHeight );
            if ( (surface->bufferWidth != bufferWidth) || (surface->bufferHeight != bufferHeight) )
            {
               surface->bufferWidth= bufferWidth;
               surface->bufferHeight= bufferHeight;
               resize= true;
            }
            
            if ( resize || (eglPixmap != (EGLNativePixmapType) WstGLGetEGLNativePixmap(renderer->glCtx, surface->nativePixmap)) )
            {
               /*
                * If the eglPixmap contained by the surface WstGLNativePixmap changed
                * (because the attached buffer dimensions changed, for example) then we
                * need to create a new texture
                */
               if ( surface->eglImage[0] )
               {
                  renderer->eglDestroyImageKHR( renderer->eglDisplay,
                                                  surface->eglImage[0] );
                  surface->eglImage[0]= 0;
               }
               eglPixmap = (EGLNativePixmapType) WstGLGetEGLNativePixmap(renderer->glCtx, surface->nativePixmap);
            }
            if ( !surface->eglImage[0] )
            {
               eglImage= renderer->eglCreateImageKHR( renderer->eglDisplay,
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
                     wstRendererDeleteTexture( renderer, surface->textureId[0] );
                  }
                  surface->textureId[0]= GL_NONE;
               }
            }
         }
      }
      #ifdef EGL_LINUX_DMA_BUF_EXT
      else if ( renderer->haveDmaBufImport )
      {
         int fd= WstSBBufferGetFd( sbBuffer );
         if ( fd >= 0 )
         {
            wstRendererEMBPrepareResource( renderer, surface, resource );
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

#ifdef ENABLE_LDBPROTOCOL
static void wstRendererEMBPrepareResourceLDB( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   if ( surface && resource )
   {
      EGLImageKHR eglImage= 0;

      struct wl_ldb_buffer *ldbBuffer;
      ldbBuffer= WstLDBBufferGet( resource );
      if ( ldbBuffer )
      {
         #ifdef EGL_LINUX_DMA_BUF_EXT
         if ( renderer->haveDmaBufImport )
         {
            if ( WstLDBBufferGetFd( ldbBuffer ) >= 0 )
            {
               int i;
               uint32_t frameFormat, frameWidth, frameHeight;
               int fd[MAX_TEXTURES];
               int32_t offset[MAX_TEXTURES], stride[MAX_TEXTURES];
               uint64_t modifier[MAX_TEXTURES];
               bool useModifiers= false;
               EGLint attr[64];

               frameFormat= WstLDBBufferGetFormat( ldbBuffer );
               frameWidth= WstLDBBufferGetWidth( ldbBuffer );
               frameHeight= WstLDBBufferGetHeight( ldbBuffer );

               modifier[0]= DRM_FORMAT_MOD_INVALID;

               for( i= 0; i < MAX_TEXTURES; ++i )
               {
                  fd[i]= WstLDBBufferGetPlaneFd( ldbBuffer, i );
                  WstLDBBufferGetPlaneOffsetAndStride( ldbBuffer, i, &offset[i], &stride[i] );
                  if ( renderer->haveDmaBufImportModifiers )
                  {
                     modifier[i]= WstLDBBufferGetPlaneModifier( ldbBuffer, i);
                  }
               }

               useModifiers= renderer->haveDmaBufImportModifiers && (modifier[0] != DRM_FORMAT_MOD_INVALID);

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

               i= 0;
               attr[i++]= EGL_WIDTH;
               attr[i++]= frameWidth;
               attr[i++]= EGL_HEIGHT;
               attr[i++]= frameHeight;
               attr[i++]= EGL_LINUX_DRM_FOURCC_EXT;
               attr[i++]= frameFormat;

               if ( ldbBuffer->info.planeCount > 0 )
               {
                  attr[i++]= EGL_DMA_BUF_PLANE0_FD_EXT;
                  attr[i++]= fd[0];
                  attr[i++]= EGL_DMA_BUF_PLANE0_OFFSET_EXT;
                  attr[i++]= offset[0];
                  attr[i++]= EGL_DMA_BUF_PLANE0_PITCH_EXT;
                  attr[i++]= stride[0];
                  if ( useModifiers )
                  {
                     attr[i++]= EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
                     attr[i++]= (modifier[0] & 0xFFFFFFFFUL);
                     attr[i++]= EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
                     attr[i++]= ((modifier[0] >> 32) & 0xFFFFFFFFUL);
                  }
               }

               if ( ldbBuffer->info.planeCount > 1 )
               {
                  attr[i++]= EGL_DMA_BUF_PLANE0_FD_EXT;
                  attr[i++]= fd[1];
                  attr[i++]= EGL_DMA_BUF_PLANE0_OFFSET_EXT;
                  attr[i++]= offset[1];
                  attr[i++]= EGL_DMA_BUF_PLANE0_PITCH_EXT;
                  attr[i++]= stride[1];
                  if ( useModifiers )
                  {
                     attr[i++]= EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
                     attr[i++]= (modifier[1] & 0xFFFFFFFFUL);
                     attr[i++]= EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
                     attr[i++]= ((modifier[1] >> 32) & 0xFFFFFFFFUL);
                  }
               }

               if ( ldbBuffer->info.planeCount > 2 )
               {
                  attr[i++]= EGL_DMA_BUF_PLANE0_FD_EXT;
                  attr[i++]= fd[2];
                  attr[i++]= EGL_DMA_BUF_PLANE0_OFFSET_EXT;
                  attr[i++]= offset[2];
                  attr[i++]= EGL_DMA_BUF_PLANE0_PITCH_EXT;
                  attr[i++]= stride[2];
                  if ( useModifiers )
                  {
                     attr[i++]= EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
                     attr[i++]= (modifier[2] & 0xFFFFFFFFUL);
                     attr[i++]= EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
                     attr[i++]= ((modifier[2] >> 32) & 0xFFFFFFFFUL);
                  }
               }

               if ( ldbBuffer->info.planeCount > 3 )
               {
                  attr[i++]= EGL_DMA_BUF_PLANE0_FD_EXT;
                  attr[i++]= fd[3];
                  attr[i++]= EGL_DMA_BUF_PLANE0_OFFSET_EXT;
                  attr[i++]= offset[3];
                  attr[i++]= EGL_DMA_BUF_PLANE0_PITCH_EXT;
                  attr[i++]= stride[3];
                  if ( useModifiers )
                  {
                     attr[i++]= EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
                     attr[i++]= (modifier[3] & 0xFFFFFFFFUL);
                     attr[i++]= EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
                     attr[i++]= ((modifier[3] >> 32) & 0xFFFFFFFFUL);
                  }
               }

               attr[i++]= EGL_NONE;

               eglImage= renderer->eglCreateImageKHR( renderer->eglDisplay,
                                                               EGL_NO_CONTEXT,
                                                               EGL_LINUX_DMA_BUF_EXT,
                                                               (EGLClientBuffer)NULL,
                                                               attr );
               if ( eglImage )
               {
                  /*
                   * We have a new eglImage.  Mark the surface as having no texture to
                   * trigger texture creation during the next scene render
                   */
                  surface->eglImage[0]= eglImage;
                  if ( surface->textureId[0] != GL_NONE )
                  {
                     wstRendererDeleteTexture( renderer, surface->textureId[0] );
                  }
                  surface->textureId[0]= GL_NONE;
               }
               else
               {
                  printf("wstRendererEMBPrepareResourceLDB: eglCreateImageKHR failed for fd %d, format %X: errno %X\n", fd[0], frameFormat, eglGetError());
               }

               surface->textureCount= 1;
            }
         }
         #endif
      }
   }
}

static void wstRendererEMBCommitLDB( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   struct wl_ldb_buffer *ldbBuffer;
   void *deviceBuffer;
   int bufferWidth, bufferHeight;

   EGLNativePixmapType eglPixmap= 0;
   EGLImageKHR eglImage= 0;
   bool resize= false;

   ldbBuffer= WstLDBBufferGet( resource );
   if ( ldbBuffer )
   {
      #ifdef EGL_LINUX_DMA_BUF_EXT
      if ( renderer->haveDmaBufImport )
      {
         int fd= WstLDBBufferGetFd( ldbBuffer );
         if ( fd >= 0 )
         {
            wstRendererEMBPrepareResourceLDB( renderer, surface, resource );
         }
      }
      #endif
   }

   #if WESTEROS_INVERTED_Y
   surface->invertedY= true;
   #endif
}
#endif

#if defined (WESTEROS_PLATFORM_RPI)
static void wstRendererEMBCommitDispmanx( WstRendererEMB *renderer, WstRenderSurface *surface, 
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
            printf("wstRendererEMBCommitDispmanx: EGL_TEXTURE_Y_U_V_WL not supported\n" );
            break;
          
         case EGL_TEXTURE_Y_UV_WL:
            printf("wstRendererEMBCommitDispmanx: EGL_TEXTURE_Y_UV_WL not supported\n" );
            break;
            
         case EGL_TEXTURE_Y_XUXV_WL:
            printf("wstRendererEMBCommitDispmanx: EGL_TEXTURE_Y_XUXV_WL not supported\n" );
            break;
            
         default:
            printf("wstRendererEMBCommitDispmanx: unknown texture format: %x\n", format );
            break;
      }
   }
}
#endif

static void wstRendererEMBRenderSurface( WstRendererEMB *renderer, WstRenderSurface *surface )
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

   float *matrix= (renderer->renderer->hints & WstHints_applyTransform
                  ? renderer->renderer->matrix : (float*)identityMatrix);

   float alpha= (renderer->renderer->hints & WstHints_applyTransform
                ? surface->opacity*renderer->renderer->alpha : surface->opacity );

   int resW, resH;
   GLint viewport[4];

   if ( renderer->renderer->hints & WstHints_fboTarget )
   {
      resW= renderer->renderer->outputWidth;
      resH= renderer->renderer->outputHeight;
   }
   else
   {
      glGetIntegerv( GL_VIEWPORT, viewport );
      resW= viewport[2];
      resH= viewport[3];
   }

   if ( surface->textureCount == 1 )
   {
      wstRendererEMBShaderDraw( surface->externalImage ? renderer->textureShaderExternal : renderer->textureShader,
                                resW,
                                resH,
                                (float*)matrix,
                                alpha,
                                surface->textureId[0],
                                GL_NONE,
                                4,
                                (const float*)verts,
                                (const float*)uv );
   }
   else
   {
      wstRendererEMBShaderDraw( renderer->textureShaderYUV,
                                resW,
                                resH,
                                (float*)matrix,
                                alpha,
                                surface->textureId[0],
                                surface->textureId[1],
                                4,
                                (const float*)verts,
                                (const float*)uv );
   }
}

static WstShader* wstRendererEMBCreateShader( WstRendererEMB *renderer, int shaderType )
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
      printf("wstRendererEMBCreateShader: failed to allocate WstShader\n");
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
         printf("wstRendererEMBCreateShader: glCreateShader (%s) error: %d\n", typeName, glGetError());
         goto exit;
      }
      glShaderSource(shader, 1, &src, NULL );
      glCompileShader(shader);
      glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
      if ( !status )
      {
         glGetShaderInfoLog(shader, sizeof(message), &len, message);
         printf("wstRendererEMBCreateShader: %s shader compile error: (%s)\n", typeName, message);
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
      printf("wstRendererEMBCreateShader: glCreateProgram error %d\n", glGetError());
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
      printf("wstRendererEMBCreateShader: %s shader link error: (%s)\n", typeName, message);
      goto exit;
   }

   shaderNew->uniRes= glGetUniformLocation(shaderNew->program, "resolution");
   if ( shaderNew->uniRes == -1 )
   {
      printf("wstRendererEMBCreateShader: uniformn 'resolution' location error\n");
      goto exit;
   }

   shaderNew->uniMatrix= glGetUniformLocation(shaderNew->program, "matrix");
   if ( shaderNew->uniMatrix == -1 )
   {
      printf("wstRendererEMBCreateShader: uniformn 'matrix' location error\n");
      goto exit;
   }

   shaderNew->uniAlpha= glGetUniformLocation(shaderNew->program, "alpha");
   if ( (shaderNew->uniAlpha == -1) && !noalpha )
   {
      printf("wstRendererEMBCreateShader: uniformn 'alpha' location error\n");
      goto exit;
   }

   shaderNew->uniTexture= glGetUniformLocation(shaderNew->program, "texture");
   if ( shaderNew->uniTexture == -1 )
   {
      printf("wstRendererEMBCreateShader: uniformn 'texture' location error\n");
      goto exit;
   }

   if ( yuv )
   {
      shaderNew->uniTextureuv= glGetUniformLocation(shaderNew->program, "textureuv");
      if ( shaderNew->uniTextureuv == -1 )
      {
         printf("wstRendererEMBCreateShader: uniformn 'textureuv' location error\n");
         goto exit;
      }
   }

exit:

   return shaderNew;
}

static void wstRendererEMBDestroyShader( WstShader *shader )
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

static void wstRendererEMBShaderDraw( WstShader *shader,
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
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   if ( rendererEMB )
   {
      wstRendererEMBDestroy( rendererEMB );
      renderer->renderer= 0;
   }
}

static void wstRendererUpdateScene( WstRenderer *renderer )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   GLuint program;

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
         printf("westeros-render-embedded: fps %f\n", fps);
         lastReportTime= now;
         frameCount= 0;
      }
   }

   wstRendererProcessDeadTextures( rendererEMB );
   
   if ( renderer->fastHint && rendererEMB->rendererFast && !rendererEMB->fastPathActive )
   {
      rendererEMB->fastPathActive= wstRendererActivateFastPath( rendererEMB );
   }

   if ( !renderer->fastHint && rendererEMB->fastPathActive )
   {
      wstRendererDeactivateFastPath( rendererEMB );
      rendererEMB->fastPathActive= false;
   }

   if ( rendererEMB->fastPathActive )
   {
      rendererEMB->rendererFast->outputX= renderer->outputX;
      rendererEMB->rendererFast->outputY= renderer->outputY;
      rendererEMB->rendererFast->outputWidth= renderer->outputWidth;
      rendererEMB->rendererFast->outputHeight= renderer->outputHeight;
      rendererEMB->rendererFast->matrix= renderer->matrix;
      rendererEMB->rendererFast->alpha= renderer->alpha;

      if ( renderer->hints & WstHints_holePunch )
      {
         int imax= rendererEMB->surfaces.size();
         for( int i= 0; i < imax; ++i )
         {
            WstRenderSurface *surface= rendererEMB->surfaces[i];

            if ( surface->visible )
            {
               int sx, sy, sw, sh;

               if ( surface->surfaceFast )
               {
                  rendererEMB->rendererFast->surfaceGetGeometry( rendererEMB->rendererFast, surface->surfaceFast, &sx, &sy, &sw, &sh );

                  if ( sw && sh )
                  {
                     WstRect r;

                     r.x= renderer->matrix[0]*sx+renderer->matrix[12];
                     r.y= renderer->matrix[5]*sy+renderer->matrix[13];
                     r.width= renderer->matrix[0]*sw;
                     r.height= renderer->matrix[5]*sh;

                     wstRendererHolePunch( renderer, r.x, r.y, r.width, r.height );
                  }
               }
            }
         }
         renderer->needHolePunch= false;
      }
      else
      {
         renderer->needHolePunch= true;
      }

      rendererEMB->rendererFast->delegateUpdateScene( rendererEMB->rendererFast, renderer->rects );

      return;
   }

   glGetIntegerv( GL_CURRENT_PROGRAM, (GLint*)&program );

   if ( !rendererEMB->textureShader )
   {
      rendererEMB->textureShader= wstRendererEMBCreateShader( rendererEMB, WstShaderType_rgb );
      rendererEMB->textureShaderYUV= wstRendererEMBCreateShader( rendererEMB, WstShaderType_yuv );
      if ( rendererEMB->haveExternalImage )
      {
         rendererEMB->textureShaderExternal= wstRendererEMBCreateShader( rendererEMB, WstShaderType_external );
      }
      rendererEMB->eglContext= eglGetCurrentContext();
   }

   /*
    * Render surfaces from bottom to top
    */   
   int imax= rendererEMB->surfaces.size();
   for( int i= 0; i < imax; ++i )
   {
      WstRenderSurface *surface= rendererEMB->surfaces[i];

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
         wstRendererEMBRenderSurface( rendererEMB, surface );
      }
   }

   glUseProgram( program );

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
}

static WstRenderSurface* wstRendererSurfaceCreate( WstRenderer *renderer )
{
   WstRenderSurface *surface;
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;

   surface= wstRendererEMBCreateSurface(rendererEMB);
   
   std::vector<WstRenderSurface*>::iterator it= rendererEMB->surfaces.begin();
   while ( it != rendererEMB->surfaces.end() )
   {
      if ( surface->zorder < (*it)->zorder )
      {
         break;
      }
      ++it;
   }
   rendererEMB->surfaces.insert(it,surface);
   
   return surface; 
}

static void wstRendererSurfaceDestroy( WstRenderer *renderer, WstRenderSurface *surface )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;

   for ( std::vector<WstRenderSurface*>::iterator it= rendererEMB->surfaces.begin(); 
         it != rendererEMB->surfaces.end();
         ++it )
   {
      if ( (*it) == surface )
      {
         rendererEMB->surfaces.erase(it);
         break;   
      }
   }   
   
   wstRendererEMBDestroySurface( rendererEMB, surface );
}

static void wstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   EGLint value;

   if ( renderer->fastHint && rendererEMB->rendererFast && !rendererEMB->fastPathActive )
   {
      rendererEMB->fastPathActive= wstRendererActivateFastPath( rendererEMB );
   }
   
   if ( !renderer->fastHint && rendererEMB->fastPathActive )
   {
      wstRendererDeactivateFastPath( rendererEMB );
      rendererEMB->fastPathActive= false;
   }
   
   if ( rendererEMB->fastPathActive )
   {
      rendererEMB->rendererFast->surfaceCommit( rendererEMB->rendererFast, surface->surfaceFast, resource );
      return;
   }

   if ( resource )
   {
      if ( wl_shm_buffer_get( resource ) )
      {
         wstRendererEMBCommitShm( rendererEMB, surface, resource );
      }
      #if defined (WESTEROS_HAVE_WAYLAND_EGL)
      else if ( rendererEMB->haveWaylandEGL && 
                (EGL_TRUE == rendererEMB->eglQueryWaylandBufferWL( rendererEMB->eglDisplay,
                                                                   resource,
                                                                   EGL_TEXTURE_FORMAT,
                                                                   &value ) ) )
      {
         wstRendererEMBCommitWaylandEGL( rendererEMB, surface, resource, value );
      }
      #endif
      #ifdef ENABLE_SBPROTOCOL
      else if ( WstSBBufferGet( resource ) )
      {
         wstRendererEMBCommitSB( rendererEMB, surface, resource );
      }
      #endif
      #ifdef ENABLE_LDBPROTOCOL
      else if ( WstLDBBufferGet( resource ) )
      {
         wstRendererEMBCommitLDB( rendererEMB, surface, resource );
      }
      #endif
      else
      {
         printf("wstRenderSurfaceCommit: unsupported buffer type\n");
      }
   }
   else
   {
      wstRendererEMBFlushSurface( rendererEMB, surface );
   }
}

static void wstRendererSurfaceSetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool visible )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;

   if ( surface )
   {
      surface->visible= visible;

      if ( surface->surfaceFast )
      {
         rendererEMB->rendererFast->surfaceSetVisible( rendererEMB->rendererFast,
                                                       surface->surfaceFast,
                                                       visible );
      }
   }
}

static bool wstRendererSurfaceGetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool *visible )
{
   bool isVisible= false;
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;

   if ( surface )
   {
      if ( surface->surfaceFast )
      {
         rendererEMB->rendererFast->surfaceGetVisible( rendererEMB->rendererFast,
                                                       surface->surfaceFast,
                                                       &surface->visible );
      }

      isVisible= surface->visible;
      
      *visible= isVisible;
   }
   
   return isVisible;   
}

static void wstRendererSurfaceSetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int x, int y, int width, int height )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   
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
      surface->dirty= true;

      if ( surface->surfaceFast )
      {
         rendererEMB->rendererFast->surfaceSetGeometry( rendererEMB->rendererFast,
                                                        surface->surfaceFast,
                                                        x, y, width, height );
      }
   }
}

void wstRendererSurfaceGetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;

   if ( surface )
   {
      if ( surface->surfaceFast )
      {
         rendererEMB->rendererFast->surfaceGetGeometry( rendererEMB->rendererFast,
                                                        surface->surfaceFast,
                                                        &surface->x,
                                                        &surface->y,
                                                        &surface->width,
                                                        &surface->height );
      }

      *x= surface->x;
      *y= surface->y;
      *width= surface->width;
      *height= surface->height;
   }
}

static void wstRendererSurfaceSetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float opacity )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   
   if ( surface )
   {
      surface->opacity= opacity;

      if ( surface->surfaceFast )
      {
         rendererEMB->rendererFast->surfaceSetOpacity( rendererEMB->rendererFast,
                                                       surface->surfaceFast,
                                                       opacity );
      }
   }
}

static float wstRendererSurfaceGetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float *opacity )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   float opacityLevel= 1.0;
   
   if ( surface )
   {
      if ( surface->surfaceFast )
      {
         rendererEMB->rendererFast->surfaceGetOpacity( rendererEMB->rendererFast,
                                                       surface->surfaceFast,
                                                       &surface->opacity );
      }

      opacityLevel= surface->opacity;
      
      *opacity= opacityLevel;
   }
   
   return opacityLevel;
}

static void wstRendererSurfaceSetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float z )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   
   if ( surface )
   {
      surface->zorder= z;

      // Remove from surface list
      for ( std::vector<WstRenderSurface*>::iterator it= rendererEMB->surfaces.begin(); 
            it != rendererEMB->surfaces.end();
            ++it )
      {
         if ( (*it) == surface )
         {
            rendererEMB->surfaces.erase(it);
            break;   
         }
      }   

      // Re-insert in surface list based on new z-order
      std::vector<WstRenderSurface*>::iterator it= rendererEMB->surfaces.begin();
      while ( it != rendererEMB->surfaces.end() )
      {
         if ( surface->zorder < (*it)->zorder )
         {
            break;
         }
         ++it;
      }
      rendererEMB->surfaces.insert(it,surface);
      
      if ( surface->surfaceFast )
      {
         rendererEMB->rendererFast->surfaceSetZOrder( rendererEMB->rendererFast,
                                                      surface->surfaceFast,
                                                      z );
      }
   }
}

static float wstRendererSurfaceGetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float *z )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   float zLevel= 1.0;
   
   if ( surface )
   {
      if ( surface->surfaceFast )
      {
         rendererEMB->rendererFast->surfaceGetZOrder( rendererEMB->rendererFast,
                                                      surface->surfaceFast,
                                                      &surface->zorder );
      }

      zLevel= surface->zorder;
      
      *z= zLevel;
   }
   
   return zLevel;
}
#ifdef ENABLE_LDBPROTOCOL
static void wstRendererQueryDmabufFormats( WstRenderer *renderer, int **formats, int *num_formats)
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   EGLBoolean b;
   EGLint numFormats;

   *num_formats= 0;
   *formats= 0;

   b= rendererEMB->eglQueryDmaBufFormatsEXT( rendererEMB->eglDisplay, 0, NULL, &numFormats );
   if ( b )
   {
      EGLint *theFormats= 0;

      theFormats= (EGLint*)calloc( numFormats, sizeof(EGLint) );
      if ( theFormats == 0 )
      {
         printf("wstRendererQueryDmabufFormats: eglQueryDmaBufFormatsEXT: failed to get num formats\n" );
         goto exit;
      }
      b= rendererEMB->eglQueryDmaBufFormatsEXT( rendererEMB->eglDisplay, numFormats, theFormats, &numFormats );
      if ( !b )
      {
         printf("wstRendererQueryDmabufFormats: eglQueryDmaBufFormatsEXT: failed to get formats\n" );
         free( theFormats );
         goto exit;
      }
      *num_formats= numFormats;
      *formats= theFormats;
   }

exit:
   return;
}

static void wstRendererQueryDmabufModifiers( WstRenderer *renderer, int format, uint64_t **modifiers, int *num_modifiers)
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   EGLBoolean b;
   EGLint numModifiers;

   *num_modifiers= 0;
   *modifiers= 0;

   b= rendererEMB->eglQueryDmaBufModifiersEXT( rendererEMB->eglDisplay, format, 0, NULL, NULL, &numModifiers );
   if ( b )
   {
      uint64_t *theModifiers= 0;

      theModifiers= (uint64_t*)calloc( numModifiers, sizeof(uint64_t) );
      if ( theModifiers == 0 )
      {
         printf("wstRendererQueryDmabufModifiers: eglQueryDmaBufModifiersEXT: failed to get num modifiers\n" );
         goto exit;
      }
      b= rendererEMB->eglQueryDmaBufModifiersEXT( rendererEMB->eglDisplay, format, numModifiers, theModifiers, NULL, &numModifiers );
      if ( !b )
      {
         printf("wstRendererQueryDmabufModifiers: eglQueryDmaBufModifiersEXT: failed to get modifiers\n" );
         free( theModifiers );
         goto exit;
      }
      *num_modifiers= numModifiers;
      *modifiers= theModifiers;
   }

exit:
   return;
}
#endif

static void wstRendererHolePunch( WstRenderer *renderer, int x, int y, int width, int height )
{
   GLfloat priorColor[4];
   GLint priorBox[4];
   GLint viewport[4];
   WstRect r;

   bool wasEnabled= glIsEnabled(GL_SCISSOR_TEST);
   glGetIntegerv( GL_SCISSOR_BOX, priorBox );
   glGetFloatv( GL_COLOR_CLEAR_VALUE, priorColor );
   glGetIntegerv( GL_VIEWPORT, viewport );

   glEnable( GL_SCISSOR_TEST );
   glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );

   r.x= x;
   r.y= y;
   r.width= width;
   r.height= height;

   glScissor( r.x, viewport[3]-(r.y+r.height), r.width, r.height );
   glClear( GL_COLOR_BUFFER_BIT );

   glClearColor( priorColor[0], priorColor[1], priorColor[2], priorColor[3] );

   if ( wasEnabled )
   {
      glScissor( priorBox[0], priorBox[1], priorBox[2], priorBox[3] );
   }
   else
   {
      glDisable( GL_SCISSOR_TEST );
   }
}

static void wstRendererInitFastPath( WstRendererEMB *renderer )
{
   bool error= false;
   void *module= 0, *init;
   WstRenderer *rendererFast= 0;
   int rc;

   char *moduleName= getenv("WESTEROS_FAST_RENDER");
   if ( moduleName )
   {
      module= dlopen( moduleName, RTLD_NOW );
      if ( !module )
      {
         printf("wstRendererInitFastPath: failed to load module (%s)\n", moduleName);
         printf("  detail: %s\n", dlerror() );
         error= true;
         goto exit;
      }
      
      init= dlsym( module, RENDERER_MODULE_INIT );
      if ( !init )
      {
         printf("wstRendererInitFastPath: failed to find module (%s) method (%s)\n", moduleName, RENDERER_MODULE_INIT );
         printf("  detail: %s\n", dlerror() );
         error= true;
         goto exit;
      }
      
      rendererFast= (WstRenderer*)calloc( 1, sizeof(WstRenderer) );
      if ( !rendererFast )
      {
         printf("wstRendererInitFastPath: no memory to allocate WstRender\n");
         error= true;
         goto exit;
      }

      rendererFast->outputWidth= renderer->renderer->outputWidth;
      rendererFast->outputHeight= renderer->renderer->outputHeight;
      rendererFast->nativeWindow= renderer->renderer->nativeWindow;
      
      rc= ((WSTMethodRenderInit)init)( rendererFast, 0, NULL );
      if ( rc )
      {
         printf("wstRendererInitFastPath: module (%s) init failed: %d\n", moduleName, rc );
         error= true;
         goto exit;
      }
      
      if ( !rendererFast->delegateUpdateScene )
      {
         printf("wstRendererInitFastPath: module (%s) does not support delegation\n", moduleName );
         error= true;
         goto exit;
      }
      
      renderer->rendererFast= rendererFast;
      
      printf("wstRendererInitFastPath: module (%s) loaded and intialized\n", moduleName );
   }
   
exit:

   if ( error )
   {
      if ( rendererFast )
      {
         if ( rendererFast->renderer )
         {
            rendererFast->renderTerm( rendererFast );
            rendererFast->renderer= 0;
         }
         free( rendererFast );      
      }
      
      if ( module )
      {
         dlclose( module );
      }
   }
}

static bool wstRendererActivateFastPath( WstRendererEMB *renderer )
{
   bool result= false;

   if ( renderer->rendererFast )
   {
      result= true;
      
      // Create fast surface instances for each surface
      int imax= renderer->surfaces.size();
      for( int i= 0; i < imax; ++i )
      {
         WstRenderSurface *surface= renderer->surfaces[i];

         if ( !surface->surfaceFast )
         {
            surface->surfaceFast= renderer->rendererFast->surfaceCreate( renderer->rendererFast );
            if ( !surface->surfaceFast )
            {
               result= false;
               break;
            }
            renderer->rendererFast->surfaceSetGeometry( renderer->rendererFast,
                                                        surface->surfaceFast,
                                                        surface->x, 
                                                        surface->y, 
                                                        surface->width, 
                                                        surface->height );
            renderer->rendererFast->surfaceSetOpacity( renderer->rendererFast,
                                                       surface->surfaceFast,
                                                       surface->opacity );
            renderer->rendererFast->surfaceSetZOrder( renderer->rendererFast,
                                                      surface->surfaceFast,
                                                      surface->zorder );
         }
      }

      if ( result )
      {
         // Discard texture info for all surfaces
         for( int i= 0; i < imax; ++i )
         {
            WstRenderSurface *surface= renderer->surfaces[i];

            wstRendererEMBFlushSurface( renderer, surface );
         }
      }
   }
   
   return result;
}

static void wstRendererDeactivateFastPath( WstRendererEMB *renderer )
{
   if ( renderer->rendererFast )
   {
      // Discard all fast surfaces
      int imax= renderer->surfaces.size();
      for( int i= 0; i < imax; ++i )
      {
         WstRenderSurface *surface= renderer->surfaces[i];
         if ( surface->surfaceFast && (surface->zorder != 1000000.0) )
         {
            renderer->rendererFast->surfaceGetGeometry( renderer->rendererFast, surface->surfaceFast,
                                                        &surface->x, &surface->y, &surface->width, &surface->height );
            renderer->rendererFast->surfaceDestroy( renderer->rendererFast, surface->surfaceFast );
            surface->surfaceFast= 0;
         }
      }
   }
}

static void wstRendererDeleteTexture( WstRendererEMB *renderer, GLuint textureId )
{
   renderer->deadTextures.push_back( textureId );
}

static void wstRendererProcessDeadTextures( WstRendererEMB *renderer )
{
   GLuint textureId;
   for( int i= renderer->deadTextures.size()-1; i >= 0; --i )
   {
      textureId= renderer->deadTextures[i];
      glDeleteTextures( 1, &textureId );
      renderer->deadTextures.pop_back();
   }   
}

extern "C"
{

int renderer_init( WstRenderer *renderer, int argc, char **argv )
{
   int rc= 0;
   WstRendererEMB *rendererEMB= 0;
   
   rendererEMB= wstRendererEMBCreate( renderer );
   if ( rendererEMB )
   {
      renderer->renderer= rendererEMB;
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
      #ifdef ENABLE_LDBPROTOCOL
      renderer->queryDmabufFormats= wstRendererQueryDmabufFormats;
      renderer->queryDmabufModifiers= wstRendererQueryDmabufModifiers;
      #endif
      renderer->holePunch= wstRendererHolePunch;
      
      wstRendererInitFastPath( rendererEMB );
   }
   else
   {
      rc= -1;
   }

exit:
   
   return 0;
}

}

