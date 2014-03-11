#include <pebble.h>

#define LOG_ERROR(...) app_log(APP_LOG_LEVEL_ERROR,__FILE__,__LINE__,__VA_ARGS__)
#define LOG_WARN(...) app_log(APP_LOG_LEVEL_WARNING,__FILE__,__LINE__,__VA_ARGS__)
#define LOG_INFO(...) app_log(APP_LOG_LEVEL_INFO,__FILE__,__LINE__,__VA_ARGS__)
#define LOG_DEBUG(...) app_log(APP_LOG_LEVEL_DEBUG,__FILE__,__LINE__,__VA_ARGS__)

#define GRECT(x,y,w,h) ((GRect){ .origin = {(x),(y)}, .size={(w),(h)} })

int tuple_get_int(const Tuple* t) {
  if (t->type == TUPLE_INT) {
    if (t->length == 1) return t->value->int8;
    if (t->length == 2) return t->value->int16;
    if (t->length == 4) return t->value->int32;
  } else if (t->type == TUPLE_UINT) {
    if (t->length == 1) return t->value->uint8;
    if (t->length == 2) return t->value->uint16;
    if (t->length == 4) return t->value->uint32;
  }
  LOG_ERROR("Expected int tuple");
  return -1;
}

// Communication

#define T_KEY_TZ_OFFSET 10
#define T_KEY_HEMISPHERE 11

static int32_t time_zone = 0; // offset in seconds between current TZ and UTC
static int8_t hemisphere = 0; // 0=northern hemisphere, 1=southern hemisphere

// Layout

static Window *window;

static TextLayer *tl_unixtime;
static TextLayer *tl_date;
static TextLayer *tl_time;
static TextLayer *tl_moonphase;
static BitmapLayer *bl_moon;

static char txt_unixtime[20] = "1234567890";
static char txt_date[20] = "1234-12-12";
static char txt_time[20] = "12:34:56";
static char txt_moonphase[20] = "made of cheese?";

static GBitmap* bm_moon = NULL;
static int bm_moon_index = -1;

static time_t time_utc() {
  return time(NULL) - time_zone;
}

static void update_time() {
  time_t now = time_utc();
  snprintf(txt_unixtime, sizeof(txt_unixtime), "%10ld", (long)now);
  text_layer_set_text(tl_unixtime, txt_unixtime);
  struct tm* tm_now = localtime(&now);
  strftime(txt_date, sizeof(txt_date), "%F", tm_now);
  strftime(txt_time, sizeof(txt_time), "%T UTC", tm_now);
  text_layer_set_text(tl_time, txt_time);
}

// Synodic month
#define SYNODIC_MONTH 29.53058868
// Known new moon is Jan 07 1970 20:35:00
#define KNOWN_NEW_MOON 7.190972

// Time should be in UTC
static double moon_phase(time_t t) {
  double days = t/86400. - KNOWN_NEW_MOON;
  double phase = days / SYNODIC_MONTH;
  phase -= (int)phase;
  if (phase<0) phase += 1;
  return phase;
}

static const char *phase_text[] = {
  "Waxing crescent",
  "Waxing gibbous",
  "Waning gibbous",
  "Waning crescent",
};

static void set_moonphase_text(double phase) {
  int32_t c = -cos_lookup((int)(phase*TRIG_MAX_ANGLE));
  int phase_pct = (c+TRIG_MAX_RATIO) * 100 / (2*TRIG_MAX_RATIO);
  LOG_INFO("Moon phase %3d (%d%%)", (int)(phase*1000), phase_pct);
  if (phase_pct == 0) {
    text_layer_set_text(tl_moonphase, "New moon");
    return;
  }
  if (phase_pct == 100) {
    text_layer_set_text(tl_moonphase, "Full moon");
    return;
  }
  if (phase_pct == 50) {
    text_layer_set_text(tl_moonphase, phase < 0.5 ? "First quarter" : "Last quarter");
    return;
  }

  int phase_q = (int)(phase*4);
  snprintf(txt_moonphase, sizeof(txt_moonphase), "%s %d%%", phase_text[phase_q], phase_pct);
}

// There are 32 moon phase images in resources; 0 is new moon, 16 is full moon
#define NUM_MOON_IMAGES 32
static uint32_t get_bm_moon_resource_id(int ix) {
  // FIXME: we assume that RESOURCE_ID_MOON_xx are ordered accurdingly
  return RESOURCE_ID_MOON_0 + ix;
}

