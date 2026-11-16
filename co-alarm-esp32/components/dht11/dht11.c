#include "dht11.h"
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define DHT11_GPIO              GPIO_NUM_4 //signal na pinu 4
#define DHT11_START_LOW_US      20000 //pocetni napon na pinu 20ms
#define DHT11_TIMEOUT_US        120 //max cekanje na promenu nivoa
#define DHT11_BIT_THRESHOLD_US  45 //granica, ako je trajanje visokog nivoa > 45, to je 1, inace 0

static bool initialized = false; //da li je pozvan 

static bool wait_for_level(int level, uint32_t timeout_us)
{
    int64_t start_time = esp_timer_get_time(); //trenutno vreme

    while (gpio_get_level(DHT11_GPIO) != level) { //dok pin nije na trazenom nivou
        if ((esp_timer_get_time() - start_time) >= timeout_us) { //ako prodje vreme cekanja, false
            return false;
        }
    }

    return true;
}


esp_err_t dht11_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << DHT11_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD, //moze io
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t err = gpio_config(&config); //podesavanje pina

    if (err != ESP_OK) { // da li je uspesno
        return err;
    }

    gpio_set_level(DHT11_GPIO, 1); //pocetno stanje 1

    initialized = true; //jeste inicijalizovan  

    return ESP_OK;
}

static esp_err_t read_byte(uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t result = 0;

    for (int i = 0; i < 8; i++) {

        if (!wait_for_level(1, DHT11_TIMEOUT_US)) { //cekaj da pin bude 1, ako ne, vrati gresku
            return ESP_ERR_TIMEOUT;
        }

        int64_t high_start = esp_timer_get_time(); //trenutno vreme, pocetak 1

        if (!wait_for_level(0, DHT11_TIMEOUT_US)) { //cekaj da pin bude 0, ako ne, vrati gresku
            return ESP_ERR_TIMEOUT;
        }

        int64_t high_duration =
            esp_timer_get_time() - high_start; //koliko je 1 trajala

        result <<= 1; //shift bitova ulevo, za dodavanje novog

        if (high_duration > DHT11_BIT_THRESHOLD_US) { //duze 45 mikros, 1 inace 0
            result |= 1;
        }
    }

    *value = result;//upisivanje rezultata 

    return ESP_OK;
}


static esp_err_t read_raw_data(uint8_t data[5])
{
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT_OD); //preuzima liniju
    gpio_set_level(DHT11_GPIO, 0); //postavlja na 0

    esp_rom_delay_us(DHT11_START_LOW_US); // drzi 20ms

    gpio_set_level(DHT11_GPIO, 1); //pusta liniju na 1
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT); // ceka odgovor senzora

    if (!wait_for_level(0, DHT11_TIMEOUT_US)) {
        return ESP_ERR_TIMEOUT;
    }

    if (!wait_for_level(1, DHT11_TIMEOUT_US)) {
        return ESP_ERR_TIMEOUT;
    }

    if (!wait_for_level(0, DHT11_TIMEOUT_US)) { //pocinje slanje podataka
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i < 5; i++) { //citanje 5 bajtova podataka
        esp_err_t err = read_byte(&data[i]);

        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}


esp_err_t dht11_read(float *temperature, float *humidity)
{
    if (temperature == NULL || humidity == NULL) { //nema gde da upise podatke, vraca gresku
        return ESP_ERR_INVALID_ARG; 
    }

    if (!initialized) { //nije inicijalizovan, vraca gresku
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[5] = {0};

    esp_err_t err = read_raw_data(data);

    if (err != ESP_OK) {
        return err;
    }

    uint8_t checksum =
        (uint8_t)(data[0] + data[1] + data[2] + data[3]); //zbir prvih 4 bajta, mora biti jednak 5. bajtu, inace greska

    if (checksum != data[4]) { 
        return ESP_ERR_INVALID_CRC;
    }

    *humidity = //vlaga
        (float)data[0] + ((float)data[1] / 10.0f);

    *temperature = //temp
        (float)data[2] + ((float)data[3] / 10.0f);

    return ESP_OK;
}