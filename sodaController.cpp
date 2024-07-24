/* 
 * This will be a single event driven executable which:
 * Handles requests to print barcodes using the munbyn printer 
 * Handles the soda machine bypass switch
 * Tests barcodes and controls the soda machine
 * 
 * Property of Couch Potato & Co LLC 
 * All rights reserved.
 */

/*************************************** 
 * Pin Out                             *
 * GPIO 12  LED1 (RED)                 *
 * GPIO 13  LED2 (GREEN)               *
 * GPIO 18  SWITCH IN                  *
 * GPIO 19  PRINT BUTTON IN            *
 * GPIO 17	EARLY OFF BUTTON IN        * 
 * GPIO 4   TO RELAY                   *
 * 5 V      TO RELAY                   *
 * 3.3 V    TO SWITCH/BUTTONS          *
 ***************************************/
 
/***************************************
 * Issues and notes:                   *
 * 1. Implementation of fsm might be a *
 *    bit overkill.                    *
 * 2. gpio 17 is not currently         *
 *    implemented on the hardware side *
 **************************************/

#include <iostream>     // for getting the keyboard input from the barcode scanner using cin
#include <time.h>       // get time as long from os
#include <pigpio.h>     // rpi gpio controller library
#include <thread>       // multithreading
#include <atomic>       // atomic operations for the threads
#include <string>       // for storing barcodes
#include <unistd.h>     // to sleep()
#include <fstream>      // for creating barcode file
#include <stdlib.h>     // for sys calls

// barcode library
#include <ZXing/BarcodeFormat.h>
#include <ZXing/BitMatrix.h>
#include <ZXing/CharacterSet.h>
#include <ZXing/MultiFormatWriter.h>
#include <ZXing/ImageView.h>

// lightweight image writing library
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "sbt_image_write.h"

using namespace ZXing;      // laziness

// How this program works:
// Thread 1 reads from a barcode scanner treated as a keyboard
// Thread 2 is an event loop which acts on barcodes and handles gpio input

const long int barcodeNull = -1;

void getBarcodes(std::atomic<bool>&, std::atomic<long int>&);
void sodaOn();
void sodaOff();
void init();

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
    long int now = time(NULL);                      // holds the now, feed to rest soda machine obj
    std::atomic<bool> stopIt(false);                // stop flag
    std::atomic<long int> theBarcode(barcodeNull);  // allow threads to work with the barcode
    long int barcodeLocal = barcodeNull;            // for handoff from thread

    // the thread
    std::thread inputThread(getBarcodes, std::ref(stopIt), std::ref(theBarcode));
	
    // event loop
    while(true)
    {
        now = time(NULL);

        // if we find that the print receipt button has been pressed, we shall print a barcode
        
        
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

void printBarcode()
{
	const int size = 206;       // 2 inches in pixels
    MultiFormatWriter writer(ZXing::BarcodeFormat::Code128);
    BitMatrix matrix = writer.encode(std::to_string(time(NULL)), size, size);
    auto bitmap =  ToMatrix<uint8_t>(matrix);
    stbi_write_png("barcode.png", bitmap.width(), bitmap.height(), 1, bitmap.data(), 0);
    system("lp -d ITPP130 ./barcode.png");
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

// new code for refactor goes here
enum class IOPin {relay = 4, redLed = 12, greenLed = 13, mainSwitch = 18, printButton = 19};		

// interface for soda machine states
class SodaState 
{
	virtual ~SodaState() {}
	virtual void update(SodaMachine &soda
	achine) {}
	virtual void handleInput(SodaMachine &sodaMachine) {}
	
	void handleBaseInput()
	{
		if(gpioRead(BUTTON) == PI_HIGH)
        {
            printBarcode();
        }
	}
	
public:
	static OnState onState;
	static OffState offState;
};

class OffState : public SodaState
{
	SodaState() : {}
	virtual void update(SodaMachine &soda
	achine) {}
	virtual void handleInput(SodaMachine &sodaMachine)
	
};

class OnState : public SodaState
{
	SodaState() : {}
	virtual void update(SodaMachine &soda
	achine) {}
	virtual void handleInput(SodaMachine &sodaMachine)
};

class SodaMachine
{
public:
	virtual void SodaMachine() { state = &SodaState::offState; return; }

	virtual void handleInput()
	{
		state->handleInput(*this);
	}
	
	virtual void update()
	{
	
		state->update(*this);
	}￼
	
	// maybe add the button enum here
	
private:
	SodaState *state;
	
	// time constants
	const int sodaTimeLimit = 60;                   // 60 seconds
    const int totalValidBarcodeTime = 60 *60;       // 90 minutes in seconds
    long int now = time(NULL);                      // holds the now
    const long int sodaOnTimeSentinel = -1;         // for off mode
    long int sodaOnTime = sodaOnTimeSentinel;       // time stamp of when the soda got turned on
    long int shutOffCountStart = 0;                 // for the quitting sequence
};

