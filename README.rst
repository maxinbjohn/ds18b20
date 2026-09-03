Based on zephyr:code-sample:: ds18b20
:name: DS18B20 1-Wire Temperature Sensor with MQTT
:relevant-api: sensor_interface w1_sensor

Read ambient temperature from a DS18B20 1-Wire sensor and publish the
temperature periodically over MQTT.

Overview

---

This sample demonstrates how to use the Zephyr :ref:`sensor` API with the
`DS18B20`_ 1-Wire temperature sensor in a esp32c3 supermini.

.. _DS18B20:
https://www.analog.com/en/products/ds18b20.html

The sample discovers the first available DS18B20 device in the system and
periodically reads its temperature using polling mode. The measured
temperature is then published as a JSON message to an MQTT broker over Wi-Fi.

The sample is intended for boards that provide Wi-Fi connectivity and an
enabled DS18B20 1-Wire interface. For this, I have used an esp32c3 supermini.

The default MQTT topic is:

```text

home/esp32c3/temperature
```

The published payload has the following format:

```text

{"temperature":23.187500}
```

Building and Running

---

The devicetree must have an enabled node with
`compatible = "maxim,ds18b20";`.

The board must also have a working Wi-Fi interface and the application must be
configured with the required Wi-Fi and MQTT settings.

If the sensor is not built into your board, start by wiring the sensor pins
as shown in the Figure Hardware Configuration of the `DS18B20 datasheet`_ at
page 10.

.. _DS18B20 datasheet:
https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf

# Hardware Configuration

Connect the DS18B20 to the board's configured 1-Wire GPIO.

The DS18B20 requires:

* VDD connected to the board supply.
* GND connected to ground.
* DQ connected to the configured 1-Wire GPIO.
* A pull-up resistor on the DQ line, typically 4.7 kOhm.

The exact GPIO and power connections depend on the target board and its
devicetree configuration.

# Wi-Fi Configuration

Configure the Wi-Fi credentials required by the target board/application.

The device connects to the configured Wi-Fi network during startup and waits
for network connectivity before attempting to connect to the MQTT broker.

The application logs the network connection status during startup.

# MQTT Configuration

The application connects to an MQTT broker after network connectivity has
been established.

The MQTT broker address and other MQTT settings are configured by the
application.

For example, the broker can be configured as:


```text
MQTT broker: <mqtt-server-ip>
MQTT port:   1883
```
Temperature measurements are published to:


```text
home/esp32c3/temperature
```

Each MQTT message contains the temperature in degrees Celsius:

```text
{"temperature":22.625000}
```

An MQTT client can be used to monitor the published values. For example,
using Mosquitto:


```text

mosquitto_sub -h <mqtt-server-ip> -p 1883 
-t home/esp32c3/temperature -v
```
# Boards with a built-in DS18B20 or a board-specific overlay

Your board may have a DS18B20 node configured in its devicetree by default,
or a board-specific overlay file with a DS18B20 node may be available.

Make sure this node has `status = "okay";`.

Make sure that you have an external circuit to provide an open-drain interface
for the 1-Wire bus.

Once you have wired the sensor and the serial peripheral on the Arduino header
to the 1-Wire bus, build and flash with:

```text
# west build -b esp32c3_supermini samples/sensor/ds18b20 --pristine
# west flash
```

The devicetree overlay
:zephyr_file:`samples/sensor/ds18b20/arduino_serial.overlay` should work on
any board with a properly configured Arduino pin-compatible Serial peripheral.

# Sample Output

The sample prints status information and sensor readings to the serial
console. DS18B20 driver, Wi-Fi, and MQTT status messages are also logged.

A typical startup sequence is:

```text

[00:00:00.042,000] <inf> ds18b20_mqtt: DS18B20 device ready: ds18b20
[00:00:00.046,000] <inf> ds18b20_mqtt: Wi-Fi connection requested
[00:00:00.046,000] <inf> ds18b20_mqtt: Waiting for network...
[00:00:05.051,000] <inf> ds18b20_mqtt: Wi-Fi connected
[00:00:05.XXX,XXX] <inf> ds18b20_mqtt: Connecting to MQTT broker
[00:00:05.XXX,XXX] <inf> ds18b20_mqtt: MQTT connected
```

Temperature readings are periodically published to the MQTT broker:

```text
home/esp32c3/temperature {"temperature":22.625000}
home/esp32c3/temperature {"temperature":23.187500}
home/esp32c3/temperature {"temperature":23.250000}
```
The exact timestamps and temperature values depend on the board, sensor,
environment, and configured sampling interval.

# MQTT Integration

The sample can be used as the temperature source for an MQTT-based monitoring
system.

A typical deployment is:

```text

DS18B20
|
| 1-Wire
v
ESP32-C3
|
| Wi-Fi
v
MQTT Broker
|
+--------------------+
|                    |
v                    v
Home Assistant       Telegraf
                      |
                      v
                    InfluxDB
                      |
                      v
                     Grafana
```

This allows the same DS18B20 measurement to be consumed by multiple MQTT
clients and monitoring applications.

Typical Telegraf config:

```text
 
[[inputs.mqtt_consumer]]
  servers = ["tcp://mqtt-broker-ip:1883"]

  topics = [
    "home/esp32c3/temperature"
  ]

  data_format = "json"

  name_override = "temperature"

  json_string_fields = []
```

Influx db settings in Telegraf:

```text

[[outputs.influxdb_v2]]
  urls = ["http://influxdb-server-ip:8086"]
  token = "secret-token"
  organization = "home"
  bucket = "temperature"

```
