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

#ifndef __WESTEROS_SINK_SOC_SVP_UTIL_H__
#define __WESTEROS_SINK_SOC_SVP_UTIL_H__

#define WESTEROS_SINK_SVP

static void wstSVPSetInputMemMode( GstWesterosSink *sink, int mode );
static void wstSVPSetOutputMemMode( GstWesterosSink *sink, int mode );
static void wstSVPDecoderConfig( GstWesterosSink *sink );
static bool wstSVPSetupOutputBuffersDmabuf( GstWesterosSink *sink );
static void wstSVPTearDownOutputBuffersDmabuf( GstWesterosSink *sink );

#endif
