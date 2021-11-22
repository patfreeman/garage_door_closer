// ESP8266 Garage Door Sensor / Closer

// Config
const char* ssid        = "<redacted>";                 // WiFi SSID
const char* wifi_psk    = "<redacted>";                 // WiFi Passphrase

const char* host        = "192.168.0.1";                // Home Assitant Server IP
const int   httpPort    = 443;
const char* bearer      = "<redacted>";                 // Home Assistant bearer authentication string.
const char* url         = "/api/states/sensor.garage_door_status";
                                                        // URL to HA device to update

const char* username    = "username";                   // Username to authenticate call to trigger open
const char* password    = "password";                   // ... and password.

const char* host_name   = "garage_door_opener";
const char* ota_pwd     = "<redacted>";
// End Config

#define ESP8266_LED 2                                   // LED on board
#define RELAY_PIN D7
#define BUZZER_PIN D0
#define POLLING_INTERVAL 1000                           // 1 sec = 1000 milliseconds
#define DELAY_BETWEEN_POST 60000                        // 1 minute
#define DELAY_BEFORE_CLOSE 300000                       // 5 minutes
#define DELAY_AFTER_CLOSING 15000                       // > time it takes for door to close
#define DELAY_FOR_HTTP_RESPONSE 150                     // Prevent 499 return code

#define ECHOPIN D2 // HC-SR04 Echo pin
#define TRIGPIN D3 // HC-SR04 Trig pin

int minimum = 30;
int maximum = 40;

unsigned long last_poll = 0;
int last_status = 1;
long int last_post = 0 - DELAY_BETWEEN_POST;
int currstatus = 1;
int door_closing = 0;
int closing = 0;
int buzz_high = 0;
unsigned long closing_start = 0;
unsigned long buzz_start = 0;

long duration; // variable for the duration of sound wave travel
int distance;  // variable for the distance measurement

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>

ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);                                  // Disable AP mode
  WiFi.begin(ssid, wifi_psk);                           // Connect to WiFi
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");                     // Display WiFi info to serial
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(ESP8266_LED, OUTPUT);                         // Allow us to control the LED on the ESP8266
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIGPIN, OUTPUT);
  pinMode(ECHOPIN, INPUT);

  update_status(1);                                     // POST an initial status to Home Assistant
  last_post = 0 - DELAY_BETWEEN_POST;                   // Reset last POST time

  server.on("/", [](){                                  // HTTP respond to /
    server.send(200, "text/plain", String(currstatus));
  });
  server.on("/status", slash_status);
  server.begin();                                       // Start HTTP server

  ArduinoOTA.setHostname(host_name);
  ArduinoOTA.setPassword(ota_pwd);
  ArduinoOTA.onStart([]() {});
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
  ArduinoOTA.onError([](ota_error_t error) {});
  ArduinoOTA.begin();
}

void slash_status() {
  if(!server.authenticate(username, password)){
    server.requestAuthentication();
  }else{
    Serial.println("Authenticated connection to /status");
    if(get_current_status() == 0) {
      Serial.println("closing door");
      digitalWrite(RELAY_PIN, HIGH);
      delay(500);
      digitalWrite(RELAY_PIN, LOW);
      last_poll = millis() + 2000;
      last_status = 1;
    }else{
      Serial.println("opening door");
      digitalWrite(RELAY_PIN, HIGH);
      delay(500);
      digitalWrite(RELAY_PIN, LOW);
      last_poll = millis() + 15000;
      last_status = currstatus = 0;
    }
  }
  server.send(200, "text/plain", String(last_status));
  post_status(last_status);
}

