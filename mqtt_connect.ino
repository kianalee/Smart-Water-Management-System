#include <WiFi.h>
#include <PubSubClient.h>

// WiFi Settings
const char *ssid = "Plymouth";         // Same network as MQTT broker
const char *password = "rwee2763";

// MQTT Broker Settings
const char *mqtt_broker = "10.236.241.200";
const char *temp_topic = "esp32/distance";
const char *mqtt_username = "master";
const char *mqtt_password = "masterpass";
const int mqtt_port = 1883;

#define SOUND_SPEED = 0.034 
const int trigPin = 5;
const int echoPin - 18; 
long duration;
float distanceCm;
float distanceInch;

// Initialize Clients
WiFiClient espClient;
PubSubClient client(espClient);

// Callback function
void callback(char *topic, byte *payload, unsigned int length) {
    Serial.print("Message received on ");
    Serial.print(topic);
    Serial.print(": ");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();
}

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

void reconnect() {
    while (!client.connected()) {
        String client_id = "esp32-client-" + String(WiFi.macAddress());
        Serial.println("Connecting to MQTT...");

        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("MQTT Connected!");
            client.subscribe(temp_topic);
        } else {
            Serial.print("Failed, rc=");
            Serial.println(client.state());
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== Starting ESP32 ===");

    // Connect to WiFi
    connectWiFi();

    // Setup MQTT
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);

    // Initial MQTT connection
    reconnect();
    Serial.begin(115200);
    pinMode(trigPin,OUTPUT);
    pinMode(echoPin, INPUT); 
}

void loop() {
    // Maintain MQTT connection
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    digitalWrite(trigPin, LOW);
    delayMicroSeconds(2);
    digitalWrite(trigPin,HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    duration = 

}
