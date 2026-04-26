#pragma once

#include "helpers/ChannelDetails.h"
#include <cstdint>

#define MESHTASTIC_MAX_CHANNELS 5

#define MESHTASTIC_MAX_MESSAGE_LENGTH 200
#define MAX_SENDER_NAME_LEN 32

struct MeshtasticBridgePrefs {
  bool enabled = false;
  uint8_t rx_pin = -1;
  uint8_t tx_pin = -1;
  uint32_t baud_rate = 115200;
  uint16_t tx_delay = 0;
  uint32_t interval_stop_relay_mc = 0;
  uint32_t interval_stop_relay_mt = 0;
  ChannelDetails bridge_channels[MESHTASTIC_MAX_CHANNELS] = {};
};

struct MeshtasticBridgeMessageToSend {
  char sender_name[MAX_SENDER_NAME_LEN]; // Max MC
  char message[MESHTASTIC_MAX_MESSAGE_LENGTH]; // Max for MT
  uint8_t meshtastic_channel_index;
  uint32_t next_time_to_send;
};

