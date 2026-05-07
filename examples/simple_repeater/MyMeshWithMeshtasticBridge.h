// ReSharper disable CppHidingFunction
#pragma once

#include "MeshtasticController.h"
#include "MyMesh.h"
#include "SimpleQueue.h"

#define PUBLIC_GROUP_PSK "izOH6cXN6mrJ5e26oRXNcg=="
#define PREFS_FILENAME "/meshtastic_bridge_prefs"

class MeshtasticController;

// ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
class MyMeshWithMeshtasticBridge : public MyMesh {
 public:
  MyMeshWithMeshtasticBridge(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms,
                             mesh::RNG &rng, mesh::RTCClock &rtc, mesh::MeshTables &tables);
  void begin(FILESYSTEM *fs);
  void beginBridge();
  void handleCommand(uint32_t sender_timestamp, char *command, char *reply);
  void loop();

  bool send_message_to_meshcore_from_meshtastic(const char *sender_name, const char *text,
                                                uint8_t meshtastic_channel_index);
  void clearStats() override;

 protected:
  int searchChannelsByHash(const uint8_t *hash, mesh::GroupChannel dest[], int max_matches) override;
  void onGroupDataRecv(mesh::Packet *packet, uint8_t type, const mesh::GroupChannel &channel, uint8_t *data,
                       size_t len) override;

 private:
  void begin_bridge();
  bool load_file_prefs();
  bool save_file_prefs();

  void handle_get_cmd(uint32_t sender_timestamp, const char *command, char *reply);
  void handle_set_cmd(uint32_t sender_timestamp, const char *command, char *reply);

  MeshtasticBridgeChannel *add_meshcore_bridge_channel(uint8_t index, const char *name, const char *region);
  bool send_message(MeshtasticBridgeMessageToSend message_to_send);
  bool send_one_message_from_queue();

  bool derive_scope_from_region_name(const char *region_name, TransportKey &scope);

  MeshtasticBridgePrefs _meshtastic_bridge_prefs;
  MeshtasticController *_meshtastic_controller;

  FILESYSTEM *_fs = nullptr;
  uint32_t _last_meshcore_rx_ms = 0;
  uint32_t _last_meshtastic_rx_ms = 0;

  uint32_t _meshtastic_rx_count; // RX from MT
  uint32_t _meshcore_rx_count;   // RX from MC
  uint32_t _meshcore_tx_count;   // TX from MT to MC
  uint32_t _meshtastic_tx_count; // TX from MC to MT

  SimpleQueue<MeshtasticBridgeMessageToSend> _queue_message_to_send_to_meshcore{};
  SimpleQueue<MeshtasticBridgeMessageToSend> _queue_message_to_send_to_meshtastic{};
};