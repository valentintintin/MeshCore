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

  loadFilePrefs();

  beginBridge();
}

void MyMeshWithMeshtasticBridge::handleCommand(uint32_t sender_timestamp, char *command, char *reply) {
  while (*command == ' ') {
    command++; // skip leading spaces
  }

  /*
 mt get enabled
 mt set enabled on|off
 mt get channels // Get
 mt get channel x // Get
 mt set channel 0 Public // Fr_Balise
 mt set channel 1 #frblabla // Fr_Blabla
 mt set channel 2 Public // LongFast
 mt set channel 3 Public // LongMod
 mt set channel 4 - // Disable
 mt get tx_delay
 mt set tx_delay delay (ms)
 mt get rx_pin
 mt set rx_pin pin
 mt get tx_pin
 mt set tx_pin pin
 mt get baud_rate
 mt set baud_rate baud
 mt get int.stop_relay_mc
 mt set int.stop_relay_mc delay (s)
 mt get int.stop_relay_mt
 mt set int.stop_relay_mt delay (s)
 */

  if (memcmp(command, "mt ", 3) == 0) {
    const char *sub_command = &command[3];
    if (memcmp(sub_command, "get ", 4) == 0) {
      handleGetCmd(sender_timestamp, &sub_command[4], reply);
    } else if (memcmp(sub_command, "set ", 4) == 0) {
      handleSetCmd(sender_timestamp, &sub_command[4], reply);
    } else if (memcmp(sub_command, "reload ", 6) == 0) {
      beginBridge();
      strcpy(reply, "OK");
    } else if (memcmp(sub_command, "save ", 4) == 0) {
      if (saveFilePrefs()) {
        beginBridge();
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "Err - save failed");
      }
    } else if (memcmp(sub_command, "stats", 5) == 0) {
      const auto mt_last_seen = _meshtastic_controller->get_last_seen();
      sprintf(reply, ">\nMT RX: %d [-%d s, !%x %s], MC->MT TX: %d\nMC RX: %d, MT->MC TX: %d [-%d s]",
        _n_received_meshtastic, (_ms->getMillis() - _last_meshtastic_message_time) / 1000,
        mt_last_seen != nullptr ? mt_last_seen->node_num : 0, mt_last_seen != nullptr ? mt_last_seen->long_name : "", _n_sent_meshtastic,
        _n_received_meshcore, _n_sent_meshcore, (_ms->getMillis() - _last_meshcore_message_time) / 1000);
    } else if (memcmp(sub_command, "test", 4) == 0) {
      MeshtasticBridgeMessageToSend message_to_send = {
        .message = "Test",
      };
      StrHelper::strncpy(message_to_send.sender_name, getNodeName(), MAX_SENDER_NAME_LEN);
      if (_meshtastic_controller->send_message(_ms->getMillis(), message_to_send)) {
        sprintf(reply, "> MT nodes: %d\nTest sent on MT primary channel", _meshtastic_controller->nodes_count());
      } else {
        sprintf(reply, "> MT nodes: %d. Can not send on MT", _meshtastic_controller->nodes_count());
      }

      _meshtastic_controller->request_node_report();
    } else if (memcmp(sub_command, "reset", 4) == 0) {
      _meshtastic_controller->stop();
      _queue_message_to_send_to_meshcore.clear();
      _queue_message_to_send_to_meshtastic.clear();
      _meshtastic_bridge_prefs = {};
      saveFilePrefs();
    } else {
      sprintf(reply, "??: mt %s", sub_command);
    }
  } else {
    MyMesh::handleCommand(sender_timestamp, command, reply);
  }
}

