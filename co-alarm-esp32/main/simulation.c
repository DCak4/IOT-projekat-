#include "simulation.h"

#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "alarm.h"
#include "display.h"
#include "led.h"

#define SIM_SAMPLE_MS      1000 
#define SIM_ROUND_PAUSE_MS 5000

//pragovi za simulaciju, da li je temperatura i co ispod ili iznad praga za paljenje/gasenje
#define T_LOW   ((float)CONFIG_ALARM_TEMP_OFF_C - 10.0f)
#define T_MID   (((float)CONFIG_ALARM_TEMP_ON_C + (float)CONFIG_ALARM_TEMP_OFF_C) / 2.0f)
#define T_HIGH  ((float)CONFIG_ALARM_TEMP_ON_C + 3.0f)
#define CO_LOW  (CONFIG_ALARM_CO_OFF_MV / 2)
#define CO_MID  ((CONFIG_ALARM_CO_ON_MV + CONFIG_ALARM_CO_OFF_MV) / 2)
#define CO_HIGH (CONFIG_ALARM_CO_ON_MV + 200)

#define NEVER (-1)

//korak simulacije
typedef struct {
    const char *name;
    float temperature;
    int co_mv;
    bool dht_failed;   
    int samples;
    int led_on_from;   
} sim_step_t;

static const sim_step_t steps[] = {
    { "Normalno",                T_LOW,  CO_LOW,  false, 4, NEVER },
    { "Samo temperatura",        T_HIGH, CO_LOW,  false, 5, NEVER },
    { "Samo CO",                 T_LOW,  CO_HIGH, false, 5, NEVER },
    { "CO + temperatura",        T_HIGH, CO_HIGH, false, 5, 2     },
    { "Izmedju",                 T_MID,  CO_MID,  false, 4, 0     },
    { "Pad ispod praga",         T_LOW,  CO_LOW,  false, 3, NEVER },
    { "Kratak skok (2 uzorka)",  T_HIGH, CO_HIGH, false, 2, NEVER },
    { "Normalno posle skoka",    T_LOW,  CO_LOW,  false, 2, NEVER },
    { "Ponovo oba visoka",       T_HIGH, CO_HIGH, false, 4, 2     },
    { "Greska DHT11",            T_HIGH, CO_HIGH, true,  3, NEVER },
    { "Kraj: normalno",          T_LOW,  CO_LOW,  false, 3, NEVER },
};

#define STEP_COUNT ((int)(sizeof(steps) / sizeof(steps[0]))) //br koraka = ceo niz/velicina jednog koraka

static const char *TAG = "SIM";

static const char *state_name(alarm_state_t state) //stanje kao broj prevodi u tekst
{
    switch (state) {
    case ALARM_STATE_NORMAL:      return "NORMAL";
    case ALARM_STATE_CO_DETECTED: return "CO_DETECTED";
    case ALARM_STATE_ACTIVE:      return "ACTIVE";
    default:                      return "?";
    }
}

//vraca se na pocetne vrednosti, brise se stanje alarma i led se gasi
static void reset_alarm(void)
{
    alarm_clear_temperature();
    alarm_update_co(0);
    led_set(false);
}

static void show(QueueHandle_t queue, const sim_step_t *step, int step_no,
                 bool led_ok)
{
    if (queue == NULL) { //ako nema ekrana nista
        return;
    }

    display_view_t view = { //sprema podatke za prikaz 
        .temperature = step->temperature,
        .humidity = 0.0f,
        .dht_valid = !step->dht_failed,
        .co_mv = step->co_mv,
    };

    snprintf(view.status, sizeof(view.status), "SIM %d/%d %s", 
             step_no % 100, STEP_COUNT % 100, led_ok ? "OK" : "FAIL");
    xQueueOverwrite(queue, &view);
}

