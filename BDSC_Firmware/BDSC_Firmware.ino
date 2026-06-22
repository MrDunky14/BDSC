#include <Arduino.h>
#include <driver/i2s.h>
#include <WiFi.h>

// ============================================================================
// BDSC: Biometric Developer Sandbox - ESP32-C3 Firmware v1.4.0
// Target Microcontroller: ESP32-C3 Supermini
// Buzzer driven directly via hardware PWM (LEDC) on GPIO 7
// ============================================================================

// --- Pinout Configuration ---
#define PIN_ULTRASONIC_TRIG  5   // TRIG pin of HC-SR04
#define PIN_ULTRASONIC_ECHO  6   // ECHO pin of HC-SR04 (Requires 2:1 voltage divider!)
#define PIN_ALARM_OUT        7   // Direct output to buzzer

// INMP441 Pin Mapping to ESP32-C3 Supermini
#define PIN_I2S_SD         2   // SD (Serial Data) -> GPIO 2
#define PIN_I2S_WS         3   // WS (Word Select) -> GPIO 3
#define PIN_I2S_BCLK       4   // SCK (Bit Clock) -> GPIO 4
// Note: VDD should go to 3.3V, GND to GND, and L/R to GND (for left channel select)

// --- Communication Settings ---
#define UART_BAUD_RATE       921600
#define SPATIAL_THRESHOLD_M  1.0     // 1 meter security perimeter
#define POLL_INTERVAL_MS     50      // Poll every 50ms
#define TRIGGER_DEBOUNCE_MS  3000    // 3-second cooldown between triggers

// I2S Configuration
#define I2S_PORT             I2S_NUM_0
#define DMA_BUF_LEN          64

// --- Global Variables ---
unsigned long last_poll_time = 0;
unsigned long last_trigger_time = 0;
bool alarm_active = false;

// Acoustic Baseline Tracking
int32_t acoustic_baseline = 0;
const int32_t ACOUSTIC_ANOMALY_THRESHOLD = 50000; // Threshold for anomaly

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

// Setup standard ESP32 I2S Peripheral driver with DMA
void setup_i2s() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // INMP441 outputs 24-bit MSB-aligned in 32-bit slots
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_SD
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_start(I2S_PORT);
}

// Read and process I2S DMA Buffer for Acoustic Anomalies
int process_acoustic_buffer() {
    int32_t samples[DMA_BUF_LEN];
    size_t bytes_read = 0;
    esp_err_t res = i2s_read(I2S_PORT, &samples, sizeof(samples), &bytes_read, 0); // Non-blocking read (timeout = 0)
    
    if (res != ESP_OK || bytes_read == 0) {
        return 0;
    }
    
    int n_samples = bytes_read / sizeof(int32_t);
    int32_t sum = 0;
    int32_t max_val = -2147483647;
    int32_t min_val = 2147483647;
    
    for (int i = 0; i < n_samples; i++) {
        // INMP441 uses 24-bit output, shifted to the upper bits of a 32-bit slot
        int32_t val = samples[i] >> 8; 
        sum += abs(val);
        if (val > max_val) max_val = val;
        if (val < min_val) min_val = val;
    }
    
    int32_t current_avg = sum / n_samples;
    
    // Simple Exponential Moving Average for Baseline Tracking
    if (acoustic_baseline == 0) {
        acoustic_baseline = current_avg;
    } else {
        acoustic_baseline = (acoustic_baseline * 9 + current_avg) / 10;
    }
    
    return (int)abs(max_val - min_val);
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

    // 6. Initialize I2S DMA
    setup_i2s();

    Serial.println("[BOOT] BDSC Edge Node v1.4.0 Online.");
}

void loop() {
    unsigned long now = millis();
    
    // 1. Compute Distance if new reading is ready
    if (new_distance_ready) {
        // Distance in meters = duration * 0.0001715
        current_distance_m = (float)echo_duration * 0.0001715f;
        new_distance_ready = false;
    }
    
    // 2. Spatial & Acoustic Polling Loop
    if (now - last_poll_time >= POLL_INTERVAL_MS) {
        last_poll_time = now;
        
        // Trigger next ultrasonic pulse (non-blocking)
        trigger_ultrasonic();
        
        int mic_level = process_acoustic_buffer();

        // Check if volumetric perimeter is breached (closer than 1.0m)
        bool perimeter_breach = (current_distance_m > 0.0f && current_distance_m <= SPATIAL_THRESHOLD_M);
        
        // Check if acoustic anomaly is detected (loud transient noise)
        bool acoustic_breach = (mic_level - acoustic_baseline > ACOUSTIC_ANOMALY_THRESHOLD);

        // Send telemetry packet over UART
        // Format: <TEL:dist_m,mic_val,breach_status>
        Serial.printf("<TEL:%.2f,%d,%d>\n", current_distance_m, mic_level, (perimeter_breach || acoustic_breach) ? 1 : 0);

        // If breached (spatial OR acoustic) and cooldown has passed, send trigger
        if ((perimeter_breach || acoustic_breach) && (now - last_trigger_time >= TRIGGER_DEBOUNCE_MS)) {
            if (perimeter_breach) Serial.println("[EVENT] Spatial breach detected!");
            if (acoustic_breach) Serial.println("[EVENT] Acoustic anomaly detected!");
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