void MyMeshWithMeshtasticBridge::handleGetCmd(uint32_t sender_timestamp, const char *config, char *reply) {
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
  } else if (memcmp(config, "stop_relay_mc", 13) == 0) {
    sprintf(reply, "> %d s", _meshtastic_bridge_prefs.interval_stop_relay_mc);
  } else if (memcmp(config, "stop_relay_mt", 13) == 0) {
    sprintf(reply, "> %d s", _meshtastic_bridge_prefs.interval_stop_relay_mt);
  } else if (memcmp(config, "channels", 8) == 0) {
    sprintf(reply, ">");
    uint8_t channel_index = 0;
    for (const auto [channel_details, region] : _meshtastic_bridge_prefs.bridge_channels) {
      if (channel_details.name[0] != '\0') {
        sprintf(reply, "%s\n%d -> %s [%s]", reply, channel_index, channel_details.name, region);
      } else {
        sprintf(reply, "%s\n%d -> disabled", reply, channel_index);
      }
      channel_index++;
    }
  } else if (memcmp(config, "channel ", 8) == 0) {
    const auto channel_index = atoi(&config[8]);
    if (channel_index < sizeof(_meshtastic_bridge_prefs.bridge_channels)) {
      const auto [channel_details, region] = _meshtastic_bridge_prefs.bridge_channels[channel_index];
      sprintf(reply, "> MT channel %d : ", channel_index);
      if (channel_details.name[0] != '\0') {
        sprintf(reply, "\t %d -> %s [%s]", channel_index, channel_details.name, region);
      } else {
        sprintf(reply, "\t %d -> disabled", channel_index);
      }
    } else {
      sprintf(reply, "ERROR: [0; %d]", sizeof(_meshtastic_bridge_prefs.bridge_channels));
    }
  } else if (memcmp(config, "node ", 5) == 0) {
    const auto node_index = atoi(&config[5]);
    if (node_index < _meshtastic_controller->nodes_count()) {
      const auto [node_num, long_name] = _meshtastic_controller->get_node(node_index);
      sprintf(reply, "> MT node %d/%d = !%x : %s", node_index, _meshtastic_controller->nodes_count(), node_num, long_name);
    } else {
      sprintf(reply, "ERROR: [0; %d]", _meshtastic_controller->nodes_count());
    }
  } else {
    sprintf(reply, "??: mt get %s", config);
  }
}

void MyMeshWithMeshtasticBridge::handleSetCmd(uint32_t sender_timestamp, const char *config, char *reply) {
  if (memcmp(config, "enabled ", 8) == 0) {
    _meshtastic_bridge_prefs.enabled = strcasecmp(&config[8], "on") == 0;
    sprintf(reply, "OK - %s", _meshtastic_bridge_prefs.enabled ? "on" : "off");
  } else if (memcmp(config, "tx_delay ", 9) == 0) {
    _meshtastic_bridge_prefs.tx_delay = (uint16_t) atoi(&config[9]);
    sprintf(reply, "OK - %d ms", _meshtastic_bridge_prefs.tx_delay);
  } else if (memcmp(config, "rx_pin ", 7) == 0) {
    _meshtastic_bridge_prefs.rx_pin = atoi(&config[7]);
    sprintf(reply, "OK - %d", _meshtastic_bridge_prefs.rx_pin);
  } else if (memcmp(config, "tx_pin ", 7) == 0) {
    _meshtastic_bridge_prefs.tx_pin = atoi(&config[7]);
    sprintf(reply, "OK - %d", _meshtastic_bridge_prefs.tx_pin);
  } else if (memcmp(config, "baud_rate ", 10) == 0) {
    _meshtastic_bridge_prefs.baud_rate = (uint32_t) atoi(&config[10]);
    sprintf(reply, "OK - %d bds", _meshtastic_bridge_prefs.baud_rate);
  } else if (memcmp(config, "stop_relay_mc ", 14) == 0) {
    _meshtastic_bridge_prefs.interval_stop_relay_mc = (uint32_t) atoi(&config[14]) * 1000;
    sprintf(reply, "> %d s", _meshtastic_bridge_prefs.interval_stop_relay_mc / 1000);
  } else if (memcmp(config, "stop_relay_mt ", 14) == 0) {
    _meshtastic_bridge_prefs.interval_stop_relay_mt = (uint32_t) atoi(&config[14]) * 1000;
    sprintf(reply, "> %d s", _meshtastic_bridge_prefs.interval_stop_relay_mt / 1000);
  } else if (memcmp(config, "channel ", 8) == 0) {
    int channel_index;
    char new_channel_name[32] = {};
    char new_region[31] = {};

    // Parser : "channel XX #name region"
    const int parsed = sscanf(&config[8], "%d %32s %31s", &channel_index, new_channel_name, new_region);

    if (parsed >= 2 && channel_index < sizeof(_meshtastic_bridge_prefs.bridge_channels)) {
      if (new_channel_name[0] != '-' && new_channel_name[0] != '\0') {
        const auto channel = add_meshcore_bridge_channel(channel_index, new_channel_name, new_region);
        if (channel != nullptr) {
          sprintf(reply, "OK - MT channel %d : %s [%s]", channel_index, channel->channel_details.name, channel->region);
        } else {
          sprintf(reply, "ERROR: MT channel %d", channel_index);
        }
      } else {
        memset(_meshtastic_bridge_prefs.bridge_channels[channel_index].region, 0, sizeof(_meshtastic_bridge_prefs.bridge_channels[channel_index].region));
        memset(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.channel.secret, 0, sizeof(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.channel.secret));
        memset(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.channel.hash, 0, sizeof(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.channel.hash));
        memset(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.name, 0, sizeof(_meshtastic_bridge_prefs.bridge_channels[channel_index].channel_details.name));
        sprintf(reply, "OK - MT channel %d : disabled", channel_index);
      }
    } else {
      sprintf(reply, "ERROR: [0; %d]", sizeof(_meshtastic_bridge_prefs.bridge_channels));
    }
  } else {
    sprintf(reply, "??: mt set %s", config);
  }
}

