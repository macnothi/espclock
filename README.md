# Word clock

This project provides the code for a word clock. The layout is based on the famous original. Here color is controlled via Blynk. 

## Hardware:
+ ESP8266 NodeMCU
+ LED stripe APA102c, alternativly SK9822
+ Realtime clock DS3231

## Software Libraries:
+ FastLED library
+ WifiManager
+ Timezone
+ Timelib
+ Blynk

Please check the code for the exact version and source url of each library. Running the clock requires at least 1A current. Please make sure that your powwer supply is stable enough.

## Notes:

Each letter is enlighted from a single LED. It's recommended to use a light gate for each single LED.

