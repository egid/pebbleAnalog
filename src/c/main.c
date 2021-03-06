#include "main.h"

#include <pebble.h>
#include <pebble-fctx/fctx.h>
#include <pebble-fctx/fpath.h>
#include <pebble-events/pebble-events.h>
#include "enamel.h"

#include "modules/util.h"

static Window *s_window;
static Layer *bg_layer, *s_date_layer, *s_complication_layer, *s_hands_layer;
static TextLayer *s_num_label;

static GPath *s_tick_paths[NUM_CLOCK_TICKS];
static FPath *g_minute, *g_hour, *g_utc;
static char s_num_buffer[4];
static GBitmap *hr_bitmap_w;
static GBitmap *hr_bitmap_b;
static BitmapLayer *hr_white_layer;
static BitmapLayer *hr_black_layer;

static EventHandle s_settings_event_handle;

#ifdef PBL_HEALTH
static bool s_sleeping;
bool s_use_sleep, s_dark_theme, s_show_hour_digits;
static EventHandle s_health_event_handle;
#endif

enum Palette {
	BEZEL_COLOR,
	FACE_COLOR,
	HAND_COLOR,
	COMPLICATION_COLOR,
	PALETTE_SIZE
};

GColor g_palette[PALETTE_SIZE];

bool hour_ticks = true;
bool minute_ticks = true;
bool complications_on = false;

// --------------------------------------------------------------------------
// Main functions.
// --------------------------------------------------------------------------
static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
	layer_mark_dirty(window_get_root_layer(s_window));
}

static void settings_handler(void *context) {
	g_palette[       BEZEL_COLOR] = enamel_get_DARK_THEME() ? GColorWhite : GColorBlack;
	g_palette[        FACE_COLOR] = enamel_get_DARK_THEME() ? GColorBlack : GColorWhite;
	g_palette[        HAND_COLOR] = enamel_get_DARK_THEME() ? GColorWhite : GColorBlack;
	g_palette[COMPLICATION_COLOR] = enamel_get_DARK_THEME() ? GColorLightGray : GColorDarkGray;

	s_use_sleep = enamel_get_SLEEP_MODE_ENABLED();
	s_dark_theme = enamel_get_DARK_THEME();

	s_show_hour_digits = PBL_IF_ROUND_ELSE(enamel_get_SHOW_HOUR_DIGITS(),false);

	window_set_background_color(s_window, g_palette[FACE_COLOR]);
	text_layer_set_text_color(s_num_label, g_palette[COMPLICATION_COLOR]);

	if (!enamel_get_SHOW_SECONDS()) {
		tick_timer_service_subscribe(MINUTE_UNIT, handle_tick);
	} else {
		tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
	}

	layer_mark_dirty(window_get_root_layer(s_window));
}

#ifdef PBL_HEALTH
static void prv_health_event_handler(HealthEventType event, void *context) {
	if (s_use_sleep && event == HealthEventSignificantUpdate) {
		prv_health_event_handler(HealthEventSleepUpdate, context);
	} else if (s_use_sleep && ( event == HealthEventSleepUpdate || (event == HealthEventMovementUpdate && s_sleeping) )) {
		HealthActivityMask mask = health_service_peek_current_activities();

		bool asleep = (mask & HealthActivitySleep) || (mask & HealthActivityRestfulSleep);

		if (asleep && !s_sleeping) {
			tick_timer_service_subscribe(HOUR_UNIT, handle_tick);
		} else {
			if (!enamel_get_SHOW_SECONDS()) {
				tick_timer_service_subscribe(MINUTE_UNIT, handle_tick);
			} else {
				tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
			}
		}
		s_sleeping = (mask & HealthActivitySleep) || (mask & HealthActivityRestfulSleep);
	}
}
#endif

