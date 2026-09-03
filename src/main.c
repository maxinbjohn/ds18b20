#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ds18b20_mqtt, LOG_LEVEL_INF);


/*
 * --------------------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------------------
 */

#define WIFI_SSID       "My SSID"
#define WIFI_PASSWORD   "WIFI PASSWORD"

#define MQTT_BROKER_IP  "my-mqtt-broker-ip"
#define MQTT_BROKER_PORT 1883

#define MQTT_CLIENT_ID  "esp32c3-ds18b20"
#define MQTT_TOPIC      "home/esp32c3/temperature"

#define MQTT_RX_BUFFER_SIZE 256
#define MQTT_TX_BUFFER_SIZE 256

#define TEMPERATURE_INTERVAL_SEC 10


/*
 * --------------------------------------------------------------------------
 * DS18B20
 * --------------------------------------------------------------------------
 */

#define DS18B20_NODE DT_NODELABEL(ds18b20)

#if !DT_NODE_EXISTS(DS18B20_NODE)
#error "No DS18B20 node found. Check your devicetree overlay."
#endif

static const struct device *const ds18b20 =
    DEVICE_DT_GET(DS18B20_NODE);


/*
 * --------------------------------------------------------------------------
 * MQTT
 * --------------------------------------------------------------------------
 */

static struct mqtt_client client;

static uint8_t rx_buffer[MQTT_RX_BUFFER_SIZE];
static uint8_t tx_buffer[MQTT_TX_BUFFER_SIZE];

static struct sockaddr_storage broker;

static bool mqtt_connected;


/*
 * --------------------------------------------------------------------------
 * MQTT callbacks
 * --------------------------------------------------------------------------
 */

static void mqtt_evt_handler(struct mqtt_client *const c,
                             const struct mqtt_evt *evt)
{
    ARG_UNUSED(c);

    switch (evt->type) {

    case MQTT_EVT_CONNACK:
        if (evt->result == 0) {
            LOG_INF("MQTT connected");
            mqtt_connected = true;
        } else {
            LOG_ERR("MQTT connection failed: %d", evt->result);
            mqtt_connected = false;
        }
        break;

    case MQTT_EVT_DISCONNECT:
        LOG_INF("MQTT disconnected: %d", evt->result);
        mqtt_connected = false;
        break;

    case MQTT_EVT_PUBLISH:
        LOG_INF("MQTT PUBLISH received");
        break;

    case MQTT_EVT_PUBACK:
        LOG_INF("MQTT PUBACK");
        break;

    default:
        break;
    }
}


/*
 * --------------------------------------------------------------------------
 * MQTT initialization
 * --------------------------------------------------------------------------
 */

static void setup_mqtt_client(void)
{
    mqtt_client_init(&client);

    client.broker = &broker;

    client.evt_cb = mqtt_evt_handler;

    client.client_id.utf8 = (uint8_t *)MQTT_CLIENT_ID;
    client.client_id.size = strlen(MQTT_CLIENT_ID);

    client.password = NULL;
    client.user_name = NULL;

    client.protocol_version = MQTT_VERSION_3_1_1;

    client.rx_buf = rx_buffer;
    client.rx_buf_size = sizeof(rx_buffer);

    client.tx_buf = tx_buffer;
    client.tx_buf_size = sizeof(tx_buffer);

    client.transport.type = MQTT_TRANSPORT_NON_SECURE;
}


/*
 * --------------------------------------------------------------------------
 * MQTT connect
 * --------------------------------------------------------------------------
 */

static int mqtt_connect_broker(void)
{
    int ret;
    struct sockaddr_in *broker4;

    broker4 = (struct sockaddr_in *)&broker;

    memset(&broker, 0, sizeof(broker));

    broker4->sin_family = AF_INET;
    broker4->sin_port = htons(MQTT_BROKER_PORT);

    ret = zsock_inet_pton(AF_INET,
                          MQTT_BROKER_IP,
                          &broker4->sin_addr);

    if (ret != 1) {
        LOG_ERR("Invalid MQTT broker address: %s",
                MQTT_BROKER_IP);
        return -EINVAL;
    }

    setup_mqtt_client();

    LOG_INF("Connecting to MQTT broker %s:%d",
            MQTT_BROKER_IP,
            MQTT_BROKER_PORT);

    ret = mqtt_connect(&client);

    if (ret < 0) {
        LOG_ERR("mqtt_connect failed: %d", ret);
        return ret;
    }

    return 0;
}


/*
 * --------------------------------------------------------------------------
 * MQTT publish temperature
 * --------------------------------------------------------------------------
 */

