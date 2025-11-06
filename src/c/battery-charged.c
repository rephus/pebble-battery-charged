#include <pebble.h>

static Window *s_window;
static TextLayer *s_battery_layer;
static TextLayer *s_status_layer;
static Layer *s_battery_icon_layer;
static AppTimer *s_vibe_timer = NULL;

// Current battery state for icon drawing
static int s_current_battery_percent = 0;
static bool s_current_charging = false;

// Track if we've already alerted at 80%
static bool s_has_alerted = false;
static const int ALERT_THRESHOLD = 80;
static int s_vibe_count = 0;
static const int MAX_VIBE_COUNT = 30;  // Vibrate 10 times (about 1 minute)

// Track previous state to avoid unnecessary redraws
static int s_last_percent = -1;
static bool s_last_charging = false;

// Persistent storage key
#define STORAGE_KEY_HAS_ALERTED 1

// Message keys for AppMessage
#define MESSAGE_KEY_ALERT 100

// Vibration timer callback for continuous vibration
static void vibe_timer_callback(void *data) {
  // Check if still charging before continuing vibration
  BatteryChargeState charge_state = battery_state_service_peek();
  bool charging = charge_state.is_charging || charge_state.is_plugged;

  if (!charging) {
    // Stopped charging - cancel vibration sequence
    s_vibe_timer = NULL;
    APP_LOG(APP_LOG_LEVEL_INFO, "Charger unplugged - stopping vibration");
    return;
  }

  if (s_vibe_count < MAX_VIBE_COUNT) {
    // Long vibration pulse
    vibes_long_pulse();
    s_vibe_count++;

    // Schedule next vibration in 2 seconds
    s_vibe_timer = app_timer_register(2000, vibe_timer_callback, NULL);

    APP_LOG(APP_LOG_LEVEL_INFO, "Vibration pulse %d of %d", s_vibe_count, MAX_VIBE_COUNT);
  } else {
    s_vibe_timer = NULL;
    APP_LOG(APP_LOG_LEVEL_INFO, "Vibration sequence complete");
  }
}

// Battery icon drawing callback
static void battery_icon_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Battery body dimensions
  int battery_width = 60;
  int battery_height = 30;
  int battery_x = (bounds.size.w - battery_width) / 2;
  int battery_y = (bounds.size.h - battery_height) / 2;

  // Draw battery outline (white)
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_rect(ctx, GRect(battery_x, battery_y, battery_width, battery_height));

  // Draw battery terminal (tip)
  int tip_width = 4;
  int tip_height = 10;
  int tip_x = battery_x + battery_width;
  int tip_y = battery_y + (battery_height - tip_height) / 2;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(tip_x, tip_y, tip_width, tip_height), 0, GCornerNone);

  // Fill battery based on percentage
  int fill_width = ((battery_width - 4) * s_current_battery_percent) / 100;

  // Choose fill color based on battery level
  GColor fill_color;
  if (s_current_battery_percent <= 20) {
    fill_color = GColorRed;
  } else if (s_current_battery_percent >= ALERT_THRESHOLD) {
    fill_color = GColorGreen;
  } else {
    fill_color = GColorWhite;
  }

  graphics_context_set_fill_color(ctx, fill_color);
  graphics_fill_rect(ctx, GRect(battery_x + 2, battery_y + 2, fill_width, battery_height - 4), 0, GCornerNone);

  // Draw lightning bolt if charging
  if (s_current_charging) {
    graphics_context_set_stroke_color(ctx, GColorYellow);
    graphics_context_set_stroke_width(ctx, 2);

    int center_x = battery_x + battery_width / 2;
    int center_y = battery_y + battery_height / 2;

    // Simple lightning bolt shape
    GPoint bolt[] = {
      {center_x - 5, center_y - 8},
      {center_x + 2, center_y - 2},
      {center_x - 2, center_y + 2},
      {center_x + 5, center_y + 8}
    };

    graphics_draw_line(ctx, bolt[0], bolt[1]);
    graphics_draw_line(ctx, bolt[1], bolt[2]);
    graphics_draw_line(ctx, bolt[2], bolt[3]);
  }
}

// Send message to phone
static void send_alert_to_phone(void) {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);

  if (result == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_ALERT, 1);
    result = app_message_outbox_send();

    if (result == APP_MSG_OK) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Alert message sent to phone");
    } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to send message: %d", result);
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to begin outbox: %d", result);
  }
}

