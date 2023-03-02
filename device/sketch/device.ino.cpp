#include <Arduino.h>
#line 1 "/home/runner/work/doorhandlelock/doorhandlelock/device/device.ino"
#include "device.hpp"
using namespace pimoroni;

// Bootsel, fingerprint sensor and servo globals
__Bootsel BOOTSEL;
SerialPIO mySerial(0, 1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
Servo servo;

// Wi-Fi globals
WiFiServer server(9999);
WiFiClient client;
bool AP_MODE = true;
uint8_t packet[255];
int wifi_status = WIFI_NOT_CONNECTED;
int timeout = 30;
int timeout_safety = 8;
String mac;
String ssid;

// Display globals
ST7789 display(320, 240, ROTATE_0, false, get_spi_pins(BG_SPI_FRONT));
PicoGraphics_PenRGB565 graphics(display.width, display.height, nullptr);
uint8_t last_pen[3];
Pen BLACK;
Pen WHITE;
Pen PRIMARY;

// Unlock servo function
#line 30 "/home/runner/work/doorhandlelock/doorhandlelock/device/device.ino"
void unlock(void);
#line 45 "/home/runner/work/doorhandlelock/doorhandlelock/device/device.ino"
void setup(void);
#line 116 "/home/runner/work/doorhandlelock/doorhandlelock/device/device.ino"
void loop(void);
#line 278 "/home/runner/work/doorhandlelock/doorhandlelock/device/device.ino"
uint8_t fingerprint_enroll(void);
#line 423 "/home/runner/work/doorhandlelock/doorhandlelock/device/device.ino"
uint8_t fingerprint_get_id(void);
#line 493 "/home/runner/work/doorhandlelock/doorhandlelock/device/device.ino"
void draw_blank_screen(void);
#line 499 "/home/runner/work/doorhandlelock/doorhandlelock/device/device.ino"
void draw_navbar(void);
#line 30 "/home/runner/work/doorhandlelock/doorhandlelock/device/device.ino"
void unlock(void) {
  // Note that this is a blocking call, the bootsell button will not be checked until the servo has completed its movement!
  for (int pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
    // in steps of 1 degree
    servo.write(pos);              // tell servo to go to position in variable 'pos'
    delay(1);                       // waits 1ms for the servo to reach the position
  }    
  delay(1000);
  for (int pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
    servo.write(pos);              // tell servo to go to position in variable 'pos'
    delay(1);                       // waits 1ms for the servo to reach the position
  }
}

// Setup 
void setup(void)
{
  // Misc setup
  pinMode(PICO_LED, OUTPUT);
  memset(last_pen, 0, sizeof(last_pen));
  memset(packet, 0, sizeof(packet));

  // Display setup
  graphics.clear();
  BLACK = graphics.create_pen(0, 0, 0);
  WHITE = graphics.create_pen(255, 255, 255);
  PRIMARY = graphics.create_pen(176, 196, 222);


  display.set_backlight(255);
  display.update(&graphics);
  graphics.set_pen(BLACK);
  graphics.rectangle(Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT));
  graphics.set_pen(PRIMARY);
  graphics.text("Door Lock", Point(10, 10), true, 2);
  display.update(&graphics);
  
  // Wi-Fi setup
  AP_MODE = true;
  WiFi.mode(WIFI_STA);

  // Get mac address of device
  mac = WiFi.macAddress();
  ssid = "Door Lock - " + mac.substring(0, 6);

  WiFi.softAP(ssid); // leave password empty for open AP
  server.begin();

  // Attach the servo motor
  servo.attach(28);

  // Set the baudrate for the sensor serial port
  finger.begin(57600);
  LED_SETUP;

  if (finger.verifyPassword()) {
    // Found the sensor
    LED_SETUP;
  } else {
    // Failed to find sensor
    // Pico LED blink is our error indicator in this case (since we couldn't find the sensor)
    while (true) {
      gpio_put(PICO_LED, 1);
      sleep_ms(100);
      gpio_put(PICO_LED, 0);
      sleep_ms(100);
    }
  }

  finger.getParameters();

  finger.getTemplateCount();
  if (finger.templateCount == 0) {
    // No fingerprints stored
    while (true)
      while (finger.getImage() != FINGERPRINT_NOFINGER) {
        LED_SETUP;
      }
      while(! fingerprint_enroll() );
  } else {
    // Fingerprints stored
    LED_SUCCESS;
  }
}

