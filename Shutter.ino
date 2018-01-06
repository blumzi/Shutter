#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <elapsedMillis.h>
#include <pins_arduino.h>
#include <Arduino.h>

// LoLin NodeMCU V3, ESP-12E

// https://www.posital.com/en/products/communication-interface/ssi/ssi-encoder.php

static const uint8_t ssi_data_pin = D2;		// ssi data
static const uint8_t ssi_clk_pin = D3;		// ssi clk
static const uint8_t enc_preset_pin = D4;	// zeroes the encoder
static const uint8_t led_pin = D5;			// i'm alive blinker
static const uint8_t debug_pin = D6;		// if grounded, debug to serial port

static const String version = "1.0";
static int ssi_clock_width = 200;

#define POS_BITS		12
#define POS_MASK		((1 << POS_BITS) - 1)
#define MAX_POS			(1 << POS_BITS)
#define TURN_BITS		12
#define TURN_MASK		((1 << TURN_BITS) - 1)
#define TOTAL_BITS		25

#define DEBUG
#ifdef DEBUG
#define debug(...)		debugging() && Serial.print(__VA_ARGS__)
#define debugln(...)	debugging() && Serial.println(__VA_ARGS__)
#define debugbegin(x)	Serial.begin(x)
#else
#define	debug(...)
#define debugln(...)
#define debugbegin(x)
#endif // DEBUG

unsigned int blinkInterval = 5000;
elapsedMillis timeFromLastBlink = 0;

struct knownNetwork {
	const char* ssid;
	const char* password;
} knownNetworks[] = {
	{ "TheBlumz", "gandalph1",},
	{ "TheBlumz5", "gandalph1", },
	{ "wo", "",},
};
const int nNetworks = sizeof(knownNetworks) / sizeof(struct knownNetwork);

WiFiServer server(80);

bool debugging() {
	return digitalRead(debug_pin) == 0;
}

