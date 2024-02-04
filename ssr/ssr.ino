//
// Target Board: Doit ESP32 DevKit V1
//
// TODO
// - WiFi connection lost when FritzBox reboots
// - integrate into Tasmota
//

#include <WiFi.h>
#include <Wire.h>
#include <AsyncUDP.h>
#include <DallasTemperature.h>

#include "ssr-phase-angles.h"
#include "ssr-wifi-credentials.h"

#define VERSION "1.2 (02.02.2024)"
// #define DEBUG

#define UDP_PORT 1975
#define UDP_BUF 16

#define LED 2

#define GP8403_ADDR 0x5f
#define GP8403_CHANNEL0_REG 0x02
#define GP8403_CHANNEL1_REG 0x04
#define GP8403_RANGE_REG 0x01
#define GP8403_RANGE_5V 0x00
#define GP8403_RANGE_10V 0x11

#define TIMER 1000000  // 1 second

#ifdef DEBUG
#define Sprint(a) (Serial.print(a))
#define Sprintln(a) (Serial.println(a))
#else
#define Sprint(a)
#define Sprintln(a)
#endif

// convert percent into voltage
static const unsigned int phase_angle[] = { PHASE_ANGLES_BOILER3 };

// define your network credentials in wifi-credentials.h
const char *ssid = SSID;
const char *password = PASSWORD;

// holds the output power load values: 0..10000
volatile uint16_t channel0, channel1;

// current temperature
volatile float temp;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

// Set web server port 80 and an async UDP server
WiFiServer server(80);
AsyncUDP udp;

// Setup a oneWire instance and pass to Dallas Temperature sensor
OneWire oneWire(4);
DallasTemperature sensors(&oneWire);

// create a hardware timer for LED
hw_timer_t *timer = NULL;
volatile byte led = LOW;

uint16_t post;
String header;

// timer interrupt routine for visualizing output power load
void IRAM_ATTR timer_interrupt() {
  timerAlarmDisable(timer);
  if (led == HIGH) {
    if (channel0 < 10000) {
      led = LOW;  // led off phase (but only when < 100%)
      long dark;
      if (channel0 > 5000)
        dark = (10000 - channel0) * 20;  // steps 2000
      else
        dark = TIMER - ((channel0 - 2000) * 300);  // steps 30000
      timerAlarmWrite(timer, dark, true);
      digitalWrite(LED, led);
    }
  } else {
    led = HIGH;  // led on phase - short flash
    timerAlarmWrite(timer, 25000, true);
    if (channel0)
      digitalWrite(LED, led);  // but only when > 0%
  }
  timerAlarmEnable(timer);
}

int i2c_write8(uint8_t addr, uint8_t reg, uint8_t data) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(data);
  return Wire.endTransmission();
}

int i2c_write16(uint8_t addr, uint8_t reg, uint16_t data) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(data & 0xff);
  Wire.write((data >> 8) & 0xff);
  return Wire.endTransmission();
}

int set_volt(uint8_t channel, uint16_t volt) {
  if (volt > 10000) {
    Sprint("GP8403 illegal voltage: ");
    Sprintln(volt);
    return 0;
  }

  uint16_t data = (uint16_t)(((float)volt / 10000) * 4095);
  data = data << 4;

  if (channel == 0) {
    channel0 = volt;
    return i2c_write16(GP8403_ADDR, GP8403_CHANNEL0_REG, data);
  } else {
    channel1 = volt;
    return i2c_write16(GP8403_ADDR, GP8403_CHANNEL1_REG, data);
  }
}

int set_percent(uint8_t channel, uint16_t percent) {
  if (percent > 100) {
    Sprint("GP8403 illegal percent: ");
    Sprintln(percent);
    return 0;
  }

  uint16_t volt = phase_angle[percent];
  return set_volt(channel, volt);
}

void log() {
  Sprint("GP8403 channel 0=");
  Sprintln(channel0);
  Sprint("GP8403 channel 1=");
  Sprintln(channel1);
  Sprint("DS18B20 temp=");
  Sprintln(temp);
}

void response_channel(WiFiClient client, uint8_t channel) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: text/plain; charset=utf-8");
  client.println("Connection: close");
  client.println();
  if (channel == 0)
    client.println(channel0);
  else
    client.println(channel1);
}

void response_temp(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: text/plain; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.println(temp);
}

void response_json(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: application/json; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.print("{ ");
  client.print("\"channel0\":\"");
  client.print(channel0);
  client.print("\", \"channel1\":\"");
  client.print(channel1);
  client.print("\", \"temp\":\"");
  client.print(temp);
  client.print("\", \"version\":\"" VERSION);
  client.println("\" }");
}

