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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <unistd.h>

#include "westeros-sink.h"
#include "wayland-egl.h"

#define PREROLL_OFFSET (0LL)

#define MODULE_BCMHOST "libbcm_host.so"
#define METHOD_BCM_HOST_INIT "bcm_host_init"
#define METHOD_BCM_HOST_DEINIT "bcm_host_deinit"

#define MODULE_OPENMAX "libopenmaxil.so"
#define METHOD_OMX_INIT "OMX_Init"
#define METHOD_OMX_DEINIT "OMX_Deinit"
#define METHOD_OMX_GETHANDLE "OMX_GetHandle"
#define METHOD_OMX_FREEHANDLE "OMX_FreeHandle"
#define METHOD_OMX_SETUPTUNNEL "OMX_SetupTunnel"

#ifdef GLIB_VERSION_2_32
  #define LOCK_SOC( sink ) g_mutex_lock( &((sink)->soc.mutex) );
  #define UNLOCK_SOC( sink ) g_mutex_unlock( &((sink)->soc.mutex) );
#else
  #define LOCK_SOC( sink ) g_mutex_lock( (sink)->soc.mutex );
  #define UNLOCK_SOC( sink ) g_mutex_unlock( (sink)->soc.mutex );
#endif

GST_DEBUG_CATEGORY_EXTERN (gst_westeros_sink_debug);
#define GST_CAT_DEFAULT gst_westeros_sink_debug

static OMX_ERRORTYPE omxComponentSetState( GstWesterosSink *sink, OMX_HANDLETYPE hComp, OMX_STATETYPE state );
static void flushComponents( GstWesterosSink *sink );
static void resetClock( GstWesterosSink *sink );
static void transitionToRenderer( GstWesterosSink *sink, WstOmxComponent *rend );
static bool setupEGL( GstWesterosSink *sink );
static void termEGL( GstWesterosSink *sink );
static GLuint createShader(GstWesterosSink *sink, GLenum shaderType, const char *shaderSource );
static bool setupGfx( GstWesterosSink *sink );
static void termGfx( GstWesterosSink *sink );
static void createImage( GstWesterosSink *sink );
static void destroyImage( GstWesterosSink *sink );
static void drawImage( GstWesterosSink *sink );
static void processFrame( GstWesterosSink *sink, GstBuffer *buffer );
static gpointer captureThread(gpointer data);
static gboolean gst_westeros_sink_soc_query_element( GstElement *element, GstQuery *query );

extern "C"
{
struct wl_display *khrn_platform_get_wl_display();
}

static gboolean (*queryOrg)(GstElement *element, GstQuery *query)= 0;

static void sbFormat(void *data, struct wl_sb *wl_sb, uint32_t format)
{
   WESTEROS_UNUSED(wl_sb);
   GstWesterosSink *sink= (GstWesterosSink*)data;
   WESTEROS_UNUSED(sink);
   GST_DEBUG_OBJECT(sink,"westeros-sink-soc: registry: sbFormat: %X", format);
}
  
static const struct wl_sb_listener sbListener = {
	sbFormat
};

void gst_westeros_sink_soc_class_init(GstWesterosSinkClass *klass)
{
   GstElementClass *gstelement_class= (GstElementClass *) klass;

   queryOrg= gstelement_class->query;
   gstelement_class->query= gst_westeros_sink_soc_query_element;
}

