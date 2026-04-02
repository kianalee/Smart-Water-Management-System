#include <WiFi.h>
#include <PubSubClient.h>

// // WiFi Settings
// const char *ssid = "Plymouth";         // Same network as MQTT broker
// const char *password = "rwee2763";

// MQTT Broker Settings
const char *mqtt_broker = "192.168.4.2";
const char *mqtt_username = "master";
const char *mqtt_password = "masterpass";
const int mqtt_port = 1883;

const char *pressure_topic = "esp32/pressure";
const char *flow_topic = "esp32/flow";
const char *pump_topic = "esp32/pump";
const char *topVal_topic = "esp32/top_valve";
const char *bottomVal_topic = "esp32/bottom_valve";

const char *ready_topic = "system_ready";
const char *status_topic = "system_status";
const char *override_topic = "override_status";

const int PRESS_PIN = 1;
const int FLOW_PIN = 2; 
const int TOP_VALVE_PIN = 3;
const int BOTTOM_VALVE_PIN = 4;
const int PUMP_PIN = 5;

const int PressT_count = 30;
const int FlowT_count = 30;
int flow_count;
float press_threshold, press_sd, flow_threshold, flow_sd;


// Initialize Clients
WiFiClient espClient;
PubSubClient client(espClient);


// Callback function, processes messages recieved 
void callback(char *topic, byte *payload, unsigned int length) {
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

    //if(strcmp(topic, "esp32/distance") == 0) return;
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


void publish_msg(String msg, const char* topic){
    // Publish to MQTT
    if (client.publish(topic, msg.c_str())) {
        Serial.println("published " + msg + " success to " + topic);
    } else {
        Serial.println("Failed to publish");
    }
}

float volts_to_psi(float volts){
    return(volts - 0.5)/4*150; //150 psi max, 0.5V min, 4.5V max
}

void flow() {
    flow_count++;
}

float sensor_measure(int type){
    if(type == 1){
        analogReadResolution(12);
        int voltage = analogRead(PRESS_PIN);
        float psi = volts_to_psi((voltage/4095)*3.3);
        return psi;
    }
    else {
        //flow freq * 60mins / 7.5 Q
        noInterrupts();
        int pulses = flow_count;
        flow_count =0;
        interrupts();
        //take counted pulses in last second, multiply by 2.25 to get flow in L/min (7.5 Q = 1L/min, so 1 pulse = 1/7.5 L/min, and we measure every second so multiply by 60 to get per minute)
        return pulses*60/7.5 ;
    }
}

// measure every 10 ms to end with 100 readings over 10s and average them out 
// Pass pointers for avg and sd — function writes results into them
void find_threshold(float *avg, float *sd, int type) {
    int reading_tot = 0;  
    float sensor_data [30];
    float sd_sum =0;
    //pressure calculations
    if(type == 1){
        //calculate average
        for (int i = 0; i < PressT_count; i++) {
            sensor_data[i] = sensor_measure(1);
            reading_tot += sensor_data[i];
            delay(30);
        }  

        //calculate standard deviation
        *avg = reading_tot/PressT_count;
        for(int j= 0; j<PressT_count; j++){
            sd_sum += pow((*avg) - sensor_data[j], 2);
        }
        *sd = sqrt(sd_sum/PressT_count);
    }
    //flow calculations
    else{
        //calculate average
        for (int i = 0; i < FlowT_count; i++) {
            sensor_data[i] = sensor_measure(2);
            reading_tot += sensor_data[i];
            delay(1000);
        }   
        *avg = reading_tot /FlowT_count;

        //calculate standard deviation
        for(int j= 0; j<FlowT_count; j++){
            sd_sum += pow((*avg) - sensor_data[j], 2);
        }
        *sd = sqrt(sd_sum/FlowT_count);
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

    // Setup MQTT
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);

    pinMode(FLOW_PIN, INPUT); 
    pinMode(TOP_VALVE_PIN, OUTPUT);
    pinMode(BOTTOM_VALVE_PIN, OUTPUT);
    pinMode(PUMP_PIN, OUTPUT);

    digitalWrite(PRESS_PIN, HIGH); 
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flow, RISING);
    digitalWrite(TOP_VALVE_PIN, LOW); //close top valve
    publish_msg("SHUT", top_valve);

    digitalWrite(BOTTOM_VALVE_PIN, HIGH); //open bottom valve
    publish_msg("OPEN", bottom_valve);

    digitalWrite(PUMP_PIN, HIGH); //turn on pump
    publish_msg("ON", pump_topic);


    //1 or 2 depending on which sensor 
    find_threshold(&press_threshold, &press_sd, 1);
    Serial.println("pressure threshold is ");
    Serial.print(press_threshold);
    find_threshold(&flow_threshold, &flow_sd, 2);
    Serial.println("flow threshold is " );
    Serial.print(flow_threshold); 

    // Initial MQTT connection
    reconnect();
}

void loop() {
    static float flow_tot = 0;; 
    static float press_tot = 0;
    static float flow_avg; 
    static float press_avg;
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
            press_tot+= sensor_measure(1);
            delay(30);
        }
        float press_avg = press_tot/10;

        for(int i =0; i<10; i++){
            flow_tot+= sensor_measure(2);
            delay(50);
        }
        float flow_avg = flow_tot/10;

        //-----pressure sensor----------
        char pressString[10];
        dtostrf(press_avg,1,2,pressString);
        publish_msg(pressString, pressure_topic);

        //-----flow sensor----------
        char flowString[10];
        dtostrf(flow_avg,1,2,flowString);
        publish_msg(flowString, flow_topic); 

        //higher pressure or flow than norm
        if(press_avg<(press_threshold - press_sd) || flow_avg<(flow_threshold - flow_sd)){
            publish_msg("LEAK", status_topic);
            //turn off pump
            digitalWrite(PUMP_PIN, LOW);
        }
        
        //lower pressure or flow than norm
        else if(press_avg > (press_threshold + press_sd) || flow_avg >(flow_threshold + flow_sd)){
            publish_msg("BLOCK", status_topic);
            //open top valve
            digitalWrite(TOP_VALVE_PIN, HIGH);
            //close bottom valve
            digitalWrite(BOTTOM_VALVE_PIN, LOW);
        }
    }
}
