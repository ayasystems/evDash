//#include <SD.h>
#include <FS.h>
#include <analogWrite.h>
//#include <WiFi.h>
//#include <WiFiClient.h>
#include <sys/time.h>
#include "config.h"
#include "BoardInterface.h"
#include "Board320_240.h"
#include <ArduinoJson.h>
#include "SIM800L.h"

RTC_DATA_ATTR unsigned int bootCount = 0;

/**
   Init board
*/
void Board320_240::initBoard() {

  // Set button pins for input
  pinMode(pinButtonMiddle, INPUT);
  pinMode(pinButtonLeft, INPUT);
  pinMode(pinButtonRight, INPUT);

  // Init time library
  struct timeval tv;
  tv.tv_sec = 1589011873;
  settimeofday(&tv, NULL);
  struct tm now;
  getLocalTime(&now, 0);
  liveData->params.chargingStartTime = liveData->params.currentTime = mktime(&now);

  // Deep sleep boot counter
  ++bootCount;
  syslog->print("Boot count: ");
  syslog->println(bootCount);
  syslog->print("SleepMode: ");
  syslog->println(liveData->settings.sleepModeLevel);
}

/**
   After setup device
*/
void Board320_240::afterSetup() {

  // Check if board was sleeping
  bool afterSetup = false;

  if (liveData->settings.sleepModeLevel == 2 && !skipAdapterScan()) {
    // Init comm device
    afterSetup = true;
    BoardInterface::afterSetup();
    // Wake or continue with sleeping
    afterSleep();
  }

  // Init display
  syslog->println("Init TFT display");
  initDisplay();

  // Show test data on right button during boot device
  liveData->params.displayScreen = liveData->settings.defaultScreen;
  if (digitalRead(pinButtonRight) == LOW) {
    loadTestData();
  }

  // Wifi
  // Starting Wifi after BLE prevents reboot loop
  if (liveData->settings.wifiEnabled == 1) {
    /*syslog->print("memReport(): MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM bytes free. ");
        syslog->println(heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
        syslog->println("WiFi init...");
      WiFi.enableSTA(true);
      WiFi.mode(WIFI_STA);
      WiFi.begin(liveData->settings.wifiSsid, liveData->settings.wifiPassword);
        syslog->println("WiFi init completed...");*/
  }

  // Init GPS
  if (liveData->settings.gpsHwSerialPort <= 2) {
    syslog->print("GPS initialization on hwUart: ");
    syslog->println(liveData->settings.gpsHwSerialPort);
    if (liveData->settings.gpsHwSerialPort == 0) {
      syslog->println("hwUart0 collision with serial console! Disabling serial console");
      syslog->flush();
      syslog->end();
    }
    gpsHwUart = new HardwareSerial(liveData->settings.gpsHwSerialPort);
    gpsHwUart->begin(9600);

    // Enable static hold
    // https://www.u-blox.com/sites/default/files/products/documents/u-blox8-M8_ReceiverDescrProtSpec_%28UBX-13003221%29.pdf
    // https://github.com/noerw/mobile-sensebox/blob/master/esp8266-gps/gps.h
    // uBlox NEO-7M can't persist settings, so we update them on runtime to get a higher update rate
    // commands extracted via u-center (https://www.youtube.com/watch?v=iWd0gCOYsdo)
    uint8_t ubloxconfig[216] = {
      // enable GPGGA & RMC sentences (only these are evaluated by TinyGPS++)
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x05, 0x38, //GGA
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x04, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x09, 0x54, //RMC
      // disable all other NMEA sentences to save bandwith
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x2B, //GLL
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x32, //GSA
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x39, //GSV
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x47, //VTG
      // setup SBAS search to EGNOS only
      0xB5, 0x62, 0x06, 0x16, 0x08, 0x00, 0x01, 0x03, 0x03, 0x00, 0x51, 0x08, 0x00, 0x00, 0x84, 0x15,
      // set NAV5 model to automotive, static hold on 0.5m/s, 3m
      0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27,
      0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x32, 0x3C, 0x00, 0x00,
      0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x85, 0x78,
      // 150ms update interval
      0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0x96, 0x00, 0x01, 0x00, 0x01, 0x00, 0xAC, 0x3E,
      // 100ms update interval (needs higher baudrate, whose config doesnt work?)
      //0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0x64, 0x00, 0x01, 0x00, 0x01, 0x00, 0x7A, 0x12,
      // uart to baud 115200 and nmea only  -> wont work?! TODO :^(
      //0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00, 0x00, 0xC2,
      //0x01, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBF, 0x78,
      // save changes
      0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x03, 0x1D, 0xAB
    };

    gpsHwUart->write(ubloxconfig, sizeof(ubloxconfig));
  }

  // SD card
  if (liveData->settings.sdcardEnabled == 1) {
    if (sdcardMount() && liveData->settings.sdcardAutstartLog == 1) {
      syslog->println("Toggle recording on SD card");
      sdcardToggleRecording();
    }
  }

  // Init SIM800L
  if (liveData->settings.gprsHwSerialPort <= 2) {
    sim800lSetup();
  }

  // Init comm device
  if (!afterSetup) {
    BoardInterface::afterSetup();
  }
}

/**
   Go to Sleep for TIME_TO_SLEEP seconds
*/
void Board320_240::goToSleep() {

  //Sleep MCP2515
  commInterface->disconnectDevice();

  //Sleep SIM800L
  if (bootCount == 1 && liveData->settings.gprsHwSerialPort <= 2 && !liveData->params.sim800l_enabled) {
    sim800lSetup();
  }
  if (liveData->params.sim800l_enabled) {
    if (sim800l->isConnectedGPRS()) {
      bool disconnected = sim800l->disconnectGPRS();
      for (uint8_t i = 0; i < 5 && !disconnected; i++) {
        delay(1000);
        disconnected = sim800l->disconnectGPRS();
      }
    }

    if (sim800l->getPowerMode() == NORMAL) {
      sim800l->setPowerMode(SLEEP);
      delay(1000);
    }
    sim800l->enterSleepMode();
  }

  //Sleep ESP32
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_37, 0); // pinButtonLeft

  if(liveData->settings.sleepModeLevel == 2) {
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * 1000000ULL);
    syslog->println("Going to sleep for " + String(TIME_TO_SLEEP) + " seconds!");
  } else {
    syslog->println("Going to sleep for ever! (shutdown)");
  }

  syslog->flush();
  delay(1000);

  esp_deep_sleep_start();
}

/*
  Wake up board from sleep
  Iterate thru commands and determine if car is charging or ignition is on
*/
void Board320_240::afterSleep() {

  // Wakeup reason
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : syslog->println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : syslog->println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : syslog->println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : syslog->println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : syslog->println("Wakeup caused by ULP program"); break;
    default: syslog->printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    liveData->settings.sleepModeLevel = 0;
    syslog->println("Button pressed = SleepMode Disabled & Waking up");
    return;
  }

  liveData->params.sleepModeQueue = true;

  bool firstRun = true;
  while (liveData->commandQueueIndex - 1 > liveData->commandQueueLoopFrom || firstRun) {
    if (liveData->commandQueueIndex - 1 == liveData->commandQueueLoopFrom) {
      firstRun = false;
    }

    if (millis() > 5000) {
      syslog->println("Time's up (5s timeout)...");
      goToSleep();
    }

    commInterface->mainLoop();
  }

  if (liveData->params.getValidResponse) {
    syslog->println("Wake up conditions satisfied... Good morning!");
    liveData->params.sleepModeQueue = false;
    liveData->params.wakeUpTime = liveData->params.currentTime;
  } else {
    syslog->println("No response from module...");
    goToSleep();
  }
}

/**
   Set brightness level
*/
void Board320_240::setBrightness(byte lcdBrightnessPerc) {

  analogWrite(pinBrightness, lcdBrightnessPerc);
}

/**
  Draw cell on dashboard
*/
void Board320_240::drawBigCell(int32_t x, int32_t y, int32_t w, int32_t h, const char* text, const char* desc, uint16_t bgColor, uint16_t fgColor) {

  int32_t posx, posy;

  posx = (x * 80) + 4;
  posy = (y * 60) + 1;

  sprFillRect(x * 80, y * 60, ((w) * 80) - 1, ((h) * 60) - 1,  bgColor);
  sprDrawFastVLine(((x + w) * 80) - 1, ((y) * 60) - 1, h * 60, TFT_BLACK);
  sprDrawFastHLine(((x) * 80) - 1, ((y + h) * 60) - 1, w * 80, TFT_BLACK);
  sprSetTextDatum(TL_DATUM); // Topleft
  sprSetTextColor(TFT_SILVER, bgColor); // Bk, fg color
  sprSetTextSize(1); // Size for small 5x7 font
  sprDrawString(desc, posx, posy, 2);

  // Big 2x2 cell in the middle of screen
  if (w == 2 && h == 2) {

    // Bottom 2 numbers with charged/discharged kWh from start
    posx = (x * 80) + 5;
    posy = ((y + h) * 60) - 32;
    sprintf(tmpStr3, "-%01.01f", liveData->params.cumulativeEnergyDischargedKWh - liveData->params.cumulativeEnergyDischargedKWhStart);
    sprSetFreeFont(&Roboto_Thin_24);
    sprSetTextDatum(TL_DATUM);
    sprDrawString(tmpStr3, posx, posy, GFXFF);

    posx = ((x + w) * 80) - 8;
    sprintf(tmpStr3, "+%01.01f", liveData->params.cumulativeEnergyChargedKWh - liveData->params.cumulativeEnergyChargedKWhStart);
    sprSetTextDatum(TR_DATUM);
    sprDrawString(tmpStr3, posx, posy, GFXFF);

    // Main number - kwh on roads, amps on charges
    posy = (y * 60) + 24;
    sprSetTextColor(fgColor, bgColor);
    sprSetFreeFont(&Orbitron_Light_32);
    sprDrawString(text, posx, posy, 7);

  } else {

    // All others 1x1 cells
    sprSetTextDatum(MC_DATUM);
    sprSetTextColor(fgColor, bgColor);
    sprSetFreeFont(&Orbitron_Light_24);
    posx = (x * 80) + (w * 80 / 2) - 3;
    posy = (y * 60) + (h * 60 / 2) + 4;
    sprDrawString(text, posx, posy, (w == 2 ? 7 : GFXFF));
  }
}

/**
  Draw small rect 80x30
*/
void Board320_240::drawSmallCell(int32_t x, int32_t y, int32_t w, int32_t h, const char* text, const char* desc, int16_t bgColor, int16_t fgColor) {

  int32_t posx, posy;

  posx = (x * 80) + 4;
  posy = (y * 32) + 1;

  sprFillRect(x * 80, y * 32, ((w) * 80), ((h) * 32),  bgColor);
  sprDrawFastVLine(((x + w) * 80) - 1, ((y) * 32) - 1, h * 32, TFT_BLACK);
  sprDrawFastHLine(((x) * 80) - 1, ((y + h) * 32) - 1, w * 80, TFT_BLACK);
  sprSetTextDatum(TL_DATUM); // Topleft
  sprSetTextColor(TFT_SILVER, bgColor); // Bk, fg bgColor
  sprSetTextSize(1); // Size for small 5x7 font
  sprDrawString(desc, posx, posy, 2);

  sprSetTextDatum(TC_DATUM);
  sprSetTextColor(fgColor, bgColor);
  posx = (x * 80) + (w * 80 / 2) - 3;
  sprDrawString(text, posx, posy + 14, 2);
}

/**
  Show tire pressures / temperatures
  Custom field
*/
void Board320_240::showTires(int32_t x, int32_t y, int32_t w, int32_t h, const char* topleft, const char* topright, const char* bottomleft, const char* bottomright, uint16_t color) {

  int32_t posx, posy;

  sprFillRect(x * 80, y * 60, ((w) * 80) - 1, ((h) * 60) - 1,  color);
  sprDrawFastVLine(((x + w) * 80) - 1, ((y) * 60) - 1, h * 60, TFT_BLACK);
  sprDrawFastHLine(((x) * 80) - 1, ((y + h) * 60) - 1, w * 80, TFT_BLACK);

  sprSetTextDatum(TL_DATUM);
  sprSetTextColor(TFT_SILVER, color);
  sprSetTextSize(1);
  posx = (x * 80) + 4;
  posy = (y * 60) + 0;
  sprDrawString(topleft, posx, posy, 2);
  posy = (y * 60) + 14;
  sprDrawString(bottomleft, posx, posy, 2);

  sprSetTextDatum(TR_DATUM);
  posx = ((x + w) * 80) - 4;
  posy = (y * 60) + 0;
  sprDrawString(topright, posx, posy, 2);
  posy = (y * 60) + 14;
  sprDrawString(bottomright, posx, posy, 2);
}

