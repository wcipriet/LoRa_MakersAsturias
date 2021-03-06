/*******************************************************************************
 * The Things Network - ABP TTGO LoRa32 (based on Adafruit example)
 * 
 * Example of using an TTGO LORA32 V1 & a DHT22 sensor connected to PIN 23
 * Single-channel TTN gateway used & installed in another TTGO LoRa32:
 * https://github.com/things4u/ESP-1ch-Gateway-v5.0
 * 
 * This uses ABP (Activation by Personalization), where session keys for
 * communication would be assigned/generated by TTN and hard-coded on the device.
 * 
 * Learn Guide: https://learn.adafruit.com/the-things-network-for-feather
 * 
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 * Copyright (c) 2018 Terry Moore, MCCI
 * Copyright (c) 2018 Brent Rubell, Adafruit Industries
 * Copyright (c) 2019 Alex Corvis (@AlexCorvis84)
 * 
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * Heltec Wifi LoRa 32, TTGO LoRa and TTGO LoRa32 V1:
ESP32          LoRa (SPI)      Display (I2C)  LED
-----------    ----------      -------------  ------------------
GPIO5  SCK     SCK
GPIO27 MOSI    MOSI
GPIO19 MISO    MISO
GPIO18 SS      NSS
GPIO14         RST
GPIO26         DIO0
GPIO33         DIO1
GPIO32         DIO2
GPIO15                         SCL
GPIO4                          SDA
GPIO16                         RST
GPIO25                                        Heltec, TTGO LoRa32
GPIO2                                         TTGO LoRa
*
* The LMIC library used is the MCCI-catena one: 
* https://github.com/mcci-catena/arduino-lmic
*
*
* You need to setup the library config file (lmic_project_config.h), found under path
* ..Arduino\libraries\MCCI_LoRaWAN_LMIC_library\project_config
*
* Content of lmic_project_config.h should be:
*
#define CFG_eu868 1
#define CFG_sx1276_radio 1
#define US_PER_OSTICK_EXPONENT 4                     // BELOW ARE OPTIONALS. NOT NEEDED
#define US_PER_OSTICK (1 << US_PER_OSTICK_EXPONENT)
#define OSTICKS_PER_SEC (1000000 / US_PER_OSTICK)
#define LMIC_DEBUG_LEVEL 0
#define LMIC_FAILURE_TO Serial
#define USE_IDEETRON_AES
*
 *******************************************************************************/
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <U8x8lib.h>
#include <CayenneLPP.h>

// Include the DHT22 Sensor Library
#include "DHT.h"

// DHT digital pin and sensor type
#define DHTPIN 23
#define DHTTYPE DHT22
#define LEDPIN 2 // Programmable Blue LED BUILTIN

//OLED Pin I2C Definitions
#define SCL 15
#define SDA 4
#define RST 16

U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8 (SCL, SDA, RST);

int channel = 0;

CayenneLPP lpp(51);

/* //
// For normal use, we require that you edit the sketch to replace FILLMEIN
// with values assigned by the TTN console. However, for regression tests,
// we want to be able to compile these scripts. The regression tests define
// COMPILE_REGRESSION_TEST, and in that case we define FILLMEIN to a non-
// working but innocuous value.

#ifdef COMPILE_REGRESSION_TEST
# define FILLMEIN 0
#else
# warning "You must replace the values marked FILLMEIN with real values from the TTN control panel!"
# define FILLMEIN (#dont edit this, edit the lines that use FILLMEIN)
#endif */

// LoRaWAN NwkSKey, network session key
static const PROGMEM u1_t NWKSKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// LoRaWAN AppSKey, application session key
static const u1_t PROGMEM APPSKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// LoRaWAN end-device address (DevAddr)
// See http://thethingsnetwork.org/wiki/AddressSpace
// The library converts the address to network byte order as needed.
//#ifndef COMPILE_REGRESSION_TEST
static const u4_t DEVADDR = 0x00000000;

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in arduino-lmic/project_config/lmic_project_config.h,
// otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 30;

// Pin mapping for for TTGO LoRa32 V1
const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 14,
    .dio = {26, 33, 32},
//    .rxtx_rx_active = 0,
//    .rssi_cal = 8,              // LBT cal for the Adafruit Feather M0 LoRa, in dB
//    .spi_freq = 8000000,
};

// init. DHT
DHT dht(DHTPIN, DHTTYPE);

void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    u8x8.setCursor (0, 3);
    u8x8.printf ("%0x", LMIC.devaddr);
    u8x8.setCursor (0, 5);
    u8x8.printf ("HORA %lu", os_getTime ());
    
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            u8x8.drawString (0, 7, "EV_SCAN_TIMEOUT");
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            u8x8.drawString (0, 7, "EV_BEACON_FOUND");
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            u8x8.drawString (0, 7, "EV_BEACON_MISSED");
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            u8x8.drawString (0, 7, "EV_BEACON_TRACKED");
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            u8x8.drawString (0, 7, "EV_JOINING");
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            u8x8.drawString (0, 7, "EV_JOINED");
            break;
        
        /* This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        case EV_RFU1:
            Serial.println(F("EV_RFU1"));
            break;*/
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            u8x8.drawString (0, 7, "EV_JOIN_FAILED");
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            u8x8.drawString (0, 7, "EV_REJOIN_FAILED");
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            u8x8.drawString (0, 7, "EV_TXCOMPLETE");
            
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("RECEIVED ACK"));
              u8x8.drawString (0, 7, "RECEIVED ACK ");
            
            if (LMIC.dataLen) {
              u8x8.clear();
              Serial.println(F("Received "));
              Serial.println(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));

              u8x8.drawString (0, 6, "RX ");
              u8x8.setCursor (4, 6);
              u8x8.printf ("%i bytes", LMIC.dataLen);
              u8x8.setCursor (0, 7);
              u8x8.printf ("RSSI %d SNR %.1d", LMIC.rssi, LMIC.snr);
            }
            
            // Schedule next transmission
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            break;
            
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            u8x8.drawString (0, 7, "EV_LOST_TSYNC");
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            u8x8.drawString (0, 7, "EV_RESET");
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            u8x8.drawString (0, 7, "EV_RX_COMPLETE");
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            u8x8.drawString (0, 7, "EV_LINK_DEAD");
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            u8x8.drawString (0, 7, "EV_LINK_ALIVE");
            break;
        /*
        || This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        || case EV_SCAN_FOUND:
        ||    Serial.println(F("EV_SCAN_FOUND"));
        ||    break;
        */
        case EV_TXSTART:
            Serial.println(F("EV_TXSTART"));
            u8x8.drawString (0, 7, "EV_TX_START  ");
            break;
        default:
            Serial.print(F("Unknown event: "));
            Serial.println((unsigned) ev);
            u8x8.setCursor(0, 7);
            u8x8.printf("UNKNOWN EVENT %d", ev);
            break;
    }
}

