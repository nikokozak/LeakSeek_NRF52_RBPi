// Define Manufacturer ID - there's an assigned-numbers list here:
// https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers
// 0x0059 is Nordic
// 0x018B is Konica Minolta, Inc. (This is what we're using)
#define MANUFACTURER_ID 0x018B

// Pin for Pair/Reset button
#define BUTTON_PIN 2

// #define UUID16_SVC_ALERT_NOTIFICATION                         0x1811, in BLEUuid.h
#define SERVICE_UUID UUID16_SVC_ALERT_NOTIFICATION

// #define UUID16_CHR_ALERT_STATUS                               0x2A3F
#define CHARACTERISTIC_UUID UUID16_CHR_ALERT_STATUS

