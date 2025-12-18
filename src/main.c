#include "Funciones.h"

LOG_MODULE_REGISTER(wearable, LOG_LEVEL_INF);

#define LED_NODE DT_ALIAS(led0)
const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

#define PWR_GPIO_NODE   DT_NODELABEL(gpio0)
#define PWR_HOLD_PIN    23
#define PWR_BTN_PIN     21

static const struct device *pwr_port = DEVICE_DT_GET(PWR_GPIO_NODE);

atomic_t adpd_error_flag    = ATOMIC_INIT(0);
atomic_t holter_done_flag   = ATOMIC_INIT(0);
atomic_t holter_active_flag = ATOMIC_INIT(0);
atomic_t tx_in_progress_flag = ATOMIC_INIT(0);

static void power_latch_init(void)
{
    if (!device_is_ready(pwr_port)) {
        return;
    }

    gpio_pin_configure(pwr_port, PWR_HOLD_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure(pwr_port, PWR_BTN_PIN, GPIO_INPUT | GPIO_PULL_UP);

}

static bool power_button_pressed(void)
{
    int val = gpio_pin_get(pwr_port, PWR_BTN_PIN);
    return (val == 0);
}

void power_off_system(void)
{
    k_msleep(200);
    gpio_pin_set(pwr_port, PWR_HOLD_PIN, 0);
    while (1) {
        k_msleep(1000);
    }
}

static void init_led(void)
{
    if (!device_is_ready(led.port)) {
        return;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

int main(void)
{

    power_latch_init();
    init_led();
    init_spi_flash();
    init_i2c();

    int adpd_err = adpd6000_init_config();
    if (adpd_err) {
    }

    if (ble_init_stack() == 0) {
        ble_start_adv();
    }

    uint32_t led_slow_acc_ms  = 0;
    uint32_t holter_done_ms   = 0;
    uint32_t btn_press_ms     = 0;
    bool     btn_long_armed   = false;

    while (1) {
        k_msleep(100);

        bool error_afe   = atomic_get(&adpd_error_flag);
        bool holter_done = atomic_get(&holter_done_flag);
        bool holter_act  = atomic_get(&holter_active_flag);
        bool tx_active   = atomic_get(&tx_in_progress_flag);

        if (error_afe) {
            gpio_pin_set_dt(&led, 1);
            holter_done_ms = 0;
        } else if (holter_done) {
            gpio_pin_toggle_dt(&led);

            if (!tx_active) {
                holter_done_ms += 100;
                if (holter_done_ms >= 60000u) {
                    power_off_system();
                }
            } else {
                holter_done_ms = 0;
            }
        } else {
            led_slow_acc_ms += 100;
            if (led_slow_acc_ms >= 500u) {
                gpio_pin_toggle_dt(&led);
                led_slow_acc_ms = 0;
            }
            holter_done_ms = 0;
        }

        if (!holter_act) {
            if (power_button_pressed()) {
                if (btn_press_ms < 10000u) {
                    btn_press_ms += 100;
                }
                if (btn_press_ms >= 10000u) {
                    btn_long_armed = true;
                }
            } else {
                if (btn_long_armed) {
                    power_off_system();
                }
                btn_press_ms   = 0;
                btn_long_armed = false;
            }
        } else {
            btn_press_ms   = 0;
            btn_long_armed = false;
        }
    }

    return 0;
}