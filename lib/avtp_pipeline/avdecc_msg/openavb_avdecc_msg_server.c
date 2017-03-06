/*************************************************************************************************************
Copyright (c) 2012-2017, Harman International Industries, Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS LISTED "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS LISTED BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Attributions: The inih library portion of the source code is licensed from
Brush Technology and Ben Hoyt - Copyright (c) 2009, Brush Technology and Copyright (c) 2009, Ben Hoyt.
Complete license and copyright information can be found at
https://github.com/benhoyt/inih/commit/74d2ca064fb293bc60a77b0bd068075b293cf175.
*************************************************************************************************************/

/*
 * MODULE SUMMARY :
 *
 * Stream clients (talkers or listeners) must connect to the central
 * "avdecc_msg" process to create a reservation for their traffic.
 *
 * This code implements the server side of the IPC.
 *
 * It provides proxy functions for the avdecc_msg to call.  The arguments
 * for those calls are packed into messages, which are unpacked in the
 * process and then used to call the real functions.
 *
 * Current IPC uses unix sockets.  Can change this by creating a new
 * implementations in openavb_avdecc_msg_client.c and openavb_avdecc_msg_server.c
 */

#include <stdlib.h>
#include <string.h>

//#define AVB_LOG_LEVEL  AVB_LOG_LEVEL_DEBUG
#define	AVB_LOG_COMPONENT	"AVDECC Msg"
#include "openavb_pub.h"
#include "openavb_log.h"
#include "openavb_avdecc_pub.h"

#include "openavb_avdecc_msg_server.h"
#include "openavb_trace.h"

// forward declarations
static bool openavbAvdeccMsgSrvrReceiveFromClient(int avdeccMsgHandle, openavbAvdeccMessage_t *msg);

// OSAL specific functions
#include "openavb_avdecc_msg_server_osal.c"

// AvdeccMsgStateListGet() support.
#include "openavb_avdecc_msg.c"

// the following are from openavb_avdecc.c
extern openavb_avdecc_cfg_t gAvdeccCfg;
extern openavb_tl_data_cfg_t * streamList;


static bool openavbAvdeccMsgSrvrReceiveFromClient(int avdeccMsgHandle, openavbAvdeccMessage_t *msg)
{
	AVB_TRACE_ENTRY(AVB_TRACE_AVDECC_MSG);

	if (!msg) {
		AVB_LOG_ERROR("Receiving message; invalid argument passed");
		AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
		return FALSE;
	}

	bool ret = FALSE;
	switch (msg->type) {
		case OPENAVB_AVDECC_MSG_VERSION_REQUEST:
			AVB_LOG_DEBUG("Message received:  OPENAVB_AVDECC_MSG_VERSION_REQUEST");
			ret = openavbAvdeccMsgSrvrHndlVerRqstFromClient(avdeccMsgHandle);
			break;
		case OPENAVB_AVDECC_MSG_LISTENER_INIT_IDENTIFY:
			AVB_LOG_DEBUG("Message received:  OPENAVB_AVDECC_MSG_LISTENER_INIT_IDENTIFY");
			ret = openavbAvdeccMsgSrvrHndlListenerInitIdentifyFromClient(avdeccMsgHandle,
				msg->params.listenerInitIdentify.stream_src_mac, msg->params.listenerInitIdentify.stream_dest_mac,
				msg->params.listenerInitIdentify.stream_uid, msg->params.listenerInitIdentify.stream_vlan_id);
			break;
		case OPENAVB_AVDECC_MSG_LISTENER_CHANGE_NOTIFICATION:
			AVB_LOG_DEBUG("Message received:  OPENAVB_AVDECC_MSG_LISTENER_CHANGE_NOTIFICATION");
			ret = openavbAvdeccMsgSrvrHndlListenerChangeNotificationFromClient(avdeccMsgHandle, msg->params.listenerChangeNotification.current_state);
			break;
		default:
			AVB_LOG_ERROR("Unexpected message received at server");
			break;
	}

	AVB_LOGF_VERBOSE("Message handled, ret=%d", ret);
	AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
	return ret;
}

void openavbAvdeccMsgSrvrSendServerVersionToClient(int avdeccMsgHandle, U32 AVBVersion)
{
	AVB_TRACE_ENTRY(AVB_TRACE_AVDECC_MSG);

	openavbAvdeccMessage_t  msgBuf;
	memset(&msgBuf, 0, OPENAVB_AVDECC_MSG_LEN);
	msgBuf.type = OPENAVB_AVDECC_MSG_VERSION_CALLBACK;
	msgBuf.params.versionCallback.AVBVersion = AVBVersion;
	openavbAvdeccMsgSrvrSendToClient(avdeccMsgHandle, &msgBuf);

	AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
}

/* Client version request
 */
bool openavbAvdeccMsgSrvrHndlVerRqstFromClient(int avdeccMsgHandle)
{
	AVB_TRACE_ENTRY(AVB_TRACE_AVDECC_MSG);

	openavbAvdeccMsgSrvrSendServerVersionToClient(avdeccMsgHandle, AVB_CORE_VER_FULL);

	AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
	return TRUE;
}

