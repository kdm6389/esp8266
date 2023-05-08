//connect wifi and log ip 
//

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <Arduino.h>
#include <ArduinoOTA.h>

// initial time (possibly given by an external RTC)
#define RTC_UTC_TEST 1510592825  // 1510592825 = Monday 13 November 2017 17:07:05 UTC


// This database is autogenerated from IANA timezone database
//    https://www.iana.org/time-zones
// and can be updated on demand in this repository
#include <TZ.h>

// "TZ_" macros follow DST change across seasons without source code change
// check for your nearest city in TZ.h

// espressif headquarter TZ
//#define MYTZ TZ_Asia_Shanghai

// example for "Not Only Whole Hours" timezones:
// Kolkata/Calcutta is shifted by 30mn
//#define MYTZ TZ_Asia_Kolkata

// example of a timezone with a variable Daylight-Saving-Time:
// demo: watch automatic time adjustment on Summer/Winter change (DST)
#define MYTZ TZ_Asia_Kolkata

////////////////////////////////////////////////////////


#include <coredecls.h>  // settimeofday_cb()
#include <Schedule.h>
#include <PolledTimeout.h>

#include <time.h>      // time() ctime()
#include <sys/time.h>  // struct timeval

#include <sntp.h>  // sntp_servermode_dhcp()


// for testing purpose:
extern "C" int clock_gettime(clockid_t unused, struct timespec* tp);

////////////////////////////////////////////////////////

// OPTIONAL: change SNTP startup delay
// a weak function is already defined and returns 0 (RFC violation)
// it can be redefined:
// uint32_t sntp_startup_delay_MS_rfc_not_less_than_60000 ()
//{
//    //info_sntp_startup_delay_MS_rfc_not_less_than_60000_has_been_called = true;
//    return 60000; // 60s (or lwIP's original default: (random() % 5000))
//}

// OPTIONAL: change SNTP update delay
// a weak function is already defined and returns 1 hour
// it can be redefined:
// uint32_t sntp_update_delay_MS_rfc_not_less_than_15000 ()
//{
//    //info_sntp_update_delay_MS_rfc_not_less_than_15000_has_been_called = true;
//    return 15000; // 15s
//}

