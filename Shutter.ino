 #include <NewPing.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <elapsedMillis.h>
#include <pins_arduino.h>
#include <Arduino.h>

// LoLin NodeMCU V3, ESP-12E

// https://www.posital.com/en/products/communication-interface/ssi/ssi-encoder.php

static const uint8_t ssi_data_pin = D2;		// ssi data
static const uint8_t ssi_clk_pin = D3;		// ssi clk
static const uint8_t enc_preset_pin = D7;	// zeroes the encoder
static const uint8_t led_pin = D5;			// i'm alive LED
static const uint8_t debug_pin = D6;		// if grounded, debug to serial port

static const uint8_t echo_pin = D2;     // HC-SR04 echo pin
static const uint8_t trig_pin = D3;     // HC-SR04 trig pin
static const uint8_t mode_pin = D8;     // selects between draw-wire encoder and ultra-sonic range finder

NewPing sonar(trig_pin, echo_pin, 400); // NewPing setup of pins and maximum distance.

static const String version = "1.1";
bool serialWasInitialized = false;

#define POS_BITS		12
#define POS_MASK		((1 << POS_BITS) - 1)
#define MAX_POS			(1 << POS_BITS)
#define TURN_BITS		12
#define TURN_MASK		((1 << TURN_BITS) - 1)
#define TOTAL_BITS		25

#define DEBUG
#ifdef DEBUG
#define debug(...)		if (debugging()) Serial.print(__VA_ARGS__)
#define debugln(...)	if (debugging()) Serial.println(__VA_ARGS__)
#else
#define	debug(...)
#define debugln(...)
#endif // DEBUG

unsigned int lookAliveInterval = 5000;
elapsedMillis timeFromLastlookAlive = 0;
elapsedMillis timeFromStartZeroing = 0;
elapsedMillis timeFromSonarRead = 0;
bool zeroing = false;

enum OpMode { WIRE = 0, SONAR = 1 };
OpMode opMode;

struct knownNetwork {
  const char* ssid;
  const char* password;
} knownNetworks[] = {
  //{ "TheBlumz", "***",},
  { "brutus", "negev2008" },
  { "Free-TAU", "Free-TAU" },
  { "wo", "", },
};
const int nKnownNetworks = sizeof(knownNetworks) / sizeof(struct knownNetwork);

WiFiServer server(80);

//
// Returns true if the debug_pin has been grounded
//
bool debugging() {
  if (digitalRead(debug_pin) != 0)	// the debug_pin must be grounded for DEBUG mode
    return false;

  if (!serialWasInitialized) {
    Serial.begin(9600);
    serialWasInitialized = true;
  }

  return true;
}

void setup()
{
  pinMode(ssi_clk_pin, OUTPUT);
  digitalWrite(ssi_clk_pin, HIGH);

  pinMode(ssi_data_pin, INPUT);

  pinMode(led_pin, OUTPUT);
  digitalWrite(led_pin, HIGH);

  pinMode(enc_preset_pin, OUTPUT);
  digitalWrite(enc_preset_pin, LOW);

  pinMode(debug_pin, INPUT_PULLUP);

  pinMode(mode_pin, INPUT_PULLUP);
  opMode = digitalRead(mode_pin) == 0 ? WIRE : SONAR;

  debugln("\nWise40 Dome Shutter Server.");

  connectWifi();

  server.begin();
  debugln("Server started.");
  digitalWrite(led_pin, LOW);
}

String enc_type(uint8_t t) {
  switch (t) {
    case ENC_TYPE_WEP:	return "WEP";
    case ENC_TYPE_TKIP:	return "TKIP(WPA)";
    case ENC_TYPE_CCMP:	return "CCMP(WPA)";
    case ENC_TYPE_NONE:	return "NONE";
    case ENC_TYPE_AUTO:	return "AUTO";
    default:			return "Unknown";
  }
}

