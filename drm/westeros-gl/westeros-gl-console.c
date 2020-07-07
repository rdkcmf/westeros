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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define INT_FATAL(FORMAT, ...)      wstLog(0, "FATAL: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_ERROR(FORMAT, ...)      wstLog(0, "ERROR: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_WARNING(FORMAT, ...)    wstLog(1, "WARN: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_INFO(FORMAT, ...)       wstLog(2, "INFO: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_DEBUG(FORMAT, ...)      wstLog(3, "DEBUG: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE1(FORMAT, ...)     wstLog(4, "TRACE: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE2(FORMAT, ...)     wstLog(5, "TRACE: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_TRACE3(FORMAT, ...)     wstLog(6, "TRACE: %s:%d " FORMAT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define INT_FRAME(FORMAT, ...)      wstFrameLog( "FRAME: " FORMAT "\n", __VA_ARGS__)

#define FATAL(...)                  INT_FATAL(__VA_ARGS__, "")
#define ERROR(...)                  INT_ERROR(__VA_ARGS__, "")
#define WARNING(...)                INT_WARNING(__VA_ARGS__, "")
#define INFO(...)                   INT_INFO(__VA_ARGS__, "")
#define DEBUG(...)                  INT_DEBUG(__VA_ARGS__, "")
#define TRACE1(...)                 INT_TRACE1(__VA_ARGS__, "")
#define TRACE2(...)                 INT_TRACE2(__VA_ARGS__, "")
#define TRACE3(...)                 INT_TRACE3(__VA_ARGS__, "")
#define FRAME(...)                  INT_FRAME(__VA_ARGS__, "")

typedef struct _WstDisplayClientConnection
{
   const char *name;
   struct sockaddr_un addr;
   int socketFd;
   int result;
} WstDisplayClientConnection;

static void wstLog( int level, const char *fmt, ... );
static void wstDestroyDisplayClientConnection( WstDisplayClientConnection *conn );

static int g_activeLevel= 2;

static long long getCurrentTimeMillis(void)
{
   struct timeval tv;
   long long utcCurrentTimeMillis;

   gettimeofday(&tv,0);
   utcCurrentTimeMillis= tv.tv_sec*1000LL+(tv.tv_usec/1000LL);

   return utcCurrentTimeMillis;
}

static void wstLog( int level, const char *fmt, ... )
{
   if ( level <= g_activeLevel )
   {
      va_list argptr;
      fprintf( stderr, "%lld: ", getCurrentTimeMillis());
      va_start( argptr, fmt );
      vfprintf( stderr, fmt, argptr );
      va_end( argptr );
   }
}

static WstDisplayClientConnection *wstCreateDisplayClientConnection()
{
   WstDisplayClientConnection *conn= 0;
   int rc;
   bool error= true;
   const char *workingDir;
   int pathNameLen, addressSize;

   conn= (WstDisplayClientConnection*)calloc( 1, sizeof(WstDisplayClientConnection));
   if ( conn )
   {
      conn->socketFd= -1;
      conn->name= "display";

      workingDir= getenv("XDG_RUNTIME_DIR");
      if ( !workingDir )
      {
         ERROR("wstCreateDisplayClientConnection: XDG_RUNTIME_DIR is not set");
         goto exit;
      }

      pathNameLen= strlen(workingDir)+strlen("/")+strlen(conn->name)+1;
      if ( pathNameLen > (int)sizeof(conn->addr.sun_path) )
      {
         ERROR("wstCreateDisplayClientConnection: name for server unix domain socket is too long: %d versus max %d",
                pathNameLen, (int)sizeof(conn->addr.sun_path) );
         goto exit;
      }

      conn->addr.sun_family= AF_LOCAL;
      strcpy( conn->addr.sun_path, workingDir );
      strcat( conn->addr.sun_path, "/" );
      strcat( conn->addr.sun_path, conn->name );

      conn->socketFd= socket( PF_LOCAL, SOCK_STREAM|SOCK_CLOEXEC, 0 );
      if ( conn->socketFd < 0 )
      {
         ERROR("wstCreateDisplayClientConnection: unable to open socket: errno %d", errno );
         goto exit;
      }

      addressSize= pathNameLen + offsetof(struct sockaddr_un, sun_path);

      rc= connect(conn->socketFd, (struct sockaddr *)&conn->addr, addressSize );
      if ( rc < 0 )
      {
         ERROR("wstCreateDisplayClientConnection: connect failed for socket: errno %d", errno );
         goto exit;
      }

      error= false;
   }

exit:

   if ( error )
   {
      wstDestroyDisplayClientConnection( conn );
      conn= 0;
   }

   return conn;
}

static void wstDestroyDisplayClientConnection( WstDisplayClientConnection *conn )
{
   if ( conn )
   {
      conn->addr.sun_path[0]= '\0';

      if ( conn->socketFd >= 0 )
      {
         close( conn->socketFd );
         conn->socketFd= -1;
      }

      free( conn );
   }
}

static bool wstDisplayClientGetResponse( WstDisplayClientConnection *conn )
{
   bool result= false;
   struct msghdr msg;
   struct iovec iov[1];
   unsigned char mbody[256+3];
   unsigned char *m= mbody;
   int len;

   iov[0].iov_base= (char*)mbody;
   iov[0].iov_len= sizeof(mbody);

   msg.msg_name= NULL;
   msg.msg_namelen= 0;
   msg.msg_iov= iov;
   msg.msg_iovlen= 1;
   msg.msg_control= 0;
   msg.msg_controllen= 0;
   msg.msg_flags= 0;

   do
   {
      len= recvmsg( conn->socketFd, &msg, 0 );
   }
   while ( (len < 0) && (errno == EINTR));

   if ( len > 0 )
   {
      unsigned char *m= mbody;
      while ( len >= 4 )
      {
         if ( (m[0] == 'D') && (m[1] == 'S') )
         {
            int mlen, id;
            mlen= m[2];
            if ( len >= (mlen+2) )
            {
               int rc= -1;

               result= true;

               if ( sscanf( m+3, "%d:", &rc ) == 1 )
               {
                  conn->result= rc;
               }

               printf("Response: [%s]\n", m+3);

               m += (mlen+3);
               len -= (mlen+3);
            }
            else
            {
               len= 0;
            }
         }
         else
         {
            len= 0;
         }
      }
   }

   return result;
}

static void wstDisplayClientSendMessage( WstDisplayClientConnection *conn, int mlen, char *m )
{
   struct msghdr msg;
   struct iovec iov[1];
   unsigned char mbody[256+4];
   int len;
   int sentLen;

   if ( mlen > sizeof(mbody)-5 )
   {
      mlen= sizeof(mbody)-5;
      WARNING("message truncated");
   }

   msg.msg_name= NULL;
   msg.msg_namelen= 0;
   msg.msg_iov= iov;
   msg.msg_iovlen= 1;
   msg.msg_control= 0;
   msg.msg_controllen= 0;
   msg.msg_flags= 0;

   len= 0;
   mbody[len++]= 'D';
   mbody[len++]= 'S';
   mbody[len++]= mlen+1;
   strncpy( &mbody[len], m, mlen+1 );
   len += (mlen+1);

   iov[0].iov_base= (char*)mbody;
   iov[0].iov_len= len;

   do
   {
      sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
   }
   while ( (sentLen < 0) && (errno == EINTR));

   if ( sentLen == len )
   {
      DEBUG("sent msg len %d to display server", len);
   }
}

int main( int argc, const char **argv )
{
   int nRC= 0;
   WstDisplayClientConnection *conn= 0;
   char msg[256];

   printf("westeros-gl-console\n");

   conn= wstCreateDisplayClientConnection();
   if ( !conn )
   {
      ERROR("Unable to connect to display server");
      nRC= -1;
      goto exit;
   }

   if ( argc > 1 )
   {
      int i, len, mlen;
      msg[0]= '\0';
      for( i= 1; i < argc; ++i )
      {
         mlen= strlen(msg);
         len= strlen(argv[i]);
         if ( mlen+len+2 < sizeof(msg) )
         {
            strcat( msg, argv[i] );
            strcat( msg, " " );
         }
      }
      wstDisplayClientSendMessage( conn, strlen(msg), msg );
      wstDisplayClientGetResponse( conn );

      nRC= conn->result;
   }

exit:
   if ( conn )
   {
      wstDestroyDisplayClientConnection( conn );
   }

   return nRC;
}
