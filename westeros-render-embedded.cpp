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

#define DEFAULT_SURFACE_WIDTH (0)
#define DEFAULT_SURFACE_HEIGHT (0)

// assume premultiplied
static const char *fShaderText =
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

static const char *vShaderText =
  "uniform vec2 u_resolution;\n"
  "uniform mat4 amymatrix;\n"
  "attribute vec2 pos;\n"
  "attribute vec2 uv;\n"
  "varying vec2 v_uv;\n"
  "void main()\n"
  "{\n"
  // map from "pixel coordinates"
  "  vec4 p = amymatrix * vec4(pos, 0, 1);\n"
  "  vec4 zeroToOne = p / vec4(u_resolution, u_resolution.x, 1);\n"
  "  vec4 zeroToTwo = zeroToOne * vec4(2.0, 2.0, 1, 1);\n"
  "  vec4 clipSpace = zeroToTwo - vec4(1.0, 1.0, 0, 0);\n"
  "  clipSpace.w = 1.0+clipSpace.z;\n"
  "  gl_Position =  clipSpace * vec4(1, -1, 1, 1);\n"
  "  v_uv = uv;\n"
  "}\n";

static const char *fShaderTextYUV =
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "uniform sampler2D s_texturey;\n"
  "uniform sampler2D s_textureuv;\n"
  "const vec3 cc_r = vec3(1.0, -0.8604, 1.59580);\n"
  "const vec4 cc_g = vec4(1.0, 0.539815, -0.39173, -0.81290);\n"
  "const vec3 cc_b = vec3(1.0, -1.071, 2.01700);\n"
  "uniform float u_alpha;\n"
  "varying vec2 v_texy;\n"
  "varying vec2 v_texuv;\n"
  "void main()\n"
  "{\n"
  "   vec4 y_vec = texture2D(s_texturey, v_texy);\n"
  "   vec4 c_vec = texture2D(s_textureuv, v_texuv);\n"
  "   vec4 temp_vec = vec4(y_vec.a, 1.0, c_vec.b, c_vec.a);\n"
  "   gl_FragColor = vec4( dot(cc_r,temp_vec.xyw), dot(cc_g,temp_vec), dot(cc_b,temp_vec.xyz), u_alpha );\n"
  "}\n";

static const char *vShaderTextYUV =
  "uniform vec2 u_resolution;\n"
  "uniform mat4 amymatrix;\n"
  "attribute vec2 pos;\n"
  "attribute vec2 texcoordy;\n"
  "attribute vec2 texcoorduv;\n"
  "varying vec2 v_texy;\n"
  "varying vec2 v_texuv;\n"
  "void main()\n"
  "{\n"
  // map from "pixel coordinates"
  "  vec4 p = amymatrix * vec4(pos, 0, 1);\n"
  "  vec4 zeroToOne = p / vec4(u_resolution, u_resolution.x, 1);\n"
  "  vec4 zeroToTwo = zeroToOne * vec4(2.0, 2.0, 1, 1);\n"
  "  vec4 clipSpace = zeroToTwo - vec4(1.0, 1.0, 0, 0);\n"
  "  clipSpace.w = 1.0+clipSpace.z;\n"
  "  gl_Position =  clipSpace * vec4(1, -1, 1, 1);\n"
  "  v_texy = texcoordy;\n"
  "  v_texuv = texcoorduv;\n"
  "}\n";
  