// Update the battery display
static void battery_update_proc(BatteryChargeState charge_state) {
  static char battery_buffer[16];

  // Debug logging
  APP_LOG(APP_LOG_LEVEL_INFO, "Battery: %d%%, is_charging: %d, is_plugged: %d",
          charge_state.charge_percent, charge_state.is_charging, charge_state.is_plugged);

  // Check both is_charging and is_plugged (they can be different)
  bool charging = charge_state.is_charging || charge_state.is_plugged;

  // Only update battery text if percentage or charging state changed
  if (charge_state.charge_percent != s_last_percent || charging != s_last_charging) {
    if (charging) {
      snprintf(battery_buffer, sizeof(battery_buffer), "Charging: %d%%", charge_state.charge_percent);
      APP_LOG(APP_LOG_LEVEL_INFO, "Setting text to: Charging: %d%%", charge_state.charge_percent);
    } else {
      snprintf(battery_buffer, sizeof(battery_buffer), "Battery: %d%%", charge_state.charge_percent);
      APP_LOG(APP_LOG_LEVEL_INFO, "Setting text to: Battery: %d%%", charge_state.charge_percent);
    }

    text_layer_set_text(s_battery_layer, battery_buffer);
    layer_mark_dirty(text_layer_get_layer(s_battery_layer));

    s_last_percent = charge_state.charge_percent;
    s_last_charging = charging;
  }

  // Update battery icon state
  s_current_battery_percent = charge_state.charge_percent;
  s_current_charging = charging;
  if (s_battery_icon_layer) {
    layer_mark_dirty(s_battery_icon_layer);
  }

  // Check if we should alert (use is_plugged or is_charging)
  if (charging &&
      charge_state.charge_percent >= ALERT_THRESHOLD &&
      !s_has_alerted) {

    // Start continuous vibration sequence
    s_vibe_count = 0;
    vibes_long_pulse();  // First pulse immediately
    s_vibe_count++;

    // Schedule repeating vibrations
    if (s_vibe_timer) {
      app_timer_cancel(s_vibe_timer);
    }
    s_vibe_timer = app_timer_register(2000, vibe_timer_callback, NULL);

    // Send notification to phone
    send_alert_to_phone();

    // Update status
    text_layer_set_text(s_status_layer, "ALERT! 80% reached!\nUnplug charger!");
    layer_mark_dirty(text_layer_get_layer(s_status_layer));

    // Mark as alerted and save to persistent storage
    s_has_alerted = true;
    persist_write_bool(STORAGE_KEY_HAS_ALERTED, true);

    APP_LOG(APP_LOG_LEVEL_INFO, "Battery reached 81%% - Alert triggered!");
  } else if (charging && charge_state.charge_percent >= ALERT_THRESHOLD) {
    // Already alerted, show status
    text_layer_set_text(s_status_layer, "80%+ reached");
    layer_mark_dirty(text_layer_get_layer(s_status_layer));
  } else if (!charging) {
    // Reset alert when unplugged (regardless of battery level)
    // This allows re-alerting if you plug back in above 80%
    s_has_alerted = false;
    persist_write_bool(STORAGE_KEY_HAS_ALERTED, false);

    // Clear status when not charging
    text_layer_set_text(s_status_layer, "");
    layer_mark_dirty(text_layer_get_layer(s_status_layer));
  } else {
    // Charging but below threshold - clear status (icon shows charging)
    text_layer_set_text(s_status_layer, "");
    layer_mark_dirty(text_layer_get_layer(s_status_layer));
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Create battery icon layer at top
  s_battery_icon_layer = layer_create(GRect(0, 10, bounds.size.w, 40));
  layer_set_update_proc(s_battery_icon_layer, battery_icon_update_proc);
  layer_add_child(window_layer, s_battery_icon_layer);

  // Create battery percentage text layer
  s_battery_layer = text_layer_create(GRect(0, 60, bounds.size.w, 40));
  text_layer_set_text(s_battery_layer, "Loading...");
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentCenter);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(s_battery_layer, GColorWhite);
  text_layer_set_background_color(s_battery_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_battery_layer));

  // Create status text layer
  s_status_layer = text_layer_create(GRect(5, 110, bounds.size.w - 10, 60));
  text_layer_set_text(s_status_layer, "Monitoring...");
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_color(s_status_layer, GColorWhite);
  text_layer_set_background_color(s_status_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_status_layer));

  // Subscribe to battery state service
  battery_state_service_subscribe(battery_update_proc);

  // Get initial battery state
  battery_update_proc(battery_state_service_peek());
}

static void prv_window_unload(Window *window) {
  // Unsubscribe from battery service
  battery_state_service_unsubscribe();

  layer_destroy(s_battery_icon_layer);
  text_layer_destroy(s_battery_layer);
  text_layer_destroy(s_status_layer);
}

static void prv_init(void) {
  // Load persistent alert state
  if (persist_exists(STORAGE_KEY_HAS_ALERTED)) {
    s_has_alerted = persist_read_bool(STORAGE_KEY_HAS_ALERTED);
  }

  // Initialize AppMessage for phone communication
  app_message_open(128, 128);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  const bool animated = false;  // Disable animation to avoid any vibration
  window_stack_push(s_window, animated);
}

static void prv_deinit(void) {
  // Cancel any pending vibration timer
  if (s_vibe_timer) {
    app_timer_cancel(s_vibe_timer);
    s_vibe_timer = NULL;
  }

  window_destroy(s_window);
}

int main(void) {
  prv_init();

  APP_LOG(APP_LOG_LEVEL_INFO, "Battery Charged app started - monitoring for 80%% threshold");

  app_event_loop();
  prv_deinit();
}