void setup()
{
	pinMode(ssi_clk_pin, OUTPUT);
	digitalWrite(ssi_clk_pin, HIGH);

	pinMode(ssi_data_pin, INPUT);

	pinMode(led_pin, OUTPUT);
	digitalWrite(led_pin, HIGH);

	pinMode(debug_pin, INPUT_PULLUP);

	debugbegin(9600);
	debugln("\nWise40 Dome Shutter Server.");

	WiFi.mode(WIFI_STA);
	delay(500);
	ConnectToWifiNetwork();

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

void ConnectToWifiNetwork() {
	int n, nDetected;
	struct knownNetwork *kp;
	bool connected = false;

	nDetected = WiFi.scanNetworks();
	debug("Detected ");
	debug(nNetworks);
	debugln(" networks");

	for (n = 0; n < nDetected; n++) {
		debug("SSID: \"");
		debug(WiFi.SSID(n));
		debug("\", RSSI: ");
		debug(WiFi.RSSI(n));
		debug(" dB, Enc: ");
		debugln(enc_type(WiFi.encryptionType(n)));
	}

	for (n = 0; n < nDetected; n++) {
		for (kp = &knownNetworks[0]; !connected && (kp - knownNetworks < nNetworks); kp++) {
			int attempts, attempt;

			for (attempts = 5, attempt = 0; attempt < attempts; attempt++) {
				if (WiFi.SSID(n) == kp->ssid) {
					const char *passwd = (WiFi.encryptionType(n) == ENC_TYPE_NONE) ? "" : kp->password;
					debug("Attempting to connect to \"");
					debug(kp->ssid);
					debug("\", passwd: \"");
					debug(passwd);
					debug("\" attempt #");
					debug(attempt);
					debug(" ... ");
					WiFi.begin(kp->ssid, passwd);
					elapsedMillis timeFromConnect = 0;
					while ((WiFi.status() != WL_CONNECTED) && timeFromConnect < 10000) {
						delay(100);
						debug(".");
					}
				}

				if (WiFi.status() == WL_CONNECTED) {
					debug(" Connected to \"");
					debug(kp->ssid);
					debug("\" as ");
					debugln(WiFi.localIP());
					connected = true;
					break;
				}
				else
					WiFi.disconnect();
			}
		}
	}
}

void blink() {
	static int blink_delay = 500;

	if (timeFromLastBlink > blinkInterval) {
		digitalWrite(led_pin, HIGH);
		delay(blink_delay);
		digitalWrite(led_pin, LOW);
		delay(blink_delay);
		digitalWrite(led_pin, HIGH);
		delay(blink_delay);
		digitalWrite(led_pin, LOW);

		timeFromLastBlink = 0;
	}
}

int ssi_read_bit() {
	int bit;

	digitalWrite(ssi_clk_pin, LOW);
	delayMicroseconds(100);
	digitalWrite(ssi_clk_pin, HIGH);
	delayMicroseconds(100);
	bit = digitalRead(ssi_data_pin);

	return bit;
}

unsigned long ssi_read() {
	int i, bit;
	unsigned long value = 0;

	for (i = 0; i < (TOTAL_BITS); i++) {	// pump-out bits
		digitalWrite(ssi_clk_pin, LOW);
		delayMicroseconds(5);
		digitalWrite(ssi_clk_pin, HIGH);
		delayMicroseconds(5);
		bit = digitalRead(ssi_data_pin);
		value |= bit;
		value <<= 1;
	}

	return value;
}

//
// Performs an encoder multi-transmission (see https://www.posital.com/en/products/communication-interface/ssi/ssi-encoder.php).
// The value is read twice, with a single clock cycle in-between.  If the two values do not match, they are thrown away.
//
String ssi_read_encoder() {
	unsigned long value[2] = { 0, 0 },  v;
	int i, turns, pos, bit;

	do {
		value[0] = ssi_read();				// read first value
		digitalWrite(ssi_clk_pin, LOW);
		delayMicroseconds(5);
		digitalWrite(ssi_clk_pin, HIGH);
		delayMicroseconds(5);
		value[1] = ssi_read();				// read second value
		if (value[0] != value[1]) {
			debug("mismatch: ");
			debug(value[0]);
			debug(" != ");
			debugln(value[1]);
		}
	} while (value[0] != value[1]);
	delayMicroseconds(20);

	pos = value[0] & POS_MASK;
	turns = (value[0] >> 13) & TURN_MASK;

	v = (turns * MAX_POS) + pos;
	debugln(v);
	return String(v);
}

//
// From: https://www.posital.com/media/en/fraba/productfinder/posital/datasheet-ixarc-ocd-sx_1.pdf
//
// The encoder value will be set to 0 after the preset input was active
// for 100 ms and changes to inactive again
//
void enc_set_to_zero() {
	digitalWrite(enc_preset_pin, HIGH);
	delay(120);
	digitalWrite(enc_preset_pin, LOW);
}

String help() {
	return String("<table>"
		" <tr><th align='left'>Cmd</th><th align='left'>Arg</th><th align='left'>Desc</th></tr>"
		" <tr><td>help</td><td/><td>shows this help<td></tr>"
		" <tr><td>encoder</td><td/><td>gets the current encoder value</td></tr>"
		" <tr><td>status</td><td/><td>prints \"ok\", if alive</td></tr>"
		" <tr><td>version</td><td/><td>prints the software version</td></tr>"
		" <tr><td>zero</td><td>?password=******</td><td>zeroes the encoder</td></tr>"
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
	else if (req.indexOf("GET /encoder HTTP/1.1") != -1) {
		content = ssi_read_encoder();
	}
	else if (req.indexOf("GET /zero?password=ne'Gev HTTP/1.1") != -1) {
		enc_set_to_zero();
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
	//blink();
	ssi_read_encoder();
	//delay(1000);
	return;


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
		Serial.println("[Client disconnected]");
	}
}