#define PTM(w) \
  Serial.print(" " #w "="); \
  Serial.print(tm->tm_##w);

void printTm(const char* what, const tm* tm) {
  Serial.print(what);
  PTM(isdst);
  PTM(yday);
  PTM(wday);
  PTM(year);
  PTM(mon);
  PTM(mday);
  PTM(hour);
  PTM(min);
  PTM(sec);
}

void showTime() {
  gettimeofday(&tv, nullptr);
  clock_gettime(0, &tp);
  now = time(nullptr);
  now_ms = millis();
  now_us = micros();

  Serial.println();
  printTm("localtime:", localtime(&now));
  Serial.println();
  printTm("gmtime:   ", gmtime(&now));
  Serial.println();

  // timezone and demo in the future
  Serial.printf("timezone:  %s\n", getenv("TZ") ?: "(none)");

  // human readable
  Serial.print("ctime:     ");
  Serial.print(ctime(&now));

  // lwIP v2 is able to list more details about the currently configured SNTP servers
  for (int i = 0; i < SNTP_MAX_SERVERS; i++) {
    IPAddress sntp = *sntp_getserver(i);
    const char* name = sntp_getservername(i);
    if (sntp.isSet()) {
      Serial.printf("sntp%d:     ", i);
      if (name) {
        Serial.printf("%s (%s) ", name, sntp.toString().c_str());
      } else {
        Serial.printf("%s ", sntp.toString().c_str());
      }
      Serial.printf("- IPv6: %s - Reachability: %o\n", sntp.isV6() ? "Yes" : "No", sntp_getreachability(i));
    }
  }

  Serial.println();

  // show subsecond synchronisation
  timeval prevtv;
  time_t prevtime = time(nullptr);
  gettimeofday(&prevtv, nullptr);

  while (true) {
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec != prevtv.tv_sec) {
      Serial.printf("time(): %u   gettimeofday(): %u.%06u  seconds are unchanged\n", (uint32_t)prevtime, (uint32_t)prevtv.tv_sec, (uint32_t)prevtv.tv_usec);
      Serial.printf("time(): %u   gettimeofday(): %u.%06u  <-- seconds have changed\n", (uint32_t)(prevtime = time(nullptr)), (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec);
      break;
    }
    prevtv = tv;
    delay(50);
  }

  Serial.println();
}



/*
#define trriggerPin=gpio5 d1
#define ecopin=gpio4 d2
#define VccPowerPin=d6 gpio12
*/


#ifndef STASSID
#define STASSID "Home-WiFi"
#define STAPSK "NOpassword?"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;



extern "C" {
	#include "user_interface.h"  // Required for wifi_station_connect() to work
}



#define FPM_SLEEP_MAX_TIME 0xFFFFFFF

void WiFiOn();
void WiFiOff();

//------------------------------------------------------------------------------

void setup() {
  WiFi.persistent(false); //save ROM red write  
  WiFi.mode(WIFI_OFF);
  Serial.begin(115200);
  Serial.println("\nStarting in 2secs...\n");
  delay(2000);
  // install callback - called when settimeofday is called (by SNTP or user)
  // once enabled (by DHCP), SNTP is updated every hour by default
  // ** optional boolean in callback function is true when triggered by SNTP **
  settimeofday_cb(time_is_set);

  // setup RTC time
  // it will be used until NTP server will send us real current time
  Serial.println("Manually setting some time from some RTC:");
  time_t rtc = RTC_UTC_TEST;
  timeval tv = { rtc, 0 };
  settimeofday(&tv, nullptr);

  // NTP servers may be overridden by your DHCP server for a more local one
  // (see below)

  // ----> Here is the ONLY ONE LINE needed in your sketch
  configTime(MYTZ, "pool.ntp.org");
  // <----
  // Replace MYTZ by a value from TZ.h (search for this file in your filesystem).

  // Former configTime is still valid, here is the call for 7 hours to the west
  // with an enabled 30mn DST
  // configTime(7 * 3600, 3600 / 2, "pool.ntp.org");

  // OPTIONAL: disable obtaining SNTP servers from DHCP
  // sntp_servermode_dhcp(0); // 0: disable obtaining SNTP servers from DHCP (enabled by default)

  // Give now a chance to the settimeofday callback,
  // because it is *always* deferred to the next yield()/loop()-call.
  yield();

  // start network
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  // don't wait for network, observe time changing
  // when NTP timestamp is received
  showTime();

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


}

void loop() {
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
  }
  Serial.print(F("WiFi Gateway IP: "));
  Serial.println(WiFi.gatewayIP());
  Serial.print(F("my IP address: "));
 Serial.println(WiFi.localIP());

   Serial.println("show time\n");
  if (showTimeNow) { showTime(); }

  ArduinoOTA.handle();
  delay(10000);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);  
  delay(1000);                      // Wait for a second
  digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
  delay(2000);

	WiFiOn();
	delay(5000);
  Serial.println("\nWiFi-OFF\n");
	WiFiOff();
	delay(5000);
  Serial.println("\nWiFi-ON\n");
	WiFiOn();
	delay(5000);
  Serial.println("\nWiFi-OFF\n");
	WiFiOff();
	delay(5000);
  Serial.println("going to sleep for 10sec");
  ESP.deepSleep(1e6 * 10, WAKE_RF_DEFAULT); // sleep 10 seconds to wake connect Wake/D0/GPIO16_to_RST flash disable on serial 
}
//------------------------------------------------------------------------------

void WiFiOn() {

	wifi_fpm_do_wakeup();
	wifi_fpm_close();

	Serial.println("WiFiOn: Reconnecting");
	wifi_set_opmode(STATION_MODE);
	wifi_station_connect();
}


void WiFiOff() {

	Serial.println("diconnecting client and wifi\n");
	//client.disconnect();
  //WiFi.disconnect(); /dont use this 
	wifi_station_disconnect();
	wifi_set_opmode(NULL_MODE);
	wifi_set_sleep_type(MODEM_SLEEP_T);
	wifi_fpm_open();
	wifi_fpm_do_sleep(FPM_SLEEP_MAX_TIME);

}