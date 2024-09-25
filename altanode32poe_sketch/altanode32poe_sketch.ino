// Import required libraries
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <mbedtls/aes.h>
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3,0,0)
#include <ETHClass2.h>       // Use the modified ETHClass for older versions
#define ETH  ETH2
#else
#include <ETH.h>             // Use standard ETH for Arduino core version 3.0.0 and above
#endif

// Ethernet settings
#define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT
#define ETH_ADDR        0
#define ETH_TYPE        ETH_PHY_LAN8720
#define ETH_RESET_PIN   5
#define ETH_MDC_PIN     23
#define ETH_MDIO_PIN    18

// SD card
#define SD_MISO_PIN     2
#define SD_MOSI_PIN     15
#define SD_SCLK_PIN     14
#define SD_CS_PIN       13

bool eth_connected = false;

// Constants and global variables
#define KEY_SIZE 16
const size_t JSON_BUFFER_SIZE = 768;
const char* http_username = "admin";
const char* http_password = "altanode";
//const int chipSelect = 13; // SD card CS pin for ESP32
const int buttonPins[] = {12, 33, 32, 16};
const char *setupfile = "/config/setup.json";

String ssid, password, apiUrl;
int entryValues[4];

AsyncWebServer server(80);
mbedtls_aes_context aes;

String urlEncode(const String& input) {
  const char *hex = "0123456789ABCDEF";
  String output = "";
  for (int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if (isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      output += c;
    } else {
      output += '%';
      output += hex[c >> 4];
      output += hex[c & 0xF];
    }
  }
  return output;
}

String urlDecode(String input) {
  String decoded = "";
  char temp[] = "0x00";
  unsigned int len = input.length();
  unsigned int i = 0;
  while (i < len) {
    if (input[i] == '%') {
      if (i + 2 < len) {
        temp[2] = input[i + 1];
        temp[3] = input[i + 2];
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
    i++;
  }
  return decoded;
}

// Function declarations
void getEncryptionKey(uint8_t* key);
void encryptData(char* input, char* output, size_t inputSize);
void decryptData(const char* input, char* output, size_t inputSize);
void writeEncryptedConfig(File& file, const char* config, size_t configSize);
void readEncryptedConfig(File& file, char* config, size_t configSize);
bool isJsonEncrypted(const char* jsonString, size_t length);
void loadSetup();
void saveSetupToFile(const String& new_apiUrl, const int new_entryValues[4]);
void setupWebServer();
void webRestart(AsyncWebServerRequest *request);
void handleSaveSetup(AsyncWebServerRequest *request);
void handleSetup(AsyncWebServerRequest *request);

// Network Event Monitor
void WiFiEvent(WiFiEvent_t event)
{
    switch (event) {
    case ARDUINO_EVENT_ETH_START:
        Serial.println("ETH Started");
        ETH.setHostname("esp32-ethernet");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("ETH Connected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        Serial.print("ETH MAC: ");
        Serial.print(ETH.macAddress());
        Serial.print(", IPv4: ");
        Serial.print(ETH.localIP());
        if (ETH.fullDuplex()) {
            Serial.print(", FULL_DUPLEX");
        }
        Serial.print(", ");
        Serial.print(ETH.linkSpeed());
        Serial.println("Mbps");
        eth_connected = true;
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("ETH Disconnected");
        eth_connected = false;
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("ETH Stopped");
        eth_connected = false;
        break;
    default:
        break;
    }
}

// Ethernet function
void setupEthernet()
{
    WiFi.onEvent(WiFiEvent);

    ETH.begin(ETH_TYPE, ETH_ADDR, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_RESET_PIN, ETH_CLK_MODE);

    // Wait for connection
    while (!eth_connected) {
        Serial.println("Connecting to network..."); 
        delay(500);
    }
}

// Encryption functions
void getEncryptionKey(uint8_t* key) {
  for (int i = 0; i < KEY_SIZE; i++) {
    key[i] = EEPROM.read(i);
  }
}

void encryptData(char* input, char* output, size_t inputSize) {
  uint8_t key[KEY_SIZE];
  getEncryptionKey(key);
  
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, KEY_SIZE * 8);
  
  size_t paddedSize = (inputSize + 15) & ~15;
  for (size_t i = 0; i < paddedSize; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, (uint8_t*)input + i, (uint8_t*)output + i);
  }
  
  mbedtls_aes_free(&aes);
}

void decryptData(const char* input, char* output, size_t inputSize) {
  uint8_t key[KEY_SIZE];
  getEncryptionKey(key);
  
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, KEY_SIZE * 8);
  
  size_t paddedSize = (inputSize + 15) & ~15;
  for (size_t i = 0; i < paddedSize; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, (uint8_t*)input + i, (uint8_t*)output + i);
  }
  
  mbedtls_aes_free(&aes);
  
  while (paddedSize > 0 && output[paddedSize-1] == 0) {
    paddedSize--;
  }
  output[paddedSize] = '\0';
}

