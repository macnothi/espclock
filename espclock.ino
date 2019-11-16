#include <FS.h>                   
                                  // From the WifiManager AutoConnectWithFSParameters: 
                                  // this needs to be first, or it all crashes and burns... hmmm is this really the case?
#define FASTLED_ESP8266_RAW_PIN_ORDER  

#define SMALLCLOCK                // small clock

#include <TimeLib.h>              // http://www.arduino.cc/playground/Code/Time Time, by Michael Margolis includes
                                  // https://github.com/PaulStoffregen/Time V1.5 
#include "CRTC.h"
#include "CNTPClient.h"
#include <Timezone.h>             // https://github.com/JChristensen/Timezone v1.2.2
#include <FastLED.h>              // https://github.com/FastLED/FastLED v3.2.10
#include "CFadeAnimation.h"
#include "CClockDisplay.h"
#include <ESP8266WiFi.h>          // ESP8266 board packagage v2.5.2
#include <DNSServer.h>            // ESP8266 board packagage v2.5.2
#include <ESP8266WebServer.h>     // ESP8266 board packagage v2.5.2
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager v0.14.0
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson v6.11.5
#include <Ticker.h>               // ESP8266 board packagage v2.5.2

Ticker ticker;

WiFiServer server(80);
unsigned long ulReqcount;



/*
 * ------------------------------------------------------------------------------
 * LED configuration
 * ------------------------------------------------------------------------------
 */


 // Number of LEDs used for the clock (11x10 + 4 minute LEDs + 4 spare LEDs)
#ifdef SMALLCLOCK
#define NUM_LEDS 118
#else
#define NUM_LEDS 114
#endif

// initial values for first time run
const int INIT_RED = 255;
const int INIT_GREEN = 127;
const int INIT_BLUE = 36;

// choose your type of LED stripe controllers
#define STRIPE_SK9822
//#define STRIPE_APA102

#define DATA_PIN 13
#define CLOCK_PIN 14

CRGB leds[NUM_LEDS];
CRGB leds_target[NUM_LEDS];
bool leds_fill[NUM_LEDS];

uint8_t gHue = 0; // demo mode hue
int pattern = 0;

/*
 * ------------------------------------------------------------------------------
 * Configuration parameters configured by the WiFiManager and stored in the FS
 * ------------------------------------------------------------------------------
 */

//The default values for the NTP server
char ntp_server[50] = "0.de.pool.ntp.org";
char clock_version[15] = __DATE__;


/*
* ------------------------------------------------------------------------------
* Wifi/NTP status
* ------------------------------------------------------------------------------
*/
enum eWifiState
{
	e_disconnected = 0,
	e_connected = 1,
	e_reconnect_needed = 2,
};

eWifiState eWiFiConnState = eWifiState::e_disconnected;
WiFiEventHandler disconnectedEventHandler;
WiFiEventHandler connectedEventHandler;

/*
 * ------------------------------------------------------------------------------
 * Clock configuration/variables
 * ------------------------------------------------------------------------------
 */

uint8_t brightnessNight =  5;
uint8_t brightnessDay   = 50;
uint8_t brightness = brightnessDay;

TimeElements DayTime;
TimeElements NightTime;

bool displayClock = true;  // as opposite to demo mode


CRTC Rtc;
CNTPClient Ntp;

//Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
Timezone CE(CEST, CET);

CFadeAnimation ani;
CClockDisplay clockDisp;

bool updateBrightness()
{
	time_t local( CE.toLocal(now()) );
	char clocal[6];

	sprintf(clocal, "%02d:%02d", hour(local), minute(local));

	if ((hour(local) * 60 + minute(local)) >= (NightTime.Hour * 60 + NightTime.Minute) 
	||  (hour(local) * 60 + minute(local)) < (DayTime.Hour * 60 + DayTime.Minute))
	{
		if (FastLED.getBrightness() != brightnessNight)
		{
			Serial.println("Update Brighness: NIGHT");
			FastLED.setBrightness(brightnessNight);
			return true;
		}
	}
	else
	{
		if (FastLED.getBrightness() != brightnessDay)
		{
			Serial.println("Update Brighness: DAY");
			FastLED.setBrightness(brightnessDay);
			return true;
		}
	}
	return false;
}

void setDayStart(String sVal)
{
	TimeElements tm;

	String temp1 = sVal.substring(0, sVal.indexOf(":") );
	String temp2 = sVal.substring(sVal.indexOf(":") + 1);

	tm.Hour = temp1.toInt();
	tm.Minute = temp2.toInt();

	DayTime = tm;

	Serial.print("DayStart=");
	Serial.print(tm.Hour);
	Serial.print(":");
	Serial.println(tm.Minute);
}

