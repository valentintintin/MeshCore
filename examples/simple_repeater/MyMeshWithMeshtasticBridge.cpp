#include "MyMeshWithMeshtasticBridge.h"

#include "helpers/BaseChatMesh.h"

#include <base64.hpp>

MyMeshWithMeshtasticBridge *MyMeshWithMeshtasticBridge::instance = nullptr;

void MyMeshWithMeshtasticBridge::node_report_callback(mt_node_t *nodeinfo, mt_nr_progress_t progress) {
  instance->add_node_info(nodeinfo, progress);
}

void MyMeshWithMeshtasticBridge::text_message_callback(uint32_t from, uint32_t to, uint8_t channel,
                                                       const char *text) {
  instance->text_message_received(from, to, channel, text);
}

void MyMeshWithMeshtasticBridge::want_to_send_to_meshtastic(const char *send_name, const char *text,
                                                            int text_len) {
#define MAX_TEXT_LEN 120

  snprintf(message_to_send, MAX_TEXT_LEN, "%s_%s", send_name, text);
  has_message_to_send = true;

  MESH_DEBUG_PRINTLN("Bridge MT want to send to MT %s", message_to_send);
}
void MyMeshWithMeshtasticBridge::handleCommand(uint32_t sender_timestamp, char *command, char *reply) {
  while (*command == ' ')
    command++; // skip leading spaces

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
 mt set tx_delay ms
 mt get rx_pin
 mt set rx_pin pin
 mt get tx_pin
 mt set tx_pin pin
 mt get baud_rate
 mt set baud_rate baud
 */

  if (memcmp(command, "mt ", 3) == 0) {
    const char *config = &command[3];
    if (memcmp(config, "get ", 4) == 0) {
      const char *configGet = &config[4];
      if (memcmp(configGet, "enabled", 7) == 0) {
        sprintf(reply, "> %s", _meshtastic_bridge_prefs.enabled ? "on" : "off");
      } else if (memcmp(configGet, "tx_delay", 8) == 0) {
        sprintf(reply, "> %d ms", _meshtastic_bridge_prefs.tx_delay);
      } else if (memcmp(configGet, "rx_pin", 6) == 0) {
        sprintf(reply, "> %d", _meshtastic_bridge_prefs.rx_pin);
      } else if (memcmp(configGet, "tx_pin", 6) == 0) {
        sprintf(reply, "> %d", _meshtastic_bridge_prefs.tx_pin);
      } else if (memcmp(configGet, "baud_rate", 9) == 0) {
        sprintf(reply, "> %d bds", _meshtastic_bridge_prefs.baud_rate);
      } else if (memcmp(configGet, "channels", 8) == 0) {
        sprintf(reply, "> MT channels:");
        uint8_t channel_index = 0;
        for (const auto [channel, name] : _meshtastic_bridge_prefs.channels) {
          if (strlen(name)) {
            sprintf(reply, "%s\n%d -> %s", reply, channel_index, name);
          } else {
            sprintf(reply, "%s\n%d -> disabled", reply, channel_index);
          }
          channel_index++;
        }
      } else if (memcmp(configGet, "channel ", 8) == 0) {
        const auto channel_index = atoi(&command[8]);
        if (channel_index < sizeof(_meshtastic_bridge_prefs.channels)) {
          const auto [channel, name] = _meshtastic_bridge_prefs.channels[channel_index];
          sprintf(reply, "> MT channel %d : ");
          if (strlen(name)) {
            sprintf(reply, "\t %d -> %s", channel_index, name);
          } else {
            sprintf(reply, "\t %d -> disabled", channel_index);
          }
        } else {
          sprintf(reply, "> error [0; %d]", sizeof(_meshtastic_bridge_prefs.channels));
        }
      } else {
        sprintf(reply, "> error get command");
      }
    } else {
      sprintf(reply, "> error mt command");
    }
  } else {
    MyMesh::handleCommand(sender_timestamp, command, reply);
  }
}

