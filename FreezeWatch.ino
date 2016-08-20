#include "Adafruit_FONA.h"
#include <SoftwareSerial.h>
#include <EEPROM.h>

#define FONA_RX 4
#define FONA_TX 3
#define FONA_RST 5
#define FONA_KEY 8
#define FONA_PWR_STAT 6
#define NUM_ALERT_NUMBERS 4
int fona_avail = 0;

char alert_numbers[NUM_ALERT_NUMBERS][12] = { "", ""}; 
//int alert_num_addr = sizeof(int);  // alert_numbers go after alert_temp at addr 0
//int alert_num_addr = 1;

char replybuffer[255];

int check_interval = 30000;  //Update timer value in loop if this changes
int warning_resend_interval = 3600;
int timer = 0;

bool pwr_alert = false;
bool tmp_alert = false;
bool send_alerts = true;

#define LED_INDICATOR 13

#define TMP_SENSOR A0
float temperature = 0.0;
int alert_temperature = 5;
int alert_temp_addr = 0;

#define PWR_SENSOR A1
int pwr_reading = 0;
bool pwr_status = false;

SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

Adafruit_FONA fona = Adafruit_FONA(FONA_RST);
uint8_t type;
uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);
uint16_t battery_level = 0;

void setup() {
  while(!Serial);
  
  //Initialize EEPROM Variables
  //EEPROM.get(alert_temp_addr, alert_temperature);
  //EEPROM.get(alert_num_addr, alert_numbers);
  alert_temperature = EEPROM.read(alert_temp_addr);
  //alert_numbers = EEPROM.read(alert_num_addr);
  
  // Configure digital pins
  pinMode(LED_INDICATOR, OUTPUT);
  pinMode(FONA_KEY, OUTPUT);
  pinMode(FONA_PWR_STAT, INPUT);
  Serial.begin(115200);
  Serial.println(F("FONA basic test"));
  Serial.println(F("Initializing....(May take 3 seconds)"));
  
  // Check for Fona
  bool fona_pwr = toggle_fona();
  fonaSerial->begin(4800);
  if (! fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't find FONA"));
    while (1);
  }
    type = fona.type();
    Serial.println(F("FONA is OK"));
    // Print module IMEI number.
    char imei[15] = {0}; // MUST use a 16 character buffer for IMEI!
    uint8_t imeiLen = fona.getIMEI(imei);
    if (imeiLen > 0) {
      Serial.print(F("Module IMEI: ")); Serial.println(imei);
    }
  Serial.println(F("Setup Complete"));
}

void loop() {

  check_network_status();
  
  temperature = get_temperature();
  if (temperature <= alert_temperature) {
    if (timer == warning_resend_interval || tmp_alert == false){
      if (send_alerts == true){
        send_sms_temp_warning(temperature);
      }
      timer = 0;
      tmp_alert = true;
     }
  } else if (temperature > alert_temperature && tmp_alert == true){
    tmp_alert = false;
  }
  Serial.print(F("Temperature: "));
  Serial.println(temperature);

  pwr_status = get_pwr_status();
  if (pwr_status == false) {
    if (timer == warning_resend_interval || timer == 0 || pwr_alert == false){
      if (send_alerts == true){
        send_sms_pwr_warning();
      }
      timer = 0;
      pwr_alert = true;
    }
  } else if (pwr_status == true && pwr_alert == true) {
    pwr_alert = false;
  }
  
  Serial.print(F("Power Status: "));
  Serial.println(pwr_status);

  get_battery_level();
  if (battery_level < 30){
    send_low_bat_warning();
  }
  
  if (text_waiting()){
    Serial.println(F("Disposition some texts"));
    disposition_sms();
  }
  timer += 30;
  delay(check_interval);

}

// System Functions
bool toggle_fona() {
  digitalWrite(FONA_KEY, LOW);
  delay(2000);
  digitalWrite(FONA_KEY, HIGH);
  delay(1000);
  int stat = digitalRead(FONA_PWR_STAT);
  Serial.print(F("Fona Power Status: "));
  Serial.println(stat);
  if (stat == HIGH) {
    return true;
  } else {
    return false;
  }
}

// Sensor reading functions
float get_temperature() {
  // OK
  int tmp_reading = analogRead(TMP_SENSOR);
  float tmp_voltage = tmp_reading * 3.3;
  tmp_voltage /= 1024.0;
  float temp = (tmp_voltage - 0.5) * 100;
  return temp;
}

bool get_pwr_status() {
  // OK
  pwr_reading = analogRead(PWR_SENSOR);
  if (pwr_reading > 500 ) { 
    return true;
  } else {
    return false;
  }
}

float get_battery_level() {
  // OK
  if (! fona.getBattPercent(&battery_level)) {
    Serial.println(F("Failed to get battery level"));
  } else {
    Serial.print(F("VPct = ")); Serial.print(battery_level); Serial.println(F("%"));
  }
}

