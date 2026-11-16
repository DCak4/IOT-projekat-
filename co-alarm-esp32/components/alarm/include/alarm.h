#ifndef ALARM_H
#define ALARM_H

#include <stdbool.h>

typedef enum {
    ALARM_STATE_NORMAL,
    ALARM_STATE_CO_DETECTED, // CO  preko granice, temp ne
    ALARM_STATE_ACTIVE       // CO i temp preko granice, alarm aktivan 
} alarm_state_t;

void alarm_update_temperature(float celsius);
void alarm_update_co(int millivolts);

void alarm_clear_temperature(void); //kad temp nije pouzdana

alarm_state_t alarm_get_state(void);//vraca trenutno stanje

#endif
