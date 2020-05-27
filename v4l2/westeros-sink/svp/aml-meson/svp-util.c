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

#include "../svp-util.h"

enum vdec_dw_mode
{
   VDEC_DW_AFBC_ONLY = 0,
   VDEC_DW_AFBC_1_1_DW = 1,
   VDEC_DW_AFBC_1_4_DW = 2,
   VDEC_DW_AFBC_x2_1_4_DW = 3,
   VDEC_DW_AFBC_1_2_DW = 4,
   VDEC_DW_NO_AFBC = 16,
};

#define V4L2_CONFIG_PARM_DECODE_CFGINFO (1 << 0)
#define V4L2_CONFIG_PARM_DECODE_PSINFO  (1 << 1)
#define V4L2_CONFIG_PARM_DECODE_HDRINFO (1 << 2)
#define V4L2_CONFIG_PARM_DECODE_CNTINFO (1 << 3)

struct aml_vdec_cfg_infos
{
   uint32_t double_write_mode;
   uint32_t init_width;
   uint32_t init_height;
   uint32_t ref_buf_margin;
   uint32_t canvas_mem_mode;
   uint32_t canvas_mem_endian;
};

#define SEI_PicTiming (1)
#define SEI_MasteringDisplayColorVolume (137)
#define SEI_ContentLightLevel (144)
/* content_light_level from SEI */
struct vframe_content_light_level_s
{
   uint32_t present_flag;
   uint32_t max_content;
   uint32_t max_pic_average;
};

/* master_display_colour_info_volume from SEI */
struct vframe_master_display_colour_s
{
   uint32_t present_flag;
   uint32_t primaries[3][2];
   uint32_t white_point[2];
   uint32_t luminance[2];
   struct vframe_content_light_level_s content_light_level;
};

struct aml_vdec_hdr_infos
{
   /*
    * bit 29   : present_flag
    * bit 28-26: video_format "component", "PAL", "NTSC", "SECAM", "MAC", "unspecified"
    * bit 25   : range "limited", "full_range"
    * bit 24   : color_description_present_flag
    * bit 23-16: color_primaries "unknown", "bt709", "undef", "bt601",
    *            "bt470m", "bt470bg", "smpte170m", "smpte240m", "film", "bt2020"
    * bit 15-8 : transfer_characteristic unknown", "bt709", "undef", "bt601",
    *            "bt470m", "bt470bg", "smpte170m", "smpte240m",
    *            "linear", "log100", "log316", "iec61966-2-4",
    *            "bt1361e", "iec61966-2-1", "bt2020-10", "bt2020-12",
    *            "smpte-st-2084", "smpte-st-428"
    * bit 7-0  : matrix_coefficient "GBR", "bt709", "undef", "bt601",
    *            "fcc", "bt470bg", "smpte170m", "smpte240m",
    *            "YCgCo", "bt2020nc", "bt2020c"
    */
   uint32_t signal_type;
   struct vframe_master_display_colour_s color_parms;
};

struct aml_vdec_ps_infos
{
   uint32_t visible_width;
   uint32_t visible_height;
   uint32_t coded_width;
   uint32_t coded_height;
   uint32_t profile;
   uint32_t mb_width;
   uint32_t mb_height;
   uint32_t dpb_size;
   uint32_t ref_frames;
   uint32_t reorder_frames;
};

struct aml_vdec_cnt_infos
{
   uint32_t bit_rate;
   uint32_t frame_count;
   uint32_t error_frame_count;
   uint32_t drop_frame_count;
   uint32_t total_data;
};

struct aml_dec_params
{
   /* one of V4L2_CONFIG_PARM_DECODE_xxx */
   uint32_t parms_status;
   struct aml_vdec_cfg_infos cfg;
   struct aml_vdec_ps_infos ps;
   struct aml_vdec_hdr_infos hdr;
   struct aml_vdec_cnt_infos cnt;
};

