#include <pebble.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// MGSV2 watchface - multi-platform (aplite / basalt / emery = Pebble Time 2)
//
// The original face was hand-tuned for a 144x168 screen. Every coordinate is
// still expressed on that 144x168 "design canvas" and scaled to the real
// screen through sx()/sy(), so the same layout adapts to the Pebble Time 2
// 200x228 display without hardcoded screen sizes.
//
// Raster art (background + diamonds) ships a "~emery" variant that the SDK
// substitutes automatically on that platform.
// ---------------------------------------------------------------------------

#define SETTINGS_KEY 1
#define DESIGN_W 144
#define DESIGN_H 168

// ---------------------------------------------------------------------------
// Field layout, in 144x168 design-canvas coords (scaled to the real screen).
//
// Measured from bgv2.2.png rather than guessed:
//   main LCD window : x 11..105, y 17..110   (LCD_* below)
//   printed emblem  : x 48..70,  y 53..76    (already part of the artwork)
//   lower band      : x  4..139, y 122..152
// Nothing may extend past the LCD window or it lands on the printed bezel
// labels (CHA / ALM / DATA / PWR.S / IMP.L / BATTY) on the right rail.
//
// Fields are placed around the printed emblem, Seiko-style:
//   seconds above it, AM/PM left of it, day-of-week right of it,
//   HH:MM below it, full date in the lower band.
// ---------------------------------------------------------------------------
#define LCD_X          11
#define LCD_Y          17
#define LCD_W          94
#define LCD_H          93

// The seconds sweep runs just inside the LCD border. It cannot go on the
// bezel: that ring is already occupied by the printed ADJUST/START.STOP text
// and the 55/60/5 - 50 - 45 - 40 - 35/30/25 scale numbers.
#define RING_X         LCD_X
#define RING_Y         LCD_Y
#define RING_W         LCD_W
#define RING_H         LCD_H

// Readouts keep a margin from the LCD edge so the sweep never overlaps them.
// The emblem is printed at y 53..76, so the two free bands inside the LCD are
// y 22..52 (seconds) and y 77..104 (time).
#define LY_SEC_X       16
#define LY_SEC_Y       22
#define LY_SEC_W       84
#define LY_SEC_H       28

// AM/PM and day-of-week flank the printed emblem (x 48..70, y 53..76), filling
// the gaps between it and the sweep marks (which reach x 15 and x 101).
// Their height must clear the label font or the glyphs get clipped.
#define LY_AMPM_X      17
#define LY_AMPM_Y      52
#define LY_AMPM_W      29
#define LY_AMPM_H      25

#define LY_DOW_X       72
#define LY_DOW_Y       52
#define LY_DOW_W       26
#define LY_DOW_H       25

#define LY_TIME_X      18
#define LY_TIME_Y      77
#define LY_TIME_W      80
#define LY_TIME_H      28

#define LY_DATE_X      8
#define LY_DATE_Y      124
#define LY_DATE_W      128
#define LY_DATE_H      28

#define LY_WEATHER_X   6
#define LY_WEATHER_Y   1
#define LY_WEATHER_W   (DESIGN_W - 12)
#define LY_WEATHER_H   12

#define LY_BATT_X      118
#define LY_BATT_Y      76
#define LY_BATT_W      20
#define LY_BATT_H      11

// Platform-scaled fonts. The "~emery" project fonts are only bundled for
// emery (see package.json), so guard their use.
#if defined(PBL_PLATFORM_EMERY)
  #define RES_FONT_TIME    RESOURCE_ID_FONT_DIGIT_SEVEN_42
  #define RES_FONT_MID     RESOURCE_ID_FONT_DIGIT_SEVEN_35
  #define RES_FONT_LETTER  RESOURCE_ID_FONT_SMALL_PIXEL_21
  #define RES_FONT_LABEL   RESOURCE_ID_FONT_SMALL_PIXEL_28
#else
  #define RES_FONT_TIME    RESOURCE_ID_FONT_DIGIT_SEVEN_30
  #define RES_FONT_MID     RESOURCE_ID_FONT_DIGIT_SEVEN_25
  #define RES_FONT_LETTER  RESOURCE_ID_FONT_SMALL_PIXEL_15
  #define RES_FONT_LABEL   RESOURCE_ID_FONT_SMALL_PIXEL_20
#endif

// --- settings -------------------------------------------------------------
typedef struct ClaySettings {
  bool PowerSaving;      // true  -> minute updates, static seconds ring
  bool TemperatureUnit;  // false -> Celsius, true -> Fahrenheit
  bool ShowWeather;      // show / hide the weather readout
  int8_t TimeFormat;     // 0 = follow system, 1 = force 12h, 2 = force 24h
} ClaySettings;

static ClaySettings settings;

