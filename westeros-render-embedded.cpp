#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>

#if defined (WESTEROS_PLATFORM_EMBEDDED)
  #ifdef ENABLE_SBPROTOCOL
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
  #endif
  #include <GLES2/gl2.h>
  #include <GLES2/gl2ext.h>
  
  #include "westeros-gl.h"
  #include "wayland-egl.h"
#else
  #include <GL/glew.h>
  #include <GL/glut.h>
  #include <GL/gl.h>
#endif

#include "westeros-render.h"
#include "wayland-client.h"

#include <vector>

//#define WST_DEBUG

#ifdef WST_DEBUG
#define INT_TRACE(FORMAT,...) printf( FORMAT "\n", __VA_ARGS__)
#else
#define INT_TRACE(FORMAT,...) 
#endif

#define WST_TRACE(...)  INT_TRACE(__VA_ARGS__, "")

#define WST_UNUSED( n ) ((void)n)

#define DEFAULT_SURFACE_WIDTH (640)
#define DEFAULT_SURFACE_HEIGHT (360)

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
   
   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   int back;
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
};

typedef struct _WstRendererEMB
{
   WstRenderer *renderer;
   int outputWidth;
   int outputHeight;

   #if defined (WESTEROS_PLATFORM_EMBEDDED)
   WstGLCtx *glCtx;
   EGLDisplay eglDisplay;

   PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
   PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
   PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
   #endif

   textureShaderProgram *textureShader;
   
   std::vector<WstRenderSurface*> surfaces;

} WstRendererEMB;


