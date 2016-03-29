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

#define DEFAULT_SURFACE_WIDTH (0)
#define DEFAULT_SURFACE_HEIGHT (0)

// assume premultiplied
static const char *fTextureShaderText =
#if defined(PX_PLATFORM_WAYLAND_EGL) || defined(PX_PLATFORM_GENERIC_EGL)
  "precision mediump float;"
#endif
  "uniform sampler2D s_texture;"
  "uniform float u_alpha;"
  "varying vec2 v_uv;"
  "void main()"
  "{"
  "  gl_FragColor = texture2D(s_texture, v_uv) * u_alpha;"
  "}";

static const char *vShaderText =
  "uniform vec2 u_resolution;"
  "uniform mat4 amymatrix;"
  "attribute vec2 pos;"
  "attribute vec2 uv;"
  "varying vec2 v_uv;"
  "void main()"
  "{"
  // map from "pixel coordinates"
  "  vec4 p = amymatrix * vec4(pos, 0, 1);"
  "  vec4 zeroToOne = p / vec4(u_resolution, u_resolution.x, 1);"
  "  vec4 zeroToTwo = zeroToOne * vec4(2.0, 2.0, 1, 1);"
  "  vec4 clipSpace = zeroToTwo - vec4(1.0, 1.0, 0, 0);"
  "  clipSpace.w = 1.0+clipSpace.z;"
  "  gl_Position =  clipSpace * vec4(1, -1, 1, 1);"
  "  v_uv = uv;"
  "}";

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
    WST_TRACE("Error: fragment shader did not compile: %d", glGetError());
    
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
    WST_TRACE("vertex shader did not compile: %d", glGetError());

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
    WST_TRACE("faild to link:%s", log);
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

struct _WstRenderSurface
{
   GLuint textureId;

   int bufferWidth;
   int bufferHeight;
   
   #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
   void *nativePixmap;
   EGLImageKHR eglImage;
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
   
   std::vector<WstRenderSurface*> surfaces;

} WstRendererEMB;


static bool wstRenderEMBSetupEGL( WstRendererEMB *renderer );
static WstRendererEMB* wstRendererEMBCreate( WstRenderer *renderer );
static void wstRendererEMBDestroy( WstRendererEMB *renderer );
static WstRenderSurface *wstRenderEMBCreateSurface(WstRendererEMB *renderer);
static void wstRenderEMBDestroySurface( WstRendererEMB *renderer, WstRenderSurface *surface );
static void wstRendererEMBCommitShm( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource );
#if defined (WESTEROS_HAVE_WAYLAND_EGL)
static void wstRendererEMBCommitWaylandEGL( WstRendererEMB *renderer, WstRenderSurface *surface, 
                                           struct wl_resource *resource, EGLint format );
#endif
#ifdef ENABLE_SBPROTOCOL
static void wstRendererEMBCommitSB( WstRendererEMB *renderer, WstRenderSurface *surface, struct wl_resource *resource );
#endif
static void wstRenderEMBRenderSurface( WstRendererEMB *renderer, WstRenderSurface *surface );

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
               printf("calling eglBindWaylandDisplayWL with eglDisplay %p and wayland display %p", rendererEMB->eglDisplay, renderer->display );
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

static WstRenderSurface *wstRenderEMBCreateSurface(WstRendererEMB *renderer)
{
    WstRenderSurface *surface= 0;

    WST_UNUSED(renderer);   
    surface= (WstRenderSurface*)calloc( 1, sizeof(WstRenderSurface) );
    if ( surface )
    {
        surface->textureId= GL_NONE;

        surface->width= DEFAULT_SURFACE_WIDTH;
        surface->height= DEFAULT_SURFACE_HEIGHT;
        surface->x= 0;
        surface->y= 0;
        surface->visible= true;
        surface->opacity= 1.0;
        surface->zorder= 0.5;
        
        surface->dirty= true;
    }
   
    return surface;
}

