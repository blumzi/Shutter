#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <elapsedMillis.h>

// https://www.posital.com/en/products/communication-interface/ssi/ssi-encoder.php

#define SSI_CLOCK_PIN	0
#define SSI_DATA_PIN	1
#define LED_PIN			2
#define DEBUG_PIN		3

#define POS_BITS		13
#define POS_MASK		((1 << POS_BITS) - 1)
#define MAX_POS			(1 << POS_BITS)
#define TURN_BITS		12
#define TURN_MASK		((1 << TURN_BITS) - 1)

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
	{ "wo", "",},
};
const int nNetworks = sizeof(knownNetworks) / sizeof(struct knownNetwork);

WiFiServer server(80);

bool debugging() {
	return true;
	// return digitalRead(DEBUG_PIN) == 1;
}

void setup()
{
	pinMode(SSI_CLOCK_PIN, OUTPUT);
	pinMode(SSI_DATA_PIN, INPUT);
	pinMode(LED_PIN, OUTPUT);

	digitalWrite(LED_PIN, HIGH);
	digitalWrite(SSI_CLOCK_PIN, HIGH);

	debugbegin(9600);
	debugln("Wise40 Dome Shutter Server.");

	WiFi.mode(WIFI_STA);
	delay(500);
	ConnectToWifiNetwork();

	server.begin();
	debugln("Server started.");
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
		for (kp = knownNetworks; !connected && (kp - knownNetworks < nNetworks); kp++) {
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
	if (timeFromLastBlink > blinkInterval ) {
		digitalWrite(LED_PIN, LOW);
		delay(500);
		digitalWrite(LED_PIN, HIGH);
		delay(500);
		digitalWrite(LED_PIN, LOW);
		delay(500);
		digitalWrite(LED_PIN, HIGH);
		timeFromLastBlink = 0;
	}
}

int ssi_read_bit() {
	digitalWrite(SSI_CLOCK_PIN, HIGH);
	delayMicroseconds(20);
	int bit = digitalRead(SSI_DATA_PIN);
	delayMicroseconds(20);
	digitalWrite(SSI_CLOCK_PIN, LOW);
	delayMicroseconds(20);

	return bit;
}

unsigned int ssi_read_encoder() {
	unsigned long value = 0;
	int i, turns, pos;

	digitalWrite(SSI_CLOCK_PIN, LOW);				// start clock sequence
	delayMicroseconds(20);
	for (i = 0; i < (TURN_BITS + POS_BITS); i++) {	// pump-out bits
		value |= ssi_read_bit();
		value <<= 1;
	}
	digitalWrite(SSI_CLOCK_PIN, HIGH);				// end clock sequence
	
	pos = value & POS_MASK;
	turns = (value >> POS_BITS) & TURN_MASK;

	return (turns * MAX_POS) + pos;
}

void loop()
{
	blink();

	WiFiClient client = server.available();
	if (!client)
		return;



	while (!client.available())
		delay(1);
	String req = client.readStringUntil('\n');
	String content = "";
	client.flush();

	debug(client.remoteIP());
	debug(":");
	debug(client.remotePort());
	debug(" [");
	debugln(req + "]");

	String reply_head = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>";
	String reply_tail = "<html>";
	int enc_value = 17;

	if (req.indexOf("GET /status HTTP/1.1") != -1) {
		client.println(reply_head + "ok" + reply_tail);
	}
	else if (req.indexOf("GET /encoder HTTP/1.1") != -1) {
		client.print(reply_head);
		client.print(enc_value);
		client.println(reply_tail);
	} else {
		client.println("HTTP/1.1 404 Not found");
	}
	client.stop();
}