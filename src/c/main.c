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
// Field layout, in 144x168 design-canvas coords (scaled to the real screen at
// runtime). Positions mirror the Seiko NextAge MGSV watch:
//   - big running seconds in the top-right of the LCD
//   - HH:MM centred over the Diamond Dogs emblem
//   - abbreviated month + day-of-month large, lower-centre (the "DEC" slot)
//   - small day-of-week in the top-left
// Tweak these if anything clips or overlaps the printed bezel art.
// ---------------------------------------------------------------------------
#define LY_SEC_X       64
#define LY_SEC_Y       13
#define LY_SEC_W       44
#define LY_SEC_H       36

#define LY_DOW_X       10
#define LY_DOW_Y       14
#define LY_DOW_W       34
#define LY_DOW_H       14

#define LY_TIME_X      6
#define LY_TIME_Y      66
#define LY_TIME_W      102
#define LY_TIME_H      48

#define LY_AMPM_X      8
#define LY_AMPM_Y      56
#define LY_AMPM_W      20
#define LY_AMPM_H      12

#define LY_DATE_X      6
#define LY_DATE_Y      108
#define LY_DATE_W      104
#define LY_DATE_H      30

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
#else
  #define RES_FONT_TIME    RESOURCE_ID_FONT_DIGIT_SEVEN_30
  #define RES_FONT_MID     RESOURCE_ID_FONT_DIGIT_SEVEN_25
  #define RES_FONT_LETTER  RESOURCE_ID_FONT_SMALL_PIXEL_15
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
static TextLayer *s_time_sec_layer;
static TextLayer *s_am_pm_layer;
static TextLayer *s_date_layer;
static TextLayer *s_weekday_text_layer;
static TextLayer *s_weather_layer;

static Layer *s_hands_layer;
static Layer *s_diamond_layer;
static Layer *s_battery_layer;

static BitmapLayer *s_background_layer;
static GBitmap *s_background_bitmap;
static BitmapLayer *s_just_diamond_layer;
static GBitmap *s_just_diamond_bitmap;
static GBitmap *s_diamond_white_bitmap;

static GFont s_time_font;
static GFont s_time_mid_font;
static GFont s_letter_font;

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
static void diamond_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_compositing_mode(ctx, GCompOpOr);
  graphics_draw_bitmap_in_rect(ctx, s_diamond_white_bitmap, layer_get_bounds(layer));
}

