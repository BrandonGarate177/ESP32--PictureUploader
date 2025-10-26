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
#include "esp_mac.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "utilities.h"
#include "wifi_config.h"
#include "server_config.h"

// SD Card pins - Option 4: Ultra-safe GPIO numbers
// GPIO 38-41 are typically the safest on ESP32-S3
#define SD_CS_PIN 38     // Option 4 CS pin (ultra-safe GPIO)
#define SD_MOSI_PIN 39   // Option 4 MOSI pin  
#define SD_MISO_PIN 40   // Option 4 MISO pin
#define SD_SCK_PIN 41    // Option 4 SCK pin

// Previous attempts that failed (commented out):
// Option 2 - Failed: 14, 15, 16, 17 (hardware command failures)
// Option 1 - Failed: 10, 11, 13, 12 (hardware command failures)  
// Original - Failed: 21, 19, 18, 5 (I2C/camera conflicts, caused resets)


void        startCameraServer();

XPowersPMU  PMU;
WiFiMulti   wifiMulti;
String      hostName = "LilyGo-Cam-";
String      ipAddress = "";
bool        use_ap_mode = false;

unsigned long lastCaptureTime = 0;
const unsigned long captureInterval = 5000; // Capture every 5 seconds (fast testing)
int imageCounter = 0;

// Upload tracking variables
int uploadAttempts = 0;
int uploadSuccesses = 0;
int uploadFailures = 0;
unsigned long totalUploadTime = 0;
size_t totalBytesUploaded = 0;

// SD card monitoring
unsigned long lastSDListTime = 0;
const unsigned long sdListInterval = 120000; // List SD contents every 2 minutes



