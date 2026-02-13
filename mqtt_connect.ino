#include <WiFi.h>
#include <PubSubClient.h>

// MQTT Broker Settings
const char *mqtt_broker = "192.168.4.2"; // Remove the trailing space!
const char *topic = "test_topic"; // Remove space from topic name
const char *mqtt_username = "master";
const char *mqtt_password = "masterpass";
const int mqtt_port = 1883; 

// Initialize Clients
WiFiClient espClient; // Changed from WiFiClientSecure
PubSubClient client(espClient);

// Callback function
void callback(char *topic, byte *payload, unsigned int length) {
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);
    Serial.print("Message: ");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println("\n-----------------------");
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== Starting ESP32 as Access Point ===");
    
    // 1. Create Access Point
    WiFi.softAP("ESP32_Broker", "12345678");
    Serial.println("Access Point Started");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP()); // Should be 192.168.4.1
    
    // 2. Setup MQTT (connecting to broker on THIS device)
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);
    
    // 3. Connect to MQTT Broker
    while (!client.connected()) {
        String client_id = "esp32-client-" + String(WiFi.macAddress());
        Serial.printf("Connecting to MQTT as %s...\n", client_id.c_str());
        
        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("MQTT Connected!");
            client.publish(topic, "Hi, I'm ESP32 AP");
            client.subscribe(topic);
        } else {
            Serial.print("Failed, rc=");
            Serial.print(client.state());
            Serial.println(" - try again in 5 seconds");
            delay(5000);
        }
    }
}

void loop() {
    client.loop();
}
