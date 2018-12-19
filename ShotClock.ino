
// Basket Ball Shot Clock

#define DEBUG_PRINT
#ifdef DEBUG_PRINT
#define PRINT_INIT(X) Serial.begin((X))
#define PRINT(X) Serial.print(X)
#define PRINTLN(X) Serial.println(X)
#else 
#define PRINT_INIT(X)
#define PRINT(X)
#define PRINTLN(X)
#endif

#define NP7S_PIN  (12)
#define NP7S_MAX  (86)

#define BUTTON_11 11
#define BUTTON_10 10
#define BUTTON_09 9
#define BUTTON_08 8

#define PIEZO_PIN 5

#include <Wire.h>

#include <RTClib.h>
#include <Adafruit_GFX.h>
#include "Adafruit_NeoPixel.h"
#include "Adafruit_LEDBackpack.h"

Adafruit_NeoPixel np7s = Adafruit_NeoPixel(
                              NP7S_MAX, 
                              NP7S_PIN, 
                              NEO_GRB + NEO_KHZ800
                              );
Adafruit_7segment led7s = Adafruit_7segment();
RTC_DS1307 rtc = RTC_DS1307();

/*
 * In the 7-segment display, each segment is made of a 3-neopixel strip
 *                       
 *       b           There are 7-segments, a to g.
 *     * * *         
 *    *     *        Segment        NP Offset
 *  a *     * c         a              0
 *    *  g  *           b              3
 *     * * *            c              6
 *    *     *           d              9
 *  f *     * d         e              12
 *    *  e  *           f              15
 *     * * *            g              18 
 */
class SevenSegment {
public:
  SevenSegment(Adafruit_NeoPixel &neopixel, uint16_t start_pixel=0) :
    np( neopixel ),
    start( start_pixel )
  {
    max_pixel = start + 21;
    red = 60, green=60, blue=60;
  };

  void Off() { 
    for (int idx=start; idx<max_pixel; idx++)
      np.setPixelColor(idx, 0, 0, 0, 0);
      np.show();
  };

  void SetColor(uint8_t r, uint8_t g, uint8_t b) {
    red = r, green = g, blue = b;
  }

  void Display(int digit) {
    int seg_np;
    uint8_t dr, dg, db;

    // any digit over 9 results in '-'
    digit = (digit > 9) ? 10 : digit;
    
    for (int seg=0; seg<7; seg++) {
      seg_np = start+(seg*3);

      if (digits[digit][seg])
        dr = red, dg = green, db = blue;
      else
        dr = dg = db = 0;

      display_segment(seg_np, dr, dg, db);
    }
    np.show();
  };

private:
  inline void display_segment(int first, uint8_t r, uint8_t g, uint8_t b) {
    for (int idx=first; idx<first+3; idx++)
        np.setPixelColor(idx, r, g, b, 0);
  }

  Adafruit_NeoPixel np;
  uint16_t start;
  uint16_t max_pixel;
  uint8_t red, green, blue;

  const uint8_t digits[11][7] = {
    { 1, 1, 1, 1, 1, 1, 0 },  // 0
    { 0, 0, 1, 1, 0, 0, 0 },  // 1
    { 0, 1, 1, 0, 1, 1, 1 },  // 2
    { 0, 1, 1, 1, 1, 0, 1 },  // 3
    { 1, 0, 1, 1, 0, 0, 1 },  // 4
    { 1, 1, 0, 1, 1, 0 ,1 },  // 5
    { 1, 1, 0, 1, 1, 1, 1 },  // 6
    { 0, 1, 1, 1, 0, 0, 0 },  // 7
    { 1, 1, 1, 1, 1, 1, 1 },  // 8
    { 1, 1, 1, 1, 1, 0, 1 },  // 9
    { 0, 0, 0, 0, 0, 0, 1 }   // - (10)
  };
  const int a=0, b=1, c=2, d=3, e=4, f=5, g=6;
};


class Colon {
public:
  Colon(Adafruit_NeoPixel &neopixel, uint16_t start_pixel=0) :
    np( neopixel ),
    start( start_pixel )
  {
    last = start + 1;
    red = 60, green=60, blue=60;
  };