static void wstRenderEMBDestroySurface( WstRendererEMB *renderer, WstRenderSurface *surface )
{
    if ( surface )
    {
        if ( surface->textureId )
        {
           glDeleteTextures( 1, &surface->textureId );
           surface->textureId= GL_NONE;
        }
        #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
        if ( surface->eglImage )
        {
            renderer->eglDestroyImageKHR( renderer->eglDisplay,
                                          surface->eglImage );
            surface->eglImage= 0;
        }
        #endif
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
        free( surface );
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
      int stride;
      GLint formatGL;
      GLenum type;

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
                  DISPMANX_RESOURCE_HANDLE_T dispResource;
                  VC_RECT_T rect;
                  
                  rect.x= 0;
                  rect.y= 0;
                  rect.width= bufferWidth;
                  rect.height= bufferHeight;
                  
                  dispResource= vc_dispmanx_get_handle_from_wl_buffer(resource);
                  if ( dispResource != DISPMANX_NO_HANDLE )
                  {
                     int result= vc_dispmanx_resource_read_data( dispResource,
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
            }
            break;
         
         case EGL_TEXTURE_Y_U_V_WL:
            printf("wstRendererEMBCommitWaylandEGL: EGL_TEXTURE_Y_U_V_WL not supported\n" );
            break;
          
         case EGL_TEXTURE_Y_UV_WL:
            printf("wstRendererEMBCommitWaylandEGL: EGL_TEXTURE_Y_UV_WL not supported\n" );
            break;
            
         case EGL_TEXTURE_Y_XUXV_WL:
            printf("wstRendererEMBCommitWaylandEGL: EGL_TEXTURE_Y_XUXV_WL not supported\n" );
            break;
            
         default:
            printf("wstRendererEMBCommitWaylandEGL: unknown texture format: %x\n", format );
            break;
      }
   }
   #else
   if ( (surface->bufferWidth != bufferWidth) || (surface->bufferHeight != bufferHeight) )
   {
      surface->bufferWidth= bufferWidth;
      surface->bufferHeight= bufferHeight;
   }

   if ( surface->eglImage )
   {
      renderer->eglDestroyImageKHR( renderer->eglDisplay,
                                    surface->eglImage );
      surface->eglImage= 0;
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
            surface->eglImage= eglImage;
            if ( surface->textureId != GL_NONE )
            {
               glDeleteTextures( 1, &surface->textureId );
            }
            surface->textureId= GL_NONE;
         }
         break;
      
      case EGL_TEXTURE_Y_U_V_WL:
         printf("wstRendererEMBCommitWaylandEGL: EGL_TEXTURE_Y_U_V_WL not supported\n" );
         break;
       
      case EGL_TEXTURE_Y_UV_WL:
         printf("wstRendererEMBCommitWaylandEGL: EGL_TEXTURE_Y_UV_WL not supported\n" );
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
   EGLNativePixmapType eglPixmap= 0;
   EGLImageKHR eglImage= 0;
   int bufferWidth, bufferHeight;
   bool resize= false;
   
   sbBuffer= WstSBBufferGet( resource );
   if ( sbBuffer )
   {
      deviceBuffer= WstSBBufferGetBuffer( sbBuffer );
      if ( deviceBuffer )
      {
         if ( surface->nativePixmap )
         {
            eglPixmap= WstGLGetEGLNativePixmap(renderer->glCtx, surface->nativePixmap);
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
            
            if ( resize || (eglPixmap != WstGLGetEGLNativePixmap(renderer->glCtx, surface->nativePixmap)) )
            {
               /*
                * If the eglPixmap contained by the surface WstGLNativePixmap changed
                * (because the attached buffer dimensions changed, for example) then we
                * need to create a new texture
                */
               if ( surface->eglImage )
               {
                  renderer->eglDestroyImageKHR( renderer->eglDisplay,
                                                  surface->eglImage );
                  surface->eglImage= 0;
               }
               eglPixmap= WstGLGetEGLNativePixmap(renderer->glCtx, surface->nativePixmap);
            }
            if ( !surface->eglImage )
            {
               eglImage= renderer->eglCreateImageKHR( renderer->eglDisplay,
                                                      EGL_NO_CONTEXT,
                                                      EGL_NATIVE_PIXMAP_KHR,
                                                      eglPixmap,
                                                      NULL // EGLInt attrList[]
                                                     );
               if ( eglImage )
               {
                  /*
                   * We have a new eglImage.  Mark the surface as having no texture to
                   * trigger texture creation during the next scene render
                   */
                  surface->eglImage= eglImage;
                  if ( surface->textureId != GL_NONE )
                  {
                     glDeleteTextures( 1, &surface->textureId );
                  }
                  surface->textureId= GL_NONE;
               }
            }
         }
      }
   }
   #if WESTEROS_INVERTED_Y
   surface->invertedY= true;
   #endif
}
#endif

