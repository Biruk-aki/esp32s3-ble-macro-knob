#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

/* Standard HID Consumer Control Report Descriptor */
static const uint8_t hid_report_map[] = {
    0x05, 0x0C,       /* Usage Page (Consumer Devices) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /*   Report ID (1) */
    0x15, 0x00,       /*   Logical Minimum (0 = released) */
    0x25, 0x01,       /*   Logical Maximum (1 = pressed) */
    0x75, 0x01,       /*   Report Size (1 bit per button) */
    0x95, 0x07,       /*   Report Count (7 functional buttons) */
    0x09, 0xE9,       /*   Usage (Volume Increment) -> Bit 0 */
    0x09, 0xEA,       /*   Usage (Volume Decrement) -> Bit 1 */
    0x09, 0xE2,       /*   Usage (Mute)             -> Bit 2 */
    0x09, 0xCD,       /*   Usage (Play/Pause)        -> Bit 3 */
    0x09, 0xB5,       /*   Usage (Scan Next Track)  -> Bit 4 */
    0x09, 0xB6,       /*   Usage (Scan Prev Track)  -> Bit 5 */
    0x09, 0xB7,       /*   Usage (Stop)             -> Bit 6 */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x75, 0x01,       /*   Report Size (1 bit) */
    0x95, 0x01,       /*   Report Count (1 bit padding) */
    0x81, 0x01,       /*   Input (Constant) -> Bit 7 padding */
    0xC0              /* End Collection */
};

/* Basic HID metadata variables */
static const uint8_t hid_info[] = { 0x01, 0x01, 0x00, 0x02 }; /* Version 1.1, country 0, normally connectable */
static uint8_t hid_protocol_mode = 0x01;                       /* 1 = Report Protocol */
static uint8_t hid_ctrl_point;
static uint8_t hid_input_value;
static const uint8_t hid_report_ref[] = { 0x01, 0x01 };        /* Report ID: 1, Type: 1 (Input) */

/* Callback: Windows reads the Report Map (Dictionary) */
static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_report_map, sizeof(hid_report_map));
}

/* Callback: Windows reads basic HID information */
static ssize_t read_hid_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_info, sizeof(hid_info));
}

/* Callback: Windows checks Report Reference ID */
static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_report_ref, sizeof(hid_report_ref));
}

/* Callback: Windows reads protocol mode */
static ssize_t read_protocol_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &hid_protocol_mode, sizeof(hid_protocol_mode));
}

/* Callback: Windows writes control point (e.g. host sleep/wake) */
static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    return len;
}


// advertizing data
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

//CONNECTION CALLBACKS
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed (err 0x%02x)\n", err);
        return;
    }

    printk("Connected to PC!\n");

    /* Request Security Level 2 (Encryption) so Windows doesn't drop the connection */
    int sec_err = bt_conn_set_security(conn, BT_SECURITY_L2);
    if (sec_err) {
        printk("Failed to set security (err %d)\n", sec_err);
    }
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (reason 0x%02x)\n", reason);

    /* Re-start advertising after disconnect */
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Failed to restart advertising (err %d)\n", err);
    } else {
        printk("Advertising restarted\n");
    }
}
/* defines what happens when the PC asks to pair */
static void auth_cancel(struct bt_conn *conn)
{
    printk("Pairing cancelled\n");
}

/*is the "Just Works" configuration (no passkeys, just connect) */
static struct bt_conn_auth_cb auth_cb_display = {
    .cancel = auth_cancel,
};
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    if (!err) {
        printk("Security established: Level %d\n", level);
    } else {
        printk("Security failed: err %d\n", err);
    }
}
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};
//declare GATT services and characteristics
BT_GATT_SERVICE_DEFINE(hids_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),

    /* 1. Protocol Mode Characteristic */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_PROTOCOL_MODE,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                           read_protocol_mode, NULL, &hid_protocol_mode),

    /* 2. HID Info Characteristic */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ_ENCRYPT,
                           read_hid_info, NULL, NULL),

    /* 3. HID Control Point Characteristic */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT,
                           NULL, write_ctrl_point, &hid_ctrl_point),

    /* 4. Report Map Characteristic (The Dictionary) */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ_ENCRYPT,
                           read_report_map, NULL, NULL),

    /* 5. Input Report Characteristic (The Notification Mailbox) */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT,
                           NULL, NULL, &hid_input_value),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF,
                       BT_GATT_PERM_READ_ENCRYPT,
                       read_report_ref, NULL, NULL),
);


// BLE ready callback
static void bt_ready(int err)
{
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");

    //regster security callbacks
    bt_conn_auth_cb_register(&auth_cb_display);
    //start General undirected advertising
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    printk("Advertising successfully started\n", CONFIG_BT_DEVICE_NAME);
}





int main(void)
{
    int err;
    printk("Starting BLE Macro Knob on ESP32-S3...\n");
    // Initialize the Bluetooth Subsystem
    err = bt_enable(bt_ready);
    if (err) {
        printk("Failed to enable Bluetooth (err %d)\n", err);
        return 0;
    }
    // Register the authentication callbacks
    bt_conn_auth_cb_register(&auth_cb_display);

    return 0;
}