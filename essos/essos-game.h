/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
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

#ifndef __ESSOS_GAME__
#define __ESSOS_GAME__

#ifndef __cplusplus
#include <stdbool.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _EssGamepad EssGamepad;

/**
 * EssGampadConnectionListener
 *
 * connected
 * Called to notify an app that a gamepad has been connected
 *
 * disconnected
 * Called to notify an app that a gamepad has been disconnected
 *
 */
typedef struct _EssGamepadConnectionListener
{
   void (*connected) ( void *userData, EssGamepad *gp );
   void (*disconnected) ( void *userData, EssGamepad *gp );
} EssGamepadConnectionListener;

/**
 * EssGampadListener
 * All id values used are defined in linux/input.h.
 *
 * buttonPressed
 * Called when a gamepad button is pressed passing button id
 *
 * buttonReleased
 * Called when a gamepad button is released passing button id
 *
 * axisChanged
 * Called when a gamepad axis value changes passing the axis
 * id and value.
 *
 */
typedef struct _EssGamepadEventListener
{
   void (*buttonPressed)( void *userData, int buttonId );
   void (*buttonReleased)( void *userData, int buttonId );
   void (*axisChanged)( void *userData, int axisId, int value );
} EssGamepadEventListener;

/**
 * EssContextSetGamepadConnectionListener
 *
 * Set a gamepad connection listener (see EssGamepadConnectionListener) to receive 
 * gamepad connect/disconnect event callbacks.  The connected method will be invoked
 * immediately if any gamepads are connected at the time this listener is set.
 */
bool EssContextSetGamepadConnectionListener( EssCtx *ctx, void *userData, EssGamepadConnectionListener *listener );

/**
 * EssGamepadSetEventListener
 *
 * Set a gamepad event listener (see EssGamepadEventListener) to receive gamepad event callbacks. Gamepad
 * definitions are Linux values defined by linux/input.h for buttons and absolute axes.
 */
bool EssGamepadSetEventListener( EssGamepad *gp, void *userData, EssGamepadEventListener *listener );

/**
 * EssGamepadGetDeviceName
 *
 * Obtain the gamepad identifier string.
 */
const char *EssGamepadGetDeviceName( EssGamepad *gp );

/**
 * EssGamepadGetDriverVersion
 *
 * Obtain the gamepad driver version number.
 */
unsigned int EssGamepadGetDriverVersion( EssGamepad *gp );

/**
 * EssGamepadGetButtonMap
 *
 * Retrieve the gamepad button count and map.  The number of
 * buttons will be written to count and the map values will be
 * written to map.  Call with map set to NULL to get the count
 * of buttons, then allocate an array of int's and call again
 * to get the map values.  The map is an array of button id's
 * using values from linux/input.h.  The buttonPressed and buttonReleased
 * listener methods will be invoked passing these button id values.
 */
bool EssGamepadGetButtonMap( EssGamepad *gp, int *count, int *map );

/**
 * EssGamepadGetAxisMap
 *
 * Retrieve the gamepad axis count and map.  The number of axes
 * will be writtin to count and the map values will be written
 * to map.  Call with map set to NULL to get the count of axes,
 * then allocate an array of int's and call again to get the
 * the map values.  The map is an array of axis id's using values
 * from linux/input.h for absolute axes.  The axisChanged listener
 * methods will be invoked passing these axes id values.
 */
bool EssGamepadGetAxisMap( EssGamepad *gp, int *count, int *map );

/**
 * EssGamepadGetState
 *
 * Retrieve the current state of all buttons and axes.  The number
 * of buttons and axes should be retrieved via calls to  EssGamepadGetButtonMap
 * and EssGamepadGetAxisMap.
 */
bool EssGamepadGetState( EssGamepad *gp, int *buttonState, int *axisState );

#if defined(__cplusplus)
} //extern "C"
#endif

#endif
