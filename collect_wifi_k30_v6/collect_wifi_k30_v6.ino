/*
THIS IS A COPY OF THE ORIGINAL PROVIDED FOR PORTFOLIO PURPOSES. CRITICAL BUT PRIVATE VALUES HAVE BEEN REMOVED.

Sketch to read CO2 concentration from a K30 sensor on a NodeMCU ESP8266 board.
Communication performed via I2C.
Contributors:  Yuexin Bian, Marv Kausch, Ben Hoffman

Board LED does two quick blinks when starting setup, then another three when setup is finished

Board LED blinks once with every successful upload
*/

// Configurables.  These are likely to change often, so I put them here at the top.
//UCSD WiFi Credentials 
#define WIFI_SSID "ssid"
#define WIFI_PASSWORD "pass"
#define Location "Earth, Probably"
#define DEVICE "ESP8266 K30 CO2 sensor 1"
#define INFLUXDB_BUCKET "k30-1-measurement"
#define VER "collect_wifi_k30_v6"
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


// useful headers
#include <Wire.h> 
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFi.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <time.h>

// Influx login info
#define INFLUXDB_URL "url"
#define INFLUXDB_TOKEN "token"
#define INFLUXDB_ORG "org"

// Time zone info
#define TZ_INFO "tz"

#define BLINK 500

// Declare InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
ESP8266WiFiMulti wifiMulti;

// Declare Data point
Point sensor(DEVICE);
int co2Addr = 0x68;  
int co2Value = 0;  // We will store the CO2 value inside this variable.
String dataString = "";   // string containing that data 
uint8_t counter = 0;
unsigned long previousMillisWiFi = 0;
char timeStringBuff[50];
unsigned long currentMillis = 0;

void customBlink(int blinkTime, int blinkGap, int numBlinks);
void checkWiFi();
int readCO2();
void connectWiFi();



void setup() {
  Serial.print("Beginning set-up for K30 sensor code");
  
  //Enable serial
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);  // initialize built-in LED to indicate successful uploads
  digitalWrite(LED_BUILTIN, HIGH);

  //two quick blinks to indicate starting set up
  customBlink(250, 500, 2);

  Wire.begin(); 

  connectWiFi();
  
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

  co2Value = readCO2();

  if (co2Value > 0)
  {
    Serial.print("CO2 Value: ");
    Serial.println(co2Value);

    sensor.clearFields();

    // Data
    sensor.addField("CO2", co2Value);

    DEBUG_UPLOAD("board_test", "sensor_functional");

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
      DEBUG_UPLOAD("board_test", "sensor_nonfunctional");
    }

    //TODO:  move this to customBlink
    // Toggle LED to indicate successful upload
    digitalWrite(LED_BUILTIN, LOW);  // Turn the LED on (Note esp8266 LED is active LOW)
    delay(BLINK);
    digitalWrite(LED_BUILTIN, HIGH);  // turn off LED

  }
  else
  {
    Serial.println("Sensor checksum failed / Communication failure");
  }

  Serial.println();

  Serial.println("Waiting...");
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



/*
  In this example we will do a basic read of the CO2 value and checksum verification.
  For more advanced applications please see the I2C Comm guide.
*/
// This is the default address of the CO2 sensor, 7bits shifted left.
///////////////////////////////////////////////////////////////////
// Function : int readCO2()
// Returns : CO2 Value upon success, 0 upon checksum failure
// Assumes : - Wire library has been imported successfully.
// - LED is connected to IO pin 13
// - CO2 sensor address is defined in co2_addr
///////////////////////////////////////////////////////////////////
int readCO2()
{
  // DEBUG_PRINT("readCO2 was called");
  co2Value = 0;

  //////////////////////////
  /* Begin Write Sequence */
  //////////////////////////
  DEBUG_PRINT("Beginning write sequence...");

  Wire.beginTransmission(co2Addr);
  Wire.write(0x22);
  Wire.write(0x00);
  Wire.write(0x08);
  Wire.write(0x2A);

  Wire.endTransmission();

  DEBUG_PRINT("Completed write sequence");

  /////////////////////////
  /* End Write Sequence. */
  /////////////////////////

  /*
    We wait 10ms for the sensor to process our command.
    The sensors's primary duties are to accurately
    measure CO2 values. Waiting 10ms will ensure the
    data is properly written to RAM

  */

  delay(10);

  /////////////////////////
  /* Begin Read Sequence */
  /////////////////////////
  
  DEBUG_PRINT("Beginning read sequence");
  
  /*
    Since we requested 2 bytes from the sensor we must
    read in 4 bytes. This includes the payload, checksum,
    and command status byte.

  */

  Wire.requestFrom(co2Addr, 4);
  // Wire.requestFrom(co2Addr, 2);

  DEBUG_PRINT("Made wire request");

  byte i = 0;
  byte buffer[4] = {0, 0, 0, 0};

  /*
    Wire.available() is not nessessary. Implementation is obscure but we leave
    it in here for portability and to future proof our code
  */
  while (Wire.available())
  {
    buffer[i] = Wire.read();
    i++;
    DEBUG_PRINT("Filling buffer...");
  }

  DEBUG_PRINT("Finished read sequence");

  ///////////////////////
  /* End Read Sequence */
  ///////////////////////

  /*
    Using some bitwise manipulation we will shift our buffer
    into an integer for general consumption
  */

  co2Value = 0;
  co2Value |= buffer[1] & 0xFF;
  co2Value = co2Value << 8;
  co2Value |= buffer[2] & 0xFF;


  byte sum = 0; //Checksum Byte
  sum = buffer[0] + buffer[1] + buffer[2]; //Byte addition utilizes overflow

  if (sum == buffer[3])
  {
    // Success!
    DEBUG_PRINT("Successfully read+translated data");
    return co2Value;
  }
  else
  {
    // Failure!
    /*
      Checksum failure can be due to a number of factors,
      fuzzy electrons, sensor busy, etc.
    */
    return 0;
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