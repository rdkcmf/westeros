/*
 * Copyright (C) 2016 RDK Management
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "../svp-util.h"

#define V4L2_CID_USER_AMLOGIC_BASE (V4L2_CID_USER_BASE + 0x1100)
#define AML_V4L2_SET_DRMMODE (V4L2_CID_USER_AMLOGIC_BASE + 0)
#define AML_V4L2_GET_INPUT_BUFFER_NUM (V4L2_CID_USER_AMLOGIC_BASE + 1)
#define AML_V4L2_SET_DURATION (V4L2_CID_USER_AMLOGIC_BASE + 2)
#define AML_V4L2_GET_FILMGRAIN_INFO (V4L2_CID_USER_AMLOGIC_BASE + 3)

enum vdec_dw_mode
{
   VDEC_DW_AFBC_ONLY = 0,
   VDEC_DW_AFBC_1_1_DW = 1,
   VDEC_DW_AFBC_1_4_DW = 2,
   VDEC_DW_AFBC_x2_1_4_DW = 3,
   VDEC_DW_AFBC_1_2_DW = 4,
   VDEC_DW_NO_AFBC = 16,
   VDEC_DW_AFBC_AUTO_1_2 = 0x100,
   VDEC_DW_AFBC_AUTO_1_4 = 0x200,
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
   uint32_t low_latency_mode;
   uint32_t uvm_hook_type;
   /*
    * bit 16       : force progressive output flag.
    * bit 15       : enable nr.
    * bit 14       : enable di local buff.
    * bit 13       : report downscale yuv buffer size flag.
    * bit 12       : for second field pts mode.default value 1.
    * bit 1        : Non-standard dv flag.
    * bit 0        : dv two layer flag.
    */
   uint32_t metadata_config_flag; // for metadata config flag
   uint32_t data[5];
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
   uint32_t reorder_margin;
   uint32_t field;
   uint32_t data[3];
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

static bool configForFilmGrain( GstWesterosSink *sink )
{
   bool result= false;
   int fd= -1;
   char valstr[64];
   const char* path= "/sys/class/video/film_grain_support";
   uint32_t val= 0;
   int rc;
   struct v4l2_control ctl;

   GST_LOG("configForFilmGrain: enter");
   fd= open(path, O_RDONLY|O_CLOEXEC);
   if ( fd < 0 )
   {
      GST_DEBUG("unable to open file (%s)", path);
      goto exit;
   }

   memset(valstr, 0, sizeof(valstr));
   read(fd, valstr, sizeof(valstr) - 1);
   valstr[strlen(valstr)] = '\0';

   if ( sscanf(valstr, "%u", &val) < 1)
   {
      GST_DEBUG("unable to get flag from: (%s)", valstr);
      goto exit;
   }

   GST_LOG("got film_grain_support:%d from node", val);
   if(val != 0)
   {
      goto exit;
   }

   memset( &ctl, 0, sizeof(ctl));
   ctl.id= AML_V4L2_GET_FILMGRAIN_INFO;
   rc= IOCTL( sink->soc.v4l2Fd, VIDIOC_G_CTRL, &ctl );
   GST_LOG("got VIDIOC_G_CTRL value: %d", ctl.value);
   if(ctl.value == 0)
   {
      goto exit;
   }

   result= true;

exit:
   if ( fd >= 0 )
   {
      close(fd);
   }
   GST_LOG("configForFilmGrain: exit: result %d", result);
   return result;
}

