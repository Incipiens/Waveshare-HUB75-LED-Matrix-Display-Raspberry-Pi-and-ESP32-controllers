# ESP32 RGB LED Matrix MQTT Display

Basic ESP32 controller for the Waveshare P2.5 RGB LED Matrix display. This folder packs two versions:

- Found in HALink-PlatformIO you'll find the software I wrote using the [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA) library. It's written in C++ and pulls from an MQTT broker to display temperatures. There is a JSON parsing function that can read data submitted by Zigbee2MQTT, and it utilizes a page-based system to change what is on the display. It can also be controlled via MQTT. 

- In the ESPHome folder, you'll find a basic template that I wrote utilizing the [ESP32-HUB75-Matrix-Panel-DMA wrapper](https://github.com/TillFleisch/ESPHome-HUB75-MatrixDisplayWrapper/tree/main), which can called as an external component in ESPHome. This simply pulls data from Home Assistant and displays it on the matrix. It is a basic example of how to use the ESP32 with the HUB75 display in an ESPHome environment.

This code is not memory safe and was designed as a proof of concept for an article on XDA-Developers. It is not intended for production use, and is not maintained. It may contain bugs or security issues, and is provided as a learning resource for those interested in working with the Waveshare HUB75 LED Matrix Display on the ESP32.