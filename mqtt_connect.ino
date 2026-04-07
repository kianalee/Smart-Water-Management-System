#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <U8g2lib.h>

// =====================================================
// MQTT BROKER SETTINGS
// =====================================================
const char *mqtt_broker   = "192.168.4.2";
const char *mqtt_username = "master";
const char *mqtt_password = "masterpass";
const int   mqtt_port     = 1883;

// =====================================================
// MQTT TOPICS
// =====================================================
const char *pressure_topic   = "esp32/pressure";
const char *flow_topic       = "esp32/flow";       // Fixed: was a duplicate of pump_topic
const char *pump_topic       = "esp32/pump";
const char *topVal_topic     = "esp32/top_valve";
const char *bottomVal_topic  = "esp32/bottom_valve";
const char *ready_topic      = "system_ready";
const char *status_topic     = "system_status";
const char *override_topic   = "override_status";  // Renamed from topic_8

// =====================================================
// PIN DEFINITIONS
// =====================================================
// Outputs
const int pump         = 32;
const int top_valve    = 33;
const int bottom_valve = 25;
const int tank_strip   = 26;

// Enable switches (physical overrides)
const int pump_enable   = 19;
const int top_enable    = 18;
const int bottom_enable = 17;
const int ready_enable  = 16;

// Sensors
const int pressureSensorPin = 35;
const int flowSensorPin     = 34;

// =====================================================
// OLED DISPLAY
// =====================================================
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE);

// =====================================================
// PWM CONFIG
// =====================================================
const int pwmFreq       = 5000;
const int pwmResolution = 8;    // duty cycle 0–255

// =====================================================
// MQTT / WIFI CLIENTS
// =====================================================
WiFiClient espClient;
PubSubClient client(espClient);

// =====================================================
// FLOW SENSOR STATE
// =====================================================
volatile int pulseCount   = 0;
float flowRate            = 0;
unsigned long lastFlowUpdate = 0;

// =====================================================
// THRESHOLD STATE
// =====================================================
float current_pressure_threshold = 0;
float current_flow_threshold     = 0;
float pressure_sd                = 0;
float flow_sd                    = 0;

// =====================================================
// SYSTEM STATE & SAVED THRESHOLDS
// =====================================================
bool is_system_ready = true; // Flag to enable/disable anomaly checks
float saved_pressure_threshold = 0;
float saved_flow_threshold     = 0;
float saved_pressure_sd        = 0;
float saved_flow_sd            = 0;

// =====================================================
// LOOP TIMING
// =====================================================
unsigned long lastPressureRead    = 0;
const unsigned long pressureInterval = 500;   // ms

float current_pressure = 0;
float current_flow     = 0;


// =====================================================
// ENABLE SWITCH HELPERS
// =====================================================
bool isPumpEnableOn()   { return digitalRead(pump_enable)   == HIGH; }
bool isTopEnableOn()    { return digitalRead(top_enable)    == HIGH; }
bool isBottomEnableOn() { return digitalRead(bottom_enable) == HIGH; }
bool isReadyEnableOn()  { return digitalRead(ready_enable)  == HIGH; }


// =====================================================
// ACTUATOR HELPERS
// =====================================================
void TurnOnPump() {
    if (digitalRead(top_valve) == LOW && digitalRead(bottom_valve) == LOW) {
        displayStatus("Cannot turn", "on Pump");    // Protect pump from running dry
    } else {
        digitalWrite(pump, HIGH);
        client.publish(pump_topic, "ON");
    }
}

void TurnOffPump() {
    digitalWrite(pump, LOW);
    client.publish(pump_topic, "OFF");
}

void OpenTopValve() {
    digitalWrite(top_valve, HIGH);
    client.publish(topVal_topic, "OPEN");
}

void CloseTopValve() {
    digitalWrite(top_valve, LOW);
    client.publish(topVal_topic, "CLOSE");
}

void OpenBottomValve() {
    digitalWrite(bottom_valve, HIGH);
    client.publish(bottomVal_topic, "OPEN");
}

void CloseBottomValve() {
    digitalWrite(bottom_valve, LOW);
    client.publish(bottomVal_topic, "CLOSE");
}

void TurnOnTankStrip()  { ledcWrite(tank_strip, 255); }
void TurnOffTankStrip() { ledcWrite(tank_strip, 0);   }

