/* 
 * This will be a single event driven executable which:
 * Handles requests to print barcodes using the munbyn printer 
 * Handles the soda machine bypass switch
 * Tests barcodes and controls the soda machine
 * 
 * Property of Couch Potato & Co LLC 
 * All rights reserved.
 */

/*
 * Note: Refactoring this using the state pattern would probably make it
 * a hell of a lot cleaner.
 */

/*************************************** 
 * Pin Out                             *
 * GPIO 12  LED1 (RED)                 *
 * GPIO 13  LED2 (GREEN)               *
 * GPIO 18  SWITCH IN                  *
 * GPIO 19  PRINT BUTTON IN            *
 * GPIO 17	 EARLY OFF BUTTON IN        *
 * GPIO 4   TO RELAY                   *
 * 5 V      TO RELAY                   *
 * 3.3 V    TO SWITCH/BUTTONS          *
 ***************************************/

#include <iostream>     // mostly debug w/ cin/cout, and for getting the keyboard input from the barcode scanner
#include <time.h>       // get time as long from os
#include <pigpio.h>     // rpi gpio controller library
#include <thread>       // multithreading
#include <atomic>       // atomic operations for the threads
#include <string>       // stores the barcodes inside our thread
#include <unistd.h>     // to sleep
#include <fstream>      // for creating barcode file
#include <stdlib.h>     // for sys calls

// for barcode lib
#include <ZXing/BarcodeFormat.h>
#include <ZXing/BitMatrix.h>
#include <ZXing/CharacterSet.h>
#include <ZXing/MultiFormatWriter.h>
#include <ZXing/ImageView.h>

// lightweight image writing lib
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "sbt_image_write.h"

using namespace ZXing;      // laziness

// handle the barcode scanner in a separate thread

enum GPIOId {relay = 4, redLed = 12, greenLed = 13, masterSwitch = 18, printButton = 19};

const long int barcodeNull = -1;

void getBarcodes(std::atomic<bool>&, std::atomic<long int>&);
void sodaOn();
void sodaOff();
void init();
void printBarcode();

