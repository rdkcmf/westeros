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
#ifndef _begl_displayplatform_h
#define _begl_displayplatform_h

#include <stdint.h>

typedef enum
{
   /* These formats are render target formats */
   BEGL_BufferFormat_eA8B8G8R8,
   BEGL_BufferFormat_eR8G8B8A8,
   BEGL_BufferFormat_eX8B8G8R8,
   BEGL_BufferFormat_eR8G8B8X8,
   BEGL_BufferFormat_eR5G6B5,

   BEGL_BufferFormat_eR4G4B4A4,
   BEGL_BufferFormat_eA4B4G4R4,
   BEGL_BufferFormat_eR4G4B4X4,
   BEGL_BufferFormat_eX4B4G4R4,

   BEGL_BufferFormat_eR5G5B5A1,
   BEGL_BufferFormat_eA1B5G5R5,
   BEGL_BufferFormat_eR5G5B5X1,
   BEGL_BufferFormat_eX1B5G5R5,

   BEGL_BufferFormat_eYV12,
   BEGL_BufferFormat_eYUV422,

   BEGL_BufferFormat_eBSTC,

   BEGL_BufferFormat_INVALID
} BEGL_BufferFormat;

typedef struct BEGL_PixmapInfo
{
   uint32_t width;
   uint32_t height;
   BEGL_BufferFormat format;
} BEGL_PixmapInfo;

#endif

