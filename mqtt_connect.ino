#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// // WiFi Settings
// const char *ssid = "Plymouth";         // Same network as MQTT broker
// const char *password = "rwee2763";

// MQTT Broker Settings
const char *mqtt_broker = "192.168.4.2";
const char *dist_topic = "esp32/distance";
const char *led_topic = "esp32/led";
const char *led2_topic = "esp32/led2";
const char *temp_topic = "esp32/temp";
const char *mqtt_username = "master";
const char *mqtt_password = "masterpass";
const int mqtt_port = 1883;

#define SOUND_SPEED 0.034
// #define DHTPIN 26
// #define DHTTYPE DHT22
// DHT dht(DHTPIN, DHTTYPE);
const int trigPin = 5;
const int echoPin = 18; 
const int ledPin = 2;
//const int led2Pin = 3;
long duration;
float distance;
int dist_threshold, dist_sd, temp_threshold, temp_sd;


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
    if(strcmp(topic, "esp32/temperature")==0) return;

    if(strcmp(topic, "esp32/LED") == 0) return; 
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

    //dht.begin();
    pinMode(trigPin,OUTPUT);
    pinMode(echoPin, INPUT); 
    pinMode(ledPin, OUTPUT);
   // pinMode(led2Pin, OUTPUT);

    // Setup MQTT
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);

    //1 or 2 depending on which sensor 
    find_threshold(&dist_threshold, &dist_sd, 1);
    Serial.println("distance threshold is ");
    Serial.print(dist_threshold);
    // find_threshold(&temp_threshold, &temp_sd, 2);
    // Serial.println("temperature threshold is " );
    // Serial.print(temp_threshold); 

    // Initial MQTT connection
    reconnect();
}

void publish_msg(String msg, const char* topic){
    // Publish to MQTT
    if (client.publish(topic, msg.c_str())) {
        Serial.print("published" + msg + "success");
    } else {
        Serial.println("Failed to publish");
    }
}

float sensor_measure(int type){
    if(type == 1){
        digitalWrite(trigPin, LOW);
        delayMicroseconds(2);
        digitalWrite(trigPin,HIGH);
        delayMicroseconds(10);
        digitalWrite(trigPin, LOW);

        duration = pulseIn(echoPin, HIGH,30000);
        return duration * SOUND_SPEED/2;
    }
    else {
        return dht.readTemperature();
    }
}

// measure every 10 ms to end with 100 readings over 10s and average them out 
// Pass pointers for avg and sd — function writes results into them
void find_threshold(int *avg, int *sd, int type) {
    int reading_tot = 0;  // fixed: was declared inside loop, also missing initializer

    for (int i = 0; i < 30; i++) {
        reading_tot += sensor_measure(type);
        if(type == 1){
            delay(30);
        }
        else{
            delay(2000);
        }
    }

    *avg = reading_tot /30;
    *sd = 0;
}


void loop() {
    static float distance_tot = 0;; 
    static float temp_tot = 0;
    static float distance_avg; 
    static int count;

    // Maintain MQTT connection
    if (!client.connected()) {
        reconnect();
    }
    client.loop();
   
    static unsigned long lastRead = 0;
    if (millis() - lastRead >= 1000) {
        lastRead+= 1000;

        for(int i =0; i<10; i++){
            distance_tot+= sensor_measure(1);
            delay(30);
        }


        float distance_avg = distance_tot/10;
        // float temp_avg = sensor_measure(2);

        //-----distance sensor----------
        char distString[10];
        dtostrf(distance_avg,1,2,distString);
        publish_msg(distString, dist_topic); 

        //check threshold
        if(distance_avg<(dist_threshold - dist_sd) || distance_avg >(dist_threshold + dist_sd)){
            digitalWrite(ledPin, HIGH);
            publish_msg("ON", led_topic);
            delay(10);
            digitalWrite(ledPin,LOW);
            publish_msg("OFF", led_topic);
        }

        //-----temperature sensor----------
        // char tempString[10];
        // dtostrf(temp_avg,1,2,tempString);
        // publish_msg(tempString, temp_topic);

        // if(temp_avg<(temp_threshold - temp_sd) || temp_avg >(temp_threshold + temp_sd)){
        //     digitalWrite(led2Pin, HIGH);
        //     publish_msg("ON", led2_topic);
        //     delay(10);
        //     digitalWrite(led2Pin,LOW);
        //     publish_msg("OFF", led2_topic);
        // }

        distance_tot = 0;
        temp_tot = 0;
        count =0;
        
    }
}