/**
   Main screen (Screen 1)
*/
void Board320_240::drawSceneMain() {

  // Tire pressure
  char pressureStr[4] = "bar";
  char temperatureStr[2] = "C";
  if (liveData->settings.pressureUnit != 'b')
    strcpy(pressureStr, "psi");
  if (liveData->settings.temperatureUnit != 'c')
    strcpy(temperatureStr, "F");
  sprintf(tmpStr1, "%01.01f%s %02.00f%s", liveData->bar2pressure(liveData->params.tireFrontLeftPressureBar), pressureStr, liveData->celsius2temperature(liveData->params.tireFrontLeftTempC), temperatureStr);
  sprintf(tmpStr2, "%02.00f%s %01.01f%s", liveData->celsius2temperature(liveData->params.tireFrontRightTempC), temperatureStr, liveData->bar2pressure(liveData->params.tireFrontRightPressureBar), pressureStr);
  sprintf(tmpStr3, "%01.01f%s %02.00f%s", liveData->bar2pressure(liveData->params.tireRearLeftPressureBar), pressureStr, liveData->celsius2temperature(liveData->params.tireRearLeftTempC), temperatureStr);
  sprintf(tmpStr4, "%02.00f%s %01.01f%s", liveData->celsius2temperature(liveData->params.tireRearRightTempC), temperatureStr, liveData->bar2pressure(liveData->params.tireRearRightPressureBar), pressureStr);
  showTires(1, 0, 2, 1, tmpStr1, tmpStr2, tmpStr3, tmpStr4, TFT_BLACK);

  // Added later - kwh total in tires box
  // TODO: refactoring
  sprSetTextDatum(TL_DATUM);
  sprSetTextColor(TFT_GREEN, TFT_BLACK);
  sprintf(tmpStr1, "C: %01.01f +%01.01fkWh", liveData->params.cumulativeEnergyChargedKWh, liveData->params.cumulativeEnergyChargedKWh - liveData->params.cumulativeEnergyChargedKWhStart);
  sprDrawString(tmpStr1, (1 * 80) + 4, (0 * 60) + 30, 2);
  sprSetTextColor(TFT_YELLOW, TFT_BLACK);
  sprintf(tmpStr1, "D: %01.01f -%01.01fkWh", liveData->params.cumulativeEnergyDischargedKWh, liveData->params.cumulativeEnergyDischargedKWh - liveData->params.cumulativeEnergyDischargedKWhStart);
  sprDrawString(tmpStr1, (1 * 80) + 4, (0 * 60) + 44, 2);

  // batPowerKwh100 on roads, else batPowerAmp
  if (liveData->params.speedKmh > 20) {
    sprintf(tmpStr1, "%01.01f", liveData->km2distance(liveData->params.batPowerKwh100));
    drawBigCell(1, 1, 2, 2, tmpStr1, ((liveData->settings.distanceUnit == 'k') ? "POWER KWH/100KM" : "POWER KWH/100MI"), (liveData->params.batPowerKwh100 >= 0 ? TFT_DARKGREEN2 : (liveData->params.batPowerKwh100 < -30.0 ? TFT_RED : TFT_DARKRED)), TFT_WHITE);
  } else {
    // batPowerAmp on chargers (under 10kmh)
    sprintf(tmpStr1, "%01.01f", liveData->params.batPowerKw);
    drawBigCell(1, 1, 2, 2, tmpStr1, "POWER KW", (liveData->params.batPowerKw >= 0 ? TFT_DARKGREEN2 : (liveData->params.batPowerKw <= -30 ? TFT_RED : TFT_DARKRED)), TFT_WHITE);
  }

  // socPerc
  sprintf(tmpStr1, "%01.00f%%", liveData->params.socPerc);
  sprintf(tmpStr2, (liveData->params.sohPerc ==  100.0 ? "SOC/H%01.00f%%" : "SOC/H%01.01f%%"), liveData->params.sohPerc);
  drawBigCell(0, 0, 1, 1, ((liveData->params.socPerc == 255) ? "---": tmpStr1), tmpStr2, (liveData->params.socPerc < 10 || liveData->params.sohPerc < 100 ? TFT_RED : (liveData->params.socPerc  > 80 ? TFT_DARKGREEN2 : TFT_DEFAULT_BK)), TFT_WHITE);

  // batPowerAmp
  sprintf(tmpStr1, (abs(liveData->params.batPowerAmp) > 9.9 ? "%01.00f" : "%01.01f"), liveData->params.batPowerAmp);
  drawBigCell(0, 1, 1, 1, tmpStr1, "CURRENT A", (liveData->params.batPowerAmp >= 0 ? TFT_DARKGREEN2 : TFT_DARKRED), TFT_WHITE);

  // batVoltage
  sprintf(tmpStr1, "%03.00f", liveData->params.batVoltage);
  drawBigCell(0, 2, 1, 1, tmpStr1, "VOLTAGE", TFT_DEFAULT_BK, TFT_WHITE);

  // batCellMinV
  sprintf(tmpStr1, "%01.02f", liveData->params.batCellMaxV - liveData->params.batCellMinV);
  sprintf(tmpStr2, "CELLS %01.02f", liveData->params.batCellMinV);
  drawBigCell(0, 3, 1, 1, ( liveData->params.batCellMaxV - liveData->params.batCellMinV == 0.00 ? "OK" : tmpStr1), tmpStr2, TFT_DEFAULT_BK, TFT_WHITE);

  // batTempC
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f" : "%01.01f"), liveData->celsius2temperature(liveData->params.batMinC));
  sprintf(tmpStr2, ((liveData->settings.temperatureUnit == 'c') ? "BATT. %01.00fC" : "BATT. %01.01fF"), liveData->celsius2temperature(liveData->params.batMaxC));
  drawBigCell(1, 3, 1, 1, tmpStr1, tmpStr2, TFT_TEMP, (liveData->params.batTempC >= 15) ? ((liveData->params.batTempC >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED);

  // batHeaterC
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f" : "%01.01f"), liveData->celsius2temperature(liveData->params.batHeaterC));
  drawBigCell(2, 3, 1, 1, tmpStr1, "BAT.HEAT", TFT_TEMP, TFT_WHITE);

  // Aux perc / temp
  if (liveData->settings.carType == CAR_BMW_I3_2014) { // TODO: use invalid auxPerc value as decision point here?
    sprintf(tmpStr1, "%01.00f", liveData->params.auxTemperature);
    drawBigCell(3, 0, 1, 1, tmpStr1, "AUX TEMP.", (liveData->params.auxTemperature < 5 ? TFT_RED : TFT_DEFAULT_BK), TFT_WHITE);
  } else {
    sprintf(tmpStr1, "%01.00f%%", liveData->params.auxPerc);
    drawBigCell(3, 0, 1, 1, tmpStr1, "AUX BAT.", (liveData->params.auxPerc < 60 ? TFT_RED : TFT_DEFAULT_BK), TFT_WHITE);
  }

  // Aux amp
  sprintf(tmpStr1, (abs(liveData->params.auxCurrentAmp) > 9.9 ? "%01.00f" : "%01.01f"), liveData->params.auxCurrentAmp);
  drawBigCell(3, 1, 1, 1, tmpStr1, "AUX AMPS",  (liveData->params.auxCurrentAmp >= 0 ? TFT_DARKGREEN2 : TFT_DARKRED), TFT_WHITE);

  // auxVoltage
  sprintf(tmpStr1, "%01.01f", liveData->params.auxVoltage);
  drawBigCell(3, 2, 1, 1, tmpStr1, "AUX VOLTS", (liveData->params.auxVoltage < 12.1 ? TFT_RED : (liveData->params.auxVoltage < 12.6 ? TFT_ORANGE : TFT_DEFAULT_BK)), TFT_WHITE);

  // indoorTemperature
  sprintf(tmpStr1, "%01.01f", liveData->celsius2temperature(liveData->params.indoorTemperature));
  sprintf(tmpStr2, "IN/OUT%01.01f", liveData->celsius2temperature(liveData->params.outdoorTemperature));
  drawBigCell(3, 3, 1, 1, tmpStr1, tmpStr2, TFT_TEMP, TFT_WHITE);
}

/**
   Speed + kwh/100km (Screen 2)
*/
void Board320_240::drawSceneSpeed() {

  int32_t posx, posy;

  sprFillRect(0, 36, 200, 160, TFT_DARKRED);

  posx = 320 / 2;
  posy = 40;
  sprSetTextDatum(TR_DATUM);
  sprSetTextColor(TFT_WHITE, TFT_DARKRED);
  sprSetTextSize(2); // Size for small 5cix7 font
  sprintf(tmpStr3, "0");
  if (liveData->params.speedKmh > 10)
    sprintf(tmpStr3, "%01.00f", liveData->km2distance(liveData->params.speedKmh));
  sprDrawString(tmpStr3, 200, posy, 7);

  posy = 145;
  sprSetTextDatum(TR_DATUM); // Top center
  sprSetTextSize(1);
  if (liveData->params.speedKmh > 25 && liveData->params.batPowerKw < 0) {
    sprintf(tmpStr3, "%01.01f", liveData->km2distance(liveData->params.batPowerKwh100));
  } else {
    sprintf(tmpStr3, "%01.01f", liveData->params.batPowerKw);
  }
  sprDrawString(tmpStr3, 200, posy, 7);

  // Bottom 2 numbers with charged/discharged kWh from start
  sprSetFreeFont(&Roboto_Thin_24);
  sprSetTextColor(TFT_WHITE, TFT_BLACK);
  posx = 5;
  posy = 5;
  sprSetTextDatum(TL_DATUM);
  sprintf(tmpStr3, ((liveData->settings.distanceUnit == 'k') ? "%01.00fkm" : "%01.00fmi"), liveData->km2distance(liveData->params.odoKm));
  sprDrawString(tmpStr3, posx, posy, GFXFF);
  sprSetTextDatum(TR_DATUM);
  if (liveData->params.batteryManagementMode != BAT_MAN_MODE_NOT_IMPLEMENTED)  {
    sprDrawString(liveData->getBatteryManagementModeStr(liveData->params.batteryManagementMode), 320 - posx, posy, GFXFF);
  } else 
  if (liveData->params.motorRpm > -1) {
    sprintf(tmpStr3, "%01.00frpm" , liveData->params.motorRpm);
    sprDrawString(tmpStr3, 320 - posx, posy, GFXFF);
  }

  // Bottom info
  // Cummulative regen/power
  posy = 240 - 5;
  sprintf(tmpStr3, "-%01.01f", liveData->params.cumulativeEnergyDischargedKWh - liveData->params.cumulativeEnergyDischargedKWhStart);
  sprSetTextDatum(BL_DATUM);
  sprDrawString(tmpStr3, posx, posy, GFXFF);
  posx = 320 - 5;
  sprintf(tmpStr3, "+%01.01f", liveData->params.cumulativeEnergyChargedKWh - liveData->params.cumulativeEnergyChargedKWhStart);
  sprSetTextDatum(BR_DATUM);
  sprDrawString(tmpStr3, posx, posy, GFXFF);
  // Bat.power
  posx = 320 / 2;
  sprintf(tmpStr3, "%01.01fkw", liveData->params.batPowerKw);
  sprSetTextDatum(BC_DATUM);
  sprDrawString(tmpStr3, posx, posy, GFXFF);

  // RIGHT INFO
  // Battery "cold gate" detection - red < 15C (43KW limit), <25 (blue - 55kW limit), green all ok
  sprFillCircle(290, 60, 25, (liveData->params.batTempC >= 15) ? ((liveData->params.batTempC >= 25) ? TFT_DARKGREEN2 : TFT_BLUE) : TFT_RED);
  sprSetTextColor(TFT_WHITE, (liveData->params.batTempC >= 15) ? ((liveData->params.batTempC >= 25) ? TFT_DARKGREEN2 : TFT_BLUE) : TFT_RED);
  sprSetFreeFont(&Roboto_Thin_24);
  sprSetTextDatum(MC_DATUM);
  sprintf(tmpStr3, "%01.00f", liveData->celsius2temperature(liveData->params.batTempC));
  sprDrawString(tmpStr3, 290, 60, GFXFF);
  // Brake lights
  sprFillRect(210, 40, 40, 40, (liveData->params.brakeLights) ? TFT_RED : TFT_BLACK);

  // Soc%, bat.kWh
  sprSetFreeFont(&Orbitron_Light_32);
  sprSetTextColor(TFT_WHITE, TFT_BLACK);
  sprSetTextDatum(TR_DATUM);
  sprintf(tmpStr3, "%01.00f%%", liveData->params.socPerc);
  sprDrawString(tmpStr3, 320, 94, GFXFF);
  if (liveData->params.socPerc > 0) {
    float capacity = liveData->params.batteryTotalAvailableKWh * (liveData->params.socPerc / 100);
    // calibration for Niro/Kona, real available capacity is ~66.5kWh, 0-10% ~6.2kWh, 90-100% ~7.2kWh
    if (liveData->settings.carType == CAR_KIA_ENIRO_2020_64 || liveData->settings.carType == CAR_HYUNDAI_KONA_2020_64) {
      capacity = (liveData->params.socPerc * 0.615) * (1 + (liveData->params.socPerc * 0.0008));
    }
    sprintf(tmpStr3, "%01.01f", capacity);
    sprDrawString(tmpStr3, 320, 129, GFXFF);
    sprDrawString("kWh", 320, 164, GFXFF);
  }
}

void Board320_240::drawSceneHud() {

    float batColor;
    
    // FULL brigtness
    setBrightness(100);
    
     // Change rotation to vertical & mirror
    if (tftGetRotation() != 7) {
      tftSetRotation(7);
      tftFillScreen(TFT_BLACK);
    }

    if (liveData->commConnected && firstReload < 3) {
      tftFillScreen(TFT_BLACK);
      firstReload++;
    }

    tftSetTextDatum(TR_DATUM); // top-right alignment
    tftSetTextColor(TFT_WHITE, TFT_BLACK); // foreground, background text color

    // Draw speed
    tftSetTextSize(3);
    sprintf(tmpStr3, "0");
    if (liveData->params.speedKmh > 10) {
      if (liveData->params.speedKmh != lastSpeedKmh) {
        tftFillRect(0, 210, 320, 30, TFT_BLACK);
        sprintf(tmpStr3, "%01.00f", liveData->km2distance(liveData->params.speedKmh));
        lastSpeedKmh = liveData->params.speedKmh;
      }
    }
    else
    {
      sprintf(tmpStr3, "0");
    }
    tftDrawString(tmpStr3, 320, 0, 7);

    // Draw power kWh/100km (>25kmh) else kW
    tftSetTextSize(1);
    if (liveData->params.speedKmh > 25 && liveData->params.batPowerKw < 0) {
      sprintf(tmpStr3, "%01.00f", liveData->km2distance(liveData->params.batPowerKwh100));
    }
    else {
      sprintf(tmpStr3, "%01.01f", liveData->params.batPowerKw);
    }
    tftFillRect(181, 149, 150, 50, TFT_BLACK);
    tftDrawString(tmpStr3, 320, 150, 7);
    
    // Draw soc%
    sprintf(tmpStr3, "%01.00f%", liveData->params.socPerc);
    tftDrawString(tmpStr3, 160 , 150, 7);

    // Cold gate battery
    batColor = (liveData->params.batTempC >= 15) ? ((liveData->params.batTempC >= 25) ? TFT_DARKGREEN2 : TFT_BLUE) : TFT_RED;
    tftFillRect(0, 70, 50, 140, batColor);
    tftFillRect(15, 60, 20, 10, batColor);
    tftSetTextColor(TFT_WHITE, batColor);
    tftSetFreeFont(&Roboto_Thin_24);
    tftSetTextDatum(MC_DATUM);
    sprintf(tmpStr3, "%01.00f", liveData->celsius2temperature(liveData->params.batTempC));
    tftDrawString(tmpStr3, 25, 180, GFXFF);

    // Brake lights
    tftFillRect(0, 215, 320, 25, (liveData->params.brakeLights) ? TFT_DARKRED : TFT_BLACK);
}

/**
   Battery cells (Screen 3)
*/
void Board320_240::drawSceneBatteryCells() {

  int32_t posx, posy;

  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.batHeaterC));
  drawSmallCell(0, 0, 1, 1, tmpStr1, "HEATER", TFT_TEMP, TFT_CYAN);
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.batInletC));
  drawSmallCell(1, 0, 1, 1, tmpStr1, "BAT.INLET", TFT_TEMP, TFT_CYAN);
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.batModuleTempC[0]));
  drawSmallCell(0, 1, 1, 1, tmpStr1, "MO1", TFT_TEMP, (liveData->params.batModuleTempC[0] >= 15) ? ((liveData->params.batModuleTempC[0] >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED);
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.batModuleTempC[1]));
  drawSmallCell(1, 1, 1, 1, tmpStr1, "MO2", TFT_TEMP, (liveData->params.batModuleTempC[1] >= 15) ? ((liveData->params.batModuleTempC[1] >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED);
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.batModuleTempC[2]));
  drawSmallCell(2, 1, 1, 1, tmpStr1, "MO3", TFT_TEMP, (liveData->params.batModuleTempC[2] >= 15) ? ((liveData->params.batModuleTempC[2] >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED);
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.batModuleTempC[3]));
  drawSmallCell(3, 1, 1, 1, tmpStr1, "MO4", TFT_TEMP, (liveData->params.batModuleTempC[3] >= 15) ? ((liveData->params.batModuleTempC[3] >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED);
  // Ioniq (up to 12 cells)
  for (uint16_t i = 4; i < liveData->params.batModuleTempCount; i++) {
    if (liveData->params.batModuleTempC[i] == 0)
      continue;
    posx = (((i - 4) % 8) * 40);
    posy = ((floor((i - 4) / 8)) * 13) + 64;
    //sprFillRect(x * 80, y * 32, ((w) * 80), ((h) * 32),  bgColor);
    sprSetTextSize(1); // Size for small 5x7 font
    sprSetTextDatum(TL_DATUM);
    sprSetTextColor(((liveData->params.batModuleTempC[i] >= 15) ? ((liveData->params.batModuleTempC[i] >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED), TFT_BLACK);
    sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00fC" : "%01.01fF"), liveData->celsius2temperature(liveData->params.batModuleTempC[i]));
    sprDrawString(tmpStr1, posx + 4, posy, 2);
  }

  sprSetTextDatum(TL_DATUM); // Topleft
  sprSetTextSize(1); // Size for small 5x7 font

  // Find min and max val
  float minVal = -1, maxVal = -1;
  for (int i = 0; i < 98; i++) {
    if ((liveData->params.cellVoltage[i] < minVal || minVal == -1) && liveData->params.cellVoltage[i] != -1)
      minVal = liveData->params.cellVoltage[i];
    if ((liveData->params.cellVoltage[i] > maxVal || maxVal == -1) && liveData->params.cellVoltage[i] != -1)
      maxVal = liveData->params.cellVoltage[i];
    if (liveData->params.cellVoltage[i] > 0 && i > liveData->params.cellCount + 1)
      liveData->params.cellCount = i + 1;
  }

  // Draw cell matrix
  for (int i = 0; i < 98; i++) {
    if (liveData->params.cellVoltage[i] == -1)
      continue;
    posx = ((i % 8) * 40) + 4;
    posy = ((floor(i / 8) + (liveData->params.cellCount > 96 ? 0 : 1)) * 13) + 68;
    sprintf(tmpStr3, "%01.02f", liveData->params.cellVoltage[i]);
    sprSetTextColor(TFT_NAVY, TFT_BLACK);
    if (liveData->params.cellVoltage[i] == minVal && minVal != maxVal)
      sprSetTextColor(TFT_RED, TFT_BLACK);
    if (liveData->params.cellVoltage[i] == maxVal && minVal != maxVal)
      sprSetTextColor(TFT_GREEN, TFT_BLACK);
    sprDrawString(tmpStr3, posx, posy, 2);
  }
}

/**
   drawPreDrawnChargingGraphs
   P = U.I
*/
void Board320_240::drawPreDrawnChargingGraphs(int zeroX, int zeroY, int mulX, int mulY) {

  // Rapid gate
  sprDrawLine(zeroX + (/* SOC FROM */ 1 * mulX), zeroY - (/*I*/ 180 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 57 * mulX), zeroY - (/*I*/ 180 * /*U SOC*/ (57 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_RAPIDGATE35);
  // Coldgate <5C
  sprDrawLine(zeroX + (/* SOC FROM */ 1 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 65 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (65 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE0_5);
  // Coldgate 5-14C
  sprDrawLine(zeroX + (/* SOC FROM */ 1 * mulX), zeroY - (/*I*/ 110 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 57 * mulX), zeroY - (/*I*/ 110 * /*U SOC*/ (57 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE5_14);
  sprDrawLine(zeroX +  (/* SOC FROM */ 57 * mulX), zeroY - (/*I*/ 110 * /*U SOC*/ (57 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 58 * mulX), zeroY - (/*I*/ 75 * /*U SOC*/ (58 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE5_14);
  sprDrawLine(zeroX + (/* SOC FROM */ 58 * mulX), zeroY - (/*I*/ 75 * /*U SOC*/ (58 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 64 * mulX), zeroY - (/*I*/ 75 * /*U SOC*/ (64 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE5_14);
  sprDrawLine(zeroX + (/* SOC FROM */ 64 * mulX), zeroY - (/*I*/ 75 * /*U SOC*/ (64 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 65 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (65 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE5_14);
  sprDrawLine(zeroX + (/* SOC FROM */ 65 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (65 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 82 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (82 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE5_14);
  sprDrawLine(zeroX + (/* SOC FROM */ 82 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (82 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 83 * mulX), zeroY - (/*I*/ 40 * /*U SOC*/ (83 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE5_14);
  // Coldgate 15-24C
  sprDrawLine(zeroX + (/* SOC FROM */ 1 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 57 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (57 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE15_24);
  sprDrawLine(zeroX +  (/* SOC FROM */ 57 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (57 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC FROM */ 58 * mulX), zeroY - (/*I*/ 110 * /*U SOC*/ (58 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE15_24);
  sprDrawLine(zeroX + (/* SOC TO */ 58 * mulX), zeroY - (/*I*/ 110 * /*U SOC*/ (58 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 78 * mulX), zeroY - (/*I*/ 110 * /*U SOC*/ (78 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_COLDGATE15_24);
  // Optimal
  sprDrawLine(zeroX + (/* SOC FROM */ 1 * mulX), zeroY - (/*I*/ 200 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 57 * mulX), zeroY - (/*I*/ 200 * /*U SOC*/ (57 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 51 * mulX), zeroY - (/*I*/ 200 * /*U SOC*/ (51 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 51 * mulX), zeroY - (/*I*/ 195 * /*U SOC*/ (51 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 53 * mulX), zeroY - (/*I*/ 200 * /*U SOC*/ (53 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 53 * mulX), zeroY - (/*I*/ 195 * /*U SOC*/ (53 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 55 * mulX), zeroY - (/*I*/ 200 * /*U SOC*/ (55 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 55 * mulX), zeroY - (/*I*/ 195 * /*U SOC*/ (55 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 57 * mulX), zeroY - (/*I*/ 200 * /*U SOC*/ (57 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 58 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (58 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX + (/* SOC FROM */ 58 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (58 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 77 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (77 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 71 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (71 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 71 * mulX), zeroY - (/*I*/ 145 * /*U SOC*/ (71 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 73 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (73 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 73 * mulX), zeroY - (/*I*/ 145 * /*U SOC*/ (73 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 75 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (75 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 75 * mulX), zeroY - (/*I*/ 145 * /*U SOC*/ (75 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 77 * mulX), zeroY - (/*I*/ 150 * /*U SOC*/ (77 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 78 * mulX), zeroY - (/*I*/ 90 * /*U SOC*/ (78 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX + (/* SOC FROM */ 78 * mulX), zeroY - (/*I*/ 90 * /*U SOC*/ (78 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 82 * mulX), zeroY - (/*I*/ 90 * /*U SOC*/ (82 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 82 * mulX), zeroY - (/*I*/ 90 * /*U SOC*/ (82 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX + (/* SOC TO */ 83 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (83 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX + (/* SOC FROM */ 83 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (83 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 92 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (92 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 92 * mulX), zeroY - (/*I*/ 60 * /*U SOC*/ (92 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 95 * mulX), zeroY - (/*I*/ 35 * /*U SOC*/ (95 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 95 * mulX), zeroY - (/*I*/ 35 * /*U SOC*/ (95 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 98 * mulX), zeroY - (/*I*/ 35 * /*U SOC*/ (98 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  sprDrawLine(zeroX +  (/* SOC FROM */ 98 * mulX), zeroY - (/*I*/ 35 * /*U SOC*/ (98 * 55 / 100 + 352) /**/ / 1000 * mulY),
               zeroX +  (/* SOC TO */ 100 * mulX), zeroY - (/*I*/ 15 * /*U SOC*/ (100 * 55 / 100 + 352) /**/ / 1000 * mulY), TFT_GRAPH_OPTIMAL25);
  // Triangles
  int x = zeroX;
  int y;
  if (liveData->params.batMaxC >= 35) {
    y = zeroY - (/*I*/ 180 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY);
  } else if (liveData->params.batMinC >= 25) {
    y = zeroY - (/*I*/ 200 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY);
  } else if (liveData->params.batMinC >= 15) {
    y = zeroY - (/*I*/ 150 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY);
  } else if (liveData->params.batMinC >= 5) {
    y = zeroY - (/*I*/ 110 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY);
  } else {
    y = zeroY - (/*I*/ 60 * /*U SOC*/ (1 * 55 / 100 + 352) /**/ / 1000 * mulY);
  }
  sprFillTriangle(x + 5, y,  x , y - 5, x, y + 5, TFT_ORANGE);
}

/**
   Charging graph (Screen 4)
*/
void Board320_240::drawSceneChargingGraph() {

  int zeroX = 0;
  int zeroY = 238;
  int mulX = 3; // 100% = 300px;
  int mulY = 2; // 100kW = 200px
  int maxKw = 80;
  int posy = 0;
  uint16_t color;

  sprFillSprite(TFT_BLACK);

  sprintf(tmpStr1, "%01.00f", liveData->params.socPerc);
  drawSmallCell(0, 0, 1, 1, tmpStr1, "SOC", TFT_TEMP, TFT_CYAN);
  sprintf(tmpStr1, "%01.01f", liveData->params.batPowerKw);
  drawSmallCell(1, 0, 1, 1, tmpStr1, "POWER kW", TFT_TEMP, TFT_CYAN);
  sprintf(tmpStr1, "%01.01f", liveData->params.batPowerAmp);
  drawSmallCell(2, 0, 1, 1, tmpStr1, "CURRENT A", TFT_TEMP, TFT_CYAN);
  sprintf(tmpStr1, "%03.00f", liveData->params.batVoltage);
  drawSmallCell(3, 0, 1, 1, tmpStr1, "VOLTAGE", TFT_TEMP, TFT_CYAN);

  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.batHeaterC));
  drawSmallCell(0, 1, 1, 1, tmpStr1, "HEATER", TFT_TEMP, TFT_RED);
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.batInletC));
  drawSmallCell(1, 1, 1, 1, tmpStr1, "BAT.INLET", TFT_TEMP, TFT_CYAN);
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.batMinC));
  drawSmallCell(2, 1, 1, 1, tmpStr1, "BAT.MIN", (liveData->params.batMinC >= 15) ? ((liveData->params.batMinC >= 25) ? TFT_DARKGREEN2 : TFT_BLUE) : TFT_RED, TFT_CYAN);
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%01.00f C" : "%01.01f F"), liveData->celsius2temperature(liveData->params.outdoorTemperature));
  drawSmallCell(3, 1, 1, 1, tmpStr1, "OUT.TEMP.", TFT_TEMP, TFT_CYAN);

  sprSetTextColor(TFT_SILVER, TFT_TEMP);

  for (int i = 0; i <= 10; i++) {
    color = TFT_DARKRED2;
    if (i == 0 || i == 5 || i == 10)
      color = TFT_DARKRED;
    sprDrawFastVLine(zeroX + (i * 10 * mulX), zeroY - (maxKw * mulY), maxKw * mulY, color);
    /*if (i != 0 && i != 10) {
      sprintf(tmpStr1, "%d%%", i * 10);
      sprSetTextDatum(BC_DATUM);
      sprDrawString(tmpStr1, zeroX + (i * 10 * mulX),  zeroY - (maxKw * mulY), 2);
      }*/
    if (i <= (maxKw / 10)) {
      sprDrawFastHLine(zeroX, zeroY - (i * 10 * mulY), 100 * mulX, color);
      if (i > 0) {
        sprintf(tmpStr1, "%d", i * 10);
        sprSetTextDatum(ML_DATUM);
        sprDrawString(tmpStr1, zeroX + (100 * mulX) + 3, zeroY - (i * 10 * mulY), 2);
      }
    }
  }

  // Draw suggested curves
  if (liveData->settings.predrawnChargingGraphs == 1) {
    drawPreDrawnChargingGraphs(zeroX, zeroY, mulX, mulY);
  }

  // Draw realtime values
  for (int i = 0; i <= 100; i++) {
    if (liveData->params.chargingGraphBatMinTempC[i] > -10)
      sprDrawFastHLine(zeroX + (i * mulX) - (mulX / 2), zeroY - (liveData->params.chargingGraphBatMinTempC[i]*mulY), mulX, TFT_BLUE);
    if (liveData->params.chargingGraphBatMaxTempC[i] > -10)
      sprDrawFastHLine(zeroX + (i * mulX) - (mulX / 2), zeroY - (liveData->params.chargingGraphBatMaxTempC[i]*mulY), mulX, TFT_BLUE);
    if (liveData->params.chargingGraphWaterCoolantTempC[i] > -10)
      sprDrawFastHLine(zeroX + (i * mulX) - (mulX / 2), zeroY - (liveData->params.chargingGraphWaterCoolantTempC[i]*mulY), mulX, TFT_PURPLE);
    if (liveData->params.chargingGraphHeaterTempC[i] > -10)
      sprDrawFastHLine(zeroX + (i * mulX) - (mulX / 2), zeroY - (liveData->params.chargingGraphHeaterTempC[i]*mulY), mulX, TFT_RED);

    if (liveData->params.chargingGraphMinKw[i] > 0)
      sprDrawFastHLine(zeroX + (i * mulX) - (mulX / 2), zeroY - (liveData->params.chargingGraphMinKw[i]*mulY), mulX, TFT_GREENYELLOW);
    if (liveData->params.chargingGraphMaxKw[i] > 0)
      sprDrawFastHLine(zeroX + (i * mulX) - (mulX / 2), zeroY - (liveData->params.chargingGraphMaxKw[i]*mulY), mulX, TFT_YELLOW);
  }

  // Bat.module temperatures
  sprSetTextSize(1); // Size for small 5x7 font
  sprSetTextDatum(BL_DATUM);
  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "1=%01.00fC" : "1=%01.00fF"), liveData->celsius2temperature(liveData->params.batModuleTempC[0]));
  sprSetTextColor((liveData->params.batModuleTempC[0] >= 15) ? ((liveData->params.batModuleTempC[0] >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED, TFT_TEMP);
  sprDrawString(tmpStr1, 0,  zeroY - (maxKw * mulY), 2);

  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "2=%01.00fC" : "2=%01.00fF"), liveData->celsius2temperature(liveData->params.batModuleTempC[1]));
  sprSetTextColor((liveData->params.batModuleTempC[1] >= 15) ? ((liveData->params.batModuleTempC[1] >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED, TFT_TEMP);
  sprDrawString(tmpStr1, 48,  zeroY - (maxKw * mulY), 2);

  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "3=%01.00fC" : "3=%01.00fF"), liveData->celsius2temperature(liveData->params.batModuleTempC[2]));
  sprSetTextColor((liveData->params.batModuleTempC[2] >= 15) ? ((liveData->params.batModuleTempC[2] >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED, TFT_TEMP);
  sprDrawString(tmpStr1, 96,  zeroY - (maxKw * mulY), 2);

  sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "4=%01.00fC" : "4=%01.00fF"), liveData->celsius2temperature(liveData->params.batModuleTempC[3]));
  sprSetTextColor((liveData->params.batModuleTempC[3] >= 15) ? ((liveData->params.batModuleTempC[3] >= 25) ? TFT_GREEN : TFT_BLUE) : TFT_RED, TFT_TEMP);
  sprDrawString(tmpStr1, 144,  zeroY - (maxKw * mulY), 2);
  sprintf(tmpStr1, "ir %01.00fkOhm", liveData->params.isolationResistanceKOhm );

  // Bms max.regen/power available
  sprSetTextColor(TFT_WHITE, TFT_BLACK);
  sprintf(tmpStr1, "xC=%01.00fkW ", liveData->params.availableChargePower);
  sprDrawString(tmpStr1, 192,  zeroY - (maxKw * mulY), 2);
  sprSetTextColor(TFT_WHITE, TFT_BLACK);
  sprintf(tmpStr1, "xD=%01.00fkW", liveData->params.availableDischargePower);
  sprDrawString(tmpStr1, 256,  zeroY - (maxKw * mulY), 2);

  //
  sprSetTextDatum(TR_DATUM);
  if (liveData->params.coolingWaterTempC != -1) {
    sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "%s / W=%01.00fC" : "%s /W=%01.00fF"), 
      liveData->getBatteryManagementModeStr(liveData->params.batteryManagementMode),
      liveData->celsius2temperature(liveData->params.coolingWaterTempC));
    sprSetTextColor(TFT_PINK, TFT_TEMP);
    sprDrawString(tmpStr1, zeroX + (10 * 10 * mulX),  zeroY - (maxKw * mulY) + (posy * 15), 2);
    posy++;
  }
  sprSetTextColor(TFT_WHITE, TFT_TEMP);
  if (liveData->params.batFanFeedbackHz > 0) {
    sprintf(tmpStr1, "FF=%03.00fHz", liveData->params.batFanFeedbackHz);
    sprDrawString(tmpStr1, zeroX + (10 * 10 * mulX),  zeroY - (maxKw * mulY) + (posy * 15), 2);
    posy++;
  }
  if (liveData->params.batFanStatus > 0) {
    sprintf(tmpStr1, "FS=%03.00f", liveData->params.batFanStatus);
    sprDrawString(tmpStr1, zeroX + (10 * 10 * mulX),  zeroY - (maxKw * mulY) + (posy * 15), 2);
    posy++;
  }
  if (liveData->params.coolantTemp1C != -1 && liveData->params.coolantTemp2C != -1) {
    sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "C1/2:%01.00f/%01.00fC" : "C1/2:%01.00f/%01.00fF"), liveData->celsius2temperature(liveData->params.coolantTemp1C), liveData->celsius2temperature(liveData->params.coolantTemp2C));
    sprDrawString(tmpStr1, zeroX + (10 * 10 * mulX),  zeroY - (maxKw * mulY) + (posy * 15), 2);
    posy++;
  }
  if (liveData->params.bmsUnknownTempA != -1) {
    sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "A=%01.00fC" : "W=%01.00fF"), liveData->celsius2temperature(liveData->params.bmsUnknownTempA));
    sprDrawString(tmpStr1, zeroX + (10 * 10 * mulX),  zeroY - (maxKw * mulY) + (posy * 15), 2);
    posy++;
  }
  if (liveData->params.bmsUnknownTempB != -1) {
    sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "B=%01.00fC" : "W=%01.00fF"), liveData->celsius2temperature(liveData->params.bmsUnknownTempB));
    sprDrawString(tmpStr1, zeroX + (10 * 10 * mulX),  zeroY - (maxKw * mulY) + (posy * 15), 2);
    posy++;
  }
  if (liveData->params.bmsUnknownTempC != -1) {
    sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "C=%01.00fC" : "W=%01.00fF"), liveData->celsius2temperature(liveData->params.bmsUnknownTempC));
    sprDrawString(tmpStr1, zeroX + (10 * 10 * mulX),  zeroY - (maxKw * mulY) + (posy * 15), 2);
    posy++;
  }
  if (liveData->params.bmsUnknownTempD != -1) {
    sprintf(tmpStr1, ((liveData->settings.temperatureUnit == 'c') ? "D=%01.00fC" : "W=%01.00fF"), liveData->celsius2temperature(liveData->params.bmsUnknownTempD));
    sprDrawString(tmpStr1, zeroX + (10 * 10 * mulX),  zeroY - (maxKw * mulY) + (posy * 15), 2);
    posy++;
  }

  // Print charging time
  time_t diffTime = liveData->params.currentTime - liveData->params.chargingStartTime;
  if ((diffTime / 60) > 99)
    sprintf(tmpStr1, "%02d:%02d:%02d", (diffTime / 3600) % 24, (diffTime / 60) % 60, diffTime % 60);
  else
    sprintf(tmpStr1, "%02d:%02d", (diffTime / 60), diffTime % 60);
  sprSetTextDatum(TL_DATUM);
  sprSetTextColor(TFT_SILVER, TFT_BLACK);
  sprDrawString(tmpStr1, 0, zeroY - (maxKw * mulY), 2);
}

/**
  SOC 10% table (screen 5)
*/
void Board320_240::drawSceneSoc10Table() {

  int zeroY = 2;
  float diffCec, diffCed, diffOdo = -1;
  float firstCed = -1, lastCed = -1, diffCed0to5 = 0;
  float firstCec = -1, lastCec = -1, diffCec0to5 = 0;
  float firstOdo = -1, lastOdo = -1, diffOdo0to5 = 0;
  float diffTime;

  sprSetTextSize(1); // Size for small 5x7 font
  sprSetTextColor(TFT_SILVER, TFT_TEMP);
  sprSetTextDatum(TL_DATUM);
  sprDrawString("CONSUMPTION | DISCH.100%->4% SOC",  2, zeroY, 2);

  sprSetTextDatum(TR_DATUM);

  sprDrawString("dis./char.kWh", 128, zeroY + (1 * 15), 2);
  sprDrawString(((liveData->settings.distanceUnit == 'k') ? "km" : "mi"), 160, zeroY + (1 * 15), 2);
  sprDrawString("kWh100", 224, zeroY + (1 * 15), 2);
  sprDrawString("avg.speed", 310, zeroY + (1 * 15), 2);

  for (int i = 0; i <= 10; i++) {
    sprintf(tmpStr1, "%d%%", (i == 0) ? 5 : i * 10);
    sprDrawString(tmpStr1, 32, zeroY + ((12 - i) * 15), 2);

    firstCed = (liveData->params.soc10ced[i] != -1) ? liveData->params.soc10ced[i] : firstCed;
    lastCed = (lastCed == -1 && liveData->params.soc10ced[i] != -1) ? liveData->params.soc10ced[i] : lastCed;
    firstCec = (liveData->params.soc10cec[i] != -1) ? liveData->params.soc10cec[i] : firstCec;
    lastCec = (lastCec == -1 && liveData->params.soc10cec[i] != -1) ? liveData->params.soc10cec[i] : lastCec;
    firstOdo = (liveData->params.soc10odo[i] != -1) ? liveData->params.soc10odo[i] : firstOdo;
    lastOdo = (lastOdo == -1 && liveData->params.soc10odo[i] != -1) ? liveData->params.soc10odo[i] : lastOdo;

    if (i != 10) {
      diffCec = (liveData->params.soc10cec[i + 1] != -1 && liveData->params.soc10cec[i] != -1) ? (liveData->params.soc10cec[i] - liveData->params.soc10cec[i + 1]) : 0;
      diffCed = (liveData->params.soc10ced[i + 1] != -1 && liveData->params.soc10ced[i] != -1) ? (liveData->params.soc10ced[i + 1] - liveData->params.soc10ced[i]) : 0;
      diffOdo = (liveData->params.soc10odo[i + 1] != -1 && liveData->params.soc10odo[i] != -1) ? (liveData->params.soc10odo[i] - liveData->params.soc10odo[i + 1]) : -1;
      diffTime = (liveData->params.soc10time[i + 1] != -1 && liveData->params.soc10time[i] != -1) ? (liveData->params.soc10time[i] - liveData->params.soc10time[i + 1]) : -1;
      if (diffCec != 0) {
        sprintf(tmpStr1, "+%01.01f", diffCec);
        sprDrawString(tmpStr1, 128, zeroY + ((12 - i) * 15), 2);
        diffCec0to5 = (i == 0) ? diffCec : diffCec0to5;
      }
      if (diffCed != 0) {
        sprintf(tmpStr1, "%01.01f", diffCed);
        sprDrawString(tmpStr1, 80, zeroY + ((12 - i) * 15), 2);
        diffCed0to5 = (i == 0) ? diffCed : diffCed0to5;
      }
      if (diffOdo != -1) {
        sprintf(tmpStr1, "%01.00f", liveData->km2distance(diffOdo));
        sprDrawString(tmpStr1, 160, zeroY + ((12 - i) * 15), 2);
        diffOdo0to5 = (i == 0) ? diffOdo : diffOdo0to5;
        if (diffTime > 0) {
          sprintf(tmpStr1, "%01.01f", liveData->km2distance(diffOdo) / (diffTime / 3600));
          sprDrawString(tmpStr1, 310, zeroY + ((12 - i) * 15), 2);
        }
      }
      if (diffOdo > 0 && diffCed != 0) {
        sprintf(tmpStr1, "%01.1f", (-diffCed * 100.0 / liveData->km2distance(diffOdo)));
        sprDrawString(tmpStr1, 224, zeroY + ((12 - i) * 15), 2);
      }
    }

    if (diffOdo == -1 && liveData->params.soc10odo[i] != -1) {
      sprintf(tmpStr1, "%01.00f", liveData->km2distance(liveData->params.soc10odo[i]));
      sprDrawString(tmpStr1, 160, zeroY + ((12 - i) * 15), 2);
    }
  }

  sprDrawString("0%", 32, zeroY + (13 * 15), 2);
  sprDrawString("0-5% is calculated (same) as 5-10%", 310, zeroY + (13 * 15), 2);

  sprDrawString("TOT.", 32, zeroY + (14 * 15), 2);
  diffCed = (lastCed != -1 && firstCed != -1) ? firstCed - lastCed + diffCed0to5 : 0;
  sprintf(tmpStr1, "%01.01f", diffCed);
  sprDrawString(tmpStr1, 80, zeroY + (14 * 15), 2);
  diffCec = (lastCec != -1 && firstCec != -1) ? lastCec - firstCec + diffCec0to5 : 0;
  sprintf(tmpStr1, "+%01.01f", diffCec);
  sprDrawString(tmpStr1, 128, zeroY + (14 * 15), 2);
  diffOdo = (lastOdo != -1 && firstOdo != -1) ? lastOdo - firstOdo + diffOdo0to5 : 0;
  sprintf(tmpStr1, "%01.00f", liveData->km2distance(diffOdo));
  sprDrawString(tmpStr1, 160, zeroY + (14 * 15), 2);
  sprintf(tmpStr1, "AVAIL.CAP: %01.01f kWh", -diffCed - diffCec);
  sprDrawString(tmpStr1, 310, zeroY + (14 * 15), 2);
}

/**
   Modify caption
*/
String Board320_240::menuItemCaption(int16_t menuItemId, String title) {

  String prefix = "", suffix = "";

  switch (menuItemId) {
    // Set vehicle type
    case MENU_VEHICLE_TYPE:     sprintf(tmpStr1, "[%d]", liveData->settings.carType); suffix = tmpStr1; break;
    case MENU_SAVE_SETTINGS:    sprintf(tmpStr1, "[v%d]", liveData->settings.settingsVersion); suffix = tmpStr1; break;
    case MENU_APP_VERSION:      sprintf(tmpStr1, "[%s]", APP_VERSION); suffix = tmpStr1; break;
    //
    case 101: prefix = (liveData->settings.carType == CAR_KIA_ENIRO_2020_64) ? ">" : ""; break;
    case 102: prefix = (liveData->settings.carType == CAR_HYUNDAI_KONA_2020_64) ? ">" : ""; break;
    case 103: prefix = (liveData->settings.carType == CAR_HYUNDAI_IONIQ_2018) ? ">" : ""; break;
    case 104: prefix = (liveData->settings.carType == CAR_KIA_ENIRO_2020_39) ? ">" : ""; break;
    case 105: prefix = (liveData->settings.carType == CAR_HYUNDAI_KONA_2020_39) ? ">" : ""; break;
    case 106: prefix = (liveData->settings.carType == CAR_RENAULT_ZOE) ? ">" : ""; break;
    case 107: prefix = (liveData->settings.carType == CAR_KIA_NIRO_PHEV) ? ">" : ""; break;
    case 108: prefix = (liveData->settings.carType == CAR_BMW_I3_2014) ? ">" : ""; break;
    case 120: prefix = (liveData->settings.carType == CAR_DEBUG_OBD2_KIA) ? ">" : ""; break;
    //
    case MENU_ADAPTER_BLE4: prefix = (liveData->settings.commType == COMM_TYPE_OBD2BLE4) ? ">" : ""; break;
    case MENU_ADAPTER_CAN:  prefix = (liveData->settings.commType == COMM_TYPE_OBD2CAN) ? ">" : ""; break;
    case MENU_ADAPTER_BT3:  prefix = (liveData->settings.commType == COMM_TYPE_OBD2BT3) ? ">" : ""; break;
    /*case MENU_WIFI:
      suffix = "n/a";
      switch (WiFi.status()) {
      WL_CONNECTED: suffix = "CONNECTED"; break;
      WL_NO_SHIELD: suffix = "NO_SHIELD"; break;
      WL_IDLE_STATUS: suffix = "IDLE_STATUS"; break;
      WL_SCAN_COMPLETED: suffix = "SCAN_COMPLETED"; break;
      WL_CONNECT_FAILED: suffix = "CONNECT_FAILED"; break;
      WL_CONNECTION_LOST: suffix = "CONNECTION_LOST"; break;
      WL_DISCONNECTED: suffix = "DISCONNECTED"; break;
      }
      break;*/
    case MENU_GPRS:             sprintf(tmpStr1, "[HW UART=%d]", liveData->settings.gprsHwSerialPort);  suffix = (liveData->settings.gprsHwSerialPort == 255) ? "[off]" : tmpStr1; break;
    case MENU_SDCARD:           sprintf(tmpStr1, "[%d] %lluMB", SD.cardType(), SD.cardSize() / (1024 * 1024)); suffix = tmpStr1; break;
    case MENU_SERIAL_CONSOLE:   suffix = (liveData->settings.serialConsolePort == 255) ? "[off]" : "[on]"; break;
    case MENU_DEBUG_LEVEL:      switch (liveData->settings.debugLevel) {
        case 0: suffix = "[all]" ; break;
        case 1: suffix = "[comm]" ; break;
        case 2: suffix = "[gsm]" ; break;
        case 3: suffix = "[sdcard]" ; break;
        default: suffix = "[unknown]";
      }
      break;
    case MENU_SCREEN_ROTATION:  suffix = (liveData->settings.displayRotation == 1) ? "[vertical]" : "[normal]"; break;
    case MENU_DEFAULT_SCREEN:   sprintf(tmpStr1, "[%d]", liveData->settings.defaultScreen); suffix = tmpStr1; break;
    case MENU_SCREEN_BRIGHTNESS: sprintf(tmpStr1, "[%d%%]", liveData->settings.lcdBrightness); suffix = (liveData->settings.lcdBrightness == 0) ? "[auto]" : tmpStr1; break;
    case MENU_PREDRAWN_GRAPHS:  suffix = (liveData->settings.predrawnChargingGraphs == 1) ? "[on]" : "[off]"; break;
    case MENU_HEADLIGHTS_REMINDER: suffix = (liveData->settings.headlightsReminder == 1) ? "[on]" : "[off]"; break;
    case MENU_SLEEP_MODE:       switch (liveData->settings.sleepModeLevel) {
        case 0: suffix = "[off]" ; break;
        case 1: suffix = "[screen only]" ; break;
        case 2: suffix = "[deep sleep]" ; break;
        case 3: suffix = "[shutdown]" ; break;
        default: suffix = "[unknown]";
      }
      break;
    case MENU_GPS:              sprintf(tmpStr1, "[HW UART=%d]", liveData->settings.gpsHwSerialPort);  suffix = (liveData->settings.gpsHwSerialPort == 255) ? "[off]" : tmpStr1; break;
    //
    case MENU_SDCARD_ENABLED:   sprintf(tmpStr1, "[%s]", (liveData->settings.sdcardEnabled == 1) ? "on" : "off"); suffix = tmpStr1; break;
    case MENU_SDCARD_AUTOSTARTLOG: sprintf(tmpStr1, "[%s]", (liveData->settings.sdcardEnabled == 0) ? "n/a" : (liveData->settings.sdcardAutstartLog == 1) ? "on" : "off"); suffix = tmpStr1; break;
    case MENU_SDCARD_MOUNT_STATUS: sprintf(tmpStr1, "[%s]", (liveData->settings.sdcardEnabled == 0) ? "n/a" :
                                             (strlen(liveData->params.sdcardFilename) != 0) ? liveData->params.sdcardFilename :
                                             (liveData->params.sdcardInit) ? "READY" : "MOUNT"); suffix = tmpStr1; break;
    case MENU_SDCARD_REC:       sprintf(tmpStr1, "[%s]", (liveData->settings.sdcardEnabled == 0) ? "n/a" : (liveData->params.sdcardRecording) ? "STOP" : "START"); suffix = tmpStr1; break;
    case MENU_SDCARD_INTERVAL:   sprintf(tmpStr1, "[%d]", liveData->settings.sdcardLogIntervalSec); suffix = tmpStr1; break;
    //
    case MENU_WIFI_ENABLED:     suffix = (liveData->settings.wifiEnabled == 1) ? "[on]" : "[off]"; break;
    case MENU_WIFI_SSID:        sprintf(tmpStr1, "%s", liveData->settings.wifiSsid); suffix = tmpStr1; break;
    case MENU_WIFI_PASSWORD:    sprintf(tmpStr1, "%s", liveData->settings.wifiPassword); suffix = tmpStr1; break;
    //
    case MENU_DISTANCE_UNIT:    suffix = (liveData->settings.distanceUnit == 'k') ? "[km]" : "[mi]"; break;
    case MENU_TEMPERATURE_UNIT: suffix = (liveData->settings.temperatureUnit == 'c') ? "[C]" : "[F]"; break;
    case MENU_PRESSURE_UNIT:    suffix = (liveData->settings.pressureUnit == 'b') ? "[bar]" : "[psi]"; break;
  }

  title = ((prefix == "") ? "" : prefix + " ") + title + ((suffix == "") ? "" : " " + suffix);

  return title;
}

/**
  Display menu
*/
void Board320_240::showMenu() {

  uint16_t posY = 0, tmpCurrMenuItem = 0;

  liveData->menuVisible = true;
  sprFillSprite(TFT_BLACK);
  sprSetTextDatum(TL_DATUM);
  sprSetFreeFont(&Roboto_Thin_24);

  // Page scroll
  uint8_t visibleCount = (int)(tftHeight() / sprFontHeight());
  if (liveData->menuItemSelected >= liveData->menuItemOffset + visibleCount)
    liveData->menuItemOffset = liveData->menuItemSelected - visibleCount + 1;
  if (liveData->menuItemSelected < liveData->menuItemOffset)
    liveData->menuItemOffset = liveData->menuItemSelected;

  // Print items
  for (uint16_t i = 0; i < liveData->menuItemsCount; ++i) {
    if (liveData->menuCurrent == liveData->menuItems[i].parentId) {
      if (tmpCurrMenuItem >= liveData->menuItemOffset) {
        bool isMenuItemSelected = liveData->menuItemSelected == tmpCurrMenuItem;
        sprFillRect(0, posY, 320, sprFontHeight() + 2, isMenuItemSelected ? TFT_DARKGREEN2 : TFT_BLACK);
        sprSetTextColor(isMenuItemSelected ? TFT_WHITE : TFT_WHITE, isMenuItemSelected ? TFT_DARKGREEN2 : TFT_BLACK);
        sprDrawString(menuItemCaption(liveData->menuItems[i].id, liveData->menuItems[i].title), 0, posY + 2, GFXFF);
        posY += sprFontHeight();
      }
      tmpCurrMenuItem++;
    }
  }

  displaySprite();
}

/**
   Hide menu
*/
void Board320_240::hideMenu() {

  liveData->menuVisible = false;
  liveData->menuCurrent = 0;
  liveData->menuItemSelected = 0;
  redrawScreen();
}

/**
  Move in menu with left/right button
*/
void Board320_240::menuMove(bool forward) {

  uint16_t tmpCount = 0;
  for (uint16_t i = 0; i < liveData->menuItemsCount; ++i) {
    if (liveData->menuCurrent == liveData->menuItems[i].parentId && strlen(liveData->menuItems[i].title) != 0)
      tmpCount++;
  }
  if (forward) {
    liveData->menuItemSelected = (liveData->menuItemSelected >= tmpCount - 1 ) ? 0 : liveData->menuItemSelected + 1;
  } else {
    liveData->menuItemSelected = (liveData->menuItemSelected <= 0) ? tmpCount - 1 : liveData->menuItemSelected - 1;
  }
  showMenu();
}

/**
   Enter menu item
*/
void Board320_240::menuItemClick() {

  // Locate menu item for meta data
  MENU_ITEM* tmpMenuItem;
  uint16_t tmpCurrMenuItem = 0;
  int16_t parentMenu = -1;
  uint16_t i;

  for (i = 0; i < liveData->menuItemsCount; ++i) {
    if (liveData->menuCurrent == liveData->menuItems[i].parentId) {
      if (parentMenu == -1)
        parentMenu = liveData->menuItems[i].targetParentId;
      if (liveData->menuItemSelected == tmpCurrMenuItem) {
        tmpMenuItem = &liveData->menuItems[i];
        break;
      }
      tmpCurrMenuItem++;
    }
  }

  // Exit menu, parent level menu, open item
  bool showParentMenu = false;
  if (liveData->menuItemSelected > 0) {
    syslog->println(tmpMenuItem->id);
    // Device list
    if (tmpMenuItem->id > 10000 && tmpMenuItem->id < 10100) {
      strlcpy((char*)liveData->settings.obdMacAddress, (char*)tmpMenuItem->obdMacAddress, 20);
      syslog->print("Selected adapter MAC address ");
      syslog->println(liveData->settings.obdMacAddress);
      saveSettings();
      ESP.restart();
    }
    // Other menus
    switch (tmpMenuItem->id) {
      // Set vehicle type
      case 101: liveData->settings.carType = CAR_KIA_ENIRO_2020_64; showMenu(); return; break;
      case 102: liveData->settings.carType = CAR_HYUNDAI_KONA_2020_64; showMenu(); return; break;
      case 103: liveData->settings.carType = CAR_HYUNDAI_IONIQ_2018; showMenu(); return; break;
      case 104: liveData->settings.carType = CAR_KIA_ENIRO_2020_39; showMenu(); return; break;
      case 105: liveData->settings.carType = CAR_HYUNDAI_KONA_2020_39; showMenu(); return; break;
      case 106: liveData->settings.carType = CAR_RENAULT_ZOE; showMenu(); return; break;
      case 107: liveData->settings.carType = CAR_KIA_NIRO_PHEV; showMenu(); return; break;
      case 108: liveData->settings.carType = CAR_BMW_I3_2014; showMenu(); return; break;
      case 120: liveData->settings.carType = CAR_DEBUG_OBD2_KIA; showMenu(); return; break;
      // Comm type
      case MENU_ADAPTER_BLE4: liveData->settings.commType = COMM_TYPE_OBD2BLE4; showMenu(); return; break;
      case MENU_ADAPTER_CAN: liveData->settings.commType = COMM_TYPE_OBD2CAN; showMenu(); return; break;
      case MENU_ADAPTER_BT3: liveData->settings.commType = COMM_TYPE_OBD2BT3; showMenu(); return; break;
      // Screen orientation
      case MENU_SCREEN_ROTATION: liveData->settings.displayRotation = (liveData->settings.displayRotation == 1) ? 3 : 1; tftSetRotation(liveData->settings.displayRotation); showMenu(); return; break;
      // Default screen
      case 3061: liveData->settings.defaultScreen = 1; showParentMenu = true; break;
      case 3062: liveData->settings.defaultScreen = 2; showParentMenu = true; break;
      case 3063: liveData->settings.defaultScreen = 3; showParentMenu = true; break;
      case 3064: liveData->settings.defaultScreen = 4; showParentMenu = true; break;
      case 3065: liveData->settings.defaultScreen = 5; showParentMenu = true; break;
      case 3066: liveData->settings.defaultScreen = 7; showParentMenu = true; break;
      // SleepMode off/on
      case MENU_SLEEP_MODE:           liveData->settings.sleepModeLevel = (liveData->settings.sleepModeLevel == 3) ? 0 : liveData->settings.sleepModeLevel + 1; showMenu(); return; break;
      case MENU_SCREEN_BRIGHTNESS:    liveData->settings.lcdBrightness += 20; if (liveData->settings.lcdBrightness > 100) liveData->settings.lcdBrightness = 0;
        setBrightness((liveData->settings.lcdBrightness == 0) ? 100 : liveData->settings.lcdBrightness); showMenu(); return; break;
      // Pre-drawn charg.graphs off/on
      case MENU_PREDRAWN_GRAPHS:      liveData->settings.predrawnChargingGraphs = (liveData->settings.predrawnChargingGraphs == 1) ? 0 : 1; showMenu(); return; break;
      case MENU_HEADLIGHTS_REMINDER:  liveData->settings.headlightsReminder = (liveData->settings.headlightsReminder == 1) ? 0 : 1; showMenu(); return; break;
      case MENU_GPRS:                 liveData->settings.gprsHwSerialPort = (liveData->settings.gprsHwSerialPort == 2) ? 255 : liveData->settings.gprsHwSerialPort + 1; showMenu(); return; break;
      case MENU_GPS:                  liveData->settings.gpsHwSerialPort = (liveData->settings.gpsHwSerialPort == 2) ? 255 : liveData->settings.gpsHwSerialPort + 1; showMenu(); return; break;
      case MENU_SERIAL_CONSOLE:       liveData->settings.serialConsolePort = (liveData->settings.serialConsolePort == 0) ? 255 : liveData->settings.serialConsolePort + 1; showMenu(); return; break;
      case MENU_DEBUG_LEVEL:          liveData->settings.debugLevel = (liveData->settings.debugLevel == 3) ? 0 : liveData->settings.debugLevel + 1; syslog->setDebugLevel(liveData->settings.debugLevel); showMenu(); return; break;
      // Wifi menu
      case MENU_WIFI_ENABLED:         liveData->settings.wifiEnabled = (liveData->settings.wifiEnabled == 1) ? 0 : 1; showMenu(); return; break;
      case MENU_WIFI_SSID:            return; break;
      case MENU_WIFI_PASSWORD:        return; break;
      // Sdcard
      case MENU_SDCARD_ENABLED:       liveData->settings.sdcardEnabled = (liveData->settings.sdcardEnabled == 1) ? 0 : 1; showMenu(); return; break;
      case MENU_SDCARD_AUTOSTARTLOG:  liveData->settings.sdcardAutstartLog = (liveData->settings.sdcardAutstartLog == 1) ? 0 : 1; showMenu(); return; break;
      case MENU_SDCARD_MOUNT_STATUS:  sdcardMount(); break;
      case MENU_SDCARD_REC:           sdcardToggleRecording(); showMenu(); return; break;
      // Distance
      case 4011: liveData->settings.distanceUnit = 'k'; showParentMenu = true; break;
      case 4012: liveData->settings.distanceUnit = 'm'; showParentMenu = true; break;
      // Temperature
      case 4021: liveData->settings.temperatureUnit = 'c'; showParentMenu = true; break;
      case 4022: liveData->settings.temperatureUnit = 'f'; showParentMenu = true; break;
      // Pressure
      case 4031: liveData->settings.pressureUnit = 'b'; showParentMenu = true; break;
      case 4032: liveData->settings.pressureUnit = 'p'; showParentMenu = true; break;
      // Pair ble device
      case 2:
        if (liveData->settings.commType == COMM_TYPE_OBD2CAN) {
          displayMessage("Not supported", "in CAN mode");
          delay(3000);
          hideMenu();
          return;
        }
        scanDevices = true;
        liveData->menuCurrent = 9999;
        commInterface->scanDevices();
        return;
      // Reset settings
      case 8: resetSettings(); hideMenu(); return;
      // Save settings
      case 9: saveSettings(); break;
      // Version
      case 10:
        /*  commInterface->executeCommand("ATSH770");
          delay(50);
          commInterface->executeCommand("3E");
          delay(50);
          commInterface->executeCommand("1003");
          delay(50);
          commInterface->executeCommand("2FBC1003");
          delay(5000);
          commInterface->executeCommand("ATSH770");
          delay(50);
          commInterface->executeCommand("3E");
          delay(50);
          commInterface->executeCommand("1003");
          delay(50);
          commInterface->executeCommand("2FBC1103");
          delay(5000);*/
        hideMenu(); return;
      // Shutdown
      case 11: shutdownDevice(); return;
      default:
        // Submenu
        liveData->menuCurrent = tmpMenuItem->id;
        liveData->menuItemSelected = 0;
        showMenu();
        return;
    }
  }

  // Exit menu
  if (liveData->menuItemSelected == 0 || (showParentMenu && parentMenu != -1)) {
    if (tmpMenuItem->parentId == 0 && tmpMenuItem->id == 0) {
      liveData->menuVisible = false;
      redrawScreen();
    } else {
      // Parent menu
      liveData->menuItemSelected = 0;
      for (i = 0; i < liveData->menuItemsCount; ++i) {
        if (parentMenu == liveData->menuItems[i].parentId) {
          if (liveData->menuItems[i].id == liveData->menuCurrent)
            break;
          liveData->menuItemSelected++;
        }
      }

      liveData->menuCurrent = parentMenu;
      syslog->println(liveData->menuCurrent);
      showMenu();
    }
    return;
  }

  // Close menu
  hideMenu();
}

/**
  Redraw screen
*/
void Board320_240::redrawScreen() {

  if (liveData->menuVisible) {
    return;
  }

  // Headlights reminders
  if (!testDataMode && liveData->settings.headlightsReminder == 1 && liveData->params.forwardDriveMode &&
      !liveData->params.headLights && !liveData->params.autoLights) {
    sprFillSprite(TFT_RED);
    sprSetFreeFont(&Orbitron_Light_32);
    sprSetTextColor(TFT_WHITE, TFT_RED);
    sprSetTextDatum(MC_DATUM);
    sprDrawString("! LIGHTS OFF !", 160, 120, GFXFF);
    displaySprite();
    return;
  }

  sprFillSprite(TFT_BLACK);

  liveData->params.displayScreenAutoMode = SCREEN_AUTO;

  // Display selected screen
  switch (liveData->params.displayScreen) {
    // 1. Auto mode = >5kpm Screen 3 - speed, other wise basic Screen2 - Main screen, if charging then Screen 5 Graph
    case SCREEN_AUTO:
      if (liveData->params.speedKmh > 5) {
        if (liveData->params.displayScreenAutoMode != SCREEN_SPEED)
          liveData->params.displayScreenAutoMode = SCREEN_SPEED;
        drawSceneSpeed();
      } else if (liveData->params.chargingOn) {
        if (liveData->params.displayScreenAutoMode != SCREEN_CHARGING)
          liveData->params.displayScreenAutoMode = SCREEN_CHARGING;
        drawSceneChargingGraph();
      } else {
        if (liveData->params.displayScreenAutoMode != SCREEN_DASH)
          liveData->params.displayScreenAutoMode = SCREEN_DASH;
        drawSceneMain();
      }
      break;
    // 2. Main screen
    case SCREEN_DASH:
      drawSceneMain();
      break;
    // 3. Big speed + kwh/100km
    case SCREEN_SPEED:
      drawSceneSpeed();
      break;
    // 4. Battery cells
    case SCREEN_CELLS:
      drawSceneBatteryCells();
      break;
    // 5. Charging graph
    case SCREEN_CHARGING:
      drawSceneChargingGraph();
      break;
    // 6. SOC10% table (CEC-CED)
    case SCREEN_SOC10:
      drawSceneSoc10Table();
      break;
    // 7. HUD
    case SCREEN_HUD:
      drawSceneHud();
      break;
  }

  // Skip following lines for HUD display mode
  if (liveData->params.displayScreen == SCREEN_HUD)
    return;

  // SDCARD recording
  /*liveData->params.sdcardRecording*/
  if (liveData->settings.sdcardEnabled == 1 && (liveData->params.queueLoopCounter & 1) == 1) {
    sprFillCircle((liveData->params.displayScreen == SCREEN_SPEED || liveData->params.displayScreenAutoMode == SCREEN_SPEED) ? 145 : 310, 10, 4, TFT_BLACK);
    sprFillCircle((liveData->params.displayScreen == SCREEN_SPEED || liveData->params.displayScreenAutoMode == SCREEN_SPEED) ? 145 : 310, 10, 3,
                   (liveData->params.sdcardInit == 1) ?
                   (liveData->params.sdcardRecording) ?
                   (strlen(liveData->params.sdcardFilename) != 0) ?
                   TFT_GREEN /* assigned filename (opsec from bms or gsm/gps timestamp */ :
                   TFT_BLUE /* recording started but waiting for data */ :
                   TFT_ORANGE /* sdcard init ready but recording not started*/ :
                   TFT_YELLOW /* failed to initialize sdcard */
                  );
  }
  // GPS state
  if (gpsHwUart != NULL && (liveData->params.displayScreen == SCREEN_SPEED || liveData->params.displayScreenAutoMode == SCREEN_SPEED)) {
    sprDrawCircle(160, 10, 5, (gps.location.isValid()) ? TFT_GREEN : TFT_RED);
    sprSetTextSize(1);
    sprSetTextColor((gps.location.isValid()) ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    sprSetTextDatum(TL_DATUM);
    sprintf(tmpStr1, "%d", liveData->params.gpsSat);
    sprDrawString(tmpStr1, 174, 2, 2);
  }

  // SIM800L status
  if (liveData->params.sim800l_enabled) {
    if (liveData->params.displayScreen == SCREEN_SPEED || liveData->params.displayScreenAutoMode == SCREEN_SPEED) {
      sprFillRect(127, 7, 7, 7,
                    (liveData->params.lastDataSent + SIM800L_SND_TIMEOUT > liveData->params.sim800l_lastOkSendTime) ?
                    (liveData->params.lastDataSent + SIM800L_SND_TIMEOUT + SIM800L_RCV_TIMEOUT > liveData->params.sim800l_lastOkReceiveTime) ?
                    TFT_GREEN   /* last request was 200 OK */ :
                    TFT_YELLOW  /* data sent but response timed out */ :
                    TFT_RED     /* failed to send data */
                  );
    } else if (liveData->params.displayScreen != SCREEN_BLANK) {
      sprFillRect(308, 0, 5, 5,
                    (liveData->params.lastDataSent + SIM800L_SND_TIMEOUT > liveData->params.sim800l_lastOkSendTime) ?
                    (liveData->params.lastDataSent + SIM800L_SND_TIMEOUT + SIM800L_RCV_TIMEOUT > liveData->params.sim800l_lastOkReceiveTime) ?
                    TFT_GREEN   /* last request was 200 OK */ :
                    TFT_YELLOW  /* data sent but response timed out */ :
                    TFT_RED     /* failed to send data */
                  );
    }
  }

  // Door status
  if (liveData->params.trunkDoorOpen)
    sprFillRect(20, 0, 320 - 40, 20, TFT_YELLOW);
  if (liveData->params.leftFrontDoorOpen)
    sprFillRect(0, 20, 20, 98, TFT_YELLOW);
  if (liveData->params.rightFrontDoorOpen)
    sprFillRect(0, 122, 20, 98, TFT_YELLOW);
  if (liveData->params.leftRearDoorOpen)
    sprFillRect(320 - 20, 20, 20, 98, TFT_YELLOW);
  if (liveData->params.rightRearDoorOpen)
    sprFillRect(320 - 20, 122, 20, 98, TFT_YELLOW);
  if (liveData->params.hoodDoorOpen)
    sprFillRect(20, 240 - 20, 320 - 40, 20, TFT_YELLOW);

  // BLE not connected
  if (!liveData->commConnected && liveData->bleConnect && liveData->tmpSettings.commType == COMM_TYPE_OBD2BLE4) {
    // Print message
    sprSetTextSize(1);
    sprSetTextColor(TFT_WHITE, TFT_BLACK);
    sprSetTextDatum(TL_DATUM);
    sprDrawString("BLE4 OBDII not connected...", 0, 180, 2);
    sprDrawString("Press middle button to menu.", 0, 200, 2);
    sprDrawString(APP_VERSION, 0, 220, 2);
  }

  displaySprite();
}

/**
  Parse test data
*/
void Board320_240::loadTestData() {

  syslog->println("Loading test data");

  testDataMode = true; // skip lights off message
  carInterface->loadTestData();
  redrawScreen();
}

/**
  Board looop
*/
void Board320_240::mainLoop() {

  ///////////////////////////////////////////////////////////////////////
  // Handle buttons
  // MIDDLE - menu select
  if (digitalRead(pinButtonMiddle) == HIGH) {
    btnMiddlePressed = false;
  } else {
    if (!btnMiddlePressed) {
      btnMiddlePressed = true;
      liveData->params.lastButtonPushedTime = liveData->params.currentTime;
      tftSetRotation(liveData->settings.displayRotation);
      if (liveData->menuVisible) {
        menuItemClick();
      } else {
        showMenu();
      }
    }
  }
  // LEFT - screen rotate, menu
  if (digitalRead((liveData->settings.displayRotation == 1) ? pinButtonRight : pinButtonLeft) == HIGH) {
    btnLeftPressed = false;
  } else {
    if (!btnLeftPressed) {
      btnLeftPressed = true;
      liveData->params.lastButtonPushedTime = liveData->params.currentTime;
      tftSetRotation(liveData->settings.displayRotation);
      // Menu handling
      if (liveData->menuVisible) {
        menuMove(false);
      } else {
        liveData->params.displayScreen++;
        if (liveData->params.displayScreen > displayScreenCount - 1)
          liveData->params.displayScreen = 0; // rotate screens
        // Turn off display on screen 0
        setBrightness((liveData->params.displayScreen == SCREEN_BLANK) ? 0 : (liveData->settings.lcdBrightness == 0) ? 100 : liveData->settings.lcdBrightness);
        redrawScreen();
      }
    }
  }
  // RIGHT - menu, debug screen rotation
  if (digitalRead((liveData->settings.displayRotation == 1) ? pinButtonLeft : pinButtonRight) == HIGH) {
    btnRightPressed = false;
  } else {
    if (!btnRightPressed) {
      btnRightPressed = true;
      liveData->params.lastButtonPushedTime = liveData->params.currentTime;
      tftSetRotation(liveData->settings.displayRotation);
      // Menu handling
      if (liveData->menuVisible) {
        menuMove(true);
      } else {
        // doAction
        if (liveData->params.displayScreen == SCREEN_SPEED) {
          liveData->params.displayScreen = SCREEN_HUD;
          tftFillScreen(TFT_BLACK);
          redrawScreen();
        }
        else if (liveData->params.displayScreen == SCREEN_HUD) {
          liveData->params.displayScreen = SCREEN_SPEED;
          redrawScreen();
        }
      }
    }
  }
  // Both left&right button (hide menu)
  if (digitalRead(pinButtonLeft) == LOW && digitalRead(pinButtonRight) == LOW) {
    hideMenu();
  }

  // GPS process
  if (gpsHwUart != NULL) {
    unsigned long start = millis();
    do {
      while (gpsHwUart->available())
        gps.encode(gpsHwUart->read());
    } while (millis() - start < 20);
    //
    syncGPS();
  }

  // SIM800L
  if (liveData->params.sim800l_enabled) {
    sim800lLoop();
  }

  // currentTime
  struct tm now;
  getLocalTime(&now, 0);
  liveData->params.currentTime = mktime(&now);

  // SD card recording
  if (liveData->params.sdcardInit && liveData->params.sdcardRecording && liveData->params.sdcardCanNotify &&
      (liveData->params.odoKm != -1 && liveData->params.socPerc != -1)) {

    //syslog->println(&now, "%y%m%d%H%M");

    // create filename
    if (liveData->params.operationTimeSec > 0 && strlen(liveData->params.sdcardFilename) == 0) {
      sprintf(liveData->params.sdcardFilename, "/%llu.json", uint64_t(liveData->params.operationTimeSec / 60));
      syslog->print("Log filename by opTimeSec: ");
      syslog->println(liveData->params.sdcardFilename);
    }
    if (liveData->params.currTimeSyncWithGps && strlen(liveData->params.sdcardFilename) < 15) {
      strftime(liveData->params.sdcardFilename, sizeof(liveData->params.sdcardFilename), "/%y%m%d%H%M.json", &now);
      syslog->print("Log filename by GPS: ");
      syslog->println(liveData->params.sdcardFilename);
    }

    // append buffer, clear buffer & notify state
    if (strlen(liveData->params.sdcardFilename) != 0) {
      liveData->params.sdcardCanNotify = false;
      File file = SD.open(liveData->params.sdcardFilename, FILE_APPEND);
      if (!file) {
        syslog->println("Failed to open file for appending");
        File file = SD.open(liveData->params.sdcardFilename, FILE_WRITE);
      }
      if (!file) {
        syslog->println("Failed to create file");
      }
      if (file) {
        syslog->println("Save buffer to SD card");
        serializeParamsToJson(file);
        file.print(",\n");
        file.close();
      }
    }
  }

  // Turn off display if Ignition is off for more than 10s, less than month (prevent sleep when gps time was synchronized)
  if (liveData->params.currentTime - liveData->params.lastIgnitionOnTime > 10
      && (liveData->params.currentTime - liveData->params.lastIgnitionOnTime < MONTH_SEC || liveData->params.lastIgnitionOnTime == 0)
      && liveData->settings.sleepModeLevel >= 1
      && liveData->params.currentTime - liveData->params.lastButtonPushedTime > 10) {
    setBrightness(0);
  } else {
    setBrightness((liveData->settings.lcdBrightness == 0) ? 100 : liveData->settings.lcdBrightness);
  }

  // Go to sleep when car is off for more than 30s and not charging (AC charger is disabled for few seconds when ignition is turned off)
  if (liveData->params.currentTime - liveData->params.lastIgnitionOnTime > 30
      && (liveData->params.currentTime - liveData->params.lastIgnitionOnTime < MONTH_SEC || liveData->params.lastIgnitionOnTime == 0)
      && !liveData->params.chargingOn
      && liveData->settings.sleepModeLevel >= 2
      && liveData->params.currentTime - liveData->params.wakeUpTime > 30
      && liveData->params.currentTime - liveData->params.lastButtonPushedTime > 10) {
    if (liveData->params.sim800l_enabled) {
      sim800lSendData();
    }
    goToSleep();
  }

  // Read data from BLE/CAN
  commInterface->mainLoop();
}

/**
   skipAdapterScan
*/
bool Board320_240::skipAdapterScan() {
  return digitalRead(pinButtonMiddle) == LOW || digitalRead(pinButtonLeft) == LOW || digitalRead(pinButtonRight) == LOW;
}

/**
   Mount sdcard
*/
bool Board320_240::sdcardMount() {

  if (liveData->params.sdcardInit) {
    syslog->print("SD card already mounted...");
    return true;
  }

  int8_t countdown = 3;
  bool SdState = false;

  while (1) {
    syslog->print("Initializing SD card...");

    /*    syslog->print(" TTGO-T4 ");
        SPIClass * hspi = new SPIClass(HSPI);
        spiSD.begin(pinSdcardSclk, pinSdcardMiso, pinSdcardMosi, pinSdcardCs); //SCK,MISO,MOSI,ss
        SdState = SD.begin(pinSdcardCs, *hspi, SPI_FREQUENCY);*/

    syslog->print(" M5STACK ");
    SdState = SD.begin(pinSdcardCs);

    if (SdState) {

      uint8_t cardType = SD.cardType();
      if (cardType == CARD_NONE) {
        syslog->println("No SD card attached");
        return false;
      }

      syslog->println("SD card found.");
      liveData->params.sdcardInit = true;

      syslog->print("SD Card Type: ");
      if (cardType == CARD_MMC) {
        syslog->println("MMC");
      } else if (cardType == CARD_SD) {
        syslog->println("SDSC");
      } else if (cardType == CARD_SDHC) {
        syslog->println("SDHC");
      } else {
        syslog->println("UNKNOWN");
      }

      uint64_t cardSize = SD.cardSize() / (1024 * 1024);
      syslog->printf("SD Card Size: %lluMB\n", cardSize);

      return true;
    }

    syslog->println("Initialization failed!");
    countdown--;
    if (countdown <= 0) {
      break;
    }
    delay(500);
  }

  return false;
}

/**
   Toggle sdcard recording
*/
void Board320_240::sdcardToggleRecording() {

  if (!liveData->params.sdcardInit)
    return;

  syslog->println("Toggle SD card recording...");
  liveData->params.sdcardRecording = !liveData->params.sdcardRecording;
  if (liveData->params.sdcardRecording) {
    liveData->params.sdcardCanNotify = true;
  } else {
    String tmpStr = "";
    tmpStr.toCharArray(liveData->params.sdcardFilename, tmpStr.length() + 1);
  }
}

/**
   Sync gps data
*/
void Board320_240::syncGPS() {

  if (gps.location.isValid()) {
    liveData->params.gpsLat = gps.location.lat();
    liveData->params.gpsLon = gps.location.lng();
    if (gps.altitude.isValid())
      liveData->params.gpsAlt = gps.altitude.meters();
  }
  if (gps.satellites.isValid()) {
    liveData->params.gpsSat = gps.satellites.value();
    //syslog->print("GPS satellites: ");
    //syslog->println(liveData->params.gpsSat);
  }
  if (!liveData->params.currTimeSyncWithGps && gps.date.isValid() && gps.time.isValid()) {
    liveData->params.currTimeSyncWithGps = true;

    struct tm tm;
    tm.tm_year = gps.date.year() - 1900;
    tm.tm_mon = gps.date.month() - 1;
    tm.tm_mday = gps.date.day();
    tm.tm_hour = gps.time.hour();
    tm.tm_min = gps.time.minute();
    tm.tm_sec = gps.time.second();
    time_t t = mktime(&tm);
    printf("%02d%02d%02d%02d%02d%02d\n", gps.date.year() - 2000, gps.date.month() - 1, gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second());
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);
  }
}

/**
  SIM800L
*/
bool Board320_240::sim800lSetup() {
  syslog->print("Setting SIM800L module. HW port: ");
  syslog->println(liveData->settings.gprsHwSerialPort);

  gprsHwUart = new HardwareSerial(liveData->settings.gprsHwSerialPort);
  gprsHwUart->begin(9600);

  sim800l = new SIM800L((Stream *)gprsHwUart, SIM800L_RST, SIM800L_INT_BUFFER , SIM800L_RCV_BUFFER);
  // SIM800L DebugMode:
  //sim800l = new SIM800L((Stream *)gprsHwUart, SIM800L_RST, SIM800L_INT_BUFFER , SIM800L_RCV_BUFFER, syslog);

  bool sim800l_ready = sim800l->isReady();
  for (uint8_t i = 0; i < 5 && !sim800l_ready; i++) {
    syslog->println("Problem to initialize SIM800L module, retry in 1 sec");
    delay(1000);
    sim800l_ready = sim800l->isReady();
  }

  if (!sim800l_ready) {
    syslog->println("Problem to initialize SIM800L module");
  } else {
    syslog->println("SIM800L module initialized");

    sim800l->exitSleepMode();

    if (sim800l->getPowerMode() != NORMAL) {
      syslog->println("SIM800L module in sleep mode - Waking up");
      if (sim800l->setPowerMode(NORMAL)) {
        syslog->println("SIM800L in normal power mode");
      } else {
        syslog->println("Failed to switch SIM800L to normal power mode");
      }
    }

    syslog->print("Setting GPRS APN to: ");
    syslog->println(liveData->settings.gprsApn);

    bool sim800l_gprs = sim800l->setupGPRS(liveData->settings.gprsApn);
    for (uint8_t i = 0; i < 5 && !sim800l_gprs; i++) {
      syslog->println("Problem to set GPRS APN, retry in 1 sec");
      delay(1000);
      sim800l_gprs = sim800l->setupGPRS(liveData->settings.gprsApn);
    }

    if (sim800l_gprs) {
      liveData->params.sim800l_enabled = true;
      syslog->println("GPRS APN set OK");
    } else {
      syslog->println("Problem to set GPRS APN");
    }
  }

  return true;
}

bool Board320_240::sim800lLoop() {
  if (liveData->params.lastDataSent + SIM800L_TIMER < liveData->params.currentTime) {
    liveData->params.lastDataSent = liveData->params.currentTime;
    sim800lSendData();
  }

  uint16_t rc = sim800l->readPostResponse(SIM800L_RCV_TIMEOUT * 1000);

  if (rc == 200) {
    syslog->print("HTTP POST OK: ");
    syslog->println(sim800l->getDataReceived());
    liveData->params.sim800l_lastOkReceiveTime = liveData->params.currentTime;
  } else if(rc != 1 && rc != 0) {
    syslog->print("HTTP POST error: ");
    syslog->println(rc);
  }
}

bool Board320_240::sim800lSendData() {
  syslog->println("Sending data via GPRS");

  if (liveData->params.socPerc < 0) {
    syslog->println("No valid data, skipping data send");
    return false;
  }

  NetworkRegistration network = sim800l->getRegistrationStatus();
  if (network != REGISTERED_HOME && network != REGISTERED_ROAMING) {
    syslog->println("SIM800L module not connected to network, skipping data send");
    return false;
  }

  if (!sim800l->isConnectedGPRS()) {
    syslog->println("GPRS not connected... Connecting");
    bool connected = sim800l->connectGPRS();
    for (uint8_t i = 0; i < 5 && !connected; i++) {
      syslog->println("Problem to connect GPRS, retry in 1 sec");
      delay(1000);
      connected = sim800l->connectGPRS();
    }
    if (connected) {
      syslog->println("GPRS connected!");
    } else {
      syslog->println("GPRS not connected!");
      sim800l->reset();
      sim800lSetup();
      return false;
    }
  }

  syslog->println("Start HTTP POST...");

  StaticJsonDocument<768> jsonData;

  jsonData["apikey"] = liveData->settings.remoteApiKey;
  jsonData["carType"] = liveData->settings.carType;
  jsonData["ignitionOn"] = liveData->params.ignitionOn;
  jsonData["chargingOn"] = liveData->params.chargingOn;
  jsonData["socPerc"] = liveData->params.socPerc;
  jsonData["sohPerc"] = liveData->params.sohPerc;
  jsonData["batPowerKw"] = liveData->params.batPowerKw;
  jsonData["batPowerAmp"] = liveData->params.batPowerAmp;
  jsonData["batVoltage"] = liveData->params.batVoltage;
  jsonData["auxVoltage"] = liveData->params.auxVoltage;
  jsonData["auxAmp"] = liveData->params.auxCurrentAmp;
  jsonData["batMinC"] = liveData->params.batMinC;
  jsonData["batMaxC"] = liveData->params.batMaxC;
  jsonData["batInletC"] = liveData->params.batInletC;
  jsonData["batFanStatus"] = liveData->params.batFanStatus;
  jsonData["speedKmh"] = liveData->params.speedKmh;
  jsonData["odoKm"] = liveData->params.odoKm;
  jsonData["cumulativeEnergyChargedKWh"] = liveData->params.cumulativeEnergyChargedKWh;
  jsonData["cumulativeEnergyDischargedKWh"] = liveData->params.cumulativeEnergyDischargedKWh;

  //Send GPS data via GPRS (if enabled)
  if (liveData->settings.gpsHwSerialPort <= 2) {
    jsonData["gpsLat"] = liveData->params.gpsLat;
    jsonData["gpsLon"] = liveData->params.gpsLon;
    jsonData["gpsAlt"] = liveData->params.gpsAlt;
  }

  char payload[768];
  serializeJson(jsonData, payload);

  syslog->print("Sending payload: ");
  syslog->println(payload);

  syslog->print("Remote API server: ");
  syslog->println(liveData->settings.remoteApiUrl);

  uint16_t rc = sim800l->doPost(liveData->settings.remoteApiUrl, "application/json", payload, SIM800L_SND_TIMEOUT * 1000);
  if (rc == 200) {
    syslog->println("HTTP POST send successful");
    liveData->params.sim800l_lastOkSendTime = liveData->params.currentTime;
  } else {
    // Failed...
    syslog->print("HTTP POST error: ");
    syslog->println(rc);
  }

  return true;
}