gboolean gst_westeros_sink_soc_init( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   const char *moduleName;
   const char *methodName;
   void *module= 0;
   bool error= false;
   OMX_ERRORTYPE omxerr;
   
   #ifdef GLIB_VERSION_2_32 
   g_mutex_init( &sink->soc.mutex );
   g_mutex_init( &sink->soc.mutexNewFrame );
   g_cond_init( &sink->soc.condNewFrame );
   #else
   sink->soc.mutex= g_mutex_new();
   sink->soc.mutexNewFrame= g_mutex_new();
   sink->soc.condNewFrame= g_cond_new();
   #endif

   sink->soc.bcmHostIsInit= false;
   sink->soc.omxIsInit= false;
   memset( &sink->soc.vidDec, 0, sizeof(WstOmxComponent) );
   memset( &sink->soc.vidSched, 0, sizeof(WstOmxComponent) );
   memset( &sink->soc.vidRend, 0, sizeof(WstOmxComponent) );
   memset( &sink->soc.eglRend, 0, sizeof(WstOmxComponent) );
   memset( &sink->soc.clock, 0, sizeof(WstOmxComponent) );
   sink->soc.asyncStateSet.done= false;
   sink->soc.asyncError.done= false;
   sink->soc.tunnelActiveClock= false;
   sink->soc.tunnelActiveVidDec= false;
   sink->soc.tunnelActiveVidSched= false;
   sink->soc.semInputActive= false;
   sink->soc.capacityInputBuffers= 0;
   sink->soc.countInputBuffers= 0;
   sink->soc.inputBuffers= 0;
   sink->soc.firstBuffer= true;
   sink->soc.decoderReady= false;
   sink->soc.schedReady= false;
   sink->soc.playingVideo= false;
   sink->soc.useGfxPath= false;
   sink->soc.rend= &sink->soc.vidRend;
   sink->soc.sb= 0;
   sink->soc.quitCaptureThread= TRUE;
   sink->soc.captureThread= NULL;
   sink->soc.buffCurrent= 0;
   sink->soc.sharedWLDisplay= false;
   sink->soc.eglSetup= false;
   sink->soc.eglDisplay= EGL_NO_DISPLAY;
   sink->soc.eglContext= EGL_NO_CONTEXT;
   sink->soc.eglSurface= EGL_NO_SURFACE;
   sink->soc.wlEGLWindow= 0;
   sink->soc.gfxSetup= false;
   sink->soc.eglCreateImageKHR= 0;
   sink->soc.eglDestroyImageKHR= 0;
   sink->soc.pEGLBufferHeader= 0;
   sink->soc.textureId= 0;
   sink->soc.eglImage= 0;
   sink->soc.textureWidth= 0;
   sink->soc.textureHeight= 0;
   sink->soc.newFrame= false;
   sink->soc.videoOutputChanged= true;
   sink->soc.frameWidth= 0;
   sink->soc.frameHeight= 0;
   #ifdef USE_GLES2
   sink->soc.progId= 0;
   sink->soc.vertId= 0;
   sink->soc.fragId= 0;
   sink->soc.posLoc= 0;
   sink->soc.uvLoc= 1;

   memset( sink->soc.matrix, 0, sizeof(sink->soc.matrix));
   sink->soc.matrix[0]= 1.0f;
   sink->soc.matrix[5]= 1.0f;
   sink->soc.matrix[10]= 1.0f;
   sink->soc.matrix[15]= 1.0f;
   #endif

   moduleName= MODULE_BCMHOST;
   module= dlopen( moduleName, RTLD_NOW );
   if ( !module )
   {
      GST_ERROR("gst_westeros_sink_soc_init: failed to load module (%s)", moduleName);
      GST_ERROR("  detail: %s", dlerror() );
      error= true;
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_init: loaded module %s : %p", moduleName, module );

   methodName= METHOD_BCM_HOST_INIT;
   sink->soc.bcm_host_init= (BcmHostInit_t)dlsym( module, methodName );
   if ( !sink->soc.bcm_host_init )
   {
      GST_ERROR("gst_westeros_sink_soc_init: failed to find module (%s) method (%s)", moduleName, methodName);
      GST_ERROR("  detail: %s", dlerror() );
      error= true;
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_init: loaded module %s method %s: %p", moduleName, methodName, sink->soc.bcm_host_init );

   methodName= METHOD_BCM_HOST_DEINIT;
   sink->soc.bcm_host_deinit= (BcmHostInit_t)dlsym( module, methodName );
   if ( !sink->soc.bcm_host_deinit )
   {
      GST_ERROR("gst_westeros_sink_soc_init: failed to find module (%s) method (%s)", moduleName, methodName);
      GST_ERROR("  detail: %s", dlerror() );
      error= true;
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_init: loaded module %s method %s: %p", moduleName, methodName, sink->soc.bcm_host_deinit );

   sink->soc.moduleBcmHost= module;
   module= 0;      
   
   
   moduleName= MODULE_OPENMAX;
   module= dlopen( moduleName, RTLD_NOW );
   if ( !module )
   {
      GST_ERROR("gst_westeros_sink_soc_init: failed to load module (%s)", moduleName);
      GST_ERROR("  detail: %s", dlerror() );
      error= true;
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_init: loaded module %s : %p", moduleName, module );

   methodName= METHOD_OMX_INIT;
   sink->soc.OMX_Init= (OMX_Init_t)dlsym( module, methodName );
   if ( !sink->soc.OMX_Init )
   {
      GST_ERROR("gst_westeros_sink_soc_init: failed to find module (%s) method (%s)", moduleName, methodName);
      GST_ERROR("  detail: %s", dlerror() );
      error= true;
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_init: loaded module %s method %s: %p", moduleName, methodName, sink->soc.OMX_Init );

   methodName= METHOD_OMX_DEINIT;
   sink->soc.OMX_Deinit= (OMX_Deinit_t)dlsym( module, methodName );
   if ( !sink->soc.OMX_Deinit )
   {
      GST_ERROR("gst_westeros_sink_soc_init: failed to find module (%s) method (%s)", moduleName, methodName);
      GST_ERROR("  detail: %s", dlerror() );
      error= true;
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_init: loaded module %s method %s: %p", moduleName, methodName, sink->soc.OMX_Deinit );

   methodName= METHOD_OMX_GETHANDLE;
   sink->soc.OMX_GetHandle= (OMX_GetHandle_t)dlsym( module, methodName );
   if ( !sink->soc.OMX_GetHandle )
   {
      GST_ERROR("gst_westeros_sink_soc_init: failed to find module (%s) method (%s)", moduleName, methodName);
      GST_ERROR("  detail: %s", dlerror() );
      error= true;
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_init: loaded module %s method %s: %p", moduleName, methodName, sink->soc.OMX_GetHandle );

   methodName= METHOD_OMX_FREEHANDLE;
   sink->soc.OMX_FreeHandle= (OMX_FreeHandle_t)dlsym( module, methodName );
   if ( !sink->soc.OMX_FreeHandle )
   {
      GST_ERROR("gst_westeros_sink_soc_init: failed to find module (%s) method (%s)", moduleName, methodName);
      GST_ERROR("  detail: %s", dlerror() );
      error= true;
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_init: loaded module %s method %s: %p", moduleName, methodName, sink->soc.OMX_FreeHandle );

   methodName= METHOD_OMX_SETUPTUNNEL;
   sink->soc.OMX_SetupTunnel= (OMX_SetupTunnel_t)dlsym( module, methodName );
   if ( !sink->soc.OMX_SetupTunnel )
   {
      GST_ERROR("gst_westeros_sink_soc_init: failed to find module (%s) method (%s)", moduleName, methodName);
      GST_ERROR("  detail: %s", dlerror() );
      error= true;
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_init: loaded module %s method %s: %p", moduleName, methodName, sink->soc.OMX_SetupTunnel );

   sink->soc.moduleOpenMax= module;
   module= 0;

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_deinit: calling bcm_host_init" );
   sink->soc.bcm_host_init();
   sink->soc.bcmHostIsInit= true;
   
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_deinit: calling OMX_Init" );
   omxerr= sink->soc.OMX_Init();
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_deinit: done calling OMX_Init: omxerr %d", omxerr );
      goto exit;
   }
   sink->soc.omxIsInit= true;

   /*
    * The userland EGL implementation has an implicit restriction of each process only being able to
    * interact as a wayland-egl client of one wayland display.  This breaks applications that use
    * wayland-egl for graphics and also use westeros-sink for video.  To workaround this, the rpi
    * wesrteros-sink soc code tries to detect and share any existing wl_display connection.
    */
   sink->display= khrn_platform_get_wl_display();
   GST_DEBUG("gst_westeros_sink_soc_init: khrn_platform_get_wl_display returns %p\n", sink->display);
   if ( sink->display )
   {
      sink->soc.sharedWLDisplay= true;
   }
   
   result= TRUE;

exit:
   if ( error )
   {
      if ( module )
      {
         dlclose( module );
      }
   }
   
   return result;
}

void gst_westeros_sink_soc_term( GstWesterosSink *sink )
{
   if ( sink->soc.sb )
   {
      wl_sb_destroy( sink->soc.sb );
      sink->soc.sb= 0;
   }

   if ( sink->soc.sharedWLDisplay )
   {
      /* We are sharing a wl_display connection.  Clear the reference so the
       * generic sink term code doesn't destroy the display out from under
       * the application which is also using the display for graphics */
      sink->display= 0;
   }

   if ( sink->soc.OMX_Deinit )
   {
      sink->soc.OMX_Deinit();
   }
   
   if ( sink->soc.moduleOpenMax )
   {
      dlclose( sink->soc.moduleOpenMax );
      sink->soc.moduleOpenMax= 0;
   }

   if ( sink->soc.bcm_host_deinit )
   {
      sink->soc.bcm_host_deinit();
   }
   
   if ( sink->soc.moduleBcmHost )
   {
      dlclose( sink->soc.moduleBcmHost );
      sink->soc.moduleBcmHost= 0;
   }

   #ifdef GLIB_VERSION_2_32 
   g_mutex_clear( &sink->soc.mutex );
   g_mutex_clear( &sink->soc.mutexNewFrame );
   g_cond_clear( &sink->soc.condNewFrame );
   #else
   g_mutex_free( sink->soc.mutex );
   g_mutex_free( sink->soc.mutexNewFrame );
   g_cond_free( sink->soc.condNewFrame );
   #endif  
}

void gst_westeros_sink_soc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
   WESTEROS_UNUSED(object);
   WESTEROS_UNUSED(prop_id);
   WESTEROS_UNUSED(value);
   WESTEROS_UNUSED(pspec);
}

void gst_westeros_sink_soc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
   WESTEROS_UNUSED(object);
   WESTEROS_UNUSED(prop_id);
   WESTEROS_UNUSED(value);
   WESTEROS_UNUSED(pspec);
}

void gst_westeros_sink_soc_registryHandleGlobal( GstWesterosSink *sink, 
                                 struct wl_registry *registry, uint32_t id,
		                           const char *interface, uint32_t version)
{
   WESTEROS_UNUSED(version);
   int len;

   len= strlen(interface);

   if ((len==5) && (strncmp(interface, "wl_sb", len) == 0)) 
   {
      sink->soc.sb= (struct wl_sb*)wl_registry_bind(registry, id, &wl_sb_interface, 1);
      GST_DEBUG_OBJECT(sink, "westeros-sink-soc: registry: sb %p", (void*)sink->soc.sb);
      wl_proxy_set_queue((struct wl_proxy*)sink->soc.sb, sink->queue);
		wl_sb_add_listener(sink->soc.sb, &sbListener, sink);
		GST_DEBUG_OBJECT(sink, "westeros-sink-soc: registry: done add sb listener");
   }
}

void gst_westeros_sink_soc_registryHandleGlobalRemove( GstWesterosSink *sink,
                                 struct wl_registry *registry,
			                        uint32_t name)
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(registry);
   WESTEROS_UNUSED(name);
}

static void waitForNewFrame( GstWesterosSink *sink )
{
  #ifdef GLIB_VERSION_2_32
  g_mutex_lock( &sink->soc.mutexNewFrame );
  g_cond_wait( &sink->soc.condNewFrame, &sink->soc.mutexNewFrame );
  g_mutex_unlock( &sink->soc.mutexNewFrame );
  #else
  g_mutex_lock( sink->soc.mutexNewFrame );
  g_cond_wait( sink->soc.condNewFrame, sink->soc.mutexNewFrame );
  g_mutex_unlock( sink->soc.mutexNewFrame );
  #endif
}


static void signalNewFrame( GstWesterosSink *sink )
{
  #ifdef GLIB_VERSION_2_32
  g_mutex_lock( &sink->soc.mutexNewFrame );
  g_cond_signal( &sink->soc.condNewFrame );
  g_mutex_unlock( &sink->soc.mutexNewFrame );
  #else
  g_mutex_lock( sink->soc.mutexNewFrame );
  g_cond_signal( sink->soc.condNewFrame );
  g_mutex_unlock( sink->soc.mutexNewFrame );
  #endif
}

static const char *omxStateName( OMX_STATETYPE state )
{
   const char *stateName;
   switch( state ) {
      case OMX_StateInvalid: stateName= "OMX_StateInvalid"; break;
      case OMX_StateLoaded: stateName= "OMX_StateLoaded"; break;
      case OMX_StateIdle: stateName= "OMX_StateIdle"; break;
      case OMX_StateExecuting: stateName= "OMX_StateExecuting"; break;
      case OMX_StatePause: stateName= "OMX_StatePause"; break;
      case OMX_StateWaitForResources: stateName= "OMX_StateWaitForResources"; break;
      default: stateName= "Unknown"; break;
   }
   return stateName;
}

static const char *omxComponentName( GstWesterosSink *sink, OMX_IN OMX_HANDLETYPE hComponent )
{
   const char *componentName= "unknown";

   if ( hComponent == sink->soc.vidDec.hComp )
   {
      componentName= sink->soc.vidDec.name;
   }
   else if ( hComponent == sink->soc.vidSched.hComp )
   {
      componentName= sink->soc.vidSched.name;
   }
   else if ( hComponent == sink->soc.clock.hComp )
   {
      componentName= sink->soc.clock.name;
   }
   else if ( hComponent == sink->soc.vidRend.hComp )
   {
      componentName= sink->soc.vidRend.name;
   }
   else if ( hComponent == sink->soc.eglRend.hComp )
   {
      componentName= sink->soc.eglRend.name;
   }
   return componentName;
}

static const char *omxPortName( GstWesterosSink *sink, OMX_U32 port )
{
   const char *portName= "unknown";

   if ( port == sink->soc.vidDec.vidInPort )
   {
      portName= "vidDecVidInput";
   }
   else if ( port == sink->soc.vidDec.vidOutPort )
   {
      portName= "vidDecVidOutput";
   }
   else if ( port == sink->soc.vidSched.vidInPort )
   {
      portName= "vidSchedVidInput";
   }
   else if ( port == sink->soc.vidSched.vidOutPort )
   {
      portName= "vidSchedVidOutput";
   }
   else if ( port == sink->soc.vidSched.otherInPort )
   {
      portName= "vidSchedOtherInput";
   }
   else if ( port == sink->soc.vidRend.vidInPort )
   {
      portName= "vidRendVidInput";
   }
   else if ( port == sink->soc.eglRend.vidInPort )
   {
      portName= "eglRendVidInput";
   }
   else if ( port == sink->soc.eglRend.vidOutPort )
   {
      portName= "eglRendVidOutput";
   }
   else if ( port == sink->soc.clock.otherOutPort )
   {
      portName= "clockOtherOutput";
   }
   return portName;
}

static const char *omxCommandName( OMX_U32 cmd )
{
   const char *name= "unknown";

   switch( cmd )
   {
      case OMX_CommandStateSet: name= "StateSet"; break;
      case OMX_CommandFlush: name= "Flush"; break;
      case OMX_CommandPortDisable: name= "PortDisable"; break;
      case OMX_CommandPortEnable: name= "PortEnable"; break;
      case OMX_CommandMarkBuffer: name= "MarkBuffer"; break;
   }
   return name;
}

static void omxDumpEvent( GstWesterosSink *sink,
                          OMX_IN OMX_HANDLETYPE hComponent,
                          OMX_IN OMX_PTR pAppData,
                          OMX_IN OMX_EVENTTYPE eEvent,
                          OMX_IN OMX_U32 nData1,
                          OMX_IN OMX_U32 nData2,
                          OMX_IN OMX_PTR pEventData )
{
   const char *compName= omxComponentName( sink, hComponent );
   switch ( eEvent )
   {
      case OMX_EventCmdComplete:
         switch( nData1 )
         {
            case OMX_CommandStateSet:
            case OMX_CommandFlush:
            case OMX_CommandMarkBuffer:
               printf("OMXEvent: CmdComplete: %s: cmd: %s n2 %d\n", compName, omxCommandName(nData1), nData2);
               break;
            case OMX_CommandPortDisable:
            case OMX_CommandPortEnable:
               printf("OMXEvent: CmdComplete: %s: cmd: %s %s (%d)\n", compName, omxCommandName(nData1), omxPortName(sink, nData2), nData2);
               break;
         }
         break;
      case OMX_EventError:
         printf("OMXEvent: Error: %s: type: %x n2 %d\n", compName, nData1, nData2);
         break;
      case OMX_EventPortSettingsChanged:
         printf("OMXEvent: PortSettingsChanged: %s: port: %s (%d) n2 %d\n", compName, omxPortName(sink, nData1), nData1, nData2);
         break;
      case OMX_EventBufferFlag:
         printf("OMXEvent: BufferFlag: %s: n1: %d n2 %d\n", compName, nData1, nData2);
         break;
      case OMX_EventResourcesAcquired:
         printf("OMXEvent: ResourcesAcquired: %s: n1: %d n2 %d\n", compName, nData1, nData2);
         break;
      case OMX_EventComponentResumed:
         printf("OMXEvent: ComponentResumed: %s: n1: %d n2 %d\n", compName, nData1, nData2);
         break;
      case OMX_EventDynamicResourcesAvailable:
         printf("OMXEvent: DynamicResourcesAvailable: %s: n1: %d n2 %d\n", compName, nData1, nData2);
         break;
      case OMX_EventPortFormatDetected:
         printf("OMXEvent: PortFormatDetected: %s: n1: %d n2 %d\n", compName, nData1, nData2);
         break;
      default:
         break;
   }
}

static OMX_AsyncResult *omxGetAsyncResult( GstWesterosSink *sink, OMX_U32 cmd, OMX_U32 nData1 )
{
   OMX_AsyncResult *pAsync= 0;
   
   switch( cmd )
   {
      case OMX_CommandStateSet:
         pAsync= &sink->soc.asyncStateSet;
         break;
      case OMX_CommandPortDisable:
      case OMX_CommandPortEnable:
      case OMX_CommandFlush:
         if ( nData1 == sink->soc.vidDec.vidInPort )
         {
            pAsync= &sink->soc.vidDec.asyncVidIn;
         }
         else if ( nData1 == sink->soc.vidDec.vidOutPort )
         {
            pAsync= &sink->soc.vidDec.asyncVidOut;
         }
         else if ( nData1 == sink->soc.vidSched.vidInPort )
         {
            pAsync= &sink->soc.vidSched.asyncVidIn;
         }
         else if ( nData1 == sink->soc.vidSched.vidOutPort )
         {
            pAsync= &sink->soc.vidSched.asyncVidOut;
         }
         else if ( nData1 == sink->soc.vidSched.otherInPort )
         {
            pAsync= &sink->soc.vidSched.asyncOtherIn;
         }
         else if ( nData1 == sink->soc.vidRend.vidInPort )
         {
            pAsync= &sink->soc.vidRend.asyncVidIn;
         }
         else if ( nData1 == sink->soc.eglRend.vidInPort )
         {
            pAsync= &sink->soc.eglRend.asyncVidIn;
         }
         else if ( nData1 == sink->soc.eglRend.vidOutPort )
         {
            pAsync= &sink->soc.eglRend.asyncVidOut;
         }
         else if ( nData1 == sink->soc.clock.otherOutPort )
         {
            pAsync= &sink->soc.clock.asyncOtherOut;
         }
      break;
   }
   
   return pAsync;
}

static OMX_ERRORTYPE omxEventHandler( OMX_IN OMX_HANDLETYPE hComponent,
                            OMX_IN OMX_PTR pAppData,
                            OMX_IN OMX_EVENTTYPE eEvent,
                            OMX_IN OMX_U32 nData1,
                            OMX_IN OMX_U32 nData2,
                            OMX_IN OMX_PTR pEventData )
{
   GstWesterosSink *sink= (GstWesterosSink*)pAppData;
   OMX_AsyncResult *pAsync= 0;
   bool eosDetected= false;
   GstDebugLevel level;

   GST_LOG("omxEventHandler: pAppData %p eEvent %d nData1 %x nData2 %d pEventData %p",
           pAppData, eEvent, nData1, nData2, pEventData );
   level= gst_debug_category_get_threshold( gst_westeros_sink_debug );
   if ( level >= GST_LEVEL_INFO )
   {
      omxDumpEvent( sink, hComponent, pAppData, eEvent, nData1, nData2, pEventData );
   }
   LOCK_SOC(sink);
   switch( eEvent )
   {
      case OMX_EventCmdComplete:
         pAsync= omxGetAsyncResult( sink, nData1, nData2 );
         break;
      case OMX_EventPortSettingsChanged:
         if ( nData1 == sink->soc.vidDec.vidOutPort )
         {
            sink->soc.videoOutputChanged= true;
         }
         break;
      case OMX_EventError:
         pAsync= &sink->soc.asyncError;
         break;
      case OMX_EventBufferFlag:
         eosDetected= true;
         break;
      default:
         break;
   }
   if ( pAsync )
   {
      pAsync->eEvent= eEvent;
      pAsync->nData1= nData1;
      pAsync->nData2= nData2;
      pAsync->done= true;
   }
   if ( (eEvent == OMX_EventPortSettingsChanged) && (nData1 == sink->soc.vidDec.vidOutPort) )
   {
      sink->soc.decoderReady= true;
   }
   else if ( (eEvent == OMX_EventPortSettingsChanged) && (nData1 == sink->soc.vidSched.vidOutPort) )
   {
      sink->soc.schedReady= true;
   }
   UNLOCK_SOC(sink);

   if ( eosDetected )
   {
      gst_westeros_sink_eos_detected( sink );       
   }

   return OMX_ErrorNone;
}

static OMX_ERRORTYPE omxEmptyBufferDone( OMX_IN OMX_HANDLETYPE hComponent,
                            OMX_IN OMX_PTR pAppData,
                            OMX_IN OMX_BUFFERHEADERTYPE* pBuffer )
{
   GstWesterosSink *sink= (GstWesterosSink*)pAppData;

   LOCK_SOC( sink );
   sink->soc.inputBuffers[sink->soc.countInputBuffers]= pBuffer;
   ++sink->soc.countInputBuffers;
   sem_post( &sink->soc.semInputBuffers );
   UNLOCK_SOC( sink );
   GST_LOG("omxEmptyBufferDone: buff %p buffers avail: %d of %d", pBuffer, sink->soc.countInputBuffers, sink->soc.capacityInputBuffers );

   return OMX_ErrorNone;
}

static OMX_ERRORTYPE omxFillBufferDone( OMX_IN OMX_HANDLETYPE hComponent,
                            OMX_IN OMX_PTR pAppData,
                            OMX_IN OMX_BUFFERHEADERTYPE* pBuffer )
{
   OMX_ERRORTYPE omxerr;
   GstWesterosSink *sink= (GstWesterosSink*)pAppData;

   if ( hComponent == sink->soc.eglRend.hComp )
   {
      GST_LOG("omxFillBufferDone for eglRender\n");

      sink->soc.newFrame= true;
      signalNewFrame( sink );

      if ( sink->soc.useGfxPath )
      {
         omxerr= OMX_FillThisBuffer( sink->soc.rend->hComp, sink->soc.pEGLBufferHeader );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("omxFillBufferDone: OMX_FillThisBuffer for eglRender: omxerr %x", omxerr );
         }
      }
   }
   return OMX_ErrorNone;
}

static OMX_CALLBACKTYPE omxCallbacks= {
   omxEventHandler,
   omxEmptyBufferDone,
   omxFillBufferDone
};

static OMX_ERRORTYPE omxWaitCommandComplete( GstWesterosSink *sink, OMX_COMMANDTYPE cmd, OMX_U32 nData1 )
{
   OMX_ERRORTYPE omxerr= OMX_ErrorNone;
   OMX_AsyncResult *pAsync= 0;
   OMX_AsyncResult asyncError;
   bool done, error;

   pAsync= omxGetAsyncResult( sink, (OMX_U32)cmd, nData1 );
   if ( pAsync )
   {
      for( ; ; )
      {         
         LOCK_SOC(sink);
         done= pAsync->done;
         error= sink->soc.asyncError.done;
         if ( error )
         {
            asyncError= sink->soc.asyncError;
            sink->soc.asyncError.done= false;
         }
         UNLOCK_SOC(sink);
         if ( done ) break;
         if ( error )
         {
            pAsync= &asyncError;
            break;
         }   
         
         usleep( 1000 );
      }
      if ( pAsync->eEvent == OMX_EventError )
      {
         omxerr= (OMX_ERRORTYPE)pAsync->nData1;
      }
   }
   else
   {
      assert(false);
   }
   
   return omxerr;
}

static OMX_ERRORTYPE omxSendCommandSync( GstWesterosSink *sink,
                                  OMX_HANDLETYPE hComp,
                                  OMX_COMMANDTYPE cmd,
                                  OMX_U32 nParam1,
                                  OMX_PTR pCmdData )
{
   OMX_ERRORTYPE omxerr;
   OMX_AsyncResult *pAsync= 0;

   pAsync= omxGetAsyncResult( sink, (OMX_U32)cmd, nParam1 );
   if ( pAsync )
   {   
      pAsync->done= false;
   }
   omxerr= OMX_SendCommand( hComp, cmd, nParam1, pCmdData );
   if ( (omxerr == OMX_ErrorNone) && pAsync )
   {
      omxerr= omxWaitCommandComplete( sink, cmd, nParam1 );
   }
   return omxerr;
}

static OMX_ERRORTYPE omxSendCommandAsync( GstWesterosSink *sink,
                                  OMX_HANDLETYPE hComp,
                                  OMX_COMMANDTYPE cmd,
                                  OMX_U32 nParam1,
                                  OMX_PTR pCmdData )
{
   OMX_ERRORTYPE omxerr;
   OMX_AsyncResult *pAsync= 0;
   
   pAsync= omxGetAsyncResult( sink, (OMX_U32)cmd, nParam1 );
   if ( pAsync )
   {
      pAsync->done= false;
   }
   omxerr= OMX_SendCommand( hComp, cmd, nParam1, pCmdData );

   return omxerr;
}

static OMX_ERRORTYPE omxComponentSetState( GstWesterosSink *sink,
                                           OMX_HANDLETYPE hComp,
                                           OMX_STATETYPE state )
{
   OMX_ERRORTYPE omxerr;
   OMX_STATETYPE stateCurrent;

   omxerr= OMX_GetState( hComp, &stateCurrent );
   if ( omxerr == OMX_ErrorNone )
   {
      if ( stateCurrent != state )
      {
         omxerr= omxSendCommandSync( sink, hComp, OMX_CommandStateSet, state, NULL );
      }
   }
   
   return omxerr;
}
                                           
gboolean gst_westeros_sink_soc_get_component( GstWesterosSink *sink, const char *componentName, WstOmxComponent *comp )
{
   gboolean result= FALSE;
   OMX_ERRORTYPE omxerr;
   OMX_PORT_PARAM_TYPE portParam;
   OMX_PARAM_PORTDEFINITIONTYPE portDef;
   char versionName[128];
   OMX_UUIDTYPE uid;
   int port;

   omxerr= sink->soc.OMX_GetHandle( &comp->hComp, (OMX_STRING)componentName, sink, &omxCallbacks );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_init: OMX_GetHandle for %s failed: %x", componentName, omxerr);
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_get_component: GetHandle for %s\n", componentName );
   comp->isOpen= true;

   omxerr= OMX_GetComponentVersion( comp->hComp, versionName, 
                                    &comp->compVersion, &comp->specVersion, &uid );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_get_component: OMX_GetComponentVersion for %s failed: %x", componentName, omxerr);
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_get_component: got %s compver: name %s ver %d.%d spec ver %d.%d",
                    componentName, versionName, 
                    comp->compVersion.s.nVersionMajor,
                    comp->compVersion.s.nVersionMinor,
                    comp->specVersion.s.nVersionMajor,
                    comp->specVersion.s.nVersionMinor );

   portParam.nSize= sizeof(OMX_PORT_PARAM_TYPE);
   portParam.nVersion.nVersion= OMX_VERSION;   
   omxerr= OMX_GetParameter( comp->hComp, OMX_IndexParamVideoInit, &portParam );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_get_component: OMX_GetParameter for OMX_PORT_PARAM_TYPE for %s failed: %x", componentName, omxerr);
      goto exit;
   }

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_get_component: %s has %d ports starting at %d", 
                    componentName, portParam.nPorts, portParam.nStartPortNumber );
   for ( unsigned int i= 0; i < portParam.nPorts; ++i )
   {
      port= portParam.nStartPortNumber+i;
      portDef.nSize= sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
      portDef.nVersion.nVersion= OMX_VERSION;
      portDef.nPortIndex= port;
      
      omxerr= OMX_GetParameter( comp->hComp, OMX_IndexParamPortDefinition, &portDef );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_get_component: "
                   "OMX_GetParameter for OMX_PARAM_PORTDEFINITIONTYPE for %s port %d failed: %x", componentName, port, omxerr);
         goto exit;
      }
      if ( portDef.eDir == OMX_DirInput )
      {
         if ( comp->vidInPort == 0 )
         {
            comp->vidInPort= port;
         }
      }
      else
      {
         if ( comp->vidOutPort == 0 )
         {
            comp->vidOutPort= port;
         }
      }
      omxerr= omxSendCommandSync( sink, comp->hComp, OMX_CommandPortDisable, port, NULL );
      assert( omxerr == OMX_ErrorNone );
   }   
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_get_component: %s: vidInPort %d vidOutPort %d", 
                    componentName, comp->vidInPort, comp->vidOutPort );

   portParam.nSize= sizeof(OMX_PORT_PARAM_TYPE);
   portParam.nVersion.nVersion= OMX_VERSION;   
   omxerr= OMX_GetParameter( comp->hComp, OMX_IndexParamOtherInit, &portParam );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_get_component: OMX_GetParameter for OMX_PORT_PARAM_TYPE for %s failed: %x", componentName, omxerr);
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_get_component: %s has %d other ports starting at %d\n", 
                    componentName, portParam.nPorts, portParam.nStartPortNumber );
   for ( unsigned int i= 0; i < portParam.nPorts; ++i )
   {
      port= portParam.nStartPortNumber+i;
      portDef.nSize= sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
      portDef.nVersion.nVersion= OMX_VERSION;
      portDef.nPortIndex= port;
      
      omxerr= OMX_GetParameter( comp->hComp, OMX_IndexParamPortDefinition, &portDef );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_get_component: "
                   "OMX_GetParameter for OMX_PARAM_PORTDEFINITIONTYPE for %s port %d failed: %x", componentName, port, omxerr);
         goto exit;
      }
      if ( portDef.eDir == OMX_DirInput )
      {
         if ( comp->otherInPort == 0 )
         {
            comp->otherInPort= port;
         }
      }
      else
      {
         if ( comp->otherOutPort == 0 )
         {
            comp->otherOutPort= port;
         }
      }
      omxerr= omxSendCommandSync( sink, comp->hComp, OMX_CommandPortDisable, port, NULL );
      assert( omxerr == OMX_ErrorNone );
   }   
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_get_component: %s: otherInPort %d otherOutPort %d", 
                    componentName, comp->otherInPort, comp->otherOutPort );
   
   comp->name= componentName;

   result= TRUE;

