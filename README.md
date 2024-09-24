# AltaNode32POE
A physical control unit for the Avigilon Alta API powered by LILYGO T-Internet-POE.

<img src="https://jwise.dev/content/images/size/w1000/2024/06/altanode.png" width="500" >

### Project Details
This project implements a LILYGO T-Internet-POE (ESP32 microcontroller) that provides control of Avigilon Alta entries via API using physical buttons.

### Features:
- Connects to network via LAN and utilizes POE.
- Provides a web interface for configuration of Avigilon Alta API details.
- Implements secure access to configuration pages with username and password authentication.
- Saves configuration data (API URL and button entries) to the SD card.
- Encrypts SD card data & stores key in EEPROM via AES
- Provides Over-the-Air (OTA) updates for easy firmware updates.
- Controls entries via button presses that make use of the Avigilon Alta API.

### Hardware Components:
- LILYGO T-Internet-POE
- Buttons
- Enclosure

### Software Libraries:
- Arduino core for ESP32
- AsyncTCP library
- ESPAsyncWebServer library
- ArduinoJson library
- AsyncElegantOTA library
- mbedtls library
- EEPROM library
- mdebtls/aes library
- LILYGO ethclass2.h library

### Resources
- [Avigilon Alta API docs](https://openpath.readme.io/)
- [Orginal AltaNode Project Blog Post](https://jwise.dev/aviligon-alta-api/)
- [Orginal AltaNode Project](https://github.com/Joshua-Wise/AltaNode)
- [Orginal AltaNode32 Project](https://github.com/Joshua-Wise/AltaNode)

### Amazon Hardware List
- [Enclosure](https://www.amazon.com/uxcell-Button-Control-Station-Aperture/dp/B07WKJM1NJ)
- [Buttons](https://www.amazon.com/Waterproof-Momentary-Mushroom-Terminal-EJ22-241A/dp/B098FGVVFZ)
- [LILYGO T-INTERNET_POE](https://www.lilygo.cc/products/t-internet-poe)
- [Panel Mount RJ45(optional)](https://www.amazon.com/PENGLIN-Shielded-Connector-Extension-Interface/dp/B09WM84YRF)
- [JST Connectors (optional)](https://www.amazon.com/dp/B076HLQ4FX)