static void hands_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  int32_t hw = bounds.size.w / 2;
  int32_t hh = bounds.size.h / 2;
  int32_t tick_len = (hw < hh ? hw : hh) / 8;   // short inward marks

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // Power-saving: draw the whole ring once; otherwise sweep up to "now".
  int last = settings.PowerSaving ? 60 : (t->tm_sec + 1);

  // 60 ticks that hug the LCD's rounded-rectangle edge (following the watch
  // frame, not a circle), filling clockwise from 12 as the seconds advance.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int i = 0; i < last; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 60;
    int32_t dx = sin_lookup(a);          // unit ray, scaled by TRIG_MAX_RATIO
    int32_t dy = -cos_lookup(a);
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;

    // px from centre to the rectangle edge along this ray
    int32_t d = 0x7fffffff;
    if (adx > 0) { int32_t e = hw * TRIG_MAX_RATIO / adx; if (e < d) d = e; }
    if (ady > 0) { int32_t e = hh * TRIG_MAX_RATIO / ady; if (e < d) d = e; }
    int32_t d_in = d - tick_len;
    if (d_in < 0) d_in = 0;

    GPoint p_out = {
      .x = center.x + (int16_t)(dx * d / TRIG_MAX_RATIO),
      .y = center.y + (int16_t)(dy * d / TRIG_MAX_RATIO),
    };
    GPoint p_in = {
      .x = center.x + (int16_t)(dx * d_in / TRIG_MAX_RATIO),
      .y = center.y + (int16_t)(dy * d_in / TRIG_MAX_RATIO),
    };
    graphics_draw_line(ctx, p_in, p_out);
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

  static char sec_buffer[3];
  if (settings.PowerSaving) {
    sec_buffer[0] = '\0';
  } else {
    strftime(sec_buffer, sizeof(sec_buffer), "%S", tick_time);
  }
  text_layer_set_text(s_time_sec_layer, sec_buffer);

  static char am_buffer[3];
  if (h24) {
    am_buffer[0] = '\0';
  } else {
    strftime(am_buffer, sizeof(am_buffer), "%p", tick_time);
  }
  text_layer_set_text(s_am_pm_layer, am_buffer);

  // "DEC 03" style: uppercase abbreviated month + day-of-month, like the
  // real watch's date field.
  static char mon_buf[6];
  strftime(mon_buf, sizeof(mon_buf), "%b", tick_time);
  for (char *p = mon_buf; *p; p++) {
    if (*p >= 'a' && *p <= 'z') {
      *p = (char)(*p - ('a' - 'A'));
    }
  }
  static char date_buffer[16];
  snprintf(date_buffer, sizeof(date_buffer), "%s %02d", mon_buf, tick_time->tm_mday);
  text_layer_set_text(s_date_layer, date_buffer);

  static char weekday_buffer[2];
  strftime(weekday_buffer, sizeof(weekday_buffer), "%u", tick_time);
  static const char *const day_names[] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };
  int idx = weekday_buffer[0] - '1';
  if (idx >= 0 && idx < 7) {
    text_layer_set_text(s_weekday_text_layer, day_names[idx]);
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

  // White diamond outline + animated seconds ring (share the same frame)
  s_diamond_white_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DIAMOND);
  s_diamond_layer = layer_create(scaled_rect(11, 16, 95, 95));
  layer_set_update_proc(s_diamond_layer, diamond_update_proc);

  s_hands_layer = layer_create(scaled_rect(11, 16, 95, 132));
  layer_set_update_proc(s_hands_layer, hands_update_proc);
  layer_add_child(s_root_layer, s_hands_layer);
  layer_add_child(s_root_layer, s_diamond_layer);

  // Small centre diamond
  s_just_diamond_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_JUST_THE_DIAMOND);
  s_just_diamond_layer = bitmap_layer_create(scaled_rect(46, 51, 25, 25));
  bitmap_layer_set_bitmap(s_just_diamond_layer, s_just_diamond_bitmap);
  bitmap_layer_set_alignment(s_just_diamond_layer, GAlignCenter);
  layer_add_child(s_root_layer, bitmap_layer_get_layer(s_just_diamond_layer));

  // Fonts
  s_time_font = fonts_load_custom_font(resource_get_handle(RES_FONT_TIME));
  s_time_mid_font = fonts_load_custom_font(resource_get_handle(RES_FONT_MID));
  s_letter_font = fonts_load_custom_font(resource_get_handle(RES_FONT_LETTER));

  // HH:MM - centred over the emblem
  s_time_layer = text_layer_create(scaled_rect(LY_TIME_X, LY_TIME_Y, LY_TIME_W, LY_TIME_H));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_text(s_time_layer, "00:00");

  // Running seconds - big, top-right of the LCD ("42" on the real watch)
  s_time_sec_layer = text_layer_create(scaled_rect(LY_SEC_X, LY_SEC_Y, LY_SEC_W, LY_SEC_H));
  text_layer_set_background_color(s_time_sec_layer, GColorClear);
  text_layer_set_text_color(s_time_sec_layer, GColorBlack);
  text_layer_set_font(s_time_sec_layer, s_time_mid_font);
  text_layer_set_text_alignment(s_time_sec_layer, GTextAlignmentRight);
  text_layer_set_text(s_time_sec_layer, "00");

  s_am_pm_layer = text_layer_create(scaled_rect(LY_AMPM_X, LY_AMPM_Y, LY_AMPM_W, LY_AMPM_H));
  text_layer_set_background_color(s_am_pm_layer, GColorClear);
  text_layer_set_text_color(s_am_pm_layer, GColorBlack);
  text_layer_set_font(s_am_pm_layer, s_letter_font);
  text_layer_set_text_alignment(s_am_pm_layer, GTextAlignmentLeft);
  text_layer_set_text(s_am_pm_layer, "AM");

  // Month + day-of-month - large, lower-centre (the "DEC" slot)
  s_date_layer = text_layer_create(scaled_rect(LY_DATE_X, LY_DATE_Y, LY_DATE_W, LY_DATE_H));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorBlack);
  text_layer_set_font(s_date_layer, s_time_mid_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  text_layer_set_text(s_date_layer, "SEP 03");

  // Day-of-week - small, top-left
  s_weekday_text_layer = text_layer_create(scaled_rect(LY_DOW_X, LY_DOW_Y, LY_DOW_W, LY_DOW_H));
  text_layer_set_background_color(s_weekday_text_layer, GColorClear);
  text_layer_set_text_color(s_weekday_text_layer, GColorBlack);
  text_layer_set_font(s_weekday_text_layer, s_letter_font);
  text_layer_set_text_alignment(s_weekday_text_layer, GTextAlignmentLeft);
  text_layer_set_text(s_weekday_text_layer, "MO");

  s_weather_layer = text_layer_create(scaled_rect(LY_WEATHER_X, LY_WEATHER_Y, LY_WEATHER_W, LY_WEATHER_H));
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, GColorWhite);
  text_layer_set_font(s_weather_layer, s_letter_font);
  text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
  text_layer_set_text(s_weather_layer, "");
  layer_set_hidden(text_layer_get_layer(s_weather_layer), !settings.ShowWeather);

  layer_add_child(s_root_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_time_sec_layer));
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
  text_layer_destroy(s_time_sec_layer);
  text_layer_destroy(s_am_pm_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_weekday_text_layer);
  text_layer_destroy(s_weather_layer);

  fonts_unload_custom_font(s_time_font);
  fonts_unload_custom_font(s_time_mid_font);
  fonts_unload_custom_font(s_letter_font);

  gbitmap_destroy(s_background_bitmap);
  gbitmap_destroy(s_diamond_white_bitmap);
  gbitmap_destroy(s_just_diamond_bitmap);

  layer_destroy(s_hands_layer);
  layer_destroy(s_diamond_layer);
  layer_destroy(s_battery_layer);
  bitmap_layer_destroy(s_background_layer);
  bitmap_layer_destroy(s_just_diamond_layer);
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