void setup()
{
    Serial.begin(115200);
    
    // Add immediate startup message
    Serial.println("=== ESP32 CAMERA BOOTING ===");
    Serial.println("Serial communication initialized");
    Serial.flush();

    // Replace the blocking while (!Serial); with a timeout
    unsigned long serialTimeout = millis() + 5000; // 5 second timeout
    while (!Serial && millis() < serialTimeout) {
        delay(100);
    }
    
    if (Serial) {
        Serial.println("Serial monitor connected within timeout");
    } else {
        Serial.println("Serial monitor timeout - continuing without monitor");
    }

    delay(1000); // Reduced from 3000ms

    Serial.println("Starting main initialization...");
    Serial.flush();

    Serial.println();

    /*********************************
     *  step 1 : Initialize power chip,
     *  turn on camera power channel
    ***********************************/
    Serial.println("Step 1: Initializing power management...");
    if (!PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
        Serial.println("Failed to initialize power management");
        Serial.println("This might be normal for some boards without AXP2101");
        // Don't hang here - some boards might not have this chip
        // while (1) { delay(5000); }
    } else {
        Serial.println("Power management initialized successfully");
    }
    // Set the working voltage of the camera, please do not modify the parameters
    if (Serial) {
        PMU.setALDO1Voltage(1800);  // CAM DVDD 1500~1800
        PMU.enableALDO1();
        PMU.setALDO2Voltage(2800);  // CAM DVDD 2500~2800
        PMU.enableALDO2();
        PMU.setALDO4Voltage(3000);  // CAM AVDD 2800~3000
        PMU.enableALDO4();

        // TS Pin detection must be disable, otherwise it cannot be charged
        PMU.disableTSPinMeasure();
        Serial.println("Power voltages configured");
    }

    /*********************************
     * step 2 : start network
     * If using station mode, please change use_ap_mode to false,
     * and fill in your account password in wifiMulti
    ***********************************/
    Serial.println("Step 2: Connecting to WiFi...");
 

    // Connect to WiFi using credentials from wifi_config.h
    Serial.printf("Attempting to connect to: %s\n", WIFI_SSID);
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
    Serial.println("Step 3: Initializing camera...");
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

    // Robust camera initialization with retry logic
    Serial.println("Initializing camera with retry logic...");
    esp_err_t err = ESP_FAIL;
    int retry_count = 0;
    const int max_retries = 3;
    
    while (err != ESP_OK && retry_count < max_retries) {
        retry_count++;
        Serial.printf("Camera init attempt %d/%d...\n", retry_count, max_retries);
        
        // Clean shutdown any previous camera instance
        if (retry_count > 1) {
            esp_camera_deinit();
            delay(1000); // Allow hardware to reset
        }
        
        err = esp_camera_init(&config);
        
        if (err != ESP_OK) {
            Serial.printf("Camera init failed with error 0x%x", err);
            if (retry_count < max_retries) {
                Serial.println(" - retrying...");
                delay(2000);
            } else {
                Serial.println(" - max retries reached!");
                while (1) {
                    delay(5000);
                }
            }
        } else {
            Serial.println("Camera initialized successfully!");
            // Give sensor time to stabilize after init
            delay(2000);
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

    // Sensor stabilization - critical for preventing green screen
    Serial.println("Stabilizing camera sensor...");
    delay(2000); // Allow sensor to fully initialize
    
    // Warm up the sensor with a few dummy captures
    Serial.println("Warming up sensor (dummy captures)...");
    for (int i = 0; i < 3; i++) {
        camera_fb_t *warm_fb = esp_camera_fb_get();
        if (warm_fb) {
            Serial.printf("Warmup capture %d: %dx%d, %d bytes\n", 
                         i+1, warm_fb->width, warm_fb->height, warm_fb->len);
            esp_camera_fb_return(warm_fb);
        }
        delay(500);
    }
    Serial.println("Camera sensor ready for capture!");



    /*********************************
     *  step 4 : Initialize SD Card
    ***********************************/
    Serial.println("Step 4: Initializing SD card...");
    Serial.println("Battery power is sufficient - attempting SD card initialization");
    
    // Initialize SPI for SD card with careful power management
    Serial.println("Configuring SPI bus for SD card...");
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    
    // Add small delay for power stabilization
    delay(100);
    
    Serial.println("Attempting SD card initialization...");
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD Card initialization failed!");
        Serial.println("Possible causes:");
        Serial.println("  • No SD card inserted");
        Serial.println("  • SD card corrupted or incompatible");
        Serial.println("  • Pin connections incorrect");
        Serial.println("Continuing without SD card support...");
    } else {
        uint8_t cardType = SD.cardType();
        if (cardType == CARD_NONE) {
            Serial.println("No SD card attached");
        } else {
            Serial.println("✓ SD card initialized successfully!");
            Serial.printf("SD Card Type: %s\n", 
                cardType == CARD_MMC ? "MMC" :
                cardType == CARD_SD ? "SDSC" :
                cardType == CARD_SDHC ? "SDHC" : "UNKNOWN");
            
            uint64_t cardSize = SD.cardSize() / (1024 * 1024);
            Serial.printf("SD Card Size: %lluMB\n", cardSize);
            
            uint64_t usedBytes = SD.usedBytes();
            uint64_t totalBytes = SD.totalBytes();
            Serial.printf("Used Space: %.2f MB / %.2f MB (%.1f%%)\n", 
                          usedBytes / (1024.0 * 1024.0), 
                          totalBytes / (1024.0 * 1024.0),
                          (float)usedBytes / totalBytes * 100);
            
            // Create images directory if it doesn't exist
            if (!SD.exists("/images")) {
                if (SD.mkdir("/images")) {
                    Serial.println("✓ Created /images directory");
                } else {
                    Serial.println("⚠ Failed to create /images directory");
                }
            } else {
                Serial.println("✓ /images directory already exists");
            }
        }
    }

    /*********************************
     *  step 5 : Camera ready for periodic capture
    ***********************************/
    Serial.println("=== INITIALIZATION COMPLETE ===");
    Serial.printf("System ready at %s\n", getTimeString().c_str());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println("Will capture first image in 5 seconds...");
    Serial.println("Camera ready for image capture every 5 seconds with SD card storage + cloud upload");
    // startCameraServer(); // Commented out - we're uploading images instead of serving them

}

void loop()
{
    unsigned long currentTime = millis();
    
    // Add heartbeat every 10 seconds to verify system is alive
    static unsigned long lastHeartbeat = 0;
    if (currentTime - lastHeartbeat >= 10000) {
        Serial.println("\n=== SYSTEM HEARTBEAT ===");
        Serial.printf("Time: %s | Free heap: %d bytes\n", getTimeString().c_str(), ESP.getFreeHeap());
        Serial.printf("Stats: Images captured: %d | Upload attempts: %d | Successes: %d\n", 
                      imageCounter, uploadAttempts, uploadSuccesses);
        
        // Monitor battery and power status
        printBatteryStatus();
        
        // Monitor WiFi connection
        checkWiFiConnection();
        
        Serial.println("========================\n");
        lastHeartbeat = currentTime;
    }
    
    // Check if it's time to capture an image
    if (currentTime - lastCaptureTime >= captureInterval) {
        captureAndProcessImage();
        lastCaptureTime = currentTime;
    }
    
    // Check if it's time to print detailed statistics
    if (currentTime - lastSDListTime >= sdListInterval) {
        printDetailedSDCardInfo();
        printUploadStatistics();
        lastSDListTime = currentTime;
    }
    
    delay(100); // Short delay to prevent excessive CPU usage
}

void captureAndProcessImage() {
    Serial.println("\n=== STARTING IMAGE CAPTURE #" + String(imageCounter + 1) + " ===");
    Serial.printf("Capture Time: %s\n", getTimeString().c_str());
    Serial.printf("Free Heap Before Capture: %d bytes\n", ESP.getFreeHeap());
    
    // Take a picture with validation and retry logic
    camera_fb_t *fb = nullptr;
    int capture_attempts = 0;
    const int max_capture_attempts = 3;
    
    while (!fb && capture_attempts < max_capture_attempts) {
        capture_attempts++;
        Serial.printf("Camera capture attempt %d/%d...\n", capture_attempts, max_capture_attempts);
        
        fb = esp_camera_fb_get();
        
        if (!fb) {
            Serial.printf("Camera capture failed on attempt %d\n", capture_attempts);
            if (capture_attempts < max_capture_attempts) {
                delay(500); // Wait before retry
            }
            continue;
        }
        
        // Validate frame buffer data
        bool valid_frame = true;
        
        // Check for minimum size (avoid green screen frames)
        if (fb->len < 1000) {
            Serial.printf("Frame too small (%d bytes) - likely corrupted\n", fb->len);
            valid_frame = false;
        }
        
        // Check for JPEG header if format is JPEG
        if (fb->format == PIXFORMAT_JPEG && fb->len > 10) {
            if (fb->buf[0] != 0xFF || fb->buf[1] != 0xD8) {
                Serial.println("Invalid JPEG header - corrupted frame");
                valid_frame = false;
            }
        }
        
        // Check for all-zero or near-zero data (green screen indicator)
        if (valid_frame && fb->len > 100) {
            int zero_count = 0;
            for (int i = 0; i < 100; i++) {
                if (fb->buf[i] == 0x00) zero_count++;
            }
            if (zero_count > 80) {
                Serial.println("Frame appears to be mostly zeros - likely green screen");
                valid_frame = false;
            }
        }
        
        if (!valid_frame) {
            Serial.println("Invalid frame detected, releasing and retrying...");
            esp_camera_fb_return(fb);
            fb = nullptr;
            if (capture_attempts < max_capture_attempts) {
                delay(1000); // Longer wait for corrupted frames
            }
        }
    }
    
    if (!fb) {
        Serial.println("All camera capture attempts failed!");
        return;
    }
    
    Serial.printf("✓ Valid image captured successfully!\n");
    Serial.printf("Dimensions: %d x %d pixels\n", fb->width, fb->height);
    Serial.printf("Size: %d bytes (%.2f KB)\n", fb->len, fb->len / 1024.0);
    Serial.printf("Format: %s\n", fb->format == PIXFORMAT_JPEG ? "JPEG" : "Other");
    
    // Generate filename with timestamp
    String filename = "/images/img_" + String(imageCounter + 1) + "_" + String(millis()) + ".jpg";
    Serial.printf("Generated filename: %s\n", filename.c_str());
    
    // Try to save to SD card first, fallback to direct upload if SD fails
    bool savedToSD = saveImageToSD(fb->buf, fb->len, filename);
    
    if (savedToSD) {
        Serial.println("✓ Image saved to SD card successfully!");
        
        // Upload from SD card to server (provides backup if upload fails)
        uploadImageFromSD(filename);
        
        // Optionally print SD card contents periodically (every 10th image)
        if (imageCounter % 10 == 0) {
            printSDCardContents();
        }
    } else {
        Serial.println("⚠ Failed to save to SD card, uploading directly from memory...");
        // Fallback: upload directly from memory
        uploadImageToServer(fb->buf, fb->len);
    }
    
    // Increment counter and show info
    imageCounter++;
    Serial.printf("Image #%d processed\n", imageCounter);
    
    // Don't forget to return the frame buffer
    esp_camera_fb_return(fb);
}

bool saveImageToSD(uint8_t* imageData, size_t imageSize, String filename) {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card not available");
        return false;
    }
    
    Serial.printf("Saving image to SD: %s\n", filename.c_str());
    
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return false;
    }
    
    size_t bytesWritten = file.write(imageData, imageSize);
    file.close();
    
    if (bytesWritten == imageSize) {
        Serial.printf("Successfully saved %d bytes to %s\n", bytesWritten, filename.c_str());
        return true;
    } else {
        Serial.printf("Write error: only %d of %d bytes written\n", bytesWritten, imageSize);
        return false;
    }
}