static int mqtt_publish_temperature(const struct sensor_value *temperature)
{
    struct mqtt_publish_param param;
    static uint8_t payload[64];

    int len;

    if (!mqtt_connected) {
        LOG_WRN("MQTT not connected");
        return -ENOTCONN;
    }

    /*
     * Publish JSON:
     *
     * {"temperature":23.625000}
     */

    len = snprintk(payload,
                   sizeof(payload),
                   "{\"temperature\":%d.%06d}",
                   temperature->val1,
                   temperature->val2);

    if (len < 0 || len >= sizeof(payload)) {
        LOG_ERR("Temperature payload too large");
        return -ENOMEM;
    }

    memset(&param, 0, sizeof(param));

    param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;

    param.message.topic.topic.utf8 =
        (uint8_t *)MQTT_TOPIC;

    param.message.topic.topic.size =
        strlen(MQTT_TOPIC);

    param.message.payload.data = payload;
    param.message.payload.len = len;

    param.message_id = 1;
    param.dup_flag = 0;
    param.retain_flag = 0;

    LOG_INF("MQTT temperature: %s", payload);

    return mqtt_publish(&client, &param);
}


/*
 * --------------------------------------------------------------------------
 * Wi-Fi
 * --------------------------------------------------------------------------
 */

static int wifi_connect(void)
{
    struct wifi_connect_req_params params = { 0 };
    struct net_if *iface;
    int ret;

    iface = net_if_get_default();

    if (iface == NULL) {
        LOG_ERR("No default network interface");
        return -ENODEV;
    }

    params.ssid = (const uint8_t *)WIFI_SSID;
    params.ssid_length = strlen(WIFI_SSID);

    params.psk = (const uint8_t *)WIFI_PASSWORD;
    params.psk_length = strlen(WIFI_PASSWORD);

    params.channel = WIFI_CHANNEL_ANY;
    params.security = WIFI_SECURITY_TYPE_PSK;

    ret = net_mgmt(NET_REQUEST_WIFI_CONNECT,
                   iface,
                   &params,
                   sizeof(params));

    if (ret < 0) {
        LOG_ERR("Wi-Fi connect failed: %d", ret);
        return ret;
    }

    LOG_INF("Wi-Fi connection requested");

    return 0;
}


/*
 * --------------------------------------------------------------------------
 * Wait for IPv4 address
 * --------------------------------------------------------------------------
 */

static int wait_for_network(void)
{
    struct net_if *iface;

    LOG_INF("Waiting for network...");

    while (true) {

        struct net_in_addr *addr;

        iface = net_if_get_default();

        if (iface != NULL && net_if_is_up(iface)) {

            addr = net_if_ipv4_get_global_addr(
                iface,
                NET_ADDR_PREFERRED);

            if (addr != NULL) {
                LOG_INF("IPv4 address acquired");
                return 0;
            }
        }

        k_sleep(K_SECONDS(1));
    }
}


/*
 * --------------------------------------------------------------------------
 * Read DS18B20
 * --------------------------------------------------------------------------
 */

static int read_temperature(struct sensor_value *temperature)
{
    int ret;

    ret = sensor_sample_fetch(ds18b20);

    if (ret < 0) {
        LOG_ERR("DS18B20 sample fetch failed: %d", ret);
        return ret;
    }

    ret = sensor_channel_get(ds18b20,
                             SENSOR_CHAN_AMBIENT_TEMP,
                             temperature);

    if (ret < 0) {
        LOG_ERR("DS18B20 temperature read failed: %d", ret);
        return ret;
    }

    return 0;
}


/*
 * --------------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------------
 */

int main(void)
{
    int ret;
    struct sensor_value temperature;

    LOG_INF("ESP32-C3 DS18B20 MQTT");

    /*
     * DS18B20
     */

    if (!device_is_ready(ds18b20)) {
        LOG_ERR("DS18B20 device not ready");
        return 0;
    }

    LOG_INF("DS18B20 device ready: %s", ds18b20->name);


    /*
     * Wi-Fi
     */

    ret = wifi_connect();

    if (ret < 0) {
        LOG_ERR("Wi-Fi connection request failed");
        return 0;
    }


    /*
     * DHCP / IPv4
     */

    ret = wait_for_network();

    if (ret < 0) {
        LOG_ERR("Network initialization failed");
        return 0;
    }


    /*
     * MQTT
     */

    ret = mqtt_connect_broker();

    if (ret < 0) {
        LOG_ERR("MQTT connection failed");
        return 0;
    }


    /*
     * Main loop
     */

    while (true) {

        /*
         * Process MQTT traffic.
         */

        ret = mqtt_input(&client);

        if (ret < 0) {
            LOG_ERR("mqtt_input: %d", ret);
            mqtt_connected = false;
            break;
        }

        ret = mqtt_live(&client);

        if (ret < 0 && ret != -EAGAIN) {
            LOG_ERR("mqtt_live: %d", ret);
            mqtt_connected = false;
            break;
        }


        /*
         * Read DS18B20.
         */

        ret = read_temperature(&temperature);

        if (ret == 0) {

            LOG_INF("Temperature: %d.%06d °C",
                    temperature.val1,
                    temperature.val2);

            /*
             * Publish temperature.
             */

            ret = mqtt_publish_temperature(&temperature);

            if (ret < 0) {
                LOG_ERR("MQTT temperature publish failed: %d",
                        ret);
            }
        }


        k_sleep(K_SECONDS(TEMPERATURE_INTERVAL_SEC));
    }

    return 0;
}
