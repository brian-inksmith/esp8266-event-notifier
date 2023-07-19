/*
  2020-04-24
  Bryce Dombrowski
  https://github.com/fwacer/esp8266-event-notifier
  
  Inspired from https://github.com/SensorsIot/Reminder-with-Google-Calender
  
*/

// INCLUDES // ************************************************************************************************************************

#include <ESP8266WiFi.h> // Preferences -> "Additional Board Manager URLs" -> http://arduino.esp8266.com/stable/package_esp8266com_index.json
#include <DebugMacros.h>
#include <HTTPSRedirect.h>
#include "credentials.h" // This contains your wifi credentials, and your secret for your Google Apps Script. See below "#ifndef CREDENTIALS" for more info
#include <Adafruit_NeoPixel.h>

// DEFINES // *************************************************************************************************************************


// Which pin on the Arduino is connected to the NeoPixels?
#define PIN D4 // On Trinket or Gemma, suggest changing this to 1

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS 7 // Popular NeoPixel ring size

// When setting up the NeoPixel library, we tell it how many pixels,
// and which pin to use to send signals. Note that for older NeoPixel
// strips you might need to change the third parameter -- see the
// strandtest example for more information on possible values.
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Define pins
#define PIN_DATA  D5 // Shift register SER
#define PIN_CLOCK D6 // Shift register SRCLK
#define PIN_LATCH D7 // Shift register RCLK

// Define the start and end of the day so that the lights turn off late at night
#define DAY_START_HOUR 7
#define DAY_END_HOUR 20

// Define refresh times
#define RESET_TIME 86400000 // Constant interval on which the ESP resets itself. (1 day in milliseconds)
#define UPDATE_TIME 60000 // Constant interval on when to phone home next. (30 minutes in milliseconds)
unsigned long startMillis;
unsigned long currentMillis;
const unsigned long period = 300000; 

// GLOBALS // *************************************************************************************************************************

//Connection Settings
const char* HOST = "script.google.com";
const int HTTPS_PORT = 443;
HTTPSRedirect* CLIENT = nullptr;

#ifndef CREDENTIALS // Include the below in a "credentials.h" file
  const char* SSID_NAME = "........."; // Replace with your ssid
  const char* SSID_PASSWORD = ".........."; // Replace with your password
  const char *GSCRIPT_ID = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"; // Replace with your gscript id for reading the calendar
#endif

unsigned long HEARTBEAT; // Used for blinking the clock LED once per second (variable is in ms)
unsigned long NEXT_UPDATE_TIME; // Used for determining when next to update the calendar (variable is in ms)
unsigned int CURRENT_DISPLAY; // Format of 32-bit binary number: 0b[8-bit MSB Hour][8-bit LSB Hour][8-bit MSB Minute][8-bit LSB Minute]
String CALENDAR_DATA = ""; // String of data received from a google apps script
String NEXT_FREE_TIME = "00:00"; // Format "23:14"
int CURRENT_HOUR = 0; // Used with DAY_START_HOUR and DAY_END_HOUR to determine if we should go into quiet mode
bool BLINK = true;

enum state{
  Busy = 0,
  Free = 1,
  Sleep = 2 // For out-of hours such as late at night
} STATE; // State variable for current on-going event

const byte SEVEN_SEGMENT[] = { // Common anode
  // DP G F E D C B A (I hooked mine up backwards, but just use MSBFIRST instead of LSBFIRST in "shiftOut()" )
  0b11000000, // 0
  0b11111001, // 1
  0b10100100, // 2
  0b10110000, // 3
  0b10011001, // 4
  0b10010010, // 5
  0b10000010, // 6
  0b11111000, // 7
  0b10000000, // 8
  0b10010000, // 9
  0b10101011, // n (10)
  0b10100011, // o (11)
  0b10100111, // c (12)
  0b11111111, // nothing (13)
  0b10000011, // b (14)
  0b10000111, // t (15)
  0b10010010, // S (16)
  0b11000111, // L (17)
  0b10001100, // P (18)
};

/*const byte SEVEN_SEGMENT[] = { // Common cathode
  // DP G F E D C B A
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111, // 9
};*/

// FUNCTIONS // ***********************************************************************************************************************