void printSDCardContents() {
    Serial.println("\n=== SD CARD CONTENTS ===");
    
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card not available");
        return;
    }
    
    File root = SD.open("/");
    if (!root) {
        Serial.println("Failed to open root directory");
        return;
    }
    
    printDirectory(root, 0);
    root.close();
    
    // Print images directory specifically
    Serial.println("\n--- Images Directory ---");
    File imagesDir = SD.open("/images");
    if (imagesDir) {
        printDirectory(imagesDir, 0);
        imagesDir.close();
    } else {
        Serial.println("Images directory not found");
    }
    
    Serial.println("========================\n");
}

void printDirectory(File dir, int numTabs) {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        
        for (uint8_t i = 0; i < numTabs; i++) {
            Serial.print('\t');
        }
        
        Serial.print(entry.name());
        if (entry.isDirectory()) {
            Serial.println("/");
            printDirectory(entry, numTabs + 1);
        } else {
            Serial.print("\t\t");
            Serial.println(entry.size());
        }
        entry.close();
    }
}

// Get formatted time string for logging
String getTimeString() {
    unsigned long currentMillis = millis();
    unsigned long seconds = currentMillis / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    
    seconds = seconds % 60;
    minutes = minutes % 60;
    hours = hours % 24;
    
    char timeStr[20];
    sprintf(timeStr, "%02lu:%02lu:%02lu.%03lu", hours, minutes, seconds, currentMillis % 1000);
    return String(timeStr);
}

