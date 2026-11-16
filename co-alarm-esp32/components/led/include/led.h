#ifndef LED_H
#define LED_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t led_init(void);
esp_err_t led_set(bool on);

bool led_is_on(void);

#endif
