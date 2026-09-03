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
// Fields follow the real Seiko WIRED layout:
//   LCD    : seconds above the emblem, AM/PM to its left, HH:MM below it
//   band   : month large on the left, day-of-week above day-of-month right
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

// The sweep marks are 1/14 of the LCD (measured off the 2200px photo), so
// they occupy roughly 7px of the design canvas along each edge. Readouts stay
// clear of that band. With the emblem printed at y 53..76, the two usable
// strips inside the LCD are y 24..52 (seconds) and y 77..103 (time).
//
// Seconds sit right of the axis, as on the real watch - not centred.
#define LY_SEC_X       19
#define LY_SEC_Y       24
#define LY_SEC_W       73
#define LY_SEC_H       28

// AM/PM sits left of the TIME digits and level with them (the real watch puts
// it beside the hour, not beside the emblem). The time is centre-aligned and
// never fills its box, so this tucks into the slack on its left.
#define LY_AMPM_X      17
#define LY_AMPM_Y      74
#define LY_AMPM_W      16
#define LY_AMPM_H      19

// Lower band, laid out like the real watch: month large on the left, then
// day-of-week stacked above day-of-month on the right ("DEC" / "MO" / "6").
// The white area starts higher on the right (y 116 from x 107, vs y 122
// across the full width), which is what makes room for the stack.
//
// Measured on the reference: DEC and 6 share a baseline, and MO sits directly
// on top of 6 with no gap. All three are 7-segment. Both right-hand boxes end
// at the same x and use the same font, so their right edges line up; the
// boxes overlap vertically but the glyphs do not, because a TextLayer
// reserves the font's empty ascender above the ink.
//
// Both boxes end at x 139, flush with the right edge of the printed rail
// (CHA/ALM/DATA/PWR.S/IMP.L/BATTY, measured at x 116..139), so the day column
// continues that rail. The rendered right edges still differ by ~2px
// because digital-7 is italic and the "3" leans further out than the "H" -
// a glyph metric, not a layout error; narrowing the box does not move it.
#define LY_DOW_X       104
#define LY_DOW_Y       112
#define LY_DOW_W       35
#define LY_DOW_H       26

#define LY_DOM_X       98
#define LY_DOM_Y       131
#define LY_DOM_W       41
#define LY_DOM_H       26

// The box starts above the emblem on purpose: a TextLayer reserves the font's
// empty ascender at the top, so the glyphs land ~8px lower than the box does.
// Measured on the reference, the digits centre at 74% of the LCD height.
#define LY_TIME_X      18
#define LY_TIME_Y      70
#define LY_TIME_W      80
#define LY_TIME_H      34

// The month is NOT flush left. On the real watch the left half of the band is
// empty and DEC sits right up against the MO/6 column: measured, DEC spans
// 43%..79% of the band width. Right-aligned so it ends just before the day.
#define LY_DATE_X      50
#define LY_DATE_Y      122
#define LY_DATE_W      58
#define LY_DATE_H      36


// Right rail, measured off bgv2.2.png. The five labelled boxes already carry
// printed text, so they can only be lit, not filled with a value. The marker
// goes in the narrow black strip at x 114..115: the gutter is wider than that,
// but x 107..113 already carries the printed 10/15/20 scale numbers. The real
// watch marks its active mode the same way, with a caret beside the label.
#define LY_MARK_X      114
#define LY_MARK_W      2
#define LY_MARK_H      7
#define LY_MARK_CHA    12   // charging
#define LY_MARK_ALM    22   // quiet time
#define LY_MARK_DATA   33   // phone connected
#define LY_MARK_PWRS   44   // power saving
#define LY_MARK_IMPL   55   // Fahrenheit

// Steps live in the rail's one empty box.
#define LY_STEPS_X     117
#define LY_STEPS_Y     71
#define LY_STEPS_W     22
#define LY_STEPS_H     18