bool openavbAvdeccMsgSrvrHndlListenerInitIdentifyFromClient(int avdeccMsgHandle, U8 stream_src_mac[6], U8 stream_dest_mac[6], U16 stream_uid, U16 stream_vlan_id)
{
	AVB_TRACE_ENTRY(AVB_TRACE_AVDECC_MSG);
	openavb_tl_data_cfg_t * currentStream;

	avdecc_msg_state_t *pState = AvdeccMsgStateListGet(avdeccMsgHandle);
	if (pState) {
		// The handle was already specified.  Something has gone terribly wrong!
		AVB_LOGF_ERROR("avdeccMsgHandle %d already used", avdeccMsgHandle);
		AvdeccMsgStateListRemove(pState);
		free(pState);
		AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
		return false;
	}

	// Create a structure to hold the client information.
	pState = (avdecc_msg_state_t *) calloc(1, sizeof(avdecc_msg_state_t));
	if (!pState) {
		AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
		return false;
	}
	pState->avdeccMsgHandle = avdeccMsgHandle;
	pState->bTalker = false;

	// Find the state information matching this item.
	for (currentStream = streamList; currentStream != NULL; currentStream = currentStream->next) {
		if (memcmp(currentStream->stream_addr.buffer.ether_addr_octet, stream_src_mac, sizeof(currentStream->stream_addr.buffer.ether_addr_octet)) == 0 &&
		    memcmp(currentStream->dest_addr.buffer.ether_addr_octet, stream_dest_mac, sizeof(currentStream->dest_addr.buffer.ether_addr_octet)) == 0 &&
		    currentStream->stream_uid == stream_uid &&
		    currentStream->vlan_id == stream_vlan_id)
		{
			break;
		}
	}
	if (!currentStream) {
		AVB_LOGF_WARNING("Ignoring unexpected client Listener %d:  "
			"src_addr %02x:%02x:%02x:%02x:%02x:%02x, "
			"stream %02x:%02x:%02x:%02x:%02x:%02x/%u, "
			"vlan_id %u",
			avdeccMsgHandle,
			stream_src_mac[0], stream_src_mac[1], stream_src_mac[2], stream_src_mac[3], stream_src_mac[4], stream_src_mac[5],
			stream_dest_mac[0], stream_dest_mac[1], stream_dest_mac[2], stream_dest_mac[3], stream_dest_mac[4], stream_dest_mac[5], stream_uid,
			stream_vlan_id);
		free(pState);
		AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
		return false;
	}

	// Keep track of this new state item.
	if (!AvdeccMsgStateListAdd(pState)) {
		AVB_LOGF_ERROR("Error saving client identity information %d", avdeccMsgHandle);
		free(pState);
		AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
		return false;
	}

	// Associate this Listener instance with the stream information.
	pState->stream = currentStream;
	currentStream->client = pState;

	AVB_LOGF_INFO("Client Listener %d Detected:  "
		"src_addr %02x:%02x:%02x:%02x:%02x:%02x, "
		"stream %02x:%02x:%02x:%02x:%02x:%02x/%u, "
		"vlan_id %u",
		avdeccMsgHandle,
		stream_src_mac[0], stream_src_mac[1], stream_src_mac[2], stream_src_mac[3], stream_src_mac[4], stream_src_mac[5],
		stream_dest_mac[0], stream_dest_mac[1], stream_dest_mac[2], stream_dest_mac[3], stream_dest_mac[4], stream_dest_mac[5], stream_uid,
		stream_vlan_id);

	AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
	return true;
}

bool openavbAvdeccMsgSrvrListenerChangeRequest(int avdeccMsgHandle, openavbAvdeccMsgStateType_t desiredState)
{
	AVB_TRACE_ENTRY(AVB_TRACE_AVDECC_MSG);
	openavbAvdeccMessage_t msgBuf;

	avdecc_msg_state_t *pState = AvdeccMsgStateListGet(avdeccMsgHandle);
	if (!pState) {
		AVB_LOGF_ERROR("avdeccMsgHandle %d not valid", avdeccMsgHandle);
		AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
		return false;
	}

	memset(&msgBuf, 0, OPENAVB_AVDECC_MSG_LEN);
	msgBuf.type = OPENAVB_AVDECC_MSG_LISTENER_CHANGE_REQUEST;
	openavbAvdeccMsgParams_ListenerChangeRequest_t * pParams =
		&(msgBuf.params.listenerChangeRequest);
	pParams->desired_state = desiredState;
	bool ret = openavbAvdeccMsgSrvrSendToClient(avdeccMsgHandle, &msgBuf);
	if (ret) {
		// Save the requested state for future reference.
		pState->lastRequestedState = desiredState;
	}

	AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
	return ret;
}

bool openavbAvdeccMsgSrvrHndlListenerChangeNotificationFromClient(int avdeccMsgHandle, openavbAvdeccMsgStateType_t currentState)
{
	AVB_TRACE_ENTRY(AVB_TRACE_AVDECC_MSG);

	avdecc_msg_state_t *pState = AvdeccMsgStateListGet(avdeccMsgHandle);
	if (!pState) {
		AVB_LOGF_ERROR("avdeccMsgHandle %d not valid", avdeccMsgHandle);
		AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
		return false;
	}

	// Save the updated state.
	if (currentState != pState->lastReportedState) {
		AVB_LOGF_INFO("client Listener %d state changed from %d to %d", avdeccMsgHandle, pState->lastReportedState, currentState);
		pState->lastReportedState = currentState;
	}

	AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
	return true;
}


/* Called if a client closes their end of the IPC
 */
void openavbAvdeccMsgSrvrCloseClientConnection(int avdeccMsgHandle)
{
	AVB_TRACE_ENTRY(AVB_TRACE_AVDECC_MSG);

	// Free the state for this handle.
	avdecc_msg_state_t *pState = AvdeccMsgStateListGet(avdeccMsgHandle);
	if (pState) {
		AvdeccMsgStateListRemove(pState);
		if (streamList && pState->stream) {
			// Clear the stream pointer to this object.
			pState->stream = NULL;
		}
		free(pState);
	}

	AVB_TRACE_EXIT(AVB_TRACE_AVDECC_MSG);
}