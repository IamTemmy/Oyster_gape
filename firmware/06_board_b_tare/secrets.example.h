#pragma once
/* secrets.example.h — TEMPLATE for Board B network config.
   Copy this file to secrets.h (same folder) and fill in real values.
   secrets.h is gitignored; this template is committed so the structure is known. */

#define NET_AP   1
#define NET_STA  2
#define NET_MODE NET_AP           // NET_AP = make own network; NET_STA = join existing WiFi

// AP mode — the network Board B creates (used only if NET_MODE == NET_AP)
const char* AP_SSID  = "OysterGape-B";
const char* AP_PASS  = "changeme8";        // >= 8 chars (WPA2)

// STA mode — an existing WiFi Board B joins (used only if NET_MODE == NET_STA)
const char* STA_SSID = "your-wifi-name";
const char* STA_PASS = "your-wifi-password";
