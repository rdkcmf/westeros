# MediaCapture
MediaCapture

MediaCapture is a tool that can be used with media pipelines that employ westeros-sink to enable 
the capture of smaples of media streams.   Streams can be captured to a file or sent via http POST
to an endpoint.

Note that mediacapture makes use of rtRemote for RPC.  rtRemote is part of pxscene (https://github.com/pxscene/pxCore).

To use mediacapture, build and install libmediacapture.so, mediacapture-daemon, 
and mediacapture-test.  Then ensure the following is defined in the envronment of the process 
creating media pipelines:

export WESTEROSSINK_ENABLE_CAPTURE=1

First launch the mediacapture-daemon:

mediacapture-daemon &

Then start your application that will play media using westeros-sink.

Finally, run mediacapture-test:

mediacapture endpoint <url> <duration>

or 

mediacapture file <file> <duration>

where duration is the desired capture duration expressed as decimal value in milliseconds.  Press 'l' to list available 
pipelines.  Press '0' to '9' to start capture from the specified pipeline.  To end the capture prior to the end 
of the duration, press 's'.  Press 'q' to exit the app.


---
# Copyright and license

If not stated otherwise in this file or this component's Licenses.txt file the
following copyright and licenses apply:

Copyright 2017 RDK Management

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

