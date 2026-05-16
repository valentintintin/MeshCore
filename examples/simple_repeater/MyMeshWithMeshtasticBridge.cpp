#include "MyMeshWithMeshtasticBridge.h"
#include "helpers/BaseChatMesh.h"
#include <base64.hpp>

MyMeshWithMeshtasticBridge::MyMeshWithMeshtasticBridge(mesh::MainBoard &board, mesh::Radio &radio,
                                                       mesh::MillisecondClock &ms, mesh::RNG &rng,
                                                       mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : MyMesh(board, radio, ms, rng, rtc, tables), _meshtastic_controller(new MeshtasticController(this)) {}

void MyMeshWithMeshtasticBridge::begin(FILESYSTEM *fs) {
  MyMesh::begin(fs);

  _fs = fs;

  add_meshcore_bridge_channel(0, "public", "");

  load_file_prefs();

  begin_bridge();
}

void MyMeshWithMeshtasticBridge::handleCommand(const uint32_t sender_timestamp, char *command, char *reply) {
  while (*command == ' ') {
    command++; // skip leading spaces
  }

  if (memcmp(command, "mt ", 3) == 0) {
    const char *sub_command = &command[3];
    if (memcmp(sub_command, "get ", 4) == 0) {
      handle_get_cmd(sender_timestamp, &sub_command[4], reply);
    } else if (memcmp(sub_command, "set ", 4) == 0) {
      handle_set_cmd(sender_timestamp, &sub_command[4], reply);
    } else if (memcmp(sub_command, "reload ", 6) == 0) {
      begin_bridge();
      strcpy(reply, "OK");
    } else if (memcmp(sub_command, "save ", 4) == 0) {
      if (save_file_prefs()) {
        begin_bridge();
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "Err - save failed");
      }
    } else if (memcmp(sub_command, "stats", 5) == 0) {
      const auto [mt_last_seen_node_num, mt_last_seen_long_name] = _meshtastic_controller->get_last_seen();
      snprintf(reply, 160,
               ">\n"
               "MT RX:%d (-%ds %s) TX->MC:%d %c\n"
               "MC RX:%d (-%ds %s) TX->MT:%d %c",
               _meshtastic_rx_count, (_ms->getMillis() - _last_meshtastic_rx_ms) / 1000,
               mt_last_seen_long_name, _meshcore_tx_count,
               has_recent_meshtastic_message() ? '+' : '-', _meshcore_rx_count,
               (_ms->getMillis() - _last_meshcore_rx_ms) / 1000,
               _last_meshcore_sender, _meshtastic_tx_count,
               has_recent_meshcore_message() ? '+' : '-');
    } else if (memcmp(sub_command, "test", 4) == 0) {
      MeshtasticBridgeMessageToSend message_to_send = {
        .message = "Test",
      };
      StrHelper::strncpy(message_to_send.sender_name, getNodeName(), MAX_SENDER_NAME_LEN);
      if (_meshtastic_controller->send_message(_ms->getMillis(), message_to_send)) {
        sprintf(reply, "> MT init: %d\nMT nodes: %d\nTest sent on MT primary channel",
                _meshtastic_controller->is_initialized(),
                _meshtastic_controller->nodes_count());
      } else {
        sprintf(reply, "> MT nodes: %d. Can not send on MT", _meshtastic_controller->nodes_count());
      }

      _meshtastic_controller->request_node_report();
    } else if (memcmp(sub_command, "reset", 4) == 0) {
      _meshtastic_controller->stop();
      _queue_message_to_send_to_meshcore.clear();
      _queue_message_to_send_to_meshtastic.clear();
      _meshtastic_bridge_prefs = {};
      save_file_prefs();
    } else {
      sprintf(reply, "??: mt %s", sub_command);
    }
  } else {
    MyMesh::handleCommand(sender_timestamp, command, reply);
  }
}

