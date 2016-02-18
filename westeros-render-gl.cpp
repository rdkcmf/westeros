#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "westeros-render.h"
#include "westeros-gl.h"
#include "wayland-client.h"
#include "wayland-egl.h"

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
#define MSG_LEN      (1024)

#define TCM11 (0)
#define TCM12 (1)
#define TCM21 (2)
#define TCM22 (3)
#define TCMDX (4)
#define TCMDY (5)

#define TCM_MAPX( x, y, t ) ((x)*(t)[TCM11]+(y)*(t)[TCM21]+(t)[TCMDX])
#define TCM_MAPY( x, y, t ) ((x)*(t)[TCM12]+(y)*(t)[TCM22]+(t)[TCMDY])

#define DEFAULT_SURFACE_WIDTH (1280)
#define DEFAULT_SURFACE_HEIGHT (720)


typedef enum _WstShaderType
{
   WstShaderType_texture,
   WstShaderType_fill,
   WstShaderType_alphaText
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

#define WST_MAX_MASK 16
typedef struct _WstShader
{
   WstShaderClass shaderClass;
   GLuint programId;
   GLuint vertexShaderId;
   GLuint fragmentShaderId;
   GLuint mvpLocation;
   GLuint vertexLocation;
   GLuint textureLocation;
   GLuint fillColorLocation;
   GLuint opacityLocation;
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
   GLuint textureId;
   void *nativePixmap;
   EGLImageKHR eglImage;

   int bufferWidth;
   int bufferHeight;

   int x;
   int y;
   int width;
   int height;

   bool visible;
   float opacity;
   float zorder;

   int onScreen;

   int vertexLocation;
   int textureLocation;

   float transform[6];

   int clip[4];

   int vertexCoordsDirty;
   int texCoordsDirty;

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

   WstGLCtx *glCtx;
   void *nativeWindow;
   
   EGLDisplay eglDisplay;
   EGLConfig eglConfig;
   EGLContext eglContext;   
   EGLSurface eglSurface;

   PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
   PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
   PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
   
   std::vector<WstRenderSurface*> surfaces;
} WstRendererGL;

static WstRendererGL* wstRendererGLCreate( int width, int height );
static void wstRendererGLDestroy( WstRendererGL *renderer );
static WstRenderSurface *wstRenderGLCreateSurface(WstRendererGL *renderer);
static void wstRenderGLDestroySurface( WstRendererGL *renderer, WstRenderSurface *surface );
static void wstRenderGLPrepareSurface( WstRendererGL *renderer, WstRenderContext *rctx, WstRenderSurface *surf );
static void wstRenderGLRenderSurface( WstRendererGL *renderer, WstRenderContext *rctx, WstRenderSurface *surf );

static bool wstRenderGLSetupEGL( WstRendererGL *renderer );
static void wstMatrixIdentity(float m[16]);
static void wstMatrixMultMatrix(float m[16], float src1[16], float src2[16]);
static void wstMatrixTranslate(float m[16], float tx, float ty, float tz);
static void wstMatrixOrtho(float m[16], float left, float right, float bottom, float top, float near, float far);
static void wstSetCurrentShader( WstRendererGL *renderer, int shaderClassId );
static WstShader* wstGetShaderForClass( WstRendererGL *renderer, int shaderClassId );
static WstShader* wstCreateShaderForClass( WstShaderClass shaderClass );
static void wstDestroyShader( WstRendererGL *renderer, WstShader *shader );
static unsigned int wstCreateGLShader(const char *pShaderText, int shaderType);


static WstRendererGL* wstRendererGLCreate( WstRenderer *renderer )
{
   WstRendererGL *rendererGL= 0;
   
   rendererGL= (WstRendererGL*)calloc(1, sizeof(WstRendererGL) );
   if ( rendererGL )
   {
      rendererGL->outputWidth= renderer->outputWidth;
      rendererGL->outputHeight= renderer->outputHeight;
      
      rendererGL->currentShaderClass= -1;

      rendererGL->glCtx= WstGLInit();
      
      rendererGL->renderer= renderer;
      wstRenderGLSetupEGL( rendererGL );

      rendererGL->eglCreateImageKHR= (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
      printf( "eglCreateImageKHR %p\n", rendererGL->eglCreateImageKHR);

      rendererGL->eglDestroyImageKHR= (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
      printf( "eglDestroyImageKHR %p\n", rendererGL->eglDestroyImageKHR);

      rendererGL->glEGLImageTargetTexture2DOES= (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
      printf( "glEGLImageTargetTexture2DOES %p\n", rendererGL->glEGLImageTargetTexture2DOES);
    
      // Get the number of texture units
      glGetIntegerv( GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &rendererGL->maxTextureUnits );
      WST_TRACE( "WstRendererGL: WstRendererGLCreate: maxTextureUnits=%d\n", rendererGL->maxTextureUnits );

      rendererGL->shaderCacheSize= 4 *                // texture + fill + alphaText + unused
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
      rendererGL->nullShader.textureLocation= -1;
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
      if ( renderer->renderer->display )
      {
         if ( renderer->nativeWindow )
         {
            wl_egl_window_destroy( (struct wl_egl_window *)renderer->nativeWindow );
            renderer->nativeWindow= 0;
         }
      }
      else
      {
         if ( renderer->nativeWindow )
         {
            WstGLDestroyNativeWindow( renderer->glCtx, renderer->nativeWindow );
            renderer->nativeWindow= 0;
         }
         
         WstGLTerm( renderer->glCtx );
         renderer->glCtx= 0;
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
        surface->textureId= GL_NONE;

        surface->width= DEFAULT_SURFACE_WIDTH;
        surface->height= DEFAULT_SURFACE_HEIGHT;
        if ( surface->width >= renderer->renderer->outputWidth )
           surface->x= 0;
        else
           surface->x= (renderer->outputWidth-surface->width)/2;
        if ( surface->height >= renderer->renderer->outputHeight )
           surface->y= 0;
        else
           surface->y= (renderer->renderer->outputHeight-surface->height)/2;
        surface->visible= true;
        surface->opacity= 1.0;
        
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
        if ( surface->textureId )
        {
           glDeleteTextures( 1, &surface->textureId );
           surface->textureId= GL_NONE;
        }
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
        free( surface );
    }
}

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
       
       if ( surf->textureId == GL_NONE )
       {
           glGenTextures(1, &surf->textureId );
           WST_TRACE("WstPrepareSurface: surf %p surface textureId=%d\n", surf, surf->textureId );
          
           /* Bind the egl image as a texture */
           glBindTexture(GL_TEXTURE_2D, surf->textureId );
           renderer->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, surf->eglImage);
           glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
           glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
           glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
           glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
           WST_TRACE("WstPrepareSurface: after binding texture: glGetError=%X\n", glGetError() );
       }

       if ( vcupdate || tcupdate || surf->vertexCoordsDirty )
       {
           surf->width= surf->bufferWidth;
           surf->height= surf->bufferHeight;
           
           itemShaderClass.bits.type= WstShaderType_texture;
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

           surf->textureLocation= renderer->currentShader->textureLocation;
           WST_TRACE("surf textureLocation %d\n", surf->textureLocation);

           textureCoords= surf->textureCoords;
           
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
           itemShaderClass.bits.type= WstShaderType_texture;
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
           glVertexAttribPointer(surf->textureLocation, 2, GL_FLOAT, GL_FALSE, 0, surf->textureCoords);
           WST_TRACE("WstRenderSurface: enable texture attrib array %d", surf->textureLocation);
           glEnableVertexAttribArray(surf->textureLocation);

           glActiveTexture(GL_TEXTURE0);
           if ( renderer->activeTextureId[0] != surf->textureId )
           {
              renderer->activeTextureId[0]= surf->textureId;
              WST_TRACE("WstRenderSurface: calling glBindTexture textureId %d", surf->textureId);
              glBindTexture(GL_TEXTURE_2D, surf->textureId);
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
   
   if ( renderer->renderer->display )
   {
      // Get EGL display from wayland display
      renderer->eglDisplay= eglGetDisplay(renderer->renderer->display);
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
   if ( renderer->renderer->display )
   {
      renderer->nativeWindow= wl_egl_window_create(renderer->renderer->surface, renderer->outputWidth, renderer->outputHeight);         
   }
   else
   {
      renderer->nativeWindow= WstGLCreateNativeWindow( renderer->glCtx, 0, 0, renderer->outputWidth, renderer->outputHeight );
   }
   printf("nativeWindow %p\n", renderer->nativeWindow );

   // Create an EGL window surface
   renderer->eglSurface= eglCreateWindowSurface( renderer->eglDisplay, 
                                                 renderer->eglConfig, 
                                                 renderer->nativeWindow,
                                                 NULL );
   printf("wstRenderGLSetupEGL: eglSurface %p\n", renderer->eglSurface );

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
// Set the 4x4 matrix m[16] to the identity matrix
// ----------------------------------------------------------------------------
static void wstMatrixIdentity(float m[16])
{
    int i;
    if (m != NULL)
    {
        for (i=0; i<16; i++)
        {
            m[i] = 0;
        }
    
        m[0] = 1.0;
        m[5] = 1.0; 
        m[10] = 1.0;
        m[15] = 1.0;
    }
}

// ----------------------------------------------------------------------------
// Multiply two 4x4 matrices
// Input: src1[16], src2[16]  --- two source matrices
// Output: m[16]              --- result matrix
// ----------------------------------------------------------------------------
static void wstMatrixMultMatrix(float m[16], float src1[16], float src2[16])
{
    /* src1 or src2 could be the result matrix m as well, so use an intermediate
       matrix tm[16] to store the result */
    float tm[16];
    int i = 0;

    if ( m != NULL 
         && src1 != NULL
         && src2 != NULL )
    {
        tm[0] =   src1[0] * src2[0] + src1[4] * src2[1] 
                + src1[8] * src2[2] + src1[12]* src2[3];
        tm[1] =   src1[1] * src2[0] + src1[5] * src2[1] 
                + src1[9] * src2[2] + src1[13]* src2[3];
        tm[2] =   src1[2] * src2[0] + src1[6] * src2[1] 
                + src1[10]* src2[2] + src1[14]* src2[3];
        tm[3] =   src1[3] * src2[0] + src1[7] * src2[1] 
                + src1[11]* src2[2] + src1[15]* src2[3];
        tm[4] =   src1[0] * src2[4] + src1[4] * src2[5] 
                + src1[8] * src2[6] + src1[12]* src2[7];
        tm[5] =   src1[1] * src2[4] + src1[5] * src2[5] 
                + src1[9] * src2[6] + src1[13]* src2[7];
        tm[6] =   src1[2] * src2[4] + src1[6] * src2[5] 
                + src1[10]* src2[6] + src1[14]* src2[7];
        tm[7] =   src1[3] * src2[4] + src1[7] * src2[5] 
                + src1[11]* src2[6] + src1[15]* src2[7];
        tm[8] =   src1[0] * src2[8] + src1[4] * src2[9] 
                + src1[8] * src2[10]+ src1[12]* src2[11];
        tm[9] =   src1[1] * src2[8] + src1[5] * src2[9] 
                + src1[9] * src2[10]+ src1[13]* src2[11];
        tm[10] =  src1[2] * src2[8] + src1[6] * src2[9] 
                + src1[10]* src2[10]+ src1[14]* src2[11];
        tm[11] =  src1[3] * src2[8] + src1[7] * src2[9] 
                + src1[11]* src2[10]+ src1[15]* src2[11];
        tm[12] =  src1[0] * src2[12]+ src1[4] * src2[13] 
                + src1[8] * src2[14]+ src1[12]* src2[15];
        tm[13] =  src1[1] * src2[12]+ src1[5] * src2[13] 
                + src1[9] * src2[14]+ src1[13]* src2[15];
        tm[14] =  src1[2] * src2[12]+ src1[6] * src2[13] 
                + src1[10]* src2[14]+ src1[14]* src2[15];
        tm[15] =  src1[3] * src2[12]+ src1[7] * src2[13] 
                + src1[11]* src2[14]+ src1[15]* src2[15];

        for (i=0; i<16; i++)
        {
            m[i] = tm[i];
        }
    }
}
// ----------------------------------------------------------------------------
// Translate the 3D object in the scene
// Input: tx , ty, tz    --- the offset for translation in X, Y, and Z dirctions 
// Output: m[16]   --- the matrix after the translate
// ----------------------------------------------------------------------------
static void wstMatrixTranslate(float m[16], float tx, float ty, float tz)
{
    if (m != NULL )
    {
        m[12] = m[0] * tx + m[4] * ty + m[8] * tz + m[12];
        m[13] = m[1] * tx + m[5] * ty + m[9] * tz + m[13];
        m[14] = m[2] * tx + m[6] * ty + m[10] * tz + m[14];
        m[15] = m[3] * tx + m[7] * ty + m[11] * tz + m[15];
    }
}

// ----------------------------------------------------------------------------
// Orthogonal projection transformation
// Input: left, right, bottom, top, near, far parameters similar to glOrtho 
//        function
// Output: m[16]  4x4 projection matrix
// ----------------------------------------------------------------------------
static void wstMatrixOrtho(float m[16], float left, float right, float bottom, float top, float near, float far)
{
    // temporary 4x4 matrix for intermediate values
    float tm[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};                
   
    if (m != NULL)
    { 
        if (right != left && top != bottom && far != near)
        {
            tm[0] = 2 / (right - left);
            tm[5] = 2 / (top - bottom);
            tm[10] = 2 / (near - far);
            tm[12] = (right + left) / (right - left);
            tm[13] = (top + bottom) / (top - bottom);
            tm[14] = (far + near) / (far - near);

            wstMatrixMultMatrix(m, m, tm);
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

#define WST_SHADER_PRECISION "mediump"

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
   char infoLog[MSG_LEN+1];


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
      EMIT( "attribute vec2 inputtexture;\n" );
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
            EMIT( "   texcoord = inputtexture;\n" );
            break;
         case WstShaderType_fill:
            EMIT( "   basecolor = inputcolor;\n" );
            break;
         case WstShaderType_alphaText:
            EMIT( "   texcoord = inputtexture;\n" );
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
      vertexShaderId = wstCreateGLShader(shaderSource, 1);
      WST_TRACE( "shader class %X vertexShaderId=%d\n", shaderClass.id, vertexShaderId );


      // Generate fragment shader source
      shaderSource[0]= '\0';
      switch ( shaderClass.bits.type )
      {
         case WstShaderType_texture:
            EMIT( "varying " WST_SHADER_PRECISION " vec2 texcoord;\n" );
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
      fragmentShaderId = wstCreateGLShader(shaderSource, 2);
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
       glBindAttribLocation(programId, attribIndex++, "inputtexture" );
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
           glGetProgramInfoLog(programId, MSG_LEN, &infoLen, infoLog);
           if (infoLen > MSG_LEN)
           {
               infoLog[MSG_LEN] = '\0';
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
               glGetProgramInfoLog(programId, MSG_LEN, &infoLen, infoLog);
               if (infoLen > MSG_LEN)
               {
                   infoLog[MSG_LEN] = '\0';
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
           shaderNew->textureLocation= glGetAttribLocation(programId, "inputtexture");
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

           #ifdef WST_DEBUG
           WST_TRACE("shader %p created for class %X:", shaderNew, shaderNew->shaderClass.id );
           WST_TRACE("programId=%d vertexShaderId=%d fragmentShaderId=%d", shaderNew->programId, shaderNew->vertexShaderId, shaderNew->fragmentShaderId );
           WST_TRACE("vertexLocation= %d textureLocation=%d fillColorLocation=%d mvpLocation= %d", shaderNew->vertexLocation, shaderNew->textureLocation, shaderNew->fillColorLocation, shaderNew->mvpLocation );
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
static unsigned int wstCreateGLShader(const char *pShaderText, int shaderType)
{
    GLuint shaderId = 0;       // vertex or fragment shader Id
    char pInfoLog[MSG_LEN+1];    // error message
    int shaderStatus, infoLogLength;    //shader's status and error information length
    int shaderTexLen = -1;      // shader text length
    if (pShaderText != NULL)
    {
        if( 1 == shaderType )
        {
            shaderId = glCreateShader(GL_VERTEX_SHADER);
        }
        else 
        {
            shaderId = glCreateShader(GL_FRAGMENT_SHADER);
        }
        if (shaderId == 0)
        {
            if( 1 == shaderType )
            {
                fprintf(stderr,"Error: Failed to create vertex Shader\n");
            }
            else
            {
                fprintf(stderr,"Error: Failed to create fragment Shader\n");
            }
            return 0;
        }
        glShaderSource(shaderId, 1, (const char **)&pShaderText, &shaderTexLen);
        glCompileShader(shaderId);

        glGetShaderiv( shaderId, GL_COMPILE_STATUS, &shaderStatus);
        if (shaderStatus != GL_TRUE)
        {
            if( 1 == shaderType )
            {
                fprintf(stderr,"Error: Failed to compile GL vertex Shader\n");
            }
            else
            {    
                fprintf(stderr,"Error: Failed to compile GL fragment Shader\n");
            }
            glGetShaderInfoLog( shaderId, MSG_LEN, &infoLogLength, pInfoLog);
            if (infoLogLength > MSG_LEN)
            {
                 pInfoLog[MSG_LEN] = '\0';
            }
            else
            {
                pInfoLog[infoLogLength] = '\0';
            }
            fprintf(stderr, "%s\n",pInfoLog);

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
      
      if ( surface->visible && (surface->eglImage || (surface->textureId != GL_NONE)) )
      {
         wstRenderGLPrepareSurface( rendererGL, rctx, surface );
         wstRenderGLRenderSurface( rendererGL, rctx, surface );
      }
   }
 
   glFlush();
   glFinish();
  
   eglSwapBuffers(rendererGL->eglDisplay, rendererGL->eglSurface);
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

static void wstRendererSurfaceCommit( WstRenderer *renderer, WstRenderSurface *surface, void *buffer )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   EGLNativePixmapType eglPixmap= 0;
   EGLImageKHR eglImage= 0;
   int bufferWidth, bufferHeight;
   bool resize= false;
   
   if ( buffer )
   {
      if ( surface->nativePixmap )
      {
         eglPixmap= WstGLGetEGLNativePixmap(rendererGL->glCtx, surface->nativePixmap);
      }
      if ( WstGLGetNativePixmap( rendererGL->glCtx, buffer, &surface->nativePixmap ) )
      {
         WstGLGetNativePixmapDimensions( rendererGL->glCtx, surface->nativePixmap, &bufferWidth, &bufferHeight );
         if ( (surface->bufferWidth != bufferWidth) || (surface->bufferHeight != bufferHeight) )
         {
            surface->bufferWidth= bufferWidth;
            surface->bufferHeight= bufferHeight;
            surface->vertexCoordsDirty= 1;
            resize= true;
         }
         
         if ( resize || (eglPixmap != WstGLGetEGLNativePixmap(rendererGL->glCtx, surface->nativePixmap)) )
         {
            /*
             * If the eglPixmap contained by the surface WstGLNativePixmap changed
             * (because the attached buffer dimensions changed, for example) then we
             * need to create a new texture
             */
            if ( surface->eglImage )
            {
               rendererGL->eglDestroyImageKHR( rendererGL->eglDisplay,
                                               surface->eglImage );
               surface->eglImage= 0;
            }
            eglPixmap= WstGLGetEGLNativePixmap(rendererGL->glCtx, surface->nativePixmap);
         }
         if ( !surface->eglImage )
         {
            eglImage= rendererGL->eglCreateImageKHR( rendererGL->eglDisplay,
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
         rendererGL->eglDestroyImageKHR( rendererGL->eglDisplay,
                                         surface->eglImage );

         WstGLReleaseNativePixmap( rendererGL->glCtx, surface->nativePixmap );
         surface->nativePixmap= 0;
         surface->eglImage= 0;
      }
   }
}

static void wstRendererSurfaceCommitMemory( WstRenderer *renderer, WstRenderSurface *surface,
                                            void *data, int width, int height, int format, int stride )
{
   WstRendererGL *rendererGL= (WstRendererGL*)renderer->renderer;
   bool fillAlpha= false;

   if ( surface->textureId == GL_NONE )
   {
      glGenTextures(1, &surface->textureId );
   }
    
   if ( surface->textureId != GL_NONE )
   {
      GLint formatGL;
      GLenum type;
      
      switch( format )
      {
         case WstRenderer_format_ARGB8888:
            formatGL= GL_BGRA_EXT;
            type= GL_UNSIGNED_BYTE;
            break;
         case WstRenderer_format_XRGB8888:
            formatGL= GL_BGRA_EXT;
            type= GL_UNSIGNED_BYTE;
            fillAlpha= true;
            break;
         case WstRenderer_format_BGRA8888:
            formatGL= GL_RGBA;
            type= GL_UNSIGNED_BYTE;
            break;
         case WstRenderer_format_BGRX8888:
            formatGL= GL_RGBA;
            type= GL_UNSIGNED_BYTE;
            fillAlpha= true;
            break;
         case WstRenderer_format_RGB565:
            formatGL= GL_RGB;
            type= GL_UNSIGNED_SHORT_5_6_5;
            break;
         case WstRenderer_format_ARGB4444:
            formatGL= GL_RGBA;
            type= GL_UNSIGNED_SHORT_4_4_4_4;
            break;
         default:
            formatGL= GL_NONE;
            break;
      }
      
      if ( formatGL != GL_NONE )
      {
         glActiveTexture(GL_TEXTURE0); 
         glBindTexture(GL_TEXTURE_2D, surface->textureId );
         
         if ( fillAlpha )
         {
            unsigned char *pixdata= (unsigned char*)data;
            for( int y= 0; y < height; ++y )
            {
               for( int x= 0; x < width; ++x )
               {
                  pixdata[y*width*4 + x*4 +3]= 0xFF;
               }
            }
         }
         glTexImage2D( GL_TEXTURE_2D,
                       0, //level
                       formatGL, //internalFormat
                       width,
                       height,
                       0, // border
                       formatGL, //format
                       type,
                       data );
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glBindTexture(GL_TEXTURE_2D, GL_NONE );

         if ( (surface->bufferWidth != width) || (surface->bufferHeight != height) )
         {         
            surface->bufferWidth= width;
            surface->bufferHeight= height;
            surface->vertexCoordsDirty= 1;
         }
      }
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