// Enhanced SD card information with detailed file listing
void printDetailedSDCardInfo() {
    Serial.println("\n=== PERIODIC SD CARD REPORT ===");
    Serial.printf("Scan Time: %s\n", getTimeString().c_str());
    
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card not available");
        return;
    }
    
    // Get card information
    uint8_t cardType = SD.cardType();
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    uint64_t usedBytes = SD.usedBytes();
    uint64_t totalBytes = SD.totalBytes();
    
    Serial.printf("Card Type: %s\n", 
                  cardType == CARD_MMC ? "MMC" :
                  cardType == CARD_SD ? "SDSC" :
                  cardType == CARD_SDHC ? "SDHC" : "UNKNOWN");
    Serial.printf("Card Size: %llu MB\n", cardSize);
    Serial.printf("Used Space: %.2f MB / %.2f MB (%.1f%%)\n", 
                  usedBytes / (1024.0 * 1024.0), 
                  totalBytes / (1024.0 * 1024.0),
                  (float)usedBytes / totalBytes * 100);
    
    // List all files in images directory with details
    Serial.println("\n=== IMAGES DIRECTORY CONTENTS ===");
    File imagesDir = SD.open("/images");
    if (imagesDir) {
        int fileCount = 0;
        size_t totalImageSize = 0;
        
        while (true) {
            File entry = imagesDir.openNextFile();
            if (!entry) break;
            
            if (!entry.isDirectory()) {
                fileCount++;
                totalImageSize += entry.size();
                
                Serial.printf("[%d] %s - %.2f KB\n", 
                             fileCount, entry.name(), entry.size() / 1024.0);
            }
            entry.close();
        }
        
        imagesDir.close();
        
        Serial.printf("\nImages Summary:\n");
        Serial.printf("   Total Files: %d\n", fileCount);
        Serial.printf("   Total Size: %.2f MB\n", totalImageSize / (1024.0 * 1024.0));
        if (fileCount > 0) {
            Serial.printf("   Average Size: %.2f KB per file\n", (totalImageSize / 1024.0) / fileCount);
        }
    } else {
        Serial.println("Images directory not found or cannot be opened");
    }
    
    Serial.println("===============================\n");
}

