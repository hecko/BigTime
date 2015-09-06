/*
 7-17-2011
 Spark Fun Electronics 2011
 Nathan Seidle
 hacked by Marcel Hecko 2013, 2015

 This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).

 This is the firmware for BigTime, the wrist watch kit clone (!). It is based on an ATmega328P running with internal
 8MHz clock and external 32kHz crystal for keeping the time (aka RTC). The code and system have been tweaked
 to lower the power consumption of the ATmeg328P as much as possible. The watch currently uses about
 1.2uA in idle (non-display) mode and about 13mA when displaying the time. With a 200mAh regular
 CR2032 battery you should get 2-3 years of use!

 To compile and load this code onto your watch, select "Arduino Pro or Pro Mini 3.3V/8MHz w/ ATmega328" from
 the Boards menu.

 If you're looking to save power in your own project, be sure to read section 9.10 of the ATmega328
 datasheet to turn off all the bits of hardware you don't need.

 BigTime requires the Pro 8MHz bootloader with a few modifications:
 Internal 8MHz
 Clock div 8 cleared
 Brown out detect disabled
 BOOTRST set
 BOOSZ = 1024
 This is to save power and open up the XTAL pins for use with a 38.786kHz external osc.

 So the fuse bits I get using AVR studio:
 HIGH 0xDA
 LOW 0xE2
 Extended 0x07

 test avrdude programmer and flash fuses
 make sure you are not powering the device with the programmer and the other way around!
 make sure the battery is in an charged
 avrdude -c usbtiny -p m328p
 avrdude -c usbtiny -p m328p -U lfuse:w:0xe2:m -U hfuse:w:0xda:m -U efuse:w:0x07:m

 3,600 seconds in an hour
 1 time check per hour, 2 seconds at 13mA
 3,598 seconds @ 1.2uA
 (3598 / 3600) * 0.0012mA + (2 / 3600) * 13mA = 0.0084mA used per hour

 200mAh / 0.0084mA = 23,809hr = 992 days = 2.7 years

 We can't use the standard Arduino delay() or delaymicrosecond() because we shut down timer0 to save power

 We turn off Brown out detect because it alone uses ~16uA.

 */

#include <avr/sleep.h> // Needed for sleep_mode
#include <avr/power.h> // Needed for powering down perihperals such as the ADC/TWI and Timers

#include "display.h"

// Set this variable to change how long the time is shown on the watch face. In milliseconds so 1677 = 1.677 seconds
int show_the_time = false;

// initial time after programming
long seconds = 30;
int minutes = 15;
int hours = 1;

int display_brightness = 15000; //A larger number makes the display more dim. This is set correctly below.

int segA = 9;   // Display pin 14
int segB = A1;  // Display pin 16
int segC = 7;   // Display pin 13
int segD = 6;   // Display pin 3
int segE = 5;   // Display pin 5
int segF = 10;  // Display pin 11
int segG = A0;  // Display pin 15
int colons = 8; // Display pin 4

int oneLed = A2; // pin 4

int topButton = 2;    // top button
int bottomButton = 3; // bottom button

// The very important 32.686kHz interrupt handler
SIGNAL(TIMER2_OVF_vect) {
  // We sleep for 8 seconds to save more power
  seconds += 8; 
  // Update the minute and hour variables
  minutes += seconds / 60;
  seconds %= 60;
  hours += minutes / 60;
  minutes %= 60;

  while (hours > 12) hours -= 12;
}

// The interrupt occurs when you push the top button
SIGNAL(INT0_vect) {
  show_the_time = true;
}

