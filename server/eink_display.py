"""
E-ink Display Worker for LeakSeek
Manages the Waveshare 2.13" e-Paper HAT display
"""

import time
import threading
from typing import Dict, List
from PIL import Image, ImageDraw, ImageFont

# Try to import waveshare library, gracefully fail if not available
try:
    import sys
    import os
    # Waveshare library is typically in a subdirectory
    # Adjust path if you cloned the repo to a different location
    epd_path = os.path.join(os.path.dirname(__file__), '..', 'e-Paper', 'RaspberryPi_JetsonNano', 'python', 'lib')
    if os.path.exists(epd_path):
        sys.path.append(epd_path)
    from waveshare_epd import epd2in13_V4
    EPD_AVAILABLE = True
except (ImportError, RuntimeError) as e:
    print(f"⚠️  E-ink display unavailable: {e}")
    EPD_AVAILABLE = False

class EinkDisplay:
    def __init__(self):
        self.epd = None
        self.enabled = EPD_AVAILABLE
        self.last_state = None
        self.update_lock = threading.Lock()
        self.last_update_time = 0
        self.debounce_interval = 5.0  # Minimum 5 seconds between updates (RPi Zero optimization)
        
        if self.enabled:
            try:
                self.epd = epd2in13_V4.EPD()
                self.epd.init()
                self.epd.Clear()
                self.show_splash()
                print("✓ E-ink display initialized")
            except Exception as e:
                print(f"❌ Failed to initialize e-ink display: {e}")
                self.enabled = False
    
    def show_splash(self):
        """Show startup splash screen"""
        if not self.enabled:
            return
        
        try:
            print(f"E-ink dimensions: {self.epd.width}x{self.epd.height}")
            
            # Create blank image
            image = Image.new('1', (self.epd.height, self.epd.width), 255)
            draw = ImageDraw.Draw(image)
            
            print(f"Image created: {image.size}")
            
            # Try to use default font, or fallback
            try:
                font_large = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 24)
                font_small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 14)
            except:
                font_large = ImageFont.load_default()
                font_small = ImageFont.load_default()
            
            # Draw splash
            draw.text((10, 40), "LeakSeek", font=font_large, fill=0)
            draw.text((10, 70), "Starting...", font=font_small, fill=0)
            draw.rectangle([(0, 0), (self.epd.height - 1, self.epd.width - 1)], outline=0)
            
            print("Drawing complete, rotating and displaying...")
            
            # Rotate 90 degrees to match physical orientation
            rotated = image.rotate(90, expand=True)
            print(f"Rotated image size: {rotated.size}")
            
            self.epd.display(self.epd.getbuffer(rotated))
            print("Display command sent!")
            
        except Exception as e:
            print(f"Error showing splash: {e}")
            import traceback
            traceback.print_exc()
    
    def update(self, sensor_data: Dict, registered_devices: Dict):
        """
        Update the display with current sensor status.
        Debounced to avoid excessive refreshes.
        """
        if not self.enabled:
            return
        
        # Debounce check
        current_time = time.time()
        if current_time - self.last_update_time < self.debounce_interval:
            return
        
        # Create display state
        state = self._create_display_state(sensor_data, registered_devices)
        
        # Only update if state changed
        if state == self.last_state:
            return
        
        # Update in background thread to avoid blocking
        threading.Thread(target=self._render, args=(state,), daemon=True).start()
    
    def _create_display_state(self, sensor_data: Dict, registered_devices: Dict) -> Dict:
        """Create a normalized state dictionary for comparison"""
        alerts = []
        ok_sensors = []
        
        for address, device_info in registered_devices.items():
            name = device_info.get("name", address[:8])
            data = sensor_data.get(address, {})
            value = data.get("value", None)
            timestamp = data.get("timestamp", 0)
            
            # Consider stale if no update in 30 seconds
            is_stale = (time.time() - timestamp) > 30 if timestamp > 0 else True
            
            if value == 1 and not is_stale:
                alerts.append(name)
            elif not is_stale:
                ok_sensors.append(name)
        
        return {
            "alert_count": len(alerts),
            "ok_count": len(ok_sensors),
            "total_count": len(alerts) + len(ok_sensors),
            "alerts": sorted(alerts),
            "ok_sensors": sorted(ok_sensors)
        }
    
    def _render(self, state: Dict):
        """Render the display (runs in background thread)"""
        with self.update_lock:
            try:
                # Create blank image
                image = Image.new('1', (self.epd.height, self.epd.width), 255)
                draw = ImageDraw.Draw(image)
                
                # Load fonts
                try:
                    font_title = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 20)
                    font_large = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 16)
                    font_normal = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 14)
                    font_small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 11)
                except:
                    font_title = font_large = font_normal = font_small = ImageFont.load_default()
                
                y_pos = 5
                
                # Header
                draw.text((5, y_pos), "LeakSeek", font=font_title, fill=0)
                y_pos += 25
                
                # Alert status
                if state["alert_count"] > 0:
                    # ALERT MODE
                    draw.rectangle([(0, y_pos), (self.epd.height, y_pos + 30)], fill=0)
                    draw.text((10, y_pos + 5), f"⚠ {state['alert_count']} LEAK ALERT", font=font_large, fill=255)
                    y_pos += 35
                    
                    # List alert sensors
                    for i, sensor_name in enumerate(state["alerts"][:3]):  # Max 3
                        draw.text((10, y_pos), f"• {sensor_name}", font=font_normal, fill=0)
                        y_pos += 18
                    
                    if len(state["alerts"]) > 3:
                        draw.text((10, y_pos), f"+ {len(state['alerts']) - 3} more", font=font_small, fill=0)
                        y_pos += 16
                    
                else:
                    # ALL CLEAR MODE
                    draw.text((5, y_pos), "✓ All Clear", font=font_large, fill=0)
                    y_pos += 25
                    
                    # Show registered sensors
                    if state["total_count"] > 0:
                        draw.text((5, y_pos), f"{state['total_count']} sensor(s) active", font=font_small, fill=0)
                        y_pos += 18
                        
                        # List sensors (max 3)
                        for sensor_name in state["ok_sensors"][:3]:
                            draw.text((10, y_pos), f"• {sensor_name}", font=font_small, fill=0)
                            y_pos += 15
                        
                        if len(state["ok_sensors"]) > 3:
                            draw.text((10, y_pos), f"+ {len(state['ok_sensors']) - 3} more", font=font_small, fill=0)
                    else:
                        draw.text((5, y_pos), "No sensors registered", font=font_small, fill=0)
                
                # Timestamp at bottom
                time_str = time.strftime("%H:%M:%S")
                draw.text((5, self.epd.width - 15), time_str, font=font_small, fill=0)
                
                # Display (rotate 90 degrees to match physical orientation)
                self.epd.display(self.epd.getbuffer(image.rotate(90)))
                
                # Update tracking
                self.last_state = state
                self.last_update_time = time.time()
                
                print(f"✓ E-ink display updated: {state['alert_count']} alerts, {state['ok_count']} OK")
                
            except Exception as e:
                print(f"❌ E-ink render error: {e}")
                self.enabled = False
    
    def cleanup(self):
        """Clean up e-ink display resources"""
        if self.enabled and self.epd:
            try:
                self.epd.sleep()
                print("E-ink display sleeping")
            except:
                pass

# Global instance
_display = None

def init_display():
    """Initialize the global display instance"""
    global _display
    if _display is None:
        _display = EinkDisplay()
    return _display

def update_display(sensor_data: Dict, registered_devices: Dict):
    """Update the display (safe to call even if not initialized)"""
    global _display
    if _display:
        _display.update(sensor_data, registered_devices)

def cleanup_display():
    """Cleanup display resources"""
    global _display
    if _display:
        _display.cleanup()