static void bg_update_proc(Layer *layer, GContext *ctx) {
	GRect bounds = layer_get_bounds(layer);
	int offset_24;
	if (s_show_hour_digits) {
		offset_24 = 12;
		bounds = grect_inset(bounds, GEdgeInsets(offset_24));
		// bounds = layer_set_bounds(layer, inset);
	} else {
		offset_24 = 0;
		bounds = layer_get_bounds(layer);
	}
	// FPoint center = FPointI(bounds.size.w / 2, bounds.size.h / 2);
	// GPoint center = grect_center_point(&bounds);

	// minute ticks
	// create all but 1200
	if (minute_ticks) {
		graphics_context_set_fill_color(ctx, g_palette[BEZEL_COLOR]);
		graphics_context_set_stroke_width(ctx, 1);
		graphics_context_set_stroke_color(ctx, g_palette[BEZEL_COLOR]);
		for (int i = 1; i < 60; i++) {
			GRect frame = grect_inset(bounds, GEdgeInsets(PBL_IF_ROUND_ELSE(3,0)));
			GRect frame2 = grect_inset(bounds, GEdgeInsets(PBL_IF_ROUND_ELSE(6,3)));
			GPoint pos = gpoint_from_polar(frame, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE( get_angle(i, 60) ));
			GPoint pos2 = gpoint_from_polar(frame2, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE( get_angle(i, 60) ));

			graphics_draw_line(ctx, pos, pos2);
		}
	}

	// 12h ticks
	// create all but 1200
	if (hour_ticks) {
		// Double-tick first
		graphics_context_set_fill_color(ctx, g_palette[BEZEL_COLOR]);
		for (int i = 0; i < NUM_CLOCK_TICKS; ++i) {
			const int x_offset = PBL_IF_ROUND_ELSE(18, 0);
			const int y_offset = PBL_IF_ROUND_ELSE(6, 14);
			gpath_move_to(s_tick_paths[i], GPoint(x_offset, y_offset + offset_24));
			gpath_draw_filled(ctx, s_tick_paths[i]);
		}

		graphics_context_set_stroke_width(ctx, 3);
		graphics_context_set_stroke_color(ctx, g_palette[BEZEL_COLOR]);

		for (int i = 1; i < 12; i++) {
			GRect frame = grect_inset(bounds, GEdgeInsets(PBL_IF_ROUND_ELSE(3,0)));
			GRect frame2 = grect_inset(bounds, GEdgeInsets(PBL_IF_ROUND_ELSE(7,4)));
			GPoint pos = gpoint_from_polar(frame, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE( get_angle(i, 12) ));
			GPoint pos2 = gpoint_from_polar(frame2, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE( get_angle(i, 12) ));

			graphics_draw_line(ctx, pos, pos2);
		}
	}

	// if (s_show_hour_digits) {

		// // vertical numerals
		// int hour_multiplier = s_show_24h ? 2 : 1;
		// graphics_context_set_text_color(ctx, GColorDarkGray);
		// for (int i = 0; i < 12; i++) {
		// 	GRect digit_frame = grect_inset(bounds, GEdgeInsets(PBL_IF_ROUND_ELSE(20,0)));
		// 	GPoint digit_pos = gpoint_from_polar(digit_frame, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE( get_angle(i, 12) ));
		//
		// 	char hour_string[3];
		// 	int hour_display;
		// 	if (i == 0) {
		// 		hour_display = 12 * hour_multiplier;
		// 	} else {
		// 		hour_display = i * hour_multiplier;
		// 	}
		// 	snprintf(hour_string, sizeof(hour_string),"%d", hour_display);
		//
		// 	graphics_draw_text(ctx, hour_string, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(digit_pos.x - 10, digit_pos.y - 9, 20, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		// }
	// }
	// Show bg digits png
	bool show_bl = (!s_dark_theme && s_show_hour_digits ? true : false);

	layer_set_hidden(bitmap_layer_get_layer(hr_white_layer), !s_show_hour_digits);
	layer_set_hidden(bitmap_layer_get_layer(hr_black_layer), !show_bl);
}

static GPoint radial_gpoint(GPoint center, int16_t length_from_center, int32_t angle) {
	return (GPoint) {
		.x = (int16_t)(sin_lookup(angle) * (int32_t)length_from_center / TRIG_MAX_RATIO) + center.x,
		.y = (int16_t)(-cos_lookup(angle) * (int32_t)length_from_center / TRIG_MAX_RATIO) + center.y,
	};
}