void displaySevenSegment(unsigned int data){ // Format of 32-bit binary number: 0b[8-bit MSB Hour][8-bit LSB Hour][8-bit MSB Minute][8-bit LSB Minute]
  if (CURRENT_DISPLAY == data) return; // Don't bother running if the display hasn't changed
  CURRENT_DISPLAY = data;
  digitalWrite(PIN_LATCH, LOW);
  digitalWrite(PIN_CLOCK, LOW);
  shiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, (data >>  0) & 0xFF ); // LSB digit of minute
  digitalWrite(PIN_CLOCK, LOW);
  shiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, (data >>  8) & 0xFF); // MSB digit of minute
  digitalWrite(PIN_CLOCK, LOW);
  shiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, (data >> 16) & 0xFF); // LSB digit of hour
  digitalWrite(PIN_CLOCK, LOW);
  shiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, (data >> 24) & 0xFF); // MSB digit of hour
  digitalWrite(PIN_LATCH, HIGH); // Move value to display register
}

void displayClear(){ // Clear the seven segment display
  displaySevenSegment(0xFFFFFFFF);
}

void displaySleep(){ // Shows on the seven segment display the word "SLP"
  unsigned int data =  SEVEN_SEGMENT[16]; // "S"
  data = (data << 8) | SEVEN_SEGMENT[17]; // "L"
  data = (data << 8) | SEVEN_SEGMENT[18]; // "P"
  data = (data << 8) | 0xFF;              // nothing
  displaySevenSegment(data);
}

void displayBoot(){ // Shows on the seven segment display the word "boot"
  unsigned int data =  SEVEN_SEGMENT[14]; // "b"
  data = (data << 8) | SEVEN_SEGMENT[11]; // "o"
  data = (data << 8) | SEVEN_SEGMENT[11]; // "o"
  data = (data << 8) | SEVEN_SEGMENT[15]; // "t"
  displaySevenSegment(data);
}

void displayTime(String timeString){ // Format "HH:MM" 24hr
  Serial.print("Time to display: ");
  Serial.println(timeString);

  bool PM = false; // We are trying to find out if the post medidiem boolean needs to be set
  int hour = timeString.substring(0,timeString.indexOf(":")).toInt();
  if (hour>12){
    PM = true;
    hour -= 12;
  }
  String hourString = String(hour);
  int hourMSB, hourLSB;
  if (hourString.length()>1){ // Two-digit number
    hourMSB = SEVEN_SEGMENT[hourString.substring(0,1).toInt()];
    hourLSB = SEVEN_SEGMENT[hourString.substring(1,2).toInt()];
  }else{ // Single digit
    hourMSB = 0xFF; // nothing
    hourLSB = SEVEN_SEGMENT[hour];
  }
  unsigned int data = 0x00;
  data = hourMSB; // MSB digit of hour
  data = (data << 8) | hourLSB & ~(BLINK ? 0x80 : 0x00) /*Set bit DP if BLINK is true*/; // LSB digit of hour
  data = (data << 8) | SEVEN_SEGMENT[timeString.substring(3,4).toInt()]; // MSB digit of minute
  data = (data << 8) | SEVEN_SEGMENT[timeString.substring(4,5).toInt()] & ~(PM ? 0x80 : 0x00)/*Set bit DP if PM is true (common anode)*/; // LSB digit of minute
  
  displaySevenSegment(data);
}

void displayError(int delayTime = 1000){ // Shows on the seven segment display "no conn" and flashes the LED pink then mustard colour
  // Show on display "no  "
  unsigned int data = SEVEN_SEGMENT[10];  // "n"
  data = (data << 8) | SEVEN_SEGMENT[11]; // "o"
  data = (data << 16) | 0xFFFF; // nothing
  displaySevenSegment(data); // Show on display "no  "
  pixels.fill(pixels.Color(255, 255, 255), 0, 7);
  pixels.show();
  delay(delayTime);
  // Show on display "conn"
  data = 0x00;
  data = (data << 8) | SEVEN_SEGMENT[12]; // "c"
  data = (data << 8) | SEVEN_SEGMENT[11]; // "o"
  data = (data << 8) | SEVEN_SEGMENT[10]; // "n"
  data = (data << 8) | SEVEN_SEGMENT[10]; // "n"
  displaySevenSegment(data); // Show on display "conn"
  delay(delayTime);
  displayClear();
}

