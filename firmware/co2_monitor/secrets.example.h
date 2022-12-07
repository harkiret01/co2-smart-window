// Copy this file to secrets.h and fill in your own values.
// secrets.h is listed in .gitignore so your credentials never get committed.
#pragma once

// Local WiFi network
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

// Twilio credentials (https://console.twilio.com)
static const char *account_sid = "YOUR_TWILIO_ACCOUNT_SID";
static const char *auth_token = "YOUR_TWILIO_AUTH_TOKEN";

// from_number: the phone number Twilio gave you, e.g. "+15551234567"
// to_number:   your phone (must be a verified number on a Twilio trial account)
static const char *from_number = "+15551234567";
static const char *to_number = "+15557654321";
