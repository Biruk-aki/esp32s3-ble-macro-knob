#include "adc_knob.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/adc.h>

#define ADC_POT_CHANNEL     6       /* GPIO 7 is ADC1_CH6 on ESP32-S3 */
#define ADC_STEP_THRESHOLD  50      /* Step sensitivity */

static const struct device *adc_dev;
static int16_t adc_raw_buf;
static int32_t ema_pot_val = 2048;
static int32_t last_sent_val = 2048;

static struct adc_channel_cfg channel_cfg = {
    .gain = ADC_GAIN_1_4,           /* Full 3.3V range attenuation */
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = ADC_POT_CHANNEL,
};

static struct adc_sequence sequence = {
    .channels = BIT(ADC_POT_CHANNEL),
    .buffer = &adc_raw_buf,
    .buffer_size = sizeof(adc_raw_buf),
    .resolution = 12,
};

int adc_knob_init(void)
{
    adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
    if (!device_is_ready(adc_dev)) {
        printk("ADC device not ready\n");
        return -ENODEV;
    }

    int err = adc_channel_setup(adc_dev, &channel_cfg);
    if (err) {
        printk("ADC setup failed: %d\n", err);
        return err;
    }

    if (adc_read(adc_dev, &sequence) == 0) {
        ema_pot_val = adc_raw_buf;
        last_sent_val = adc_raw_buf;
    }

    printk("ADC initialized on GPIO 7 with 12dB full 3.3V attenuation\n");
    return 0;
}

int adc_knob_process(void)
{
    if (!device_is_ready(adc_dev)) {
        return KNOB_EVENT_NONE;
    }

    if (adc_read(adc_dev, &sequence) == 0) {
        /* EMA Smoothing */
        ema_pot_val = (adc_raw_buf + (3 * ema_pot_val)) / 4;
        int delta = ema_pot_val - last_sent_val;

        if (delta >= ADC_STEP_THRESHOLD) {
            last_sent_val = ema_pot_val;
            return KNOB_EVENT_CW;
        } else if (delta <= -ADC_STEP_THRESHOLD) {
            last_sent_val = ema_pot_val;
            return KNOB_EVENT_CCW;
        }
    }

    return KNOB_EVENT_NONE;
}