void post_status(int current_status) {
  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect(host, httpPort)) {                // connect to Home Assistant
    Serial.println("https connection failed");
    return;
  }
  last_status=current_status;
  last_post=millis();
  String post;
  switch (current_status) {
    case 0:
      post="{\"state\":\"OPEN\"}";break;
    case 1:
      post="{\"state\":\"CLOSED\"}";break;
    case 2:
      post="{\"state\":\"AUTO_CLOSED\"}";break;
    case 3:
      post="{\"state\":\"AUTO_OPENED\"}";break;
  }
  Serial.println(post);
  client.setTimeout(2000);
  client.print(String("POST ") + url + " HTTP/1.1\r\n" +// Send data to Home Assistant
               "Host: " + host + "\r\n" +
               "User-Agent: esp8266_garage_door_closer\r\n" +
               "Connection: close\r\n" +
               "Content-Type: text/json\r\n" +
               "Authorization: Bearer " + bearer + "\r\n" +
               "Content-Length: " + post.length() + "\r\n\r\n" +
               post + "\r\n");
  delay(DELAY_FOR_HTTP_RESPONSE);
  while(client.available()){
    String line = client.readStringUntil('\r');         // Print HTTP response to serial
    Serial.print(line);
  }
}

void update_status(int current_status) {                // Action function
  Serial.println(current_status);
  if(current_status != last_status) {
    // confirm it is by checking again
    delay(100);
    currstatus=current_status=get_current_status();
    if(current_status != last_status) {
      // confirm it is by checking again
      delay(100);
      currstatus=current_status=get_current_status();
      if( current_status != last_status) {
        post_status(current_status);                        // POST all changes
      }
    }
  } else if (current_status == 0) {
    if(millis() < last_post + DELAY_BEFORE_CLOSE) {
      Serial.println("Not closing due to close delay");
    } else if (!closing) {
      closing_start = buzz_start = millis();
      closing = 1;
    }
  }
}

void loop() {                                           // Main loop
  if(millis() > last_poll + POLLING_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(ESP8266_LED, HIGH);                    // Blink the LED if the WiFi is connected
    }
    currstatus=get_current_status();
    last_poll=millis();
  } else if (millis() > last_poll + (POLLING_INTERVAL/2)) {
    digitalWrite(ESP8266_LED, LOW);
  }
  update_status(currstatus);                            // Run POST function
  server.handleClient();                                // Respond to HTTP requests
  ArduinoOTA.handle();
  if (closing) {
    Serial.println("in closing");
    process_closing();
  }
}

int get_current_status() {
  digitalWrite(TRIGPIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGPIN, HIGH); // Sets the trigPin HIGH (ACTIVE) for 10 microseconds
  delayMicroseconds(10);
  digitalWrite(TRIGPIN, LOW);
  duration = pulseIn(ECHOPIN, HIGH); // Reads the echoPin, returns the sound wave travel time in microseconds
  distance = duration * 0.034 / 2; // Speed of sound wave divided by 2 (go and back)
  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" cm");
  if(distance > maximum) { // closed
    return 1;
  } else if(distance > minimum) { // open
    return 0;
  }
  return currstatus; // somthing blocking, like a spider, just assume no change
}

void process_closing() {
  if (door_closing) {                                   // Door actively closing
    if (millis() > closing_start + DELAY_AFTER_CLOSING) {
      closing = door_closing = 0;                       // All done closing
    }
  } else if (millis() > closing_start + 30000) {        // Done buzzing, start the close
    buzz_high = 0;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("closing door");
    digitalWrite(RELAY_PIN, HIGH);
    delay(500);
    digitalWrite(RELAY_PIN, LOW);
    last_status = 2;
    post_status(last_status);
    door_closing = 1;
  } else if (millis() > buzz_start + 1000) {            // Warning Buzzer
    if (buzz_high) {
      digitalWrite(BUZZER_PIN, LOW);
      buzz_high = 0;
    } else {
      digitalWrite(BUZZER_PIN, HIGH);
      buzz_high = 1;
    }
    buzz_start = millis();
    currstatus=get_current_status();
    if(currstatus == 1) {
      last_status = 1;
      closing = buzz_high = 0;
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}
