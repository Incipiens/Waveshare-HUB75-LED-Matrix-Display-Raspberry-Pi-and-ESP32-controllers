# ESP32 RGB LED Matrix MQTT Display

Basic ESP32 controller for the Waveshare P2.5 RGB LED Matrix display. This folder packs two versions:

- Found in HALink-PlatformIO you'll find the software I wrote using the [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA) library. It's written in C++ and pulls from an MQTT broker to display temperatures. There is a JSON parsing function that can read data submitted by Zigbee2MQTT, and it utilizes a page-based system to change what is on the display. It can also be controlled via MQTT. 

- In the ESPHome folder, you'll find two basic configuration files, which both utilize the [ESP32-HUB75-Matrix-Panel-DMA wrapper](https://github.com/TillFleisch/ESPHome-HUB75-MatrixDisplayWrapper/tree/main). This is called as an external component in ESPHome. 
  - **sample1.yaml**: This simply pulls data from Home Assistant and displays it on the matrix. It is a basic example of how to use the ESP32 with the HUB75 display in an ESPHome environment, and is the primary example shown in the XDA article.
  - **sample2.yaml**: This is a more complex example that includes a background image and scrolling text. It can be controlled from Home Assistant or the web server, allowing for dynamic updates to the display content. There is a bg_image component that can be updated with a URL, though this does not work on my ESP32 as it cannot allocate the heap required to fetch and decode the image. However, the local image file works fine. The scrolling text can be updated via the web server or Home Assistant, and it uses a script to refresh the display when the text changes.

This code is not memory safe and was designed as a proof of concept for an article on XDA-Developers. It is not intended for production use, and is not maintained. It may contain bugs or security issues, and is provided as a learning resource for those interested in working with the Waveshare HUB75 LED Matrix Display on the ESP32.