// Print upload statistics
void printUploadStatistics() {
    Serial.println("=== UPLOAD STATISTICS ===");
    Serial.printf("Total Attempts: %d\n", uploadAttempts);
    Serial.printf("Successes: %d\n", uploadSuccesses);
    Serial.printf("Failures: %d\n", uploadFailures);

    if (uploadAttempts > 0) {
        float successRate = (float)uploadSuccesses / uploadAttempts * 100;
        Serial.printf("Success Rate: %.1f%%\n", successRate);

        if (uploadSuccesses > 0) {
            float avgUploadTime = (float)totalUploadTime / uploadSuccesses;
            float avgFileSize = (float)totalBytesUploaded / uploadSuccesses / 1024.0;
            Serial.printf("Average Upload Time: %.1f ms\n", avgUploadTime);
            Serial.printf("Average File Size: %.2f KB\n", avgFileSize);
            Serial.printf("Total Data Uploaded: %.2f MB\n", totalBytesUploaded / (1024.0 * 1024.0));
        }
    }

    Serial.printf("Current Free Heap: %d bytes (%.2f KB)\n", 
                  ESP.getFreeHeap(), ESP.getFreeHeap() / 1024.0);
    Serial.println("============================\n");
}

void uploadImageFromSD(String filename) {
    Serial.printf("Starting upload from SD card: %s\n", filename.c_str());
    
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card not available for upload");
        return;
    }
    
    File file = SD.open(filename);
    if (!file) {
        Serial.println("Failed to open file for reading");
        return;
    }
    
    size_t fileSize = file.size();
    Serial.printf("File size: %d bytes\n", fileSize);
    
    // Allocate buffer for file contents
    uint8_t* fileBuffer = (uint8_t*)malloc(fileSize);
    if (!fileBuffer) {
        Serial.printf("Failed to allocate %d bytes for file buffer\n", fileSize);
        file.close();
        return;
    }
    
    // Read file into buffer
    size_t bytesRead = file.read(fileBuffer, fileSize);
    file.close();
    
    if (bytesRead != fileSize) {
        Serial.printf("Read error: only %d of %d bytes read\n", bytesRead, fileSize);
        free(fileBuffer);
        return;
    }
    
    Serial.printf("Successfully read %d bytes from SD card\n", bytesRead);
    
    // Upload using existing function
    uploadImageToServer(fileBuffer, fileSize);
    
    // Clean up
    free(fileBuffer);
    
    Serial.printf("Upload from SD card completed for: %s\n", filename.c_str());
}