// Main loop
void loop(void)
{
  if (client) {
    if (AP_MODE)
    {
      unlock();
      draw_blank_screen();
      graphics.set_pen(WHITE);
      graphics.text("Connecting to", Point(10, 10), true, 2);
      display.update(&graphics);

      // AP mode will receive the home networks 
      // SSID and password in the form SSID?PASSWORD
      client.readBytes(packet, 255);
      int question = 0;
      int semicolon = 0;
      for (int i = 0; i < 255; i++) {
        if (packet[i] == '?') {
          packet[i] = 0;
          question = i;
        }
        else if (packet[i] == ';') {
          packet[i] = 0;
          semicolon = i; 
          break;
        }
      }
      char *home_ssid = (char *)packet;
      char *home_password = (char *)packet + question + 1;
            
      graphics.text(home_ssid, Point(10, 50), true, 2);
      graphics.text(home_password, Point(10, 70), true, 2);
      display.update(&graphics);

      // connect to home network
      AP_MODE = false;
      server.close();
      WiFi.softAPdisconnect(true);
      WiFi.begin("SKYVUY5A", "oops hehe");   
      
      timeout = timeout > 30 ? timeout : 30; // 30 * 500 = 15 seconds, reasonable time

      while (WiFi.status() != WL_CONNECTED and timeout > 0) {
        // temp blocking loop - move to second core
        // otherwise if this hangs forever, the
        // rest of the program will not run
        delay(500); 
        LED_SETUP;
        timeout--;
        if (WiFi.status() == WL_CONNECT_FAILED) {
          break;
        }
      }

      if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(); // cancel connection attempt, if any
        // in the case of multiple retries, that are caused by
        // the connection simply being slow, we will increase the timeout attempts
        if (timeout == 0) {
          timeout = 30 + timeout_safety;
          timeout_safety += 8;
        }
        // turn AP back on
        AP_MODE = true;
        WiFi.softAP(ssid); // leave password empty for open AP
        server.begin();
        LED_ERROR;
        delay(500);
        draw_blank_screen();
        graphics.set_pen(WHITE);
        graphics.text("Failed to connect, please make sure your details are correct. Rejoin the Door Lock xx:xx network and try sending them again.", Point(10, 10), true, 2);
        display.update(&graphics);
        return;
      }
      // get my ip address
      IPAddress ip = WiFi.localIP();
            
      draw_blank_screen();
      graphics.set_pen(WHITE);
      graphics.text("Connected with IP address", Point(10, 10), true, 2);
      graphics.text(ip.toString().c_str(), Point(10, 70), true, 2);
      display.update(&graphics);
      
      server.begin();
      LED_SUCCESS;
      delay(500);
    }
    else {
      size_t read = client.readBytes(packet, 5);
      if (read == 0) {
        //unlock();
      }
      else if (read != 5) {
        // unknown command
      }
      else {
        if (memcmp(packet, "UNLCK", 5) == 0) {
          unlock();
        }
        else if (memcmp(packet, "IMAGE", 5) == 0) {
          while (client.connected()) {
            if (client.available()) {
              size_t read = client.readBytes(packet, 255);
              int index = 0;
              while (read >= 3) {
                if (last_pen[0] != packet[index] || last_pen[1] != packet[index + 1] || last_pen[2] != packet[index + 2]) {
                  last_pen[0] = packet[index];
                  last_pen[1] = packet[index + 1];
                  last_pen[2] = packet[index + 2];
                  graphics.set_pen(graphics.create_pen(packet[index], packet[index + 1], packet[index + 2]));
                }
                int x = packet[index + 3] + packet[index + 4];
                int y = packet[index + 5];
                if (x > 320 || y > 240) {
                  continue; // not sure what to do here but continue for now
                }
                graphics.pixel(Point(x, y));
                index += 6;
                read -= 6;
              }
            }
            display.update(&graphics);
          }
        }
      }
    }
  }
  else {
    client = server.available();
  }

  int p = 0;
  if ( get_bootsel_button() ) {
    if (finger.templateCount == 0) {
      // No fingerprints stored, so enroll a new one
      while(! fingerprint_enroll());
    }
    else {
      // We need confirmation from an existing fingerprint before being able to enroll a new one
      if (fingerprint_get_id() > 0) {
        LED_SUCCESS;
        while(! fingerprint_enroll() );
      }
      else {
        //LED_OFF;
      }
    }
  }
  else {
    p = fingerprint_get_id();
    if (p == -1) {
      LED_OFF(LED_RED);
      LED_OFF(LED_BLUE);
      LED_OFF(LED_PURPLE);
    }
    else if (p > 0) {
      unlock();
    }
  }
}

