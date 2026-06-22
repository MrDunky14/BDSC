# **Product Requirements Document (PRD)**

**Project Name:** The Biometric Developer Sandbox & Corporate Vault

**Version:** 1.0.0 (Quackathon 2026 Release)

**Track:** Track 02: Hardware — Physical Perception (Afferens)

## **1\. Executive Summary**

The Biometric Developer Sandbox is a high-speed, multimodal perception system that pairs a constrained, bare-metal edge microcontroller with a high-level cognitive cloud AI. It establishes a "Zero-Infrastructure Workspace Security Node" designed to prevent unauthorized physical observation of sensitive enterprise code repositories or data science workflows.

## **2\. Problem Statement**

Current workspace security relies on either:

1. **Manual human intervention** (e.g., remembering to press Win+L), which fails under cognitive load.  
2. **Always-on visual AI**, which requires massive local GPU compute, drains laptop batteries, and continuously violates corporate privacy by streaming/recording constant video.  
3. **Dumb hardware sensors**, which suffer from extreme false-positive rates (e.g., an ultrasonic sensor triggering an alarm because a chair was moved).

## **3\. Product Vision & Solution**

The system creates a deterministic, privacy-first physical perimeter loop. A low-cost ($3) ESP32-C3 microcontroller acts as a highly efficient edge-sampling node. It utilizes ultrasonic and acoustic sensors to maintain a strict 1-meter volumetric envelope around a developer's workstation.

The system remains entirely dormant regarding optical data until the physical perimeter is breached. Upon a hardware trigger, a local IPC gateway captures a single optical frame and transmits it to the **Afferens VISION API**. Afferens provides the cognitive verification (e.g., distinguishing between a moving chair and a human intruder). Only upon positive AI verification does the system actuate: locking the OS and triggering a localized hardware alarm.

## **4\. Target Audience**

* Enterprise Software Developers  
* Data Scientists handling PII (Personally Identifiable Information)  
* Cybersecurity Analysts operating in zero-trust or shared physical environments.

## **5\. Functional Requirements**

* **FR-1 (Spatial Polling):** The hardware edge node must continuously poll an ultrasonic sensor array with sub-millisecond latency.  
* **FR-2 (Acoustic Parsing):** The system must utilize I2S DMA buffering and fixed-point integer mathematics to detect transient acoustic anomalies without blocking the CPU.  
* **FR-3 (High-Speed Bridging):** The hardware must transmit deterministic trigger packets over a 921,600-baud UART bridge to a local host machine.  
* **FR-4 (Optical Capture):** Upon receiving a hardware trigger, the host machine gateway must instantly capture a real-time webcam frame.  
* **FR-5 (Cognitive Verification):** The gateway must transmit the synthesized multimodal payload to the Afferens API (modality=VISION) and parse the JSON response for contextual threats (e.g., object\_count \> 1 and label \== 'person').  
* **FR-6 (Dual-Layer Actuation):** Upon positive verification, the gateway must execute an OS-level screen lock AND transmit an asynchronous hardware interrupt back to the ESP32-C3 to fire an NE555-driven warning laser/LED array.

## **6\. Non-Functional Requirements**

* **Performance:** Total loop latency (Physical Event \-\> Verification \-\> Hardware Actuation) must not exceed 800ms (excluding external API network transit time).  
* **Efficiency:** The ESP32-C3 must maintain \< 20% CPU utilization by disabling the internal Wi-Fi stack and offloading optical logic.  
* **Privacy:** The system must strictly avoid continuous optical streaming. The camera buffer is only sampled upon a verified hardware trigger.