//broj palih ukupnih provera
static int run_round(QueueHandle_t queue, int round)
{
    int failures = 0;
    int checks = 0;
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "===== Runda %d: %d koraka =====", round, STEP_COUNT);//u monitoru se ispisuje redni broj runde i koliko koraka ima

    for (int s = 0; s < STEP_COUNT; s++) { //svi testovi/simulacije
        const sim_step_t *step = &steps[s]; //trenutni korak simulacije
        int step_failures = 0;

        ESP_LOGI(TAG, "--- Korak %d/%d: %s (T=%.1f C, CO=%d mV%s)", //ispis
                 s + 1, STEP_COUNT, step->name, (double)step->temperature,
                 step->co_mv, step->dht_failed ? ", DHT11 ne radi" : "");

        for (int i = 0; i < step->samples; i++) { //za svaki uzorak u koraku simulacije
            if (step->dht_failed) {
                alarm_clear_temperature(); //uklanja se temp usled kvara dht11
            } else {
                alarm_update_temperature(step->temperature);
            }
            alarm_update_co(step->co_mv);//salje se co 

            alarm_state_t state = alarm_get_state();
            led_set(state == ALARM_STATE_ACTIVE); 

            bool expected = step->led_on_from != NEVER && i >= step->led_on_from;
            bool actual = led_is_on();
            bool ok = expected == actual; // da li je led upaljen kad je ocekivano

            checks++; //br provera
            if (!ok) {
                failures++; //br gresaka, ako je do greske doslo
                step_failures++;
            }

            //ispis
            ESP_LOGI(TAG, "  uzorak %d: stanje=%-11s LED ocekivano=%-3s pin=%-3s %s",
                     i + 1, state_name(state), expected ? "ON" : "OFF",
                     actual ? "ON" : "OFF", ok ? "PASS" : "<<< FAIL");

            show(queue, step, s + 1, ok); //prikaz na ekranu
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SIM_SAMPLE_MS)); //cekanje
        }

        if (step_failures == 0) { //rezultat koraka simulacije
            ESP_LOGI(TAG, "Korak %d: PASS", s + 1);
        } else {
            ESP_LOGE(TAG, "Korak %d: FAIL (%d gresaka)", s + 1, step_failures);
        }
    }

    if (failures == 0) { //rezultat runde simulacije
        ESP_LOGI(TAG, "===== Runda %d: SVE PROSLO (%d/%d provera) =====",
                 round, checks, checks);
    } else {
        ESP_LOGE(TAG, "===== Runda %d: %d od %d provera PALO =====",
                 round, failures, checks);
    }

    return failures; //vraca br gresaka
}

void simulation_task(void *pvParameters)
{
    QueueHandle_t queue = (QueueHandle_t)pvParameters;

    ESP_LOGW(TAG, "TEST SIMULACIJA - pravi senzori se NE citaju");
    ESP_LOGI(TAG, "Pragovi: T on/off = %d/%d C, CO on/off = %d/%d mV",
             CONFIG_ALARM_TEMP_ON_C, CONFIG_ALARM_TEMP_OFF_C,
             CONFIG_ALARM_CO_ON_MV, CONFIG_ALARM_CO_OFF_MV);
    ESP_LOGI(TAG, "Test vrednosti: T low/mid/high = %.1f/%.1f/%.1f C, "
             "CO low/mid/high = %d/%d/%d mV",
             (double)T_LOW, (double)T_MID, (double)T_HIGH,
             CO_LOW, CO_MID, CO_HIGH);
    ESP_LOGI(TAG, "LED (GPIO25) treba da svetli SAMO u koracima "
             "\"CO + temperatura\", \"Izmedju\" i \"Ponovo oba visoka\".");

    for (int round = 1;; round++) { //beskonacna petlja, izvrsavanje svih test primera
        reset_alarm();
        run_round(queue, round);
        vTaskDelay(pdMS_TO_TICKS(SIM_ROUND_PAUSE_MS));
    }
}
