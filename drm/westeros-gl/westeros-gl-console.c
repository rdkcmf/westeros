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
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "westeros-gl-console-helper.h"

int main( int argc, const char **argv )
{
   int nRC= 0;
   char msg[256];
   char *rsp= 0;

   printf("westeros-gl-console\n");

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

      nRC= WstGLConsoleCommand( msg, &rsp );
      if ( rsp )
      {
         printf("Response: [%s]\n", rsp);
         free( rsp );
      }
   }

   return nRC;
}
