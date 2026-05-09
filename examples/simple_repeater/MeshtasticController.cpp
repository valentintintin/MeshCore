#include "MeshtasticController.h"

#include "MeshCore.h"
#include "mt_internals.h"

#ifndef MT_SERIAL
#define MT_SERIAL Serial1
#endif

MeshtasticController* MeshtasticController::instance = nullptr;

MeshtasticController::MeshtasticController(MyMeshWithMeshtasticBridge* mesh)
    : _mesh(mesh) {
  instance = this;

  set_text_message_callback(text_message_callback);
}

bool MeshtasticController::begin(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud_rate) {
  stop();

  if (rx_pin == 255 || tx_pin == 255 || baud_rate == 0) {
    MESH_DEBUG_PRINTLN(
        "[MT Bridge] Meshtastic serial init refused (TX=%d RX=%d baud=%d)",
        tx_pin, rx_pin, baud_rate);
    return false;
  }

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

  MESH_DEBUG_PRINTLN(
      "[MT Bridge] Meshtastic serial started (TX=%d RX=%d baud=%ld)", tx_pin,
      rx_pin, baud_rate);

  mt_serial_init(&MT_SERIAL);

  return request_node_report();
}

void MeshtasticController::loop(uint32_t now) {
  if (!mt_serial_mode) {
    return;
  }

  const auto can_send = mt_loop(now);

  if (can_send) {
    if (now - _next_node_report_time > NODE_REPORT_PERIOD) {
      request_node_report();
      _next_node_report_time = millis();
    }
  }
}

void MeshtasticController::stop() {
  mt_serial_mode = false;
  MT_SERIAL.end();
  MESH_DEBUG_PRINTLN("[MT Bridge] Meshtastic serial stopped");
}

void MeshtasticController::node_report_callback(mt_node_t* node_info,
                                                const mt_nr_progress_t progress) {
  instance->add_node_to_db(node_info, progress);
}

void MeshtasticController::text_message_callback(const uint32_t from_node_id,
                                                 const uint32_t to_node_id,
                                                 const uint8_t channel_index,
                                                 const char* text) {
  instance->text_message_received(from_node_id, to_node_id, channel_index, text);
}

bool MeshtasticController::request_node_report() {
  _nodes_count = 0;
  MESH_DEBUG_PRINTLN("[MT Bridge] Request Meshtastic node report");
  return mt_request_node_report(node_report_callback);
}

void MeshtasticController::add_node_to_db(mt_node_t* node_info,
                                          const mt_nr_progress_t progress) {
  MESH_DEBUG_PRINTLN(
      "[MT Bridge] Node report progress=%d node=!%x short='%s' long='%s'",
      progress, node_info->node_num, node_info->short_name, node_info->long_name);

  if (progress == MT_NR_IN_PROGRESS) {
    if (_nodes_count >= MESHTASTIC_MAX_NODEDB) {
      MESH_DEBUG_PRINTLN("[MT Bridge] Node DB full, dropping node !%x",
                         node_info->node_num);
      return;
    }
    _nodes[_nodes_count].node_num = node_info->node_num;
    StrHelper::strncpy(_nodes[_nodes_count].long_name, node_info->long_name,
                       sizeof(_nodes[_nodes_count].long_name));
    _nodes_count++;
  }
}

void MeshtasticController::text_message_received(const uint32_t from_node_id,
                                                 const uint32_t to_node_id,
                                                 const uint8_t channel_index,
                                                 const char* text) {
  MESH_DEBUG_PRINTLN(
      "[MT Bridge] RX text channel=%d from=!%x to=!%x payload='%s'",
      channel_index, from_node_id, to_node_id, text);

  if (to_node_id == 0xFFFFFFFF) {
    MeshtasticNode* node_info = nullptr;
    for (auto i = 0; i < _nodes_count; i++) {
      const auto [node_num, long_name] = _nodes[i];

      if (!node_num) {
        continue;
      }

      if (node_num == from_node_id) {
        MESH_DEBUG_PRINTLN("[MT Bridge] Matched node !%x (%s)", node_num,
                           long_name);
        node_info = &_nodes[i];
        break;
      }
    }

    char sender_name[MESHTASTIC_MAX_MESSAGE_LENGTH]{};
    snprintf(sender_name, sizeof(sender_name), "!%x", from_node_id);

    if (node_info != nullptr) {
      _last_seen = node_info;

      if (strlen(node_info->long_name)) {
        StrHelper::strncpy(sender_name, node_info->long_name, sizeof(sender_name));
      }
    }

    _mesh->send_message_to_meshcore_from_meshtastic(sender_name, text, channel_index);
  }
}

bool MeshtasticController::send_message(
    uint32_t now, MeshtasticBridgeMessageToSend message_to_send) {
  if (!mt_serial_mode) {
    MESH_DEBUG_PRINTLN("[MT Bridge] TX aborted: Meshtastic serial not started");
    return false;
  }

  const auto can_send = mt_loop(now);

  if (!can_send) {
    MESH_DEBUG_PRINTLN("[MT Bridge] TX deferred: Meshtastic stack not ready");
    return false;
  }

  char temp[MESHTASTIC_MAX_MESSAGE_LENGTH];
  snprintf(temp, MESHTASTIC_MAX_MESSAGE_LENGTH, "MT_%s:%s", message_to_send.sender_name, message_to_send.message);

  MESH_DEBUG_PRINTLN("[MT Bridge] Send to Meshtastic channel=%d payload='%s'",
                     message_to_send.meshtastic_channel_index, temp);

  return mt_send_text(temp, 0xFFFFFFFF, message_to_send.meshtastic_channel_index);
}
