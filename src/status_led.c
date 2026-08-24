#include "status_led.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define WS2812_PIN 16  /* GPIO 48 = Port 1, Pin 16 (32 + 16 = 48) */

static const struct device *gpio1_dev;

/* Cycle delay using Xtensa CCOUNT register (ESP32-S3 running at 240MHz: 1us = 240 cycles) */
static inline void delay_cycles(uint32_t cycles)
{
    uint32_t start;
    __asm__ volatile("rsr.ccount %0" : "=a"(start));
    while (1) {
        uint32_t current;
        __asm__ volatile("rsr.ccount %0" : "=a"(current));
        if ((current - start) >= cycles) {
            break;
        }
    }
}

/* Transmit 24-bit GRB to WS2812 */
static void ws2812_send_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;

    unsigned int key = irq_lock();

    for (int i = 23; i >= 0; i--) {
        if (grb & (1 << i)) {
            /* Bit 1: High for ~800ns (190 cycles), Low for ~450ns (100 cycles) */
            gpio_pin_set(gpio1_dev, WS2812_PIN, 1);
            delay_cycles(190);
            gpio_pin_set(gpio1_dev, WS2812_PIN, 0);
            delay_cycles(100);
        } else {
            /* Bit 0: High for ~350ns (80 cycles), Low for ~900ns (210 cycles) */
            gpio_pin_set(gpio1_dev, WS2812_PIN, 1);
            delay_cycles(80);
            gpio_pin_set(gpio1_dev, WS2812_PIN, 0);
            delay_cycles(210);
        }
    }

    irq_unlock(key);

    /* Latch / Reset pulse (>50us) */
    k_busy_wait(60);
}

int status_led_init(void)
{
    gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    if (!device_is_ready(gpio1_dev)) {
        printk("GPIO1 device not ready for WS2812\n");
        return -ENODEV;
    }

    gpio_pin_configure(gpio1_dev, WS2812_PIN, GPIO_OUTPUT_INACTIVE);

    printk("WS2812 Bitbang LED initialized on GPIO 48\n");
    status_led_set_mode(MODE_MEDIA);
    return 0;
}

void status_led_set_mode(controller_mode_t mode)
{
    if (!device_is_ready(gpio1_dev)) {
        return;
    }

    switch (mode) {
    case MODE_MEDIA:
        /* Mode 1: Blue (R=0, G=0, B=30) */
        ws2812_send_color(0, 0, 30);
        break;

    case MODE_NAVIGATION:
        /* Mode 2: Green (R=0, G=30, B=0) */
        ws2812_send_color(0, 30, 0);
        break;

    case MODE_SYSTEM:
        /* Mode 3: Magenta (R=30, G=0, B=30) */
        ws2812_send_color(30, 0, 30);
        break;

    default:
        ws2812_send_color(0, 0, 0);
        break;
    }
}