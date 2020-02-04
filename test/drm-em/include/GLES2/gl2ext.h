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
#ifndef _gl2ext_h
#define _gl2ext_h

#define GL_APIENTRYP *

#define GL_BGRA_EXT                       0x80E1

#ifndef GL_OES_EGL_image_external
#define GL_OES_EGL_image_external 1
#define GL_TEXTURE_EXTERNAL_OES           0x8D65
#define GL_SAMPLER_EXTERNAL_OES           0x8D66
#endif /* GL_OES_EGL_image_external */

typedef void *GLeglImageOES;

typedef void (GL_APIENTRYP PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) (GLenum target, GLeglImageOES image);

GL_APICALL void GL_APIENTRY glEGLImageTargetTexture2DOES (GLenum target, GLeglImageOES image);

#endif