void writeEncryptedConfig(File& file, const char* config, size_t configSize) {
  size_t paddedSize = (configSize + 15) & ~15;
  char* paddedConfig = new char[paddedSize];
  memset(paddedConfig, 0, paddedSize);
  memcpy(paddedConfig, config, configSize);
  
  char* encryptedConfig = new char[paddedSize];
  encryptData(paddedConfig, encryptedConfig, paddedSize);
  
  size_t bytesWritten = file.write((uint8_t*)encryptedConfig, paddedSize);
  
  Serial.printf("Original size: %d, Padded size: %d, Bytes written: %d\n", configSize, paddedSize, bytesWritten);
  
  delete[] paddedConfig;
  delete[] encryptedConfig;
}

void readEncryptedConfig(File& file, char* config, size_t configSize) {
  size_t paddedSize = (configSize + 15) & ~15;
  char* encryptedConfig = new char[paddedSize];
  file.read((uint8_t*)encryptedConfig, paddedSize);
  
  decryptData(encryptedConfig, config, paddedSize);
  
  delete[] encryptedConfig;
}

bool isJsonEncrypted(const char* jsonString, size_t length) {
  if (length > 0 && jsonString[0] == '{') {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonString, length);
    return error != DeserializationError::Ok;
  }
  return true;
}

// Setup functions
void loadSetup() {
  File dataFile = SD.open(setupfile, FILE_READ);
  if (!dataFile) {
    Serial.println("Failed to open setup.json");
    return;
  }

  size_t fileSize = dataFile.size();
  char* jsonBuffer = new char[fileSize + 1];
  
  dataFile.readBytes(jsonBuffer, fileSize);
  jsonBuffer[fileSize] = '\0';
  dataFile.close();

  bool encrypted = isJsonEncrypted(jsonBuffer, fileSize);
  Serial.printf("Setup data is %s\n", encrypted ? "encrypted" : "unencrypted");

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error;

  if (encrypted) {
    char* decryptedJson = new char[fileSize];
    decryptData(jsonBuffer, decryptedJson, fileSize);
    error = deserializeJson(doc, decryptedJson);
    delete[] decryptedJson;
  } else {
    error = deserializeJson(doc, jsonBuffer);
  }

  delete[] jsonBuffer;

  if (error) {
    Serial.print(F("Parsing setup JSON failed: "));
    Serial.println(error.c_str());
    return;
  }

  apiUrl = doc["apiurl"].as<String>();
  for (int i = 1; i <= 4; i++) {
    entryValues[i - 1] = doc["entries"][String(i)];
  }

  Serial.print("API URL: ");
  Serial.println(apiUrl);
  for (int i = 0; i < 4; i++) {
    Serial.printf("Entry ID %d: %d\n", i + 1, entryValues[i]);
  }

  if (!encrypted) {
    Serial.println("Encrypting and saving setup configuration...");
    saveSetupToFile(apiUrl, entryValues);
  }
}

void saveSetupToFile(const String& new_apiUrl, const int new_entryValues[4]) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["apiurl"] = new_apiUrl;
  JsonObject entries = doc["entries"].to<JsonObject>();
  for (int i = 0; i < 4; i++) {
    entries[String(i + 1)] = new_entryValues[i];
  }

  String jsonString;
  serializeJson(doc, jsonString);

  SD.remove(setupfile);
  File file = SD.open(setupfile, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create setup file"));
    return;
  }

  writeEncryptedConfig(file, jsonString.c_str(), jsonString.length());
  file.close();

  Serial.println("Encrypted setup configuration saved to SD card");
}

