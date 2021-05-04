/*
 * Counts the no. of seconds since PIN_START turns HIGH
 * until PIN_STOP turns HIGH.
 *
 * PIN_START is connected to BBB SYS_RESETn, while PIN_STOP
 * to BBB GPIO pin that signals system boot completion.
 *
 * The result is displayed on the OV7670 I2C camera module,
 * based on the ST703 image sensor.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <LCD_ST7032.h>

LCD_ST7032 lcd;

#define PIN_START     D2
#define PIN_STOP      D4

static void print_msg(const char *str) {
  lcd.setCursor(0, 0); /* LINE 0, ADDRESS 0 */
  lcd.print(str);
  for (size_t n = strlen(str) + 1; n <= 16; n++) {
    lcd.print(' ');
  }
}

static void print_time(int msec) {
  lcd.setCursor(1, 2); /* LINE 1, ADDRESS 2 */
  lcd.print(msec / 1000, DEC);
  lcd.print('.');
  lcd.print(msec % 1000, DEC);
  lcd.print(" sec ");
}

void setup()
{
  lcd.begin();
  lcd.setcontrast(30);

  Serial.begin(9600);

  /* Configure PINS input and enable the internal pull-down resistors */
  pinMode(PIN_START, INPUT_PULLDOWN);
  pinMode(PIN_STOP, INPUT_PULLDOWN);
}

void loop() {
  int start_time, elapsed;

  /* Waiting for BBB power (SYS_RESETn PIN turns HIGH) */
  if (digitalRead(PIN_START) == LOW) {
    print_msg("Please start BBB");
    Serial.println("Waiting for BBB to start");
    while ((digitalRead(PIN_START)) == LOW);
  }

  /* Waiting for BBB reset (SYS_RESETn PIN turns LOW when RST pressed) */
  print_msg("Please reset BBB");
  Serial.println("Waiting while SYS_RESETn is HIGH");
  while ((digitalRead(PIN_START)) == HIGH);
  Serial.println("Waiting while SYS_RESETn is LOW");
  while ((digitalRead(PIN_START)) == LOW);

  print_msg("Booting BBB...");
  Serial.println("Starting counter");
  start_time = millis();

  /* Waiting for BBB boot completion */
  while ((digitalRead(PIN_STOP)) == LOW) {
    elapsed = millis() - start_time;
    print_time(elapsed);
    delay(100);

    /* Check for a possible BBB reset */
    if (digitalRead(PIN_START) == LOW && elapsed > 500)
       break;
  }
}