void setNightStart(String sVal)
{
	TimeElements tm;
	tm.Hour = sVal.substring(0, sVal.indexOf(":") ).toInt();
	tm.Minute = sVal.substring(sVal.indexOf(":") + 1).toInt();

	NightTime = tm;

	Serial.print("NightStart=");
	Serial.print(tm.Hour);
	Serial.print(":");
	Serial.println(tm.Minute);
}

String getTimeString(TimeElements time)
{
	String sTime;
	char cBuff[6];
	sprintf(cBuff, "%02d:%02d", time.Hour, time.Minute);

	sTime = cBuff;
	return sTime;
}


/*
 * ------------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------------
 */

//------------------------------------------------------------------------------
//Time callback:

time_t getDateTimeFromRTC()
{
	if (eWiFiConnState == eWifiState::e_disconnected)
	{
		Serial.println("WiFi Adapter disconnected from WLAN");
	}
	else if (eWiFiConnState == eWifiState::e_reconnect_needed)
	{
		Serial.println("WiFi Adapter reconnect needed");
	}
	else if (eWiFiConnState == eWifiState::e_connected)
	{
		Serial.println("WiFi Adapter connected to WLAN");
	}
	
	return Rtc.now();
}


//------------------------------------------------------------------------------
//Ticker callback:
void cbTick()
{
	if (0 == leds[0].b)
	{
		leds[0] = CRGB::Blue;
		leds[1] = CRGB::Black;
	}
	else
	{
		leds[0] = CRGB::Black;
		leds[1] = CRGB::Blue;
	}

	FastLED.show();
}

//------------------------------------------------------------------------------
//WiFiManager callback:
bool shouldSaveConfig = false;

void cbSaveConfig()
{
	Serial.println("::cbSaveConfig(): Should save config");
	shouldSaveConfig = true;
}

void cbConfigMode(WiFiManager *myWiFiManager)
{
	ticker.attach(0.2, cbTick);
}


void DemoMode()
{
	fill_solid(&(leds[0]), NUM_LEDS, CRGB::Black);
	FastLED.show();

	for (int i = 0; i < NUM_LEDS; i++)
	{
		leds[i] = CRGB::Red;
		FastLED.show();
		delay(300);
		leds[i] = CRGB::Black;
	}
	delay(150);
}


/*
* ------------------------------------------------------------------------------
* clock settings
* brightness day/night + color Red/Green/Blue
* NTPserver
* ------------------------------------------------------------------------------
*/
void writeSettings()
{
	Serial.println("writing config");

	StaticJsonDocument<200> jsonBuffer;

	CRGB ledcolor;
	ledcolor = clockDisp.getColor();

	// colors
	jsonBuffer["red"] = ledcolor.r;
	jsonBuffer["green"] = ledcolor.g;
	jsonBuffer["blue"] = ledcolor.b;

	// ntp server
	jsonBuffer["ntp_server"] = ntp_server;

	// brightness
	jsonBuffer["day"] = brightnessDay;
	jsonBuffer["night"] = brightnessNight;

	// day-night switch for brightness
	jsonBuffer["DayH"] = DayTime.Hour;
	jsonBuffer["DayM"] = DayTime.Minute;
	jsonBuffer["NigH"] = NightTime.Hour;
	jsonBuffer["NigM"] = NightTime.Minute;

	// ColorMode
	if (CClockDisplay::e_ModeGlitter == clockDisp.getColorMode())
	{
		jsonBuffer["ColMo"] = "glit";
	}
	else if (CClockDisplay::e_ModeGradient == clockDisp.getColorMode())
	{
		jsonBuffer["ColMo"] = "grad";
	}
	else if (CClockDisplay::e_ModeRainbow_1 == clockDisp.getColorMode())
	{
		jsonBuffer["ColMo"] = "rain1";
	}
	else if (CClockDisplay::e_ModeRainbow_2 == clockDisp.getColorMode())
	{
		jsonBuffer["ColMo"] = "rain2";
	}
	else if (CClockDisplay::e_ModeRainbow_3 == clockDisp.getColorMode())
	{
		jsonBuffer["ColMo"] = "rain3";
	}
	else
	{
		jsonBuffer["ColMo"] = "sol";
	}

	File configFile = SPIFFS.open("/config.json", "w");
	if (!configFile)
	{
		Serial.println("failed to open config file for writing");
		return;
	}

	serializeJson(jsonBuffer, configFile);

	configFile.close();

	Serial.println("done saving");
}

