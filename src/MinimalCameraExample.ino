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
#include "esp_camera.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "utilities.h"


void        startCameraServer();

XPowersPMU  PMU;
WiFiMulti   wifiMulti;
String      hostName = "LilyGo-Cam-";
String      ipAddress = "";
bool        use_ap_mode = true;



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
    if (use_ap_mode) {

        WiFi.mode(WIFI_AP);
        hostName += WiFi.macAddress().substring(0, 5);
        WiFi.softAP(hostName.c_str());
        ipAddress = WiFi.softAPIP().toString();
        Serial.print("Started AP mode host name :");
        Serial.print(hostName);
        Serial.print("IP address is :");
        Serial.println(WiFi.softAPIP().toString());

    } else {

        wifiMulti.addAP("ssid_from_AP_1", "your_password_for_AP_1");
        wifiMulti.addAP("ssid_from_AP_2", "your_password_for_AP_2");
        wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");


        Serial.println("Connecting Wifi...");
        if (wifiMulti.run() == WL_CONNECTED) {
            Serial.println("");
            Serial.println("WiFi connected");
            Serial.println("IP address: ");
            Serial.println(WiFi.localIP());
        }
    }



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
     *  step 4 : start camera web server
    ***********************************/
    startCameraServer();

}

void loop()
{
    delay(10000);
}
