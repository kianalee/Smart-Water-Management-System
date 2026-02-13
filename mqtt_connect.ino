#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

// WiFi Credentials
const char *ssid = "kianatest"; 
const char *password = "sharingiscaring";  

// MQTT Broker Settings
const char *mqtt_broker = "172.20.10.2"; 
const char *topic = "Test topic";
const char *mqtt_username = "master";
const char *mqtt_password = "masterpass";
const int mqtt_port = 1883; 

// Root CA Certificate
const char* ca_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n" \
"..." // (Truncated for readability, keep your full string here)
"-----END CERTIFICATE-----\n";

// Initialize Clients
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Callback function header
void callback(char *topic, byte *payload, unsigned int length) {
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);
    Serial.print("Message:");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println("\n-----------------------");
}

void setup() {
    Serial.begin(115200);
    
    // 1. Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    // 2. Set SSL Certificate (Only if using port 8883)
    // espClient.setCACert(ca_cert); 
    // If you are using port 1883 (non-secure), use WiFiClient instead of WiFiClientSecure
    espClient.setInsecure(); // Skips cert validation for local testing on 1883

    // 3. Setup MQTT
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);

    IPAddress serverIP; // This creates the variable to hold the result

    if (WiFi.hostByName("172.20.10.2", serverIP)) { 
        Serial.println("Laptop found on network!"); 
        Serial.print("Confirmed IP: ");
        Serial.println(serverIP); 
    } else { 
        Serial.println("Laptop NOT found. Check IP!"); 
    }
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
    
        // 4. Connect to MQTT Broker
        while (!client.connected()) {
            String client_id = "esp32-client-" + String(WiFi.macAddress());
            Serial.printf("Connecting to MQTT as %s...\n", client_id.c_str());
            
            if (client.connect(client_id.c_str())) {
                Serial.println("MQTT Connected!");
                client.publish(topic, "Hi, I'm ESP32 ^^");
                client.subscribe(topic);
            } else {
                Serial.print("Failed, rc=");
                Serial.print(client.state());
                Serial.println(" try again in 5 seconds");
                delay(5000);
            }
        }
}

void loop() {
    // Keep the connection alive and process callbacks
    client.loop();
}