void MyMeshWithMeshtasticBridge::beginBridge() {
  if (_meshtastic_bridge_prefs.enabled) {
    uint8_t channel_index = 0;
    for (const auto [channel_details, region] : _meshtastic_bridge_prefs.bridge_channels) {
      if (channel_details.name[0] != '\0') {
        MESH_DEBUG_PRINTLN("Bridge MT channel %d -> %s [%s]", channel_index, channel_details.name, region);
      } else {
        MESH_DEBUG_PRINTLN("Bridge MT channel %d -> disabled", channel_index);
      }
      channel_index++;
    }

    _meshtastic_controller->begin(_meshtastic_bridge_prefs.rx_pin, _meshtastic_bridge_prefs.tx_pin, _meshtastic_bridge_prefs.baud_rate);
  } else {
    MESH_DEBUG_PRINTLN("Bridge MT disabled");
  }
}

bool MyMeshWithMeshtasticBridge::loadFilePrefs() {
  if (_fs->exists(PREFS_FILENAME)) {
#if defined(RP2040_PLATFORM)
    File file = _fs->open(PREFS_FILENAME, "r");
#else
    File file = _fs->open(PREFS_FILENAME);
#endif
    if (file) {
      memset(_meshtastic_controller, 0, sizeof(_meshtastic_bridge_prefs));

      const auto success = file.read((uint8_t *)&_meshtastic_bridge_prefs,
                                     sizeof(_meshtastic_bridge_prefs)) == sizeof(_meshtastic_bridge_prefs);
      file.close();

      if (success) {
        MESH_DEBUG_PRINTLN("Bridge MT. Load prefs from file OK");
      } else {
        MESH_DEBUG_PRINTLN("Bridge MT. Load prefs from file KO");
      }

      return success;
    }
  }

  MESH_DEBUG_PRINTLN("Bridge MT. Load prefs from file KO. No file");

  return false;
}

bool MyMeshWithMeshtasticBridge::saveFilePrefs() {
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
      MESH_DEBUG_PRINTLN("Bridge MT. Save prefs to file OK");
    } else {
      MESH_DEBUG_PRINTLN("Bridge MT. Save prefs to file KO");
    }

    return success;
  }

  MESH_DEBUG_PRINTLN("Bridge MT. Save prefs to file KO. No file");

  return false;
}

