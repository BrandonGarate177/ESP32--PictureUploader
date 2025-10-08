# OV5640 Camera Configuration Notes

## Overview
This project has been modified to work with the OV5640 5MP camera module instead of the original OV3660.

## Key Changes Made

### 1. Camera Sensor Detection
- Updated sensor detection logic to handle OV5640_PID instead of OV3660_PID
- Configured OV5640-specific settings for optimal performance

### 2. Resolution Settings
- **Default Resolution**: QSXGA (2592x1944) - OV5640's maximum resolution
- **With PSRAM**: FHD (1920x1080) for better streaming performance  
- **Without PSRAM**: SVGA (800x600) fallback
- **Face Detection**: VGA (640x480) for better accuracy

### 3. OV5640 Optimizations
- **JPEG Quality**: Set to 8-10 for high quality images
- **Frame Buffer**: 2 buffers when PSRAM is available
- **Auto Controls**: Enabled AWB, AGC, AEC for automatic adjustments
- **Lens Correction**: Enabled for better image quality
- **White Pixel Correction**: Enabled
- **Gamma Correction**: Enabled

### 4. Performance Settings
- **Clock Frequency**: 20MHz (optimal for OV5640)
- **Frame Rate**: Optimized by starting with SVGA resolution
- **Memory**: Uses PSRAM for frame buffers when available

## OV5640 Capabilities
- **Resolution**: Up to 5MP (2592x1944)
- **Format Support**: JPEG, RGB565, YUV422
- **Auto Functions**: Auto focus, auto exposure, auto white balance
- **Special Features**: Image stabilization, lens correction

## Usage
The camera will automatically detect the OV5640 sensor and apply the optimized settings. The web server will be available at the IP address shown in the serial monitor.

## Troubleshooting
- If image quality is poor, adjust JPEG quality in the sensor configuration
- If streaming is slow, reduce the frame size resolution
- Ensure PSRAM is enabled for high-resolution capture
- Check power supply - OV5640 requires stable 3.3V power