// Weather and sun times take the free left block of the lower band
// (measured x 6..66, y 122..152) - the slot the real watch gives the
// stopwatch. Two fixed lines, so nothing has to be waited for.
#define LY_TEMP_X      7
#define LY_TEMP_Y      121
#define LY_TEMP_W      58
#define LY_TEMP_H      16

#define LY_SUN_X       7
#define LY_SUN_Y       136
#define LY_SUN_W       58
#define LY_SUN_H       16

// Battery moves onto the printed yellow bar, where the real watch has its own.
#define LY_BATT_X      117
#define LY_BATT_Y      92
#define LY_BATT_W      22
#define LY_BATT_H      4

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
static TextLayer *s_dom_layer;
static TextLayer *s_am_pm_layer;
static TextLayer *s_date_layer;
static TextLayer *s_weekday_text_layer;

static Layer *s_hands_layer;
static Layer *s_battery_layer;

static BitmapLayer *s_background_layer;
static GBitmap *s_background_bitmap;

static GFont s_time_font;
static GFont s_time_mid_font;
static GFont s_letter_font;
static GFont s_label_font;

static int s_battery_level;
static bool s_charging;
static bool s_bt_connected;
static int s_sunrise_min = -1;   // minutes past midnight, -1 = unknown
static int s_sunset_min  = -1;
static Layer *s_marks_layer;
static TextLayer *s_steps_layer;
static TextLayer *s_temp_layer;
static TextLayer *s_sun_layer;

// Last weather sample, kept in Celsius so we can re-render on a unit change
// without hitting the network again.
static int s_weather_temp_c;
static bool s_weather_valid;
static char s_conditions_buffer[32];

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

static void prv_apply_tick_subscription(void) {
  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(settings.PowerSaving ? MINUTE_UNIT : SECOND_UNIT, tick_handler);
}

static void prv_apply_settings(void) {
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
  }

  Tuple *sunrise_t = dict_find(iter, MESSAGE_KEY_SUNRISE);
  if (sunrise_t) s_sunrise_min = (int)sunrise_t->value->int32;
  Tuple *sunset_t = dict_find(iter, MESSAGE_KEY_SUNSET);
  if (sunset_t) s_sunset_min = (int)sunset_t->value->int32;

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

// --- rail markers ------------------------------------------------------
// The printed labels already name states this watchface tracks, so lighting
// them costs nothing and reads like the real watch's mode marker.
static void marks_update_proc(Layer *layer, GContext *ctx) {
  struct { int y; bool on; } marks[] = {
    { LY_MARK_CHA,  s_charging },
    { LY_MARK_ALM,  quiet_time_is_active() },
    { LY_MARK_DATA, s_bt_connected },
    { LY_MARK_PWRS, settings.PowerSaving },
    { LY_MARK_IMPL, settings.TemperatureUnit },
  };
  // Coordinates are relative to the layer, whose top is LY_MARK_CHA - a
  // layer's bounds always report origin (0,0), so subtract in design space.
  graphics_context_set_fill_color(ctx, GColorWhite);
  for (unsigned i = 0; i < ARRAY_LENGTH(marks); i++) {
    if (!marks[i].on) continue;
    int16_t y = sy(marks[i].y) - sy(LY_MARK_CHA);
    graphics_fill_rect(ctx, GRect(0, y, sx(LY_MARK_W), sy(LY_MARK_H)), 0, GCornerNone);
  }
}

// --- battery ----------------------------------------------------------
static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  s_charging = state.is_charging || state.is_plugged;
  layer_mark_dirty(s_battery_layer);
  if (s_marks_layer) layer_mark_dirty(s_marks_layer);
}

static void bluetooth_callback(bool connected) {
  s_bt_connected = connected;
  if (s_marks_layer) layer_mark_dirty(s_marks_layer);
}

static void battery_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int width = (int)((int32_t)s_battery_level * bounds.size.w / 100);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, width, bounds.size.h), 0, GCornerNone);
}