// --- state --------------------------------------------------------------
static Window *s_main_window;
static Layer *s_root_layer;
static GRect s_bounds;

static TextLayer *s_time_layer;
static TextLayer *s_sec_layer;
static TextLayer *s_am_pm_layer;
static TextLayer *s_date_layer;
static TextLayer *s_weekday_text_layer;
static TextLayer *s_weather_layer;

static Layer *s_hands_layer;
static Layer *s_battery_layer;

static BitmapLayer *s_background_layer;
static GBitmap *s_background_bitmap;

static GFont s_time_font;
static GFont s_time_mid_font;
static GFont s_letter_font;
static GFont s_label_font;

static int s_battery_level;

// Last weather sample, kept in Celsius so we can re-render on a unit change
// without hitting the network again.
static int s_weather_temp_c;
static bool s_weather_valid;
static char s_conditions_buffer[32];
static char s_weather_buffer[42];

static void update_time(void);
static void tick_handler(struct tm *tick_time, TimeUnits units_changed);

// --- scaling helpers ---------------------------------------------------
static int16_t sx(int v) { return (int16_t)((int32_t)v * s_bounds.size.w / DESIGN_W); }
static int16_t sy(int v) { return (int16_t)((int32_t)v * s_bounds.size.h / DESIGN_H); }
static GRect scaled_rect(int x, int y, int w, int h) {
  return GRect(sx(x), sy(y), sx(w), sy(h));
}

// --- settings persistence -------------------------------------------------
static void prv_default_settings(void) {
  settings.PowerSaving = false;
  settings.TemperatureUnit = false;
  settings.ShowWeather = true;
  settings.TimeFormat = 0;
}

static void prv_load_settings(void) {
  prv_default_settings();
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void prv_save_settings(void) {
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static bool prv_use_24h(void) {
  if (settings.TimeFormat == 1) return false;
  if (settings.TimeFormat == 2) return true;
  return clock_is_24h_style();
}

static void prv_render_weather(void) {
  if (!s_weather_valid) {
    return;
  }
  int t = s_weather_temp_c;
  char unit = 'C';
  if (settings.TemperatureUnit) {
    t = t * 9 / 5 + 32;
    unit = 'F';
  }
  snprintf(s_weather_buffer, sizeof(s_weather_buffer), "%d%c %s", t, unit, s_conditions_buffer);
  text_layer_set_text(s_weather_layer, s_weather_buffer);
}

static void prv_apply_tick_subscription(void) {
  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(settings.PowerSaving ? MINUTE_UNIT : SECOND_UNIT, tick_handler);
}

static void prv_apply_settings(void) {
  if (s_weather_layer) {
    layer_set_hidden(text_layer_get_layer(s_weather_layer), !settings.ShowWeather);
  }
  prv_render_weather();
  prv_apply_tick_subscription();
  update_time();
  if (s_hands_layer) {
    layer_mark_dirty(s_hands_layer);
  }
}

// --- AppMessage --------------------------------------------------------
static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  // Weather payload
  Tuple *temp_t = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
  Tuple *cond_t = dict_find(iter, MESSAGE_KEY_CONDITIONS);
  if (temp_t) {
    s_weather_temp_c = (int)temp_t->value->int32;
    s_weather_valid = true;
  }
  if (cond_t) {
    snprintf(s_conditions_buffer, sizeof(s_conditions_buffer), "%s", cond_t->value->cstring);
  }
  if (temp_t || cond_t) {
    prv_render_weather();
  }

  // Clay settings payload
  bool changed = false;
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_PowerSaving))) {
    settings.PowerSaving = t->value->int32 != 0;
    changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TemperatureUnit))) {
    settings.TemperatureUnit = t->value->int32 != 0;
    changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ShowWeather))) {
    settings.ShowWeather = t->value->int32 != 0;
    changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TimeFormat))) {
    if (t->type == TUPLE_CSTRING) {
      settings.TimeFormat = (int8_t)atoi(t->value->cstring);
    } else {
      settings.TimeFormat = (int8_t)t->value->int32;
    }
    changed = true;
  }

  if (changed) {
    prv_save_settings();
    prv_apply_settings();
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped! %d", (int)reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed! %d", (int)reason);
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

// --- battery ----------------------------------------------------------
static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  layer_mark_dirty(s_battery_layer);
}

static void battery_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int width = (int)((int32_t)s_battery_level * bounds.size.w / 100);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, width, bounds.size.h), 0, GCornerNone);
}

