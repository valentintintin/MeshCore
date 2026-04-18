#include "MeshtasticController.h"

#include "MeshCore.h"

MeshtasticController *MeshtasticController::instance = nullptr;

MeshtasticController::MeshtasticController(mesh::MillisecondClock &ms) : ms(ms) {
}

void MeshtasticController::loop() {
  const auto now = millis();
  const auto can_send = mt_loop(now);

  if (can_send) {
    if (now - _next_node_report_time > NODE_REPORT_PERIOD) {
      request_node_report();
      _next_node_report_time = millis();
    } else if (_message_to_send.next_time_to_send > 0 && now > _message_to_send.next_time_to_send) {
      send_to_meshtastic();
    }
  }
}

void MeshtasticController::node_report_callback(mt_node_t *nodeinfo, mt_nr_progress_t progress) {
  instance->add_node_info(nodeinfo, progress);
}

void MeshtasticController::text_message_callback(uint32_t from, uint32_t to, uint8_t channel,
                                                       const char *text) {
  instance->text_message_received(from, to, channel, text);
}

void MeshtasticController::add_message_to_queue(const char *send_name, const char *text, const uint16_t tx_delay) {
  snprintf(_message_to_send.message, MESHTASTIC_MAX_MESSAGE_LENGTH, "%s_%s", send_name, text);
  _message_to_send.next_time_to_send = ms.getMillis() + tx_delay;

  MESH_DEBUG_PRINTLN("Bridge MT want to send to MT %s", _message_to_send.message);
}

bool MeshtasticController::send_to_meshtastic() {
  if (has_message_to_send) {
    has_message_to_send = false;

    MESH_DEBUG_PRINTLN("Bridge MT send to MT: %s", message_to_send);

    return mt_send_text(message_to_send);
  }

  return false;
}