static void wstRenderEMBRenderSurface( WstRendererEMB *renderer, WstRenderSurface *surface )
{
   if ( (surface->textureId == GL_NONE) || surface->memDirty )
   {
      if ( surface->textureId == GL_NONE )
      {
         glGenTextures(1, &surface->textureId );
      }
    
      /* Bind the egl image as a texture */
      glBindTexture(GL_TEXTURE_2D, surface->textureId );
      #if defined (WESTEROS_PLATFORM_EMBEDDED) || defined (WESTEROS_HAVE_WAYLAND_EGL)
      if ( surface->eglImage && renderer->eglContext )
      {
         renderer->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, surface->eglImage);
      }
      else 
      #endif
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
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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

   const float *uv= surface->invertedY ? (const float*)uvYInverted : (const float*)uvNormal;   
   
   renderer->textureShader->draw( renderer->renderer->resW,
                                  renderer->renderer->resH,
                                  renderer->renderer->matrix,
                                  renderer->renderer->alpha*surface->opacity,
                                  surface->textureId,
                                  4,
                                  (const float*)verts, 
                                  (const float*)uv );
                                     
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

   if ( !rendererEMB->textureShader )
   {
      rendererEMB->textureShader= new textureShaderProgram();
      rendererEMB->textureShader->init(vShaderText,fTextureShaderText);
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
            surface->eglImage ||
            #endif
            surface->memDirty ||
            (surface->textureId != GL_NONE)
          )
        )
      {
         wstRenderEMBRenderSurface( rendererEMB, surface );
      }
   }
   
   glFlush();
   glFinish();
}

static WstRenderSurface* wstRendererSurfaceCreate( WstRenderer *renderer )
{
   WstRenderSurface *surface;
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;

   surface= wstRenderEMBCreateSurface(rendererEMB);
   
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
   
   wstRenderEMBDestroySurface( rendererEMB, surface );
}

static void wstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, struct wl_resource *resource )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   EGLint value;

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

static void wstRendererSurfaceSetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool visible )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;

   if ( surface )
   {
      surface->visible= visible;
   }
}

static bool wstRendererSurfaceGetVisible( WstRenderer *renderer, WstRenderSurface *surface, bool *visible )
{
   bool isVisible= false;
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;

   if ( surface )
   {
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
   }
}

void wstRendererSurfaceGetGeometry( WstRenderer *renderer, WstRenderSurface *surface, int *x, int *y, int *width, int *height )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;

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
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   
   if ( surface )
   {
      surface->opacity= opacity;
   }
}

static float wstRendererSurfaceGetOpacity( WstRenderer *renderer, WstRenderSurface *surface, float *opacity )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
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
   }
}

static float wstRendererSurfaceGetZOrder( WstRenderer *renderer, WstRenderSurface *surface, float *z )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
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
   }
   else
   {
      rc= -1;
   }

exit:
   
   return 0;
}

}