// Integer square root (Newton). Used to aim each second-mark at the LCD
// centre without pulling in floating point.
static int32_t isqrt32(int32_t n) {
  if (n <= 0) return 0;
  int32_t x = n, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + n / x) / 2; }
  return x;
}

// --- custom layers --------------------------------------------------
//
// The 60 second-marks are spaced evenly ALONG THE PERIMETER of the LCD, not
// by angle. That is what lines them up with the printed scale numbers:
// projecting equal angles onto a rectangle bunches marks near the sides and
// spreads them at the corners. Walking the perimeter puts mark 5 under the
// printed "5", mark 10 under "10", mark 45 under "45" (checked on the art).
//
// The path is an OCTAGON, not a rectangle and not a rounded rectangle. The
// LCD window in bgv2.2.png is chamfered: its corners are straight 45-degree
// cuts about 5px long, not arcs (the Watchy MGSV face draws the same shape).
// Following that keeps the marks on the artwork's own outline, and lets them
// pivot through 45 degrees at each corner instead of jumping 90.
//
// Each mark POINTS AT THE CENTRE, it is not normal to its edge. On the real
// watch the top row starts vertical under the printed "60" and leans further
// with every step toward the corner - a fan, not a comb. The Watchy MGSV
// sprites are drawn the same way. Position comes from the perimeter walk (so
// the marks stay on the printed scale); direction comes from the centre.
static void hands_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int32_t w = b.size.w - 1;
  int32_t h = b.size.h - 1;
  if (w < 24 || h < 24) return;

  int32_t small = w < h ? w : h;
  int32_t c   = small / 16;   // chamfer leg, matching the artwork
  int32_t len = small / 14;   // mark length, measured off the reference photo
  if (len < 3) len = 3;
  if (c < 2) c = 2;

  // A chamfer of leg c is c*sqrt(2) long.
  int32_t diag = (c * 1414) / 1000;
  int32_t run_x = w - 2 * c;
  int32_t run_y = h - 2 * c;
  int32_t half  = run_x / 2;
  int32_t perim = 2 * run_x + 2 * run_y + 4 * diag;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int last = settings.PowerSaving ? 60 : (t->tm_sec + 1);

  // 2px reads as a hairline on emery but is chunky on a 144x168 screen, where
  // it would crowd the time underneath.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, b.size.w >= 120 ? 2 : 1);

  int32_t cx = w / 2, cy = h / 2;

  for (int i = 0; i < last; i++) {
    int32_t d = ((int32_t)i * perim) / 60;   // walked clockwise from top centre
    int32_t ox, oy, k;

    if (d < half) {                                 // top edge, centre -> right
      ox = w / 2 + d;   oy = 0;
    } else if ((d -= half) < diag) {                // top-right chamfer
      k = (d * c) / diag;  ox = w - c + k;   oy = k;
    } else if ((d -= diag) < run_y) {               // right edge, down
      ox = w;           oy = c + d;
    } else if ((d -= run_y) < diag) {               // bottom-right chamfer
      k = (d * c) / diag;  ox = w - k;       oy = h - c + k;
    } else if ((d -= diag) < run_x) {               // bottom edge, right -> left
      ox = w - c - d;   oy = h;
    } else if ((d -= run_x) < diag) {               // bottom-left chamfer
      k = (d * c) / diag;  ox = c - k;       oy = h - k;
    } else if ((d -= diag) < run_y) {               // left edge, up
      ox = 0;           oy = h - c - d;
    } else if ((d -= run_y) < diag) {               // top-left chamfer
      k = (d * c) / diag;  ox = k;           oy = c - k;
    } else {                                        // top edge, left -> centre
      ox = c + (d - diag);  oy = 0;
    }

    // Step `len` from the rim toward the centre.
    int32_t vx = cx - ox, vy = cy - oy;
    int32_t dist = isqrt32(vx * vx + vy * vy);
    if (dist < 1) dist = 1;
    int32_t ix = ox + (vx * len) / dist;
    int32_t iy = oy + (vy * len) / dist;

    graphics_draw_line(ctx, GPoint(ix, iy), GPoint(ox, oy));
  }
}

