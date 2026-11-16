#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include "esp_err.h"

/* 16 characters so that a 16x2 LCD could show the same status. */
#define DISPLAY_STATUS_MAX_LEN 16

typedef struct {
    float temperature;
    float humidity;
    bool dht_valid;
    int co_mv;
    char status[DISPLAY_STATUS_MAX_LEN + 1];
} display_view_t;

esp_err_t display_init(void);
esp_err_t display_show(const display_view_t *view);

#endif
