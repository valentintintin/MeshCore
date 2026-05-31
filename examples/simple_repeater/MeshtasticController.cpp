#include "MeshtasticController.h"

#include "MeshCore.h"
#include "mt_internals.h"

#ifndef MESHTASTIC_BRIDGE_SERIAL_PORT
#define MESHTASTIC_BRIDGE_SERIAL_PORT Serial1
#endif

MeshtasticController *MeshtasticController::instance = nullptr;

MeshtasticController::MeshtasticController(MyMeshWithMeshtasticBridge *mesh) : _mesh(mesh) {
  instance = this;

  set_text_message_callback(text_message_callback);
}

bool MeshtasticController::begin(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud_rate) {
  stop();

  if (rx_pin == 255 || tx_pin == 255 || baud_rate == 0) {
    MESH_DEBUG_PRINTLN("[MT Bridge] Meshtastic serial init refused (TX=%d RX=%d baud=%d)", tx_pin, rx_pin,
                       baud_rate);
    return false;
  }

#if defined(ARDUINO_ARCH_ESP32)
  MESHTASTIC_BRIDGE_SERIAL_PORT.begin(baud_rate, SERIAL_8N1, rx_pin, tx_pin);
#elif defined(ARDUINO_ARCH_RP2040)
  MESHTASTIC_BRIDGE_SERIAL_PORT.setPinout(tx_pin, rx_pin);
#ifdef SERIAL_HW_FIFO_SIZE
  MESHTASTIC_BRIDGE_SERIAL_PORT.setFIFOSize(SERIAL_HW_FIFO_SIZE);
#endif
  MESHTASTIC_BRIDGE_SERIAL_PORT.begin(baud_rate);
#else
  MESHTASTIC_BRIDGE_SERIAL_PORT.begin(baud_rate);
#endif

  MESH_DEBUG_PRINTLN("[MT Bridge] Meshtastic serial started (TX=%d RX=%d baud=%ld)", tx_pin, rx_pin,
                     baud_rate);

  mt_serial_init(&MESHTASTIC_BRIDGE_SERIAL_PORT);

  return request_node_report();
}

void MeshtasticController::loop(const uint32_t now) {
  if (!mt_serial_mode) {
    return;
  }

  const auto can_send = mt_loop(now);

  if (_next_node_report_time_timeout > 0 && _mesh->millisHasNowPassed(_next_node_report_time_timeout)) {
    _initialized = false;
    _next_node_report_time_timeout = 0;
    _next_node_report_time = _mesh->futureMillis(NODE_REPORT_TIMEOUT);
    MESH_DEBUG_PRINTLN("[MT Bridge] Meshtastic report timeout KO. Communication error ?");
  }

  if (can_send) {
    if (_next_node_report_time > 0 && _mesh->millisHasNowPassed(_next_node_report_time)) {
      request_node_report();
    }
  }
}

void MeshtasticController::stop() {
  mt_serial_mode = false;
  _initialized = false;
  _next_node_report_time = 0;
  _next_node_report_time_timeout = 0;
  _nodes_count = 0;
  MESHTASTIC_BRIDGE_SERIAL_PORT.end();
  MESH_DEBUG_PRINTLN("[MT Bridge] Meshtastic serial stopped");
}

void MeshtasticController::node_report_callback(mt_node_t *node_info, const mt_nr_progress_t progress) {
  instance->add_node_to_db(node_info, progress);
}

void MeshtasticController::text_message_callback(const uint32_t from_node_id, const uint32_t to_node_id,
                                                 const uint8_t channel_index, const char *text) {
  instance->text_message_received(from_node_id, to_node_id, channel_index, text);
}

bool MeshtasticController::request_node_report() {
  _nodes_count = 0;
  _next_node_report_time_timeout = _mesh->futureMillis(NODE_REPORT_TIMEOUT);
  _next_node_report_time = 0;

  MESH_DEBUG_PRINTLN("[MT Bridge] Request Meshtastic node report");

  if (!mt_request_node_report(node_report_callback)) {
    _initialized = false;
    _next_node_report_time_timeout = 0;
    _next_node_report_time = _mesh->futureMillis(NODE_REPORT_TIMEOUT);
    MESH_DEBUG_PRINTLN("[MT Bridge] Meshtastic report timeout KO. Communication error ?");
  }

  return true;
}

