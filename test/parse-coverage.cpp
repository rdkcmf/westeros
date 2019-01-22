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

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

void processFile( FILE *pFile, int *executable, int *executed );

void showUsage( void )
{
   printf("Usage: parse-coverage <file1> ... <fileN>\n");
   printf(" where: each file is the name of a gcov .gcno data file\n");
   printf("\n");
}

int main( int argc, const char **argv )
{
   int rc= 0;
   int argidx;
   const char *fileName;
   FILE *pFile= 0;
   int totalExecutable= 0;
   int totalExecuted= 0;

   if ( argc < 2 )
   {
      showUsage();
      rc= 1;
      goto exit;
   }

   for( argidx= 1; argidx < argc; ++argidx )
   {
      fileName= argv[argidx];
      pFile= fopen( fileName, "rt" );
      if ( pFile )
      {
         int executable= 0;
         int executed= 0;
         processFile( pFile, &executable, &executed );
         fclose( pFile );
         printf("%s: %d of %d : %.1f%%\n", fileName, executed, executable, (executable ? 100.0*(float)executed/(float)executable : 0.0) );

         totalExecutable += executable;
         totalExecuted += executed;
      }
      else
      {
         printf("Error: unable to open input file (%s)\n", fileName );
      }
   }

   printf("=============================================================================\n");
   printf("Overall: %d of %d : %.1f%%\n", totalExecuted, totalExecutable, (totalExecutable ? 100.0*(float)totalExecuted/(float)totalExecutable : 0.0) );
   printf("=============================================================================\n");

exit:
   return rc;
}

void processFile( FILE *pFile, int *executable, int *executed )
{
   int lineBufferCapacity= 0;
   int lineBufferLen= 0;
   char *lineBuffer= 0;
   int c;
   int linesExecuted= 0;
   int linesExecutable= 0;
   bool stop= false;

   for( ; ; )
   {
      lineBufferLen= 0;
      for( ; ; )
      {
         c= fgetc( pFile );
         if ( c == EOF ) goto exit;

         if ( c == '\n' ) break;

         if ( lineBufferLen >= lineBufferCapacity )
         {
            char *newBuffer= 0;
            int newSize= lineBufferCapacity*2;
            if ( newSize == 0 ) newSize= 1024;
            newBuffer= (char*)malloc( newSize );
            if ( newBuffer )
            {
               if ( lineBuffer )
               {
                  memcpy( newBuffer, lineBuffer, lineBufferLen );
                  free( lineBuffer );
               }
               lineBuffer= newBuffer;
               lineBufferCapacity= newSize;
            }
            else
            {
               printf("Error: no memory for line buffer size %d bytes\n", newSize );
               goto exit;
            }
         }

         lineBuffer[lineBufferLen++]= c;      
      }
      if ( lineBufferLen )
      {
         int i;
         stop= false;
         for( i= 0; i < lineBufferLen; ++i )
         {
            c= lineBuffer[i];
            switch( c )
            {
               case '1':
               case '2':
               case '3':
               case '4':
               case '5':
               case '6':
               case '7':
               case '8':
               case '9':
                  ++linesExecutable;
                  ++linesExecuted;
                  stop= true;
                  break;
               case '#':
                  ++linesExecutable;
                  stop= true;
                  break;
               case '-':
                  stop= true;
                  break;
               default:
                  break;
            }
            if ( stop ) break;
         }
      }
   }

exit:

   *executable= linesExecutable;
   *executed= linesExecuted;

   if ( lineBuffer )
   {
      free( lineBuffer );
   }
}


