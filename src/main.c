#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "ble_hid.h"
#include "adc_knob.h"
#include "button_mode.h"
#include "status_led.h"

int main(void)
{
    k_msleep(500);
    printk("\n=== 3-Mode BLE Controller (With RGB Status LED) ===\n");

    status_led_init();

    if (button_mode_init() < 0) {
        printk("Button driver init failed\n");
    }

    if (adc_knob_init() < 0) {
        printk("ADC knob driver init failed\n");
    }

    if (ble_hid_init() < 0) {
        printk("BLE HID init failed\n");
    }

    printk("Ready. Initial State: [MODE 1: MEDIA (LED: BLUE)]\n");

    while (1) {
        k_msleep(20);

        button_mode_process();

        if (ble_hid_is_connected()) {
            int knob_event = adc_knob_process();
            controller_mode_t mode = button_mode_get_current();

            if (knob_event == KNOB_EVENT_CW) {
                if (mode == MODE_MEDIA) {
                    printk("[KNOB][MEDIA] VOL UP\n");
                    ble_hid_send_consumer_key(HID_KEY_VOL_UP);
                } else if (mode == MODE_NAVIGATION) {
                    printk("[KNOB][NAV] SCROLL DOWN\n");
                    ble_hid_send_mouse_scroll(-1);
                } else if (mode == MODE_SYSTEM) {
                    printk("[KNOB][SYSTEM] BRIGHTNESS UP\n");
                    ble_hid_send_consumer_key(HID_KEY_BRIGHTNESS_UP);
                }
            } else if (knob_event == KNOB_EVENT_CCW) {
                if (mode == MODE_MEDIA) {
                    printk("[KNOB][MEDIA] VOL DOWN\n");
                    ble_hid_send_consumer_key(HID_KEY_VOL_DOWN);
                } else if (mode == MODE_NAVIGATION) {
                    printk("[KNOB][NAV] SCROLL UP\n");
                    ble_hid_send_mouse_scroll(1);
                } else if (mode == MODE_SYSTEM) {
                    printk("[KNOB][SYSTEM] BRIGHTNESS DOWN\n");
                    ble_hid_send_consumer_key(HID_KEY_BRIGHTNESS_DOWN);
                }
            }
        }
    }

    return 0;
}