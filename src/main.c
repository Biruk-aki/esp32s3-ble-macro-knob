#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>

#define BTN_MUTE_PIN        6       /* GPIO 6 */
#define BTN_PLAY_PIN        5       /* GPIO 5 */
#define BTN_TRACK_PIN       4       /* GPIO 4 */
#define ADC_POT_CHANNEL     6       /* GPIO 7 is ADC1_CH6 on ESP32-S3 */

#define HID_KEY_NONE        0x00
#define HID_KEY_VOL_UP      (1 << 0)/* Bit 0: Vol Up (0x01) */
#define HID_KEY_VOL_DOWN    (1 << 1)/* Bit 1: Vol Down (0x02) */
#define HID_KEY_MUTE        (1 << 2)/* Bit 2: Mute (0x04) */
#define HID_KEY_PLAY_PAUSE  (1 << 3)/* Bit 3: Play/Pause (0x08) */
#define HID_KEY_NEXT_TRACK  (1 << 4)/* Bit 4: Next Track (0x10) */
#define HID_KEY_PREV_TRACK  (1 << 5)/* Bit 5: Previous Track (0x20) */

#define HOLD_THRESHOLD_MS   800
#define ADC_DEADBAND        120     /* Threshold to trigger a volume step */

static struct bt_conn *current_conn = NULL;

/* Consumer Control Report Descriptor */
static const uint8_t hid_report_map[] = {
    0x05, 0x0C,       /* Usage Page (Consumer Devices) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /*   Report ID (1) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1 bit) */
    0x95, 0x07,       /*   Report Count (7 functional bits) */
    0x09, 0xE9,       /*   Usage (Volume Increment) -> Bit 0 */
    0x09, 0xEA,       /*   Usage (Volume Decrement) -> Bit 1 */
    0x09, 0xE2,       /*   Usage (Mute) -> Bit 2 */
    0x09, 0xCD,       /*   Usage (Play/Pause) -> Bit 3 */
    0x09, 0xB5,       /*   Usage (Scan Next Track) -> Bit 4 */
    0x09, 0xB6,       /*   Usage (Scan Prev Track) -> Bit 5 */
    0x09, 0xB7,       /*   Usage (Stop) */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x75, 0x01,       /*   Report Size (1 bit) */
    0x95, 0x01,       /*   Report Count (1 bit padding) */
    0x81, 0x01,       /*   Input (Constant) */
    0xC0              /* End Collection */
};

static const uint8_t hid_info[] = { 0x01, 0x01, 0x00, 0x02 };
static uint8_t hid_protocol_mode = 0x01;
static uint8_t hid_ctrl_point;
static uint8_t hid_input_value;
static const uint8_t hid_report_ref[] = { 0x01, 0x01 };

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_report_map, sizeof(hid_report_map));
}

static ssize_t read_hid_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_info, sizeof(hid_info));
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_report_ref, sizeof(hid_report_ref));
}

static ssize_t read_protocol_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &hid_protocol_mode, sizeof(hid_protocol_mode));
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(buf);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);
    return len;
}

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_restart_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(adv_restart_work, adv_restart_handler);

static void adv_restart_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed: 0x%02x\n", err);
        return;
    }
    current_conn = bt_conn_ref(conn);
    printk(">>> CONNECTED TO PC <<<\n");
    bt_conn_set_security(conn, BT_SECURITY_L2);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected: 0x%02x\n", reason);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    k_work_reschedule(&adv_restart_work, K_MSEC(200));
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    ARG_UNUSED(conn);
    if (!err && level >= BT_SECURITY_L2) {
        printk("Security established (Level %d)\n", level);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

BT_GATT_SERVICE_DEFINE(hids_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_PROTOCOL_MODE,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_protocol_mode, NULL, &hid_protocol_mode),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_hid_info, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, write_ctrl_point, &hid_ctrl_point),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_report_map, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           NULL, NULL, &hid_input_value),

    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF,
                       BT_GATT_PERM_READ,
                       read_report_ref, NULL, NULL),
);

static void pairing_confirm(struct bt_conn *conn)
{
    bt_conn_auth_pairing_confirm(conn);
}

static void auth_cancel(struct bt_conn *conn)
{
    ARG_UNUSED(conn);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .pairing_confirm = pairing_confirm,
    .cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    ARG_UNUSED(conn);
    printk("Pairing bonded: %s\n", bonded ? "true" : "false");
}

static struct bt_conn_auth_info_cb auth_info_cb = {
    .pairing_complete = pairing_complete,
};

