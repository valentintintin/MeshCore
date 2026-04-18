#pragma once

#include "Dispatcher.h"

#include <Meshtastic.h>

// Request a node report every this many msec
#define NODE_REPORT_PERIOD (300 * 1000)

#define MESHTASTIC_MAX_MESSAGE_LENGTH 200

#ifndef MT_MAX_NODEDB
#define MT_MAX_NODEDB 100
#endif

struct MeshtasticNode {
  uint32_t node_num;
  char long_name[MAX_LONG_NAME_LEN];
};

struct MeshtasticMessageToSend {
  char message[MESHTASTIC_MAX_MESSAGE_LENGTH];
  uint8_t channel_index;
  uint32_t next_time_to_send;
};

class MeshtasticController {
public:
  explicit MeshtasticController(mesh::MillisecondClock& ms);
  void want_to_send_to_meshtastic(const char *send_name, const char *text, int text_len);
  void loop();
private:
  static void node_report_callback(mt_node_t *nodeinfo, mt_nr_progress_t progress);
  static void text_message_callback(uint32_t from, uint32_t to, uint8_t channel, const char *text);
  static MeshtasticController* instance;

  void add_node_info(mt_node_t *nodeinfo, mt_nr_progress_t progress);
  void text_message_received(uint32_t from, uint32_t to, uint8_t channel, const char *text);
  bool request_node_report();
  bool send_to_meshtastic();

  MeshtasticNode _node_infos[MT_MAX_NODEDB]{};
  MeshtasticMessageToSend _message_to_send{};

  uint8_t _node_infos_count = 0;
  uint32_t _next_node_report_time = 0;

  mesh::MillisecondClock& ms;
};
