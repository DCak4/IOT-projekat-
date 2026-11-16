#include "led.h" //deklaracije led_init, led_set, led_is_on

#include "driver/gpio.h" //fje za rad sa pinovima

#define LED_GPIO GPIO_NUM_25 //led je na 25

esp_err_t led_init(void)
{
    gpio_config_t config = { //podesavanje pina
        .pin_bit_mask = 1ULL << LED_GPIO, //pin se podesava
        .mode = GPIO_MODE_INPUT_OUTPUT, //input i output
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t err = gpio_config(&config); //primeni podesavanje pina

    if (err != ESP_OK) { //ako je doslo do greske pri podesavanju
        return err;
    }

    return gpio_set_level(LED_GPIO, 0); //pocetno, led iskljucen
}

esp_err_t led_set(bool on)
{
    return gpio_set_level(LED_GPIO, on ? 1 : 0); //true -svetli 3v3, false-ugaseno 0v
}

bool led_is_on(void)
{
    return gpio_get_level(LED_GPIO) == 1; //citanje nivoa sa pina, provera da li je yaista upaljen
}
