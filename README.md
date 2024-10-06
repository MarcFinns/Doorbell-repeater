# Doorbell Repeater

Can be invoked via HTTTP from any home automation system
- Works with any I2S dac (tested with Adafruit MAX98357 board)
- Plays an MP3 file when **/ring** endpoint is invoked
- reboots wia **/reboot** endpoint
- MP3 file can be uploaded via **/upload** endpoint
- Status can be visualised via **/settings** endpoing
- Integrates WifiManager for network selection
- Integrates ArduinoOTA for updates without cable

  **NOTE:** better compiled with Arduino 1.x IDE as more heap is available