static void wstSVPDecoderConfig( GstWesterosSink *sink )
{
   int rc;
   int frameWidth, frameHeight;
   const char *env;
   struct v4l2_streamparm streamparm;
   struct aml_dec_params *decParm= (struct aml_dec_params*)streamparm.parm.raw_data;
   memset( &streamparm, 0, sizeof(streamparm) );
   streamparm.type= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
   decParm->parms_status= V4L2_CONFIG_PARM_DECODE_CFGINFO;

   switch( sink->soc.inputFormat )
   {
      default:
      case V4L2_PIX_FMT_MPEG:
      case V4L2_PIX_FMT_H264:
         decParm->cfg.double_write_mode= VDEC_DW_NO_AFBC;
         break;
      case V4L2_PIX_FMT_HEVC:
      case V4L2_PIX_FMT_VP9:
         frameWidth= sink->soc.frameWidth;
         frameHeight= sink->soc.frameHeight;
         if ( (frameWidth > 1920) || (frameHeight > 1080) )
         {
            decParm->cfg.double_write_mode= VDEC_DW_AFBC_1_2_DW;
         }
         else
         {
            decParm->cfg.double_write_mode= VDEC_DW_AFBC_1_1_DW;
         }
         GST_DEBUG("format %s size %dx%d dw mode %d",
                    (sink->soc.inputFormat == V4L2_PIX_FMT_HEVC ? "HEVC" : "VP9"),
                    frameWidth, frameHeight, decParm->cfg.double_write_mode);
         break;
   }

   env= getenv("WESTEROS_SINK_AMLOGIC_DW_MODE");
   if ( env )
   {
      int dwMode= atoi(env);
      switch( dwMode )
      {
         case 0: case 1: case 2: case 3: case 4: case 16:
            decParm->cfg.double_write_mode= dwMode;
            break;
      }
   }

   if ( decParm->cfg.double_write_mode != sink->soc.dwMode )
   {
      sink->soc.dwMode= decParm->cfg.double_write_mode;
      g_print("wstSVPDecoderConfig: dw mode %d\n", decParm->cfg.double_write_mode);
   }

   if ( decParm->cfg.double_write_mode == VDEC_DW_AFBC_ONLY )
   {
      sink->soc.preferNV12M= FALSE;
   }
   if ( sink->soc.inputFormat != V4L2_PIX_FMT_MPEG2 )
   {
      decParm->cfg.ref_buf_margin= 7;
   }
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_S_PARM, &streamparm );
   if ( rc != 0)
   {
      GST_ERROR("VIDIOC_S_PARAM failed for aml driver raw_data: %d", rc);
   }
}

static void wstSVPSetInputMemMode( GstWesterosSink *sink, int mode )
{
   int len= strlen( (char*)sink->soc.caps.driver );

   if ( (len == 14) && !strncmp( (char*)sink->soc.caps.driver, "aml-vcodec-dec", len) )
   {
      int rc;
      struct v4l2_queryctrl queryctrl;
      struct v4l2_control control;

      #define V4L2_CID_USER_AMLOGIC_BASE (V4L2_CID_USER_BASE + 0x1100)
      #define AML_V4L2_SET_DRMMODE (V4L2_CID_USER_AMLOGIC_BASE + 0)
      memset( &queryctrl, 0, sizeof (queryctrl) );
      queryctrl.id= AML_V4L2_SET_DRMMODE;

      rc= IOCTL(sink->soc.v4l2Fd, VIDIOC_QUERYCTRL, &queryctrl );
      if ( rc == 0 )
      {
         if ( !(queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) )
         {
            /* On AmLogic, use of dma-buf implies secure memory */
            sink->soc.secureVideo= ( mode == V4L2_MEMORY_DMABUF ) ? TRUE : FALSE;

            if ( sink->soc.secureVideo || getenv("WESTEROS_SINK_AMLOGIC_USE_DMABUF") )
            {
               sink->soc.useDmabufOutput= TRUE;
               wstSVPDecoderConfig( sink );
            }

            memset (&control, 0, sizeof (control) );
            control.id= AML_V4L2_SET_DRMMODE;
            control.value= (sink->soc.secureVideo ? 1 : 0);
            rc= IOCTL(sink->soc.v4l2Fd, VIDIOC_S_CTRL, &control);
            if ( rc != 0 )
            {
               GST_ERROR("AML_V4L2_SET_DRMMODE fail: rc %d", rc);
            }
         }
         else
         {
            GST_ERROR("AML_V4L2_SET_DRMMODE is disabled");
         }
      }
      else
      {
         GST_ERROR("AML_V4L2_SET_DRMMODE is not supported");
      }
   }
   else
   {
      GST_ERROR("Not aml-vcodec-dec: driver (%s)", sink->soc.caps.driver );
   }
}