void setupSDCard() {
  pinMode(SD_MISO_PIN, INPUT_PULLUP);
  SPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
  if (!SD.begin(SD_CS_PIN)) {
      Serial.println("SD card initialization failed");
      return;
  }
  Serial.println("SD card initialized.");
}

// Web server functions
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    File htmlFile = SD.open("/html/index.html", FILE_READ);
    if (!htmlFile) {
      request->send(500, "text/plain", "Error: Could not open file");
      return;
    }
    String htmlContent = htmlFile.readString();
    htmlFile.close();
    request->send(200, "text/html", htmlContent);
  });

  server.on("/setup", HTTP_GET, handleSetup);
  server.on("/saveSetup", HTTP_GET, handleSaveSetup);
  server.on("/webRestart", HTTP_GET, webRestart);
  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(401);
  });

  AsyncElegantOTA.begin(&server);
  server.begin();
  Serial.println("Web server started");
}

void webRestart(AsyncWebServerRequest *request) {
  Serial.println("Restarting...");
  request->send(200, "text/plain", "Restarting...");
  delay(1000);
  ESP.restart();
}

void handleSaveSetup(AsyncWebServerRequest *request) {
  String new_apiUrl = request->getParam("webapiurl")->value();
  new_apiUrl = urlDecode(new_apiUrl);  // Decode the URL
  
  int new_entryValues[4];
  for (int i = 0; i < 4; i++) {
    new_entryValues[i] = request->getParam("webentry" + String(i+1))->value().toInt();
  }

  saveSetupToFile(new_apiUrl, new_entryValues);

  Serial.println("Saving new setup configuration:");
  Serial.println("API URL: " + new_apiUrl);
  for (int i = 0; i < 4; i++) {
    Serial.println("Entry " + String(i+1) + ": " + String(new_entryValues[i]));
  }

  File htmlFile = SD.open("/html/save.html", FILE_READ);
  if (!htmlFile) {
    request->send(500, "text/plain", "Error: Could not open file");
    return;
  }
  String htmlContent = htmlFile.readString();
  htmlFile.close();
  request->send(200, "text/html", htmlContent);
}

void handleSetup(AsyncWebServerRequest *request) {
  if(!request->authenticate(http_username, http_password))
    return request->requestAuthentication();
  
  File htmlFile = SD.open("/html/setup.html", FILE_READ);
  if (!htmlFile) {
    request->send(500, "text/plain", "Error: Could not open file");
    return;
  }
  
  String htmlContent = htmlFile.readString();
  htmlFile.close();
  
  // Replace placeholders with actual data, using the decoded API URL
  htmlContent.replace("%%API_URL%%", apiUrl);  // No need to encode here
  for (int i = 0; i < 4; i++) {
    htmlContent.replace("%%ENTRY" + String(i+1) + "%%", String(entryValues[i]));
  }
  
  request->send(200, "text/html", htmlContent);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(KEY_SIZE);

  for (int pin : buttonPins) {
    pinMode(pin, INPUT_PULLUP);
  }

  setupSDCard();
  setupEthernet();

  // Wait for Ethernet connection
  while (!eth_connected) {
    Serial.println("Waiting for Ethernet connection...");
    delay(1000);
  }

  loadSetup();
  setupWebServer();
}

void loop() {
 if (eth_connected) {
    for (int i = 0; i < 4; i++) {
      if (digitalRead(buttonPins[i]) == LOW) {
        Serial.printf("Button %d pressed!\n", i + 1);
        
        HTTPClient http;
        http.begin(apiUrl);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        
        String httpRequestData = "entryId=" + String(entryValues[i]);
        
        http.sendRequest("POST", httpRequestData);
        
        // No need to wait for or process the response
        http.end();
        
        delay(500); // Debounce delay
      }
    }
  } else {
    Serial.println("Ethernet not connected. Trying to reconnect...");
    setupEthernet();
    delay(5000); // Wait 5 seconds before trying again
  }
}