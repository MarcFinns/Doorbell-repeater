// Doorbell Repeater - MarcFinns 2024

#include <LittleFS.h>           // For file handling on ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>        // WiFi manager for easy setup
#include <ArduinoJson.h>        // JSON for configuration and settings
#include <ArduinoOTA.h>         // OTA (Over-the-Air) firmware updates

#include <AudioOutputI2S.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorMP3.h>  // MP3 playback

// Firmware and device info
#define FIRMWARE_VERSION "1.0 HW DAC"
#define HOSTNAME "DoorBellRepeater"

// Pin configuration
#define PORTAL_BUTTON_PIN 4  // Pin for triggering config portal
#define LED_RED_PIN D0
#define LED_BLUE_PIN D1

// OTA credentials
#define OTA_PASSWORD "DoorBellRepeaterPWD"

// HTTP Server settings
#define HTTP_PORT 80
#define RINGTONE_NAME "/ringtone.mp3"  // MP3 file for ringtone

// Global objects
ESP8266WebServer httpServer(HTTP_PORT);  // Web server instance
WiFiManager wifiManager;                 // WiFi manager instance

// Replace global pointers with static instances
AudioGeneratorMP3 mp3;                   // MP3 player instance
AudioFileSourceLittleFS file;            // MP3 file source
AudioOutputI2S out;                      // I2S audio output

// Function prototypes
void configModeCallback(WiFiManager *myWiFiManager);
void setHttpServerRouting();
void doRing();
void getSettings();
void doReboot();
void handleFileUpload();