void connectToWifi() { // Connect to the wifi
  WiFi.begin(SSID_NAME, SSID_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi connected ");
  Serial.println(WiFi.localIP());

  // Use HTTPSRedirect class to create a new TLS connection
  CLIENT = new HTTPSRedirect(HTTPS_PORT);
  CLIENT->setInsecure();
  CLIENT->setPrintResponseBody(true);
  CLIENT->setContentTypeHeader("application/json");

  Serial.print("Connecting to ");
  Serial.println(HOST);

  // Try to connect for a maximum of 5 times
  bool flag = false;
  for (int i = 0; i < 5; i++) {
    int retval = CLIENT->connect(HOST, HTTPS_PORT);
    if (retval == 1) {
      flag = true;
      break;
    }
    else
      Serial.println("Connection failed. Retrying...");
  }

  if (!flag) {
    Serial.print("Could not connect to server: ");
    Serial.println(HOST);
    for (int i=0; i<2; i++){
      displayError(); // Let user know that the device couldn't connect
      delay(500);
    }
    Serial.println("Exiting...");
    ESP.reset();
  }
  Serial.println("Connected to Google");
}

void getCalendar() { // Gets a String of sent information from the google apps script
  Serial.println("Start Request");
  // HTTPSRedirect CLIENT(HTTPS_PORT);
  unsigned long getCalenderEntry = millis();

  // Try to connect for a maximum of 5 times
  bool flag = false;
  for (int i = 0; i < 2; i++) {
    int retval = CLIENT->connect(HOST, HTTPS_PORT);
    if (retval == 1) {
      flag = true;
      break;
    }
    else
      Serial.println("Connection failed. Retrying...");
  }
  if (!flag) {
    Serial.print("Could not connect to server: ");
    Serial.println(HOST);
    for(int i = 0; i<2; i++){
      displayError();
      delay(500);
    }
    Serial.println("Exiting...");
    ESP.reset();
  }
  //Fetch Google Calendar events
  String url = String("/macros/s/") + GSCRIPT_ID + "/exec";
  CLIENT->GET(url, HOST);
  CALENDAR_DATA = CLIENT->getResponseBody();
  Serial.print("Calendar Data---> ");
  Serial.println(CALENDAR_DATA);
  yield();
}

void classifyEvent(String calendar_data) { // Finds the highest priority event, then does stuff. (Like a state machine)
  Serial.println(calendar_data); // Debug information
  
  int tildeIndex = calendar_data.indexOf("~"); // This is the border of when the next part of the data starts
  int eventColourIndex = -1;
  int i = 12; // There are 11 colours, "i" will cycle through them
  while ((eventColourIndex < 0) && (i > 0)){ // Cycle through the 11 colours
    i = i-1;
    eventColourIndex = calendar_data.indexOf(String(i));
    if (eventColourIndex >= tildeIndex) eventColourIndex = -1; // Don't look at data after the tilde
  } // At this point, "eventColourIndex" either found a calendar event or it didn't.

  // State assignment
  if ( eventColourIndex < tildeIndex && eventColourIndex >= 0){
    STATE = Busy;
    pixels.fill(pixels.Color(150, 0, 0), 0, 7);
    pixels.show();
    Serial.println("Event found.");
  } else { // No event found - display green to show it's clear
    displayClear();
    STATE = Free;
    pixels.fill(pixels.Color(0, 150, 0), 0, 7);
    pixels.show();
    Serial.println("No events found.");
  }

  // Get the calendar's next free time. It looks over the next 14 hours.
  int timeIndex = tildeIndex+1;
  if(timeIndex >= 0 ){
    String newTime = calendar_data.substring(timeIndex, calendar_data.indexOf(",sToPhoneHome=", timeIndex)-3 ); // Grab only "15:23"
    if (newTime != NEXT_FREE_TIME){
      NEXT_FREE_TIME = newTime; // Format is "23:14"
    }
  }else{
    NEXT_FREE_TIME = "00:00"; // Error
    Serial.println("NEXT_FREE_TIME error");
  }

  // Get the number of seconds until we should phone home again.
  /* This takes the minimum time from of a list of all events happening over the next day:
   *  - The end time from the event that will end earliest
   *  - The start time from the event that will start earliest (that hasn't already started)
   */
  int sToPhoneHomeIndex = calendar_data.indexOf("sToPhoneHome=", timeIndex) + 13;
  if(sToPhoneHomeIndex >= timeIndex ){
    unsigned long newTime = calendar_data.substring(sToPhoneHomeIndex, calendar_data.indexOf(",",sToPhoneHomeIndex)).toInt()*1000 + millis();
    if (newTime < NEXT_UPDATE_TIME){ // Check if the time suggested by the google script is less than the next time we were already planning to update
      //NEXT_UPDATE_TIME = newTime;
      NEXT_UPDATE_TIME = millis() + UPDATE_TIME;
      Serial.println("Next updating in "+String(NEXT_UPDATE_TIME-millis())+" ms.");
    }
  }else{
    Serial.println("sToPhoneHomeIndex error");
  }

  int currentTimeIndex = calendar_data.indexOf("currentTime=", timeIndex) + 12;
  if(currentTimeIndex >= sToPhoneHomeIndex ){
    CURRENT_HOUR = calendar_data.substring(currentTimeIndex, calendar_data.indexOf(":", currentTimeIndex) ).toInt(); // Grab only the hour
  }else{
    Serial.println("currentTime error");
  }
}

void setup() { // Main code that runs once, initializes variables.
  Serial.begin(9600);

  startMillis = millis();

  pixels.begin();
  pixels.clear();
  pixels.setBrightness(240);
  
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_LATCH, OUTPUT);
  NEXT_UPDATE_TIME = millis();
  HEARTBEAT = millis();
  STATE = Free;
  
  displayBoot(); // Show "boot" on seven segment display
  
  connectToWifi();
}