static void set_moonphase_bitmap(double phase) {
  // We have 32 
  int ix = (int)(phase*NUM_MOON_IMAGES + 0.5);
  // In southern hemisphere, moon phases look "mirrored"
  if (hemisphere) ix = NUM_MOON_IMAGES - ix;
  ix %= NUM_MOON_IMAGES;
  if (ix == bm_moon_index) return; // nothing to do
  // To save RAM, we are only keeping the current moon images
  if (bm_moon) {
    bitmap_layer_set_bitmap(bl_moon, NULL);
    gbitmap_destroy(bm_moon);
    bm_moon = NULL;
  }
  bm_moon = gbitmap_create_with_resource(get_bm_moon_resource_id(ix));
  if (bm_moon) {
    LOG_INFO("Loaded bitmap with index %d", ix);
    bm_moon_index = ix;
    bitmap_layer_set_bitmap(bl_moon, bm_moon);
  } else {
    LOG_ERROR("Cannot load bitmap with index %d", ix);
  }
}

static void update_moon() {
  double phase = moon_phase(time_utc());
  set_moonphase_text(phase);
  set_moonphase_bitmap(phase);
}

static TextLayer* tl_init(Layer* layer, int y, const char* font, const char* init_text) {
  TextLayer* tl = text_layer_create(GRECT(0, y, 144, 24));
  text_layer_set_background_color(tl, GColorBlack);
  text_layer_set_text_color(tl, GColorWhite);
  text_layer_set_font(tl, fonts_get_system_font(font));
  text_layer_set_text_alignment(tl, GTextAlignmentCenter);
  text_layer_set_text(tl, init_text);
  layer_add_child(layer, text_layer_get_layer(tl));
  return tl;
}

static void window_load(Window *window) {
  Layer* window_layer = window_get_root_layer(window);
  tl_date = tl_init(window_layer, 8, FONT_KEY_GOTHIC_24_BOLD, txt_date);
  tl_time = tl_init(window_layer, 32, FONT_KEY_GOTHIC_24_BOLD, txt_time);

  bl_moon = bitmap_layer_create(GRECT(0, 56, 144, 48));
  bitmap_layer_set_background_color(bl_moon, GColorBlack);
  bitmap_layer_set_alignment(bl_moon, GAlignCenter);
  layer_add_child(window_layer, bitmap_layer_get_layer(bl_moon));

  tl_moonphase = tl_init(window_layer, 104, FONT_KEY_GOTHIC_18_BOLD, txt_moonphase);
  tl_unixtime  = tl_init(window_layer, 128, FONT_KEY_GOTHIC_18_BOLD, txt_unixtime);

  update_time();
  update_moon();
}

static void window_unload(Window *window) {
  bitmap_layer_destroy(bl_moon);
  if (bm_moon) {
    gbitmap_destroy(bm_moon);
    bm_moon = NULL;
  }
  text_layer_destroy(tl_moonphase);
  text_layer_destroy(tl_time);
  text_layer_destroy(tl_date);
  text_layer_destroy(tl_unixtime);
}

static void persist_load() {
  // fetch cached info from the persistent storage
  time_zone = persist_read_int(T_KEY_TZ_OFFSET);
  hemisphere = persist_read_int(T_KEY_HEMISPHERE);
  LOG_INFO("Loaded time_zone=%d hemisphere=%d", (int)time_zone, (int)hemisphere);
}

static void persist_save() {
  persist_write_int(T_KEY_TZ_OFFSET, time_zone);
  persist_write_int(T_KEY_HEMISPHERE, hemisphere);  
}

static void on_second_tick(struct tm* tick_time, TimeUnits units_changed) {
  update_time();
  if (units_changed & HOUR_UNIT) update_moon();
}

static void on_message_received(DictionaryIterator* dict, void* ctx) {
  LOG_INFO("Message rcvd!");
  bool changed = false;
  for (Tuple* t = dict_read_first(dict); t; t = dict_read_next(dict)) {
    if (t->key == T_KEY_TZ_OFFSET) {
      int new_tz = tuple_get_int(t);
      if (new_tz != time_zone) {
        LOG_INFO("New TZ=%d", new_tz);
        time_zone = new_tz;
        changed = true;
      }
    }
    if (t->key == T_KEY_HEMISPHERE) {
      int new_h = tuple_get_int(t);
      if (new_h != hemisphere) {
        LOG_INFO("New Hemisphere=%d", new_h);
        hemisphere = new_h;
        changed = true;
      }
    }
  }
  if (changed) {
    persist_save();
    update_time();
    update_moon();
  }
}

static void init(void) {
  persist_load();
  window = window_create();
  window_set_background_color(window, GColorBlack);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;
  window_stack_push(window, animated);

  tick_timer_service_subscribe(SECOND_UNIT, on_second_tick);
  app_message_open(64,64);
  app_message_register_inbox_received(on_message_received);
}

static void deinit(void) {
  app_message_deregister_callbacks();
  tick_timer_service_unsubscribe();
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
