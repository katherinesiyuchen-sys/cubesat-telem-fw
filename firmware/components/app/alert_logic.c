#include "alert_logic.h"

cubesat_alert_result_t alert_logic_evaluate(const cubesat_alert_input_t *input) {
    if (input == NULL) {
        return (cubesat_alert_result_t){ CUBESAT_ALERT_WARN, "alert input missing" };
    }

    if (input->replay_rejected) {
        return (cubesat_alert_result_t){ CUBESAT_ALERT_CRITICAL, "replay rejected" };
    }
    if (input->fix_type == 0 || input->satellites < 4) {
        return (cubesat_alert_result_t){ CUBESAT_ALERT_WARN, "gnss degraded" };
    }
    if (input->temperature_c_x10 > 650 || input->temperature_c_x10 < -200) {
        return (cubesat_alert_result_t){ CUBESAT_ALERT_WARN, "temperature outside demo envelope" };
    }
    if (input->link_margin_db_x10 < 30) {
        return (cubesat_alert_result_t){ CUBESAT_ALERT_WARN, "link margin low" };
    }

    return (cubesat_alert_result_t){ CUBESAT_ALERT_NONE, "nominal" };
}