void MyMeshWithMeshtasticBridge::handle_get_cmd(uint32_t sender_timestamp, const char *config, char *reply) {
  if (memcmp(config, "enabled", 7) == 0) {
    sprintf(reply, "> %s", _meshtastic_bridge_prefs.enabled ? "on" : "off");
  } else if (memcmp(config, "tx_delay", 8) == 0) {
    sprintf(reply, "> %d ms", _meshtastic_bridge_prefs.tx_delay);
  } else if (memcmp(config, "rx_pin", 6) == 0) {
    sprintf(reply, "> %d", _meshtastic_bridge_prefs.rx_pin);
  } else if (memcmp(config, "tx_pin", 6) == 0) {
    sprintf(reply, "> %d", _meshtastic_bridge_prefs.tx_pin);
  } else if (memcmp(config, "baud_rate", 9) == 0) {
    sprintf(reply, "> %d bds", _meshtastic_bridge_prefs.baud_rate);
  } else if (memcmp(config, "mc_rx_timeout", 13) == 0) {
    sprintf(reply, "> %d s", _meshtastic_bridge_prefs.meshcore_rx_timeout_ms / 1000);
  } else if (memcmp(config, "mt_rx_timeout", 13) == 0) {
    sprintf(reply, "> %d s", _meshtastic_bridge_prefs.meshtastic_rx_timeout_ms / 1000);
  } else if (memcmp(config, "channels", 8) == 0 && sender_timestamp == 0) { // Only via Serial
    sprintf(reply, ">");
    uint8_t channel_index = 0;
    for (const auto [channel_details, region] : _meshtastic_bridge_prefs.bridge_channels) {
      if (channel_details.name[0] != '\0') {
        snprintf(reply, 160, "%s\n%d:%.15s[%.10s]", reply, channel_index, channel_details.name, region);
      } else {
        snprintf(reply, 160, "%s\n%d:disabled", reply, channel_index);
      }
      channel_index++;
    }
  } else if (memcmp(config, "channel ", 8) == 0) {
    const int channel_index = atoi(&config[8]);
    if (channel_index >= 0 && channel_index < MESHTASTIC_MAX_CHANNELS) {
      const auto [channel_details, region] = _meshtastic_bridge_prefs.bridge_channels[channel_index];
      if (channel_details.name[0] != '\0') {
        sprintf(reply, "> %d: %s [%s]", channel_index, channel_details.name, region);
      } else {
        sprintf(reply, "> %d: disabled", channel_index);
      }
    } else {
      sprintf(reply, "ERROR: [0; %d]", MESHTASTIC_MAX_CHANNELS - 1);
    }
  } else if (memcmp(config, "node ", 5) == 0) {
    const int node_index = atoi(&config[5]);
    if (node_index >= 0 && node_index < _meshtastic_controller->nodes_count()) {
      const auto [node_num, long_name] = _meshtastic_controller->get_node(node_index);
      sprintf(reply, "> MT node %d/%d = !%x : %s", node_index, _meshtastic_controller->nodes_count(),
              node_num, long_name);
    } else {
      sprintf(reply, "ERROR: [0; %d]", _meshtastic_controller->nodes_count());
    }
  } else {
    sprintf(reply, "??: mt get %s", config);
  }
}

