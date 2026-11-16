#include "co_sensor.h"

#include <stddef.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define CO_ADC_UNIT     ADC_UNIT_1 //adc1, adc2 ne moze da se koristi sa wifi
#define CO_ADC_CHANNEL  ADC_CHANNEL_6 //kanal 6, pin 34
#define CO_ADC_ATTEN    ADC_ATTEN_DB_12 
#define CO_ADC_BITWIDTH ADC_BITWIDTH_DEFAULT
#define CO_SAMPLE_COUNT 16

static const char *TAG = "CO_SENSOR"; 

//da li je inicijalizovan adc i kalibracija
static adc_oneshot_unit_handle_t adc_handle = NULL; // za adc
static adc_cali_handle_t cali_handle = NULL; //za kalibraciju adc-a


esp_err_t co_sensor_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_config = { 
        .unit_id = CO_ADC_UNIT
    };

    esp_err_t err = adc_oneshot_new_unit(&unit_config, &adc_handle); //inicijalizacija adc-a

    if (err != ESP_OK) { //ako dodje do gresle
        ESP_LOGE(TAG, "Greska pri inicijalizaciji ADC-a: %s",
                 esp_err_to_name(err));
        return err;
    }

    //podesavanje kanala adc-a
    adc_oneshot_chan_cfg_t channel_config = { 
        .atten = CO_ADC_ATTEN,
        .bitwidth = CO_ADC_BITWIDTH
    };

    err = adc_oneshot_config_channel(adc_handle, CO_ADC_CHANNEL,
                                     &channel_config);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Greska pri konfiguraciji ADC kanala: %s",
                 esp_err_to_name(err));
        return err;
    }

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = CO_ADC_UNIT,
        .atten = CO_ADC_ATTEN,
        .bitwidth = CO_ADC_BITWIDTH,
        .default_vref = 1100
    };

    err = adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle); //kalibracija adc-a, radi tacnog citanja
 
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Greska pri kalibraciji ADC-a: %s",
                 esp_err_to_name(err));
    }

    return err;
}

esp_err_t co_sensor_read_mv(int *millivolts)
{
    if (millivolts == NULL) { 
        return ESP_ERR_INVALID_ARG;
    }

    if (adc_handle == NULL || cali_handle == NULL) { //neuspesan init
        return ESP_ERR_INVALID_STATE;
    }

    int sum = 0;

    for (int i = 0; i < CO_SAMPLE_COUNT; i++) {
        int raw;
        esp_err_t err = adc_oneshot_read(adc_handle, CO_ADC_CHANNEL, &raw);

        if (err != ESP_OK) {
            return err;
        }

        sum += raw; //sabiranje 16 uzoraka koje smo odredili, pa njihov prosek
    }

    return adc_cali_raw_to_voltage(cali_handle, sum / CO_SAMPLE_COUNT,
                                   millivolts);
}
