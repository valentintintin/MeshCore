#pragma once

#include "Dispatcher.h"
#include "MeshtasticBridgeCommon.h"
#include "MyMeshWithMeshtasticBridge.h"

#include <Meshtastic.h>

#define NODE_REPORT_TIMEOUT 10 * 1000

// Request a node report every this many msec
#ifndef NODE_REPORT_PERIOD
#define NODE_REPORT_PERIOD (300 * 1000)
#endif

#ifndef MESHTASTIC_BRIDGE_MAX_NODEDB
#define MESHTASTIC_BRIDGE_MAX_NODEDB 100
#endif

struct MeshtasticNode {
  uint32_t node_num;
  char long_name[MAX_LONG_NAME_LEN];
};

class MyMeshWithMeshtasticBridge;

class MeshtasticController {
 public:
  explicit MeshtasticController(MyMeshWithMeshtasticBridge* mesh);
  bool begin(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud_rate);
  void loop(uint32_t now);
  void stop();
  bool request_node_report();
  bool send_message(uint32_t now, MeshtasticBridgeMessageToSend message_to_send);

  bool is_initialized() const {
    return _initialized;
  }

  uint8_t nodes_count() const {
    return _nodes_count;
  }

  MeshtasticNode get_node(uint8_t index) const {
    return _nodes[index];
  }

  MeshtasticNode get_last_seen() const {
    return _last_seen;
  }

 private:
  static void node_report_callback(mt_node_t* node_info,
                                   mt_nr_progress_t progress);
  static void text_message_callback(uint32_t from_node_id,
                                    uint32_t to_node_id,
                                    uint8_t channel_index,
                                    const char* text);
  static MeshtasticController* instance;

  void add_node_to_db(mt_node_t* node_info, mt_nr_progress_t progress);
  void text_message_received(uint32_t from_node_id,
                             uint32_t to_node_id,
                             uint8_t channel_index,
                             const char* text);

  MeshtasticNode _nodes[MESHTASTIC_BRIDGE_MAX_NODEDB]{};
  MyMeshWithMeshtasticBridge* _mesh;

  uint8_t _nodes_count = 0;
  uint32_t _next_node_report_time = 0;
  uint32_t _next_node_report_time_timeout = 0;

  MeshtasticNode _last_seen{};

  bool _initialized = false;
};