void setup()
{
  // Initialize serial communication
  Serial.begin(115200);
  delay(1000);  // Give the serial port some time to stabilize

  // Startup messages
  Serial.println(F("\n\n-------------------------"));
  Serial.println(F(HOSTNAME));
  Serial.println(F(FIRMWARE_VERSION));
  Serial.println(F("Starting..."));
  Serial.println(F("-------------------------"));

  // Configure button and LED
  pinMode(PORTAL_BUTTON_PIN, INPUT_PULLUP);  // Internal pull-up resistor for the button
  pinMode(LED_RED_PIN, OUTPUT);           // Set LED pin as output
  pinMode(LED_BLUE_PIN, OUTPUT);           // Set LED pin as output

  // Circle all LEDs at boot
  for (int i = 0; i < 3; i++)
  {
    digitalWrite(LED_RED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_BLUE_PIN, HIGH);
    delay(100);
    digitalWrite(LED_RED_PIN, LOW);
    delay(100);
    digitalWrite(LED_BLUE_PIN, LOW);
    delay(100);
  }

  // Turn on RED LED until connected
  digitalWrite(LED_RED_PIN, HIGH);

  // Initialize LittleFS for file handling
  if (!LittleFS.begin())
  {
    Serial.println(F("LittleFS Mount Failed. Restarting..."));
    delay(5000);
    ESP.restart();  // Restart if filesystem mount fails
  }
  Serial.println(F("LittleFS mounted successfully"));

  // Connect to WiFi
  Serial.println(F("Connecting to WiFi..."));
  Serial.println(F("MAC address: ") + WiFi.macAddress());
  wifiManager.setAPCallback(configModeCallback);  // Callback for config portal mode
  wifiManager.setDebugOutput(false);
  wifiManager.setTimeout(180);  // Timeout for autoConnect

  if (!wifiManager.autoConnect(HOSTNAME))
  {
    Serial.println(F("Failed to connect to WiFi. Restarting..."));
    delay(5000);
    ESP.restart();  // Restart on failed connection
  }

  // Turn off RED LED when connected
  digitalWrite(LED_RED_PIN, LOW);


  // Circle BLUE LEDs at connect
  for (int i = 0; i < 2; i++)
  {
    digitalWrite(LED_BLUE_PIN, HIGH);
    delay(200);

    digitalWrite(LED_BLUE_PIN, LOW);
    delay(200);
  }

  // Show connection info
  Serial.println(F("Connected to WiFi with IP: ") + WiFi.localIP().toString());

  // Start mDNS service for easier access via hostname
  if (MDNS.begin(HOSTNAME))
  {
    Serial.println(F("MDNS responder started"));
  }

  // Initialize HTTP server and API endpoints
  setHttpServerRouting();
  httpServer.begin();
  Serial.println(F("HTTP server started"));

  // Initialize OTA (Over-The-Air) updates
  ArduinoOTA.setPort(8266);              // Set OTA port
  ArduinoOTA.setHostname(HOSTNAME);      // Set OTA hostname
  ArduinoOTA.setPassword(OTA_PASSWORD);  // OTA password

  // OTA callback functions
  ArduinoOTA.onStart([]()
  {
    Serial.println(F("OTA Start"));
  });
  ArduinoOTA.onEnd([]()
  {
    Serial.println(F("\nOTA End"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
  {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error)
  {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR)
      Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR)
      Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR)
      Serial.println(F("End Failed"));
  });
  ArduinoOTA.begin();

  // Initialize audio system
  //out.SetGain(3.0);  // Set gain directly on the static object

  // Check if ringtone exists
  if (LittleFS.exists(RINGTONE_NAME))
  {
    Serial.println(F("INFO: Ringtone file found"));
  }
  else
  {
    Serial.println(F("WARNING: No ringtone file found!"));
  }

  // Log free heap space after initialization
  Serial.println("Initialization complete - Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
}

// Callback for WiFiManager config portal activation
void configModeCallback(WiFiManager *myWiFiManager)
{
  Serial.println(F("Entered Captive Portal Mode"));
  digitalWrite(LED_RED_PIN, HIGH);  // Turn on LED to indicate portal mode
}

// Set up HTTP server endpoints
void setHttpServerRouting()
{
  // Root endpoint
  httpServer.on("/", HTTP_GET, []()
  {
    httpServer.send(200, F("text/html"), String(HOSTNAME) + " " + String(FIRMWARE_VERSION) + ", Built " + String(__DATE__ " " __TIME__) + " (c) MarcFinns 2024. Accepted commands: settings, ring, upload, reboot");
  });

  // Custom API routes
  httpServer.on(F("/ring"), HTTP_GET, doRing);
  httpServer.on(F("/settings"), HTTP_GET, getSettings);
  httpServer.on(F("/reboot"), HTTP_GET, doReboot);

  // File upload form
  httpServer.on("/upload", HTTP_GET, []()
  {
    Serial.println(F("Upload page requested"));
    httpServer.send(200, F("text/html"), "<form method=\"POST\" action=\"/upload\" enctype=\"multipart/form-data\"><input type=\"file\" name=\"file\"><input type=\"submit\" value=\"Upload\"></form>");
  });

  // File upload endpoint
  httpServer.on("/upload", HTTP_POST, []()
  {
    httpServer.send(200, F("text/plain"), "File Uploaded");
  },
  handleFileUpload);
}

// Play the ringtone (MP3)
void doRing()
{
  Serial.println(F("Ring command received!"));

  httpServer.send(200, F("application/json"), "{\"message\":\"Ding Dong!\"}");

  if (!mp3.isRunning())
  {
    Serial.println(F("Starting MP3 playback..."));
    // Turn on LED...
    digitalWrite(LED_BLUE_PIN, HIGH);

    file.open(RINGTONE_NAME);  // Open ringtone file
    mp3.begin(&file, &out);    // Start MP3 playback
    Serial.println("MP3 playback started - Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  }
  else
  {
    mp3.stop();
    file.close();
    Serial.println(F("Restarting MP3 playback..."));
    file.open(RINGTONE_NAME);
    mp3.begin(&file, &out);
    Serial.println("MP3 playback restarted - Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  }
}

// Get system settings as JSON
void getSettings()
{
  Serial.println(F("Serving system settings"));

  StaticJsonDocument<512> doc;

  // Populate settings in JSON format
  doc["version"] = String(FIRMWARE_VERSION) + ", Built " + String(__DATE__ " " __TIME__);
  doc["mac"] = WiFi.macAddress();
  doc["ssid"] = WiFi.SSID();
  doc["signalStrength(dBm)"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["gateway"] = WiFi.gatewayIP().toString();
  doc["netmask"] = WiFi.subnetMask().toString();
  doc["uptime(s)"] = millis() / 1000.0;
  doc["freeHeap(bytes)"] = ESP.getFreeHeap();
  doc["chipId"] = ESP.getChipId();
  doc["flashChipId"] = ESP.getFlashChipId();
  doc["flashChipSize"] = ESP.getFlashChipSize();
  doc["flashChipRealSize"] = ESP.getFlashChipRealSize();

  // Send response
  String buf;
  serializeJsonPretty(doc, buf);
  httpServer.send(200, F("application/json"), buf);

  Serial.println("System settings served - Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
}

// Reboot the ESP8266
void doReboot()
{
  Serial.println(F("Reboot requested"));
  httpServer.send(200, F("application/json"), "{\"message\":\"Rebooting\"}");
  delay(1000);
  ESP.restart();
}

// Handle file uploads (MP3 ringtones)
void handleFileUpload()
{
  Serial.println(F("File upload started"));
  HTTPUpload &upload = httpServer.upload();
  static File fsUploadFile;  // Make it static to retain state between calls

  if (upload.status == UPLOAD_FILE_START)
  {
    // Delete the original file if it exists
    if (LittleFS.exists(RINGTONE_NAME))
    {
      LittleFS.remove(RINGTONE_NAME);
      Serial.println(F("Original file deleted"));
    }

    // Open the file for writing
    fsUploadFile = LittleFS.open(RINGTONE_NAME, "w");
    if (!fsUploadFile)
    {
      Serial.println(F("Failed to open file for writing"));
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    // Write buffer to the file
    if (fsUploadFile)
    {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (fsUploadFile)
    {
      fsUploadFile.close();
      Serial.printf("Upload complete: %s, %u bytes\n", upload.filename.c_str(), upload.totalSize);
      Serial.println("Free Heap after upload: " + String(ESP.getFreeHeap()) + " bytes");

      // Validate if upload was successful by checking file size
      if (upload.totalSize == 0)
      {
        LittleFS.remove(RINGTONE_NAME);
        Serial.println(F("Upload failed, file removed"));
      }
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED)
  {
    // Handle aborted uploads
    if (fsUploadFile)
    {
      fsUploadFile.close();
      LittleFS.remove(RINGTONE_NAME);
      Serial.println(F("Upload aborted, file removed"));
    }
  }
}

// Main loop
void loop()
{
  // Check if config portal is requested by button press
  if (digitalRead(PORTAL_BUTTON_PIN) == LOW)
  {
    Serial.println(F("Opening WiFi portal"));
    WiFi.disconnect(true);
    if (!wifiManager.startConfigPortal(HOSTNAME))
    {
      ESP.reset();  // Reset on failure
      delay(5000);
    }
  }

  // Handle OTA
  ArduinoOTA.handle();

  // Handle HTTP server requests
  httpServer.handleClient();

  // Handle MP3 player
  if (mp3.isRunning())
  {
    mp3.loop();

    // Check for end of file
    if (file.getSize() == file.getPos())
    {
      Serial.println(F("End of MP3 file reached, stopping playback"));
      mp3.stop();
      file.close();

      // Turn off LED
      digitalWrite(LED_BLUE_PIN, LOW);
      Serial.println("MP3 playback stopped - Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
      Serial.println("Heap Fragmentation: " + String(ESP.getHeapFragmentation()) + "%");
    }
  }
}