static GLuint createShaderProgram(const char* vShaderTxt, const char* fShaderTxt)
{
  GLuint fragShader, vertShader, program = 0;
  GLint stat;
  
  fragShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragShader, 1, (const char **) &fShaderTxt, NULL);
  glCompileShader(fragShader);
  glGetShaderiv(fragShader, GL_COMPILE_STATUS, &stat);

  if (!stat)
  {
    WST_TRACE("Error: fragment shader did not compile: %d\nSource:\n%s", glGetError(), fShaderTxt);
    
    GLint maxLength = 0;
    glGetShaderiv(fragShader, GL_INFO_LOG_LENGTH, &maxLength);
    
    //The maxLength includes the NULL character
    std::vector<char> errorLog(maxLength);
    glGetShaderInfoLog(fragShader, maxLength, &maxLength, &errorLog[0]);    
    WST_TRACE("%s", &errorLog[0]);
    
    glDeleteShader(fragShader);

    return GL_NONE;
  }
  
  vertShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertShader, 1, (const char **) &vShaderTxt, NULL);
  glCompileShader(vertShader);
  glGetShaderiv(vertShader, GL_COMPILE_STATUS, &stat);

  if (!stat)
  {
    WST_TRACE("vertex shader did not compile: %d\nSource:\n%s", glGetError(), vShaderTxt);

    glDeleteShader(fragShader);
    glDeleteShader(vertShader);

    return GL_NONE;
  }
  
  program = glCreateProgram();
  glAttachShader(program, fragShader);
  glAttachShader(program, vertShader);
  
  return program;
}

void linkShaderProgram(GLuint program)
{
  GLint stat;

  glLinkProgram(program);  /* needed to put attribs into effect */
  glGetProgramiv(program, GL_LINK_STATUS, &stat);
  if (!stat)
  {
    char log[1000];
    GLsizei len;
    glGetProgramInfoLog(program, 1000, &len, log);
    WST_TRACE("failed to link:%s", log);
    assert(false);
  }
}

class shaderProgram
{
public:
  virtual void init(const char* v, const char* f)
  {
    mProgram = createShaderProgram(v, f);

    prelink();
    linkShaderProgram(mProgram);
    postlink();
  }

  int getUniformLocation(const char* name)
  {
    int l = glGetUniformLocation(mProgram, name);
    if (l == -1)
    {
      WST_TRACE("Shader does not define uniform %s.\n", name);
    }
    return l;
  }
    
  void use()
  {
    glUseProgram(mProgram);
  }

protected:
  // Override to do uniform lookups
  virtual void prelink() {}
  virtual void postlink() {}

  GLuint mProgram;
};

class textureShaderProgram: public shaderProgram
{
protected:
  virtual void prelink()
  {
    mPosLoc = 0;
    mUVLoc = 1;
    glBindAttribLocation(mProgram, mPosLoc, "pos");
    glBindAttribLocation(mProgram, mUVLoc, "uv");
  }

  virtual void postlink()
  {
    mResolutionLoc = getUniformLocation("u_resolution");
    mMatrixLoc = getUniformLocation("amymatrix");
    mAlphaLoc = getUniformLocation("u_alpha");
    mTextureLoc = getUniformLocation("s_texture");
  }

public:
  void draw(int resW, int resH, float* matrix, float alpha, 
            GLuint textureId, int count,            
            const float* pos, const float* uv )
  {
    use();
    glUniform2f(mResolutionLoc, resW, resH);
    glUniformMatrix4fv(mMatrixLoc, 1, GL_FALSE, matrix);
    glUniform1f(mAlphaLoc, alpha);

    glActiveTexture(GL_TEXTURE1); 
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(mTextureLoc, 1);
    glVertexAttribPointer(mPosLoc, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer(mUVLoc, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glEnableVertexAttribArray(mPosLoc);
    glEnableVertexAttribArray(mUVLoc);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, count);
    glDisableVertexAttribArray(mPosLoc);
    glDisableVertexAttribArray(mUVLoc);
  }

private:
  GLint mResolutionLoc;
  GLint mMatrixLoc;

  GLint mPosLoc;
  GLint mUVLoc;

  GLint mAlphaLoc;

  GLint mTextureLoc;
};

class textureShaderYUVProgram: public shaderProgram
{
protected:
  virtual void prelink()
  {
    mPosLoc = 0;
    mTexYLoc = 1;
    mTexUVLoc = 2;
    glBindAttribLocation(mProgram, mPosLoc, "pos");
    glBindAttribLocation(mProgram, mTexYLoc, "texcoordy");
    glBindAttribLocation(mProgram, mTexUVLoc, "texcoorduv");
  }

