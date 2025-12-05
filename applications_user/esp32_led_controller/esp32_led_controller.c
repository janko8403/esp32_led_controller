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

// Konfiguracja ESP32 - zmień na adres IP swojego ESP32
#define ESP32_IP "192.168.0.187"  // ZMIEŃ NA ADRES IP TWOJEGO ESP32
#define ESP32_PORT 1234  // Port UDP

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

    // Nagłówek
    canvas_draw_str(canvas, 2, 10, APP_NAME);
    canvas_draw_line(canvas, 0, 12, 128, 12);

    // Status połączenia
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, "ESP32 IP:");
    canvas_draw_str(canvas, 2, 34, ESP32_IP);
    
    if(state->connected) {
        canvas_draw_str(canvas, 2, 46, "Status: Connected");
    } else {
        canvas_draw_str(canvas, 2, 46, "Status: Disconnected");
    }

    // Stan LED
    canvas_draw_str(canvas, 2, 58, "LED State:");
    if(state->led_state) {
        canvas_draw_str(canvas, 70, 58, "ON");
        canvas_draw_circle(canvas, 100, 50, 5);
        canvas_draw_dot(canvas, 100, 50);
    } else {
        canvas_draw_str(canvas, 70, 58, "OFF");
        canvas_draw_circle(canvas, 100, 50, 5);
    }

    // Instrukcje
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

// Funkcja do wysyłania komendy UDP do ESP32
// Używa Maraudera lub alternatywnego rozwiązania
static bool send_udp_command(const char* command, char* response, size_t response_size) {
    bool success = false;
    
    FURI_LOG_I(TAG, "Sending UDP command: %s to %s:%d", command, ESP32_IP, ESP32_PORT);
    
    // UWAGA: Flipper Zero nie ma natywnego UDP API
    // Musimy użyć Maraudera lub innego rozwiązania
    
    // Próba 1: Użyj Maraudera przez CLI (jeśli dostępny)
    // To wymaga, aby Marauder był zainstalowany i skonfigurowany
    FuriString* cmd = furi_string_alloc();
    
    // Przykład komendy Maraudera (może wymagać dostosowania):
    // marauder udp_send <IP> <PORT> <MESSAGE>
    furi_string_printf(cmd, "marauder udp_send %s %d %s", ESP32_IP, ESP32_PORT, command);
    
    FURI_LOG_I(TAG, "Command: %s", furi_string_get_cstr(cmd));
    
    // TODO: Wykonaj komendę przez system() lub Marauder API
    // Na razie symulujemy odpowiedź, ale w rzeczywistości trzeba:
    // 1. Połączyć się z WiFi przez Maraudera
    // 2. Wysłać pakiet UDP
    // 3. Odczytać odpowiedź
    
    // Symulacja odpowiedzi (do czasu implementacji rzeczywistego UDP)
    if(strcmp(command, "TOGGLE") == 0) {
        snprintf(response, response_size, "ON");
        success = true;
        FURI_LOG_I(TAG, "LED toggled (simulated - UDP not implemented)");
    } else if(strcmp(command, "STATE") == 0) {
        snprintf(response, response_size, "OFF");
        success = true;
        FURI_LOG_I(TAG, "LED state checked (simulated - UDP not implemented)");
    }
    
    furi_string_free(cmd);
    return success;
}

static void toggle_led(AppState* state) {
    FURI_LOG_I(TAG, "Toggling LED");
    
    // Wysyłanie komendy UDP do ESP32
    char response[16];
    if(send_udp_command("TOGGLE", response, sizeof(response))) {
        // Aktualizuj stan na podstawie odpowiedzi
        state->led_state = (strcmp(response, "ON") == 0);
        snprintf(state->status_message, sizeof(state->status_message), "LED: %s", response);
        state->connected = true;
    } else {
        snprintf(state->status_message, sizeof(state->status_message), "Connection failed");
        state->connected = false;
    }
}

static void check_led_state(AppState* state) {
    char response[16];
    if(send_udp_command("STATE", response, sizeof(response))) {
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
    
    // Sprawdzenie stanu LED przy starcie
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
    
    // Cleanup
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_mutex_free(state->mutex);
    free(state);
    furi_message_queue_free(event_queue);
    
    return 0;
}