// Fingerprint helper function definitions
uint8_t fingerprint_enroll(void) {
  int p = -1;
  LED_SETUP;
  while(p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch(p) {
    case FINGERPRINT_OK:
      // Image taken
      break;
    case FINGERPRINT_NOFINGER:
      // Waiting for finger
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      // Communication error
      break;
    case FINGERPRINT_IMAGEFAIL:
      // Imaging error
      break;
    default:
      // Unknown error 
      break;
    }
  }

  p = finger.image2Tz(1);
  switch(p) {
    case FINGERPRINT_OK:
      // Image converted
      break;
    case FINGERPRINT_IMAGEMESS:
      // Image too messy
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      // Communication error
      return p;
    case FINGERPRINT_FEATUREFAIL:
      // Could not find fingerprint features
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      // Could not find fingerprint features
      return p;
    default:
      // Unknown error 
      return p;
  }
  
  // Get user to remove finger
  LED_OFF(LED_PURPLE);
  delay(1000);

  // Get user to place finger again
  LED_ON(LED_BLUE);
  p = 0;

  while(p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }

  p = -1;

  while(p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch(p) {
    case FINGERPRINT_OK:
      // Image taken
      break;
    case FINGERPRINT_NOFINGER:
      // Waiting for finger
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      // Communication error
      break;
    case FINGERPRINT_IMAGEFAIL:
      // Imaging error
      break;
    default:
      // Unknown error 
      break;
    }
  }

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      // Image converted
      break;
    case FINGERPRINT_IMAGEMESS:
      // Image too messy
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      // Communication error
      return p;
    case FINGERPRINT_FEATUREFAIL:
      // Could not find fingerprint features
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      // Could not find fingerprint features
      return p;
    default:
      // Unknown error 
      return p;
  }

  // Create fingerprint model
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    // Prints matched
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    // Communication error
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    // Fingerprints did not match
    return p;
  } else {
    // Unknown error
    return p;
  }

  // TODO: Do something better with ID
  int id = finger.getTemplateCount() + 1;
  if (id > 127) {
    LED_FATAL;
    // TODO: This is not good, enable deletion of fingerprints
  }
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    // Stored
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    // Communication error
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    // Could not store in that location
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    // Error writing to flash !uh oh!
    return p;
  } else {
    // Unknown error 
    return p;
  }

  LED_SUCCESS;
  return true;
}

uint8_t fingerprint_get_id(void) {
  uint8_t p = finger.getImage();
  switch (p) {
    case FINGERPRINT_OK:
      // Image taken
      break;
    case FINGERPRINT_NOFINGER:
      // Waiting for finger
      return 0;
    case FINGERPRINT_PACKETRECIEVEERR:
      // Communication error
      return 0;
    case FINGERPRINT_IMAGEFAIL:
      // Imaging error
      return 0;
    default:
      // Unknown error 
      return 0;
  }

  p = finger.image2Tz();
  switch (p) {
    case FINGERPRINT_OK:
      // Image converted
      break;
    case FINGERPRINT_IMAGEMESS:
      // Image too messy
      return 0;
    case FINGERPRINT_PACKETRECIEVEERR:
      // Communication error
      return 0;
    case FINGERPRINT_FEATUREFAIL:
      // Could not find fingerprint features
      return 0;
    case FINGERPRINT_INVALIDIMAGE:
      // Could not find fingerprint features
      return 0;
    default:
      // Unknown error 
      return 0;
  }

  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    // Found a match!
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    // Communication error
    return 0;
  } else if (p == FINGERPRINT_NOTFOUND) {
    // Failed to find match
    return 0;
  } else {
    // Unknown error 
    return 0;
  }

  // found a match!
  int id = finger.fingerID;
  int confidence = finger.confidence;

  if (confidence > CONFIDENCE_THRESHOLD) {
    LED_SUCCESS;
    return id;
  } else {
    LED_ERROR;
    return -1;
  }
}

// Display helper function defintions
void draw_blank_screen(void) { // I think this can be made better
  graphics.set_pen(BLACK);
  graphics.rectangle(Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT));
  display.update(&graphics);
}

void draw_navbar(void) {
  graphics.set_pen(PRIMARY);
  graphics.rectangle(Rect(0, 0, DISPLAY_WIDTH, int(DISPLAY_HEIGHT / 8)));
  graphics.set_pen(BLACK);
  graphics.text("Door lock", Point(5, 5), 50, 3);
  graphics.set_pen(WHITE);
  graphics.rectangle(Rect(120, 5, 3, 19));
  graphics.set_pen(BLACK);
  std::string message;
  switch(wifi_status)
  {
    case WIFI_CHIP_ERROR:
      message = "ERROR";
      break;
    case WIFI_FAILED:
      message = "FAILED";
      break;
    case WIFI_CONNECTED:
      message = "ipaddres";
      break;
    case WIFI_CONNECTING:
      message = "CONNECTING";
      break;
    case WIFI_NOT_CONNECTED:
      message = "NOT CONNECTED";
      break;
    default:
      message = "FATAL";
    }
    graphics.text(message, Point(130, 8), 200, 2);
    display.update(&graphics);
}