void readSettings()
{
	if (SPIFFS.exists("/config.json"))
	{
		Serial.println("INIT: reading config file");

		File configFile = SPIFFS.open("/config.json", "r");
		if (configFile)
		{
			Serial.println("INIT: opened config file");

			size_t size = configFile.size();
			if (size > 1024)
			{
				Serial.println("INIT: config file size is too large");
				return;
			}

			// Allocate a buffer to store contents of the file.
			std::unique_ptr<char[]> buf(new char[size]);

			configFile.readBytes(buf.get(), size);

			StaticJsonDocument<200> json;
			auto error = deserializeJson(json, buf.get());
			if (error)
			{
				Serial.println("INIT: failed to parse config file");
				return;
			}

			// brightness settings
			if (json["day"])
				brightnessDay = int(json["day"]);

			if (json["night"])
				brightnessNight = int(json["night"]);

			brightness = brightnessDay;

			Serial.print("INIT: brightness=");
			Serial.print(brightnessDay);
			Serial.print("/");
			Serial.println(brightnessNight);

			// color settings
			CRGB ledcolor;
			if (json["red"])
				ledcolor.r = int(json["red"]);
			if (json["green"])
				ledcolor.g = int(json["green"]);
			if (json["blue"])
				ledcolor.b = int(json["blue"]);

			Serial.print("INIT: color=");
			Serial.print(ledcolor.r);
			Serial.print("/");
			Serial.print(ledcolor.g);
			Serial.print("/");
			Serial.println(ledcolor.b);

			clockDisp.setColor(ledcolor);

			// day-night switch for brightness
			if (json["DayH"]) 
				DayTime.Hour = int(json["DayH"]);
			if (json["DayM"])
				DayTime.Minute = int(json["DayM"]);
			if (json["NigH"])
				NightTime.Hour = int(json["NigH"]);
			if (json["NigM"])
				NightTime.Minute = int(json["NigM"]);

			Serial.print("INIT: Day-Night=");
			Serial.print(getTimeString(DayTime));
			Serial.print("-");
			Serial.println(getTimeString(NightTime));

			if (json["ntp_server"])
				strcpy(ntp_server, json["ntp_server"]);

			Serial.print("INIT: NTP=");
			Serial.println(ntp_server);

			// ColorMode
			String colMode = json["ColMo"];
			if (colMode.length() > 0)
			{
				if (colMode.equals("glit") )
				{
					clockDisp.setColorMode(CClockDisplay::e_ModeGlitter);
					Serial.println("INIT: colormode=glitter");
				}
				else if (colMode.equals("grad"))
				{
					clockDisp.setColorMode(CClockDisplay::e_ModeGradient);
					Serial.println("INIT: coloparsedrmode=gradient");
				}
				else if (colMode.equals("rain1"))
				{
					clockDisp.setColorMode(CClockDisplay::e_ModeRainbow_1);
					Serial.println("INIT: colormode=rainbow1");
				}
				else if (colMode.equals("rain2"))
				{
					clockDisp.setColorMode(CClockDisplay::e_ModeRainbow_2);
					Serial.println("INIT: colormode=rainbow2");
				}
				else if (colMode.equals("rain3"))
				{
					clockDisp.setColorMode(CClockDisplay::e_ModeRainbow_3);
					Serial.println("INIT: colormode=rainbow3");
				}
				else
				{
					clockDisp.setColorMode(CClockDisplay::e_ModeSolid);
					Serial.println("INIT: colormode=solid");
				}
			}
			else
			{
				clockDisp.setColorMode(CClockDisplay::e_ModeSolid);
				Serial.println("INIT: default colormode=solid");
			}
		}
		else
		{
			// no config file found, use defaults
			Serial.println("INIT: config not found, using defaults");
		}
	}
}

void setDefaults()
{
	brightnessDay = 250;
	brightnessNight = 150;
	brightness = brightnessDay;

	CRGB ledcolor;
	ledcolor.r = INIT_RED;
	ledcolor.g = INIT_GREEN;
	ledcolor.b = INIT_BLUE;

	setDayStart("06:00");
	setNightStart("22:00");

	clockDisp.setColorMode(CClockDisplay::e_ModeSolid);
}


void WiFiReconnect()
{
	if (eWifiState::e_reconnect_needed == eWiFiConnState)
	{
		Serial.println("WiFi reconnected. Trying to establish all connections again ... ");
		SetNewNtp(ntp_server);
		eWiFiConnState = eWifiState::e_connected;
	}
}

// WiFi disconnect handler
// WiFiEventHandler onStationModeDisconnected(std::function<void(const WiFiEventStationModeDisconnected&)>);
void onWiFiDisconnected(const WiFiEventStationModeDisconnected& event)
{
	if (eWiFiConnState != eWifiState::e_disconnected)
	{
		Serial.println("event - WiFi Adapter disconnected from WLAN");
	}
	eWiFiConnState = eWifiState::e_disconnected;
}