void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
        u8x8.drawString (0, 7, "OP_TXRXPEND, not sent");
        
    } else {
        // read the temperature from the DHT22
        float temperature = dht.readTemperature();
        Serial.print("Temperature: "); Serial.print(temperature);
        Serial.println(" *C");      
        // read the humidity from the DHT22
        float rHumidity = dht.readHumidity(); Serial.print("Humidity: ");
        Serial.print(rHumidity);
        Serial.println(" %RH ");
        
		lpp.reset();
		lpp.addTemperature(1, temperature);
		lpp.addRelativeHumidity(2, rHumidity);

        // prepare upstream data transmission at the next possible time.
        // transmit on port 1 (the first parameter); you can use any value from 1 to 223 (others are reserved).
        // don't request an ack (the last parameter, if not zero, requests an ack from the network).
        // Remember, acks consume a lot of network resources; don't ask for an ack unless you really need it.
        digitalWrite(LEDPIN, HIGH);
        delay(1000);
        LMIC_setTxData2(1, lpp.getBuffer(), lpp.getSize(), 0);
        digitalWrite(LEDPIN,LOW);
        
        Serial.println (F ("Packet queued"));
        u8x8.drawString (0, 7, "PACKET QUEUED");        
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {

    Serial.begin(115200);
    Serial.println(".....");
    Serial.println(F("Starting"));
    dht.begin();
    pinMode(LEDPIN, OUTPUT);
    u8x8.begin ();
    u8x8.setFont (u8x8_font_5x7_f);
    u8x8.drawString (0, 1, "TTGO LoRa32 V1");

    // LMIC init
	Serial.println("Inicializado Radio LORA");
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // Set static session parameters. Instead of dynamically establishing a session
    // by joining the network, precomputed session parameters are be provided.
    #ifdef PROGMEM
    // On AVR, these values are stored in flash and only copied to RAM
    // once. Copy them to a temporary buffer here, LMIC_setSession will
    // copy them into a buffer of its own again.
    uint8_t appskey[sizeof(APPSKEY)];
    uint8_t nwkskey[sizeof(NWKSKEY)];
    memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
    memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
    LMIC_setSession (0x13, DEVADDR, nwkskey, appskey);
    #else
    // If not running an AVR with PROGMEM, just use the arrays directly
    LMIC_setSession (0x13, DEVADDR, NWKSKEY, APPSKEY);
    #endif

    #if defined(CFG_eu868)
    // We'll only enable Channel 0 (868.1Mhz) since we're transmitting on a single-channel
    LMIC_setupChannel (0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);
    LMIC_setupChannel (1, 868100000, DR_RANGE_MAP (DR_SF12, DR_SF7), BAND_CENTI);      // g-band
    LMIC_setupChannel (2, 868100000, DR_RANGE_MAP (DR_SF12, DR_SF7), BAND_CENTI);      // g-band
    LMIC_setupChannel (3, 868100000, DR_RANGE_MAP (DR_SF12, DR_SF7), BAND_CENTI);      // g-band
    LMIC_setupChannel (4, 868100000, DR_RANGE_MAP (DR_SF12, DR_SF7), BAND_CENTI);      // g-band
    LMIC_setupChannel (5, 868100000, DR_RANGE_MAP (DR_SF12, DR_SF7), BAND_CENTI);      // g-band
    LMIC_setupChannel (6, 868100000, DR_RANGE_MAP (DR_SF12, DR_SF7), BAND_CENTI);      // g-band
    LMIC_setupChannel (7, 868100000, DR_RANGE_MAP (DR_SF12, DR_SF7), BAND_CENTI);      // g-band
    LMIC_setupChannel (8, 868100000, DR_RANGE_MAP (DR_SF12, DR_SF7), BAND_CENTI);      // g2-band
    #endif
    
    // We'll disable all 9 channels used by TTN
	for(int i=0; i<9; i++) { // For EU; for US use i<71
        if(i != channel) {
            LMIC_disableChannel(i);
        }
    }
    
    LMIC_enableChannel(channel);
    LMIC.txChnl=channel;

    // Disable link check validation
    LMIC_setLinkCheckMode(0);

    // TTN uses SF9 for its RX2 window.
	// https://github.com/mcci-catena/arduino-lmic#downlink-datarate
    LMIC.dn2Dr = DR_SF9;

    // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
    LMIC_setDrTxpow(DR_SF7,14);

    // Start job
    do_send(&sendjob);
}

void loop() {
	
  os_runloop_once();  

}
