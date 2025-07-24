# ESP32 and Raspberry Pi controller for Waveshare HUB75 LED Matrix Display

This repository contains code for both ESP32 and Raspberry Pi controllers for the Waveshare HUB75 LED Matrix Display. This code was written for an XDA-Developers article and is not intended for production use. It is provided as a learning resource and may contain issues.

For the ESP32, we include the following examples:

- Using the [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA) library to connect to an MQTT broker for information retrieval and device control.
- ESPHome for interfacing with an example sensor, [using an external component](https://github.com/TillFleisch/ESPHome-HUB75-MatrixDisplayWrapper/tree/main) that wraps the ESP32-HUB75-MatrixPanel-DMA library.

The Raspberry Pi version is written in C++ and uses the [rpi-rgb-led-matrix library](https://github.com/hzeller/rpi-rgb-led-matrix). It is designed to display weather information from Home Assistant and Spotify track information, both retrieved from an MQTT broker.

Both versions are not memory safe and were designed as proof of concepts. They are not maintained and may contain bugs or security issues. They are provided as a learning resource for those interested in working with the Waveshare HUB75 LED Matrix Display on either device.