// Validate JPEG image data integrity
bool validateJPEGData(uint8_t* imageData, size_t imageSize) {
    if (!imageData || imageSize < 4) {
        Serial.println("ERROR: Invalid image data or size too small");
        return false;
    }
    
    // Check JPEG magic bytes (SOI - Start of Image)
    if (imageData[0] != 0xFF || imageData[1] != 0xD8 || imageData[2] != 0xFF) {
        Serial.printf("ERROR: Invalid JPEG header. Expected FF D8 FF, got %02X %02X %02X\n", 
                     imageData[0], imageData[1], imageData[2]);
        return false;
    }
    
    // Check for JPEG end marker (EOI - End of Image)
    if (imageSize >= 2) {
        if (imageData[imageSize-2] != 0xFF || imageData[imageSize-1] != 0xD9) {
            Serial.printf("WARNING: Missing JPEG end marker. Expected FF D9, got %02X %02X\n", 
                         imageData[imageSize-2], imageData[imageSize-1]);
            // Don't fail here as some cameras might not add proper EOI
        }
    }
    
    Serial.printf("✓ JPEG validation passed - SOI: %02X %02X %02X, EOI: %02X %02X\n",
                 imageData[0], imageData[1], imageData[2], 
                 imageData[imageSize-2], imageData[imageSize-1]);
    return true;
}

// Upload image to server via HTTP POST with multipart form data
void uploadImageToServer(uint8_t* imageData, size_t imageSize) {
    // Track upload start time
    unsigned long uploadStartTime = millis();
    uploadAttempts++;
    
    // Create detailed timestamp
    String timestamp = getTimeString();

    Serial.println("\n=== STARTING IMAGE UPLOAD #" + String(uploadAttempts) + " ===");
    Serial.printf("Upload Start Time: %s\n", timestamp.c_str());
    Serial.printf("Upload Statistics - Attempts: %d | Successes: %d | Failures: %d\n", 
                  uploadAttempts, uploadSuccesses, uploadFailures);
    Serial.printf("WiFi Status: %d (3=Connected)\n", WiFi.status());
    Serial.printf("Image Counter: #%d\n", imageCounter);
    Serial.printf("Image Size: %d bytes (%.2f KB)\n", imageSize, imageSize / 1024.0);
    Serial.printf("Free Heap: %d bytes (%.2f KB)\n", ESP.getFreeHeap(), ESP.getFreeHeap() / 1024.0);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, skipping upload");
        return;
    }
    
    if (!imageData || imageSize == 0) {
        Serial.println("Invalid image data or size");
        uploadFailures++;
        return;
    }
    
    // Validate JPEG data integrity before uploading
    if (!validateJPEGData(imageData, imageSize)) {
        Serial.println("JPEG validation failed - skipping upload");
        uploadFailures++;
        return;
    }
    
    // Check available heap memory
    Serial.printf("Free heap before HTTP: %d bytes\n", ESP.getFreeHeap());
    
    HTTPClient http;
    
    // Configure HTTP client for ngrok
    String url = SERVER_UPLOAD_URL;
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
    Serial.print("First 20 bytes of payload (multipart headers): ");
    for (int i = 0; i < 20 && i < totalSize; i++) {
        Serial.printf("%02X ", payload[i]);
    }
    Serial.println();
    
    // Print first few bytes of actual image data (skip multipart header)
    size_t imageStart = header.length();
    Serial.printf("First 10 bytes of image data (at offset %d): ", imageStart);
    for (int i = 0; i < 10 && (imageStart + i) < totalSize; i++) {
        Serial.printf("%02X ", payload[imageStart + i]);
    }
    Serial.println();
    
    // Print last few bytes of image data (before footer)
    size_t imageEnd = header.length() + imageSize;
    Serial.printf("Last 10 bytes of image data (at offset %d): ", imageEnd - 10);
    for (int i = imageEnd - 10; i < imageEnd && i < totalSize; i++) {
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
    
    // Calculate upload duration
    unsigned long uploadDuration = millis() - uploadStartTime;
    totalUploadTime += uploadDuration;
    
    // Handle response
    if (httpResponseCode >= 200 && httpResponseCode < 300) {
        uploadSuccesses++;
        totalBytesUploaded += imageSize;
        
        String response = http.getString();
        Serial.println("\n=== UPLOAD SUCCESSFUL ===");
        Serial.printf("Success! Response Code: %d\n", httpResponseCode);
        Serial.printf("Upload Duration: %lu ms\n", uploadDuration);
        Serial.printf("Upload Speed: %.2f KB/s\n", (imageSize / 1024.0) / (uploadDuration / 1000.0));
        Serial.printf("Server Response: %s\n", response.c_str());
        Serial.printf("Total Success Rate: %.1f%% (%d/%d)\n", 
                      (float)uploadSuccesses / uploadAttempts * 100, uploadSuccesses, uploadAttempts);
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
        uploadFailures++;
        Serial.println("\n=== UPLOAD FAILED - CONNECTION ERROR ===");
        Serial.printf("Connection Error Code: %d\n", httpResponseCode);
        Serial.printf("Failed After: %lu ms\n", uploadDuration);
        Serial.printf("Error Description: %s\n", http.errorToString(httpResponseCode).c_str());
        Serial.printf("Total Failure Rate: %.1f%% (%d/%d)\n", 
                      (float)uploadFailures / uploadAttempts * 100, uploadFailures, uploadAttempts);
        Serial.println("Possible causes:");
        Serial.println("   - ngrok tunnel is down");
        Serial.println("   - WiFi connection unstable");
        Serial.println("   - DNS resolution failed");
        Serial.println("   - Server not responding");
    } else {
        uploadFailures++;
        Serial.println("\n=== UPLOAD FAILED - HTTP ERROR ===");
        Serial.printf("HTTP Error Code: %d\n", httpResponseCode);
        Serial.printf("Failed After: %lu ms\n", uploadDuration);
        Serial.printf("Error Description: %s\n", http.errorToString(httpResponseCode).c_str());
        String response = http.getString();
        if (response.length() > 0) {
            Serial.printf("Error Response: %s\n", response.c_str());
        }
        Serial.printf("Total Failure Rate: %.1f%% (%d/%d)\n", 
                      (float)uploadFailures / uploadAttempts * 100, uploadFailures, uploadAttempts);
        
        // Additional debugging info
        Serial.printf("Connected to host: %s\n", http.connected() ? "Yes" : "No");
        Serial.printf("Final URL: %s\n", http.getLocation().c_str());
    }
    
    http.end();

    Serial.printf("Free Heap After: %d bytes (%.2f KB)\n", ESP.getFreeHeap(), ESP.getFreeHeap() / 1024.0);
    Serial.println("=== UPLOAD COMPLETE ===\n");
}