int main()
{
    // initialization step
    if(gpioInitialise() < 0)
    {
        return 1;
    }

    // init all pins. See above pinout comment for purpose of each pin.
    else
    {
        init();
    }

    // variables
    const int sodaTimeLimit = 60;                   // 60 seconds
    const int totalValidBarcodeTime = 60 *60;       // 90 minutes in seconds
    long int now = time(NULL);                      // holds the now
    const long int sodaOnTimeSentinel = -1;         // for off mode
    long int sodaOnTime = sodaOnTimeSentinel;       // time stamp of when the soda got turned on
    long int shutOffCountStart = 0;                 // for the quitting sequence
    long int receiptQueue = 0;                      // probably gonna be removed
    const long int shutoffCountMax = 5;             // 5 seconds
    std::atomic<bool> stopIt(false);                // stop flag
    std::atomic<long int> theBarcode(barcodeNull);  // allow threads to work with the barcode
    long int barcodeLocal = barcodeNull;            // for handoff from thread

    // the thread
    std::thread inputThread(getBarcodes, std::ref(stopIt), std::ref(theBarcode));
	

    // There is a case to be made for a state machine here, but maybe that's overkill for something this small

    // event loop
    while(true)
    {
        now = time(NULL);

        // shutoff button
        if(gpioRead(GPIOPin::printButton) == PI_HIGH && gpioRead(SWITCH) == PI_HIGH)
        {
            if(shutOffCountStart == sodaOnTimeSentinel)
            {
                shutOffCountStart = now;
            }
            else if((now - shutOffCountStart) >= shutoffCountMax)
            {
                sodaOff();
                gpioTerminate();
                return 1;
            }

        }
        else
        {
            shutOffCountStart = sodaOnTimeSentinel;
        }

        // if we find that the print receipt button has been pressed, we shall print a barcode
        if(gpioRead(GPIOPin::printButton) == PI_HIGH)
        {
            printBarcode();
        }
        
        
        // if we find that the switch is turned on
        if(gpioRead(SWITCH) == PI_HIGH)
        {
            // turn on soda machine
            sodaOn();
        }

        // add check which makes sure the soda machine wasn't already on from a customer
        else if(sodaOnTime == sodaOnTimeSentinel)
        {
            sodaOff();
        }
        
        // if the machine is on and not reached the time limit
        if(sodaOnTime != sodaOnTimeSentinel && now - sodaOnTime <= sodaTimeLimit)
        {
            // keep machine on
            sodaOn();

		 // unless we get an early off button signal
		 if(gpioRead(EARLY_OFF_BUTTON) == PI_HIGH)
		 {
		 	sodaOff();
		 }

        }

        // if it has reached the time limit
        else if(sodaOnTime != sodaOnTimeSentinel && now - sodaOnTime > sodaTimeLimit)
        {
            sodaOff();
            sodaOnTime = sodaOnTimeSentinel;
        }

        // we always handle codes scanned from other thread
        // refreshing the timer
        // this prevents snags
        if(theBarcode != barcodeNull)
        {
            std::cout << "Barcode received in main\n";
            if(barcodeLocal != barcodeNull && (now - theBarcode > totalValidBarcodeTime || now - theBarcode < 0))        // throw away trash if we have a gucci code
            {
                theBarcode = barcodeNull;
            }
            else
            {
                barcodeLocal = theBarcode;
                theBarcode = barcodeNull;
            }
        }
   
        // process received codes
        if(barcodeLocal != barcodeNull && now - barcodeLocal <= totalValidBarcodeTime)
        {
            sodaOn();
            sodaOnTime = now;
            barcodeLocal = barcodeNull;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // frees memory/threads and so on
    stopIt = true;
    gpioTerminate();
    return 0;
}

void printBarcode()
{
	const int size = 206;       // 2 inches in pixels
    MultiFormatWriter writer(ZXing::BarcodeFormat::Code128);
    BitMatrix matrix = writer.encode(std::to_string(time(NULL)), size, size);
    auto bitmap =  ToMatrix<uint8_t>(matrix);
    stbi_write_png("barcode.png", bitmap.width(), bitmap.height(), 1, bitmap.data(), 0);
    system("lp -d ITPP130 ./barcode.png");
    return;
}

// gets barcodes as keyboard input and then "returns" the barcode as a long int 
void getBarcodes(std::atomic<bool>& stopIt, std::atomic<long int>& theBarCode)
{
    std::string input;
    while(!stopIt)
    {
        theBarCode = barcodeNull;
        std::getline(std::cin, input);
        try
        {
            theBarCode = std::stol(input);
        }
        
        catch(std::invalid_argument) {theBarCode = barcodeNull; input = "";}
        catch(std::out_of_range) {theBarCode = barcodeNull; input = "";}

        while(theBarCode != barcodeNull)        // let other thread do stuff until its ready to handle stuff
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

void sodaOn()
{
    gpioWrite(RED_LED, PI_LOW);
    gpioWrite(GREEN_LED, PI_HIGH);
    gpioWrite(RELAY, PI_HIGH);
}

void sodaOff()
{
    gpioWrite(RED_LED, PI_HIGH);
    gpioWrite(GREEN_LED, PI_LOW);
    gpioWrite(RELAY, PI_LOW);
}

void init()
{
        gpioSetMode(12, PI_OUTPUT);
        gpioSetMode(13, PI_OUTPUT);
        gpioSetMode(17, PI_INPUT);
        gpioSetMode(18, PI_INPUT);
        gpioSetMode(19, PI_INPUT);
        gpioSetMode(4, PI_OUTPUT);
        gpioWrite(12, 1);           // red
        gpioWrite(13, 0);           // green
        gpioWrite(4, 0);            // relay
}