static void wstSVPSetOutputMemMode( GstWesterosSink *sink, int mode )
{
   int len= strlen( (char*)sink->soc.caps.driver );

   sink->soc.outputMemMode= mode;

   if ( (len == 14) && !strncmp( (char*)sink->soc.caps.driver, "aml-vcodec-dec", len) )
   {
      if ( mode == V4L2_MEMORY_DMABUF )
      {
         wstSVPDecoderConfig( sink );
      }
   }
   else
   {
      GST_ERROR("Not aml-vcodec-dec: driver (%s)", sink->soc.caps.driver );
   }
}

static void wstSVPDestroyGemBuffer( GstWesterosSink *sink, WstGemBuffer *gemBuf )
{
   int rc, i;
   struct drm_gem_close gclose;

   for( i= 0; i < gemBuf->planeCount; ++i )
   {
      if ( gemBuf->fd[i] >= 0 )
      {
         close( gemBuf->fd[i] );
         gemBuf->fd[i]= -1;
      }
      if ( gemBuf->handle[i] > 0 )
      {
         memset( &gclose, 0, sizeof(gclose) );
         gclose.handle= gemBuf->handle[i];
         rc= ioctl( sink->soc.drmFd, DRM_IOCTL_GEM_CLOSE, &gclose );
         if ( rc < 0 )
         {
            GST_ERROR("Failed to release gem buffer handle %d: DRM_IOCTL_MODE_DESTROY_DUMB rc %d errno %d",
                      gemBuf->handle[i], rc, errno );
         }
      }
   }
}

static bool wstSVPCreateGemBuffer( GstWesterosSink *sink, WstGemBuffer *gemBuf )
{
   bool result= false;
   int rc, i;
   struct drm_meson_gem_create gc;

   for( i= 0; i < gemBuf->planeCount; ++i )
   {
      gemBuf->fd[i]= -1;
      memset( &gc, 0, sizeof(gc) );
      if ( sink->soc.fmtOut.fmt.pix_mp.pixelformat == V4L2_PIX_FMT_NV12 )
      {
         gc.flags= MESON_USE_VIDEO_AFBC;
         gc.size= gemBuf->width*gemBuf->height*2;
         gemBuf->stride[i]= gemBuf->width*2;
      }
      else
      {
         gc.flags= MESON_USE_VIDEO_PLANE;
         if ( sink->soc.secureVideo )
         {
            gc.flags |= MESON_USE_PROTECTED;
         }
         if ( i == 0 )
         {
            gc.size= gemBuf->width*gemBuf->height;
         }
         else
         {
            gc.size= gemBuf->width*gemBuf->height/2;
         }
         gemBuf->stride[i]= gemBuf->width;
      }
      gemBuf->size[i]= gc.size;

      rc= ioctl( sink->soc.drmFd, DRM_IOCTL_MESON_GEM_CREATE, &gc );
      if ( rc < 0 )
      {
         GST_ERROR("Failed to create gem buffer: plane %d DRM_IOCTL_MESON_GEM_CREATE rc %d", i, rc);
         goto exit;
      }

      gemBuf->handle[i]= gc.handle;
      gemBuf->offset[i]= 0;

      rc= drmPrimeHandleToFD( sink->soc.drmFd, gemBuf->handle[i], DRM_CLOEXEC | DRM_RDWR, &gemBuf->fd[i] );
      if ( rc < 0 )
      {
         GST_ERROR("Failed to get fd for gem buf plane %d: handle %d drmPrimeHandleToFD rc %d",
                   i, gemBuf->handle[i], rc );
         goto exit;
      }

      GST_DEBUG("create gem buf plane %d size (%dx%d) %u bytes stride %d offset %d handle %d fd %d",
                i, gemBuf->width, gemBuf->height, gemBuf->size[i],
                 gemBuf->stride[i], gemBuf->offset[i], gemBuf->handle[i], gemBuf->fd[i] );
   }

   result= true;

exit:

   return result;
}

#define DEFAULT_DRM_NAME "/dev/dri/renderD128"