static void hands_update_proc(Layer *layer, GContext *ctx) {
	GRect bounds = layer_get_bounds(layer);
	GPoint center = grect_center_point(&bounds);
	float modifier = 1.0;
	if (s_show_hour_digits) {
		modifier = 0.9;
	}

	// time stuff
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	struct tm *gmt = gmtime(&now);

	// angles
	int32_t gmt_angle = (TRIG_MAX_ANGLE * (((gmt->tm_hour + enamel_get_OFFSET_GMT_HAND()) * 6) + (gmt->tm_min / 10))) / (24 * 6);
	int32_t second_angle = TRIG_MAX_ANGLE * t->tm_sec / 60;
	int32_t minute_angle = TRIG_MAX_ANGLE * (t->tm_min * 60 + t->tm_sec) / 3600;
	int32_t hour_angle = (TRIG_MAX_ANGLE * (((t->tm_hour % 12) * 6) + (t->tm_min / 10))) / (12 * 6);

	// init fctx
	FContext fctx;
	fctx_init_context(&fctx, ctx);
	FPoint f_center = FPointI(bounds.size.w / 2, bounds.size.h / 2);
	// fixed_t f_scale = 16;


	////////////////////////////////////////////////////////////////////////////
	// GMT pointer
	////////////////////////////////////////////////////////////////////////////

	fctx_set_fill_color(&fctx, enamel_get_COLOR_GMT_HAND());
	fctx_set_offset(&fctx, f_center);
	fctx_set_scale(&fctx,
			PBL_IF_ROUND_ELSE(
				FPoint(10, 10),
				FPoint(10,10)
			), PBL_IF_ROUND_ELSE(
				FPoint(10*modifier, 10*modifier),
				FPoint(8*modifier,8*modifier)
			));
	fctx_set_rotation(&fctx, gmt_angle);

	fctx_begin_fill(&fctx);
	fctx_draw_commands(&fctx, FPointZero, g_utc->data, g_utc->size);
	fctx_end_fill(&fctx);


	////////////////////////////////////////////////////////////////////////////
	// hour hand
	////////////////////////////////////////////////////////////////////////////

	fctx_set_fill_color(&fctx, g_palette[HAND_COLOR]);
	fctx_set_offset(&fctx, f_center);
	fctx_set_scale(&fctx, PBL_IF_ROUND_ELSE(FPoint(10, 10), FPoint(10,10)), PBL_IF_ROUND_ELSE(FPoint(10*modifier, 10*modifier), FPoint(8*modifier,8*modifier)));
	fctx_set_rotation(&fctx, hour_angle);

	fctx_begin_fill(&fctx);
	fctx_draw_commands(&fctx, FPointZero, g_hour->data, g_hour->size);
	fctx_end_fill(&fctx);


	////////////////////////////////////////////////////////////////////////////
	// minute hand
	////////////////////////////////////////////////////////////////////////////

	fctx_set_fill_color(&fctx, g_palette[HAND_COLOR]);
	fctx_set_offset(&fctx, f_center);
	fctx_set_scale(&fctx, PBL_IF_ROUND_ELSE(FPoint(10, 10), FPoint(10,10)), PBL_IF_ROUND_ELSE(FPoint(10*modifier, 10*modifier), FPoint(8*modifier,8*modifier)));
	fctx_set_rotation(&fctx, minute_angle);

	fctx_begin_fill(&fctx);
	fctx_draw_commands(&fctx, FPointZero, g_minute->data, g_minute->size);
	fctx_end_fill(&fctx);


	////////////////////////////////////////////////////////////////////////////
	// second hand
	////////////////////////////////////////////////////////////////////////////

	if (enamel_get_SHOW_SECONDS()) {
		const int16_t second_hand_length = PBL_IF_ROUND_ELSE((bounds.size.w / 2) - 8, bounds.size.w / 2) * modifier;
		GPoint second_hand = radial_gpoint(center, second_hand_length - 15, second_angle);
		GPoint second_hand_point = radial_gpoint(center, second_hand_length, second_angle);

		graphics_context_set_stroke_width(ctx, 2);
		graphics_context_set_stroke_color(ctx, enamel_get_COLOR_SECOND_HAND());
		graphics_draw_line(ctx, second_hand, second_hand_point);
	}


	////////////////////////////////////////////////////////////////////////////
	// center pivot
	////////////////////////////////////////////////////////////////////////////

	fctx_set_fill_color(&fctx, GColorDarkGray);
	fctx_begin_fill(&fctx);
	fctx_plot_circle(&fctx, &f_center, 5*16);
	fctx_end_fill(&fctx);

	// deinit fctx
	fctx_deinit_context(&fctx);}

static void date_update_proc(Layer *layer, GContext *ctx) {
	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	strftime(s_num_buffer, sizeof(s_num_buffer), "%d", t);
	text_layer_set_text(s_num_label, s_num_buffer);
}

static void complication_update_proc(Layer *layer, GContext *ctx) {
	float step_goal = .7;
	GRect layer_bounds = layer_get_bounds(layer);
	GRect bounds = GRect(
		layer_bounds.origin.x + 20,
		layer_bounds.origin.y + (layer_bounds.size.w * 3 / 8),
		layer_bounds.size.w / 4,
		layer_bounds.size.h / 4
	);
	// GRect bounds = GRect(layer_bounds.origin.x, layer_bounds.origin.y,
	//                      layer_bounds.size.w / 2, layer_bounds.size.h);

	graphics_context_set_stroke_color(ctx, g_palette[COMPLICATION_COLOR]);
	graphics_context_set_stroke_width(ctx, 2);
	graphics_draw_arc(ctx, bounds, GOvalScaleModeFitCircle, 0, DEG_TO_TRIGANGLE(step_goal * 360));
}

