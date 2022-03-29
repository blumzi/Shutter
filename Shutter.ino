#include <NewPing.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <elapsedMillis.h>
#include <pins_arduino.h>
#include <Arduino.h>

// LoLin NodeMCU V3, ESP-12E

static const uint8_t echo_pin       = D2; // HC-SR04 echo pin
static const uint8_t trig_pin       = D3; // HC-SR04 trig pin
static const uint8_t led_pin        = D5;	// i'm alive LED
static const uint8_t debug_pin      = D6;	// if grounded, debug to serial port

NewPing sonar(trig_pin, echo_pin, 400); // NewPing setup of pins and maximum distance.

static const String version = "1.2";
bool serialWasInitialized = false;

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

WiFiServer server(80);

//
// Returns true if the debug_pin has been grounded
//
bool debugging() {
  return true;
  if (digitalRead(debug_pin) != 0)	// the debug_pin must be grounded for DEBUG mode
    return false;

  if (!serialWasInitialized) {
    Serial.begin(115200);
    serialWasInitialized = true;
  }

  return true;
}

void setup()
{
  Serial.begin(115200);
  serialWasInitialized = true;
  
  pinMode(led_pin, OUTPUT);
  digitalWrite(led_pin, HIGH);              // LED ON till WiFi is connected and server started
  pinMode(debug_pin, INPUT_PULLUP);

  debugln("\nWise40 Dome Shutter Server.");

  connectWifi();

  server.begin();
  debugln("Server started.");
  digitalWrite(led_pin, LOW);               // LED OFF, we're in business
}

String enc_type(uint8_t t) {
  switch (t) {
    case ENC_TYPE_WEP:	return "WEP";
    case ENC_TYPE_TKIP:	return "TKIP(WPA)";
    case ENC_TYPE_CCMP:	return "CCMP(WPA)";
    case ENC_TYPE_NONE:	return "NONE";
    case ENC_TYPE_AUTO:	return "AUTO";
    default:			      return "Unknown";
  }
}


String connection_status(int status) {
  switch (status) {
    case WL_NO_SHIELD:      return "No shield" ;
    case WL_IDLE_STATUS:    return "Idle" ;
    case WL_NO_SSID_AVAIL:  return "No SSID available" ;
    case WL_SCAN_COMPLETED: return "Scan completed" ;
    case WL_CONNECTED:      return "Connected" ;
    case WL_CONNECT_FAILED: return "Connect failed" ;
    case WL_CONNECTION_LOST:return "Connection lost" ;
    case WL_WRONG_PASSWORD: return "Wrong password" ;
    case WL_DISCONNECTED:   return "Disconnected" ;
    default:                return "Unknown";
  }
}

void connectWifi() {
  int i, nDetected;
  String ssid, password;

  while (WiFi.status() != WL_CONNECTED) {
    
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

    password = String("not-detected");
  
    for (i = 0; i < nDetected; i++) {
      ssid = WiFi.SSID(i);
  
      debug("SSID: \"");
      debug(ssid);
      debug("\", RSSI: ");
      debug(WiFi.RSSI(i));
      debug(" dB, Enc: ");
      debugln(enc_type(WiFi.encryptionType(i)));
  
      if (ssid.compareTo("TheBlumz") == 0) {
        password = String("gandalph1");
        break;
      } else if (ssid.compareTo("brutus") == 0) {
        password = String("negev2008");
        break;
      } else if (ssid.compareTo("wo") == 0) {
        password = String("");
        break;
      }
    }
  
    if (password.compareTo("not-detected") == 0) {
      debugln("Did not detect a known WIFI network, re-scanning!");
      continue;
    }

    int attempts, attempt;
  
    for (attempts = 5, attempt = 0; attempt < attempts; attempt++) {
  
      debug("\nAttempting to connect to \"");
      debug(ssid);
      debug("\", passwd: \"");
      debug(password);
      debug("\" attempt #");
      debug(attempt);
      debugln("");
  
      if (ssid.compareTo("wo") == 0) {
        IPAddress ip(192, 168, 1, 6);
        IPAddress subnet(255, 255, 255, 0);
        IPAddress gateway(192, 168, 1, 1);
        IPAddress dns(132, 66, 65, 135);
  
        WiFi.config(ip, dns, gateway, subnet);
      }
      WiFi.begin(ssid.c_str(), password.c_str());   // start connecting to the known network
  
      elapsedMillis timeFromConnect = 0;
      while ((WiFi.status() != WL_CONNECTED) && timeFromConnect < 30000) {    // try for 30 seconds to connect to the known network
        delay(200);
        debug(".");
      }
  
      if (WiFi.status() == WL_CONNECTED) {
        debugln();
        debug("WiFi status: ");
        debug(connection_status(WiFi.status()));
        debug(", Connected to \"");
        debug(ssid);
        debug("\" as ");
        debug(WiFi.localIP());
        debug(", MAC: ");
        debugln(WiFi.macAddress());
        debugln();
        return;
      }
      else
      {
        debugln();
        debug("After ");
        debug(timeFromConnect);
        debug(" millis, WiFi status: ");
        debug(connection_status(WiFi.status()));
        debug(" . Giving up on SSID \"");
        debug(ssid);
        debugln("\"");
        WiFi.disconnect();
        delay(100);
      }
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

  debug("sonar range: "); debug(cm); debug(" cm");
  debug(", Wifi: "); debugln(connection_status(WiFi.status()));
  
  return cm;
}

String help() {
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
  else if (req.indexOf("GET /range HTTP/1.1") != -1) {
    content = String(read_range_cm());
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
  
  read_range_cm();    // for debugging

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

        // wait for end of client's request, marked by an empty line
        if (line.length() == 1 && line[0] == '\n')
        {
          client.println(make_http_reply(request));
          break;
        }
      }
    }
    delay(1000); // give the web browser time to receive the data

    // close the connection:
    client.stop();
    debugln("[Client disconnected]");
    blink(2);
  }
}
