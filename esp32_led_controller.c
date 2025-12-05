#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stream/stream.h>
#include <stream/buffered_file_stream.h>
#include <flipper_format/flipper_format.h>

#define APP_NAME "ESP32 LED Control"
#define TAG "ESP32LED"

#define ESP32_IP "192.168.0.187"
#define ESP32_PORT 1234

typedef enum {
    EventTypeTick,
    EventTypeInput,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} AppEvent;

typedef struct {
    bool led_state;
    bool connected;
    char status_message[64];
    FuriMutex* mutex;
} AppState;

static void render_callback(Canvas* canvas, void* ctx) {
    AppState* state = (AppState*)ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    canvas_draw_str(canvas, 2, 10, APP_NAME);
    canvas_draw_line(canvas, 0, 12, 128, 12);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, "ESP32 IP:");
    canvas_draw_str(canvas, 2, 34, ESP32_IP);
    
    if(state->connected) {
        canvas_draw_str(canvas, 2, 46, "Status: Connected");
    } else {
        canvas_draw_str(canvas, 2, 46, "Status: Disconnected");
    }

    canvas_draw_str(canvas, 2, 58, "LED State:");
    if(state->led_state) {
        canvas_draw_str(canvas, 70, 58, "ON");
        canvas_draw_circle(canvas, 100, 50, 5);
        canvas_draw_dot(canvas, 100, 50);
    } else {
        canvas_draw_str(canvas, 70, 58, "OFF");
        canvas_draw_circle(canvas, 100, 50, 5);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 60, "OK: Toggle LED");
    canvas_draw_str(canvas, 2, 60, "Back: Exit");

    furi_mutex_release(state->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    AppEvent event = {.type = EventTypeInput, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static bool send_http_request(const char* endpoint, char* response, size_t response_size) {
    bool success = false;
    
    FuriString* url = furi_string_alloc();
    furi_string_printf(url, "http://%s:%d%s", ESP32_IP, ESP32_PORT, endpoint);
    
    FURI_LOG_I(TAG, "Request URL: %s", furi_string_get_cstr(url));
    

    if(strstr(endpoint, "/led/toggle") != NULL) {
        snprintf(response, response_size, "ON");
        success = true;
        FURI_LOG_I(TAG, "LED toggled (simulated)");
    } else if(strstr(endpoint, "/led/state") != NULL) {
        snprintf(response, response_size, "OFF");
        success = true;
        FURI_LOG_I(TAG, "LED state checked (simulated)");
    }
    
    furi_string_free(url);
    return success;
}

static bool send_http_get(const char* endpoint) {
    char response[16];
    return send_http_request(endpoint, response, sizeof(response));
}

static void toggle_led(AppState* state) {
    FURI_LOG_I(TAG, "Toggling LED");

    if(send_http_get("/led/toggle")) {
        state->led_state = !state->led_state;
        snprintf(state->status_message, sizeof(state->status_message), "LED toggled");
        state->connected = true;
    } else {
        snprintf(state->status_message, sizeof(state->status_message), "Connection failed");
        state->connected = false;
    }
}

static void check_led_state(AppState* state) {
    char response[16];
    if(send_http_request("/led/state", response, sizeof(response))) {
        state->led_state = (strcmp(response, "ON") == 0);
        state->connected = true;
    } else {
        state->connected = false;
    }
}

int32_t esp32_led_controller_app(void* p) {
    UNUSED(p);
    
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(AppEvent));
    
    AppState* state = malloc(sizeof(AppState));
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    state->led_state = false;
    state->connected = false;
    strcpy(state->status_message, "Ready");
    
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, state);
    view_port_input_callback_set(view_port, input_callback, event_queue);
    
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    
    check_led_state(state);
    
    FURI_LOG_I(TAG, "Application started");
    
    AppEvent event;
    bool running = true;
    
    while(running) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == EventTypeInput) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyOk:
                        furi_mutex_acquire(state->mutex, FuriWaitForever);
                        toggle_led(state);
                        furi_mutex_release(state->mutex);
                        view_port_update(view_port);
                        break;
                    case InputKeyBack:
                        running = false;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        
        view_port_update(view_port);
    }

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_mutex_free(state->mutex);
    free(state);
    furi_message_queue_free(event_queue);
    
    return 0;
}

