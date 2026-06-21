#include <Arduino.h>
#include <WiFi.h>

// ============================================================================
// BDSC: Biometric Developer Sandbox - ESP32-C3 Firmware v1.5.0
// Target Microcontroller: ESP32-C3 Supermini
// Primary Edge Trigger: HC-SR04 Ultrasonic Sensor (Spatial Perimeter)
// ============================================================================

// --- Pinout Configuration ---
#define PIN_ULTRASONIC_TRIG  5   // TRIG pin of HC-SR04
#define PIN_ULTRASONIC_ECHO  6   // ECHO pin of HC-SR04 (Requires 2:1 voltage divider!)
#define PIN_ALARM_OUT        7   // Direct output to buzzer

// --- Communication Settings ---
#define UART_BAUD_RATE       921600
#define SPATIAL_THRESHOLD_M  1.0     // 1 meter security perimeter
#define POLL_INTERVAL_MS     50      // Poll every 50ms
#define TRIGGER_DEBOUNCE_MS  3000    // 3-second cooldown between triggers

// --- Global Variables ---
unsigned long last_poll_time = 0;
unsigned long last_trigger_time = 0;
bool alarm_active = false;

// Non-blocking Ultrasonic State
volatile unsigned long echo_start = 0;
volatile unsigned long echo_duration = 0;
volatile bool new_distance_ready = false;
float current_distance_m = -1.0f;

// --- ISR for HC-SR04 Echo Pin ---
void IRAM_ATTR echo_isr() {
    if (digitalRead(PIN_ULTRASONIC_ECHO) == HIGH) {
        echo_start = micros();
    } else {
        echo_duration = micros() - echo_start;
        new_distance_ready = true;
    }
}

// --- Helper Functions ---

// Trigger HC-SR04 pulse (non-blocking)
void trigger_ultrasonic() {
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
}

void setup() {
    // 1. Offload Wi-Fi to preserve CPU & prevent FreeRTOS starvation
    WiFi.mode(WIFI_OFF);
    
    // 2. Initialize High-Speed USB-CDC Serial
    Serial.begin(UART_BAUD_RATE);
    
    // 3. Configure Pins
    pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
    pinMode(PIN_ULTRASONIC_ECHO, INPUT);

    // 4. Setup hardware for buzzer
    pinMode(PIN_ALARM_OUT, OUTPUT);
    digitalWrite(PIN_ALARM_OUT, LOW); // Start silent

    // 5. Attach Interrupt for Non-Blocking Distance Reading
    attachInterrupt(digitalPinToInterrupt(PIN_ULTRASONIC_ECHO), echo_isr, CHANGE);

    Serial.println("[BOOT] BDSC Edge Node v1.5.0 Online.");
}

void loop() {
    unsigned long now = millis();
    
    // 1. Compute Distance if new reading is ready
    if (new_distance_ready) {
        // Distance in meters = duration * 0.0001715
        current_distance_m = (float)echo_duration * 0.0001715f;
        new_distance_ready = false;
    }
    
    // 2. Spatial Polling Loop
    if (now - last_poll_time >= POLL_INTERVAL_MS) {
        last_poll_time = now;
        
        // Trigger next ultrasonic pulse (non-blocking)
        trigger_ultrasonic();
        
        // Check if volumetric perimeter is breached (closer than 1.0m)
        bool perimeter_breach = (current_distance_m > 0.0f && current_distance_m <= SPATIAL_THRESHOLD_M);
        
        // Send telemetry packet over UART
        // Format: <TEL:dist_m,breach_status>
        Serial.printf("<TEL:%.2f,%d>\n", current_distance_m, perimeter_breach ? 1 : 0);

        // If breached (spatial) and cooldown has passed, send trigger
        if (perimeter_breach && (now - last_trigger_time >= TRIGGER_DEBOUNCE_MS)) {
            Serial.println("[EVENT] Spatial breach detected!");
            Serial.println("<TRG:1>");
            last_trigger_time = now;
        }
    }

    // 3. Command parser (Incoming packets from host machine)
    // Non-blocking serial read
    if (Serial.available() > 0) {
        int cmd = Serial.read();
        
        if (cmd == 0xFF) {
            // LOCKDOWN COMMAND RECEIVED
            alarm_active = true;
            Serial.println("<ALARM:ON>");
        } 
        else if (cmd == 0x00) {
            // CLEAR / DISARM COMMAND RECEIVED
            alarm_active = false;
            digitalWrite(PIN_ALARM_OUT, LOW);    // Silence buzzer
            Serial.println("<ALARM:OFF>");
        }
    }

    // 4. Buzzer Pulsing Logic (replaces hardware PWM)
    // 2Hz beep: 250ms ON, 250ms OFF
    if (alarm_active) {
        if ((now / 250) % 2 == 0) {
            digitalWrite(PIN_ALARM_OUT, HIGH);
        } else {
            digitalWrite(PIN_ALARM_OUT, LOW);
        }
    }
}