// --- secondary readouts ------------------------------------------------
// Each value has a fixed home, so none of them has to be waited for. A slot
// with no data yet simply stays blank rather than showing a placeholder.
static void prv_update_readouts(void) {
  static char steps_buf[12];
  steps_buf[0] = '\0';
#if defined(PBL_HEALTH)
  HealthValue steps = health_service_sum_today(HealthMetricStepCount);
  if (steps > 0) snprintf(steps_buf, sizeof(steps_buf), "%d", (int)steps);
#endif
  text_layer_set_text(s_steps_layer, steps_buf);

  static char temp_buf[48];   // holds the longest condition string
  temp_buf[0] = '\0';
  if (settings.ShowWeather && s_weather_valid) {
    int v = s_weather_temp_c;
    char u = 'C';
    if (settings.TemperatureUnit) { v = v * 9 / 5 + 32; u = 'F'; }
    snprintf(temp_buf, sizeof(temp_buf), "%d%c %s", v, u, s_conditions_buffer);
  }
  text_layer_set_text(s_temp_layer, temp_buf);

  static char sun_buf[24];
  sun_buf[0] = '\0';
  if (s_sunrise_min >= 0 && s_sunset_min >= 0) {
    snprintf(sun_buf, sizeof(sun_buf), "%d:%02d %d:%02d",
             s_sunrise_min / 60, s_sunrise_min % 60,
             s_sunset_min / 60, s_sunset_min % 60);
  }
  text_layer_set_text(s_sun_layer, sun_buf);
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

  prv_update_readouts();

  // Lower band, as on the real watch: month large on the left, day-of-week
  // above day-of-month on the right. No year - neither the Seiko nor the
  // in-game HUD shows one.
  static char mon_buffer[6];
  strftime(mon_buffer, sizeof(mon_buffer), "%b", tick_time);
  for (char *p = mon_buffer; *p; p++) {
    if (*p >= 'a' && *p <= 'z') {
      *p = (char)(*p - ('a' - 'A'));
    }
  }
  text_layer_set_text(s_date_layer, mon_buffer);

  // Day-of-month, right of the month in the lower band (the "6" on the watch).
  static char dom_buffer[3];
  strftime(dom_buffer, sizeof(dom_buffer), "%d", tick_time);
  text_layer_set_text(s_dom_layer, dom_buffer);

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
  text_layer_set_text_alignment(s_sec_layer, GTextAlignmentRight);
  text_layer_set_text(s_sec_layer, "00");

  // HH:MM - centred
  s_time_layer = text_layer_create(scaled_rect(LY_TIME_X, LY_TIME_Y, LY_TIME_W, LY_TIME_H));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_font(s_time_layer, s_time_mid_font);
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
  text_layer_set_font(s_date_layer, s_time_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentRight);
  text_layer_set_text(s_date_layer, "SEP");

  // Day-of-week - upper right of the lower band (the "MO" on the watch)
  s_weekday_text_layer = text_layer_create(scaled_rect(LY_DOW_X, LY_DOW_Y, LY_DOW_W, LY_DOW_H));
  text_layer_set_background_color(s_weekday_text_layer, GColorClear);
  text_layer_set_text_color(s_weekday_text_layer, GColorBlack);
  text_layer_set_font(s_weekday_text_layer, s_time_mid_font);
  text_layer_set_text_alignment(s_weekday_text_layer, GTextAlignmentRight);
  text_layer_set_text(s_weekday_text_layer, "MO");

  // Day-of-month - directly below it (the "6" on the watch)
  s_dom_layer = text_layer_create(scaled_rect(LY_DOM_X, LY_DOM_Y, LY_DOM_W, LY_DOM_H));
  text_layer_set_background_color(s_dom_layer, GColorClear);
  text_layer_set_text_color(s_dom_layer, GColorBlack);
  text_layer_set_font(s_dom_layer, s_time_mid_font);
  text_layer_set_text_alignment(s_dom_layer, GTextAlignmentRight);
  text_layer_set_text(s_dom_layer, "03");


  layer_add_child(s_root_layer, text_layer_get_layer(s_sec_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_am_pm_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_date_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_weekday_text_layer));
  layer_add_child(s_root_layer, text_layer_get_layer(s_dom_layer));

  // Steps, in the rail's one empty box
  s_steps_layer = text_layer_create(scaled_rect(LY_STEPS_X, LY_STEPS_Y, LY_STEPS_W, LY_STEPS_H));
  text_layer_set_background_color(s_steps_layer, GColorClear);
  text_layer_set_text_color(s_steps_layer, GColorBlack);
  text_layer_set_font(s_steps_layer, s_letter_font);
  text_layer_set_text_alignment(s_steps_layer, GTextAlignmentCenter);
  text_layer_set_text(s_steps_layer, "");
  layer_add_child(s_root_layer, text_layer_get_layer(s_steps_layer));

  // Weather and sun times, in the band's free left block
  s_temp_layer = text_layer_create(scaled_rect(LY_TEMP_X, LY_TEMP_Y, LY_TEMP_W, LY_TEMP_H));
  text_layer_set_background_color(s_temp_layer, GColorClear);
  text_layer_set_text_color(s_temp_layer, GColorBlack);
  text_layer_set_font(s_temp_layer, s_letter_font);
  text_layer_set_text_alignment(s_temp_layer, GTextAlignmentLeft);
  text_layer_set_text(s_temp_layer, "");
  layer_add_child(s_root_layer, text_layer_get_layer(s_temp_layer));

  s_sun_layer = text_layer_create(scaled_rect(LY_SUN_X, LY_SUN_Y, LY_SUN_W, LY_SUN_H));
  text_layer_set_background_color(s_sun_layer, GColorClear);
  text_layer_set_text_color(s_sun_layer, GColorBlack);
  text_layer_set_font(s_sun_layer, s_letter_font);
  text_layer_set_text_alignment(s_sun_layer, GTextAlignmentLeft);
  text_layer_set_text(s_sun_layer, "");
  layer_add_child(s_root_layer, text_layer_get_layer(s_sun_layer));

  // Battery, on the printed yellow bar
  s_battery_layer = layer_create(scaled_rect(LY_BATT_X, LY_BATT_Y, LY_BATT_W, LY_BATT_H));
  layer_set_update_proc(s_battery_layer, battery_update_proc);
  layer_add_child(s_root_layer, s_battery_layer);

  // Mode markers in the gutter beside the rail labels
  s_marks_layer = layer_create(scaled_rect(LY_MARK_X, LY_MARK_CHA,
                                           LY_MARK_W, LY_MARK_IMPL - LY_MARK_CHA + LY_MARK_H));
  layer_set_update_proc(s_marks_layer, marks_update_proc);
  layer_add_child(s_root_layer, s_marks_layer);

  update_time();
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_sec_layer);
  text_layer_destroy(s_am_pm_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_weekday_text_layer);
  text_layer_destroy(s_dom_layer);

  fonts_unload_custom_font(s_time_font);
  fonts_unload_custom_font(s_time_mid_font);
  fonts_unload_custom_font(s_letter_font);
  fonts_unload_custom_font(s_label_font);

  gbitmap_destroy(s_background_bitmap);

  layer_destroy(s_hands_layer);
  text_layer_destroy(s_steps_layer);
  text_layer_destroy(s_temp_layer);
  text_layer_destroy(s_sun_layer);
  layer_destroy(s_battery_layer);
  layer_destroy(s_marks_layer);
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

  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bluetooth_callback
  });
  bluetooth_callback(connection_service_peek_pebble_app_connection());

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