  virtual void postlink()
  {
    mResolutionLoc = getUniformLocation("u_resolution");
    mMatrixLoc = getUniformLocation("amymatrix");
    mAlphaLoc = getUniformLocation("u_alpha");
    mTextureYLoc = getUniformLocation("s_texturey");
    mTextureUVLoc = getUniformLocation("s_textureuv");
  }

public:
  void draw(int resW, int resH, float* matrix, float alpha, 
            GLuint textureYId, GLuint textureUVId, int count,            
            const float* pos, const float* uv )
  {
    use();
    glUniform2f(mResolutionLoc, resW, resH);
    glUniformMatrix4fv(mMatrixLoc, 1, GL_FALSE, matrix);
    glUniform1f(mAlphaLoc, alpha);

    glActiveTexture(GL_TEXTURE1); 
    glBindTexture(GL_TEXTURE_2D, textureYId);
    glUniform1i(mTextureYLoc, 1);
    glActiveTexture(GL_TEXTURE2); 
    glBindTexture(GL_TEXTURE_2D, textureUVId);
    glUniform1i(mTextureUVLoc, 2);
    glVertexAttribPointer(mPosLoc, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer(mTexYLoc, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glVertexAttribPointer(mTexUVLoc, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glEnableVertexAttribArray(mPosLoc);
    glEnableVertexAttribArray(mTexYLoc);
    glEnableVertexAttribArray(mTexUVLoc);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, count);
    glDisableVertexAttribArray(mPosLoc);
    glDisableVertexAttribArray(mTexYLoc);
    glDisableVertexAttribArray(mTexUVLoc);
  }

private:
  GLint mResolutionLoc;
  GLint mMatrixLoc;

  GLint mPosLoc;
  GLint mTexYLoc;
  GLint mTexUVLoc;

  GLint mAlphaLoc;

  GLint mTextureYLoc;
  GLint mTextureUVLoc;
};

#define MAX_TEXTURES (2)

struct _WstRenderSurface
{
   int textureCount;
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

   #if defined (WESTEROS_HAVE_WAYLAND_EGL)
   bool haveWaylandEGL;
   PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
   PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
   PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
   #endif

   textureShaderProgram *textureShader;
   textureShaderYUVProgram *textureShaderYUV;
   
   std::vector<WstRenderSurface*> surfaces;

   bool fastPathActive;   
   WstRenderer *rendererFast;

} WstRendererEMB;


static bool wstRendererEMBSetupEGL( WstRendererEMB *renderer );
static WstRendererEMB* wstRendererEMBCreate( WstRenderer *renderer );
static void wstRendererEMBDestroy( WstRendererEMB *renderer );
static WstRenderSurface *wstRendererEMBCreateSurface(WstRendererEMB *renderer);
static void wstRendererEMBDestroySurface( WstRendererEMB *renderer, WstRenderSurface *surface );
static void wstRendererEMBFlushSurface( WstRendererEMB *renderer, WstRenderSurface *surface );
static void wstRendererEMBCommitShm( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource );
#if defined (WESTEROS_HAVE_WAYLAND_EGL)
static void wstRendererEMBCommitWaylandEGL( WstRendererEMB *renderer, WstRenderSurface *surface, 
                                           struct wl_resource *resource, EGLint format );
#endif
#ifdef ENABLE_SBPROTOCOL
static void wstRendererEMBCommitSB( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource );
#endif
#if defined (WESTEROS_PLATFORM_RPI)
static void wstRendererEMBCommitDispmanx( WstRendererEMB *renderer, WstRenderSurface *surface, 
                                         DISPMANX_RESOURCE_HANDLE_T dispResource,
                                         EGLint format, int bufferWidth, int bufferHeight );
#endif                                         
static void wstRendererEMBRenderSurface( WstRendererEMB *renderer, WstRenderSurface *surface );
static void wstRendererInitFastPath( WstRendererEMB *renderer );
static bool wstRendererActivateFastPath( WstRendererEMB *renderer );
static void wstRendererDeactivateFastPath( WstRendererEMB *renderer );

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
      rendererEMB->outputWidth= renderer->outputWidth;
      rendererEMB->outputHeight= renderer->outputHeight;
      
      rendererEMB->renderer= renderer;
      
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

      rendererEMB->eglDisplay= eglGetDisplay(EGL_DEFAULT_DISPLAY);

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
      }
      printf("have wayland-egl: %d\n", rendererEMB->haveWaylandEGL );
      #endif
   }

exit:
   