void MyMeshWithMeshtasticBridge::handle_set_cmd(uint32_t sender_timestamp, const char *config, char *reply) {
  if (memcmp(config, "enabled ", 8) == 0) {
    _meshtastic_bridge_prefs.enabled = strcasecmp(&config[8], "on") == 0;
    sprintf(reply, "OK - %s", _meshtastic_bridge_prefs.enabled ? "on" : "off");
  } else if (memcmp(config, "tx_delay ", 9) == 0) {
    _meshtastic_bridge_prefs.tx_delay = (uint16_t)atoi(&config[9]);
    sprintf(reply, "OK - %d ms", _meshtastic_bridge_prefs.tx_delay);
  } else if (memcmp(config, "rx_pin ", 7) == 0) {
    _meshtastic_bridge_prefs.rx_pin = atoi(&config[7]);
    sprintf(reply, "OK - %d", _meshtastic_bridge_prefs.rx_pin);
  } else if (memcmp(config, "tx_pin ", 7) == 0) {
    _meshtastic_bridge_prefs.tx_pin = atoi(&config[7]);
    sprintf(reply, "OK - %d", _meshtastic_bridge_prefs.tx_pin);
  } else if (memcmp(config, "baud_rate ", 10) == 0) {
    _meshtastic_bridge_prefs.baud_rate = (uint32_t)atoi(&config[10]);
    sprintf(reply, "OK - %d bds", _meshtastic_bridge_prefs.baud_rate);
  } else if (memcmp(config, "mc_rx_timeout ", 14) == 0) {
    _meshtastic_bridge_prefs.meshcore_rx_timeout_ms = (uint32_t)atoi(&config[14]) * 1000;
    sprintf(reply, "> %d s", _meshtastic_bridge_prefs.meshcore_rx_timeout_ms / 1000);
  } else if (memcmp(config, "mt_rx_timeout ", 14) == 0) {
    _meshtastic_bridge_prefs.meshtastic_rx_timeout_ms = (uint32_t)atoi(&config[14]) * 1000;
    sprintf(reply, "> %d s", _meshtastic_bridge_prefs.meshtastic_rx_timeout_ms / 1000);
  } else if (memcmp(config, "channel ", 8) == 0) {
    int channel_index;
    char new_channel_name[32] = {};
    char new_region[31] = {};

    // Parser : "channel XX #name region"
    const int parsed = sscanf(&config[8], "%d %32s %31s", &channel_index, new_channel_name, new_region);

    if (parsed >= 2 && channel_index >= 0 && channel_index < MESHTASTIC_MAX_CHANNELS) {
      if (new_channel_name[0] != '-' && new_channel_name[0] != '\0') {
        const auto channel = add_meshcore_bridge_channel(channel_index, new_channel_name, new_region);
        if (channel != nullptr) {
          sprintf(reply, "OK - %d: %s [%s]", channel_index, channel->channel_details.name, channel->region);
        } else {
          sprintf(reply, "ERROR: %d", channel_index);
        }
      } else {
        memset(_meshtastic_bridge_prefs.bridge_channels[channel_index].region, 0,
               sizeof(_meshtastic_bridge_prefs.bridge_channels[channel_index].region));
        memset(
            _meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.channel.secret, 0,
            sizeof(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.channel.secret));
        memset(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.channel.hash, 0,
               sizeof(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.channel.hash));
        memset(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.name, 0,
               sizeof(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.name));
        sprintf(reply, "OK - %d: disabled", channel_index);
      }
    } else {
      sprintf(reply, "ERROR: [0; %d]", MESHTASTIC_MAX_CHANNELS - 1);
    }
  } else {
    sprintf(reply, "??: mt set %s", config);
  }
}

void MyMeshWithMeshtasticBridge::beginBridge() {
  begin_bridge();
}

void MyMeshWithMeshtasticBridge::begin_bridge() {
  if (_meshtastic_bridge_prefs.enabled) {
    uint8_t channel_index = 0;
    for (const auto [channel_details, region] : _meshtastic_bridge_prefs.bridge_channels) {
      if (channel_details.name[0] != '\0') {
        MESH_DEBUG_PRINTLN("[MT Bridge] Channel %d -> %s [%s]", channel_index, channel_details.name, region);
      } else {
        MESH_DEBUG_PRINTLN("[MT Bridge] Channel %d disabled", channel_index);
      }
      channel_index++;
    }

    _meshtastic_controller->begin(_meshtastic_bridge_prefs.rx_pin, _meshtastic_bridge_prefs.tx_pin,
                                  _meshtastic_bridge_prefs.baud_rate);
  } else {
    MESH_DEBUG_PRINTLN("[MT Bridge] Bridge disabled");
  }
}

