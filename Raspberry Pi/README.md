# Raspberry Pi RGB LED Matrix MQTT Display

Basic Raspberry Pi controller for the Waveshare P2.5 RGB LED Matrix display. This program makes heavy utilization of the [rpi-rgb-led-matrix library](https://github.com/hzeller/rpi-rgb-led-matrix), and is written in C++. Much of it is not memory safe, and was designed as a proof of concept for an article on XDA-Developers. It is not intended for production use, and is not maintained.

This program requires an MQTT broker to function, and is designed to display weather information from Home Assistant, alongside Spotify track information published to an MQTT broker. Basic ASCII icons are included for weather conditions, and the program can be extended to support additional icons or features. There are many issues with this code and it is provded as a learning resource.

I tested this with the Mosquitto MQTT broker with an automation in Home Assistant to publish data to it. It was used on the Raspberry Pi Model B+, and will work according to the documentation of the rpi-rgb-led-matrix library.