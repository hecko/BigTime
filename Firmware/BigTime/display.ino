void displayNumberFor(int toDisplay, boolean displayColon, int display_for_ms) {

#define DIGIT_ON  HIGH
#define DIGIT_OFF  LOW

  //Now display this digit (7 segment)
  litNumber(toDisplay); //Turn on the right segments for this digit

  if (displayColon == true) {
    litNumber('.');
  }

  delay(display_for_ms);

  //Turn off all segments (see below)
  litNumber(13);

}
