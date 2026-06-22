import os
import sys
import time
import serial
import requests
import threading
import argparse
import ctypes
import winsound
from datetime import datetime
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

# ============================================================================
# BDSC: Biometric Developer Sandbox - IPC Gateway Controller v3.0
# Target Platform: Windows 10/11
# AI Perception: Afferens Cloud Vision API (phone camera as sensor node)
# Alarm: Digital siren through PC speakers + Windows screen lock
# ============================================================================

AFFERENS_API_KEY = os.environ.get("AFFERENS_API_KEY")
if not AFFERENS_API_KEY:
    print("[Error] AFFERENS_API_KEY not found in .env file or environment variables.")
    sys.exit(1)
API_ENDPOINT = "https://afferens.com/api/perception"
DEMO_ENDPOINT = "https://afferens.com/api/demo"
LOG_FILE = "bdsc_events.log"

# Shared Event to trigger perception query
trigger_event = threading.Event()
lockdown_active = False
lockdown_time = 0

def log_event(message):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    formatted_msg = f"[{timestamp}] {message}"
    print(formatted_msg)
    try:
        with open(LOG_FILE, "a") as f:
            f.write(formatted_msg + "\n")
    except Exception:
        pass

class SerialMonitorThread(threading.Thread):
    def __init__(self, port, baud):
        super().__init__()
        self.port = port
        self.baud = baud
        self.running = True
        self.ser = None

    def run(self):
        log_event(f"[Serial] Connecting to {self.port} at {self.baud} baud...")
        while self.running:
            try:
                self.ser = serial.Serial(self.port, self.baud, timeout=1.0)
                log_event(f"[Serial] Connected to {self.port}.")
                
                while self.running:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line:
                        continue
                    
                    if line.startswith("<TEL:"):
                        pass
                        
                    elif line == "<TRG:1>":
                        log_event("[Trigger] Spatial envelope breached! Raising trigger event.")
                        trigger_event.set()

                    elif line.startswith("<ALARM:"):
                        log_event(f"[Hardware] ESP32 confirms: {line}")
                        
            except serial.SerialException as e:
                log_event(f"[Serial Error] Connection lost: {e}. Retrying in 2 seconds...")
                if self.ser:
                    self.ser.close()
                time.sleep(2)

    def send_command(self, cmd_byte):
        if self.ser and self.ser.is_open:
            try:
                self.ser.write(bytes([cmd_byte]))
                self.ser.flush()
                log_event(f"[Serial] Sent command: 0x{cmd_byte:02X}")
                return True
            except Exception as e:
                log_event(f"[Serial Command Error] Failed to write command: {e}")
        return False

    def stop(self):
        self.running = False
        if self.ser:
            self.ser.close()

# ============================================================================
# DIGITAL ALARM: AI-driven siren through PC speakers
# ============================================================================
def play_alarm_sound():
    """Play a dramatic AI alarm siren through PC speakers."""
    def _siren():
        try:
            # Voice announcement (loud and impressive for demo)
            os.system('powershell -Command "Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; $s.Volume = 100; $s.Speak(\'Security Breach. Intruder Detected. Lockdown Initiated.\');"')
            
            # Play standard Windows error sound which routes to main speakers
            for _ in range(10):
                winsound.MessageBeep(winsound.MB_ICONHAND)
                time.sleep(0.3)
        except Exception:
            pass
    
    alarm_thread = threading.Thread(target=_siren, daemon=True)
    alarm_thread.start()

def perform_system_lockdown(serial_thread):
    global lockdown_active, lockdown_time
    if lockdown_active:
        return
    
    lockdown_active = True
    lockdown_time = time.time()
    log_event("[Lockdown] Initiating Triple-Layer Lockdown Sequence...")
    
    # 1. Digital Alarm: Siren through PC speakers
    log_event("[Alarm] 🔊 Sounding AI digital alarm through PC speakers!")
    play_alarm_sound()
    
    # 2. Hardware: Signal ESP32 buzzer
    serial_thread.send_command(0xFF)
    
    # 3. OS Lock: Lock Windows workstation
    log_event("[Lockdown] Locking Windows Workstation via ctypes...")
    try:
        ctypes.windll.user32.LockWorkStation()
    except Exception as e:
        log_event(f"[Lockdown Error] Failed to lock screen: {e}")

def clear_lockdown(serial_thread):
    global lockdown_active
    lockdown_active = False
    log_event("[Lockdown] Cooldown complete. System re-armed.")
    serial_thread.send_command(0x00)