// --- custom layers --------------------------------------------------
//
// The 60 second-marks are spaced evenly ALONG THE PERIMETER of the LCD, not
// by angle. That is what makes them line up with the printed scale numbers:
// projecting equal angles onto a rectangle bunches marks near the sides and
// spreads them at the corners, which is why the ring looked off-centre.
// Walking the perimeter puts mark 5 under the printed "5", mark 10 under
// "10", mark 45 under "45", and so on (verified against the artwork).
//
// Each mark is a short segment perpendicular to its edge, pointing inward.
static void hands_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int32_t w = b.size.w;
  int32_t h = b.size.h;
  if (w < 16 || h < 16) return;

  int32_t len = (w < h ? w : h) / 22;   // mark length, inward
  if (len < 3) len = 3;

  int32_t half = w / 2;
  int32_t perim = 2 * (w + h);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int last = settings.PowerSaving ? 60 : (t->tm_sec + 1);

  // 2px reads as a hairline on emery but is chunky on a 144x168 screen, where
  // it would crowd the time underneath.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, w >= 120 ? 2 : 1);
  for (int i = 0; i < last; i++) {
    int32_t d = ((int32_t)i * perim) / 60;   // distance from top-centre, clockwise
    int32_t ox, oy, ix, iy;

    if (d <= half) {                       // top edge, centre -> right
      ox = half + d;      oy = 0;          ix = ox;        iy = len;
    } else if ((d -= half) <= h) {         // right edge, top -> bottom
      ox = w - 1;         oy = d;          ix = w - 1 - len; iy = oy;
    } else if ((d -= h) <= w) {            // bottom edge, right -> left
      ox = w - d;         oy = h - 1;      ix = ox;        iy = h - 1 - len;
    } else if ((d -= w) <= h) {            // left edge, bottom -> top
      ox = 0;             oy = h - d;      ix = len;       iy = oy;
    } else {                               // top edge, left -> centre
      ox = d - h;         oy = 0;          ix = ox;        iy = len;
    }

    graphics_draw_line(ctx, GPoint(ix, iy), GPoint(ox, oy));
  }
}

// --- time / date ---------------------------------------------------
static void update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  bool h24 = prv_use_24h();

  static char buffer[6];
  strftime(buffer, sizeof(buffer), h24 ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, buffer);

  static char am_buffer[3];
  if (h24) {
    am_buffer[0] = '\0';
  } else {
    strftime(am_buffer, sizeof(am_buffer), "%p", tick_time);
  }
  text_layer_set_text(s_am_pm_layer, am_buffer);

  // Big running seconds in the upper band (the "22" on the in-game HUD).
  // Blank in power-saving mode, where the watch only ticks once a minute.
  static char sec_buffer[3];
  if (settings.PowerSaving) {
    sec_buffer[0] = '\0';
  } else {
    strftime(sec_buffer, sizeof(sec_buffer), "%S", tick_time);
  }
  text_layer_set_text(s_sec_layer, sec_buffer);

  // Full date in the lower band: uppercase month + day-of-month, "SEP 03".
  static char mon_buffer[6];
  strftime(mon_buffer, sizeof(mon_buffer), "%b", tick_time);
  for (char *p = mon_buffer; *p; p++) {
    if (*p >= 'a' && *p <= 'z') {
      *p = (char)(*p - ('a' - 'A'));
    }
  }
  static char date_buffer[16];
  snprintf(date_buffer, sizeof(date_buffer), "%s %02d", mon_buffer, tick_time->tm_mday);
  text_layer_set_text(s_date_layer, date_buffer);

  // Day-of-week straight from tm_wday (0=Sunday). Deriving it via strftime
  // "%u" needed a 2-byte buffer with no slack; on a short write the label
  // would silently keep the previous day.
  static const char *const day_names[] = { "SU", "MO", "TU", "WE", "TH", "FR", "SA" };
  int wday = tick_time->tm_wday;
  if (wday >= 0 && wday < 7) {
    text_layer_set_text(s_weekday_text_layer, day_names[wday]);
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();

  if (!settings.PowerSaving) {
    layer_mark_dirty(s_hands_layer);
  }

  // Ask the phone for fresh weather every 30 minutes (once, on the boundary).
  if (tick_time->tm_min % 30 == 0 && tick_time->tm_sec == 0) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
      app_message_outbox_send();
    }
  }
}