   return rendererEMB;
}

static void wstRendererEMBDestroy( WstRendererEMB *renderer )
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
         delete renderer->textureShader;
         renderer->textureShader= 0;
      }
      if ( renderer->textureShaderYUV )
      {
         delete renderer->textureShaderYUV;
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
              glDeleteTextures( 1, &surface->textureId[i] );
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
               glDeleteTextures( 1, &surface->textureId[0] );
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
                  glDeleteTextures( 1, &surface->textureId[i] );
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
   if ( (surface->textureId[0] == GL_NONE) || surface->memDirty )
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
            renderer->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, surface->eglImage[i]);
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

   surface->width= surface->bufferWidth;
   surface->height= surface->bufferHeight;
   
   const float verts[4][2] = 
   {
      { surface->x, surface->y },
      { surface->x+surface->width, surface->y },
      { surface->x,  surface->y+surface->height },
      { surface->x+surface->width, surface->y+surface->height }
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
   
   const float matrix[4][4] =
   {
      {1, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {0, 0, 0, 1}
   };

   const float *uv= surface->invertedY ? (const float*)uvYInverted : (const float*)uvNormal;   
   
   if ( surface->textureCount == 1 )
   {
      renderer->textureShader->draw( renderer->renderer->outputWidth,
                                     renderer->renderer->outputHeight,
                                     (float*)matrix,
                                     surface->opacity,
                                     surface->textureId[0],
                                     4,
                                     (const float*)verts, 
                                     (const float*)uv );
   }
   else
   {
      renderer->textureShaderYUV->draw( renderer->renderer->outputWidth,
                                        renderer->renderer->outputHeight,
                                        (float*)matrix,
                                        surface->opacity,
                                        surface->textureId[0],
                                        surface->textureId[1],
                                        4,
                                        (const float*)verts, 
                                        (const float*)uv );
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

   if ( rendererEMB->fastPathActive )
   {
      rendererEMB->rendererFast->outputX= renderer->outputX;
      rendererEMB->rendererFast->outputY= renderer->outputY;
      rendererEMB->rendererFast->outputWidth= renderer->outputWidth;
      rendererEMB->rendererFast->outputHeight= renderer->outputHeight;
      rendererEMB->rendererFast->matrix= renderer->matrix;
      rendererEMB->rendererFast->alpha= renderer->alpha;

      rendererEMB->rendererFast->delegateUpdateScene( rendererEMB->rendererFast, renderer->rects );

      renderer->needHolePunch= true;
      return;
   }

   if ( !rendererEMB->textureShader )
   {
      rendererEMB->textureShader= new textureShaderProgram();
      rendererEMB->textureShader->init(vShaderText,fShaderText);
      rendererEMB->textureShaderYUV= new textureShaderYUVProgram();
      rendererEMB->textureShaderYUV->init(vShaderTextYUV,fShaderTextYUV);
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
   
   glFlush();
   glFinish();
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
            renderer->rendererFast->surfaceDestroy( renderer->rendererFast, surface->surfaceFast );
            surface->surfaceFast= 0;
         }
      }
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

