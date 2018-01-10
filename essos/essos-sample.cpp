/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2017 RDK Management
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
#include "essos.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/input.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static EssCtx *ctx= 0;
static bool gRunning;
static GLuint gProg= 0;
static GLuint gFrag= 0;
static GLuint gVert= 0;
static GLuint gOffset= 0;
static GLuint gXform;
static GLuint gPos;
static GLuint gColor;
static int gDisplayWidth;
static int gDisplayHeight;
static int gRow=1;
static int gCol=1;
static long long gStartTime;
static long long gCurrTime;

static bool setupGL(void);
static bool renderGL(void);


static long long currentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static void signalHandler(int signum)
{
   printf("signalHandler: signum %d\n", signum);
   gRunning= false;
}

static void terminated( void * )
{
   printf("terminated event\n");
   gRunning= false;
}

static EssTerminateListener terminateListener=
{
   terminated
};

static void keyPressed( void *, unsigned int key )
{
   switch( key )
   {
       case KEY_UP:
          gRow= gRow-1;
          if ( gRow < 0 ) gRow= 2;
          break;
       case KEY_DOWN:
          gRow= gRow+1;
          if ( gRow > 2 ) gRow= 0;
          break;
       case KEY_LEFT:
          gCol= gCol-1;
          if ( gCol < 0 ) gCol= 2;
          break;
       case KEY_RIGHT:
          gCol= gCol+1;
          if ( gCol > 2 ) gCol= 0;
          break;
       default:
          break;
   }
}

static void keyReleased( void *, unsigned int )
{
}

static EssKeyListener keyListener=
{
   keyPressed,
   keyReleased
};

static void pointerMotion( void *, int, int )
{
}

static void pointerButtonPressed( void *, int button, int x, int y )
{
   int thx0= gDisplayWidth * 160/1280;
   int thx1= thx0 + 320;
   int thx2= thx1 + 320;
   int thy0= gDisplayHeight * 90/720;
   int thy1= thy0 + 180;
   int thy2= thy1 + 180;

   if ( x < thx1 ) 
      gCol= 0;
   else if ( x < thx2 )
      gCol= 1;
   else
      gCol= 2;   

   if ( y < thy1 ) 
      gRow= 0;
   else if ( y < thy2 )
      gRow= 1;
   else
      gRow= 2;
}

static void pointerButtonReleased( void *, int, int, int )
{
}

static EssPointerListener pointerListener=
{
   pointerMotion,
   pointerButtonPressed,
   pointerButtonReleased
};

int main( int argc, char **argv )
{
   int nRC= 0;

   printf("ess-sample v1.0\n");
   ctx= EssContextCreate();
   if ( ctx )
   {
      int len;
      bool error= false;

      for( int i= 1; i < argc; ++i )
      {
         len= strlen(argv[i]);
         if ( (len == 9) && !strncmp( (const char*)argv[i], "--wayland", len) )
         {
            if ( !EssContextSetUseWayland( ctx, true ) )
            {
               error= true;
               break;
            }
         }
      }

      if ( !EssContextSetTerminateListener( ctx, 0, &terminateListener ) )
      {
         error= true;
      }

      if ( !EssContextSetKeyListener( ctx, 0, &keyListener ) )
      {
         error= true;
      }

      if ( !EssContextSetPointerListener( ctx, 0, &pointerListener ) )
      {
         error= true;
      }

      if ( !error )
      {
         struct sigaction sigint;

         sigint.sa_handler= signalHandler;
         sigemptyset(&sigint.sa_mask);
         sigint.sa_flags= SA_RESETHAND;
         sigaction(SIGINT, &sigint, NULL);

         if ( !EssContextStart( ctx ) )
         {
            error= true;
         }
         else
         if ( !EssContextGetDisplaySize( ctx, &gDisplayWidth, &gDisplayHeight ) )
         {
            error= true;
         }

         if ( !error )
         {
            setupGL();

            gRunning= true;
            while( gRunning )
            {
               renderGL();
               EssContextUpdateDisplay( ctx );
               EssContextRunEventLoopOnce( ctx );
            }
         }
      }

      if ( error )
      {
         const char *detail= EssContextGetLastErrorDetail( ctx );
         printf("Essos error: (%s)\n", detail );
      }

      EssContextDestroy( ctx );
   }

   return nRC;
}