static bool wstSVPSetupOutputBuffersDmabuf( GstWesterosSink *sink )
{
   bool result= false;
   int len= strlen( (char*)sink->soc.caps.driver );
   if ( (len == 14) && !strncmp( (char*)sink->soc.caps.driver, "aml-vcodec-dec", len) )
   {
      int i, j, fd, rc;
      struct v4l2_buffer *bufOut;
      int32_t bufferType;
      int numPlanes, width, height;
      const char *drmName;

      drmName= getenv("WESTEROS_SINK_DRM_NAME");
      if ( !drmName )
      {
         drmName= DEFAULT_DRM_NAME;
      }

      sink->soc.drmFd= open( drmName, O_RDWR );
      if ( sink->soc.drmFd < 0 )
      {
         GST_ERROR("Failed to open drm render node: %d", errno);
         goto exit;
      }

      if ( sink->soc.isMultiPlane )
      {
         bufferType= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
         numPlanes= sink->soc.fmtOut.fmt.pix_mp.num_planes;
         width= sink->soc.fmtOut.fmt.pix_mp.width;
         height= sink->soc.fmtOut.fmt.pix_mp.height;
      }
      else
      {
         bufferType= V4L2_BUF_TYPE_VIDEO_CAPTURE;
         numPlanes= 1;
         width= sink->soc.fmtOut.fmt.pix.width;
         height= sink->soc.fmtOut.fmt.pix.height;
      }

      for( i= 0; i < sink->soc.numBuffersOut; ++i )
      {
         for( j= 0; j < WST_MAX_PLANES; ++j )
         {
            sink->soc.outBuffers[i].gemBuf.fd[j]= -1;
         }
      }

      for( i= 0; i < sink->soc.numBuffersOut; ++i )
      {
         sink->soc.outBuffers[i].gemBuf.planeCount= numPlanes;
         sink->soc.outBuffers[i].gemBuf.width= width;
         sink->soc.outBuffers[i].gemBuf.height= height;
         if ( !wstSVPCreateGemBuffer( sink, &sink->soc.outBuffers[i].gemBuf ) )
         {
            GST_ERROR("Failed to allocate gem buffer");
            goto exit;
         }

         bufOut= &sink->soc.outBuffers[i].buf;
         bufOut->type= bufferType;
         bufOut->index= i;
         bufOut->memory= sink->soc.outputMemMode;
         if ( sink->soc.isMultiPlane )
         {
            memset( sink->soc.outBuffers[i].planes, 0, sizeof(struct v4l2_plane)*WST_MAX_PLANES);
            bufOut->m.planes= sink->soc.outBuffers[i].planes;
            bufOut->length= numPlanes;
         }

         rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_QUERYBUF, bufOut );
         if ( rc < 0 )
         {
            GST_ERROR("wstSVPSetupDmabufOutputBuffer: failed to query input buffer %d: rc %d errno %d", i, rc, errno);
            goto exit;
         }

         if ( sink->soc.isMultiPlane )
         {
            sink->soc.outBuffers[i].planeCount= numPlanes;
         }

         for( j= 0; j < sink->soc.outBuffers[i].gemBuf.planeCount; ++j )
         {
            fd= sink->soc.outBuffers[i].gemBuf.fd[j];
            bufOut->m.planes[j].m.fd= fd;
            sink->soc.outBuffers[i].planeInfo[j].fd= fd;
         }

         /* Use fd of first plane to identify buffer */
         sink->soc.outBuffers[i].fd= sink->soc.outBuffers[i].planeInfo[0].fd;
      }

      result= true;

   exit:
      if ( !result )
      {
         wstSVPTearDownOutputBuffersDmabuf( sink );
      }
   }
   else
   {
      GST_ERROR("Not aml-vcodec-dec: driver (%s) - no output dmabuf support", sink->soc.caps.driver );
   }

   return result;
}

static void wstSVPTearDownOutputBuffersDmabuf( GstWesterosSink *sink )
{
   int len= strlen( (char*)sink->soc.caps.driver );

   if ( (len == 14) && !strncmp( (char*)sink->soc.caps.driver, "aml-vcodec-dec", len) )
   {
      int i;
      for( i= 0; i < sink->soc.numBuffersOut; ++i )
      {
         wstSVPDestroyGemBuffer( sink, &sink->soc.outBuffers[i].gemBuf );
      }
      if ( sink->soc.drmFd >= 0 )
      {
         close( sink->soc.drmFd );
         sink->soc.drmFd= -1;
      }
   }
   else
   {
      GST_ERROR("Not aml-vcodec-dec: driver (%s) - no output dmabuf support", sink->soc.caps.driver );
   }
}