exit:
   return result;   
}

gboolean gst_westeros_sink_soc_setup_tunnel( GstWesterosSink *sink, WstOmxComponent *comp1, int outPort, WstOmxComponent *comp2, int inPort )
{
   gboolean result= FALSE;
   OMX_ERRORTYPE omxerr;
   OMX_STATETYPE state;

   omxerr= OMX_GetState( comp1->hComp, &state );
   assert( omxerr == OMX_ErrorNone );
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_setup_tunnel: %s in state %s", comp1->name, omxStateName(state) );   
   if ( state == OMX_StateLoaded )
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_setup_tunnel: calling OMX_SendCommand for %s setState OMX_StateIdle", comp1->name );
      omxerr= omxSendCommandSync( sink, comp1->hComp, OMX_CommandStateSet, OMX_StateIdle, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_setup_tunnel: OMX_SendCommand for %s setState OMX_StateIdle: omxerr %x", comp1->name, omxerr );
      }
   }

   omxerr= OMX_SendCommand( comp1->hComp, OMX_CommandPortDisable, outPort, NULL );
   assert( omxerr == OMX_ErrorNone );
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_setup_tunnel: disable %s port %d", comp1->name, outPort );

   omxerr= OMX_SendCommand( comp2->hComp, OMX_CommandPortDisable, inPort, NULL );
   assert( omxerr == OMX_ErrorNone );
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_setup_tunnel: disable %s port %d", comp2->name, inPort );

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_setup_tunnel: calling OMX_SetupTunnel for %s port %d to %s port %d",
           comp1->name, outPort, comp2->name, inPort );
   omxerr= sink->soc.OMX_SetupTunnel( comp1->hComp, outPort, comp2->hComp, inPort );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_setup_tunnel: calling OMX_SetupTunnel for %s port %d to %s port %d : omxerr %x",
                comp1->name, outPort, comp2->name, inPort, omxerr );
   }

   omxerr= omxSendCommandAsync( sink, comp1->hComp, OMX_CommandPortEnable, outPort, NULL );
   assert( omxerr == OMX_ErrorNone );
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_setup_tunnel: enable %s port %d", comp1->name, outPort );

   omxerr= omxSendCommandAsync( sink, comp2->hComp, OMX_CommandPortEnable, inPort, NULL );
   assert( omxerr == OMX_ErrorNone );
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_setup_tunnel: enable %s port %d", comp2->name, inPort );


   omxerr= OMX_GetState( comp2->hComp, &state );
   assert( omxerr == OMX_ErrorNone );
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_setup_tunnel: %s in state %s: omxerr %x", comp2->name, omxStateName(state), omxerr );
   
   if ( state == OMX_StateLoaded )
   {
      omxWaitCommandComplete( sink, OMX_CommandPortEnable, inPort );

      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_setup_tunnel: calling OMX_SendCommand for %s "
                       "setState OMX_StateIdle: sink %p\n", comp2->name, sink );
      omxerr= omxSendCommandAsync( sink, comp2->hComp, OMX_CommandStateSet, OMX_StateIdle, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_setup_tunnel: OMX_SendCommand for %s setState OMX_StateIdle: omxerr %x", comp2->name, omxerr );
      }
      
      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc_setup_tunnel: waiting for %s setState OMX_StateIdle", comp2->name );
      omxWaitCommandComplete( sink, OMX_CommandStateSet, 0 );
   }
   else
   {
      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc_setup_tunnel: waiting for %s port %d enable", comp2->name, inPort );
      omxWaitCommandComplete( sink, OMX_CommandPortEnable, inPort );
   }
   
   GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc_setup_tunnel: waiting for %s port %d enable", comp1->name, outPort );
   omxWaitCommandComplete( sink, OMX_CommandPortEnable, outPort );

   result= TRUE;
   
   return result;   
}

gboolean gst_westeros_sink_soc_null_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   gboolean result= FALSE;
   OMX_ERRORTYPE omxerr;
   WESTEROS_UNUSED(passToDefault);
   OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;
      
   result= gst_westeros_sink_soc_get_component( sink, 
                                                "OMX.broadcom.video_decode", 
                                                &sink->soc.vidDec );
   if ( !result )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: failed to get component OMX.broadcom.video_decode" );
      goto exit;
   }
   if ( !sink->soc.vidDec.vidInPort || !sink->soc.vidDec.vidOutPort )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: insufficient ports for vidDec: vidInPort %d vidOutPort %d",
                sink->soc.vidDec.vidInPort, sink->soc.vidDec.vidOutPort );
      goto exit;
   }

   result= gst_westeros_sink_soc_get_component( sink, 
                                                "OMX.broadcom.video_render", 
                                                &sink->soc.vidRend );
   if ( !result )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: failed to get component OMX.broadcom.video_render" );
      goto exit;
   }
   if ( !sink->soc.vidRend.vidInPort )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: insufficient ports for vidRend: vidInPort %d",
                sink->soc.vidRend.vidInPort );
      goto exit;
   }

   result= gst_westeros_sink_soc_get_component( sink, 
                                                "OMX.broadcom.egl_render", 
                                                &sink->soc.eglRend );
   if ( !result )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: failed to get component OMX.broadcom.egl_render" );
      goto exit;
   }
   if ( !sink->soc.eglRend.vidInPort || !sink->soc.eglRend.vidOutPort )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: insufficient ports for eglRend: vidInPort %d vidOutPort %d",
                sink->soc.eglRend.vidInPort, sink->soc.eglRend.vidOutPort );
      goto exit;
   }

   result= gst_westeros_sink_soc_get_component( sink, 
                                                "OMX.broadcom.clock", 
                                                &sink->soc.clock );
   if ( !result )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: failed to get component OMX.broadcom.clock" );
      goto exit;
   }
   if ( !sink->soc.clock.otherOutPort )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: insufficient ports for clock: otherOutPort %d",
                sink->soc.clock.otherOutPort );
      goto exit;
   }
   
   memset( &clockState, 0, sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE) );
   clockState.nSize= sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE);
   clockState.nVersion.nVersion= OMX_VERSION;
   clockState.eState= OMX_TIME_ClockStateWaitingForStartTime;
   clockState.nWaitMask= OMX_CLOCKPORT0;
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_null_to_ready: calling OMX_SetConifg for clock state");
   omxerr= OMX_SetConfig( sink->soc.clock.hComp, OMX_IndexConfigTimeClockState, &clockState );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: OMX_SetConfig for clock state: omxerr %x", omxerr );
   }

   result= gst_westeros_sink_soc_get_component( sink, 
                                                "OMX.broadcom.video_scheduler", 
                                                &sink->soc.vidSched );
   if ( !result )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: failed to get component OMX.broadcom.video_scheduler" );
      goto exit;
   }
   if ( !sink->soc.vidSched.vidInPort || !sink->soc.vidSched.vidOutPort || !sink->soc.vidSched.otherInPort )
   {
      GST_ERROR("gst_westeros_sink_soc_null_to_ready: insufficient ports for vidSched: vidInPort %d vidOutPort %d otherInPort %d",
                sink->soc.vidSched.vidInPort, sink->soc.vidSched.vidOutPort, sink->soc.vidSched.otherInPort );
      goto exit;
   }
   
   result= TRUE;