void response(WiFiClient client) {
  if (header.indexOf("GET /t") >= 0)
    response_temp(client);
  else if (header.indexOf("GET /0") >= 0)
    response_channel(client, 0);
  else if (header.indexOf("GET /1") >= 0)
    response_channel(client, 1);
  else if (header.indexOf("GET /") >= 0)
    response_json(client);
  else if (header.indexOf("POST /v/0/") >= 0) {
    post = header.substring(10).toInt();
    set_volt(0, post);
    response_channel(client, 0);
  } else if (header.indexOf("POST /v/1/") >= 0) {
    post = header.substring(10).toInt();
    set_volt(1, post);
    response_channel(client, 1);
  } else if (header.indexOf("POST /p/0/") >= 0) {
    post = header.substring(10).toInt();
    set_percent(0, post);
    response_channel(client, 0);
  } else if (header.indexOf("POST /p/1/") >= 0) {
    post = header.substring(10).toInt();
    set_percent(1, post);
    response_channel(client, 1);
  }

#ifdef DEBUG
  log();
#endif
}

void udp_handle_packet(AsyncUDPPacket packet) {
  size_t len = packet.length();

  char buf[UDP_BUF + 1];
  uint8_t i;

  if (len == 0 || len > UDP_BUF)
    return;  // ignore invalid packet

  for (i = 0; i < len; i++)
    buf[i] = (char)*(packet.data() + i);
  buf[len] = 0x00;  // null-terminate
  Sprint("UDP received len ");
  Sprintln(packet.length());
  Sprint("UDP received data ");
  Sprintln(buf);

  // find second ':'
  i = 2;
  while (buf[i++])
    if (buf[i] == ':')
      break;

  if (buf[0] == 'v') {

    // voltage command
    set_volt(0, (uint16_t)atoi(buf + 2));
    if (i != len)
      set_volt(1, (uint16_t)atoi(buf + i + 1));

  } else if (buf[0] == 'p') {

    // percent command
    set_percent(0, (uint16_t)atoi(buf + 2));
    if (i != len)
      set_percent(1, (uint16_t)atoi(buf + i + 1));

  } else {
    Sprintln("invalid UDP command received");
  }

#ifdef DEBUG
  log();
#endif
}

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  Sprintln("Firmware version " VERSION);
#endif

  // Set LED pin to OUTPUT
  pinMode(LED, OUTPUT);

  // 1st blink - startup
  digitalWrite(LED, HIGH);
  delay(500);
  digitalWrite(LED, LOW);
  delay(500);

  // Start the DS18B20 sensor and update temperature
  sensors.begin();
  sensors.requestTemperatures();
  temp = sensors.getTempCByIndex(0);

  // initialize I2C
  Wire.begin();
  Wire.setClock(400000);

  // initialize GP8403 with 0..10V
  if (i2c_write8(GP8403_ADDR, GP8403_RANGE_REG, GP8403_RANGE_10V) != 0) {
    Sprintln("GP8403 init failed!");
#ifndef DEBUG
    return;
#endif
  }

  // set both chanels to 0
  if (set_volt(0, 0) != 0) {
    Sprintln("GP8403 write to channel 0 failed!");
#ifndef DEBUG
    return;
#endif
  }

  if (set_volt(1, 0) != 0) {
    Sprintln("GP8403 write to channel 1 failed!");
#ifndef DEBUG
    return;
#endif
  }

  Sprintln("GP8403 initialzed 0..10V");
  log();

  // 2nd blink - I2C OK
  digitalWrite(LED, HIGH);
  delay(500);
  digitalWrite(LED, LOW);
  delay(250);

  // Connect to Wi-Fi network with SSID and password
  Sprint("Connecting to ");
  Sprintln(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Sprint(".");
  }

  // Print local IP address and start web server
  Sprintln("");
  Sprintln("WiFi connected.");
  Sprint("IP address: ");
  Sprintln(WiFi.localIP());
  Sprint("MAC address: ");
  Sprintln(WiFi.macAddress());
  server.begin();

  // setup UDP packet receive handler to accept messages like "v:12345:56789" or "p:50:100"
  if (udp.listen(UDP_PORT))
    udp.onPacket(&udp_handle_packet);

  // 3rd blink - WiFi OK
  digitalWrite(LED, HIGH);
  delay(500);
  digitalWrite(LED, LOW);

  // setup and start the timer
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &timer_interrupt, true);
  timerAlarmWrite(timer, TIMER, true);
  timerAlarmEnable(timer);
}

void loop() {
  WiFiClient client = server.available();  // Listen for incoming clients

  if (client) {  // If a new client connects,
    currentTime = millis();
    previousTime = currentTime;

    Sprintln("New Client.");  // print a message out in the serial port
    String currentLine = "";  // make a String to hold incoming data from the client

    // update temperature
    sensors.requestTemperatures();
    temp = sensors.getTempCByIndex(0);

    while (client.connected() && currentTime - previousTime <= timeoutTime) {  // loop while the client's connected
      currentTime = millis();
      if (client.available()) {  // if there's bytes to read from the client,
        char c = client.read();  // read a byte, then
        Serial.write(c);         // print it out the serial monitor
        header += c;
        if (c == '\n') {  // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            response(client);
            break;
          } else {  // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }

    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Sprintln("Client disconnected.");
    Sprintln("");
  }
}