MeshtasticBridgeChannel* MyMeshWithMeshtasticBridge::add_meshcore_bridge_channel(const uint8_t index, const char *name, const char *region) {
  MESH_DEBUG_PRINTLN("Bridge MT. Add channel %s at position %d", name, index);

  if (strlen(name) > 32) {
    MESH_DEBUG_PRINTLN("Bridge MT. Add channel %s KO. Len: %d > 32", name, strlen(name));
    return nullptr;
  }

  if (strlen(region) > 31) {
    MESH_DEBUG_PRINTLN("Bridge MT. Add channel %s KO. Len: (%s) %d > 31", name, region, strlen(region));
    return nullptr;
  }

  const auto bridge_channel = &_meshtastic_bridge_prefs.bridge_channels[index];
  memset(bridge_channel->channel_details.channel.secret, 0, sizeof(bridge_channel->channel_details.channel.secret));
  memset(bridge_channel->channel_details.channel.hash, 0, sizeof(bridge_channel->channel_details.channel.hash));
  memset(bridge_channel->channel_details.name, 0, sizeof(bridge_channel->channel_details.name));
  memset(&bridge_channel->region, 0, sizeof(bridge_channel->region));

  int len = 0;

  if (strcasecmp(name, "Public") == 0) {
    const auto psk_base64 = PUBLIC_GROUP_PSK;
    len = decode_base64((unsigned char *)psk_base64, strlen(psk_base64), bridge_channel->channel_details.channel.secret);
    if (len == 32 || len == 16) {
      mesh::Utils::sha256(bridge_channel->channel_details.channel.hash, sizeof(bridge_channel->channel_details.channel.hash), bridge_channel->channel_details.channel.secret, len);
    } else {
      MESH_DEBUG_PRINTLN("Bridge MT. Add channel (public) KO. Len: %d", len);

      return nullptr;
    }
  } else {
    len = 16;
    mesh::Utils::sha256(bridge_channel->channel_details.channel.secret, len, (const uint8_t *)name, strlen(name));
  }

  mesh::Utils::sha256(bridge_channel->channel_details.channel.hash, sizeof(bridge_channel->channel_details.channel.hash), bridge_channel->channel_details.channel.secret, len);

  strncpy(bridge_channel->channel_details.name, name, sizeof(bridge_channel->channel_details.name));

  if (!strlen(region)) {
    StrHelper::strncpy(bridge_channel->region, "*", sizeof(bridge_channel->region));
  } else {
    StrHelper::strncpy(bridge_channel->region, region, sizeof(bridge_channel->region));
  }

  MESH_DEBUG_PRINTLN("Bridge MT. Add channel %s [%s] (0x%x | 0x%x) at position %d OK", name, bridge_channel->region, bridge_channel->channel_details.channel.secret[0], bridge_channel->channel_details.channel.hash[0], index);

  return bridge_channel;
}