exit:

   return result;
}

gboolean gst_westeros_sink_soc_ready_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   gboolean result= FALSE;
   OMX_ERRORTYPE omxerr;
   WESTEROS_UNUSED(passToDefault);
   OMX_STATETYPE state;
   OMX_VIDEO_PARAM_PORTFORMATTYPE videoPortFormat;
   OMX_PARAM_PORTDEFINITIONTYPE portDef;
   OMX_BUFFERHEADERTYPE *buff;

   sink->soc.firstBuffer= true;

   if ( !gst_westeros_sink_soc_setup_tunnel( sink, 
                                             &sink->soc.clock, sink->soc.clock.otherOutPort, 
                                             &sink->soc.vidSched, sink->soc.vidSched.otherInPort ) )
   {
      GST_ERROR( "gst_westeros_sink_soc_ready_to_paused: failed to setup tunnel from clock to vidSched" );
      goto exit;
   }
   sink->soc.tunnelActiveClock= true;

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_paused: calling OMX_SendCommand for clock setState OMX_StateExecuting" );
   omxerr= omxComponentSetState( sink, sink->soc.clock.hComp, OMX_StateExecuting );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_ready_to_paused: OMX_SendCommand for clock setState OMX_StateExecuting: omxerr %x", omxerr );
   }

   omxerr= OMX_GetState( sink->soc.vidDec.hComp, &state );
   assert( omxerr == OMX_ErrorNone );
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_paused: vidDec in state %s", omxStateName(state) );
   if ( state != OMX_StateIdle )
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_paused: calling OMX_SendCommand for vidDec setState OMX_StateIdle" );
      omxerr= omxSendCommandSync( sink, sink->soc.vidDec.hComp, OMX_CommandStateSet, OMX_StateIdle, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_ready_to_paused: OMX_SendCommand for vidDec setState OMX_StateIdle: omxerr %x", omxerr );
      }
   }

   memset( &videoPortFormat, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE) );
   videoPortFormat.nSize= sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   videoPortFormat.nVersion.nVersion= OMX_VERSION;
   videoPortFormat.nPortIndex= sink->soc.vidDec.vidInPort;
   videoPortFormat.eCompressionFormat= OMX_VIDEO_CodingAVC;
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_paused: calling OMX_SetParamter for vidDec inPort format %d", 
                    videoPortFormat.eCompressionFormat );
   omxerr= OMX_SetParameter( sink->soc.vidDec.hComp, OMX_IndexParamVideoPortFormat, &videoPortFormat);
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_ready_to_paused: calling OMX_SetParamter for vidDec inPort format: omxerr %x", omxerr);
      goto exit;
   }
   
   memset( &portDef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE) );
   portDef.nSize= sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
   portDef.nVersion.nVersion= OMX_VERSION;
   portDef.nPortIndex= sink->soc.vidDec.vidInPort;   
   omxerr= OMX_GetParameter( sink->soc.vidDec.hComp, OMX_IndexParamPortDefinition, &portDef );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("Error getting vidDec input port %d definition: %x\n", sink->soc.vidDec.vidInPort, omxerr );
      goto exit;
   }
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_paused: vidDec input port: "
           "nBufferCountActual %d mBufferSize %d nBufferAlignment %d\n",
           portDef.nBufferCountActual, portDef.nBufferSize, portDef.nBufferAlignment );
           
   if ( sem_init( &sink->soc.semInputBuffers, 0, portDef.nBufferCountActual ) != 0 )
   {
      GST_ERROR("Error initializing input buffer semaphore: %d", errno );
      goto exit;
   }
   sink->soc.semInputActive= true;
   
   if ( sink->soc.capacityInputBuffers < portDef.nBufferCountActual )
   {
      if ( sink->soc.inputBuffers )
      {
         free( sink->soc.inputBuffers );
         sink->soc.inputBuffers= 0;
      }
      sink->soc.inputBuffers= (OMX_BUFFERHEADERTYPE**)calloc( 1, portDef.nBufferCountActual*sizeof(OMX_BUFFERHEADERTYPE*) );
      if ( !sink->soc.inputBuffers )
      {
         GST_ERROR( "Error allocating vidDec input buffer array (count %d)", portDef.nBufferCountActual );
         goto exit;
      }
      sink->soc.capacityInputBuffers= portDef.nBufferCountActual;
      sink->soc.countInputBuffers= 0;      
   }

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_paused: requesting async enable vidDec port %d", sink->soc.vidDec.vidInPort );
   omxerr= omxSendCommandAsync( sink, sink->soc.vidDec.hComp, OMX_CommandPortEnable, sink->soc.vidDec.vidInPort, NULL );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_ready_to_paused: requested async enable vidDec port %d: omxerr %x", sink->soc.vidDec.vidInPort, omxerr );
      goto exit;
   }
   
   for( unsigned int i= 0; i < portDef.nBufferCountActual; ++ i )
   {
      omxerr= OMX_AllocateBuffer( sink->soc.vidDec.hComp,
                                  &buff,
                                  sink->soc.vidDec.vidInPort,
                                  0,
                                  portDef.nBufferSize );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("Error allocating vidDec input buffer (%d of %d size %d): %x", 
                   i+1, portDef.nBufferCountActual, portDef.nBufferSize, omxerr );
         goto exit;
      }
      
      GST_LOG("gst_westeros_sink_soc_ready_to_paused: adding vidDecInput buffer %p (%d of %d)", buff, i+1, sink->soc.capacityInputBuffers );
      sink->soc.inputBuffers[sink->soc.countInputBuffers]= buff;
      ++sink->soc.countInputBuffers;
   }
   
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_paused: waiting for viddec port enable...");
   omxerr= omxWaitCommandComplete( sink, OMX_CommandPortEnable, sink->soc.vidDec.vidInPort );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_ready_to_paused: viddec port enable: omxerr %x", omxerr );
      goto exit;
   }

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_paused: calling OMX_SendCommand for vidDec setState OMX_StateExecuting" );
   omxerr= omxComponentSetState( sink, sink->soc.vidDec.hComp, OMX_StateExecuting );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_ready_to_paused: OMX_SendCommand for vidDec setState OMX_StateExecuting: omxerr %x", omxerr );
   }

   sink->soc.playingVideo= true;
      
   result= TRUE;
   
exit:
   
   return result;
}

gboolean gst_westeros_sink_soc_paused_to_playing( GstWesterosSink *sink, gboolean *passToDefault )
{
   OMX_ERRORTYPE omxerr;
   OMX_TIME_CONFIG_SCALETYPE clockScale;

   WESTEROS_UNUSED(passToDefault);

   LOCK( sink );
   gst_westeros_sink_soc_start_video( sink );
   UNLOCK( sink );

   LOCK_SOC(sink);
   sink->soc.playingVideo= true;
   UNLOCK_SOC(sink);

   memset( &clockScale, 0, sizeof(OMX_TIME_CONFIG_SCALETYPE) );
   clockScale.nSize= sizeof(OMX_TIME_CONFIG_SCALETYPE);
   clockScale.nVersion.nVersion= OMX_VERSION;
   clockScale.xScale= (1<<16);
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_paused_to_playing: calling OMX_SetConifg for clock scale");
   omxerr= OMX_SetConfig( sink->soc.clock.hComp, OMX_IndexConfigTimeScale, &clockScale );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_paused_to_playing: OMX_SetConfig for clock scale: omxerr %x", omxerr );
   }

   return TRUE;
}

