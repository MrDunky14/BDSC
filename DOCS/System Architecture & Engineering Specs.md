# **System Architecture & Engineering Specifications**

**Subsystem:** Multimodal Edge-to-Cloud AI Verification System

## **1\. System Topology Overview**

To bypass the single-core constraints of the RISC-V ESP32-C3 while maintaining microsecond-level determinism, the system is decoupled into three distinct architectural layers:

1. **Deterministic Hardware Edge (ESP32-C3 \+ NE555 \+ Sensors)**  
2. **Asynchronous IPC Gateway (Python Multiprocessing \+ OpenCV)**  
3. **Cognitive Cloud Cortex (Afferens API)**

## **2\. Hardware Edge Implementation**

The ESP32-C3 is deliberately stripped of network and heavy optical processing responsibilities. The internal lwIP Wi-Fi stack is disabled to prevent FreeRTOS task starvation and WDT panics.

### **2.1 Non-Blocking DMA Audio Acquisition**

* **Protocol:** I2S (Inter-IC Sound)  
* **Microphone:** MEMS Digital PDM/I2S Microphone (e.g., INMP441)  
* **Configuration:** The I2S peripheral uses continuous "ping-pong" DMA buffers. While the RISC-V CPU parses PCM data in Buffer A via custom fixed-point bit-shifting envelope filters, hardware continuously streams data into Buffer B.

### **2.2 The High-Speed UART Bridge**

* **Baud Rate:** 921,600 via hardware UART (not simulated USB-CDC).  
* **Interrupt Strategy:** The system utilizes UART\_INTR\_RXFIFO\_FULL thresholds to batch hardware interrupts, minimizing context-switching overhead.  
* **Asynchronous Return:** UART\_PATTERN\_DET is enabled at the silicon level to instantly detect actuation command bytes (e.g., 0xFF) from the gateway, triggering zero-latency hardware alarms without waiting for software queue parsing.

### **2.3 Hardware Actuation (NE555 Timer Offload)**

Instead of consuming ESP32 clock cycles to pulse LEDs, the microcontroller simply drives a single GPIO HIGH to trigger an external NE555 timer configured in astable multivibrator mode, driving a 650nm laser and buzzer array.

## **3\. The IPC Gateway (Host Machine)**

The gateway runs via a multi-threaded Python instance to bridge serial hardware telemetry with asynchronous optical capture.

* **Serial Thread:** A dedicated background thread monitors the 921,600 baud line for \<TRG:1\> packets.  
* **Optical Thread:** Maintains a flushed cv2.VideoCapture buffer.  
* **Event Synthesis:** When a serial trigger is raised via a threading Event, the optical thread snaps the frame, encodes it to base64, and fires the REST payload to the cloud.

## **4\. Afferens API Integration (Cognitive Verification)**

The core intelligence relies on the Afferens VISION modality.

* **Endpoint:** https://afferens.com/api/demo?modality=VISION  
* **Logic Pipeline:**  
  1. The hardware triggers due to a spatial anomaly (e.g., 0.8m ultrasonic echo).  
  2. Gateway POSTs the image to Afferens.  
  3. Gateway parses JSON: Checks data\[0\].data.object\_count.  
  4. Gateway checks data\[0\].data.objects\[i\].label and confidence \> 0.85.  
  5. If multiple humans are detected (primary operator \+ intruder), cognitive verification is confirmed. Closed-loop actuation fires.