static void window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	bg_layer = layer_create(bounds);
	layer_set_update_proc(bg_layer, bg_update_proc);
	layer_add_child(window_layer, bg_layer);

	s_date_layer = layer_create(bounds);
	layer_set_update_proc(s_date_layer, date_update_proc);
	layer_add_child(window_layer, s_date_layer);

	if (complications_on || s_sleeping) {
		s_complication_layer = layer_create(bounds);
		layer_set_update_proc(s_complication_layer, complication_update_proc);
		layer_add_child(window_layer, s_complication_layer);
	}

	s_num_label = text_layer_create(PBL_IF_ROUND_ELSE(GRect(130, 77, 18, 20),GRect(110, 72, 18, 20)));
	text_layer_set_text(s_num_label, s_num_buffer);
	text_layer_set_background_color(s_num_label, g_palette[FACE_COLOR]);
	text_layer_set_text_color(s_num_label, g_palette[COMPLICATION_COLOR]);
	text_layer_set_font(s_num_label, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

	layer_add_child(s_date_layer, text_layer_get_layer(s_num_label));

	s_hands_layer = layer_create(bounds);
	layer_set_update_proc(s_hands_layer, hands_update_proc);
	layer_add_child(window_layer, s_hands_layer);

	settings_handler(NULL);
	s_settings_event_handle = enamel_settings_received_subscribe(settings_handler, NULL);
}

static void window_unload(Window *window) {
	enamel_settings_received_unsubscribe(s_settings_event_handle);

	layer_destroy(bg_layer);
	layer_destroy(s_date_layer);

	if (complications_on) {
		layer_destroy(s_complication_layer);
	}

	text_layer_destroy(s_num_label);

	layer_destroy(s_hands_layer);
}

static void init() {
	enamel_init();
	events_app_message_open();

	if (s_health_event_handle == NULL) {
		s_health_event_handle = events_health_service_events_subscribe(prv_health_event_handler, NULL);
	}

	g_minute = fpath_create_from_resource(RESOURCE_ID_MINUTE_FPATH);
	g_hour = fpath_create_from_resource(RESOURCE_ID_HOUR_FPATH);
	g_utc = fpath_create_from_resource(RESOURCE_ID_UTC_FPATH);

	s_window = window_create();
	window_set_background_color(s_window, g_palette[FACE_COLOR]);
	window_set_window_handlers(s_window, (WindowHandlers) {
		.load = window_load,
		.unload = window_unload,
	});
	window_stack_push(s_window, true);


	hr_bitmap_w = gbitmap_create_with_resource(RESOURCE_ID_NUM_WHITE);
	hr_bitmap_b = gbitmap_create_with_resource(RESOURCE_ID_NUM_BLACK);
	hr_white_layer = bitmap_layer_create(GRect(0, 0, 180, 180));
	hr_black_layer = bitmap_layer_create(GRect(0, 0, 180, 180));
	bitmap_layer_set_compositing_mode(hr_white_layer, GCompOpSet);
	bitmap_layer_set_compositing_mode(hr_black_layer, GCompOpSet);
	bitmap_layer_set_bitmap(hr_black_layer, hr_bitmap_b);
	bitmap_layer_set_bitmap(hr_white_layer, hr_bitmap_w);
	layer_add_child(window_get_root_layer(s_window), bitmap_layer_get_layer(hr_white_layer));
	layer_add_child(window_get_root_layer(s_window), bitmap_layer_get_layer(hr_black_layer));

	s_num_buffer[0] = '\0';

	// init hand paths

	for (int i = 0; i < NUM_CLOCK_TICKS; ++i) {
		s_tick_paths[i] = gpath_create(&ANALOG_BG_POINTS[i]);
	}
}

static void deinit() {
	#ifdef PBL_HEALTH
		if (s_health_event_handle != NULL) {
			events_health_service_events_unsubscribe(s_health_event_handle);
			s_health_event_handle = NULL;
		}
	#endif

	fpath_destroy(g_minute);
	fpath_destroy(g_hour);
	fpath_destroy(g_utc);
	gbitmap_destroy(hr_bitmap_w);
	gbitmap_destroy(hr_bitmap_b);
	bitmap_layer_destroy(hr_white_layer);
	bitmap_layer_destroy(hr_black_layer);

	tick_timer_service_unsubscribe();
	window_destroy(s_window);

	enamel_deinit();
}

int main() {
	init();
	app_event_loop();
	deinit();
}