static int send_consumer_key(uint8_t key_mask)
{
    if (!current_conn) {
        return -ENOTCONN;
    }

    const struct bt_gatt_attr *report_attr = &hids_svc.attrs[10];
    uint8_t report = key_mask;
    int err;

    /* Press */
    err = bt_gatt_notify(current_conn, report_attr, &report, sizeof(report));
    if (err) {
        return err;
    }

    k_msleep(20);

    /* Release */
    report = HID_KEY_NONE;
    err = bt_gatt_notify(current_conn, report_attr, &report, sizeof(report));
    return err;
}

static const struct device *gpio_dev;
static const struct device *adc_dev;

static int16_t adc_raw_buf;
static struct adc_channel_cfg channel_cfg = {
    .gain = ADC_GAIN_1,
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

int main(void)
{
    k_msleep(500);
    printk("\n=== Desk Controller (Buttons + Potentiometer on GPIO 7) ===\n");

    /* 1. Init GPIOs */
    gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio_dev)) {
        printk("GPIO0 not ready\n");
        return 0;
    }
    gpio_pin_configure(gpio_dev, BTN_MUTE_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, BTN_PLAY_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, BTN_TRACK_PIN, GPIO_INPUT | GPIO_PULL_UP);

    /* 2. Init ADC on GPIO 7 (ADC1_CH6) */
    adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
    if (device_is_ready(adc_dev)) {
        adc_channel_setup(adc_dev, &channel_cfg);
        printk("ADC initialized on GPIO 7 (ADC1_CH6)\n");
    } else {
        printk("Warning: ADC0 controller not ready\n");
    }

    /* 3. Start BLE */
    bt_conn_auth_cb_register(&auth_cb_display);
    bt_conn_auth_info_cb_register(&auth_info_cb);

    if (bt_enable(NULL)) {
        printk("BT init fail\n");
        return 0;
    }

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_subsys_init();
        settings_load();
    }

    size_t id_count = 0;
    bt_id_get(NULL, &id_count);
    if (id_count == 0) {
        bt_id_create(NULL, NULL);
    }

    bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    printk("Ready and advertising...\n");

    int btn_mute_last = 1;
    int btn_play_last = 1;
    int btn_track_last = 1;
    int64_t track_press_time = 0;
    bool track_long_handled = false;

    int last_pot_val = 2048;
    if (device_is_ready(adc_dev) && adc_read(adc_dev, &sequence) == 0) {
        last_pot_val = adc_raw_buf;
    }

    while (1) {
        k_msleep(20);

        int64_t now = k_uptime_get();

        /* --- BUTTON SAMPLING --- */
        int mute_val = gpio_pin_get(gpio_dev, BTN_MUTE_PIN);
        int play_val = gpio_pin_get(gpio_dev, BTN_PLAY_PIN);
        int track_val = gpio_pin_get(gpio_dev, BTN_TRACK_PIN);

        if (mute_val == 0 && btn_mute_last == 1) {
            printk("[BUTTON] MUTE\n");
            send_consumer_key(HID_KEY_MUTE);
            k_msleep(100);
        }

        if (play_val == 0 && btn_play_last == 1) {
            printk("[BUTTON] PLAY / PAUSE\n");
            send_consumer_key(HID_KEY_PLAY_PAUSE);
            k_msleep(100);
        }

        if (track_val == 0 && btn_track_last == 1) {
            track_press_time = now;
            track_long_handled = false;
        } else if (track_val == 0 && btn_track_last == 0) {
            if (!track_long_handled && (now - track_press_time) >= HOLD_THRESHOLD_MS) {
                printk("[BUTTON] PREVIOUS TRACK\n");
                send_consumer_key(HID_KEY_PREV_TRACK);
                track_long_handled = true;
            }
        } else if (track_val == 1 && btn_track_last == 0) {
            if (!track_long_handled) {
                printk("[BUTTON] NEXT TRACK\n");
                send_consumer_key(HID_KEY_NEXT_TRACK);
            }
        }

        btn_mute_last = mute_val;
        btn_play_last = play_val;
        btn_track_last = track_val;

        /* --- POTENTIOMETER SAMPLING (GPIO 7) --- */
        if (device_is_ready(adc_dev) && current_conn) {
            if (adc_read(adc_dev, &sequence) == 0) {
                int delta = adc_raw_buf - last_pot_val;

                if (delta > ADC_DEADBAND) {
                    printk("[KNOB] Turn CW -> VOL UP (ADC: %d)\n", adc_raw_buf);
                    send_consumer_key(HID_KEY_VOL_UP);
                    last_pot_val = adc_raw_buf;
                } else if (delta < -ADC_DEADBAND) {
                    printk("[KNOB] Turn CCW -> VOL DOWN (ADC: %d)\n", adc_raw_buf);
                    send_consumer_key(HID_KEY_VOL_DOWN);
                    last_pot_val = adc_raw_buf;
                }
            }
        }
    }

    return 0;
}