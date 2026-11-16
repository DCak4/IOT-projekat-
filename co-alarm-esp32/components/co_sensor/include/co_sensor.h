#ifndef CO_SENSOR_H
#define CO_SENSOR_H

#include "esp_err.h"

esp_err_t co_sensor_init(void);

esp_err_t co_sensor_read_mv(int *millivolts);

#endif