void loop() { // Code that runs continuously on a loop
  yield(); // Let the ESP8266 do background stuff if it needs to

  currentMillis = millis();  //get the current "time" (actually the number of milliseconds since the program started)
  if (currentMillis - startMillis >= period)  //test whether the period has elapsed
  {
    getCalendar();
    classifyEvent(CALENDAR_DATA);
    startMillis = currentMillis;  //IMPORTANT to save the start time of the current LED state.
  }
    
  //if (millis() > NEXT_UPDATE_TIME) {
  if (millis() > NEXT_UPDATE_TIME) {
    NEXT_UPDATE_TIME = millis() + UPDATE_TIME;
    getCalendar();
    classifyEvent(CALENDAR_DATA);
  }
  
  if (millis() > HEARTBEAT + 1000) { // Once per second
    HEARTBEAT = millis();

    if ((DAY_END_HOUR <= CURRENT_HOUR) || ( CURRENT_HOUR < DAY_START_HOUR)){ // If it's after hours, turn off the display and LED
      if (STATE != Sleep){
        STATE = Sleep; // Run this code only once
        displaySleep(); // Display "SLP" on seven segment display

        displayClear();  // Turn off display and LED
        NEXT_UPDATE_TIME = (24 - DAY_END_HOUR + DAY_START_HOUR)*3600*1000; // Don't update until the morning.
        Serial.println("Entering sleep mode.");
      } 
    }else if (STATE == Sleep){ // We are now back in working hours, since the first "if" statement was not triggered.
      NEXT_UPDATE_TIME = 0; // On the next run of "loop()", the google apps script will be called.
      
    }else if (STATE == Busy) { // Display next free time if we have an event ongoing
      BLINK = !BLINK; // Blink the clock LED to show that the code is not hung
      displayTime(NEXT_FREE_TIME);
        
    }else{} // STATE == Free
  }
  
  if (millis() > RESET_TIME){ // Reboot regularly
    // I've heard that the String class has poor garbage collection / leaves "holes" in the heap which can lead to strange behaviour.
    // Since I want to use String for ease of programming and readability, the patch solution is to reboot every now and then.
    // Currently resets every day (24 hours)
    Serial.println("Reached 24 hour time limit. Rebooting...");
    ESP.reset();
  }
}
