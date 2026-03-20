#include <WiFi.h>
#include <PubSubClient.h>

/*CHANGES TO MAKE
    - first 10 seconds when "pump" LED is on, find threshold value 
        - threshold value found thru average value in that time 
        - once found, go into ready state
    - avg out every 10 readings to send out thru mqtt  
    - send state of LED "valve" to mqtt 
    - if over threshold, blockage state 
    - if under threshold, leakage state 
*/

// // WiFi Settings
// const char *ssid = "Plymouth";         // Same network as MQTT broker
// const char *password = "rwee2763";

// MQTT Broker Settings
const char *mqtt_broker = "192.168.4.2";
const char *dist_topic = "esp32/distance";
const char *mqtt_username = "master";
const char *mqtt_password = "masterpass";
const int mqtt_port = 1883;

#define SOUND_SPEED 0.034
const int trigPin = 5;
const int echoPin = 18; 
const int ledPin = 2;
long duration;
float distance;

// Initialize Clients
WiFiClient espClient;
PubSubClient client(espClient);

// Callback function, processes messages recieved 
void callback(char *topic, byte *payload, unsigned int length) {
    if (strcmp(topic, "esp32/distance") == 0) return;

    char msg_char [length+1];
    Serial.print("Message received on ");
    Serial.print(topic);
    Serial.print(": ");
    for (int i = 0; i < length; i++) {
        msg_char[i] = (char) payload[i];
    }
    msg_char[length] = '\0';  
    Serial.print(msg_char); 
    Serial.println();

    if(strcmp(topic, "esp32/distance") == 0) return;

    if(strcmp(topic, "esp32/LED") == 0){
        if(strcmp(msg_char, "ON") == 0){
            digitalWrite(ledPin, HIGH);
        }
        else if(strcmp(msg_char, "OFF") == 0){
            digitalWrite(ledPin,LOW);
        }
    }
}

// void connectWiFi() {
//     WiFi.mode(WIFI_STA);
//     WiFi.begin(ssid, password);
//     Serial.print("Connecting to WiFi");
//     while (WiFi.status() != WL_CONNECTED) {
//         delay(500);
//         Serial.print(".");
//     }
//     Serial.println("\nWiFi Connected!");
//     Serial.print("IP Address: ");
//     Serial.println(WiFi.localIP());
// }

void reconnect() {
    while (!client.connected()) {
        String client_id = "esp32-client-" + String(WiFi.macAddress());
        Serial.println("Connecting to MQTT...");

        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("MQTT Connected!");
            if(client.subscribe("esp32/LED")){

                Serial.println("subbed to LED");
            }
            else{
                Serial.println("sub failed");
            }
            //client.subscribe(dist_topic);
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

     // Start Access Point
    WiFi.softAP("ESP32_Broker", "12345678");
    Serial.println("Access Point Started");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());

    pinMode(trigPin,OUTPUT);
    pinMode(echoPin, INPUT); 
    pinMode(ledPin, OUTPUT);

    // Setup MQTT
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);

    // Initial MQTT connection
    reconnect();
   
}
void publish_msg(msg, topic)){
    // Publish to MQTT
    if (client.publish(topic, msg)) {
        Serial.print("published" + msg + "success");
    } else {
        Serial.println("Failed to publish");
    }
}

int distance_measure(){
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin,HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    duration = pulseIn(echoPin, HIGH,30000);
    return duration * SOUND_SPEED/2;
}

// measure every 10 ms to end with 100 readings over 10s and average them out 
int find_threshold(){
    for(int i= 0; i<100; i++){}
        distance_tot += distance_measure();
        wait(); //make it 10 ms 
    }
    avg = distance_tot/100
    sd = 0; 

    return avg, sd;
}


void loop() {
    // Maintain MQTT connection
    if (!client.connected()) {
        reconnect();
    }
    client.loop();
    int dist_threshold = find_threshold();
    static unsigned long lastRead = 0;
    if (millis() - lastRead >= 1000) {
        lastRead = millis();

        distance = distance_measure();
        
        char distString[10];

        if(distance_count <=10){
            distance_tot += distance;
        }
        else{
            distance_avg = distance_tot/10;

            dtostrf(distance_avg,1,2,distString);
            publish_msg(distString, distance); 
            distance_tot = 0;

        }

 
        client.loop();
    }


}