bool MyMeshWithMeshtasticBridge::send_message_to_meshcore_from_meshtastic(const char *sender_name,
                                                                          const char *text,
                                                                          uint8_t meshtastic_channel_index) {
  _n_received_meshtastic++;

  if (_meshtastic_bridge_prefs.interval_stop_relay_mc > 0) {
    if (_ms->getMillis() - _last_meshcore_message_time > _meshtastic_bridge_prefs.interval_stop_relay_mc) {
      MESH_DEBUG_PRINTLN("Bridge MT received from MT a message: %s but no MC seen during the interval (%d > %d s). Ignore it",
        text, (_ms->getMillis() - _last_meshcore_message_time) / 1000, _meshtastic_bridge_prefs.interval_stop_relay_mc / 1000);
      return false;
    }
  }

  if (_queue_message_to_send_to_meshcore.isFull()) {
    MESH_DEBUG_PRINTLN("Bridge MT can not add message from MT from %s because queue is full", sender_name);
    return false;
  }

  if (strlen(_meshtastic_bridge_prefs.bridge_channels[meshtastic_channel_index].channel_details.name) == 0) {
    MESH_DEBUG_PRINTLN("Bridge MT channel index %d not configured", meshtastic_channel_index);
    return false;
  }

  MeshtasticBridgeMessageToSend message_to_send = { .meshtastic_channel_index = meshtastic_channel_index,
                                                    .next_time_to_send = _ms->getMillis() +
                                                                         _meshtastic_bridge_prefs.tx_delay };
  strcpy(message_to_send.sender_name, sender_name);
  strcpy(message_to_send.message, text);

  const auto success = _queue_message_to_send_to_meshcore.enqueue(message_to_send);

  _last_meshtastic_message_time = _ms->getMillis();
  _n_sent_meshcore++;

  if (success) {
    MESH_DEBUG_PRINTLN("Bridge MT message from MT from %s OK. Next TX at %ld (%ld)", sender_name,
                        message_to_send.next_time_to_send, _ms->getMillis());
  } else {
    MESH_DEBUG_PRINTLN("Bridge MT message from MT from %s KO. Queue full", sender_name);
  }

  return success;
}
void MyMeshWithMeshtasticBridge::clearStats() {
  MyMesh::clearStats();

  _n_received_meshcore = 0;
  _n_received_meshtastic = 0;
  _n_sent_meshcore = 0;
  _n_sent_meshtastic = 0;
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
      MESH_DEBUG_PRINTLN("Bridge MT Message from %s channel", bridge_channel.channel_details.name);
      dest[n++] = bridge_channel.channel_details.channel;
    }
  }

  if (!n) {
    MESH_DEBUG_PRINTLN("Bridge MT Message from unknown channel: %x", hash[0]);
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

    _n_received_meshcore++;

    MESH_DEBUG_PRINTLN("Bridge MT received from MC a message: %s from channel: %x", text, channel.hash[0]);

    if (_meshtastic_bridge_prefs.interval_stop_relay_mt > 0) {
      if (_ms->getMillis() - _last_meshtastic_message_time > _meshtastic_bridge_prefs.interval_stop_relay_mt) {
        MESH_DEBUG_PRINTLN("Bridge MT received from MC a message: %s but no MT seen during the interval (%d > %d s). Ignore it",
          text, (_ms->getMillis() - _last_meshtastic_message_time) / 1000, _meshtastic_bridge_prefs.interval_stop_relay_mt / 1000);
        return;
      }
    }

    uint8_t channel_index = 0;

    for (auto [channel_details, name] : _meshtastic_bridge_prefs.bridge_channels) {
      if (channel_details.channel.hash[0] == channel.hash[0] && name[0] != '\0') {
        break;
      }

      channel_index++;
    }

    if (channel_index >= MESHTASTIC_MAX_CHANNELS) {
      MESH_DEBUG_PRINTLN("Bridge MT Message from 0x%X channel hash without bridge configured",
                         channel.hash[0]);
    } else {
      MeshtasticBridgeMessageToSend message_to_send = { .meshtastic_channel_index = channel_index,
                                                    .next_time_to_send = _ms->getMillis() +
                                                                         _meshtastic_bridge_prefs.tx_delay };
      sscanf(text, "%31[^:]: %199s", message_to_send.sender_name, message_to_send.message);

      _last_meshcore_message_time = _ms->getMillis();
      _n_sent_meshtastic++;

      const auto success = _queue_message_to_send_to_meshtastic.enqueue(message_to_send);

      if (success) {
        MESH_DEBUG_PRINTLN("Bridge MT message from MC from %s OK. Next TX at %ld (%ld)", message_to_send.sender_name,
                            message_to_send.next_time_to_send, _ms->getMillis());
      } else {
        MESH_DEBUG_PRINTLN("Bridge MT message from MC from %s KO. Queue full", message_to_send.sender_name);
      }
    }
  }
}

bool MyMeshWithMeshtasticBridge::send_message(MeshtasticBridgeMessageToSend message_to_send) {
  const auto bridge_channel = _meshtastic_bridge_prefs.bridge_channels[message_to_send.meshtastic_channel_index];

  if (strlen(bridge_channel.channel_details.name) == 0) {
    MESH_DEBUG_PRINTLN("Bridge MT channel index %d not found", message_to_send.meshtastic_channel_index);
    return false;
  }

  TransportKey scope;
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

  const auto pkt =
      createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, bridge_channel.channel_details.channel, temp, 5 + prefix_len + text_len);

  MESH_DEBUG_PRINTLN("Bridge MT send to MC on channel %s : %s", bridge_channel.channel_details.name, temp + 5);

  if (pkt) {
    sendFloodScoped(scope, pkt, 0, getNodePrefs()->path_hash_mode + 1);
    return true;
  }

  return false;
}

bool MyMeshWithMeshtasticBridge::send_one_message_from_queue() {
  if (!_queue_message_to_send_to_meshcore.isEmpty()) {
    const auto meshcore_message = _queue_message_to_send_to_meshcore.get_next();
    if (_ms->getMillis() > meshcore_message->next_time_to_send) {
      if (send_message(*meshcore_message)) {
        MeshtasticBridgeMessageToSend message{};
        _queue_message_to_send_to_meshcore.dequeue(message);
        return true;
      }
    }
  }

  if (!_queue_message_to_send_to_meshtastic.isEmpty()) {
    const auto meshtastic_message = _queue_message_to_send_to_meshtastic.get_next();
    if (_ms->getMillis() > meshtastic_message->next_time_to_send) {
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