static void wstSVPDecoderConfig( GstWesterosSink *sink )
{
   int rc;
   int frameWidth, frameHeight;
   const char *env, *fmt;
   struct v4l2_streamparm streamparm;
   struct aml_dec_params *decParm= (struct aml_dec_params*)streamparm.parm.raw_data;
   memset( &streamparm, 0, sizeof(streamparm) );
   streamparm.type= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
   decParm->parms_status= V4L2_CONFIG_PARM_DECODE_CFGINFO;

   if ( sink->soc.useImmediateOutput )
   {
      GST_DEBUG("enable low latency mode");
      decParm->cfg.low_latency_mode= 1;
   }

   /*set bit12 value to 1,
    *v4l2 output 0 pts of second interlace field frame */
   decParm->cfg.metadata_config_flag |= (1 << 12);

   /* have base and enhancement layers both, that means its dual layer, dv two layer flag will be true */
   if ( (sink->soc.dvBaseLayerPresent == 1) && (sink->soc.dvEnhancementLayerPresent == 1) )
   {
      decParm->cfg.metadata_config_flag |= (1 << 0);
   }
   else
   {
      decParm->cfg.metadata_config_flag |= (0 << 0);
   }

   /* have one of then, it's standard dv stream, Non-standard dv flag will be false */
   if ( (sink->soc.dvBaseLayerPresent == 0) && (sink->soc.dvEnhancementLayerPresent == 0) )
   {
      decParm->cfg.metadata_config_flag |= (1 << 1);
   }
   else
   {
      decParm->cfg.metadata_config_flag |= (0 << 1);
   }

   switch( sink->soc.inputFormat )
   {
      default:
      case V4L2_PIX_FMT_MPEG:
      case V4L2_PIX_FMT_H264:
         decParm->cfg.double_write_mode= VDEC_DW_NO_AFBC;
         break;
      case V4L2_PIX_FMT_HEVC:
      case V4L2_PIX_FMT_VP9:
      #ifdef V4L2_PIX_FMT_AV1
      case V4L2_PIX_FMT_AV1:
      #endif
         switch ( sink->soc.inputFormat )
         {
            case V4L2_PIX_FMT_HEVC:
               fmt= "HEVC";
               break;
            case V4L2_PIX_FMT_VP9:
               fmt= "VP9";
               break;
            #ifdef V4L2_PIX_FMT_AV1
            case V4L2_PIX_FMT_AV1:
               fmt= "AV1";
               break;
            #endif
            default:
               fmt= "?";
               break;
         }
         if ( sink->soc.lowMemoryMode )
         {
            decParm->cfg.double_write_mode=VDEC_DW_AFBC_ONLY;
            GST_DEBUG("format %s dw mode afbc only", fmt);
            GST_WARNING("NOTE: afbc only is incompatible with video textures");
            break;
         }
         frameWidth= sink->soc.frameWidthStream;
         frameHeight= sink->soc.frameHeightStream;
         #ifdef WESTEROS_SINK_LOW_MEM_DWMODE
         decParm->cfg.double_write_mode= VDEC_DW_AFBC_AUTO_1_4;
         #else
         decParm->cfg.double_write_mode= VDEC_DW_AFBC_AUTO_1_2;
         if ( (sink->soc.interlaced && (sink->soc.inputFormat == V4L2_PIX_FMT_HEVC)) ||
              configForFilmGrain(sink) )
         {
            decParm->cfg.double_write_mode= VDEC_DW_AFBC_1_1_DW;
            GST_DEBUG("set dw mode 1:1 for H265 interlaced or film grain. format %s dw mode %d",
                      fmt, decParm->cfg.double_write_mode);
         }
         #endif
         decParm->cfg.metadata_config_flag |= (1<<13);
         GST_DEBUG("format %s size %dx%d dw mode %d",
                    fmt, frameWidth, frameHeight, decParm->cfg.double_write_mode);
         break;
   }

   g_print("metadata_config_flag 0x%x\n", decParm->cfg.metadata_config_flag);

   env= getenv("WESTEROS_SINK_AMLOGIC_DW_MODE");
   if ( env )
   {
      int dwMode= atoi(env);
      switch( dwMode )
      {
         case 0: case 1: case 2: case 3: case 4: case 16: case 256: case 512:
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
   if (sink->soc.lowMemoryMode)
   {
      decParm->cfg.ref_buf_margin= 5;
   }
   else
   {
      decParm->cfg.ref_buf_margin= 7;
   }

   if ( sink->soc.haveColorimetry ||
        sink->soc.haveMasteringDisplay ||
        sink->soc.haveContentLightLevel )
   {
      GST_DEBUG("Adding HDR info to VIDIOC_S_PARM: have: colorimetry %d mastering %d contentLightLevel %d",
                 sink->soc.haveColorimetry, sink->soc.haveMasteringDisplay, sink->soc.haveContentLightLevel );
      decParm->parms_status |= V4L2_CONFIG_PARM_DECODE_HDRINFO;
      if ( sink->soc.haveColorimetry )
      {
         decParm->hdr.signal_type= (1<<29); /* present flag */

         /*set default value, this is to keep up with driver hdr info synchronization*/
         decParm->hdr.signal_type |= (5<<26) | (1<<24);

         /* range */
         switch( sink->soc.hdrColorimetry[0] )
         {
            case GST_VIDEO_COLOR_RANGE_0_255:
            case GST_VIDEO_COLOR_RANGE_16_235:
               decParm->hdr.signal_type |= ((sink->soc.hdrColorimetry[0] % 2)<<25);
               break;
            default:
               break;
         }
         /* matrix coefficient */
         switch( sink->soc.hdrColorimetry[1] )
         {
            case GST_VIDEO_COLOR_MATRIX_RGB: /* RGB */
               decParm->hdr.signal_type |= 0;
               break;
            case GST_VIDEO_COLOR_MATRIX_FCC: /* FCC */
               decParm->hdr.signal_type |= 4;
               break;
            case GST_VIDEO_COLOR_MATRIX_BT709: /* BT709 */
               decParm->hdr.signal_type |= 1;
               break;
            case GST_VIDEO_COLOR_MATRIX_BT601: /* BT601 */
               decParm->hdr.signal_type |= 3;
               break;
            case GST_VIDEO_COLOR_MATRIX_SMPTE240M: /* SMPTE240M */
               decParm->hdr.signal_type |= 7;
               break;
            case GST_VIDEO_COLOR_MATRIX_BT2020: /* BT2020 */
               decParm->hdr.signal_type |= 9;
               break;
            default: /* unknown */
               decParm->hdr.signal_type |= 2;
               break;
         }
         /* transfer function */
         switch( sink->soc.hdrColorimetry[2] )
         {
            case GST_VIDEO_TRANSFER_BT709: /* BT709 */
               decParm->hdr.signal_type |= (1<<8);
               break;
            case GST_VIDEO_TRANSFER_SMPTE240M: /* SMPTE240M */
               decParm->hdr.signal_type |= (7<<8);
               break;
            case GST_VIDEO_TRANSFER_LOG100: /* LOG100 */
               decParm->hdr.signal_type |= (9<<8);
               break;
            case GST_VIDEO_TRANSFER_LOG316: /* LOG316 */
               decParm->hdr.signal_type |= (10<<8);
               break;
            case GST_VIDEO_TRANSFER_BT2020_12: /* BT2020_12 */
               decParm->hdr.signal_type |= (15<<8);
               break;
            case GST_VIDEO_TRANSFER_BT2020_10: /* BT2020_10 */
               decParm->hdr.signal_type |= (14<<8);
               break;
            #if ((GST_VERSION_MAJOR == 1) && (GST_VERSION_MINOR >= 18))
            case GST_VIDEO_TRANSFER_SMPTE2084: /* SMPTE2084 */
            #else
            case GST_VIDEO_TRANSFER_SMPTE_ST_2084: /* SMPTE2084 */
            #endif
               decParm->hdr.signal_type |= (16<<8);
               break;
            case GST_VIDEO_TRANSFER_ARIB_STD_B67: /* ARIB_STD_B67 */
               decParm->hdr.signal_type |= (18<<8);
               break;
            #if ((GST_VERSION_MAJOR == 1) && (GST_VERSION_MINOR >= 18))
            case GST_VIDEO_TRANSFER_BT601: /* BT601 */
               decParm->hdr.signal_type |= (3<<8);
               break;
            #endif
            case GST_VIDEO_TRANSFER_GAMMA10: /* GAMMA10 */
            case GST_VIDEO_TRANSFER_GAMMA18: /* GAMMA18 */
            case GST_VIDEO_TRANSFER_GAMMA20: /* GAMMA20 */
            case GST_VIDEO_TRANSFER_GAMMA22: /* GAMMA22 */
            case GST_VIDEO_TRANSFER_SRGB: /* SRGB */
            case GST_VIDEO_TRANSFER_GAMMA28: /* GAMMA28 */
            case GST_VIDEO_TRANSFER_ADOBERGB: /* ADOBERGB */
            default:
               break;
         }
         /* primaries */
         switch( sink->soc.hdrColorimetry[3] )
         {
            case GST_VIDEO_COLOR_PRIMARIES_BT709: /* BT709 */
               decParm->hdr.signal_type |= ((1<<24)|(1<<16));
               break;
            case GST_VIDEO_COLOR_PRIMARIES_BT470M: /* BT470M */
               decParm->hdr.signal_type |= ((1<<24)|(4<<16));
               break;
            case GST_VIDEO_COLOR_PRIMARIES_BT470BG: /* BT470BG */
               decParm->hdr.signal_type |= ((1<<24)|(5<<16));
               break;
            case GST_VIDEO_COLOR_PRIMARIES_SMPTE170M: /* SMPTE170M */
               decParm->hdr.signal_type |= ((1<<24)|(6<<16));
               break;
            case GST_VIDEO_COLOR_PRIMARIES_SMPTE240M: /* SMPTE240M */
               decParm->hdr.signal_type |= ((1<<24)|(7<<16));
               break;
            case GST_VIDEO_COLOR_PRIMARIES_FILM: /* FILM */
               decParm->hdr.signal_type |= ((1<<24)|(8<<16));
               break;
            case GST_VIDEO_COLOR_PRIMARIES_BT2020: /* BT2020 */
               decParm->hdr.signal_type |= ((1<<24)|(9<<16));
               break;
            case GST_VIDEO_COLOR_PRIMARIES_ADOBERGB: /* ADOBERGB */
            #if ((GST_VERSION_MAJOR == 1) && (GST_VERSION_MINOR >= 18))
            case GST_VIDEO_COLOR_PRIMARIES_SMPTEST428:
            case GST_VIDEO_COLOR_PRIMARIES_SMPTERP431:
            case GST_VIDEO_COLOR_PRIMARIES_SMPTEEG432:
            case GST_VIDEO_COLOR_PRIMARIES_EBU3213:
            #endif
            default:
               break;
         }
         GST_DEBUG("HDR signal_type %X", decParm->hdr.signal_type);
      }
      if ( sink->soc.haveMasteringDisplay )
      {
         decParm->hdr.color_parms.present_flag= 1;
         decParm->hdr.color_parms.primaries[2][0]= (uint32_t)(sink->soc.hdrMasteringDisplay[0]*50000); /* R.x */
         decParm->hdr.color_parms.primaries[2][1]= (uint32_t)(sink->soc.hdrMasteringDisplay[1]*50000); /* R.y */
         decParm->hdr.color_parms.primaries[0][0]= (uint32_t)(sink->soc.hdrMasteringDisplay[2]*50000); /* G.x */
         decParm->hdr.color_parms.primaries[0][1]= (uint32_t)(sink->soc.hdrMasteringDisplay[3]*50000); /* G.y */
         decParm->hdr.color_parms.primaries[1][0]= (uint32_t)(sink->soc.hdrMasteringDisplay[4]*50000); /* B.x */
         decParm->hdr.color_parms.primaries[1][1]= (uint32_t)(sink->soc.hdrMasteringDisplay[5]*50000); /* B.y */
         decParm->hdr.color_parms.white_point[0]= (uint32_t)(sink->soc.hdrMasteringDisplay[6]*50000);
         decParm->hdr.color_parms.white_point[1]= (uint32_t)(sink->soc.hdrMasteringDisplay[7]*50000);
         decParm->hdr.color_parms.luminance[0]= (uint32_t)(sink->soc.hdrMasteringDisplay[8]);
         decParm->hdr.color_parms.luminance[1]= (uint32_t)(sink->soc.hdrMasteringDisplay[9]);
         GST_DEBUG("HDR mastering: primaries %X %X %X %X %X %X",
             decParm->hdr.color_parms.primaries[2][0],
             decParm->hdr.color_parms.primaries[2][1],
             decParm->hdr.color_parms.primaries[0][0],
             decParm->hdr.color_parms.primaries[0][1],
             decParm->hdr.color_parms.primaries[1][0],
             decParm->hdr.color_parms.primaries[1][1] );
         GST_DEBUG("HDR mastering: white point: %X %X",
             decParm->hdr.color_parms.white_point[0],
             decParm->hdr.color_parms.white_point[1] );
         GST_DEBUG("HDR mastering: luminance: %X %X",
             decParm->hdr.color_parms.luminance[0],
             decParm->hdr.color_parms.luminance[1] );
      }
      if ( sink->soc.haveContentLightLevel )
      {
         decParm->hdr.color_parms.content_light_level.max_content= sink->soc.hdrContentLightLevel[0];
         decParm->hdr.color_parms.content_light_level.max_pic_average= sink->soc.hdrContentLightLevel[1];
         GST_DEBUG("HDR contentLightLevel: %X %X",
             decParm->hdr.color_parms.content_light_level.max_content,
             decParm->hdr.color_parms.content_light_level.max_pic_average );
      }
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
         gemBuf->handle[i]= 0;
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

      sink->soc.drmFd= open( drmName, O_RDWR|O_CLOEXEC );
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
