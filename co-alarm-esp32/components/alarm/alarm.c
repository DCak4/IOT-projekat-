#include "alarm.h"

#include "sdkconfig.h"

#define ALARM_DEBOUNCE_SAMPLES 3

_Static_assert(CONFIG_ALARM_TEMP_OFF_C < CONFIG_ALARM_TEMP_ON_C, //provera praga za paljenje/gasenje
               "Temperature clear threshold must be below the on threshold");
_Static_assert(CONFIG_ALARM_CO_OFF_MV < CONFIG_ALARM_CO_ON_MV,
               "CO clear threshold must be below the on threshold");

typedef struct {
    bool active; //da li je aktivan
    int samples_over; //koliko uzastopnih merenja je iznad praga za paljenje
} detector_t;

//detektori za temperaturu i co
static detector_t temperature_detector;
static detector_t co_detector;

static void detector_update(detector_t *detector, float value,
                            float on_threshold, float off_threshold)
{
    if (detector->active) { //ako je aktivan, proverava se da li je ispod praga za gasenje, onda se resetuje
        if (value < off_threshold) { 
            detector->active = false;
            detector->samples_over = 0;
        }
        return;
    }

    if (value < on_threshold) {//ako je ispod praga za paljenje, resetuje se brojac
        detector->samples_over = 0;
        return;
    }

    detector->samples_over++; 

    if (detector->samples_over >= ALARM_DEBOUNCE_SAMPLES) { //kad brojac dodje do 3 aktivira se alarm
        detector->active = true;
    }
}

void alarm_update_temperature(float celsius)
{//detekcija, pragovi
    detector_update(&temperature_detector, celsius,
                    CONFIG_ALARM_TEMP_ON_C, CONFIG_ALARM_TEMP_OFF_C);
}

void alarm_update_co(int millivolts)
{
    detector_update(&co_detector, (float)millivolts,
                    CONFIG_ALARM_CO_ON_MV, CONFIG_ALARM_CO_OFF_MV);
}

void alarm_clear_temperature(void) //clear vrednosti, kad senzor ne radi
{
    temperature_detector.active = false;
    temperature_detector.samples_over = 0;
}

alarm_state_t alarm_get_state(void) 
{
    if (!co_detector.active) { //ako je co okej alarm se ne pali
        return ALARM_STATE_NORMAL;
    }

    //ako co jeste preko praga, ako je i temp aktivira se active, inace samo co detected
    return temperature_detector.active ? ALARM_STATE_ACTIVE
                                       : ALARM_STATE_CO_DETECTED;
}