void check_network_status() {
  // OK
  uint8_t network_status = fona.getNetworkStatus();
  Serial.print(F("Network status: "));
  Serial.println(network_status);

  // Check network status
  if (network_status != 1){
    while (network_status != 1) {
      for (int i = 0; i < 4; ++i){
        // Flash 4 times if cannot connect to networks
        digitalWrite(LED_INDICATOR, HIGH);
        delay(1000);
        digitalWrite(LED_INDICATOR, LOW);
        delay(1000);
      }
      delay(5000);
      network_status = fona.getNetworkStatus();
    }
  }
  // If successfully connected, get the signal strength
  uint8_t rssi = fona.getRSSI();
  Serial.print("RSSI: ");
  Serial.println(rssi);
  if (rssi < 10) {
    while (rssi < 10) {
      for (int i = 0; i < 5; ++i){
        // Flash 5 times if signal too weak
        digitalWrite(LED_INDICATOR, HIGH);
        delay(1000);
        digitalWrite(LED_INDICATOR, LOW);
        delay(1000);
      }
      delay(5000);
      rssi = fona.getRSSI();
    }
  }
}

// Notification Functions
void send_sms(char* message) {
  // OK
  //Serial.println(message); 
  for (int i = 0; i < NUM_ALERT_NUMBERS; ++i) {  
    if (strcasecmp(alert_numbers[i], "0") != 0) {
      if (!fona.sendSMS(alert_numbers[i], message)) {
        Serial.print(F("SMS alert failed to "));
        Serial.println(alert_numbers[i]);
      } else {
        Serial.print(F("SMS alert sent to "));
        Serial.println(alert_numbers[i]);
      }
    }
  }
}

void send_sms_pwr_warning() {
  // OK
  char message[24] = "The power has gone out!";
  send_sms(message);
}

void send_sms_temp_warning(float temp) {
  // OK
  flushSerial();
  Serial.println(F("Sending temperature message"));
  char message[35] = "The temp in your home is now ";
  char tmp_msg[4] = ""; 
  dtostrf(temp,3, 1, tmp_msg);
  strcat(message, tmp_msg);
  send_sms(message);
}

void send_sms_alert_temp() {
  // OK
  flushSerial();
  Serial.println(F("Sending temperature message"));
  char message[35] = "The alert temperature is ";
  char tmp_msg[4] = ""; 
  int alert_temp = EEPROM.read(alert_temp_addr);
  dtostrf(alert_temp,3, 1, tmp_msg);
  strcat(message, tmp_msg);
  send_sms(message);
}


void send_battery_level() {
  // OK
  char message[24] = "My battery level is ";
  char tmp_msg[4] = "";
  sprintf(tmp_msg, "%d %", battery_level);
  strcat(message, tmp_msg);
  Serial.print(message);
  send_sms(message);
}

void send_low_bat_warning() {
  // OK
  char message[32] = "My battery is getting low.";
  Serial.print(message);
  send_sms(message);
}

void send_status() {
  // OK
  Serial.println(F("Sending status update"));
  float _temp = get_temperature();
  bool _pwr = get_pwr_status();
    
  char pwr_msg[4] = "";
  if (_pwr == true) {
    strcat(pwr_msg,"on");
  } else {
    strcat(pwr_msg,"OUT");
  }
  
  char tmp_msg[5] = ""; 
  dtostrf(_temp,3, 1, tmp_msg);
  
  char message[75] = "";
  strcat(message,"Hi there! Its FreezeWatch.");
  strcat(message, "\nThe temp is ");
  strcat(message, tmp_msg);
  strcat(message, " degrees.\nThe power is ");
  strcat(message, pwr_msg);

  send_sms(message);
}

// Remote programming functions
bool text_waiting() {
  // OK
  int8_t smsnum = fona.getNumSMS();
  Serial.print(F("Texts waiting: "));
  Serial.println(smsnum);
  if (smsnum >= 1) {
    return 1;
  } else {
    return 0;
  }
}

void add_alert_number(char *new_number) {
  // OK
  bool success = false;
  char msg[141] = "";
  for (int i = 0; i < NUM_ALERT_NUMBERS; ++i) {
    if (strcasecmp(alert_numbers[i], "0") == 0) {
      strcpy(alert_numbers[i], new_number);
      //EEPROM.put(alert_num_addr, alert_numbers);
      //EEPROM.write(alert_num_addr, alert_numbers);
      success = true;
      break;
    }
  }
  if (success == false) {
    strcat(msg,"Please delete a number first:\n");
    for (int i = 0; i < NUM_ALERT_NUMBERS; ++i){
      if (strcasecmp(alert_numbers[i], "0") != 0) {
        strcat(msg, alert_numbers[i]);
      }
      if (strcasecmp(alert_numbers[i+1], "0") != 0) {
        strcat(msg,"\n");
      }
    }
    send_sms(msg);
  } else {
    strcat(msg, "Number Added!");
    send_sms(msg);
  }
}

