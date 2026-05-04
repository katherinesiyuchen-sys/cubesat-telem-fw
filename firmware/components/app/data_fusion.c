#include "data_fusion.h"

esp_err_t data_fusion_build_telemetry_sample(const cubesat_sensor_frame_t *frame, telemetry_sample_t *sample) {
    if (frame == NULL || sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!frame->gnss.valid) {
        return ESP_ERR_INVALID_STATE;
    }

    sample->latitude_e7 = frame->gnss.latitude_e7;
    sample->longitude_e7 = frame->gnss.longitude_e7;
    sample->temperature_c_x10 = frame->temperature_c_x10;
    sample->fix_type = frame->gnss.fix_type;
    sample->satellites = frame->gnss.satellites;
    return ESP_OK;
}