void fadeTankStripOnce() {
    for (int duty = 0; duty <= 255; duty++) { ledcWrite(tank_strip, duty); delay(3); }
    for (int duty = 255; duty >= 0; duty--) { ledcWrite(tank_strip, duty); delay(3); }
}


// =====================================================
// SENSOR HELPERS
// =====================================================
void IRAM_ATTR countPulse() {
    pulseCount++;
}

// Returns pressure in PSI from the sensor voltage
// Sensor: 0.5V = 0 PSI, 4.5V = 150 PSI
float getPressure() {
    analogReadResolution(12);
    int raw     = analogRead(pressureSensorPin);
    float volts = (raw / 4095.0) * 3.3;
    float psi   = (volts - 0.5) / 4.0 * 150.0;
    return max(psi, 0.0f);   // clamp negatives from noise
}

float getFlow() {
    return flowRate;
}

// Call frequently in loop; updates flowRate once per second
void updateFlow() {
    unsigned long now     = millis();
    unsigned long elapsed = now - lastFlowUpdate;

    if (elapsed >= 1000) {
        noInterrupts();
        int count  = pulseCount;
        pulseCount = 0;
        interrupts();

        float frequency = (count * 1000.0) / elapsed;  // pulses/sec
        flowRate        = frequency / 7.5;              // L/min
        lastFlowUpdate  = now;
    }
}


// =====================================================
// DISPLAY HELPERS
// =====================================================
void drawCenteredText(const char* text, int y) {
    int w = u8g2.getStrWidth(text);
    int x = (128 - w) / 2;
    u8g2.drawStr(x, y, text);
}

void displayStatus(const char* line1, const char* line2) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr);
    drawCenteredText(line1, 25);
    drawCenteredText(line2, 40);
    u8g2.sendBuffer();
}

void displayPressureAndFlow(float psi, float flow, float Pthreshold, float Fthreshold) {
    char psiStr[10], flowStr[10], line1[30], line2[30], buffer[50];

    dtostrf(psi,  4, 2, psiStr);
    dtostrf(flow, 4, 2, flowStr);
    sprintf(line1, "%s PSI",   psiStr);
    sprintf(line2, "%s L/min", flowStr);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr);
    drawCenteredText(line1, 15);
    drawCenteredText(line2, 40);

    snprintf(buffer, sizeof(buffer), "P:%.2f  F:%.2f", Pthreshold, Fthreshold);
    u8g2.setFont(u8g2_font_5x8_tr);
    drawCenteredText(buffer, 62);

    u8g2.sendBuffer();
}


// =====================================================
// MQTT HELPERS
// =====================================================
void callback(char *topic, byte *payload, unsigned int length) {
    char msg[length + 1];
    for (unsigned int i = 0; i < length; i++) msg[i] = (char)payload[i];
    msg[length] = '\0';

    Serial.print("Message received on ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(msg);

    if (strcmp(topic, ready_topic) == 0) {
        if (strcmp(msg, "PAUSE") == 0) {
            is_system_ready = false;
            // Save the current thresholds and standard deviations
            saved_pressure_threshold = current_pressure_threshold;
            saved_flow_threshold     = current_flow_threshold;
            saved_pressure_sd        = pressure_sd;
            saved_flow_sd            = flow_sd;
            
            Serial.println("System PAUSED. Thresholds saved.");
            displayStatus("System", "Paused");
        } 
        else if (strcmp(msg, "RESUME") == 0) {
            is_system_ready = true;
            // Restore the saved thresholds
            current_pressure_threshold = saved_pressure_threshold;
            current_flow_threshold     = saved_flow_threshold;
            pressure_sd                = saved_pressure_sd;
            flow_sd                    = saved_flow_sd;
            
            Serial.println("System RESUMED. Thresholds restored.");
            displayStatus("System", "Resumed");
        }
    }
}

void SetupMQTT() {
    Serial.println("\n=== Starting ESP32 as Access Point ===");
    WiFi.softAP("ESP32_Broker", "12345678");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());

    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);
}