static const char *vert_shader_text=
   "uniform mat4 xform;\n"
   "uniform vec4 offset;\n"
   "attribute vec4 pos;\n"
   "attribute vec4 color;\n"
   "varying vec4 v_color;\n"
   "void main()\n"
   "{\n"
   "  gl_Position= xform * pos + offset;\n"
   "  v_color= color;\n"
   "}\n";

static const char *frag_shader_text=
   "precision mediump float;\n"
   "varying vec4 v_color;\n"
   "void main()\n"
   "{\n"
   "  gl_FragColor= v_color;\n"
   "}\n";

static GLuint createShader(GLenum shaderType, const char *shaderSource )
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
         printf("Error compiling %s shader: %*s\n",
                ((shaderType == GL_VERTEX_SHADER) ? "vertex" : "fragment"),
                length,
                logText );
      }
   }

   return shader;
}

static bool setupGL(void)
{
   bool result= false;
   GLuint frag, vert;
   GLint status;

   gFrag= createShader(GL_FRAGMENT_SHADER, frag_shader_text);
   gVert= createShader(GL_VERTEX_SHADER, vert_shader_text);

   gProg= glCreateProgram();
   glAttachShader(gProg, gFrag);
   glAttachShader(gProg, gVert);
   glLinkProgram(gProg);

   glGetProgramiv(gProg, GL_LINK_STATUS, &status);
   if (!status)
   {
      char log[1000];
      GLsizei len;
      glGetProgramInfoLog(gProg, 1000, &len, log);
      fprintf(stderr, "Error: linking:\n%*s\n", len, log);
      goto exit;
   }

   glUseProgram(gProg);

   gPos= 0;
   gColor= 1;

   glBindAttribLocation(gProg, gPos, "pos");
   glBindAttribLocation(gProg, gColor, "color");
   glLinkProgram(gProg);

   gOffset= glGetUniformLocation(gProg, "offset");
   gXform= glGetUniformLocation(gProg, "xform");

   gStartTime= currentTimeMillis();

exit:
   return result;
}

static bool renderGL(void)
{
   static const GLfloat verts[3][2] = {
      { -0.5, -0.5 },
      {  0.5, -0.5 },
      {  0.0,  0.5 }
   };
   static const GLfloat colors[3][4] = {
      { 1, 0, 0, 1.0 },
      { 0, 1, 0, 1.0 },
      { 0, 0, 1, 1.0 }
   };
   GLfloat angle;
   GLfloat matrix[4][4] = {
      { 1, 0, 0, 0 },
      { 0, 1, 0, 0 },
      { 0, 0, 1, 0 },
      { 0, 0, 0, 1 }
   };
   static const uint32_t speed_div= 5;
   EGLint rect[4];

   glViewport( 0, 0, gDisplayWidth, gDisplayHeight );
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT);

   gCurrTime= currentTimeMillis();

   angle= ((gCurrTime-gStartTime) / speed_div) % 360 * M_PI / 180.0;
   matrix[0][0]= (1.0/3.0)*cos(angle);
   matrix[0][2]= (1.0/3.0)*sin(angle);
   matrix[1][1]= (1.0/3.0);
   matrix[2][0]= -(1.0/3.0)*sin(angle);
   matrix[2][2]= (1.0/3.0)*cos(angle);

   glUniform4f(gOffset, -0.3333+gCol*0.3333, 0.3333-gRow*0.3333, 0, 0 );
   glUniformMatrix4fv(gXform, 1, GL_FALSE, (GLfloat *)matrix);

   glVertexAttribPointer(gPos, 2, GL_FLOAT, GL_FALSE, 0, verts);
   glVertexAttribPointer(gColor, 4, GL_FLOAT, GL_FALSE, 0, colors);
   glEnableVertexAttribArray(gPos);
   glEnableVertexAttribArray(gColor);

   glDrawArrays(GL_TRIANGLES, 0, 3);

   glDisableVertexAttribArray(gPos);
   glDisableVertexAttribArray(gColor);
   
   GLenum err= glGetError();
   if ( err != GL_NO_ERROR )
   {
      printf( "renderGL: glGetError() = %X\n", err );
   }
}