gboolean gst_westeros_sink_soc_playing_to_paused( GstWesterosSink *sink, gboolean *passToDefault )
{
   OMX_ERRORTYPE omxerr;
   OMX_TIME_CONFIG_SCALETYPE clockScale;

   LOCK( sink );
   sink->videoStarted= FALSE;
   UNLOCK( sink );

   memset( &clockScale, 0, sizeof(OMX_TIME_CONFIG_SCALETYPE) );
   clockScale.nSize= sizeof(OMX_TIME_CONFIG_SCALETYPE);
   clockScale.nVersion.nVersion= OMX_VERSION;
   clockScale.xScale= 0;
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_playing_to_paused: calling OMX_SetConifg for clock scale");
   omxerr= OMX_SetConfig( sink->soc.clock.hComp, OMX_IndexConfigTimeScale, &clockScale );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_playing_to_paused: OMX_SetConfig for clock scale: omxerr %x", omxerr );
   }

   *passToDefault= false;
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_paused_to_ready( GstWesterosSink *sink, gboolean *passToDefault )
{
   OMX_ERRORTYPE omxerr;
   OMX_BUFFERHEADERTYPE *buff;

   LOCK_SOC(sink);
   sink->soc.playingVideo= false;
   sink->soc.decoderReady= false;
   sink->soc.schedReady= false;
   sem_post( &sink->soc.semInputBuffers );
   UNLOCK_SOC(sink);

   LOCK( sink );
   if ( sink->soc.captureThread )
   {
      sink->soc.quitCaptureThread= TRUE;
      UNLOCK( sink );
      signalNewFrame( sink );
      g_thread_join( sink->soc.captureThread );
      LOCK( sink );
      sink->soc.captureThread= NULL;
   }
   UNLOCK( sink );

   flushComponents( sink );

   if ( sink->soc.tunnelActiveVidDec )
   {
      omxerr= OMX_SendCommand( sink->soc.vidDec.hComp, OMX_CommandPortDisable, sink->soc.vidDec.vidOutPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable vidDec port %d: omxerr %x", sink->soc.vidDec.vidOutPort, omxerr );
      }

      omxerr= OMX_SendCommand( sink->soc.vidSched.hComp, OMX_CommandPortDisable, sink->soc.vidSched.vidInPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable vidSched port %d: omxerr %x", sink->soc.vidSched.vidInPort, omxerr );
      }
   }

   if ( sink->soc.tunnelActiveVidSched )
   {
      omxerr= OMX_SendCommand( sink->soc.vidSched.hComp, OMX_CommandPortDisable, sink->soc.vidSched.vidOutPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable vidSched port %d: omxerr %x", sink->soc.vidSched.vidOutPort, omxerr );
      }

      if ( sink->soc.rend == &sink->soc.eglRend )
      {
         omxerr= OMX_SendCommand( sink->soc.rend->hComp, OMX_CommandPortDisable, sink->soc.rend->vidOutPort, NULL );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable %s port %d: omxerr %x",
                       sink->soc.rend->name, sink->soc.rend->vidInPort, omxerr );
         }
      }

      omxerr= OMX_SendCommand( sink->soc.rend->hComp, OMX_CommandPortDisable, sink->soc.rend->vidInPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable %s port %d: omxerr %x", 
                    sink->soc.rend->name, sink->soc.rend->vidInPort, omxerr );
      }
   }

   if ( sink->soc.tunnelActiveClock )
   {
      omxerr= OMX_SendCommand( sink->soc.clock.hComp, OMX_CommandPortDisable, sink->soc.clock.otherOutPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable clock port %d: omxerr %x", sink->soc.clock.otherOutPort, omxerr );
      }

      omxerr= OMX_SendCommand( sink->soc.vidSched.hComp, OMX_CommandPortDisable, sink->soc.vidSched.otherInPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable vidSched port %d: omxerr %x", sink->soc.vidSched.otherInPort, omxerr );
      }
   }

   omxerr= OMX_SendCommand( sink->soc.vidDec.hComp, OMX_CommandPortDisable, sink->soc.vidDec.vidInPort, NULL );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable vidDec port %d: omxerr %x\n", sink->soc.vidDec.vidInPort, omxerr );
   }

   sink->videoStarted= FALSE;
   
   int i, count, capacity, nochange;
   i= 0;
   nochange= 0;
   do
   {
      LOCK_SOC(sink);
      count= sink->soc.countInputBuffers;
      capacity= sink->soc.capacityInputBuffers;
      UNLOCK_SOC(sink);
      
      if ( i < count )
      {
         buff= sink->soc.inputBuffers[i];
         GST_LOG("gst_westeros_sink_soc_paused_to_ready: free vidDec input buffer %p", buff );
         OMX_FreeBuffer( sink->soc.vidDec.hComp,
                         sink->soc.vidDec.vidInPort,
                         buff );
         ++i;
      }
      else if ( count == capacity )
      {
         break;
      }
      else
      {
         ++nochange;
         usleep( 1000 );
      }      
   }
   while( nochange < 1000 );

   sink->soc.countInputBuffers= 0;
   if ( sink->soc.inputBuffers )
   {
      free( sink->soc.inputBuffers );
      sink->soc.inputBuffers= 0;
   }
   sink->soc.capacityInputBuffers= 0;
   if ( sink->soc.semInputActive )
   {
      sem_destroy( &sink->soc.semInputBuffers );
      sink->soc.semInputActive= false;
   }

   if ( sink->soc.pEGLBufferHeader )
   {
      GST_LOG("gst_westeros_sink_soc_paused_to_ready: free pEGLBuffer %p", sink->soc.pEGLBufferHeader );
      OMX_FreeBuffer( sink->soc.eglRend.hComp,
                      sink->soc.eglRend.vidOutPort,
                      sink->soc.pEGLBufferHeader );
      sink->soc.pEGLBufferHeader= 0;
      usleep( 200000 );
   }
   
   // Need to wait for viddec inport to disable?  Without sleep get 0x80001000 on vidDec transition to Idle state
   usleep( 100000 );

   if ( sink->soc.tunnelActiveVidDec )
   {
      omxerr= sink->soc.OMX_SetupTunnel( sink->soc.vidDec.hComp, sink->soc.vidDec.vidOutPort, NULL, 0 );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: tear down tunnel vidDec port %d: omxerr %x", sink->soc.vidDec.vidOutPort, omxerr );
      }

      omxerr= sink->soc.OMX_SetupTunnel( sink->soc.vidSched.hComp, sink->soc.vidSched.vidInPort, NULL, 0 );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: tear down tunnel vidSched port %d: omxerr %x", sink->soc.vidSched.vidInPort, omxerr );
      }
      
      sink->soc.tunnelActiveVidDec= false;
   }

   if ( sink->soc.tunnelActiveVidSched )
   {
      omxerr= sink->soc.OMX_SetupTunnel( sink->soc.vidSched.hComp, sink->soc.vidSched.vidOutPort, NULL, 0 );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: tear down tunnel vidSched port %d: omxerr %x", sink->soc.vidSched.vidOutPort, omxerr );
      }

      omxerr= sink->soc.OMX_SetupTunnel( sink->soc.rend->hComp, sink->soc.rend->vidInPort, NULL, 0 );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: tear down tunnel %s port %d: omxerr %x", 
                    sink->soc.rend->name, sink->soc.rend->vidInPort, omxerr );
      }
      
      sink->soc.tunnelActiveVidSched= false;
   }

   if ( sink->soc.tunnelActiveClock )
   {
      omxerr= sink->soc.OMX_SetupTunnel( sink->soc.clock.hComp, sink->soc.clock.otherOutPort, NULL, 0 );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: tear down tunnel clock port %d: omxerr %x", sink->soc.clock.otherOutPort, omxerr );
      }

      omxerr= sink->soc.OMX_SetupTunnel( sink->soc.vidSched.hComp, sink->soc.vidSched.otherInPort, NULL, 0 );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: tear down tunnel vidSched port %d: omxerr %x", sink->soc.vidSched.otherInPort, omxerr );
      }
      
      sink->soc.tunnelActiveClock= false;
   }

   if ( sink->soc.eglRend.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.eglRend.hComp, OMX_StateIdle );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: OMX_SendCommand for eglRend setState OMX_StateIdle: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.vidRend.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.vidRend.hComp, OMX_StateIdle );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: OMX_SendCommand for vidRend setState OMX_StateIdle: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.vidSched.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.vidSched.hComp, OMX_StateIdle );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: OMX_SendCommand for vidSched setState OMX_StateIdle: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.vidDec.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.vidDec.hComp, OMX_StateIdle );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: OMX_SendCommand for vidDec setState OMX_StateIdle: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.clock.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.clock.hComp, OMX_StateIdle );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_paused_to_ready: OMX_SendCommand for clock setState OMX_StateIdle: omxerr %x", omxerr );
      }
   }

   *passToDefault= false;
   
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_paused_to_ready: done");
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_ready_to_null( GstWesterosSink *sink, gboolean *passToDefault )
{
   WESTEROS_UNUSED(sink);
   OMX_ERRORTYPE omxerr;

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_null: enter");

   if ( sink->surface && sink->display )
   {
      wl_surface_attach( sink->surface, 0, sink->windowX, sink->windowY );
      wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
      wl_surface_commit( sink->surface );
      wl_display_flush( sink->display );
   }

   sink->soc.playingVideo= false;

   if ( sink->soc.eglRend.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.eglRend.hComp, OMX_StateLoaded );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_ready_to_null: OMX_SendCommand for eglRend setState OMX_StateLoaded: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.vidRend.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.vidRend.hComp, OMX_StateLoaded );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_ready_to_null: OMX_SendCommand for vidRend setState OMX_StateLoaded: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.vidSched.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.vidSched.hComp, OMX_StateLoaded );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_ready_to_null: OMX_SendCommand for vidSched setState OMX_StateLoaded: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.vidDec.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.vidDec.hComp, OMX_StateLoaded );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_ready_to_null: OMX_SendCommand for vidDec setState OMX_StateLoaded: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.clock.isOpen )
   {
      omxerr= omxComponentSetState( sink, sink->soc.clock.hComp, OMX_StateLoaded );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc_ready_to_null: OMX_SendCommand for clock setState OMX_StateLoaded: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.eglRend.isOpen )
   {
      omxerr= sink->soc.OMX_FreeHandle( sink->soc.eglRend.hComp );
      sink->soc.eglRend.isOpen= false;
   }

   if ( sink->soc.vidRend.isOpen )
   {
      omxerr= sink->soc.OMX_FreeHandle( sink->soc.vidRend.hComp );
      sink->soc.vidRend.isOpen= false;
   }

   if ( sink->soc.vidSched.isOpen )
   {
      omxerr= sink->soc.OMX_FreeHandle( sink->soc.vidSched.hComp );
      sink->soc.vidSched.isOpen= false;
   }

   if ( sink->soc.clock.isOpen )
   {
      omxerr= sink->soc.OMX_FreeHandle( sink->soc.clock.hComp );
      sink->soc.clock.isOpen= false;
   }

   if ( sink->soc.vidDec.isOpen )
   {
      omxerr= sink->soc.OMX_FreeHandle( sink->soc.vidDec.hComp );
      sink->soc.vidDec.isOpen= false;
   }

   *passToDefault= false;
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_ready_to_null: done");
   
   return TRUE;
}

gboolean gst_westeros_sink_soc_accept_caps( GstWesterosSink *sink, GstCaps *caps )
{
   bool result= FALSE;
   GstStructure *structure;
   const gchar *mime;

   WESTEROS_UNUSED(sink);

   structure= gst_caps_get_structure(caps, 0);
   if(structure )
   {
      mime= gst_structure_get_name(structure);
      if (strcmp("video/x-h264", mime) == 0)
      {
         result= TRUE;
      }
      else
      {
         GST_ERROR("gst_westeros_sink_soc_accept_caps: not accepting caps (%s)", mime );
      }
   }

   return result;   
}

void gst_westeros_sink_soc_set_startPTS( GstWesterosSink *sink, gint64 pts )
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(pts);
}

void gst_westeros_sink_soc_render( GstWesterosSink *sink, GstBuffer *buffer )
{  
   OMX_ERRORTYPE omxerr;
   int retryCount;
   gboolean eosDetected;
   
   LOCK(sink);
   eosDetected= sink->eosDetected;
   UNLOCK(sink);

   if ( sink->soc.playingVideo && !sink->flushStarted )
   {
      LOCK(sink);
      if ( (sink->soc.decoderReady && !sink->soc.tunnelActiveVidSched) ||
            (sink->soc.tunnelActiveVidSched && sink->soc.videoOutputChanged) )
      {
         OMX_PARAM_PORTDEFINITIONTYPE portDef;

         omxerr= OMX_SendCommand( sink->soc.vidDec.hComp, OMX_CommandPortDisable, sink->soc.vidDec.vidOutPort, NULL );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable vidDec port %d: omxerr %x", sink->soc.vidDec.vidOutPort, omxerr );
         }

         portDef.nSize= sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
         portDef.nVersion.nVersion= OMX_VERSION;
         portDef.nPortIndex= sink->soc.vidDec.vidOutPort;

         omxerr= OMX_GetParameter( sink->soc.vidDec.hComp, OMX_IndexParamPortDefinition, &portDef );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("gst_westeros_sink_soc_render: "
                      "OMX_GetParameter for OMX_PARAM_PORTDEFINITIONTYPE for vidDec output port %d failed: %x", sink->soc.vidDec.vidOutPort, omxerr);
            goto exit;
         }

         sink->soc.frameWidth= portDef.format.video.nFrameWidth;
         sink->soc.frameHeight= portDef.format.video.nFrameHeight;
         printf("gst_westeros_sink_soc_render: video frame size (%d x %d)\n", sink->soc.frameWidth, sink->soc.frameHeight );

         if ( sink->soc.tunnelActiveVidSched )
         {
            omxerr= omxSendCommandSync( sink, sink->soc.rend->hComp, OMX_CommandStateSet, OMX_StateIdle, NULL );
            if ( omxerr != OMX_ErrorNone )
            {
               GST_ERROR("gst_westeros_sink_soc_render: OMX_SendCommand for rend setState OMX_StateIdle: omxerr %x", omxerr );
            }

            omxerr= omxSendCommandSync( sink, sink->soc.vidSched.hComp, OMX_CommandStateSet, OMX_StateIdle, NULL );
            if ( omxerr != OMX_ErrorNone )
            {
               GST_ERROR("gst_westeros_sink_soc_render: OMX_SendCommand for vidSched setState OMX_StateIdle: omxerr %x", omxerr );
            }

            sink->soc.tunnelActiveVidSched= false;
         }
         else
         {
            omxerr= OMX_SendCommand( sink->soc.vidDec.hComp, OMX_CommandPortEnable, sink->soc.vidDec.vidOutPort, NULL );
            if ( omxerr != OMX_ErrorNone )
            {
               GST_ERROR("gst_westeros_sink_soc_paused_to_ready: disable vidDec port %d: omxerr %x", sink->soc.vidDec.vidOutPort, omxerr );
            }
         }

         sink->soc.videoOutputChanged= false;
      }

      if ( sink->soc.decoderReady && !sink->soc.tunnelActiveVidSched )
      {
         if ( !gst_westeros_sink_soc_setup_tunnel( sink, 
                                                   &sink->soc.vidDec, sink->soc.vidDec.vidOutPort, 
                                                   &sink->soc.vidSched, sink->soc.vidSched.vidInPort ) )
         {
            GST_ERROR( "gst_westeros_sink_soc_render: failed to setup tunnel from vidDec to vidSched" );
            UNLOCK(sink);
            goto exit;
         }
         sink->soc.tunnelActiveVidDec= true;

         GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_render: calling OMX_SendCommand for vidSched setState OMX_StateExecuting" );
         omxerr= omxSendCommandSync( sink, sink->soc.vidSched.hComp, OMX_CommandStateSet, OMX_StateExecuting, NULL );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("gst_westeros_sink_soc_render: OMX_SendCommand for vidSched setState OMX_StateExecuting: omxerr %x", omxerr );
         }

         GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_render: waiting for schedReady..." );
         retryCount= 2000;
         while ( !sink->soc.schedReady )
         {
            usleep( 1000 );
            if ( --retryCount == 0 )
            {
               break;
            }
         }
         if ( retryCount > 0 )
         {
            GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_render: got schedReady" );
         }
         else
         {
            GST_ERROR("gst_westeros_sink_soc_render: failed to get schedReady" );
         }

         if ( !gst_westeros_sink_soc_setup_tunnel( sink, 
                                                   &sink->soc.vidSched, sink->soc.vidSched.vidOutPort, 
                                                   sink->soc.rend, sink->soc.rend->vidInPort ) )
         {
            GST_ERROR( "gst_westeros_sink_soc_render: failed to setup tunnel from vidSched to %s", sink->soc.rend->name );
            UNLOCK(sink);
            goto exit;
         }
         sink->soc.tunnelActiveVidSched= true;
         
         GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_render: calling OMX_SendCommand for %s setState OMX_StateExecuting",
                          sink->soc.rend->name );
         omxerr= omxSendCommandSync( sink, sink->soc.rend->hComp, OMX_CommandStateSet, OMX_StateExecuting, NULL );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("gst_westeros_sink_soc_render: OMX_SendCommand for %s setState OMX_StateExecuting: omxerr %x", 
                      sink->soc.rend->name, omxerr );
         }
      }
      UNLOCK(sink);

      processFrame( sink, buffer );

      if ( wl_display_dispatch_queue_pending(sink->display, sink->queue) == 0 )
      {
         wl_display_flush(sink->display);
         if ( !eosDetected )
         {
            wl_display_roundtrip_queue(sink->display,sink->queue);
         }
      }
   }

exit:
   return;   
}

void gst_westeros_sink_soc_flush( GstWesterosSink *sink )
{
   LOCK_SOC(sink);
   sink->soc.playingVideo= false;
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_flush: post sem to wake up render");
   sem_post( &sink->soc.semInputBuffers );
   UNLOCK_SOC(sink);
   flushComponents( sink );
   resetClock( sink );
}

gboolean gst_westeros_sink_soc_start_video( GstWesterosSink *sink )
{
   gboolean result= FALSE;
   
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_start_video" );

   sink->videoStarted= TRUE;
   
   result= TRUE;
 
   return result;   
}

