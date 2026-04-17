#pragma once

#include "MyMesh.h"
#include "helpers/ChannelDetails.h"

#include <Meshtastic.h>

// Request a node report every this many msec
#define NODE_REPORT_PERIOD (300 * 1000)
#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="
#define MESHTASTIC_MAX_CHANNELS 5

struct MeshtasticBridgePrefs {
  bool enabled = false;
  uint8_t rx_pin = -1;
  uint8_t tx_pin = -1;
  uint8_t baud_rate = 115200;
  uint16_t tx_delay = 0;
  ChannelDetails channels[MESHTASTIC_MAX_CHANNELS] = {};
};

class MyMeshWithMeshtasticBridge : public MyMesh {
public:
  MyMeshWithMeshtasticBridge(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms,
                             mesh::RNG &rng, mesh::RTCClock &rtc, mesh::MeshTables &tables);
  bool begin(FILESYSTEM *fs, int8_t rxPin, int8_t txPin, uint32_t baudRate = 115200);
  void want_to_send_to_meshtastic(const char *send_name, const char *text, int text_len);
  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  void loop();

protected:
  int searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel dest[], int max_matches) override;
  void onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) override;
  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp, const char *text);

private:
  static void node_report_callback(mt_node_t *nodeinfo, mt_nr_progress_t progress);
  static void text_message_callback(uint32_t from, uint32_t to, uint8_t channel, const char *text);
  static MyMeshWithMeshtasticBridge* instance;

  bool add_channel(uint8_t index, const char *name);
  void add_node_info(mt_node_t *nodeinfo, mt_nr_progress_t progress);
  void text_message_received(uint32_t from, uint32_t to, uint8_t channel, const char *text);
  bool request_node_report();

  bool send_to_meshtastic();
  bool send_to_meshcore(const char *sender_name, const char *text, int text_len);

  MeshtasticBridgePrefs _meshtastic_bridge_prefs;
  mesh::GroupChannel meshcore_public_channel{};
  mesh::GroupChannel meshcore_test_channel{};
  mt_node_t node_infos[100]{};
  uint8_t node_infos_count = 0;
  uint32_t next_node_report_time = 0;
  char message_to_send[100]{};
  bool has_message_to_send = false;
};