void ConnectMQTT() {
    while (!client.connected() && !isReadyEnableOn()) {
        String client_id = "esp32-client-" + String(WiFi.macAddress());
        Serial.printf("Connecting to MQTT as %s...\n", client_id.c_str());
        displayStatus("Connecting", "to MQTT");

        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("MQTT Connected!");
            client.publish(ready_topic, "YES");
            client.subscribe(pump_topic);
            client.subscribe(flow_topic);
            client.subscribe(topVal_topic);
            client.subscribe(bottomVal_topic);
            client.subscribe(ready_topic);
        } else {
            Serial.print("Failed, rc=");
            Serial.print(client.state());
            Serial.println(" — retrying in 5s");

            // Wait 5 s but bail early if ready switch is flipped
            for (int i = 0; i < 5000; i++) {
                delay(1);
                if (isReadyEnableOn()) return;
            }
        }
    }
}


// =====================================================
// THRESHOLD CALCULATION
// =====================================================
void CalculateThresholds(int valuesPerSecond, int seconds) {
    // Reset flow counter before sampling
    lastFlowUpdate = millis();
    noInterrupts();
    pulseCount = 0;
    interrupts();

    int totalCalculations = valuesPerSecond * seconds;
    int delayPerSample    = 1000 / valuesPerSecond;

    // Cap array size so we don't overflow the stack
    const int MAX_SAMPLES = 100;
    totalCalculations = min(totalCalculations, MAX_SAMPLES);

    float pressure_data[MAX_SAMPLES] = {};
    float flow_data[MAX_SAMPLES]     = {};

    float PressureSum = 0;
    float FlowSum     = 0;
    int   flowSamples = 0;

    unsigned long lastFlowSample = millis();
    int animation_counter        = 0;
    const char* animations[]     = { "", ".", "..", "..." };

    for (int i = 0; i < totalCalculations; i++) {
        updateFlow();

        char buffer[30];
        snprintf(buffer, sizeof(buffer), "Thresholds %s", animations[animation_counter]);
        animation_counter = (animation_counter + 1) % 4;
        displayStatus("Calculating", buffer);

        // Pressure sample (fast)
        pressure_data[i]  = getPressure();
        PressureSum       += pressure_data[i];

        // Flow sample (only once per second)
        unsigned long now = millis();
        if (now - lastFlowSample >= 1000) {
            lastFlowSample    = now;
            flow_data[i]      = getFlow();
            FlowSum           += flow_data[i];
            flowSamples++;
        }

        delay(delayPerSample);
    }

    // Averages
    current_pressure_threshold = PressureSum / totalCalculations;
    current_flow_threshold     = (flowSamples > 0) ? FlowSum / flowSamples : 0;

    // Standard deviations
    float pressure_sd_sum = 0;
    float flow_sd_sum     = 0;
    for (int i = 0; i < totalCalculations; i++) {
        pressure_sd_sum += pow(current_pressure_threshold - pressure_data[i], 2);
    }
    for (int i = 0; i < flowSamples; i++) {
        flow_sd_sum += pow(current_flow_threshold - flow_data[i], 2);
    }
    pressure_sd = sqrt(pressure_sd_sum / totalCalculations);
    flow_sd     = (flowSamples > 0) ? sqrt(flow_sd_sum / flowSamples) : 0;

    Serial.printf("Pressure threshold: %.2f ± %.2f PSI\n",  current_pressure_threshold, pressure_sd);
    Serial.printf("Flow threshold:     %.2f ± %.2f L/min\n", current_flow_threshold,     flow_sd);
}


// =====================================================
// SETUP
// =====================================================
void setup() {
    Serial.begin(115200);

    // Pin modes
    pinMode(pump,         OUTPUT);
    pinMode(top_valve,    OUTPUT);
    pinMode(bottom_valve, OUTPUT);
    pinMode(tank_strip,   OUTPUT);

    pinMode(pump_enable,   INPUT_PULLUP);
    pinMode(top_enable,    INPUT_PULLUP);
    pinMode(bottom_enable, INPUT_PULLUP);
    pinMode(ready_enable,  INPUT_PULLUP);

    pinMode(pressureSensorPin, INPUT);
    pinMode(flowSensorPin,     INPUT);
    attachInterrupt(digitalPinToInterrupt(flowSensorPin), countPulse, RISING);
    lastFlowUpdate = millis();

    // PWM for LED strip
    ledcAttach(tank_strip, pwmFreq, pwmResolution);
    TurnOffTankStrip();

    // Display + OLED
    Wire.begin(21, 22);
    u8g2.begin();
    TurnOnTankStrip();
    delay(2000);

    // State 1-2: MQTT setup and connect
    SetupMQTT();
    ConnectMQTT();

    // State 3: Wait for user to flip ready switch off before continuing
    while (isReadyEnableOn()) {
        displayStatus("Flip Switch 4", "to continue");
    }

    // State 4: Open bottom valve, wait for water to settle
    OpenBottomValve();
    for (int i = 7; i >= 0; i--) {
        char buffer[32];
        sprintf(buffer, "Opening: %d", i);
        displayStatus(buffer, "");
        delay(1000);
    }

    // State 5: Start pump, calibrate thresholds
    TurnOnPump();
    displayStatus("Turning", "on Pump");
    delay(3000);

    CalculateThresholds(10, 10);
    client.publish(ready_topic, "YES");
}