void gst_westeros_sink_soc_eos_event( GstWesterosSink *sink )
{
   int rc; 
   OMX_ERRORTYPE omxerr;
   OMX_BUFFERHEADERTYPE *buff= 0;

   if ( sink->soc.playingVideo )
   {
      if ( sink->soc.semInputActive )
      {
         if ( sink->soc.buffCurrent )
         {
            buff= sink->soc.buffCurrent;
            buff->nOffset= 0;
            buff->nFlags= 0;
            GST_LOG("gst_westeros_sink_soc_eos_event: pass buffer %p to vidDec with %d bytes", buff, buff->nFilledLen );
            omxerr= OMX_EmptyThisBuffer( sink->soc.vidDec.hComp, buff );
            if ( omxerr != OMX_ErrorNone )
            {
               GST_ERROR( "gst_westeros_sink_soc_eos_event: error from OMX_EmptyThisBuffer: %x", omxerr );
            }
            buff= sink->soc.buffCurrent= 0;
         }
      
         buff= 0;
         while( !buff )
         {
            rc= sem_trywait( &sink->soc.semInputBuffers );

            if ( !sink->soc.playingVideo )
            {
               break;;
            }

            if ( rc != 0 )
            {
               usleep( 1000 );
            }

            if ( rc == 0 )
            {
               LOCK_SOC( sink );
               if ( sink->soc.countInputBuffers )
               {
                  --sink->soc.countInputBuffers;
                  buff= sink->soc.inputBuffers[sink->soc.countInputBuffers];
               }
               UNLOCK_SOC( sink );
               
               buff->nFilledLen= 0;
               buff->nFlags= OMX_BUFFERFLAG_EOS;

               omxerr= OMX_EmptyThisBuffer( sink->soc.vidDec.hComp, buff );
               GST_LOG("gst_westeros_sink_soc_eos_event: pass buffer %p to vidDec with EOS flag", buff );
               if ( omxerr != OMX_ErrorNone )
               {
                  GST_ERROR( "gst_westeros_sink_soc_eos_event: error from OMX_EmptyThisBuffer: %x", omxerr );
               }
            }
         }
      }
   }
}

static void flushComponents( GstWesterosSink *sink )
{
   OMX_ERRORTYPE omxerr;
   OMX_STATETYPE vidDecState;

   omxerr= OMX_GetState( sink->soc.vidDec.hComp, &vidDecState );
   assert( omxerr == OMX_ErrorNone );
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc: flushComponents: vidDec in state %s", omxStateName(vidDecState) );   
   if ( vidDecState == OMX_StateExecuting )
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc: flushComponents: calling OMX_SendCommand for vidDec setState OMX_StateIdle" );
      omxerr= omxSendCommandSync( sink, sink->soc.vidDec.hComp, OMX_CommandStateSet, OMX_StateIdle, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc: flushComponents: OMX_SendCommand for vidDec setState OMX_StateIdle: omxerr %x", omxerr );
      }
   }

   if ( sink->soc.tunnelActiveVidDec )
   {
      omxerr= omxSendCommandAsync( sink, sink->soc.vidSched.hComp, OMX_CommandFlush, sink->soc.vidSched.vidInPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc: flushComponents: flush vidSched port %d: omxerr %x", sink->soc.vidSched.vidInPort, omxerr );
      }

      omxerr= omxSendCommandAsync( sink, sink->soc.vidDec.hComp, OMX_CommandFlush, sink->soc.vidDec.vidOutPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc: flushComponents: flush vidDec port %d: omxerr %x", sink->soc.vidDec.vidOutPort, omxerr );
      }
      
      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc: flushComponents: waiting for vidDec port %d flush", sink->soc.vidDec.vidOutPort );
      omxWaitCommandComplete( sink, OMX_CommandFlush, sink->soc.vidDec.vidOutPort );

      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc: flushComponents: waiting for vidSched port %d flush", sink->soc.vidSched.vidInPort );
      omxWaitCommandComplete( sink, OMX_CommandFlush, sink->soc.vidSched.vidInPort );
      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc: flushComponents: done waiting for flushes" );
   }

   if ( sink->soc.tunnelActiveVidSched )
   {
      omxerr= omxSendCommandAsync( sink, sink->soc.rend->hComp, OMX_CommandFlush, sink->soc.rend->vidInPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc: flushComponents: flush %s port %d: omxerr %x", 
                    sink->soc.rend->name, sink->soc.rend->vidInPort, omxerr );
      }
      
      omxerr= omxSendCommandAsync( sink, sink->soc.vidSched.hComp, OMX_CommandFlush, sink->soc.vidSched.vidOutPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc: flushComponents: flush vidSched port %d: omxerr %x", sink->soc.vidSched.vidOutPort, omxerr );
      }

      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc: flushComponents: waiting for vidSched port %d flush", sink->soc.vidSched.vidOutPort );
      omxWaitCommandComplete( sink, OMX_CommandFlush, sink->soc.vidSched.vidOutPort );

      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc: flushComponents: waiting for %s port %d flush", 
                        sink->soc.rend->name, sink->soc.rend->vidInPort );
      omxWaitCommandComplete( sink, OMX_CommandFlush, sink->soc.rend->vidInPort );
      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc: flushComponents: done waiting for flushes" );
   }

   if ( sink->soc.tunnelActiveClock )
   {
      omxerr= omxSendCommandAsync( sink, sink->soc.vidSched.hComp, OMX_CommandFlush, sink->soc.vidSched.otherInPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc: flushComponents: flush vidSched port %d: omxerr %x", sink->soc.vidSched.otherInPort, omxerr );
      }

      omxerr= omxSendCommandAsync( sink, sink->soc.clock.hComp, OMX_CommandFlush, sink->soc.clock.otherOutPort, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc: flushComponents: flush clock port %d: omxerr %x", sink->soc.clock.otherOutPort, omxerr );
      }

      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc: flushComponents: waiting for clock port %d flush", sink->soc.clock.otherOutPort );
      omxWaitCommandComplete( sink, OMX_CommandFlush, sink->soc.clock.otherOutPort );

      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc: flushComponents: waiting for vidSched port %d flush", sink->soc.vidSched.otherInPort );
      omxWaitCommandComplete( sink, OMX_CommandFlush, sink->soc.vidSched.otherInPort );
      GST_DEBUG_OBJECT( sink, "gst_westeros_sink_soc: flushComponents: done waiting for flushes" );
   }

   if ( vidDecState == OMX_StateExecuting )
   {
      GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc: flushComponents: calling OMX_SendCommand for vidDec setState OMX_StateExecuting" );
      omxerr= omxSendCommandSync( sink, sink->soc.vidDec.hComp, OMX_CommandStateSet, OMX_StateExecuting, NULL );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc: flushComponents: OMX_SendCommand for vidDec setState OMX_StateExecuting: omxerr %x", omxerr );
      }
   }

   sink->soc.firstBuffer= true;
}

static void resetClock( GstWesterosSink *sink )
{
   OMX_ERRORTYPE omxerr;
   OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;

   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_reset_clock" );

   memset( &clockState, 0, sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE) );
   clockState.nSize= sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE);
   clockState.nVersion.nVersion= OMX_VERSION;
   clockState.eState= OMX_TIME_ClockStateStopped;
   clockState.nOffset.nLowPart=(PREROLL_OFFSET&0xFFFFFFFF);
   clockState.nOffset.nHighPart=((PREROLL_OFFSET>>32)&0xFFFFFFFF);
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_reset_clock: calling OMX_SetConifg for clock state");
   omxerr= OMX_SetConfig( sink->soc.clock.hComp, OMX_IndexConfigTimeClockState, &clockState );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_reset_clock: OMX_SetConfig for clock state: omxerr %x", omxerr );
   }

   memset( &clockState, 0, sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE) );
   clockState.nSize= sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE);
   clockState.nVersion.nVersion= OMX_VERSION;
   clockState.eState= OMX_TIME_ClockStateWaitingForStartTime;
   clockState.nWaitMask= OMX_CLOCKPORT0;
   clockState.nOffset.nLowPart=(PREROLL_OFFSET&0xFFFFFFFF);
   clockState.nOffset.nHighPart=((PREROLL_OFFSET>>32)&0xFFFFFFFF);
   GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_reset_clock: calling OMX_SetConifg for clock state");
   omxerr= OMX_SetConfig( sink->soc.clock.hComp, OMX_IndexConfigTimeClockState, &clockState );
   if ( omxerr != OMX_ErrorNone )
   {
      GST_ERROR("gst_westeros_sink_soc_reset_clock:: OMX_SetConfig for clock state: omxerr %x", omxerr );
   }
}

void gst_westeros_sink_soc_set_video_path( GstWesterosSink *sink, bool useGfxPath )
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(useGfxPath);
   bool oldUseGfxPath;
   
   oldUseGfxPath= sink->soc.useGfxPath;

   sink->soc.useGfxPath= useGfxPath;

   if ( useGfxPath && !oldUseGfxPath )
   {
      struct wl_buffer *buff;

      if ( sink->soc.frameWidth && sink->soc.frameHeight )
      {
         transitionToRenderer( sink, &sink->soc.eglRend );
      }

      buff= wl_sb_create_buffer( sink->soc.sb, 
                                 0,
                                 sink->windowWidth, 
                                 sink->windowHeight, 
                                 sink->windowWidth*4, 
                                 WL_SB_FORMAT_ARGB8888 );
      wl_surface_attach( sink->surface, buff, sink->windowX, sink->windowY );
      wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
      wl_surface_commit( sink->surface );
      wl_display_flush(sink->display);
      wl_display_dispatch_queue_pending(sink->display, sink->queue);
   }
   else if ( !useGfxPath && oldUseGfxPath )
   {
      transitionToRenderer( sink, &sink->soc.vidRend );

      gst_westeros_sink_soc_update_video_position( sink );
      
      wl_surface_attach( sink->surface, 0, sink->windowX, sink->windowY );
      wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
      wl_surface_commit( sink->surface );
      wl_display_flush(sink->display);
      wl_display_dispatch_queue_pending(sink->display, sink->queue);
   }   
}

void gst_westeros_sink_soc_update_video_position( GstWesterosSink *sink )
{
   OMX_ERRORTYPE omxerr;
   OMX_CONFIG_DISPLAYREGIONTYPE displayRegion;
   int wx, wy, ww, wh;
   int vx, vy, vw, vh;

   wx= sink->windowX;
   wy= sink->windowY;
   ww= sink->windowWidth;
   wh= sink->windowHeight;
   sink->windowChange= false;
      
   if ( !sink->soc.useGfxPath )
   {
      vx= ((wx*sink->scaleXNum)/sink->scaleXDenom) + sink->transX;
      vy= ((wy*sink->scaleYNum)/sink->scaleYDenom) + sink->transY;
      vw= ((ww)*sink->scaleXNum)/sink->scaleXDenom;
      vh= ((wh)*sink->scaleYNum)/sink->scaleYDenom;
      if ( !sink->windowSizeOverride )
      {
         double sizeXFactor= ((double)sink->outputWidth)/DEFAULT_WINDOW_WIDTH;
         double sizeYFactor= ((double)sink->outputHeight)/DEFAULT_WINDOW_HEIGHT;
         vw *= sizeXFactor;
         vh *= sizeYFactor;
      }
      
      memset( &displayRegion, 0, sizeof(OMX_CONFIG_DISPLAYREGIONTYPE) );
      displayRegion.nSize= sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
      displayRegion.nVersion.nVersion= OMX_VERSION;
      displayRegion.nPortIndex= sink->soc.rend->vidInPort;
      displayRegion.set= (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_FULLSCREEN|
                                              OMX_DISPLAY_SET_NOASPECT|
                                              OMX_DISPLAY_SET_DEST_RECT|
                                              OMX_DISPLAY_SET_LAYER);
      displayRegion.fullscreen= OMX_FALSE;
      displayRegion.noaspect= OMX_FALSE;
      displayRegion.dest_rect.x_offset= vx;
      displayRegion.dest_rect.y_offset= vy;
      displayRegion.dest_rect.width= vw;
      displayRegion.dest_rect.height= vh;
      displayRegion.layer= -1;
      omxerr= OMX_SetConfig( sink->soc.rend->hComp, OMX_IndexConfigDisplayRegion, &displayRegion );
      if ( omxerr != OMX_ErrorNone )
      {
         GST_ERROR("gst_westeros_sink_soc update_video_position: OMX_SetConfig for display region: error: %x", omxerr );
      }

      // Send a buffer to compositor to update hole punch geometry
      if ( sink->soc.sb )
      {
         wl_surface_attach( sink->surface, 0, sink->windowX, sink->windowY );
         wl_surface_damage( sink->surface, 0, 0, sink->windowWidth, sink->windowHeight );
         wl_surface_commit( sink->surface );
      }
   }
}