bool MyMeshWithMeshtasticBridge::load_file_prefs() {
  if (_fs->exists(PREFS_FILENAME)) {
#if defined(RP2040_PLATFORM)
    File file = _fs->open(PREFS_FILENAME, "r");
#else
    File file = _fs->open(PREFS_FILENAME);
#endif
    if (file) {
      memset(&_meshtastic_bridge_prefs, 0, sizeof(_meshtastic_bridge_prefs));

      const auto success = file.read((uint8_t *)&_meshtastic_bridge_prefs,
                                     sizeof(_meshtastic_bridge_prefs)) == sizeof(_meshtastic_bridge_prefs);
      file.close();

      if (success) {
        MESH_DEBUG_PRINTLN("[MT Bridge] Preferences loaded from file");
      } else {
        MESH_DEBUG_PRINTLN("[MT Bridge] Failed to read preferences from file");
      }

      return success;
    }
  }

  MESH_DEBUG_PRINTLN("[MT Bridge] Preferences file not found");

  return false;
}

bool MyMeshWithMeshtasticBridge::save_file_prefs() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(PREFS_FILENAME);
  File file = _fs->open(PREFS_FILENAME, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(PREFS_FILENAME, "w");
#else
  File file = _fs->open(PREFS_FILENAME, "w", true);
#endif
  if (file) {
    const auto success = file.write((uint8_t *)&_meshtastic_bridge_prefs, sizeof(_meshtastic_bridge_prefs)) ==
                         sizeof(_meshtastic_bridge_prefs);
    file.close();

    if (success) {
      MESH_DEBUG_PRINTLN("[MT Bridge] Preferences saved to file");
    } else {
      MESH_DEBUG_PRINTLN("[MT Bridge] Failed to write preferences to file");
    }

    return success;
  }

  MESH_DEBUG_PRINTLN("[MT Bridge] Failed to open preferences file for writing");

  return false;
}

MeshtasticBridgeChannel *MyMeshWithMeshtasticBridge::add_meshcore_bridge_channel(const uint8_t index,
                                                                                 const char *name,
                                                                                 const char *region) {
  if (index >= MESHTASTIC_MAX_CHANNELS) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Invalid channel index %d (max: %d)", index, MESHTASTIC_MAX_CHANNELS - 1);
    return nullptr;
  }

  MESH_DEBUG_PRINTLN("[MT Bridge] Configure channel '%s' at index %d", name, index);

  if (strlen(name) > 32) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Invalid channel name '%s': length %d > 32", name, strlen(name));
    return nullptr;
  }

  if (strlen(region) > 31) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Invalid region '%s' for channel '%s': length %d > 31", region, name,
                       strlen(region));
    return nullptr;
  }

  const auto bridge_channel = &_meshtastic_bridge_prefs.bridge_channels[index];
  memset(bridge_channel->channel_details.channel.secret, 0,
         sizeof(bridge_channel->channel_details.channel.secret));
  memset(bridge_channel->channel_details.channel.hash, 0,
         sizeof(bridge_channel->channel_details.channel.hash));
  memset(bridge_channel->channel_details.name, 0, sizeof(bridge_channel->channel_details.name));
  memset(&bridge_channel->region, 0, sizeof(bridge_channel->region));

  int len = 0;

  if (strcasecmp(name, "Public") == 0) {
    const auto psk_base64 = PUBLIC_GROUP_PSK;
    len = decode_base64((unsigned char *)psk_base64, strlen(psk_base64),
                        bridge_channel->channel_details.channel.secret);
    if (len == 32 || len == 16) {
      mesh::Utils::sha256(bridge_channel->channel_details.channel.hash,
                          sizeof(bridge_channel->channel_details.channel.hash),
                          bridge_channel->channel_details.channel.secret, len);
    } else {
      MESH_DEBUG_PRINTLN("[MT Bridge] Invalid decoded PSK length for public channel: %d", len);

      return nullptr;
    }
  } else {
    len = 16;
    mesh::Utils::sha256(bridge_channel->channel_details.channel.secret, len, (const uint8_t *)name,
                        strlen(name));
  }

  mesh::Utils::sha256(bridge_channel->channel_details.channel.hash,
                      sizeof(bridge_channel->channel_details.channel.hash),
                      bridge_channel->channel_details.channel.secret, len);

  StrHelper::strncpy(bridge_channel->channel_details.name, name,
                     sizeof(bridge_channel->channel_details.name));

  if (!strlen(region)) {
    StrHelper::strncpy(bridge_channel->region, "*", sizeof(bridge_channel->region));
  } else {
    StrHelper::strncpy(bridge_channel->region, region, sizeof(bridge_channel->region));
  }

  MESH_DEBUG_PRINTLN("[MT Bridge] Channel '%s' [%s] configured (secret[0]=0x%x hash[0]=0x%x) at index %d",
                     name, bridge_channel->region, bridge_channel->channel_details.channel.secret[0],
                     bridge_channel->channel_details.channel.hash[0], index);

  return bridge_channel;
}

