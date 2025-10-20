#!/usr/bin/python
# Simple test to verify e-ink works - based on Waveshare example

import sys
import os

# Add Waveshare library path
epd_path = os.path.join(os.path.dirname(__file__), '..', 'e-Paper', 'RaspberryPi_JetsonNano', 'python', 'lib')
sys.path.append(epd_path)

from waveshare_epd import epd2in13_V2
from PIL import Image, ImageDraw, ImageFont
import time

try:
    print("Initializing e-ink display...")
    epd = epd2in13_V2.EPD()
    
    print(f"Display dimensions: width={epd.width}, height={epd.height}")
    
    print("Init...")
    epd.init(epd.FULL_UPDATE)
    
    print("Clearing...")
    epd.Clear()
    
    print("Creating image...")
    # Note: Waveshare uses height for width and vice versa
    image = Image.new('1', (epd.height, epd.width), 255)  # 255 = white background
    draw = ImageDraw.Draw(image)
    
    # Load font
    try:
        font24 = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 24)
        font14 = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 14)
    except:
        font24 = ImageFont.load_default()
        font14 = ImageFont.load_default()
    
    # Draw text
    draw.text((10, 10), 'LeakSeek', font=font24, fill=0)
    draw.text((10, 40), 'E-ink Test', font=font14, fill=0)
    draw.rectangle([(0, 0), (epd.height-1, epd.width-1)], outline=0)
    
    print("Displaying...")
    epd.display(epd.getbuffer(image))
    
    print("Done! Image should be visible on screen.")
    print("Waiting 10 seconds...")
    time.sleep(10)
    
    print("Putting display to sleep...")
    epd.sleep()
    
except Exception as e:
    print(f"Error: {e}")
    import traceback
    traceback.print_exc()