  void SetColor(uint8_t r, uint8_t g, uint8_t b) {
    red = r, green = g, blue = b;
  }

  void Display(bool on) {
    if (on) {
      np.setPixelColor(start, red, green, blue, 0);
      np.setPixelColor(last, red, green, blue, 0);
    } else {
      np.setPixelColor(start, 0, 0, 0, 0);
      np.setPixelColor(last, 0, 0, 0, 0);
    }
    np.show();
  }
  
  Adafruit_NeoPixel np;
  uint16_t start;
  uint16_t last;
  uint8_t red, green, blue;
};

enum PiezoSound {loud=129, quiet=10, off=0};
static void Piezo(int vol)
{
  analogWrite(PIEZO_PIN, vol);
}

static bool button_press(int button, int time_ms=0) {
  int lo_count = 0;
  long unsigned start = millis();
  long unsigned finish;

  Piezo(quiet);
  while(digitalRead(button) == LOW);

  // debounce button release.
  while(lo_count < 300) {
    if (digitalRead(button) == HIGH)
      lo_count++;
    else
      lo_count = 0;
  }

  Piezo(off);
  // was the button held down at least time_ms?
  return (time_ms > (millis() - start)) ? false : true; 
}

enum shot_clock_timeout {shot_clock_14 = 14, shot_clock_24 = 24};
enum mode { time_clock, shot_clock, timeout };

/* Globals */

/* Neopixel 7-Segment display digits. Each Neopixel 7-Seg is 21-neopixels,
   so the offset between digits is 21.*/
SevenSegment d[4] = {
  SevenSegment(np7s),
  SevenSegment(np7s, 21),  
  SevenSegment(np7s, 44), /* +2 for the 2-Neopixel colon */
  SevenSegment(np7s, 65)
};

Colon colon = Colon(np7s, 42);

DateTime tn;      /* Time now - read from RTC */
DateTime tt;      /* Time then - for comparison */

bool blinkColon;

/* The two digits to the left (hours on a clock */
static void display_left(int value, bool on=true)
{
  if (on) {
    int tens = (value/10);
    int units = value % 10;
  
    d[0].Display(tens);
    d[1].Display(units);

    led7s.writeDigitNum(0, tens, false);
    led7s.writeDigitNum(1, units, false);
    led7s.writeDisplay();
  }
  else
  {
    d[0].Off();
    d[1].Off();
    led7s.writeDigitNum(0, 18, false);
    led7s.writeDigitNum(1, 18, false);
    led7s.writeDisplay();
  }
}

/* The two digits to the right (seconds on a clock */
static void display_right(int value, bool on=true)
{
  if (on) {
    int tens = (value/10);
    int units = value % 10;
  
    d[2].Display(tens);
    d[3].Display(units);

    led7s.writeDigitNum(3, tens, false);
    led7s.writeDigitNum(4, units, false);
    led7s.writeDisplay();
  }
  else
  {
    d[2].Off();
    d[3].Off();
    led7s.writeDigitNum(3, 17, false);
    led7s.writeDigitNum(4, 17, false);
    led7s.writeDisplay();
  }
}

static void clear_displays(void) {
  for (int idx=0; idx<4; idx++) d[idx].Off();
  colon.Display(false);
  led7s.clear();
  led7s.writeDisplay();
}

enum shotclock {shotclock_off, shotclock_24=24, shotclock_14=14, };
enum timeouter {timeout_off, timeout_60=60};
enum user_action {none, sc_reset, sc_start_stop, to_reset, to_start_stop };
enum user_mode {clock_mode, basketball_mode};
unsigned long sc_countdown;
unsigned long to_countdown;
unsigned long last;

user_action action;
user_mode mode;
shotclock sc;
timeouter to;
bool sc_running;
bool to_running;
int sc_last;
int to_last;

unsigned long countdown_to_clock;
bool waiting_for_clock;


