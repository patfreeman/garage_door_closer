// ESP8266 Garage Door Sensor / Closer

// Config
const char* ssid        = "<redacted>";                 // WiFi SSID
const char* wifi_psk    = "<redacted>";                 // WiFi Passphrase

const char* host        = "192.168.0.1";                // Home Assitant Server IP
const int   httpPort    = 443;
const char* bearer      = "<redacted>";                 // Home Assistant bearer authentication string.
const char* url         = "/api/states/sensor.garage_door_status";
                                                        // URL to HA device to update
const char fingerprint[] PROGMEM = "01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 FF";
                                                        // HTTPS cert fingerprint for HA server

const char* username    = "username";                   // Username to authenticate call to trigger open
const char* password    = "password";                   // ... and password.
// End Config

#define ESP8266_LED 2                                   // LED on board
#define SENSOR_PIN D8
#define RELAY_PIN D7
#define BUZZER_PIN D0
#define POLLING_INTERVAL 1000                           // 1 sec = 1000 milliseconds
#define DELAY_BETWEEN_POST 60000                        // 1 minute
#define DELAY_BEFORE_CLOSE 300000                       // 5 minutes
#define DELAY_AFTER_CLOSING 15000                       // > time it takes for door to close
#define DELAY_FOR_HTTP_RESPONSE 150                     // Prevent 499 return code

unsigned long currtime;
int last_status = 1;
long int last_post = 0 - DELAY_BETWEEN_POST;
int currstatus = 1;

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>

ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);                                  // Disable AP mode
  WiFi.begin(ssid, wifi_psk);                           // Connect to WiFi
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");                     // Display WiFi info to serial
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  pinMode(ESP8266_LED, OUTPUT);                         // Allow us to control the LED on the ESP8266
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  update_status(1);                                     // POST an initial status to Home Assistant
  last_post = 0 - DELAY_BETWEEN_POST;                   // Reset last POST time

  server.on("/", [](){                                  // HTTP respond to /
    server.send(200, "text/plain", String(currstatus));
  });
  server.on("/status", slash_status);
  server.begin();                                       // Start HTTP server
}

void slash_status() {
  if(!server.authenticate(username, password)){
    server.requestAuthentication();
  }else{
    Serial.println("Authenticated connection to /status");
    if(currstatus == 0) {
      Serial.println("closing door");
      digitalWrite(RELAY_PIN, HIGH);
      delay(500);
      digitalWrite(RELAY_PIN, LOW);
      last_status = 1;
    }else{
      Serial.println("opening door");
      digitalWrite(RELAY_PIN, HIGH);
      delay(500);
      digitalWrite(RELAY_PIN, LOW);
      last_status = 0;
    }
  }
  server.send(200, "text/plain", String(last_status));
  delay(DELAY_AFTER_CLOSING);
  post_status(last_status);
}

void post_status(int current_status) {
  WiFiClientSecure client;
  client.setFingerprint(fingerprint);
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


void initiate_close() {
  for (int i=0; i<15; i++) {                            // Make some noise for 30 seconds
    digitalWrite(BUZZER_PIN, HIGH);
    delay(1000);
    server.handleClient();                              // Respond to HTTP requests
    digitalWrite(BUZZER_PIN, LOW);
    currstatus=digitalRead(SENSOR_PIN);                 // Check to see if door was manually closed
    if(currstatus == 1) {
      last_status = 1;
      return;                                           // If so exit
    }
    delay(1000);
    server.handleClient();                              // Respond to HTTP requests
  }
  
  Serial.println("closing door");
  digitalWrite(RELAY_PIN, HIGH);
  delay(500);
  digitalWrite(RELAY_PIN, LOW);
  delay(DELAY_AFTER_CLOSING);
  last_status = 2;
  post_status(last_status);
}

void update_status(int current_status) {                // Action function
  if( current_status != last_status) {
    post_status(current_status);                        // POST all changes
  }
 if(current_status == last_status) {
    if (current_status == 0) {
      if(millis() < last_post + DELAY_BEFORE_CLOSE) {
        Serial.println("Not closing due to close delay");
      }else{
        initiate_close();
      }
    }
  }
}
void loop() {                                           // Main loop
  digitalWrite(ESP8266_LED, LOW);
  delay(POLLING_INTERVAL/2);
  server.handleClient();                                // Respond to HTTP requests
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(ESP8266_LED, HIGH);                    // Blink the LED if the WiFi is connected
  }
  delay(POLLING_INTERVAL/2);
  server.handleClient();                                // Respond to HTTP requests
  currtime=millis();
  currstatus=digitalRead(SENSOR_PIN);                   // Grab the status from the sensor
  Serial.print(currtime);
  Serial.print(" ");
  Serial.println(currstatus);
  update_status(currstatus);                            // Run POST function
  server.handleClient();                                // Respond to HTTP requests
}