bool MyMeshWithMeshtasticBridge::send_message_to_meshcore_from_meshtastic(const char *sender_name,
                                                                          const char *text,
                                                                          uint8_t meshtastic_channel_index) {
  if (meshtastic_channel_index >= MESHTASTIC_MAX_CHANNELS) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Drop MT->MC message: invalid channel index %d", meshtastic_channel_index);
    return false;
  }

  if (strlen(_meshtastic_bridge_prefs.bridge_channels[meshtastic_channel_index].channel_details.name) == 0) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Drop MT->MC message: channel index %d is not configured",
                       meshtastic_channel_index);
    return false;
  }

  _last_meshtastic_rx_ms = _ms->getMillis();
  _meshtastic_rx_count++;

  if (!has_recent_meshcore_message()) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Drop MT->MC message '%s': no MeshCore RX for %d s (limit: %d s)", text,
                       (_ms->getMillis() - _last_meshcore_rx_ms) / 1000,
                       _meshtastic_bridge_prefs.meshcore_rx_timeout_ms / 1000);
    return false;
  }

  if (_queue_message_to_send_to_meshcore.is_full()) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Drop MT->MC message from '%s': MeshCore queue is full", sender_name);
    return false;
  }

  MeshtasticBridgeMessageToSend message_to_send = { .meshtastic_channel_index = meshtastic_channel_index,
                                                    .next_time_to_send = _ms->getMillis() +
                                                                         _meshtastic_bridge_prefs.tx_delay };
  StrHelper::strncpy(message_to_send.sender_name, sender_name, sizeof(message_to_send.sender_name));
  StrHelper::strncpy(message_to_send.message, text, sizeof(message_to_send.message));

  const auto success = _queue_message_to_send_to_meshcore.enqueue(message_to_send);

  _meshcore_tx_count++;

  if (success) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Queue MT->MC message from '%s' (next TX: %ld, now: %ld)", sender_name,
                       message_to_send.next_time_to_send, _ms->getMillis());
  } else {
    MESH_DEBUG_PRINTLN("[MT Bridge] Failed to queue MT->MC message from '%s': queue is full", sender_name);
  }

  return success;
}
void MyMeshWithMeshtasticBridge::clearStats() {
  MyMesh::clearStats();

  _meshcore_rx_count = 0;
  _meshtastic_rx_count = 0;
  _meshcore_tx_count = 0;
  _meshtastic_tx_count = 0;
}

void MyMeshWithMeshtasticBridge::loop() {
  MyMesh::loop();

  _meshtastic_controller->loop(_ms->getMillis());
  send_one_message_from_queue();
}

int MyMeshWithMeshtasticBridge::searchChannelsByHash(const uint8_t *hash, mesh::GroupChannel dest[],
                                                     int max_matches) {
  int n = 0;

  for (auto bridge_channel : _meshtastic_bridge_prefs.bridge_channels) {
    if (bridge_channel.channel_details.channel.hash[0] == hash[0] && n < max_matches) {
      MESH_DEBUG_PRINTLN("[MT Bridge] Matched MeshCore channel '%s'", bridge_channel.channel_details.name);
      dest[n++] = bridge_channel.channel_details.channel;
    }
  }

  if (!n) {
    MESH_DEBUG_PRINTLN("[MT Bridge] No bridge mapping for MeshCore channel hash[0]=0x%x", hash[0]);
  }

  return n;
}

