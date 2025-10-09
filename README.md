# ESP32 Picture Uploader

A complete IoT camera system using ESP32-S3 with OV5640 camera module that automatically captures and uploads images to a remote server via HTTP POST requests.

## System Architecture

```
┌─────────────────┐    WiFi/HTTP     ┌─────────────────┐    File I/O    ┌─────────────────┐
│   ESP32-S3      │ ───────────────> │   Go Server     │ ─────────────> │  Local Storage  │
│  + OV5640       │   (Image Upload) │                 │   (Save Images)│    (uploads/)   │
│  + XPowers PMU  │                  │                 │                │                 │
└─────────────────┘                  └─────────────────┘                └─────────────────┘
```

I hosted the project using [NGROK][https://ngrok.com/] for this development project. 

##  Project Overview

This project consists of two main components:

1. **ESP32 Camera Module** - Captures images periodically and uploads them via HTTP
2. **Go Upload Server** - Receives and stores uploaded images with timestamps

The system is designed for autonomous operation, taking photos every 30 seconds (configurable) and uploading them to a server for remote monitoring or surveillance applications.

## Hardware Components

- **ESP32-S3 Development Board** (LilyGO T3-S3)
- **OV5640 5MP Camera Module** with auto-focus capability
- **XPowers AXP2101 PMU** for power management
- **PSRAM** for high-resolution image buffering

### Hardware Features
- 5MP camera with JPEG compression
- Auto white balance, exposure, and gain control
- Power management for camera modules
- WiFi connectivity for image transmission
- Serial debugging output

## Project Structure

```
testing-ESP32/
├── src/
│   ├── MinimalCameraExample.ino    # Main ESP32 firmware
│   ├── app_httpd.cpp              # HTTP server (streaming - currently unused)
│   ├── utilities.h                # Hardware pin definitions
│   ├── wifi_config.h              # WiFi credentials (create from template)
│   └── wifi_config_template.h     # WiFi configuration template
├── server/
│   ├── main.go                    # Go upload server
│   ├── go.mod                     # Go module dependencies
│   └── uploads/                   # Stored images directory
├── platformio.ini                 # PlatformIO build configuration
├── CMakeLists.txt                # ESP-IDF build configuration
├── partitions.csv                # ESP32 flash partitioning
├── sdkconfig.lilygo-t3-s3        # ESP32 SDK configuration
└── OV5640_README.md              # Camera-specific documentation
```

## Quick Start Guide

### 1. Hardware Setup

1. Connect the OV5640 camera module to your ESP32-S3 board
2. Ensure proper power connections through the XPowers PMU
3. Verify PSRAM is available for high-resolution capture

### 2. ESP32 Firmware Setup

1. **Install PlatformIO** if not already installed
2. **Configure WiFi credentials:**
   ```bash
   cp src/wifi_config_template.h src/wifi_config.h
   ```
   Edit `src/wifi_config.h` with your WiFi credentials:
   ```cpp
   const char* WIFI_SSID = "YourWiFiName";
   const char* WIFI_PASSWORD = "YourWiFiPassword";
   ```

3. **Build and upload firmware:**
   ```bash
   platformio run --target upload
   ```

4. **Monitor serial output:**
   ```bash
   platformio device monitor
   ```

### 3. Server Setup

1. **Navigate to server directory:**
   ```bash
   cd server
   ```

2. **Run the Go server:**
   ```bash
   go run main.go
   ```
   Server will start on `http://localhost:8081`

3. **For remote access, set up ngrok (optional):**
   ```bash
   ngrok http 8081
   ```
   Update the ESP32 code with your ngrok URL.

### 4. Update Upload URL

In `MinimalCameraExample.ino`, update the server URL:
```cpp
String url = "http://your-server-address:8081/upload";
// or for ngrok:
String url = "http://your-ngrok-id.ngrok-free.app/upload";
```

## System Flow

### ESP32 Operation Flow

1. **Initialization Phase:**
   - Initialize XPowers PMU for camera power management
   - Configure camera power rails (1.8V, 2.8V, 3.0V)
   - Connect to WiFi using provided credentials
   - Initialize OV5640 camera with optimized settings

2. **Image Capture Loop:**
   - Wait for capture interval (30 seconds default)
   - Capture JPEG image from OV5640 camera
   - Prepare multipart form data with image
   - Upload via HTTP POST to server
   - Log results and repeat

3. **Error Handling:**
   - WiFi connection monitoring
   - Camera capture failure detection
   - HTTP upload retry logic
   - Memory management for large images

### Server Operation Flow

1. **HTTP Server Setup:**
   - Start Go HTTP server on port 8081
   - Create uploads directory if not exists
   - Register `/upload` and `/` endpoints

2. **Image Processing:**
   - Receive multipart form data
   - Validate file as image format
   - Generate timestamped filename
   - Save to local uploads directory
   - Return JSON success response

## Configuration Options

### Camera Settings (in MinimalCameraExample.ino)

```cpp
// Capture interval (milliseconds)
const unsigned long captureInterval = 30000; // 30 seconds

// Image quality settings
config.jpeg_quality = 10;        // JPEG quality (0-63, lower = higher quality)
config.frame_size = FRAMESIZE_XGA; // Resolution (VGA, SVGA, XGA, etc.)
```

### Server Settings (in server/main.go)

```go
const (
    uploadDir = "uploads"  // Directory to save images
    port      = ":8081"    // Server port
)
```

### WiFi Configuration

Create `src/wifi_config.h` from template:
```cpp
const char* WIFI_SSID = "YourNetworkName";
const char* WIFI_PASSWORD = "YourNetworkPassword";
```

## Development Tools

### PlatformIO Commands
```bash
# Build project
platformio run

# Upload to device
platformio run --target upload

# Monitor serial output
platformio device monitor

# Clean build
platformio run --target clean
```

### Go Server Commands
```bash
# Run server
go run main.go

# Build executable
go build -o image-server main.go

# Run with custom port
PORT=8080 go run main.go
```

## Monitoring & Debugging

### ESP32 Serial Output
The ESP32 provides detailed logging via serial monitor:
- WiFi connection status
- Camera initialization results
- Image capture statistics (size, resolution)
- HTTP upload status and responses
- Memory usage information
- Error messages and troubleshooting info

### Server Logs
The Go server logs:
- Incoming upload requests
- File validation results
- Successful saves with file sizes
- Error conditions

### File Organization
Uploaded images are saved with timestamps:
```
uploads/
├── 20241009_112429_image.jpg
├── 20241009_122605_image.jpg
└── 20241009_122635_image.jpg
```

## Troubleshooting

### Common Issues

1. **Camera Initialization Failed:**
   - Check camera module connections
   - Verify power supply stability
   - Ensure PSRAM is available

2. **WiFi Connection Problems:**
   - Verify WiFi credentials in `wifi_config.h`
   - Check network signal strength
   - Ensure 2.4GHz network (ESP32 doesn't support 5GHz)

3. **Upload Failures:**
   - Verify server is running and accessible
   - Check firewall settings
   - Validate ngrok tunnel if using remote access
   - Monitor memory usage during upload

4. **Image Quality Issues:**
   - Adjust JPEG quality setting
   - Modify frame size for better performance
   - Check camera lens focus and cleanliness

### Debug Information

Enable verbose debugging by checking serial monitor output. The ESP32 provides comprehensive logging for:
- Memory allocation status
- HTTP request/response details
- Image capture metrics
- Network connectivity status

## Advanced Features

### OV5640 Camera Optimizations
- **Auto-focus capability** for sharp images
- **Lens correction** for distortion compensation
- **Automatic exposure and white balance**
- **Multiple resolution support** (up to 5MP)
- **Hardware JPEG compression**

### Power Management
- **XPowers PMU integration** for efficient power usage
- **Camera power rail management** (1.8V, 2.8V, 3.0V)
- **Sleep mode capability** for battery applications

### Networking Features
- **WiFi connection management** with auto-reconnect
- **HTTP multipart form upload** with proper headers
- **Memory-efficient image transmission**
- **Error recovery and retry logic**

## TODO / Future Improvements

- [ ] Add image compression options
- [ ] Implement motion detection
- [ ] Add battery level monitoring
- [ ] Create web interface for image viewing
- [ ] Add FTP upload option
- [ ] Implement time-lapse functionality
- [ ] Add remote configuration via web interface
- [ ] Implement secure HTTPS upload

## License

This project is based on the LilyGO ESP32-S3 camera examples with modifications for OV5640 support and image uploading functionality.

## Contributing

Feel free to submit issues, fork the repository, and create pull requests for any improvements.