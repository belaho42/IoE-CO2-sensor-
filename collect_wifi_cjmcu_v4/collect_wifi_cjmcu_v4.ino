/*
THIS IS A COPY OF THE ORIGINAL PROVIDED FOR PORTFOLIO PURPOSES. CRITICAL BUT PRIVATE VALUES HAVE BEEN REMOVED.

This version has continuing difficulties with measurements other than CO2

Sketch to read CO2 concentration from a CJMCU sensor on a NodeMCU ESP8266 board.
Communication performed via I2C.
Contributors:  Yuexin Bian, Marv Kausch, Ben Hoffman

board LED does two quick blinks when starting setup, then another three when setup is finished

board LED blinks once with every successful upload

 k30 0 MAC  d8:bf:c0:ec:54:2c
 k30 1 MAC  c8:c9:a3:5c:8b:e9

*/

// Configurables.  These are likely to change often, so I put them here at the top.
//UCSD WiFi Credentials 
#define WIFI_SSID "ssid"
#define WIFI_PASSWORD "pass"
#define Location "Earth, probably"
#define DEVICE "ESP8266 CJMCU CO2 sensor 1"
#define INFLUXDB_BUCKET "k30-1-measurement"
#define VER "collect_wifi_cjmcu_v4"
#define SLEEP 10000

// Print debug info to serial or not
// #define DEBUG1
#ifdef DEBUG1
  #define DEBUG_PRINT(x)  Serial.println (x)
#else
  #define DEBUG_PRINT(x)
#endif

// #define DEBUG2
#ifdef DEBUG2
  #define DEBUG_UPLOAD(x, y)  sensor.addField (x, y)
#else
  #define DEBUG_UPLOAD(x, y) 
#endif



#include <Wire.h> 
#include "src/ccs811.h"
#include "ClosedCube_HDC1080.h"
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <time.h>

#define INFLUXDB_URL "url"
#define INFLUXDB_TOKEN "token"
#define INFLUXDB_ORG "org"

// Time zone info
#define TZ_INFO "tz"

#define BLINK 500

// Declare InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Declare Data point
Point sensor(DEVICE);
uint8_t counter = 0;
unsigned long previousMillisWiFi = 0;
char timeStringBuff[50];
unsigned long currentMillis = 0;
CCS811 ccs811(D3); // nWAKE on D3
ClosedCube_HDC1080 hdc1080;
uint16_t eco2, etvoc, errstat, raw;

void customBlink(int blinkTime, int blinkGap, int numBlinks);
void setupBlink();
void checkWiFi();
void connectWiFi();

void setup() {
  Serial.print("Beginning set-up for CJMCU sensor code");

  //Enable serial
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);  // initialize built-in LED to indicate successful uploads
  digitalWrite(LED_BUILTIN, HIGH);

  //two quick blinks to indicate starting set up
  customBlink(250, 500, 2);

  // hdc1080 info
  hdc1080.begin(0x40);
  Serial.print("Manufacturer ID=0x");
  Serial.println(hdc1080.readManufacturerId(), HEX); // 0x5449 ID of Texas Instruments
  Serial.print("Device ID=0x");
  Serial.println(hdc1080.readDeviceId(), HEX); // 0x1050 ID of the device


  Wire.begin(); 

  ccs811.set_i2cdelay(50); // Needed for ESP8266 because it doesn't handle I2C clock stretch correctly
  bool ok= ccs811.begin();
  if( !ok ) Serial.println("setup: CCS811 begin FAILED");


  Serial.print("setup: hardware    version: "); Serial.println(ccs811.hardware_version(),HEX);
  Serial.print("setup: bootloader  version: "); Serial.println(ccs811.bootloader_version(),HEX);
  Serial.print("setup: application version: "); Serial.println(ccs811.application_version(),HEX);

  ok= ccs811.start(CCS811_MODE_1SEC);
  if( !ok ) Serial.println("setup: CCS811 start FAILED");

    // Setup wifi
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to wifi");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();
  
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Check server connection
  while (!client.validateConnection()) {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }

  Serial.print("Connected to InfluxDB: ");
  Serial.println(client.getServerUrl());
  
    // Add constant tags - only once
  sensor.addTag("device", DEVICE);
  sensor.addTag("location", Location);
  sensor.addTag("software_ver", VER);

  //three quick blinks to indicate finished set up
  customBlink(250, 500, 3);
}