MyMeshWithMeshtasticBridge::MyMeshWithMeshtasticBridge(mesh::MainBoard &board, mesh::Radio &radio,
                                                       mesh::MillisecondClock &ms, mesh::RNG &rng,
                                                       mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : MyMesh(board, radio, ms, rng, rtc, tables) {
  instance = this;

  memset(meshcore_public_channel.secret, 0, sizeof(meshcore_public_channel.secret));
#define PUBLIC_GROUP_PSK "izOH6cXN6mrJ5e26oRXNcg=="
  const auto psk_base64 = PUBLIC_GROUP_PSK;
  int len = decode_base64((unsigned char *)psk_base64, strlen(psk_base64), meshcore_public_channel.secret);
  if (len == 32 || len == 16) {
    mesh::Utils::sha256(meshcore_public_channel.hash, sizeof(meshcore_public_channel.hash),
                        meshcore_public_channel.secret, len);
  }

  memset(meshcore_test_channel.secret, 0, sizeof(meshcore_test_channel.secret));
  const auto channel_name = "#valentintest";
  len = 16;
  mesh::Utils::sha256(meshcore_test_channel.secret, len, (const uint8_t *)channel_name, strlen(channel_name));
  mesh::Utils::sha256(meshcore_test_channel.hash, sizeof(meshcore_test_channel.hash),
                      meshcore_test_channel.secret, len);
}

bool MyMeshWithMeshtasticBridge::begin(FILESYSTEM *fs, const int8_t rxPin, const int8_t txPin,
                                       const uint32_t baudRate) {
  MyMesh::begin(fs);

  add_channel(0, "public");
  add_channel(1, "#valentintest");

  if (rxPin != -1 && txPin != -1 && baudRate > 0) {
    MESH_DEBUG_PRINTLN("Bridge MT init on pin TX: %d and RX: %d with baudRate: %d", txPin, rxPin, baudRate);

    mt_serial_init(rxPin, txPin, baudRate);
    set_text_message_callback(text_message_callback);

    return request_node_report();
  }

  return true;
}

bool MyMeshWithMeshtasticBridge::add_channel(const uint8_t index, const char *name) {
  MESH_DEBUG_PRINTLN("Bridge MT. Add channel %s at position %d", name, index);

  auto [channel, channel_name] = _meshtastic_bridge_prefs.channels[index];
  memset(channel.secret, 0, sizeof(channel.secret));
  memset(channel_name, 0, sizeof(channel_name));

  if (strcasecmp_P(name, PSTR("Public")) == 0) {
    const auto psk_base64 = PUBLIC_GROUP_PSK;
    const int len = decode_base64((unsigned char *)psk_base64, strlen(psk_base64), channel.secret);
    if (len == 32 || len == 16) {
      mesh::Utils::sha256(channel.hash, sizeof(channel.hash), channel.secret, len);

      strcpy(channel_name, name);

      MESH_DEBUG_PRINTLN("Bridge MT. Add channel (public) at position %d OK", index);

      return true;
    }

    MESH_DEBUG_PRINTLN("Bridge MT. Add channel (public) KO. Len: %d", len);

    return false;
  }

  if (strlen(name) > 32) {
    MESH_DEBUG_PRINTLN("Bridge MT. Add channel %s KO. Len: %d > 32", name, strlen(name));

    return false;
  }

  constexpr uint8_t len = 16;
  mesh::Utils::sha256(meshcore_test_channel.secret, len, (const uint8_t *)name, strlen(name));
  mesh::Utils::sha256(meshcore_test_channel.hash, sizeof(meshcore_test_channel.hash),
                      meshcore_test_channel.secret, len);
  strcpy(channel_name, name);

  MESH_DEBUG_PRINTLN("Bridge MT. Add channel %s at position %d OK", name, index);

  return true;
}

void MyMeshWithMeshtasticBridge::add_node_info(mt_node_t *nodeinfo, mt_nr_progress_t progress) {
  MESH_DEBUG_PRINTLN("Bridge MT receive node info progress: %d. Node id: !%x, %s -> %s", progress,
                     nodeinfo->node_num, nodeinfo->short_name, nodeinfo->long_name);

  if (progress == MT_NR_IN_PROGRESS) {
    node_infos[node_infos_count++] = *nodeinfo;
  }
}

void MyMeshWithMeshtasticBridge::text_message_received(uint32_t from, uint32_t to, uint8_t channel,
                                                       const char *text) {
  MESH_DEBUG_PRINTLN("Bridge MT receive a text message on channel: %d from !%x to !%x: %s", channel, from, to,
                     text);

  if (to == 0xFFFFFFFF) {
#define MAX_TEXT_LEN 110

    const mt_node_t *node_info = nullptr;
    for (const auto i : node_infos) {
      MESH_DEBUG_PRINTLN("Match received %x with node %x (%s) ?", from, i.node_num, i.long_name);
      if (i.node_num == from && i.has_user && strlen(i.long_name) > 0) {
        MESH_DEBUG_PRINTLN("Match found for node %x (%s) !", i.node_num, i.long_name);
        node_info = &i;
        break;
      }
    }

    char temp[45];

    if (node_info != nullptr) {
      snprintf(temp, sizeof(temp), "MT_%s", node_info->long_name);
    } else {
      snprintf(temp, sizeof(temp), "MT!%x", from);
    }

    send_to_meshcore(temp, text, strlen(text));
  }
}

bool MyMeshWithMeshtasticBridge::request_node_report() {
  node_infos_count = 0;
  MESH_DEBUG_PRINTLN("Bridge MT request nodeinfo from MT");
  return mt_request_node_report(node_report_callback);
}

bool MyMeshWithMeshtasticBridge::send_to_meshtastic() {
  if (has_message_to_send) {
    has_message_to_send = false;

    MESH_DEBUG_PRINTLN("Bridge MT send to MT: %s", message_to_send);

    return mt_send_text(message_to_send);
  }

  return false;
}

bool MyMeshWithMeshtasticBridge::send_to_meshcore(const char *sender_name, const char *text, int text_len) {
  uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();

#define MAX_TEXT_LEN                                                                           \
  (10 * CIPHER_BLOCK_SIZE) // must be LESS than (MAX_PACKET_PAYLOAD - 4 - CIPHER_MAC_SIZE - 1)

  uint8_t temp[5 + MAX_TEXT_LEN + 32];
  memcpy(temp, &timestamp, 4); // mostly an extra blob to help make packet_hash unique
  temp[4] = 0;                 // TXT_TYPE_PLAIN

  sprintf((char *)&temp[5], "%s: ", sender_name); // <sender>: <msg>
  char *ep = strchr((char *)&temp[5], 0);
  const int prefix_len = ep - (char *)&temp[5];

  if (text_len + prefix_len > MAX_TEXT_LEN) text_len = MAX_TEXT_LEN - prefix_len;
  memcpy(ep, text, text_len);
  ep[text_len] = 0; // null terminator

  const auto pkt =
      createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, meshcore_test_channel, temp, 5 + prefix_len + text_len);

  MESH_DEBUG_PRINTLN("Bridge MT send to MC %s", temp + 5);

  if (pkt) {
    sendFlood(pkt);
    return true;
  }
  return false;
}

