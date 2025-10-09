/**
 * @file      MinimalCameraExample.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2022  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2022-09-16
 * @modified  Modified to support OV5640 camera module
 *
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include "esp_camera.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "utilities.h"
#include "wifi_config.h"


void        startCameraServer();

XPowersPMU  PMU;
WiFiMulti   wifiMulti;
String      hostName = "LilyGo-Cam-";
String      ipAddress = "";
bool        use_ap_mode = false;

unsigned long lastCaptureTime = 0;
const unsigned long captureInterval = 30000; // Capture every 30 seconds (adjust as needed)
int imageCounter = 0;



void setup()
{

    Serial.begin(115200);

    // Start while waiting for Serial monitoring
    while (!Serial);

    delay(3000);

    Serial.println();

    /*********************************
     *  step 1 : Initialize power chip,
     *  turn on camera power channel
    ***********************************/
    if (!PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
        Serial.println("Failed to initialize power.....");
        while (1) {
            delay(5000);
        }
    }
    // Set the working voltage of the camera, please do not modify the parameters
    PMU.setALDO1Voltage(1800);  // CAM DVDD 1500~1800
    PMU.enableALDO1();
    PMU.setALDO2Voltage(2800);  // CAM DVDD 2500~2800
    PMU.enableALDO2();
    PMU.setALDO4Voltage(3000);  // CAM AVDD 2800~3000
    PMU.enableALDO4();

    // TS Pin detection must be disable, otherwise it cannot be charged
    PMU.disableTSPinMeasure();


    /*********************************
     * step 2 : start network
     * If using station mode, please change use_ap_mode to false,
     * and fill in your account password in wifiMulti
    ***********************************/
 

    // Connect to WiFi using credentials from wifi_config.h
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    
    Serial.println("");
    Serial.println("WiFi connected successfully!");
    Serial.print("Network: ");
    Serial.println(WIFI_SSID);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength: ");
    Serial.println(WiFi.RSSI());
    



    /*********************************
     *  step 3 : Initialize camera
    ***********************************/
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000; // 20MHz works well for OV5640
    config.frame_size = FRAMESIZE_SVGA; // Start with reasonable size for OV5640 streaming (800x600)
    config.pixel_format = PIXFORMAT_JPEG; // for streaming
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 12; // Balanced quality for streaming
    config.fb_count = 1;

    // Configure for streaming optimization
    if (config.pixel_format == PIXFORMAT_JPEG) {
        if (psramFound()) {
            config.jpeg_quality = 10; // Good quality for OV5640
            config.fb_count = 2;
            config.grab_mode = CAMERA_GRAB_LATEST;
            // OV5640 streaming at XGA resolution
            config.frame_size = FRAMESIZE_XGA; // 1024x768 for good balance
        } else {
            // Limit the frame size when PSRAM is not available
            config.frame_size = FRAMESIZE_SVGA; // 800x600 fallback
            config.fb_location = CAMERA_FB_IN_DRAM;
        }
    } else {
        // RGB format fallback
        config.frame_size = FRAMESIZE_VGA;
#if CONFIG_IDF_TARGET_ESP32S3
        config.fb_count = 2;
#endif
    }

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x Please check if the camera is connected well.", err);
        while (1) {
            delay(5000);
        }
    }

    sensor_t *s = esp_camera_sensor_get();
    // Configure for OV5640 camera sensor
    if (s->id.PID == OV5640_PID) {
        Serial.println("OV5640 camera sensor detected - applying optimized settings");
        // OV5640 specific optimizations
        s->set_vflip(s, 0); // OV5640 typically doesn't need vertical flip
        s->set_hmirror(s, 0); // No horizontal mirror by default
        s->set_brightness(s, 0); // Start with neutral brightness
        s->set_contrast(s, 0); // Neutral contrast
        s->set_saturation(s, 0); // Neutral saturation
        s->set_sharpness(s, 0); // Neutral sharpness
        s->set_denoise(s, 0); // Disable denoise for better performance
        s->set_gainceiling(s, (gainceiling_t)6); // Set gain ceiling
        s->set_quality(s, 10); // JPEG quality (0-63, lower means higher quality)
        s->set_colorbar(s, 0); // Disable color bar test pattern
        s->set_whitebal(s, 1); // Enable auto white balance
        s->set_gain_ctrl(s, 1); // Enable AGC (Automatic Gain Control)
        s->set_exposure_ctrl(s, 1); // Enable AEC (Automatic Exposure Control)
        s->set_awb_gain(s, 1); // Enable Auto White Balance gain
        s->set_aec2(s, 0); // Disable AEC sensor
        s->set_aec_value(s, 300); // Set AEC value
        s->set_agc_gain(s, 0); // Set AGC gain
        s->set_bpc(s, 0); // Disable black pixel correction
        s->set_wpc(s, 1); // Enable white pixel correction
        s->set_raw_gma(s, 1); // Enable raw gamma
        s->set_lenc(s, 1); // Enable lens correction
        s->set_special_effect(s, 0); // No special effects
        s->set_wb_mode(s, 0); // Auto white balance mode
        s->set_ae_level(s, 0); // Auto exposure level
    }
    // Start with moderate frame size for stable streaming
    if (config.pixel_format == PIXFORMAT_JPEG) {
        s->set_framesize(s, FRAMESIZE_VGA); // Start with VGA for stable streaming
    }

