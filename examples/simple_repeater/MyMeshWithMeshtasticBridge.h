#pragma once

#include "MeshtasticController.h"
#include "MyMesh.h"
#include "helpers/ChannelDetails.h"

#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="
#define MESHTASTIC_MAX_CHANNELS 5

struct MeshtasticBridgePrefs {
  bool enabled = false;
  uint8_t rx_pin = -1;
  uint8_t tx_pin = -1;
  uint8_t baud_rate = 115200;
  uint16_t tx_delay = 0;
  uint16_t interval_stop_repeat_mc = 0;
  uint16_t interval_stop_repeat_mt = 0;
  ChannelDetails channels[MESHTASTIC_MAX_CHANNELS] = {};
};

class MyMeshWithMeshtasticBridge : public MyMesh {
public:
  MyMeshWithMeshtasticBridge(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms,
                             mesh::RNG &rng, mesh::RTCClock &rtc, mesh::MeshTables &tables);
  bool begin(FILESYSTEM *fs);
  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  void loop();

protected:
  int searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel dest[], int max_matches) override;
  void onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) override;
  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp, const char *text);

private:
  bool add_channel(uint8_t index, const char *name);
  bool send_to_meshcore(const char *sender_name, const char *text, const mesh::GroupChannel &channel);

  MeshtasticBridgePrefs _meshtastic_bridge_prefs;
  MeshtasticController _meshtastic_controller;
};