static WstRendererEMB* wstRendererEMBCreate( WstRenderer *renderer );
static void wstRendererEMBDestroy( WstRendererEMB *renderer );
static WstRenderSurface *wstRenderEMBCreateSurface(WstRendererEMB *renderer);
static void wstRenderEMBDestroySurface( WstRendererEMB *renderer, WstRenderSurface *surface );
static void wstRenderEMBRenderSurface( WstRendererEMB *renderEMB, WstRenderSurface *surface );


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
      if ( rendererEMB->glCtx )
      {
         rendererEMB->eglCreateImageKHR= (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
         WST_TRACE( "eglCreateImageKHR %p\n", rendererEMB->eglCreateImageKHR);

         rendererEMB->eglDestroyImageKHR= (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
         WST_TRACE( "eglDestroyImageKHR %p\n", rendererEMB->eglDestroyImageKHR);

         rendererEMB->glEGLImageTargetTexture2DOES= (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
         WST_TRACE( "glEGLImageTargetTexture2DOES %p\n", rendererEMB->glEGLImageTargetTexture2DOES);    

         rendererEMB->eglDisplay= eglGetDisplay(EGL_DEFAULT_DISPLAY);         
      }
      else
      {
         free( rendererEMB );
         rendererEMB= 0;
      }
      #endif
   }
   
   return rendererEMB;
}

static void wstRendererEMBDestroy( WstRendererEMB *renderer )
{
   if ( renderer )
   {
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
        #if defined (WESTEROS_PLATFORM_EMBEDDED)
        if ( surface->eglImage )
        {
            renderer->eglDestroyImageKHR( renderer->eglDisplay,
                                          surface->eglImage );
            surface->eglImage= 0;
        }
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

static void wstRenderEMBRenderSurface( WstRendererEMB *rendererEMB, WstRenderSurface *surface )
{
   if ( (surface->textureId == GL_NONE) || surface->memDirty )
   {
      if ( surface->textureId == GL_NONE )
      {
         glGenTextures(1, &surface->textureId );
      }
    
      /* Bind the egl image as a texture */
      glBindTexture(GL_TEXTURE_2D, surface->textureId );
      #if defined (WESTEROS_PLATFORM_EMBEDDED)
      if ( surface->eglImage )
      {
         rendererEMB->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, surface->eglImage);
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
 
   const float uv[4][2] = 
   {
      { 0,  0 },
      { 1,  0 },
      { 0,  1 },
      { 1,  1 }
   };
   
   rendererEMB->textureShader->draw( rendererEMB->renderer->resW,
                                     rendererEMB->renderer->resH,
                                     rendererEMB->renderer->matrix,
                                     rendererEMB->renderer->alpha*surface->opacity,
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
            #if defined (WESTEROS_PLATFORM_EMBEDDED)
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

static void wstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, void *buffer )
{
   #if defined (WESTEROS_PLATFORM_EMBEDDED) && defined (ENABLE_SBPROTOCOL)
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   EGLNativePixmapType eglPixmap= 0;
   EGLImageKHR eglImage= 0;

   if ( buffer )
   {
      if ( surface->nativePixmap )
      {
         eglPixmap= WstGLGetEGLNativePixmap(rendererEMB->glCtx, surface->nativePixmap);
      }
      if ( WstGLGetNativePixmap( rendererEMB->glCtx, buffer, &surface->nativePixmap ) )
      {
         WstGLGetNativePixmapDimensions( rendererEMB->glCtx, surface->nativePixmap, &surface->bufferWidth, &surface->bufferHeight );
         
         if ( eglPixmap != WstGLGetEGLNativePixmap(rendererEMB->glCtx, surface->nativePixmap) )
         {
            /*
             * If the eglPixmap contained by the surface WstGLNativePixmap changed
             * (because the attached buffer dimensions changed, for example) then we
             * need to create a new texture
             */
            if ( surface->eglImage )
            {
               rendererEMB->eglDestroyImageKHR( rendererEMB->eglDisplay,
                                               surface->eglImage );
               surface->eglImage= 0;
            }
            eglPixmap= WstGLGetEGLNativePixmap(rendererEMB->glCtx, surface->nativePixmap);
         }
         if ( !surface->eglImage )
         {
            eglImage= rendererEMB->eglCreateImageKHR( rendererEMB->eglDisplay,
                                                      EGL_NO_CONTEXT,
                                                      EGL_NATIVE_PIXMAP_KHR,
                                                      eglPixmap,
                                                      NULL // EGLInt attrList[]
                                                     );
            WST_TRACE("wstRendererSurfaceAttach: buffer %p eglPixmap %p eglCreateImageKHR: got eglImage  %p\n", buffer, eglPixmap, eglImage );
            
            if ( eglImage )
            {
               /*
                * We have a new eglImage.  Mark the surface as having no texture to
                * trigger texture creation during the next scene render
                */
               surface->eglImage= eglImage;
               surface->textureId= GL_NONE;
            }
         }
      }
   }
   else
   {
      if ( surface->eglImage )
      {
         WST_TRACE("wstRendererSurfaceAttach: call eglDestroyImageKHR for eglImage  %p\n", surface->eglImage );
         rendererEMB->eglDestroyImageKHR( rendererEMB->eglDisplay,
                                         surface->eglImage );

         WstGLReleaseNativePixmap( rendererEMB->glCtx, surface->nativePixmap );
         surface->nativePixmap= 0;
         surface->eglImage= 0;
      }
   }
   #else
   WST_UNUSED(renderer);
   WST_UNUSED(surface);
   WST_UNUSED(buffer);
   #endif
}

static void wstRendererSurfaceCommitMemory( WstRenderer *renderer, WstRenderSurface *surface,
                                            void *data, int width, int height, int format, int stride )
{
   WstRendererEMB *rendererEMB= (WstRendererEMB*)renderer->renderer;
   GLint formatGL;
   GLenum type;
   int bytesPerLine;
   bool fillAlpha= false;

   switch( format )
   {
      case WstRenderer_format_ARGB8888:
         #if defined (WESTEROS_PLATFORM_EMBEDDED)
         formatGL= GL_BGRA_EXT;
         #else
         formatGL= GL_RGBA;
         #endif
         type= GL_UNSIGNED_BYTE;
         bytesPerLine= 4*width;
         break;
      case WstRenderer_format_XRGB8888:
         #if defined (WESTEROS_PLATFORM_EMBEDDED)
         formatGL= GL_BGRA_EXT;
         #else
         formatGL= GL_RGBA;
         #endif
         type= GL_UNSIGNED_BYTE;
         bytesPerLine= 4*width;
         fillAlpha= true;
         break;
      case WstRenderer_format_BGRA8888:
         formatGL= GL_RGBA;
         type= GL_UNSIGNED_BYTE;
         bytesPerLine= 4*width;
         break;
      case WstRenderer_format_BGRX8888:
         formatGL= GL_RGBA;
         type= GL_UNSIGNED_BYTE;
         bytesPerLine= 4*width;
         fillAlpha= true;
         break;
      case WstRenderer_format_RGB565:
         formatGL= GL_RGB;
         type= GL_UNSIGNED_SHORT_5_6_5;
         bytesPerLine= 2*width;
         break;
      case WstRenderer_format_ARGB4444:
         formatGL= GL_RGBA;
         type= GL_UNSIGNED_SHORT_4_4_4_4;
         bytesPerLine= 2*width;
         break;
      default:
         formatGL= GL_NONE;
         break;
   }
   
   if ( formatGL != GL_NONE )
   {
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
         surface->mem= (unsigned char*)malloc( bytesPerLine*height );
      }
      if ( surface->mem )
      {
         memcpy( surface->mem, data, bytesPerLine*height );
         #if defined (WESTEROS_PLATFORM_EMBEDDED)
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
         #else
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
         #endif
         surface->bufferWidth= width;
         surface->bufferHeight= height;
         surface->memWidth= width;
         surface->memHeight= height;
         surface->memFormatGL= formatGL;
         surface->memType= type;
         surface->memDirty= true;
      }      
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
      renderer->surfaceCommitMemory= wstRendererSurfaceCommitMemory;
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