// =====================================================
// LOOP
// =====================================================
void loop() {
    if (!client.connected()) ConnectMQTT();
    client.loop();

    updateFlow();

    unsigned long now = millis();

    if (now - lastPressureRead >= pressureInterval) {
        lastPressureRead = now;
        current_pressure = getPressure();
        current_flow     = getFlow();

        Serial.printf("Pressure: %.2f PSI  |  Flow: %.2f L/min\n", current_pressure, current_flow);
    }

    // Format and publish sensor readings
    char Pressuremsg[10], Flowmsg[10];
    dtostrf(current_pressure, 4, 2, Pressuremsg);
    dtostrf(current_flow,     4, 2, Flowmsg);
    client.publish(pressure_topic, Pressuremsg);
    client.publish(flow_topic,     Flowmsg);     // Fixed: was publishing to pump_topic

    displayPressureAndFlow(current_pressure, current_flow, current_pressure_threshold, current_flow_threshold);

    // --- Anomaly detection ---
    if(is_system_ready){
        // Leak: pressure or flow dropped below threshold
        if (current_pressure <= (current_pressure_threshold - pressure_sd) ||
            current_flow     <= (current_flow_threshold     - flow_sd)) {

            client.publish(status_topic, "LEAK");
            displayStatus("Leak Detected", "");
            TurnOffPump();
            for (int i = 0; i < 2; i++) fadeTankStripOnce();

            while (!isReadyEnableOn()) displayStatus("Toggle Switch 4", "to reset");
            while ( isReadyEnableOn()) displayStatus("Toggle Switch 4", "to reset");

            TurnOnPump();
            TurnOnTankStrip();
            displayStatus("Turning", "on Pump");
            delay(3000);
            CalculateThresholds(10, 10);
            client.publish(ready_topic, "YES");
        }

        // Blockage: pressure too high (check before critical to keep correct ordering)
        else if (current_pressure >= (current_pressure_threshold + pressure_sd) ||
                 current_flow     >= (current_flow_threshold     + flow_sd)) {

            client.publish(status_topic, "BLOCK");
            displayStatus("Blockage", "Detected");

            OpenTopValve();
            for (int i = 0; i < 2; i++) {
                fadeTankStripOnce();
                displayStatus("Rerouting Water", "");
            }
            TurnOnTankStrip();
            CloseBottomValve();

            while (!isReadyEnableOn()) displayStatus("Toggle Switch 4", "to reset");
            while ( isReadyEnableOn()) { displayStatus("Toggle Switch 4", "to reset"); TurnOffPump(); }

            OpenBottomValve();
            CloseTopValve();
            for (int i = 7; i >= 0; i--) {
                char buffer[32];
                sprintf(buffer, "Resetting: %d", i);
                displayStatus(buffer, "");
                delay(1000);
            }
            TurnOnPump();
            CalculateThresholds(10, 10);
            client.publish(ready_topic, "YES");
        }

        // Override: if override switch is on, skip anomaly checks and just display override status
        else {
            if (isPumpEnableOn()) {
                client.publish(override_topic, "Pump Override");
                displayStatus("Pump Override", "Enabled");
            } else if (isTopEnableOn()) {
                client.publish(override_topic, "Top Valve Override");
                displayStatus("Top Valve", "Override");
            } else if (isBottomEnableOn()) {
                client.publish(override_topic, "Bottom Valve Override");
                displayStatus("Bottom Valve", "Override");
            }
        }
    }
}
