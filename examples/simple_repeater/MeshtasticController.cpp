#include "MeshtasticController.h"

#include "MeshCore.h"

MeshtasticController *MeshtasticController::instance = nullptr;

MeshtasticController::MeshtasticController(MyMeshWithMeshtasticBridge* mesh) : _mesh(mesh) {
  instance = this;

  set_text_message_callback(text_message_callback);
}
bool MeshtasticController::begin(uint8_t rx_pin, uint8_t tx_pin, uint8_t baud_rate) {
#ifndef MT_SERIAL
#if defined(ARDUINO_ARCH_SAMD)
#define MT_SERIAL Serial1
#elif defined(ARDUINO_ARCH_ESP32)
#define MT_SERIAL Serial1
#elif defined(ARDUINO_ARCH_RP2040)
#define MT_SERIAL Serial1
#endif
#endif

#if defined(ARDUINO_ARCH_ESP32)
  MT_SERIAL.begin(baud_rate, SERIAL_8N1, rx_pin, tx_pin);
#elif defined(ARDUINO_ARCH_RP2040)
  MT_SERIAL.setPinout(tx_pin, rx_pin);
#ifdef SERIAL_HW_FIFO_SIZE
  MT_SERIAL.setFIFOSize(SERIAL_HW_FIFO_SIZE);
#endif
  MT_SERIAL.begin(baud_rate);
#else
  MT_SERIAL.begin(baud_rate);
#endif

  mt_serial_init(&MT_SERIAL);

  return request_node_report();
}

void MeshtasticController::loop(uint32_t now) {
  const auto can_send = mt_loop(now);

  if (can_send) {
    if (now - _next_node_report_time > NODE_REPORT_PERIOD) {
      request_node_report();
      _next_node_report_time = millis();
    }
  }
}

void MeshtasticController::node_report_callback(mt_node_t *node_info, const mt_nr_progress_t progress) {
  instance->add_node_to_db(node_info, progress);
}

void MeshtasticController::text_message_callback(const uint32_t from_node_id, const uint32_t to_node_id, const uint8_t channel_index, const char *text) {
  instance->text_message_received(from_node_id, to_node_id, channel_index, text);
}

bool MeshtasticController::request_node_report() {
  _nodes_count = 0;
  MESH_DEBUG_PRINTLN("Bridge MT request nodeinfo from MT");
  return mt_request_node_report(node_report_callback);
}

void MeshtasticController::add_node_to_db(mt_node_t *node_info, const mt_nr_progress_t progress) {
  MESH_DEBUG_PRINTLN("Bridge MT receive node info progress: %d. Node id: !%x, %s -> %s", progress,
                     node_info->node_num, node_info->short_name, node_info->long_name);

  if (progress == MT_NR_IN_PROGRESS) {
    _nodes[_nodes_count++].node_num = node_info->node_num;
    strcpy(_nodes[_nodes_count++].long_name, node_info->long_name);
  }
}

void MeshtasticController::text_message_received(const uint32_t from_node_id, const uint32_t to_node_id, const uint8_t channel_index, const char *text) {
  MESH_DEBUG_PRINTLN("Bridge MT receive a text message on channel: %d from !%x to !%x: %s", channel_index, from_node_id, to_node_id,text);

  if (to_node_id == 0xFFFFFFFF) {
    MeshtasticNode *node_info = nullptr;
    for (auto &i : _nodes) {
      MESH_DEBUG_PRINTLN("Bridge MT match received %x with node %x (%s) ?", from_node_id, i.node_num, i.long_name);
      if (i.node_num == from_node_id && strlen(i.long_name) > 0) {
        MESH_DEBUG_PRINTLN("Bridge MT match found for node %x (%s) !", i.node_num, i.long_name);
        node_info = &i;
        break;
      }
    }

    char sender_name[MESHTASTIC_MAX_MESSAGE_LENGTH];

    if (node_info != nullptr) {
      strncpy(sender_name, node_info->long_name, sizeof(sender_name));
      node_info->last_send_timestamp = _mesh->getRTCClock()->getCurrentTime();
      _last_seen = node_info;
    } else {
      snprintf(sender_name, sizeof(sender_name), "!%x", from_node_id);
    }

    _mesh->send_message_to_meshcore_from_meshtastic(sender_name, text, channel_index);
  }
}

bool MeshtasticController::send_message(uint32_t now, MeshtasticBridgeMessageToSend message_to_send) {
  const auto can_send = mt_loop(now);

  if (!can_send) {
    return false;
  }

  char temp[MESHTASTIC_MAX_MESSAGE_LENGTH];
  snprintf(temp, MESHTASTIC_MAX_MESSAGE_LENGTH, "MT_%s:%s", message_to_send.sender_name, message_to_send.message);

  MESH_DEBUG_PRINTLN("Bridge MT send message to MT %s", temp);

  return mt_send_text(temp, 0xFFFFFF, message_to_send.meshtastic_channel_index);
}