void MyMeshWithMeshtasticBridge::onGroupDataRecv(mesh::Packet *packet, uint8_t type,
                                                 const mesh::GroupChannel &channel, uint8_t *data,
                                                 size_t len) {
  if (!_meshtastic_bridge_prefs.enabled) {
    return;
  }

  const uint8_t txt_type = data[4];
  if (type == PAYLOAD_TYPE_GRP_TXT && len > 5 && (txt_type >> 2) == 0) { // 0 = plain text msg
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    // len can be > original length, but 'text' will be padded with zeroes
    data[len] = 0; // need to make a C string again, with null terminator
    const auto text = (const char *)&data[5];

    MESH_DEBUG_PRINTLN("[MT Bridge] Received MC->MT message '%s' on channel hash[0]=0x%x", text,
                       channel.hash[0]);

    uint8_t channel_index = 0;

    for (auto [channel_details, name] : _meshtastic_bridge_prefs.bridge_channels) {
      if (channel_details.channel.hash[0] == channel.hash[0] && name[0] != '\0') {
        break;
      }

      channel_index++;
    }

    if (channel_index >= MESHTASTIC_MAX_CHANNELS) {
      MESH_DEBUG_PRINTLN("[MT Bridge] Drop MC->MT message: no bridge mapping for channel hash[0]=0x%X",
                         channel.hash[0]);
    } else {
      _meshcore_rx_count++;
      _last_meshcore_rx_ms = _ms->getMillis();

      MeshtasticBridgeMessageToSend message_to_send = {
        .meshtastic_channel_index = channel_index,
        .next_time_to_send = _ms->getMillis() + _meshtastic_bridge_prefs.tx_delay
      };
      sscanf(text, "%31[^:]: %199s", message_to_send.sender_name, message_to_send.message);

      StrHelper::strncpy(_last_meshcore_sender, message_to_send.sender_name, sizeof(_last_meshcore_sender));

      if (!has_recent_meshtastic_message()) {
        MESH_DEBUG_PRINTLN("[MT Bridge] Drop MC->MT message '%s': no Meshtastic RX for %d s (limit: %d s)",
                           text, (_ms->getMillis() - _last_meshtastic_rx_ms) / 1000,
                           _meshtastic_bridge_prefs.meshtastic_rx_timeout_ms / 1000);
        return;
      }

      _meshtastic_tx_count++;

      const auto success = _queue_message_to_send_to_meshtastic.enqueue(message_to_send);

      if (success) {
        MESH_DEBUG_PRINTLN("[MT Bridge] Queue MC->MT message from '%s' (next TX: %ld, now: %ld)",
                           message_to_send.sender_name, message_to_send.next_time_to_send, _ms->getMillis());
      } else {
        MESH_DEBUG_PRINTLN("[MT Bridge] Failed to queue MC->MT message from '%s': queue is full",
                           message_to_send.sender_name);
      }
    }
  }
}

bool MyMeshWithMeshtasticBridge::send_message(MeshtasticBridgeMessageToSend message_to_send) {
  if (message_to_send.meshtastic_channel_index >= MESHTASTIC_MAX_CHANNELS) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Cannot send MT->MC: invalid channel index %d",
                       message_to_send.meshtastic_channel_index);
    return false;
  }

  const auto bridge_channel =
      _meshtastic_bridge_prefs.bridge_channels[message_to_send.meshtastic_channel_index];

  if (strlen(bridge_channel.channel_details.name) == 0) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Cannot send MT->MC: channel index %d not found",
                       message_to_send.meshtastic_channel_index);
    return false;
  }

  TransportKey scope{};
  derive_scope_from_region_name(bridge_channel.region, scope);

  auto text_len = strlen(message_to_send.message);

  // Reprise du code de BaseChatMesh::sendGroupMessage
  const uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();