void setup() {
  PRINT_INIT( 9600 );
  PRINTLN("Setup started.");

  if (!rtc.begin()) {
    PRINTLN("Couldn't initialise the RTC.");
    while(1);
  } else {
    /* Set the initial date time to compile time of code. */
    if (!rtc.isrunning()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    PRINTLN("Real time clock initialised...");
    tt = tn = rtc.now();
  }

  np7s.begin();
  PRINTLN("Neopixels initialised...");
  
  led7s.begin(0x70);
  led7s.clear();
  led7s.writeDisplay();
  PRINTLN("7-Seg LED initialised...");

  pinMode(BUTTON_11, INPUT_PULLUP);
  pinMode(BUTTON_10, INPUT_PULLUP);
  pinMode(BUTTON_09, INPUT_PULLUP);
  pinMode(BUTTON_08, INPUT_PULLUP);
  PRINTLN("Input buttons initialised... ");

  for (int idx=0; idx < NP7S_MAX; idx++) {
    np7s.setPixelColor(idx, 0, 0, 0, 0);
  }
  np7s.show();

  int r = 64, g=128, b=192;
  for (int idx=0; idx < NP7S_MAX; idx++) {
    np7s.setPixelColor(idx, r, g, b, 0);
    r = (r + 77) % 255;
    g = (g + 77) % 255;
    b = (b + 77) % 255;
    delay(20);
    np7s.show();
  }  

  for(int i=0; i<4; i++) d[i].SetColor(0, 128, 128);
  colon.SetColor(0, 128, 128);

  /* Start is clock mode */
  mode = clock_mode;
  action = none;
  sc = shotclock_off;
  to = timeout_off;
  sc_running = false;
  to_running = false;
  to_countdown = 0;
  sc_countdown = 0;

  countdown_to_clock = 0;
  waiting_for_clock = false;
  
  display_left(tn.hour());
  display_right(tn.minute());

  blinkColon = false;
  colon.Display(blinkColon);

  Piezo(off);

  last = millis();
  PRINTLN("Setup completed.");
}

void loop() {

  tn = rtc.now();

  if (digitalRead(BUTTON_08) == LOW) {
    button_press(BUTTON_08);
    action = sc_reset;
  } else if (digitalRead(BUTTON_09) == LOW) {
    button_press(BUTTON_09);
    action = sc_start_stop;
  } else if (digitalRead(BUTTON_10) == LOW) {
    button_press(BUTTON_10);
    action = to_reset;
  } else if (digitalRead(BUTTON_11) == LOW) {
    button_press(BUTTON_11);
    action = to_start_stop;
  }
  
  if ( action == sc_reset || action == to_reset  ) {
    if (mode == clock_mode) 
    { 
      clear_displays();
      colon.SetColor(0, 255, 0);
      colon.Display(true);
      led7s.drawColon(true);
      led7s.writeDisplay();
      mode = basketball_mode;
    }
    if (action == sc_reset && !sc_running)
    {
      switch (sc) {
        case (shotclock_off):
          sc = shotclock_24;
          sc_countdown = 24 * 1000;
          sc_last = 24;
          break;
        case (shotclock_24):
          sc = shotclock_14;
          sc_countdown = 14 * 1000;
          sc_last = 14;
          break;
        case (shotclock_14):
          sc = shotclock_off;
          sc_countdown = 0;
          sc_last = 0;
          break;
      }
      if (sc) {
        sc_running = false;
        waiting_for_clock = false;
        d[0].SetColor(255, 0, 0);
        d[1].SetColor(255, 0, 0);
        display_left(sc);
      } else {
        display_left(0, false);
      }
    } else if (action == to_reset && !to_running) {
       to = (to) ? timeout_off : timeout_60;
       to_last = to;

      if (to) {
        to_running = false;
        waiting_for_clock = false;

        to_countdown = 60000;
        d[2].SetColor(0, 0, 255);
        d[3].SetColor(0, 0, 255);
        display_right(to);
      } else {
        to_countdown = 0;
        display_right(0, false);
      }
    }
  } else if (action == sc_start_stop && sc_countdown) {
    sc_running = !sc_running;
  } else if (action == to_start_stop && to_countdown) {
    to_running = !to_running;
  }
  action = none;

  if (mode == basketball_mode) 
  {
    unsigned long elapsed = millis() - last;

    if (sc_running) {

      if (sc_countdown <= elapsed)
        sc_countdown = 0;
      else
        sc_countdown -= elapsed;

      if (sc_countdown > 0) {
        int sc_now;
        sc_now = sc_countdown/1000;
        if (sc_now != sc_last) {
          display_left(sc_now);
          sc_last = sc_now;
        }
      }
      else {
        sc_running = false;
        sc_countdown = 0;
        sc = shotclock_off;
      }
    }

    if (to_running) {
      if (to_countdown <= elapsed)
        to_countdown = 0;
      else
        to_countdown -= elapsed;

      if (to_countdown > 0) {
        int to_now;
        to_now = to_countdown/1000;
        if (to_now != to_last) {
          display_right(to_now);
          to_last = to_now;
        }
      }
      else {
        to_running = false;
        to_countdown = 0;
        to = timeout_off;
      }
    }

    if (sc == shotclock_off && to == timeout_off && !waiting_for_clock)
    {
      waiting_for_clock = true;
      countdown_to_clock = 5000;
      PRINTLN("Waiting for clock");
    }

    if (waiting_for_clock) {
      if (countdown_to_clock > 0) {
        countdown_to_clock -= (millis() - last);
      } else {
        for(int i=0; i<4; i++) d[i].SetColor(0, 128, 128);
        colon.SetColor(0, 128, 128);
        
        display_left(tn.hour());
        display_right(tn.minute());

        blinkColon = false;
        colon.Display(blinkColon);

        mode = clock_mode;
      }
    }
  }

  if (mode == clock_mode)
  {
    if (tn.second() != tt.second())
    {
      PRINT(tn.hour());
      PRINT(":"); 
      PRINT(tn.minute());
      PRINT(":"); 
      PRINTLN(tn.second());
  
      blinkColon = !blinkColon;
      colon.Display(blinkColon);
      led7s.drawColon(blinkColon);
      led7s.writeDisplay();
    }
    
    if (tt.minute() != tn.minute()) {
      display_left(tn.hour());
      display_right(tn.minute());
    }
  }

  tt = tn;
  last = millis();

#if 0
  
  if (digitalRead(BLACK_BUTTON) == LOW) {
    PRINTLN("Black Button");
    button_press(BLACK_BUTTON);

    shotclock_run = false;

    switch(shotclock) {
      case off:
        shotclock = sec_24;
        countdown = sec_24 * 1000;
        d1.SetColor(255, 0, 0);
        d2.SetColor(255, 0, 0);
        display_seconds(countdown/1000);
        break;
      case sec_24:
        countdown = shotclock = sec_14;
        countdown = sec_14 * 1000;
        d1.SetColor(255, 0, 0);
        d2.SetColor(255, 0, 0);
        display_seconds(countdown/1000);
        break;
      case sec_14:
        shotclock = off;
        countdown = 999;
        d1.Off();
        d2.Off();
        break;
    }
  }
  
  if (digitalRead(RED_BUTTON) == LOW) {
    PRINTLN("Red Button");
    button_press(RED_BUTTON);

    if (shotclock) {
      shotclock_run = (shotclock_run) ? false : true;
      last = millis();
    }
    
//    analogWrite(PIZO_BUZZER, 0);
//    delay(300);
//    analogWrite(PIZO_BUZZER, 255);
  }

  if (shotclock_run) {
    countdown -= (millis() - last);
    last = millis();
    display_seconds(countdown/1000);
    PRINTLN(countdown);
  }
  delay(200);
  if (countdown <= 0)
  {
      shotclock = off;
      countdown = 999;
      shotclock_run = false;
      
      d1.Off();
      d2.Off();
      delay(500);
      for (int loop=0; loop<3; loop++) {
        display_seconds(0);
        digitalWrite(PIZO_BUZZER, HIGH);
        delay(500);
        digitalWrite(PIZO_BUZZER, LOW);
        d1.Off();
        d2.Off();
        delay(500);
      }
  }
    
#endif

#if 0
  for(int col=0; col<6; col++) {
    d1.SetColor(colors[col][0], colors[col][1], colors[col][2]);
    d2.SetColor(colors[col][0], colors[col][1], colors[col][2]);

    d1.Off();
    d2.Off();
    delay(1000);
  
    for (int count=60; count>=0; count--) {
      display_seconds(count);
      delay(1000);
    }
  }
#endif

}
