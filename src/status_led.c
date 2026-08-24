#include "status_led.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define STATUS_LED_PIN      10       /* GPIO 10: Standard Breadboard LED */

static const struct device *gpio0_dev;
static controller_mode_t active_mode = MODE_MEDIA;
static int blink_step = 0;

static void led_worker_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(led_work, led_worker_handler);

static void led_set_state(int state)
{
    if (device_is_ready(gpio0_dev)) {
        gpio_pin_set(gpio0_dev, STATUS_LED_PIN, state);
    }
}

static void led_worker_handler(struct k_work *work)
{
    int next_delay_ms = 100;

    switch (active_mode) {
    case MODE_MEDIA:
        /* Mode 1: Solid ON */
        led_set_state(1);
        next_delay_ms = 500;
        break;

    case MODE_NAVIGATION:
        /* Mode 2: Blink twice (Flash -> Flash -> Pause) */
        if (blink_step == 0) {
            led_set_state(1);
            next_delay_ms = 120;
            blink_step++;
        } else if (blink_step == 1) {
            led_set_state(0);
            next_delay_ms = 120;
            blink_step++;
        } else if (blink_step == 2) {
            led_set_state(1);
            next_delay_ms = 120;
            blink_step++;
        } else {
            led_set_state(0);
            next_delay_ms = 1000;
            blink_step = 0;
        }
        break;

    case MODE_SYSTEM:
        /* Mode 3: Blink three times (Flash -> Flash -> Flash -> Pause) */
        if (blink_step == 0 || blink_step == 2 || blink_step == 4) {
            led_set_state(1);
            next_delay_ms = 120;
            blink_step++;
        } else if (blink_step == 1 || blink_step == 3) {
            led_set_state(0);
            next_delay_ms = 120;
            blink_step++;
        } else {
            led_set_state(0);
            next_delay_ms = 1000;
            blink_step = 0;
        }
        break;

    default:
        led_set_state(0);
        next_delay_ms = 1000;
        break;
    }

    k_work_reschedule(&led_work, K_MSEC(next_delay_ms));
}

int status_led_init(void)
{
    gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio0_dev)) {
        printk("GPIO0 device not ready\n");
        return -ENODEV;
    }

    gpio_pin_configure(gpio0_dev, STATUS_LED_PIN, GPIO_OUTPUT_INACTIVE);

    printk("Discrete Status LED Initialized on GPIO 10\n");
    status_led_set_mode(MODE_MEDIA);
    return 0;
}

void status_led_set_mode(controller_mode_t mode)
{
    active_mode = mode;
    blink_step = 0;

    k_work_reschedule(&led_work, K_NO_WAIT);
}