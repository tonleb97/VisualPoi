// SPDX-FileCopyrightText: 2019 Phillip Burgess for Adafruit Industries
//
// SPDX-License-Identifier: MIT

/*------------------------------------------------------------------------
  POV IR Supernova Poi sketch.  Uses the following Adafruit parts
  (X2 for two poi):

  - Teensy 3.2 (required - NOT compatible w/AVR-based boards)
  - 2200 mAh Lithium Ion Battery https://www.adafruit.com/product/1781
  - LiPoly Backpack https://www.adafruit.com/product/2124
  - 144 LED/m DotStar strip (#2328 or #2329)
    (ONE METER is enough for TWO poi)
  - Infrared Sensor: https://www.adafruit.com/product/157
  - Mini Remote Control: https://www.adafruit.com/product/389
    (only one remote is required for multiple poi)

  Needs Adafruit_DotStar library: github.com/adafruit/Adafruit_DotStar
  Also, uses version of IRremote library from the Teensyduino installer,
  the stock IRremote lib will NOT work here!

  This is based on the LED poi code (also included in the repository),
  but AVR-specific code has been stripped out for brevity, since these
  mega-poi pretty much require a Teensy 3.X.

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Phil Burgess / Paint Your Dragon for Adafruit Industries.
  MIT license, all text above must be included in any redistribution.
  See 'COPYING' file for additional notes.
  ------------------------------------------------------------------------*/

#include <Arduino.h>
#include <Adafruit_DotStar.h>
//#include <avr/power.h>
//#include <avr/sleep.h>
//#include <IRremote.h>
#include <SPI.h>
#include <bluefruit.h>


typedef uint16_t line_t;

// CONFIGURABLE STUFF ------------------------------------------------------

#include "graphics.h" // Graphics data is contained in this header file.
// It's generated using the 'convert.py' Python script.  Various image
// formats are supported, trading off color fidelity for PROGMEM space.
// Handles 1-, 4- and 8-bit-per-pixel palette-based images, plus 24-bit
// truecolor.  1- and 4-bit palettes can be altered in RAM while running
// to provide additional colors, but be mindful of peak & average current
// draw if you do that!  Power limiting is normally done in convert.py
// (keeps this code relatively small & fast).

// Ideally you use hardware SPI as it's much faster, though limited to
// specific pins.  If you really need to bitbang DotStar data & clock on
// different pins, optionally define those here:
#define LED_DATA_PIN  11
#define LED_CLOCK_PIN 9

// Empty and full thresholds (millivolts) used for battery level display:
#define BATT_MIN_MV 3350 // Some headroom over battery cutoff near 2.9V
#define BATT_MAX_MV 4000 // And little below fresh-charged battery near 4.1V

boolean autoCycle = false; // Set to true to cycle images by default
uint32_t CYCLE_TIME = 12; // Time, in seconds, between auto-cycle images

#define VBATPIN A6

// -------------------------------------------------------------------------

#if defined(LED_DATA_PIN) && defined(LED_CLOCK_PIN)
// Older DotStar LEDs use GBR order.  If colors are wrong, edit here.
Adafruit_DotStar strip = Adafruit_DotStar(NUM_LEDS,
  LED_DATA_PIN, LED_CLOCK_PIN, DOTSTAR_BGR);
#else
Adafruit_DotStar strip = Adafruit_DotStar(NUM_LEDS, DOTSTAR_BGR); 
#endif

//Battery voltage
float measuredvbat;

// Uart over BLE service
BLEUart bleuart;

uint32_t color = 0xFF0000;      // 'On' color (starts red)

// Function prototypes for packetparser.cpp
uint8_t readPacket (BLEUart *ble_uart, uint16_t timeout);
float   parsefloat (uint8_t *buffer);
void    printHex   (const uint8_t * data, const uint32_t numBytes);

// Packet buffer
extern uint8_t packetbuffer[];

//Button press (from BLE)
uint8_t button1;
uint8_t button2;

void     imageInit(void);
         //IRinterrupt(void);
uint16_t readVoltage(void);

void setup() {
  strip.begin(); // Allocate DotStar buffer, init SPI
  strip.clear(); // Make sure strip is clear
  strip.show();  // before measuring battery
  
  imageInit();   // Initialize pointers for default image

  Bluefruit.begin();
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values

  // Configure and start the BLE Uart service
  bleuart.begin();

  // Set up and start advertising
  startAdv();
  
  button1 = 0; //initialize button press to 0
  button2 = 0;
  
}

void startAdv(void)
{
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  
  // Include the BLE UART (AKA 'NUS') 128-bit UUID
  Bluefruit.Advertising.addService(bleuart);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();

  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds  
}

// GLOBAL STATE STUFF ------------------------------------------------------

