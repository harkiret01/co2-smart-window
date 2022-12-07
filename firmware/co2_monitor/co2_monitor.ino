/*
 * CO2 Level Detection in Indoor Areas with IoT Application
 * EE 198B Senior Project, San Jose State University (Fall 2022)
 *
 * Authors: Sahil Shah, Harkiret Singh, Christopher Lim, David Gomez
 *
 * This program reads the CO2 concentration of the room using the Sensirion
 * SCD40 CO2 sensor, displays the concentration on a webpage, and notifies the
 * user via SMS (Twilio) when a set threshold is reached. The SMS includes a
 * link (the ESP32's IP address) to a webpage showing the current CO2
 * concentration, where the user can open or close a window. When an option is
 * chosen, a stepper motor rotates until the window is fully open/closed.
 *
 * The webpage HTML/JavaScript is adapted from Rui Santos'
 * (randomnerdtutorials.com) ESP32 async web server tutorials.
 */

/*
 * PINOUT (ESP32-DevKitC, 38-pin; physical pin -> GPIO):
 *
 *   SCD40 VCC   <- 5V / 3.3V      (SCD40 accepts 2.4 - 5.5 V)
 *   SCD40 GND   <- GND
 *   SCD40 SCL   <- Pin 36 (GPIO22, default I2C SCL)
 *   SCD40 SDA   <- Pin 33 (GPIO21, default I2C SDA)
 *
 *   A4988 STEP  <- Pin 24 (GPIO2)
 *   A4988 DIR   <- Pin 23 (GPIO15)
 *
 * GPIO17 and GPIO16 are not wired to anything. They are used as "virtual"
 * flags: the webpage toggles them HIGH via /update, and loop() reads them to
 * decide whether to open or close the window.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <SensirionI2CScd4x.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "twilio.hpp"

// WiFi + Twilio credentials live in secrets.h (not committed).
// Copy secrets.example.h -> secrets.h and fill in your values.
#include "secrets.h"

/* ---------------------------------------------------------------------------
 * CO2 sensor
 * ------------------------------------------------------------------------- */

// The SensirionI2CScd4x library wraps all the I2C address calls into
// easy-to-use functions.
SensirionI2CScd4x co2_sensor;

static const uint16_t co2_threshold = 2000;  // Alert threshold in ppm (~2,000 ppm reduces cognitive function)

uint16_t co2_reading;                  // CO2 concentration reading (ppm)
float temperature_reading;             // Room temperature (C)
float humidity_reading;                // Room relative humidity (%)
String co2_concentration_debug_string; // CO2 concentration string for debugging

/* ---------------------------------------------------------------------------
 * Twilio
 * ------------------------------------------------------------------------- */

static String alert = "";  // Message sent when CO2 exceeds the threshold
static String response;
Twilio *twilio;

/* ---------------------------------------------------------------------------
 * Web server
 * ------------------------------------------------------------------------- */

// The ESP32 receives requests that look like: /update?output=17&state=1
const char *PARAM_INPUT_1 = "output";  // GPIO pin to write to
const char *PARAM_INPUT_2 = "state";   // 1 = HIGH/ON, 0 = LOW/OFF

AsyncWebServer server(80);

/* ---------------------------------------------------------------------------
 * Motor control
 * ------------------------------------------------------------------------- */

#define stepPin 2
#define dirPin 15
#define OPENING_WINDOW 17  // Virtual flag set by the "Open Window" toggle
#define CLOSING_WINDOW 16  // Virtual flag set by the "Close Window" toggle

// Number of steps needed to move the window between fully closed and fully
// open on our proof-of-concept lead-screw model. Tune for your own window.
#define WINDOW_TRAVEL_STEPS 5250
#define STEP_DELAY_US 900

bool open_status = false;   // true = window is opening
bool close_status = false;  // true = window is closing

// Returns the latest CO2 reading as a string for the webpage
String readCO2() {
  return String(co2_reading);
}