// WiFi connect handler
// WiFiEventHandler onStationModeConnected(std::function<void(const WiFiEventStationModeConnected&)>);
void onWiFiConnected(const WiFiEventStationModeConnected& event)
{
	if (eWiFiConnState != eWifiState::e_reconnect_needed)
	{
		Serial.println("event - WiFi Adapter connected to WLAN");
	}
	eWiFiConnState = eWifiState::e_reconnect_needed;
}

void SetNewNtp(const char* ntp)
{
	strcpy(ntp_server, ntp);

	IPAddress timeServerIP;
	WiFi.hostByName(ntp_server, timeServerIP);

	Serial.print("NTP Server: ");
	Serial.print(ntp_server);
	Serial.print(" - ");
	Serial.println(timeServerIP);

	// Rtc.setup();
	Serial.println("Reinitializing RTC");

	Ntp.setup(timeServerIP);
	Rtc.setSyncProvider(&Ntp);
	
	setSyncProvider(getDateTimeFromRTC);
}



/*
* ------------------------------------------------------------------------------
* setup MCU
* ------------------------------------------------------------------------------
*/
void setup()
{
	Serial.begin(57600);

	Serial.print("compiled: ");
	Serial.print(__DATE__);
	Serial.println(__TIME__);

	// init LEDs
#ifdef STRIPE_APA102
	Serial.println("init LED stripe APA102...");
	FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds, NUM_LEDS);
#endif
#ifdef STRIPE_SK9822
	Serial.println("init LED stripe SK9822...");
	FastLED.addLeds<SK9822, DATA_PIN, CLOCK_PIN, BGR, DATA_RATE_MHZ(12)>(leds, NUM_LEDS);
#endif

	clockDisp.setup( &(leds_target[0]), &(leds_fill[0]), NUM_LEDS);
	clockDisp.setTimezone(&CE);

	setDefaults();

	if (!SPIFFS.begin())
	{
		Serial.println("Failed to mount file system");
		return;
	}

	readSettings();

	fill_solid( &(leds[0]), NUM_LEDS, CRGB::Black);
	FastLED.show();

	ticker.attach(0.6, cbTick);

#ifdef SMALLCLOCK
  wifi_station_set_hostname("smallword");
#else
  wifi_station_set_hostname("bigword");
#endif

	WiFiManager wifiManager;
	wifiManager.setAPCallback(cbConfigMode);
	wifiManager.setSaveConfigCallback(cbSaveConfig);

	WiFiManagerParameter custom_ntp_server("server", "NTP server", ntp_server, 50);
	wifiManager.addParameter(&custom_ntp_server);

	//reset settings - for testing
	//wifiManager.resetSettings();

	if (!wifiManager.autoConnect("WordClock")) 
	{
		Serial.println("failed to connect and hit timeout");
		delay(3000);

		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(5000);
	}

	//if you get here you have connected to the WiFi
	Serial.println("connected...yeey :)");
	WiFi.setAutoReconnect(true);

	// first connection establish, lets initialize the handlers needed for proper reconnect
	Serial.println("connecting WIFI event handler");
	disconnectedEventHandler = WiFi.onStationModeDisconnected(&onWiFiDisconnected);
	connectedEventHandler    = WiFi.onStationModeConnected(&onWiFiConnected);

	//read updated parameters
	strcpy(ntp_server, custom_ntp_server.getValue());

	//save the custom parameters to FS
	if (shouldSaveConfig) 
	{
		writeSettings();
	}
  
	Serial.print("IP number assigned by DHCP is ");
	Serial.println(WiFi.localIP());

	Serial.print("SSID:");
	Serial.println(WiFi.SSID());

	Wire.begin(0, 2); // due to limited pins, use pin 0 and 2 for SDA, SCL

	IPAddress timeServerIP;
	WiFi.hostByName(ntp_server, timeServerIP);
	Serial.print("NTP Server: ");
	Serial.print(ntp_server);
	Serial.print(" - ");
	Serial.println(timeServerIP);

	Rtc.setup();
	Ntp.setup(timeServerIP);
	Rtc.setSyncProvider(&Ntp);
	setSyncProvider(getDateTimeFromRTC);
 
	clockDisp.update(true);
	updateBrightness();
	FastLED.show();
 
	// Start the Wifi server
	server.begin();
	Serial.println("Webserver - server started");

	ticker.detach();
}


