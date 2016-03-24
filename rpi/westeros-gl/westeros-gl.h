#ifndef _WESTEROS_GL_H
#define _WESTEROS_GL_H

typedef struct _WstGLCtx WstGLCtx;

WstGLCtx* WstGLInit();
void WstGLTerm( WstGLCtx *ctx );
void* WstGLCreateNativeWindow( WstGLCtx *ctx, int x, int y, int width, int height );
void WstGLDestroyNativeWindow( WstGLCtx *ctx, void *nativeWindow );
bool WstGLGetNativePixmap( WstGLCtx *ctx, void *nativeBuffer, void **nativePixmap );
void WstGLGetNativePixmapDimensions( WstGLCtx *ctx, void *nativePixmap, int *width, int *height );
void WstGLReleaseNativePixmap( WstGLCtx *ctx, void *nativePixmap );
void* WstGLGetEGLNativePixmap( WstGLCtx *ctx, void *nativePixmap );

#endif