void MyMeshWithMeshtasticBridge::loop() {
  MyMesh::loop();

  const auto now = millis();
  const auto can_send = mt_loop(now);

  if (can_send) {
    if (now - next_node_report_time > NODE_REPORT_PERIOD) {
      request_node_report();
      next_node_report_time = millis();
    } else if (has_message_to_send) {
      send_to_meshtastic();
    }
  }
}

int MyMeshWithMeshtasticBridge::searchChannelsByHash(const uint8_t *hash, mesh::GroupChannel dest[],
                                                     int max_matches) {
  int n = 0;
  if (meshcore_public_channel.hash[0] == hash[0]) {
    MESH_DEBUG_PRINTLN("Message from public channel");
    dest[n++] = meshcore_public_channel;
  }
  if (meshcore_test_channel.hash[0] == hash[0]) {
    MESH_DEBUG_PRINTLN("Message from test channel");
    dest[n++] = meshcore_test_channel;
  }

  if (!n) {
    MESH_DEBUG_PRINTLN("Message from unknown channel: %x. Public: %x. Test: %x", hash[0],
                       meshcore_public_channel.hash[0], meshcore_test_channel.hash[0]);
  }

  return n;
}

void MyMeshWithMeshtasticBridge::onGroupDataRecv(mesh::Packet *packet, uint8_t type,
                                                 const mesh::GroupChannel &channel, uint8_t *data,
                                                 size_t len) {
  MESH_DEBUG_PRINTLN("Bridge MT received from MC a packet: %d from channel: %x of size: %d", type,
                     channel.hash[0], len);

  uint8_t txt_type = data[4];
  if (type == PAYLOAD_TYPE_GRP_TXT && len > 5 && (txt_type >> 2) == 0) { // 0 = plain text msg
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    // len can be > original length, but 'text' will be padded with zeroes
    data[len] = 0; // need to make a C string again, with null terminator

    // notify UI  of this new message
    onChannelMessageRecv(channel, packet, timestamp, (const char *)&data[5]); // let UI know
  }
}

void MyMeshWithMeshtasticBridge::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt,
                                                      uint32_t timestamp, const char *text) {
  want_to_send_to_meshtastic("MC", text, strlen(text));
}