/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2018 RDK Management
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
#ifndef _WESTEROS_UT_EM_H
#define _WESTEROS_UT_EM_H

#define _EMERROR( FORMAT, ... ) EMSetError(emctx, "Error: %s:%d " FORMAT, __FILE__, __LINE__, __VA_ARGS__)
#define EMERROR(...) _EMERROR( __VA_ARGS__, "" )

typedef struct _EMCTX EMCTX;

typedef bool (*TESTCASEFUNC)( EMCTX *ctx );

typedef struct _TESTCASE
{
   const char *name;
   const char *desc;
   TESTCASEFUNC func;
} TESTCASE;

typedef struct _EMSimpleVideoDecoder EMSimpleVideoDecoder;

typedef enum _EM_TUNERID
{
   EM_TUNERID_MAIN
} EM_TUNERID;

typedef void (*EMTextureCreated)( EMCTX *ctx, void *userData, int bufferId );

EMCTX* EMCreateContext( void );
void EMDestroyContext( EMCTX* ctx );
bool EMSetDisplaySize( EMCTX *ctx, int width, int height );
bool EMGetWaylandThreadingIssue( EMCTX *ctx );
void EMSetWesterosModuleIntFail( EMCTX *ctx, bool initShouldFail );
bool EMGetWesterosModuleInitCalled( EMCTX *ctx );
bool EMGetWesterosModuleTermCalled( EMCTX *ctx );
void EMSetError( EMCTX *ctx, const char *fmt, ... );
const char* EMGetError( EMCTX *ctx );

long long EMGetCurrentTimeMicro(void);

void EMSetStcChannel( EMCTX *ctx, void *stcChannel );
void* EMGetStcChannel( EMCTX *ctx );
void EMSetVideoCodec( EMCTX *ctx, int codec );
int EMGetVideoCodec( EMCTX *ctx );
void EMSetVideoPidChannel( EMCTX *ctx, void *videoPidChannel );
void* EMGetVideoPidChannel( EMCTX *ctx );

EMSimpleVideoDecoder* EMGetSimpleVideoDecoder( EMCTX *ctx, int id );
void EMSimpleVideoDecoderSetVideoSize( EMSimpleVideoDecoder *dec, int width, int height );
void EMSimpleVideoDecoderSetFrameRate( EMSimpleVideoDecoder *dec, float fps );
float EMSimpleVideoDecoderGetFrameRate( EMSimpleVideoDecoder *dec );
void EMSimpleVideoDecoderSetBitRate( EMSimpleVideoDecoder *dec, float MBps );
float EMSimpleVideoDecoderGetBitRate( EMSimpleVideoDecoder *dec );
void EMSimpleVideoDecoderSetColorimetry( EMSimpleVideoDecoder *dec, const char *colorimetry );
const char* EMSimpleVideoDecoderGetColorimetry( EMSimpleVideoDecoder *dec );
void EMSimpleVideoDecoderSetMasteringMeta( EMSimpleVideoDecoder *dec, const char *masteringMeta );
const char* EMSimpleVideoDecoderGetMasteringMeta( EMSimpleVideoDecoder *dec );
void EMSimpleVideoDecoderSetContentLight( EMSimpleVideoDecoder *dec, const char *contentLight );
const char* EMSimpleVideoDecoderGetContentLight( EMSimpleVideoDecoder *dec );
void EMSimpleVideoDecoderSetSegmentsStartAtZero( EMSimpleVideoDecoder *dec, bool startAtZero );
bool EMSimpleVideoDecoderGetSegmentsStartAtZero( EMSimpleVideoDecoder *dec );
void EMSimpleVideoDecoderSetFrameNumber( EMSimpleVideoDecoder *dec, unsigned frameNumber );
unsigned EMSimpleVideoDecoderGetFrameNumber( EMSimpleVideoDecoder *dec );
void EMSimpleVideoDecoderSignalUnderflow( EMSimpleVideoDecoder *dec );
void EMSimpleVideoDecoderSignalPtsError( EMSimpleVideoDecoder *dec );
void EMSimpleVideoDecoderSetTrickStateRate( EMSimpleVideoDecoder *dec, int rate );
int EMSimpleVideoDecoderGetTrickStateRate( EMSimpleVideoDecoder *dec );
int EMSimpleVideoDecoderGetHdrEotf( EMSimpleVideoDecoder *dec );

int EMWLEGLWindowGetSwapCount( struct wl_egl_window *w );
void EMWLEGLWindowSetBufferRange( struct wl_egl_window *w, int base, int count );
void EMSetTextureCreatedCallback( EMCTX *ctx, EMTextureCreated cb, void *userData );

#endif