void remove_alert_number(char* del_number) {
  // OK
  bool success = false;
  char message[30] = "";
  for (int i = 0; i < NUM_ALERT_NUMBERS; ++i){
    if (strcasecmp(alert_numbers[i], del_number) == 0){
      strcpy(alert_numbers[i], "0");
      //EEPROM.put(alert_num_addr, alert_numbers);
      //EEPROM.write(alert_num_addr, alert_numbers);
      success = true;
    }
  }
  if (success == false) {
    strcat(message, "Failed to remove that number");
    send_sms(message);
  } else {
    strcat(message,"Number removed");
    send_sms(message);
  }
  
}

void send_alert_numbers(){
  // OK - Test with 4 numbers
  char message[80] = "Here are the alert numbers:\n";
  char tmp_msg[12] = ""; 
  for (int i = 0; i < NUM_ALERT_NUMBERS; ++i){
    if (strcasecmp(alert_numbers[i], "0") != 0) {
      strcat(message, alert_numbers[i]);
    }
    if (strcasecmp(alert_numbers[i+1], "0") != 0) {
      strcat(message,"\n");
    }
  }
  send_sms(message);
}

void disposition_sms() {
  // Retrieve SMS sender address/phone number.
  flushSerial();
  int8_t smsn = fona.getNumSMS();
  Serial.print(F("Dispositioning text number "));
  Serial.println(smsn);
  
  if (! fona.getSMSSender(smsn, replybuffer, 250)) {
    Serial.println(F("Reading SMS Sender Failed!"));
    return;
  }
  Serial.print(F("FROM: ")); Serial.println(replybuffer);
  
  uint16_t smslen;
  if (! fona.readSMS(smsn, replybuffer, 250, &smslen)) { // pass in buffer and max len!
    Serial.println(F("Read SMS Failed!"));
    return;
  }
  Serial.print(F("***** SMS #")); Serial.println(smsn);
  Serial.println(replybuffer);
  Serial.println(F("*****"));

  //Disposition SMS
  if (strcasecmp(replybuffer, "status") == 0) {
   send_status();
  }
  if (strcasecmp(replybuffer, "current temp") == 0) {
    send_sms_temp_warning(temperature);
  }
  if (strcasecmp(replybuffer, "battery level") == 0) {
    send_battery_level();
  }
  if (strcasecmp(replybuffer, "alert numbers") == 0) {
    send_alert_numbers();
  }  
  if (strcasecmp(replybuffer, "alert temp") == 0) {
      send_sms_alert_temp();
  }
  if (strcasecmp(replybuffer, "stop alerts") == 0) {
      send_alerts = false;
  }  
  if (strncmp(replybuffer, "add number", 10) == 0) {
    char* words = strtok(replybuffer, " ");
    while (words != 0) {
      if (isdigit(words[0])){
        add_alert_number(words);
      }
      words = strtok(0, " ");
    }
  }

  if (strncmp(replybuffer, "remove number", 13) == 0) {
    char* words = strtok(replybuffer, " ");
    while (words != 0) {
      if (isdigit(words[0])){
        remove_alert_number(words);
      }
      words = strtok(0, " ");
    }
  }
  
  if (strncmp(replybuffer, "set alert temp to", 17) == 0) {
    char* words = strtok(replybuffer, " ");
    while (words != 0) {
      if (isdigit(words[0])){
        set_alert_temperature(atoi(words));
      }
      words = strtok(0, " ");
    }
  }
  delete_sms();
}

void set_alert_temperature(int temp){
  // OK
  alert_temperature = temp;
  EEPROM.write(alert_temp_addr, alert_temperature);
  char temp_str[3] = "";
  sprintf(temp_str, "%d", alert_temperature);
  char msg[19] = "New alert temp: ";
  strcat(msg, temp_str);
  send_sms(msg); 
}

void delete_sms() {
  // OK
  int8_t smsnum = fona.getNumSMS();
  Serial.print(F("\n\rDeleting SMS #")); Serial.println(smsnum);
  if (fona.deleteSMS(smsnum)) {
    Serial.println(F("Deleted text"));
  } else {
    Serial.println(F("Couldn't delete"));
  }
}

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout) {
  // OK
  uint16_t buffidx = 0;
  boolean timeoutvalid = true;
  if (timeout == 0) timeoutvalid = false;

  while (true) {
    if (buffidx > maxbuff) {
      //Serial.println(F("SPACE"));
      break;
    }

    while (Serial.available()) {
      char c =  Serial.read();

      //Serial.print(c, HEX); Serial.print("#"); Serial.println(c);

      if (c == '\r') continue;
      if (c == 0xA) {
        if (buffidx == 0)   // the first 0x0A is ignored
          continue;

        timeout = 0;         // the second 0x0A is the end of the line
        timeoutvalid = true;
        break;
      }
      buff[buffidx] = c;
      buffidx++;
    }

    if (timeoutvalid && timeout == 0) {
      //Serial.println(F("TIMEOUT"));
      break;
    }
    delay(1);
  }
  buff[buffidx] = 0;  // null term
  return buffidx;
}

void flushSerial() {
  while (Serial.available()){
    Serial.read();
  }
}