// --- window ------------------------------------------------------
static void main_window_load(Window *window) {
  s_root_layer = window_get_root_layer(window);
  s_bounds = layer_get_bounds(s_root_layer);

  window_set_background_color(window, GColorBlack);

  // Background (bgv2.2.png, or bgv2.2~emery.png on Pebble Time 2)
  s_background_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_MGSV);
  s_background_layer = bitmap_layer_create(s_bounds);
  bitmap_layer_set_bitmap(s_background_layer, s_background_bitmap);
  bitmap_layer_set_alignment(s_background_layer, GAlignCenter);
  layer_add_child(s_root_layer, bitmap_layer_get_layer(s_background_layer));

  // Seconds sweep - rides the printed scale ring on the bezel, clear of both
  // the LCD readouts and the labels on the right rail.
  s_hands_layer = layer_create(scaled_rect(RING_X, RING_Y, RING_W, RING_H));
  layer_set_update_proc(s_hands_layer, hands_update_proc);
  layer_add_child(s_root_layer, s_hands_layer);

  // The Diamond Dogs emblem is already part of bgv2.2.png - nothing to draw.

  // Fonts
  s_time_font = fonts_load_custom_font(resource_get_handle(RES_FONT_TIME));
  s_time_mid_font = fonts_load_custom_font(resource_get_handle(RES_FONT_MID));
  s_letter_font = fonts_load_custom_font(resource_get_handle(RES_FONT_LETTER));
  s_label_font = fonts_load_custom_font(resource_get_handle(RES_FONT_LABEL));

  // Running seconds - large, upper band
  s_sec_layer = text_layer_create(scaled_rect(LY_SEC_X, LY_SEC_Y, LY_SEC_W, LY_SEC_H));
  text_layer_set_background_color(s_sec_layer, GColorClear);
  text_layer_set_text_color(s_sec_layer, GColorBlack);
  text_layer_set_font(s_sec_layer, s_time_mid_font);
  text_layer_set_text_alignment(s_sec_layer, GTextAlignmentCenter);
  text_layer_set_text(s_sec_layer, "00");

  // HH:MM - centred
  s_time_layer = text_layer_create(scaled_rect(LY_TIME_X, LY_TIME_Y, LY_TIME_W, LY_TIME_H));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_text(s_time_layer, "00:00");

  s_am_pm_layer = text_layer_create(scaled_rect(LY_AMPM_X, LY_AMPM_Y, LY_AMPM_W, LY_AMPM_H));
  text_layer_set_background_color(s_am_pm_layer, GColorClear);
  text_layer_set_text_color(s_am_pm_layer, GColorBlack);
  text_layer_set_font(s_am_pm_layer, s_label_font);
  text_layer_set_text_alignment(s_am_pm_layer, GTextAlignmentLeft);
  text_layer_set_text(s_am_pm_layer, "AM");

  // Full date - large, lower band
  s_date_layer = text_layer_create(scaled_rect(LY_DATE_X, LY_DATE_Y, LY_DATE_W, LY_DATE_H));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorBlack);
  text_layer_set_font(s_date_layer, s_time_mid_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  text_layer_set_text(s_date_layer, "SEP 03");

  // Day-of-week - small, right of the time
  s_weekday_text_layer = text_layer_create(scaled_rect(LY_DOW_X, LY_DOW_Y, LY_DOW_W, LY_DOW_H));
  text_layer_set_background_color(s_weekday_text_layer, GColorClear);
  text_layer_set_text_color(s_weekday_text_layer, GColorBlack);
  text_layer_set_font(s_weekday_text_layer, s_label_font);
  text_layer_set_text_alignment(s_weekday_text_layer, GTextAlignmentRight);
  text_layer_set_text(s_weekday_text_layer, "MO");

  s_weather_layer = text_layer_create(scaled_rect(LY_WEATHER_X, LY_WEATHER_Y, LY_WEATHER_W, LY_WEATHER_H));
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, GColorWhite);
  text_layer_set_font(s_weather_layer, s_letter_font);
  text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
  text_layer_set_text(s_weather_layer, "");
  layer_set_hidden(text_layer_get_layer(s_weather_layer), !settings.ShowWeather);

  layer_add_child(s_root_layer, text_layer_get_layer(s_sec_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_am_pm_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_date_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_weekday_text_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_weather_layer));

  // Battery meter - by the "BATTY" label on the right rail
  s_battery_layer = layer_create(scaled_rect(LY_BATT_X, LY_BATT_Y, LY_BATT_W, LY_BATT_H));
  layer_set_update_proc(s_battery_layer, battery_update_proc);
  layer_add_child(s_root_layer, s_battery_layer);

  update_time();
  prv_render_weather();
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_sec_layer);
  text_layer_destroy(s_am_pm_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_weekday_text_layer);
  text_layer_destroy(s_weather_layer);

  fonts_unload_custom_font(s_time_font);
  fonts_unload_custom_font(s_time_mid_font);
  fonts_unload_custom_font(s_letter_font);
  fonts_unload_custom_font(s_label_font);

  gbitmap_destroy(s_background_bitmap);

  layer_destroy(s_hands_layer);
  layer_destroy(s_battery_layer);
  bitmap_layer_destroy(s_background_layer);
}

// --- app lifecycle -----------------------------------------------
static void init(void) {
  prv_load_settings();

  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  update_time();
  prv_apply_tick_subscription();

  battery_state_service_subscribe(battery_callback);
  battery_callback(battery_state_service_peek());

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(256, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