uint32_t lastImageTime = 0L, // Time of last image change
         lastLineTime  = 0L;
uint8_t  imageNumber   = 0,  // Current image being displayed
         imageType,          // Image type: PALETTE[1,4,8] or TRUECOLOR
        *imagePalette,       // -> palette data in PROGMEM
        *imagePixels,        // -> pixel data in PROGMEM
         palette[16][3];     // RAM-based color table for 1- or 4-bit images
line_t   imageLines,         // Number of lines in active image
         imageLine;          // Current line number in image

const uint8_t PROGMEM brightness[] = { 15, 31, 63, 127, 255 };
uint8_t bLevel = sizeof(brightness) - 1;

// Microseconds per line for various speed settings
const uint16_t PROGMEM lineTable[] = { // 375 * 2^(n/3)
  1000000L /  375, // 375 lines/sec = slowest
  1000000L /  472,
  1000000L /  595,
  1000000L /  750, // 750 lines/sec = mid
  1000000L /  945,
  1000000L / 1191,
  1000000L / 1500  // 1500 lines/sec = fastest
};
uint8_t  lineIntervalIndex = 3;
uint16_t lineInterval      = 1000000L / 750;

void imageInit() { // Initialize global image state for current imageNumber
  imageType    = images[imageNumber].type;
  imageLines   = images[imageNumber].lines;
  imageLine    = 0;
  imagePalette = (uint8_t *)images[imageNumber].palette;
  imagePixels  = (uint8_t *)images[imageNumber].pixels;
  // 1- and 4-bit images have their color palette loaded into RAM both for
  // faster access and to allow dynamic color changing.  Not done w/8-bit
  // because that would require inordinate RAM (328P could handle it, but
  // I'd rather keep the RAM free for other features in the future).
  if(imageType == PALETTE1)      memcpy_P(palette, imagePalette,  2 * 3);
  else if(imageType == PALETTE4) memcpy_P(palette, imagePalette, 16 * 3);
  lastImageTime = millis(); // Save time of image init for next auto-cycle
}

void nextImage(void) {
  if(++imageNumber >= NUM_IMAGES) imageNumber = 0;
  imageInit();
}

void prevImage(void) {
  imageNumber = imageNumber ? imageNumber - 1 : NUM_IMAGES - 1;
  imageInit();
}

// MAIN LOOP ---------------------------------------------------------------