void transitionToRenderer( GstWesterosSink *sink, WstOmxComponent *rend )
{
   OMX_ERRORTYPE omxerr;
   OMX_STATETYPE state;
   bool toGfx= (rend == &sink->soc.eglRend);

   LOCK(sink);
   if ( toGfx && !sink->soc.captureThread )
   {
      sink->soc.quitCaptureThread= FALSE;
      if ( sink->soc.captureThread == NULL )
      {
         GST_DEBUG_OBJECT(sink, "transitionToRenderer: starting westeros_sink_capture thread");
         sink->soc.captureThread= g_thread_new("westeros_sink_capture", captureThread, sink);
      }
   }

   if ( sink->soc.rend != rend )
   {
      if ( sink->soc.tunnelActiveVidSched )
      {
         omxerr= OMX_SendCommand( sink->soc.vidSched.hComp, OMX_CommandPortDisable, sink->soc.vidSched.vidInPort, NULL );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("transitionToRenderer: disable vidSched port %d: omxerr %x", sink->soc.vidSched.vidInPort, omxerr );
         }

         omxerr= OMX_SendCommand( sink->soc.vidSched.hComp, OMX_CommandPortDisable, sink->soc.vidSched.vidOutPort, NULL );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("transitionToRenderer: disable vidSched port %d: omxerr %x", sink->soc.vidSched.vidOutPort, omxerr );
         }

         omxerr= OMX_SendCommand( sink->soc.rend->hComp, OMX_CommandPortDisable, sink->soc.rend->vidInPort, NULL );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("transitionToRenderer: disable %s port %d: omxerr %x",
                       sink->soc.rend->name, sink->soc.rend->vidInPort, omxerr );
         }

         if ( !toGfx )
         {
            omxerr= OMX_SendCommand( sink->soc.rend->hComp, OMX_CommandPortDisable, sink->soc.rend->vidOutPort, NULL );
            if ( omxerr != OMX_ErrorNone )
            {
               GST_ERROR("transitionToRenderer: disable %s port %d: omxerr %x",
                          sink->soc.rend->name, sink->soc.rend->vidOutPort, omxerr );
            }
         }

         omxWaitCommandComplete( sink, OMX_CommandPortDisable, sink->soc.vidSched.vidOutPort );

         omxWaitCommandComplete( sink, OMX_CommandPortDisable, sink->soc.rend->vidInPort );

         // Without this sleep, egl_render module doesn't fill buffers.  Why?
         usleep( 75000 );

         omxerr= OMX_SendCommand( sink->soc.vidSched.hComp, OMX_CommandPortEnable, sink->soc.vidSched.vidInPort, NULL );
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("transitionToRenderer: enable vidSched port %d: omxerr %x", sink->soc.vidSched.vidInPort, omxerr );
         }

         sink->soc.tunnelActiveVidSched= false;
      }

      sink->soc.rend= rend;

      if ( !gst_westeros_sink_soc_setup_tunnel( sink,
                                                &sink->soc.vidSched, sink->soc.vidSched.vidOutPort,
                                                sink->soc.rend, sink->soc.rend->vidInPort ) )
      {
         GST_ERROR( "transitionToRenderer: failed to setup tunnel from vidSched to %s", sink->soc.rend->name );
         goto exit;
      }
      sink->soc.tunnelActiveVidSched= true;

      if ( toGfx )
      {
         if (!sink->soc.pEGLBufferHeader )
         {
            omxerr= OMX_GetState( sink->soc.rend->hComp, &state );
            assert( omxerr == OMX_ErrorNone );
            GST_DEBUG_OBJECT(sink, "transitionToRenderer: %s in state %s", rend->name, omxStateName(state) );

            if ( state != OMX_StateIdle )
            {
               GST_DEBUG_OBJECT(sink, "transitionToRenderer: calling OMX_SendCommand for %s setState OMX_StateIdle", rend->name );
               omxerr= omxSendCommandSync( sink, sink->soc.rend->hComp, OMX_CommandStateSet, OMX_StateIdle, NULL );
               if ( omxerr != OMX_ErrorNone )
               {
                  GST_ERROR("transitionToRenderer: OMX_SendCommand for %s setState OMX_StateIdle: omxerr %x", rend->name, omxerr );
               }
            }

            omxerr= omxSendCommandAsync( sink, sink->soc.rend->hComp, OMX_CommandPortEnable, sink->soc.rend->vidOutPort, NULL );
            assert( omxerr == OMX_ErrorNone );
            GST_DEBUG_OBJECT(sink, "transitionToRenderer: enable %s port %d", rend->name, sink->soc.rend->vidOutPort );

            omxerr= OMX_UseEGLImage( sink->soc.rend->hComp, &sink->soc.pEGLBufferHeader, sink->soc.rend->vidOutPort, NULL, sink->soc.eglImage );
            GST_DEBUG_OBJECT(sink, "transitiontoRenderer: OMX_UseEGLImage omxerr %x texid %x eglImage %p pEGLBufferHeader %p\n", omxerr, sink->soc.textureId, sink->soc.eglImage, sink->soc.pEGLBufferHeader);
            if ( omxerr != OMX_ErrorNone )
            {
               GST_ERROR("transitionToRenderer: OMX_UseEGLImage for eglRender: omxerr %x", omxerr );
            }

            omxerr= OMX_GetState( sink->soc.rend->hComp, &state );
            assert( omxerr == OMX_ErrorNone );
            GST_DEBUG_OBJECT(sink, "transitionToRenderer: %s in state %s", rend->name, omxStateName(state) );

            if ( state != OMX_StateExecuting )
            {
               GST_DEBUG_OBJECT(sink, "transitionToRenderer: calling OMX_SendCommand for %s setState OMX_StateExecuting", rend->name );
               omxerr= omxSendCommandSync( sink, sink->soc.rend->hComp, OMX_CommandStateSet, OMX_StateExecuting, NULL );
               if ( omxerr != OMX_ErrorNone )
               {
                  GST_ERROR("transitionToRenderer: OMX_SendCommand for %s setState OMX_StateExecuting: omxerr %x", rend->name, omxerr );
               }
            }
         }
         else
         {
            omxerr= omxSendCommandAsync( sink, sink->soc.rend->hComp, OMX_CommandPortEnable, sink->soc.rend->vidOutPort, NULL );
            assert( omxerr == OMX_ErrorNone );
            GST_DEBUG_OBJECT(sink, "transitionToRenderer: enable %s port %d", rend->name, sink->soc.rend->vidOutPort );
         }
      }

      if ( toGfx )
      {
         omxerr= OMX_FillThisBuffer( sink->soc.rend->hComp, sink->soc.pEGLBufferHeader );
         GST_DEBUG_OBJECT(sink, "transitionToRenderer: OMX_FillThisBuffer omxerr %x\n", omxerr);
         if ( omxerr != OMX_ErrorNone )
         {
            GST_ERROR("transitionToRenderer: OMX_FillThisBuffer for eglRender: omxerr %x", omxerr );
         }
      }
   }

exit:
   UNLOCK(sink);
   return;
}

#define RED_SIZE (8)
#define GREEN_SIZE (8)
#define BLUE_SIZE (8)
#define ALPHA_SIZE (8)
#define DEPTH_SIZE (0)

static bool setupEGL( GstWesterosSink *sink )
{
   bool result= false;
   int i;
   EGLBoolean b;
   EGLint major, minor;
   EGLint configCount;
   EGLConfig *eglConfigs= 0;
   #ifdef USE_GLES2
   EGLint ctxAttrib[3];
   #endif
   EGLint attr[24];
   EGLint redSize, greenSize, blueSize, alphaSize, depthSize;

   if ( !sink->soc.eglSetup )
   {
      sink->soc.eglDisplay= eglGetDisplay( sink->display );
      if ( sink->soc.eglDisplay == EGL_NO_DISPLAY )
      {
         GST_ERROR( "no EGL display available" );
         goto exit;
      }

      b= eglInitialize( sink->soc.eglDisplay, &major, &minor );
      if ( !b )
      {
         GST_ERROR( "unable to initialize EGL display" );
         goto exit;
      }

      b= eglGetConfigs( sink->soc.eglDisplay, NULL, 0, &configCount );
      if ( !b )
      {
         GST_ERROR("unable to get count of EGL configurations: %X", eglGetError());
         goto exit;
      }

      eglConfigs= (EGLConfig*)malloc( configCount*sizeof(EGLConfig) );
      if ( !eglConfigs )
      {
         GST_ERROR("unable to allocate memory for EGL configurations");
         goto exit;
      }

      i= 0;
      attr[i++]= EGL_RED_SIZE;
      attr[i++]= RED_SIZE;
      attr[i++]= EGL_GREEN_SIZE;
      attr[i++]= GREEN_SIZE;
      attr[i++]= EGL_BLUE_SIZE;
      attr[i++]= BLUE_SIZE;
      attr[i++]= EGL_ALPHA_SIZE;
      attr[i++]= ALPHA_SIZE;
      attr[i++]= EGL_DEPTH_SIZE;
      attr[i++]= DEPTH_SIZE;
      attr[i++]= EGL_SURFACE_TYPE;
      attr[i++]= EGL_WINDOW_BIT;
      #ifdef USE_GLES2
      attr[i++]= EGL_STENCIL_SIZE;
      attr[i++]= 0;
      attr[i++]= EGL_RENDERABLE_TYPE;
      attr[i++]= EGL_OPENGL_ES2_BIT;
      #endif
      attr[i++]= EGL_NONE;

      b= eglChooseConfig( sink->soc.eglDisplay,
                          attr,
                          eglConfigs,
                          configCount,
                          &configCount );
      if ( !b )
      {
         GST_ERROR("eglChooseConfig failed: %X", eglGetError());
         goto exit;
      }

      for( i= 0; i < configCount; ++i )
      {
         eglGetConfigAttrib( sink->soc.eglDisplay, eglConfigs[i], EGL_RED_SIZE, &redSize );
         eglGetConfigAttrib( sink->soc.eglDisplay, eglConfigs[i], EGL_GREEN_SIZE, &greenSize );
         eglGetConfigAttrib( sink->soc.eglDisplay, eglConfigs[i], EGL_BLUE_SIZE, &blueSize );
         eglGetConfigAttrib( sink->soc.eglDisplay, eglConfigs[i], EGL_ALPHA_SIZE, &alphaSize );
         eglGetConfigAttrib( sink->soc.eglDisplay, eglConfigs[i], EGL_DEPTH_SIZE, &depthSize );

         GST_DEBUG("setupEGL: config %d: red: %d green: %d blue: %d alpha: %d depth: %d\n",
                 i, redSize, greenSize, blueSize, alphaSize, depthSize );
         if ( (redSize == RED_SIZE) &&
              (greenSize == GREEN_SIZE) &&
              (blueSize == BLUE_SIZE) &&
              (alphaSize == ALPHA_SIZE) &&
              (depthSize >= DEPTH_SIZE) )
         {
            GST_DEBUG( "setupEGL: choosing config %d\n", i);
            break;
         }
      }

      if ( i == configCount )
      {
         GST_ERROR("no suitable configuration available\n");
         goto exit;
      }
      sink->soc.eglConfig= eglConfigs[i];

      #ifdef USE_GLES2
      ctxAttrib[0]= EGL_CONTEXT_CLIENT_VERSION;
      ctxAttrib[1]= 2;
      ctxAttrib[2]= EGL_NONE;
      #endif

      sink->soc.eglContext= eglCreateContext( sink->soc.eglDisplay,
                                              sink->soc.eglConfig,
                                              EGL_NO_CONTEXT,
                                              #ifdef USE_GLES2
                                              ctxAttrib
                                              #else
                                              NULL
                                              #endif
                                            );
      if ( sink->soc.eglContext == EGL_NO_CONTEXT )
      {
         GST_ERROR("eglCreateContext failed: %X", eglGetError());
         goto exit;
      }
      GST_INFO("setupEGL: eglContext: %p\n", sink->soc.eglContext);

      sink->soc.eglSetup= true;
   }

exit:

   result= (sink->soc.eglSetup == true);

   if ( eglConfigs )
   {
      free( eglConfigs );
   }

   if ( !result )
   {
      termEGL( sink );
   }

   return result;
}