void loop() {

  checkWiFi();
// Pass environmental data from ENS210 to CCS811
  ccs811.set_envdata210(float(hdc1080.readTemperature()), float(hdc1080.readHumidity()));
  ccs811.set_envdata((hdc1080.readTemperature() + 25) * 512, hdc1080.readHumidity() * 512 );
  Serial.print("\n\ntemperature: ");
  Serial.print(hdc1080.readTemperature());
  Serial.print(" C");

  Serial.print("\nhumidity: ");
  Serial.print(hdc1080.readHumidity());
  Serial.println(" %");

  ccs811.read(&eco2, &etvoc, &errstat, &raw);
  if ( errstat == CCS811_ERRSTAT_OK ) {

    Serial.print("\n\ntemperature: ");
    Serial.print(hdc1080.readTemperature());
    Serial.print(" C");

    Serial.print("\nhumidity: ");
    Serial.print(hdc1080.readHumidity());
    Serial.print(" %");

    sensor.clearFields();

    // Data
    sensor.addField("temperature", hdc1080.readTemperature());
    sensor.addField("humidity", hdc1080.readHumidity());
    sensor.addField("CO2", eco2);
    sensor.addField("TVOC", etvoc);
    // Print what are we exactly writing
    Serial.print("Writing: ");
    Serial.println(client.pointToLineProtocol(sensor));

    if (wifiMulti.run() != WL_CONNECTED) {
      Serial.println("Wifi connection lost");
    }
  
    // Write point
    if (!client.writePoint(sensor)) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
    }

    Serial.print("\neCO2 concentration: ");
    Serial.print(eco2);
    Serial.print(" ppm");

    Serial.print("\nTVOC concentration: ");
    Serial.print(etvoc);
    Serial.print(" ppb");

    //TODO:  replace this with the customblink function
    // Toggle LED to indicate successful upload
    digitalWrite(LED_BUILTIN, LOW);  // Turn the LED on (Note esp8266 LED is active LOW)
    delay(BLINK);
    digitalWrite(LED_BUILTIN, HIGH);  // turn off LED 

  } else if ( errstat == CCS811_ERRSTAT_OK_NODATA ) {
    Serial.println("CCS811: waiting for (new) data");
  } else if ( errstat & CCS811_ERRSTAT_I2CFAIL ) {
    Serial.println("CCS811: I2C error");
  } else {
    Serial.print("CCS811: errstat="); Serial.print(errstat, HEX);
    Serial.print("="); Serial.println( ccs811.errstat_str(errstat) );
  }
  Serial.println();

  Serial.println("Waiting...");
  // delay(5 * 1000); // wait 5 seconds
  delay(SLEEP);
}



/*
  This function causes the built-in board LED to blink a custom number
  of times, for a custom duration of each, with custom gaps between 
  each blink. 
  blinkTime gives the time in ms to keep the LED on.
  blinkGap gives the time in ms between the periods that the LED is switched on
  numBlinks gives the number of blinks to execute
*/
void customBlink(int blinkTime, int blinkGap, int numBlinks) {
  for (int i = 0; i<numBlinks; i++) {
    digitalWrite(LED_BUILTIN, LOW); // Turn the LED on (esp8266 LED is active LOW)
    delay(blinkTime);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(blinkGap);
  }
}



void connectWiFi() {
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to WiFi:  %s ", WIFI_SSID);
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();
  Serial.println("WiFi connection successful");
  
}



void checkWiFi() {
  currentMillis = millis();

  if (wifiMulti.run() == WL_CONNECTED)
  {              //if we are connected to Eduroam network
    counter = 0; //reset counter
    if (currentMillis - previousMillisWiFi >= 15 * 1000)
    {
      // printLocalTime(true);
      previousMillisWiFi = currentMillis;
      Serial.print(F("WiFi is still connected with IP: "));
      Serial.println(WiFi.localIP()); //inform user about his IP address
    }
  }
  else if (wifiMulti.run() != WL_CONNECTED)
  { //if we lost connection, retry
    Serial.printf("Reconnecting to WiFi:  %s ", WIFI_SSID);
    connectWiFi();
  }
}