// Reads a measurement from the CO2 sensor over I2C
void readCO2Measurement(void) {
  uint16_t error;
  char errorMessage[256];

  error = co2_sensor.readMeasurement(co2_reading, temperature_reading, humidity_reading);
  if (error) {
    Serial.print("Error trying to execute readMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
}

// If the room's CO2 concentration is above the threshold, text the user
void sendTextAlert(void) {
  String IP_addr = WiFi.localIP().toString();

  if (co2_reading > co2_threshold) {
    alert += "CO2 concentration is too high. Current ppm: ";
    alert += String(co2_reading) + " ppm.\n";
    alert += "Please go to " + IP_addr + " to check on latest reading and options to fix CO2 concentration";
    delay(1000);

    bool success = twilio->send_message(to_number, from_number, alert, response);
    if (success) {
      Serial.println("Sent message successfully! Delaying program for 10 seconds to avoid multiple messages being sent.");
      delay(10000);
    } else {
      Serial.println(response);
    }
    alert = "";
  }
}

// Steps the motor a full window's travel in the given direction
void moveWindow(bool open) {
  digitalWrite(dirPin, open ? HIGH : LOW);
  for (int x = 0; x < WINDOW_TRAVEL_STEPS; x++) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(STEP_DELAY_US);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(STEP_DELAY_US);
  }
}

// Checks the webpage toggles and opens or closes the window accordingly
void checkWindowStatus(void) {
  // GPIO17 HIGH -> the user asked to open the window
  if (digitalRead(OPENING_WINDOW) && !digitalRead(CLOSING_WINDOW)) {
    open_status = true;
    close_status = false;
    digitalWrite(OPENING_WINDOW, LOW);  // Reset flag so the window doesn't keep opening
  }
  // GPIO16 HIGH -> the user asked to close the window
  else if (digitalRead(CLOSING_WINDOW) && !digitalRead(OPENING_WINDOW)) {
    close_status = true;
    open_status = false;
    digitalWrite(CLOSING_WINDOW, LOW);  // Reset flag so the window doesn't keep closing
  }
  // Neither (or both) pressed -> do nothing
  else {
    open_status = false;
    close_status = false;
    digitalWrite(OPENING_WINDOW, LOW);
    digitalWrite(CLOSING_WINDOW, LOW);
  }

  Serial.print("Is Window opening? (1 = yes, 0 = no): ");
  Serial.println(open_status);
  Serial.print("Is Window closing? (1 = yes, 0 = no): ");
  Serial.println(close_status);

  if (open_status && !close_status) {
    moveWindow(true);
    open_status = false;
  } else if (close_status && !open_status) {
    moveWindow(false);
    close_status = false;
  }
  // else: window stays where it is
}

/*
 * Webpage (adapted from Rui Santos' DHT sensor web server)
 *
 * %<value>% placeholders are replaced at request time by processor().
 * The script polls /co2_conc every 10 s and sends /update requests when a
 * toggle is flipped.
 */
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <style>
    html {
      font-family: Arial;
      display: inline-block;
      margin: 0px auto;
      text-align: center;
    }
    h2 { font-size: 3.0rem; }
    p { font-size: 3.0rem; }
    .units { font-size: 1.2rem; }
    .co2-labels {
      font-size: 1.5rem;
      vertical-align: middle;
      padding-bottom: 15px;
    }
    body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}
    .switch {position: relative; display: inline-block; width: 120px; height: 68px}
    .switch input {display: none}
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 6px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 3px}
    input:checked+.slider {background-color: #b30000}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
  </style>
</head>
<body>
  <h2>CO2 Monitoring Server</h2>
  <p>
    <i class="fas fa-lungs"></i>
    <span class="co2-labels">CO2 Concentration:</span>
    <span id="co2_conc">%CO2_CONC%</span>
    <sup class="units">ppm</sup>
  </p>
  <p>
    %BUTTONPLACEHOLDER%
  </p>
</body>
<script>
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("co2_conc").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/co2_conc", true);
  xhttp.send();
}, 10000 ) ;

function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  if(element.checked){ xhr.open("GET", "/update?output="+element.id+"&state=1", true); }
  else { xhr.open("GET", "/update?output="+element.id+"&state=0", true); }
  xhr.send();
}
</script>
</html>)rawliteral";