void connectWifi() {
  int n, nDetected;
  struct knownNetwork *kp;
  bool connected = false;

  nDetected = 0;
  do {
    debugln("Detecting networks ...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(500);
    nDetected = WiFi.scanNetworks();
    debug("Detected ");
    debug(nDetected);
    debugln(" networks");
  } while (nDetected == 0);

  for (n = 0; n < nDetected; n++) {
    debug("SSID: \"");
    debug(WiFi.SSID(n));
    debug("\", RSSI: ");
    debug(WiFi.RSSI(n));
    debug(" dB, Enc: ");
    debugln(enc_type(WiFi.encryptionType(n)));
  }
  debugln("");

  for (kp = &knownNetworks[0]; !connected && (kp - knownNetworks < nKnownNetworks); kp++) {
    int attempts, attempt;

    for (attempts = 5, attempt = 0; attempt < attempts; attempt++) {
      const char *passwd = (WiFi.encryptionType(n) == ENC_TYPE_NONE) ? "" : kp->password;

      debug("\nAttempting to connect to \"");
      debug(kp->ssid);
      debug("\", passwd: \"");
      debug(passwd);
      debug("\" attempt #");
      debug(attempt);
      debugln("");

      IPAddress ip(192, 168,1,6);
      IPAddress subnet(255,255,255,0);
      IPAddress gateway(192,168,1,1);
      IPAddress dns(132,66,65,135);

      WiFi.config(ip, dns, gateway, subnet);
      WiFi.begin(kp->ssid, passwd);

      elapsedMillis timeFromConnect = 0;
      while ((WiFi.status() != WL_CONNECTED) && timeFromConnect < 10000) {
        delay(200);
        debug(".");
      }

      if (WiFi.status() == WL_CONNECTED) {
        debugln("");
        debug(" Connected to \"");
        debug(kp->ssid);
        debug("\" as ");
        debug(WiFi.localIP());
        debug(", MAC: ");
        debugln(WiFi.macAddress());
        connected = true;
        break;
      }
      else
        WiFi.disconnect();
    }
  }
}

void blink(int nblinks) {
  static int interval = 400;

  for (int i = 0; i < nblinks; i++) {
    digitalWrite(led_pin, HIGH);
    delayMicroseconds(interval);
    digitalWrite(led_pin, LOW);
    delayMicroseconds(interval);
  }
}

void lookAlive() {
  static unsigned int lookAlive_delay = 500;
  static unsigned int intervals[] = {
    lookAliveInterval + 0 * lookAlive_delay,	// 0: go high
    lookAliveInterval + 1 * lookAlive_delay,	// 1: go low
    lookAliveInterval + 2 * lookAlive_delay,	// 2: go high
    lookAliveInterval + 3 * lookAlive_delay,	// 3: go low and reset
  };
  static int counter = 0;
  static int state = LOW;

  if (timeFromLastlookAlive > intervals[counter]) {
    state ^= 1;
    digitalWrite(led_pin, state);
    counter++;
    if (counter == 4) {
      counter = 0;
      timeFromLastlookAlive = 0;
    }
  }
}

unsigned int read_range_cm() {
  unsigned int cm = sonar.convert_cm(sonar.ping_median(10));
  
  debug("sonar: "); debug(cm); debugln(" cm");  
  return cm;
}

int ssi_read_bit() {
  digitalWrite(ssi_clk_pin, LOW);
  delayMicroseconds(6);
  digitalWrite(ssi_clk_pin, HIGH);
  delayMicroseconds(5);
  return digitalRead(ssi_data_pin);
}

unsigned long ssi_read_single() {
  int i, bit;
  unsigned long value = 0;

  for (i = 0; i < TOTAL_BITS; i++) {	// pump-out bits
    bit = ssi_read_bit();
    value |= bit;
    value <<= 1;
  }
  return value;
}

void debug_single(unsigned long value) {
  String *s = new String();

  for (int i = TOTAL_BITS - 1; i >= 0; i--) {
    *s += (value & (1 << i)) ? "1" : "0";
  }
  debugln(*s);
  delete s;
}

//
// Performs an encoder multi-transmission (see https://www.posital.com/en/products/communication-interface/ssi/ssi-encoder.php).
// The value is read twice, with a single clock cycle in-between.  If the two values do not match, they are thrown away.
//
String ssi_read_encoder() {
  unsigned long value[2] = { 0, 0 },  v;
  int turns, pos;

  do {
    value[0] = ssi_read_single();				// read first value
    digitalWrite(ssi_clk_pin, LOW);
    delayMicroseconds(5);
    digitalWrite(ssi_clk_pin, HIGH);
    delayMicroseconds(5);
    value[1] = ssi_read_single();				// read second value

    if (value[0] != value[1]) {
      debug("mismatch: ");
      debug(value[0]);
      debug(" != ");
      debugln(value[1]);
    } else
      debug_single(value[0]);

  } while (value[0] != value[1]);

  digitalWrite(ssi_clk_pin, HIGH);
  delayMicroseconds(25);

  pos = value[0] & POS_MASK;
  turns = (value[0] >> 13) & TURN_MASK;

  v = (turns * MAX_POS) + pos;
  debug("turns: ");
  debug(turns);
  debug(", pos: ");
  debug(pos);
  debug(" => ");
  debugln(v);
  return String(v);
}


String help() {
  if (opMode == WIRE)
    return String("<table>"
                " <tr><th align='left'>Cmd</th><th align='left'>Arg</th><th align='left'>Desc</th></tr>"
                " <tr><td>help</td><td/><td>shows this help<td></tr>"
                " <tr><td>encoder</td><td/><td>gets the current encoder value</td></tr>"
                " <tr><td>status</td><td/><td>prints \"ok\", if alive</td></tr>"
                " <tr><td>version</td><td/><td>prints the software version</td></tr>"
                " <tr><td>zero</td><td>?password=******</td><td>zeroes the encoder</td></tr>"
                "</table>");
   else
    return String("<table>"
                " <tr><th align='left'>Cmd</th><th align='left'>Arg</th><th align='left'>Desc</th></tr>"
                " <tr><td>help</td><td/><td>shows this help<td></tr>"
                " <tr><td>range</td><td/><td>gets the current range in cm</td></tr>"
                " <tr><td>status</td><td/><td>prints \"ok\", if alive</td></tr>"
                " <tr><td>version</td><td/><td>prints the software version</td></tr>"
                "</table>");    
}

String make_http_reply(String req) {

  String content, reply;

  debugln("\n[Request - start]");
  debug(req);
  debugln("[Request - end]\n");

  if (req.indexOf("GET /status HTTP/1.1") != -1) {
    content = String("ok");
  }
  else if (opMode == WIRE && req.indexOf("GET /encoder HTTP/1.1") != -1) {
    content = ssi_read_encoder();
  }
  else if (opMode == SONAR && req.indexOf("GET /range HTTP/1.1") != -1) {
    content = read_range_cm();
  }
  else if (opMode == WIRE && req.indexOf("GET /zero?password=ne%27Gev HTTP/1.1") != -1) {
    zeroing = true;
    digitalWrite(enc_preset_pin, HIGH);
    timeFromStartZeroing = 0;
    content = String("encoder zeroed");
  }
  else if (req.indexOf("GET /help HTTP/1.1") != -1) {
    content = help();
  }
  else if (req.indexOf("GET /version HTTP/1.1") != -1) {
    content = String(version);
  }

  if (content.length() != 0) {
    content = String("\r\n"
                     "<!DOCTYPE HTML>\r\n"
                     "<html>") + content + String("</html>");

    reply = String("HTTP/1.1 200 OK\r\n"
                   "Pragma: no-cache\r\n"
                   "Cache-Control: no-cache\r\n"
                   "Connection: close\r\n"
                   "Content-Type: text/html\r\n"
                   "Content-Length: ") + String(content.length()) + String("\r\n") +
            content;
  }
  else
    reply = String("HTTP/1.1 404 Not found");

  debugln("\n[Reply - start]");
  debugln(reply);
  debugln("[Reply - end]\n");
  return reply;
}

void loop()
{
  lookAlive();
  
  if (opMode == SONAR)
    read_range_cm();

  if (opMode == WIRE && zeroing && timeFromStartZeroing >= 120) {
    //
    // From: https://www.posital.com/media/en/fraba/productfinder/posital/datasheet-ixarc-ocd-sx_1.pdf
    //
    // The encoder value will be set to 0 after the preset input was active
    // for 100 ms and changes to inactive again
    //
    digitalWrite(enc_preset_pin, LOW);
    zeroing = false;
  }

  WiFiClient client = server.available();
  if (client)
  {
    debug("\n[Client connected ");
    debug(client.remoteIP());
    debug(":");
    debug(client.remotePort());
    debug("]\n");

    String request;
    while (client.connected())
    {
      // read line by line what the client (web browser) is requesting
      if (client.available())
      {
        String line = client.readStringUntil('\r');
        request += line;

        // wait for end of client's request, that is marked with an empty line
        if (line.length() == 1 && line[0] == '\n')
        {
          client.println(make_http_reply(request));
          break;
        }
      }
    }
    delay(1); // give the web browser time to receive the data

    // close the connection:
    client.stop();
    debugln("[Client disconnected]");
    blink(3);
  }
}