#define MAX_TEXT_LEN                                                                           \
  (10 * CIPHER_BLOCK_SIZE) // must be LESS than (MAX_PACKET_PAYLOAD - 4 - CIPHER_MAC_SIZE - 1)

  uint8_t temp[5 + MAX_TEXT_LEN + 32];
  memcpy(temp, &timestamp, 4); // mostly an extra blob to help make packet_hash unique
  temp[4] = 0;                 // TXT_TYPE_PLAIN

  sprintf((char *)&temp[5], "MT_%s: ", message_to_send.sender_name); // <sender>: <msg>
  char *ep = strchr((char *)&temp[5], 0);
  const int prefix_len = ep - (char *)&temp[5];

  if (text_len + prefix_len > MAX_TEXT_LEN) text_len = MAX_TEXT_LEN - prefix_len;
  memcpy(ep, message_to_send.message, text_len);
  ep[text_len] = 0; // null terminator

  const auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, bridge_channel.channel_details.channel, temp,
                                       5 + prefix_len + text_len);

  MESH_DEBUG_PRINTLN("[MT Bridge] Send MT->MC on channel '%s': %s", bridge_channel.channel_details.name,
                     temp + 5);

  if (pkt) {
    sendFloodScoped(scope, pkt, 0, getNodePrefs()->path_hash_mode + 1);
    return true;
  }

  return false;
}

bool MyMeshWithMeshtasticBridge::send_one_message_from_queue() {
  if (!_queue_message_to_send_to_meshcore.is_empty()) {
    const auto meshcore_message = _queue_message_to_send_to_meshcore.get_next();
    if (millisHasNowPassed(meshcore_message->next_time_to_send)) {
      if (send_message(*meshcore_message)) {
        MeshtasticBridgeMessageToSend message{};
        _queue_message_to_send_to_meshcore.dequeue(message);
        return true;
      }
    }
  }

  if (!_queue_message_to_send_to_meshtastic.is_empty()) {
    const auto meshtastic_message = _queue_message_to_send_to_meshtastic.get_next();
    if (millisHasNowPassed(meshtastic_message->next_time_to_send)) {
      if (_meshtastic_controller->send_message(_ms->getMillis(), *meshtastic_message)) {
        MeshtasticBridgeMessageToSend message{};
        _queue_message_to_send_to_meshtastic.dequeue(message);
        return true;
      }
    }
  }

  return false;
}

bool MyMeshWithMeshtasticBridge::derive_scope_from_region_name(const char *region_name, TransportKey &scope) {
  memset(scope.key, 0, sizeof(scope.key));
  if (strcmp(region_name, "*") == 0) {
    return false; // use fallback
  }
  if (region_name[0] == '$') {
    return false; // private region unsupported without keystore
  }
  char region_tag[32] = {};
  if (region_name[0] == '#') {
    StrHelper::strncpy(region_tag, region_name, sizeof(region_tag));
  } else {
    region_tag[0] = '#';
    StrHelper::strncpy(&region_tag[1], region_name, sizeof(region_tag) - 1);
  }
  mesh::Utils::sha256(scope.key, sizeof(scope.key), (const uint8_t *)region_tag, strlen(region_tag));
  return true;
}

bool MyMeshWithMeshtasticBridge::has_recent_meshtastic_message() const {
  if (_meshtastic_bridge_prefs.meshtastic_rx_timeout_ms == 0) {
    return true;
  }

  return !millisHasNowPassed(_last_meshtastic_rx_ms + _meshtastic_bridge_prefs.meshtastic_rx_timeout_ms);
}

bool MyMeshWithMeshtasticBridge::has_recent_meshcore_message() const {
  if (_meshtastic_bridge_prefs.meshcore_rx_timeout_ms == 0) {
    return true;
  }

  return !millisHasNowPassed(_last_meshcore_rx_ms + _meshtastic_bridge_prefs.meshcore_rx_timeout_ms);
}