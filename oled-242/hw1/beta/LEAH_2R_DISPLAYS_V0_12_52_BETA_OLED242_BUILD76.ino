// ==================================================
// LEAH_2R_DISPLAYS_V0_12_52_BETA_OLED242_BUILD76
// Product-grade OLED 2.42-inch Nightscout display.
//
// Consolidated beta release:
// - v0.11.4-beta: Cleaner, warmer local webserver layout.
// - v0.11.6-beta: Basic / Advanced web view.
// - v0.12.0-beta: Removes DFR0782/Bluetooth audio path and adds Twilio WhatsApp Sandbox alerts.
// - v0.12.2-beta: Adds Sensor Data screen, manual sensor life tracking and improved Twilio status screen.
// - v0.12.3-beta: Removes web audio/mute settings, improves sensor days+hours, remaps buttons.
// - v0.12.4-beta: Adds WhatsApp provider dropdown, API/CallMeBot/TextMeBot, repeat timers and caregiver reminders.
// - v0.12.5-beta: Cleans WhatsApp web UI with hidden provider panels and selected-provider test layout.
// - v0.12.8-beta: Adds 3 stored Wi-Fi profiles, mDNS alias, glucose units, insulin bolus screen and third TextMeBot recipient.
// - v0.12.9-beta: Renames parent UI to caregivers and adds per-caregiver WhatsApp alarm selection.
// - v0.12.11-beta: Adds manual GitHub release redirect handling and clearer OTA HTTP diagnostics.
// - v0.12.12-beta: Refines OLED layout, collapsible web menus, dynamic caregiver/test panels and Wi-Fi scan/save/connect UI.
// - v0.12.15-beta: Moves OLED bottom row up, restores delta units, and keeps Current status web card open.
// - v0.12.18-beta: Restyles OLED web UI to match 4-inch layout and adds main-screen insulin/IOB summary.
// - v0.12.19-beta: Cleans main screen: smaller shifted arrows, visible delta/unit, IOB/today/age/phone/last bolus rows.
// - v0.12.25-beta: Adds clean night glucose screen during the configured dimming window.
// - v0.12.26-beta: Adds controlled random buzzer patterns, 30s/15s audio cycle and 1-30 min mute setting.
// - v0.12.32-beta: Clears audio mute when alarm state clears so a new alarm episode sounds immediately.
// - v0.12.34-beta: Adds per-alarm local sound selection and live mute controls/status to Current status.
// - v0.12.39-beta: Adds today/yesterday insulin and carbohydrate totals, administration times and COB/IOB Total screen.
// - v0.12.41-beta: Rebuilds the Insulin / bolus Web UI as Today/Yesterday treatment tables and removes duplicated summary tiles.
// - v0.12.44-beta: Corrects daily-history helper declarations and event/history count scoping for Arduino IDE preprocessing.
// - v0.12.45-beta: Restores fixed setup hotspot leah 2R - 2.42I with password leah00000000.
// - v0.12.2 compile-fix: Fixes web redirect handler names, restart/reset/logout handlers and directionText reference.
// - v0.12.46-beta: Moves WhatsApp HTTPS delivery and buzzer timing into dedicated FreeRTOS tasks; adds episode acknowledgement.
// - v0.12.47-beta: Swaps OLED I2C to SDA GPIO22/SCL GPIO21, removes the GPIO27 WhatsApp button, and re-enables the GPIO25 siren master.
// - v0.12.48-beta: Adds warm-restart OLED bus recovery, hard-low buzzer startup/shutdown, boot audio holdoff, and safe restart handling.
// - v0.12.49-beta: Adds full NVS factory reset under Advanced and hardens OTA finalization/restart with Espressif partition checks, a restart guard, and post-boot confirmation.
// - v0.12.50-beta: Corrects the physical audio control label to RIGHT button and adds deliberate 10-second OFF / 3-second ON buzzer-master holds.
// - v0.12.51-beta: Captures WiFiManager connections into the three encrypted Leah NVS profiles, retains prior networks, and verifies Wi-Fi profile persistence.
// - v0.12.52-beta: Separates Advanced-view selection from the full settings save, removes post-save HTTPS work, and adds grace-based non-destructive Wi-Fi recovery.
// - Uses Leah 2R Displays branding throughout the firmware and web UI.
// - Keeps beta release-channel behavior.
// - Keeps 4-hour trend, diagnostics, night dimming, OTA and setup features.
// - No hardcoded Nightscout credentials.
// - Twilio credentials are stored encrypted in ESP32 Preferences/NVS.
//
// Beta only. Stable release will only be made after confidence testing.

// 1. LIBRARIES
// ==================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include "mbedtls/sha256.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include <time.h>
#include <string.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>



// ==================================================
// 2. USER SETTINGS
// ==================================================

// ---------- Product / firmware identity ----------
#define PRODUCT_NAME        "Leah 2R Displays"
#define PRODUCT_MODEL       "oled-242"
#define HARDWARE_REVISION   "hw1"
#define FIRMWARE_VERSION    "0.12.52-beta"
#define BUILD_NUMBER        76

// ---------- Release channel policy ----------
#define ENABLE_BETA_CHANNEL_SELECTOR true
#define DEFAULT_UPDATE_CHANNEL "beta"

// ---------- Leah 2R Displays startup branding ----------
const unsigned long STARTUP_BRAND_MS = 5000UL;
const unsigned long STARTUP_VISION_MS = 10000UL;
#define BUILD_DATE          __DATE__ " " __TIME__

// OTA update manifests hosted on GitHub raw.
// Final URL is built from:
// <model>/<hardware_rev>/<channel>/latest.json
const char* UPDATE_BASE_URL =
"https://raw.githubusercontent.com/hermanusscholtz-wq/cgm-display-updates/main";

// ---------- Local network alias ----------
const char* DEFAULT_MDNS_NAME = "leah-2r-242";

const char* LEAH_VISION = "To transform diabetes monitoring into connected, human-centred care that brings greater safety, confidence and independence to everyday life";
const char* LEAH_MISSION = "Leah designs dependable, easy-to-use connected displays that transform continuous glucose monitoring data into clear visual information, timely alerts and meaningful caregiver awareness. Our products help families, schools and care teams remain informed and respond confidently, without replacing professional medical guidance or established care plans.";

// ---------- Nightscout ----------
// Nightscout host and token are no longer hardcoded.
// They are entered by the user through the setup portal or local webserver
// and stored locally in ESP32 Preferences/NVS.

// ---------- OLED pins ----------
#define OLED_SDA 22
#define OLED_SCL 21

// ---------- Buttons ----------
// Physical button map:
// Right  GPIO32 = alarm acknowledge/snooze and deliberate buzzer-master long press
// Middle GPIO33 = cycle OLED screens
// GPIO27 is intentionally unused. WhatsApp provider control is available only in the Web UI.
#define BUTTON_AUDIO_PIN     32
#define BUTTON_SCREEN_PIN    33

// Right audio-button hold behaviour. A short press during an alarm acknowledges
// and snoozes the local buzzer. Master OFF requires a deliberate 10-second hold;
// when already OFF, master ON requires a 3-second hold.
const unsigned long AUDIO_BUTTON_MASTER_OFF_HOLD_MS = 10000UL;
const unsigned long AUDIO_BUTTON_MASTER_ON_HOLD_MS  = 3000UL;
const unsigned long AUDIO_BUTTON_DEBOUNCE_MS        = 40UL;

// ---------- Buzzer / siren ----------
#define BUZZER_PIN 25

// GPIO25 buzzer/siren hardware is enabled for this product configuration.
#define ENABLE_SIREN_HARDWARE true

// Compile-time GPIO map validation for this hardware revision.
static_assert(OLED_SDA == 22, "OLED SDA must be GPIO22");
static_assert(OLED_SCL == 21, "OLED SCL must be GPIO21");
static_assert(BUZZER_PIN == 25, "Buzzer/siren must be GPIO25");
static_assert(OLED_SDA != OLED_SCL, "OLED SDA and SCL must use different GPIOs");
static_assert(BUZZER_PIN != OLED_SDA && BUZZER_PIN != OLED_SCL,
              "Buzzer GPIO must not conflict with OLED I2C GPIOs");
static_assert(BUTTON_AUDIO_PIN != BUTTON_SCREEN_PIN,
              "Physical button GPIOs must be unique");

// Alarm-specific macro timing is handled by the dedicated audio task.
// Pulse-level on/off timing remains inside each alarm pattern.
const unsigned long AUDIO_ACTIVE_WINDOW_MS = 30000UL; // Low/high default active window.
const unsigned long AUDIO_QUIET_WINDOW_MS  = 15000UL; // Low/high default fatigue pause.
const uint8_t AUDIO_PATTERN_VARIANTS = 50;
const int AUDIO_ALARM_NO_DATA = 5;
const uint16_t ALARM_AUDIO_TASK_PERIOD_MS = 15;
// Prevent an active alarm from driving GPIO25 until the OLED has been recovered,
// the first live screen has been drawn, and the supply has had time to settle.
const unsigned long ALARM_AUDIO_BOOT_HOLDOFF_MS = 3000UL;
const uint16_t TEXTMEBOT_RECIPIENT_GAP_MS = 10000;
const uint8_t CLOUD_ALERT_QUEUE_LENGTH = 12;
const uint32_t OTA_RESTART_GUARD_DELAY_MS = 4000UL;
const char* OTA_STATE_NAMESPACE = "ota_state";
const char* OTA_PENDING_VERSION_KEY = "pendingVer";

// ---------- Timing ----------
const unsigned long glucoseUpdateInterval = 5000;       // Nightscout glucose read every 5 seconds
const unsigned long batteryUpdateInterval = 300000;     // Phone battery read every 5 minutes
const unsigned long insulinUpdateInterval = 300000;     // Nightscout treatments/bolus read every 5 minutes
const unsigned long sageUpdateInterval    = 1800000;    // Nightscout SAGE property read every 30 minutes
const unsigned long screenTimeout         = 20000;      // Trend/limits screen timeout
// Mute/snooze duration is now configurable from the local webserver.

// ---------- Data age ----------
const long maxDataAgeMinutes = 5;

// ---------- Fallback alarm limits in mmol/L ----------
float urgentLowLimitMmol  = 3.0;
float lowLimitMmol        = 3.9;
float highLimitMmol       = 10.0;
float urgentHighLimitMmol = 11.0;

// ==================================================
// 2B. USER CONFIGURATION STORED IN NVS
// ==================================================

struct DeviceConfig {
  String deviceName = "Leah 2R Displays";
  String patientName = "Leah";
  String deviceLocation = "Bedroom";
  // User-defined mDNS hostname, stored without the .local suffix.
  String localAliasName = "";

  String nightscoutHost = "";
  String nightscoutToken = "";

  // Up to three preferred Wi-Fi networks saved in this firmware namespace.
  // Passwords are encrypted before being stored in Preferences/NVS.
  String wifiSsid1 = "";
  String wifiPass1 = "";
  String wifiSsid2 = "";
  String wifiPass2 = "";
  String wifiSsid3 = "";
  String wifiPass3 = "";

  // Display unit. Alarm math remains in mmol/L internally.
  String glucoseUnits = "MMOL";   // MMOL or MGDL

  // Insulin/bolus display. The remaining calculation is a simple linear
  // estimate over this action window for awareness only.
  float insulinActionHours = 4.0;

  String adminUsername = "admin";
  String adminPassword = "";

  bool useNightscoutLimits = false;

  // Sensor information.
  // Nightscout commonly exposes uploader/source strings, but sensor serial and remaining days
  // are not reliably available for every CGM source. These fields allow reliable local tracking.
  bool sensorAutoRead = true;         // Auto=Nightscout only; Manual=local configuration only.
  String sensorSource = "Auto";       // Auto or a manual CGM/source label.
  String sensorSerial = "";           // Optional. Stored encrypted in NVS.
  String sensorStartDate = "";        // YYYY-MM-DD, local/manual entry.
  String sensorStartTime = "00:00";   // HH:MM, local/manual entry.
  uint8_t sensorWearDays = 10;         // Dexcom normally 10, Libre normally 14.

  float urgentLow = 3.0;
  float low = 3.9;
  float high = 10.0;
  float urgentHigh = 11.0;

  uint16_t muteLowMinutes = 10;
  uint16_t muteHighMinutes = 20;
  uint16_t muteNoDataMinutes = 10;

  bool alarmSoundEnabled = true;
  bool randomAudioEnabled = true;       // Controlled seeded variation in buzzer rhythm.
  uint16_t audioMuteMinutes = 10;       // Global audio mute duration, clamped 1-30 minutes.

  // Local buzzer routing by alarm class. Visual and WhatsApp alarms are independent.
  bool soundUrgentLow = true;
  bool soundLow = false;
  bool soundHigh = false;
  bool soundUrgentHigh = true;
  bool soundNoData = false;

  uint8_t displayContrast = 210;        // Day contrast

  bool nightDimEnabled = true;
  uint8_t nightContrast = 120;          // Night contrast
  uint8_t dimStartHour = 21;
  uint8_t dimStartMinute = 0;
  uint8_t dimEndHour = 7;
  uint8_t dimEndMinute = 0;

  // WhatsApp alert provider selection.
  // OFF = no cloud WhatsApp messages.
  // API = generic custom HTTP API/webhook.
  // TWILIO = Twilio WhatsApp.
  // CALLMEBOT = CallMeBot personal WhatsApp.
  // TEXTMEBOT = TextMeBot own-number WhatsApp.
  String alertProvider = "OFF";

  // Generic API / webhook provider.
  // Endpoint receives GET parameters: recipient, phone, text and apikey.
  String apiEndpoint = "";
  String apiKey = "";
  String apiParent1 = "";
  String apiParent2 = "";

  // Twilio WhatsApp Sandbox alert testing.
  bool twilioEnabled = false;
  String twilioSid = "";
  String twilioToken = "";
  String twilioFrom = "whatsapp:+14155238886";
  String twilioParent1 = "";
  String twilioParent2 = "";

  // CallMeBot personal WhatsApp provider.
  // Each recipient must activate CallMeBot and receive their own API key.
  bool callMeBotEnabled = false;
  String callMeBotParent1 = "";
  String callMeBotApiKey1 = "";
  String callMeBotParent2 = "";
  String callMeBotApiKey2 = "";

  // TextMeBot own-number WhatsApp provider.
  // One API key is linked to the sender phone using TextMeBot addphone setup.
  bool textMeBotEnabled = false;
  String textMeBotApiKey = "";
  String textMeBotParent1 = "";
  String textMeBotParent2 = "";
  String textMeBotParent3 = "";

  // Caregiver labels and WhatsApp event routing.
  // These are provider-independent. Caregiver 3 is used by TextMeBot only.
  String caregiver1Name = "Caregiver 1";
  String caregiver2Name = "Caregiver 2";
  String caregiver3Name = "Caregiver 3";

  bool cg1Low = true;
  bool cg1UrgentLow = true;
  bool cg1High = true;
  bool cg1UrgentHigh = true;
  bool cg1NoData = true;

  bool cg2Low = true;
  bool cg2UrgentLow = true;
  bool cg2High = true;
  bool cg2UrgentHigh = true;
  bool cg2NoData = true;

  bool cg3Low = true;
  bool cg3UrgentLow = true;
  bool cg3High = true;
  bool cg3UrgentHigh = true;
  bool cg3NoData = true;

  // WhatsApp repeat intervals in minutes.
  uint16_t lowRepeatMinutes = 15;
  uint16_t urgentLowRepeatMinutes = 5;
  uint16_t highRepeatMinutes = 30;
  uint16_t urgentHighRepeatMinutes = 15;
  uint16_t noDataRepeatMinutes = 30;

  // Extra caregiver reminders.
  bool phoneBatteryAlertEnabled = true;
  uint8_t phoneBatteryAlertPercent = 10;
  uint16_t phoneBatteryRepeatMinutes = 60;

  bool sensorExpiryReminderEnabled = true;
  uint8_t sensorExpiryReminderDays = 3;
  uint16_t sensorExpiryRepeatHours = 24;

  // Web interface view.
  // false = friendly/basic user view, true = advanced technical view.
  bool advancedWebView = false;

  // OTA release channel.
  // Stable is used for commercial/user devices.
  // Beta can only be selected when ENABLE_BETA_CHANNEL_SELECTOR is true.
  String updateChannel = DEFAULT_UPDATE_CHANNEL;
};

DeviceConfig appConfig;
Preferences preferences;
WebServer webServer(80);

String sanitizeLocalAliasName(String value) {
  value.trim();
  value.toLowerCase();

  if (value.startsWith("http://")) value.remove(0, 7);
  if (value.startsWith("https://")) value.remove(0, 8);

  int slashIndex = value.indexOf('/');
  if (slashIndex >= 0) value = value.substring(0, slashIndex);

  if (value.endsWith(".local")) {
    value.remove(value.length() - 6);
  }

  String cleaned = "";
  cleaned.reserve(64);
  bool lastWasDash = false;

  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    bool isAlphaNumeric = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');

    if (isAlphaNumeric) {
      cleaned += c;
      lastWasDash = false;
    } else if (c == '-' || c == '_' || c == ' ' || c == '.') {
      if (cleaned.length() > 0 && !lastWasDash) {
        cleaned += '-';
        lastWasDash = true;
      }
    }

    if (cleaned.length() >= 63) break;
  }

  while (cleaned.endsWith("-")) cleaned.remove(cleaned.length() - 1);
  while (cleaned.startsWith("-")) cleaned.remove(0, 1);

  return cleaned;
}

String getLocalMdnsName() {
  String cleaned = sanitizeLocalAliasName(appConfig.localAliasName);
  if (cleaned.length() == 0) cleaned = DEFAULT_MDNS_NAME;
  return cleaned;
}

String getLocalMdnsHost() {
  return getLocalMdnsName() + ".local";
}

unsigned long activeSilenceDurationMs = 10UL * 60UL * 1000UL;
bool webServerStarted = false;
int currentAppliedContrast = -1;

// Protect a healthy station connection from transient status changes during
// NVS writes, Web UI saves, mDNS changes, and short Wi-Fi driver disturbances.
unsigned long wifiRecoveryHoldoffUntilMs = 0;
unsigned long wifiDisconnectedSinceMs = 0;
unsigned long lastWiFiSoftReconnectAttemptMs = 0;
unsigned long lastWiFiProfileRecoveryAttemptMs = 0;
unsigned long scheduledSageRefreshAtMs = 0;
String lastWiFiRecoveryAction = "None";

const unsigned long WIFI_SAVE_RECOVERY_HOLDOFF_MS = 15000UL;
const unsigned long WIFI_TRANSIENT_GRACE_MS = 5000UL;
const unsigned long WIFI_PROFILE_RECOVERY_DELAY_MS = 30000UL;
const unsigned long WIFI_SOFT_RECONNECT_INTERVAL_MS = 5000UL;
const unsigned long WIFI_PROFILE_RECOVERY_INTERVAL_MS = 60000UL;

// Twilio WhatsApp alert state.
String twilioLastStatus = "No WhatsApp alert sent yet";
String twilioLastResponse = "";
int twilioLastHttpCode = 0;
String twilioCurrentEventKey = "NONE";
unsigned long twilioLastEventSendMs = 0;
String twilioLastEvent = "None";
volatile bool twilioSendInProgress = false;

// CallMeBot WhatsApp alert state.
String callMeBotLastStatus = "No CallMeBot alert sent yet";
String callMeBotLastResponse = "";
int callMeBotLastHttpCode = 0;
String callMeBotCurrentEventKey = "NONE";
unsigned long callMeBotLastEventSendMs = 0;
String callMeBotLastEvent = "None";
volatile bool callMeBotSendInProgress = false;

// TextMeBot WhatsApp alert state.
String textMeBotLastStatus = "No TextMeBot alert sent yet";
String textMeBotLastResponse = "";
int textMeBotLastHttpCode = 0;
String textMeBotCurrentEventKey = "NONE";
unsigned long textMeBotLastEventSendMs = 0;
String textMeBotLastEvent = "None";
volatile bool textMeBotSendInProgress = false;

// Generic API / webhook alert state.
String apiLastStatus = "No API alert sent yet";
String apiLastResponse = "";
int apiLastHttpCode = 0;
String apiCurrentEventKey = "NONE";
unsigned long apiLastEventSendMs = 0;
String apiLastEvent = "None";
volatile bool apiSendInProgress = false;

// Selected provider scheduler state.
String selectedAlertCurrentEventKey = "NONE";
unsigned long selectedAlertLastEventSendMs = 0;
String phoneBatteryReminderKey = "NONE";
unsigned long phoneBatteryLastAlertSendMs = 0;
String sensorExpiryReminderKey = "NONE";
unsigned long sensorExpiryLastAlertSendMs = 0;

// Alarm episode acknowledgement. The first cloud alert for an episode is still queued;
// acknowledgement suppresses repeat cloud alerts for that same episode.
String currentCloudAlarmEpisodeKey = "NONE";
uint32_t currentCloudAlarmEpisodeId = 0;
uint32_t acknowledgedCloudAlarmEpisodeId = 0;
uint32_t selectedAlertLastQueuedEpisodeId = 0;

// Background task and queue diagnostics.
struct CloudAlertJob {
  char provider[16];       // SELECTED, API, TWILIO, CALLMEBOT or TEXTMEBOT.
  char target[8];          // all, both, p1, p2 or p3.
  char eventKey[28];
  char message[768];       // Event snapshot built when the job is queued.
  bool testMode;
  uint32_t episodeId;
};

QueueHandle_t cloudAlertQueue = nullptr;
TaskHandle_t cloudAlertTaskHandle = nullptr;
TaskHandle_t alarmAudioTaskHandle = nullptr;
TaskHandle_t otaRestartGuardTaskHandle = nullptr;
RTC_DATA_ATTR uint32_t factoryResetBootMarker = 0;
const uint32_t FACTORY_RESET_BOOT_MAGIC = 0x4C324652UL; // "L2FR"
volatile uint32_t cloudAlertQueueDropCount = 0;
volatile uint32_t cloudAlertTaskHeartbeat = 0;
volatile uint32_t alarmAudioTaskHeartbeat = 0;
volatile uint32_t lastCloudAlertSendDurationMs = 0;
volatile uint32_t mainLoopMaxDelayMs = 0;
unsigned long lastMainLoopPassMs = 0;

// Peripheral startup/restart interlocks. These stop a warm restart from leaving
// the passive buzzer PWM or the software-I2C OLED in an undefined state.
volatile bool peripheralRestartInProgress = false;
volatile bool oledRuntimeReady = false;
volatile bool alarmAudioArmed = false;
volatile unsigned long alarmAudioArmAtMs = 0;

// Explicit prototypes for restart/NVS helpers. Arduino IDE 1.8.x can place
// generated prototypes before custom declarations, so these are kept explicit.
void otaRestartGuardTask(void *parameter);
bool startOtaRestartGuard();
[[noreturn]] void restartImmediatelyWithEspIdf(const char *reason);
void storePendingOtaBootConfirmation(const String &targetVersion);
void showPendingOtaBootConfirmation();
void handleWebFactoryReset();


// ==================================================
// 3A. OTA RELEASE CHANNEL HELPERS
// ==================================================

String normalizeUpdateChannel(String channel) {
  channel.toLowerCase();
  channel.trim();

  // Stable/commercial firmware is locked to Stable.
  // Browser-posted beta values are ignored unless this firmware was
  // deliberately compiled as a beta-selector build.
  if (!ENABLE_BETA_CHANNEL_SELECTOR) {
    return "stable";
  }

  if (channel == "beta") {
    return "beta";
  }

  return "stable";
}

String getUpdateChannelLabel() {
  if (normalizeUpdateChannel(appConfig.updateChannel) == "beta") {
    return "Beta";
  }

  return "Stable";
}

String getUpdateChannelShort() {
  if (normalizeUpdateChannel(appConfig.updateChannel) == "beta") {
    return "BETA";
  }

  return "STBL";
}

String getUpdateChannelPolicyText() {
  if (ENABLE_BETA_CHANNEL_SELECTOR) {
    return "Beta selector build";
  }

  return "Stable locked";
}

String getUpdateManifestUrl() {
  String url = String(UPDATE_BASE_URL);
  url += "/";
  url += PRODUCT_MODEL;
  url += "/";
  url += HARDWARE_REVISION;
  url += "/";
  url += normalizeUpdateChannel(appConfig.updateChannel);
  url += "/latest.json";
  return url;
}


// ==================================================
// 3. VISUAL ALARM SETTINGS
// ==================================================

// Keep these as enum constants, but use int in function signatures.
// This avoids Arduino IDE 1.8.x auto-prototype errors with custom return types.
enum AlarmLevel {
  ALARM_NONE,
  ALARM_LOW,
  ALARM_URGENT_LOW,
  ALARM_HIGH,
  ALARM_URGENT_HIGH
};


// ==================================================
// 3B. WEB UI AND VOICE-SELECTION HELPERS
// ==================================================

String getStatusBadge(String label, String state, String cssClass) {
  return "<span class='badge " + cssClass + "'>" + htmlEscape(label) + ": " + htmlEscape(state) + "</span>";
}

String yesNo(bool value) {
  return value ? "Yes" : "No";
}

String onOff(bool value) {
  return value ? "On" : "Off";
}

String normalizeAlertProvider(String provider) {
  provider.trim();
  provider.toUpperCase();
  provider.replace(" ", "");
  provider.replace("-", "");
  provider.replace("_", "");

  if (provider == "TWILIO") return "TWILIO";
  if (provider == "CALLMEBOT") return "CALLMEBOT";
  if (provider == "TEXTMEBOT") return "TEXTMEBOT";
  if (provider == "API" || provider == "GENERICAPI" || provider == "WEBHOOK") return "API";
  if (provider == "OFF" || provider == "NONE" || provider == "DISABLED") return "OFF";

  return "OFF";
}

String getAlertProviderLabel(String provider) {
  provider = normalizeAlertProvider(provider);
  if (provider == "TWILIO") return "Twilio";
  if (provider == "CALLMEBOT") return "CallMeBot";
  if (provider == "TEXTMEBOT") return "TextMeBot";
  if (provider == "API") return "Generic API";
  return "Off";
}

void applyAlertProviderToEnabledFlags() {
  appConfig.alertProvider = normalizeAlertProvider(appConfig.alertProvider);
  appConfig.twilioEnabled = appConfig.alertProvider == "TWILIO";
  appConfig.callMeBotEnabled = appConfig.alertProvider == "CALLMEBOT";
  appConfig.textMeBotEnabled = appConfig.alertProvider == "TEXTMEBOT";
}

String alertProviderOption(String value, String label) {
  String html = "<option value='" + value + "'";
  if (normalizeAlertProvider(appConfig.alertProvider) == normalizeAlertProvider(value)) html += " selected";
  html += ">" + htmlEscape(label) + "</option>";
  return html;
}

bool isAnyWhatsAppProviderSelected() {
  return normalizeAlertProvider(appConfig.alertProvider) != "OFF";
}



// Low events flash the glucose value.
// High events flash the border.
// Optimised so the display never appears blank for several seconds.

// Low < 3.9 mmol/L:
// Blink once per second. The glucose value is hidden only briefly.
const unsigned long lowValueBlinkCycle      = 1000;  // 1 second
const unsigned long lowValueBlinkOffTime    = 250;   // hidden for 250 ms, visible for 750 ms

// Urgent low <= 3.0 mmol/L:
// Faster blink, but still avoids a long blank display.
const unsigned long urgentLowBlinkCycle     = 600;   // faster cycle
const unsigned long urgentLowBlinkOffTime   = 250;   // hidden briefly

// High >= 10.0 mmol/L:
// Heavy border pulses every 1 second.
const unsigned long highBorderFlashInterval       = 1000;

// Urgent high >= configured urgent high limit:
// Heavy border pulses every 0.5 seconds.
const unsigned long urgentHighBorderFlashInterval = 500;

// Border visible time during high/urgent-high pulse.
const unsigned long borderFlashOnTime = 250;


// ==================================================
// 4. OLED DISPLAY OBJECT
// ==================================================

U8G2_SSD1309_128X64_NONAME0_F_SW_I2C u8g2(
  U8G2_R0,
  OLED_SCL,
  OLED_SDA,
  U8X8_PIN_NONE
);

// If your OLED is blank, comment the above and use this:
// U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(
//   U8G2_R0,
//   OLED_SCL,
//   OLED_SDA,
//   U8X8_PIN_NONE
// );


// ==================================================
// 5. GLOBAL VARIABLES
// ==================================================

int glucoseMgdl = 0;
int previousMgdl = 0;

float glucoseMmol = 0.0;
float deltaMmol = 0.0;

String directionText = "";

long ageMinutes = -1;
long long latestEntryDateMs = 0;

int phoneBattery = -1;

const uint8_t MAX_DAILY_ADMIN_EVENTS = 20;
const uint8_t MAX_DAILY_HISTORY_ROWS = 48;

struct DailyTreatmentRow {
  long long timeMs;
  float insulinUnits;
  float carbsGrams;
};

DailyTreatmentRow todayTreatmentRows[MAX_DAILY_HISTORY_ROWS];
uint8_t todayTreatmentRowCount = 0;
DailyTreatmentRow yesterdayTreatmentRows[MAX_DAILY_HISTORY_ROWS];
uint8_t yesterdayTreatmentRowCount = 0;

float lastBolusUnits = 0.0;
float insulinRemainingUnits = 0.0;
float todayBolusUnits = 0.0;
uint8_t todayBolusCount = 0;
float todayCarbsGrams = 0.0;
uint8_t todayCarbCount = 0;
float yesterdayBolusUnits = 0.0;
uint8_t yesterdayBolusCount = 0;
float yesterdayCarbsGrams = 0.0;
uint8_t yesterdayCarbCount = 0;
String todayBolusAdministrationText = "None";
String todayCarbAdministrationText = "None";
String dailyTotalsStatus = "Not checked";

// Nightscout API v2 /properties/iob,cob direct values.
// These are preferred for display when available; treatment parsing remains the fallback
// and continues to supply dose-today and last-bolus details.
float nightscoutIobUnits = 0.0;
float nightscoutIobActivity = 0.0;
bool nightscoutIobValid = false;
float nightscoutCobGrams = 0.0;
float lastCarbsGrams = 0.0;
long long lastCarbsTimeMs = 0;
bool nightscoutCobValid = false;
String iobCobStatus = "Not checked";
unsigned long lastIobCobReadMs = 0;

long long lastBolusTimeMs = 0;
String lastBolusType = "None";
String insulinStatus = "Not checked";
unsigned long lastInsulinUpdate = 0;

String lastEntryDevice = "";
String lastEntryType = "";
String lastDeviceStatusDevice = "";
String detectedCgmSource = "Unknown";
String sensorInfoStatus = "Not checked";

String nightscoutSensorSource = "";
String nightscoutSensorSerial = "";
long long nightscoutSensorStartMs = 0;
uint8_t nightscoutSensorWearDays = 0;

String nightscoutSageDisplay = "";
String nightscoutSageDisplayLong = "";
String nightscoutSageSource = "";
long long nightscoutSageTreatmentMs = 0;
int nightscoutSageAgeHours = -1;  // Direct age reported by /api/v2/properties/sage.
int nightscoutSageLevel = -1;
String sageStatus = "Not checked";
unsigned long lastSageUpdate = 0;

unsigned long lastGlucoseUpdate = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastScreenInteraction = 0;

volatile bool sirenEnabled = true;
volatile bool sirenSilenced = false;
// Mute is tied to the current alarm episode only.
// When the glucose/no-data alarm clears, this is reset so a new alarm can sound immediately.
int mutedAudioAlarmType = ALARM_NONE;
unsigned long silenceStartTime = 0;

bool buzzerIsOn = false;
unsigned long lastBuzzerToggle = 0;
volatile int activeAudioAlarmType = ALARM_NONE;
uint32_t audioAlarmEpisode = 0;
uint8_t activeAudioVariant = 0;
unsigned long audioCycleStartMs = 0;

String lastStatus = "Starting";


// ==================================================
// 6. SYSTEM HEALTH / HEARTBEAT VARIABLES
// ==================================================

unsigned long lastHeartbeat = 0;
unsigned long lastNoDataDisplayRefresh = 0;
unsigned long lastMainDisplayRefresh = 0;
unsigned long lastSuccessfulNightscoutRead = 0;

// ==================================================
// 6B. FIRMWARE UPDATE STATUS
// ==================================================

bool updateAvailable = false;
String availableFirmwareVersion = "";
int availableFirmwareBuild = 0;
String availableFirmwareNotes = "";
String availableFirmwareUrl = "";
String availableFirmwareSha256 = "";
size_t availableFirmwareSize = 0;
bool availableFirmwareCritical = false;
bool availableFirmwareMandatory = false;
String availableFirmwareChannel = "";
bool otaInProgress = false;
String lastOtaStatus = "Idle";
unsigned long lastFirmwareCheck = 0;

// Network protection and recovery.
// v0.12.6-beta protects Nightscout polling from long WhatsApp-provider HTTPS calls.
uint8_t consecutiveNightscoutFailures = 0;
unsigned long lastNightscoutRecoveryMs = 0;
const uint8_t NIGHTSCOUT_FAILS_BEFORE_WIFI_RECOVERY = 6;
const unsigned long NIGHTSCOUT_WIFI_RECOVERY_COOLDOWN_MS = 120000UL;

volatile bool cloudAlertSendInProgress = false;
volatile unsigned long lastCloudAlertAttemptMs = 0;
const unsigned long CLOUD_ALERT_MIN_GAP_MS = 60000UL;
const unsigned long PROVIDER_HTTP_TIMEOUT_MS = 7000UL;
const unsigned long PROVIDER_RESPONSE_READ_TIMEOUT_MS = 1500UL;



// ==================================================
// 7. SCREEN MODES
// ==================================================

enum ScreenMode {
  SCREEN_MAIN,
  SCREEN_TREND,
  SCREEN_DIAGNOSTICS,
  SCREEN_SENSOR_DATA,
  SCREEN_INSULIN,
  SCREEN_COB_IOB_TOTAL,
  SCREEN_TWILIO
};

ScreenMode currentScreen = SCREEN_MAIN;


void drawSensorDataScreen();
void drawInsulinScreen();
void drawCobIobTotalScreen();
void drawTwilioScreen();
bool readNightscoutBolusInfo();
bool readNightscoutSageInfo();
void updateInsulinRemainingEstimate();
String getBolusAgeText();
String formatTimestampTimeOnly(long long timestampMs);
long long getStartOfTodayLocalMs();
long long getStartOfYesterdayLocalMs();
long long getEndOfTodayLocalMs();
String formatUtcIsoFromMs(long long timestampMs);

// Explicit declarations for the daily treatment/history helpers.
// Keeping these here avoids Arduino IDE auto-prototype ordering problems.
void addDailyAdministrationEvent(long long times[], float amounts[], uint8_t &eventCount,
                                 long long eventTime, float amount);
void sortDailyAdministrationEvents(long long times[], float amounts[], uint8_t eventCount);
String formatDailyAdministrationEvents(long long times[], float amounts[], uint8_t eventCount,
                                       const String &unit, uint8_t decimals);
void addDailyTreatmentRow(bool useTodayRows,
                          long long eventTime, float insulinUnits, float carbsGrams);
void sortDailyTreatmentRowsNewestFirst(bool useTodayRows);
String formatHistoryDay(long long timestampMs);
String formatHistoryTime(long long timestampMs);
String buildDailyTreatmentTableHtml(const String &title, bool useTodayRows,
                                    float insulinTotal, float carbTotal);
void updateCloudAlarmEpisodeTracker();
bool isCurrentCloudAlarmEpisodeAcknowledged();
bool selectedProviderBusy();
bool queueCloudAlertJob(const String &provider, const String &target,
                        const String &eventKey, bool testMode, uint32_t episodeId);
bool sendTwilioMessageToConfiguredParents(String eventKey, const String &message);
bool sendCallMeBotMessageToConfiguredParents(String eventKey, const String &message);
bool sendTextMeBotMessageToConfiguredParents(String eventKey, const String &message);
bool sendApiMessageToConfiguredParents(String eventKey, const String &message);
bool sendSelectedProviderToConfiguredParentsNow(String eventKey, bool testMode);
void sendSelectedProviderTestToTarget(String target, String eventKey);
void cloudAlertWorkerTask(void *parameter);
void alarmAudioWorkerTask(void *parameter);
void startBackgroundTasks();
void getAlarmMacroWindow(int alarmType, unsigned long &activeWindowMs,
                         unsigned long &quietWindowMs);
void recoverOledSoftwareI2cBus();
void initializeOledSafely();
void forceBuzzerHardwareOff();
void preparePeripheralsForRestart();
void restartDeviceSafely();

String getScreenModeText() {
  if (currentScreen == SCREEN_MAIN) return "Main";
  if (currentScreen == SCREEN_TREND) return "Trend 4h";
  if (currentScreen == SCREEN_DIAGNOSTICS) return "Diagnostics";
  if (currentScreen == SCREEN_SENSOR_DATA) return "Sensor Data";
  if (currentScreen == SCREEN_INSULIN) return "Insulin";
  if (currentScreen == SCREEN_COB_IOB_TOTAL) return "COB/IOB Total";
  if (currentScreen == SCREEN_TWILIO) return "WhatsApp";
  return "Unknown";
}


String normalizeSensorSource(String src) {
  src.trim();
  if (src.length() == 0) return "Auto";
  return src;
}

String normalizeSensorStartTime(String value) {
  value.trim();

  if (value.length() < 4) {
    return "00:00";
  }

  value.replace(".", ":");
  value.replace("-", ":");

  int h = 0;
  int m = 0;
  int colon = value.indexOf(":");

  if (colon > 0) {
    h = value.substring(0, colon).toInt();
    m = value.substring(colon + 1).toInt();
  } else {
    h = value.substring(0, 2).toInt();
    m = value.substring(2).toInt();
  }

  h = constrain(h, 0, 23);
  m = constrain(m, 0, 59);

  String result = "";
  if (h < 10) result += "0";
  result += String(h);
  result += ":";
  if (m < 10) result += "0";
  result += String(m);

  return result;
}

String detectCgmSourceFromText(String text) {
  String t = text;
  t.toUpperCase();

  if (appConfig.sensorSource.length() > 0 && appConfig.sensorSource != "Auto") {
    return appConfig.sensorSource;
  }

  if (t.indexOf("DEXCOM") >= 0 || t.indexOf("DEX") >= 0 || t.indexOf("SHARE") >= 0 || t.indexOf("G6") >= 0 || t.indexOf("G7") >= 0) {
    if (t.indexOf("G7") >= 0) return "Dexcom G7";
    if (t.indexOf("G6") >= 0) return "Dexcom G6";
    return "Dexcom";
  }

  if (t.indexOf("LIBRE") >= 0 || t.indexOf("LIMITTER") >= 0 || t.indexOf("JUGGLUCO") >= 0) {
    if (t.indexOf("LIBRE 3") >= 0 || t.indexOf("LIBRE3") >= 0) return "Libre 3";
    if (t.indexOf("LIBRE 2") >= 0 || t.indexOf("LIBRE2") >= 0) return "Libre 2";
    return "Libre";
  }

  if (t.indexOf("XDRIP") >= 0 || t.indexOf("XDRIP+") >= 0) return "xDrip+";
  if (t.indexOf("NIGHTSCOUT") >= 0) return "Nightscout";

  return "Unknown";
}

void updateDetectedCgmSource() {
  // Manual mode is fully local. Do not mix uploader/device strings into the
  // displayed sensor source when the user has selected manual configuration.
  if (!appConfig.sensorAutoRead) {
    detectedCgmSource = (appConfig.sensorSource.length() > 0 && appConfig.sensorSource != "Auto")
      ? appConfig.sensorSource
      : "Manual";
    sensorInfoStatus = "Manual local";
    return;
  }

  String combined = nightscoutSensorSource + " " + lastEntryDevice + " " + lastEntryType + " " + lastDeviceStatusDevice;
  detectedCgmSource = detectCgmSourceFromText(combined);

  if (nightscoutSageSource.length() > 0 || nightscoutSensorStartMs > 0 || nightscoutSageAgeHours >= 0) {
    sensorInfoStatus = "Nightscout auto";
  } else if (lastEntryDevice.length() > 0 || lastDeviceStatusDevice.length() > 0) {
    sensorInfoStatus = "Nightscout waiting";
  } else {
    sensorInfoStatus = "Nightscout unavailable";
  }
}

bool isLeapYearInt(int y) {
  return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
}

int daysBeforeMonth(int y, int m) {
  static const int daysNorm[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  if (m < 1 || m > 12) return 0;
  int d = daysNorm[m - 1];
  if (m > 2 && isLeapYearInt(y)) d++;
  return d;
}

long daysFromCivil(int y, int m, int d) {
  // Howard Hinnant style days from civil, epoch 1970-01-01.
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + (long)doe - 719468L;
}

bool parseYyyyMmDd(String s, int &y, int &m, int &d) {
  s.trim();
  if (s.length() != 10) return false;
  if (s.charAt(4) != '-' || s.charAt(7) != '-') return false;
  y = s.substring(0, 4).toInt();
  m = s.substring(5, 7).toInt();
  d = s.substring(8, 10).toInt();
  if (y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  int maxD = 31;
  if (m == 4 || m == 6 || m == 9 || m == 11) maxD = 30;
  if (m == 2) maxD = isLeapYearInt(y) ? 29 : 28;
  return d <= maxD;
}

bool parseHhMm(String s, int &h, int &m) {
  s = normalizeSensorStartTime(s);
  int colon = s.indexOf(":");
  if (colon <= 0) return false;

  h = s.substring(0, colon).toInt();
  m = s.substring(colon + 1).toInt();

  return h >= 0 && h <= 23 && m >= 0 && m <= 59;
}

long long normalizeTimestampToMs(long long value) {
  if (value <= 0) return 0;

  if (value < 10000000000LL) {
    return value * 1000LL;
  }

  if (value < 10000000000000LL) {
    return value;
  }

  return value / 1000LL;
}

long long parseDateTimeStringToMs(String value) {
  value.trim();
  if (value.length() < 10) return 0;

  int y = value.substring(0, 4).toInt();
  int mo = value.substring(5, 7).toInt();
  int d = value.substring(8, 10).toInt();

  if (y < 2020 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;

  int h = 0;
  int mi = 0;
  int sec = 0;

  if (value.length() >= 16) {
    h = value.substring(11, 13).toInt();
    mi = value.substring(14, 16).toInt();
  }

  if (value.length() >= 19) {
    sec = value.substring(17, 19).toInt();
  }

  h = constrain(h, 0, 23);
  mi = constrain(mi, 0, 59);
  sec = constrain(sec, 0, 59);

  long days = daysFromCivil(y, mo, d);
  long long totalSeconds = ((long long)days * 86400LL) + ((long long)h * 3600LL) + ((long long)mi * 60LL) + sec;
  return totalSeconds * 1000LL;
}

long long getManualSensorStartMs() {
  int y, m, d;
  int h, mi;

  if (!parseYyyyMmDd(appConfig.sensorStartDate, y, m, d)) return 0;
  if (!parseHhMm(appConfig.sensorStartTime, h, mi)) {
    h = 0;
    mi = 0;
  }

  long days = daysFromCivil(y, m, d);
  long long totalSeconds = ((long long)days * 86400LL) + ((long long)h * 3600LL) + ((long long)mi * 60LL);

  return totalSeconds * 1000LL;
}

long long getActiveSensorStartMs() {
  // Auto mode must never fall back to the locally configured manual date.
  // Manual mode must never use a stale Nightscout sensor timestamp.
  if (appConfig.sensorAutoRead) return nightscoutSensorStartMs;
  return getManualSensorStartMs();
}

uint8_t getActiveSensorWearDays() {
  // Wear duration is always user configured. Nightscout supplies the sensor
  // age/start, while Leah 2R Displays applies the chosen wear period.
  return appConfig.sensorWearDays;
}

String getActiveSensorSerial() {
  if (appConfig.sensorAutoRead) {
    return nightscoutSensorSerial;
  }
  return appConfig.sensorSerial;
}

String getActiveSensorSourceText() {
  if (appConfig.sensorAutoRead) {
    if (nightscoutSageSource.length() > 0) return nightscoutSageSource;
    if (nightscoutSensorSource.length() > 0) return nightscoutSensorSource;
    return "Nightscout";
  }

  if (appConfig.sensorSource.length() > 0 && appConfig.sensorSource != "Auto") {
    return appConfig.sensorSource;
  }
  return "Manual";
}

String getActiveSensorModeText() {
  return appConfig.sensorAutoRead ? "Auto" : "Manual";
}

String formatHoursAsDaysHours(long hours) {
  bool negative = hours < 0;
  if (negative) hours = -hours;

  long days = hours / 24L;
  long remHours = hours % 24L;

  String result = "";
  if (negative) result += "-";
  result += String(days);
  result += "d ";
  result += String(remHours);
  result += "h";

  return result;
}

long getSensorUsedHours() {
  // Prefer Nightscout's own SAGE age in Auto mode. This avoids mixing a
  // direct SAGE display value with a stale/manual start timestamp.
  if (appConfig.sensorAutoRead && nightscoutSageAgeHours >= 0) {
    return nightscoutSageAgeHours;
  }

  long long startMs = getActiveSensorStartMs();
  if (startMs <= 0) return -999999L;

  time_t now = time(nullptr);
  if (now < 1600000000) return -999999L;

  long long nowMs = (long long)now * 1000LL;
  long long diffMs = nowMs - startMs;
  return (long)(diffMs / 3600000LL);
}

long getSensorLeftHours() {
  long usedHours = getSensorUsedHours();
  if (usedHours == -999999L) return -999999L;

  return ((long)getActiveSensorWearDays() * 24L) - usedHours;
}

String getSensorUsedText() {
  long usedHours = getSensorUsedHours();
  if (usedHours == -999999L) return "Set start";
  if (usedHours < 0) return "Starts " + formatHoursAsDaysHours(-usedHours);
  return formatHoursAsDaysHours(usedHours);
}

String getSensorLeftText() {
  long leftHours = getSensorLeftHours();
  if (leftHours == -999999L) return "Unknown";

  if (leftHours < 0) {
    return "Expired " + formatHoursAsDaysHours(-leftHours);
  }

  return formatHoursAsDaysHours(leftHours);
}

String getSensorLifeText() {
  long usedHours = getSensorUsedHours();
  long leftHours = getSensorLeftHours();

  if (usedHours == -999999L || leftHours == -999999L) {
    return "Set start date/time";
  }

  if (leftHours < 0) {
    return formatHoursAsDaysHours(usedHours) + " used, expired " + formatHoursAsDaysHours(-leftHours);
  }

  return formatHoursAsDaysHours(usedHours) + " used, " + formatHoursAsDaysHours(leftHours) + " left";
}

String getSageDisplayText() {
  if (appConfig.sensorAutoRead) {
    if (nightscoutSageDisplay.length() > 0) return nightscoutSageDisplay;
    if (nightscoutSageAgeHours >= 0) return formatHoursAsDaysHours(nightscoutSageAgeHours);
    return "Waiting";
  }

  long usedHours = getSensorUsedHours();
  if (usedHours != -999999L) return formatHoursAsDaysHours(usedHours);
  return "Set start";
}

String getSageLevelText() {
  if (!appConfig.sensorAutoRead) return "Local";
  if (nightscoutSageLevel < 0) return "n/a";
  if (nightscoutSageLevel == 0) return "OK";
  if (nightscoutSageLevel == 1) return "Warn";
  if (nightscoutSageLevel >= 2) return "High";
  return String(nightscoutSageLevel);
}

String getSageSourceText() {
  if (!appConfig.sensorAutoRead) return "Manual local";
  if (nightscoutSageSource.length() > 0) return nightscoutSageSource;
  return "Nightscout";
}

String getJsonStringIfPresent(JsonVariant v) {
  if (v.is<const char*>()) return v.as<String>();
  if (v.is<String>()) return v.as<String>();
  return "";
}

long long getJsonTimestampMsIfPresent(JsonVariant v) {
  if (v.is<long long>()) return normalizeTimestampToMs(v.as<long long>());
  if (v.is<unsigned long long>()) return normalizeTimestampToMs((long long)v.as<unsigned long long>());
  if (v.is<long>()) return normalizeTimestampToMs((long long)v.as<long>());
  if (v.is<int>()) return normalizeTimestampToMs((long long)v.as<int>());
  if (v.is<const char*>()) return parseDateTimeStringToMs(v.as<String>());
  if (v.is<String>()) return parseDateTimeStringToMs(v.as<String>());
  return 0;
}

float getJsonFloatIfPresent(JsonVariant v, float fallback = 0.0) {
  if (v.isNull()) return fallback;
  if (v.is<float>() || v.is<double>() || v.is<int>() || v.is<long>() || v.is<unsigned int>() || v.is<unsigned long>()) {
    return v.as<float>();
  }
  if (v.is<const char*>()) {
    String s = v.as<String>();
    s.trim();
    if (s.length() == 0 || s == "null") return fallback;
    return s.toFloat();
  }
  if (v.is<String>()) {
    String s = v.as<String>();
    s.trim();
    if (s.length() == 0 || s == "null") return fallback;
    return s.toFloat();
  }
  return fallback;
}

String pickFirstString(JsonObject obj, const char* k1, const char* k2, const char* k3, const char* k4, const char* k5) {
  String value = "";
  if (strlen(k1) > 0) value = getJsonStringIfPresent(obj[k1]);
  if (value.length() == 0 && strlen(k2) > 0) value = getJsonStringIfPresent(obj[k2]);
  if (value.length() == 0 && strlen(k3) > 0) value = getJsonStringIfPresent(obj[k3]);
  if (value.length() == 0 && strlen(k4) > 0) value = getJsonStringIfPresent(obj[k4]);
  if (value.length() == 0 && strlen(k5) > 0) value = getJsonStringIfPresent(obj[k5]);
  value.trim();
  return value;
}

long long pickFirstTimestampMs(JsonObject obj, const char* k1, const char* k2, const char* k3, const char* k4, const char* k5) {
  long long value = 0;
  if (strlen(k1) > 0) value = getJsonTimestampMsIfPresent(obj[k1]);
  if (value == 0 && strlen(k2) > 0) value = getJsonTimestampMsIfPresent(obj[k2]);
  if (value == 0 && strlen(k3) > 0) value = getJsonTimestampMsIfPresent(obj[k3]);
  if (value == 0 && strlen(k4) > 0) value = getJsonTimestampMsIfPresent(obj[k4]);
  if (value == 0 && strlen(k5) > 0) value = getJsonTimestampMsIfPresent(obj[k5]);
  return value;
}

uint8_t pickFirstWearDays(JsonObject obj) {
  int value = 0;

  if (obj["wearDays"].is<int>()) value = obj["wearDays"].as<int>();
  else if (obj["lifeDays"].is<int>()) value = obj["lifeDays"].as<int>();
  else if (obj["durationDays"].is<int>()) value = obj["durationDays"].as<int>();
  else if (obj["sensorDays"].is<int>()) value = obj["sensorDays"].as<int>();
  else if (obj["period"].is<int>()) value = obj["period"].as<int>();

  if (value <= 0 || value > 30) return 0;
  return (uint8_t)value;
}

void updateNightscoutSensorInfo(JsonObject status) {
  nightscoutSensorSource = "";
  nightscoutSensorSerial = "";
  nightscoutSensorStartMs = 0;
  nightscoutSensorWearDays = 0;

  if (status["sensor"].is<JsonObject>()) {
    JsonObject sensor = status["sensor"].as<JsonObject>();

    nightscoutSensorSource = pickFirstString(sensor, "source", "type", "device", "model", "name");
    nightscoutSensorSerial = pickFirstString(sensor, "serial", "sensorSerial", "sensorId", "transmitterId", "txId");
    nightscoutSensorStartMs = pickFirstTimestampMs(sensor, "startedAt", "started_at", "startDate", "sessionStart", "insertedAt");
    if (nightscoutSensorStartMs == 0) {
      nightscoutSensorStartMs = pickFirstTimestampMs(sensor, "inserted_at", "activatedAt", "activationDate", "start", "started");
    }
    nightscoutSensorWearDays = pickFirstWearDays(sensor);
  }

  // Some uploaders expose a flat status payload instead of a nested sensor object.
  if (nightscoutSensorSerial.length() == 0) {
    nightscoutSensorSerial = pickFirstString(status, "sensorSerial", "sensorId", "transmitterId", "txId", "serial");
  }

  if (nightscoutSensorStartMs == 0) {
    nightscoutSensorStartMs = pickFirstTimestampMs(status, "sensorStart", "sensorStartedAt", "sessionStart", "sensorInsertTime", "sensorStarted");
  }

  if (nightscoutSensorWearDays == 0) {
    nightscoutSensorWearDays = pickFirstWearDays(status);
  }

  if (nightscoutSensorSource.length() == 0) {
    nightscoutSensorSource = lastDeviceStatusDevice;
  }

  updateDetectedCgmSource();
}

String maskPhoneForOled(String n) {
  n.trim();
  n.replace("whatsapp:", "");
  if (n.length() == 0) return "Not set";
  if (n.length() <= 6) return n;
  return "*" + n.substring(n.length() - 6);
}

String getTwilioShortError() {
  if (twilioLastHttpCode >= 200 && twilioLastHttpCode < 300) return "OK " + String(twilioLastHttpCode);
  if (twilioLastHttpCode == 0) return shortenText(twilioLastStatus, 18);

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, twilioLastResponse);
  if (!e) {
    int code = doc["code"] | 0;
    String msg = doc["message"] | "";
    if (code > 0) {
      return "Err " + String(code) + " " + shortenText(msg, 10);
    }
  }

  return "HTTP " + String(twilioLastHttpCode);
}




// ==================================================
// 8. BASIC HELPER FUNCTIONS
// ==================================================

String shortenText(String text, int maxLen) {
  if (text.length() <= maxLen) return text;
  return text.substring(0, maxLen - 3) + "...";
}

String shortenTextToPixelWidth(String text, int maxWidthPx) {
  // Uses the currently selected U8g2 font.  This is used on the 128x64 OLED
  // header so the patient name never runs into the clock/update/Wi-Fi area.
  if (maxWidthPx <= 0) return "";
  if (u8g2.getStrWidth(text.c_str()) <= maxWidthPx) return text;

  String suffix = "..";
  int suffixWidth = u8g2.getStrWidth(suffix.c_str());
  if (suffixWidth >= maxWidthPx) return ".";

  while (text.length() > 0 && u8g2.getStrWidth((text + suffix).c_str()) > maxWidthPx) {
    text.remove(text.length() - 1);
  }

  text.trim();
  return text + suffix;
}

String normalizeGlucoseUnits(String units) {
  units.trim();
  units.toUpperCase();
  units.replace(" ", "");
  units.replace("_", "");

  if (units == "MGDL" || units == "MG/DL" || units == "MGML" || units == "MG/ML") {
    return "MGDL";
  }

  return "MMOL";
}

bool glucoseUnitsAreMgdl() {
  return normalizeGlucoseUnits(appConfig.glucoseUnits) == "MGDL";
}

String getGlucoseUnitLabel() {
  return glucoseUnitsAreMgdl() ? "mg/dL" : "mmol/L";
}

float mmolToDisplayGlucose(float mmol) {
  if (glucoseUnitsAreMgdl()) return mmol * 18.0;
  return mmol;
}

float mgdlToMmol(float mgdl) {
  return mgdl / 18.0;
}

float displayGlucoseToMmol(float displayValue) {
  if (glucoseUnitsAreMgdl()) return displayValue / 18.0;
  return displayValue;
}

String formatGlucoseDisplay(float mmol) {
  if (glucoseUnitsAreMgdl()) {
    float mgdl = mmol * 18.0;
    int rounded = (int)(mgdl + 0.5);
    return String(rounded);
  }

  return String(mmol, 1);
}

String formatDelta(float valueMmol) {
  if (glucoseUnitsAreMgdl()) {
    float mgdl = valueMmol * 18.0;
    int rounded = (mgdl >= 0) ? (int)(mgdl + 0.5) : (int)(mgdl - 0.5);
    if (rounded > 0) return "+" + String(rounded);
    return String(rounded);
  }

  if (valueMmol > 0.05) {
    return "+" + String(valueMmol, 1);
  } else {
    return String(valueMmol, 1);
  }
}

String formatLimitInputValue(float mmol) {
  if (glucoseUnitsAreMgdl()) {
    return String((int)((mmol * 18.0) + 0.5));
  }
  return String(mmol, 1);
}

float parseLimitInputToMmol(String arg, float fallbackMmol) {
  arg.trim();
  if (arg.length() == 0) return fallbackMmol;
  float value = arg.toFloat();
  if (value <= 0) return fallbackMmol;
  return displayGlucoseToMmol(value);
}

float convertThresholdToMmol(float value) {
  // If Nightscout returns mg/dL values, convert to mmol/L.
  if (value > 30.0) {
    return value / 18.0;
  }

  return value;
}

long long getEntryTimeMs(JsonObject entry) {
  long long t = 0;

  if (!entry["date"].isNull()) {
    t = entry["date"].as<long long>();
  }

  if (t <= 0 && !entry["mills"].isNull()) {
    t = entry["mills"].as<long long>();
  }

  // Convert seconds to milliseconds if required
  if (t > 0 && t < 10000000000LL) {
    t = t * 1000LL;
  }

  // If timestamp is missing, use current time
  if (t <= 0) {
    t = (long long)time(nullptr) * 1000LL;
  }

  return t;
}

void updateDataAge() {
  if (latestEntryDateMs <= 0) {
    ageMinutes = -1;
    return;
  }

  time_t nowSeconds = time(nullptr);
  long long nowMs = (long long)nowSeconds * 1000LL;

  if (nowMs > latestEntryDateMs) {
    ageMinutes = (long)((nowMs - latestEntryDateMs) / 60000LL);
  } else {
    ageMinutes = 0;
  }
}

bool isDataFresh() {
  updateDataAge();

  if (glucoseMmol <= 0) return false;
  if (ageMinutes < 0) return false;

  return ageMinutes <= maxDataAgeMinutes;
}


// ==================================================
// 9. ALARM LEVEL LOGIC
// ==================================================
// This controls the display flashing.
// It ignores siren mute/off, so visual alarm remains active.

int getCurrentAlarmLevel() {
  if (!isDataFresh()) {
    return ALARM_NONE;
  }

  if (glucoseMmol <= urgentLowLimitMmol) {
    return ALARM_URGENT_LOW;
  }

  if (glucoseMmol <= lowLimitMmol) {
    return ALARM_LOW;
  }

  if (glucoseMmol >= urgentHighLimitMmol) {
    return ALARM_URGENT_HIGH;
  }

  if (glucoseMmol >= highLimitMmol) {
    return ALARM_HIGH;
  }

  return ALARM_NONE;
}

bool borderFlashIsOn(unsigned long interval) {
  unsigned long phase = millis() % interval;
  return phase < borderFlashOnTime;
}

bool shouldShowGlucoseValue() {
  int level = getCurrentAlarmLevel();

  // Urgent low: faster blink, but the value is hidden only briefly.
  if (level == ALARM_URGENT_LOW) {
    unsigned long phase = millis() % urgentLowBlinkCycle;
    return phase >= urgentLowBlinkOffTime;
  }

  // Low: blink once per second.
  // Value hidden for 250 ms and visible for 750 ms.
  if (level == ALARM_LOW) {
    unsigned long phase = millis() % lowValueBlinkCycle;
    return phase >= lowValueBlinkOffTime;
  }

  // Normal, high, and urgent high:
  // Keep the glucose number visible. Only the border pulses for high alarms.
  return true;
}

String getAlarmDisplayText() {
  int level = getCurrentAlarmLevel();

  if (level == ALARM_URGENT_LOW) return "U-LOW";
  if (level == ALARM_LOW) return "LOW";
  if (level == ALARM_HIGH) return "HIGH";
  if (level == ALARM_URGENT_HIGH) return "U-HI";

  return "OK";
}

String getSirenDisplayText() {
  if (!appConfig.alarmSoundEnabled) return "OFF";
  if (sirenSilenced) return "MUTE";
  return "ON";
}


// ==================================================
// 10. WIFI SIGNAL STRENGTH BAR
// ==================================================
// Draws a 4-bar Wi-Fi signal indicator in the top-right corner.
// Uses RSSI:
// -55 dBm and better = 4 bars
// -65 dBm            = 3 bars
// -75 dBm            = 2 bars
// -85 dBm            = 1 bar
// weaker/disconnected = 0 bars

int getWiFiBars() {
  if (WiFi.status() != WL_CONNECTED) {
    return 0;
  }

  int rssi = WiFi.RSSI();

  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;

  return 0;
}

void drawWiFiSignalBars() {
  int bars = getWiFiBars();

  // Top-right corner
  int x = 106;
  int y = 4;

  int barWidth = 3;

  // Heights
  int h1 = 3;
  int h2 = 5;
  int h3 = 7;
  int h4 = 9;

  // Empty bar outlines
  u8g2.drawFrame(x,      y + 6, barWidth, h1);
  u8g2.drawFrame(x + 5,  y + 4, barWidth, h2);
  u8g2.drawFrame(x + 10, y + 2, barWidth, h3);
  u8g2.drawFrame(x + 15, y,     barWidth, h4);

  // Filled bars
  if (bars >= 1) u8g2.drawBox(x,      y + 6, barWidth, h1);
  if (bars >= 2) u8g2.drawBox(x + 5,  y + 4, barWidth, h2);
  if (bars >= 3) u8g2.drawBox(x + 10, y + 2, barWidth, h3);
  if (bars >= 4) u8g2.drawBox(x + 15, y,     barWidth, h4);
}


// ==================================================
// 11. HEARTBEAT
// ==================================================
// This proves the ESP32 main loop is still alive.
// If this stops, the ESP32 is stuck in a blocking call.

void printHeartbeat() {
  if (millis() - lastHeartbeat >= 1000) {
    Serial.print("HEARTBEAT | millis: ");
    Serial.print(millis());

    Serial.print(" | WiFi: ");
    Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "LOST");

    Serial.print(" | RSSI: ");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(WiFi.RSSI());
      Serial.print(" dBm");
    } else {
      Serial.print("N/A");
    }

    Serial.print(" | Screen: ");
    Serial.print(currentScreen);

    Serial.print(" | Glucose: ");
    Serial.print(glucoseMmol, 1);

    Serial.print(" | Age: ");
    Serial.print(ageMinutes);

    Serial.print(" | Alarm: ");
    Serial.print(getAlarmDisplayText());

    Serial.print(" | CloudQ: ");
    Serial.print(cloudAlertQueue ? uxQueueMessagesWaiting(cloudAlertQueue) : 0);
    Serial.print(" | CloudBusy: ");
    Serial.print(cloudAlertSendInProgress ? "YES" : "NO");
    Serial.print(" | CloudLastMs: ");
    Serial.print(lastCloudAlertSendDurationMs);
    Serial.print(" | LoopMaxMs: ");
    Serial.print(mainLoopMaxDelayMs);
    Serial.print(" | Last NS OK age sec: ");

    if (lastSuccessfulNightscoutRead > 0) {
      Serial.println((millis() - lastSuccessfulNightscoutRead) / 1000);
    } else {
      Serial.println("never");
    }

    mainLoopMaxDelayMs = 0;
    lastHeartbeat = millis();
  }
}


// ==================================================
// 11B. WARM-RESTART PERIPHERAL RECOVERY
// ==================================================

void forceBuzzerHardwareOff() {
#if ENABLE_SIREN_HARDWARE
  // noTone() releases any LEDC/tone state that may survive a software restart.
  // Re-assert OUTPUT/LOW afterwards so the transistor or piezo cannot float.
  noTone(BUZZER_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
#endif
  buzzerIsOn = false;
}

void recoverOledSoftwareI2cBus() {
  // A software restart does not remove power from the SSD1309. If a transfer was
  // interrupted, SDA can remain low and the next u8g2.begin() cannot complete.
  pinMode(OLED_SDA, INPUT_PULLUP);
  pinMode(OLED_SCL, INPUT_PULLUP);
  delay(3);

  // Clock out a possibly incomplete byte/ACK. Twelve pulses provide margin over
  // the normal nine-clock I2C bus-clear sequence without changing OLED data.
  if (digitalRead(OLED_SDA) == LOW) {
    pinMode(OLED_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(OLED_SCL, HIGH);
    for (uint8_t i = 0; i < 12; i++) {
      digitalWrite(OLED_SCL, LOW);
      delayMicroseconds(12);
      digitalWrite(OLED_SCL, HIGH);
      delayMicroseconds(12);
    }
  }

  // Generate a legal STOP condition: SDA low -> SCL high -> SDA high.
  pinMode(OLED_SDA, OUTPUT_OPEN_DRAIN);
  pinMode(OLED_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(OLED_SDA, LOW);
  digitalWrite(OLED_SCL, LOW);
  delayMicroseconds(12);
  digitalWrite(OLED_SCL, HIGH);
  delayMicroseconds(12);
  digitalWrite(OLED_SDA, HIGH);
  delayMicroseconds(12);

  pinMode(OLED_SDA, INPUT_PULLUP);
  pinMode(OLED_SCL, INPUT_PULLUP);
  delay(5);
}

void initializeOledSafely() {
  oledRuntimeReady = false;
  recoverOledSoftwareI2cBus();

  u8g2.begin();

  // Force the still-powered SSD1309 through display-off/display-on after a warm
  // reset, then transmit a known blank framebuffer before the startup artwork.
  u8g2.setPowerSave(1);
  delay(20);
  u8g2.setPowerSave(0);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  delay(20);

  oledRuntimeReady = true;
}

void preparePeripheralsForRestart() {
  peripheralRestartInProgress = true;
  alarmAudioArmed = false;
  sirenEnabled = false;
  sirenSilenced = true;
  activeAudioAlarmType = ALARM_NONE;

  // Give the audio worker at least two service periods to observe the inhibit.
  delay((ALARM_AUDIO_TASK_PERIOD_MS * 2U) + 5U);

  if (alarmAudioTaskHandle != nullptr) {
    vTaskSuspend(alarmAudioTaskHandle);
  }
  forceBuzzerHardwareOff();

  if (oledRuntimeReady) {
    // Leave the externally powered OLED in a defined sleep state before the CPU
    // resets. The next boot will run the bus-clear and full initialization again.
    u8g2.clearBuffer();
    u8g2.sendBuffer();
    u8g2.setPowerSave(1);
    delay(20);
  }

  oledRuntimeReady = false;
  pinMode(OLED_SDA, INPUT_PULLUP);
  pinMode(OLED_SCL, INPUT_PULLUP);
}

void restartDeviceSafely() {
  preparePeripheralsForRestart();
  delay(50);
  esp_restart();
  while (true) {
    delay(1000);
  }
}

void otaRestartGuardTask(void *parameter) {
  (void)parameter;
  vTaskDelay(pdMS_TO_TICKS(OTA_RESTART_GUARD_DELAY_MS));

  // This task is a final safety net. It does not touch the OLED, network, NVS,
  // or application state; it invokes Espressif's chip restart directly.
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("OTA restart guard forcing esp_restart().");
  Serial.flush();
  esp_restart();
  vTaskDelete(nullptr);
}

bool startOtaRestartGuard() {
  if (otaRestartGuardTaskHandle != nullptr) return true;

  BaseType_t result = xTaskCreatePinnedToCore(
    otaRestartGuardTask,
    "OtaRestartGuard",
    2048,
    nullptr,
    5,
    &otaRestartGuardTaskHandle,
    0
  );

  if (result != pdPASS) {
    otaRestartGuardTaskHandle = nullptr;
    Serial.println("WARNING: OTA restart guard task could not be created.");
    return false;
  }

  return true;
}

[[noreturn]] void restartImmediatelyWithEspIdf(const char *reason) {
  peripheralRestartInProgress = true;
  alarmAudioArmed = false;
  sirenEnabled = false;
  sirenSilenced = true;
  activeAudioAlarmType = ALARM_NONE;
  forceBuzzerHardwareOff();

  Serial.print("Espressif restart requested: ");
  Serial.println(reason ? reason : "unspecified");
  Serial.flush();
  delay(50);

  // Espressif documents esp_restart() as the software-reset API. It should not
  // return. No OLED or NVS operation is attempted after this point.
  esp_restart();

  while (true) {
    delay(1000);
  }
}

void storePendingOtaBootConfirmation(const String &targetVersion) {
  Preferences otaPreferences;
  if (!otaPreferences.begin(OTA_STATE_NAMESPACE, false)) {
    Serial.println("WARNING: Could not open OTA state namespace.");
    return;
  }

  otaPreferences.putString(OTA_PENDING_VERSION_KEY, targetVersion);
  otaPreferences.end();
}

void showPendingOtaBootConfirmation() {
  Preferences otaPreferences;

  // Open read-only first. On a factory-clean device this does not create a new
  // NVS namespace merely to discover that no OTA marker exists.
  if (!otaPreferences.begin(OTA_STATE_NAMESPACE, true)) return;

  String expectedVersion = otaPreferences.getString(OTA_PENDING_VERSION_KEY, "");
  otaPreferences.end();
  if (expectedVersion.length() == 0) return;

  // Clear the marker only after the new application has reached setup() and the
  // OLED is operational. This provides visible proof of the booted image.
  if (otaPreferences.begin(OTA_STATE_NAMESPACE, false)) {
    otaPreferences.remove(OTA_PENDING_VERSION_KEY);
    otaPreferences.end();
  }

  if (expectedVersion == String(FIRMWARE_VERSION)) {
    drawMessage("OTA Successful", "Update complete", "FW " + String(FIRMWARE_VERSION), "Build " + String(BUILD_NUMBER));
    Serial.println("OTA boot confirmation passed: running expected firmware.");
  } else {
    drawMessage("OTA Boot Check", "Expected " + shortenText(expectedVersion, 15), "Running " + String(FIRMWARE_VERSION), "Check update");
    Serial.println("WARNING: OTA boot confirmation version mismatch.");
  }
  delay(3000);
}

// ==================================================
// 12. OLED BASIC MESSAGE SCREEN
// ==================================================

void drawMessage(String line1, String line2, String line3, String line4) {
  applyDisplayContrast();
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);

  // Wi-Fi bars visible on messages too
  drawWiFiSignalBars();

  u8g2.setFont(u8g2_font_6x12_tr);

  u8g2.setCursor(4, 14);
  u8g2.print(line1);

  u8g2.setCursor(4, 29);
  u8g2.print(line2);

  u8g2.setCursor(4, 44);
  u8g2.print(line3);

  u8g2.setCursor(4, 59);
  u8g2.print(line4);

  u8g2.sendBuffer();
}

void drawLeah2RDisplaysStartupBrand() {
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);

  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.setCursor(8, 22);
  u8g2.print("Leah 2R Displays");

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.setCursor(23, 39);
  u8g2.print("Read. Respond.");

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(34, 55);
  u8g2.print("OLED 2.42");

  u8g2.sendBuffer();
}

void drawLeah2RDisplaysVisionScreen() {
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);

  // Vision only on the startup vision screen. The mission is shown in the Web UI.
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(4, 9);  u8g2.print("To transform diabetes");
  u8g2.setCursor(4, 17); u8g2.print("monitoring into");
  u8g2.setCursor(4, 25); u8g2.print("connected, human-centred");
  u8g2.setCursor(4, 33); u8g2.print("care that brings greater");
  u8g2.setCursor(4, 41); u8g2.print("safety, confidence and");
  u8g2.setCursor(4, 49); u8g2.print("independence to everyday");
  u8g2.setCursor(4, 57); u8g2.print("life");

  u8g2.sendBuffer();
}

void showLeah2RDisplaysStartupSequence() {
  drawLeah2RDisplaysStartupBrand();
  delay(STARTUP_BRAND_MS);

  drawLeah2RDisplaysVisionScreen();
  delay(STARTUP_VISION_MS);
}




// ==================================================
// 13. DRAW TREND ARROWS
// ==================================================

void drawTrendArrowIcon(String direction) {

  // Compact arrow set for 128x64 OLED. The arrows are shifted left and made
  // smaller so the right-hand area can carry the delta + selected unit.
  if (direction == "DoubleUp") {
    u8g2.drawLine(78, 41, 78, 21);
    u8g2.drawLine(78, 21, 72, 29);
    u8g2.drawLine(78, 21, 84, 29);

    u8g2.drawLine(96, 41, 96, 21);
    u8g2.drawLine(96, 21, 90, 29);
    u8g2.drawLine(96, 21, 102, 29);
  }

  else if (direction == "SingleUp") {
    u8g2.drawLine(88, 41, 88, 21);
    u8g2.drawLine(88, 21, 80, 31);
    u8g2.drawLine(88, 21, 96, 31);
  }

  else if (direction == "FortyFiveUp") {
    u8g2.drawLine(76, 41, 104, 21);
    u8g2.drawLine(104, 21, 92, 23);
    u8g2.drawLine(104, 21, 102, 33);
  }

  else if (direction == "Flat") {
    u8g2.drawLine(76, 31, 106, 31);
    u8g2.drawLine(106, 31, 96, 24);
    u8g2.drawLine(106, 31, 96, 38);
  }

  else if (direction == "FortyFiveDown") {
    u8g2.drawLine(76, 21, 104, 41);
    u8g2.drawLine(104, 41, 92, 39);
    u8g2.drawLine(104, 41, 102, 29);
  }

  else if (direction == "SingleDown") {
    u8g2.drawLine(88, 21, 88, 41);
    u8g2.drawLine(88, 41, 80, 31);
    u8g2.drawLine(88, 41, 96, 31);
  }

  else if (direction == "DoubleDown") {
    u8g2.drawLine(78, 21, 78, 41);
    u8g2.drawLine(78, 41, 72, 33);
    u8g2.drawLine(78, 41, 84, 33);

    u8g2.drawLine(96, 21, 96, 41);
    u8g2.drawLine(96, 41, 90, 33);
    u8g2.drawLine(96, 41, 102, 33);
  }

  else {
    u8g2.setFont(u8g2_font_10x20_tr);
    u8g2.setCursor(84, 38);
    u8g2.print("?");
  }
}


// ==================================================
// 14. DRAW HIGH ALARM BORDER
// ==================================================

void drawAlarmBorder() {
  int level = getCurrentAlarmLevel();

  bool flashBorder = false;

  if (level == ALARM_HIGH) {
    flashBorder = borderFlashIsOn(highBorderFlashInterval);
  }
  else if (level == ALARM_URGENT_HIGH) {
    flashBorder = borderFlashIsOn(urgentHighBorderFlashInterval);
  }

  if (flashBorder) {
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawFrame(1, 1, 126, 62);
    u8g2.drawFrame(2, 2, 124, 60);

    u8g2.drawBox(0, 0, 8, 8);
    u8g2.drawBox(120, 0, 8, 8);
    u8g2.drawBox(0, 56, 8, 8);
    u8g2.drawBox(120, 56, 8, 8);
  } else {
    u8g2.drawFrame(0, 0, 128, 64);
  }
}


// ==================================================
// 15. OLED MAIN SCREEN
// ==================================================

void drawNoDataScreen() {
  applyDisplayContrast();
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);

  // Keep the same compact header as the main screen: name, time, update icon, Wi-Fi.
  drawWiFiSignalBars();
  drawPatientLabelNearWiFi();

  // Main no-data message.
  u8g2.setFont(u8g2_font_10x20_tr);
  u8g2.setCursor(24, 31);
  u8g2.print("NO DATA");

  // Last valid reading, with age right-aligned so the line stays clean.
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.setCursor(4, 43);
  if (glucoseMmol > 0) {
    u8g2.print("Last:");
    u8g2.print(formatGlucoseDisplay(glucoseMmol));
    u8g2.print(" ");
    u8g2.print(getGlucoseUnitLabel());
    if (ageMinutes >= 0) {
      String ageText = String(ageMinutes) + "m";
      int ageWidth = u8g2.getStrWidth(ageText.c_str());
      u8g2.setCursor(124 - ageWidth, 43);
      u8g2.print(ageText);
    }
  } else {
    u8g2.print("Last: --");
  }

  // Short status line one row lower.
  u8g2.setCursor(4, 53);
  u8g2.print(shortenText(lastStatus, 20));

  // IP address uses the smaller 5x8 font so it clears the bottom border.
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(4, 61);
  u8g2.print("IP:");
  u8g2.print(WiFi.localIP());

  u8g2.sendBuffer();
}

void drawNightTrendArrowIcon(String direction) {
  // Very compact night-screen arrows. These stay at the far right so the
  // glucose number can use almost the full height and width of the OLED.
  const int cx = 106;
  const int yTop = 21;
  const int yMid = 35;
  const int yBot = 49;

  if (direction == "DoubleUp") {
    u8g2.drawLine(99, yBot, 99, yTop);
    u8g2.drawLine(99, yTop, 94, yTop + 7);
    u8g2.drawLine(99, yTop, 104, yTop + 7);
    u8g2.drawLine(115, yBot, 115, yTop);
    u8g2.drawLine(115, yTop, 110, yTop + 7);
    u8g2.drawLine(115, yTop, 120, yTop + 7);
  }
  else if (direction == "SingleUp") {
    u8g2.drawLine(cx, yBot, cx, yTop);
    u8g2.drawLine(cx, yTop, cx - 7, yTop + 9);
    u8g2.drawLine(cx, yTop, cx + 7, yTop + 9);
  }
  else if (direction == "FortyFiveUp") {
    u8g2.drawLine(96, yBot, 119, yTop + 4);
    u8g2.drawLine(119, yTop + 4, 110, yTop + 5);
    u8g2.drawLine(119, yTop + 4, 118, yTop + 13);
  }
  else if (direction == "Flat") {
    u8g2.drawLine(94, yMid, 120, yMid);
    u8g2.drawLine(120, yMid, 111, yMid - 6);
    u8g2.drawLine(120, yMid, 111, yMid + 6);
  }
  else if (direction == "FortyFiveDown") {
    u8g2.drawLine(96, yTop + 4, 119, yBot);
    u8g2.drawLine(119, yBot, 110, yBot - 1);
    u8g2.drawLine(119, yBot, 118, yBot - 9);
  }
  else if (direction == "SingleDown") {
    u8g2.drawLine(cx, yTop, cx, yBot);
    u8g2.drawLine(cx, yBot, cx - 7, yBot - 9);
    u8g2.drawLine(cx, yBot, cx + 7, yBot - 9);
  }
  else if (direction == "DoubleDown") {
    u8g2.drawLine(99, yTop, 99, yBot);
    u8g2.drawLine(99, yBot, 94, yBot - 7);
    u8g2.drawLine(99, yBot, 104, yBot - 7);
    u8g2.drawLine(115, yTop, 115, yBot);
    u8g2.drawLine(115, yBot, 110, yBot - 7);
    u8g2.drawLine(115, yBot, 120, yBot - 7);
  }
}

int getCenteredFontBaseline(int topY, int bottomY) {
  // U8g2 font metrics are relative to the baseline. getDescent() is normally negative.
  // This places the visible glyph height in the vertical centre of the selected band.
  int asc = u8g2.getAscent();
  int desc = u8g2.getDescent();
  int glyphHeight = asc - desc;
  int bandHeight = bottomY - topY + 1;
  int baseline = topY + ((bandHeight - glyphHeight) / 2) + asc;
  return constrain(baseline, topY + asc, bottomY + desc);
}

void drawNightGlucoseScreen() {
  // Active only during the configured night-dimming window.
  // Full-bedside view: maximum glucose number, vertically centred in the 64-line OLED.
  applyDisplayContrast();
  u8g2.clearBuffer();

  // Keep alarm border behaviour so high/urgent-high alarms still pulse visually.
  drawAlarmBorder();

  String glucoseText = formatGlucoseDisplay(glucoseMmol);

  if (shouldShowGlucoseValue()) {
    // Start with the largest practical font. If the displayed value is too wide,
    // step down safely so the arrows and delta area remain clear.
    u8g2.setFont(u8g2_font_logisoso38_tr);
    if (u8g2.getStrWidth(glucoseText.c_str()) > 92) {
      u8g2.setFont(u8g2_font_logisoso34_tr);
    }
    if (u8g2.getStrWidth(glucoseText.c_str()) > 92) {
      u8g2.setFont(u8g2_font_logisoso28_tr);
    }

    // Centre the large glucose glyph between the top and bottom borders.
    // The 2..61 band keeps the value clear of the one-pixel alarm frame.
    int glucoseBaseline = getCenteredFontBaseline(2, 61);
    u8g2.setCursor(4, glucoseBaseline);
    u8g2.print(glucoseText);
  }

  drawNightTrendArrowIcon(directionText);

  // Delta and unit stay in the lower-right, above the bottom frame line.
  u8g2.setFont(u8g2_font_5x8_tr);
  String nightDeltaText = formatDelta(deltaMmol) + " " + getGlucoseUnitLabel();
  int deltaWidth = u8g2.getStrWidth(nightDeltaText.c_str());
  int deltaX = max(58, 124 - deltaWidth);
  u8g2.setCursor(deltaX, 60);
  u8g2.print(nightDeltaText);

  u8g2.sendBuffer();
}

void drawGlucoseScreen() {
  if (!isDataFresh()) {
    drawNoDataScreen();
    return;
  }

  if (isNightDimActive()) {
    drawNightGlucoseScreen();
    return;
  }

  applyDisplayContrast();
  u8g2.clearBuffer();

  drawAlarmBorder();

  // Wi-Fi signal bars top-right.
  drawWiFiSignalBars();

  // Patient name is now reserved on the top-left row.
  // The glucose value is lowered so longer names remain readable.
  drawPatientLabelNearWiFi();

  if (shouldShowGlucoseValue()) {
    u8g2.setFont(u8g2_font_logisoso28_tr);
    // Raised one row to use the space between the patient name and glucose value.
    u8g2.setCursor(4, 43);
    u8g2.print(formatGlucoseDisplay(glucoseMmol));
  }

  drawTrendArrowIcon(directionText);

  // Keep delta and selected unit together above the arrows, for example +0.3 mmol/L.
  // The compact font lets the full text fit in the right-hand status area.
  u8g2.setFont(u8g2_font_5x8_tr);
  String deltaUnitText = formatDelta(deltaMmol) + " " + getGlucoseUnitLabel();
  u8g2.setCursor(68, 20);
  u8g2.print(shortenText(deltaUnitText, 12));

  // Two fixed-column bottom rows.  Fixed X positions keep IOB, T, A, phone,
  // last dose and time readable on the 128x64 OLED.
  updateInsulinRemainingEstimate();

  String iobText = "IOB:" + String(insulinRemainingUnits, 1) + "u";
  String todayText = "T:" + String(todayBolusUnits, 1) + "u";
  String ageText = "A:";
  if (ageMinutes >= 0) ageText += String(ageMinutes) + "m";
  else ageText += "--";

  String phoneText = "Ph:";
  if (phoneBattery >= 0) phoneText += String(phoneBattery) + "%";
  else phoneText += "--%";

  String lastDoseText = "L:";
  String lastTimeText = "--:--";
  if (lastBolusUnits > 0.0) {
    lastDoseText += String(lastBolusUnits, 1) + "u";
    lastTimeText = formatTimestampTimeOnly(lastBolusTimeMs);
  } else {
    lastDoseText += "--";
  }

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(4, 54);
  u8g2.print(shortenText(iobText, 8));
  u8g2.setCursor(50, 54);
  u8g2.print(shortenText(todayText, 7));
  u8g2.setCursor(88, 54);
  u8g2.print(shortenText(ageText, 6));

  u8g2.setCursor(4, 62);
  u8g2.print(shortenText(phoneText, 7));
  u8g2.setCursor(50, 62);
  u8g2.print(shortenText(lastDoseText, 7));
  u8g2.setCursor(88, 62);
  u8g2.print(shortenText(lastTimeText, 6));

  u8g2.sendBuffer();
}



// ==================================================
// 15B. CONFIGURATION STORAGE AND LOCAL WEB SERVER
// ==================================================

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

String checkedAttribute(bool value) {
  return value ? " checked" : "";
}

String getCaregiverLabel(uint8_t caregiverNo) {
  String label = "";
  if (caregiverNo == 1) label = appConfig.caregiver1Name;
  else if (caregiverNo == 2) label = appConfig.caregiver2Name;
  else if (caregiverNo == 3) label = appConfig.caregiver3Name;

  label.trim();
  if (label.length() == 0) {
    label = "Caregiver " + String(caregiverNo);
  }
  return label;
}

bool caregiverWantsWhatsAppEvent(uint8_t caregiverNo, String eventKey) {
  eventKey.trim();
  eventKey.toUpperCase();

  // Direct test messages and reminder messages still go to configured caregivers.
  if (eventKey == "" || eventKey == "NORMAL" || eventKey == "TEST" ||
      eventKey == "PHONE_BATTERY_LOW" || eventKey == "SENSOR_EXPIRY") {
    return true;
  }

  if (caregiverNo == 1) {
    if (eventKey == "LOW") return appConfig.cg1Low;
    if (eventKey == "URGENT_LOW") return appConfig.cg1UrgentLow;
    if (eventKey == "HIGH") return appConfig.cg1High;
    if (eventKey == "URGENT_HIGH") return appConfig.cg1UrgentHigh;
    if (eventKey == "NO_DATA") return appConfig.cg1NoData;
  } else if (caregiverNo == 2) {
    if (eventKey == "LOW") return appConfig.cg2Low;
    if (eventKey == "URGENT_LOW") return appConfig.cg2UrgentLow;
    if (eventKey == "HIGH") return appConfig.cg2High;
    if (eventKey == "URGENT_HIGH") return appConfig.cg2UrgentHigh;
    if (eventKey == "NO_DATA") return appConfig.cg2NoData;
  } else if (caregiverNo == 3) {
    if (eventKey == "LOW") return appConfig.cg3Low;
    if (eventKey == "URGENT_LOW") return appConfig.cg3UrgentLow;
    if (eventKey == "HIGH") return appConfig.cg3High;
    if (eventKey == "URGENT_HIGH") return appConfig.cg3UrgentHigh;
    if (eventKey == "NO_DATA") return appConfig.cg3NoData;
  }

  return false;
}

bool anyCaregiverWantsWhatsAppEvent(String eventKey) {
  return caregiverWantsWhatsAppEvent(1, eventKey) ||
         caregiverWantsWhatsAppEvent(2, eventKey) ||
         caregiverWantsWhatsAppEvent(3, eventKey);
}

String caregiverAlarmSummary(uint8_t caregiverNo) {
  String s = "";
  if (caregiverWantsWhatsAppEvent(caregiverNo, "URGENT_LOW")) s += "UL ";
  if (caregiverWantsWhatsAppEvent(caregiverNo, "LOW")) s += "L ";
  if (caregiverWantsWhatsAppEvent(caregiverNo, "HIGH")) s += "H ";
  if (caregiverWantsWhatsAppEvent(caregiverNo, "URGENT_HIGH")) s += "UH ";
  if (caregiverWantsWhatsAppEvent(caregiverNo, "NO_DATA")) s += "ND";
  s.trim();
  if (s.length() == 0) s = "Off";
  return s;
}

String caregiverCheckboxHtml(uint8_t caregiverNo, String eventSuffix, bool checked) {
  String name = "cg" + String(caregiverNo) + "_" + eventSuffix;
  String html = "<td style='text-align:center'><input type='checkbox' name='" + name + "'";
  html += checkedAttribute(checked);
  html += "></td>";
  return html;
}

String caregiverAlarmMatrixRow(uint8_t caregiverNo, bool textMeBotOnly) {
  String html = "<tr";
  if (textMeBotOnly) html += " data-caregiver3-only='1'";
  html += "><td><strong>" + htmlEscape(getCaregiverLabel(caregiverNo)) + "</strong>";
  if (textMeBotOnly) html += "<br><span class='muted'>TextMeBot only</span>";
  html += "</td>";

  if (caregiverNo == 1) {
    html += caregiverCheckboxHtml(caregiverNo, "urgent_low", appConfig.cg1UrgentLow);
    html += caregiverCheckboxHtml(caregiverNo, "low", appConfig.cg1Low);
    html += caregiverCheckboxHtml(caregiverNo, "high", appConfig.cg1High);
    html += caregiverCheckboxHtml(caregiverNo, "urgent_high", appConfig.cg1UrgentHigh);
    html += caregiverCheckboxHtml(caregiverNo, "no_data", appConfig.cg1NoData);
  } else if (caregiverNo == 2) {
    html += caregiverCheckboxHtml(caregiverNo, "urgent_low", appConfig.cg2UrgentLow);
    html += caregiverCheckboxHtml(caregiverNo, "low", appConfig.cg2Low);
    html += caregiverCheckboxHtml(caregiverNo, "high", appConfig.cg2High);
    html += caregiverCheckboxHtml(caregiverNo, "urgent_high", appConfig.cg2UrgentHigh);
    html += caregiverCheckboxHtml(caregiverNo, "no_data", appConfig.cg2NoData);
  } else {
    html += caregiverCheckboxHtml(caregiverNo, "urgent_low", appConfig.cg3UrgentLow);
    html += caregiverCheckboxHtml(caregiverNo, "low", appConfig.cg3Low);
    html += caregiverCheckboxHtml(caregiverNo, "high", appConfig.cg3High);
    html += caregiverCheckboxHtml(caregiverNo, "urgent_high", appConfig.cg3UrgentHigh);
    html += caregiverCheckboxHtml(caregiverNo, "no_data", appConfig.cg3NoData);
  }

  html += "</tr>";
  return html;
}

String caregiverAlarmMatrixHtml() {
  String html = "<details id='caregivers'><summary>Caregiver WhatsApp alarms</summary>";
  html += "<p class='muted'>Tick which glucose/no-data WhatsApp alarms each caregiver receives. Caregiver 3 is only used when TextMeBot is selected.</p>";
  html += "<div class='grid'>";
  html += "<div><label>Caregiver 1 name</label><input name='cg1_name' maxlength='24' value='" + htmlEscape(appConfig.caregiver1Name) + "'></div>";
  html += "<div><label>Caregiver 2 name</label><input name='cg2_name' maxlength='24' value='" + htmlEscape(appConfig.caregiver2Name) + "'></div>";
  html += "<div data-caregiver3-only='1'><label>Caregiver 3 name</label><input name='cg3_name' maxlength='24' value='" + htmlEscape(appConfig.caregiver3Name) + "'></div>";
  html += "</div>";
  html += "<div style='overflow-x:auto'><table style='width:100%;border-collapse:collapse;margin-top:12px'>";
  html += "<tr><th style='text-align:left'>Caregiver</th><th>Urgent low</th><th>Low</th><th>High</th><th>Urgent high</th><th>No data</th></tr>";
  html += caregiverAlarmMatrixRow(1, false);
  html += caregiverAlarmMatrixRow(2, false);
  html += caregiverAlarmMatrixRow(3, true);
  html += "</table></div>";
  html += "<p class='fieldhint'>These checkboxes control WhatsApp delivery only. The OLED visual alarms and local buzzer are not affected.</p>";
  html += "</details>";
  return html;
}



String wifiSecurityLabel(int encType) {
  if (encType == WIFI_AUTH_OPEN) return "Open";
  if (encType == WIFI_AUTH_WEP) return "WEP";
  if (encType == WIFI_AUTH_WPA_PSK) return "WPA";
  if (encType == WIFI_AUTH_WPA2_PSK) return "WPA2";
  if (encType == WIFI_AUTH_WPA_WPA2_PSK) return "WPA/WPA2";
  if (encType == WIFI_AUTH_WPA2_ENTERPRISE) return "WPA2 Enterprise";
#ifdef WIFI_AUTH_WPA3_PSK
  if (encType == WIFI_AUTH_WPA3_PSK) return "WPA3";
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
  if (encType == WIFI_AUTH_WPA2_WPA3_PSK) return "WPA2/WPA3";
#endif
  return "Secured";
}

String wifiSecurityCssClass(int encType) {
  if (encType == WIFI_AUTH_OPEN) return "bad";
  if (encType == WIFI_AUTH_WEP || encType == WIFI_AUTH_WPA_PSK) return "warn";
  return "ok";
}

String wifiStrengthLabel(int rssi) {
  if (rssi >= -55) return "Excellent";
  if (rssi >= -65) return "Good";
  if (rssi >= -75) return "Fair";
  return "Weak";
}

int savedWifiSlotForSsid(String ssid) {
  ssid.trim();
  if (ssid.length() == 0) return 0;
  if (ssid == appConfig.wifiSsid1) return 1;
  if (ssid == appConfig.wifiSsid2) return 2;
  if (ssid == appConfig.wifiSsid3) return 3;
  return 0;
}

String savedWifiProfilesSummaryHtml() {
  String html = "<div class='statusgrid'>";
  html += "<div class='tile'><div class='label'>NVS Slot 1</div><div class='value'>" + htmlEscape(appConfig.wifiSsid1.length() ? shortenText(appConfig.wifiSsid1, 16) : "Empty") + "</div></div>";
  html += "<div class='tile'><div class='label'>NVS Slot 2</div><div class='value'>" + htmlEscape(appConfig.wifiSsid2.length() ? shortenText(appConfig.wifiSsid2, 16) : "Empty") + "</div></div>";
  html += "<div class='tile'><div class='label'>NVS Slot 3</div><div class='value'>" + htmlEscape(appConfig.wifiSsid3.length() ? shortenText(appConfig.wifiSsid3, 16) : "Empty") + "</div></div>";
  html += "</div>";
  return html;
}

String wifiScanTableHtml() {
  String html = "";
  int n = WiFi.scanNetworks(false, true);

  if (n <= 0) {
    html += "<p class='muted'>No nearby Wi-Fi networks were found during this scan. Refresh the page to scan again.</p>";
    return html;
  }

  html += "<div style='overflow-x:auto'><table class='datatable'><tr><th>SSID</th><th>Signal</th><th>Security</th><th>NVS status</th><th>Save to slot</th></tr>";

  int rows = 0;
  for (int i = 0; i < n && rows < 12; i++) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (ssid.length() == 0) continue;

    int rssi = WiFi.RSSI(i);
    int enc = (int)WiFi.encryptionType(i);
    int savedSlot = savedWifiSlotForSsid(ssid);

    html += "<tr><td><strong>" + htmlEscape(shortenText(ssid, 24)) + "</strong>";
    if (WiFi.status() == WL_CONNECTED && ssid == WiFi.SSID()) html += "<br><span class='badge ok'>Connected</span>";
    html += "</td>";
    html += "<td>" + String(rssi) + " dBm<br><span class='muted'>" + wifiStrengthLabel(rssi) + "</span></td>";
    html += "<td><span class='badge " + wifiSecurityCssClass(enc) + "'>" + wifiSecurityLabel(enc) + "</span></td>";
    if (savedSlot > 0) html += "<td><span class='badge info'>Saved in NVS slot " + String(savedSlot) + "</span></td>";
    else html += "<td><span class='badge off'>Not saved</span></td>";
    html += "<td>";
    html += "<button type='button' class='btn secondary smallbtn' data-ssid='" + htmlEscape(ssid) + "' onclick='fillWifiSlot(this.dataset.ssid,1)'>Slot 1</button>";
    html += "<button type='button' class='btn secondary smallbtn' data-ssid='" + htmlEscape(ssid) + "' onclick='fillWifiSlot(this.dataset.ssid,2)'>Slot 2</button>";
    html += "<button type='button' class='btn secondary smallbtn' data-ssid='" + htmlEscape(ssid) + "' onclick='fillWifiSlot(this.dataset.ssid,3)'>Slot 3</button>";
    html += "</td></tr>";
    rows++;
  }

  html += "</table></div>";
  WiFi.scanDelete();
  return html;
}

String wifiSettingsCardHtml() {
  String html = "<div class='card blueline' id='wifi'><h2>Wi-Fi networks</h2>";
  html += "<p class='muted'>Save up to three preferred networks in NVS. The ESP32 tries Slot 1 first, then Slot 2, then Slot 3. Put the strongest/preferred home network in Slot 1.</p>";
  html += "<div class='goodbox'>Current connection: <b>" + htmlEscape(WiFi.SSID()) + "</b> | IP: <b>" + WiFi.localIP().toString() + "</b> | Alias: <b>" + htmlEscape(getLocalMdnsHost()) + "</b> | RSSI: <b>" + String(WiFi.RSSI()) + " dBm</b></div>";
  html += savedWifiProfilesSummaryHtml();
  html += "<div class='grid'>";
  html += "<div><label>Wi-Fi 1 SSID</label><input id='wifi_ssid1' name='wifi_ssid1' value='" + htmlEscape(appConfig.wifiSsid1) + "'><label>Wi-Fi 1 password</label><input type='password' name='wifi_pass1' placeholder='Leave blank to keep current'></div>";
  html += "<div><label>Wi-Fi 2 SSID</label><input id='wifi_ssid2' name='wifi_ssid2' value='" + htmlEscape(appConfig.wifiSsid2) + "'><label>Wi-Fi 2 password</label><input type='password' name='wifi_pass2' placeholder='Leave blank to keep current'></div>";
  html += "<div><label>Wi-Fi 3 SSID</label><input id='wifi_ssid3' name='wifi_ssid3' value='" + htmlEscape(appConfig.wifiSsid3) + "'><label>Wi-Fi 3 password</label><input type='password' name='wifi_pass3' placeholder='Leave blank to keep current'></div>";
  html += "</div>";
  html += "<div class='buttonrow'><button class='btn' type='submit'>Save Wi-Fi only later</button><button class='btn warn' type='submit' formaction='/save-wifi-reconnect'>Save and reconnect/restart</button></div>";
  html += "<h3>Nearby networks</h3>";
  html += "<p class='muted'>Use the strongest secured network where possible. Open networks are shown in red and are not recommended for a care device.</p>";
  html += wifiScanTableHtml();
  html += "</div>";
  return html;
}


// ==================================================
// 9B. LOCAL ENCRYPTION FOR TWILIO SETTINGS
// ==================================================

const char* TWILIO_ENC_SALT = "LEAH_R2_TWILIO_LOCAL_STORE_V1_HS_2026";

void deriveTwilioKey(uint8_t key[32]) {
  uint64_t mac = ESP.getEfuseMac();
  String seed = String((uint32_t)(mac >> 32), HEX);
  seed += String((uint32_t)mac, HEX);
  seed += "::";
  seed += TWILIO_ENC_SALT;

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const unsigned char*)seed.c_str(), seed.length());
  mbedtls_sha256_finish(&ctx, key);
  mbedtls_sha256_free(&ctx);
}

String base64EncodeLocal(const uint8_t* data, size_t len) {
  size_t olen = 0;
  mbedtls_base64_encode(NULL, 0, &olen, data, len);

  unsigned char* out = (unsigned char*)malloc(olen + 1);
  if (!out) return "";

  if (mbedtls_base64_encode(out, olen, &olen, data, len) != 0) {
    free(out);
    return "";
  }

  out[olen] = '\0';
  String result = String((char*)out);
  free(out);
  return result;
}

bool base64DecodeLocal(const String& b64, uint8_t** outData, size_t* outLen) {
  *outData = NULL;
  *outLen = 0;

  size_t olen = 0;
  int rc = mbedtls_base64_decode(NULL, 0, &olen,
                                 (const unsigned char*)b64.c_str(), b64.length());
  if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && rc != 0) return false;

  unsigned char* buf = (unsigned char*)malloc(olen + 1);
  if (!buf) return false;

  rc = mbedtls_base64_decode(buf, olen, &olen,
                             (const unsigned char*)b64.c_str(), b64.length());
  if (rc != 0) {
    free(buf);
    return false;
  }

  *outData = buf;
  *outLen = olen;
  return true;
}

String encryptLocalSecret(const String& plain) {
  if (plain.length() == 0) return "";

  uint8_t key[32];
  deriveTwilioKey(key);

  const size_t blockSize = 16;
  size_t plainLen = plain.length();
  uint8_t pad = blockSize - (plainLen % blockSize);
  if (pad == 0) pad = blockSize;

  size_t cipherLen = plainLen + pad;
  size_t totalLen = 16 + cipherLen;

  uint8_t* input = (uint8_t*)calloc(cipherLen, 1);
  uint8_t* output = (uint8_t*)calloc(totalLen, 1);
  if (!input || !output) {
    if (input) free(input);
    if (output) free(output);
    return "";
  }

  memcpy(input, plain.c_str(), plainLen);
  for (size_t i = plainLen; i < cipherLen; i++) input[i] = pad;

  for (int i = 0; i < 16; i += 4) {
    uint32_t r = esp_random();
    output[i + 0] = (r >> 24) & 0xFF;
    output[i + 1] = (r >> 16) & 0xFF;
    output[i + 2] = (r >> 8) & 0xFF;
    output[i + 3] = r & 0xFF;
  }

  uint8_t iv[16];
  memcpy(iv, output, 16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int rc = mbedtls_aes_setkey_enc(&aes, key, 256);
  if (rc == 0) rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, cipherLen, iv, input, output + 16);
  mbedtls_aes_free(&aes);

  String result = "";
  if (rc == 0) result = base64EncodeLocal(output, totalLen);

  free(input);
  free(output);
  return result;
}

String decryptLocalSecret(const String& encryptedB64) {
  if (encryptedB64.length() == 0) return "";

  uint8_t* raw = NULL;
  size_t rawLen = 0;
  if (!base64DecodeLocal(encryptedB64, &raw, &rawLen)) return "";

  if (rawLen < 32 || ((rawLen - 16) % 16) != 0) {
    free(raw);
    return "";
  }

  uint8_t key[32];
  deriveTwilioKey(key);

  size_t cipherLen = rawLen - 16;
  uint8_t* plain = (uint8_t*)calloc(cipherLen + 1, 1);
  if (!plain) {
    free(raw);
    return "";
  }

  uint8_t iv[16];
  memcpy(iv, raw, 16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int rc = mbedtls_aes_setkey_dec(&aes, key, 256);
  if (rc == 0) rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipherLen, iv, raw + 16, plain);
  mbedtls_aes_free(&aes);

  String result = "";
  if (rc == 0) {
    uint8_t pad = plain[cipherLen - 1];
    if (pad > 0 && pad <= 16 && pad <= cipherLen) {
      bool padOk = true;
      for (size_t i = cipherLen - pad; i < cipherLen; i++) {
        if (plain[i] != pad) { padOk = false; break; }
      }
      if (padOk) {
        size_t realLen = cipherLen - pad;
        plain[realLen] = '\0';
        result = String((char*)plain);
      }
    }
  }

  free(raw);
  free(plain);
  return result;
}

String maskValue(String s, uint8_t showStart = 4, uint8_t showEnd = 4) {
  s.trim();
  if (s.length() == 0) return "(not set)";
  if (s.length() <= showStart + showEnd + 2) return "********";
  return s.substring(0, showStart) + "********" + s.substring(s.length() - showEnd);
}

String urlEncode(String text) {
  String encoded = "";
  const char *hex = "0123456789ABCDEF";

  for (size_t i = 0; i < text.length(); i++) {
    uint8_t c = (uint8_t)text.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else if (c == ' ') {
      encoded += "%20";
    } else if (c == '\n') {
      encoded += "%0A";
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

String readHttpResponseSnippet(HTTPClient &http, size_t maxBytes, unsigned long timeoutMs) {
  String response = "";
  response.reserve(min(maxBytes, (size_t)512));

  WiFiClient *stream = http.getStreamPtr();
  if (!stream) {
    return response;
  }

  unsigned long startMs = millis();
  unsigned long lastDataMs = millis();

  while (millis() - startMs < timeoutMs) {
    while (stream->available()) {
      int c = stream->read();
      if (c < 0) break;

      if (response.length() < maxBytes) {
        response += (char)c;
      }

      lastDataMs = millis();
    }

    if (response.length() > 0 && millis() - lastDataMs > 250UL) {
      break;
    }

    if (!stream->connected() && !stream->available()) {
      break;
    }

    delay(1);
    yield();
  }

  return response;
}

bool cloudAlertCooldownActive() {
  if (lastCloudAlertAttemptMs == 0) return false;
  return millis() - lastCloudAlertAttemptMs < CLOUD_ALERT_MIN_GAP_MS;
}


String normalizeNightscoutHost(String host) {
  host.trim();
  host.replace("https://", "");
  host.replace("http://", "");

  int slashIndex = host.indexOf('/');
  if (slashIndex >= 0) {
    host = host.substring(0, slashIndex);
  }

  host.trim();
  return host;
}

bool hasNightscoutConfig() {
  return appConfig.nightscoutHost.length() > 0 && appConfig.nightscoutToken.length() > 0;
}

String getMacCompact() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  return mac;
}

String getMacSuffix(uint8_t chars) {
  String mac = getMacCompact();
  if (mac.length() <= chars) return mac;
  return mac.substring(mac.length() - chars);
}

String getSetupApSsid() {
  // Fixed first-time setup hotspot name for the OLED 2.42-inch product.
  return "leah 2R - 2.42I";
}

String getSetupApPassword() {
  // Fixed setup hotspot password. Keep this unchanged for user documentation
  // and make it visible on the OLED whenever the configuration portal opens.
  return "leah00000000";
}

String getDefaultAdminPassword() {
  // Prototype/beta fallback only.
  // This avoids leaving the local webserver unprotected if the tester forgets to set a password.
  // For commercial units, replace this with a factory-random password per unit.
  return "Admin" + getMacSuffix(8);
}

bool hasAdminPassword() {
  return appConfig.adminPassword.length() >= 8;
}

bool setupIsIncomplete() {
  return !hasNightscoutConfig() || !hasAdminPassword() || sanitizeLocalAliasName(appConfig.localAliasName).length() == 0;
}

bool requireWebLogin() {
  if (!hasAdminPassword()) {
    appConfig.adminPassword = getDefaultAdminPassword();
  }

  if (!webServer.authenticate(appConfig.adminUsername.c_str(), appConfig.adminPassword.c_str())) {
    webServer.requestAuthentication();
    return false;
  }

  return true;
}



// ==================================================
// 15B-1. DISPLAY CONTRAST / NIGHT DIMMING
// ==================================================

int getLocalMinutesOfDay() {
  time_t now = time(nullptr);

  if (now < 100000) {
    return -1;
  }

  struct tm timeInfo;
  localtime_r(&now, &timeInfo);

  return (timeInfo.tm_hour * 60) + timeInfo.tm_min;
}

String getLocalTimeText() {
  time_t now = time(nullptr);

  if (now < 100000) {
    return "not synced";
  }

  struct tm timeInfo;
  localtime_r(&now, &timeInfo);

  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
           timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);

  return String(buffer);
}

String getLocalHeaderTimeText() {
  time_t now = time(nullptr);

  if (now < 100000) {
    return "--:--";
  }

  struct tm timeInfo;
  localtime_r(&now, &timeInfo);

  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
  return String(buffer);
}

String formatTimestampShort(long long timestampMs) {
  if (timestampMs <= 0) return "Unknown";

  time_t t = (time_t)(timestampMs / 1000LL);
  if (t < 100000) return "Unknown";

  struct tm timeInfo;
  localtime_r(&t, &timeInfo);

  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02d/%02d %02d:%02d",
           timeInfo.tm_mday,
           timeInfo.tm_mon + 1,
           timeInfo.tm_hour,
           timeInfo.tm_min);

  return String(buffer);
}

String formatTimestampTimeOnly(long long timestampMs) {
  if (timestampMs <= 0) return "--:--";

  time_t t = (time_t)(timestampMs / 1000LL);
  if (t < 100000) return "--:--";

  struct tm timeInfo;
  localtime_r(&t, &timeInfo);

  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);

  return String(buffer);
}

long long getStartOfTodayLocalMs() {
  time_t now = time(nullptr);
  if (now < 100000) return 0;

  struct tm timeInfo;
  localtime_r(&now, &timeInfo);
  timeInfo.tm_hour = 0;
  timeInfo.tm_min = 0;
  timeInfo.tm_sec = 0;

  time_t midnight = mktime(&timeInfo);
  if (midnight < 100000) return 0;

  return (long long)midnight * 1000LL;
}

long long getStartOfYesterdayLocalMs() {
  time_t now = time(nullptr);
  if (now < 100000) return 0;

  struct tm timeInfo;
  localtime_r(&now, &timeInfo);
  timeInfo.tm_mday -= 1;
  timeInfo.tm_hour = 0;
  timeInfo.tm_min = 0;
  timeInfo.tm_sec = 0;

  time_t midnight = mktime(&timeInfo);
  if (midnight < 100000) return 0;
  return (long long)midnight * 1000LL;
}

long long getEndOfTodayLocalMs() {
  long long startMs = getStartOfTodayLocalMs();
  if (startMs <= 0) return 0;
  return startMs + 86400000LL;
}

String formatUtcIsoFromMs(long long timestampMs) {
  if (timestampMs <= 0) return "";

  time_t t = (time_t)(timestampMs / 1000LL);
  if (t < 100000) return "";

  struct tm timeInfo;
  gmtime_r(&t, &timeInfo);

  char buffer[25];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
           timeInfo.tm_year + 1900,
           timeInfo.tm_mon + 1,
           timeInfo.tm_mday,
           timeInfo.tm_hour,
           timeInfo.tm_min,
           timeInfo.tm_sec);

  return String(buffer);
}

String getLocalAliasUrl() {
  return String("http://") + getLocalMdnsHost();
}

String getDimScheduleText() {
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%02d:%02d-%02d:%02d",
           appConfig.dimStartHour,
           appConfig.dimStartMinute,
           appConfig.dimEndHour,
           appConfig.dimEndMinute);

  return String(buffer);
}


bool isNightDimActive() {
  if (!appConfig.nightDimEnabled) {
    return false;
  }

  int nowMin = getLocalMinutesOfDay();

  if (nowMin < 0) {
    return false;
  }

  int startMin = (appConfig.dimStartHour * 60) + appConfig.dimStartMinute;
  int endMin = (appConfig.dimEndHour * 60) + appConfig.dimEndMinute;

  if (startMin == endMin) {
    return false;
  }

  // Same-day dim period, for example 13:00 to 18:00
  if (startMin < endMin) {
    return nowMin >= startMin && nowMin < endMin;
  }

  // Overnight dim period, for example 21:00 to 07:00
  return nowMin >= startMin || nowMin < endMin;
}

uint8_t getActiveDisplayContrast() {
  if (isNightDimActive()) {
    return constrain(appConfig.nightContrast, 20, 255);
  }

  return constrain(appConfig.displayContrast, 20, 255);
}

String getDimmingStatusText() {
  if (!appConfig.nightDimEnabled) {
    return "Off";
  }

  String status = isNightDimActive() ? "Night " : "Day ";
  status += String(getActiveDisplayContrast());
  return status;
}

void applyDisplayContrast() {
  uint8_t targetContrast = getActiveDisplayContrast();

  if (currentAppliedContrast != targetContrast) {
    u8g2.setContrast(targetContrast);
    currentAppliedContrast = targetContrast;

    Serial.print("OLED contrast set to: ");
    Serial.print(targetContrast);
    Serial.print(" | Local time: ");
    Serial.print(getLocalTimeText());
    Serial.print(" | Dim status: ");
    Serial.println(getDimmingStatusText());
  }
}

void drawUpdateEnvelopeIcon(int x, int y) {
  // Small 9 x 7 envelope icon for update notification.
  // Drawn only when a newer firmware version is available.
  u8g2.drawFrame(x, y, 9, 7);
  u8g2.drawLine(x, y, x + 4, y + 4);
  u8g2.drawLine(x + 8, y, x + 4, y + 4);
  u8g2.drawLine(x, y + 6, x + 3, y + 3);
  u8g2.drawLine(x + 8, y + 6, x + 5, y + 3);
}

bool shouldShowUpdateEnvelopeIcon() {
  if (!updateAvailable) {
    return false;
  }

  // Gentle pulse: visible most of the time, off briefly.
  unsigned long phase = millis() % 2000UL;
  return phase < 1500UL;
}

void drawPatientLabelNearWiFi() {
  // Top-row layout on the 128x64 OLED:
  // [patient name, clipped] [HH:MM] [update envelope] [Wi-Fi bars]
  // The Wi-Fi bars are drawn separately at x=106.  Keep the envelope just left
  // of the bars and the clock left of the envelope.  The name gets all remaining
  // space and is clipped by pixel width before it reaches the clock.
  const int nameX = 4;
  const int nameY = 12;
  const int timeX = 66;
  const int timeY = 12;
  const int envelopeX = 94;
  const int envelopeY = 4;
  const int nameMaxWidth = timeX - nameX - 4;

  u8g2.setFont(u8g2_font_6x12_tr);
  String nameLabel = shortenTextToPixelWidth(appConfig.patientName, nameMaxWidth);
  u8g2.setCursor(nameX, nameY);
  u8g2.print(nameLabel);

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(timeX, timeY);
  u8g2.print(getLocalHeaderTimeText());

  if (shouldShowUpdateEnvelopeIcon()) {
    drawUpdateEnvelopeIcon(envelopeX, envelopeY);
  }
}

void applyConfigToRuntime() {
  urgentLowLimitMmol  = appConfig.urgentLow;
  lowLimitMmol        = appConfig.low;
  highLimitMmol       = appConfig.high;
  urgentHighLimitMmol = appConfig.urgentHigh;

  sirenEnabled = appConfig.alarmSoundEnabled;

  appConfig.displayContrast = constrain(appConfig.displayContrast, 20, 255);
  appConfig.nightContrast = constrain(appConfig.nightContrast, 20, 255);
  appConfig.dimStartHour = constrain(appConfig.dimStartHour, 0, 23);
  appConfig.dimStartMinute = constrain(appConfig.dimStartMinute, 0, 59);
  appConfig.dimEndHour = constrain(appConfig.dimEndHour, 0, 23);
  appConfig.dimEndMinute = constrain(appConfig.dimEndMinute, 0, 59);

  appConfig.localAliasName = sanitizeLocalAliasName(appConfig.localAliasName);
  appConfig.glucoseUnits = normalizeGlucoseUnits(appConfig.glucoseUnits);
  appConfig.insulinActionHours = constrain(appConfig.insulinActionHours, 1.0, 8.0);

  // SSIDs are normalized, but Wi-Fi passwords are kept byte-for-byte.
  // Leading or trailing spaces are legal password characters and must not be removed.
  appConfig.wifiSsid1.trim();
  appConfig.wifiSsid2.trim();
  appConfig.wifiSsid3.trim();
  if (appConfig.wifiSsid1.length() == 0) appConfig.wifiPass1 = "";
  if (appConfig.wifiSsid2.length() == 0) appConfig.wifiPass2 = "";
  if (appConfig.wifiSsid3.length() == 0) appConfig.wifiPass3 = "";

  appConfig.sensorSource = normalizeSensorSource(appConfig.sensorSource);
  appConfig.sensorSerial.trim();
  appConfig.sensorStartDate.trim();
  appConfig.sensorStartTime = normalizeSensorStartTime(appConfig.sensorStartTime);
  appConfig.sensorWearDays = constrain(appConfig.sensorWearDays, 1, 30);

  appConfig.lowRepeatMinutes = constrain(appConfig.lowRepeatMinutes, 1, 240);
  appConfig.urgentLowRepeatMinutes = constrain(appConfig.urgentLowRepeatMinutes, 1, 240);
  appConfig.highRepeatMinutes = constrain(appConfig.highRepeatMinutes, 1, 240);
  appConfig.urgentHighRepeatMinutes = constrain(appConfig.urgentHighRepeatMinutes, 1, 240);
  appConfig.noDataRepeatMinutes = constrain(appConfig.noDataRepeatMinutes, 1, 240);
  appConfig.phoneBatteryAlertPercent = constrain(appConfig.phoneBatteryAlertPercent, 1, 100);
  appConfig.phoneBatteryRepeatMinutes = constrain(appConfig.phoneBatteryRepeatMinutes, 5, 1440);
  appConfig.sensorExpiryReminderDays = constrain(appConfig.sensorExpiryReminderDays, 1, 14);
  appConfig.sensorExpiryRepeatHours = constrain(appConfig.sensorExpiryRepeatHours, 1, 168);
  appConfig.alertProvider = normalizeAlertProvider(appConfig.alertProvider);
  applyAlertProviderToEnabledFlags();
  appConfig.updateChannel = normalizeUpdateChannel(appConfig.updateChannel);

  applyDisplayContrast();
}

void loadConfig() {
  preferences.begin("cgm_cfg", true);

  appConfig.deviceName        = preferences.getString("devName", "Leah 2R Displays");
  appConfig.patientName       = preferences.getString("patient", "Leah");
  appConfig.deviceLocation    = preferences.getString("loc", "Bedroom");
  bool aliasWasStored         = preferences.isKey("mdnsName");
  bool existingUnitConfig     = preferences.isKey("nsHost") || preferences.isKey("devName");
  appConfig.localAliasName    = preferences.getString("mdnsName", existingUnitConfig ? DEFAULT_MDNS_NAME : "");
  appConfig.nightscoutHost    = preferences.getString("nsHost", "");
  appConfig.nightscoutToken   = preferences.getString("nsToken", "");
  appConfig.wifiSsid1         = preferences.getString("wSsid1", "");
  appConfig.wifiPass1         = decryptLocalSecret(preferences.getString("wPass1", ""));
  appConfig.wifiSsid2         = preferences.getString("wSsid2", "");
  appConfig.wifiPass2         = decryptLocalSecret(preferences.getString("wPass2", ""));
  appConfig.wifiSsid3         = preferences.getString("wSsid3", "");
  appConfig.wifiPass3         = decryptLocalSecret(preferences.getString("wPass3", ""));
  appConfig.glucoseUnits      = normalizeGlucoseUnits(preferences.getString("gluUnit", "MMOL"));
  appConfig.insulinActionHours = preferences.getFloat("insActH", 4.0);
  appConfig.adminUsername     = preferences.getString("adminUser", "admin");
  appConfig.adminPassword     = preferences.getString("adminPass", "");

  appConfig.sensorAutoRead    = preferences.getBool("sensAuto", true);
  appConfig.sensorSource      = preferences.getString("sensSrc", "Auto");
  appConfig.sensorSerial      = decryptLocalSecret(preferences.getString("sensSer", ""));
  appConfig.sensorStartDate   = preferences.getString("sensDate", "");
  appConfig.sensorStartTime   = preferences.getString("sensTime", "00:00");
  appConfig.sensorWearDays    = preferences.getUChar("sensDays", 10);

  appConfig.useNightscoutLimits = preferences.getBool("useNsLim", false);

  appConfig.urgentLow  = preferences.getFloat("uLow", 3.0);
  appConfig.low        = preferences.getFloat("low", 3.9);
  appConfig.high       = preferences.getFloat("high", 10.0);
  appConfig.urgentHigh = preferences.getFloat("uHigh", 11.0);

  appConfig.muteLowMinutes    = preferences.getUShort("muteLow", 10);
  appConfig.muteHighMinutes   = preferences.getUShort("muteHigh", 20);
  appConfig.muteNoDataMinutes = preferences.getUShort("muteNoData", 10);

  appConfig.alarmSoundEnabled = preferences.getBool("sndOn", true);
  appConfig.randomAudioEnabled = preferences.getBool("randAud", true);
  appConfig.audioMuteMinutes  = preferences.getUShort("muteMin", 10);
  appConfig.soundUrgentLow    = preferences.getBool("sndULow", true);
  appConfig.soundLow          = preferences.getBool("sndLow", false);
  appConfig.soundHigh         = preferences.getBool("sndHigh", false);
  appConfig.soundUrgentHigh   = preferences.getBool("sndUHigh", true);
  appConfig.soundNoData       = preferences.getBool("sndNoDat", false);
  appConfig.displayContrast   = preferences.getUChar("contrast", 210);
  appConfig.nightDimEnabled   = preferences.getBool("nightDim", true);
  appConfig.nightContrast     = preferences.getUChar("nightCon", 120);
  appConfig.dimStartHour      = preferences.getUChar("dimSH", 21);
  appConfig.dimStartMinute    = preferences.getUChar("dimSM", 0);
  appConfig.dimEndHour        = preferences.getUChar("dimEH", 7);
  appConfig.dimEndMinute      = preferences.getUChar("dimEM", 0);

  appConfig.alertProvider          = preferences.getString("alertProv", "");
  appConfig.apiEndpoint            = decryptLocalSecret(preferences.getString("apiEnd", ""));
  appConfig.apiKey                 = decryptLocalSecret(preferences.getString("apiKey", ""));
  appConfig.apiParent1             = decryptLocalSecret(preferences.getString("apiP1", ""));
  appConfig.apiParent2             = decryptLocalSecret(preferences.getString("apiP2", ""));
  appConfig.twilioEnabled          = preferences.getBool("twEn", false);
  appConfig.twilioSid              = decryptLocalSecret(preferences.getString("twSid", ""));
  appConfig.twilioToken            = decryptLocalSecret(preferences.getString("twTok", ""));
  appConfig.twilioFrom             = decryptLocalSecret(preferences.getString("twFrom", ""));
  appConfig.twilioParent1          = decryptLocalSecret(preferences.getString("twP1", ""));
  appConfig.twilioParent2          = decryptLocalSecret(preferences.getString("twP2", ""));
  appConfig.callMeBotEnabled       = preferences.getBool("cbEn", false);
  appConfig.callMeBotParent1       = decryptLocalSecret(preferences.getString("cbP1", ""));
  appConfig.callMeBotApiKey1       = decryptLocalSecret(preferences.getString("cbK1", ""));
  appConfig.callMeBotParent2       = decryptLocalSecret(preferences.getString("cbP2", ""));
  appConfig.callMeBotApiKey2       = decryptLocalSecret(preferences.getString("cbK2", ""));
  appConfig.textMeBotEnabled       = preferences.getBool("tmbEn", false);
  appConfig.textMeBotApiKey        = decryptLocalSecret(preferences.getString("tmbKey", ""));
  appConfig.textMeBotParent1       = decryptLocalSecret(preferences.getString("tmbP1", ""));
  appConfig.textMeBotParent2       = decryptLocalSecret(preferences.getString("tmbP2", ""));
  appConfig.textMeBotParent3       = decryptLocalSecret(preferences.getString("tmbP3", ""));
  appConfig.caregiver1Name         = preferences.getString("cg1Name", "Caregiver 1");
  appConfig.caregiver2Name         = preferences.getString("cg2Name", "Caregiver 2");
  appConfig.caregiver3Name         = preferences.getString("cg3Name", "Caregiver 3");
  appConfig.cg1Low                 = preferences.getBool("cg1Low", true);
  appConfig.cg1UrgentLow           = preferences.getBool("cg1UL", true);
  appConfig.cg1High                = preferences.getBool("cg1High", true);
  appConfig.cg1UrgentHigh          = preferences.getBool("cg1UH", true);
  appConfig.cg1NoData              = preferences.getBool("cg1ND", true);
  appConfig.cg2Low                 = preferences.getBool("cg2Low", true);
  appConfig.cg2UrgentLow           = preferences.getBool("cg2UL", true);
  appConfig.cg2High                = preferences.getBool("cg2High", true);
  appConfig.cg2UrgentHigh          = preferences.getBool("cg2UH", true);
  appConfig.cg2NoData              = preferences.getBool("cg2ND", true);
  appConfig.cg3Low                 = preferences.getBool("cg3Low", true);
  appConfig.cg3UrgentLow           = preferences.getBool("cg3UL", true);
  appConfig.cg3High                = preferences.getBool("cg3High", true);
  appConfig.cg3UrgentHigh          = preferences.getBool("cg3UH", true);
  appConfig.cg3NoData              = preferences.getBool("cg3ND", true);
  appConfig.lowRepeatMinutes        = preferences.getUShort("repLow", 15);
  appConfig.urgentLowRepeatMinutes  = preferences.getUShort("repULow", 5);
  appConfig.highRepeatMinutes       = preferences.getUShort("repHigh", 30);
  appConfig.urgentHighRepeatMinutes = preferences.getUShort("repUHigh", 15);
  appConfig.noDataRepeatMinutes     = preferences.getUShort("repNoData", 30);
  appConfig.phoneBatteryAlertEnabled = preferences.getBool("phBatEn", true);
  appConfig.phoneBatteryAlertPercent = preferences.getUChar("phBatPct", 10);
  appConfig.phoneBatteryRepeatMinutes = preferences.getUShort("phBatRep", 60);
  appConfig.sensorExpiryReminderEnabled = preferences.getBool("sensRemEn", true);
  appConfig.sensorExpiryReminderDays = preferences.getUChar("sensRemD", 3);
  appConfig.sensorExpiryRepeatHours = preferences.getUShort("sensRemH", 24);
  appConfig.advancedWebView         = preferences.getBool("advView", false);
  appConfig.updateChannel           = normalizeUpdateChannel(preferences.getString("updChan", DEFAULT_UPDATE_CHANNEL));
  bool build71SirenMigrationDone     = preferences.getBool("b71Siren", false);

  preferences.end();

  // Build 71 GPIO migration: re-enable the GPIO25 local siren master once after OTA.
  // Per-alarm sound routing remains under user control in the Web UI.
  if (!build71SirenMigrationDone && existingUnitConfig) {
    appConfig.alarmSoundEnabled = true;
    preferences.begin("cgm_cfg", false);
    preferences.putBool("sndOn", true);
    preferences.putBool("b71Siren", true);
    preferences.end();
    Serial.println("Build 71 migration: GPIO25 buzzer/siren master enabled.");
  }

  // Existing upgraded units retain the historical alias; fresh units are asked to choose one.
  if (!aliasWasStored && existingUnitConfig && appConfig.localAliasName.length() == 0) {
    appConfig.localAliasName = DEFAULT_MDNS_NAME;
  }

  // Migrate legacy default branding while preserving any custom device name.
  String legacyDeviceName = appConfig.deviceName;
  legacyDeviceName.trim();
  if (legacyDeviceName.length() == 0 ||
      legacyDeviceName.equalsIgnoreCase("Leah CGM") ||
      legacyDeviceName.equalsIgnoreCase("Leah CGM Display") ||
      legacyDeviceName.equalsIgnoreCase("Leah R2 Care Display") ||
      legacyDeviceName.equalsIgnoreCase("Leah R2 Display")) {
    appConfig.deviceName = "Leah 2R Displays";
  }

  appConfig.nightscoutHost = normalizeNightscoutHost(appConfig.nightscoutHost);
  if (appConfig.twilioFrom.length() == 0) appConfig.twilioFrom = "whatsapp:+14155238886";
  if (appConfig.alertProvider.length() == 0) {
    if (appConfig.textMeBotEnabled) appConfig.alertProvider = "TEXTMEBOT";
    else if (appConfig.callMeBotEnabled) appConfig.alertProvider = "CALLMEBOT";
    else if (appConfig.twilioEnabled) appConfig.alertProvider = "TWILIO";
    else appConfig.alertProvider = "OFF";
  }
  appConfig.alertProvider = normalizeAlertProvider(appConfig.alertProvider);
  applyAlertProviderToEnabledFlags();
  appConfig.apiEndpoint.trim();
  appConfig.apiKey.trim();
  appConfig.apiParent1.trim();
  appConfig.apiParent2.trim();
  appConfig.callMeBotParent1.trim();
  appConfig.callMeBotParent2.trim();
  appConfig.callMeBotApiKey1.trim();
  appConfig.callMeBotApiKey2.trim();
  appConfig.textMeBotParent1.trim();
  appConfig.textMeBotParent2.trim();
  appConfig.textMeBotParent3.trim();
  appConfig.textMeBotApiKey.trim();
  appConfig.caregiver1Name.trim();
  appConfig.caregiver2Name.trim();
  appConfig.caregiver3Name.trim();
  if (appConfig.caregiver1Name.length() == 0) appConfig.caregiver1Name = "Caregiver 1";
  if (appConfig.caregiver2Name.length() == 0) appConfig.caregiver2Name = "Caregiver 2";
  if (appConfig.caregiver3Name.length() == 0) appConfig.caregiver3Name = "Caregiver 3";
  appConfig.glucoseUnits = normalizeGlucoseUnits(appConfig.glucoseUnits);
  appConfig.insulinActionHours = constrain(appConfig.insulinActionHours, 1.0, 8.0);

  appConfig.adminUsername.trim();
  appConfig.adminPassword.trim();
  appConfig.sensorStartDate.trim();
  appConfig.sensorStartTime = normalizeSensorStartTime(appConfig.sensorStartTime);
  appConfig.sensorWearDays = constrain(appConfig.sensorWearDays, 1, 30);

  if (appConfig.adminUsername.length() == 0) {
    appConfig.adminUsername = "admin";
  }

  if (appConfig.urgentLow <= 0) appConfig.urgentLow = 3.0;
  if (appConfig.low <= 0) appConfig.low = 3.9;
  if (appConfig.high <= 0) appConfig.high = 10.0;
  if (appConfig.urgentHigh <= 0) appConfig.urgentHigh = 11.0;

  appConfig.displayContrast = constrain(appConfig.displayContrast, 20, 255);
  appConfig.nightContrast = constrain(appConfig.nightContrast, 20, 255);
  appConfig.dimStartHour = constrain(appConfig.dimStartHour, 0, 23);
  appConfig.dimStartMinute = constrain(appConfig.dimStartMinute, 0, 59);
  appConfig.dimEndHour = constrain(appConfig.dimEndHour, 0, 23);
  appConfig.dimEndMinute = constrain(appConfig.dimEndMinute, 0, 59);

  appConfig.audioMuteMinutes = constrain(appConfig.audioMuteMinutes, 1, 30);
  appConfig.muteLowMinutes = appConfig.audioMuteMinutes;
  appConfig.muteHighMinutes = appConfig.audioMuteMinutes;
  appConfig.muteNoDataMinutes = appConfig.audioMuteMinutes;

  appConfig.lowRepeatMinutes = constrain(appConfig.lowRepeatMinutes, 1, 240);
  appConfig.urgentLowRepeatMinutes = constrain(appConfig.urgentLowRepeatMinutes, 1, 240);
  appConfig.highRepeatMinutes = constrain(appConfig.highRepeatMinutes, 1, 240);
  appConfig.urgentHighRepeatMinutes = constrain(appConfig.urgentHighRepeatMinutes, 1, 240);
  appConfig.noDataRepeatMinutes = constrain(appConfig.noDataRepeatMinutes, 1, 240);
  appConfig.phoneBatteryAlertPercent = constrain(appConfig.phoneBatteryAlertPercent, 1, 100);
  appConfig.phoneBatteryRepeatMinutes = constrain(appConfig.phoneBatteryRepeatMinutes, 5, 1440);
  appConfig.sensorExpiryReminderDays = constrain(appConfig.sensorExpiryReminderDays, 1, 14);
  appConfig.sensorExpiryRepeatHours = constrain(appConfig.sensorExpiryRepeatHours, 1, 168);
  appConfig.alertProvider = normalizeAlertProvider(appConfig.alertProvider);
  applyAlertProviderToEnabledFlags();
  appConfig.updateChannel = normalizeUpdateChannel(appConfig.updateChannel);
}

void saveConfig() {
  preferences.begin("cgm_cfg", false);

  preferences.putString("devName", appConfig.deviceName);
  preferences.putString("patient", appConfig.patientName);
  preferences.putString("loc", appConfig.deviceLocation);
  preferences.putString("mdnsName", sanitizeLocalAliasName(appConfig.localAliasName));
  preferences.putString("nsHost", normalizeNightscoutHost(appConfig.nightscoutHost));
  preferences.putString("nsToken", appConfig.nightscoutToken);
  preferences.putString("wSsid1", appConfig.wifiSsid1);
  preferences.putString("wPass1", encryptLocalSecret(appConfig.wifiPass1));
  preferences.putString("wSsid2", appConfig.wifiSsid2);
  preferences.putString("wPass2", encryptLocalSecret(appConfig.wifiPass2));
  preferences.putString("wSsid3", appConfig.wifiSsid3);
  preferences.putString("wPass3", encryptLocalSecret(appConfig.wifiPass3));
  preferences.putString("gluUnit", normalizeGlucoseUnits(appConfig.glucoseUnits));
  preferences.putFloat("insActH", appConfig.insulinActionHours);
  preferences.putString("adminUser", appConfig.adminUsername);
  preferences.putString("adminPass", appConfig.adminPassword);

  preferences.putBool("sensAuto", appConfig.sensorAutoRead);
  preferences.putString("sensSrc", appConfig.sensorSource);
  preferences.putString("sensSer", encryptLocalSecret(appConfig.sensorSerial));
  preferences.putString("sensDate", appConfig.sensorStartDate);
  preferences.putString("sensTime", appConfig.sensorStartTime);
  preferences.putUChar("sensDays", appConfig.sensorWearDays);

  preferences.putBool("useNsLim", appConfig.useNightscoutLimits);

  preferences.putFloat("uLow", appConfig.urgentLow);
  preferences.putFloat("low", appConfig.low);
  preferences.putFloat("high", appConfig.high);
  preferences.putFloat("uHigh", appConfig.urgentHigh);

  preferences.putUShort("muteLow", appConfig.muteLowMinutes);
  preferences.putUShort("muteHigh", appConfig.muteHighMinutes);
  preferences.putUShort("muteNoData", appConfig.muteNoDataMinutes);

  preferences.putBool("sndOn", appConfig.alarmSoundEnabled);
  preferences.putBool("randAud", appConfig.randomAudioEnabled);
  preferences.putUShort("muteMin", appConfig.audioMuteMinutes);
  preferences.putBool("sndULow", appConfig.soundUrgentLow);
  preferences.putBool("sndLow", appConfig.soundLow);
  preferences.putBool("sndHigh", appConfig.soundHigh);
  preferences.putBool("sndUHigh", appConfig.soundUrgentHigh);
  preferences.putBool("sndNoDat", appConfig.soundNoData);
  preferences.putUChar("contrast", appConfig.displayContrast);
  preferences.putBool("nightDim", appConfig.nightDimEnabled);
  preferences.putUChar("nightCon", appConfig.nightContrast);
  preferences.putUChar("dimSH", appConfig.dimStartHour);
  preferences.putUChar("dimSM", appConfig.dimStartMinute);
  preferences.putUChar("dimEH", appConfig.dimEndHour);
  preferences.putUChar("dimEM", appConfig.dimEndMinute);

  preferences.putString("alertProv", normalizeAlertProvider(appConfig.alertProvider));
  preferences.putString("apiEnd", encryptLocalSecret(appConfig.apiEndpoint));
  preferences.putString("apiKey", encryptLocalSecret(appConfig.apiKey));
  preferences.putString("apiP1", encryptLocalSecret(appConfig.apiParent1));
  preferences.putString("apiP2", encryptLocalSecret(appConfig.apiParent2));
  preferences.putBool("twEn", appConfig.twilioEnabled);
  preferences.putString("twSid", encryptLocalSecret(appConfig.twilioSid));
  preferences.putString("twTok", encryptLocalSecret(appConfig.twilioToken));
  preferences.putString("twFrom", encryptLocalSecret(appConfig.twilioFrom));
  preferences.putString("twP1", encryptLocalSecret(appConfig.twilioParent1));
  preferences.putString("twP2", encryptLocalSecret(appConfig.twilioParent2));
  preferences.putBool("cbEn", appConfig.callMeBotEnabled);
  preferences.putString("cbP1", encryptLocalSecret(appConfig.callMeBotParent1));
  preferences.putString("cbK1", encryptLocalSecret(appConfig.callMeBotApiKey1));
  preferences.putString("cbP2", encryptLocalSecret(appConfig.callMeBotParent2));
  preferences.putString("cbK2", encryptLocalSecret(appConfig.callMeBotApiKey2));
  preferences.putBool("tmbEn", appConfig.textMeBotEnabled);
  preferences.putString("tmbKey", encryptLocalSecret(appConfig.textMeBotApiKey));
  preferences.putString("tmbP1", encryptLocalSecret(appConfig.textMeBotParent1));
  preferences.putString("tmbP2", encryptLocalSecret(appConfig.textMeBotParent2));
  preferences.putString("tmbP3", encryptLocalSecret(appConfig.textMeBotParent3));
  preferences.putString("cg1Name", appConfig.caregiver1Name);
  preferences.putString("cg2Name", appConfig.caregiver2Name);
  preferences.putString("cg3Name", appConfig.caregiver3Name);
  preferences.putBool("cg1Low", appConfig.cg1Low);
  preferences.putBool("cg1UL", appConfig.cg1UrgentLow);
  preferences.putBool("cg1High", appConfig.cg1High);
  preferences.putBool("cg1UH", appConfig.cg1UrgentHigh);
  preferences.putBool("cg1ND", appConfig.cg1NoData);
  preferences.putBool("cg2Low", appConfig.cg2Low);
  preferences.putBool("cg2UL", appConfig.cg2UrgentLow);
  preferences.putBool("cg2High", appConfig.cg2High);
  preferences.putBool("cg2UH", appConfig.cg2UrgentHigh);
  preferences.putBool("cg2ND", appConfig.cg2NoData);
  preferences.putBool("cg3Low", appConfig.cg3Low);
  preferences.putBool("cg3UL", appConfig.cg3UrgentLow);
  preferences.putBool("cg3High", appConfig.cg3High);
  preferences.putBool("cg3UH", appConfig.cg3UrgentHigh);
  preferences.putBool("cg3ND", appConfig.cg3NoData);
  preferences.putUShort("repLow", appConfig.lowRepeatMinutes);
  preferences.putUShort("repULow", appConfig.urgentLowRepeatMinutes);
  preferences.putUShort("repHigh", appConfig.highRepeatMinutes);
  preferences.putUShort("repUHigh", appConfig.urgentHighRepeatMinutes);
  preferences.putUShort("repNoData", appConfig.noDataRepeatMinutes);
  preferences.putBool("phBatEn", appConfig.phoneBatteryAlertEnabled);
  preferences.putUChar("phBatPct", appConfig.phoneBatteryAlertPercent);
  preferences.putUShort("phBatRep", appConfig.phoneBatteryRepeatMinutes);
  preferences.putBool("sensRemEn", appConfig.sensorExpiryReminderEnabled);
  preferences.putUChar("sensRemD", appConfig.sensorExpiryReminderDays);
  preferences.putUShort("sensRemH", appConfig.sensorExpiryRepeatHours);
  preferences.putBool("advView", appConfig.advancedWebView);
  preferences.putString("updChan", normalizeUpdateChannel(appConfig.updateChannel));

  preferences.end();

  applyConfigToRuntime();
}

void normalizeWiFiProfilesInMemory() {
  appConfig.wifiSsid1.trim();
  appConfig.wifiSsid2.trim();
  appConfig.wifiSsid3.trim();

  if (appConfig.wifiSsid1.length() == 0) appConfig.wifiPass1 = "";
  if (appConfig.wifiSsid2.length() == 0) appConfig.wifiPass2 = "";
  if (appConfig.wifiSsid3.length() == 0) appConfig.wifiPass3 = "";

  // Retain the first occurrence and remove later duplicate SSIDs.
  if (appConfig.wifiSsid2.length() > 0 && appConfig.wifiSsid2 == appConfig.wifiSsid1) {
    appConfig.wifiSsid2 = "";
    appConfig.wifiPass2 = "";
  }
  if (appConfig.wifiSsid3.length() > 0 &&
      (appConfig.wifiSsid3 == appConfig.wifiSsid1 || appConfig.wifiSsid3 == appConfig.wifiSsid2)) {
    appConfig.wifiSsid3 = "";
    appConfig.wifiPass3 = "";
  }
}

bool verifyWiFiProfilesInNvs() {
  Preferences verifyPreferences;
  if (!verifyPreferences.begin("cgm_cfg", true)) {
    Serial.println("Wi-Fi NVS verify failed: namespace could not be opened.");
    return false;
  }

  String storedSsid1 = verifyPreferences.getString("wSsid1", "");
  String storedPass1 = decryptLocalSecret(verifyPreferences.getString("wPass1", ""));
  String storedSsid2 = verifyPreferences.getString("wSsid2", "");
  String storedPass2 = decryptLocalSecret(verifyPreferences.getString("wPass2", ""));
  String storedSsid3 = verifyPreferences.getString("wSsid3", "");
  String storedPass3 = decryptLocalSecret(verifyPreferences.getString("wPass3", ""));
  verifyPreferences.end();

  bool verified =
      storedSsid1 == appConfig.wifiSsid1 && storedPass1 == appConfig.wifiPass1 &&
      storedSsid2 == appConfig.wifiSsid2 && storedPass2 == appConfig.wifiPass2 &&
      storedSsid3 == appConfig.wifiSsid3 && storedPass3 == appConfig.wifiPass3;

  Serial.print("Wi-Fi NVS profile verification: ");
  Serial.println(verified ? "PASS" : "FAILED");
  Serial.print("  Slot 1: "); Serial.println(storedSsid1.length() ? storedSsid1 : "(empty)");
  Serial.print("  Slot 2: "); Serial.println(storedSsid2.length() ? storedSsid2 : "(empty)");
  Serial.print("  Slot 3: "); Serial.println(storedSsid3.length() ? storedSsid3 : "(empty)");
  return verified;
}

bool rememberConnectedWiFiProfile(const String &rawSsid, const String &rawPassword) {
  String ssid = rawSsid;
  String password = rawPassword;  // Do not trim a Wi-Fi password.
  ssid.trim();

  if (ssid.length() == 0 || ssid == getSetupApSsid()) {
    Serial.println("Wi-Fi profile capture skipped: connected station SSID is empty or is the setup AP.");
    return false;
  }
  if (ssid.length() > 32 || password.length() > 64) {
    Serial.println("Wi-Fi profile capture rejected: SSID/password exceeds ESP32 limits.");
    return false;
  }

  String oldSsid1 = appConfig.wifiSsid1;
  String oldPass1 = appConfig.wifiPass1;
  String oldSsid2 = appConfig.wifiSsid2;
  String oldPass2 = appConfig.wifiPass2;
  String oldSsid3 = appConfig.wifiSsid3;
  String oldPass3 = appConfig.wifiPass3;

  int existingSlot = savedWifiSlotForSsid(ssid);
  if (existingSlot == 1) {
    // Preserve a known password if the driver reports an empty PSK for an
    // already-stored secured network. New open networks still store empty.
    if (password.length() > 0 || appConfig.wifiPass1.length() == 0) {
      appConfig.wifiPass1 = password;
    }
  } else if (existingSlot == 2) {
    String selectedPass = password.length() > 0 ? password : oldPass2;
    appConfig.wifiSsid1 = oldSsid2;
    appConfig.wifiPass1 = selectedPass;
    appConfig.wifiSsid2 = oldSsid1;
    appConfig.wifiPass2 = oldPass1;
    appConfig.wifiSsid3 = oldSsid3;
    appConfig.wifiPass3 = oldPass3;
  } else if (existingSlot == 3) {
    String selectedPass = password.length() > 0 ? password : oldPass3;
    appConfig.wifiSsid1 = oldSsid3;
    appConfig.wifiPass1 = selectedPass;
    appConfig.wifiSsid2 = oldSsid1;
    appConfig.wifiPass2 = oldPass1;
    appConfig.wifiSsid3 = oldSsid2;
    appConfig.wifiPass3 = oldPass2;
  } else {
    // A newly configured network becomes Slot 1. Previous networks move down
    // one slot so the device retains up to three places it has used.
    appConfig.wifiSsid3 = oldSsid2;
    appConfig.wifiPass3 = oldPass2;
    appConfig.wifiSsid2 = oldSsid1;
    appConfig.wifiPass2 = oldPass1;
    appConfig.wifiSsid1 = ssid;
    appConfig.wifiPass1 = password;
  }

  normalizeWiFiProfilesInMemory();

  bool changed =
      oldSsid1 != appConfig.wifiSsid1 || oldPass1 != appConfig.wifiPass1 ||
      oldSsid2 != appConfig.wifiSsid2 || oldPass2 != appConfig.wifiPass2 ||
      oldSsid3 != appConfig.wifiSsid3 || oldPass3 != appConfig.wifiPass3;

  Serial.print("Connected Wi-Fi captured in Leah NVS profile Slot 1: ");
  Serial.println(appConfig.wifiSsid1);
  return changed;
}

bool importLegacyEspressifWiFiProfileIfNeeded() {
  if (appConfig.wifiSsid1.length() > 0 || appConfig.wifiSsid2.length() > 0 || appConfig.wifiSsid3.length() > 0) {
    return false;
  }

  // Older builds relied on WiFiManager/Espressif's station configuration but
  // did not copy it into the Leah cgm_cfg profile namespace. Recover it once
  // when the three Leah slots are empty.
  String legacySsid = WiFi.SSID();
  String legacyPassword = WiFi.psk();
  legacySsid.trim();
  if (legacySsid.length() == 0 || legacySsid == getSetupApSsid()) return false;

  Serial.print("Recovering legacy Espressif Wi-Fi profile into Leah NVS: ");
  Serial.println(legacySsid);
  bool changed = rememberConnectedWiFiProfile(legacySsid, legacyPassword);
  if (changed) {
    saveConfig();
    return verifyWiFiProfilesInNvs();
  }
  return false;
}

String buildNightscoutUrl(String endpoint) {
  if (!hasNightscoutConfig()) {
    return "";
  }

  String url = "https://";
  url += normalizeNightscoutHost(appConfig.nightscoutHost);

  if (!endpoint.startsWith("/")) {
    url += "/";
  }

  url += endpoint;

  if (appConfig.nightscoutToken.length() > 0) {
    if (url.indexOf("?") >= 0) {
      url += "&token=";
    } else {
      url += "?token=";
    }

    url += appConfig.nightscoutToken;
  }

  return url;
}

unsigned long getMuteDurationForCurrentStateMs() {
  uint16_t minutes = appConfig.audioMuteMinutes;

  if (minutes < 1) minutes = 1;
  if (minutes > 30) minutes = 30;

  return (unsigned long)minutes * 60UL * 1000UL;
}

unsigned long getSirenMuteRemainingSeconds() {
  if (!sirenSilenced) return 0;

  unsigned long elapsed = millis() - silenceStartTime;
  if (elapsed >= activeSilenceDurationMs) return 0;

  unsigned long remainingMs = activeSilenceDurationMs - elapsed;
  return (remainingMs + 999UL) / 1000UL;
}

String getSirenMuteRemainingText() {
  unsigned long totalSeconds = getSirenMuteRemainingSeconds();
  if (!sirenSilenced || totalSeconds == 0) return "Off";

  unsigned long minutes = totalSeconds / 60UL;
  unsigned long seconds = totalSeconds % 60UL;
  String result = String(minutes) + "m ";
  if (seconds < 10) result += "0";
  result += String(seconds) + "s";
  return result;
}

bool isLocalSoundEnabledForAlarmType(int alarmType) {
  if (alarmType == ALARM_URGENT_LOW) return appConfig.soundUrgentLow;
  if (alarmType == ALARM_LOW) return appConfig.soundLow;
  if (alarmType == ALARM_HIGH) return appConfig.soundHigh;
  if (alarmType == ALARM_URGENT_HIGH) return appConfig.soundUrgentHigh;
  if (alarmType == AUDIO_ALARM_NO_DATA) return appConfig.soundNoData;
  return false;
}

String getSelectedLocalSoundsText() {
  String selected = "";
  if (appConfig.soundUrgentLow) selected += "U-Low ";
  if (appConfig.soundLow) selected += "Low ";
  if (appConfig.soundHigh) selected += "High ";
  if (appConfig.soundUrgentHigh) selected += "U-High ";
  if (appConfig.soundNoData) selected += "No data ";
  selected.trim();
  return selected.length() > 0 ? selected : "None";
}

String getDashboardHtml() {
  String html = "";
  html.reserve(appConfig.advancedWebView ? 72000 : 56000);

  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Leah 2R Displays</title>";
  html += "<style>";
  html += ":root{--navy:#07265A;--blue:#3B82F6;--violet:#7C3AED;--teal:#0F9AA5;--orange:#F59E0B;--amber:#F59E0B;--red:#EF4444;--green:#22A66F;--soft:#F3F7FA;--card:#FFFFFF;--muted:#667085;--line:#D9E2EC;}";
  html += "body{font-family:Arial,Helvetica,sans-serif;background:var(--soft);color:#10233F;margin:0;padding:14px;line-height:1.35}";
  html += ".wrap{max-width:960px;margin:0 auto}.hero{background:linear-gradient(135deg,var(--navy),var(--teal));color:white;border-radius:18px;padding:18px;margin-bottom:14px;box-shadow:0 8px 22px #0002}";
  html += ".hero h1{margin:0;font-size:28px}.hero .tag{font-size:17px;opacity:.95;margin-top:4px}.nav{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}";
  html += ".nav a,.btn{display:inline-block;background:var(--teal);color:white;text-decoration:none;border:0;border-radius:10px;padding:10px 13px;font-weight:700;cursor:pointer;margin:3px}";
  html += ".btn.secondary{background:#475467}.btn.warn{background:var(--amber);color:#152238}.btn.danger{background:var(--red)}.smallbtn{padding:6px 8px;font-size:12px}";
  html += ".card{background:var(--card);border-radius:16px;padding:14px 14px 16px;margin:14px 0;box-shadow:0 5px 16px #0B2D5C14;border:1px solid #E7EDF3;border-top:5px solid var(--blue)}.card.greenline{border-top-color:var(--green)}.card.redline{border-top-color:var(--red)}.card.orangeline{border-top-color:var(--orange)}.card.purpleline{border-top-color:var(--violet)}.card.blueline{border-top-color:var(--blue)}.card h2,.provider-head h2{cursor:pointer}.card h2:before,.provider-head h2:before{content:'▶ ';color:var(--blue);font-size:17px}.card:not(.collapsed) h2:before,.card:not(.collapsed) .provider-head h2:before{content:'▼ '}.card.greenline h2:before{color:var(--green)}.card.redline h2:before{color:var(--red)}.card.orangeline h2:before{color:var(--orange)}.card.purpleline h2:before{color:var(--violet)}.card.blueline h2:before{color:var(--blue)}.card.no-collapse h2:before{content:''}.card.collapsed> :not(h2):not(.provider-head){display:none!important}.card.collapsed>.provider-head~*{display:none!important}";
  html += ".card h2{margin:0 0 10px;color:var(--navy);font-size:22px;border-bottom:1px solid var(--line);padding-bottom:10px}.provider-head{border-bottom:1px solid var(--line);padding-bottom:10px;margin-bottom:10px}.provider-head h2{margin:0;color:var(--navy);font-size:22px}.muted{color:var(--muted);font-size:14px}.small{font-size:13px}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px}.statusgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}";
  html += ".tile{background:#FBFCFE;border:1px solid #E6ECF2;border-radius:12px;padding:12px}.tile .label{color:var(--muted);font-size:13px}.tile .value{font-size:20px;font-weight:800;margin-top:3px;color:var(--navy)}.tile .value.purposecopy{font-size:15px;line-height:1.45;font-weight:600}";
  html += ".badge{display:inline-block;border-radius:999px;padding:6px 10px;margin:3px;font-weight:700;font-size:13px}.ok{background:#E8F7F1;color:#067647}.warn{background:#FFF4E5;color:#B54708}.bad{background:#FDECEC;color:#B42318}.info{background:#EAF3FF;color:#175CD3}.off{background:#F2F4F7;color:#475467}";
  html += "label{display:block;font-weight:700;margin-top:10px;color:#344054}input,select{width:100%;box-sizing:border-box;padding:11px;border:1px solid #CCD5E0;border-radius:10px;margin-top:5px;background:white;font-size:15px}";
  html += "input[type=checkbox]{width:auto;transform:scale(1.1);margin-right:8px}details{border:1px solid #EAECF0;border-radius:14px;padding:10px;background:#FCFCFD;margin-top:10px}summary{font-weight:800;color:var(--navy);cursor:pointer}";
  html += ".dangerbox{background:#FFF2F2;border-left:5px solid var(--red);padding:10px;border-radius:10px}.goodbox{background:#ECFDF3;border-left:5px solid var(--green);padding:10px;border-radius:10px}";
  html += ".alias-access{margin:12px 0;background:#ECFDF3;border:2px solid var(--green);border-radius:14px;padding:12px;text-align:center}.alias-access .caption{font-size:14px;font-weight:800;color:#067647}.alias-access .host{font-size:30px;font-weight:900;color:var(--navy);line-height:1.15;overflow-wrap:anywhere}.alias-access a{color:inherit;text-decoration:none}";
  html += ".local-address-card{border:2px solid #FF5A57;border-radius:16px;padding:14px;margin:10px 0 16px;background:#FFFDFD}.local-address-card h3{margin:0 0 6px;color:#B42318;font-size:22px}.local-address-card .alias-input{font-size:28px;font-weight:900;text-align:center;color:var(--navy);padding:16px}.local-address-card .current-address{margin-top:10px;padding:10px;border:1px solid #FF8A86;border-radius:10px;background:#FFF1F0;color:#9B1C1C;font-weight:800;font-size:16px}.local-address-card .example{font-size:12px;color:var(--muted);margin-top:6px}";
  html += ".provider-panel,.testpanel{display:none}.provider-panel.active,.testpanel.active{display:block}.provider-head{display:flex;gap:10px;align-items:center;justify-content:space-between;flex-wrap:wrap}.datatable{width:100%;border-collapse:collapse;margin-top:10px}.datatable th,.datatable td{border-bottom:1px solid #EAECF0;padding:8px;text-align:left;vertical-align:top}.historypanel{border:1px solid #D0D5DD;border-radius:12px;padding:10px;margin-top:10px;background:#fff}.historypanel h3{margin:0 0 6px;color:#102A56}.historytable th{background:#F2F4F7}.historytable tfoot td{font-weight:800;background:#ECFDF3;border-top:1px solid #A6F4C5}.pill{display:inline-block;border-radius:999px;padding:5px 9px;background:#EEF4FF;color:#3538CD;font-weight:800;font-size:12px}.buttonrow form{display:inline-block;margin:2px}.fieldhint{font-size:12px;color:var(--muted);margin-top:4px}.split{display:grid;grid-template-columns:1fr;gap:12px}@media(min-width:820px){.split{grid-template-columns:1fr 1fr}}";
  html += "@media(max-width:600px){.hero h1{font-size:24px}.tile .value{font-size:18px}}";
  html += "</style></head><body><div class='wrap'>";

  html += "<div class='hero'><h1>Leah 2R Displays</h1><div class='tag'>Read. Respond.</div>";
  html += "<div class='nav'><a href='#status'>Status</a><a href='#view'>Page view</a><a href='#setup'>Patient & display</a><a href='#limits'>Glucose alerts</a><a href='#insulin'>Insulin</a><a href='#whatsapp'>WhatsApp</a><a href='#firmware'>Firmware/support</a></div></div>";

  html += "<div class='card blueline no-collapse' id='purpose'><h2>Vision and mission</h2>";
  html += "<div class='split'>";
  html += "<div class='tile'><div class='label'>Vision</div><div class='value purposecopy'>" + htmlEscape(String(LEAH_VISION)) + "</div></div>";
  html += "<div class='tile'><div class='label'>Mission</div><div class='value purposecopy'>" + htmlEscape(String(LEAH_MISSION)) + "</div></div>";
  html += "</div></div>";

  bool dataFresh = isDataFresh();
  bool wifiOk = WiFi.status() == WL_CONNECTED;
  String dashboardPhoneBatteryText = phoneBattery >= 0 ? String(phoneBattery) + "%" : "n/a";
  String dashboardSensorLeftText = shortenText(getSensorLeftText(), 14);
  String dashboardSageText = shortenText(getSageDisplayText(), 14);
  String activeProvider = normalizeAlertProvider(appConfig.alertProvider);
  String selectedProviderStatus = "Cloud alerts off";
  unsigned long muteRemainingSeconds = getSirenMuteRemainingSeconds();
  String muteRemainingDisplay = getSirenMuteRemainingText();
  String muteButtonLabel = sirenSilenced
    ? "Restart acknowledgement + snooze " + String(appConfig.audioMuteMinutes) + " min"
    : "Acknowledge + snooze local alarm";
  if (activeProvider == "API") selectedProviderStatus = apiLastStatus;
  else if (activeProvider == "TWILIO") selectedProviderStatus = twilioLastStatus;
  else if (activeProvider == "CALLMEBOT") selectedProviderStatus = callMeBotLastStatus;
  else if (activeProvider == "TEXTMEBOT") selectedProviderStatus = textMeBotLastStatus;

  html += "<div class='card greenline no-collapse' id='status'><h2>Current status</h2>";
  html += "<div>";
  html += getStatusBadge("Glucose data", dataFresh ? "Fresh" : "Old", dataFresh ? "ok" : "bad");
  html += getStatusBadge("Wi-Fi", wifiOk ? "Connected" : "Disconnected", wifiOk ? "ok" : "bad");
  html += getStatusBadge("WhatsApp", getAlertProviderLabel(appConfig.alertProvider), isAnyWhatsAppProviderSelected() ? "info" : "off");
  html += getStatusBadge("Channel", getUpdateChannelLabel(), appConfig.updateChannel == "beta" ? "warn" : "ok");
  html += "</div>";
  html += "<div class='statusgrid'>";
  if (dataFresh) {
    html += "<div class='tile'><div class='label'>Glucose (" + getGlucoseUnitLabel() + ")</div><div class='value'>" + formatGlucoseDisplay(glucoseMmol) + "</div></div>";
    html += "<div class='tile'><div class='label'>Delta (" + getGlucoseUnitLabel() + ")</div><div class='value'>" + formatDelta(deltaMmol) + "</div></div>";
  } else {
    html += "<div class='tile'><div class='label'>Glucose</div><div class='value'>NO DATA</div></div>";
    html += "<div class='tile'><div class='label'>Last status</div><div class='value'>Check</div></div>";
  }
  html += "<div class='tile'><div class='label'>Age</div><div class='value'>" + String(ageMinutes) + " min</div></div>";
  html += "<div class='tile'><div class='label'>Nightscout</div><div class='value'>" + htmlEscape(shortenText(lastStatus, 18)) + "</div></div>";
  html += "<div class='tile'><div class='label'>WhatsApp</div><div class='value'>" + htmlEscape(getAlertProviderLabel(appConfig.alertProvider)) + "</div></div>";
  html += "<div class='tile'><div class='label'>IOB now</div><div class='value'>" + String(insulinRemainingUnits, 2) + "u</div></div>";
  html += "<div class='tile'><div class='label'>COB now</div><div class='value'>" + (nightscoutCobValid ? String(nightscoutCobGrams, 0) + "g" : String("n/a")) + "</div></div>";
  html += "<div class='tile'><div class='label'>Dose today</div><div class='value'>" + String(todayBolusUnits, 1) + "u</div></div>";
  html += "<div class='tile'><div class='label'>Carbs today</div><div class='value'>" + String(todayCarbsGrams, 0) + "g</div></div>";
  html += "<div class='tile'><div class='label'>Last bolus</div><div class='value'>" + String(lastBolusUnits, 1) + "u</div></div>";
  html += "<div class='tile'><div class='label'>SSID</div><div class='value'>" + htmlEscape(shortenText(WiFi.SSID(), 14)) + "</div></div>";
  html += "<div class='tile'><div class='label'>Phone batt</div><div class='value'>" + htmlEscape(dashboardPhoneBatteryText) + "</div></div>";
  html += "<div class='tile'><div class='label'>SAGE</div><div class='value'>" + htmlEscape(dashboardSageText) + "</div></div>";
  html += "<div class='tile'><div class='label'>Sensor left</div><div class='value'>" + htmlEscape(dashboardSensorLeftText) + "</div></div>";
  html += "<div class='tile'><div class='label'>Sensor mode</div><div class='value'>" + htmlEscape(getActiveSensorModeText()) + "</div></div>";
  html += "<div class='tile'><div class='label'>Sensor source</div><div class='value'>" + htmlEscape(shortenText(getSageSourceText(), 16)) + "</div></div>";
  html += "<div class='tile'><div class='label'>Local sounds</div><div class='value'>" + htmlEscape(shortenText(getSelectedLocalSoundsText(), 16)) + "</div></div>";
  html += "<div class='tile'><div class='label'>Mute remaining</div><div class='value' id='muteRemaining' data-seconds='" + String(muteRemainingSeconds) + "'>" + htmlEscape(muteRemainingDisplay) + "</div></div>";
  html += "<div class='tile'><div class='label'>Cloud queue</div><div class='value'>" + String(cloudAlertQueue ? uxQueueMessagesWaiting(cloudAlertQueue) : 0) + "</div></div>";
  html += "<div class='tile'><div class='label'>Cloud send</div><div class='value'>" + String(cloudAlertSendInProgress ? "Working" : "Idle") + "</div></div>";
  html += "<div class='tile'><div class='label'>Alarm episode</div><div class='value'>" + String(currentCloudAlarmEpisodeId) + (isCurrentCloudAlarmEpisodeAcknowledged() ? " ACK" : "") + "</div></div>";
  html += "<div class='tile'><div class='label'>Last cloud send</div><div class='value'>" + String(lastCloudAlertSendDurationMs) + " ms</div></div>";
  html += "<div class='tile'><div class='label'>Loop max gap</div><div class='value'>" + String(mainLoopMaxDelayMs) + " ms</div></div>";
  html += "<div class='tile'><div class='label'>Audio task</div><div class='value'>" + String(alarmAudioTaskHandle ? "Running" : "Stopped") + "</div></div>";
  html += "<div class='tile'><div class='label'>Queue drops</div><div class='value'>" + String(cloudAlertQueueDropCount) + "</div></div>";
  html += "</div>";
  html += "<div class='alias-access'><div class='caption'>Open this display on the same Wi-Fi network</div><div class='host'><a href='" + htmlEscape(getLocalAliasUrl()) + "'>" + htmlEscape(getLocalMdnsHost()) + "</a></div></div>";
  html += "<div class='buttonrow'><button class='btn' type='submit' form='settingsForm'>Save settings</button>";
  html += "<form method='POST' action='/mute' style='display:inline-block'><button class='btn secondary' type='submit'>" + htmlEscape(muteButtonLabel) + "</button></form></div>";
  html += "<p class='muted'>This is a companion awareness display. It does not replace the primary CGM app, receiver, finger-prick confirmation where required, or the care plan.</p>";
  html += "</div>";

  html += "<div class='card purpleline' id='view'><h2>Page view</h2>";
  html += "<form method='POST' action='/set-view'>";
  html += "<label><input type='checkbox' name='advanced_view' ";
  if (appConfig.advancedWebView) html += "checked";
  html += "> Show advanced technical settings</label>";
  html += "<p class='muted'>This changes only the page view. It does not save other settings, restart the device, or reconnect Wi-Fi.</p>";
  html += "<button class='btn secondary' type='submit'>Apply page view</button></form>";
  html += "</div>";

  html += "<form id='settingsForm' method='POST' action='/save'>";

  html += "<div class='card blueline' id='setup'><h2>Patient & display</h2>";
  html += "<div class='local-address-card'><h3>Local address setup</h3>";
  html += "<p class='muted'>This is the easy name used to open this Leah 2R Display from a phone or computer connected to the same Wi-Fi. The display adds <b>.local</b> automatically.</p>";
  html += "<label>Local address name</label><input id='localAliasName' class='alias-input' name='local_alias' maxlength='63' value='" + htmlEscape(getLocalMdnsName()) + "' placeholder='Example: Mat CGM 1'>";
  html += "<div class='current-address'>Current address: <span id='localAliasPreview'>" + htmlEscape(getLocalAliasUrl()) + "</span></div>";
  html += "<div class='example'>A custom entry such as Mat CGM 1 becomes mat-cgm-1.local. Only one device on a network should use the same address.</div></div>";
  html += "<div class='grid'>";
  html += "<div><label>Device name</label><input name='device_name' maxlength='31' value='" + htmlEscape(appConfig.deviceName) + "'></div>";
  html += "<div><label>Patient / child name</label><input name='patient_name' maxlength='31' value='" + htmlEscape(appConfig.patientName) + "'></div>";
  html += "<div><label>Room / location</label><input name='device_location' maxlength='31' value='" + htmlEscape(appConfig.deviceLocation) + "'></div>";
  html += "<div><label>Glucose units</label><select name='glucose_units'>";
  html += "<option value='MMOL'";
  if (!glucoseUnitsAreMgdl()) html += " selected";
  html += ">mmol/L</option>";
  html += "<option value='MGDL'";
  if (glucoseUnitsAreMgdl()) html += " selected";
  html += ">mg/dL</option>";
  html += "</select></div>";
  html += "</div><p class='muted'>Alarm calculations remain stored internally in mmol/L. The OLED and web page show the selected display units.</p>";

  html += "<details id='displaynight'><summary>Display and night settings</summary>";
  html += "<label><input type='checkbox' name='night_dim' ";
  if (appConfig.nightDimEnabled) html += "checked";
  html += "> Enable night dimming</label>";
  html += "<div class='grid'>";
  html += "<div><label>Day contrast 20-255</label><input name='contrast' type='number' min='20' max='255' value='" + String(appConfig.displayContrast) + "'></div>";
  html += "<div><label>Night contrast 20-255</label><input name='night_contrast' type='number' min='20' max='255' value='" + String(appConfig.nightContrast) + "'></div>";
  html += "<div><label>Dim start hour</label><input name='dim_start_hour' type='number' min='0' max='23' value='" + String(appConfig.dimStartHour) + "'></div>";
  html += "<div><label>Dim start minute</label><input name='dim_start_minute' type='number' min='0' max='59' value='" + String(appConfig.dimStartMinute) + "'></div>";
  html += "<div><label>Dim end hour</label><input name='dim_end_hour' type='number' min='0' max='23' value='" + String(appConfig.dimEndHour) + "'></div>";
  html += "<div><label>Dim end minute</label><input name='dim_end_minute' type='number' min='0' max='59' value='" + String(appConfig.dimEndMinute) + "'></div>";
  html += "</div><p class='muted'>Default: dim from 21:00 to 07:00 using contrast 120.</p></details>";

  html += "<details id='sensor'><summary>Sensor data</summary>";
  html += "<p class='muted'>Choose one source of truth. Auto reads sensor age from Nightscout. Manual ignores Nightscout sensor age and uses the locally saved start date/time.</p>";
  html += "<div class='grid'>";
  html += "<div><label>Sensor age mode</label><select id='sensor_mode' name='sensor_mode' onchange='updateSensorModeUi()'>";
  html += "<option value='AUTO'";
  if (appConfig.sensorAutoRead) html += " selected";
  html += ">Auto - Nightscout</option>";
  html += "<option value='MANUAL'";
  if (!appConfig.sensorAutoRead) html += " selected";
  html += ">Manual - local settings</option></select></div>";
  html += "<div><label>Sensor wear period (days)</label><input name='sensor_wear_days' type='number' min='1' max='30' value='" + String(appConfig.sensorWearDays) + "'><div class='fieldhint'>Used for the countdown in both Auto and Manual modes.</div></div>";
  html += "</div>";
  html += "<div id='sensorAutoInfo' class='goodbox'>Auto mode reads <b>/api/v2/properties/sage</b>. The Nightscout SAGE age is subtracted from the user-set wear period above.</div>";
  html += "<div id='manualSensorSettings' class='sensor-manual'>";
  html += "<div class='grid'>";
  html += "<div><label>Manual sensor source</label><select name='sensor_source'>";
  String sensorOptions[] = {"Manual", "Dexcom G6", "Dexcom G7", "Libre", "Libre 2", "Libre 3", "xDrip+"};
  for (int i = 0; i < 7; i++) {
    html += "<option value='" + sensorOptions[i] + "'";
    if (appConfig.sensorSource == sensorOptions[i]) html += " selected";
    html += ">" + sensorOptions[i] + "</option>";
  }
  html += "</select></div>";
  html += "<div><label>Manual sensor serial / ID optional</label><input name='sensor_serial' value='" + htmlEscape(appConfig.sensorSerial) + "' placeholder='Leave blank if not available'></div>";
  html += "<div><label>Manual sensor start date</label><input name='sensor_start_date' type='date' value='" + htmlEscape(appConfig.sensorStartDate) + "'></div>";
  html += "<div><label>Manual sensor start time</label><input name='sensor_start_time' type='time' value='" + htmlEscape(appConfig.sensorStartTime) + "'></div>";
  html += "</div></div>";
  html += "<div class='statusgrid'>";
  html += "<div class='tile'><div class='label'>Sensor age</div><div class='value'>" + htmlEscape(getSageDisplayText()) + "</div></div>";
  html += "<div class='tile'><div class='label'>Wear period</div><div class='value'>" + String(getActiveSensorWearDays()) + " days</div></div>";
  html += "<div class='tile'><div class='label'>Time left</div><div class='value'>" + htmlEscape(getSensorLeftText()) + "</div></div>";
  html += "<div class='tile'><div class='label'>Mode</div><div class='value'>" + htmlEscape(getActiveSensorModeText()) + "</div></div>";
  html += "<div class='tile'><div class='label'>Age source</div><div class='value'>" + htmlEscape(shortenText(getSageSourceText(), 18)) + "</div></div>";
  html += "<div class='tile'><div class='label'>SAGE level</div><div class='value'>" + htmlEscape(getSageLevelText()) + "</div></div>";
  html += "</div>";
  html += "<p class='muted'>SAGE status: " + htmlEscape(sageStatus) + ". Countdown: " + htmlEscape(getSensorLifeText()) + ".</p>";
  html += "<p class='muted'>Uploader/device details are diagnostic only and do not determine the sensor age source.</p></details>";

  html += "<details id='wifi'><summary>Wi-Fi networks</summary>";
  html += "<p class='muted'>Save up to three preferred networks in NVS. The ESP32 tries Slot 1 first, then Slot 2, then Slot 3.</p>";
  html += "<div class='goodbox'>Current connection: <b>" + htmlEscape(WiFi.SSID()) + "</b> | IP: <b>" + WiFi.localIP().toString() + "</b> | Alias: <b>" + htmlEscape(getLocalMdnsHost()) + "</b> | RSSI: <b>" + String(WiFi.RSSI()) + " dBm</b></div>";
  html += savedWifiProfilesSummaryHtml();
  html += "<div class='grid'>";
  html += "<div><label>Wi-Fi 1 SSID</label><input id='wifi_ssid1' name='wifi_ssid1' value='" + htmlEscape(appConfig.wifiSsid1) + "'><label>Wi-Fi 1 password</label><input type='password' name='wifi_pass1' placeholder='Leave blank to keep current'></div>";
  html += "<div><label>Wi-Fi 2 SSID</label><input id='wifi_ssid2' name='wifi_ssid2' value='" + htmlEscape(appConfig.wifiSsid2) + "'><label>Wi-Fi 2 password</label><input type='password' name='wifi_pass2' placeholder='Leave blank to keep current'></div>";
  html += "<div><label>Wi-Fi 3 SSID</label><input id='wifi_ssid3' name='wifi_ssid3' value='" + htmlEscape(appConfig.wifiSsid3) + "'><label>Wi-Fi 3 password</label><input type='password' name='wifi_pass3' placeholder='Leave blank to keep current'></div>";
  html += "</div>";
  html += "<div class='buttonrow'><button class='btn' type='submit'>Save settings</button><button class='btn warn' type='submit' formaction='/save-wifi-reconnect'>Save and reconnect/restart</button></div>";
  html += "<h3>Nearby networks</h3><p class='muted'>Use the strongest secured network where possible. Open networks are shown in red and are not recommended for a care device.</p>";
  html += wifiScanTableHtml();
  html += "</details>";

  html += "</div>";


  html += "<div class='card redline' id='limits'><h2>Glucose alert limits (" + getGlucoseUnitLabel() + ")</h2>";
  html += "<label><input type='checkbox' name='use_ns_limits' ";
  if (appConfig.useNightscoutLimits) html += "checked";
  html += "> Read limits from Nightscout when available</label>";
  html += "<div class='grid'>";
  html += "<div><label>Urgent low</label><input name='urgent_low' type='number' step='" + String(glucoseUnitsAreMgdl() ? "1" : "0.1") + "' value='" + formatLimitInputValue(appConfig.urgentLow) + "'></div>";
  html += "<div><label>Low</label><input name='low' type='number' step='" + String(glucoseUnitsAreMgdl() ? "1" : "0.1") + "' value='" + formatLimitInputValue(appConfig.low) + "'></div>";
  html += "<div><label>High</label><input name='high' type='number' step='" + String(glucoseUnitsAreMgdl() ? "1" : "0.1") + "' value='" + formatLimitInputValue(appConfig.high) + "'></div>";
  html += "<div><label>Urgent high</label><input name='urgent_high' type='number' step='" + String(glucoseUnitsAreMgdl() ? "1" : "0.1") + "' value='" + formatLimitInputValue(appConfig.urgentHigh) + "'></div>";
  html += "</div>";
  html += "<details id='audioalarm' open><summary>Local buzzer sounds</summary>";
  html += "<div class='statusgrid'>";
  html += "<div class='tile'><div class='label'>Buzzer master</div><div class='value'>" + String(appConfig.alarmSoundEnabled ? "ON" : "OFF") + "</div></div>";
  html += "<div class='tile'><div class='label'>Audio mode</div><div class='value'>" + String(appConfig.randomAudioEnabled ? "Random" : "Fixed") + "</div></div>";
  html += "<div class='tile'><div class='label'>Alarm cycle</div><div class='value small'>30s sound / 15s pause</div></div>";
  html += "<div class='tile'><div class='label'>Mute remaining</div><div class='value'>" + htmlEscape(getSirenMuteRemainingText()) + "</div></div>";
  html += "</div>";
  html += "<div class='grid'>";
  html += "<div><label>Mute duration - minutes (1-30)</label><input name='audio_mute_minutes' type='number' min='1' max='30' required value='" + String(appConfig.audioMuteMinutes) + "'><p class='fieldhint'>The user must set and save this value. Mute silences only the local buzzer; visual alarms and WhatsApp alerts remain active.</p></div>";
  html += "<div><label><input type='checkbox' name='random_audio_enabled' ";
  if (appConfig.randomAudioEnabled) html += "checked";
  html += "> Random audio pattern - seeded 50 rhythm variants per alarm type</label></div>";
  html += "</div>";
  html += "<label><input type='checkbox' name='alarm_sound_enabled' ";
  if (appConfig.alarmSoundEnabled) html += "checked";
  html += "> Local buzzer master enabled</label>";
  html += "<div class='grid'>";
  html += "<label><input type='checkbox' name='sound_urgent_low' "; if (appConfig.soundUrgentLow) html += "checked"; html += "> Sound urgent low</label>";
  html += "<label><input type='checkbox' name='sound_low' "; if (appConfig.soundLow) html += "checked"; html += "> Sound low</label>";
  html += "<label><input type='checkbox' name='sound_high' "; if (appConfig.soundHigh) html += "checked"; html += "> Sound high</label>";
  html += "<label><input type='checkbox' name='sound_urgent_high' "; if (appConfig.soundUrgentHigh) html += "checked"; html += "> Sound urgent high</label>";
  html += "<label><input type='checkbox' name='sound_no_data' "; if (appConfig.soundNoData) html += "checked"; html += "> Sound no data</label>";
  html += "</div>";
  html += "<p class='muted'>Selected local sounds: " + htmlEscape(getSelectedLocalSoundsText()) + ". Urgent low has no long macro pause; urgent high uses a short pause; low/high retain the 30-second activity and 15-second fatigue cycle. Random mode varies only the rhythm inside each alarm identity.</p>";
  html += "<p class='fieldhint'>The physical RIGHT button acknowledges and snoozes an active alarm. Hold it for 10 seconds to switch the buzzer master OFF; when OFF, hold it for 3 seconds to switch the buzzer master ON. Both master states are saved in NVS.</p></details>";
  html += "</div>";

  html += "<div class='card blueline' id='insulin'><h2>Insulin / bolus</h2>";
  html += "<details open><summary>IOB and COB history</summary>";
  html += "<p class='muted'>This history lists recorded insulin and carbohydrate treatments. Current IOB and COB are shown in Current status because daily totals are not the amounts still active.</p>";
  html += "<div class='statusgrid'>";
  html += "<div class='tile'><div class='label'>Today insulin</div><div class='value'>" + String(todayBolusUnits, 1) + "u</div></div>";
  html += "<div class='tile'><div class='label'>Today carbs</div><div class='value'>" + String(todayCarbsGrams, 0) + "g</div></div>";
  html += "<div class='tile'><div class='label'>Yesterday insulin</div><div class='value'>" + String(yesterdayBolusUnits, 1) + "u</div></div>";
  html += "<div class='tile'><div class='label'>Yesterday carbs</div><div class='value'>" + String(yesterdayCarbsGrams, 0) + "g</div></div>";
  html += "<div class='tile'><div class='label'>Last insulin</div><div class='value small'>" + String(lastBolusUnits, 1) + "u " + htmlEscape(formatTimestampTimeOnly(lastBolusTimeMs)) + "</div></div>";
  html += "<div class='tile'><div class='label'>Last carbs</div><div class='value small'>" + String(lastCarbsGrams, 0) + "g " + htmlEscape(formatTimestampTimeOnly(lastCarbsTimeMs)) + "</div></div>";
  html += "</div>";
  html += buildDailyTreatmentTableHtml("Today", true, todayBolusUnits, todayCarbsGrams);
  html += buildDailyTreatmentTableHtml("Yesterday", false, yesterdayBolusUnits, yesterdayCarbsGrams);
  html += "<p class='muted'>Daily totals use local midnight-to-midnight boundaries. Status: " + htmlEscape(dailyTotalsStatus) + ".</p>";
  html += "<details><summary>Fallback insulin estimate setting</summary><div class='grid'><div><label>Insulin action window hours</label><input name='insulin_action_hours' type='number' min='1' max='8' step='0.5' value='" + String(appConfig.insulinActionHours, 1) + "'><div class='fieldhint'>Used only when direct Nightscout IOB is unavailable. This is an awareness estimate, not dosing guidance.</div></div></div></details>";
  html += "</details></div>";

  html += "<div class='card orangeline' id='whatsapp'><div class='provider-head'><h2>WhatsApp notifications</h2><span class='pill'>Selected provider only</span></div>";
  html += "<div class='goodbox'>Choose one WhatsApp delivery provider. Local OLED alarms and buzzer remain active regardless of this setting.</div>";
  html += "<label>Active WhatsApp provider</label><select id='alert_provider' name='alert_provider' onchange='updateProviderUi()'>";
  html += alertProviderOption("OFF", "Off - local display and buzzer only");
  html += alertProviderOption("API", "API / custom webhook");
  html += alertProviderOption("TWILIO", "Twilio WhatsApp");
  html += alertProviderOption("CALLMEBOT", "CallMeBot");
  html += alertProviderOption("TEXTMEBOT", "TextMeBot");
  html += "</select>";
  html += "<p id='providerHint' class='muted'></p>";
  html += "<div class='statusgrid'>";
  html += "<div class='tile'><div class='label'>Active provider</div><div class='value'>" + htmlEscape(getAlertProviderLabel(appConfig.alertProvider)) + "</div></div>";
  html += "<div class='tile'><div class='label'>Last provider status</div><div class='value'>" + htmlEscape(shortenText(selectedProviderStatus, 18)) + "</div></div>";
  html += "</div>";

  html += "<div id='providerOffNotice' class='goodbox' data-show-when='OFF'>Cloud WhatsApp is off. The display will continue to use the local 2.42-inch 128x64 OLED, visual alarms and buzzer. No WhatsApp messages will be sent until a provider is selected and saved.</div>";

  html += caregiverAlarmMatrixHtml();

  html += "<details class='provider-panel' id='api' data-provider='API'><summary>API / custom webhook setup</summary>";
  html += "<p class='muted'>Use this when you have your own gateway or webhook. Leah 2R Displays sends recipient, phone, apikey and text query parameters.</p>";
  html += "<label>API endpoint</label><input name='api_endpoint' value='" + htmlEscape(appConfig.apiEndpoint) + "' placeholder='https://example.com/send'>";
  html += "<label>API key / token</label><input type='password' name='api_key' placeholder='Leave blank to keep existing key'>";
  html += "<div class='fieldhint'>Stored API key: " + htmlEscape(maskValue(appConfig.apiKey)) + ". The key is stored encrypted and is not displayed.</div>";
  html += "<div class='grid'>";
  html += "<div><label>Caregiver 1 recipient</label><input name='api_parent1' value='" + htmlEscape(appConfig.apiParent1) + "' placeholder='26481XXXXXXX'></div>";
  html += "<div><label>Caregiver 2 recipient</label><input name='api_parent2' value='" + htmlEscape(appConfig.apiParent2) + "' placeholder='26481XXXXXXX'></div>";
  html += "</div><p class='muted'>Last API status: " + htmlEscape(apiLastStatus) + "</p></details>";

  html += "<details class='provider-panel' id='twilio' data-provider='TWILIO'><summary>Twilio WhatsApp setup</summary>";
  html += "<p class='muted'>Use Twilio Sandbox or an approved Twilio WhatsApp sender. Numbers must use whatsapp:+countrycode format.</p>";
  html += "<label>Twilio Account SID</label><input name='twilio_sid' value='" + htmlEscape(appConfig.twilioSid) + "' placeholder='ACxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'>";
  html += "<label>Twilio Auth Token</label><input type='password' name='twilio_token' placeholder='Leave blank to keep existing token'>";
  html += "<div class='fieldhint'>Stored token: " + htmlEscape(maskValue(appConfig.twilioToken)) + ". The token is stored encrypted and is not displayed.</div>";
  html += "<label>Twilio From / Sender</label><input name='twilio_from' value='" + htmlEscape(appConfig.twilioFrom) + "' placeholder='whatsapp:+14155238886'>";
  html += "<div class='grid'>";
  html += "<div><label>Caregiver 1 WhatsApp To</label><input name='twilio_parent1' value='" + htmlEscape(appConfig.twilioParent1) + "' placeholder='whatsapp:+26481XXXXXXX'></div>";
  html += "<div><label>Caregiver 2 WhatsApp To</label><input name='twilio_parent2' value='" + htmlEscape(appConfig.twilioParent2) + "' placeholder='whatsapp:+26481XXXXXXX'></div>";
  html += "</div><p class='muted'>Last Twilio status: " + htmlEscape(twilioLastStatus) + "</p></details>";

  html += "<details class='provider-panel' id='callmebot' data-provider='CALLMEBOT'><summary>CallMeBot setup</summary>";
  html += "<p class='muted'>Simple personal WhatsApp alerts. Each caregiver number must activate CallMeBot and has its own API key.</p>";
  html += "<div class='grid'>";
  html += "<div><label>Caregiver 1 phone</label><input name='cb_parent1' value='" + htmlEscape(appConfig.callMeBotParent1) + "' placeholder='26481XXXXXXX'></div>";
  html += "<div><label>Caregiver 1 API key</label><input type='password' name='cb_key1' placeholder='Leave blank to keep existing key'></div>";
  html += "<div><label>Caregiver 2 phone</label><input name='cb_parent2' value='" + htmlEscape(appConfig.callMeBotParent2) + "' placeholder='26481XXXXXXX'></div>";
  html += "<div><label>Caregiver 2 API key</label><input type='password' name='cb_key2' placeholder='Leave blank to keep existing key'></div>";
  html += "</div><div class='fieldhint'>Stored C1 key: " + htmlEscape(maskValue(appConfig.callMeBotApiKey1)) + ". Stored C2 key: " + htmlEscape(maskValue(appConfig.callMeBotApiKey2)) + ". Use phone format without plus sign.</div>";
  html += "<p class='muted'>Last CallMeBot status: " + htmlEscape(callMeBotLastStatus) + "</p></details>";

  html += "<details class='provider-panel' id='textmebot' data-provider='TEXTMEBOT'><summary>TextMeBot setup</summary>";
  html += "<p class='muted'>Own-number style testing. First link your sender using https://api.textmebot.com/addphone.php?apikey=YOUR_API_KEY, then save the API key here.</p>";
  html += "<label>TextMeBot API key</label><input type='password' name='tmb_key' placeholder='Leave blank to keep existing key'>";
  html += "<div class='fieldhint'>Stored TextMeBot key: " + htmlEscape(maskValue(appConfig.textMeBotApiKey)) + ". The key is stored encrypted and is not displayed.</div>";
  html += "<div class='grid'>";
  html += "<div><label>Caregiver 1 recipient</label><input name='tmb_parent1' value='" + htmlEscape(appConfig.textMeBotParent1) + "' placeholder='26481XXXXXXX'></div>";
  html += "<div><label>Caregiver 2 recipient</label><input name='tmb_parent2' value='" + htmlEscape(appConfig.textMeBotParent2) + "' placeholder='26481XXXXXXX'></div>";
  html += "<div><label>Caregiver 3 recipient</label><input name='tmb_parent3' value='" + htmlEscape(appConfig.textMeBotParent3) + "' placeholder='26481XXXXXXX'></div>";
  html += "</div><p class='muted'>TextMeBot can send to three caregivers. Alert messages are spaced 10 seconds apart between recipients.</p>";
  html += "<p class='muted'>Last TextMeBot status: " + htmlEscape(textMeBotLastStatus) + "</p></details>";

  html += "<details id='rules'><summary>Notification repeat rules</summary>";
  html += "<p class='muted'>These timers apply to the selected WhatsApp provider. They prevent repeated messages from spamming while an alert condition remains active.</p>";
  html += "<div class='grid'>";
  html += "<div><label>Low repeat minutes</label><input name='low_repeat' type='number' min='1' max='240' value='" + String(appConfig.lowRepeatMinutes) + "'></div>";
  html += "<div><label>Low low / urgent low repeat minutes</label><input name='urgent_low_repeat' type='number' min='1' max='240' value='" + String(appConfig.urgentLowRepeatMinutes) + "'></div>";
  html += "<div><label>High repeat minutes</label><input name='high_repeat' type='number' min='1' max='240' value='" + String(appConfig.highRepeatMinutes) + "'></div>";
  html += "<div><label>High high / urgent high repeat minutes</label><input name='urgent_high_repeat' type='number' min='1' max='240' value='" + String(appConfig.urgentHighRepeatMinutes) + "'></div>";
  html += "<div><label>No data repeat minutes</label><input name='no_data_repeat' type='number' min='1' max='240' value='" + String(appConfig.noDataRepeatMinutes) + "'></div>";
  html += "</div></details>";

  html += "<details id='reminders'><summary>Care reminders</summary>";
  html += "<div class='split'><div>";
  html += "<label><input type='checkbox' name='phone_batt_alert' ";
  if (appConfig.phoneBatteryAlertEnabled) html += "checked";
  html += "> Notify when CGM/uploader phone battery is low</label>";
  html += "<div class='grid'>";
  html += "<div><label>Battery alert percent</label><input name='phone_batt_percent' type='number' min='1' max='100' value='" + String(appConfig.phoneBatteryAlertPercent) + "'></div>";
  html += "<div><label>Battery repeat minutes</label><input name='phone_batt_repeat' type='number' min='5' max='1440' value='" + String(appConfig.phoneBatteryRepeatMinutes) + "'></div>";
  html += "</div></div><div>";
  html += "<label><input type='checkbox' name='sensor_expiry_alert' ";
  if (appConfig.sensorExpiryReminderEnabled) html += "checked";
  html += "> Send daily sensor expiry countdown</label>";
  html += "<div class='grid'>";
  html += "<div><label>Start countdown when days left is</label><input name='sensor_expiry_days' type='number' min='1' max='14' value='" + String(appConfig.sensorExpiryReminderDays) + "'></div>";
  html += "<div><label>Sensor reminder repeat hours</label><input name='sensor_expiry_repeat' type='number' min='1' max='168' value='" + String(appConfig.sensorExpiryRepeatHours) + "'></div>";
  html += "</div></div></div>";
  html += "<p class='muted'>Phone battery comes from Nightscout device status. Sensor countdown uses Nightscout sensor data when available; otherwise it uses the manual sensor start date/time above.</p></details>";

  html += "</div>";

  if (appConfig.advancedWebView) {
    html += "<div class='card purpleline'><h2>Nightscout setup</h2>";
    html += "<label>Nightscout website address</label><input name='ns_host' placeholder='example.nightscoutpro.com' value='" + htmlEscape(appConfig.nightscoutHost) + "'>";
    html += "<label>Nightscout access key</label><input type='password' name='ns_token' placeholder='Leave blank to keep existing token'>";
    html += "<p class='muted'>The access key is stored locally on this ESP32 only.</p></div>";

    html += "<div class='card blueline'><h2>Firmware release channel</h2>";
    if (ENABLE_BETA_CHANNEL_SELECTOR) {
      html += "<label>Update channel</label><select name='update_channel'>";
      html += "<option value='stable'";
      if (normalizeUpdateChannel(appConfig.updateChannel) == "stable") html += " selected";
      html += ">Stable - normal tested releases</option>";
      html += "<option value='beta'";
      if (normalizeUpdateChannel(appConfig.updateChannel) == "beta") html += " selected";
      html += ">Beta - Hermanus/home test devices only</option></select>";
      html += "<div class='dangerbox'>Beta selector build. Use this only on Hermanus/home/bench test devices.</div>";
    } else {
      html += "<div class='goodbox'>This firmware is locked to the Stable release channel.</div>";
      html += "<input type='hidden' name='update_channel' value='stable'>";
    }
    html += "<p class='muted'>Manifest: " + htmlEscape(getUpdateManifestUrl()) + "</p></div>";

    html += "<div class='card redline'><h2>Security</h2>";
    html += "<label>Admin username</label><input name='admin_user' maxlength='31' value='" + htmlEscape(appConfig.adminUsername) + "'>";
    html += "<label>New admin password</label><input name='admin_password' type='password' placeholder='Leave blank to keep current password'>";
    html += "<p class='muted'>Use at least 8 characters.</p></div>";
  } else {
    html += "<input type='hidden' name='ns_host' value='" + htmlEscape(appConfig.nightscoutHost) + "'>";
    html += "<input type='hidden' name='update_channel' value='" + htmlEscape(appConfig.updateChannel) + "'>";
    html += "<input type='hidden' name='admin_user' value='" + htmlEscape(appConfig.adminUsername) + "'>";
  }


  if (appConfig.advancedWebView) {
    html += "<div class='card purpleline' id='advanced'><h2>Advanced technical information</h2>";
    html += "<div class='statusgrid'>";
    html += "<div class='tile'><div class='label'>IP address</div><div class='value'>" + WiFi.localIP().toString() + "</div></div>";
    html += "<div class='tile'><div class='label'>Local alias</div><div class='value'>" + htmlEscape(getLocalMdnsHost()) + "</div></div>";
    html += "<div class='tile'><div class='label'>SSID</div><div class='value'>" + htmlEscape(shortenText(WiFi.SSID(), 18)) + "</div></div>";
    html += "<div class='tile'><div class='label'>RSSI</div><div class='value'>";
    if (wifiOk) html += String(WiFi.RSSI()) + " dBm"; else html += "Disconnected";
    html += "</div></div>";
    html += "<div class='tile'><div class='label'>Wi-Fi recovery</div><div class='value small'>" + htmlEscape(lastWiFiRecoveryAction) + "</div></div>";
    html += "<div class='tile'><div class='label'>Build</div><div class='value'>" + String(BUILD_NUMBER) + "</div></div>";
    html += "<div class='tile'><div class='label'>OLED screen</div><div class='value'>" + htmlEscape(getScreenModeText()) + "</div></div>";
    html += "</div>";
    html += "<p class='muted'>OTA status: " + htmlEscape(lastOtaStatus) + "</p>";
    html += "<p class='muted'>Twilio status: " + htmlEscape(twilioLastStatus) + "</p>";
    html += "<p class='muted'>Last Twilio event: " + htmlEscape(twilioLastEvent) + "</p>";
    html += "</div>";

  }

  html += "</form>";

  if (appConfig.advancedWebView) {
    html += "<div class='card redline no-collapse' id='factory-reset'><h2>Factory reset</h2>";
    html += "<div class='dangerbox'><b>Permanent action.</b> This erases the complete default NVS partition, including saved Wi-Fi profiles, Nightscout details, admin login, sensor details, alarms, WhatsApp providers, caregiver routing and all other stored settings. Firmware and OTA application partitions are not erased.</div>";
    html += "<form method='POST' action='/factory-reset' onsubmit=\"return confirm('Erase all saved settings and return this Leah display to factory setup?');\">";
    html += "<label>Type RESET to confirm</label><input name='factory_confirm' autocomplete='off' required pattern='RESET' placeholder='RESET'>";
    html += "<button class='btn danger' type='submit'>Erase NVS and restore factory defaults</button></form>";
    html += "<p class='muted'>After restart, connect to <b>leah 2R - 2.42I</b> using <b>leah00000000</b>, then open 192.168.4.1.</p></div>";
  }

  html += "<div class='card blueline' id='firmware'><h2>Firmware and support actions</h2>";
  html += "<details open><summary>OLED screen controls</summary><div class='buttonrow'>";
  html += "<form method='POST' action='/screen-main'><button class='btn'>Show main</button></form>";
  html += "<form method='POST' action='/screen-trend'><button class='btn'>Show trend</button></form>";
  html += "<form method='POST' action='/screen-diagnostics'><button class='btn'>Show diagnostics</button></form>";
  html += "<form method='POST' action='/screen-sensor'><button class='btn'>Show sensor data</button></form>";
  html += "<form method='POST' action='/screen-insulin'><button class='btn'>Show insulin</button></form>";
  html += "<form method='POST' action='/screen-cob-iob-total'><button class='btn'>Show COB/IOB total</button></form>";
  html += "<form method='POST' action='/screen-twilio'><button class='btn'>Show WhatsApp</button></form>";
  html += "</div></details>";

  html += "<details class='testpanel' id='selectedProviderTests'><summary>Test selected WhatsApp provider</summary>";
  html += "<p class='muted' id='selectedProviderTestHint'>Active provider: " + htmlEscape(getAlertProviderLabel(appConfig.alertProvider)) + ". These buttons test only the selected provider.</p>";
  html += "<div class='buttonrow'>";
  html += "<form method='POST' action='/provider-test-parent1'><button class='btn provider-test-label' data-caregiver='1'>Test Caregiver 1</button></form>";
  html += "<form method='POST' action='/provider-test-parent2'><button class='btn provider-test-label' data-caregiver='2'>Test Caregiver 2</button></form>";
  html += "<form method='POST' action='/provider-test-parent3' data-textmebot-only='1'><button class='btn provider-test-label' data-caregiver='3'>Test Caregiver 3</button></form>";
  html += "<form method='POST' action='/provider-test-urgent-low'><button class='btn danger'>Urgent low to configured caregivers</button></form>";
  html += "<form method='POST' action='/provider-test-battery'><button class='btn warn'>Phone battery warning</button></form>";
  html += "<form method='POST' action='/provider-test-sensor'><button class='btn warn'>Sensor expiry reminder</button></form>";
  html += "</div></details>";

  html += "<details><summary>Firmware update</summary>";
  html += "<p>Installed: <b>" + String(FIRMWARE_VERSION) + " build " + String(BUILD_NUMBER) + "</b></p>";
  html += "<p>Channel: <b>" + htmlEscape(getUpdateChannelLabel()) + "</b></p>";
  html += "<p>Status: " + htmlEscape(lastOtaStatus) + "</p>";
  html += "<div class='buttonrow'>";
  html += "<form method='POST' action='/check-update'><button class='btn'>Check for update</button></form>";
  if (updateAvailable) {
    html += "<form method='POST' action='/install-update'><button class='btn danger'>Install update</button></form>";
  }
  html += "<form method='POST' action='/restart'><button class='btn secondary'>Restart device</button></form>";
  html += "</div></details>";

  html += "<details><summary>Support tools</summary><div class='buttonrow'>";
  html += "<form method='POST' action='/test-nightscout'><button class='btn secondary'>Test Nightscout</button></form>";
  html += "<form method='POST' action='/reset-wifi'><button class='btn danger'>Reset Wi-Fi setup</button></form>";
  html += "</div><p><a href='/logout'>Logout</a></p></details>";

  if (appConfig.advancedWebView) {
    html += "<details><summary>Advanced: individual provider test buttons</summary>";
    html += "<p class='muted'>Use these only when troubleshooting a provider that is not currently selected.</p>";
    html += "<h3>API / webhook</h3><div class='buttonrow'>";
    html += "<form method='POST' action='/api-test-parent1'><button class='btn'>API Caregiver 1</button></form>";
    html += "<form method='POST' action='/api-test-parent2'><button class='btn'>API Caregiver 2</button></form>";
    html += "<form method='POST' action='/api-test-urgent-low'><button class='btn danger'>API urgent low</button></form>";
    html += "<form method='POST' action='/api-test-nodata'><button class='btn warn'>API no-data</button></form>";
    html += "<form method='POST' action='/api-clear-status'><button class='btn secondary'>Clear API status</button></form></div>";
    html += "<h3>Twilio</h3><div class='buttonrow'>";
    html += "<form method='POST' action='/twilio-test-parent1'><button class='btn'>Twilio Caregiver 1</button></form>";
    html += "<form method='POST' action='/twilio-test-parent2'><button class='btn'>Twilio Caregiver 2</button></form>";
    html += "<form method='POST' action='/twilio-test-urgent-low'><button class='btn danger'>Twilio urgent low</button></form>";
    html += "<form method='POST' action='/twilio-test-nodata'><button class='btn warn'>Twilio no-data</button></form>";
    html += "<form method='POST' action='/twilio-clear-status'><button class='btn secondary'>Clear Twilio status</button></form></div>";
    html += "<h3>CallMeBot</h3><div class='buttonrow'>";
    html += "<form method='POST' action='/callmebot-test-parent1'><button class='btn'>CallMeBot Caregiver 1</button></form>";
    html += "<form method='POST' action='/callmebot-test-parent2'><button class='btn'>CallMeBot Caregiver 2</button></form>";
    html += "<form method='POST' action='/callmebot-test-urgent-low'><button class='btn danger'>CallMeBot urgent low</button></form>";
    html += "<form method='POST' action='/callmebot-test-nodata'><button class='btn warn'>CallMeBot no-data</button></form>";
    html += "<form method='POST' action='/callmebot-clear-status'><button class='btn secondary'>Clear CallMeBot status</button></form></div>";
    html += "<h3>TextMeBot</h3><div class='buttonrow'>";
    html += "<form method='POST' action='/textmebot-test-parent1'><button class='btn'>TextMeBot Caregiver 1</button></form>";
    html += "<form method='POST' action='/textmebot-test-parent2'><button class='btn'>TextMeBot Caregiver 2</button></form>";
    html += "<form method='POST' action='/textmebot-test-parent3'><button class='btn'>TextMeBot Caregiver 3</button></form>";
    html += "<form method='POST' action='/textmebot-test-urgent-low'><button class='btn danger'>TextMeBot urgent low</button></form>";
    html += "<form method='POST' action='/textmebot-test-nodata'><button class='btn warn'>TextMeBot no-data</button></form>";
    html += "<form method='POST' action='/textmebot-clear-status'><button class='btn secondary'>Clear TextMeBot status</button></form></div>";
    html += "</details>";
  }

  html += "</div>";

  html += "<script>";
  html += "function setCollapsed(el,on){if(el){el.classList.toggle('collapsed',!!on);}}";
  html += "function makeCardsCollapsible(){document.querySelectorAll('.card').forEach(function(c){if(c.id==='status'||c.classList.contains('no-collapse'))return;var h=null;for(var i=0;i<c.children.length;i++){var ch=c.children[i];if(ch.tagName==='H2'){h=ch;break;}if(ch.classList&&ch.classList.contains('provider-head')){h=ch.querySelector('h2');break;}}if(!h||h.dataset.bound==='1')return;h.dataset.bound='1';h.addEventListener('click',function(){c.classList.toggle('collapsed');});c.classList.add('collapsed');});}";
  html += "function fillWifiSlot(ssid,slot){var e=document.getElementById('wifi_ssid'+slot);if(e){e.value=ssid;e.focus();}var w=document.getElementById('wifi');if(w){w.open=true;}var s=document.getElementById('setup');if(s){s.classList.remove('collapsed');}}";
  html += "function updateSensorModeUi(){var s=document.getElementById('sensor_mode');var manual=document.getElementById('manualSensorSettings');var autoInfo=document.getElementById('sensorAutoInfo');var isManual=s&&s.value==='MANUAL';if(manual)manual.style.display=isManual?'block':'none';if(autoInfo)autoInfo.style.display=isManual?'none':'block';}";
  html += "function normalizeAliasPreview(v){v=(v||'').toLowerCase();if(v.indexOf('http://')===0)v=v.substring(7);if(v.indexOf('https://')===0)v=v.substring(8);v=v.split('/')[0];if(v.endsWith('.local'))v=v.substring(0,v.length-6);v=v.replace(/[^a-z0-9]+/g,'-');while(v.startsWith('-'))v=v.substring(1);while(v.endsWith('-'))v=v.substring(0,v.length-1);if(!v)v='leah-2r-242';return 'http://'+v+'.local';}";
  html += "function updateAliasPreview(){var input=document.getElementById('localAliasName');var preview=document.getElementById('localAliasPreview');if(input&&preview)preview.textContent=normalizeAliasPreview(input.value);}";
  html += "function updateProviderUi(){";
  html += "var s=document.getElementById('alert_provider');var p=s?s.value:'OFF';";
  html += "var labels={OFF:'Off',API:'API',TWILIO:'Twilio',CALLMEBOT:'CallMeBot',TEXTMEBOT:'TextMeBot'};";
  html += "var hints={OFF:'Cloud WhatsApp is disabled. Local display and buzzer still work.',API:'Custom API mode: enter endpoint, token and recipients.',TWILIO:'Twilio mode: enter SID, auth token, sender and WhatsApp recipients.',CALLMEBOT:'CallMeBot mode: each caregiver needs their own phone number and API key.',TEXTMEBOT:'TextMeBot mode: link your sender number once, then enter API key and recipients.'};";
  html += "document.querySelectorAll('.provider-panel').forEach(function(e){var on=e.getAttribute('data-provider')===p;e.classList.toggle('active',on);e.style.display=on?'block':'none';if(on){e.open=true;}else{e.open=false;}});";
  html += "document.querySelectorAll('[data-show-when]').forEach(function(e){e.style.display=(e.getAttribute('data-show-when')===p)?'block':'none';});";
  html += "document.querySelectorAll('[data-textmebot-only]').forEach(function(e){e.style.display=(p==='TEXTMEBOT')?'':'none';});";
  html += "document.querySelectorAll('[data-caregiver3-only]').forEach(function(e){e.style.display=(p==='TEXTMEBOT')?'':'none';});";
  html += "var caregivers=document.getElementById('caregivers');if(caregivers){caregivers.style.display=(p==='OFF')?'none':'block';setCollapsed(caregivers,p==='OFF');}";
  html += "var t=document.getElementById('selectedProviderTests');if(t){var ton=(p!=='OFF');t.classList.toggle('active',ton);t.style.display=ton?'block':'none';t.open=ton;}";
  html += "document.querySelectorAll('.provider-test-label').forEach(function(b){var n=b.getAttribute('data-caregiver');b.textContent='Test '+(labels[p]||'selected')+' Caregiver '+n;});";
  html += "var th=document.getElementById('selectedProviderTestHint');if(th){th.textContent=(p==='OFF')?'Select a WhatsApp provider to show test buttons.':'Active provider: '+(labels[p]||p)+'. These buttons test only the selected provider.';}";
  html += "var h=document.getElementById('providerHint');if(h){h.textContent=hints[p]||'';}";
  html += "}";
  html += "function formatMuteTime(s){s=Math.max(0,parseInt(s||0));if(s<=0)return 'Off';var m=Math.floor(s/60),r=s%60;return m+'m '+(r<10?'0':'')+r+'s';}";
  html += "function refreshMuteStatus(){var e=document.getElementById('muteRemaining');if(!e)return;fetch('/mute-status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(j){e.dataset.seconds=j.remaining_seconds||0;e.textContent=j.muted?formatMuteTime(j.remaining_seconds):'Off';}).catch(function(){var s=parseInt(e.dataset.seconds||0);if(s>0){s--;e.dataset.seconds=s;e.textContent=formatMuteTime(s);}});}";
  html += "document.addEventListener('DOMContentLoaded',function(){makeCardsCollapsible();updateProviderUi();updateSensorModeUi();updateAliasPreview();var aliasInput=document.getElementById('localAliasName');if(aliasInput)aliasInput.addEventListener('input',updateAliasPreview);refreshMuteStatus();setInterval(refreshMuteStatus,2000);});";
  html += "</script>";
  html += "</div></body></html>";

  return html;
}



// ==================================================
// 15C. USER-CONFIRMED OTA UPDATE INSTALL
// ==================================================

String bytesToHexString(const unsigned char* bytes, size_t length) {
  const char hexChars[] = "0123456789ABCDEF";
  String result = "";
  result.reserve(length * 2);

  for (size_t i = 0; i < length; i++) {
    result += hexChars[(bytes[i] >> 4) & 0x0F];
    result += hexChars[bytes[i] & 0x0F];
  }

  return result;
}

String normalizeFirmwareDownloadUrl(String url) {
  url.trim();

  if (url.length() == 0) return url;

  // Allow a tester to paste github.com/... links in the manifest by mistake.
  // The ESP32 must download the raw .bin file, not the GitHub HTML page.
  if (url.startsWith("github.com/")) {
    url = "https://" + url;
  }

  if (url.indexOf("github.com/") >= 0 && url.indexOf("/blob/") >= 0) {
    url.replace("https://github.com/", "https://raw.githubusercontent.com/");
    url.replace("http://github.com/", "https://raw.githubusercontent.com/");
    url.replace("/blob/", "/");
  }

  if (url.indexOf("github.com/") >= 0 && url.indexOf("/raw/") >= 0) {
    url.replace("https://github.com/", "https://raw.githubusercontent.com/");
    url.replace("http://github.com/", "https://raw.githubusercontent.com/");
    url.replace("/raw/", "/");
  }

  return url;
}

String httpClientErrorDescription(HTTPClient &http, int httpCode) {
  String text = http.errorToString(httpCode);
  text.trim();

  if (text.length() == 0) {
    text = "HTTP client error";
  }

  return "HTTP " + String(httpCode) + " - " + text;
}

bool isSafeForOtaInstall(String &reason) {
  if (WiFi.status() != WL_CONNECTED) {
    reason = "WiFi not connected";
    return false;
  }

  if (!isDataFresh()) {
    reason = "CGM data stale or missing";
    return false;
  }

  int level = getCurrentAlarmLevel();
  if (level != ALARM_NONE) {
    reason = "Active glucose alarm";
    return false;
  }

  reason = "OK";
  return true;
}


bool isHttpRedirectCode(int code) {
  return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
}

String makeAbsoluteRedirectUrl(String baseUrl, String location) {
  baseUrl.trim();
  location.trim();

  if (location.length() == 0) return location;

  if (location.startsWith("https://") || location.startsWith("http://")) {
    if (location.startsWith("http://")) {
      location.replace("http://", "https://");
    }
    return location;
  }

  if (location.startsWith("//")) {
    return "https:" + location;
  }

  if (location.startsWith("/")) {
    int schemePos = baseUrl.indexOf("://");
    if (schemePos < 0) return location;

    int hostStart = schemePos + 3;
    int pathStart = baseUrl.indexOf("/", hostStart);

    if (pathStart < 0) {
      return baseUrl + location;
    }

    return baseUrl.substring(0, pathStart) + location;
  }

  int lastSlash = baseUrl.lastIndexOf('/');
  if (lastSlash > 7) {
    return baseUrl.substring(0, lastSlash + 1) + location;
  }

  return location;
}

void prepareOtaHttpClient(HTTPClient &http) {
  http.setTimeout(60000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  // HTTP/1.1 is more reliable with GitHub release assets than forcing HTTP/1.0.
  // The manifest still supplies the expected firmware size, so the OTA read loop
  // does not depend on chunked transfer behaviour.
  http.useHTTP10(false);

  const char *headerKeys[] = { "Location", "Content-Length", "Content-Type" };
  http.collectHeaders(headerKeys, 3);
}

bool openFirmwareDownloadStream(HTTPClient &http, WiFiClientSecure &client, const String &startUrl, int &httpCode, String &httpErrorText, String &resolvedUrl) {
  String url = startUrl;
  resolvedUrl = startUrl;
  httpCode = -999;
  httpErrorText = "Not started";

  for (uint8_t redirectCount = 0; redirectCount <= 5; redirectCount++) {
    url.trim();

    if (!url.startsWith("https://")) {
      httpErrorText = "Redirect blocked: HTTPS required";
      httpCode = -998;
      return false;
    }

    http.end();
    client.stop();
    delay(100);

    prepareOtaHttpClient(http);

    Serial.print("OTA resolved URL attempt: ");
    Serial.println(url);

    if (!http.begin(client, url)) {
      httpErrorText = "HTTPS begin failed";
      httpCode = -997;
      Serial.println(httpErrorText);
      return false;
    }

    http.addHeader("Connection", "close");
    http.addHeader("Cache-Control", "no-cache");
    http.addHeader("Accept", "application/octet-stream");
    http.addHeader("User-Agent", "Leah-2R-Displays/" + String(FIRMWARE_VERSION));

    httpCode = http.GET();
    httpErrorText = httpClientErrorDescription(http, httpCode);

    Serial.print("OTA HTTP Code: ");
    Serial.println(httpCode);
    Serial.print("OTA HTTP Text: ");
    Serial.println(httpErrorText);

    if (httpCode == 200) {
      resolvedUrl = url;
      return true;
    }

    if (isHttpRedirectCode(httpCode)) {
      String location = http.header("Location");
      location.trim();

      Serial.print("OTA redirect location: ");
      Serial.println(location);

      if (location.length() == 0) {
        httpErrorText = "Redirect without Location header";
        return false;
      }

      url = makeAbsoluteRedirectUrl(url, location);
      resolvedUrl = url;

      http.end();
      client.stop();
      delay(250);
      continue;
    }

    return false;
  }

  httpErrorText = "Too many redirects";
  httpCode = -996;
  return false;
}

bool installFirmwareFromManifest() {
  String reason;

  if (!isSafeForOtaInstall(reason)) {
    lastOtaStatus = "Install blocked: " + reason;
    Serial.println(lastOtaStatus);
    drawMessage("OTA Blocked", reason, "Try later", "");
    delay(2000);
    return false;
  }

  if (!updateAvailable) {
    lastOtaStatus = "No update available";
    return false;
  }

  if (availableFirmwareUrl.length() == 0 || availableFirmwareSize == 0 || availableFirmwareSha256.length() == 0) {
    lastOtaStatus = "Manifest incomplete";
    drawMessage("OTA Failed", "Manifest", "incomplete", "");
    delay(2000);
    return false;
  }

  otaInProgress = true;
  lastOtaStatus = "Downloading " + availableFirmwareVersion;
  drawMessage("OTA Update", "Downloading", "FW " + availableFirmwareVersion, "Do not power off");

  WiFi.setSleep(false);

  String firmwareDownloadUrl = normalizeFirmwareDownloadUrl(availableFirmwareUrl);

  if (!firmwareDownloadUrl.startsWith("https://")) {
    lastOtaStatus = "Firmware URL must use HTTPS";
    Serial.print("Blocked firmware URL: ");
    Serial.println(firmwareDownloadUrl);
    drawMessage("OTA Failed", "Bad FW URL", "HTTPS required", "");
    delay(2000);
    otaInProgress = false;
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(60);

  HTTPClient https;

  int httpCode = -999;
  String httpErrorText = "";
  String resolvedFirmwareUrl = firmwareDownloadUrl;
  bool downloadOpen = false;

  for (uint8_t attempt = 1; attempt <= 3; attempt++) {
    Serial.print("OTA firmware URL: ");
    Serial.println(firmwareDownloadUrl);
    Serial.print("OTA download attempt ");
    Serial.println(attempt);

    downloadOpen = openFirmwareDownloadStream(https, client, firmwareDownloadUrl, httpCode, httpErrorText, resolvedFirmwareUrl);

    Serial.print("OTA final/resolved URL: ");
    Serial.println(resolvedFirmwareUrl);

    if (downloadOpen && httpCode == 200) {
      break;
    }

    https.end();
    client.stop();

    if (attempt < 3) {
      lastOtaStatus = "OTA retry " + String(attempt + 1) + ": " + httpErrorText;
      drawMessage("OTA Retry", "Download failed", "Attempt " + String(attempt + 1), shortenText(httpErrorText, 18));
      delay(2000);
      yield();
    }
  }

  if (!downloadOpen || httpCode != 200) {
    lastOtaStatus = "Download " + httpErrorText;
    drawMessage("OTA Failed", "Download", "HTTP " + String(httpCode), shortenText(httpErrorText, 20));
    delay(2500);
    https.end();
    client.stop();
    otaInProgress = false;
    return false;
  }

  int contentLength = https.getSize();
  Serial.print("OTA HTTP content length: ");
  Serial.println(contentLength);

  if (contentLength > 0 && (size_t)contentLength != availableFirmwareSize) {
    lastOtaStatus = "Size mismatch before download";
    Serial.print("Expected size: ");
    Serial.println(availableFirmwareSize);
    Serial.print("HTTP size: ");
    Serial.println(contentLength);
    https.end();
    client.stop();
    otaInProgress = false;
    return false;
  }

  if (!Update.begin(availableFirmwareSize)) {
    lastOtaStatus = "Update begin failed";
    Serial.print("Update error: ");
    Serial.println(Update.errorString());
    https.end();
    client.stop();
    otaInProgress = false;
    return false;
  }

  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  mbedtls_sha256_starts(&shaCtx, 0);

  WiFiClient *stream = https.getStreamPtr();

  uint8_t buffer[1024];
  size_t written = 0;
  unsigned long lastDataTime = millis();
  unsigned long lastProgressScreen = 0;

  while (written < availableFirmwareSize) {
    size_t available = stream->available();

    if (available > 0) {
      if (available > sizeof(buffer)) available = sizeof(buffer);
      size_t remaining = availableFirmwareSize - written;
      if (available > remaining) available = remaining;

      int bytesRead = stream->readBytes(buffer, available);

      if (bytesRead > 0) {
        mbedtls_sha256_update(&shaCtx, buffer, bytesRead);
        size_t bytesWritten = Update.write(buffer, bytesRead);

        if (bytesWritten != (size_t)bytesRead) {
          lastOtaStatus = "Flash write failed";
          Update.abort();
          mbedtls_sha256_free(&shaCtx);
          https.end();
          client.stop();
          otaInProgress = false;
          return false;
        }

        written += bytesWritten;
        lastDataTime = millis();

        if (millis() - lastProgressScreen > 1000) {
          int pct = (int)((written * 100UL) / availableFirmwareSize);
          drawMessage("OTA Update", "Downloading", String(pct) + "%", "Do not power off");
          Serial.print("OTA progress: ");
          Serial.print(pct);
          Serial.println("%");
          lastProgressScreen = millis();
        }
      }
    }
    else {
      if (millis() - lastDataTime > 45000) {
        lastOtaStatus = "Download timeout";
        Update.abort();
        mbedtls_sha256_free(&shaCtx);
        https.end();
        client.stop();
        otaInProgress = false;
        return false;
      }

      delay(10);
      yield();
    }
  }

  unsigned char shaResult[32];
  mbedtls_sha256_finish(&shaCtx, shaResult);
  mbedtls_sha256_free(&shaCtx);

  String calculatedSha = bytesToHexString(shaResult, 32);
  calculatedSha.toUpperCase();
  availableFirmwareSha256.toUpperCase();

  Serial.print("OTA written bytes: ");
  Serial.println(written);
  Serial.print("OTA calculated SHA256: ");
  Serial.println(calculatedSha);
  Serial.print("OTA expected SHA256: ");
  Serial.println(availableFirmwareSha256);

  if (written != availableFirmwareSize) {
    lastOtaStatus = "Downloaded size mismatch";
    Update.abort();
    https.end();
    client.stop();
    otaInProgress = false;
    return false;
  }

  if (calculatedSha != availableFirmwareSha256) {
    lastOtaStatus = "SHA256 mismatch";
    Update.abort();
    https.end();
    client.stop();
    otaInProgress = false;
    drawMessage("OTA Failed", "SHA mismatch", "Not installed", "");
    delay(3000);
    return false;
  }

  // The complete manifest-sized image must be present before finalization. Use
  // strict end(false), not evenIfRemaining=true, so a partial image can never be
  // accepted merely because the HTTP transfer ended.
  if (!Update.isFinished()) {
    lastOtaStatus = "Update image incomplete";
    Serial.println(lastOtaStatus);
    Update.abort();
    https.end();
    client.stop();
    otaInProgress = false;
    drawMessage("OTA Failed", "Image incomplete", "Old FW retained", "");
    delay(3000);
    return false;
  }

  if (!Update.end(false)) {
    lastOtaStatus = "Update end failed";
    Serial.print("Update error: ");
    Serial.println(Update.errorString());
    https.end();
    client.stop();
    otaInProgress = false;
    return false;
  }

  // Strict Update.end(false) validates the image and selects its OTA partition. Confirm
  // that Espressif now reports a different boot partition before restarting.
  const esp_partition_t *runningPartition = esp_ota_get_running_partition();
  const esp_partition_t *bootPartition = esp_ota_get_boot_partition();

  if (runningPartition == nullptr || bootPartition == nullptr ||
      runningPartition->address == bootPartition->address) {
    lastOtaStatus = "OTA boot partition not selected";
    Serial.println(lastOtaStatus);
    if (runningPartition) { Serial.print("Running partition: "); Serial.println(runningPartition->label); }
    if (bootPartition) { Serial.print("Boot partition: "); Serial.println(bootPartition->label); }
    https.end();
    client.stop();
    otaInProgress = false;
    drawMessage("OTA Failed", "Boot slot error", "Old FW retained", "");
    delay(3000);
    return false;
  }

  Serial.print("OTA running partition: ");
  Serial.print(runningPartition->label);
  Serial.print(" @0x");
  Serial.println(runningPartition->address, HEX);
  Serial.print("OTA next boot partition: ");
  Serial.print(bootPartition->label);
  Serial.print(" @0x");
  Serial.println(bootPartition->address, HEX);

  https.end();
  client.stop();

  storePendingOtaBootConfirmation(availableFirmwareVersion);

  lastOtaStatus = "Update complete and verified";
  otaInProgress = true;
  peripheralRestartInProgress = true;
  alarmAudioArmed = false;
  sirenEnabled = false;
  sirenSilenced = true;
  activeAudioAlarmType = ALARM_NONE;

  // Freeze application workers before the final boot handover. The system Wi-Fi
  // and TCP/IP tasks remain available long enough to close the completed request.
  if (cloudAlertTaskHandle != nullptr) vTaskSuspend(cloudAlertTaskHandle);
  if (alarmAudioTaskHandle != nullptr) vTaskSuspend(alarmAudioTaskHandle);
  forceBuzzerHardwareOff();

  // Start a Core-0 restart guard before the final OLED message. Even if the
  // software-I2C display call stalls, the guard invokes esp_restart().
  startOtaRestartGuard();
  drawMessage("OTA COMPLETE", "Image verified", "Restarting now", "FW " + availableFirmwareVersion);
  Serial.println("OTA complete. Restarting into verified boot partition.");
  Serial.flush();
  delay(1500);
  restartImmediatelyWithEspIdf("verified OTA install");

  return true;
}


void handleWebRoot() {
  if (!requireWebLogin()) return;
  webServer.send(200, "text/html", getDashboardHtml());
}

void handleWebSetView() {
  if (!requireWebLogin()) return;

  // Page view is a UI preference only. Do not run the full configuration save,
  // restart mDNS, read Nightscout, or touch the active Wi-Fi connection.
  appConfig.advancedWebView = webServer.hasArg("advanced_view");

  Preferences viewPreferences;
  bool stored = false;
  if (viewPreferences.begin("cgm_cfg", false)) {
    stored = viewPreferences.putBool("advView", appConfig.advancedWebView) > 0;
    viewPreferences.end();
  }

  wifiRecoveryHoldoffUntilMs = millis() + 3000UL;

  String response = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  response += "<meta http-equiv='refresh' content='1;url=/'>";
  response += "<style>body{font-family:Arial;padding:20px}</style></head><body>";
  response += "<h2>Page view updated</h2><p>Advanced technical settings are now <b>";
  response += appConfig.advancedWebView ? "shown" : "hidden";
  response += "</b>.</p>";
  if (!stored) response += "<p>Warning: the view preference could not be verified in NVS.</p>";
  response += "<p><a href='/'>Return to dashboard</a></p></body></html>";
  webServer.send(200, "text/html", response);
}

void handleWebSave() {
  if (!requireWebLogin()) return;

  // Keep provider credentials and caregiver routing stable while the cloud worker is sending.
  // Alarm acknowledgement remains available through the separate /mute endpoint.
  if (selectedProviderBusy()) {
    webServer.send(409, "text/html",
      "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'></head>"
      "<body><h2>Cloud alert in progress</h2><p>Please save settings after the queued WhatsApp alert has completed.</p>"
      "<p>The Acknowledge + snooze button remains available on the dashboard.</p><p><a href='/'>Back</a></p></body></html>");
    return;
  }

  String previousAliasName = getLocalMdnsName();

  appConfig.deviceName = webServer.arg("device_name");
  appConfig.patientName = webServer.arg("patient_name");
  appConfig.deviceLocation = webServer.arg("device_location");

  appConfig.deviceName.trim();
  appConfig.patientName.trim();
  appConfig.deviceLocation.trim();

  String submittedAlias = sanitizeLocalAliasName(webServer.arg("local_alias"));
  if (submittedAlias.length() > 0) appConfig.localAliasName = submittedAlias;

  if (appConfig.deviceName.length() == 0) appConfig.deviceName = "Leah 2R Displays";
  if (appConfig.patientName.length() == 0) appConfig.patientName = "Patient";
  if (appConfig.deviceLocation.length() == 0) appConfig.deviceLocation = "Room";

  String formGlucoseUnits = normalizeGlucoseUnits(appConfig.glucoseUnits);
  String submittedGlucoseUnits = normalizeGlucoseUnits(webServer.arg("glucose_units"));
  if (submittedGlucoseUnits.length() == 0) submittedGlucoseUnits = formGlucoseUnits;

  String submittedSensorMode = webServer.arg("sensor_mode");
  submittedSensorMode.trim();
  submittedSensorMode.toUpperCase();
  if (submittedSensorMode.length() > 0) {
    appConfig.sensorAutoRead = submittedSensorMode != "MANUAL";
  } else {
    // Backward-compatible handling for older cached pages.
    appConfig.sensorAutoRead = webServer.hasArg("sensor_auto_read");
  }

  appConfig.sensorWearDays = constrain(webServer.arg("sensor_wear_days").toInt(), 1, 30);

  // Preserve local manual settings while Auto is selected, but never use them
  // for sensor age/countdown until Manual mode is explicitly selected.
  if (webServer.hasArg("sensor_source")) appConfig.sensorSource = normalizeSensorSource(webServer.arg("sensor_source"));
  if (webServer.hasArg("sensor_serial")) appConfig.sensorSerial = webServer.arg("sensor_serial");
  if (webServer.hasArg("sensor_start_date")) appConfig.sensorStartDate = webServer.arg("sensor_start_date");
  if (webServer.hasArg("sensor_start_time")) appConfig.sensorStartTime = normalizeSensorStartTime(webServer.arg("sensor_start_time"));
  if (appConfig.sensorAutoRead) {
    appConfig.sensorSource = "Auto";
  } else if (appConfig.sensorSource.length() == 0 || appConfig.sensorSource == "Auto") {
    appConfig.sensorSource = "Manual";
  }
  appConfig.sensorSerial.trim();
  appConfig.sensorStartDate.trim();

  appConfig.nightscoutHost = normalizeNightscoutHost(webServer.arg("ns_host"));

  String newAdminUser = webServer.arg("admin_user");
  String newAdminPass = webServer.arg("admin_password");

  newAdminUser.trim();
  newAdminPass.trim();

  if (newAdminUser.length() > 0) {
    appConfig.adminUsername = newAdminUser;
  }

  if (newAdminPass.length() > 0) {
    if (newAdminPass.length() >= 8) {
      appConfig.adminPassword = newAdminPass;
    } else {
      webServer.send(400, "text/html", "<html><body><h2>Password too short</h2><p>Use at least 8 characters.</p><p><a href='/'>Back</a></p></body></html>");
      return;
    }
  }

  String newToken = webServer.arg("ns_token");
  newToken.trim();
  if (newToken.length() > 0) {
    appConfig.nightscoutToken = newToken;
  }

  String oldWifiSsid1 = appConfig.wifiSsid1;
  String oldWifiSsid2 = appConfig.wifiSsid2;
  String oldWifiSsid3 = appConfig.wifiSsid3;

  if (webServer.hasArg("wifi_ssid1")) appConfig.wifiSsid1 = webServer.arg("wifi_ssid1");
  if (webServer.hasArg("wifi_ssid2")) appConfig.wifiSsid2 = webServer.arg("wifi_ssid2");
  if (webServer.hasArg("wifi_ssid3")) appConfig.wifiSsid3 = webServer.arg("wifi_ssid3");
  appConfig.wifiSsid1.trim();
  appConfig.wifiSsid2.trim();
  appConfig.wifiSsid3.trim();

  String newWifiPass1 = webServer.arg("wifi_pass1");
  String newWifiPass2 = webServer.arg("wifi_pass2");
  String newWifiPass3 = webServer.arg("wifi_pass3");
  // Do not trim passwords: spaces may be intentional password characters.
  if (newWifiPass1.length() > 0) appConfig.wifiPass1 = newWifiPass1;
  else if (appConfig.wifiSsid1 != oldWifiSsid1) appConfig.wifiPass1 = "";
  if (newWifiPass2.length() > 0) appConfig.wifiPass2 = newWifiPass2;
  else if (appConfig.wifiSsid2 != oldWifiSsid2) appConfig.wifiPass2 = "";
  if (newWifiPass3.length() > 0) appConfig.wifiPass3 = newWifiPass3;
  else if (appConfig.wifiSsid3 != oldWifiSsid3) appConfig.wifiPass3 = "";
  normalizeWiFiProfilesInMemory();

  appConfig.useNightscoutLimits = webServer.hasArg("use_ns_limits");

  // Parse alert limit fields using the unit that was visible when the page was opened.
  // This avoids corrupting thresholds when the user changes the display unit selector.
  appConfig.glucoseUnits = formGlucoseUnits;
  appConfig.urgentLow = parseLimitInputToMmol(webServer.arg("urgent_low"), 3.0);
  appConfig.low = parseLimitInputToMmol(webServer.arg("low"), 3.9);
  appConfig.high = parseLimitInputToMmol(webServer.arg("high"), 10.0);
  appConfig.urgentHigh = parseLimitInputToMmol(webServer.arg("urgent_high"), 11.0);
  appConfig.glucoseUnits = submittedGlucoseUnits;

  if (appConfig.urgentLow <= 0) appConfig.urgentLow = 3.0;
  if (appConfig.low <= 0) appConfig.low = 3.9;
  if (appConfig.high <= 0) appConfig.high = 10.0;
  if (appConfig.urgentHigh <= 0) appConfig.urgentHigh = 11.0;

  appConfig.alarmSoundEnabled = webServer.hasArg("alarm_sound_enabled");
  appConfig.randomAudioEnabled = webServer.hasArg("random_audio_enabled");
  appConfig.audioMuteMinutes = constrain(webServer.arg("audio_mute_minutes").toInt(), 1, 30);
  appConfig.soundUrgentLow = webServer.hasArg("sound_urgent_low");
  appConfig.soundLow = webServer.hasArg("sound_low");
  appConfig.soundHigh = webServer.hasArg("sound_high");
  appConfig.soundUrgentHigh = webServer.hasArg("sound_urgent_high");
  appConfig.soundNoData = webServer.hasArg("sound_no_data");
  appConfig.muteLowMinutes = appConfig.audioMuteMinutes;
  appConfig.muteHighMinutes = appConfig.audioMuteMinutes;
  appConfig.muteNoDataMinutes = appConfig.audioMuteMinutes;

  // Advanced/basic page view is saved only by /set-view. Preserve it here.

  appConfig.displayContrast = constrain(webServer.arg("contrast").toInt(), 20, 255);
  appConfig.nightDimEnabled = webServer.hasArg("night_dim");
  appConfig.nightContrast = constrain(webServer.arg("night_contrast").toInt(), 20, 255);
  appConfig.dimStartHour = constrain(webServer.arg("dim_start_hour").toInt(), 0, 23);
  appConfig.dimStartMinute = constrain(webServer.arg("dim_start_minute").toInt(), 0, 59);
  appConfig.dimEndHour = constrain(webServer.arg("dim_end_hour").toInt(), 0, 23);
  appConfig.dimEndMinute = constrain(webServer.arg("dim_end_minute").toInt(), 0, 59);

  appConfig.alertProvider = normalizeAlertProvider(webServer.arg("alert_provider"));
  appConfig.apiEndpoint = webServer.arg("api_endpoint");
  appConfig.apiParent1 = webServer.arg("api_parent1");
  appConfig.apiParent2 = webServer.arg("api_parent2");

  String newApiKey = webServer.arg("api_key");
  newApiKey.trim();
  if (newApiKey.length() > 0) appConfig.apiKey = newApiKey;

  appConfig.apiEndpoint.trim();
  appConfig.apiKey.trim();
  appConfig.apiParent1.trim();
  appConfig.apiParent2.trim();

  appConfig.twilioEnabled = webServer.hasArg("twilio_enabled");
  appConfig.twilioSid = webServer.arg("twilio_sid");
  appConfig.twilioFrom = webServer.arg("twilio_from");
  appConfig.twilioParent1 = webServer.arg("twilio_parent1");
  appConfig.twilioParent2 = webServer.arg("twilio_parent2");

  String newTwilioToken = webServer.arg("twilio_token");
  newTwilioToken.trim();
  if (newTwilioToken.length() > 0) {
    appConfig.twilioToken = newTwilioToken;
  }

  appConfig.twilioSid.trim();
  appConfig.twilioFrom.trim();
  appConfig.twilioParent1.trim();
  appConfig.twilioParent2.trim();
  if (appConfig.twilioFrom.length() == 0) appConfig.twilioFrom = "whatsapp:+14155238886";

  appConfig.callMeBotEnabled = webServer.hasArg("callmebot_enabled");
  appConfig.callMeBotParent1 = webServer.arg("cb_parent1");
  appConfig.callMeBotParent2 = webServer.arg("cb_parent2");

  String newCallMeBotKey1 = webServer.arg("cb_key1");
  String newCallMeBotKey2 = webServer.arg("cb_key2");
  newCallMeBotKey1.trim();
  newCallMeBotKey2.trim();
  if (newCallMeBotKey1.length() > 0) appConfig.callMeBotApiKey1 = newCallMeBotKey1;
  if (newCallMeBotKey2.length() > 0) appConfig.callMeBotApiKey2 = newCallMeBotKey2;

  appConfig.callMeBotParent1.trim();
  appConfig.callMeBotParent2.trim();
  appConfig.callMeBotApiKey1.trim();
  appConfig.callMeBotApiKey2.trim();

  appConfig.textMeBotEnabled = webServer.hasArg("textmebot_enabled");
  appConfig.textMeBotParent1 = webServer.arg("tmb_parent1");
  appConfig.textMeBotParent2 = webServer.arg("tmb_parent2");
  appConfig.textMeBotParent3 = webServer.arg("tmb_parent3");

  String newTextMeBotKey = webServer.arg("tmb_key");
  newTextMeBotKey.trim();
  if (newTextMeBotKey.length() > 0) appConfig.textMeBotApiKey = newTextMeBotKey;

  appConfig.textMeBotParent1.trim();
  appConfig.textMeBotParent2.trim();
  appConfig.textMeBotParent3.trim();
  appConfig.textMeBotApiKey.trim();

  appConfig.caregiver1Name = webServer.arg("cg1_name");
  appConfig.caregiver2Name = webServer.arg("cg2_name");
  appConfig.caregiver3Name = webServer.arg("cg3_name");
  appConfig.caregiver1Name.trim();
  appConfig.caregiver2Name.trim();
  appConfig.caregiver3Name.trim();
  if (appConfig.caregiver1Name.length() == 0) appConfig.caregiver1Name = "Caregiver 1";
  if (appConfig.caregiver2Name.length() == 0) appConfig.caregiver2Name = "Caregiver 2";
  if (appConfig.caregiver3Name.length() == 0) appConfig.caregiver3Name = "Caregiver 3";

  appConfig.cg1UrgentLow = webServer.hasArg("cg1_urgent_low");
  appConfig.cg1Low = webServer.hasArg("cg1_low");
  appConfig.cg1High = webServer.hasArg("cg1_high");
  appConfig.cg1UrgentHigh = webServer.hasArg("cg1_urgent_high");
  appConfig.cg1NoData = webServer.hasArg("cg1_no_data");
  appConfig.cg2UrgentLow = webServer.hasArg("cg2_urgent_low");
  appConfig.cg2Low = webServer.hasArg("cg2_low");
  appConfig.cg2High = webServer.hasArg("cg2_high");
  appConfig.cg2UrgentHigh = webServer.hasArg("cg2_urgent_high");
  appConfig.cg2NoData = webServer.hasArg("cg2_no_data");
  appConfig.cg3UrgentLow = webServer.hasArg("cg3_urgent_low");
  appConfig.cg3Low = webServer.hasArg("cg3_low");
  appConfig.cg3High = webServer.hasArg("cg3_high");
  appConfig.cg3UrgentHigh = webServer.hasArg("cg3_urgent_high");
  appConfig.cg3NoData = webServer.hasArg("cg3_no_data");

  appConfig.glucoseUnits = normalizeGlucoseUnits(submittedGlucoseUnits);
  if (webServer.hasArg("insulin_action_hours")) appConfig.insulinActionHours = webServer.arg("insulin_action_hours").toFloat();
  appConfig.insulinActionHours = constrain(appConfig.insulinActionHours, 1.0, 8.0);

  appConfig.lowRepeatMinutes = constrain(webServer.arg("low_repeat").toInt(), 1, 240);
  appConfig.urgentLowRepeatMinutes = constrain(webServer.arg("urgent_low_repeat").toInt(), 1, 240);
  appConfig.highRepeatMinutes = constrain(webServer.arg("high_repeat").toInt(), 1, 240);
  appConfig.urgentHighRepeatMinutes = constrain(webServer.arg("urgent_high_repeat").toInt(), 1, 240);
  appConfig.noDataRepeatMinutes = constrain(webServer.arg("no_data_repeat").toInt(), 1, 240);
  appConfig.phoneBatteryAlertEnabled = webServer.hasArg("phone_batt_alert");
  appConfig.phoneBatteryAlertPercent = constrain(webServer.arg("phone_batt_percent").toInt(), 1, 100);
  appConfig.phoneBatteryRepeatMinutes = constrain(webServer.arg("phone_batt_repeat").toInt(), 5, 1440);
  appConfig.sensorExpiryReminderEnabled = webServer.hasArg("sensor_expiry_alert");
  appConfig.sensorExpiryReminderDays = constrain(webServer.arg("sensor_expiry_days").toInt(), 1, 14);
  appConfig.sensorExpiryRepeatHours = constrain(webServer.arg("sensor_expiry_repeat").toInt(), 1, 168);

  applyAlertProviderToEnabledFlags();

  String previousChannel = appConfig.updateChannel;
  String requestedChannel = webServer.arg("update_channel");
  requestedChannel.trim();

  if (ENABLE_BETA_CHANNEL_SELECTOR) {
    appConfig.updateChannel = normalizeUpdateChannel(requestedChannel);
  } else {
    // Commercial/stable firmware ignores any browser-submitted beta value.
    appConfig.updateChannel = "stable";
  }

  if (normalizeUpdateChannel(previousChannel) != normalizeUpdateChannel(appConfig.updateChannel)) {
    availableFirmwareVersion = "";
    availableFirmwareBuild = 0;
    availableFirmwareNotes = "";
    availableFirmwareUrl = "";
    availableFirmwareSha256 = "";
    availableFirmwareSize = 0;
    availableFirmwareChannel = "";
    updateAvailable = false;
    lastFirmwareCheck = 0;
    lastOtaStatus = "Channel changed";
  }


  saveConfig();
  if (!verifyWiFiProfilesInNvs()) {
    Serial.println("WARNING: Web Wi-Fi profile save did not verify in NVS.");
  }

  // Do not perform HTTPS work inside the Web save request. A TLS allocation
  // immediately after a large form/NVS write can briefly disturb the Wi-Fi
  // stack and makes the page appear frozen. Schedule the SAGE refresh for the
  // normal main loop after the response has completed.
  if (appConfig.sensorAutoRead) {
    sageStatus = "Refresh pending";
    scheduledSageRefreshAtMs = millis() + 5000UL;
  } else {
    scheduledSageRefreshAtMs = 0;
    sageStatus = "Manual local";
    updateDetectedCgmSource();
  }

  bool reconnectNow = (webServer.uri() == "/save-wifi-reconnect");

  // A normal settings save must never touch the station connection. Restart
  // mDNS only when the alias itself changed; mDNS changes do not require Wi-Fi
  // disconnect/reconnect.
  String currentAliasName = getLocalMdnsName();
  if (!reconnectNow && WiFi.status() == WL_CONNECTED && currentAliasName != previousAliasName) {
    startLocalMdnsAlias();
  }

  // Ignore short driver-status disturbances caused by flash/NVS activity.
  // The main loop may still report status, but it will not launch a destructive
  // profile scan/reconnect immediately after a successful save.
  wifiRecoveryHoldoffUntilMs = millis() + WIFI_SAVE_RECOVERY_HOLDOFF_MS;

  String response = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  if (!reconnectNow) response += "<meta http-equiv='refresh' content='2;url=/'>";
  response += "<style>body{font-family:Arial;padding:20px}</style></head><body>";
  if (reconnectNow) {
    response += "<h2>Wi-Fi saved</h2><p>The device is restarting now and will try NVS Wi-Fi Slot 1, then Slot 2, then Slot 3. Each profile has a hard timeout; if none connect, the setup portal opens again.</p><p>Reconnect using the IP address or <b>" + htmlEscape(getLocalMdnsHost()) + "</b> after it comes online.</p>";
  } else {
    response += "<h2>Settings saved</h2><p>The device will use the new settings now.</p><p><a href='/'>Return to dashboard</a></p>";
  }
  response += "</body></html>";

  webServer.send(200, "text/html", response);

  currentScreen = SCREEN_MAIN;
  drawGlucoseScreen();

  if (reconnectNow) {
    delay(1200);
    restartDeviceSafely();
  }
}

void handleWebMute() {
  if (!requireWebLogin()) return;

  updateCloudAlarmEpisodeTracker();
  sirenSilenced = true;
  mutedAudioAlarmType = getCurrentAudioAlarmType();
  silenceStartTime = millis();
  activeSilenceDurationMs = getMuteDurationForCurrentStateMs();
  if (currentCloudAlarmEpisodeKey != "NONE") {
    acknowledgedCloudAlarmEpisodeId = currentCloudAlarmEpisodeId;
  }

  webServer.sendHeader("Location", "/", true);
  webServer.send(302, "text/plain", "");
}

void handleWebMuteStatus() {
  if (!requireWebLogin()) return;

  String response = "{";
  response += "\"muted\":" + String(sirenSilenced ? "true" : "false");
  response += ",\"remaining_seconds\":" + String(getSirenMuteRemainingSeconds());
  response += ",\"remaining\":\"" + htmlEscape(getSirenMuteRemainingText()) + "\"";
  response += ",\"duration_minutes\":" + String(appConfig.audioMuteMinutes);
  response += "}";

  webServer.send(200, "application/json", response);
}

void handleWebTestNightscout() {
  if (!requireWebLogin()) return;

  bool ok = readNightscoutEntries();

  String response = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  response += "<style>body{font-family:Arial;padding:20px}</style></head><body>";

  if (ok) {
    response += "<h2>Nightscout test OK</h2>";
    response += "<p>Latest glucose: " + formatGlucoseDisplay(glucoseMmol) + " " + getGlucoseUnitLabel() + "</p>";
    response += "<p>Age: " + String(ageMinutes) + " min</p>";
  } else {
    response += "<h2>Nightscout test failed</h2>";
    response += "<p>Status: " + htmlEscape(lastStatus) + "</p>";
    response += "<p>Check Nightscout host and token.</p>";
  }

  response += "<p><a href='/'>Return to dashboard</a></p></body></html>";
  webServer.send(200, "text/html", response);
}


void handleWebCheckUpdate() {
  if (!requireWebLogin()) return;

  bool found = checkFirmwareUpdateManifest(false);

  String response = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  response += "<meta http-equiv='refresh' content='2;url=/'>";
  response += "<style>body{font-family:Arial;padding:20px}</style></head><body>";
  response += "<h2>Firmware check complete</h2>";
  response += "<p>Installed: " + String(FIRMWARE_VERSION) + " build " + String(BUILD_NUMBER) + "</p>";
  response += "<p>Available: " + htmlEscape(availableFirmwareVersion) + " build " + String(availableFirmwareBuild) + "</p>";
  response += "<p>Update available: ";
  response += found ? "YES" : "NO";
  response += "</p><p><a href='/'>Return to dashboard</a></p></body></html>";

  webServer.send(200, "text/html", response);
}

void handleWebInstallUpdate() {
  if (!requireWebLogin()) return;

  String reason;
  if (!isSafeForOtaInstall(reason)) {
    String response = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    response += "<style>body{font-family:Arial;padding:20px}</style></head><body>";
    response += "<h2>Update blocked</h2>";
    response += "<p>Reason: " + htmlEscape(reason) + "</p>";
    response += "<p>Try again when glucose data is fresh and no alarm is active.</p>";
    response += "<p><a href='/'>Return to dashboard</a></p></body></html>";
    webServer.send(409, "text/html", response);
    return;
  }

  webServer.send(200, "text/html", "<html><body><h2>Update starting...</h2><p>Do not power off the device.</p></body></html>");
  delay(500);

  if (!installFirmwareFromManifest()) {
    drawMessage("OTA Failed", shortenText(lastOtaStatus, 20), "Open web page", "");
  }
}


void redirectBackToDashboard() {
  webServer.sendHeader("Location", "/", true);
  webServer.send(302, "text/plain", "");
}

void handleWebScreenMain() {
  if (!requireWebLogin()) return;

  currentScreen = SCREEN_MAIN;
  lastScreenInteraction = millis();
  drawGlucoseScreen();

  redirectBackToDashboard();
}

void handleWebScreenTrend() {
  if (!requireWebLogin()) return;

  currentScreen = SCREEN_TREND;
  lastScreenInteraction = millis();

  // Send response first so the phone browser does not feel stuck while
  // the ESP32 performs the 4-hour Nightscout trend read.
  redirectBackToDashboard();

  drawTrendScreen();
}

void handleWebScreenDiagnostics() {
  if (!requireWebLogin()) return;

  currentScreen = SCREEN_DIAGNOSTICS;
  lastScreenInteraction = millis();
  drawDiagnosticsScreen();

  redirectBackToDashboard();
}



void handleWebScreenSensorData() {
  if (!requireWebLogin()) return;

  currentScreen = SCREEN_SENSOR_DATA;
  lastScreenInteraction = millis();
  drawSensorDataScreen();

  redirectBackToDashboard();
}

void handleWebScreenInsulin() {
  if (!requireWebLogin()) return;

  currentScreen = SCREEN_INSULIN;
  lastScreenInteraction = millis();
  readNightscoutBolusInfo();
  drawInsulinScreen();

  redirectBackToDashboard();
}

void handleWebScreenCobIobTotal() {
  if (!requireWebLogin()) return;

  currentScreen = SCREEN_COB_IOB_TOTAL;
  lastScreenInteraction = millis();
  readNightscoutIobCobInfo();
  readNightscoutBolusInfo();
  drawCobIobTotalScreen();

  redirectBackToDashboard();
}

void handleWebProviderTestParent1() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("SELECTED", "p1", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebProviderTestParent2() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("SELECTED", "p2", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebProviderTestParent3() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("SELECTED", "p3", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebProviderTestUrgentLow() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("SELECTED", "both", "URGENT_LOW", true, 0);
  redirectBackToDashboard();
}

void handleWebProviderTestBattery() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("SELECTED", "both", "PHONE_BATTERY_LOW", true, 0);
  redirectBackToDashboard();
}

void handleWebProviderTestSensor() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("SELECTED", "both", "SENSOR_EXPIRY", true, 0);
  redirectBackToDashboard();
}

void handleWebApiTestParent1() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("API", "p1", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebApiTestParent2() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("API", "p2", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebApiTestUrgentLow() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("API", "both", "URGENT_LOW", true, 0);
  redirectBackToDashboard();
}

void handleWebApiTestNoData() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("API", "both", "NO_DATA", true, 0);
  redirectBackToDashboard();
}

void handleWebApiClearStatus() {
  if (!requireWebLogin()) return;
  if (selectedProviderBusy()) { redirectBackToDashboard(); return; }
  apiLastStatus = "Cleared";
  apiLastResponse = "";
  apiLastHttpCode = 0;
  apiLastEvent = "None";
  redirectBackToDashboard();
}

void handleWebTwilioTestParent1() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("TWILIO", "p1", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebTwilioTestParent2() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("TWILIO", "p2", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebTwilioTestUrgentLow() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("TWILIO", "both", "URGENT_LOW", true, 0);
  redirectBackToDashboard();
}

void handleWebTwilioTestNoData() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("TWILIO", "both", "NO_DATA", true, 0);
  redirectBackToDashboard();
}

void handleWebTwilioClearStatus() {
  if (!requireWebLogin()) return;
  if (selectedProviderBusy()) { redirectBackToDashboard(); return; }
  twilioLastStatus = "Cleared";
  twilioLastResponse = "";
  twilioLastHttpCode = 0;
  twilioLastEvent = "None";
  redirectBackToDashboard();
}

void handleWebCallMeBotTestParent1() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("CALLMEBOT", "p1", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebCallMeBotTestParent2() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("CALLMEBOT", "p2", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebCallMeBotTestUrgentLow() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("CALLMEBOT", "both", "URGENT_LOW", true, 0);
  redirectBackToDashboard();
}

void handleWebCallMeBotTestNoData() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("CALLMEBOT", "both", "NO_DATA", true, 0);
  redirectBackToDashboard();
}

void handleWebCallMeBotClearStatus() {
  if (!requireWebLogin()) return;
  if (selectedProviderBusy()) { redirectBackToDashboard(); return; }
  callMeBotLastStatus = "Cleared";
  callMeBotLastResponse = "";
  callMeBotLastHttpCode = 0;
  callMeBotLastEvent = "None";
  redirectBackToDashboard();
}


void handleWebTextMeBotTestParent1() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("TEXTMEBOT", "p1", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebTextMeBotTestParent2() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("TEXTMEBOT", "p2", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebTextMeBotTestParent3() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("TEXTMEBOT", "p3", "normal", true, 0);
  redirectBackToDashboard();
}

void handleWebTextMeBotTestUrgentLow() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("TEXTMEBOT", "both", "URGENT_LOW", true, 0);
  redirectBackToDashboard();
}

void handleWebTextMeBotTestNoData() {
  if (!requireWebLogin()) return;
  queueCloudAlertJob("TEXTMEBOT", "both", "NO_DATA", true, 0);
  redirectBackToDashboard();
}

void handleWebTextMeBotClearStatus() {
  if (!requireWebLogin()) return;
  if (selectedProviderBusy()) { redirectBackToDashboard(); return; }
  textMeBotLastStatus = "Cleared";
  textMeBotLastResponse = "";
  textMeBotLastHttpCode = 0;
  textMeBotLastEvent = "None";
  redirectBackToDashboard();
}

void handleWebScreenTwilio() {
  if (!requireWebLogin()) return;

  currentScreen = SCREEN_TWILIO;
  lastScreenInteraction = millis();
  drawTwilioScreen();

  webServer.send(200, "text/html", "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'></head><body><h2>WhatsApp screen shown</h2><p><a href='/'>Back</a></p></body></html>");
}


void handleWebRestart() {
  if (!requireWebLogin()) return;

  webServer.send(200, "text/html", "<html><body><h2>Restarting...</h2></body></html>");
  delay(500);
  restartDeviceSafely();
}

void handleWebResetWiFi() {
  if (!requireWebLogin()) return;

  webServer.send(200, "text/html", "<html><body><h2>WiFi reset. Restarting...</h2><p>Reconnect to setup portal.</p></body></html>");
  delay(500);
  appConfig.wifiSsid1 = ""; appConfig.wifiPass1 = "";
  appConfig.wifiSsid2 = ""; appConfig.wifiPass2 = "";
  appConfig.wifiSsid3 = ""; appConfig.wifiPass3 = "";
  saveConfig();
  WiFi.disconnect(true, true);
  delay(500);
  restartDeviceSafely();
}

void handleWebFactoryReset() {
  if (!requireWebLogin()) return;

  if (!appConfig.advancedWebView) {
    webServer.send(403, "text/html", "<html><body><h2>Advanced view required</h2><p><a href='/'>Back</a></p></body></html>");
    return;
  }

  if (otaInProgress) {
    webServer.send(409, "text/html", "<html><body><h2>Factory reset blocked</h2><p>An OTA update is in progress.</p><p><a href='/'>Back</a></p></body></html>");
    return;
  }

  String confirmation = webServer.arg("factory_confirm");
  confirmation.trim();
  confirmation.toUpperCase();
  if (confirmation != "RESET") {
    webServer.send(400, "text/html", "<html><body><h2>Confirmation did not match</h2><p>Type RESET exactly.</p><p><a href='/#factory-reset'>Back</a></p></body></html>");
    return;
  }

  webServer.send(200, "text/html",
    "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head>"
    "<body><h2>Factory reset started</h2><p>All NVS settings are being erased.</p>"
    "<p>The device will restart into the Leah setup hotspot.</p></body></html>");
  delay(500);

  peripheralRestartInProgress = true;
  alarmAudioArmed = false;
  sirenEnabled = false;
  sirenSilenced = true;
  activeAudioAlarmType = ALARM_NONE;
  if (cloudAlertTaskHandle != nullptr) vTaskSuspend(cloudAlertTaskHandle);
  if (alarmAudioTaskHandle != nullptr) vTaskSuspend(alarmAudioTaskHandle);
  forceBuzzerHardwareOff();

  drawMessage("Factory Reset", "Clearing all NVS", "Please wait", "");
  Serial.println("FACTORY RESET: stopping services and erasing default NVS partition.");

  webServer.stop();
  webServerStarted = false;

  // Stop Wi-Fi before erasing NVS so no Wi-Fi task attempts to persist state
  // after the partition has been de-initialized. The final erase is performed
  // after disconnect, so both Arduino Preferences and Wi-Fi credentials go.
  WiFi.disconnect(true, true);
  delay(150);
  WiFi.mode(WIFI_OFF);
  delay(150);

  esp_err_t eraseResult = nvs_flash_erase();
  Serial.print("FACTORY RESET: nvs_flash_erase result = ");
  Serial.println(esp_err_to_name(eraseResult));

  if (eraseResult != ESP_OK) {
    drawMessage("Reset Failed", "NVS erase error", shortenText(String(esp_err_to_name(eraseResult)), 18), "Restarting");
    forceBuzzerHardwareOff();
    Serial.println("FACTORY RESET FAILED: restarting without claiming success.");
    Serial.flush();
    delay(3000);
    restartImmediatelyWithEspIdf("factory reset erase failure");
  }

  // RTC marker survives this software restart without repopulating NVS.
  factoryResetBootMarker = FACTORY_RESET_BOOT_MAGIC;
  drawMessage("Factory Reset", "NVS fully cleared", "Restarting setup", "192.168.4.1");
  Serial.println("FACTORY RESET COMPLETE: NVS erased; restarting to compiled defaults.");
  Serial.flush();
  delay(1200);
  restartImmediatelyWithEspIdf("factory reset complete");
}

void handleWebLogout() {
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "0");
  webServer.requestAuthentication();
}

void startLocalWebServer() {
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/set-view", HTTP_POST, handleWebSetView);
  webServer.on("/save", HTTP_POST, handleWebSave);
  webServer.on("/save-wifi-reconnect", HTTP_POST, handleWebSave);
  webServer.on("/mute", HTTP_POST, handleWebMute);
  webServer.on("/mute-status", HTTP_GET, handleWebMuteStatus);
  webServer.on("/test-nightscout", HTTP_POST, handleWebTestNightscout);
  webServer.on("/check-update", HTTP_POST, handleWebCheckUpdate);
  webServer.on("/install-update", HTTP_POST, handleWebInstallUpdate);
  webServer.on("/restart", HTTP_POST, handleWebRestart);
  webServer.on("/reset-wifi", HTTP_POST, handleWebResetWiFi);
  webServer.on("/factory-reset", HTTP_POST, handleWebFactoryReset);
  webServer.on("/logout", HTTP_GET, handleWebLogout);

  // OLED screen selection from web buttons and direct browser links.
  webServer.on("/screen-main", HTTP_POST, handleWebScreenMain);
  webServer.on("/screen-trend", HTTP_POST, handleWebScreenTrend);
  webServer.on("/screen-diagnostics", HTTP_POST, handleWebScreenDiagnostics);
  webServer.on("/screen-sensor", HTTP_POST, handleWebScreenSensorData);
  webServer.on("/screen-insulin", HTTP_POST, handleWebScreenInsulin);
  webServer.on("/screen-cob-iob-total", HTTP_POST, handleWebScreenCobIobTotal);

  webServer.on("/screen-main", HTTP_GET, handleWebScreenMain);
  webServer.on("/screen-trend", HTTP_GET, handleWebScreenTrend);
  webServer.on("/screen-diagnostics", HTTP_GET, handleWebScreenDiagnostics);
  webServer.on("/screen-sensor", HTTP_GET, handleWebScreenSensorData);
  webServer.on("/screen-insulin", HTTP_GET, handleWebScreenInsulin);
  webServer.on("/screen-cob-iob-total", HTTP_GET, handleWebScreenCobIobTotal);
  webServer.on("/sensor", HTTP_GET, handleWebScreenSensorData);
  webServer.on("/insulin", HTTP_GET, handleWebScreenInsulin);
  webServer.on("/cob-iob-total", HTTP_GET, handleWebScreenCobIobTotal);
  webServer.on("/screen-twilio", HTTP_POST, handleWebScreenTwilio);
  webServer.on("/screen-twilio", HTTP_GET, handleWebScreenTwilio);
  webServer.on("/twilio", HTTP_GET, handleWebScreenTwilio);
  webServer.on("/provider-test-parent1", HTTP_POST, handleWebProviderTestParent1);
  webServer.on("/provider-test-parent2", HTTP_POST, handleWebProviderTestParent2);
  webServer.on("/provider-test-parent3", HTTP_POST, handleWebProviderTestParent3);
  webServer.on("/provider-test-urgent-low", HTTP_POST, handleWebProviderTestUrgentLow);
  webServer.on("/provider-test-battery", HTTP_POST, handleWebProviderTestBattery);
  webServer.on("/provider-test-sensor", HTTP_POST, handleWebProviderTestSensor);
  webServer.on("/api-test-parent1", HTTP_POST, handleWebApiTestParent1);
  webServer.on("/api-test-parent2", HTTP_POST, handleWebApiTestParent2);
  webServer.on("/api-test-urgent-low", HTTP_POST, handleWebApiTestUrgentLow);
  webServer.on("/api-test-nodata", HTTP_POST, handleWebApiTestNoData);
  webServer.on("/api-clear-status", HTTP_POST, handleWebApiClearStatus);
  webServer.on("/twilio-test-parent1", HTTP_POST, handleWebTwilioTestParent1);
  webServer.on("/twilio-test-parent2", HTTP_POST, handleWebTwilioTestParent2);
  webServer.on("/twilio-test-urgent-low", HTTP_POST, handleWebTwilioTestUrgentLow);
  webServer.on("/twilio-test-nodata", HTTP_POST, handleWebTwilioTestNoData);
  webServer.on("/twilio-clear-status", HTTP_POST, handleWebTwilioClearStatus);
  webServer.on("/callmebot-test-parent1", HTTP_POST, handleWebCallMeBotTestParent1);
  webServer.on("/callmebot-test-parent2", HTTP_POST, handleWebCallMeBotTestParent2);
  webServer.on("/callmebot-test-urgent-low", HTTP_POST, handleWebCallMeBotTestUrgentLow);
  webServer.on("/callmebot-test-nodata", HTTP_POST, handleWebCallMeBotTestNoData);
  webServer.on("/callmebot-clear-status", HTTP_POST, handleWebCallMeBotClearStatus);
  webServer.on("/textmebot-test-parent1", HTTP_POST, handleWebTextMeBotTestParent1);
  webServer.on("/textmebot-test-parent2", HTTP_POST, handleWebTextMeBotTestParent2);
  webServer.on("/textmebot-test-parent3", HTTP_POST, handleWebTextMeBotTestParent3);
  webServer.on("/textmebot-test-urgent-low", HTTP_POST, handleWebTextMeBotTestUrgentLow);
  webServer.on("/textmebot-test-nodata", HTTP_POST, handleWebTextMeBotTestNoData);
  webServer.on("/textmebot-clear-status", HTTP_POST, handleWebTextMeBotClearStatus);

  // Short aliases for support convenience.
  webServer.on("/main", HTTP_GET, handleWebScreenMain);
  webServer.on("/trend", HTTP_GET, handleWebScreenTrend);
  webServer.on("/diagnostics", HTTP_GET, handleWebScreenDiagnostics);
  webServer.on("/sensor", HTTP_GET, handleWebScreenSensorData);

  webServer.begin();
  webServerStarted = true;

  Serial.println("Local webserver started.");
  Serial.print("Open: http://");
  Serial.println(WiFi.localIP());
  Serial.print("Alias: http://");
  Serial.println(getLocalMdnsHost());
}




String wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "Idle";
    case WL_NO_SSID_AVAIL: return "SSID not found";
    case WL_SCAN_COMPLETED: return "Scan done";
    case WL_CONNECTED: return "Connected";
    case WL_CONNECT_FAILED: return "Connect failed";
    case WL_CONNECTION_LOST: return "Connection lost";
    case WL_DISCONNECTED: return "Disconnected";
    default: return "Status " + String((int)status);
  }
}

bool scanForStoredSsid(const String &targetSsid, int &bestRssi, String &securityLabel) {
  bestRssi = -999;
  securityLabel = "Unknown";

  String cleanTarget = targetSsid;
  cleanTarget.trim();
  if (cleanTarget.length() == 0) return false;

  // Temporarily disable reconnect during the scan. Otherwise the station can
  // keep trying the previous network and the device can appear stuck on the
  // OLED "Connecting..." screen.
  WiFi.setAutoReconnect(false);
  int n = WiFi.scanNetworks(false, true);
  WiFi.setAutoReconnect(true);

  if (n <= 0) {
    WiFi.scanDelete();
    return false;
  }

  bool found = false;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (ssid == cleanTarget) {
      int rssi = WiFi.RSSI(i);
      if (!found || rssi > bestRssi) {
        bestRssi = rssi;
        securityLabel = wifiSecurityLabel((int)WiFi.encryptionType(i));
      }
      found = true;
    }
  }

  WiFi.scanDelete();
  return found;
}

bool connectToStoredWiFiSlot(const String &ssid, const String &password, uint8_t slot, bool showDisplay) {
  const unsigned long WIFI_PROFILE_CONNECT_TIMEOUT_MS = 15000UL;

  String cleanSsid = ssid;
  cleanSsid.trim();
  if (cleanSsid.length() == 0) return false;

  Serial.print("Trying NVS Wi-Fi slot ");
  Serial.print(slot);
  Serial.print(": ");
  Serial.println(cleanSsid);

  if (showDisplay) {
    drawMessage("WiFi Profile " + String(slot), shortenText(cleanSsid, 18), "Scanning...", "");
  }

  int bestRssi = -999;
  String securityLabel = "Unknown";
  bool ssidSeen = scanForStoredSsid(cleanSsid, bestRssi, securityLabel);

  if (!ssidSeen) {
    Serial.print("NVS Wi-Fi slot ");
    Serial.print(slot);
    Serial.println(" skipped: SSID not seen in scan.");
    if (showDisplay) {
      drawMessage("WiFi Profile " + String(slot), shortenText(cleanSsid, 18), "SSID not found", "Trying next...");
      delay(900);
    }
    return false;
  }

  Serial.print("NVS Wi-Fi slot scan RSSI: ");
  Serial.print(bestRssi);
  Serial.print(" dBm, security: ");
  Serial.println(securityLabel);

  if (showDisplay) {
    drawMessage("WiFi Profile " + String(slot), shortenText(cleanSsid, 18), String(bestRssi) + "dBm " + securityLabel, "Connecting...");
  }

  WiFi.disconnect(true, false);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);

  unsigned long beginStart = millis();
  WiFi.begin(cleanSsid.c_str(), password.c_str());
  Serial.print("WiFi.begin duration ms: ");
  Serial.println(millis() - beginStart);

  unsigned long start = millis();
  unsigned long lastDisplayUpdate = 0;
  wl_status_t lastStatus = WiFi.status();

  while (millis() - start < WIFI_PROFILE_CONNECT_TIMEOUT_MS) {
    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
      WiFi.setAutoReconnect(true);
      Serial.print("Connected using NVS Wi-Fi slot ");
      Serial.println(slot);
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return true;
    }

    if (status != lastStatus) {
      Serial.print("Wi-Fi slot ");
      Serial.print(slot);
      Serial.print(" status: ");
      Serial.println(wifiStatusText(status));
      lastStatus = status;
    }

    if (showDisplay && millis() - lastDisplayUpdate > 2500UL) {
      unsigned long elapsed = millis() - start;
      int remaining = (int)((WIFI_PROFILE_CONNECT_TIMEOUT_MS - elapsed + 999UL) / 1000UL);
      if (remaining < 0) remaining = 0;
      drawMessage("WiFi Profile " + String(slot), shortenText(cleanSsid, 18), wifiStatusText(status), "Timeout " + String(remaining) + "s");
      lastDisplayUpdate = millis();
    }

    // Wrong passwords often become WL_CONNECT_FAILED. Do not sit on this
    // profile for the full boot path if the driver already knows it failed.
    if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
      delay(800);
      break;
    }

    delay(200);
    yield();
  }

  wl_status_t finalStatus = WiFi.status();
  Serial.print("NVS Wi-Fi slot failed: ");
  Serial.print(slot);
  Serial.print(" final status: ");
  Serial.println(wifiStatusText(finalStatus));

  if (showDisplay) {
    drawMessage("WiFi Profile " + String(slot), shortenText(cleanSsid, 18), "Failed: " + wifiStatusText(finalStatus), "Trying next...");
    delay(1000);
  }

  WiFi.disconnect(true, false);
  delay(400);
  return false;
}

bool connectToStoredWiFiNetworks(bool showDisplay) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);

  if (connectToStoredWiFiSlot(appConfig.wifiSsid1, appConfig.wifiPass1, 1, showDisplay)) return true;
  if (connectToStoredWiFiSlot(appConfig.wifiSsid2, appConfig.wifiPass2, 2, showDisplay)) return true;
  if (connectToStoredWiFiSlot(appConfig.wifiSsid3, appConfig.wifiPass3, 3, showDisplay)) return true;

  WiFi.setAutoReconnect(true);
  if (showDisplay) {
    drawMessage("WiFi Profiles", "No profile joined", "Opening setup", getSetupApSsid());
    delay(1200);
  }
  return false;
}

void ensureAdminPasswordConfigured() {
  if (!hasAdminPassword()) {
    appConfig.adminPassword = getDefaultAdminPassword();
    Serial.println("Admin password not set. Using device-specific fallback password.");
    Serial.print("Fallback admin username: ");
    Serial.println(appConfig.adminUsername);
    Serial.print("Fallback admin password: ");
    Serial.println(appConfig.adminPassword);
    saveConfig();
  }
}

void startLocalMdnsAlias() {
  if (WiFi.status() != WL_CONNECTED) return;

  String mdnsName = getLocalMdnsName();
  MDNS.end();
  if (MDNS.begin(mdnsName.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS alias active: http://");
    Serial.println(getLocalMdnsHost());
  } else {
    Serial.println("mDNS alias failed to start.");
  }
}


// ==================================================
// 16. WIFI MANAGER - STARTUP ONLY
// ==================================================

void connectWiFiWithManager() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  // Build 75 migration: recover a WiFiManager/Espressif station profile left
  // by older builds when the dedicated Leah NVS slots are still empty.
  importLegacyEspressifWiFiProfileIfNeeded();

  bool setupRequired = setupIsIncomplete();

  String setupSsid = getSetupApSsid();
  String setupPass = getSetupApPassword();

  if (setupRequired) {
    drawMessage("Setup Hotspot", setupSsid, "Password:", setupPass);
  } else {
    drawMessage("WiFi Setup", "Connecting...", "Portal if needed", setupSsid);
  }

  // First try the three Leah 2R Displays NVS Wi-Fi profiles. If they fail, fall back to WiFiManager.
  if (connectToStoredWiFiNetworks(true)) {
    ensureAdminPasswordConfigured();
    Serial.println("WiFi connected from Leah 2R Displays NVS profile.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    drawMessage("WiFi Connected", "IP:" + WiFi.localIP().toString(), "Alias:", getLocalMdnsHost());
    delay(2000);
    return;
  }

  WiFiManager wm;

  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(300);

  String devValue = appConfig.deviceName;
  String patientValue = appConfig.patientName;
  String locValue = appConfig.deviceLocation;
  String aliasValue = appConfig.localAliasName.length() > 0 ? getLocalMdnsHost() : "";
  String hostValue = appConfig.nightscoutHost;
  String tokenValue = "";
  String adminValue = "";

  WiFiManagerParameter p_device("device_name", "Device name", devValue.c_str(), 32);
  WiFiManagerParameter p_patient("patient_name", "Patient / child name", patientValue.c_str(), 32);
  WiFiManagerParameter p_location("device_location", "Room / location", locValue.c_str(), 32);
  WiFiManagerParameter p_alias("local_alias", "Local alias, e.g. Mat cgm 1.local", aliasValue.c_str(), 70);
  WiFiManagerParameter p_host("ns_host", "Nightscout host", hostValue.c_str(), 96);
  WiFiManagerParameter p_token("ns_token", "Nightscout token", tokenValue.c_str(), 128);
  WiFiManagerParameter p_admin("admin_password", "Admin password min 8 chars", adminValue.c_str(), 32);

  wm.addParameter(&p_device);
  wm.addParameter(&p_patient);
  wm.addParameter(&p_location);
  wm.addParameter(&p_alias);
  wm.addParameter(&p_host);
  wm.addParameter(&p_token);
  wm.addParameter(&p_admin);

  bool connected = false;

  // Stored profiles have failed or first-time setup is incomplete. Display the
  // exact hotspot credentials immediately before WiFiManager opens the portal.
  drawMessage("Setup Hotspot", setupSsid, "Password:", setupPass);

  if (setupRequired) {
    connected = wm.startConfigPortal(setupSsid.c_str(), setupPass.c_str());
  } else {
    connected = wm.autoConnect(setupSsid.c_str(), setupPass.c_str());
  }

  if (!connected) {
    drawMessage("WiFi Failed", "Restarting ESP32", "", "");
    delay(2000);
    restartDeviceSafely();
  }

  // WiFiManager connected successfully. Older builds stopped here and never
  // copied the chosen SSID/password into appConfig.wifiSsid1/wifiPass1, while
  // WiFi.persistent(false) prevented the driver profile from being dependable
  // across restart. Capture the live station configuration now.
  String portalConnectedSsid = WiFi.SSID();
  String portalConnectedPassword = WiFi.psk();
  rememberConnectedWiFiProfile(portalConnectedSsid, portalConnectedPassword);

  // Commit the Wi-Fi profile before validating optional/custom setup fields.
  // This prevents a missing alias or another setup-field restart from losing
  // the network that was already joined successfully.
  saveConfig();
  bool portalWiFiSaved = verifyWiFiProfilesInNvs();
  if (!portalWiFiSaved) {
    drawMessage("WiFi Save Error", "NVS verify failed", "Profile not stored", "Retry setup");
    Serial.println("ERROR: setup-portal Wi-Fi profile failed immediate NVS verification.");
    delay(2500);
  }

  String newDevice = String(p_device.getValue());
  String newPatient = String(p_patient.getValue());
  String newLocation = String(p_location.getValue());
  String newAlias = sanitizeLocalAliasName(String(p_alias.getValue()));
  String newHost = String(p_host.getValue());
  String newToken = String(p_token.getValue());
  String newAdmin = String(p_admin.getValue());

  newDevice.trim();
  newPatient.trim();
  newLocation.trim();
  newHost.trim();
  newToken.trim();
  newAdmin.trim();

  // A user-defined local alias is required on first-time setup.
  if (setupRequired && newAlias.length() == 0) {
    drawMessage("Alias Required", "Example: Mat cgm 1", "Address: .local", "Restarting setup");
    Serial.println("Initial setup incomplete: local alias was not entered.");
    delay(2500);
    WiFi.disconnect(true, false);
    restartDeviceSafely();
  }

  if (newDevice.length() > 0 && newDevice != appConfig.deviceName) {
    appConfig.deviceName = newDevice;
  }

  if (newPatient.length() > 0 && newPatient != appConfig.patientName) {
    appConfig.patientName = newPatient;
  }

  if (newLocation.length() > 0 && newLocation != appConfig.deviceLocation) {
    appConfig.deviceLocation = newLocation;
  }

  if (newAlias.length() > 0 && newAlias != appConfig.localAliasName) {
    appConfig.localAliasName = newAlias;
  }

  if (appConfig.localAliasName.length() == 0) {
    appConfig.localAliasName = String("leah-2r-") + getMacSuffix(4);
  }

  if (newHost.length() > 0) {
    newHost = normalizeNightscoutHost(newHost);
    if (newHost != appConfig.nightscoutHost) {
      appConfig.nightscoutHost = newHost;
    }
  }

  if (newToken.length() > 0) {
    appConfig.nightscoutToken = newToken;
  }

  if (newAdmin.length() > 0) {
    if (newAdmin.length() >= 8) {
      appConfig.adminPassword = newAdmin;
    } else {
      Serial.println("Admin password ignored: too short.");
    }
  }

  if (!hasAdminPassword()) {
    appConfig.adminPassword = getDefaultAdminPassword();
    Serial.println("Admin password not set. Using device-specific fallback password.");
    Serial.print("Fallback admin username: ");
    Serial.println(appConfig.adminUsername);
    Serial.print("Fallback admin password: ");
    Serial.println(appConfig.adminPassword);
  }

  // Always save after a successful setup-portal connection. This guarantees
  // that a same-name network/password refresh is committed even when no custom
  // setup field changed.
  saveConfig();
  bool wifiProfilesVerified = verifyWiFiProfilesInNvs();
  if (!wifiProfilesVerified) {
    drawMessage("WiFi Save Error", "NVS verify failed", "Please retry setup", "192.168.4.1");
    Serial.println("ERROR: connected Wi-Fi profile could not be verified after setup.");
    delay(2500);
  }

  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  Serial.println("WiFi connected.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Local web user: ");
  Serial.println(appConfig.adminUsername);
  Serial.print("Setup AP SSID if reset: ");
  Serial.println(setupSsid);
  Serial.print("Setup AP password if reset: ");
  Serial.println(setupPass);

  drawMessage("WiFi Connected", "IP:" + WiFi.localIP().toString(), "Alias:", getLocalMdnsHost());
  delay(2500);
}


// ==================================================
// 17. TIME SYNC
// ==================================================

void setupTime() {
  drawMessage("Syncing time", "Using NTP...", "", "");

  // Namibia/South Africa local time UTC+2. Used for OLED night dimming schedule.
  configTime(2 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);
  int attempts = 0;

  while (now < 100000 && attempts < 30) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }

  Serial.println();

  if (now > 100000) {
    Serial.println("Time synced.");
    drawMessage("Time Synced", "NTP OK", "", "");
  } else {
    Serial.println("Time failed.");
    drawMessage("Time Failed", "Age may be wrong", "", "");
  }

  delay(1000);
}


// ==================================================
// 18. SAFE HTTPS GET - HARD TIMEOUT VERSION
// ==================================================
// This avoids https.getString(), which may block after HTTP 200.

bool httpsGetWithLimit(String url,
                       String &payload,
                       size_t maxPayloadBytes,
                       unsigned long totalReadTimeout,
                       unsigned long noDataTimeout) {
  payload = "";

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(8);

  HTTPClient https;

  Serial.println();
  Serial.println("GET request started");
  Serial.println("URL hidden for safety");

  https.setTimeout(5000);
  https.setReuse(false);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.useHTTP10(true);

  if (!https.begin(client, url)) {
    Serial.println("HTTPS begin failed.");
    payload = "";
    return false;
  }

  https.addHeader("Connection", "close");
  https.addHeader("Cache-Control", "no-cache");

  int httpCode = https.GET();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode < 0) {
    Serial.print("HTTP Error Text: ");
    Serial.println(https.errorToString(httpCode));
    https.end();
    client.stop();
    payload = "";
    return false;
  }

  if (httpCode != 200) {
    Serial.println("HTTP not 200.");
    https.end();
    client.stop();
    payload = "";
    return false;
  }

  WiFiClient *stream = https.getStreamPtr();

  unsigned long startTime = millis();
  unsigned long lastDataTime = millis();
  bool payloadTruncated = false;

  payload.reserve(min((size_t)maxPayloadBytes, (size_t)20000));

  while (millis() - startTime < totalReadTimeout) {

    while (stream->available()) {
      char c = stream->read();

      if (payload.length() < maxPayloadBytes) {
        payload += c;
      }
      else {
        payloadTruncated = true;
      }

      lastDataTime = millis();
    }

    if (payload.length() > 0 && millis() - lastDataTime > noDataTimeout) {
      break;
    }

    if (!stream->connected() && !stream->available()) {
      break;
    }

    delay(1);
    yield();
  }

  https.end();
  client.stop();

  Serial.print("Payload length: ");
  Serial.println(payload.length());

  if (payloadTruncated) {
    Serial.print("Payload truncated at limit: ");
    Serial.println(maxPayloadBytes);
    return false;
  }

  if (payload.length() == 0) {
    Serial.println("Payload empty after HTTP 200.");
    return false;
  }

  Serial.println("GET request finished safely.");
  return true;
}

bool httpsGet(String url, String &payload) {
  // Default reader for normal small requests such as latest glucose,
  // battery, status, and update manifest.
  return httpsGetWithLimit(url, payload, 6000, 3000, 600);
}




// ==================================================
// 18B. FIRMWARE VERSION / OTA MANIFEST CHECK
// ==================================================
// v0.8.0 only checks the update manifest and tells the user if a
// newer version is available.
// The actual user-confirmed OTA download/install will be added in v0.9.x.

int getVersionPart(String version, int index) {
  int currentIndex = 0;
  int start = 0;

  version.trim();

  for (int i = 0; i <= version.length(); i++) {
    if (i == version.length() || version.charAt(i) == '.') {
      if (currentIndex == index) {
        String part = version.substring(start, i);
        return part.toInt();
      }

      currentIndex++;
      start = i + 1;
    }
  }

  return 0;
}

int compareSemanticVersions(String remoteVersion, String localVersion) {
  for (int i = 0; i < 3; i++) {
    int remotePart = getVersionPart(remoteVersion, i);
    int localPart = getVersionPart(localVersion, i);

    if (remotePart > localPart) return 1;
    if (remotePart < localPart) return -1;
  }

  return 0;
}

bool isRemoteFirmwareNewer(String remoteVersion, int remoteBuild) {
  int versionCompare = compareSemanticVersions(remoteVersion, String(FIRMWARE_VERSION));

  if (versionCompare > 0) return true;
  if (versionCompare < 0) return false;

  // Same semantic version: use build number as tie-breaker.
  return remoteBuild > BUILD_NUMBER;
}

void printFirmwareInfo() {
  Serial.println();
  Serial.println("======================================");
  Serial.println("FIRMWARE INFO");
  Serial.print("Product: ");
  Serial.println(PRODUCT_NAME);
  Serial.print("Model: ");
  Serial.println(PRODUCT_MODEL);
  Serial.print("Hardware: ");
  Serial.println(HARDWARE_REVISION);
  Serial.print("Firmware: ");
  Serial.println(FIRMWARE_VERSION);
  Serial.print("Build: ");
  Serial.println(BUILD_NUMBER);
  Serial.print("Build date: ");
  Serial.println(BUILD_DATE);
  Serial.print("Update channel policy: ");
  Serial.println(getUpdateChannelPolicyText());
  Serial.println("======================================");
  Serial.println();
}

bool checkFirmwareUpdateManifest(bool showResultOnDisplay) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Firmware check skipped: WiFi not connected.");
    return false;
  }

  String payload;

  String manifestUrl = getUpdateManifestUrl();

  Serial.println("Checking firmware update manifest...");
  Serial.print("Update channel: ");
  Serial.println(getUpdateChannelLabel());
  Serial.print("Channel policy: ");
  Serial.println(getUpdateChannelPolicyText());
  Serial.print("Manifest URL: ");
  Serial.println(manifestUrl);

  if (!httpsGet(manifestUrl, payload)) {
    Serial.println("Firmware manifest read failed.");

    if (showResultOnDisplay) {
      drawMessage("FW Check Failed", "Could not read", "update manifest", "");
      delay(1500);
    }

    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("Firmware manifest JSON error: ");
    Serial.println(error.c_str());

    if (showResultOnDisplay) {
      drawMessage("FW JSON Error", "Manifest invalid", "", "");
      delay(1500);
    }

    return false;
  }

  String product = doc["product"] | "";
  String model = doc["model"] | "";
  String hw = doc["hardware_rev"] | "";
  String manifestChannel = normalizeUpdateChannel(doc["channel"] | appConfig.updateChannel);

  if (product != "leah-cgm-display" || model != PRODUCT_MODEL || hw != HARDWARE_REVISION) {
    Serial.println("Firmware manifest does not match this product/model/hardware.");

    if (showResultOnDisplay) {
      drawMessage("FW Not Match", "Wrong model/hw", "", "");
      delay(1500);
    }

    return false;
  }

  if (manifestChannel != normalizeUpdateChannel(appConfig.updateChannel)) {
    Serial.println("Firmware manifest channel does not match selected device channel.");

    if (showResultOnDisplay) {
      drawMessage("FW Channel", "Wrong channel", manifestChannel, "");
      delay(1500);
    }

    return false;
  }

  String remoteVersion = doc["latest_version"] | "";
  int remoteBuild = doc["build"] | 0;
  String notes = doc["release_notes"] | "";
  String firmwareUrl = doc["firmware_url"] | "";
  size_t firmwareSize = doc["firmware_size"] | 0;
  String firmwareSha256 = doc["sha256"] | "";
  bool critical = doc["critical"] | false;
  bool mandatory = doc["mandatory"] | false;

  if (remoteVersion.length() == 0) {
    Serial.println("Firmware manifest missing latest_version.");
    return false;
  }

  availableFirmwareVersion = remoteVersion;
  availableFirmwareBuild = remoteBuild;
  availableFirmwareNotes = notes;
  availableFirmwareUrl = firmwareUrl;
  availableFirmwareSize = firmwareSize;
  availableFirmwareSha256 = firmwareSha256;
  availableFirmwareSha256.toUpperCase();
  availableFirmwareCritical = critical;
  availableFirmwareMandatory = mandatory;
  availableFirmwareChannel = manifestChannel;

  updateAvailable = isRemoteFirmwareNewer(remoteVersion, remoteBuild);
  lastFirmwareCheck = millis();

  Serial.println("Firmware manifest parsed:");
  Serial.print("Installed FW: ");
  Serial.print(FIRMWARE_VERSION);
  Serial.print(" build ");
  Serial.println(BUILD_NUMBER);

  Serial.print("Available FW: ");
  Serial.print(availableFirmwareVersion);
  Serial.print(" build ");
  Serial.println(availableFirmwareBuild);
  Serial.print("Available channel: ");
  Serial.println(availableFirmwareChannel);

  Serial.print("Firmware URL: ");
  Serial.println(availableFirmwareUrl);
  Serial.print("Firmware size: ");
  Serial.println(availableFirmwareSize);
  Serial.print("Firmware SHA256: ");
  Serial.println(availableFirmwareSha256);

  Serial.print("Update available: ");
  Serial.println(updateAvailable ? "YES" : "NO");

  lastOtaStatus = updateAvailable ? ("Update available (" + getUpdateChannelLabel() + ")") : ("Firmware current (" + getUpdateChannelLabel() + ")");

  if (updateAvailable) {
    drawMessage("Update Found", 
                "New FW: " + availableFirmwareVersion,
                getUpdateChannelLabel(),
                "Open web page");
    delay(3000);
  }
  else if (showResultOnDisplay) {
    drawMessage("Firmware OK",
                "FW: " + String(FIRMWARE_VERSION),
                "No update found",
                "");
    delay(1500);
  }

  return updateAvailable;
}


// ==================================================
// 19. READ LATEST NIGHTSCOUT GLUCOSE
// ==================================================

bool readNightscoutEntries() {
  if (WiFi.status() != WL_CONNECTED) {
    lastStatus = "WiFi lost";
    return false;
  }

  if (!hasNightscoutConfig()) {
    lastStatus = "Nightscout setup missing";
    return false;
  }

  String url = buildNightscoutUrl("/api/v1/entries.json?count=4");

  String payload;
  bool gotData = false;

  for (int attempt = 1; attempt <= 1; attempt++) {
    Serial.print("Entries attempt: ");
    Serial.println(attempt);

    if (httpsGet(url, payload)) {
      gotData = true;
      break;
    }
  }

  if (!gotData) {
    lastStatus = "Entries HTTP fail";
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("Entries JSON parse failed: ");
    Serial.println(error.c_str());
    lastStatus = "Entries JSON error";
    return false;
  }

  if (!doc.is<JsonArray>() || doc.size() == 0) {
    lastStatus = "No entries";
    return false;
  }

  int found = 0;

  for (JsonObject entry : doc.as<JsonArray>()) {
    int sgv = entry["sgv"] | 0;

    if (sgv > 0) {
      if (found == 0) {
        glucoseMgdl = sgv;
        directionText = entry["direction"] | "";
        latestEntryDateMs = getEntryTimeMs(entry);
        lastEntryDevice = entry["device"] | "";
        lastEntryType = entry["type"] | "";
        found++;
      }
      else if (found == 1) {
        previousMgdl = sgv;
        found++;
        break;
      }
    }
  }

  if (found == 0) {
    lastStatus = "No SGV";
    return false;
  }

  if (found == 1) {
    previousMgdl = 0;
  }

  glucoseMmol = glucoseMgdl / 18.0;

  if (previousMgdl > 0) {
    deltaMmol = (glucoseMgdl - previousMgdl) / 18.0;
  } else {
    deltaMmol = 0.0;
  }

  updateDataAge();

  lastSuccessfulNightscoutRead = millis();
  updateDetectedCgmSource();

  Serial.println("Parsed latest glucose:");
  Serial.print("mmol/L: ");
  Serial.println(glucoseMmol, 1);
  Serial.print("Delta: ");
  Serial.println(deltaMmol, 1);
  Serial.print("Direction: ");
  Serial.println(directionText);
  Serial.print("Age min: ");
  Serial.println(ageMinutes);
  Serial.print("Alarm level: ");
  Serial.println(getAlarmDisplayText());

  lastStatus = "Entries OK";
  return true;
}




// ==================================================
// 19B. READ LATEST NIGHTSCOUT BOLUS / INSULIN
// ==================================================

long long parseIsoUtcToMs(String isoText) {
  isoText.trim();
  if (isoText.length() < 19) return 0;

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  int parsed = sscanf(isoText.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
  if (parsed != 6) {
    parsed = sscanf(isoText.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
  }

  if (parsed != 6) return 0;
  if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return 0;

  long days = daysFromCivil(year, month, day);
  long long epochSeconds = days * 86400LL + (long long)hour * 3600LL + (long long)minute * 60LL + second;
  return epochSeconds * 1000LL;
}

float getFloatField(JsonObject obj, const char* key) {
  JsonVariant v = obj[key];
  if (v.isNull()) return 0.0;
  if (v.is<float>() || v.is<double>() || v.is<int>() || v.is<long>() || v.is<unsigned int>() || v.is<unsigned long>()) {
    return v.as<float>();
  }
  if (v.is<const char*>()) {
    String text = v.as<const char*>();
    text.trim();
    return text.toFloat();
  }
  return 0.0;
}

float getTreatmentBolusUnits(JsonObject treatment) {
  float dose = 0.0;

  const char* keys[] = {
    "insulin", "enteredinsulin", "enteredInsulin", "entered_insulin",
    "bolus", "dose"
  };

  for (int i = 0; i < 6; i++) {
    dose = getFloatField(treatment, keys[i]);
    if (dose > 0.0 && dose < 200.0) return dose;
  }

  return 0.0;
}

float getTreatmentCarbsGrams(JsonObject treatment) {
  float carbs = 0.0;

  const char* keys[] = {
    "carbs", "enteredcarbs", "enteredCarbs", "entered_carbs",
    "carbInput", "carbinput"
  };

  for (int i = 0; i < 6; i++) {
    carbs = getFloatField(treatment, keys[i]);
    if (carbs > 0.0 && carbs < 1000.0) return carbs;
  }

  return 0.0;
}

void addDailyAdministrationEvent(long long times[], float amounts[], uint8_t &eventCount,
                                 long long eventTime, float amount) {
  if (eventTime <= 0 || amount <= 0.0 || eventCount >= MAX_DAILY_ADMIN_EVENTS) return;
  times[eventCount] = eventTime;
  amounts[eventCount] = amount;
  eventCount++;
}

void sortDailyAdministrationEvents(long long times[], float amounts[], uint8_t eventCount) {
  for (uint8_t i = 0; i < eventCount; i++) {
    for (uint8_t j = i + 1; j < eventCount; j++) {
      if (times[j] < times[i]) {
        long long tempTime = times[i];
        times[i] = times[j];
        times[j] = tempTime;
        float tempAmount = amounts[i];
        amounts[i] = amounts[j];
        amounts[j] = tempAmount;
      }
    }
  }
}

String formatDailyAdministrationEvents(long long times[], float amounts[], uint8_t eventCount,
                                       const String &unit, uint8_t decimals) {
  if (eventCount == 0) return "None recorded today";

  sortDailyAdministrationEvents(times, amounts, eventCount);
  String result = "";

  for (uint8_t i = 0; i < eventCount; i++) {
    String item = formatTimestampTimeOnly(times[i]) + " " + String(amounts[i], static_cast<unsigned int>(decimals)) + unit;
    if (result.length() > 0) item = " | " + item;
    if (result.length() + item.length() > 240) {
      result += " | ...";
      break;
    }
    result += item;
  }

  return result;
}

void addDailyTreatmentRow(bool useTodayRows,
                          long long eventTime, float insulinUnits, float carbsGrams) {
  if (eventTime <= 0) return;
  if (insulinUnits <= 0.0 && carbsGrams <= 0.0) return;

  DailyTreatmentRow *rows = useTodayRows ? todayTreatmentRows : yesterdayTreatmentRows;
  uint8_t *countPtr = useTodayRows ? &todayTreatmentRowCount : &yesterdayTreatmentRowCount;
  uint8_t &historyCount = *countPtr;

  DailyTreatmentRow newRow;
  newRow.timeMs = eventTime;
  newRow.insulinUnits = insulinUnits > 0.0 ? insulinUnits : 0.0;
  newRow.carbsGrams = carbsGrams > 0.0 ? carbsGrams : 0.0;

  if (historyCount < MAX_DAILY_HISTORY_ROWS) {
    rows[historyCount] = newRow;
    historyCount++;
    return;
  }

  // Keep the newest bounded set even if Nightscout returns rows oldest-first.
  uint8_t oldestIndex = 0;
  for (uint8_t i = 1; i < historyCount; i++) {
    if (rows[i].timeMs < rows[oldestIndex].timeMs) oldestIndex = i;
  }
  if (eventTime > rows[oldestIndex].timeMs) rows[oldestIndex] = newRow;
}

void sortDailyTreatmentRowsNewestFirst(bool useTodayRows) {
  DailyTreatmentRow *rows = useTodayRows ? todayTreatmentRows : yesterdayTreatmentRows;
  uint8_t historyCount = useTodayRows ? todayTreatmentRowCount : yesterdayTreatmentRowCount;

  for (uint8_t i = 0; i < historyCount; i++) {
    for (uint8_t j = i + 1; j < historyCount; j++) {
      if (rows[j].timeMs > rows[i].timeMs) {
        DailyTreatmentRow temp = rows[i];
        rows[i] = rows[j];
        rows[j] = temp;
      }
    }
  }
}

String formatHistoryDay(long long timestampMs) {
  if (timestampMs <= 0) return "--";
  time_t t = (time_t)(timestampMs / 1000LL);
  if (t < 100000) return "--";

  struct tm timeInfo;
  localtime_r(&t, &timeInfo);
  const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%s %02d/%02d",
           dayNames[timeInfo.tm_wday], timeInfo.tm_mday, timeInfo.tm_mon + 1);
  return String(buffer);
}

String formatHistoryTime(long long timestampMs) {
  if (timestampMs <= 0) return "--";
  time_t t = (time_t)(timestampMs / 1000LL);
  if (t < 100000) return "--";

  struct tm timeInfo;
  localtime_r(&t, &timeInfo);
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%02dH%02d", timeInfo.tm_hour, timeInfo.tm_min);
  return String(buffer);
}

String buildDailyTreatmentTableHtml(const String &title, bool useTodayRows,
                                    float insulinTotal, float carbTotal) {
  DailyTreatmentRow *rows = useTodayRows ? todayTreatmentRows : yesterdayTreatmentRows;
  uint8_t historyCount = useTodayRows ? todayTreatmentRowCount : yesterdayTreatmentRowCount;

  String html = "<div class='historypanel'><h3>" + htmlEscape(title) + "</h3>";
  html += "<div style='overflow-x:auto'><table class='datatable historytable'>";
  html += "<thead><tr><th>Day</th><th>Time</th><th>Insulin</th><th>Carbs</th></tr></thead><tbody>";

  if (historyCount == 0) {
    html += "<tr><td colspan='4'>No insulin or carbohydrate treatments recorded.</td></tr>";
  } else {
    for (uint8_t i = 0; i < historyCount; i++) {
      html += "<tr>";
      html += "<td>" + htmlEscape(formatHistoryDay(rows[i].timeMs)) + "</td>";
      html += "<td>" + htmlEscape(formatHistoryTime(rows[i].timeMs)) + "</td>";
      String insulinText = rows[i].insulinUnits > 0.0 ? String(rows[i].insulinUnits, 1) + "u" : "--";
      String carbText = rows[i].carbsGrams > 0.0 ? String(rows[i].carbsGrams, 0) + "g" : "--";
      html += "<td>" + insulinText + "</td>";
      html += "<td>" + carbText + "</td>";
      html += "</tr>";
    }
  }

  html += "</tbody><tfoot><tr><td><b>Total</b></td><td></td>";
  html += "<td>" + String(insulinTotal, 1) + "u</td>";
  html += "<td>" + String(carbTotal, 0) + "g</td></tr></tfoot>";
  html += "</table></div></div>";
  return html;
}

long long getTreatmentTimeMs(JsonObject treatment) {
  long long t = 0;

  if (!treatment["date"].isNull()) t = treatment["date"].as<long long>();
  if (t <= 0 && !treatment["mills"].isNull()) t = treatment["mills"].as<long long>();
  if (t <= 0 && !treatment["timestamp"].isNull()) t = treatment["timestamp"].as<long long>();

  if (t > 0 && t < 10000000000LL) t *= 1000LL;
  if (t > 0) return t;

  String created = "";
  if (treatment["created_at"].is<const char*>()) created = treatment["created_at"].as<String>();
  else if (treatment["createdAt"].is<const char*>()) created = treatment["createdAt"].as<String>();
  else if (treatment["time"].is<const char*>()) created = treatment["time"].as<String>();

  t = parseIsoUtcToMs(created);
  if (t > 0) return t;

  return (long long)time(nullptr) * 1000LL;
}

String getTreatmentTypeText(JsonObject treatment) {
  String typeText = "Bolus";

  if (treatment["eventType"].is<const char*>()) typeText = treatment["eventType"].as<String>();
  else if (treatment["eventtype"].is<const char*>()) typeText = treatment["eventtype"].as<String>();
  else if (treatment["type"].is<const char*>()) typeText = treatment["type"].as<String>();
  else if (treatment["notes"].is<const char*>()) typeText = treatment["notes"].as<String>();

  typeText.trim();
  if (typeText.length() == 0) typeText = "Bolus";
  return shortenText(typeText, 18);
}

void updateInsulinRemainingEstimate() {
  // Prefer Nightscout's own API v2 IOB calculation when it has been read recently.
  // This matches the Nightscout pill more closely than the simple local linear fallback.
  if (nightscoutIobValid && (millis() - lastIobCobReadMs) <= (insulinUpdateInterval * 3UL)) {
    insulinRemainingUnits = nightscoutIobUnits;
    return;
  }

  if (lastBolusUnits <= 0.0 || lastBolusTimeMs <= 0) {
    insulinRemainingUnits = 0.0;
    return;
  }

  time_t nowSeconds = time(nullptr);
  long long nowMs = (long long)nowSeconds * 1000LL;

  float actionHours = appConfig.insulinActionHours;
  if (actionHours < 1.0) actionHours = 1.0;
  if (actionHours > 8.0) actionHours = 8.0;

  float elapsedHours = (float)(nowMs - lastBolusTimeMs) / 3600000.0;
  if (elapsedHours < 0.0) elapsedHours = 0.0;

  float fractionRemaining = 1.0 - (elapsedHours / actionHours);
  if (fractionRemaining < 0.0) fractionRemaining = 0.0;
  if (fractionRemaining > 1.0) fractionRemaining = 1.0;

  insulinRemainingUnits = lastBolusUnits * fractionRemaining;
}

String getBolusAgeText() {
  if (lastBolusTimeMs <= 0) return "No dose";

  time_t nowSeconds = time(nullptr);
  long long nowMs = (long long)nowSeconds * 1000LL;
  long long ageMs = nowMs - lastBolusTimeMs;
  if (ageMs < 0) ageMs = 0;

  long minutes = (long)(ageMs / 60000LL);
  if (minutes < 60) return String(minutes) + " min ago";

  long hours = minutes / 60;
  long remMin = minutes % 60;
  return String(hours) + "h " + String(remMin) + "m ago";
}

bool readNightscoutIobCobInfo() {
  if (WiFi.status() != WL_CONNECTED) {
    iobCobStatus = "WiFi lost";
    return false;
  }

  if (!hasNightscoutConfig()) {
    iobCobStatus = "NS setup missing";
    return false;
  }

  // Nightscout API v2 can expose the same values used by the web pills.
  // Example path: /api/v2/properties/iob,cob
  String url = buildNightscoutUrl("/api/v2/properties/iob,cob");
  String payload;

  if (!httpsGetWithLimit(url, payload, 12000, 6000, 2200)) {
    iobCobStatus = "IOB/COB HTTP fail";
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error || !doc.is<JsonObject>()) {
    iobCobStatus = "IOB/COB JSON error";
    return false;
  }

  JsonObject root = doc.as<JsonObject>();
  bool anyOk = false;

  if (root["iob"].is<JsonObject>()) {
    JsonObject iobObj = root["iob"].as<JsonObject>();
    nightscoutIobUnits = getJsonFloatIfPresent(iobObj["iob"], 0.0);
    nightscoutIobActivity = getJsonFloatIfPresent(iobObj["activity"], 0.0);
    if (nightscoutIobUnits < 0.0) nightscoutIobUnits = 0.0;
    if (nightscoutIobUnits > 200.0) nightscoutIobUnits = 0.0;
    nightscoutIobValid = true;
    insulinRemainingUnits = nightscoutIobUnits;
    anyOk = true;

    if (iobObj["lastBolus"].is<JsonObject>()) {
      JsonObject lb = iobObj["lastBolus"].as<JsonObject>();
      float dose = getTreatmentBolusUnits(lb);
      long long doseTime = getTreatmentTimeMs(lb);
      if (dose > 0.0 && doseTime > 0) {
        lastBolusUnits = dose;
        lastBolusTimeMs = doseTime;
        lastBolusType = getTreatmentTypeText(lb);
      }
    }
  } else {
    nightscoutIobValid = false;
  }

  if (root["cob"].is<JsonObject>()) {
    JsonObject cobObj = root["cob"].as<JsonObject>();
    nightscoutCobGrams = getJsonFloatIfPresent(cobObj["cob"], 0.0);
    if (nightscoutCobGrams < 0.0) nightscoutCobGrams = 0.0;
    if (nightscoutCobGrams > 1000.0) nightscoutCobGrams = 0.0;
    nightscoutCobValid = true;
    anyOk = true;

    if (cobObj["lastCarbs"].is<JsonObject>()) {
      JsonObject lc = cobObj["lastCarbs"].as<JsonObject>();
      lastCarbsGrams = getJsonFloatIfPresent(lc["carbs"], 0.0);
      lastCarbsTimeMs = getTreatmentTimeMs(lc);
    }
  } else {
    nightscoutCobValid = false;
  }

  if (anyOk) {
    lastIobCobReadMs = millis();
    iobCobStatus = "IOB/COB OK";
    if (nightscoutIobValid) insulinStatus = "IOB auto OK";

    Serial.print("Nightscout IOB: ");
    Serial.println(nightscoutIobUnits, 2);
    Serial.print("Nightscout COB: ");
    Serial.println(nightscoutCobGrams, 1);
    return true;
  }

  iobCobStatus = "IOB/COB not found";
  return false;
}


bool readNightscoutBolusInfo() {
  if (WiFi.status() != WL_CONNECTED) {
    insulinStatus = "WiFi lost";
    dailyTotalsStatus = "WiFi lost";
    return false;
  }

  if (!hasNightscoutConfig()) {
    insulinStatus = "NS setup missing";
    dailyTotalsStatus = "NS setup missing";
    return false;
  }

  long long todayStartMs = getStartOfTodayLocalMs();
  long long todayEndMs = getEndOfTodayLocalMs();
  long long yesterdayStartMs = getStartOfYesterdayLocalMs();

  // Read one bounded two-day treatment window so the same raw response supplies:
  // current-day insulin/carbohydrate totals, yesterday totals, latest entries and
  // today's administration-time lists. Every entry is still checked against local
  // midnight boundaries before it is counted.
  String endpoint = "/api/v1/treatments.json?count=1000";
  bool usingTwoDayFilteredQuery = false;

  if (yesterdayStartMs > 0 && todayEndMs > yesterdayStartMs) {
    String startIso = formatUtcIsoFromMs(yesterdayStartMs);
    String endIso = formatUtcIsoFromMs(todayEndMs);

    if (startIso.length() > 0 && endIso.length() > 0) {
      endpoint += "&find%5Bcreated_at%5D%5B%24gte%5D=" + urlEncode(startIso);
      endpoint += "&find%5Bcreated_at%5D%5B%24lt%5D=" + urlEncode(endIso);
      usingTwoDayFilteredQuery = true;
    }
  }

  String url = buildNightscoutUrl(endpoint);
  String payload;
  bool httpOk = httpsGet(url, payload);

  if (!httpOk && usingTwoDayFilteredQuery) {
    Serial.println("Two-day treatment query failed. Falling back to count=1000.");
    url = buildNightscoutUrl("/api/v1/treatments.json?count=1000");
    payload = "";
    httpOk = httpsGet(url, payload);
  }

  if (!httpOk) {
    insulinStatus = "Treatment HTTP fail";
    dailyTotalsStatus = "Treatment HTTP fail";
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error || !doc.is<JsonArray>()) {
    insulinStatus = "Treatment JSON error";
    dailyTotalsStatus = "Treatment JSON error";
    return false;
  }

  // The response parsed correctly; rebuild the visible history tables from this raw data.
  todayTreatmentRowCount = 0;
  yesterdayTreatmentRowCount = 0;

  float newestDose = 0.0;
  long long newestDoseTime = 0;
  String newestDoseType = "Bolus";
  float newestCarbs = 0.0;
  long long newestCarbTime = 0;

  float todayDoseTotal = 0.0;
  float todayCarbTotal = 0.0;
  float yesterdayDoseTotal = 0.0;
  float yesterdayCarbTotal = 0.0;
  uint8_t todayDoseCountLocal = 0;
  uint8_t todayCarbCountLocal = 0;
  uint8_t yesterdayDoseCountLocal = 0;
  uint8_t yesterdayCarbCountLocal = 0;
  uint16_t treatmentRowsChecked = 0;

  long long todayDoseTimes[MAX_DAILY_ADMIN_EVENTS] = {0};
  float todayDoseAmounts[MAX_DAILY_ADMIN_EVENTS] = {0};
  uint8_t todayDoseEventCount = 0;
  long long todayCarbTimes[MAX_DAILY_ADMIN_EVENTS] = {0};
  float todayCarbAmounts[MAX_DAILY_ADMIN_EVENTS] = {0};
  uint8_t todayCarbEventCount = 0;

  for (JsonObject treatment : doc.as<JsonArray>()) {
    long long eventTime = getTreatmentTimeMs(treatment);
    if (eventTime <= 0) continue;

    float dose = getTreatmentBolusUnits(treatment);
    float carbs = getTreatmentCarbsGrams(treatment);
    if (dose <= 0.0 && carbs <= 0.0) continue;
    treatmentRowsChecked++;

    bool isToday = todayStartMs > 0 && todayEndMs > todayStartMs &&
                   eventTime >= todayStartMs && eventTime < todayEndMs;
    bool isYesterday = yesterdayStartMs > 0 && todayStartMs > yesterdayStartMs &&
                       eventTime >= yesterdayStartMs && eventTime < todayStartMs;

    if (isToday) {
      addDailyTreatmentRow(true, eventTime, dose, carbs);
    } else if (isYesterday) {
      addDailyTreatmentRow(false, eventTime, dose, carbs);
    }

    if (dose > 0.0) {
      if (isToday) {
        todayDoseTotal += dose;
        if (todayDoseCountLocal < 250) todayDoseCountLocal++;
        addDailyAdministrationEvent(todayDoseTimes, todayDoseAmounts,
                                    todayDoseEventCount, eventTime, dose);
      } else if (isYesterday) {
        yesterdayDoseTotal += dose;
        if (yesterdayDoseCountLocal < 250) yesterdayDoseCountLocal++;
      }

      if (eventTime > newestDoseTime) {
        newestDoseTime = eventTime;
        newestDose = dose;
        newestDoseType = getTreatmentTypeText(treatment);
      }
    }

    if (carbs > 0.0) {
      if (isToday) {
        todayCarbTotal += carbs;
        if (todayCarbCountLocal < 250) todayCarbCountLocal++;
        addDailyAdministrationEvent(todayCarbTimes, todayCarbAmounts,
                                    todayCarbEventCount, eventTime, carbs);
      } else if (isYesterday) {
        yesterdayCarbTotal += carbs;
        if (yesterdayCarbCountLocal < 250) yesterdayCarbCountLocal++;
      }

      if (eventTime > newestCarbTime) {
        newestCarbTime = eventTime;
        newestCarbs = carbs;
      }
    }
  }

  sortDailyTreatmentRowsNewestFirst(true);
  sortDailyTreatmentRowsNewestFirst(false);

  todayBolusUnits = todayDoseTotal;
  todayBolusCount = todayDoseCountLocal;
  todayCarbsGrams = todayCarbTotal;
  todayCarbCount = todayCarbCountLocal;
  yesterdayBolusUnits = yesterdayDoseTotal;
  yesterdayBolusCount = yesterdayDoseCountLocal;
  yesterdayCarbsGrams = yesterdayCarbTotal;
  yesterdayCarbCount = yesterdayCarbCountLocal;

  todayBolusAdministrationText = formatDailyAdministrationEvents(
    todayDoseTimes, todayDoseAmounts, todayDoseEventCount, "u", 1);
  todayCarbAdministrationText = formatDailyAdministrationEvents(
    todayCarbTimes, todayCarbAmounts, todayCarbEventCount, "g", 0);

  if (newestDose > 0.0 && newestDoseTime > 0) {
    lastBolusUnits = newestDose;
    lastBolusTimeMs = newestDoseTime;
    lastBolusType = newestDoseType;
  }

  if (newestCarbs > 0.0 && newestCarbTime > 0) {
    lastCarbsGrams = newestCarbs;
    lastCarbsTimeMs = newestCarbTime;
  }

  updateInsulinRemainingEstimate();

  insulinStatus = todayDoseCountLocal > 0 ? "Bolus day OK" : "No bolus today";
  dailyTotalsStatus = "2-day totals OK";

  Serial.println("Daily insulin/carbohydrate totals:");
  Serial.print("  Yesterday start: ");
  Serial.println(formatTimestampShort(yesterdayStartMs));
  Serial.print("  Today start:     ");
  Serial.println(formatTimestampShort(todayStartMs));
  Serial.print("  Tomorrow start:  ");
  Serial.println(formatTimestampShort(todayEndMs));
  Serial.print("  Query: ");
  Serial.println(usingTwoDayFilteredQuery ? "two-day date-filtered" : "recent fallback");
  Serial.print("  Treatment rows checked: ");
  Serial.println(treatmentRowsChecked);
  Serial.print("  Today insulin: ");
  Serial.print(todayBolusUnits, 2);
  Serial.print("u in ");
  Serial.print(todayBolusCount);
  Serial.println(" entries");
  Serial.print("  Today carbs: ");
  Serial.print(todayCarbsGrams, 1);
  Serial.print("g in ");
  Serial.print(todayCarbCount);
  Serial.println(" entries");
  Serial.print("  Yesterday insulin: ");
  Serial.print(yesterdayBolusUnits, 2);
  Serial.println("u");
  Serial.print("  Yesterday carbs: ");
  Serial.print(yesterdayCarbsGrams, 1);
  Serial.println("g");

  return true;
}


bool applyNightscoutSageCandidate(JsonObject candidate, const String &sourceName) {
  bool found = candidate["found"] | false;
  if (!found) return false;

  String display = getJsonStringIfPresent(candidate["display"]);
  String displayLong = getJsonStringIfPresent(candidate["displayLong"]);
  long long treatmentMs = getJsonTimestampMsIfPresent(candidate["treatmentDate"]);

  int ageHours = -1;
  if (candidate["age"].is<int>()) {
    ageHours = candidate["age"].as<int>();
  } else if (candidate["hours"].is<int>() && candidate["days"].is<int>()) {
    ageHours = (candidate["days"].as<int>() * 24) + candidate["hours"].as<int>();
  }

  if (display.length() == 0 && ageHours >= 0) {
    display = formatHoursAsDaysHours(ageHours);
  }

  if (display.length() == 0 && treatmentMs <= 0) {
    return false;
  }

  nightscoutSageDisplay = display;
  nightscoutSageDisplayLong = displayLong;
  nightscoutSageSource = sourceName;
  nightscoutSageTreatmentMs = treatmentMs;
  nightscoutSageAgeHours = ageHours;
  nightscoutSageLevel = candidate["level"] | -1;

  // Treat SAGE treatmentDate as the active sensor start/change timestamp
  // so manual sensor-left calculations can use the same source.
  if (treatmentMs > 0) {
    nightscoutSensorStartMs = treatmentMs;
  }

  // The source identifies the Nightscout SAGE event used for the age.
  nightscoutSensorSource = sourceName;
  // Wear duration remains user-configured in all modes.
  nightscoutSensorWearDays = appConfig.sensorWearDays;

  sageStatus = "SAGE OK";
  if (display.length() > 0) {
    sageStatus += " ";
    sageStatus += display;
  }

  sensorInfoStatus = sageStatus;
  updateDetectedCgmSource();

  Serial.print("Nightscout SAGE: ");
  Serial.print(sourceName);
  Serial.print(" ");
  Serial.print(nightscoutSageDisplay);
  Serial.print(" level ");
  Serial.println(nightscoutSageLevel);

  return true;
}

bool readNightscoutSageInfo() {
  if (!appConfig.sensorAutoRead) {
    sageStatus = "Manual local";
    updateDetectedCgmSource();
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    sageStatus = "WiFi lost";
    return false;
  }

  if (!hasNightscoutConfig()) {
    sageStatus = "NS setup missing";
    return false;
  }

  // Nightscout's SAGE plugin exposes this calculated property in API v2.
  // Example path: /api/v2/properties/sage
  String url = buildNightscoutUrl("/api/v2/properties/sage");
  String payload;

  if (!httpsGetWithLimit(url, payload, 12000, 5000, 800)) {
    sageStatus = "SAGE HTTP fail";
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error || !doc.is<JsonObject>()) {
    sageStatus = "SAGE JSON error";
    return false;
  }

  JsonObject root = doc.as<JsonObject>();
  JsonObject sageRoot;

  if (root["sage"].is<JsonObject>()) {
    sageRoot = root["sage"].as<JsonObject>();
  } else {
    sageRoot = root;
  }

  String preferred = getJsonStringIfPresent(sageRoot["min"]);
  preferred.trim();

  // Nightscout SAGE is normally based on Sensor Start or Sensor Change.
  // Some rigs/plugins may expose insertion wording, so support these too.
  const char* sageKeys[] = {
    "Sensor Change",
    "Sensor Start",
    "Sensor Insert",
    "Sensor Inserted",
    "Sensor Insertion",
    "Sensor Started",
    "CGM Sensor Insert",
    "CGM Sensor Start"
  };

  if (preferred.length() > 0 && sageRoot[preferred.c_str()].is<JsonObject>()) {
    if (applyNightscoutSageCandidate(sageRoot[preferred.c_str()].as<JsonObject>(), preferred)) return true;
  }

  for (uint8_t i = 0; i < sizeof(sageKeys) / sizeof(sageKeys[0]); i++) {
    String key = String(sageKeys[i]);
    if (key == preferred) continue;
    if (sageRoot[key.c_str()].is<JsonObject>()) {
      if (applyNightscoutSageCandidate(sageRoot[key.c_str()].as<JsonObject>(), key)) return true;
    }
  }

  sageStatus = "SAGE not found";
  return false;
}


// ==================================================
// 20. READ PHONE BATTERY
// ==================================================

bool readNightscoutDeviceStatus() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  if (!hasNightscoutConfig()) {
    phoneBattery = -1;
    return false;
  }

  String url = buildNightscoutUrl("/api/v1/devicestatus.json?count=1");

  String payload;

  if (!httpsGet(url, payload)) {
    phoneBattery = -1;
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error || !doc.is<JsonArray>() || doc.size() == 0) {
    phoneBattery = -1;
    return false;
  }

  JsonObject status = doc[0];

  phoneBattery = -1;
  lastDeviceStatusDevice = "";

  if (status["device"].is<const char*>()) {
    lastDeviceStatusDevice = status["device"].as<String>();
  } else if (status["uploader"]["device"].is<const char*>()) {
    lastDeviceStatusDevice = status["uploader"]["device"].as<String>();
  } else if (status["uploader"]["name"].is<const char*>()) {
    lastDeviceStatusDevice = status["uploader"]["name"].as<String>();
  }

  if (status["uploaderBattery"].is<int>()) {
    phoneBattery = status["uploaderBattery"].as<int>();
  }
  else if (status["uploader"]["battery"].is<int>()) {
    phoneBattery = status["uploader"]["battery"].as<int>();
  }
  else if (status["battery"].is<int>()) {
    phoneBattery = status["battery"].as<int>();
  }

  updateNightscoutSensorInfo(status);

  Serial.print("Phone battery: ");
  Serial.println(phoneBattery);
  Serial.print("Device status source: ");
  Serial.println(lastDeviceStatusDevice);

  return true;
}


// ==================================================
// 21. READ DIAGNOSTICS
// ==================================================

float readLimit(JsonVariant settings, const char* key, float fallback) {
  if (!settings[key].isNull()) {
    float value = settings[key].as<float>();

    if (value > 0) {
      return convertThresholdToMmol(value);
    }
  }

  return fallback;
}

bool readNightscoutLimits() {
  if (!appConfig.useNightscoutLimits) {
    Serial.println("Using local alarm limits from device settings.");
    applyConfigToRuntime();
    return false;
  }

  if (!hasNightscoutConfig()) {
    Serial.println("Nightscout setup missing. Using local limits.");
    applyConfigToRuntime();
    return false;
  }

  String url = buildNightscoutUrl("/api/v1/status.json");

  String payload;

  if (!httpsGet(url, payload)) {
    Serial.println("Status read failed. Using fallback limits.");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("Status JSON error: ");
    Serial.println(error.c_str());
    return false;
  }

  JsonVariant settings = doc["settings"];

  if (settings.isNull()) {
    Serial.println("No settings found. Using fallback limits.");
    return false;
  }

  urgentLowLimitMmol  = readLimit(settings, "bgLow", urgentLowLimitMmol);
  lowLimitMmol        = readLimit(settings, "bgTargetBottom", lowLimitMmol);
  highLimitMmol       = readLimit(settings, "bgTargetTop", highLimitMmol);
  urgentHighLimitMmol = readLimit(settings, "bgHigh", urgentHighLimitMmol);

  Serial.println("Nightscout limits:");
  Serial.print("Urg Low: ");
  Serial.println(urgentLowLimitMmol, 1);
  Serial.print("Low: ");
  Serial.println(lowLimitMmol, 1);
  Serial.print("High: ");
  Serial.println(highLimitMmol, 1);
  Serial.print("Urg High: ");
  Serial.println(urgentHighLimitMmol, 1);

  return true;
}


// ==================================================
// 22. DIAGNOSTICS / UNIT INFO SCREEN
// ==================================================

String formatUptime() {
  unsigned long totalSeconds = millis() / 1000UL;
  unsigned long days = totalSeconds / 86400UL;
  unsigned long hours = (totalSeconds % 86400UL) / 3600UL;
  unsigned long minutes = (totalSeconds % 3600UL) / 60UL;

  if (days > 0) {
    return String(days) + "d" + String(hours) + "h";
  }

  if (hours > 0) {
    return String(hours) + "h" + String(minutes) + "m";
  }

  return String(minutes) + "m";
}

String getNightscoutLastOkText() {
  if (lastSuccessfulNightscoutRead == 0) {
    return "never";
  }

  unsigned long ageSec = (millis() - lastSuccessfulNightscoutRead) / 1000UL;

  if (ageSec < 60) {
    return String(ageSec) + "s";
  }

  return String(ageSec / 60UL) + "m";
}

String getAlarmSourceText() {
  return appConfig.useNightscoutLimits ? "NS" : "LOCAL";
}

void drawDiagnosticsScreen() {
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);
  drawWiFiSignalBars();

  u8g2.setFont(u8g2_font_6x12_tr);

  u8g2.setCursor(4, 11);
  u8g2.print("NETWORK");
  u8g2.drawLine(0, 15, 127, 15);

  u8g2.setFont(u8g2_font_5x8_tr);

  u8g2.setCursor(4, 26);
  u8g2.print("SSID:");
  if (WiFi.status() == WL_CONNECTED) u8g2.print(shortenText(WiFi.SSID(), 18));
  else u8g2.print("Disconnected");

  u8g2.setCursor(4, 37);
  u8g2.print("IP:");
  if (WiFi.status() == WL_CONNECTED) u8g2.print(WiFi.localIP());
  else u8g2.print("No IP");

  u8g2.setCursor(4, 48);
  u8g2.print("Alias:");
  u8g2.print(shortenText(getLocalMdnsHost(), 18));

  u8g2.setCursor(4, 60);
  u8g2.print("RSSI:");
  if (WiFi.status() == WL_CONNECTED) u8g2.print(WiFi.RSSI());
  else u8g2.print("LOST");

  u8g2.setCursor(66, 60);
  u8g2.print("B");
  u8g2.print(BUILD_NUMBER);
  u8g2.print(" ");
  u8g2.print(getUpdateChannelShort());

  u8g2.sendBuffer();
}



// Trend request settings.
// Dexcom/Nightscout data is normally every ~5 minutes.
// 96 entries gives better margin for a 4-hour graph at normal 5-minute CGM intervals.
const int TREND_REQUEST_COUNT = 96;
const int TREND_MAX_POINTS = 96;

// ==================================================
// 23. 4-HOUR TREND SCREEN
// ==================================================


int mapTrendValueToY(float value, float minVal, float maxVal, int graphY, int graphH) {
  if (maxVal - minVal <= 0.01) {
    return graphY + graphH / 2;
  }

  float yRatio = (value - minVal) / (maxVal - minVal);

  if (yRatio < 0.0) yRatio = 0.0;
  if (yRatio > 1.0) yRatio = 1.0;

  return graphY + graphH - 1 - (int)(yRatio * (graphH - 1));
}

void drawDottedHorizontalLine(int x, int y, int w) {
  for (int px = x; px < x + w; px += 4) {
    u8g2.drawHLine(px, y, 2);
  }
}

void drawTrendLimitLine(float limitValue,
                        const char* label,
                        float minVal,
                        float maxVal,
                        int graphX,
                        int graphY,
                        int graphW,
                        int graphH) {
  if (limitValue < minVal || limitValue > maxVal) {
    return;
  }

  int y = mapTrendValueToY(limitValue, minVal, maxVal, graphY, graphH);

  if (y <= graphY || y >= graphY + graphH - 1) {
    return;
  }

  drawDottedHorizontalLine(graphX + 1, y, graphW - 2);

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(graphX + 2, y - 1);
  u8g2.print(label);
}

void drawTrendScreen() {
  drawMessage("Reading trend", "Last 4 hours", "Please wait...", "");

  if (!hasNightscoutConfig()) {
    drawMessage("Setup Missing", "Enter Nightscout", "from web page", WiFi.localIP().toString());
    return;
  }

  String url = buildNightscoutUrl("/api/v1/entries.json?count=" + String(TREND_REQUEST_COUNT));

  String payload;

  if (!httpsGetWithLimit(url, payload, 36000, 9000, 1500)) {
    drawMessage("Trend Error", "Read failed", "or too large", "Try again");
    return;
  }

  Serial.print("Trend payload length: ");
  Serial.println(payload.length());

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("Trend JSON parse failed: ");
    Serial.println(error.c_str());

    drawMessage("Trend Error",
                "JSON failed",
                String(error.c_str()),
                "Try again");
    return;
  }

  if (!doc.is<JsonArray>()) {
    Serial.println("Trend JSON failed: payload is not a JSON array.");

    drawMessage("Trend Error",
                "Not JSON array",
                "",
                "");
    return;
  }

  float values[TREND_MAX_POINTS];
  long long times[TREND_MAX_POINTS];
  int count = 0;

  int inRangeCount = 0;
  int belowRangeCount = 0;
  int aboveRangeCount = 0;

  long long nowMs = (long long)time(nullptr) * 1000LL;
  long long startMs = nowMs - (4LL * 60LL * 60LL * 1000LL);

  for (JsonObject entry : doc.as<JsonArray>()) {
    int sgv = entry["sgv"] | 0;

    if (sgv <= 0) continue;

    long long t = getEntryTimeMs(entry);

    if (t >= startMs && t <= nowMs && count < TREND_MAX_POINTS) {
      float mmol = sgv / 18.0;

      values[count] = mmol;
      times[count] = t;

      if (mmol < lowLimitMmol) {
        belowRangeCount++;
      }
      else if (mmol > highLimitMmol) {
        aboveRangeCount++;
      }
      else {
        inRangeCount++;
      }

      count++;
    }
  }

  Serial.print("Trend valid points: ");
  Serial.println(count);
  Serial.print("Trend below range: ");
  Serial.println(belowRangeCount);
  Serial.print("Trend in range: ");
  Serial.println(inRangeCount);
  Serial.print("Trend above range: ");
  Serial.println(aboveRangeCount);

  if (count < 2) {
    drawMessage("Trend", "Not enough", "data available", "");
    return;
  }

  int tirPercent = (inRangeCount * 100 + count / 2) / count;
  int lowPercent = (belowRangeCount * 100 + count / 2) / count;
  int highPercent = (aboveRangeCount * 100 + count / 2) / count;

  float minVal = values[0];
  float maxVal = values[0];

  long long oldestMs = times[0];
  long long newestMs = times[0];

  for (int i = 1; i < count; i++) {
    if (values[i] < minVal) minVal = values[i];
    if (values[i] > maxVal) maxVal = values[i];

    if (times[i] < oldestMs) oldestMs = times[i];
    if (times[i] > newestMs) newestMs = times[i];
  }

  // Dynamic Y-axis scaling:
  // The graph always includes the actual trend min/max glucose values.
  // If glucose rises to values such as 18 mmol/L, maxVal follows it so
  // the full trend remains visible. Alarm lines are also included.
  if (lowLimitMmol < minVal) minVal = lowLimitMmol;
  if (highLimitMmol > maxVal) maxVal = highLimitMmol;

  minVal -= 0.4;
  maxVal += 0.4;

  if (maxVal - minVal < 1.0) {
    maxVal += 0.5;
    minVal -= 0.5;
  }

  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);

  // Compact top row.
  // Trend screen deliberately does not draw Wi-Fi bars.
  // This prevents overlap with the 4H / in-range header on the 128x64 OLED.
  u8g2.setFont(u8g2_font_5x8_tr);

  u8g2.setCursor(3, 8);
  u8g2.print("4H");

  u8g2.setCursor(20, 8);
  u8g2.print("IN");
  u8g2.print(tirPercent);
  u8g2.print("%");

  // Larger graph area: fills most of the OLED while preserving top and bottom data.
  int graphX = 3;
  int graphY = 11;
  int graphW = 122;
  int graphH = 44;

  u8g2.drawFrame(graphX, graphY, graphW, graphH);

  // Draw active range limit lines first so the trend line remains visible.
  drawTrendLimitLine(highLimitMmol, "H", minVal, maxVal, graphX, graphY, graphW, graphH);
  drawTrendLimitLine(lowLimitMmol, "L", minVal, maxVal, graphX, graphY, graphW, graphH);

  // To fill the graph window visually, stretch the available data points
  // across the full graph width. This avoids a half-empty graph when the
  // Nightscout response contains high-frequency entries that do not cover
  // the full 4-hour period.
  int lastX = -1;
  int lastY = -1;

  for (int i = count - 1; i >= 0; i--) {
    int plottedIndex = (count - 1) - i;
    float xRatio = (float)plottedIndex / (float)(count - 1);

    int x = graphX + (int)(xRatio * (graphW - 1));
    int y = mapTrendValueToY(values[i], minVal, maxVal, graphY, graphH);

    if (lastX >= 0) {
      u8g2.drawLine(lastX, lastY, x, y);
    }

    u8g2.drawPixel(x, y);

    lastX = x;
    lastY = y;
  }

  u8g2.setFont(u8g2_font_5x8_tr);

  u8g2.setCursor(3, 63);
  u8g2.print("L");
  u8g2.print(lowPercent);

  u8g2.setCursor(31, 63);
  u8g2.print("I");
  u8g2.print(tirPercent);

  u8g2.setCursor(62, 63);
  u8g2.print("H");
  u8g2.print(highPercent);

  u8g2.setCursor(92, 63);
  u8g2.print("P");
  u8g2.print(count);

  u8g2.setCursor(113, 63);
  u8g2.print("%");

  u8g2.sendBuffer();
}



// ==================================================
// 24A. TWILIO WHATSAPP ALERTS
// ==================================================

String normalizeWhatsAppNumber(String value) {
  value.trim();
  if (value.length() == 0) return "";
  if (!value.startsWith("whatsapp:")) {
    value = "whatsapp:" + value;
  }
  return value;
}

String getTwilioStatusText() {
  if (!appConfig.twilioEnabled) return "Disabled";
  if (appConfig.twilioSid.length() < 10 || appConfig.twilioToken.length() < 8) return "Enabled, settings incomplete";
  return twilioLastStatus;
}

String getTwilioEventKeyForCurrentState() {
  if (!isDataFresh()) return "NO_DATA";

  int level = getCurrentAlarmLevel();
  if (level == ALARM_LOW) return "LOW";
  if (level == ALARM_URGENT_LOW) return "URGENT_LOW";
  if (level == ALARM_HIGH) return "HIGH";
  if (level == ALARM_URGENT_HIGH) return "URGENT_HIGH";
  return "NONE";
}

unsigned long getTwilioRepeatMs(String eventKey) {
  uint16_t minutes = 0;
  if (eventKey == "LOW") minutes = appConfig.lowRepeatMinutes;
  else if (eventKey == "URGENT_LOW") minutes = appConfig.urgentLowRepeatMinutes;
  else if (eventKey == "HIGH") minutes = appConfig.highRepeatMinutes;
  else if (eventKey == "URGENT_HIGH") minutes = appConfig.urgentHighRepeatMinutes;
  else if (eventKey == "NO_DATA") minutes = appConfig.noDataRepeatMinutes;
  else if (eventKey == "PHONE_BATTERY_LOW") minutes = appConfig.phoneBatteryRepeatMinutes;
  else if (eventKey == "SENSOR_EXPIRY") minutes = appConfig.sensorExpiryRepeatHours * 60;
  else minutes = 0;

  if (minutes == 0) return 0;
  return (unsigned long)minutes * 60UL * 1000UL;
}

String buildTwilioAlertMessage(String eventKey, bool testMode) {
  String prefix = testMode ? "TEST - " : "";
  String msg = "";

  if (eventKey == "URGENT_LOW") {
    msg += prefix + "URGENT LOW ALERT\n";
    msg += appConfig.patientName + ": " + formatGlucoseDisplay(glucoseMmol > 0 ? glucoseMmol : appConfig.urgentLow - 0.1) + " " + getGlucoseUnitLabel() + "\n";
    msg += "Trend: " + directionText + "\n";
    msg += "Room: " + appConfig.deviceLocation + "\n";
    msg += "Action: Check CGM phone now.";
  } else if (eventKey == "LOW") {
    msg += prefix + "LOW GLUCOSE ALERT\n";
    msg += appConfig.patientName + ": " + formatGlucoseDisplay(glucoseMmol > 0 ? glucoseMmol : appConfig.low - 0.1) + " " + getGlucoseUnitLabel() + "\n";
    msg += "Trend: " + directionText + "\n";
    msg += "Room: " + appConfig.deviceLocation + "\n";
    msg += "Action: Check CGM phone.";
  } else if (eventKey == "URGENT_HIGH") {
    msg += prefix + "URGENT HIGH ALERT\n";
    msg += appConfig.patientName + ": " + formatGlucoseDisplay(glucoseMmol > 0 ? glucoseMmol : appConfig.urgentHigh + 0.1) + " " + getGlucoseUnitLabel() + "\n";
    msg += "Trend: " + directionText + "\n";
    msg += "Room: " + appConfig.deviceLocation + "\n";
    msg += "Action: Check CGM phone and care plan.";
  } else if (eventKey == "HIGH") {
    msg += prefix + "HIGH GLUCOSE ALERT\n";
    msg += appConfig.patientName + ": " + formatGlucoseDisplay(glucoseMmol > 0 ? glucoseMmol : appConfig.high + 0.1) + " " + getGlucoseUnitLabel() + "\n";
    msg += "Trend: " + directionText + "\n";
    msg += "Room: " + appConfig.deviceLocation + "\n";
    msg += "Action: Check CGM phone.";
  } else if (eventKey == "PHONE_BATTERY_LOW") {
    msg += prefix + "CGM PHONE BATTERY LOW\n";
    msg += appConfig.patientName + " CGM/uploader phone battery: ";
    msg += phoneBattery >= 0 ? String(phoneBattery) + "%\n" : String("unknown\n");
    msg += "Alert limit: " + String(appConfig.phoneBatteryAlertPercent) + "%\n";
    msg += "Room: " + appConfig.deviceLocation + "\n";
    msg += "Please charge or check the CGM phone.";
  } else if (eventKey == "SENSOR_EXPIRY") {
    long leftHours = getSensorLeftHours();
    long daysLeft = leftHours >= 0 ? ((leftHours + 23L) / 24L) : -1;
    msg += prefix + "CGM SENSOR EXPIRY REMINDER\n";
    msg += appConfig.patientName + " sensor countdown: ";
    if (leftHours >= 0) {
      if (daysLeft > 0) msg += String(daysLeft) + " day(s) left\n";
      else msg += "less than 1 day left\n";
      msg += "Exact remaining: " + getSensorLeftText() + "\n";
    } else {
      msg += "expired or unknown\n";
    }
    msg += "Room: " + appConfig.deviceLocation + "\n";
    msg += "Please prepare a replacement sensor.";
  } else if (eventKey == "NO_DATA") {
    msg += prefix + "NO CGM DATA\n";
    msg += "Leah 2R Displays has not received fresh glucose data for " + appConfig.patientName + ".\n";
    msg += "Room: " + appConfig.deviceLocation + "\n";
    msg += "Check phone, internet, Nightscout and CGM source.";
  } else {
    msg += prefix + "LEAH 2R DISPLAYS TEST\n";
    msg += "Leah 2R Displays WhatsApp alert test from ESP32.\n";
    msg += "Device: " + appConfig.deviceName + "\n";
    msg += "Room: " + appConfig.deviceLocation;
  }

  return msg;
}

bool sendTwilioWhatsApp(String toNumber, String message) {
  if (!appConfig.twilioEnabled) {
    twilioLastStatus = "Twilio disabled";
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    twilioLastStatus = "Wi-Fi disconnected";
    return false;
  }

  toNumber = normalizeWhatsAppNumber(toNumber);
  String fromNumber = normalizeWhatsAppNumber(appConfig.twilioFrom);

  if (appConfig.twilioSid.length() < 10 || appConfig.twilioToken.length() < 8 ||
      fromNumber.length() < 10 || toNumber.length() < 10) {
    twilioLastStatus = "Twilio settings incomplete";
    return false;
  }

  String url = "https://api.twilio.com/2010-04-01/Accounts/" + appConfig.twilioSid + "/Messages.json";
  String body = "From=" + urlEncode(fromNumber) +
                "&To=" + urlEncode(toNumber) +
                "&Body=" + urlEncode(message);

  WiFiClientSecure client;
  client.setInsecure(); // Field-test build. Production should validate TLS certificates or use a gateway.
  client.setHandshakeTimeout(5);

  HTTPClient http;
  http.setTimeout(PROVIDER_HTTP_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    twilioLastStatus = "HTTP begin failed";
    return false;
  }

  http.setAuthorization(appConfig.twilioSid.c_str(), appConfig.twilioToken.c_str());
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Connection", "close");

  twilioSendInProgress = true;
  int code = http.POST(body);
  String response = readHttpResponseSnippet(http, 1200, PROVIDER_RESPONSE_READ_TIMEOUT_MS);
  http.end();
  twilioSendInProgress = false;

  twilioLastHttpCode = code;
  twilioLastResponse = response.substring(0, 1200);

  if (code >= 200 && code < 300) {
    twilioLastStatus = "Accepted HTTP " + String(code);
    return true;
  }

  twilioLastStatus = getTwilioShortError();
  return false;
}

String caregiverSendStatus(bool configured, bool eventEnabled, bool ok) {
  if (!configured) return "--";
  if (!eventEnabled) return "OFF";
  return ok ? "OK" : "FAIL";
}

bool sendTwilioMessageToConfiguredParents(String eventKey, const String &message) {
  bool ok1 = false;
  bool ok2 = false;

  bool c1Configured = appConfig.twilioParent1.length() > 0;
  bool c2Configured = appConfig.twilioParent2.length() > 0;
  bool c1Enabled = caregiverWantsWhatsAppEvent(1, eventKey);
  bool c2Enabled = caregiverWantsWhatsAppEvent(2, eventKey);

  if (c1Configured && c1Enabled) {
    ok1 = sendTwilioWhatsApp(appConfig.twilioParent1, message);
    delay(250);
  }

  if (c2Configured && c2Enabled) {
    ok2 = sendTwilioWhatsApp(appConfig.twilioParent2, message);
  }

  twilioLastEvent = eventKey;

  if (c1Configured || c2Configured) {
    twilioLastStatus = String("C1 ") + caregiverSendStatus(c1Configured, c1Enabled, ok1) +
                       ", C2 " + caregiverSendStatus(c2Configured, c2Enabled, ok2) +
                       ", HTTP " + String(twilioLastHttpCode);
  }

  return ok1 || ok2;
}

bool sendTwilioToConfiguredParents(String eventKey, bool testMode) {
  return sendTwilioMessageToConfiguredParents(eventKey, buildTwilioAlertMessage(eventKey, testMode));
}

void sendTwilioTestToTarget(String target, String eventKey) {
  if (target == "p1") {
    bool ok = sendTwilioWhatsApp(appConfig.twilioParent1, buildTwilioAlertMessage(eventKey, true));
    twilioLastEvent = "TEST_P1";
    if (ok) twilioLastStatus = "Caregiver 1 test accepted HTTP " + String(twilioLastHttpCode);
  } else if (target == "p2") {
    bool ok = sendTwilioWhatsApp(appConfig.twilioParent2, buildTwilioAlertMessage(eventKey, true));
    twilioLastEvent = "TEST_P2";
    if (ok) twilioLastStatus = "Caregiver 2 test accepted HTTP " + String(twilioLastHttpCode);
  } else {
    sendTwilioToConfiguredParents(eventKey, true);
  }
}

void updateTwilioAlerts() {
  if (!appConfig.twilioEnabled) return;
  if (twilioSendInProgress) return;
  if (sirenSilenced) return;

  String eventKey = getTwilioEventKeyForCurrentState();

  if (eventKey == "NONE") {
    twilioCurrentEventKey = "NONE";
    return;
  }

  unsigned long repeatMs = getTwilioRepeatMs(eventKey);
  if (repeatMs == 0) return;

  bool eventChanged = eventKey != twilioCurrentEventKey;
  bool repeatDue = (millis() - twilioLastEventSendMs) >= repeatMs;

  if (eventChanged || repeatDue) {
    twilioCurrentEventKey = eventKey;
    twilioLastEventSendMs = millis();
    queueCloudAlertJob("TWILIO", "all", eventKey, false, 0);
  }
}


// ==================================================
// 24B. CALLMEBOT PERSONAL WHATSAPP ALERTS
// ==================================================

String normalizeCallMeBotPhone(String value) {
  value.trim();
  value.replace("whatsapp:", "");
  value.replace("+", "");
  value.replace(" ", "");
  value.replace("-", "");
  value.replace("(", "");
  value.replace(")", "");
  return value;
}

String getCallMeBotShortError() {
  if (callMeBotLastHttpCode >= 200 && callMeBotLastHttpCode < 300) return "OK " + String(callMeBotLastHttpCode);
  if (callMeBotLastHttpCode == 0) return shortenText(callMeBotLastStatus, 18);
  return "HTTP " + String(callMeBotLastHttpCode);
}

bool isCallMeBotConfiguredForAlerts() {
  bool p1 = appConfig.callMeBotParent1.length() >= 8 && appConfig.callMeBotApiKey1.length() >= 3;
  bool p2 = appConfig.callMeBotParent2.length() >= 8 && appConfig.callMeBotApiKey2.length() >= 3;
  return p1 || p2;
}

bool sendCallMeBotWhatsApp(String phone, String apiKey, String message) {
  if (!appConfig.callMeBotEnabled) {
    callMeBotLastStatus = "CallMeBot disabled";
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    callMeBotLastStatus = "Wi-Fi disconnected";
    return false;
  }

  phone = normalizeCallMeBotPhone(phone);
  apiKey.trim();

  if (phone.length() < 8 || apiKey.length() < 3) {
    callMeBotLastStatus = "CallMeBot settings incomplete";
    return false;
  }

  String url = "https://api.callmebot.com/whatsapp.php?phone=";
  url += urlEncode(phone);
  url += "&text=";
  url += urlEncode(message);
  url += "&apikey=";
  url += urlEncode(apiKey);

  WiFiClientSecure client;
  client.setInsecure(); // Field-test build. Production should validate TLS certificates or use a gateway.
  client.setHandshakeTimeout(5);

  HTTPClient http;
  http.setTimeout(PROVIDER_HTTP_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    callMeBotLastStatus = "HTTP begin failed";
    return false;
  }

  http.addHeader("Connection", "close");

  callMeBotSendInProgress = true;
  int code = http.GET();
  String response = readHttpResponseSnippet(http, 1200, PROVIDER_RESPONSE_READ_TIMEOUT_MS);
  http.end();
  callMeBotSendInProgress = false;

  callMeBotLastHttpCode = code;
  callMeBotLastResponse = response.substring(0, 1200);

  String responseLower = response;
  responseLower.toLowerCase();

  if (code >= 200 && code < 300 && responseLower.indexOf("error") < 0) {
    callMeBotLastStatus = "Accepted HTTP " + String(code);
    return true;
  }

  callMeBotLastStatus = getCallMeBotShortError() + " " + shortenText(response, 24);
  return false;
}

bool sendCallMeBotMessageToConfiguredParents(String eventKey, const String &message) {
  bool ok1 = false;
  bool ok2 = false;

  bool c1Configured = appConfig.callMeBotParent1.length() > 0 && appConfig.callMeBotApiKey1.length() > 0;
  bool c2Configured = appConfig.callMeBotParent2.length() > 0 && appConfig.callMeBotApiKey2.length() > 0;
  bool c1Enabled = caregiverWantsWhatsAppEvent(1, eventKey);
  bool c2Enabled = caregiverWantsWhatsAppEvent(2, eventKey);

  if (c1Configured && c1Enabled) {
    ok1 = sendCallMeBotWhatsApp(appConfig.callMeBotParent1, appConfig.callMeBotApiKey1, message);
    delay(250);
  }

  if (c2Configured && c2Enabled) {
    ok2 = sendCallMeBotWhatsApp(appConfig.callMeBotParent2, appConfig.callMeBotApiKey2, message);
  }

  callMeBotLastEvent = eventKey;

  if (c1Configured || c2Configured) {
    callMeBotLastStatus = String("C1 ") + caregiverSendStatus(c1Configured, c1Enabled, ok1) +
                          ", C2 " + caregiverSendStatus(c2Configured, c2Enabled, ok2) +
                          ", HTTP " + String(callMeBotLastHttpCode);
  } else if (ok1 || ok2) {
    callMeBotLastStatus = "CallMeBot accepted HTTP " + String(callMeBotLastHttpCode);
  }

  return ok1 || ok2;
}

bool sendCallMeBotToConfiguredParents(String eventKey, bool testMode) {
  return sendCallMeBotMessageToConfiguredParents(eventKey, buildTwilioAlertMessage(eventKey, testMode));
}

void sendCallMeBotTestToTarget(String target, String eventKey) {
  if (target == "p1") {
    bool ok = sendCallMeBotWhatsApp(appConfig.callMeBotParent1, appConfig.callMeBotApiKey1, buildTwilioAlertMessage(eventKey, true));
    callMeBotLastEvent = "TEST_P1";
    if (ok) callMeBotLastStatus = "Caregiver 1 test accepted HTTP " + String(callMeBotLastHttpCode);
  } else if (target == "p2") {
    bool ok = sendCallMeBotWhatsApp(appConfig.callMeBotParent2, appConfig.callMeBotApiKey2, buildTwilioAlertMessage(eventKey, true));
    callMeBotLastEvent = "TEST_P2";
    if (ok) callMeBotLastStatus = "Caregiver 2 test accepted HTTP " + String(callMeBotLastHttpCode);
  } else {
    sendCallMeBotToConfiguredParents(eventKey, true);
  }
}

void updateCallMeBotAlerts() {
  if (!appConfig.callMeBotEnabled) return;
  if (callMeBotSendInProgress) return;

  String eventKey = getTwilioEventKeyForCurrentState();

  if (eventKey == "NONE") {
    callMeBotCurrentEventKey = "NONE";
    return;
  }

  unsigned long repeatMs = getTwilioRepeatMs(eventKey);
  if (repeatMs == 0) return;

  bool eventChanged = eventKey != callMeBotCurrentEventKey;
  bool repeatDue = (millis() - callMeBotLastEventSendMs) >= repeatMs;

  if (eventChanged || repeatDue) {
    callMeBotCurrentEventKey = eventKey;
    callMeBotLastEventSendMs = millis();
    queueCloudAlertJob("CALLMEBOT", "all", eventKey, false, 0);
  }
}


// ==================================================
// 24C. TEXTMEBOT OWN-NUMBER WHATSAPP ALERTS
// ==================================================

String normalizeTextMeBotPhone(String value) {
  value.trim();
  value.replace("whatsapp:", "");
  value.replace("+", "");
  value.replace(" ", "");
  value.replace("-", "");
  value.replace("(", "");
  value.replace(")", "");
  return value;
}

String getTextMeBotShortError() {
  if (textMeBotLastHttpCode >= 200 && textMeBotLastHttpCode < 300) return "OK " + String(textMeBotLastHttpCode);
  if (textMeBotLastHttpCode == 0) return shortenText(textMeBotLastStatus, 18);
  return "HTTP " + String(textMeBotLastHttpCode);
}

bool isTextMeBotConfiguredForAlerts() {
  bool p1 = appConfig.textMeBotParent1.length() >= 8;
  bool p2 = appConfig.textMeBotParent2.length() >= 8;
  bool p3 = appConfig.textMeBotParent3.length() >= 8;
  return appConfig.textMeBotApiKey.length() >= 3 && (p1 || p2 || p3);
}

bool sendTextMeBotWhatsApp(String recipient, String apiKey, String message) {
  if (!appConfig.textMeBotEnabled) {
    textMeBotLastStatus = "TextMeBot disabled";
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    textMeBotLastStatus = "Wi-Fi disconnected";
    return false;
  }

  recipient = normalizeTextMeBotPhone(recipient);
  apiKey.trim();

  if (recipient.length() < 8 || apiKey.length() < 3) {
    textMeBotLastStatus = "TextMeBot settings incomplete";
    return false;
  }

  String url = "https://api.textmebot.com/send.php?recipient=";
  url += urlEncode(recipient);
  url += "&apikey=";
  url += urlEncode(apiKey);
  url += "&text=";
  url += urlEncode(message);

  WiFiClientSecure client;
  client.setInsecure(); // Field-test build. Production should validate TLS certificates or use a gateway.
  client.setHandshakeTimeout(5);

  HTTPClient http;
  http.setTimeout(PROVIDER_HTTP_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    textMeBotLastStatus = "HTTP begin failed";
    return false;
  }

  http.addHeader("Connection", "close");

  textMeBotSendInProgress = true;
  int code = http.GET();
  String response = readHttpResponseSnippet(http, 1200, PROVIDER_RESPONSE_READ_TIMEOUT_MS);
  http.end();
  textMeBotSendInProgress = false;

  textMeBotLastHttpCode = code;
  textMeBotLastResponse = response.substring(0, 1200);

  String responseLower = response;
  responseLower.toLowerCase();

  if (code >= 200 && code < 300 && responseLower.indexOf("error") < 0) {
    textMeBotLastStatus = "Accepted HTTP " + String(code);
    return true;
  }

  textMeBotLastStatus = getTextMeBotShortError() + " " + shortenText(response, 24);
  return false;
}

void delayBetweenTextMeBotRecipients() {
  // This function is now called only by the background cloud task.
  // vTaskDelay yields cleanly without blocking the Arduino loop, Web UI or audio task.
  vTaskDelay(pdMS_TO_TICKS(TEXTMEBOT_RECIPIENT_GAP_MS));
}

bool sendTextMeBotMessageToConfiguredParents(String eventKey, const String &message) {
  bool ok1 = false;
  bool ok2 = false;
  bool ok3 = false;

  bool c1Configured = appConfig.textMeBotParent1.length() > 0;
  bool c2Configured = appConfig.textMeBotParent2.length() > 0;
  bool c3Configured = appConfig.textMeBotParent3.length() > 0;
  bool c1Enabled = caregiverWantsWhatsAppEvent(1, eventKey);
  bool c2Enabled = caregiverWantsWhatsAppEvent(2, eventKey);
  bool c3Enabled = caregiverWantsWhatsAppEvent(3, eventKey);

  bool hasP1 = c1Configured && c1Enabled;
  bool hasP2 = c2Configured && c2Enabled;
  bool hasP3 = c3Configured && c3Enabled;

  if (hasP1) {
    ok1 = sendTextMeBotWhatsApp(appConfig.textMeBotParent1, appConfig.textMeBotApiKey, message);
    if (hasP2 || hasP3) delayBetweenTextMeBotRecipients();
  }

  if (hasP2) {
    ok2 = sendTextMeBotWhatsApp(appConfig.textMeBotParent2, appConfig.textMeBotApiKey, message);
    if (hasP3) delayBetweenTextMeBotRecipients();
  }

  if (hasP3) {
    ok3 = sendTextMeBotWhatsApp(appConfig.textMeBotParent3, appConfig.textMeBotApiKey, message);
  }

  textMeBotLastEvent = eventKey;

  if (c1Configured || c2Configured || c3Configured) {
    textMeBotLastStatus = String("C1 ") + caregiverSendStatus(c1Configured, c1Enabled, ok1) +
                          ", C2 " + caregiverSendStatus(c2Configured, c2Enabled, ok2) +
                          ", C3 " + caregiverSendStatus(c3Configured, c3Enabled, ok3) +
                          ", HTTP " + String(textMeBotLastHttpCode);
  } else if (ok1 || ok2 || ok3) {
    textMeBotLastStatus = "TextMeBot accepted HTTP " + String(textMeBotLastHttpCode);
  }

  return ok1 || ok2 || ok3;
}

bool sendTextMeBotToConfiguredParents(String eventKey, bool testMode) {
  return sendTextMeBotMessageToConfiguredParents(eventKey, buildTwilioAlertMessage(eventKey, testMode));
}

void sendTextMeBotTestToTarget(String target, String eventKey) {
  if (target == "p1") {
    bool ok = sendTextMeBotWhatsApp(appConfig.textMeBotParent1, appConfig.textMeBotApiKey, buildTwilioAlertMessage(eventKey, true));
    textMeBotLastEvent = "TEST_P1";
    if (ok) textMeBotLastStatus = "Caregiver 1 test accepted HTTP " + String(textMeBotLastHttpCode);
  } else if (target == "p2") {
    bool ok = sendTextMeBotWhatsApp(appConfig.textMeBotParent2, appConfig.textMeBotApiKey, buildTwilioAlertMessage(eventKey, true));
    textMeBotLastEvent = "TEST_P2";
    if (ok) textMeBotLastStatus = "Caregiver 2 test accepted HTTP " + String(textMeBotLastHttpCode);
  } else if (target == "p3") {
    bool ok = sendTextMeBotWhatsApp(appConfig.textMeBotParent3, appConfig.textMeBotApiKey, buildTwilioAlertMessage(eventKey, true));
    textMeBotLastEvent = "TEST_P3";
    if (ok) textMeBotLastStatus = "Caregiver 3 test accepted HTTP " + String(textMeBotLastHttpCode);
  } else {
    sendTextMeBotToConfiguredParents(eventKey, true);
  }
}

void updateTextMeBotAlerts() {
  if (!appConfig.textMeBotEnabled) return;
  if (textMeBotSendInProgress) return;

  String eventKey = getTwilioEventKeyForCurrentState();

  if (eventKey == "NONE") {
    textMeBotCurrentEventKey = "NONE";
    return;
  }

  unsigned long repeatMs = getTwilioRepeatMs(eventKey);
  if (repeatMs == 0) return;

  bool eventChanged = eventKey != textMeBotCurrentEventKey;
  bool repeatDue = (millis() - textMeBotLastEventSendMs) >= repeatMs;

  if (eventChanged || repeatDue) {
    textMeBotCurrentEventKey = eventKey;
    textMeBotLastEventSendMs = millis();
    queueCloudAlertJob("TEXTMEBOT", "all", eventKey, false, 0);
  }
}


// ==================================================
// 24D. SELECTED PROVIDER + GENERIC API ALERT ENGINE
// ==================================================

String getApiShortError() {
  if (apiLastHttpCode >= 200 && apiLastHttpCode < 300) return "OK " + String(apiLastHttpCode);
  if (apiLastHttpCode == 0) return shortenText(apiLastStatus, 18);
  return "HTTP " + String(apiLastHttpCode);
}

bool isApiConfiguredForAlerts() {
  bool p1 = appConfig.apiParent1.length() >= 8;
  bool p2 = appConfig.apiParent2.length() >= 8;
  return appConfig.apiEndpoint.length() >= 8 && (p1 || p2);
}

bool sendApiWhatsApp(String recipient, String message) {
  if (normalizeAlertProvider(appConfig.alertProvider) != "API") {
    apiLastStatus = "API provider not selected";
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    apiLastStatus = "Wi-Fi disconnected";
    return false;
  }

  recipient = normalizeTextMeBotPhone(recipient);
  appConfig.apiEndpoint.trim();
  appConfig.apiKey.trim();

  if (appConfig.apiEndpoint.length() < 8 || recipient.length() < 8) {
    apiLastStatus = "API settings incomplete";
    return false;
  }

  String url = appConfig.apiEndpoint;
  if (url.indexOf("?") >= 0) {
    if (!url.endsWith("?") && !url.endsWith("&")) url += "&";
  } else {
    url += "?";
  }
  url += "recipient=" + urlEncode(recipient);
  url += "&phone=" + urlEncode(recipient);
  url += "&text=" + urlEncode(message);
  if (appConfig.apiKey.length() > 0) {
    url += "&apikey=" + urlEncode(appConfig.apiKey);
  }

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;
  http.setTimeout(PROVIDER_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  bool began = false;
  if (url.startsWith("https://")) {
    secureClient.setInsecure(); // Beta field-test build.
    secureClient.setHandshakeTimeout(5);
    began = http.begin(secureClient, url);
  } else {
    began = http.begin(plainClient, url);
  }

  if (!began) {
    apiLastStatus = "HTTP begin failed";
    return false;
  }

  http.addHeader("Connection", "close");

  apiSendInProgress = true;
  int code = http.GET();
  String response = readHttpResponseSnippet(http, 1200, PROVIDER_RESPONSE_READ_TIMEOUT_MS);
  http.end();
  apiSendInProgress = false;

  apiLastHttpCode = code;
  apiLastResponse = response.substring(0, 1200);

  String responseLower = response;
  responseLower.toLowerCase();

  if (code >= 200 && code < 300 && responseLower.indexOf("error") < 0) {
    apiLastStatus = "Accepted HTTP " + String(code);
    return true;
  }

  apiLastStatus = getApiShortError() + " " + shortenText(response, 24);
  return false;
}

bool sendApiMessageToConfiguredParents(String eventKey, const String &message) {
  bool ok1 = false;
  bool ok2 = false;

  bool c1Configured = appConfig.apiParent1.length() > 0;
  bool c2Configured = appConfig.apiParent2.length() > 0;
  bool c1Enabled = caregiverWantsWhatsAppEvent(1, eventKey);
  bool c2Enabled = caregiverWantsWhatsAppEvent(2, eventKey);

  if (c1Configured && c1Enabled) {
    ok1 = sendApiWhatsApp(appConfig.apiParent1, message);
    delay(250);
  }

  if (c2Configured && c2Enabled) {
    ok2 = sendApiWhatsApp(appConfig.apiParent2, message);
  }

  apiLastEvent = eventKey;

  if (c1Configured || c2Configured) {
    apiLastStatus = String("C1 ") + caregiverSendStatus(c1Configured, c1Enabled, ok1) +
                    ", C2 " + caregiverSendStatus(c2Configured, c2Enabled, ok2) +
                    ", HTTP " + String(apiLastHttpCode);
  } else if (ok1 || ok2) {
    apiLastStatus = "API accepted HTTP " + String(apiLastHttpCode);
  }

  return ok1 || ok2;
}

bool sendApiToConfiguredParents(String eventKey, bool testMode) {
  return sendApiMessageToConfiguredParents(eventKey, buildTwilioAlertMessage(eventKey, testMode));
}

void sendApiTestToTarget(String target, String eventKey) {
  if (target == "p1") {
    bool ok = sendApiWhatsApp(appConfig.apiParent1, buildTwilioAlertMessage(eventKey, true));
    apiLastEvent = "TEST_P1";
    if (ok) apiLastStatus = "Caregiver 1 test accepted HTTP " + String(apiLastHttpCode);
  } else if (target == "p2") {
    bool ok = sendApiWhatsApp(appConfig.apiParent2, buildTwilioAlertMessage(eventKey, true));
    apiLastEvent = "TEST_P2";
    if (ok) apiLastStatus = "Caregiver 2 test accepted HTTP " + String(apiLastHttpCode);
  } else {
    sendApiToConfiguredParents(eventKey, true);
  }
}

bool sendSelectedProviderToConfiguredParentsNow(String eventKey, bool testMode) {
  String provider = normalizeAlertProvider(appConfig.alertProvider);

  if (provider == "OFF") return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  lastCloudAlertAttemptMs = millis();

  bool result = false;
  if (provider == "API") result = sendApiToConfiguredParents(eventKey, testMode);
  else if (provider == "TWILIO") result = sendTwilioToConfiguredParents(eventKey, testMode);
  else if (provider == "CALLMEBOT") result = sendCallMeBotToConfiguredParents(eventKey, testMode);
  else if (provider == "TEXTMEBOT") result = sendTextMeBotToConfiguredParents(eventKey, testMode);

  return result;
}

bool queueCloudAlertJob(const String &provider, const String &target,
                        const String &eventKey, bool testMode, uint32_t episodeId) {
  if (cloudAlertQueue == nullptr) {
    cloudAlertQueueDropCount++;
    return false;
  }

  CloudAlertJob job = {};
  String resolvedProvider = provider;
  if (resolvedProvider == "SELECTED") {
    resolvedProvider = normalizeAlertProvider(appConfig.alertProvider);
  }
  String messageSnapshot = buildTwilioAlertMessage(eventKey, testMode);

  strlcpy(job.provider, resolvedProvider.c_str(), sizeof(job.provider));
  strlcpy(job.target, target.c_str(), sizeof(job.target));
  strlcpy(job.eventKey, eventKey.c_str(), sizeof(job.eventKey));
  strlcpy(job.message, messageSnapshot.c_str(), sizeof(job.message));
  job.testMode = testMode;
  job.episodeId = episodeId;

  if (xQueueSend(cloudAlertQueue, &job, 0) != pdTRUE) {
    cloudAlertQueueDropCount++;
    Serial.println("Cloud alert queue full; job dropped.");
    return false;
  }

  Serial.print("Cloud alert queued | provider=");
  Serial.print(job.provider);
  Serial.print(" target=");
  Serial.print(job.target);
  Serial.print(" event=");
  Serial.print(job.eventKey);
  Serial.print(" episode=");
  Serial.println(job.episodeId);
  return true;
}

void cloudAlertWorkerTask(void *parameter) {
  (void)parameter;
  CloudAlertJob job = {};

  for (;;) {
    if (xQueueReceive(cloudAlertQueue, &job, portMAX_DELAY) != pdTRUE) continue;

    cloudAlertTaskHeartbeat++;
    cloudAlertSendInProgress = true;
    lastCloudAlertAttemptMs = millis();
    unsigned long sendStartMs = millis();

    String provider = String(job.provider);
    String target = String(job.target);
    String eventKey = String(job.eventKey);
    String messageSnapshot = String(job.message);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Cloud worker skipped job: Wi-Fi disconnected.");
      if (job.episodeId != 0 && job.episodeId == currentCloudAlarmEpisodeId) {
        selectedAlertLastQueuedEpisodeId = 0; // retry the initial alarm after Wi-Fi recovers
      }
    } else if (provider == "SELECTED") {
      // Compatibility fallback. New jobs resolve SELECTED to the active provider before enqueueing.
      if (target == "all") sendSelectedProviderToConfiguredParentsNow(eventKey, job.testMode);
      else sendSelectedProviderTestToTarget(target, eventKey);
    } else if (provider == "API") {
      if (target == "all" || target == "both") sendApiMessageToConfiguredParents(eventKey, messageSnapshot);
      else sendApiTestToTarget(target, eventKey);
    } else if (provider == "TWILIO") {
      if (target == "all" || target == "both") sendTwilioMessageToConfiguredParents(eventKey, messageSnapshot);
      else sendTwilioTestToTarget(target, eventKey);
    } else if (provider == "CALLMEBOT") {
      if (target == "all" || target == "both") sendCallMeBotMessageToConfiguredParents(eventKey, messageSnapshot);
      else sendCallMeBotTestToTarget(target, eventKey);
    } else if (provider == "TEXTMEBOT") {
      if (target == "all" || target == "both") sendTextMeBotMessageToConfiguredParents(eventKey, messageSnapshot);
      else sendTextMeBotTestToTarget(target, eventKey);
    }

    lastCloudAlertSendDurationMs = millis() - sendStartMs;
    cloudAlertSendInProgress = false;

    // Give the main loop a scheduling opportunity after every completed cloud job.
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}


void sendSelectedProviderTestToTarget(String target, String eventKey) {
  String provider = normalizeAlertProvider(appConfig.alertProvider);

  if (target == "p3" && provider != "TEXTMEBOT") return;

  if (provider == "API") sendApiTestToTarget(target, eventKey);
  else if (provider == "TWILIO") sendTwilioTestToTarget(target, eventKey);
  else if (provider == "CALLMEBOT") sendCallMeBotTestToTarget(target, eventKey);
  else if (provider == "TEXTMEBOT") sendTextMeBotTestToTarget(target, eventKey);
}


bool selectedProviderBusy() {
  if (cloudAlertSendInProgress) return true;
  if (cloudAlertQueue != nullptr && uxQueueMessagesWaiting(cloudAlertQueue) > 0) return true;
  String provider = normalizeAlertProvider(appConfig.alertProvider);
  if (provider == "API") return apiSendInProgress;
  if (provider == "TWILIO") return twilioSendInProgress;
  if (provider == "CALLMEBOT") return callMeBotSendInProgress;
  if (provider == "TEXTMEBOT") return textMeBotSendInProgress;
  return false;
}

void updateCloudAlarmEpisodeTracker() {
  String eventKey = getTwilioEventKeyForCurrentState();

  if (eventKey == "NONE") {
    if (currentCloudAlarmEpisodeKey != "NONE") {
      currentCloudAlarmEpisodeKey = "NONE";
      acknowledgedCloudAlarmEpisodeId = 0;
    }
    return;
  }

  if (eventKey != currentCloudAlarmEpisodeKey) {
    currentCloudAlarmEpisodeKey = eventKey;
    currentCloudAlarmEpisodeId++;
    if (currentCloudAlarmEpisodeId == 0) currentCloudAlarmEpisodeId = 1;
    acknowledgedCloudAlarmEpisodeId = 0;

    Serial.print("New cloud alarm episode ");
    Serial.print(currentCloudAlarmEpisodeId);
    Serial.print(" type ");
    Serial.println(currentCloudAlarmEpisodeKey);
  }
}

bool isCurrentCloudAlarmEpisodeAcknowledged() {
  return currentCloudAlarmEpisodeKey != "NONE" &&
         currentCloudAlarmEpisodeId != 0 &&
         acknowledgedCloudAlarmEpisodeId == currentCloudAlarmEpisodeId;
}

void updateSelectedWhatsAppAlerts() {
  String provider = normalizeAlertProvider(appConfig.alertProvider);
  if (provider == "OFF") return;

  updateCloudAlarmEpisodeTracker();
  String eventKey = currentCloudAlarmEpisodeKey;

  if (eventKey == "NONE") {
    selectedAlertCurrentEventKey = "NONE";
    return;
  }

  if (!anyCaregiverWantsWhatsAppEvent(eventKey)) {
    selectedAlertCurrentEventKey = eventKey;
    selectedAlertLastQueuedEpisodeId = currentCloudAlarmEpisodeId;
    return;
  }

  unsigned long repeatMs = getTwilioRepeatMs(eventKey);
  if (repeatMs == 0) return;

  bool initialForEpisodeDue = selectedAlertLastQueuedEpisodeId != currentCloudAlarmEpisodeId;
  bool repeatDue = !initialForEpisodeDue &&
                   (millis() - selectedAlertLastEventSendMs) >= repeatMs;

  // Acknowledgement never cancels the first message for an episode. It suppresses repeats only.
  if (!initialForEpisodeDue && isCurrentCloudAlarmEpisodeAcknowledged()) return;
  if (!initialForEpisodeDue && selectedProviderBusy()) return;
  if (!initialForEpisodeDue && cloudAlertCooldownActive()) return;

  if (initialForEpisodeDue || repeatDue) {
    if (queueCloudAlertJob("SELECTED", "all", eventKey, false, currentCloudAlarmEpisodeId)) {
      selectedAlertCurrentEventKey = eventKey;
      selectedAlertLastEventSendMs = millis();
      selectedAlertLastQueuedEpisodeId = currentCloudAlarmEpisodeId;
    }
  }
}


void updatePhoneBatteryReminderAlert() {
  if (!appConfig.phoneBatteryAlertEnabled) return;
  if (normalizeAlertProvider(appConfig.alertProvider) == "OFF") return;
  if (selectedProviderBusy()) return;

  if (phoneBattery < 0 || phoneBattery > appConfig.phoneBatteryAlertPercent) {
    phoneBatteryReminderKey = "NONE";
    return;
  }

  String eventKey = "PHONE_BATTERY_LOW";
  unsigned long repeatMs = getTwilioRepeatMs(eventKey);
  if (repeatMs == 0) return;

  bool eventChanged = phoneBatteryReminderKey != eventKey;
  bool repeatDue = (millis() - phoneBatteryLastAlertSendMs) >= repeatMs;

  if (eventChanged || repeatDue) {
    phoneBatteryReminderKey = eventKey;
    phoneBatteryLastAlertSendMs = millis();
    queueCloudAlertJob("SELECTED", "all", eventKey, false, 0);
  }
}

void updateSensorExpiryReminderAlert() {
  if (!appConfig.sensorExpiryReminderEnabled) return;
  if (normalizeAlertProvider(appConfig.alertProvider) == "OFF") return;
  if (selectedProviderBusy()) return;

  long leftHours = getSensorLeftHours();
  if (leftHours < 0 || leftHours == -999999L) {
    sensorExpiryReminderKey = "NONE";
    return;
  }

  long thresholdHours = (long)appConfig.sensorExpiryReminderDays * 24L;
  if (leftHours > thresholdHours) {
    sensorExpiryReminderKey = "NONE";
    return;
  }

  long daysLeft = (leftHours + 23L) / 24L;
  String eventKey = "SENSOR_EXPIRY_D" + String(daysLeft);
  unsigned long repeatMs = getTwilioRepeatMs("SENSOR_EXPIRY");
  if (repeatMs == 0) return;

  bool dayChanged = sensorExpiryReminderKey != eventKey;
  bool repeatDue = (millis() - sensorExpiryLastAlertSendMs) >= repeatMs;

  if (dayChanged || repeatDue) {
    sensorExpiryReminderKey = eventKey;
    sensorExpiryLastAlertSendMs = millis();
    queueCloudAlertJob("SELECTED", "all", "SENSOR_EXPIRY", false, 0);
  }
}

void updateCaregiverReminderAlerts() {
  // Glucose and no-data alerts have priority.
  // Do not stack battery/sensor reminder HTTP calls on top of active alarm sends.
  if (getTwilioEventKeyForCurrentState() != "NONE") return;
  if (cloudAlertCooldownActive()) return;

  updatePhoneBatteryReminderAlert();

  if (cloudAlertCooldownActive()) return;
  updateSensorExpiryReminderAlert();
}



void drawSensorDataScreen() {
  applyDisplayContrast();
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);
  drawWiFiSignalBars();

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.setCursor(4, 11);
  u8g2.print("SENSOR DATA");
  u8g2.drawLine(0, 15, 127, 15);

  u8g2.setCursor(4, 27);
  u8g2.print("Age:");
  u8g2.print(shortenText(getSageDisplayText(), 14));

  u8g2.setCursor(4, 39);
  u8g2.print("Left:");
  u8g2.print(shortenText(getSensorLeftText(), 14));

  u8g2.setCursor(4, 51);
  u8g2.print("Wear:");
  u8g2.print(getActiveSensorWearDays());
  u8g2.print("d ");
  u8g2.print(appConfig.sensorAutoRead ? "Auto" : "Manual");

  u8g2.setCursor(4, 63);
  u8g2.print("Src:");
  u8g2.print(shortenText(getSageSourceText(), 15));

  u8g2.sendBuffer();
}


void drawInsulinScreen() {
  applyDisplayContrast();
  updateInsulinRemainingEstimate();

  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);
  drawWiFiSignalBars();

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.setCursor(4, 11);
  u8g2.print("INSULIN");
  u8g2.drawLine(0, 15, 127, 15);

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.setCursor(4, 27);
  u8g2.print("Dose:");
  if (lastBolusUnits > 0.0) {
    u8g2.print(lastBolusUnits, 1);
    u8g2.print("u");
  } else {
    u8g2.print("None");
  }

  u8g2.setCursor(68, 27);
  u8g2.print("Left:");
  u8g2.print(insulinRemainingUnits, 1);
  u8g2.print("u");

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(4, 40);
  u8g2.print("Last: ");
  u8g2.print(shortenText(formatTimestampShort(lastBolusTimeMs), 17));

  u8g2.setCursor(4, 50);
  u8g2.print(shortenText(getBolusAgeText(), 24));

  u8g2.setCursor(4, 61);
  u8g2.print(shortenText(insulinStatus + " " + lastBolusType, 24));

  u8g2.sendBuffer();
}

void drawCobIobTotalScreen() {
  applyDisplayContrast();
  updateInsulinRemainingEstimate();

  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.setCursor(4, 11);
  u8g2.print("COB/IOB TOTAL");
  u8g2.drawLine(0, 15, 127, 15);

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(4, 27);
  u8g2.print("NOW  I:");
  u8g2.print(insulinRemainingUnits, 2);
  u8g2.print("u C:");
  if (nightscoutCobValid) u8g2.print(nightscoutCobGrams, 0);
  else u8g2.print("--");
  u8g2.print("g");

  u8g2.setCursor(4, 39);
  u8g2.print("TODAY I:");
  u8g2.print(todayBolusUnits, 1);
  u8g2.print("u C:");
  u8g2.print(todayCarbsGrams, 0);
  u8g2.print("g");

  u8g2.setCursor(4, 51);
  u8g2.print("YDAY  I:");
  u8g2.print(yesterdayBolusUnits, 1);
  u8g2.print("u C:");
  u8g2.print(yesterdayCarbsGrams, 0);
  u8g2.print("g");

  u8g2.setCursor(4, 61);
  u8g2.print("T# I");
  u8g2.print(todayBolusCount);
  u8g2.print(" C");
  u8g2.print(todayCarbCount);
  u8g2.print("  Y# I");
  u8g2.print(yesterdayBolusCount);
  u8g2.print(" C");
  u8g2.print(yesterdayCarbCount);

  u8g2.sendBuffer();
}

void drawTwilioScreen() {
  applyDisplayContrast();
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);
  drawWiFiSignalBars();

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.setCursor(5, 10);
  u8g2.print("WHATSAPP ALERTS");
  u8g2.drawLine(0, 13, 127, 13);

  String provider = normalizeAlertProvider(appConfig.alertProvider);
  String shownStatus = "";
  String shownEvent = "";
  int shownHttp = 0;

  if (provider == "API") {
    shownStatus = getApiShortError();
    shownEvent = apiLastEvent;
    shownHttp = apiLastHttpCode;
  } else if (provider == "TWILIO") {
    shownStatus = getTwilioShortError();
    shownEvent = twilioLastEvent;
    shownHttp = twilioLastHttpCode;
  } else if (provider == "CALLMEBOT") {
    shownStatus = getCallMeBotShortError();
    shownEvent = callMeBotLastEvent;
    shownHttp = callMeBotLastHttpCode;
  } else if (provider == "TEXTMEBOT") {
    shownStatus = getTextMeBotShortError();
    shownEvent = textMeBotLastEvent;
    shownHttp = textMeBotLastHttpCode;
  } else {
    shownStatus = "Disabled";
    shownEvent = "None";
    shownHttp = 0;
  }

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(5, 24);
  u8g2.print("Prov:");
  u8g2.print(shortenText(getAlertProviderLabel(provider), 12));

  if (provider == "TEXTMEBOT") {
    u8g2.setCursor(5, 35);
    u8g2.print("C1:");
    u8g2.print(maskPhoneForOled(appConfig.textMeBotParent1));

    u8g2.setCursor(64, 35);
    u8g2.print("C2:");
    u8g2.print(maskPhoneForOled(appConfig.textMeBotParent2));

    u8g2.setCursor(5, 46);
    u8g2.print("C3:");
    u8g2.print(maskPhoneForOled(appConfig.textMeBotParent3));
    u8g2.setCursor(64, 46);
    u8g2.print("HTTP:");
    u8g2.print(shownHttp);
  } else {
    u8g2.setCursor(5, 35);
    u8g2.print("C1:");
    if (provider == "API") u8g2.print(maskPhoneForOled(appConfig.apiParent1));
    else if (provider == "TWILIO") u8g2.print(maskPhoneForOled(appConfig.twilioParent1));
    else if (provider == "CALLMEBOT") u8g2.print(maskPhoneForOled(appConfig.callMeBotParent1));
    else u8g2.print("Off");

    u8g2.setCursor(64, 35);
    u8g2.print("C2:");
    if (provider == "API") u8g2.print(maskPhoneForOled(appConfig.apiParent2));
    else if (provider == "TWILIO") u8g2.print(maskPhoneForOled(appConfig.twilioParent2));
    else if (provider == "CALLMEBOT") u8g2.print(maskPhoneForOled(appConfig.callMeBotParent2));
    else u8g2.print("Off");

    u8g2.setCursor(5, 46);
    u8g2.print("HTTP:");
    u8g2.print(shownHttp);
    u8g2.print(" Ev:");
    u8g2.print(shortenText(shownEvent, 8));
  }

  u8g2.setCursor(5, 57);
  u8g2.print(shortenText(shownStatus, 24));

  u8g2.sendBuffer();
}

// ==================================================
// 24. SAFE SIREN CONTROL
// ==================================================

void buzzerOff() {
  forceBuzzerHardwareOff();
}

void resetAudioAlarmEpisode(int newAlarmType) {
  activeAudioAlarmType = newAlarmType;
  audioAlarmEpisode++;
  audioCycleStartMs = millis();
  lastBuzzerToggle = millis();
  buzzerIsOn = false;

  uint32_t seed = (uint32_t)ESP.getEfuseMac();
  seed ^= ((uint32_t)newAlarmType * 0x45D9F3BU);
  seed ^= (audioAlarmEpisode * 0x9E3779B9UL);
  seed ^= (seed >> 16);
  activeAudioVariant = seed % AUDIO_PATTERN_VARIANTS;
}

int getCurrentAudioAlarmType() {
  if (!isDataFresh()) {
    // Do not start a no-data buzzer before the first successful glucose read.
    // Once at least one valid value has existed, stale data can trigger the no-data audio pattern.
    if (glucoseMmol > 0) return AUDIO_ALARM_NO_DATA;
    return ALARM_NONE;
  }

  return getCurrentAlarmLevel();
}

void getFixedAudioPattern(int alarmType, int &frequency, unsigned long &onTime, unsigned long &offTime) {
  if (alarmType == ALARM_URGENT_LOW) {
    frequency = 500;
    onTime = 300;
    offTime = 300;
  }
  else if (alarmType == ALARM_LOW) {
    frequency = 700;
    onTime = 250;
    offTime = 750;
  }
  else if (alarmType == ALARM_HIGH) {
    frequency = 1800;
    onTime = 150;
    offTime = 850;
  }
  else if (alarmType == ALARM_URGENT_HIGH) {
    frequency = 2200;
    onTime = 150;
    offTime = 250;
  }
  else if (alarmType == AUDIO_ALARM_NO_DATA) {
    frequency = 1000;
    onTime = 180;
    offTime = 2200;
  }
  else {
    frequency = 0;
    onTime = 0;
    offTime = 1000;
  }
}

void getSeededRandomAudioPattern(int alarmType, int &frequency, unsigned long &onTime, unsigned long &offTime) {
  uint8_t v = activeAudioVariant % AUDIO_PATTERN_VARIANTS;

  if (alarmType == ALARM_URGENT_LOW) {
    frequency = 470 + (v % 5) * 35;
    onTime = 180 + (v % 6) * 30;       // 180-330 ms
    offTime = 100 + ((v * 7) % 8) * 25; // 100-275 ms
  }
  else if (alarmType == ALARM_LOW) {
    frequency = 640 + (v % 6) * 25;
    onTime = 120 + (v % 6) * 25;       // 120-245 ms
    offTime = 180 + ((v * 5) % 11) * 25; // 180-430 ms
  }
  else if (alarmType == ALARM_HIGH) {
    frequency = 1550 + (v % 7) * 45;
    onTime = 150 + (v % 7) * 25;       // 150-300 ms
    offTime = 500 + ((v * 9) % 15) * 50; // 500-1200 ms
  }
  else if (alarmType == ALARM_URGENT_HIGH) {
    frequency = 2050 + (v % 6) * 45;
    onTime = 150 + (v % 5) * 25;       // 150-250 ms
    offTime = 200 + ((v * 7) % 7) * 35; // 200-410 ms
  }
  else if (alarmType == AUDIO_ALARM_NO_DATA) {
    frequency = 900 + (v % 5) * 30;
    onTime = 120 + (v % 5) * 20;       // 120-200 ms
    offTime = 1400 + ((v * 11) % 12) * 80; // 1400-2280 ms
  }
  else {
    frequency = 0;
    onTime = 0;
    offTime = 1000;
  }
}

void beepPattern(int frequency, unsigned long onTime, unsigned long offTime) {
#if ENABLE_SIREN_HARDWARE
  if (frequency <= 0 || onTime == 0) {
    buzzerOff();
    return;
  }

  unsigned long now = millis();

  if (buzzerIsOn) {
    if (now - lastBuzzerToggle >= onTime) {
      noTone(BUZZER_PIN);
      buzzerIsOn = false;
      lastBuzzerToggle = now;
    }
  } else {
    if (now - lastBuzzerToggle >= offTime) {
      tone(BUZZER_PIN, frequency);
      buzzerIsOn = true;
      lastBuzzerToggle = now;
    }
  }
#else
  buzzerIsOn = false;
#endif
}

void updateBuzzer() {
#if ENABLE_SIREN_HARDWARE

  if (peripheralRestartInProgress || !oledRuntimeReady || !alarmAudioArmed ||
      (long)(millis() - alarmAudioArmAtMs) < 0) {
    buzzerOff();
    return;
  }

  if (!sirenEnabled || sirenSilenced || !appConfig.alarmSoundEnabled) {
    buzzerOff();
    return;
  }

  int audioAlarmType = getCurrentAudioAlarmType();

  if (audioAlarmType == ALARM_NONE) {
    activeAudioAlarmType = ALARM_NONE;
    buzzerOff();
    return;
  }

  // The user may independently enable or disable local sound for each alarm class.
  // Visual alarms and WhatsApp routing continue even when this local sound is disabled.
  if (!isLocalSoundEnabledForAlarmType(audioAlarmType)) {
    activeAudioAlarmType = ALARM_NONE;
    buzzerOff();
    return;
  }

  if (audioAlarmType != activeAudioAlarmType) {
    resetAudioAlarmEpisode(audioAlarmType);
  }

  unsigned long now = millis();
  unsigned long activeWindowMs = AUDIO_ACTIVE_WINDOW_MS;
  unsigned long quietWindowMs = AUDIO_QUIET_WINDOW_MS;
  getAlarmMacroWindow(audioAlarmType, activeWindowMs, quietWindowMs);
  unsigned long cycleMs = activeWindowMs + quietWindowMs;

  if (quietWindowMs > 0 && cycleMs > 0) {
    unsigned long cyclePos = (now - audioCycleStartMs) % cycleMs;
    if (cyclePos >= activeWindowMs) {
      buzzerOff();
      return;
    }
  }

  int frequency = 0;
  unsigned long onTime = 0;
  unsigned long offTime = 1000;

  if (appConfig.randomAudioEnabled) {
    getSeededRandomAudioPattern(audioAlarmType, frequency, onTime, offTime);
  } else {
    getFixedAudioPattern(audioAlarmType, frequency, onTime, offTime);
  }

  beepPattern(frequency, onTime, offTime);

#else
  buzzerIsOn = false;
#endif
}


void getAlarmMacroWindow(int alarmType, unsigned long &activeWindowMs,
                         unsigned long &quietWindowMs) {
  if (alarmType == ALARM_URGENT_LOW) {
    // Urgent low must not have a long fatigue silence.
    activeWindowMs = 30000UL;
    quietWindowMs = 0;
  } else if (alarmType == ALARM_URGENT_HIGH) {
    activeWindowMs = 30000UL;
    quietWindowMs = 5000UL;
  } else if (alarmType == AUDIO_ALARM_NO_DATA) {
    activeWindowMs = 10000UL;
    quietWindowMs = 50000UL;
  } else {
    activeWindowMs = AUDIO_ACTIVE_WINDOW_MS;
    quietWindowMs = AUDIO_QUIET_WINDOW_MS;
  }
}

void alarmAudioWorkerTask(void *parameter) {
  (void)parameter;
  for (;;) {
    updateBuzzer();
    alarmAudioTaskHeartbeat++;
    vTaskDelay(pdMS_TO_TICKS(ALARM_AUDIO_TASK_PERIOD_MS));
  }
}

void startBackgroundTasks() {
  if (cloudAlertQueue == nullptr) {
    cloudAlertQueue = xQueueCreate(CLOUD_ALERT_QUEUE_LENGTH, sizeof(CloudAlertJob));
  }

  if (cloudAlertQueue == nullptr) {
    Serial.println("ERROR: Cloud alert queue creation failed.");
  } else if (cloudAlertTaskHandle == nullptr) {
    BaseType_t cloudTaskResult = xTaskCreatePinnedToCore(
      cloudAlertWorkerTask,
      "LeahCloudAlert",
      16384,
      nullptr,
      1,
      &cloudAlertTaskHandle,
      0
    );
    if (cloudTaskResult != pdPASS) {
      cloudAlertTaskHandle = nullptr;
      Serial.println("ERROR: Cloud alert task creation failed.");
    }
  }

  if (alarmAudioTaskHandle == nullptr) {
    BaseType_t audioTaskResult = xTaskCreatePinnedToCore(
      alarmAudioWorkerTask,
      "LeahAlarmAudio",
      3072,
      nullptr,
      3,
      &alarmAudioTaskHandle,
      0
    );
    if (audioTaskResult != pdPASS) {
      alarmAudioTaskHandle = nullptr;
      Serial.println("ERROR: Alarm audio task creation failed.");
    }
  }
}


// ==================================================
// 25. BUTTON HANDLING
// ==================================================

bool isTwilioConfiguredForAlerts() {
  if (appConfig.twilioSid.length() < 10) return false;
  if (appConfig.twilioToken.length() < 8) return false;
  if (appConfig.twilioFrom.length() == 0) return false;
  if (appConfig.twilioParent1.length() == 0 && appConfig.twilioParent2.length() == 0) return false;
  return true;
}

void clearAlarmMute(const char *reason) {
  if (!sirenSilenced) return;

  sirenSilenced = false;
  mutedAudioAlarmType = ALARM_NONE;
  activeAudioAlarmType = ALARM_NONE;

  Serial.print("Alarm mute cleared: ");
  Serial.println(reason);

  if (currentScreen == SCREEN_MAIN) drawGlucoseScreen();
}

void updateAlarmMuteExpiry() {
  if (!sirenSilenced) return;

  int currentAudioAlarmType = getCurrentAudioAlarmType();

  // Episode-based mute: once glucose/no-data leaves the alarm state, clear the mute.
  // If the alarm returns one minute later, it is a new episode and must sound again.
  if (currentAudioAlarmType == ALARM_NONE) {
    clearAlarmMute("alarm state cleared");
    return;
  }

  // Safety: if the alarm class changes while muted, treat it as a new alarm episode.
  // Example: HIGH becomes URGENT HIGH, or LOW becomes URGENT LOW.
  if (mutedAudioAlarmType != ALARM_NONE && currentAudioAlarmType != mutedAudioAlarmType) {
    clearAlarmMute("alarm type changed");
    return;
  }

  if (millis() - silenceStartTime >= activeSilenceDurationMs) {
    clearAlarmMute("mute timer expired");
  }
}

void handleAudioButton() {
  // Debounced, non-blocking state machine. GPIO32 is the physical RIGHT button
  // on this enclosure, even though earlier builds labelled it as the LEFT button.
  static bool rawLastState = HIGH;
  static bool stableState = HIGH;
  static unsigned long rawStateChangedAt = 0;
  static unsigned long pressStartedAt = 0;
  static bool pressActive = false;
  static bool soundWasEnabledAtPress = true;
  static bool longPressActionHandled = false;
  static bool alarmAcknowledgedAtPress = false;

  const unsigned long now = millis();
  const bool rawState = digitalRead(BUTTON_AUDIO_PIN);

  if (rawState != rawLastState) {
    rawLastState = rawState;
    rawStateChangedAt = now;
  }

  // Accept a state transition only after it has remained stable for the debounce period.
  if (rawState != stableState && now - rawStateChangedAt >= AUDIO_BUTTON_DEBOUNCE_MS) {
    stableState = rawState;

    if (stableState == LOW) {
      // Button pressed.
      pressActive = true;
      pressStartedAt = now;
      soundWasEnabledAtPress = appConfig.alarmSoundEnabled;
      longPressActionHandled = false;
      alarmAcknowledgedAtPress = false;

      const int level = getCurrentAlarmLevel();
      const bool alarmActive = !isDataFresh() || level != ALARM_NONE;

      // Preserve the normal alarm-control action: the RIGHT button immediately
      // acknowledges the current alarm episode and snoozes only the local buzzer.
      if (alarmActive) {
        updateCloudAlarmEpisodeTracker();
        sirenSilenced = true;
        mutedAudioAlarmType = getCurrentAudioAlarmType();
        silenceStartTime = now;
        activeSilenceDurationMs = getMuteDurationForCurrentStateMs();
        if (currentCloudAlarmEpisodeKey != "NONE") {
          acknowledgedCloudAlarmEpisodeId = currentCloudAlarmEpisodeId;
        }
        alarmAcknowledgedAtPress = true;

        Serial.print("Alarm acknowledged and snoozed from RIGHT audio button for minutes: ");
        Serial.println(activeSilenceDurationMs / 60000UL);

        drawMessage("Alarm snoozed",
                    String(activeSilenceDurationMs / 60000UL) + " minutes",
                    "RIGHT button",
                    "Hold 10s = OFF");
      }
    } else {
      // Button released.
      if (pressActive) {
        if (!longPressActionHandled && !alarmAcknowledgedAtPress) {
          // A short press outside an alarm no longer toggles the master by accident.
          if (appConfig.alarmSoundEnabled) {
            drawMessage("Buzzer remains ON",
                        "Hold RIGHT button",
                        "10 seconds for OFF",
                        "");
          } else {
            drawMessage("Buzzer remains OFF",
                        "Hold RIGHT button",
                        "3 seconds for ON",
                        "");
          }
        } else {
          currentScreen = SCREEN_MAIN;
          drawGlucoseScreen();
        }
      }

      pressActive = false;
      longPressActionHandled = false;
      alarmAcknowledgedAtPress = false;
    }
  }

  if (!pressActive || stableState != LOW || longPressActionHandled) return;

  const unsigned long heldMs = now - pressStartedAt;

  if (soundWasEnabledAtPress && heldMs >= AUDIO_BUTTON_MASTER_OFF_HOLD_MS) {
    // Deliberate 10-second hold while ON: disable and persist the buzzer master.
    appConfig.alarmSoundEnabled = false;
    sirenEnabled = false;
    sirenSilenced = false;
    mutedAudioAlarmType = ALARM_NONE;
    activeAudioAlarmType = ALARM_NONE;
    activeSilenceDurationMs = 0;
    forceBuzzerHardwareOff();
    saveConfig();
    longPressActionHandled = true;

    Serial.println("Buzzer master switched OFF by 10-second RIGHT-button hold.");
    drawMessage("Buzzer master OFF",
                "Saved to memory",
                "RIGHT button held",
                "10 seconds");
  } else if (!soundWasEnabledAtPress && heldMs >= AUDIO_BUTTON_MASTER_ON_HOLD_MS) {
    // Deliberate 3-second hold while OFF: enable and persist the buzzer master.
    appConfig.alarmSoundEnabled = true;
    sirenEnabled = true;
    sirenSilenced = false;
    mutedAudioAlarmType = ALARM_NONE;
    activeAudioAlarmType = ALARM_NONE;
    activeSilenceDurationMs = 0;
    audioCycleStartMs = now;
    lastBuzzerToggle = now;
    forceBuzzerHardwareOff();
    saveConfig();
    longPressActionHandled = true;

    Serial.println("Buzzer master switched ON by 3-second RIGHT-button hold.");
    drawMessage("Buzzer master ON",
                "Saved to memory",
                "RIGHT button held",
                "3 seconds");
  }
}

void showCurrentScreen() {
  lastScreenInteraction = millis();

  if (currentScreen == SCREEN_MAIN) {
    drawGlucoseScreen();
  } else if (currentScreen == SCREEN_TREND) {
    drawTrendScreen();
  } else if (currentScreen == SCREEN_SENSOR_DATA) {
    drawSensorDataScreen();
  } else if (currentScreen == SCREEN_INSULIN) {
    drawInsulinScreen();
  } else if (currentScreen == SCREEN_COB_IOB_TOTAL) {
    drawCobIobTotalScreen();
  } else if (currentScreen == SCREEN_DIAGNOSTICS) {
    drawDiagnosticsScreen();
  } else if (currentScreen == SCREEN_TWILIO) {
    drawTwilioScreen();
  }
}

void cycleToNextScreen() {
  if (currentScreen == SCREEN_MAIN) {
    currentScreen = SCREEN_TREND;
  } else if (currentScreen == SCREEN_TREND) {
    currentScreen = SCREEN_SENSOR_DATA;
  } else if (currentScreen == SCREEN_SENSOR_DATA) {
    currentScreen = SCREEN_INSULIN;
  } else if (currentScreen == SCREEN_INSULIN) {
    currentScreen = SCREEN_COB_IOB_TOTAL;
  } else if (currentScreen == SCREEN_COB_IOB_TOTAL) {
    currentScreen = SCREEN_DIAGNOSTICS;
  } else if (currentScreen == SCREEN_DIAGNOSTICS) {
    currentScreen = SCREEN_TWILIO;
  } else {
    currentScreen = SCREEN_MAIN;
  }

  showCurrentScreen();
}

void handleScreenCycleButton() {
  static bool lastState = HIGH;
  static unsigned long lastPressTime = 0;

  bool state = digitalRead(BUTTON_SCREEN_PIN);

  if (lastState == HIGH && state == LOW && millis() - lastPressTime > 300) {
    lastPressTime = millis();
    Serial.println("Middle screen-cycle button pressed");
    cycleToNextScreen();
  }

  lastState = state;
}

void handleButtons() {
  handleAudioButton();
  handleScreenCycleButton();
  updateAlarmMuteExpiry();
}

// ==================================================
// 26. SCREEN TIMEOUT
// ==================================================

void handleScreenTimeout() {
  if (currentScreen != SCREEN_MAIN) {
    if (millis() - lastScreenInteraction > screenTimeout) {
      currentScreen = SCREEN_MAIN;
      drawGlucoseScreen();
    }
  }
}


// ==================================================
// 27. UPDATE LATEST GLUCOSE
// ==================================================

void noteNightscoutReadResult(bool ok) {
  if (ok) {
    consecutiveNightscoutFailures = 0;
    return;
  }

  if (consecutiveNightscoutFailures < 250) {
    consecutiveNightscoutFailures++;
  }

  Serial.print("Consecutive Nightscout failures: ");
  Serial.println(consecutiveNightscoutFailures);

  if (WiFi.status() == WL_CONNECTED &&
      consecutiveNightscoutFailures >= NIGHTSCOUT_FAILS_BEFORE_WIFI_RECOVERY &&
      millis() - lastNightscoutRecoveryMs >= NIGHTSCOUT_WIFI_RECOVERY_COOLDOWN_MS) {

    Serial.println("Nightscout recovery: forcing Wi-Fi reconnect after repeated HTTPS failures.");
    lastStatus = "NS recovery reconnect";
    lastNightscoutRecoveryMs = millis();
    consecutiveNightscoutFailures = 0;

    WiFi.disconnect(false);
    delay(100);
    WiFi.reconnect();
  }
}

void updateLatestGlucose() {
  Serial.println("Starting glucose update...");

  bool ok = readNightscoutEntries();

  if (ok) {
    Serial.println("Glucose update OK.");
  } else {
    Serial.print("Glucose update failed: ");
    Serial.println(lastStatus);
  }

  noteNightscoutReadResult(ok);

  if (currentScreen == SCREEN_MAIN) {
    drawGlucoseScreen();
  } else if (currentScreen == SCREEN_SENSOR_DATA) {
    drawSensorDataScreen();
  }

  Serial.println("Glucose update finished.");
}


// ==================================================
// 28. UPDATE PHONE BATTERY
// ==================================================

void updatePhoneBatteryIfDue() {
  if (millis() - lastBatteryUpdate >= batteryUpdateInterval) {
    readNightscoutDeviceStatus();

    if (currentScreen == SCREEN_MAIN) {
      drawGlucoseScreen();
    } else if (currentScreen == SCREEN_SENSOR_DATA) {
      drawSensorDataScreen();
    }

    lastBatteryUpdate = millis();
  }
}

void updateSageIfDue() {
  if (millis() - lastSageUpdate >= sageUpdateInterval) {
    readNightscoutSageInfo();

    if (currentScreen == SCREEN_SENSOR_DATA) {
      drawSensorDataScreen();
    }

    lastSageUpdate = millis();
  }
}




// ==================================================
// 28B. UPDATE INSULIN / BOLUS
// ==================================================

void updateInsulinIfDue() {
  if (millis() - lastInsulinUpdate >= insulinUpdateInterval) {
    readNightscoutIobCobInfo();
    readNightscoutBolusInfo();
    lastInsulinUpdate = millis();
  } else {
    updateInsulinRemainingEstimate();
  }

  static unsigned long lastInsulinScreenRefreshMs = 0;
  if (millis() - lastInsulinScreenRefreshMs >= 1000UL) {
    if (currentScreen == SCREEN_INSULIN) {
      drawInsulinScreen();
      lastInsulinScreenRefreshMs = millis();
    } else if (currentScreen == SCREEN_COB_IOB_TOTAL) {
      drawCobIobTotalScreen();
      lastInsulinScreenRefreshMs = millis();
    }
  }
}


// ==================================================
// 29. SETUP
// ==================================================

void setup() {
  // Clamp GPIO25 before Serial startup or any delay. This removes the faint tone
  // caused by a floating pin or retained LEDC state during a warm restart.
  peripheralRestartInProgress = false;
  alarmAudioArmed = false;
  noTone(BUZZER_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.begin(115200);
  delay(250);

  loadConfig();
  normalizeWiFiProfilesInMemory();
  Serial.println("Loaded Leah Wi-Fi profiles from NVS:");
  Serial.print("  Slot 1: "); Serial.println(appConfig.wifiSsid1.length() ? appConfig.wifiSsid1 : "(empty)");
  Serial.print("  Slot 2: "); Serial.println(appConfig.wifiSsid2.length() ? appConfig.wifiSsid2 : "(empty)");
  Serial.print("  Slot 3: "); Serial.println(appConfig.wifiSsid3.length() ? appConfig.wifiSsid3 : "(empty)");
  printFirmwareInfo();

  pinMode(BUTTON_AUDIO_PIN, INPUT_PULLUP);
  pinMode(BUTTON_SCREEN_PIN, INPUT_PULLUP);
  forceBuzzerHardwareOff();

  Serial.println("GPIO map: OLED SDA=22, OLED SCL=21, RIGHT audio button=GPIO32, middle screen button=GPIO33, GPIO27 unused, buzzer/siren=GPIO25 enabled.");
  Serial.println("Warm-restart recovery: buzzer clamped LOW and OLED I2C bus cleared.");

  initializeOledSafely();

  if (factoryResetBootMarker == FACTORY_RESET_BOOT_MAGIC) {
    factoryResetBootMarker = 0;
    drawMessage("Factory Defaults", "NVS was cleared", "Setup hotspot", "192.168.4.1");
    delay(2500);
  }

  showPendingOtaBootConfirmation();
  showLeah2RDisplaysStartupSequence();
  applyConfigToRuntime();

  drawMessage("ESP32 CGM", "Starting...", "OLED OK", "");

  connectWiFiWithManager();
  setupTime();

  // Time is now available, so apply the day/night OLED contrast immediately.
  // Earlier versions only applied schedule after the next periodic check.
  currentAppliedContrast = -1; // force contrast apply after NTP
  applyDisplayContrast();

  startLocalMdnsAlias();
  startLocalWebServer();

  drawMessage("Web Setup", "IP:" + WiFi.localIP().toString(), "Alias:", getLocalMdnsHost());
  delay(1500);

  drawMessage("Firmware", "FW: " + String(FIRMWARE_VERSION), "Checking update", "");
  checkFirmwareUpdateManifest(false);

  readNightscoutLimits();
  readNightscoutDeviceStatus();
  readNightscoutSageInfo();
  readNightscoutIobCobInfo();
  readNightscoutBolusInfo();
  updateLatestGlucose();

  lastGlucoseUpdate = millis();
  lastBatteryUpdate = millis();
  lastSageUpdate = millis();
  lastInsulinUpdate = millis();
  lastScreenInteraction = millis();

  // Draw a confirmed live frame before the alarm task is allowed to touch GPIO25.
  currentScreen = SCREEN_MAIN;
  drawGlucoseScreen();
  alarmAudioArmAtMs = millis() + ALARM_AUDIO_BOOT_HOLDOFF_MS;
  alarmAudioArmed = true;

  startBackgroundTasks();
  Serial.println("Background cloud-alert and alarm-audio tasks started on core 0.");
  Serial.println("Alarm audio armed after 3-second post-display boot holdoff.");
  Serial.println("Setup complete.");
}


// ==================================================
// 30. MAIN LOOP - SAFE VERSION
// ==================================================

void loop() {
  unsigned long loopPassNowMs = millis();
  if (lastMainLoopPassMs != 0) {
    unsigned long loopGapMs = loopPassNowMs - lastMainLoopPassMs;
    if (loopGapMs > mainLoopMaxDelayMs) mainLoopMaxDelayMs = loopGapMs;
  }
  lastMainLoopPassMs = loopPassNowMs;

  printHeartbeat();

  handleButtons();
  handleScreenTimeout();

  // --------------------------------------------------
  // Safe Wi-Fi reconnect.
  // Do not open WiFiManager inside normal operation.
  // --------------------------------------------------
  static unsigned long lastWiFiLostScreenUpdate = 0;
  unsigned long nowMs = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDisconnectedSinceMs == 0) {
      wifiDisconnectedSinceMs = nowMs;
      lastWiFiRecoveryAction = "Grace period";
      Serial.println("Wi-Fi status changed from connected; starting transient-loss grace period.");
    }

    unsigned long disconnectedForMs = nowMs - wifiDisconnectedSinceMs;
    bool saveHoldoffActive = wifiRecoveryHoldoffUntilMs != 0 &&
                             (long)(wifiRecoveryHoldoffUntilMs - nowMs) > 0;

    // First allow the driver to recover without scanning or switching profiles.
    // WiFi.reconnect() preserves the current station context and is far less
    // disruptive than WiFi.disconnect(true, false) plus a network scan.
    if (disconnectedForMs >= WIFI_TRANSIENT_GRACE_MS &&
        nowMs - lastWiFiSoftReconnectAttemptMs >= WIFI_SOFT_RECONNECT_INTERVAL_MS) {
      lastWiFiSoftReconnectAttemptMs = nowMs;
      lastWiFiRecoveryAction = "Soft reconnect";
      Serial.println("Wi-Fi still down after grace period; requesting non-destructive reconnect.");
      WiFi.reconnect();
    }

    // Only after a sustained outage do a full scan and profile recovery. This
    // operation intentionally tears down the station and therefore must never
    // run for a one-frame/transient status change.
    if (!saveHoldoffActive &&
        disconnectedForMs >= WIFI_PROFILE_RECOVERY_DELAY_MS &&
        nowMs - lastWiFiProfileRecoveryAttemptMs >= WIFI_PROFILE_RECOVERY_INTERVAL_MS) {
      lastWiFiProfileRecoveryAttemptMs = nowMs;
      lastWiFiRecoveryAction = "Stored profile recovery";
      Serial.println("Wi-Fi sustained outage; trying stored profiles.");
      if (!connectToStoredWiFiNetworks(false)) {
        WiFi.reconnect();
      }
    }

    if (currentScreen == SCREEN_MAIN &&
        disconnectedForMs >= WIFI_TRANSIENT_GRACE_MS &&
        nowMs - lastWiFiLostScreenUpdate > 3000UL) {
      String lastLine = "Please wait";
      if (glucoseMmol > 0) {
        updateDataAge();
        lastLine = "Last:" + formatGlucoseDisplay(glucoseMmol) + getGlucoseUnitLabel() + " " + String(ageMinutes) + "m";
      }
      drawMessage("WiFi Lost", saveHoldoffActive ? "Waiting..." : "Reconnecting...", lastLine, "");
      lastWiFiLostScreenUpdate = nowMs;
    }

    delay(20);
    return;
  }

  if (wifiDisconnectedSinceMs != 0) {
    Serial.print("Wi-Fi recovered after ms: ");
    Serial.println(nowMs - wifiDisconnectedSinceMs);
    wifiDisconnectedSinceMs = 0;
    wifiRecoveryHoldoffUntilMs = 0;
    lastWiFiRecoveryAction = "Connected";
    startLocalMdnsAlias();
  }

  // Keep the alarm episode state current before processing Web UI acknowledgement.
  updateCloudAlarmEpisodeTracker();

  // --------------------------------------------------
  // Handle local webserver requests.
  // --------------------------------------------------
  if (webServerStarted) {
    webServer.handleClient();
  }

  // --------------------------------------------------
  // Apply OLED day/night contrast schedule.
  // --------------------------------------------------
  static unsigned long lastContrastScheduleCheck = 0;
  if (millis() - lastContrastScheduleCheck >= 5000) {
    applyDisplayContrast();
    lastContrastScheduleCheck = millis();
  }

  // --------------------------------------------------
  // Read latest Nightscout glucose every 5 seconds.
  // This continues even when OLED says NO DATA.
  // --------------------------------------------------
  if (millis() - lastGlucoseUpdate >= glucoseUpdateInterval) {
    Serial.println("5-second Nightscout poll due.");
    updateLatestGlucose();
    lastGlucoseUpdate = millis();
  }

  // --------------------------------------------------
  // Read phone battery every 5 minutes.
  // --------------------------------------------------
  updatePhoneBatteryIfDue();

  // --------------------------------------------------
  // Complete a deferred post-save SAGE refresh only after the Web response and
  // NVS writes are finished. This keeps settings saves lightweight.
  // --------------------------------------------------
  if (scheduledSageRefreshAtMs != 0 &&
      (long)(millis() - scheduledSageRefreshAtMs) >= 0) {
    scheduledSageRefreshAtMs = 0;
    readNightscoutSageInfo();
    lastSageUpdate = millis();
  }

  // --------------------------------------------------
  // Read Nightscout SAGE property every 30 minutes.
  // --------------------------------------------------
  updateSageIfDue();

  // --------------------------------------------------
  // Read latest bolus/treatment and update insulin estimate.
  // --------------------------------------------------
  updateInsulinIfDue();

  // --------------------------------------------------
  // Selected WhatsApp provider alerts and caregiver reminders.
  // These functions only enqueue jobs; HTTPS delivery runs in the background cloud task.
  // --------------------------------------------------
  updateSelectedWhatsAppAlerts();
  updateCaregiverReminderAlerts();

  // --------------------------------------------------
  // Display refresh strategy.
  // Fast refresh only when alarm flashing is needed.
  // Slow refresh for normal or NO DATA.
  // --------------------------------------------------
  if (currentScreen == SCREEN_MAIN) {
    int level = getCurrentAlarmLevel();

    bool alarmFlashNeeded =
      level == ALARM_LOW ||
      level == ALARM_URGENT_LOW ||
      level == ALARM_HIGH ||
      level == ALARM_URGENT_HIGH ||
      updateAvailable;

    if (alarmFlashNeeded) {
      if (millis() - lastMainDisplayRefresh >= 250) {
        drawGlucoseScreen();
        lastMainDisplayRefresh = millis();
      }
    } else {
      if (millis() - lastNoDataDisplayRefresh >= 3000) {
        drawGlucoseScreen();
        lastNoDataDisplayRefresh = millis();
      }
    }
  }

  delay(20);
}