void setup() {
  //To reduce power, setup all pins as inputs with no pullups
  for (int x = 1 ; x < 18 ; x++) {
    pinMode(x, INPUT);
    digitalWrite(x, LOW);
  }

  pinMode(oneLed, OUTPUT);

  pinMode(topButton, INPUT); //This is the main top button, tied to INT0
  digitalWrite(topButton, HIGH); //Enable internal pull up on button

  pinMode(bottomButton, INPUT); //This is the main bottom button, tied to INT0
  digitalWrite(bottomButton, HIGH); //Enable internal pull up on button

  //These pins are used to control the display
  pinMode(segA,   OUTPUT);
  pinMode(segB,   OUTPUT);
  pinMode(segC,   OUTPUT);
  pinMode(segD,   OUTPUT);
  pinMode(segE,   OUTPUT);
  pinMode(segF,   OUTPUT);
  pinMode(segG,   OUTPUT);
  pinMode(colons, OUTPUT);

  //Power down various bits of hardware to lower power usage
  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  sleep_enable();

  //Shut off ADC, TWI, SPI, Timer0, Timer1

  ADCSRA &= ~(1 << ADEN); //Disable ADC
  ACSR = (1 << ACD); //Disable the analog comparator
  DIDR0 = 0x3F; //Disable digital input buffers on all ADC0-ADC5 pins
  DIDR1 = (1 << AIN1D) | (1 << AIN0D); //Disable digital input buffer on AIN1/0

  power_twi_disable();
  power_spi_disable();
  power_usart0_disable(); // Needed for serial.print
  // power_timer0_disable(); // Needed for delay and millis()
  power_timer1_disable();
  // power_timer2_disable(); // Needed for asynchronous 32kHz operation

  // Setup TIMER2
  TCCR2A = 0x00;
  // Set CLK/1024 or overflow interrupt every 8s
  TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20);
  // Enable asynchronous operation
  ASSR = (1 << AS2);
  // Enable the timer 2 interrupt
  TIMSK2 = (1 << TOIE2); 
  // Setup external INT0 interrupt
  // Interrupt on falling edge
  EICRA = (1 << ISC01);
  // Enable INT0 interrupt
  EIMSK = (1 << INT0);

  // System clock futzing
  // CLKPR = (1<<CLKPCE); //Enable clock writing
  // CLKPR = (1<<CLKPS3); //Divide the system clock by 256

  // Show the current time on startup
  showTime();
  //Enable global interrupts
  sei();
}

void loop() {
  digitalWrite(oneLed, HIGH); // oneLed on
  delayMicroseconds(1000);
  digitalWrite(oneLed, LOW); // oneLed off

  sleep_mode(); //Stop everything and go to sleep. Wake up if the Timer2 buffer overflows or if you hit the button

  if (show_the_time == true) {

    // Debounce
    delayMicroseconds(100000);
    showTime(); //Show the current time

    // If you are holding the button after the time is shown, then you must be wanting to to adjust the time
    if (digitalRead(topButton) == LOW) {
      setTime();
    }

    show_the_time = false; //Reset the show variable
  }
}

void showTime() {
  boolean buttonPreviouslyHit = false;
  int min_10 = minutes / 10;
  int min_01 = minutes - (min_10 * 10);
  long startTime;

  startTime = millis();
  while ( (millis() - startTime) < 700) {
    displayNumberFor(hours, true, 20);
    delayMicroseconds(8000);
  }
  delayMicroseconds(500000);

  startTime = millis();
  while ( (millis() - startTime) < 500) {
    displayNumberFor(min_10, false, 20);
    delayMicroseconds(8000);
  }
  delayMicroseconds(400000);

  startTime = millis();
  while ( (millis() - startTime) < 500) {
    displayNumberFor(min_01, false, 20);
    delayMicroseconds(8000);
  }
}