static void termEGL( GstWesterosSink *sink )
{
   if ( sink->soc.eglDisplay != EGL_NO_DISPLAY )
   {
      eglMakeCurrent( sink->soc.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
      eglDestroyContext( sink->soc.eglDisplay, sink->soc.eglContext );
      sink->soc.eglContext= EGL_NO_CONTEXT;

      if ( !sink->soc.sharedWLDisplay )
      {
         eglTerminate( sink->soc.eglDisplay );
         sink->soc.eglDisplay= EGL_NO_DISPLAY;
         eglReleaseThread();
      }
   }
   sink->soc.eglSetup= false;
}

#ifdef USE_GLES2
static const char *fragShaderText =
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

static const char *vertexShaderText =
  "uniform vec2 u_resolution;\n"
  "uniform mat4 amymatrix;\n"
  "attribute vec2 pos;\n"
  "attribute vec2 uv;\n"
  "varying vec2 v_uv;\n"
  "void main()\n"
  "{\n"
  "  vec4 p = amymatrix * vec4(pos, 0, 1);\n"
  "  vec4 zeroToOne = p / vec4(u_resolution, u_resolution.x, 1);\n"
  "  vec4 zeroToTwo = zeroToOne * vec4(2.0, 2.0, 1, 1);\n"
  "  vec4 clipSpace = zeroToTwo - vec4(1.0, 1.0, 0, 0);\n"
  "  clipSpace.w = 1.0+clipSpace.z;\n"
  "  gl_Position =  clipSpace * vec4(1, -1, 1, 1);\n"
  "  v_uv = uv;\n"
  "}\n";

static GLuint createShader(GstWesterosSink *sink, GLenum shaderType, const char *shaderSource )
{
   GLuint shader= 0;
   GLint shaderStatus;
   GLsizei length;
   char logText[1000];

   shader= glCreateShader( shaderType );
   if ( shader )
   {
      glShaderSource( shader, 1, (const char **)&shaderSource, NULL );
      glCompileShader( shader );
      glGetShaderiv( shader, GL_COMPILE_STATUS, &shaderStatus );
      if ( !shaderStatus )
      {
         glGetShaderInfoLog( shader, sizeof(logText), &length, logText );
         GST_ERROR("Error compiling %s shader: %*s\n",
                   ((shaderType == GL_VERTEX_SHADER) ? "vertex" : "fragment"),
                   length,
                   logText );
      }
   }

   return shader;
}
#endif

static bool setupGfx( GstWesterosSink *sink )
{
   bool result= false;

   if ( !sink->soc.gfxSetup )
   {
      EGLBoolean b;
      GLint statusShader;

      setupEGL( sink );

      sink->soc.wlEGLWindow= wl_egl_window_create( sink->surface, sink->windowWidth, sink->windowHeight );

      sink->soc.eglSurface= eglCreateWindowSurface( sink->soc.eglDisplay,
                                                    sink->soc.eglConfig,
                                                    (EGLNativeWindowType)sink->soc.wlEGLWindow,
                                                    NULL );
      GST_DEBUG("setupGfx: eglSurface %p\n", sink->soc.eglSurface );
      if ( sink->soc.eglSurface == EGL_NO_SURFACE )
      {
         GST_ERROR("eglCreateWindowSurface failed: %X", eglGetError());
         goto exit;
      }

      b= eglMakeCurrent( sink->soc.eglDisplay, sink->soc.eglSurface, sink->soc.eglSurface, sink->soc.eglContext );
      if ( !b )
      {
         GST_ERROR("eglMakeCurrent failed: %X", eglGetError());
         goto exit;
      }

      sink->soc.eglCreateImageKHR= (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
      GST_DEBUG("setupGfx: eglCreateImageKHR %p\n", sink->soc.eglCreateImageKHR );

      sink->soc.eglDestroyImageKHR= (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
      GST_DEBUG("setupGfx: eglDestroyImageKHR %p\n", sink->soc.eglDestroyImageKHR );

      glGetError();
      createImage( sink );

      #ifdef USE_GLES2
      sink->soc.vertId= createShader(sink, GL_VERTEX_SHADER, vertexShaderText);
      sink->soc.fragId= createShader(sink, GL_FRAGMENT_SHADER, fragShaderText);
      sink->soc.progId= glCreateProgram();
      glAttachShader(sink->soc.progId, sink->soc.vertId);
      glAttachShader(sink->soc.progId, sink->soc.fragId);
      glBindAttribLocation(sink->soc.progId, sink->soc.posLoc, "pos");
      glBindAttribLocation(sink->soc.progId, sink->soc.uvLoc, "uv");
      glLinkProgram(sink->soc.progId);
      glGetProgramiv(sink->soc.progId, GL_LINK_STATUS, &statusShader);
      if (!statusShader)
      {
         char log[1000];
         GLsizei len;
         glGetProgramInfoLog(sink->soc.progId, 1000, &len, log);
         GST_ERROR("Error: linking:\n%*s\n", len, log);
      }
      sink->soc.resLoc= glGetUniformLocation(sink->soc.progId,"u_resolution");
      sink->soc.matrixLoc= glGetUniformLocation(sink->soc.progId,"amymatrix");
      sink->soc.alphaLoc= glGetUniformLocation(sink->soc.progId,"u_alpha");
      sink->soc.textureLoc= glGetUniformLocation(sink->soc.progId,"s_texture");
      #else
      glHint( GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST );
      #endif

      sink->soc.rend= &sink->soc.eglRend;

      sink->soc.gfxSetup= true;
   }

exit:

   result= (sink->soc.gfxSetup == true);

   return result;
}

static void termGfx( GstWesterosSink *sink )
{
   destroyImage( sink );
   #ifdef USE_GLES2
   if ( sink->soc.vertId )
   {
      glDeleteShader( sink->soc.vertId );
      sink->soc.vertId= 0;
   }
   if ( sink->soc.fragId )
   {
      glDeleteShader( sink->soc.fragId );
      sink->soc.fragId= 0;
   }
   if ( sink->soc.progId )
   {
      glDeleteProgram( sink->soc.progId );
      sink->soc.progId= 0;
   }
   #endif
   if ( sink->soc.eglSurface != EGL_NO_SURFACE )
   {
      eglDestroySurface( sink->soc.eglDisplay, sink->soc.eglSurface );
      sink->soc.eglSurface= EGL_NO_SURFACE;
   }
   if ( sink->soc.wlEGLWindow )
   {
      wl_egl_window_destroy( sink->soc.wlEGLWindow );
      sink->soc.wlEGLWindow= 0;
   }
   termEGL(sink);
   sink->soc.gfxSetup= false;
}

static void createImage( GstWesterosSink *sink )
{
   destroyImage( sink );

   glGenTextures( 1, &sink->soc.textureId );

   glBindTexture(GL_TEXTURE_2D, sink->soc.textureId);

   glTexImage2D( GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 sink->soc.frameWidth,
                 sink->soc.frameHeight,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 NULL );

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

   sink->soc.eglImage= sink->soc.eglCreateImageKHR( sink->soc.eglDisplay,
                                                    sink->soc.eglContext,
                                                    EGL_GL_TEXTURE_2D_KHR,
                                                    (EGLClientBuffer)sink->soc.textureId,
                                                    NULL );

   sink->soc.textureWidth= sink->soc.frameWidth;
   sink->soc.textureHeight= sink->soc.frameHeight;
}

static void destroyImage( GstWesterosSink *sink )
{
   if ( sink->soc.textureId )
   {
      glDeleteTextures( 1, &sink->soc.textureId );
      sink->soc.textureId= 0;
   }
   if ( sink->soc.eglImage )
   {
      sink->soc.eglDestroyImageKHR( sink->soc.eglDisplay, sink->soc.eglImage );
      sink->soc.eglImage= 0;
   }
   sink->soc.textureWidth= 0;
   sink->soc.textureHeight= 0;
}

static void drawImage ( GstWesterosSink *sink )
{
   float x, y, w, h;

   x= 0;
   y= 0;
   w= sink->windowWidth;
   h= sink->windowHeight;

   const float verts[4][2] =
   {
      { x, y },
      { x+w, y },
      { x,  y+h },
      { x+w, y+h }
   };

   const float uv[4][2] =
   {
      #ifdef USE_GLES2
      { 0,  0 },
      { 1,  0 },
      { 0,  1 },
      { 1,  1 }
      #else
      { 0,  1 },
      { 1,  1 },
      { 0,  0 },
      { 1,  0 }
      #endif
   };

   #ifdef USE_GLES2
   glViewport( 0, 0, w, h );
   glClearColor( 0.0f, 1.0f, 0.0f, 1.0f );
   glClear( GL_COLOR_BUFFER_BIT );
   glEnable(GL_BLEND);

   glBlendFuncSeparate( GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE );
   glUseProgram(sink->soc.progId);
   glUniform2f(sink->soc.resLoc, w, h);
   glUniformMatrix4fv(sink->soc.matrixLoc, 1, GL_FALSE, (GLfloat*)sink->soc.matrix);
   glUniform1f(sink->soc.alphaLoc, 1.0f);

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, sink->soc.textureId);
   glUniform1i(sink->soc.textureLoc, 0);
   glVertexAttribPointer(sink->soc.posLoc, 2, GL_FLOAT, GL_FALSE, 0, verts);
   glVertexAttribPointer(sink->soc.uvLoc, 2, GL_FLOAT, GL_FALSE, 0, uv);
   glEnableVertexAttribArray(sink->soc.posLoc);
   glEnableVertexAttribArray(sink->soc.uvLoc);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   glDisableVertexAttribArray(sink->soc.posLoc);
   glDisableVertexAttribArray(sink->soc.uvLoc);
   #else
   glViewport( 0, 0, w, h );
   glClearColor( 0.0f, 1.0f, 0.0f, 1.0f );
   glClear( GL_COLOR_BUFFER_BIT );
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrthof( 0.0f, sink->windowWidth, 0.0f, sink->windowHeight, 0.0f, 1.0f );
   glEnableClientState( GL_VERTEX_ARRAY );
   glVertexPointer( 2, GL_FLOAT, 0, verts );
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTexCoordPointer(2, GL_FLOAT, 0, uv);
   glEnableClientState(GL_TEXTURE_COORD_ARRAY);
   glEnable(GL_TEXTURE_2D);
   glBindTexture(GL_TEXTURE_2D, sink->soc.textureId);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   #endif

   glFlush();
   glFinish();

   eglSwapBuffers( sink->soc.eglDisplay, sink->soc.eglSurface );

   wl_display_flush(sink->display);
   wl_display_dispatch_queue_pending(sink->display, sink->queue);
}

static void processFrame( GstWesterosSink *sink, GstBuffer *buffer )
{  
   int rc; 
   OMX_ERRORTYPE omxerr;
   OMX_BUFFERHEADERTYPE *buff= 0;
   #ifdef USE_GST1
   GstMapInfo map;
   #endif
   int inSize, avail, copylen, filledlen;
   unsigned char *inData;
   bool windowChange;
   gint64 nanoTime;

   if ( sink->soc.playingVideo && !sink->flushStarted )
   {
      #ifdef USE_GST1
      gst_buffer_map(buffer, &map, (GstMapFlags)GST_MAP_READ);
      inSize= map.size;
      inData= map.data;
      #else
      inSize= (int)GST_BUFFER_SIZE(buffer);
      inData= GST_BUFFER_DATA(buffer);
      #endif

      GST_LOG("processFrame: buffer %p, len %d timestamp: %lld", buffer, inSize, GST_BUFFER_PTS(buffer) );
      
      nanoTime= GST_BUFFER_PTS(buffer);

      LOCK(sink);
      windowChange= sink->windowChange;
      if ( windowChange )
      {
         gst_westeros_sink_soc_update_video_position( sink );
      }
      UNLOCK(sink);
    
      while ( inSize )
      {
         buff= sink->soc.buffCurrent;
         
         while ( !buff )
         {
            if ( sink->soc.semInputActive )
            {
               rc= sem_trywait( &sink->soc.semInputBuffers );

               if ( !sink->soc.playingVideo )
               {
                  goto exit;
               }

               if ( rc != 0 )
               {
                  usleep( 1000 );
               }

               if ( rc == 0 )
               {
                  LOCK_SOC( sink );
                  if ( sink->soc.countInputBuffers )
                  {
                     --sink->soc.countInputBuffers;
                     buff= sink->soc.inputBuffers[sink->soc.countInputBuffers];
                     buff->nTimeStamp.nLowPart= ((nanoTime/1000LL)&0xFFFFFFFFLL);
                     buff->nTimeStamp.nHighPart= (((nanoTime/1000LL)>>32)&0xFFFFFFFFLL);
                  }
                  UNLOCK_SOC( sink );
               }
            }
            else
            {
               goto exit;
            }            
         }
         
         if ( buff )
         {
            avail= buff->nAllocLen-buff->nFilledLen;
            copylen= inSize;
            if ( copylen > avail )
            {
               copylen= avail;
            }
            GST_LOG("processFrame: copy %d bytes into buff %p (%d of %d)", 
                    copylen, buff, sink->soc.countInputBuffers, sink->soc.capacityInputBuffers );
            memcpy( buff->pBuffer+buff->nFilledLen, inData, copylen );
            buff->nFilledLen += copylen;
            inSize -= copylen;
            avail -= copylen;
            inData += copylen;
            
            {
               if ( sink->soc.firstBuffer )
               {
                  if ( nanoTime >= sink->positionSegmentStart )
                  {
                     sink->soc.firstBuffer= false;
                     buff->nFlags= OMX_BUFFERFLAG_STARTTIME;
                  }
                  else
                  {
                     buff->nFlags= OMX_BUFFERFLAG_DECODEONLY;
                  }
               }
               else
               {
                  buff->nFlags= 0;
               }
               buff->nOffset= 0;
               filledlen= buff->nFilledLen;
               omxerr= OMX_EmptyThisBuffer( sink->soc.vidDec.hComp, buff );
               GST_LOG("processFrame: pass buffer %p to vidDec with %d bytes", buff, filledlen );
               if ( omxerr != OMX_ErrorNone )
               {
                  GST_ERROR( "processFrame: error from OMX_EmptyThisBuffer: %x", omxerr );
                  goto exit;
               }
               buff= 0;
               sink->soc.buffCurrent= 0;
            }
         }
      }
      
      sink->soc.buffCurrent= buff;

      #ifdef USE_GST1
      gst_buffer_unmap( buffer, &map);
      #endif
   }

exit:
   return;     
}

static gpointer captureThread(gpointer data)
{
   GstWesterosSink *sink= (GstWesterosSink*)data;

   GST_DEBUG_OBJECT(sink, "captureThread: enter");

   setupGfx( sink );

   while( !sink->soc.quitCaptureThread )
   {
      waitForNewFrame( sink );
      if ( sink->soc.useGfxPath && sink->soc.newFrame )
      {
         sink->soc.newFrame= false;
         drawImage( sink );
      }
   }

   GST_DEBUG_OBJECT(sink, "captureThread: ending");

   termGfx( sink );

   GST_DEBUG_OBJECT(sink, "captureThread: exit");

   g_thread_exit(NULL);

   return NULL;
}

// For soc specfic pad query
gboolean gst_westeros_sink_soc_query( GstWesterosSink *sink, GstQuery *query )
{
   WESTEROS_UNUSED(sink);
   WESTEROS_UNUSED(query);
   gboolean result = FALSE;

   //TBD

   return result;
}

// For soc specific element query
static gboolean gst_westeros_sink_soc_query_element( GstElement *element, GstQuery *query )
{
   gboolean result = FALSE;
   GstWesterosSink *sink= GST_WESTEROS_SINK(element);

   switch (GST_QUERY_TYPE(query))
   {
      case GST_QUERY_POSITION:
         if ( sink->soc.playingVideo )
         {
            OMX_ERRORTYPE omxerr;
            OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;

            memset( &clockState, 0, sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE) );
            clockState.nSize= sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE);
            clockState.nVersion.nVersion= OMX_VERSION;
            GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_query_element: calling OMX_GetConifg for clock state");
            omxerr= OMX_GetConfig( sink->soc.clock.hComp, OMX_IndexConfigTimeClockState, &clockState );
            if ( (omxerr == OMX_ErrorNone) && (clockState.eState == OMX_TIME_ClockStateRunning) )
            {
               OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;

               memset( &timeStamp, 0, sizeof(OMX_TIME_CONFIG_TIMESTAMPTYPE) );
               timeStamp.nSize= sizeof(OMX_TIME_CONFIG_TIMESTAMPTYPE);
               timeStamp.nVersion.nVersion= OMX_VERSION;
               timeStamp.nPortIndex= sink->soc.clock.otherOutPort;
               GST_DEBUG_OBJECT(sink, "gst_westeros_sink_soc_query_element: calling OMX_GetConifg for timeStamp");
               omxerr= OMX_GetConfig( sink->soc.clock.hComp, OMX_IndexConfigTimeCurrentMediaTime, &timeStamp );
               if ( omxerr == OMX_ErrorNone )
               {
                  gint64 position= (((gint64)(timeStamp.nTimestamp.nLowPart)) + (((gint64)timeStamp.nTimestamp.nHighPart)<<32))*1000LL;
                  LOCK( sink );
                  sink->position= position;
                  UNLOCK( sink );
               }
               else
               {
                  GST_ERROR("gst_westeros_sink_soc_query_element: OMX_GetConfig for timeStamp: omxerr %d", omxerr );
               }
            }
            else
            {
               GST_ERROR("gst_westeros_sink_soc_query_element: OMX_GetConfig for clockState: omxerr %d", omxerr );
            }
         }
         break;
       default:
         break;
   }
   if ( queryOrg )
   {
      result= queryOrg( element, query );
   }
   return result;
}