void MeshtasticController::add_node_to_db(mt_node_t *node_info, const mt_nr_progress_t progress) {
  MESH_DEBUG_PRINTLN("[MT Bridge] Node report progress=%d node=!%x short='%s' long='%s'", progress,
                     node_info->node_num, node_info->short_name, node_info->long_name);

  if (progress == MT_NR_IN_PROGRESS) {
    if (_nodes_count >= MESHTASTIC_BRIDGE_MAX_NODEDB) {
      MESH_DEBUG_PRINTLN("[MT Bridge] Node DB full, dropping node !%x", node_info->node_num);
      return;
    }

    if (!node_info->has_user) {
      MESH_DEBUG_PRINTLN("[MT Bridge] No user, dropping node !%x", node_info->node_num);
      return;
    }

    if (node_info->is_unmessagable || node_info->role >= meshtastic_Config_DeviceConfig_Role_ROUTER) {
      MESH_DEBUG_PRINTLN("[MT Bridge] Not messageable, dropping node !%x with role %d", node_info->node_num,
                         node_info->role);
      return;
    }

    _nodes[_nodes_count].node_num = node_info->node_num;
    StrHelper::strncpy(_nodes[_nodes_count].long_name, node_info->long_name,
                       sizeof(_nodes[_nodes_count].long_name));
    _nodes_count++;
  } else if (progress == MT_NR_DONE) {
    _initialized = true;
    _next_node_report_time_timeout = 0;
    _next_node_report_time = _mesh->futureMillis(NODE_REPORT_PERIOD);
    MESH_DEBUG_PRINTLN("[MT Bridge] Meshtastic report OK. %d nodes", _nodes_count);
  }
}

void MeshtasticController::text_message_received(const uint32_t from_node_id, const uint32_t to_node_id,
                                                 const uint8_t channel_index, const char *text) {
  MESH_DEBUG_PRINTLN("[MT Bridge] RX text channel=%d from=!%x to=!%x payload='%s'", channel_index,
                     from_node_id, to_node_id, text);

  if (to_node_id == 0xFFFFFFFF) {
    _last_seen.node_num = from_node_id;
    snprintf(_last_seen.long_name, sizeof(_last_seen.long_name), "!%x", from_node_id);

    for (auto i = 0; i < _nodes_count; i++) {
      const auto [node_num, long_name] = _nodes[i];

      if (!node_num) {
        continue;
      }

      if (node_num == from_node_id) {
        if (strlen(_nodes[i].long_name) > 0) {
          MESH_DEBUG_PRINTLN("[MT Bridge] Matched node !%x (%s)", node_num, long_name);
          StrHelper::strncpy(_last_seen.long_name, _nodes[i].long_name, sizeof(_last_seen.long_name));
        }

        break;
      }
    }

    _mesh->send_message_to_meshcore_from_meshtastic(_last_seen.long_name, text, channel_index);
  }
}

bool MeshtasticController::send_message(uint32_t now, MeshtasticBridgeMessageToSend message_to_send) {
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
  snprintf(temp, MESHTASTIC_MAX_MESSAGE_LENGTH, "MT_%s:%s", message_to_send.sender_name,
           message_to_send.message);

  MESH_DEBUG_PRINTLN("[MT Bridge] Send to Meshtastic channel=%d payload='%s'",
                     message_to_send.meshtastic_channel_index, temp);

  if (!mt_send_text(temp, 0xFFFFFFFF, message_to_send.meshtastic_channel_index)) {
    _initialized = false;
    _next_node_report_time_timeout = 0;
    _next_node_report_time = _mesh->futureMillis(NODE_REPORT_TIMEOUT);
    MESH_DEBUG_PRINTLN("[MT Bridge] Send to Meshtastic KO. Communication error ?");
    return false;
  }

  return true;
}
