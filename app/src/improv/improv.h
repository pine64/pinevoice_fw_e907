// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#ifndef _IMPROV_H_
#define _IMPROV_H_

#include <stdint.h>

enum improv_state_e {
  IMPROV_STATE_STOPPED = 0x00,
  IMPROV_STATE_AWAITING_AUTHORIZATION = 0x01,
  IMPROV_STATE_AUTHORIZED = 0x02,
  IMPROV_STATE_PROVISIONING = 0x03,
  IMPROV_STATE_PROVISIONED = 0x04,
};

enum improv_error_state_e {
  IMPROV_NO_ERROR = 0x00,                    // This shows there is no current error state.
  IMPROV_ERROR_INVALID_RPC_PACKET = 0x01,    // RPC packet was malformed/invalid.
  IMPROV_ERROR_UNKNOWN_RPC_COMMAND = 0x02,   // The command sent is unknown.
  IMPROV_ERROR_UNABLE_TO_CONNECT = 0x03,     // The credentials have been received and an attempt to connect to the network has failed.
  IMPROV_ERROR_NOT_AUTHORIZED = 0x04,        // Credentials were sent via RPC but the Improv service is not authorized.
  IMPROV_ERROR_UNKNOWN_ERROR = 0xFF,
};

enum improv_command_e {
  IMPROV_CMD_UNKNOWN = 0x00,
  IMPROV_CMD_WIFI_SETTINGS = 0x01,
  IMPROV_CMD_IDENTIFY = 0x02,
  IMPROV_CMD_GET_CURRENT_STATE = 0x02,
  IMPROV_CMD_GET_DEVICE_INFO = 0x03,
  IMPROV_CMD_GET_WIFI_NETWORKS = 0x04,
  IMPROV_CMD_BAD_CHECKSUM = 0xFF,
};


int improv_start(uint32_t timeout_s);

#endif