def main():
    parser = argparse.ArgumentParser(description="BDSC Gateway v3.0")
    parser.add_argument("--port", type=str, default="COM5", help="Serial port for ESP32-C3")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate")
    args = parser.parse_args()

    # Start Serial Monitoring Thread
    serial_thread = SerialMonitorThread(args.port, args.baud)
    serial_thread.daemon = True
    serial_thread.start()

    log_event("[Gateway] BDSC Gateway v3.0 Online.")
    log_event("[Gateway] AI Perception: Afferens Cloud Vision API")
    log_event("[Gateway] Alarm: Digital siren (PC speakers) + Windows Lock")
    log_event("[Gateway] Awaiting edge spatial perimeter breach triggers...")

    try:
        while True:
            # Auto-clear lockdown after 10 seconds
            if lockdown_active and (time.time() - lockdown_time > 10):
                clear_lockdown(serial_thread)

            # Wait for hardware trigger from ESP32
            if trigger_event.wait(timeout=1.0):
                trigger_event.clear()
                
                log_event("[Perception] Breach event detected! Querying Afferens AI...")
                
                try:
                    start_time = time.time()
                    
                    # Query Afferens live perception
                    response = requests.get(
                        API_ENDPOINT,
                        headers={"X-API-KEY": AFFERENS_API_KEY},
                        params={"modality": "VISION", "limit": 1},
                        timeout=10.0
                    )
                    
                    is_demo = False
                    
                    # Fallback to demo if no live node
                    if response.status_code == 404:
                        log_event("[Perception] No live node. Falling back to demo data...")
                        response = requests.get(
                            DEMO_ENDPOINT,
                            params={"modality": "VISION"},
                            timeout=10.0
                        )
                        is_demo = True
                    
                    latency = (time.time() - start_time) * 1000
                    log_event(f"[Perception] Afferens responded in {latency:.0f}ms.")
                    
                    if response.status_code == 200:
                        result = response.json()
                        is_demo = result.get("demo", is_demo)
                        data_list = result.get("data", [])
                        
                        if not data_list:
                            log_event("[Cognition] No perception data. Ignoring.")
                            continue
                        
                        event = data_list[0]
                        inner_data = event.get("data", event.get("spatial_coords", {}))
                        objects = inner_data.get("objects", [])
                        classification = event.get("classification", "unknown")
                        confidence = event.get("confidence", 0)
                        
                        # We previously injected a 2nd person here for demo purposes.
                        # This has been removed so it correctly sees 1 operator and stays quiet.
                        
                        source = "DEMO" if is_demo else "LIVE"
                        log_event(f"[Cognition] [{source}] Scene: {classification} (conf: {confidence:.2f})")
                        log_event(f"[Cognition] [{source}] {len(objects)} objects detected:")
                        
                        # Count humans
                        human_count = 0
                        for obj in objects:
                            label = obj.get("label", "").lower()
                            obj_conf = obj.get("confidence", 0.0)
                            log_event(f"  -> {label} (confidence: {obj_conf:.2f})")
                            
                            if label == "person" and obj_conf >= 0.5:
                                human_count += 1
                                log_event(f"  -> ⚠️ HUMAN detected (confidence: {obj_conf:.2f})")
                        
                        # Log tokens
                        tokens_remaining = result.get("tokens_remaining", "unknown")
                        tokens_consumed = event.get("sense_tokens_consumed", "unknown")
                        log_event(f"[Tokens] Consumed: {tokens_consumed} | Remaining: {tokens_remaining}")
                        
                        # THREAT DECISION
                        if human_count > 1:
                            log_event(f"[THREAT] ⚠️ {human_count} HUMANS detected! INTRUDER PRESENT!")
                            perform_system_lockdown(serial_thread)
                        elif human_count == 1:
                            log_event("[Cognition] 1 person (Operator). No intruder. Standing by.")
                        else:
                            log_event("[Cognition] No humans detected. False positive. Ignoring.")
                    
                    else:
                        log_event(f"[Error] Afferens returned {response.status_code}: {response.text}")
                        
                except requests.exceptions.RequestException as e:
                    log_event(f"[Network Error] Cannot reach Afferens: {e}")

    except KeyboardInterrupt:
        log_event("\n[Gateway] Shutting down...")
    finally:
        serial_thread.stop()
        log_event("[Gateway] Offline.")

if __name__ == "__main__":
    main()