/*
* ------------------------------------------------------------------------------
* Loop - main routine
* ------------------------------------------------------------------------------
*/
void loop () 
{

	// reconnect WiFiManager, if needed
	WiFiReconnect();

	if (timeSet != timeStatus())
	{
		Serial.println("Time is not set. The time & date are unknown.");
	}

	if (displayClock)
	{
		updateBrightness();

		bool bChanged = false;
		if (clockDisp.getColorMode() == CClockDisplay::e_ModeGlitter)
		{
			bChanged = clockDisp.update(true);
			ani.transform2(leds, leds_target, NUM_LEDS);
		}
		else
		{
			bChanged = clockDisp.update();
			ani.transform(leds, leds_target, NUM_LEDS, bChanged);
		}

		FastLED.show();
	}
	else
	{
		FastLED.setBrightness(brightnessDay);
		if (pattern > 2)
			pattern = 0;

		switch (pattern)
		{
		case 0:
			{
				// JUGGLE: eight colored dots, weaving in and out of sync with each other
				fadeToBlackBy(leds, NUM_LEDS, 20);
				byte dothue = 0;
				for (int i = 0; i < 4; i++)
				{
					leds[beatsin16(i + 2, 0, NUM_LEDS)] |= CHSV(dothue, 200, 255);
					dothue += 50;
				}
			}
			break;

		case 1:
			{
				// CONFETTI: random colored speckles that blink in and fade smoothly
				fadeToBlackBy(leds, NUM_LEDS, 10);
				int pos = random16(NUM_LEDS);
				leds[pos] += CHSV(gHue + random8(64), 200, 255);			
			}
			break;

		case 2:
			{
				// SINELON: a colored dot sweeping back and forth, with fading trails
				fadeToBlackBy(leds, NUM_LEDS, 20);
				int pos = beatsin16(13, 0, NUM_LEDS);
				leds[pos] += CHSV(gHue, 255, 192);
			}
			break;

		case 3:
			{
				// BPM: colored stripes pulsing at a defined Beats-Per-Minute (BPM)
				uint8_t BeatsPerMinute = 62;
				CRGBPalette16 palette = PartyColors_p;
				uint8_t beat = beatsin8(BeatsPerMinute, 64, 255);
				for (int i = 0; i < NUM_LEDS; i++)
				{ //9948
					leds[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
				}			
			}
			break;

		case 4:
			{
				// RAINBOW: FastLED's built-in rainbow generator
				fill_rainbow(leds, NUM_LEDS, gHue, 7);
			}
			break;

		case 5:
			{
				// RAINBOW WITH GLITTER
				fill_rainbow(leds, NUM_LEDS, gHue, 7);
				if (random8() < 80)
				{
					leds[random16(NUM_LEDS)] += CRGB::White;
				}
			}
			break;
		}

		FastLED.show();

		// insert a delay to keep the framerate modest
		FastLED.delay(1000 / 120);

		// do some periodic updates
		EVERY_N_MILLISECONDS(20) { gHue++; } // slowly cycle the "base color" through the rainbow
		EVERY_N_SECONDS(10) { pattern++; } // change patterns periodically
	}

	////////////////////////////////////////////////////////////////////////////
	// webserver implementation
	////////////////////////////////////////////////////////////////////////////
	WiFiClient client = server.available();
	if (!client)
	{
		return;
	}

	// Wait until the client sends some data
	Serial.println("Webserver - new client");
	unsigned long ultimeout = millis() + 250;
	while (!client.available() && (millis()<ultimeout))
	{
		delay(1);
	}
	if (millis()>ultimeout)
	{
		Serial.println("Webserver - client connection time-out!");
		return;
	}

	// Read the first line of the request
	String sRequest = client.readStringUntil('\r');
	//Serial.println(sRequest);
	client.flush();

	// stop client, if request is empty
	if (sRequest == "")
	{
		Serial.println("Webserver - empty request! - stopping client");
		client.stop();
		return;
	}

	// get path; end of path is either space or ?
	// Syntax is e.g. GET /?pin=MOTOR1STOP HTTP/1.1
	String sPath = "", sParam = "", sCmd = "";
	String sGetstart = "GET ";
	int iStart, iEndSpace, iEndQuest;
	iStart = sRequest.indexOf(sGetstart);
	if (iStart >= 0)
	{
		iStart += +sGetstart.length();
		iEndSpace = sRequest.indexOf(" ", iStart);
		iEndQuest = sRequest.indexOf("?", iStart);

		// are there parameters?
		if (iEndSpace>0)
		{
			if (iEndQuest>0)
			{
				// there are parameters
				sPath = sRequest.substring(iStart, iEndQuest);
				sParam = sRequest.substring(iEndQuest, iEndSpace);
			}
			else
			{
				// NO parameters
				sPath = sRequest.substring(iStart, iEndSpace);
			}
		}
	}

	///////////////////////////////////////////////////////////////////////////////
	// output parameters to serial, you may connect e.g. an Arduino and react on it
	if (sParam.length()>0)
	{
		int iEqu = sParam.indexOf("?");
		if (iEqu >= 0)
		{
			sCmd = sParam.substring(iEqu + 1, sParam.length());
			Serial.print("CMD:");
			Serial.println(sCmd);
		}
	}


	// format the html response
	String sResponse, sResponse2, sResponse3, sHeader;
	if (sPath != "/")
	{
		// 404 for non-matching path
		sResponse = "<html><head><title>404 Not Found</title></head><body><h1>Not Found</h1><p>The requested URL was not found on this server.</p></body></html>";

		sHeader = "HTTP/1.1 404 Not found\r\n";
		sHeader += "Content-Length: ";
		sHeader += sResponse.length();
		sHeader += "\r\n";
		sHeader += "Content-Type: text/html\r\n";
		sHeader += "Connection: close\r\n";
		sHeader += "\r\n";
	}
	else
	{
		// format the html page

		// react on parameters
		if (sCmd.length() > 0)
		{
			int iEqu = sParam.indexOf("=");
			int iVal;
			String sVal;
			if (iEqu >= 0)
			{
				sVal = sParam.substring(iEqu + 1, sParam.length());
				iVal = sVal.toInt();
			}

			// switch GPIO
			if (sCmd.indexOf("DAYSHIFT") >= 0)
			{
				setDayStart(sVal);
			}
			else if (sCmd.indexOf("NIGHTSHIFT") >= 0)
			{
				setNightStart(sVal);
			}
			else if (sCmd.indexOf("DEMO") >= 0)
			{
				displayClock = !displayClock;
			}
			else if (sCmd.indexOf("RED") >= 0)
			{
				CRGB ledcolor;
				ledcolor = clockDisp.getColor();
				if (sVal.equals("PLUS"))
				{
					ledcolor.r = (ledcolor.r < 255) ? ledcolor.r + 1 : 255;
				}
				else if (sVal.startsWith("MIN"))
				{
					ledcolor.r = (ledcolor.r > 0) ? ledcolor.r - 1 : 0;
				}
				else
				{
					ledcolor.r = iVal;
				}

				clockDisp.setColor(ledcolor);
				clockDisp.update(true);
			}
			else if (sCmd.indexOf("GREEN") >= 0)
			{
				CRGB ledcolor;
				ledcolor = clockDisp.getColor();
				if (sVal.equals("PLUS"))
				{
					ledcolor.g = (ledcolor.g < 255) ? ledcolor.g + 1 : 255;
				}
				else if (sVal.startsWith("MIN"))
				{
					ledcolor.g = (ledcolor.g > 0) ? ledcolor.g - 1 : 0;
				}
				else
				{
					ledcolor.g = iVal;
				}

				clockDisp.setColor(ledcolor);
				clockDisp.update(true);
			}
			else if (sCmd.indexOf("BLUE") >= 0)
			{
				CRGB ledcolor;
				ledcolor = clockDisp.getColor();
				if (sVal.equals("PLUS"))
				{
					ledcolor.b = (ledcolor.b < 255) ? ledcolor.b + 1 : 255;
				}
				else if (sVal.startsWith("MIN"))
				{
					ledcolor.b = (ledcolor.b > 0) ? ledcolor.b - 1 : 0;
				}
				else
				{
					ledcolor.b = iVal;
				}
				clockDisp.setColor(ledcolor);
				clockDisp.update(true);
			}
			else if (sCmd.indexOf("DAY") >= 0)
			{
				if (sVal.equals("PLUS"))
				{
					brightnessDay = (brightnessDay < 255) ? brightnessDay + 1 : 255;
				}
				else if (sVal.startsWith("MIN"))
				{
					brightnessDay = (brightnessDay > 0) ? brightnessDay - 1 : 0;
				}
				else
				{
					brightnessDay = iVal;
				}
				updateBrightness();
			}
			else if (sCmd.indexOf("NIGHT") >= 0)
			{
				if (sVal.equals("PLUS"))
				{
					brightnessNight = (brightnessNight < 255) ? brightnessNight + 1 : 255;
				}
				else if (sVal.startsWith("MIN"))
				{
					brightnessNight = (brightnessNight > 0) ? brightnessNight - 1 : 0;
				}
				else
				{
					brightnessNight = iVal;
				}
				updateBrightness();
			}
			else if (sCmd.indexOf("SYNC") >= 0)
			{
				time_t tTime = Ntp.now();
				if (tTime == 0)
				{
					delay(1000);
					Ntp.now();
				}
			}
			else if (sCmd.indexOf("COLORMODE") >= 0)
			{
				if (sVal.equals("SOLID"))
				{
					clockDisp.setColorMode(clockDisp.e_ModeSolid);
				}
				else if (sVal.equals("GRADIENT"))
				{
					clockDisp.setColorMode(clockDisp.e_ModeGradient);
				}
				else if (sVal.equals("GLITTER"))
				{
					clockDisp.setColorMode(clockDisp.e_ModeGlitter);
				}
				else if (sVal.equals("RAINBOW1"))
				{
					clockDisp.setColorMode(clockDisp.e_ModeRainbow_1);
				}
				else if (sVal.equals("RAINBOW2"))
				{
					clockDisp.setColorMode(clockDisp.e_ModeRainbow_2);
				}
				else if (sVal.equals("RAINBOW3"))
				{
					clockDisp.setColorMode(clockDisp.e_ModeRainbow_3);
				}
			}
			else if (sCmd.indexOf("HUEDELTA") >= 0)
			{
				//clockDisp.setHueDelta(iVal);
			}
			else if (sCmd.indexOf("HUEINIT") >= 0)
			{
				//clockDisp.setHueInitial(iVal);
			}
			else if (sCmd.indexOf("HUEMOVE") >= 0)
			{
				//clockDisp.setHueMove((bool) iVal);
			}
			else if (sCmd.indexOf("SAVE") >= 0)
			{
				writeSettings();
			}
			clockDisp.update(true);
		}

		CRGB ledcolor;
		ledcolor = clockDisp.getColor();

		sResponse = "<html>\r\n<head>\r\n<title>Konfiguration WordClock</title>\r\n";
		sResponse += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">\r\n";
		sResponse += "</head>\r\n<body style='font-family:verdana;background:#FBFBEF'>\r\n";
		sResponse += "<h1>Word Clock</h1>\r\n";
		sResponse += "<h3>Helligkeit</h3>\r\n";
		sResponse += "<table>\r\n";
		sResponse += "<tr><td width='100'>Tageszeit</td>\r\n";
		sResponse += "<td><input onchange=\"window.location.href='?DAYSHIFT='+this.value;\" value='";
		sResponse += getTimeString(DayTime);
		sResponse += "' type ='time' />&nbsp; -&nbsp; ";
		sResponse += "<input onchange=\"window.location.href='?NIGHTSHIFT='+this.value;\" value='";
		sResponse += getTimeString(NightTime);
		sResponse += "' type ='time'/>";
		sResponse += "</td></tr>\r\n";

		sResponse += "<tr><td width='100'>bei Tag</td>\r\n";
		sResponse += "<td><button type='button' onclick=\"window.location.href='?DAY=MINUS'\"><</button><input onchange=\"window.location.href='?DAY='+this.value;\" value='";
		sResponse += brightnessDay;
		sResponse += "' min='0' max='255' type='range'>";
		sResponse += "<button type='button' onclick=\"window.location.href='?DAY=PLUS'\">></button>&nbsp;&nbsp;";
		sResponse += brightnessDay;
		sResponse += "</td></tr>\r\n";

		sResponse += "<tr><td width='100'>bei Nacht</td>\r\n";
		sResponse += "<td><button type='button' onclick=\"window.location.href='?NIGHT=MINUS'\"><</button><input onchange=\"window.location.href='?NIGHT='+this.value;\" value='";
		sResponse += brightnessNight;
		sResponse += "' min='0' max='255' type='range'>";
		sResponse += "<button type='button' onclick=\"window.location.href='?NIGHT=PLUS'\">></button>&nbsp;&nbsp;";
		sResponse += brightnessNight;
		sResponse += "</td></tr>\r\n</table>\r\n";

		sResponse += "<h3>Farbe</h3>\r\n";
		sResponse += "<table><tr><td width='100'>Rot</td>\r\n";
		sResponse += "<td><button type='button' onclick=\"window.location.href='?RED=MINUS'\"><</button><input onchange=\"window.location.href='?RED='+this.value;\" value='";
		sResponse += ledcolor.r;
		sResponse += "' min='0' max='255' type='range'>";
		sResponse += "<button type='button' onclick=\"window.location.href='?RED=PLUS'\">></button>&nbsp;&nbsp;";
		sResponse += ledcolor.r;
		sResponse += "</td></tr>\r\n";
		sResponse += "<tr><td width='100'>Gr&uuml;n</td>\r\n";
		sResponse += "<td><button type='button' onclick=\"window.location.href='?GREEN=MINUS'\"><</button><input onchange=\"window.location.href='?GREEN='+this.value;\" value='";
		sResponse += ledcolor.g;
		sResponse += "' min='0' max='255' type='range'>";
		sResponse += "<button type='button' onclick=\"window.location.href='?GREEN=PLUS'\">></button>&nbsp;&nbsp;";
		sResponse += ledcolor.g;
		sResponse += "</td></tr>\r\n";
		sResponse += "<tr><td width='100'>Blau</td>\r\n";
		sResponse += "<td><button type='button' onclick=\"window.location.href='?BLUE=MINUS'\"><</button><input onchange=\"window.location.href='?BLUE='+this.value;\" value='";
		sResponse += ledcolor.b;
		sResponse += "' min='0' max='255' type='range'>";
		sResponse += "<button type='button' onclick=\"window.location.href='?BLUE=PLUS'\">></button>&nbsp;&nbsp;";
		sResponse += ledcolor.b;
		sResponse += "</td></tr>";

		sResponse += "<tr><td width='100'>Modus</td>\r\n<td>\r\n";
		sResponse += "<select name=\"ColorMode\" onChange=\"window.location.href='?COLORMODE=' + this.options[this.selectedIndex].value\">\r\n";
		sResponse += "<option value=\"SOLID\"";
		sResponse += (clockDisp.getColorMode() == CClockDisplay::e_ModeSolid) ? " selected" : "";
		sResponse += " >Solid</option>\r\n";
		sResponse += "<option value=\"GRADIENT\"";
		sResponse += (clockDisp.getColorMode() == CClockDisplay::e_ModeGradient) ? " selected" : "";
		sResponse += " >Gradient</option>\r\n";
		sResponse += "<option value=\"GLITTER\"";
		sResponse += (clockDisp.getColorMode() == CClockDisplay::e_ModeGlitter) ? " selected" : "";
		sResponse += " >Glitter</option>\r\n";
		sResponse += "<option value=\"RAINBOW1\"";
		sResponse += (clockDisp.getColorMode() == CClockDisplay::e_ModeRainbow_1) ? " selected" : "";
		sResponse += " >Rainbow_1</option>\r\n";
		sResponse += "<option value=\"RAINBOW2\"";
		sResponse += (clockDisp.getColorMode() == CClockDisplay::e_ModeRainbow_2) ? " selected" : "";
		sResponse += " >Rainbow_2</option>\r\n";
		sResponse += "<option value=\"RAINBOW3\"";
		sResponse += (clockDisp.getColorMode() == CClockDisplay::e_ModeRainbow_3) ? " selected" : "";
		sResponse += " >Rainbow_3</option>\r\n";
		sResponse += "</select>\r\n";

		sResponse2 = "\r\n</td></tr>";
		sResponse2 += "</table>\r\n<br/>\r\n";
		sResponse2 += "<button type='button' onclick=\"window.location.href='?SAVE=true'\">Speichern</button>\r\n";

		sResponse3 = "<br/><br/>\r\n";
		sResponse3 += "<h3>Info</h3>\r\n";
		sResponse3 += "<table><tr><td width='100'>Zeitserver</td>";
		sResponse3 += "<td>";
		sResponse3 += "&nbsp;<input id='ntpserver' onchange=\"window.location.href='?NTP='+this.value;\" value='";
		sResponse3 += ntp_server;
		sResponse3 += "' type ='text' />&nbsp;";
		sResponse3 += "<button type='button' onclick=\"window.location.href='?SYNC=true'\">Sync</button>\r\n";
		sResponse3 += "</td></tr>\r\n";
		sResponse3 += "<tr><td width='100'>Sync</td>\r\n";
		sResponse3 += "<td>";
		time_t lastSync (CE.toLocal(Ntp.getLastSync()));
		sResponse3 += hour(lastSync);
		sResponse3 += ":";
		sResponse3 += minute(lastSync);
		sResponse3 += ".";
		sResponse3 += second(lastSync);
		sResponse3 += "</td></tr>\r\n";
		sResponse3 += "<tr><td width='100'>Software</td>\r\n";
		sResponse3 += "<td>";
		sResponse3 += clock_version;
		sResponse3 += "</td></tr>\r\n";
			
		sResponse3 += "<tr><td width='100'>LED</td>\r\n";
		sResponse3 += "<td>";
#ifdef STRIPE_APA102
		sResponse3 += "APA102C";
#endif
#ifdef STRIPE_SK9822
		sResponse3 += "SK9822";
#endif
		sResponse3 += "</td></tr></table>\r\n";
		sResponse3 += "<button type = \"button\" onclick=\"window.location.href = '?DEMO=true'\">LED Demo</button>\r\n";
		sResponse3 += "</body></html>\r\n";

		sHeader = "HTTP/1.1 200 OK\r\n";
		sHeader += "Content-Length: ";
		sHeader += ( sResponse.length() + sResponse2.length() + sResponse3.length() );
		sHeader += "\r\n";
		sHeader += "Content-Type: text/html\r\n";
		sHeader += "Connection: close\r\n";
		sHeader += "\r\n";
	}

	// Send the response to the client
	client.print(sHeader);
	client.print(sResponse);
	client.print(sResponse2);
	client.print(sResponse3);

	// and stop the client
	client.stop();
	Serial.println("Client disonnected");
}