// Battery monitoring function with correct AXP2101 methods
void printBatteryStatus() {
    if (PMU.isBatteryConnect()) {
        Serial.printf("Battery connected: YES\n");
        Serial.printf("Battery voltage: %.2f V\n", PMU.getBattVoltage() / 1000.0);
        Serial.printf("Battery charging: %s\n", PMU.isCharging() ? "YES" : "NO");
        
        // Get system voltage instead of discharge current
        Serial.printf("System voltage: %.2f V\n", PMU.getSystemVoltage() / 1000.0);
        Serial.printf("VBUS voltage: %.2f V\n", PMU.getVbusVoltage() / 1000.0);
        Serial.printf("VBUS present: %s\n", PMU.isVbusIn() ? "YES" : "NO");
    } else {
        Serial.println("Battery connected: NO");
        Serial.printf("VBUS present: %s\n", PMU.isVbusIn() ? "YES (USB Power)" : "NO");
    }
}

// WiFi connection monitoring and auto-reconnect
void checkWiFiConnection() {
    wl_status_t status = WiFi.status();
    Serial.printf("WiFi Status: %d ", status);
    
    switch(status) {
        case WL_CONNECTED:
            Serial.printf("(CONNECTED) - IP: %s, RSSI: %d dBm\n", 
                         WiFi.localIP().toString().c_str(), WiFi.RSSI());
            break;
        case WL_NO_SSID_AVAIL:
            Serial.println("(NO SSID AVAILABLE)");
            break;
        case WL_CONNECT_FAILED:
            Serial.println("(CONNECTION FAILED)");
            break;
        case WL_DISCONNECTED:
            Serial.println("(DISCONNECTED)");
            break;
        default:
            Serial.printf("(OTHER: %d)\n", status);
    }
    
    if (status != WL_CONNECTED) {
        Serial.println("Attempting to reconnect WiFi...");
        WiFi.reconnect();
    }
}