// Returns "checked" if the GPIO pin is HIGH, otherwise ""
String outputState(int output) {
  if (digitalRead(output)) {
    return "checked";
  }
  return "";
}

// Replaces webpage placeholders with live values
String processor(const String &var) {
  if (var == "CO2_CONC") {
    return readCO2();
  } else if (var == "BUTTONPLACEHOLDER") {
    String buttons = "";
    buttons += "<h4>Open Window</h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"17\" " + outputState(OPENING_WINDOW) + "><span class=\"slider\"></span></label>";
    buttons += "<h4>Close Window</h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"16\" " + outputState(CLOSING_WINDOW) + "><span class=\"slider\"></span></label>";
    return buttons;
  }
  return String();
}

// Registers all web server routes and starts the server
void webpageInitialization(void) {
  // Root page: serve index_html, filling placeholders via processor()
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  // Polled by the page every 10 s for the latest reading
  server.on("/co2_conc", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", readCO2());
  });

  // GET <ESP_IP>/update?output=<gpio>&state=<0|1>
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    String inputMessage1;
    String inputMessage2;
    if (request->hasParam(PARAM_INPUT_1) && request->hasParam(PARAM_INPUT_2)) {
      inputMessage1 = request->getParam(PARAM_INPUT_1)->value();
      inputMessage2 = request->getParam(PARAM_INPUT_2)->value();
      int pin = inputMessage1.toInt();
      // Only allow the two window flags to be set from the webpage
      if (pin == OPENING_WINDOW || pin == CLOSING_WINDOW) {
        digitalWrite(pin, inputMessage2.toInt());
      }
    } else {
      inputMessage1 = "No message sent";
      inputMessage2 = "No message sent";
    }
    Serial.print("GPIO: ");
    Serial.print(inputMessage1);
    Serial.print(" - Set to: ");
    Serial.println(inputMessage2);
    request->send(200, "text/plain", "OK");
  });

  server.begin();
}

// Initializes the SCD40 for I2C communication with the ESP32
void sensorInitialization(void) {
  uint16_t error;
  char errorMessage[256];

  Wire.begin();  // Default ESP32 I2C pins: GPIO22 = SCL, GPIO21 = SDA
  co2_sensor.begin(Wire);

  // Stop any previous periodic measurement
  error = co2_sensor.stopPeriodicMeasurement();
  if (error) {
    Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  // Start periodic measurement of CO2, temperature and humidity (every 5 s)
  error = co2_sensor.startPeriodicMeasurement();
  if (error) {
    Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
}

// Prints the CO2 concentration to the serial monitor (debugging)
void debugString(void) {
  co2_concentration_debug_string += String("CO2 Concentration (in ppm): ") + co2_reading + String("\n");
  Serial.println(co2_concentration_debug_string);
  co2_concentration_debug_string = "";
}

void setup() {
  Serial.begin(115200);  // ESP32 default baud rate
  while (!Serial) {
    delay(100);
  }

  Serial.print("Connecting to WiFi network: ");
  Serial.print(ssid);
  Serial.println("...");
  WiFi.begin(ssid, password);

  // Window flag pins (opening / closing / neutral)
  pinMode(OPENING_WINDOW, OUTPUT);
  digitalWrite(OPENING_WINDOW, LOW);
  pinMode(CLOSING_WINDOW, OUTPUT);
  digitalWrite(CLOSING_WINDOW, LOW);

  // Stepper driver pins
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting...");
    delay(500);
  }
  Serial.println("Connected!");
  Serial.println(WiFi.localIP());

  twilio = new Twilio(account_sid, auth_token);

  sensorInitialization();
  webpageInitialization();

  Serial.println("Waiting for first measurement... (5 sec)");
}

void loop() {
  delay(5000);            // SCD40 produces a new reading every 5 s
  readCO2Measurement();   // Read CO2 from the sensor
  // debugString();       // Uncomment to print readings to the serial monitor
  sendTextAlert();        // Text the user if CO2 is too high
  checkWindowStatus();    // Open/close the window if requested from the webpage
}