#if defined(LILYGO_ESP32S3_CAM_PIR_VOICE)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
#endif



    /*********************************
     *  step 4 : Camera ready for periodic capture
    ***********************************/
    Serial.println("Camera initialized and ready for periodic image capture");
    // startCameraServer(); // Commented out - we're uploading images instead of serving them

}

void loop()
{
    unsigned long currentTime = millis();
    
    // Check if it's time to capture an image
    if (currentTime - lastCaptureTime >= captureInterval) {
        captureAndProcessImage();
        lastCaptureTime = currentTime;
    }
    
    delay(100); // Short delay to prevent excessive CPU usage
}

void captureAndProcessImage() {
    Serial.println("Capturing image...");
    
    // Take a picture
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }
    
    Serial.printf("Image captured! Size: %d bytes, Width: %d, Height: %d\n", 
                  fb->len, fb->width, fb->height);
    
    // Upload the image to server
    uploadImageToServer(fb->buf, fb->len);
    
    // Increment counter and show info
    imageCounter++;
    Serial.printf("Image #%d processed\n", imageCounter);
    
    // Don't forget to return the frame buffer
    esp_camera_fb_return(fb);
}

// Upload image to server via HTTP POST with multipart form data
void uploadImageToServer(uint8_t* imageData, size_t imageSize) {
    Serial.println("=== STARTING IMAGE UPLOAD DEBUG ===");
    Serial.printf("WiFi Status: %d (3=Connected)\n", WiFi.status());
    Serial.printf("Image data pointer: %p\n", imageData);
    Serial.printf("Image size: %d bytes\n", imageSize);
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, skipping upload");
        return;
    }
    
    if (!imageData || imageSize == 0) {
        Serial.println("Invalid image data or size");
        return;
    }
    
    // Check available heap memory
    Serial.printf("Free heap before HTTP: %d bytes\n", ESP.getFreeHeap());
    
    HTTPClient http;
    
    // Configure HTTP client for ngrok
    String url = "http://484ec20e9888.ngrok-free.app/upload";
    Serial.printf("Connecting to: %s\n", url.c_str());
    
    bool httpBeginResult = http.begin(url);
    Serial.printf("HTTP begin result: %s\n", httpBeginResult ? "SUCCESS" : "FAILED");
    
    if (!httpBeginResult) {
        Serial.println("Failed to initialize HTTP client");
        return;
    }
    
    http.addHeader("ngrok-skip-browser-warning", "true");
    http.addHeader("User-Agent", "ESP32-Camera/1.0");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Handle redirects
    http.setTimeout(15000); // Re-add timeout but shorter
    
    Serial.println("✓ HTTP client configured, starting upload...");
    
    // Create multipart form data boundary
    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    String contentType = "multipart/form-data; boundary=" + boundary;
    http.addHeader("Content-Type", contentType);
    
    // Calculate total payload size
    String header = "--" + boundary + "\r\n";
    header += "Content-Disposition: form-data; name=\"file\"; filename=\"image.jpg\"\r\n";
    header += "Content-Type: image/jpeg\r\n\r\n";
    
    String footer = "\r\n--" + boundary + "--\r\n";
    
    size_t totalSize = header.length() + imageSize + footer.length();
    
    // Check if we have enough memory
    Serial.printf("Free heap before malloc: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Trying to allocate: %d bytes for payload\n", totalSize);
    
    // Create payload buffer (more memory efficient)
    uint8_t* payload = (uint8_t*)malloc(totalSize);
    if (!payload) {
        Serial.printf("Failed to allocate %d bytes for payload\n", totalSize);
        Serial.printf("Available heap: %d bytes\n", ESP.getFreeHeap());
        http.end();
        return;
    }
    
    Serial.printf("✓ Successfully allocated %d bytes for payload\n", totalSize);
    Serial.printf("Free heap after malloc: %d bytes\n", ESP.getFreeHeap());
    
    // Build multipart payload with proper binary handling
    size_t offset = 0;
    
    // Add header
    memcpy(payload + offset, header.c_str(), header.length());
    offset += header.length();
    
    // Add binary image data (NO CONVERSION - keeps original bytes)
    memcpy(payload + offset, imageData, imageSize);
    offset += imageSize;
    
    // Add footer
    memcpy(payload + offset, footer.c_str(), footer.length());
    
    Serial.printf("✓ Payload assembled successfully\n");
    Serial.printf("Total payload size: %d bytes\n", totalSize);
    Serial.printf("  - Header size: %d bytes\n", header.length());
    Serial.printf("  - Image size: %d bytes\n", imageSize);
    Serial.printf("  - Footer size: %d bytes\n", footer.length());
    
    // Print first few bytes of payload for verification
    Serial.print("First 20 bytes of payload: ");
    for (int i = 0; i < 20 && i < totalSize; i++) {
        Serial.printf("%02X ", payload[i]);
    }
    Serial.println();
    
    Serial.println("Sending POST request...");
    
    // Send binary POST request
    int httpResponseCode = http.POST(payload, totalSize);
    
    Serial.printf("POST request completed with response code: %d\n", httpResponseCode);
    
    // Clean up memory
    free(payload);
    
    Serial.printf("Response code received: %d\n", httpResponseCode);
    
    // Handle response
    if (httpResponseCode >= 200 && httpResponseCode < 300) {
        String response = http.getString();
        Serial.printf("Upload successful! Response code: %d\n", httpResponseCode);
        Serial.printf("Server response: %s\n", response.c_str());
    } else if (httpResponseCode >= 300 && httpResponseCode < 400) {
        Serial.printf("Redirect detected (code: %d)\n", httpResponseCode);
        String location = http.header("Location");
        if (location.length() > 0) {
            Serial.printf("Redirect location: %s\n", location.c_str());
        }
        
        // Print all headers for debugging
        Serial.println("All response headers:");
        for (int i = 0; i < http.headers(); i++) {
            Serial.printf("  %s: %s\n", http.headerName(i).c_str(), http.header(i).c_str());
        }
        
        String response = http.getString();
        if (response.length() > 0) {
            Serial.printf("Response body: %s\n", response.c_str());
        }
    } else if (httpResponseCode <= 0) {
        Serial.printf("HTTP Connection failed! Error code: %d\n", httpResponseCode);
        Serial.printf("Error description: %s\n", http.errorToString(httpResponseCode).c_str());
        Serial.println("Possible causes:");
        Serial.println("  - ngrok tunnel is down");
        Serial.println("  - WiFi connection unstable");
        Serial.println("  - DNS resolution failed");
        Serial.println("  - Server not responding");
    } else {
        Serial.printf("Upload failed! HTTP Error code: %d\n", httpResponseCode);
        Serial.printf("Error description: %s\n", http.errorToString(httpResponseCode).c_str());
        String response = http.getString();
        if (response.length() > 0) {
            Serial.printf("Error response: %s\n", response.c_str());
        }
        
        // Additional debugging info
        Serial.printf("Connected to host: %s\n", http.connected() ? "Yes" : "No");
        Serial.printf("Final URL: %s\n", http.getLocation().c_str());
    }
    
    http.end();
    
    Serial.printf("Free heap after HTTP: %d bytes\n", ESP.getFreeHeap());
    Serial.println("=== UPLOAD DEBUG COMPLETE ===\n");
}