// This routine occurs when you hold the top button down
void setTime(void) {
  int exitMode = 0;

  while (exitMode == 0) {

    //We don't want the interrupt changing values at the same time we are!
    //cli();
    
    while (hours > 12) hours -= 12;

    int min_10 = minutes / 10;
    int min_01 = minutes - (min_10 * 10);

    long startTime = millis();
    while ( (millis() - startTime) < 700) {
      displayNumberFor(hours, true, 20);
      delayMicroseconds(5000);
    }
    delayMicroseconds(600000);

    startTime = millis();
    while ( (millis() - startTime) < 400) {
      displayNumberFor(min_10, false, 20);
      delayMicroseconds(5000);
    }
    delayMicroseconds(400000);

    startTime = millis();
    while ( (millis() - startTime) < 400) {
      displayNumberFor(min_01, false, 20);
      delayMicroseconds(5000);
    }

    if (digitalRead(topButton) == LOW) {
      exitMode = 1;
      // sei(); //Resume interrupts
    }

    if (digitalRead(bottomButton) == LOW) {
      // we have pressed the button - reset the seconds to the end of the minute (i.e. incrementing time by one minute)
      seconds = 60;
      // also calculate other values
      minutes += seconds / 60;
      seconds %= 60;
      hours += minutes / 60;
      minutes %= 60;
    }

    delayMicroseconds(600000);
  }

}



//Given a number, turns on those segments
//If number == 13, then turn off all segments
void litNumber(int numberToDisplay) {

#define SEGMENT_ON  LOW
#define SEGMENT_OFF HIGH

  /*
  Segments
   -  A
   F / / B
   -  G
   E / / C
   -  D
   */

  switch (numberToDisplay) {

    case 0:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      digitalWrite(segD, SEGMENT_ON);
      digitalWrite(segE, SEGMENT_ON);
      digitalWrite(segF, SEGMENT_ON);
      break;

    case 1:
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      break;

    case 2:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segD, SEGMENT_ON);
      digitalWrite(segE, SEGMENT_ON);
      digitalWrite(segG, SEGMENT_ON);
      break;

    case 3:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      digitalWrite(segD, SEGMENT_ON);
      digitalWrite(segG, SEGMENT_ON);
      break;

    case 4:
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      digitalWrite(segF, SEGMENT_ON);
      digitalWrite(segG, SEGMENT_ON);
      break;

    case 5:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      digitalWrite(segD, SEGMENT_ON);
      digitalWrite(segF, SEGMENT_ON);
      digitalWrite(segG, SEGMENT_ON);
      break;

    case 6:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      digitalWrite(segD, SEGMENT_ON);
      digitalWrite(segE, SEGMENT_ON);
      digitalWrite(segF, SEGMENT_ON);
      digitalWrite(segG, SEGMENT_ON);
      break;

    case 7:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      break;

    case 8:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      digitalWrite(segD, SEGMENT_ON);
      digitalWrite(segE, SEGMENT_ON);
      digitalWrite(segF, SEGMENT_ON);
      digitalWrite(segG, SEGMENT_ON);
      break;

    case 9:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      digitalWrite(segD, SEGMENT_ON);
      digitalWrite(segF, SEGMENT_ON);
      digitalWrite(segG, SEGMENT_ON);
      break;

    case 10:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segD, SEGMENT_ON);
      digitalWrite(segF, SEGMENT_ON);
      digitalWrite(segG, SEGMENT_ON);
      break;

    case 11:
      digitalWrite(segB, SEGMENT_ON);
      digitalWrite(segC, SEGMENT_ON);
      digitalWrite(segE, SEGMENT_ON);
      digitalWrite(segF, SEGMENT_ON);
      break;

    case 12:
      digitalWrite(segA, SEGMENT_ON);
      digitalWrite(segG, SEGMENT_ON);
      digitalWrite(segD, SEGMENT_ON);
      break;

    // all segments off
    case 13:
      digitalWrite(segA, SEGMENT_OFF);
      digitalWrite(segB, SEGMENT_OFF);
      digitalWrite(segC, SEGMENT_OFF);
      digitalWrite(segD, SEGMENT_OFF);
      digitalWrite(segE, SEGMENT_OFF);
      digitalWrite(segF, SEGMENT_OFF);
      digitalWrite(segG, SEGMENT_OFF);
      digitalWrite(colons, SEGMENT_OFF);
      break;

    case '.':
      digitalWrite(colons, SEGMENT_ON);
      break;
  }
}