void loop() {
  uint32_t t = millis(); // Current time, milliseconds
  
  measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3600;  // Multiply by 3.6V, our reference voltage
  measuredvbat /= 1024; // convert to millivolts
  if(measuredvbat > BATT_MIN_MV){ //If battery is above threshold, then I'm willing to display colors

    if(autoCycle) {
      if((t - lastImageTime) >= (CYCLE_TIME * 1000L)) nextImage();
    // CPU clocks vary slightly; multiple poi won't stay in perfect sync.
    // Keep this in mind when using auto-cycle mode, you may want to cull
    // the image selection to avoid unintentional regrettable combinations.
    }

    // Transfer one scanline from pixel data to LED strip:
    
    uint8_t len = readPacket(&bleuart, 1);
    if (len > 0){ //If I've received a packet
      if (packetbuffer[1] == 'B') { //If packet type is button press
        button1 = packetbuffer[2] - '0';
        button2 = packetbuffer[3] - '0';
      }
    }
    
    switch(imageType) {

      case PALETTE1: { // 1-bit (2 color) palette-based image
        uint8_t  pixelNum = 0, byteNum, bitNum, pixels, idx,
                *ptr = (uint8_t *)&imagePixels[imageLine * NUM_LEDS / 8];
        for(byteNum = NUM_LEDS/8; byteNum--; ) { // Always padded to next byte
          pixels = *ptr++;                       // 8 pixels of data (pixel 0 = LSB)
          for(bitNum = 8; bitNum--; pixels >>= 1) {
            idx = pixels & 1; // Color table index for pixel (0 or 1)
            strip.setPixelColor(pixelNum++,
              palette[idx][0], palette[idx][1], palette[idx][2]);
          }
        }
        break;
      }

      case PALETTE4: { // 4-bit (16 color) palette-based image
        uint8_t  pixelNum, p1, p2,
                *ptr = (uint8_t *)&imagePixels[imageLine * NUM_LEDS / 2];
        for(pixelNum = 0; pixelNum < NUM_LEDS; ) {
          p2  = *ptr++;  // Data for two pixels...
          p1  = p2 >> 4; // Shift down 4 bits for first pixel
          p2 &= 0x0F;    // Mask out low 4 bits for second pixel
          strip.setPixelColor(pixelNum++,
            palette[p1][0], palette[p1][1], palette[p1][2]);
          strip.setPixelColor(pixelNum++,
            palette[p2][0], palette[p2][1], palette[p2][2]);
        }
        break;
      }

      case PALETTE8: { // 8-bit (256 color) PROGMEM-palette-based image
        uint16_t  o;
        uint8_t   pixelNum,
                *ptr = (uint8_t *)&imagePixels[imageLine * NUM_LEDS];
        for(pixelNum = 0; pixelNum < NUM_LEDS; pixelNum++) {
          o = *ptr++ * 3; // Offset into imagePalette
          strip.setPixelColor(pixelNum,
            imagePalette[o],
            imagePalette[o + 1],
            imagePalette[o + 2]);
        }
        break;
      }

      case TRUECOLOR: { // 24-bit ('truecolor') image (no palette)
        uint8_t  pixelNum, r, g, b,
                *ptr = (uint8_t *)&imagePixels[imageLine * NUM_LEDS * 3];
        for(pixelNum = 0; pixelNum < NUM_LEDS; pixelNum++) {
          r = *ptr++;
          g = *ptr++;
          b = *ptr++;
          strip.setPixelColor(pixelNum, r, g, b);
        }
        break;
      }
    }
    
    // if(!strip.getBrightness()) { // If strip is off...
    //       // Set brightness to last level
    //       strip.setBrightness(brightness[bLevel]);
    //       // and ignore button press (don't fall through)
    //       // effectively, first press is 'wake'
    // }
    if(++imageLine >= imageLines) imageLine = 0; // Next scanline, wrap around
    while(((t = micros()) - lastLineTime) < lineInterval) {
      if(button1 != 0) {
        if(!strip.getBrightness()) { // If strip is off...
          // Set brightness to last level
          strip.setBrightness(brightness[bLevel]);
          // and ignore button press (don't fall through)
          // effectively, first press is 'wake'
        } else {
          switch(button1) {
          case 1:
            switch(button2) {
              case 0:
                autoCycle = !autoCycle;
                break;
              case 1:
                if(bLevel < (sizeof(brightness) - 1))
                strip.setBrightness(brightness[++bLevel]); // Increases brightness
                break;
              case 2:
                if(bLevel)
                  strip.setBrightness(brightness[--bLevel]); // Decreases brightness
                break;
              case 3:
                nextImage();
                break;
              case 4:
                prevImage();
                break;
              case 5:
                imageNumber = 30;
                imageInit();
                break;
              case 6:
                imageNumber = 41;
                imageInit();
                break;
              case 7:
                imageNumber = 50;
                imageInit();
                break;
              case 8:
                imageNumber = 3;
                imageInit();
                break;
              case 9:
                imageNumber = 4;
                imageInit();
                break;
              case 10:
                imageNumber = 5;
                imageInit();
                break;
              case 11:
                imageNumber = 6;
                imageInit();
                break;
              case 12:
                imageNumber = 7;
                imageInit();
                break;
              case 13:
                imageNumber = 8;
                imageInit();
                break;
              case 14:
                imageNumber = 9;
                imageInit();
                break;
              case 15:
                imageNumber = 10;
                imageInit();
                break;
            }//end nested switch for button1 = 1
            break;
          case 2:
            switch(button2){
              case 0:
                imageNumber = 11;
                imageInit();
                break;
              case 1:
                imageNumber = 12;
                imageInit();
                break;
              case 2:
                imageNumber = 13;
                imageInit();
                break;
              case 3:
                imageNumber = 14;
                imageInit();
                break;
              case 4:
                imageNumber = 15;
                imageInit();
                break;
              case 5:
                imageNumber = 16;
                imageInit();
                break;
              case 6:
                imageNumber = 17;
                imageInit();
                break;
              case 7:
                imageNumber = 18;
                imageInit();
                break;
              case 8:
                imageNumber = 19;
                imageInit();
                break;
              case 9:
                imageNumber = 20;
                imageInit();
                break;
              case 10:
                imageNumber = 21;
                imageInit();
                break;
              case 11:
                imageNumber = 22;
                imageInit();
                break;
              case 12:
                imageNumber = 23;
                imageInit();
                break;
              case 13:
                imageNumber = 24;
                imageInit();
                break;
              case 14:
                imageNumber = 25;
                imageInit();
                break;
              case 15:
                imageNumber = 26;
                imageInit();
                break;
            }//end nested switch for button1 = 2
            break;
          }
        }
        button1 = 0; //reset button press to 0
        button2 = 0;
      }
    }
    strip.show(); // Refresh LEDs
  }//end battery threshold if statement
  lastLineTime = t;
}

//void IRinterrupt() {
 // if (irrecv.decode(&results)) {
 //   Serial.println(results.value, HEX);
 //   irrecv.resume(); // Receive the next value
 // }
//}
