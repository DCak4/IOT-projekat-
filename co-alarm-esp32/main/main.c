#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "sdkconfig.h"

#include "dht11.h"
#include "co_sensor.h"
#include "led.h"
#include "display.h"
#include "alarm.h"
#include "simulation.h"

#define SENSOR_PERIOD_MS 1000 //trajanje jednog ciklusa merenja senzora 1s
#define DHT_READ_EVERY_N_CYCLES 5 //dht11 na svakih 5 ciklusa/5s
#define DHT_MAX_FAILED_READS 3 //neuspesna citanja posle toga se prikazuje greska
#define DHT_POWER_UP_DELAY_MS 1000 //nakon pokretanja ceka se 1s za stabilizaciju dht

static const char *TAG = "MAIN";

static QueueHandle_t display_queue = NULL;

#ifndef CONFIG_ALARM_SIMULATION
static void update_dht(display_view_t *view, int *failed_reads)//podaci za ekran i brojac neuspesnih citanja
{
    //lokalne promenljive za citanje senzora
    float temperature;
    float humidity;

    esp_err_t err = dht11_read(&temperature, &humidity); //citanje, upisuje se na navedene adrese

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG,
                 "Temperatura: %.1f C | Vlaznost: %.1f %%", //pise na monitoru
                 (double)temperature,
                 (double)humidity);

        *failed_reads = 0; //reset neuspesnih citanja
        view->temperature = temperature; //vrednosti za prikaz na ekranu
        view->humidity = humidity;
        view->dht_valid = true;
        alarm_update_temperature(temperature); //prijava temp alarmu
        return;
    }

    ESP_LOGE(TAG, //ako je doslo do greske, ispisi na monitor
             "Greska pri citanju DHT11 senzora: %s",
             esp_err_to_name(err));

    (*failed_reads)++; //brojac neuspesnih citanja

    if (*failed_reads >= DHT_MAX_FAILED_READS) //tri uzastopna neuspesna citanja
    {
        view->dht_valid = false;
        alarm_clear_temperature(); //temp se uklanja, neispravno citanje
    }
}

static void set_status(display_view_t *view, alarm_state_t state, int warmup_left_s)
{
    if (warmup_left_s > 0) //dok traje zagrevanje, prikazuje se odbrojavanje
    {
        snprintf(view->status, sizeof(view->status), "Zagrevanje %d s", warmup_left_s % 1000);
    }
    else if (state == ALARM_STATE_ACTIVE) //co i temperatura iznad praga, alarm
    {
        snprintf(view->status, sizeof(view->status), "ALARM: CO + TEMP");
    }
    else if (state == ALARM_STATE_CO_DETECTED) //ako je detektovan co, prikazuje se
    {
        snprintf(view->status, sizeof(view->status), "CO detektovan");
    }
    else if (!view->dht_valid) //ako dht11 ne radi, prikazuje se greska
    {
        snprintf(view->status, sizeof(view->status), "Greska DHT11");
    }
    else //ako je sve okej, prikazuje se ok
    {
        snprintf(view->status, sizeof(view->status), "OK");
    }
}

static void sensor_task(void *pvParameters)
{
    display_view_t view = {0}; //pocetne vrednosti za prikaz na ekranu,0
    alarm_state_t previous_state = ALARM_STATE_NORMAL; //predhodno stanje alarma, za proveru da li je doslo do promene stanja
    int failed_reads = 0; 
    int warmup_left_s = CONFIG_ALARM_CO_WARMUP_S;
    uint32_t cycle = 0; //redni br ciklusa

    TickType_t last_wake_time = xTaskGetTickCount();

    while (1)
    {
        bool dht_cycle = (cycle % DHT_READ_EVERY_N_CYCLES) == 0; //svaki 5. za dht

        if (dht_cycle) //samo tad se cita vrednost
        {
            update_dht(&view, &failed_reads);
        }

        esp_err_t err = co_sensor_read_mv(&view.co_mv);  //merenje co svake s

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Greska pri citanju CO senzora: %s", esp_err_to_name(err)); //neuspesno merenje
        }
        else
        {
            if (dht_cycle) //ispis na svakih 5, kao i dht11
            {
                ESP_LOGI(TAG, "CO: %d mV", view.co_mv);
            }

            if (warmup_left_s == 0) //co se prijavljuje samo nakon zavrsenog zagrevanja
            {
                alarm_update_co(view.co_mv);
            }
        }

        alarm_state_t state = alarm_get_state();//trenutno stanje alarma

        if (state != previous_state) //ako je doslo do promene
        {
            ESP_LOGW(TAG, "Promena stanja alarma: %d -> %d", previous_state, state);
            previous_state = state;
        }

        led_set(state == ALARM_STATE_ACTIVE); //u zavisnosti od stanja alarma, led se pali ili gasi

        if (display_queue != NULL) //ako postoji ekran ispisuje se status
        {
            set_status(&view, state, warmup_left_s);
            xQueueOverwrite(display_queue, &view);//pregazi prethodno
        }

        if (warmup_left_s > 0) //ako je zagrevanje u toku smanjuje se odbrojavanje
        {
            warmup_left_s--;
        }

        cycle++;
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SENSOR_PERIOD_MS)); //spavanje dok ne prodje 1s, trajanje jednog ciklusa
    }
}

#endif 

static void display_task(void *pvParameters)//spava dok ne dobije podatke za prikaz, kad dobije, prikazuje ih
{
    display_view_t view; 

    while (1)
    {
        if (xQueueReceive(display_queue, &view, portMAX_DELAY) == pdTRUE)
        {
            esp_err_t err = display_show(&view);

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Greska pri prikazu: %s", esp_err_to_name(err));
            }
        }
    }
}

static void start_display(void)
{
    if (display_init() != ESP_OK) //ako ne radi displej i dalje nastavlja rad, samo se ispisuje upozorenje, ne gresku
    {
        ESP_LOGW(TAG, "Displej nije dostupan. Aplikacija nastavlja rad.");
        return;
    }

    display_queue = xQueueCreate(1, sizeof(display_view_t));

    if (display_queue == NULL)
    {
        ESP_LOGE(TAG, "Ne mogu da kreiram red za displej.");
        return;
    }

    if (xTaskCreate(display_task, "display_task", 4096, NULL, 3, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Ne mogu da kreiram display_task.");
        vQueueDelete(display_queue);
        display_queue = NULL;
    }
}

#ifdef CONFIG_ALARM_SIMULATION
void app_main(void) //samo za simulaciju
{
    ESP_ERROR_CHECK(led_init());//proverava init led-a

    start_display(); //ako radi/postoji pokrece displej

    if (xTaskCreate(simulation_task, "simulation_task", 4096, //pokrece simulaciju
                    display_queue, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Ne mogu da kreiram simulation_task.");
    }
}
#else
void app_main(void) //main inace, van simulacije
{
    //provera ispravnosti senzora i led-a
    ESP_ERROR_CHECK(dht11_init());
    ESP_ERROR_CHECK(co_sensor_init());
    ESP_ERROR_CHECK(led_init());

    ESP_LOGI(TAG, "Senzori i LED su inicijalizovani.");

    start_display(); 

    //ceka se 1s da dht11 stabilizuje merenja
    vTaskDelay(pdMS_TO_TICKS(DHT_POWER_UP_DELAY_MS));

    if (xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL) != pdPASS) // pokrece se zadatak za citanje senzora
    {
        ESP_LOGE(TAG, "Ne mogu da kreiram sensor_task.");
        return;
    }

}
#endif
