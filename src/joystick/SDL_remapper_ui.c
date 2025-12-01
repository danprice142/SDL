/* Complete SDL3 Remapper UI - matches reference UI layout */
/* Two-page system: Profile Selection ΓåÆ Button Remapping */
/* Reference resolution: 3840x2160 (4K) matching reference viewbox */

#include "SDL_internal.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_joystick.h"
#include "SDL3/SDL_keyboard.h"
#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_remapper.h"
#include "SDL3/SDL_remapper_io.h"

/* Reference resolution for layout scaling */
#define REFERENCE_WIDTH 3840.0f
#define REFERENCE_HEIGHT 2160.0f

/* Small helper for local array size */
#define SDL_UI_ARRAY_SIZE(arr) ((int)(sizeof((arr)) / sizeof((arr)[0])))

#define SDL_UI_MAX_DEVICES 8

/* Page system */
typedef enum {
    PAGE_DEVICE_SELECT,   /* New landing page: choose which connected device/type to configure */
    PAGE_PROFILE_SELECT,
    PAGE_BUTTON_MAPPING
} UIPage;

/* High-level device types shown on the landing page carousel */
typedef enum {
    UI_DEVICE_TYPE_GAMEPAD = 0,
    UI_DEVICE_TYPE_KEYBOARD,
    UI_DEVICE_TYPE_MOUSE,
    UI_DEVICE_TYPE_JOYSTICK
} UIDeviceType;

/* Dialog types */
typedef enum {
    DIALOG_NONE,
    DIALOG_NEW_PROFILE,
    DIALOG_RENAME_PROFILE,
    DIALOG_DELETE_CONFIRM,
    DIALOG_BUTTON_OPTIONS,
    DIALOG_MAPPING_SELECT,
    DIALOG_TRIGGER_OPTIONS,
    DIALOG_STICK_CONFIG,
    DIALOG_MOUSE_MOVE_CONFIG,
    DIALOG_VIRTUAL_KEYBOARD
} DialogType;

/* UI State */
typedef struct {
    UIPage current_page;
    DialogType active_dialog;

    /* Landing page: device selection carousel */
    int device_count;
    int selected_device;
    bool device_back_focused;         /* True when Back button is focused on device page */
    UIDeviceType device_types[SDL_UI_MAX_DEVICES];
    char device_labels[SDL_UI_MAX_DEVICES][64];
    SDL_JoystickID device_gamepad_ids[SDL_UI_MAX_DEVICES];
    SDL_MouseID active_mouse_id;
    SDL_KeyboardID active_keyboard_id;

    /* Profile management */
    int profile_count;
    int selected_profile;
    int profile_list_scroll;           /* Scroll offset for profile list */
    char profile_names[10][64];

    /* Button mapping */
    SDL_GamepadButton selected_button;
    SDL_GamepadAxis selected_axis;
    int selected_mouse_slot;          /* UI_MOUSE_SLOT_* when editing mouse device */
    int selected_keyboard_slot;       /* UI_KEYBOARD_SLOT_* when editing keyboard device */
    int active_slot;  /* 0=primary, 1=shift, 2=hold */
    int active_tab;   /* 0=controller, 1=mouse, 2=keyboard, 3=touch */
    int list_selection;
    int list_scroll;

    /* Stick config */
    bool stick_wasd;
    bool stick_arrows;
    bool stick_mouse;
    bool stick_controller;
    int stick_controller_target;  /* 0=left stick, 1=right stick */
    bool stick_dpad;
    bool stick_gyro;
    bool stick_touch_mouse;
    int stick_touch_finger;      /* 1=first finger, 2=second finger */
    bool stick_invert_x;
    bool stick_invert_y;
    float stick_h_sens;
    float stick_v_sens;
    float stick_h_accel;
    float stick_v_accel;
    float stick_gyro_h_sens;
    float stick_gyro_v_sens;
    float stick_gyro_accel;
    bool stick_gyro_mode_roll;

    /* Trigger options */
    float trigger_deadzone_left;
    float trigger_deadzone_right;

    /* True if Mapping Selection was opened from Trigger Options dialog */
    bool mapping_from_trigger;

    /* When true, dialogs act in read-only mode (used on profile page) */
    bool dialog_read_only;

    /* Profile page focus state */
    bool profile_focus_on_new_button;  /* true = New Profile button, false = profile list */
    int profile_action_focus;          /* -1 = left column, 0..3 = Edit/Duplicate/Delete/Rename, 4 = Back */
    int profile_preview_index;         /* -1 = no preview focus, 0..N-1 = controller preview elements */
    int profile_mouse_origin_index;    /* Mouse preview index to restore when leaving action buttons */
    int profile_gamepad_origin_index;  /* Gamepad preview index to restore when leaving action buttons */

    /* Button mapping page focus state */
    int mapping_action_focus;          /* -1 = controller/mouse/keyboard grid, 0 = Restore to Defaults, 1 = Back */
    int mouse_mapping_origin_slot;     /* Last mouse slot we came from when entering action buttons */
    int mapping_gamepad_origin_index;  /* Last controller nav index when entering action buttons */
    int keyboard_mapping_origin_slot;  /* Last keyboard slot when entering action buttons */

    /* Generic focus index for the currently active dialog */
    int dialog_focus_index;

    /* Input state */
    char input_buffer[64];
    int input_cursor;
    bool show_osk;  /* Signal to show on-screen keyboard */

    /* Virtual keyboard state */
    int vk_row;     /* Current row in virtual keyboard (0-3) */
    int vk_col;     /* Current column in virtual keyboard (0-9) */

    /* Gamepad left stick navigation state (for D-pad-style navigation) */
    int nav_stick_x_dir;
    int nav_stick_y_dir;
} UIState;

/* Global UI remapper profile storage (per-window)
 * We allocate mappings for all buttons followed by all axes and then small
 * fixed sets of mouse-source slots (Left/Right/Middle/X1/X2 buttons, wheel up,
 * wheel down, and mouse move) and keyboard-source slots (W/A/S/D, arrows,
 * space/enter/escape/shift/ctrl) so the SDL_RemapperProfile can include both
 * gamepad, mouse, and keyboard sources.
 *
 * To support multiple logical profiles (default plus user-created profiles)
 * we keep a separate mapping array per profile and point g_ui_profile.mappings
 * at the slice for the currently selected profile. */

/* Maximum number of profiles supported by the UIState.profile_names array. */
#define SDL_UI_MAX_PROFILES 10

/* Number of explicit mouse source slots reserved in each profile. */
#define SDL_UI_MOUSE_MAPPING_COUNT 8

/* Number of explicit keyboard source slots reserved in each profile. */
#define SDL_UI_KEYBOARD_MAPPING_COUNT SDL_SCANCODE_COUNT

typedef enum
{
    UI_MOUSE_SLOT_LEFT = 0,
    UI_MOUSE_SLOT_RIGHT,
    UI_MOUSE_SLOT_MIDDLE,
    UI_MOUSE_SLOT_X1,
    UI_MOUSE_SLOT_X2,
    UI_MOUSE_SLOT_WHEEL_UP,
    UI_MOUSE_SLOT_WHEEL_DOWN,
    UI_MOUSE_SLOT_MOVE,
    UI_MOUSE_SLOT_COUNT
} UI_MouseSlot;

/* Keyboard slots now represent full scancode range (one slot per key) */
typedef SDL_Scancode UI_KeyboardSlot;
#define UI_KEYBOARD_SLOT_COUNT SDL_UI_KEYBOARD_MAPPING_COUNT

static SDL_RemapperProfile g_ui_profile;
static SDL_RemapperMapping g_ui_profile_mappings[SDL_UI_MAX_PROFILES][SDL_GAMEPAD_BUTTON_COUNT + SDL_GAMEPAD_AXIS_COUNT + SDL_UI_MOUSE_MAPPING_COUNT + SDL_UI_KEYBOARD_MAPPING_COUNT];
static SDL_RemapperStickMapping g_left_stick_mapping;
static SDL_RemapperStickMapping g_right_stick_mapping;
static SDL_RemapperStickMapping g_mouse_move_mapping;
static int g_ui_active_profile_index = 0;
static float g_ui_profile_trigger_deadzone_left[SDL_UI_MAX_PROFILES];
static float g_ui_profile_trigger_deadzone_right[SDL_UI_MAX_PROFILES];

/* One candidate mapping option in the Mapping Selection dialog */
typedef struct MappingOption
{
    const char *label;
    SDL_RemapperActionKind kind;
    int code;
    int value;
} MappingOption;

/* Controller / mouse / keyboard mapping option tables */
static const MappingOption controller_options[] = {
    { "None",           SDL_REMAPPER_ACTION_NONE,           0,                        0 },
    { "A Button",       SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_SOUTH, 0 },
    { "B Button",       SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_EAST,  0 },
    { "X Button",       SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_WEST,  0 },
    { "Y Button",       SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_NORTH, 0 },
    { "Left Bumper",    SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  0 },
    { "Right Bumper",   SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0 },
    { "View Button",    SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_BACK,          0 },
    { "Menu Button",    SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_START,         0 },
    { "Left Stick",     SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_LEFT_STICK,    0 },
    { "Right Stick",    SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_RIGHT_STICK,   0 },
    { "D-Pad Up",       SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_UP,       0 },
    { "D-Pad Down",     SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_DOWN,     0 },
    { "D-Pad Left",     SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_LEFT,     0 },
    { "D-Pad Right",    SDL_REMAPPER_ACTION_GAMEPAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,    0 }
};

static const MappingOption mouse_options[] = {
    { "None",             SDL_REMAPPER_ACTION_NONE,         0,                    0 },
    { "Mouse Left",       SDL_REMAPPER_ACTION_MOUSE_BUTTON, SDL_BUTTON_LEFT,      0 },
    { "Mouse Right",      SDL_REMAPPER_ACTION_MOUSE_BUTTON, SDL_BUTTON_RIGHT,     0 },
    { "Mouse Middle",     SDL_REMAPPER_ACTION_MOUSE_BUTTON, SDL_BUTTON_MIDDLE,    0 },
    { "Wheel Up",         SDL_REMAPPER_ACTION_MOUSE_WHEEL,  0,                    1 },
    { "Wheel Down",       SDL_REMAPPER_ACTION_MOUSE_WHEEL,  0,                   -1 },
    { "Mouse Move",       SDL_REMAPPER_ACTION_MOUSE_MOVEMENT, 0,                  0 }
};

static const MappingOption keyboard_options[] = {
    { "None",            SDL_REMAPPER_ACTION_NONE,         0,                          0 },

    { "A",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_A,             0 },
    { "B",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_B,             0 },
    { "C",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_C,             0 },
    { "D",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_D,             0 },
    { "E",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_E,             0 },
    { "F",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F,             0 },
    { "G",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_G,             0 },
    { "H",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_H,             0 },
    { "I",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_I,             0 },
    { "J",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_J,             0 },
    { "K",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_K,             0 },
    { "L",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_L,             0 },
    { "M",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_M,             0 },
    { "N",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_N,             0 },
    { "O",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_O,             0 },
    { "P",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_P,             0 },
    { "Q",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_Q,             0 },
    { "R",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_R,             0 },
    { "S",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_S,             0 },
    { "T",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_T,             0 },
    { "U",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_U,             0 },
    { "V",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_V,             0 },
    { "W",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_W,             0 },
    { "X",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_X,             0 },
    { "Y",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_Y,             0 },
    { "Z",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_Z,             0 },

    { "1",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_1,             0 },
    { "2",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_2,             0 },
    { "3",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_3,             0 },
    { "4",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_4,             0 },
    { "5",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_5,             0 },
    { "6",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_6,             0 },
    { "7",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_7,             0 },
    { "8",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_8,             0 },
    { "9",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_9,             0 },
    { "0",               SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_0,             0 },

    { "Enter",           SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_RETURN,        0 },
    { "Escape",          SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_ESCAPE,        0 },
    { "Backspace",       SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_BACKSPACE,     0 },
    { "Tab",             SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_TAB,           0 },
    { "Space",           SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_SPACE,         0 },

    { "Minus",           SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_MINUS,         0 },
    { "Equals",          SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_EQUALS,        0 },
    { "Left Bracket",    SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_LEFTBRACKET,   0 },
    { "Right Bracket",   SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_RIGHTBRACKET,  0 },
    { "Backslash",       SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_BACKSLASH,     0 },
    { "Non-US Hash",     SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_NONUSHASH,     0 },
    { "Semicolon",       SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_SEMICOLON,     0 },
    { "Apostrophe",      SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_APOSTROPHE,    0 },
    { "Grave",           SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_GRAVE,         0 },
    { "Comma",           SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_COMMA,         0 },
    { "Period",          SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_PERIOD,        0 },
    { "Slash",           SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_SLASH,         0 },
    { "Non-US Backslash",SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_NONUSBACKSLASH,0 },

    { "Caps Lock",       SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_CAPSLOCK,      0 },

    { "F1",              SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F1,            0 },
    { "F2",              SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F2,            0 },
    { "F3",              SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F3,            0 },
    { "F4",              SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F4,            0 },
    { "F5",              SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F5,            0 },
    { "F6",              SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F6,            0 },
    { "F7",              SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F7,            0 },
    { "F8",              SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F8,            0 },
    { "F9",              SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F9,            0 },
    { "F10",             SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F10,           0 },
    { "F11",             SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F11,           0 },
    { "F12",             SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_F12,           0 },

    { "Print Screen",    SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_PRINTSCREEN,   0 },
    { "Scroll Lock",     SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_SCROLLLOCK,    0 },
    { "Pause",           SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_PAUSE,         0 },
    { "Insert",          SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_INSERT,        0 },
    { "Home",            SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_HOME,          0 },
    { "Page Up",         SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_PAGEUP,        0 },
    { "Delete",          SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_DELETE,        0 },
    { "End",             SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_END,           0 },
    { "Page Down",       SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_PAGEDOWN,      0 },

    { "Right Arrow",     SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_RIGHT,         0 },
    { "Left Arrow",      SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_LEFT,          0 },
    { "Down Arrow",      SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_DOWN,          0 },
    { "Up Arrow",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_UP,            0 },

    { "Num Lock",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_NUMLOCKCLEAR,  0 },
    { "Keypad /",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_DIVIDE,     0 },
    { "Keypad *",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_MULTIPLY,   0 },
    { "Keypad -",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_MINUS,      0 },
    { "Keypad +",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_PLUS,       0 },
    { "Keypad Enter",    SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_ENTER,      0 },
    { "Keypad 1",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_1,          0 },
    { "Keypad 2",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_2,          0 },
    { "Keypad 3",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_3,          0 },
    { "Keypad 4",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_4,          0 },
    { "Keypad 5",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_5,          0 },
    { "Keypad 6",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_6,          0 },
    { "Keypad 7",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_7,          0 },
    { "Keypad 8",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_8,          0 },
    { "Keypad 9",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_9,          0 },
    { "Keypad 0",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_0,          0 },
    { "Keypad .",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_KP_PERIOD,     0 },

    { "Application",     SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_APPLICATION,   0 },
    { "Power",           SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_POWER,         0 },

    { "Left Ctrl",       SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_LCTRL,         0 },
    { "Left Shift",      SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_LSHIFT,        0 },
    { "Left Alt",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_LALT,          0 },
    { "Left GUI",        SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_LGUI,          0 },
    { "Right Ctrl",      SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_RCTRL,         0 },
    { "Right Shift",     SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_RSHIFT,        0 },
    { "Right Alt",       SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_RALT,          0 },
    { "Right GUI",       SDL_REMAPPER_ACTION_KEYBOARD_KEY, SDL_SCANCODE_RGUI,          0 }
};

static const MappingOption touch_options[] = {
    { "None",             SDL_REMAPPER_ACTION_NONE,              0,    0 },
    { "Touch Tap",        SDL_REMAPPER_ACTION_TOUCH_TAP,         0,    0 },
    { "Touch Hold",       SDL_REMAPPER_ACTION_TOUCH_HOLD,        0,    0 },
    { "Double Tap",       SDL_REMAPPER_ACTION_TOUCH_DOUBLE_TAP,  0,    0 },
    { "Swipe Up",         SDL_REMAPPER_ACTION_TOUCH_SWIPE_UP,    0,    0 },
    { "Swipe Down",       SDL_REMAPPER_ACTION_TOUCH_SWIPE_DOWN,  0,    0 },
    { "Swipe Left",       SDL_REMAPPER_ACTION_TOUCH_SWIPE_LEFT,  0,    0 },
    { "Swipe Right",      SDL_REMAPPER_ACTION_TOUCH_SWIPE_RIGHT, 0,    0 },
    { "Finger 2 Tap",     SDL_REMAPPER_ACTION_TOUCH_FINGER2_TAP, 0,    0 },
    { "Finger 2 Hold",    SDL_REMAPPER_ACTION_TOUCH_FINGER2_HOLD,0,    0 },
    { "Pinch In",         SDL_REMAPPER_ACTION_TOUCH_PINCH_IN,    0,    0 },
    { "Pinch Out",        SDL_REMAPPER_ACTION_TOUCH_PINCH_OUT,   0,    0 },
    { "Rotate CW",        SDL_REMAPPER_ACTION_TOUCH_ROTATE_CW,   0,    0 },
    { "Rotate CCW",       SDL_REMAPPER_ACTION_TOUCH_ROTATE_CCW,  0,    0 }
};

/* Physical keyboard layout for UK QWERTY */
typedef struct KeyPosition {
    SDL_Scancode scancode;
    int row;
    float col;    /* Column position (can be fractional for staggered rows) */
    float width;  /* In key units (1.0 = standard key width) */
    const char *label;
} KeyPosition;

static const KeyPosition uk_qwerty_layout[] = {
    /* Row 0: Number row */
    { SDL_SCANCODE_GRAVE, 0, 0, 1.0f, "`" },
    { SDL_SCANCODE_1, 0, 1, 1.0f, "1" },
    { SDL_SCANCODE_2, 0, 2, 1.0f, "2" },
    { SDL_SCANCODE_3, 0, 3, 1.0f, "3" },
    { SDL_SCANCODE_4, 0, 4, 1.0f, "4" },
    { SDL_SCANCODE_5, 0, 5, 1.0f, "5" },
    { SDL_SCANCODE_6, 0, 6, 1.0f, "6" },
    { SDL_SCANCODE_7, 0, 7, 1.0f, "7" },
    { SDL_SCANCODE_8, 0, 8, 1.0f, "8" },
    { SDL_SCANCODE_9, 0, 9, 1.0f, "9" },
    { SDL_SCANCODE_0, 0, 10, 1.0f, "0" },
    { SDL_SCANCODE_MINUS, 0, 11, 1.0f, "-" },
    { SDL_SCANCODE_EQUALS, 0, 12, 1.0f, "=" },
    { SDL_SCANCODE_BACKSPACE, 0, 13, 2.0f, "Backspace" },

    /* Row 1: QWERTY row */
    { SDL_SCANCODE_TAB, 1, 0, 1.5f, "Tab" },
    { SDL_SCANCODE_Q, 1, 1.5f, 1.0f, "Q" },
    { SDL_SCANCODE_W, 1, 2.5f, 1.0f, "W" },
    { SDL_SCANCODE_E, 1, 3.5f, 1.0f, "E" },
    { SDL_SCANCODE_R, 1, 4.5f, 1.0f, "R" },
    { SDL_SCANCODE_T, 1, 5.5f, 1.0f, "T" },
    { SDL_SCANCODE_Y, 1, 6.5f, 1.0f, "Y" },
    { SDL_SCANCODE_U, 1, 7.5f, 1.0f, "U" },
    { SDL_SCANCODE_I, 1, 8.5f, 1.0f, "I" },
    { SDL_SCANCODE_O, 1, 9.5f, 1.0f, "O" },
    { SDL_SCANCODE_P, 1, 10.5f, 1.0f, "P" },
    { SDL_SCANCODE_LEFTBRACKET, 1, 11.5f, 1.0f, "[" },
    { SDL_SCANCODE_RIGHTBRACKET, 1, 12.5f, 1.0f, "]" },
    { SDL_SCANCODE_RETURN, 1, 13.5f, 1.5f, "Enter" },

    /* Row 2: ASDF row */
    { SDL_SCANCODE_CAPSLOCK, 2, 0, 1.75f, "Caps" },
    { SDL_SCANCODE_A, 2, 1.75f, 1.0f, "A" },
    { SDL_SCANCODE_S, 2, 2.75f, 1.0f, "S" },
    { SDL_SCANCODE_D, 2, 3.75f, 1.0f, "D" },
    { SDL_SCANCODE_F, 2, 4.75f, 1.0f, "F" },
    { SDL_SCANCODE_G, 2, 5.75f, 1.0f, "G" },
    { SDL_SCANCODE_H, 2, 6.75f, 1.0f, "H" },
    { SDL_SCANCODE_J, 2, 7.75f, 1.0f, "J" },
    { SDL_SCANCODE_K, 2, 8.75f, 1.0f, "K" },
    { SDL_SCANCODE_L, 2, 9.75f, 1.0f, "L" },
    { SDL_SCANCODE_SEMICOLON, 2, 10.75f, 1.0f, ";" },
    { SDL_SCANCODE_APOSTROPHE, 2, 11.75f, 1.0f, "'" },
    { SDL_SCANCODE_NONUSHASH, 2, 12.75f, 1.0f, "#" },

    /* Row 3: ZXCV row */
    { SDL_SCANCODE_LSHIFT, 3, 0, 1.25f, "LShift" },
    { SDL_SCANCODE_NONUSBACKSLASH, 3, 1.25f, 1.0f, "\\" },
    { SDL_SCANCODE_Z, 3, 2.25f, 1.0f, "Z" },
    { SDL_SCANCODE_X, 3, 3.25f, 1.0f, "X" },
    { SDL_SCANCODE_C, 3, 4.25f, 1.0f, "C" },
    { SDL_SCANCODE_V, 3, 5.25f, 1.0f, "V" },
    { SDL_SCANCODE_B, 3, 6.25f, 1.0f, "B" },
    { SDL_SCANCODE_N, 3, 7.25f, 1.0f, "N" },
    { SDL_SCANCODE_M, 3, 8.25f, 1.0f, "M" },
    { SDL_SCANCODE_COMMA, 3, 9.25f, 1.0f, "," },
    { SDL_SCANCODE_PERIOD, 3, 10.25f, 1.0f, "." },
    { SDL_SCANCODE_SLASH, 3, 11.25f, 1.0f, "/" },
    { SDL_SCANCODE_RSHIFT, 3, 12.25f, 2.75f, "RShift" },

    /* Row 4: Bottom row */
    { SDL_SCANCODE_LCTRL, 4, 0, 1.5f, "LCtrl" },
    { SDL_SCANCODE_LGUI, 4, 1.5f, 1.25f, "Win" },
    { SDL_SCANCODE_LALT, 4, 2.75f, 1.25f, "LAlt" },
    { SDL_SCANCODE_SPACE, 4, 4.0f, 6.25f, "Space" },
    { SDL_SCANCODE_RALT, 4, 10.25f, 1.25f, "RAlt" },
    { SDL_SCANCODE_RGUI, 4, 11.5f, 1.25f, "Fn" },
    { SDL_SCANCODE_APPLICATION, 4, 12.75f, 1.25f, "Menu" },
    { SDL_SCANCODE_RCTRL, 4, 14.0f, 1.0f, "RCtrl" },

    /* Function keys (row -1, above number row) */
    { SDL_SCANCODE_ESCAPE, -1, 0, 1.0f, "Esc" },
    { SDL_SCANCODE_F1, -1, 2, 1.0f, "F1" },
    { SDL_SCANCODE_F2, -1, 3, 1.0f, "F2" },
    { SDL_SCANCODE_F3, -1, 4, 1.0f, "F3" },
    { SDL_SCANCODE_F4, -1, 5, 1.0f, "F4" },
    { SDL_SCANCODE_F5, -1, 6.5f, 1.0f, "F5" },
    { SDL_SCANCODE_F6, -1, 7.5f, 1.0f, "F6" },
    { SDL_SCANCODE_F7, -1, 8.5f, 1.0f, "F7" },
    { SDL_SCANCODE_F8, -1, 9.5f, 1.0f, "F8" },
    { SDL_SCANCODE_F9, -1, 11.0f, 1.0f, "F9" },
    { SDL_SCANCODE_F10, -1, 12.0f, 1.0f, "F10" },
    { SDL_SCANCODE_F11, -1, 13.0f, 1.0f, "F11" },
    { SDL_SCANCODE_F12, -1, 14.0f, 1.0f, "F12" },

    /* Navigation cluster (right side) */
    { SDL_SCANCODE_PRINTSCREEN, -1, 15.5f, 1.0f, "PrtSc" },
    { SDL_SCANCODE_SCROLLLOCK, -1, 16.5f, 1.0f, "ScrLk" },
    { SDL_SCANCODE_PAUSE, -1, 17.5f, 1.0f, "Pause" },

    { SDL_SCANCODE_INSERT, 0, 15.5f, 1.0f, "Ins" },
    { SDL_SCANCODE_HOME, 0, 16.5f, 1.0f, "Home" },
    { SDL_SCANCODE_PAGEUP, 0, 17.5f, 1.0f, "PgUp" },

    { SDL_SCANCODE_DELETE, 1, 15.5f, 1.0f, "Del" },
    { SDL_SCANCODE_END, 1, 16.5f, 1.0f, "End" },
    { SDL_SCANCODE_PAGEDOWN, 1, 17.5f, 1.0f, "PgDn" },

    { SDL_SCANCODE_UP, 2, 16.5f, 1.0f, "Γåæ" },

    { SDL_SCANCODE_LEFT, 3, 15.5f, 1.0f, "ΓåÉ" },
    { SDL_SCANCODE_DOWN, 3, 16.5f, 1.0f, "Γåô" },
    { SDL_SCANCODE_RIGHT, 3, 17.5f, 1.0f, "ΓåÆ" },

    /* Numpad (rightmost section) */
    { SDL_SCANCODE_NUMLOCKCLEAR, -1, 19.0f, 1.0f, "Num" },
    { SDL_SCANCODE_KP_DIVIDE, -1, 20.0f, 1.0f, "/" },
    { SDL_SCANCODE_KP_MULTIPLY, -1, 21.0f, 1.0f, "*" },
    { SDL_SCANCODE_KP_MINUS, -1, 22.0f, 1.0f, "-" },

    { SDL_SCANCODE_KP_7, 0, 19.0f, 1.0f, "7" },
    { SDL_SCANCODE_KP_8, 0, 20.0f, 1.0f, "8" },
    { SDL_SCANCODE_KP_9, 0, 21.0f, 1.0f, "9" },
    { SDL_SCANCODE_KP_PLUS, 0, 22.0f, 1.0f, "+" },

    { SDL_SCANCODE_KP_4, 1, 19.0f, 1.0f, "4" },
    { SDL_SCANCODE_KP_5, 1, 20.0f, 1.0f, "5" },
    { SDL_SCANCODE_KP_6, 1, 21.0f, 1.0f, "6" },

    { SDL_SCANCODE_KP_1, 2, 19.0f, 1.0f, "1" },
    { SDL_SCANCODE_KP_2, 2, 20.0f, 1.0f, "2" },
    { SDL_SCANCODE_KP_3, 2, 21.0f, 1.0f, "3" },
    { SDL_SCANCODE_KP_ENTER, 2, 22.0f, 1.0f, "Enter" },

    { SDL_SCANCODE_KP_0, 3, 19.0f, 2.0f, "0" },
    { SDL_SCANCODE_KP_PERIOD, 3, 21.0f, 1.0f, "." }
};

static const int uk_qwerty_layout_count = sizeof(uk_qwerty_layout) / sizeof(uk_qwerty_layout[0]);

static void
UI_ComputeKeyboardLayoutBounds(float key_unit, float gap,
                               float *out_min_x, float *out_max_x,
                               float *out_min_y, float *out_max_y)
{
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    bool first = true;

    for (int i = 0; i < uk_qwerty_layout_count; ++i) {
        const KeyPosition *kp = &uk_qwerty_layout[i];

        float x = kp->col * (key_unit + gap);
        float w = kp->width * key_unit + (kp->width - 1.0f) * gap;
        float y = kp->row * (key_unit + gap);
        float h = key_unit;

        if (kp->scancode == SDL_SCANCODE_KP_ENTER || kp->scancode == SDL_SCANCODE_KP_PLUS) {
            h = key_unit * 2.0f + gap;
        }

        if (first) {
            min_x = x;
            max_x = x + w;
            min_y = y;
            max_y = y + h;
            first = false;
        } else {
            if (x < min_x) {
                min_x = x;
            }
            if (x + w > max_x) {
                max_x = x + w;
            }
            if (y < min_y) {
                min_y = y;
            }
            if (y + h > max_y) {
                max_y = y + h;
            }
        }
    }

    if (out_min_x) {
        *out_min_x = min_x;
    }
    if (out_max_x) {
        *out_max_x = max_x;
    }
    if (out_min_y) {
        *out_min_y = min_y;
    }
    if (out_max_y) {
        *out_max_y = max_y;
    }
}

/* Helper: get mapping for button/axis given an explicit profile index.
 * This is used by callers that know the current UIState and want the
 * mapping slice for state->selected_profile. */
static SDL_RemapperMapping *
UI_GetMappingForButtonInProfile(SDL_GamepadButton button, int profile_index)
{
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) {
        return NULL;
    }
    if (profile_index < 0 || profile_index >= SDL_UI_MAX_PROFILES) {
        return NULL;
    }
    return &g_ui_profile_mappings[profile_index][(int)button];
}

static SDL_RemapperMapping *
UI_GetMappingForAxisInProfile(SDL_GamepadAxis axis, int profile_index)
{
    if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) {
        return NULL;
    }
    if (profile_index < 0 || profile_index >= SDL_UI_MAX_PROFILES) {
        return NULL;
    }
    return &g_ui_profile_mappings[profile_index][SDL_GAMEPAD_BUTTON_COUNT + (int)axis];
}

/* Helper: get mapping for a given gamepad button in the current UI profile */
static SDL_RemapperMapping *
UI_GetMappingForButton(SDL_GamepadButton button)
{
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) {
        return NULL;
    }

    int p = g_ui_active_profile_index;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    return &g_ui_profile_mappings[p][(int)button];
}

/* Helper: get mapping for a given gamepad axis (used for triggers/sticks) */
static SDL_RemapperMapping *
UI_GetMappingForAxis(SDL_GamepadAxis axis)
{
    if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) {
        return NULL;
    }

    int p = g_ui_active_profile_index;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    return &g_ui_profile_mappings[p][SDL_GAMEPAD_BUTTON_COUNT + (int)axis];
}

/* Helpers: per-profile mouse source mapping slots (Left/Right/Middle/X1/X2,
 * Wheel Up/Down, and Mouse Move). */
static SDL_RemapperMapping *
UI_GetMouseSlotMappingInProfile(UI_MouseSlot slot, int profile_index)
{
    int base;

    if (slot < 0 || slot >= UI_MOUSE_SLOT_COUNT) {
        return NULL;
    }
    if (profile_index < 0 || profile_index >= SDL_UI_MAX_PROFILES) {
        return NULL;
    }

    base = SDL_GAMEPAD_BUTTON_COUNT + SDL_GAMEPAD_AXIS_COUNT;
    return &g_ui_profile_mappings[profile_index][base + (int)slot];
}

static SDL_RemapperMapping *
UI_GetMouseSlotMapping(UI_MouseSlot slot)
{
    int p = g_ui_active_profile_index;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    return UI_GetMouseSlotMappingInProfile(slot, p);
}

/* Helpers: per-profile keyboard source slots. */
static SDL_RemapperMapping *
UI_GetKeyboardSlotMappingInProfile(UI_KeyboardSlot slot, int profile_index)
{
    int base;

    if (slot < 0 || slot >= UI_KEYBOARD_SLOT_COUNT) {
        return NULL;
    }
    if (profile_index < 0 || profile_index >= SDL_UI_MAX_PROFILES) {
        return NULL;
    }

    base = SDL_GAMEPAD_BUTTON_COUNT + SDL_GAMEPAD_AXIS_COUNT + SDL_UI_MOUSE_MAPPING_COUNT;
    return &g_ui_profile_mappings[profile_index][base + (int)slot];
}

static SDL_RemapperMapping *
UI_GetKeyboardSlotMapping(UI_KeyboardSlot slot)
{
    int p = g_ui_active_profile_index;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    return UI_GetKeyboardSlotMappingInProfile(slot, p);
}

static void
UI_ResetKeyboardMappingsToDefaults(UIState *state)
{
    if (!state) {
        return;
    }

    int p = g_ui_active_profile_index;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    for (int slot = 0; slot < UI_KEYBOARD_SLOT_COUNT; slot++) {
        SDL_RemapperMapping *mapping = UI_GetKeyboardSlotMappingInProfile((UI_KeyboardSlot)slot, p);
        if (mapping) {
            SDL_zero(*mapping);
        }
    }
}

/* Reset all mouse slot mappings to defaults (clear all mappings) */
static void
UI_ResetMouseMappingsToDefaults(UIState *state)
{
    if (!state) {
        return;
    }

    int p = g_ui_active_profile_index;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    /* Clear all mouse slot mappings for this profile */
    for (int slot = 0; slot < UI_MOUSE_SLOT_COUNT; slot++) {
        SDL_RemapperMapping *mapping = UI_GetMouseSlotMappingInProfile((UI_MouseSlot)slot, p);
        if (mapping) {
            SDL_zero(*mapping);
        }
    }
}

static void
UI_CommitProfileToContext(SDL_RemapperContext *ctx,
                          SDL_JoystickID gamepad_id,
                          UIState *state)
{
    if (!state) {
        return;
    }

    int p = state->selected_profile;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    g_ui_active_profile_index = p;

    g_ui_profile.name = state->profile_names[p];
    g_ui_profile.num_mappings = SDL_GAMEPAD_BUTTON_COUNT + SDL_GAMEPAD_AXIS_COUNT + SDL_UI_MOUSE_MAPPING_COUNT + SDL_UI_KEYBOARD_MAPPING_COUNT;
    g_ui_profile.mappings = g_ui_profile_mappings[p];

    /* Keep trigger deadzones in sync between UI and the profile struct. */
    if (g_ui_profile_trigger_deadzone_left[p] <= 0.0f) {
        g_ui_profile_trigger_deadzone_left[p] = 50.0f;
    }
    if (g_ui_profile_trigger_deadzone_right[p] <= 0.0f) {
        g_ui_profile_trigger_deadzone_right[p] = 50.0f;
    }
    state->trigger_deadzone_left = g_ui_profile_trigger_deadzone_left[p];
    state->trigger_deadzone_right = g_ui_profile_trigger_deadzone_right[p];
    g_ui_profile.left_trigger_deadzone = g_ui_profile_trigger_deadzone_left[p];
    g_ui_profile.right_trigger_deadzone = g_ui_profile_trigger_deadzone_right[p];

    if (ctx) {
        UIDeviceType device_type = UI_DEVICE_TYPE_GAMEPAD;

        if (state->device_count > 0) {
            int dev_idx = state->selected_device;
            if (dev_idx < 0) {
                dev_idx = 0;
            } else if (dev_idx >= state->device_count) {
                dev_idx = state->device_count - 1;
            }
            device_type = state->device_types[dev_idx];
        }

        switch (device_type) {
        case UI_DEVICE_TYPE_MOUSE:
            /* Bind this profile to the default mouse (ID 0) and any specific mouse. */
            g_ui_profile.gamepad_id = 0;
            SDL_SetRemapperMouseProfile(ctx, 0, &g_ui_profile);  /* Default/virtual mouse */
            if (state->active_mouse_id != 0) {
                SDL_SetRemapperMouseProfile(ctx, state->active_mouse_id, &g_ui_profile);
            }
            break;
        case UI_DEVICE_TYPE_KEYBOARD:
            /* Bind this profile to the active keyboard device, if any. */
            g_ui_profile.gamepad_id = 0;
            if (state->active_keyboard_id != 0) {
                SDL_SetRemapperKeyboardProfile(ctx, state->active_keyboard_id, &g_ui_profile);
            }
            break;
        case UI_DEVICE_TYPE_GAMEPAD:
        default:
            /* Default behavior: bind profile to the selected gamepad. */
            g_ui_profile.gamepad_id = gamepad_id;
            SDL_SetRemapperProfile(ctx, gamepad_id, &g_ui_profile);
            break;
        }
    }
}

static void
UI_InitProfileMappings(int profile_index)
{
    if (profile_index < 0 || profile_index >= SDL_UI_MAX_PROFILES) {
        return;
    }

    SDL_RemapperMapping *mappings = g_ui_profile_mappings[profile_index];
    SDL_memset(mappings, 0, sizeof(g_ui_profile_mappings[profile_index]));

    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
        SDL_RemapperMapping *m = &mappings[i];
        m->source_type = SDL_REMAPPER_SOURCE_BUTTON;
        m->source.button = (SDL_GamepadButton)i;
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->stick_mapping = NULL;
    }

    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
        SDL_RemapperMapping *m = &mappings[SDL_GAMEPAD_BUTTON_COUNT + i];
        m->source_type = SDL_REMAPPER_SOURCE_AXIS;
        m->source.axis = (SDL_GamepadAxis)i;
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->stick_mapping = NULL;
    }

    /* Initialize explicit mouse-source slots (shared for all devices, but
     * primarily used when the Mouse device is selected). */
    {
        int base = SDL_GAMEPAD_BUTTON_COUNT + SDL_GAMEPAD_AXIS_COUNT;
        SDL_RemapperMapping *m;

        /* Left Button */
        m = &mappings[base + UI_MOUSE_SLOT_LEFT];
        m->source_type = SDL_REMAPPER_SOURCE_MOUSE_BUTTON;
        m->source.button = SDL_BUTTON_LEFT;
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;

        /* Right Button */
        m = &mappings[base + UI_MOUSE_SLOT_RIGHT];
        m->source_type = SDL_REMAPPER_SOURCE_MOUSE_BUTTON;
        m->source.button = SDL_BUTTON_RIGHT;
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;

        /* Middle Button */
        m = &mappings[base + UI_MOUSE_SLOT_MIDDLE];
        m->source_type = SDL_REMAPPER_SOURCE_MOUSE_BUTTON;
        m->source.button = SDL_BUTTON_MIDDLE;
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;

        /* X1 Button */
        m = &mappings[base + UI_MOUSE_SLOT_X1];
        m->source_type = SDL_REMAPPER_SOURCE_MOUSE_BUTTON;
        m->source.button = SDL_BUTTON_X1;
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;

        /* X2 Button */
        m = &mappings[base + UI_MOUSE_SLOT_X2];
        m->source_type = SDL_REMAPPER_SOURCE_MOUSE_BUTTON;
        m->source.button = SDL_BUTTON_X2;
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;

        /* Wheel Up */
        m = &mappings[base + UI_MOUSE_SLOT_WHEEL_UP];
        m->source_type = SDL_REMAPPER_SOURCE_MOUSE_WHEEL;
        m->source.axis = 0; /* vertical up */
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;

        /* Wheel Down */
        m = &mappings[base + UI_MOUSE_SLOT_WHEEL_DOWN];
        m->source_type = SDL_REMAPPER_SOURCE_MOUSE_WHEEL;
        m->source.axis = 1; /* vertical down */
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;

        /* Mouse Move (placeholder for future motion handling) */
        m = &mappings[base + UI_MOUSE_SLOT_MOVE];
        m->source_type = SDL_REMAPPER_SOURCE_MOUSE_MOTION;
        m->source.axis = 0;
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;
    }

    /* Initialize keyboard-source slots for all scancodes (full keyboard support) */
    {
        int base = SDL_GAMEPAD_BUTTON_COUNT + SDL_GAMEPAD_AXIS_COUNT + SDL_UI_MOUSE_MAPPING_COUNT;

        for (int i = 0; i < SDL_UI_KEYBOARD_MAPPING_COUNT; ++i) {
            SDL_RemapperMapping *m = &mappings[base + i];
            m->source_type = SDL_REMAPPER_SOURCE_KEYBOARD_KEY;
            m->source.button = (SDL_Scancode)i;
            m->use_as_shift = false;
            m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
            m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
            m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;
        }
    }
}

static void
UI_InitGamepadPassthroughDefaultsForProfile(int profile_index)
{
    if (profile_index < 0 || profile_index >= SDL_UI_MAX_PROFILES) {
        return;
    }

    SDL_RemapperMapping *mappings = g_ui_profile_mappings[profile_index];
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
        SDL_RemapperMapping *m = &mappings[i];
        m->primary_action.kind = SDL_REMAPPER_ACTION_GAMEPAD_BUTTON;
        m->primary_action.code = i;
        m->primary_action.value = 0;
    }
}

static void
UI_InitMouseKeyboardDefaultsForProfile(int profile_index)
{
    (void)profile_index;
}

/* Apply a loaded SDL_RemapperProfile (from disk) into one UI profile slot. */
static void
UI_ApplyLoadedProfileToSlot(const SDL_RemapperProfile *loaded, int profile_index)
{
    int expected;
    int count;
    SDL_RemapperMapping *dst;

    if (!loaded) {
        return;
    }
    if (profile_index < 0 || profile_index >= SDL_UI_MAX_PROFILES) {
        return;
    }

    expected = SDL_GAMEPAD_BUTTON_COUNT + SDL_GAMEPAD_AXIS_COUNT + SDL_UI_MOUSE_MAPPING_COUNT + SDL_UI_KEYBOARD_MAPPING_COUNT;
    count = loaded->num_mappings;
    if (count > expected) {
        count = expected;
    }

    /* Start from a clean default mapping for this slot. */
    UI_InitProfileMappings(profile_index);
    dst = g_ui_profile_mappings[profile_index];

    for (int i = 0; i < count; ++i) {
        SDL_RemapperMapping *dstm = &dst[i];
        const SDL_RemapperMapping *srcm = &loaded->mappings[i];

        /* Preserve source_type/source set up by UI_InitProfileMappings based on index.
         * Only copy the logical actions and flags. */
        dstm->use_as_shift = srcm->use_as_shift;
        dstm->primary_action = srcm->primary_action;
        dstm->shift_action = srcm->shift_action;
        dstm->hold_action = srcm->hold_action;
    }
}

/* Save the currently selected UI profile to disk using SDL_SaveRemapperProfile. */
static void
UI_SaveCurrentProfileToDisk(const UIState *state)
{
    int p;
    const char *name;
    char safe_name[64];
    char filename[128];

    if (!state) {
        return;
    }

    p = state->selected_profile;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    name = state->profile_names[p];
    if (!name || !*name) {
        name = "Profile";
    }

    SDL_strlcpy(safe_name, name, sizeof(safe_name));
    for (char *c = safe_name; *c; ++c) {
        if (*c == ' ' || *c == '\t' || *c == '/' || *c == '\\' || *c == ':' ||
            *c == '*' || *c == '?' || *c == '"' || *c == '<' || *c == '>' || *c == '|') {
            *c = '_';
        }
    }

    SDL_snprintf(filename, sizeof(filename), "%s.profile", safe_name);

    if (!g_ui_profile.mappings || g_ui_profile.num_mappings <= 0) {
        return;
    }

    SDL_SaveRemapperProfile(&g_ui_profile, filename);
}

/* Load any profiles persisted on disk and merge them into the UI profile list. */
static void
UI_LoadProfilesFromDisk(SDL_RemapperContext *ctx,
                        SDL_JoystickID gamepad_id,
                        UIState *state)
{
    int file_count = 0;
    char **files;

    if (!state) {
        return;
    }

    files = SDL_GetRemapperProfileList(&file_count);
    if (!files || file_count <= 0) {
        if (files) {
            SDL_FreeRemapperProfileList(files, file_count);
        }
        /* No persisted profiles yet; commit the in-memory defaults. */
        UI_CommitProfileToContext(ctx, gamepad_id, state);
        return;
    }

    for (int i = 0; i < file_count; ++i) {
        SDL_RemapperProfile *loaded = SDL_LoadRemapperProfile(files[i]);
        int target_index = -1;

        if (!loaded) {
            continue;
        }

        if (loaded->name && *loaded->name) {
            for (int p = 0; p < state->profile_count; ++p) {
                if (SDL_strcmp(state->profile_names[p], loaded->name) == 0) {
                    target_index = p;
                    break;
                }
            }

            if (target_index == -1 && state->profile_count < SDL_UI_MAX_PROFILES) {
                target_index = state->profile_count;
                SDL_strlcpy(state->profile_names[target_index], loaded->name,
                            sizeof(state->profile_names[target_index]));
                state->profile_count++;
            }
        }

        if (target_index >= 0 && target_index < SDL_UI_MAX_PROFILES) {
            UI_ApplyLoadedProfileToSlot(loaded, target_index);

            /* Persist loaded trigger deadzones into our per-profile arrays. */
            {
                float left = loaded->left_trigger_deadzone;
                float right = loaded->right_trigger_deadzone;
                if (left <= 0.0f) {
                    left = 50.0f;
                }
                if (right <= 0.0f) {
                    right = 50.0f;
                }
                g_ui_profile_trigger_deadzone_left[target_index] = left;
                g_ui_profile_trigger_deadzone_right[target_index] = right;
            }
        }

        SDL_FreeRemapperProfile(loaded);
    }

    SDL_FreeRemapperProfileList(files, file_count);

    UI_CommitProfileToContext(ctx, gamepad_id, state);
}

/* Helper: format a SDL_RemapperAction into a short user-facing label */
static void
UI_FormatActionText(const SDL_RemapperAction *action, char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return;
    }

    if (!action || action->kind == SDL_REMAPPER_ACTION_NONE) {
        SDL_strlcpy(buffer, "None", buffer_len);
        return;
    }

    switch (action->kind) {
    case SDL_REMAPPER_ACTION_GAMEPAD_BUTTON: {
        const char *name = NULL;
        switch ((SDL_GamepadButton)action->code) {
        case SDL_GAMEPAD_BUTTON_SOUTH: name = "A"; break;
        case SDL_GAMEPAD_BUTTON_EAST:  name = "B"; break;
        case SDL_GAMEPAD_BUTTON_WEST:  name = "X"; break;
        case SDL_GAMEPAD_BUTTON_NORTH: name = "Y"; break;
        case SDL_GAMEPAD_BUTTON_BACK:  name = "View"; break;
        case SDL_GAMEPAD_BUTTON_GUIDE: name = "Guide"; break;
        case SDL_GAMEPAD_BUTTON_START: name = "Menu"; break;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:  name = "L Stick"; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: name = "R Stick"; break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  name = "LB"; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: name = "RB"; break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:    name = "DPad Up"; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  name = "DPad Down"; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  name = "DPad Left"; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: name = "DPad Right"; break;
        case SDL_GAMEPAD_BUTTON_MISC1: name = "Misc1"; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: name = "R Paddle 1"; break;
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:  name = "L Paddle 1"; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: name = "R Paddle 2"; break;
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:  name = "L Paddle 2"; break;
        case SDL_GAMEPAD_BUTTON_TOUCHPAD: name = "Touchpad"; break;
        case SDL_GAMEPAD_BUTTON_MISC2: name = "Misc2"; break;
        case SDL_GAMEPAD_BUTTON_MISC3: name = "Misc3"; break;
        case SDL_GAMEPAD_BUTTON_MISC4: name = "Misc4"; break;
        case SDL_GAMEPAD_BUTTON_MISC5: name = "Misc5"; break;
        case SDL_GAMEPAD_BUTTON_MISC6: name = "Misc6"; break;
        default: break;
        }
        if (name && *name) {
            SDL_strlcpy(buffer, name, buffer_len);
        } else {
            SDL_snprintf(buffer, buffer_len, "Gamepad %d", action->code);
        }
        break;
    }
    case SDL_REMAPPER_ACTION_GAMEPAD_AXIS:
        SDL_snprintf(buffer, buffer_len, "Axis %d", action->code);
        break;
    case SDL_REMAPPER_ACTION_KEYBOARD_KEY: {
        const char *name = SDL_GetScancodeName((SDL_Scancode)action->code);
        if (name && *name) {
            SDL_strlcpy(buffer, name, buffer_len);
        } else {
            SDL_snprintf(buffer, buffer_len, "Key %d", action->code);
        }
        break;
    }
    case SDL_REMAPPER_ACTION_MOUSE_BUTTON:
        if (action->code == SDL_BUTTON_LEFT) {
            SDL_strlcpy(buffer, "Mouse Left", buffer_len);
        } else if (action->code == SDL_BUTTON_RIGHT) {
            SDL_strlcpy(buffer, "Mouse Right", buffer_len);
        } else if (action->code == SDL_BUTTON_MIDDLE) {
            SDL_strlcpy(buffer, "Mouse Middle", buffer_len);
        } else if (action->code == SDL_BUTTON_X1) {
            SDL_strlcpy(buffer, "Mouse Back", buffer_len);
        } else if (action->code == SDL_BUTTON_X2) {
            SDL_strlcpy(buffer, "Mouse Forward", buffer_len);
        } else {
            SDL_snprintf(buffer, buffer_len, "Mouse Button %d", action->code);
        }
        break;
    case SDL_REMAPPER_ACTION_MOUSE_WHEEL:
        if (action->value > 0) {
            SDL_strlcpy(buffer, "Wheel Up", buffer_len);
        } else if (action->value < 0) {
            SDL_strlcpy(buffer, "Wheel Down", buffer_len);
        } else {
            SDL_strlcpy(buffer, "Mouse Wheel", buffer_len);
        }
        break;
    case SDL_REMAPPER_ACTION_MOUSE_MOVEMENT:
        SDL_strlcpy(buffer, "Mouse Move", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_TAP:
        SDL_strlcpy(buffer, "Touch Tap", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_HOLD:
        SDL_strlcpy(buffer, "Touch Hold", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_DOUBLE_TAP:
        SDL_strlcpy(buffer, "Double Tap", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_SWIPE_UP:
        SDL_strlcpy(buffer, "Swipe Up", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_SWIPE_DOWN:
        SDL_strlcpy(buffer, "Swipe Down", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_SWIPE_LEFT:
        SDL_strlcpy(buffer, "Swipe Left", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_SWIPE_RIGHT:
        SDL_strlcpy(buffer, "Swipe Right", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_FINGER2_TAP:
        SDL_strlcpy(buffer, "Finger 2 Tap", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_FINGER2_HOLD:
        SDL_strlcpy(buffer, "Finger 2 Hold", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_PINCH_IN:
        SDL_strlcpy(buffer, "Pinch In", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_PINCH_OUT:
        SDL_strlcpy(buffer, "Pinch Out", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_ROTATE_CW:
        SDL_strlcpy(buffer, "Rotate CW", buffer_len);
        break;
    case SDL_REMAPPER_ACTION_TOUCH_ROTATE_CCW:
        SDL_strlcpy(buffer, "Rotate CCW", buffer_len);
        break;
    default:
        SDL_strlcpy(buffer, "Unknown", buffer_len);
        break;
    }
}

/* Helper: summarize a stick mapping into a short label (for LS/RS Move buttons) */
static void
UI_FormatStickSummary(const SDL_RemapperStickMapping *stick, char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return;
    }

    if (!stick) {
        SDL_strlcpy(buffer, "None", buffer_len);
        return;
    }

    if (stick->map_to_gyroscope) {
        SDL_strlcpy(buffer, "Gyroscope", buffer_len);
    } else if (stick->map_to_touch_mouse) {
        SDL_strlcpy(buffer, "Touch Mouse", buffer_len);
    } else if (stick->map_to_mouse_movement) {
        SDL_strlcpy(buffer, "Mouse", buffer_len);
    } else if (stick->map_to_wasd) {
        SDL_strlcpy(buffer, "WASD", buffer_len);
    } else if (stick->map_to_arrow_keys) {
        SDL_strlcpy(buffer, "Arrow Keys", buffer_len);
    } else if (stick->map_to_controller_movement) {
        SDL_strlcpy(buffer, "Controller", buffer_len);
    } else if (stick->map_to_dpad) {
        SDL_strlcpy(buffer, "D-Pad", buffer_len);
    } else {
        SDL_strlcpy(buffer, "None", buffer_len);
    }
}

/* Helper: get currently active MappingOption table for the mapping dialog */
static void
UI_GetActiveOptions(const UIState *state, const MappingOption **out_options, int *out_count)
{
    if (!out_options || !out_count) {
        return;
    }

    switch (state->active_tab) {
    default:
    case 0:
        *out_options = controller_options;
        *out_count = SDL_UI_ARRAY_SIZE(controller_options);
        break;
    case 1:
        *out_options = mouse_options;
        *out_count = SDL_UI_ARRAY_SIZE(mouse_options);
        break;
    case 2:
        *out_options = keyboard_options;
        *out_count = SDL_UI_ARRAY_SIZE(keyboard_options);
        break;
    case 3:
        *out_options = touch_options;
        *out_count = SDL_UI_ARRAY_SIZE(touch_options);
        break;
    }
}

/* Apply a chosen MappingOption to a given mapping + slot, then push into SDL_RemapperContext */
static void
UI_ApplyMappingToSlot(SDL_RemapperContext *ctx,
                      SDL_JoystickID gamepad_id,
                      SDL_RemapperMapping *mapping,
                      int slot,
                      const MappingOption *opt,
                      UIState *state)
{
    SDL_RemapperAction *target = NULL;

    if (!mapping || !opt) {
        return;
    }

    switch (slot) {
    case 0: target = &mapping->primary_action; break;
    case 1: target = &mapping->shift_action;   break;
    case 2: target = &mapping->hold_action;    break;
    default: return;
    }

    target->kind = opt->kind;
    target->code = opt->code;
    target->value = opt->value;

    UI_CommitProfileToContext(ctx, gamepad_id, state);

    if (state) {
        UI_SaveCurrentProfileToDisk(state);
    }
}

/* Apply a chosen MappingOption to a given button + slot */
static void
UI_ApplyMappingOption(SDL_RemapperContext *ctx,
                      SDL_JoystickID gamepad_id,
                      SDL_GamepadButton button,
                      int slot,
                      const MappingOption *opt,
                      UIState *state)
{
    int p = 0;
    if (state) {
        p = state->selected_profile;
        if (p < 0) {
            p = 0;
        } else if (p >= SDL_UI_MAX_PROFILES) {
            p = SDL_UI_MAX_PROFILES - 1;
        }
    }
    SDL_RemapperMapping *mapping = UI_GetMappingForButtonInProfile(button, p);
    UI_ApplyMappingToSlot(ctx, gamepad_id, mapping, slot, opt, state);
}

/* Load stick config in UIState from the stick mapping associated with an axis */
static void
UI_LoadStickStateFromAxis(SDL_GamepadAxis axis, UIState *state)
{
    SDL_RemapperMapping *mapping;
    SDL_RemapperStickMapping *stick = NULL;

    if (!state) {
        return;
    }

    int p = state->selected_profile;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    mapping = UI_GetMappingForAxisInProfile(axis, p);
    if (mapping) {
        stick = mapping->stick_mapping;
    }

    if (!stick) {
        if (axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY) {
            SDL_memset(&g_left_stick_mapping, 0, sizeof(g_left_stick_mapping));
            stick = &g_left_stick_mapping;
        } else if (axis == SDL_GAMEPAD_AXIS_RIGHTX || axis == SDL_GAMEPAD_AXIS_RIGHTY) {
            SDL_memset(&g_right_stick_mapping, 0, sizeof(g_right_stick_mapping));
            stick = &g_right_stick_mapping;
        }
        if (mapping) {
            mapping->stick_mapping = stick;
        }
    }

    if (!stick) {
        /* Nothing to load */
        state->stick_wasd = false;
        state->stick_arrows = false;
        state->stick_mouse = false;
        state->stick_controller = false;
        state->stick_controller_target = 0;
        state->stick_dpad = false;
        state->stick_gyro = false;
        state->stick_touch_mouse = false;
        state->stick_touch_finger = 1;  /* Default to first finger */
        state->stick_invert_x = false;
        state->stick_invert_y = false;
        state->stick_h_sens = 0.0f;
        state->stick_v_sens = 0.0f;
        state->stick_h_accel = 0.0f;
        state->stick_v_accel = 0.0f;
        state->stick_gyro_h_sens = 0.0f;
        state->stick_gyro_v_sens = 0.0f;
        state->stick_gyro_accel = 0.0f;
        state->stick_gyro_mode_roll = false;
        return;
    }

    state->stick_wasd = stick->map_to_wasd;
    state->stick_arrows = stick->map_to_arrow_keys;
    state->stick_mouse = stick->map_to_mouse_movement;
    state->stick_controller = stick->map_to_controller_movement;
    state->stick_controller_target = stick->controller_target_stick;
    state->stick_dpad = stick->map_to_dpad;
    state->stick_gyro = stick->map_to_gyroscope;
    state->stick_touch_mouse = stick->map_to_touch_mouse;
    state->stick_touch_finger = stick->touch_finger;
    state->stick_invert_x = stick->invert_horizontal;
    state->stick_invert_y = stick->invert_vertical;
    state->stick_h_sens = stick->horizontal_sensitivity;
    state->stick_v_sens = stick->vertical_sensitivity;
    state->stick_h_accel = stick->horizontal_acceleration;
    state->stick_v_accel = stick->vertical_acceleration;
    state->stick_gyro_h_sens = stick->gyro_horizontal_sensitivity;
    state->stick_gyro_v_sens = stick->gyro_vertical_sensitivity;
    state->stick_gyro_accel = stick->gyro_acceleration;
    state->stick_gyro_mode_roll = stick->gyro_mode_roll;
}

/* Save stick config from UIState back into the stick mapping for an axis */
static void
UI_SaveStickStateToAxis(SDL_RemapperContext *ctx,
                        SDL_JoystickID gamepad_id,
                        SDL_GamepadAxis axis,
                        UIState *state)
{
    SDL_RemapperMapping *mapping;
    SDL_RemapperStickMapping *stick;

    if (!state) {
        return;
    }

    int p = state->selected_profile;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    mapping = UI_GetMappingForAxisInProfile(axis, p);
    if (!mapping) {
        return;
    }

    stick = mapping->stick_mapping;
    if (!stick) {
        if (axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY) {
            stick = &g_left_stick_mapping;
        } else if (axis == SDL_GAMEPAD_AXIS_RIGHTX || axis == SDL_GAMEPAD_AXIS_RIGHTY) {
            stick = &g_right_stick_mapping;
        }
        mapping->stick_mapping = stick;
    }

    if (!stick) {
        return;
    }

    stick->map_to_wasd = state->stick_wasd;
    stick->map_to_arrow_keys = state->stick_arrows;
    stick->map_to_mouse_movement = state->stick_mouse;
    stick->map_to_controller_movement = state->stick_controller;
    stick->controller_target_stick = state->stick_controller_target;
    stick->map_to_dpad = state->stick_dpad;
    stick->map_to_gyroscope = state->stick_gyro;
    stick->map_to_touch_mouse = state->stick_touch_mouse;
    stick->touch_finger = state->stick_touch_finger;
    stick->invert_horizontal = state->stick_invert_x;
    stick->invert_vertical = state->stick_invert_y;
    stick->horizontal_sensitivity = state->stick_h_sens;
    stick->vertical_sensitivity = state->stick_v_sens;
    stick->horizontal_acceleration = state->stick_h_accel;
    stick->vertical_acceleration = state->stick_v_accel;
    stick->gyro_horizontal_sensitivity = state->stick_gyro_h_sens;
    stick->gyro_vertical_sensitivity = state->stick_gyro_v_sens;
    stick->gyro_acceleration = state->stick_gyro_accel;
    stick->gyro_mode_roll = state->stick_gyro_mode_roll;


    /* Enforce cross-stick constraint for controller stick target.
     * If this stick is in controller mode and set to left stick (0), and the
     * opposite stick is also in controller mode with left stick (0), switch it to right stick (1). */
    if (state->stick_controller) {
        SDL_GamepadAxis other_axis = SDL_GAMEPAD_AXIS_INVALID;
        if (axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY) {
            other_axis = SDL_GAMEPAD_AXIS_RIGHTX;
        } else if (axis == SDL_GAMEPAD_AXIS_RIGHTX || axis == SDL_GAMEPAD_AXIS_RIGHTY) {
            other_axis = SDL_GAMEPAD_AXIS_LEFTX;
        }

        if (other_axis != SDL_GAMEPAD_AXIS_INVALID) {
            SDL_RemapperMapping *other_mapping = UI_GetMappingForAxisInProfile(other_axis, p);
            if (other_mapping && other_mapping->stick_mapping) {
                SDL_RemapperStickMapping *other_stick = other_mapping->stick_mapping;
                if (other_stick->map_to_controller_movement) {
                    /* If both have same target stick, swap the other one */
                    if (other_stick->controller_target_stick == state->stick_controller_target) {
                        other_stick->controller_target_stick = (state->stick_controller_target == 0) ? 1 : 0;
                    }
                }
            }
        }
    }
    /* Enforce cross-stick constraint for touch finger selection.
     * If this stick is in touch mode and set to finger 1, and the
     * opposite stick is also in touch mode with finger 1, switch it to finger 2. */
    if (state->stick_touch_mouse && state->stick_touch_finger > 0) {
        SDL_GamepadAxis other_axis = SDL_GAMEPAD_AXIS_INVALID;
        if (axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY) {
            other_axis = SDL_GAMEPAD_AXIS_RIGHTX;
        } else if (axis == SDL_GAMEPAD_AXIS_RIGHTX || axis == SDL_GAMEPAD_AXIS_RIGHTY) {
            other_axis = SDL_GAMEPAD_AXIS_LEFTX;
        }

        if (other_axis != SDL_GAMEPAD_AXIS_INVALID) {
            SDL_RemapperMapping *other_mapping = UI_GetMappingForAxisInProfile(other_axis, p);
            if (other_mapping && other_mapping->stick_mapping) {
                SDL_RemapperStickMapping *other_stick = other_mapping->stick_mapping;
                if (other_stick->map_to_touch_mouse) {
                    /* If both have same finger, swap the other one */
                    if (other_stick->touch_finger == state->stick_touch_finger) {
                        other_stick->touch_finger = (state->stick_touch_finger == 1) ? 2 : 1;
                    }
                }
            }
        }
    }

    /* Enforce cross-stick constraint for gyro pitch/yaw vs roll.
     * If this stick is gyro-enabled and set to Pitch/Yaw mode, and the
     * opposite stick is also gyro-enabled and in Pitch/Yaw mode, force
     * the opposite stick to Roll mode so only one uses Pitch/Yaw. */
    if (state->stick_gyro && !state->stick_gyro_mode_roll) {
        SDL_GamepadAxis other_axis = SDL_GAMEPAD_AXIS_INVALID;
        if (axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY) {
            other_axis = SDL_GAMEPAD_AXIS_RIGHTX;
        } else if (axis == SDL_GAMEPAD_AXIS_RIGHTX || axis == SDL_GAMEPAD_AXIS_RIGHTY) {
            other_axis = SDL_GAMEPAD_AXIS_LEFTX;
        }

        if (other_axis != SDL_GAMEPAD_AXIS_INVALID) {
            SDL_RemapperMapping *other_mapping = UI_GetMappingForAxisInProfile(other_axis, p);
            if (other_mapping && other_mapping->stick_mapping) {
                SDL_RemapperStickMapping *other_stick = other_mapping->stick_mapping;
                if (other_stick->map_to_gyroscope && !other_stick->gyro_mode_roll) {
                    other_stick->gyro_mode_roll = true; /* force other stick to Roll mode */
                }
            }
        }
    }

    UI_CommitProfileToContext(ctx, gamepad_id, state);

    UI_SaveCurrentProfileToDisk(state);
}

/* Load stick-like config in UIState from the Mouse Move slot mapping */
static void
UI_LoadMouseMoveState(UIState *state)
{
    SDL_RemapperMapping *mapping;
    SDL_RemapperStickMapping *stick = NULL;

    if (!state) {
        return;
    }

    int p = state->selected_profile;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    mapping = UI_GetMouseSlotMappingInProfile(UI_MOUSE_SLOT_MOVE, p);
    if (mapping) {
        stick = mapping->stick_mapping;
    }

    if (!stick) {
        SDL_memset(&g_mouse_move_mapping, 0, sizeof(g_mouse_move_mapping));
        stick = &g_mouse_move_mapping;
        if (mapping) {
            mapping->stick_mapping = stick;
        }
    }

    if (!stick) {
        /* Nothing to load */
        state->stick_wasd = false;
        state->stick_arrows = false;
        state->stick_mouse = false;
        state->stick_controller = false;
        state->stick_controller_target = 0;
        state->stick_dpad = false;
        state->stick_gyro = false;
        state->stick_touch_mouse = false;
        state->stick_touch_finger = 1;  /* Default to first finger */
        state->stick_invert_x = false;
        state->stick_invert_y = false;
        state->stick_h_sens = 0.0f;
        state->stick_v_sens = 0.0f;
        state->stick_h_accel = 0.0f;
        state->stick_v_accel = 0.0f;
        state->stick_gyro_h_sens = 0.0f;
        state->stick_gyro_v_sens = 0.0f;
        state->stick_gyro_accel = 0.0f;
        state->stick_gyro_mode_roll = false;
        return;
    }

    state->stick_wasd = stick->map_to_wasd;
    state->stick_arrows = stick->map_to_arrow_keys;
    state->stick_mouse = stick->map_to_mouse_movement;
    state->stick_controller = stick->map_to_controller_movement;
    state->stick_controller_target = stick->controller_target_stick;
    state->stick_dpad = stick->map_to_dpad;
    state->stick_gyro = stick->map_to_gyroscope;
    state->stick_touch_mouse = stick->map_to_touch_mouse;
    state->stick_touch_finger = stick->touch_finger;
    state->stick_invert_x = stick->invert_horizontal;
    state->stick_invert_y = stick->invert_vertical;
    state->stick_h_sens = stick->horizontal_sensitivity;
    state->stick_v_sens = stick->vertical_sensitivity;
    state->stick_h_accel = stick->horizontal_acceleration;
    state->stick_v_accel = stick->vertical_acceleration;
    state->stick_gyro_h_sens = stick->gyro_horizontal_sensitivity;
    state->stick_gyro_v_sens = stick->gyro_vertical_sensitivity;
    state->stick_gyro_accel = stick->gyro_acceleration;
    state->stick_gyro_mode_roll = stick->gyro_mode_roll;
}

/* Save stick-like config from UIState back into the Mouse Move slot mapping */
static void
UI_SaveMouseMoveState(SDL_RemapperContext *ctx,
                      SDL_JoystickID gamepad_id,
                      UIState *state)
{
    SDL_RemapperMapping *mapping;
    SDL_RemapperStickMapping *stick;

    if (!state) {
        return;
    }

    int p = state->selected_profile;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    mapping = UI_GetMouseSlotMappingInProfile(UI_MOUSE_SLOT_MOVE, p);
    if (!mapping) {
        return;
    }

    stick = mapping->stick_mapping;
    if (!stick) {
        stick = &g_mouse_move_mapping;
        mapping->stick_mapping = stick;
    }

    if (!stick) {
        return;
    }

    stick->map_to_wasd = state->stick_wasd;
    stick->map_to_arrow_keys = state->stick_arrows;
    stick->map_to_mouse_movement = state->stick_mouse;
    stick->map_to_controller_movement = state->stick_controller;
    stick->controller_target_stick = state->stick_controller_target;
    stick->map_to_dpad = state->stick_dpad;
    stick->map_to_gyroscope = state->stick_gyro;
    stick->map_to_touch_mouse = state->stick_touch_mouse;
    stick->touch_finger = state->stick_touch_finger;
    stick->invert_horizontal = state->stick_invert_x;
    stick->invert_vertical = state->stick_invert_y;
    stick->horizontal_sensitivity = state->stick_h_sens;
    stick->vertical_sensitivity = state->stick_v_sens;
    stick->horizontal_acceleration = state->stick_h_accel;
    stick->vertical_acceleration = state->stick_v_accel;
    stick->gyro_horizontal_sensitivity = state->stick_gyro_h_sens;
    stick->gyro_vertical_sensitivity = state->stick_gyro_v_sens;
    stick->gyro_acceleration = state->stick_gyro_accel;
    stick->gyro_mode_roll = state->stick_gyro_mode_roll;

    UI_CommitProfileToContext(ctx, gamepad_id, state);

    UI_SaveCurrentProfileToDisk(state);
}

/* Remapping button positions - used by navigation and drawing */
typedef struct {
    SDL_GamepadButton button;
    float x, y;
    const char *label;
    const char *tag;
} RemappingButton;

#define REMAPPING_BUTTON_COUNT 14

static const RemappingButton remapping_buttons[REMAPPING_BUTTON_COUNT] = {
    /* Face buttons */
    { SDL_GAMEPAD_BUTTON_SOUTH, 2775, 888,  "A Button", "A Button" },       /* 0 */
    { SDL_GAMEPAD_BUTTON_EAST,  2775, 718,  "B Button", "B Button" },       /* 1 */
    { SDL_GAMEPAD_BUTTON_WEST,  2775, 1061, "X Button", "X Button" },       /* 2 */
    { SDL_GAMEPAD_BUTTON_NORTH, 2775, 548,  "Y Button", "Y Button" },       /* 3 */
    /* Bumpers */
    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  1430, 417, "Left Bumper", "Left Bumper" },   /* 4 */
    { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 2358, 417, "Right Bumper", "Right Bumper" }, /* 5 */
    /* Sticks */
    { SDL_GAMEPAD_BUTTON_LEFT_STICK,  1035, 718,  "Left Stick", "Left Stick" },   /* 6 */
    { SDL_GAMEPAD_BUTTON_RIGHT_STICK, 2775, 1584, "Right Stick", "Right Stick" }, /* 7 */
    /* D-Pad */
    { SDL_GAMEPAD_BUTTON_DPAD_UP,    1035, 1060, "D-Pad Up", "D-Pad Up" },       /* 8 */
    { SDL_GAMEPAD_BUTTON_DPAD_DOWN,  1035, 1404, "D-Pad Down", "D-Pad Down" },   /* 9 */
    { SDL_GAMEPAD_BUTTON_DPAD_LEFT,  1035, 1236, "D-Pad Left", "D-Pad Left" },   /* 10 */
    { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 1035, 1573, "D-Pad Right", "D-Pad Right" }, /* 11 */
    /* Center buttons */
    { SDL_GAMEPAD_BUTTON_BACK,  1800, 417, "View", "View Button" },  /* 12 */
    { SDL_GAMEPAD_BUTTON_START, 1988, 417, "Menu", "Menu Button" }   /* 13 */
};

/* Neighbor navigation tables for spatial controller navigation.
 * Total elements: 14 buttons + 2 triggers (LT=14, RT=15) + 2 stick moves (LS=16, RS=17) = 18
 * Value of -1 means no neighbor in that direction, -2 means go to action buttons */
#define NAV_IDX_LT 14
#define NAV_IDX_RT 15
#define NAV_IDX_LS_MOVE 16
#define NAV_IDX_RS_MOVE 17
#define NAV_IDX_ACTION -2
#define NAV_TOTAL 18

/* Neighbor for UP direction from each element */
static const int gamepad_nav_up[NAV_TOTAL] = {
    /* 0:A  */ 1,           /* A -> B */
    /* 1:B  */ 3,           /* B -> Y */
    /* 2:X  */ 0,           /* X -> A */
    /* 3:Y  */ 5,           /* Y -> RB */
    /* 4:LB */ -1,          /* LB -> stay */
    /* 5:RB */ -1,          /* RB -> stay */
    /* 6:LS */ NAV_IDX_LS_MOVE, /* LS -> LS Move */
    /* 7:RS */ 17,          /* RS -> RS Move */
    /* 8:DU */ 6,           /* D-Up -> LS */
    /* 9:DD */ 10,          /* D-Down -> D-Left */
    /* 10:DL*/ 8,           /* D-Left -> D-Up */
    /* 11:DR*/ 9,           /* D-Right -> D-Down */
    /* 12:View */ -1,       /* View -> stay */
    /* 13:Menu */ -1,       /* Menu -> stay */
    /* 14:LT */ -1,         /* LT -> stay */
    /* 15:RT */ -1,         /* RT -> stay */
    /* 16:LS Move */ 4,     /* LS Move -> LB */
    /* 17:RS Move */ 2      /* RS Move -> X */
};

/* Neighbor for DOWN direction from each element */
static const int gamepad_nav_down[NAV_TOTAL] = {
    /* 0:A  */ 2,           /* A -> X */
    /* 1:B  */ 0,           /* B -> A */
    /* 2:X  */ 17,          /* X -> RS Move */
    /* 3:Y  */ 1,           /* Y -> B */
    /* 4:LB */ NAV_IDX_LS_MOVE, /* LB -> LS Move */
    /* 5:RB */ 3,           /* RB -> Y */
    /* 6:LS */ 8,           /* LS -> D-Up */
    /* 7:RS */ NAV_IDX_ACTION, /* RS -> action buttons */
    /* 8:DU */ 10,          /* D-Up -> D-Left */
    /* 9:DD */ 11,          /* D-Down -> D-Right */
    /* 10:DL*/ 9,           /* D-Left -> D-Down */
    /* 11:DR*/ NAV_IDX_ACTION, /* D-Right -> action buttons */
    /* 12:View */ 4,        /* View -> LB */
    /* 13:Menu */ 5,        /* Menu -> RB */
    /* 14:LT */ 4,          /* LT -> LB */
    /* 15:RT */ 5,          /* RT -> RB */
    /* 16:LS Move */ 6,     /* LS Move -> LS */
    /* 17:RS Move */ 7      /* RS Move -> RS */
};

static void
UI_SetGamepadSelectionFromNavIndex(UIState *state, int nav_index)
{
    if (!state || nav_index < 0) {
        return;
    }

    if (nav_index < REMAPPING_BUTTON_COUNT) {
        state->selected_button = remapping_buttons[nav_index].button;
        state->selected_axis = SDL_GAMEPAD_AXIS_INVALID;
        return;
    }

    switch (nav_index) {
    case NAV_IDX_LT:
        state->selected_button = SDL_GAMEPAD_BUTTON_INVALID;
        state->selected_axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        break;
    case NAV_IDX_RT:
        state->selected_button = SDL_GAMEPAD_BUTTON_INVALID;
        state->selected_axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        break;
    case NAV_IDX_LS_MOVE:
        state->selected_button = SDL_GAMEPAD_BUTTON_INVALID;
        state->selected_axis = SDL_GAMEPAD_AXIS_LEFTX;
        break;
    case NAV_IDX_RS_MOVE:
        state->selected_button = SDL_GAMEPAD_BUTTON_INVALID;
        state->selected_axis = SDL_GAMEPAD_AXIS_RIGHTX;
        break;
    default:
        break;
    }
}

/* Neighbor for LEFT direction from each element */
/* Top row order (by x-coord): LB - LT - View - Menu - RT - RB */
static const int gamepad_nav_left[NAV_TOTAL] = {
    /* 0:A  */ 8,           /* A -> D-Up */
    /* 1:B  */ 6,           /* B -> LS */
    /* 2:X  */ 10,          /* X -> D-Left */
    /* 3:Y  */ NAV_IDX_LS_MOVE, /* Y -> LS Move */
    /* 4:LB */ NAV_IDX_LS_MOVE, /* LB -> LS Move (left end of row, go to top of left column) */
    /* 5:RB */ NAV_IDX_RT,  /* RB -> RT */
    /* 6:LS */ -1,          /* LS -> stay */
    /* 7:RS */ 11,          /* RS -> D-Right (bottom of left column) */
    /* 8:DU */ -1,          /* D-Up -> stay */
    /* 9:DD */ -1,          /* D-Down -> stay */
    /* 10:DL*/ -1,          /* D-Left -> stay */
    /* 11:DR*/ -1,          /* D-Right -> stay */
    /* 12:View */ NAV_IDX_LT, /* View -> LT */
    /* 13:Menu */ 12,       /* Menu -> View */
    /* 14:LT */ 4,          /* LT -> LB */
    /* 15:RT */ 13,         /* RT -> Menu */
    /* 16:LS Move */ -1,    /* LS Move -> stay */
    /* 17:RS Move */ 9      /* RS Move -> D-Down */
};

/* Neighbor for RIGHT direction from each element */
/* Top row order (by x-coord): LB - LT - View - Menu - RT - RB */
static const int gamepad_nav_right[NAV_TOTAL] = {
    /* 0:A  */ -1,          /* A -> stay */
    /* 1:B  */ -1,          /* B -> stay */
    /* 2:X  */ -1,          /* X -> stay */
    /* 3:Y  */ -1,          /* Y -> stay */
    /* 4:LB */ NAV_IDX_LT,  /* LB -> LT */
    /* 5:RB */ 3,           /* RB -> Y (right end of row, go to top of right column) */
    /* 6:LS */ 1,           /* LS -> B */
    /* 7:RS */ -1,          /* RS -> stay */
    /* 8:DU */ 0,           /* D-Up -> A */
    /* 9:DD */ 17,          /* D-Down -> RS Move */
    /* 10:DL*/ 2,           /* D-Left -> X */
    /* 11:DR*/ 7,           /* D-Right -> RS */
    /* 12:View */ 13,       /* View -> Menu */
    /* 13:Menu */ NAV_IDX_RT, /* Menu -> RT */
    /* 14:LT */ 12,         /* LT -> View */
    /* 15:RT */ 5,          /* RT -> RB */
    /* 16:LS Move */ 3,     /* LS Move -> Y */
    /* 17:RS Move */ 7      /* RS Move -> RS */
};

/* Mouse navigation tables for profile page preview (2x4 grid layout)
 * Layout order (indices 0-7):
 *   Left column:  0=Left Click, 2=Mouse Move, 4=Forward, 6=Back
 *   Right column: 1=Right Click, 3=Wheel Up, 5=Middle Click, 7=Wheel Down
 */
#define MOUSE_NAV_TOTAL 8
#define MOUSE_NAV_ACTION -2

static const int mouse_nav_up[MOUSE_NAV_TOTAL] = {
    /* 0:Left Click */   MOUSE_NAV_ACTION,  /* top of left column - go to action buttons */
    /* 1:Right Click */  MOUSE_NAV_ACTION,  /* top of right column - go to action buttons */
    /* 2:Mouse Move */   0,   /* -> Left Click */
    /* 3:Wheel Up */     1,   /* -> Right Click */
    /* 4:Forward */      2,   /* -> Mouse Move */
    /* 5:Middle Click */ 3,   /* -> Wheel Up */
    /* 6:Back */         4,   /* -> Forward */
    /* 7:Wheel Down */   5    /* -> Middle Click */
};

static const int mouse_nav_down[MOUSE_NAV_TOTAL] = {
    /* 0:Left Click */   2,   /* -> Mouse Move */
    /* 1:Right Click */  3,   /* -> Wheel Up */
    /* 2:Mouse Move */   4,   /* -> Forward */
    /* 3:Wheel Up */     5,   /* -> Middle Click */
    /* 4:Forward */      6,   /* -> Back */
    /* 5:Middle Click */ 7,   /* -> Wheel Down */
    /* 6:Back */         MOUSE_NAV_ACTION,  /* bottom left - go to action buttons */
    /* 7:Wheel Down */   MOUSE_NAV_ACTION   /* bottom right - go to action buttons */
};

static const int mouse_nav_left[MOUSE_NAV_TOTAL] = {
    /* 0:Left Click */   -1,  /* left column - stay */
    /* 1:Right Click */  0,   /* -> Left Click */
    /* 2:Mouse Move */   -1,  /* left column - stay */
    /* 3:Wheel Up */     2,   /* -> Mouse Move */
    /* 4:Forward */      -1,  /* left column - stay */
    /* 5:Middle Click */ 4,   /* -> Forward */
    /* 6:Back */         -1,  /* left column - stay */
    /* 7:Wheel Down */   6    /* -> Back */
};

static const int mouse_nav_right[MOUSE_NAV_TOTAL] = {
    /* 0:Left Click */   1,   /* -> Right Click */
    /* 1:Right Click */  -1,  /* right column - stay */
    /* 2:Mouse Move */   3,   /* -> Wheel Up */
    /* 3:Wheel Up */     -1,  /* right column - stay */
    /* 4:Forward */      5,   /* -> Middle Click */
    /* 5:Middle Click */ -1,  /* right column - stay */
    /* 6:Back */         7,   /* -> Wheel Down */
    /* 7:Wheel Down */   -1   /* right column - stay */
};

/* Handle back button press - navigate to previous page or close window */
static void
UI_HandleBack(UIState *state, bool *done)
{
    if (!state || !done) {
        return;
    }

    switch (state->current_page) {
    case PAGE_DEVICE_SELECT:
        *done = true;
        break;
    case PAGE_PROFILE_SELECT:
        state->current_page = PAGE_DEVICE_SELECT;
        break;
    case PAGE_BUTTON_MAPPING:
        state->current_page = PAGE_PROFILE_SELECT;
        break;
    }
}

/* Open the mapping dialog for a profile preview element at a given index.
 * This is used for keyboard/gamepad navigation on the profile page. */
static void
UI_OpenProfilePreviewAtIndex(SDL_RemapperContext *ctx,
                             SDL_JoystickID gamepad_id,
                             UIState *state,
                             int index)
{
    (void)ctx;
    (void)gamepad_id;

    if (!state || index < 0) {
        return;
    }

    /* The preview index maps to controller buttons/triggers/sticks in order:
     * 0..N-1 = remapping_buttons array indices
     * N, N+1 = LT, RT triggers
     * N+2, N+3 = LS Move, RS Move */
    /* For now, this is a placeholder - specific implementation depends on
     * how preview elements are ordered in the UI */
}

/* Handle gamepad navigation button press (D-pad, A, B buttons) */
static void
UI_HandleGamepadNavButton(SDL_RemapperContext *ctx,
                          SDL_JoystickID gamepad_id,
                          SDL_GamepadButton button,
                          UIState *state,
                          bool *done)
{
    if (!state || !done) {
        return;
    }

    /* Handle dialog navigation first */
    if (state->active_dialog != DIALOG_NONE) {
        switch (state->active_dialog) {
        case DIALOG_BUTTON_OPTIONS:
            if (button == SDL_GAMEPAD_BUTTON_EAST) {
                /* B = Cancel */
                state->active_dialog = DIALOG_NONE;
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                state->dialog_focus_index--;
                if (state->dialog_focus_index < 0) {
                    state->dialog_focus_index = 4; /* Wrap to Cancel */
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                state->dialog_focus_index++;
                if (state->dialog_focus_index > 4) {
                    state->dialog_focus_index = 0;
                }
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                /* A = Activate focused element */
                if (state->dialog_focus_index == 4) {
                    /* Cancel */
                    state->active_dialog = DIALOG_NONE;
                } else if (!state->dialog_read_only) {
                    if (state->dialog_focus_index == 0) {
                        state->active_slot = 0;
                        state->mapping_from_trigger = false;
                        state->active_dialog = DIALOG_MAPPING_SELECT;
                    } else if (state->dialog_focus_index == 1) {
                        state->active_slot = 2;
                        state->mapping_from_trigger = false;
                        state->active_dialog = DIALOG_MAPPING_SELECT;
                    } else if (state->dialog_focus_index == 2) {
                        state->active_slot = 1;
                        state->mapping_from_trigger = false;
                        state->active_dialog = DIALOG_MAPPING_SELECT;
                    } else if (state->dialog_focus_index == 3) {
                        /* Toggle Use as Shift checkbox */
                        int p = state->selected_profile;
                        if (p < 0) p = 0;
                        if (p >= SDL_UI_MAX_PROFILES) p = SDL_UI_MAX_PROFILES - 1;

                        SDL_RemapperMapping *mapping = NULL;
                        if (state->selected_button != SDL_GAMEPAD_BUTTON_INVALID) {
                            mapping = UI_GetMappingForButtonInProfile(state->selected_button, p);
                        } else if (state->selected_keyboard_slot >= 0 &&
                                   state->selected_keyboard_slot < UI_KEYBOARD_SLOT_COUNT) {
                            mapping = UI_GetKeyboardSlotMappingInProfile(
                                (UI_KeyboardSlot)state->selected_keyboard_slot, p);
                        } else if (state->selected_mouse_slot >= 0 &&
                                   state->selected_mouse_slot < UI_MOUSE_SLOT_COUNT) {
                            mapping = UI_GetMouseSlotMappingInProfile(
                                (UI_MouseSlot)state->selected_mouse_slot, p);
                        }

                        if (mapping) {
                            mapping->use_as_shift = !mapping->use_as_shift;
                            UI_CommitProfileToContext(ctx, gamepad_id, state);
                            UI_SaveCurrentProfileToDisk(state);
                        }
                    }
                }
            }
            break;

        case DIALOG_MAPPING_SELECT:
            if (button == SDL_GAMEPAD_BUTTON_EAST) {
                state->active_dialog = DIALOG_NONE;
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                state->list_selection--;
                if (state->list_selection < 0) {
                    state->list_selection = 0;
                }
                /* Scroll if needed */
                if (state->list_selection < state->list_scroll) {
                    state->list_scroll = state->list_selection;
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                const MappingOption *options = NULL;
                int option_count = 0;
                UI_GetActiveOptions(state, &options, &option_count);
                state->list_selection++;
                if (state->list_selection >= option_count) {
                    state->list_selection = option_count - 1;
                }
                /* Scroll if needed */
                if (state->list_selection >= state->list_scroll + 5) {
                    state->list_scroll = state->list_selection - 4;
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                state->active_tab--;
                if (state->active_tab < 0) {
                    state->active_tab = 3;
                }
                state->list_selection = 0;
                state->list_scroll = 0;
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                state->active_tab++;
                if (state->active_tab > 3) {
                    state->active_tab = 0;
                }
                state->list_selection = 0;
                state->list_scroll = 0;
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                /* A = Confirm selection */
                const MappingOption *options = NULL;
                int option_count = 0;
                UI_GetActiveOptions(state, &options, &option_count);

                if (option_count > 0) {
                    SDL_RemapperMapping *mapping = NULL;
                    if (state->selected_button != SDL_GAMEPAD_BUTTON_INVALID) {
                        mapping = UI_GetMappingForButton(state->selected_button);
                    } else if (state->selected_axis != SDL_GAMEPAD_AXIS_INVALID) {
                        mapping = UI_GetMappingForAxis(state->selected_axis);
                    } else if (state->selected_mouse_slot >= 0 && state->selected_mouse_slot < UI_MOUSE_SLOT_COUNT) {
                        mapping = UI_GetMouseSlotMapping((UI_MouseSlot)state->selected_mouse_slot);
                    } else if (state->selected_keyboard_slot >= 0 && state->selected_keyboard_slot < UI_KEYBOARD_SLOT_COUNT) {
                        mapping = UI_GetKeyboardSlotMapping((UI_KeyboardSlot)state->selected_keyboard_slot);
                    }

                    if (mapping) {
                        int sel = state->list_selection;
                        if (sel < 0 || sel >= option_count) sel = 0;
                        UI_ApplyMappingToSlot(ctx, gamepad_id, mapping, state->active_slot, &options[sel], state);
                    }
                }

                if (state->mapping_from_trigger && state->selected_axis != SDL_GAMEPAD_AXIS_INVALID) {
                    state->active_dialog = DIALOG_TRIGGER_OPTIONS;
                } else {
                    state->active_dialog = DIALOG_NONE;
                }
            }
            break;

        case DIALOG_TRIGGER_OPTIONS:
            if (button == SDL_GAMEPAD_BUTTON_EAST) {
                state->active_dialog = DIALOG_NONE;
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                state->dialog_focus_index--;
                if (state->dialog_focus_index < 0) {
                    state->dialog_focus_index = 3;
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                state->dialog_focus_index++;
                if (state->dialog_focus_index > 3) {
                    state->dialog_focus_index = 0;
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT && !state->dialog_read_only) {
                /* Adjust deadzone slider left (decrease) when focused */
                if (state->dialog_focus_index == 2) {
                    float step = 5.0f;
                    int p = state->selected_profile;
                    if (p < 0) p = 0;
                    else if (p >= SDL_UI_MAX_PROFILES) p = SDL_UI_MAX_PROFILES - 1;

                    if (state->selected_axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                        state->trigger_deadzone_left -= step;
                        if (state->trigger_deadzone_left < 1.0f) state->trigger_deadzone_left = 1.0f;
                        g_ui_profile_trigger_deadzone_left[p] = state->trigger_deadzone_left;
                    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                        state->trigger_deadzone_right -= step;
                        if (state->trigger_deadzone_right < 1.0f) state->trigger_deadzone_right = 1.0f;
                        g_ui_profile_trigger_deadzone_right[p] = state->trigger_deadzone_right;
                    }
                    UI_CommitProfileToContext(ctx, gamepad_id, state);
                    UI_SaveCurrentProfileToDisk(state);
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT && !state->dialog_read_only) {
                /* Adjust deadzone slider right (increase) when focused */
                if (state->dialog_focus_index == 2) {
                    float step = 5.0f;
                    int p = state->selected_profile;
                    if (p < 0) p = 0;
                    else if (p >= SDL_UI_MAX_PROFILES) p = SDL_UI_MAX_PROFILES - 1;

                    if (state->selected_axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                        state->trigger_deadzone_left += step;
                        if (state->trigger_deadzone_left > 100.0f) state->trigger_deadzone_left = 100.0f;
                        g_ui_profile_trigger_deadzone_left[p] = state->trigger_deadzone_left;
                    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                        state->trigger_deadzone_right += step;
                        if (state->trigger_deadzone_right > 100.0f) state->trigger_deadzone_right = 100.0f;
                        g_ui_profile_trigger_deadzone_right[p] = state->trigger_deadzone_right;
                    }
                    UI_CommitProfileToContext(ctx, gamepad_id, state);
                    UI_SaveCurrentProfileToDisk(state);
                }
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH && !state->dialog_read_only) {
                if (state->dialog_focus_index == 0) {
                    state->active_slot = 0;
                    state->mapping_from_trigger = true;
                    state->active_dialog = DIALOG_MAPPING_SELECT;
                } else if (state->dialog_focus_index == 1) {
                    state->active_slot = 1;
                    state->mapping_from_trigger = true;
                    state->active_dialog = DIALOG_MAPPING_SELECT;
                } else if (state->dialog_focus_index == 3) {
                    state->active_dialog = DIALOG_NONE;
                }
            }
            break;

        case DIALOG_STICK_CONFIG:
        case DIALOG_MOUSE_MOVE_CONFIG:
            if (button == SDL_GAMEPAD_BUTTON_EAST) {
                state->active_dialog = DIALOG_NONE;
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                state->dialog_focus_index--;
                if (state->dialog_focus_index < 0) {
                    state->dialog_focus_index = 14; /* Wrap to Cancel */
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                state->dialog_focus_index++;
                if (state->dialog_focus_index > 14) {
                    state->dialog_focus_index = 0;
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT && !state->dialog_read_only) {
                /* Toggle sub-options for checkboxes that have them */
                if (state->dialog_focus_index == 3 && state->stick_controller) {
                    /* Toggle controller target: right -> left */
                    if (state->stick_controller_target == 1) {
                        state->stick_controller_target = 0;
                        /* Update other stick if it conflicts */
                        SDL_GamepadAxis other_axis = (state->selected_axis == SDL_GAMEPAD_AXIS_LEFTX || state->selected_axis == SDL_GAMEPAD_AXIS_LEFTY)
                            ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX;
                        SDL_RemapperMapping *other_mapping = UI_GetMappingForAxisInProfile(other_axis, state->selected_profile);
                        if (other_mapping && other_mapping->stick_mapping && other_mapping->stick_mapping->map_to_controller_movement) {
                            if (other_mapping->stick_mapping->controller_target_stick == 0) {
                                other_mapping->stick_mapping->controller_target_stick = 1;
                            }
                        }
                    }
                } else if (state->dialog_focus_index == 5 && state->stick_gyro) {
                    /* Toggle gyro mode: roll -> pitch/yaw */
                    state->stick_gyro_mode_roll = false;
                } else if (state->dialog_focus_index == 6 && state->stick_touch_mouse) {
                    /* Toggle touch finger: second -> first */
                    if (state->stick_touch_finger == 2) {
                        state->stick_touch_finger = 1;
                    }
                }
                /* Adjust sliders left (decrease value) */
                float step = 5.0f;
                if (state->dialog_focus_index == 9) {
                    if (state->stick_gyro) {
                        state->stick_gyro_h_sens -= step;
                        if (state->stick_gyro_h_sens < -50.0f) state->stick_gyro_h_sens = -50.0f;
                    } else {
                        state->stick_h_sens -= step;
                        if (state->stick_h_sens < -50.0f) state->stick_h_sens = -50.0f;
                    }
                } else if (state->dialog_focus_index == 10) {
                    if (state->stick_gyro) {
                        state->stick_gyro_v_sens -= step;
                        if (state->stick_gyro_v_sens < -50.0f) state->stick_gyro_v_sens = -50.0f;
                    } else {
                        state->stick_v_sens -= step;
                        if (state->stick_v_sens < -50.0f) state->stick_v_sens = -50.0f;
                    }
                } else if (state->dialog_focus_index == 11) {
                    if (state->stick_gyro) {
                        state->stick_gyro_accel -= step;
                        if (state->stick_gyro_accel < -50.0f) state->stick_gyro_accel = -50.0f;
                    } else {
                        state->stick_h_accel -= step;
                        if (state->stick_h_accel < -50.0f) state->stick_h_accel = -50.0f;
                    }
                } else if (state->dialog_focus_index == 12 && !state->stick_gyro) {
                    state->stick_v_accel -= step;
                    if (state->stick_v_accel < -50.0f) state->stick_v_accel = -50.0f;
                } else if (state->dialog_focus_index == 13) {
                    /* OK -> Cancel navigation not needed, only two buttons side by side */
                } else if (state->dialog_focus_index == 14) {
                    state->dialog_focus_index = 13; /* Cancel -> OK */
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT && !state->dialog_read_only) {
                /* Toggle sub-options for checkboxes that have them */
                if (state->dialog_focus_index == 3 && state->stick_controller) {
                    /* Toggle controller target: left -> right */
                    if (state->stick_controller_target == 0) {
                        state->stick_controller_target = 1;
                        /* Update other stick if it conflicts */
                        SDL_GamepadAxis other_axis = (state->selected_axis == SDL_GAMEPAD_AXIS_LEFTX || state->selected_axis == SDL_GAMEPAD_AXIS_LEFTY)
                            ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX;
                        SDL_RemapperMapping *other_mapping = UI_GetMappingForAxisInProfile(other_axis, state->selected_profile);
                        if (other_mapping && other_mapping->stick_mapping && other_mapping->stick_mapping->map_to_controller_movement) {
                            if (other_mapping->stick_mapping->controller_target_stick == 1) {
                                other_mapping->stick_mapping->controller_target_stick = 0;
                            }
                        }
                    }
                } else if (state->dialog_focus_index == 5 && state->stick_gyro) {
                    /* Toggle gyro mode: pitch/yaw -> roll */
                    state->stick_gyro_mode_roll = true;
                } else if (state->dialog_focus_index == 6 && state->stick_touch_mouse) {
                    /* Toggle touch finger: first -> second */
                    if (state->stick_touch_finger == 1) {
                        state->stick_touch_finger = 2;
                    }
                }
                /* Adjust sliders right (increase value) */
                float step = 5.0f;
                if (state->dialog_focus_index == 9) {
                    if (state->stick_gyro) {
                        state->stick_gyro_h_sens += step;
                        if (state->stick_gyro_h_sens > 50.0f) state->stick_gyro_h_sens = 50.0f;
                    } else {
                        state->stick_h_sens += step;
                        if (state->stick_h_sens > 50.0f) state->stick_h_sens = 50.0f;
                    }
                } else if (state->dialog_focus_index == 10) {
                    if (state->stick_gyro) {
                        state->stick_gyro_v_sens += step;
                        if (state->stick_gyro_v_sens > 50.0f) state->stick_gyro_v_sens = 50.0f;
                    } else {
                        state->stick_v_sens += step;
                        if (state->stick_v_sens > 50.0f) state->stick_v_sens = 50.0f;
                    }
                } else if (state->dialog_focus_index == 11) {
                    if (state->stick_gyro) {
                        state->stick_gyro_accel += step;
                        if (state->stick_gyro_accel > 50.0f) state->stick_gyro_accel = 50.0f;
                    } else {
                        state->stick_h_accel += step;
                        if (state->stick_h_accel > 50.0f) state->stick_h_accel = 50.0f;
                    }
                } else if (state->dialog_focus_index == 12 && !state->stick_gyro) {
                    state->stick_v_accel += step;
                    if (state->stick_v_accel > 50.0f) state->stick_v_accel = 50.0f;
                } else if (state->dialog_focus_index == 13) {
                    state->dialog_focus_index = 14; /* OK -> Cancel */
                }
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH && !state->dialog_read_only) {
                /* Toggle checkboxes or activate buttons */
                if (state->dialog_focus_index < 7) {
                    /* Control type options (0-6) are mutually exclusive - radio button behavior */
                    bool *control_types[7] = {
                        &state->stick_wasd, &state->stick_arrows, &state->stick_mouse,
                        &state->stick_controller, &state->stick_dpad, &state->stick_gyro,
                        &state->stick_touch_mouse
                    };
                    /* Clear all control types first */
                    for (int i = 0; i < 7; i++) {
                        *control_types[i] = false;
                    }
                    /* Set the selected one */
                    *control_types[state->dialog_focus_index] = true;
                } else if (state->dialog_focus_index == 7) {
                    /* Invert Horizontal - independent toggle */
                    state->stick_invert_x = !state->stick_invert_x;
                } else if (state->dialog_focus_index == 8) {
                    /* Invert Vertical - independent toggle */
                    state->stick_invert_y = !state->stick_invert_y;
                } else if (state->dialog_focus_index == 13) {
                    /* OK button */
                    if (state->active_dialog == DIALOG_STICK_CONFIG) {
                        SDL_GamepadAxis axis = state->selected_axis;
                        SDL_GamepadAxis canonical_axis = axis;
                        if (axis == SDL_GAMEPAD_AXIS_LEFTY) canonical_axis = SDL_GAMEPAD_AXIS_LEFTX;
                        else if (axis == SDL_GAMEPAD_AXIS_RIGHTY) canonical_axis = SDL_GAMEPAD_AXIS_RIGHTX;
                        UI_SaveStickStateToAxis(ctx, gamepad_id, canonical_axis, state);
                    } else {
                        UI_SaveMouseMoveState(ctx, gamepad_id, state);
                    }
                    state->active_dialog = DIALOG_NONE;
                } else if (state->dialog_focus_index == 14) {
                    /* Cancel */
                    state->active_dialog = DIALOG_NONE;
                }
            }
            break;

        case DIALOG_NEW_PROFILE:
        case DIALOG_RENAME_PROFILE:
            /* Text input dialogs - focus: 0=TextField, 1=OK, 2=Cancel */
            if (button == SDL_GAMEPAD_BUTTON_EAST) {
                state->active_dialog = DIALOG_NONE;
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                /* Move focus up: buttons -> text field */
                if (state->dialog_focus_index > 0) {
                    state->dialog_focus_index = 0; /* Text field */
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                /* Move focus down: text field -> OK button */
                if (state->dialog_focus_index == 0) {
                    state->dialog_focus_index = 1; /* OK */
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                if (state->dialog_focus_index == 2) {
                    state->dialog_focus_index = 1; /* Cancel -> OK */
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                if (state->dialog_focus_index == 1) {
                    state->dialog_focus_index = 2; /* OK -> Cancel */
                }
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                if (state->dialog_focus_index == 0) {
                    /* Text field selected - signal to show on-screen keyboard */
                    /* This is handled in the main loop by checking dialog_focus_index == 0 */
                    state->show_osk = true;
                } else if (state->dialog_focus_index == 2) {
                    /* Cancel */
                    state->active_dialog = DIALOG_NONE;
                } else if (state->dialog_focus_index == 1) {
                    /* OK - submit the input */
                    if (state->active_dialog == DIALOG_NEW_PROFILE) {
                        if (state->profile_count < SDL_UI_MAX_PROFILES) {
                            int index = state->profile_count;
                            if (state->input_buffer[0] != '\0') {
                                SDL_strlcpy(state->profile_names[index], state->input_buffer, 64);
                            } else {
                                SDL_snprintf(state->profile_names[index], 64, "New Profile %d", index + 1);
                            }
                            state->profile_count++;
                            state->selected_profile = index;
                            UI_InitProfileMappings(index);
                            UI_CommitProfileToContext(ctx, gamepad_id, state);
                            UI_SaveCurrentProfileToDisk(state);
                        }
                    } else {
                        /* Rename - apply new name */
                        if (state->input_buffer[0] != '\0' && state->selected_profile >= 0 &&
                            state->selected_profile < state->profile_count) {
                            SDL_strlcpy(state->profile_names[state->selected_profile], state->input_buffer, 64);
                            UI_SaveCurrentProfileToDisk(state);
                        }
                    }
                    state->active_dialog = DIALOG_NONE;
                }
            }
            break;

        case DIALOG_DELETE_CONFIRM:
            /* Confirmation dialog: B = Cancel, A = select Yes/No */
            if (button == SDL_GAMEPAD_BUTTON_EAST) {
                state->active_dialog = DIALOG_NONE;
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                state->dialog_focus_index = 0; /* Yes */
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                state->dialog_focus_index = 1; /* No */
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                if (state->dialog_focus_index == 1) {
                    /* No - cancel */
                    state->active_dialog = DIALOG_NONE;
                } else if (state->dialog_focus_index == 0) {
                    /* Yes - delete the profile */
                    int p = state->selected_profile;
                    if (p > 0 && p < state->profile_count && state->profile_count > 1) {
                        /* Shift profiles down (names, mappings, and trigger deadzones) */
                        for (int i = p; i < state->profile_count - 1; i++) {
                            SDL_strlcpy(state->profile_names[i], state->profile_names[i + 1], 64);
                            SDL_memcpy(g_ui_profile_mappings[i], g_ui_profile_mappings[i + 1],
                                       sizeof(g_ui_profile_mappings[i]));
                            g_ui_profile_trigger_deadzone_left[i] = g_ui_profile_trigger_deadzone_left[i + 1];
                            g_ui_profile_trigger_deadzone_right[i] = g_ui_profile_trigger_deadzone_right[i + 1];
                        }
                        state->profile_count--;
                        if (state->selected_profile >= state->profile_count) {
                            state->selected_profile = state->profile_count - 1;
                        }
                        if (state->selected_profile < 0) {
                            state->selected_profile = 0;
                        }
                        UI_CommitProfileToContext(ctx, gamepad_id, state);
                        UI_SaveCurrentProfileToDisk(state);
                    }
                    state->active_dialog = DIALOG_NONE;
                }
            }
            break;

        case DIALOG_VIRTUAL_KEYBOARD:
            {
                /* Virtual keyboard layout - 4 rows x 10 columns */
                static const char *vk_keys[4][10] = {
                    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
                    { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
                    { "A", "S", "D", "F", "G", "H", "J", "K", "L", "Bksp" },
                    { "Z", "X", "C", "V", "Space", "B", "N", "M", "Done", "Esc" }
                };

                if (button == SDL_GAMEPAD_BUTTON_EAST) {
                    /* B = Cancel and close */
                    state->active_dialog = DIALOG_RENAME_PROFILE;
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                    state->vk_row--;
                    if (state->vk_row < 0) state->vk_row = 3;
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                    state->vk_row++;
                    if (state->vk_row > 3) state->vk_row = 0;
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                    state->vk_col--;
                    if (state->vk_col < 0) state->vk_col = 9;
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                    state->vk_col++;
                    if (state->vk_col > 9) state->vk_col = 0;
                } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    /* A = Select key */
                    const char *key = vk_keys[state->vk_row][state->vk_col];
                    int max_len = (int)(sizeof(state->input_buffer) - 1);

                    if (SDL_strcmp(key, "Bksp") == 0) {
                        /* Backspace */
                        if (state->input_cursor > 0) {
                            state->input_cursor--;
                            state->input_buffer[state->input_cursor] = '\0';
                        }
                    } else if (SDL_strcmp(key, "Done") == 0) {
                        /* Confirm and close */
                        state->active_dialog = DIALOG_RENAME_PROFILE;
                        state->dialog_focus_index = 1; /* Focus OK button */
                    } else if (SDL_strcmp(key, "Esc") == 0) {
                        /* Cancel and close */
                        state->active_dialog = DIALOG_RENAME_PROFILE;
                    } else if (SDL_strcmp(key, "Space") == 0) {
                        /* Space */
                        if (state->input_cursor < max_len) {
                            state->input_buffer[state->input_cursor++] = ' ';
                            state->input_buffer[state->input_cursor] = '\0';
                        }
                    } else {
                        /* Regular character - use lowercase */
                        if (state->input_cursor < max_len && key[0] != '\0') {
                            char c = key[0];
                            if (c >= 'A' && c <= 'Z') {
                                c = (char)(c + ('a' - 'A')); /* Convert to lowercase */
                            }
                            state->input_buffer[state->input_cursor++] = c;
                            state->input_buffer[state->input_cursor] = '\0';
                        }
                    }
                }
            }
            break;

        default:
            /* Unknown dialogs: B = close */
            if (button == SDL_GAMEPAD_BUTTON_EAST) {
                state->active_dialog = DIALOG_NONE;
            }
            break;
        }
        return;
    }

    /* Page-level navigation */
    switch (state->current_page) {
    case PAGE_DEVICE_SELECT:
        if (state->device_back_focused) {
            /* Back button is focused */
            if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                state->device_back_focused = false;
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                *done = true;  /* Exit the remapper */
            } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
                *done = true;
            }
        } else {
            /* Device selection area */
            if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                state->selected_device--;
                if (state->selected_device < 0) {
                    state->selected_device = state->device_count - 1;
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                state->selected_device++;
                if (state->selected_device >= state->device_count) {
                    state->selected_device = 0;
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                state->device_back_focused = true;
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                /* A = Configure */
                if (state->device_count > 0) {
                    state->profile_action_focus = -1;
                    state->profile_focus_on_new_button = false;
                    state->profile_preview_index = -1;
                    state->current_page = PAGE_PROFILE_SELECT;
                }
            } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
                *done = true;
            }
        }
        break;

    case PAGE_PROFILE_SELECT:
        {
        /* Determine device type and preview element count */
        UIDeviceType profile_device_type = UI_DEVICE_TYPE_GAMEPAD;
        int preview_max = 18; /* Default: 14 buttons + 2 triggers + 2 sticks */
        if (state->device_count > 0) {
            int dev_idx = state->selected_device;
            if (dev_idx < 0) dev_idx = 0;
            if (dev_idx >= state->device_count) dev_idx = state->device_count - 1;
            profile_device_type = state->device_types[dev_idx];
        }
        if (profile_device_type == UI_DEVICE_TYPE_MOUSE) {
            preview_max = UI_MOUSE_SLOT_COUNT; /* 8 mouse slots */
        } else if (profile_device_type == UI_DEVICE_TYPE_KEYBOARD) {
            preview_max = uk_qwerty_layout_count; /* Full keyboard layout */
        }

        if (button == SDL_GAMEPAD_BUTTON_EAST) {
            UI_HandleBack(state, done);
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
            if (state->profile_preview_index >= 0) {
                /* In preview area - use spatial navigation */
                if (profile_device_type == UI_DEVICE_TYPE_GAMEPAD) {
                    int new_idx = gamepad_nav_up[state->profile_preview_index];
                    if (new_idx == NAV_IDX_ACTION || new_idx < 0) {
                        /* Exit preview area, go to action buttons */
                        state->profile_preview_index = -1;
                        state->profile_action_focus = 0;
                    } else if (new_idx >= 0 && new_idx < NAV_TOTAL) {
                        state->profile_preview_index = new_idx;
                    }
                } else if (profile_device_type == UI_DEVICE_TYPE_MOUSE) {
                    int new_idx = mouse_nav_up[state->profile_preview_index];
                    if (new_idx == MOUSE_NAV_ACTION) {
                        /* At top of column - go to action buttons (Edit) */
                        state->profile_preview_index = -1;
                        state->profile_action_focus = 0;
                    } else if (new_idx >= 0) {
                        state->profile_preview_index = new_idx;
                    }
                } else if (profile_device_type == UI_DEVICE_TYPE_KEYBOARD) {
                    /* Keyboard: spatial navigation using uk_qwerty_layout */
                    int idx = state->profile_preview_index;
                    if (idx >= 0 && idx < uk_qwerty_layout_count) {
                        float current_row = uk_qwerty_layout[idx].row;
                        float current_col = uk_qwerty_layout[idx].col;
                        float target_row = current_row - 1.0f;
                        int best_idx = -1;
                        float best_dist = 1000.0f;
                        for (int i = 0; i < uk_qwerty_layout_count; i++) {
                            if (SDL_fabsf(uk_qwerty_layout[i].row - target_row) < 0.5f) {
                                float dist = SDL_fabsf(uk_qwerty_layout[i].col - current_col);
                                if (dist < best_dist) {
                                    best_dist = dist;
                                    best_idx = i;
                                }
                            }
                        }
                        if (best_idx >= 0) {
                            state->profile_preview_index = best_idx;
                        } else {
                            /* No row above - go to action buttons */
                            state->profile_preview_index = -1;
                            state->profile_action_focus = 0;
                        }
                    }
                } else {
                    state->profile_preview_index--;
                    if (state->profile_preview_index < 0) {
                        state->profile_preview_index = -1;
                        state->profile_action_focus = 0;
                    }
                }
            } else if (state->profile_action_focus >= 0) {
                /* In action buttons - move up to preview area */
                if (profile_device_type == UI_DEVICE_TYPE_GAMEPAD) {
                    state->profile_action_focus = -1;
                    if (state->profile_gamepad_origin_index >= 0) {
                        state->profile_preview_index = state->profile_gamepad_origin_index;
                    } else {
                        state->profile_preview_index = 7; /* RS - bottom of right column */
                    }
                    state->profile_gamepad_origin_index = -1;
                } else if (profile_device_type == UI_DEVICE_TYPE_MOUSE) {
                    state->profile_action_focus = -1;
                    if (state->profile_mouse_origin_index >= 0) {
                        state->profile_preview_index = state->profile_mouse_origin_index;
                    } else {
                        state->profile_preview_index = 7; /* default to bottom right */
                    }
                    state->profile_mouse_origin_index = -1;
                } else {
                    /* For other devices, go to left column */
                    state->profile_action_focus = -1;
                    state->profile_focus_on_new_button = false;
                    state->selected_profile = 0;
                }
            } else if (state->profile_focus_on_new_button) {
                /* Already at top - do nothing */
            } else {
                /* Move up in profile list or to new button */
                if (state->selected_profile > 0) {
                    state->selected_profile--;
                } else {
                    state->profile_focus_on_new_button = true;
                }
            }
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
            if (state->profile_preview_index >= 0) {
                /* In preview area - use spatial navigation */
                if (profile_device_type == UI_DEVICE_TYPE_GAMEPAD) {
                    int current_idx = state->profile_preview_index;
                    int new_idx = gamepad_nav_down[current_idx];
                    if (new_idx == NAV_IDX_ACTION) {
                        /* Go to Back button from bottom of either column */
                        state->profile_preview_index = -1;
                        state->profile_gamepad_origin_index = current_idx;
                        state->profile_action_focus = 4; /* Back button */
                    } else if (new_idx >= 0 && new_idx < NAV_TOTAL) {
                        state->profile_preview_index = new_idx;
                    }
                    /* If -1, stay in place */
                } else if (profile_device_type == UI_DEVICE_TYPE_MOUSE) {
                    int current_idx = state->profile_preview_index;
                    int new_idx = mouse_nav_down[current_idx];
                    if (new_idx == MOUSE_NAV_ACTION) {
                        /* Go to Back button from bottom of either column */
                        state->profile_mouse_origin_index = current_idx;
                        state->profile_preview_index = -1;
                        state->profile_action_focus = 4; /* Back button */
                    } else if (new_idx >= 0) {
                        state->profile_preview_index = new_idx;
                    }
                } else if (profile_device_type == UI_DEVICE_TYPE_KEYBOARD) {
                    /* Keyboard: spatial navigation using uk_qwerty_layout */
                    int idx = state->profile_preview_index;
                    if (idx >= 0 && idx < uk_qwerty_layout_count) {
                        float current_row = uk_qwerty_layout[idx].row;
                        float current_col = uk_qwerty_layout[idx].col;
                        float target_row = current_row + 1.0f;
                        int best_idx = -1;
                        float best_dist = 1000.0f;
                        for (int i = 0; i < uk_qwerty_layout_count; i++) {
                            if (SDL_fabsf(uk_qwerty_layout[i].row - target_row) < 0.5f) {
                                float dist = SDL_fabsf(uk_qwerty_layout[i].col - current_col);
                                if (dist < best_dist) {
                                    best_dist = dist;
                                    best_idx = i;
                                }
                            }
                        }
                        if (best_idx >= 0) {
                            state->profile_preview_index = best_idx;
                        } else {
                            /* No row below - go to Back button */
                            state->profile_preview_index = -1;
                            state->profile_action_focus = 4;
                        }
                    }
                } else {
                    state->profile_preview_index++;
                    if (state->profile_preview_index >= preview_max) {
                        state->profile_preview_index = preview_max - 1;
                    }
                }
            } else if (state->profile_action_focus >= 0) {
                /* In action buttons - move down to preview area (top of left column) */
                state->profile_action_focus = -1;
                state->profile_mouse_origin_index = -1;
                state->profile_gamepad_origin_index = -1;
                if (profile_device_type == UI_DEVICE_TYPE_GAMEPAD) {
                    state->profile_preview_index = NAV_IDX_LS_MOVE; /* LS Move - top of left column */
                } else if (profile_device_type == UI_DEVICE_TYPE_MOUSE) {
                    state->profile_preview_index = 0; /* Left Click - top of left column */
                } else if (profile_device_type == UI_DEVICE_TYPE_KEYBOARD) {
                    state->profile_preview_index = 0; /* First key in layout */
                } else {
                    state->profile_preview_index = 0;
                }
            } else if (state->profile_focus_on_new_button) {
                state->profile_focus_on_new_button = false;
                state->selected_profile = 0;
            } else {
                /* In profile list - move down in list */
                if (state->selected_profile < state->profile_count - 1) {
                    state->selected_profile++;
                }
            }
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
            if (state->profile_preview_index >= 0) {
                /* In preview area - use spatial navigation */
                if (profile_device_type == UI_DEVICE_TYPE_GAMEPAD) {
                    int new_idx = gamepad_nav_right[state->profile_preview_index];
                    if (new_idx >= 0 && new_idx < NAV_TOTAL) {
                        state->profile_preview_index = new_idx;
                    }
                    /* If -1, stay in place */
                } else if (profile_device_type == UI_DEVICE_TYPE_MOUSE) {
                    int new_idx = mouse_nav_right[state->profile_preview_index];
                    if (new_idx >= 0) {
                        state->profile_preview_index = new_idx;
                    }
                    /* If -1, stay in place */
                } else if (profile_device_type == UI_DEVICE_TYPE_KEYBOARD) {
                    /* Keyboard: spatial navigation using uk_qwerty_layout */
                    int idx = state->profile_preview_index;
                    if (idx >= 0 && idx < uk_qwerty_layout_count) {
                        float current_row = uk_qwerty_layout[idx].row;
                        float current_col = uk_qwerty_layout[idx].col;
                        int best_idx = -1;
                        float best_col = 1000.0f;
                        for (int i = 0; i < uk_qwerty_layout_count; i++) {
                            if (SDL_fabsf(uk_qwerty_layout[i].row - current_row) < 0.5f &&
                                uk_qwerty_layout[i].col > current_col + 0.1f &&
                                uk_qwerty_layout[i].col < best_col) {
                                best_col = uk_qwerty_layout[i].col;
                                best_idx = i;
                            }
                        }
                        if (best_idx >= 0) {
                            state->profile_preview_index = best_idx;
                        }
                    }
                } else {
                    state->profile_preview_index++;
                    if (state->profile_preview_index >= preview_max) {
                        state->profile_preview_index = preview_max - 1;
                    }
                }
            } else if (state->profile_action_focus == -1) {
                /* In left column - move right to action buttons */
                state->profile_action_focus = 0; /* Edit */
            } else if (state->profile_action_focus < 4) {
                /* In action buttons - move right */
                state->profile_action_focus++;
            }
            /* At Back button (4), stay there - don't wrap */
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
            if (state->profile_preview_index >= 0) {
                /* In preview area - use spatial navigation */
                if (profile_device_type == UI_DEVICE_TYPE_GAMEPAD) {
                    int new_idx = gamepad_nav_left[state->profile_preview_index];
                    if (new_idx >= 0 && new_idx < NAV_TOTAL) {
                        state->profile_preview_index = new_idx;
                    }
                    /* If -1, stay in place */
                } else if (profile_device_type == UI_DEVICE_TYPE_MOUSE) {
                    int new_idx = mouse_nav_left[state->profile_preview_index];
                    if (new_idx >= 0) {
                        state->profile_preview_index = new_idx;
                    }
                    /* If -1, stay in place */
                } else if (profile_device_type == UI_DEVICE_TYPE_KEYBOARD) {
                    /* Keyboard: spatial navigation using uk_qwerty_layout */
                    int idx = state->profile_preview_index;
                    if (idx >= 0 && idx < uk_qwerty_layout_count) {
                        float current_row = uk_qwerty_layout[idx].row;
                        float current_col = uk_qwerty_layout[idx].col;
                        int best_idx = -1;
                        float best_col = -1000.0f;
                        for (int i = 0; i < uk_qwerty_layout_count; i++) {
                            if (SDL_fabsf(uk_qwerty_layout[i].row - current_row) < 0.5f &&
                                uk_qwerty_layout[i].col < current_col - 0.1f &&
                                uk_qwerty_layout[i].col > best_col) {
                                best_col = uk_qwerty_layout[i].col;
                                best_idx = i;
                            }
                        }
                        if (best_idx >= 0) {
                            state->profile_preview_index = best_idx;
                        }
                    }
                } else {
                    state->profile_preview_index--;
                    if (state->profile_preview_index < 0) {
                        state->profile_preview_index = 0;
                    }
                }
            } else if (state->profile_action_focus > 0) {
                /* In action buttons - move left */
                state->profile_action_focus--;
            } else if (state->profile_action_focus == 0) {
                /* At Edit button - move left to profile list */
                state->profile_action_focus = -1;
            }
        } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
            if (state->profile_preview_index >= 0) {
                /* In preview area - open read-only dialog for this element */
                /* Determine device type for appropriate element handling */
                UIDeviceType device_type = UI_DEVICE_TYPE_GAMEPAD;
                if (state->device_count > 0) {
                    int dev_idx = state->selected_device;
                    if (dev_idx < 0) dev_idx = 0;
                    if (dev_idx >= state->device_count) dev_idx = state->device_count - 1;
                    device_type = state->device_types[dev_idx];
                }

                int idx = state->profile_preview_index;
                state->dialog_read_only = true;
                state->dialog_focus_index = 0;
                state->selected_keyboard_slot = -1;
                state->selected_mouse_slot = -1;
                state->selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                state->selected_axis = SDL_GAMEPAD_AXIS_INVALID;

                if (device_type == UI_DEVICE_TYPE_KEYBOARD) {
                    /* Keyboard preview - use full uk_qwerty_layout */
                    if (idx >= 0 && idx < uk_qwerty_layout_count) {
                        state->selected_keyboard_slot = (int)uk_qwerty_layout[idx].scancode;
                        state->active_tab = 2;
                        state->active_dialog = DIALOG_BUTTON_OPTIONS;
                    }
                } else if (device_type == UI_DEVICE_TYPE_MOUSE) {
                    /* Mouse preview - 8 mouse slots */
                    if (idx >= 0 && idx < UI_MOUSE_SLOT_COUNT) {
                        state->selected_mouse_slot = idx;
                        state->dialog_read_only = false;
                        state->dialog_focus_index = 0;
                        state->active_tab = 1;
                        if (idx == UI_MOUSE_SLOT_MOVE) {
                            UI_LoadMouseMoveState(state);
                            state->active_dialog = DIALOG_MOUSE_MOVE_CONFIG;
                        } else {
                            state->active_dialog = DIALOG_BUTTON_OPTIONS;
                        }
                    }
                } else {
                    /* Gamepad preview - 14 buttons + 2 triggers + 2 sticks */
                    if (idx < REMAPPING_BUTTON_COUNT) {
                        state->selected_button = remapping_buttons[idx].button;
                        state->active_dialog = DIALOG_BUTTON_OPTIONS;
                    } else if (idx == REMAPPING_BUTTON_COUNT) {
                        state->selected_axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                        state->active_slot = 0;
                        state->active_dialog = DIALOG_TRIGGER_OPTIONS;
                    } else if (idx == REMAPPING_BUTTON_COUNT + 1) {
                        state->selected_axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                        state->active_slot = 0;
                        state->active_dialog = DIALOG_TRIGGER_OPTIONS;
                    } else if (idx == REMAPPING_BUTTON_COUNT + 2) {
                        state->selected_axis = SDL_GAMEPAD_AXIS_LEFTX;
                        UI_LoadStickStateFromAxis(SDL_GAMEPAD_AXIS_LEFTX, state);
                        state->active_dialog = DIALOG_STICK_CONFIG;
                    } else if (idx == REMAPPING_BUTTON_COUNT + 3) {
                        state->selected_axis = SDL_GAMEPAD_AXIS_RIGHTX;
                        UI_LoadStickStateFromAxis(SDL_GAMEPAD_AXIS_RIGHTX, state);
                        state->active_dialog = DIALOG_STICK_CONFIG;
                    }
                }
            } else if (state->profile_focus_on_new_button) {
                /* Create new profile */
                if (state->profile_count < SDL_UI_MAX_PROFILES) {
                    int index = state->profile_count;
                    SDL_snprintf(state->profile_names[index], sizeof(state->profile_names[index]), "New Profile %d", index + 1);
                    state->profile_count++;
                    state->selected_profile = index;
                    UI_InitProfileMappings(index);
                    UI_CommitProfileToContext(ctx, gamepad_id, state);
                    UI_SaveCurrentProfileToDisk(state);
                }
            } else if (state->profile_action_focus == 0) {
                /* Edit */
                state->mapping_action_focus = -1;
                state->current_page = PAGE_BUTTON_MAPPING;
            } else if (state->profile_action_focus == 1) {
                /* Duplicate */
                if (state->profile_count < SDL_UI_MAX_PROFILES && state->selected_profile >= 0) {
                    int src = state->selected_profile;
                    int dst = state->profile_count;
                    SDL_snprintf(state->profile_names[dst], 64, "%s (Copy)", state->profile_names[src]);
                    SDL_memcpy(g_ui_profile_mappings[dst], g_ui_profile_mappings[src],
                               sizeof(g_ui_profile_mappings[dst]));
                    g_ui_profile_trigger_deadzone_left[dst] = g_ui_profile_trigger_deadzone_left[src];
                    g_ui_profile_trigger_deadzone_right[dst] = g_ui_profile_trigger_deadzone_right[src];
                    state->profile_count++;
                    state->selected_profile = dst;
                    UI_CommitProfileToContext(ctx, gamepad_id, state);
                    UI_SaveCurrentProfileToDisk(state);
                }
            } else if (state->profile_action_focus == 2) {
                /* Delete - open confirmation dialog */
                if (state->profile_count > 1 && state->selected_profile > 0) {
                    state->dialog_focus_index = 1; /* Default to No */
                    state->active_dialog = DIALOG_DELETE_CONFIRM;
                }
            } else if (state->profile_action_focus == 3) {
                /* Rename - open rename dialog (not for default profile) */
                if (state->selected_profile > 0 && state->selected_profile < state->profile_count) {
                    SDL_strlcpy(state->input_buffer, state->profile_names[state->selected_profile], 64);
                    state->input_cursor = (int)SDL_strlen(state->input_buffer);
                    state->dialog_focus_index = 0; /* Default to text field */
                    state->active_dialog = DIALOG_RENAME_PROFILE;
                }
            } else if (state->profile_action_focus == 4) {
                /* Back */
                UI_HandleBack(state, done);
            }
        }
        }
        break;

    case PAGE_BUTTON_MAPPING:
        if (button == SDL_GAMEPAD_BUTTON_EAST) {
            UI_HandleBack(state, done);
        } else {
            /* Determine device type for navigation */
            UIDeviceType device_type = UI_DEVICE_TYPE_GAMEPAD;
            if (state->device_count > 0) {
                int idx = state->selected_device;
                if (idx < 0) idx = 0;
                if (idx >= state->device_count) idx = state->device_count - 1;
                device_type = state->device_types[idx];
            }

            int preview_index = (state && state->profile_preview_index >= 0) ? state->profile_preview_index : -1;

        if (device_type == UI_DEVICE_TYPE_MOUSE) {
                /* Mouse: 8 slots in 2x4 layout - use same navigation tables as profile page */
                /* Layout order mapping for slot <-> index conversion:
                 * Index 0: Left Click,   Index 1: Right Click
                 * Index 2: Mouse Move,   Index 3: Wheel Up
                 * Index 4: Forward (X2), Index 5: Middle Click
                 * Index 6: Back (X1),    Index 7: Wheel Down
                 */
                static const int mouse_slot_to_idx[8] = {
                    0,  /* UI_MOUSE_SLOT_LEFT  -> idx 0 */
                    1,  /* UI_MOUSE_SLOT_RIGHT -> idx 1 */
                    5,  /* UI_MOUSE_SLOT_MIDDLE -> idx 5 */
                    6,  /* UI_MOUSE_SLOT_X1 (Back) -> idx 6 */
                    4,  /* UI_MOUSE_SLOT_X2 (Forward) -> idx 4 */
                    3,  /* UI_MOUSE_SLOT_WHEEL_UP -> idx 3 */
                    7,  /* UI_MOUSE_SLOT_WHEEL_DOWN -> idx 7 */
                    2   /* UI_MOUSE_SLOT_MOVE -> idx 2 */
                };
                static const int mouse_idx_to_slot[8] = {
                    UI_MOUSE_SLOT_LEFT,       /* idx 0 */
                    UI_MOUSE_SLOT_RIGHT,      /* idx 1 */
                    UI_MOUSE_SLOT_MOVE,       /* idx 2 */
                    UI_MOUSE_SLOT_WHEEL_UP,   /* idx 3 */
                    UI_MOUSE_SLOT_X2,         /* idx 4 (Forward) */
                    UI_MOUSE_SLOT_MIDDLE,     /* idx 5 */
                    UI_MOUSE_SLOT_X1,         /* idx 6 (Back) */
                    UI_MOUSE_SLOT_WHEEL_DOWN  /* idx 7 */
                };

                if (state->mapping_action_focus == -1) {
                    /* In mouse grid - use spatial navigation tables */
                    int current_idx = 0;
                    if (state->selected_mouse_slot >= 0 && state->selected_mouse_slot < 8) {
                        current_idx = mouse_slot_to_idx[state->selected_mouse_slot];
                    }

                    int new_idx = current_idx;
                    if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                        new_idx = mouse_nav_up[current_idx];
                        if (new_idx >= 0 && new_idx < 8) {
                            state->selected_mouse_slot = mouse_idx_to_slot[new_idx];
                        }
                        /* At top of column, stay in place */
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                        new_idx = mouse_nav_down[current_idx];
                        if (new_idx == MOUSE_NAV_ACTION) {
                            /* Moving from grid to action buttons - store origin for return path */
                            state->mouse_mapping_origin_slot = state->selected_mouse_slot;
                            state->selected_mouse_slot = -1;
                            state->mapping_action_focus = 0;
                        } else if (new_idx >= 0 && new_idx < 8) {
                            state->selected_mouse_slot = mouse_idx_to_slot[new_idx];
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                        new_idx = mouse_nav_left[current_idx];
                        if (new_idx >= 0 && new_idx < 8) {
                            state->selected_mouse_slot = mouse_idx_to_slot[new_idx];
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                        new_idx = mouse_nav_right[current_idx];
                        if (new_idx >= 0 && new_idx < 8) {
                            state->selected_mouse_slot = mouse_idx_to_slot[new_idx];
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        /* Open dialog for selected mouse slot */
                        state->selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                        state->selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                        state->selected_keyboard_slot = -1;
                        state->mapping_from_trigger = false;
                        state->dialog_read_only = false;
                        state->active_slot = 0;
                        state->active_tab = 1;
                        state->dialog_focus_index = 0;

                        if (state->selected_mouse_slot == UI_MOUSE_SLOT_MOVE) {
                            UI_LoadMouseMoveState(state);
                            state->active_dialog = DIALOG_MOUSE_MOVE_CONFIG;
                        } else {
                            state->active_dialog = DIALOG_BUTTON_OPTIONS;
                        }
                    }
                } else {
                    /* On action buttons: 0 = Restore to Defaults, 1 = Back */
                    if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                        if (state->mapping_action_focus == 1) {
                            state->mapping_action_focus = 0;
                        } else {
                            state->mapping_action_focus = -1;
                            if (state->mouse_mapping_origin_slot >= 0 &&
                                state->mouse_mapping_origin_slot < UI_MOUSE_SLOT_COUNT) {
                                state->selected_mouse_slot = state->mouse_mapping_origin_slot;
                            }
                            state->mouse_mapping_origin_slot = -1;
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                        if (state->mapping_action_focus == 0) {
                            /* Restore to Defaults -> Back */
                            state->mapping_action_focus = 1;
                        }
                        /* From Back, stay */
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                        /* Toggle between Restore to Defaults (0) and Back (1) */
                        state->mapping_action_focus = (state->mapping_action_focus == 0) ? 1 : 0;
                    } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        if (state->mapping_action_focus == 1) {
                            UI_HandleBack(state, done);
                        } else if (state->mapping_action_focus == 0) {
                            /* Restore to Defaults - reset all mouse mappings for this profile */
                            UI_ResetMouseMappingsToDefaults(state);
                        }
                    }
                }
            } else if (device_type == UI_DEVICE_TYPE_KEYBOARD) {
                /* Keyboard navigation - use full uk_qwerty_layout with spatial navigation */
                if (state->mapping_action_focus == -1) {
                    /* Find current key's position in layout */
                    int current_idx = -1;
                    float current_row = 0, current_col = 0;
                    for (int i = 0; i < uk_qwerty_layout_count; i++) {
                        if ((int)uk_qwerty_layout[i].scancode == state->selected_keyboard_slot) {
                            current_idx = i;
                            current_row = uk_qwerty_layout[i].row;
                            current_col = uk_qwerty_layout[i].col;
                            break;
                        }
                    }
                    if (current_idx < 0) {
                        /* Default to first key (grave/backtick) */
                        current_idx = 0;
                        state->selected_keyboard_slot = (int)uk_qwerty_layout[0].scancode;
                        current_row = uk_qwerty_layout[0].row;
                        current_col = uk_qwerty_layout[0].col;
                    }

                    if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                        /* Find nearest key in the row above */
                        float target_row = current_row - 1.0f;
                        int best_idx = -1;
                        float best_dist = 1000.0f;
                        for (int i = 0; i < uk_qwerty_layout_count; i++) {
                            if (SDL_fabsf(uk_qwerty_layout[i].row - target_row) < 0.5f) {
                                float dist = SDL_fabsf(uk_qwerty_layout[i].col - current_col);
                                if (dist < best_dist) {
                                    best_dist = dist;
                                    best_idx = i;
                                }
                            }
                        }
                        if (best_idx >= 0) {
                            state->selected_keyboard_slot = (int)uk_qwerty_layout[best_idx].scancode;
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                        /* Find nearest key in the row below */
                        float target_row = current_row + 1.0f;
                        int best_idx = -1;
                        float best_dist = 1000.0f;
                        for (int i = 0; i < uk_qwerty_layout_count; i++) {
                            if (SDL_fabsf(uk_qwerty_layout[i].row - target_row) < 0.5f) {
                                float dist = SDL_fabsf(uk_qwerty_layout[i].col - current_col);
                                if (dist < best_dist) {
                                    best_dist = dist;
                                    best_idx = i;
                                }
                            }
                        }
                        if (best_idx >= 0) {
                            state->selected_keyboard_slot = (int)uk_qwerty_layout[best_idx].scancode;
                        } else {
                            /* No row below - go to action buttons */
                            state->keyboard_mapping_origin_slot = state->selected_keyboard_slot;
                            state->selected_keyboard_slot = -1;
                            state->mapping_action_focus = 0;
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                        /* Find nearest key to the left in same row */
                        int best_idx = -1;
                        float best_col = -1000.0f;
                        for (int i = 0; i < uk_qwerty_layout_count; i++) {
                            if (SDL_fabsf(uk_qwerty_layout[i].row - current_row) < 0.5f &&
                                uk_qwerty_layout[i].col < current_col - 0.1f &&
                                uk_qwerty_layout[i].col > best_col) {
                                best_col = uk_qwerty_layout[i].col;
                                best_idx = i;
                            }
                        }
                        if (best_idx >= 0) {
                            state->selected_keyboard_slot = (int)uk_qwerty_layout[best_idx].scancode;
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                        /* Find nearest key to the right in same row */
                        int best_idx = -1;
                        float best_col = 1000.0f;
                        for (int i = 0; i < uk_qwerty_layout_count; i++) {
                            if (SDL_fabsf(uk_qwerty_layout[i].row - current_row) < 0.5f &&
                                uk_qwerty_layout[i].col > current_col + 0.1f &&
                                uk_qwerty_layout[i].col < best_col) {
                                best_col = uk_qwerty_layout[i].col;
                                best_idx = i;
                            }
                        }
                        if (best_idx >= 0) {
                            state->selected_keyboard_slot = (int)uk_qwerty_layout[best_idx].scancode;
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        /* Open dialog if a key is selected */
                        if (state->selected_keyboard_slot >= 0 && state->selected_keyboard_slot < UI_KEYBOARD_SLOT_COUNT) {
                            state->selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                            state->selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                            state->selected_mouse_slot = -1;
                            state->mapping_from_trigger = false;
                            state->dialog_read_only = false;
                            state->active_slot = 0;
                            state->active_tab = 2;
                            state->dialog_focus_index = 0;
                            state->active_dialog = DIALOG_BUTTON_OPTIONS;
                        }
                    }
                } else {
                    /* On action buttons */
                    if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                        if (state->mapping_action_focus == 1) {
                            state->mapping_action_focus = 0;
                        } else {
                            state->mapping_action_focus = -1;
                            if (state->keyboard_mapping_origin_slot >= 0) {
                                state->selected_keyboard_slot = state->keyboard_mapping_origin_slot;
                            } else {
                                /* Default to Space key when returning from action buttons */
                                state->selected_keyboard_slot = (int)SDL_SCANCODE_SPACE;
                            }
                            state->keyboard_mapping_origin_slot = -1;
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                        if (state->mapping_action_focus == 0) {
                            state->mapping_action_focus = 1;
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                        state->mapping_action_focus = (state->mapping_action_focus == 0) ? 1 : 0;
                    } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        if (state->mapping_action_focus == 1) {
                            UI_HandleBack(state, done);
                        } else if (state->mapping_action_focus == 0) {
                            UI_ResetKeyboardMappingsToDefaults(state);
                        }
                    }
                }
            } else {
                /* Gamepad: spatial neighbor-based navigation through buttons/triggers/sticks */
                if (state->mapping_action_focus == -1) {
                    /* Determine current focus index using NAV_IDX constants */
                    int focus_idx = -1;
                    if (state->selected_button != SDL_GAMEPAD_BUTTON_INVALID) {
                        for (int i = 0; i < REMAPPING_BUTTON_COUNT; i++) {
                            if (remapping_buttons[i].button == state->selected_button) {
                                focus_idx = i;
                                break;
                            }
                        }
                    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                        focus_idx = NAV_IDX_LT;
                    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                        focus_idx = NAV_IDX_RT;
                    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_LEFTX || state->selected_axis == SDL_GAMEPAD_AXIS_LEFTY) {
                        focus_idx = NAV_IDX_LS_MOVE;
                    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHTX || state->selected_axis == SDL_GAMEPAD_AXIS_RIGHTY) {
                        focus_idx = NAV_IDX_RS_MOVE;
                    }

                    if (focus_idx < 0) {
                        focus_idx = 0;
                        state->selected_button = remapping_buttons[0].button;
                        state->selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                    }

                    /* Use spatial neighbor tables for navigation */
                    int new_idx = focus_idx;
                    if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                        new_idx = gamepad_nav_up[focus_idx];
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                        new_idx = gamepad_nav_down[focus_idx];
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                        new_idx = gamepad_nav_left[focus_idx];
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                        new_idx = gamepad_nav_right[focus_idx];
                    } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        /* Open appropriate dialog */
                        state->selected_keyboard_slot = -1;
                        state->selected_mouse_slot = -1;
                        state->dialog_read_only = false;
                        state->dialog_focus_index = 0;

                        if (focus_idx < REMAPPING_BUTTON_COUNT) {
                            state->active_dialog = DIALOG_BUTTON_OPTIONS;
                        } else if (focus_idx == NAV_IDX_LT || focus_idx == NAV_IDX_RT) {
                            state->mapping_from_trigger = false;
                            state->active_slot = 0;
                            state->active_dialog = DIALOG_TRIGGER_OPTIONS;
                        } else {
                            UI_LoadStickStateFromAxis(state->selected_axis, state);
                            state->active_dialog = DIALOG_STICK_CONFIG;
                        }
                    }

                    /* Handle special navigation results */
                    if (new_idx == NAV_IDX_ACTION) {
                        /* Go to Restore to Defaults button first */
                        state->mapping_action_focus = 0;
                        state->mapping_gamepad_origin_index = focus_idx;
                        state->selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                        state->selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                    } else if (new_idx >= 0 && new_idx < NAV_TOTAL) {
                        /* Update selection based on new_idx */
                        focus_idx = new_idx;
                        UI_SetGamepadSelectionFromNavIndex(state, focus_idx);
                        state->mapping_gamepad_origin_index = -1;
                    }
                    /* If new_idx == -1, stay in place (no neighbor in that direction) */
                } else {
                    /* On action buttons: 0 = Restore to Defaults, 1 = Back */
                    if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                        if (state->mapping_action_focus == 1) {
                            /* Back -> Restore to Defaults */
                            state->mapping_action_focus = 0;
                        } else {
                            /* Restore to Defaults -> return to stored origin (or default) */
                            state->mapping_action_focus = -1;
                            if (state->mapping_gamepad_origin_index >= 0) {
                                UI_SetGamepadSelectionFromNavIndex(state, state->mapping_gamepad_origin_index);
                            } else {
                                UI_SetGamepadSelectionFromNavIndex(state, NAV_IDX_RS_MOVE);
                            }
                            state->mapping_gamepad_origin_index = -1;
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                        if (state->mapping_action_focus == 0) {
                            /* Restore to Defaults -> Back */
                            state->mapping_action_focus = 1;
                        } else {
                            /* Back -> stay */
                        }
                    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                        /* Toggle between Restore to Defaults (0) and Back (1) */
                        state->mapping_action_focus = (state->mapping_action_focus == 0) ? 1 : 0;
                    } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        if (state->mapping_action_focus == 1) {
                            UI_HandleBack(state, done);
                        } else if (state->mapping_action_focus == 0) {
                            /* Restore to Defaults: reset current profile to passthrough */
                            int p = state->selected_profile;
                            if (p >= 0 && p < SDL_UI_MAX_PROFILES) {
                                UI_InitProfileMappings(p);
                                UI_InitGamepadPassthroughDefaultsForProfile(p);
                                /* Reset trigger deadzones to defaults */
                                g_ui_profile_trigger_deadzone_left[p] = 10.0f;
                                g_ui_profile_trigger_deadzone_right[p] = 10.0f;
                                state->trigger_deadzone_left = 10.0f;
                                state->trigger_deadzone_right = 10.0f;
                                UI_CommitProfileToContext(ctx, gamepad_id, state);
                                UI_SaveCurrentProfileToDisk(state);
                            }
                        }
                    }
                }
            }
        }
        break;
    }
}

/* Triggers and stick movement (special handling) - also from RemappingPage.xaml */
#define LT_X 1614.0f
#define LT_Y 417.0f               /* Row 0, Margin Y=417 */
#define RT_X 2175.0f
#define RT_Y 417.0f
#define LS_MOVE_X 1035.0f
#define LS_MOVE_Y 548.0f          /* Row 0, Margin Y=548 */
#define RS_MOVE_X 2775.0f
#define RS_MOVE_Y 1399.0f         /* Grid.Row=1, Margin Y=401 => 401 + 998 */


/* All icon textures from all three asset folders */
typedef struct {
    const char *tag;       /* e.g. "A Button", "Left Click", "W" */
    const char *folder;    /* "Controller", "Mouse", "Keyboard" */
    const char *filename;  /* PNG filename */
    SDL_Texture *texture;  /* Loaded texture, or NULL if not loaded */
} AllIconTexture;

static AllIconTexture g_all_icons[] = {
    /* Controller icons */
    { "A Button",           "Controller", "GamepadButtonA.png",        NULL },
    { "B Button",           "Controller", "GamepadButtonB.png",        NULL },
    { "X Button",           "Controller", "GamepadButtonX.png",        NULL },
    { "Y Button",           "Controller", "GamepadButtonY.png",        NULL },
    { "Left Bumper",        "Controller", "GamepadButtonLB.png",       NULL },
    { "Right Bumper",       "Controller", "GamepadButtonRB.png",       NULL },
    { "Left Trigger",       "Controller", "GamepadButtonLT.png",       NULL },
    { "Right Trigger",      "Controller", "GamepadButtonRT.png",       NULL },
    { "Left Stick",         "Controller", "GamepadButtonLeftStick.png", NULL },
    { "Right Stick",        "Controller", "GamepadButtonRightStick.png",NULL },
    { "D-Pad Up",           "Controller", "GamepadDpadUp.png",         NULL },
    { "D-Pad Down",         "Controller", "GamepadDpadDown.png",       NULL },
    { "D-Pad Left",         "Controller", "GamepadDpadLeft.png",       NULL },
    { "D-Pad Right",        "Controller", "GamepadDpadRight.png",      NULL },
    { "View Button",        "Controller", "GamepadButtonView.png",     NULL },
    { "Menu Button",        "Controller", "GamepadButtonMenu.png",     NULL },
    { "Left Stick Move",    "Controller", "GamepadStickLeftMove.png",  NULL },
    { "Right Stick Move",   "Controller", "GamepadStickRightMove.png", NULL },

    /* Mouse icons */
    { "Left Click",         "Mouse",      "MouseButtonLeft.png",    NULL },
    { "Right Click",        "Mouse",      "MouseButtonRight.png",   NULL },
    { "Middle Click",       "Mouse",      "MouseButtonMiddle.png",  NULL },
    { "Mouse Back",         "Mouse",      "MouseButtonBack.png",    NULL },
    { "Mouse Forward",      "Mouse",      "MouseButtonForward.png", NULL },
    { "Scroll Up",          "Mouse",      "MouseWheelUp.png",       NULL },
    { "Scroll Down",        "Mouse",      "MouseWheelDown.png",     NULL },
    { "Scroll Left",        "Mouse",      "MouseWheelLeft.png",     NULL },
    { "Scroll Right",       "Mouse",      "MouseWheelRight.png",    NULL },
    { "Mouse Move",         "Mouse",      "MouseMotion.png",        NULL },

    /* Keyboard icons */
    { "W",                  "Keyboard",   "KeyW.png",           NULL },
    { "A",                  "Keyboard",   "KeyA.png",           NULL },
    { "S",                  "Keyboard",   "KeyS.png",           NULL },
    { "D",                  "Keyboard",   "KeyD.png",           NULL },
    { "Up",                 "Keyboard",   "KeyArrowUp.png",     NULL },
    { "Down",               "Keyboard",   "KeyArrowDown.png",   NULL },
    { "Left",               "Keyboard",   "KeyArrowLeft.png",   NULL },
    { "Right",              "Keyboard",   "KeyArrowRight.png",  NULL },
    { "Space",              "Keyboard",   "KeySpace.png",       NULL },
    { "Return",             "Keyboard",   "KeyEnter.png",       NULL },
    { "Escape",             "Keyboard",   "KeyEscape.png",      NULL },
    { "Left Shift",         "Keyboard",   "LeftShiftKey.png",   NULL },
    { "Left Ctrl",          "Keyboard",   "KeyCtrl.png",        NULL },

    /* Touch icons */
    { "Touch Tap",          "Touch",      "TouchTap.png",           NULL },
    { "Touch Hold",         "Touch",      "TouchHold.png",          NULL },
    { "Touch Double Tap",   "Touch",      "TouchDoubleTap.png",     NULL },
    { "Touch Swipe Up",     "Touch",      "TouchSwipeUp.png",       NULL },
    { "Touch Swipe Down",   "Touch",      "TouchSwipeDown.png",     NULL },
    { "Touch Swipe Left",   "Touch",      "TouchSwipeLeft.png",     NULL },
    { "Touch Swipe Right",  "Touch",      "TouchSwipeRight.png",    NULL },
    { "Touch Finger2 Tap",  "Touch",      "TouchFinger2Tap.png",    NULL },
    { "Touch Finger2 Hold", "Touch",      "TouchFinger2Hold.png",   NULL },
    { "Touch Pinch In",     "Touch",      "TouchPinchIn.png",       NULL },
    { "Touch Pinch Out",    "Touch",      "TouchPinchOut.png",      NULL },
    { "Touch Rotate CW",    "Touch",      "TouchRotateCW.png",      NULL },
    { "Touch Rotate CCW",   "Touch",      "TouchRotateCCW.png",     NULL }
};

/* Page-specific background textures (forward declarations) */
static SDL_Texture *g_controller_device_bg = NULL;
static SDL_Texture *g_controller_profile_bg = NULL;
static SDL_Texture *g_controller_remapper_bg = NULL;
static SDL_Texture *g_mouse_device_bg = NULL;
static SDL_Texture *g_mouse_profile_bg = NULL;
static SDL_Texture *g_mouse_remapper_bg = NULL;
static SDL_Texture *g_keyboard_device_bg = NULL;

/* Get the SDL repo root directory from this source file's path */
static const char *
UI_GetSDLSourceDir(void)
{
    static char sdl_root[512] = {0};
    if (sdl_root[0] == '\0') {
        /* __FILE__ contains path to this source file: .../SDL/src/joystick/SDL_remapper_ui.c */
        /* We need to go up to the SDL root directory */
        const char *this_file = __FILE__;
        SDL_strlcpy(sdl_root, this_file, sizeof(sdl_root));

        /* Find and remove "src/joystick/SDL_remapper_ui.c" or "src\\joystick\\SDL_remapper_ui.c" */
        char *p = SDL_strstr(sdl_root, "src");
        if (p && p > sdl_root) {
            *p = '\0';
        }
    }
    return sdl_root;
}

static void
UI_LoadAllIcons(SDL_Renderer *renderer)
{
    /* Use paths relative to SDL source directory */
    const char *folder_names[4] = {
        "SDL Remapper Assets/Controller/ControllerMapImages",
        "SDL Remapper Assets/Mouse/MouseMapImages",
        "SDL Remapper Assets/Keyboard/KeyboardMapImages",
        "SDL Remapper Assets/Touch/TouchMapImages"
    };

    if (!renderer) {
        return;
    }

    const char *sdl_root = UI_GetSDLSourceDir();

    for (int i = 0; i < SDL_UI_ARRAY_SIZE(g_all_icons); ++i) {
        AllIconTexture *icon = &g_all_icons[i];

        if (icon->texture) {
            continue; /* Already loaded */
        }

        int folder_idx = 0;
        if (SDL_strcmp(icon->folder, "Mouse") == 0) folder_idx = 1;
        else if (SDL_strcmp(icon->folder, "Keyboard") == 0) folder_idx = 2;
        else if (SDL_strcmp(icon->folder, "Touch") == 0) folder_idx = 3;

        char full_path[512];
        SDL_snprintf(full_path, sizeof(full_path), "%s%s/%s",
                     sdl_root, folder_names[folder_idx], icon->filename);

        SDL_Surface *surface = SDL_LoadPNG(full_path);
        if (!surface) {
            SDL_Log("Failed to load icon '%s' from %s folder: %s", full_path, icon->folder, SDL_GetError());
            continue;
        }

        icon->texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);

        if (!icon->texture) {
            SDL_Log("Failed to create texture for icon '%s': %s", full_path, SDL_GetError());
        }
    }
}

static SDL_Texture *
UI_GetIconTexture(const char *tag)
{
    if (!tag) {
        return NULL;
    }

    for (int i = 0; i < SDL_UI_ARRAY_SIZE(g_all_icons); ++i) {
        if (SDL_strcmp(g_all_icons[i].tag, tag) == 0) {
            return g_all_icons[i].texture;
        }
    }

    return NULL;
}

/* Populate the device list for the landing page based on connected hardware. */
static void
UI_InitDeviceList(UIState *state, SDL_JoystickID default_gamepad_id)
{
    int i;

    if (!state) {
        return;
    }

    state->device_count = 0;
    state->selected_device = 0;
    SDL_memset(state->device_types, 0, sizeof(state->device_types));
    SDL_memset(state->device_labels, 0, sizeof(state->device_labels));
    SDL_memset(state->device_gamepad_ids, 0, sizeof(state->device_gamepad_ids));
    state->active_mouse_id = 0;
    state->active_keyboard_id = 0;

    /* Gamepads */
    {
        int gp_count = 0;
        SDL_JoystickID *gamepad_ids = SDL_GetGamepads(&gp_count);
        if (gamepad_ids) {
            for (i = 0; i < gp_count && state->device_count < SDL_UI_MAX_DEVICES; ++i) {
                SDL_JoystickID id = gamepad_ids[i];
                const char *name;
                int idx;

                if (id == 0) {
                    continue;
                }

                idx = state->device_count++;
                state->device_types[idx] = UI_DEVICE_TYPE_GAMEPAD;
                name = SDL_GetGamepadNameForID(id);
                if (!name || !*name) {
                    name = "Gamepad";
                }
                SDL_strlcpy(state->device_labels[idx], name, sizeof(state->device_labels[idx]));
                state->device_gamepad_ids[idx] = id;

                if (id == default_gamepad_id) {
                    state->selected_device = idx;
                }
            }
            SDL_free(gamepad_ids);
        }
    }

    /* Keyboard (aggregate) */
    if (state->device_count < SDL_UI_MAX_DEVICES && SDL_HasKeyboard()) {
        int idx = state->device_count++;
        state->device_types[idx] = UI_DEVICE_TYPE_KEYBOARD;
        SDL_strlcpy(state->device_labels[idx], "Keyboard", sizeof(state->device_labels[idx]));
        state->device_gamepad_ids[idx] = 0;

        /* Query the list of keyboards and pick the first as the active keyboard ID. */
        {
            int keyboard_count = 0;
            SDL_KeyboardID *keyboards = SDL_GetKeyboards(&keyboard_count);
            if (keyboards) {
                if (keyboard_count > 0) {
                    state->active_keyboard_id = keyboards[0];
                }
                SDL_free(keyboards);
            }
        }
    }

    /* Mouse (aggregate) */
    if (state->device_count < SDL_UI_MAX_DEVICES && SDL_HasMouse()) {
        int idx = state->device_count++;
        state->device_types[idx] = UI_DEVICE_TYPE_MOUSE;
        SDL_strlcpy(state->device_labels[idx], "Mouse", sizeof(state->device_labels[idx]));
        state->device_gamepad_ids[idx] = 0;

        /* Query the list of mice and pick the first as the active mouse ID. */
        {
            int mouse_count = 0;
            SDL_MouseID *mice = SDL_GetMice(&mouse_count);
            if (mice) {
                if (mouse_count > 0) {
                    state->active_mouse_id = mice[0];
                }
                SDL_free(mice);
            }
        }
    }

    /* Generic joysticks that are not exposed as gamepads */
    if (state->device_count < SDL_UI_MAX_DEVICES && SDL_HasJoystick()) {
        int joy_count = 0;
        SDL_JoystickID *joy_ids = SDL_GetJoysticks(&joy_count);
        if (joy_ids) {
            for (i = 0; i < joy_count && state->device_count < SDL_UI_MAX_DEVICES; ++i) {
                SDL_JoystickID jid = joy_ids[i];
                const char *name;
                int idx;

                if (SDL_IsGamepad(jid)) {
                    continue; /* already listed as a gamepad */
                }

                idx = state->device_count++;
                state->device_types[idx] = UI_DEVICE_TYPE_JOYSTICK;
                name = SDL_GetJoystickNameForID(jid);
                if (!name || !*name) {
                    name = "Joystick";
                }
                SDL_strlcpy(state->device_labels[idx], name, sizeof(state->device_labels[idx]));
                state->device_gamepad_ids[idx] = 0;
            }
            SDL_free(joy_ids);
        }
    }

    /* Fallback: if nothing was detected but a gamepad ID was provided, at least
     * show a single generic gamepad entry so the UI remains usable. */
    if (state->device_count == 0 && default_gamepad_id != 0) {
        int idx = 0;
        const char *name;

        state->device_count = 1;
        state->selected_device = 0;
        state->device_types[0] = UI_DEVICE_TYPE_GAMEPAD;

        name = SDL_GetGamepadNameForID(default_gamepad_id);
        if (!name || !*name) {
            name = "Gamepad";
        }
        SDL_strlcpy(state->device_labels[idx], name, sizeof(state->device_labels[idx]));
        state->device_gamepad_ids[idx] = default_gamepad_id;
    }
}

/* Get icon texture for a given SDL_RemapperAction (follows ImageHelper.cs logic) */
static SDL_Texture *
UI_GetActionIconTexture(const SDL_RemapperAction *action)
{
    if (!action || action->kind == SDL_REMAPPER_ACTION_NONE) {
        return NULL;
    }

    switch (action->kind) {
    case SDL_REMAPPER_ACTION_GAMEPAD_BUTTON:
        switch ((SDL_GamepadButton)action->code) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return UI_GetIconTexture("A Button");
        case SDL_GAMEPAD_BUTTON_EAST:  return UI_GetIconTexture("B Button");
        case SDL_GAMEPAD_BUTTON_WEST:  return UI_GetIconTexture("X Button");
        case SDL_GAMEPAD_BUTTON_NORTH: return UI_GetIconTexture("Y Button");
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return UI_GetIconTexture("Left Bumper");
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return UI_GetIconTexture("Right Bumper");
        case SDL_GAMEPAD_BUTTON_BACK:  return UI_GetIconTexture("View Button");
        case SDL_GAMEPAD_BUTTON_START: return UI_GetIconTexture("Menu Button");
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:  return UI_GetIconTexture("Left Stick");
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return UI_GetIconTexture("Right Stick");
        case SDL_GAMEPAD_BUTTON_DPAD_UP:    return UI_GetIconTexture("D-Pad Up");
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  return UI_GetIconTexture("D-Pad Down");
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  return UI_GetIconTexture("D-Pad Left");
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return UI_GetIconTexture("D-Pad Right");
        default: break;
        }
        break;
    case SDL_REMAPPER_ACTION_MOUSE_BUTTON:
        if (action->code == SDL_BUTTON_LEFT) return UI_GetIconTexture("Left Click");
        else if (action->code == SDL_BUTTON_RIGHT) return UI_GetIconTexture("Right Click");
        else if (action->code == SDL_BUTTON_MIDDLE) return UI_GetIconTexture("Middle Click");
        else if (action->code == SDL_BUTTON_X1) return UI_GetIconTexture("Mouse Back");
        else if (action->code == SDL_BUTTON_X2) return UI_GetIconTexture("Mouse Forward");
        break;
    case SDL_REMAPPER_ACTION_MOUSE_WHEEL:
        if (action->value > 0) return UI_GetIconTexture("Scroll Up");
        else if (action->value < 0) return UI_GetIconTexture("Scroll Down");
        break;
    case SDL_REMAPPER_ACTION_MOUSE_MOVEMENT:
        return UI_GetIconTexture("Mouse Move");
    case SDL_REMAPPER_ACTION_KEYBOARD_KEY:
        switch ((SDL_Scancode)action->code) {
        case SDL_SCANCODE_W: return UI_GetIconTexture("W");
        case SDL_SCANCODE_A: return UI_GetIconTexture("A");
        case SDL_SCANCODE_S: return UI_GetIconTexture("S");
        case SDL_SCANCODE_D: return UI_GetIconTexture("D");
        case SDL_SCANCODE_UP: return UI_GetIconTexture("Up");
        case SDL_SCANCODE_DOWN: return UI_GetIconTexture("Down");
        case SDL_SCANCODE_LEFT: return UI_GetIconTexture("Left");
        case SDL_SCANCODE_RIGHT: return UI_GetIconTexture("Right");
        case SDL_SCANCODE_SPACE: return UI_GetIconTexture("Space");
        case SDL_SCANCODE_RETURN: return UI_GetIconTexture("Return");
        case SDL_SCANCODE_ESCAPE: return UI_GetIconTexture("Escape");
        case SDL_SCANCODE_LSHIFT: return UI_GetIconTexture("Left Shift");
        case SDL_SCANCODE_LCTRL: return UI_GetIconTexture("Left Ctrl");
        default: break;
        }
        break;
    /* Touch actions - use touch-specific icons when available */
    case SDL_REMAPPER_ACTION_TOUCH_TAP:
        return UI_GetIconTexture("Touch Tap");
    case SDL_REMAPPER_ACTION_TOUCH_HOLD:
        return UI_GetIconTexture("Touch Hold");
    case SDL_REMAPPER_ACTION_TOUCH_DOUBLE_TAP:
        return UI_GetIconTexture("Touch Double Tap");
    case SDL_REMAPPER_ACTION_TOUCH_SWIPE_UP:
        return UI_GetIconTexture("Touch Swipe Up");
    case SDL_REMAPPER_ACTION_TOUCH_SWIPE_DOWN:
        return UI_GetIconTexture("Touch Swipe Down");
    case SDL_REMAPPER_ACTION_TOUCH_SWIPE_LEFT:
        return UI_GetIconTexture("Touch Swipe Left");
    case SDL_REMAPPER_ACTION_TOUCH_SWIPE_RIGHT:
        return UI_GetIconTexture("Touch Swipe Right");
    case SDL_REMAPPER_ACTION_TOUCH_FINGER2_TAP:
        return UI_GetIconTexture("Touch Finger2 Tap");
    case SDL_REMAPPER_ACTION_TOUCH_FINGER2_HOLD:
        return UI_GetIconTexture("Touch Finger2 Hold");
    case SDL_REMAPPER_ACTION_TOUCH_PINCH_IN:
        return UI_GetIconTexture("Touch Pinch In");
    case SDL_REMAPPER_ACTION_TOUCH_PINCH_OUT:
        return UI_GetIconTexture("Touch Pinch Out");
    case SDL_REMAPPER_ACTION_TOUCH_ROTATE_CW:
        return UI_GetIconTexture("Touch Rotate CW");
    case SDL_REMAPPER_ACTION_TOUCH_ROTATE_CCW:
        return UI_GetIconTexture("Touch Rotate CCW");
    default:
        break;
    }

    return NULL;
}

/* Scale from reference resolution to actual window size */
static inline float ScaleX(float ref_x, int window_w) {
    return (ref_x / REFERENCE_WIDTH) * window_w;
}

static inline float ScaleY(float ref_y, int window_h) {
    return (ref_y / REFERENCE_HEIGHT) * window_h;
}

/* Simplified text rendering using SDL_RenderDebugText (ASCII, scaled) */
static void DrawText(SDL_Renderer *r, const char *text, float x, float y, float size,
                    Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    if (!r || !text || !*text) {
        return;
    }

    /* Base debug font character size is 8x8 pixels */
    const float base_char = (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    float scale = size > 0.0f ? (size / base_char) : 1.0f;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    int len = (int)SDL_strlen(text);

    /* Save current scale and set temporary scale for text */
    float old_sx = 1.0f, old_sy = 1.0f;
    SDL_GetRenderScale(r, &old_sx, &old_sy);
    SDL_SetRenderScale(r, scale, scale);

    /* Center text around (x, y) similar to the old stub */
    float x_scaled = (x / scale) - (len * base_char * 0.5f) / 1.0f;
    float y_scaled = (y / scale) - (base_char * 0.5f) / 1.0f;

    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    if (!SDL_RenderDebugText(r, x_scaled, y_scaled, text)) {
        SDL_Log("SDL_RenderDebugText failed: %s", SDL_GetError());
    }

    /* Restore previous scale */
    SDL_SetRenderScale(r, old_sx, old_sy);
}

/* Left-aligned variant for labels (checkboxes, sliders, etc.) */
static void DrawTextLeft(SDL_Renderer *r, const char *text, float x, float y, float size,
                         Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    if (!r || !text || !*text) {
        return;
    }

    const float base_char = (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    float scale = size > 0.0f ? (size / base_char) : 1.0f;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    /* Save current scale and set temporary scale for text */
    float old_sx = 1.0f, old_sy = 1.0f;
    SDL_GetRenderScale(r, &old_sx, &old_sy);
    SDL_SetRenderScale(r, scale, scale);

    /* Left-align horizontally at x, center vertically around y */
    float x_scaled = x / scale;
    float y_scaled = (y / scale) - (base_char * 0.5f);

    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    if (!SDL_RenderDebugText(r, x_scaled, y_scaled, text)) {
        SDL_Log("SDL_RenderDebugText failed: %s", SDL_GetError());
    }

    SDL_SetRenderScale(r, old_sx, old_sy);
}

/* Rounded rectangle with optional corner radius */
static void DrawRoundedRectEx(SDL_Renderer *r, float x, float y, float w, float h,
                              Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha, bool filled, float radius)
{
    SDL_SetRenderDrawColor(r, red, green, blue, alpha);

    if (radius <= 0 || radius > w / 2 || radius > h / 2) {
        /* No radius or invalid - draw regular rectangle */
        SDL_FRect rect = { x, y, w, h };
        if (filled) {
            SDL_RenderFillRect(r, &rect);
        } else {
            SDL_RenderRect(r, &rect);
        }
        return;
    }

    /* Draw rounded rectangle using 3 rectangles + 4 corner circles approximation */
    if (filled) {
        /* Center rectangle (full width, reduced height) */
        SDL_FRect center = { x, y + radius, w, h - 2 * radius };
        SDL_RenderFillRect(r, &center);

        /* Top rectangle (reduced width) */
        SDL_FRect top = { x + radius, y, w - 2 * radius, radius };
        SDL_RenderFillRect(r, &top);

        /* Bottom rectangle (reduced width) */
        SDL_FRect bottom = { x + radius, y + h - radius, w - 2 * radius, radius };
        SDL_RenderFillRect(r, &bottom);

        /* Draw corner circles using small filled rectangles as approximation */
        int steps = (int)(radius * 2);
        if (steps < 4) steps = 4;
        for (int i = 0; i <= steps; i++) {
            float angle = (float)i / (float)steps * 1.5708f; /* 0 to PI/2 */
            float cos_a = SDL_cosf(angle);
            float sin_a = SDL_sinf(angle);
            float cx, cy, px, py;

            /* Top-left corner */
            cx = x + radius;
            cy = y + radius;
            px = cx - cos_a * radius;
            py = cy - sin_a * radius;
            SDL_FRect tl = { px, py, cos_a * radius, 1 };
            SDL_RenderFillRect(r, &tl);

            /* Top-right corner */
            cx = x + w - radius;
            cy = y + radius;
            px = cx;
            py = cy - sin_a * radius;
            SDL_FRect tr = { px, py, cos_a * radius, 1 };
            SDL_RenderFillRect(r, &tr);

            /* Bottom-left corner */
            cx = x + radius;
            cy = y + h - radius;
            px = cx - cos_a * radius;
            py = cy + sin_a * radius;
            SDL_FRect bl = { px, py, cos_a * radius, 1 };
            SDL_RenderFillRect(r, &bl);

            /* Bottom-right corner */
            cx = x + w - radius;
            cy = y + h - radius;
            px = cx;
            py = cy + sin_a * radius;
            SDL_FRect br = { px, py, cos_a * radius, 1 };
            SDL_RenderFillRect(r, &br);
        }
    } else {
        /* For non-filled, just draw the outline as regular rect for now */
        SDL_FRect rect = { x, y, w, h };
        SDL_RenderRect(r, &rect);
    }
}

/* Rounded rectangle (no corner radius - backward compatible) */
static void DrawRoundedRect(SDL_Renderer *r, float x, float y, float w, float h,
                           Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha, bool filled)
{
    DrawRoundedRectEx(r, x, y, w, h, red, green, blue, alpha, filled, 0);
}

/* Calculate button width based on text length with padding */
static float CalcButtonWidth(const char *text, float height, float padding)
{
    if (!text || !*text) return padding * 2;
    int len = (int)SDL_strlen(text);
    float text_size = height * 0.4f;
    float scale = text_size / (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    float text_width = len * (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;
    return text_width + padding * 2;
}

/* Button with hover/pressed states and corner radius */
static void DrawButton(SDL_Renderer *r, const char *text, float x, float y, float w, float h,
                      bool hover, bool pressed)
{
    Uint8 bg = pressed ? 43 : (hover ? 43 : 48);
    DrawRoundedRectEx(r, x, y, w, h, bg, bg, bg, 255, true, 4.0f);
    DrawText(r, text, x + w/2, y + h/2, h * 0.4f, 255, 255, 255, 255);
}

/* Button that auto-sizes to fit text with padding */
static float DrawButtonAuto(SDL_Renderer *r, const char *text, float x, float y, float h,
                           float padding, bool hover, bool pressed)
{
    float w = CalcButtonWidth(text, h, padding);
    DrawButton(r, text, x, y, w, h, hover, pressed);
    return w;
}

/* Device Selection Page (landing screen) */
static void DrawDeviceSelectPage(SDL_Renderer *r, UIState *state, int w, int h)
{
    /* Title in upper-left - left-aligned with margin from screen edge */
    DrawTextLeft(r, "Accessories", ScaleX(40.0f, w), ScaleY(120.0f, h), ScaleY(60.0f, h), 255, 255, 255, 255);

    if (!state || state->device_count <= 0) {
        DrawText(r, "No devices detected", w / 2.0f, h / 2.0f, ScaleY(48.0f, h), 200, 200, 200, 255);
        return;
    }

    int idx = state->selected_device;
    if (idx < 0) {
        idx = 0;
    } else if (idx >= state->device_count) {
        idx = state->device_count - 1;
    }

    UIDeviceType type = state->device_types[idx];

    /* Centered square area for device visual - no visible border */
    float card_w = ScaleX(1100.0f, w);
    float card_h = card_w;  /* Square shape */
    float card_x = (float)w * 0.5f - card_w * 0.5f;
    float card_y = (float)h * 0.5f - card_h * 0.5f; /* centered vertically */

    /* No card background - matches page background (33, 33, 33) */

    /* Select device-specific image */
    SDL_Texture *device_bg = NULL;
    if (type == UI_DEVICE_TYPE_GAMEPAD) {
        device_bg = g_controller_device_bg;
    } else if (type == UI_DEVICE_TYPE_MOUSE) {
        device_bg = g_mouse_device_bg;
    } else if (type == UI_DEVICE_TYPE_KEYBOARD) {
        device_bg = g_keyboard_device_bg;
    }

    /* Device image with fixed height, auto width (maintain aspect ratio) */
    if (device_bg) {
        float tex_w, tex_h;
        SDL_GetTextureSize(device_bg, &tex_w, &tex_h);

        /* Scale based on height only - width adjusts to maintain aspect ratio */
        float img_h = card_h;
        float img_w = (tex_h > 0) ? (tex_w / tex_h) * img_h : card_w;
        float img_x = (float)w * 0.5f - img_w * 0.5f;  /* Center horizontally */
        float img_y = card_y;

        SDL_FRect dest = { img_x, img_y, img_w, img_h };
        SDL_RenderTexture(r, device_bg, NULL, &dest);
    } else {
        const char *center_text = "Device";
        if (type == UI_DEVICE_TYPE_KEYBOARD) {
            center_text = "Keyboard";
        } else if (type == UI_DEVICE_TYPE_MOUSE) {
            center_text = "Mouse";
        } else if (type == UI_DEVICE_TYPE_JOYSTICK) {
            center_text = "Joystick";
        }

        DrawText(r, center_text,
                 w / 2.0f,
                 card_y + card_h * 0.5f,
                 ScaleY(64.0f, h), 255, 255, 255, 255);
    }

    /* Configure button centered below the card - same height as other buttons */
    float btn_h = 50.0f;
    float btn_padding = 20.0f;
    float btn_w = CalcButtonWidth("CONFIGURE", btn_h, btn_padding);
    float btn_x = (float)w * 0.5f - btn_w * 0.5f;
    float btn_y = card_y + card_h + ScaleY(80.0f, h);  /* Moved down slightly */
    bool config_focused = (state && !state->device_back_focused);
    DrawButton(r, "CONFIGURE", btn_x, btn_y, btn_w, btn_h, false, config_focused);

    /* Simple left/right selectors to hint at multiple devices */
    if (state->device_count > 1) {
        float arrow_size = ScaleY(80.0f, h);
        float left_x = ScaleX(180.0f, w);
        float right_x = (float)w - ScaleX(180.0f, w);
        float arrow_y = (float)h * 0.5f;

        DrawText(r, "<", left_x, arrow_y, arrow_size, 200, 200, 200, 255);
        DrawText(r, ">", right_x, arrow_y, arrow_size, 200, 200, 200, 255);
    }

    /* Back button - same position as other pages (bottom right) */
    float back_w = CalcButtonWidth("Back", btn_h, btn_padding);
    float back_x = (float)w - back_w - 20.0f;
    float back_y = (float)h - 80.0f;
    bool back_focused = (state && state->device_back_focused);
    DrawButton(r, "Back", back_x, back_y, back_w, btn_h, false, back_focused);
}

/* Profile Selection Page */
static void DrawProfileSelectPage(SDL_Renderer *r, UIState *state, int w, int h)
{
    UIDeviceType device_type = UI_DEVICE_TYPE_GAMEPAD;

    if (state && state->device_count > 0) {
        int idx = state->selected_device;
        if (idx < 0) {
            idx = 0;
        } else if (idx >= state->device_count) {
            idx = state->device_count - 1;
        }
        device_type = state->device_types[idx];
    }

    /* Current preview focus index for controller/mouse/keyboard overlays */
    int preview_index = (state && state->profile_preview_index >= 0) ? state->profile_preview_index : -1;

    /* Left panel - profile list */
    float panel_left = 20.0f;
    float panel_top = 40.0f;  /* Top border aligned near profile text */
    float panel_width = 300.0f;
    float panel_height = (float)h - 60.0f;  /* Taller list */
    DrawRoundedRect(r, panel_left, panel_top, panel_width, panel_height, 33, 33, 33, 255, true);

    DrawTextLeft(r, "Profiles", panel_left + 20.0f, panel_top + 30.0f, 24, 255, 255, 255, 255);

    /* New Profile button */
    bool new_profile_sel = (state && state->profile_focus_on_new_button);
    float new_btn_y = panel_top + 60.0f;
    DrawButton(r, "+ New Profile", panel_left + 20.0f, new_btn_y, 260, 50, false, new_profile_sel);

    /* Profile list with scrolling */
    {
        const float item_height = 60.0f;
        const float list_top = new_btn_y + 70.0f;  /* Start below New Profile button */
        const float list_bottom = panel_top + panel_height - 20.0f;  /* Leave padding at bottom */
        const int visible_rows = (int)((list_bottom - list_top) / item_height);

        /* Calculate scroll bounds */
        int max_scroll = 0;
        if (state->profile_count > visible_rows) {
            max_scroll = state->profile_count - visible_rows;
        }

        /* Clamp scroll position */
        if (state->profile_list_scroll < 0) {
            state->profile_list_scroll = 0;
        } else if (state->profile_list_scroll > max_scroll) {
            state->profile_list_scroll = max_scroll;
        }

        /* Keep selected profile in view */
        if (state->selected_profile < state->profile_list_scroll) {
            state->profile_list_scroll = state->selected_profile;
        } else if (state->selected_profile >= state->profile_list_scroll + visible_rows) {
            state->profile_list_scroll = state->selected_profile - visible_rows + 1;
        }

        /* Draw only visible profiles */
        int row = 0;
        for (int i = state->profile_list_scroll; i < state->profile_count && row < visible_rows; i++, row++) {
            bool selected = (i == state->selected_profile);
            float y = list_top + (float)row * item_height;

            /* Only draw if within visible bounds */
            if (y + 50.0f > list_bottom) {
                break;
            }

            if (selected) {
                DrawRoundedRect(r, panel_left + 20.0f, y, 260, 50, 43, 43, 43, 255, true);
            }

            DrawText(r, state->profile_names[i], panel_left + 150.0f, y + 25.0f, 18, 255, 255, 255, 255);
        }

        /* Draw scroll indicator if there are more items */
        if (state->profile_count > visible_rows) {
            /* Simple scroll indicator - up arrow if can scroll up */
            if (state->profile_list_scroll > 0) {
                DrawText(r, "^", panel_left + 150.0f, list_top - 15.0f, 16, 150, 150, 150, 255);
            }
            /* Down arrow if can scroll down */
            if (state->profile_list_scroll < max_scroll) {
                DrawText(r, "v", panel_left + 150.0f, list_bottom + 5.0f, 16, 150, 150, 150, 255);
            }
        }
    }

    /* Center panel - profile details */
    float center_x = 360;
    float center_w = w - 400;

    /* Action buttons - auto-sized with padding */
    float btn_y = 90;
    float btn_h = 50;
    float btn_padding = 20;  /* Padding on each side of text */
    float btn_gap = 15;      /* Gap between buttons */
    float start_x = center_x + 20;
    int action_focus = state ? state->profile_action_focus : -1;

    /* Profile name - aligned with start of Edit button */
    DrawTextLeft(r, state->profile_names[state->selected_profile],
                 start_x, 50, 32, 255, 255, 255, 255);

    /* Draw auto-sized buttons in a row */
    float cur_x = start_x;
    cur_x += DrawButtonAuto(r, "Edit", cur_x, btn_y, btn_h, btn_padding, false, action_focus == 0);
    cur_x += btn_gap;
    cur_x += DrawButtonAuto(r, "Duplicate", cur_x, btn_y, btn_h, btn_padding, false, action_focus == 1);
    cur_x += btn_gap;
    cur_x += DrawButtonAuto(r, "Delete", cur_x, btn_y, btn_h, btn_padding, false, action_focus == 2);
    cur_x += btn_gap;
    DrawButtonAuto(r, "Rename", cur_x, btn_y, btn_h, btn_padding, false, action_focus == 3);

    /* Back button - auto-sized */
    float back_w = CalcButtonWidth("Back", 50, btn_padding);
    DrawButton(r, "Back", w - back_w - 20, h - 80, back_w, 50, false, action_focus == 4);

    /* Controller / mouse / keyboard overlays on the profile page */
    {
        int w_window = w;
        int h_window = h;

        if (device_type == UI_DEVICE_TYPE_MOUSE) {
            /* Labels: Button -> Click */
            const char *mouse_labels[UI_MOUSE_SLOT_COUNT] = {
                "Left Click",
                "Right Click",
                "Middle Click",
                "Back",
                "Forward",
                "Wheel Up",
                "Wheel Down",
                "Mouse Move"
            };

            int p = state->selected_profile;
            if (p < 0) {
                p = 0;
            } else if (p >= SDL_UI_MAX_PROFILES) {
                p = SDL_UI_MAX_PROFILES - 1;
            }

            float tile_w = ScaleX(130, w_window);
            float tile_h = ScaleY(130, h_window);
            float gap_y = ScaleY(40, h_window);

            /* Calculate FULL box dimensions (used for button positioning) */
            float btn_size_ref = 130.0f;
            float x_offset = 300.0f;

            float left_btn_x = 1035.0f;
            float right_btn_x = 2775.0f;
            float top_btn_y = 417.0f;
            float bottom_btn_y = 1584.0f;

            float inner_left = left_btn_x + btn_size_ref;
            float inner_right = right_btn_x;
            float inner_top = top_btn_y + 40.0f + btn_size_ref / 2.0f;

            float overlap = btn_size_ref * 0.20f;

            float bg_left_ref = inner_left + x_offset - overlap;
            float bg_right_ref = inner_right + x_offset + overlap;

            float old_bg_top = inner_top + 250.0f - overlap;
            float old_bg_bottom = bottom_btn_y + 40.0f + 250.0f + btn_size_ref / 2.0f;
            float box_h_ref = old_bg_bottom - old_bg_top;

            /* Full box dimensions for button positioning */
            float full_bg_left = ScaleX(bg_left_ref, w_window);
            float full_bg_right = ScaleX(bg_right_ref, w_window);
            float full_bg_w = full_bg_right - full_bg_left;
            float full_bg_h = ScaleY(box_h_ref, h_window);

            float edit_bottom_screen = 140.0f;
            float back_top_screen = (float)h_window - 80.0f;
            float region_center_screen = (edit_bottom_screen + back_top_screen) / 2.0f;

            float full_bg_top = region_center_screen - full_bg_h / 2.0f;
            float full_center_x = full_bg_left + full_bg_w * 0.5f;
            float full_center_y = full_bg_top + full_bg_h * 0.5f;

            /* Draw SMALLER box (80% of full size) centered at same position */
            float box_scale = 0.80f;
            float bg_w = full_bg_w * box_scale;
            float bg_h = full_bg_h * box_scale;
            float bg_left = full_center_x - bg_w * 0.5f;
            float bg_top = full_center_y - bg_h * 0.5f;

            SDL_SetRenderDrawColor(r, 33, 33, 33, 255);
            SDL_FRect bg_rect = { bg_left, bg_top, bg_w, bg_h };
            SDL_RenderFillRect(r, &bg_rect);

            if (g_mouse_profile_bg) {
                SDL_FRect img_dest = { bg_left, bg_top, bg_w, bg_h };
                SDL_RenderTexture(r, g_mouse_profile_bg, NULL, &img_dest);
            }

            /* 2x4 grid layout - buttons positioned relative to FULL box size horizontally */
            float left_col_x = full_bg_left + full_bg_w * 0.20f - tile_w * 0.5f;
            float right_col_x = full_bg_left + full_bg_w * 0.80f - tile_w * 0.5f;

            /* Layout order mapping:
             * Left column (top to bottom): Left Click, Mouse Move, Forward, Back
             * Right column (top to bottom): Right Click, Wheel Up, Middle Click, Wheel Down */
            const int layout_order[8] = {
                UI_MOUSE_SLOT_LEFT,       /* Left col, row 0 */
                UI_MOUSE_SLOT_RIGHT,      /* Right col, row 0 */
                UI_MOUSE_SLOT_MOVE,       /* Left col, row 1 */
                UI_MOUSE_SLOT_WHEEL_UP,   /* Right col, row 1 */
                UI_MOUSE_SLOT_X2,         /* Left col, row 2 (Forward) */
                UI_MOUSE_SLOT_MIDDLE,     /* Right col, row 2 */
                UI_MOUSE_SLOT_X1,         /* Left col, row 3 (Back) */
                UI_MOUSE_SLOT_WHEEL_DOWN  /* Right col, row 3 */
            };

            /* Buttons start below top of image box with more padding */
            int rows = 4;
            float start_y = bg_top + ScaleY(40.0f, h_window);  /* More padding below image top */

            for (int idx = 0; idx < 8; ++idx) {
                int slot = layout_order[idx];
                int col = idx % 2;  /* 0 = left, 1 = right */
                int row = idx / 2;
                float x = (col == 0) ? left_col_x : right_col_x;
                float y = start_y + (float)row * (tile_h + gap_y);
                bool selected = false;
                if (state) {
                    /* On profile page, only use preview_index for highlighting */
                    selected = (preview_index == idx);
                }
                Uint8 bgc = selected ? 60 : 48;

                DrawRoundedRect(r, x, y, tile_w, tile_h, bgc, bgc, bgc, 255, true);

                SDL_RemapperMapping *m = UI_GetMouseSlotMappingInProfile((UI_MouseSlot)slot, p);
                SDL_Texture *icon = UI_GetActionIconTexture(m ? &m->primary_action : NULL);

                if (!icon) {
                    switch (slot) {
                    case UI_MOUSE_SLOT_LEFT:
                        icon = UI_GetIconTexture("Left Click");
                        break;
                    case UI_MOUSE_SLOT_RIGHT:
                        icon = UI_GetIconTexture("Right Click");
                        break;
                    case UI_MOUSE_SLOT_MIDDLE:
                        icon = UI_GetIconTexture("Middle Click");
                        break;
                    case UI_MOUSE_SLOT_WHEEL_UP:
                        icon = UI_GetIconTexture("Scroll Up");
                        break;
                    case UI_MOUSE_SLOT_WHEEL_DOWN:
                        icon = UI_GetIconTexture("Scroll Down");
                        break;
                    case UI_MOUSE_SLOT_X1:
                        icon = UI_GetIconTexture("Mouse Back");
                        break;
                    case UI_MOUSE_SLOT_X2:
                        icon = UI_GetIconTexture("Mouse Forward");
                        break;
                    case UI_MOUSE_SLOT_MOVE:
                        icon = UI_GetIconTexture("Mouse Move");
                        break;
                    default:
                        break;
                    }
                }

                if (icon) {
                    float pad = tile_w * 0.15f;
                    SDL_FRect dst = { x + pad, y + pad, tile_w - 2.0f * pad, tile_h - 2.0f * pad };
                    SDL_RenderTexture(r, icon, NULL, &dst);
                }
            }
        } else if (device_type == UI_DEVICE_TYPE_KEYBOARD) {
            /* Full keyboard layout with 110x110 tiles */
            int p = state->selected_profile;
            if (p < 0) {
                p = 0;
            } else if (p >= SDL_UI_MAX_PROFILES) {
                p = SDL_UI_MAX_PROFILES - 1;
            }

            float key_unit = ScaleX(110.0f, w_window);  /* 110x110 for profile page */
            float gap = ScaleX(8.0f, w_window);
            float min_x;
            float max_x;
            float min_y;
            float max_y;
            UI_ComputeKeyboardLayoutBounds(key_unit, gap, &min_x, &max_x, &min_y, &max_y);

            float layout_center_x = (min_x + max_x) * 0.5f;
            float layout_center_y = (min_y + max_y) * 0.5f;

            float btn_bottom = btn_y + btn_h;
            float back_top = (float)h_window - 80.0f;

            float panel_right = 20.0f + 300.0f;
            float region_center_x = (panel_right + (float)w_window) * 0.5f;
            float target_center_y = (btn_bottom + back_top) * 0.5f;

            float kbd_start_x = region_center_x - layout_center_x;
            float kbd_start_y = target_center_y - layout_center_y;

            /* Map profile_preview_index to scancode using uk_qwerty_layout */
            SDL_Scancode preview_scancode = SDL_SCANCODE_UNKNOWN;
            if (preview_index >= 0 && preview_index < uk_qwerty_layout_count) {
                preview_scancode = uk_qwerty_layout[preview_index].scancode;
            }

            for (int i = 0; i < uk_qwerty_layout_count; ++i) {
                const KeyPosition *kp = &uk_qwerty_layout[i];

                float x = kbd_start_x + kp->col * (key_unit + gap);
                float y = kbd_start_y + kp->row * (key_unit + gap);
                float w = kp->width * key_unit + (kp->width - 1.0f) * gap;
                float h = key_unit;

                /* Handle multi-row keys (like numpad Enter) */
                if (kp->scancode == SDL_SCANCODE_KP_ENTER && i + 1 < uk_qwerty_layout_count) {
                    h = key_unit * 2.0f + gap;
                } else if (kp->scancode == SDL_SCANCODE_KP_PLUS && i - 1 >= 0) {
                    /* KP_PLUS should also span 2 rows but positioned at row 0 */
                    h = key_unit * 2.0f + gap;
                }

                bool selected = false;
                if (state) {
                    if (state->selected_keyboard_slot == (int)kp->scancode) {
                        selected = true;
                    } else if (preview_scancode != SDL_SCANCODE_UNKNOWN && kp->scancode == preview_scancode) {
                        selected = true;
                    }
                }
                Uint8 bg = selected ? 60 : 48;
                DrawRoundedRect(r, x, y, w, h, bg, bg, bg, 255, true);

                SDL_RemapperMapping *m = UI_GetKeyboardSlotMappingInProfile(kp->scancode, p);
                SDL_Texture *icon = UI_GetActionIconTexture(m ? &m->primary_action : NULL);

                if (icon) {
                    float pad = w * 0.1f;
                    SDL_FRect dst = { x + pad, y + pad, w - 2.0f * pad, h - 2.0f * pad };
                    SDL_RenderTexture(r, icon, NULL, &dst);
                }

                /* Draw key label */
                float font_size = ScaleY(18.0f, h_window);
                if (w > key_unit * 1.5f) {
                    font_size = ScaleY(14.0f, h_window);  /* Smaller font for wide keys */
                }
                DrawText(r, kp->label, x + w * 0.5f, y + h * 0.5f, font_size, 200, 200, 200, 255);
            }
        } else {
            /* Draw background shape in the hollow center area between buttons */
            {
                /* Buttons form a ring/frame around a hollow center.
                 * The background fills the center and slightly overlaps the INNER edges of buttons. */
                float btn_size = 130.0f;  /* Button width/height in reference space */
                float x_offset = 300.0f;
                float y_offset = 250.0f;

                /* Button positions define the outer ring */
                float left_btn_x = 1035.0f;   /* Leftmost buttons X position */
                float right_btn_x = 2775.0f;  /* Rightmost buttons X position */
                float top_btn_y = 417.0f;     /* Top buttons Y (center - 40) */
                float bottom_btn_y = 1584.0f; /* Bottom buttons Y (center - 40) */

                /* Inner edges of the button ring (the hollow center boundaries) */
                float inner_left = left_btn_x + btn_size;     /* Right edge of left buttons */
                float inner_right = right_btn_x;              /* Left edge of right buttons */
                float inner_top = top_btn_y + 40.0f + btn_size / 2.0f;    /* Bottom edge of top buttons */

                /* How much the background extends INTO the buttons (overlapping inner edges) */
                float overlap = btn_size * 0.20f;

                /* Background extends from inner edges, going INTO buttons by overlap amount */
                /* Bottom edge aligns with outer edge of bottom buttons */
                float bg_left_ref = inner_left + x_offset - overlap;
                float bg_right_ref = inner_right + x_offset + overlap;
                float bg_top_ref = inner_top + y_offset - overlap;
                float bg_bottom_ref = bottom_btn_y + 40.0f + y_offset + btn_size / 2.0f;  /* Outer bottom edge */

                /* Scale to window coordinates */
                float bg_left = ScaleX(bg_left_ref, w_window);
                float bg_right = ScaleX(bg_right_ref, w_window);
                float bg_top = ScaleY(bg_top_ref, h_window);
                float bg_bottom = ScaleY(bg_bottom_ref, h_window);

                float bg_w = bg_right - bg_left;
                float bg_h = bg_bottom - bg_top;

                /* Draw background matching page color (33,33,33) with no border */
                SDL_SetRenderDrawColor(r, 33, 33, 33, 255);
                SDL_FRect bg_rect = { bg_left, bg_top, bg_w, bg_h };
                SDL_RenderFillRect(r, &bg_rect);

                /* Render controller background image fitted inside the box */
                if (g_controller_profile_bg) {
                    SDL_FRect img_dest = { bg_left, bg_top, bg_w, bg_h };
                    SDL_RenderTexture(r, g_controller_profile_bg, NULL, &img_dest);
                }
            }

            /* Buttons */
            for (int i = 0; i < (int)(sizeof(remapping_buttons) / sizeof(remapping_buttons[0])); i++) {
                const RemappingButton *btn = &remapping_buttons[i];
                float bx = ScaleX(btn->x + 300.0f, w_window);  /* Shift right by 300px */
                float bw = ScaleX(130, w_window);
                float bh = ScaleY(130, h_window);

                /* Keep the same visual center that we had with 80px-high rectangles.
                 * Original top was btn->y with height 80, so center was btn->y + 40.
                 * Shift down by 250px for better vertical centering. */
                float center_y = ScaleY(btn->y + 40.0f + 250.0f, h_window);
                float by = center_y - bh / 2.0f;

                bool selected = false;
                if (state) {
                    /* Highlight if this is the selected button OR the current preview focus */
                    if (btn->button == state->selected_button) {
                        selected = true;
                    } else if (preview_index == i) {
                        selected = true;
                    }
                }
                Uint8 bg = selected ? 60 : 48;

                int p = state->selected_profile;
                if (p < 0) {
                    p = 0;
                } else if (p >= SDL_UI_MAX_PROFILES) {
                    p = SDL_UI_MAX_PROFILES - 1;
                }

                SDL_RemapperMapping *mapping = UI_GetMappingForButtonInProfile(btn->button, p);
                DrawRoundedRect(r, bx, by, bw, bh, bg, bg, bg, 255, true);

                /* Icon for the current mapping (controller/mouse/keyboard),
                 * falling back to the physical controller button icon. */
                SDL_Texture *icon = UI_GetActionIconTexture(mapping ? &mapping->primary_action : NULL);
                if (!icon) {
                    icon = UI_GetIconTexture(btn->tag);
                }
                if (icon) {
                    float pad = bw * 0.15f;
                    SDL_FRect dst = { bx + pad, by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
                    SDL_RenderTexture(r, icon, NULL, &dst);
                }
            }

            /* Triggers */
            {
                float bw = ScaleX(130, w_window);
                float bh = ScaleY(130, h_window);
                float lt_bx = ScaleX(LT_X + 300.0f, w_window);  /* Shift right by 300px */
                float rt_bx = ScaleX(RT_X + 300.0f, w_window);  /* Shift right by 300px */

                /* Preserve original centers (top + 40 in reference space).
                 * Shift down by 250px for better vertical centering. */
                float lt_center_y = ScaleY(LT_Y + 40.0f + 250.0f, h_window);
                float rt_center_y = ScaleY(RT_Y + 40.0f + 250.0f, h_window);
                float lt_by = lt_center_y - bh / 2.0f;
                float rt_by = rt_center_y - bh / 2.0f;

                int p = state->selected_profile;
                if (p < 0) {
                    p = 0;
                } else if (p >= SDL_UI_MAX_PROFILES) {
                    p = SDL_UI_MAX_PROFILES - 1;
                }

                SDL_RemapperMapping *lt_map = UI_GetMappingForAxisInProfile(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, p);
                SDL_RemapperMapping *rt_map = UI_GetMappingForAxisInProfile(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, p);

                {
                    bool sel_lt = false;
                    if (state) {
                        sel_lt = (state->selected_axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) ||
                                 (preview_index == REMAPPING_BUTTON_COUNT);
                    }
                    Uint8 bg_lt = sel_lt ? 60 : 48;
                    DrawRoundedRect(r, lt_bx, lt_by, bw, bh, bg_lt, bg_lt, bg_lt, 255, true);

                    SDL_Texture *lt_icon = UI_GetActionIconTexture(lt_map ? &lt_map->primary_action : NULL);
                    if (!lt_icon) {
                        lt_icon = UI_GetIconTexture("Left Trigger");
                    }
                    if (lt_icon) {
                        float pad = bw * 0.15f;
                        SDL_FRect dst = { lt_bx + pad, lt_by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
                        SDL_RenderTexture(r, lt_icon, NULL, &dst);
                    }
                }

                {
                    bool sel_rt = false;
                    if (state) {
                        sel_rt = (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) ||
                                 (preview_index == REMAPPING_BUTTON_COUNT + 1);
                    }
                    Uint8 bg_rt = sel_rt ? 60 : 48;
                    DrawRoundedRect(r, rt_bx, rt_by, bw, bh, bg_rt, bg_rt, bg_rt, 255, true);

                    SDL_Texture *rt_icon = UI_GetActionIconTexture(rt_map ? &rt_map->primary_action : NULL);
                    if (!rt_icon) {
                        rt_icon = UI_GetIconTexture("Right Trigger");
                    }
                    if (rt_icon) {
                        float pad = bw * 0.15f;
                        SDL_FRect dst = { rt_bx + pad, rt_by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
                        SDL_RenderTexture(r, rt_icon, NULL, &dst);
                    }
                }
            }

            /* Stick movement */
            {
                float bw = ScaleX(130, w_window);
                float bh = ScaleY(130, h_window);
                float ls_bx = ScaleX(LS_MOVE_X + 300.0f, w_window);  /* Shift right by 300px */
                float rs_bx = ScaleX(RS_MOVE_X + 300.0f, w_window);  /* Shift right by 300px */

                /* Preserve centers from the old 200x80 layout (top + 40).
                 * Shift down by 250px for better vertical centering. */
                float ls_center_y = ScaleY(LS_MOVE_Y + 40.0f + 250.0f, h_window);
                float rs_center_y = ScaleY(RS_MOVE_Y + 40.0f + 250.0f, h_window);
                float ls_by = ls_center_y - bh / 2.0f;
                float rs_by = rs_center_y - bh / 2.0f;

                int p = state->selected_profile;
                if (p < 0) {
                    p = 0;
                } else if (p >= SDL_UI_MAX_PROFILES) {
                    p = SDL_UI_MAX_PROFILES - 1;
                }

                SDL_RemapperMapping *ls_map = UI_GetMappingForAxisInProfile(SDL_GAMEPAD_AXIS_LEFTX, p);
                SDL_RemapperMapping *rs_map = UI_GetMappingForAxisInProfile(SDL_GAMEPAD_AXIS_RIGHTX, p);
                SDL_RemapperStickMapping *ls_stick = ls_map ? ls_map->stick_mapping : NULL;
                SDL_RemapperStickMapping *rs_stick = rs_map ? rs_map->stick_mapping : NULL;

                {
                    bool sel_ls = false;
                    if (state) {
                        sel_ls = (state->selected_axis == SDL_GAMEPAD_AXIS_LEFTX || state->selected_axis == SDL_GAMEPAD_AXIS_LEFTY) ||
                                 (preview_index == REMAPPING_BUTTON_COUNT + 2);
                    }
                    Uint8 bg_ls = sel_ls ? 60 : 48;
                    DrawRoundedRect(r, ls_bx, ls_by, bw, bh, bg_ls, bg_ls, bg_ls, 255, true);

                    /* Choose icon based on left stick mapping mode, similar to text summary. */
                    SDL_Texture *ls_icon = NULL;
                    if (ls_stick) {
                        if (ls_stick->map_to_mouse_movement) {
                            ls_icon = UI_GetIconTexture("Mouse Move");
                        } else if (ls_stick->map_to_wasd) {
                            /* Use W key icon as representative for WASD mode. */
                            ls_icon = UI_GetIconTexture("W");
                        } else if (ls_stick->map_to_arrow_keys) {
                            /* Use Up arrow icon as representative for arrow key mode. */
                            ls_icon = UI_GetIconTexture("Up");
                        } else if (ls_stick->map_to_controller_movement) {
                            ls_icon = UI_GetIconTexture("Left Stick Move");
                        } else if (ls_stick->map_to_dpad) {
                            /* Use D-Pad Up icon as representative for D-Pad mode. */
                            ls_icon = UI_GetIconTexture("D-Pad Up");
                        }
                    }
                    if (!ls_icon) {
                        /* Fallback to the generic left stick move icon. */
                        ls_icon = UI_GetIconTexture("Left Stick Move");
                    }
                    if (ls_icon) {
                        float pad = bw * 0.15f;
                        SDL_FRect dst = { ls_bx + pad, ls_by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
                        SDL_RenderTexture(r, ls_icon, NULL, &dst);
                    }
                }

                {
                    bool sel_rs = false;
                    if (state) {
                        sel_rs = (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHTX || state->selected_axis == SDL_GAMEPAD_AXIS_RIGHTY) ||
                                 (preview_index == REMAPPING_BUTTON_COUNT + 3);
                    }
                    Uint8 bg_rs = sel_rs ? 60 : 48;
                    DrawRoundedRect(r, rs_bx, rs_by, bw, bh, bg_rs, bg_rs, bg_rs, 255, true);

                    SDL_Texture *rs_icon = NULL;
                    if (rs_stick) {
                        if (rs_stick->map_to_gyroscope || rs_stick->map_to_touch_mouse || rs_stick->map_to_mouse_movement) {
                            rs_icon = UI_GetIconTexture("Mouse Move");
                        } else if (rs_stick->map_to_wasd) {
                            rs_icon = UI_GetIconTexture("W");
                        } else if (rs_stick->map_to_arrow_keys) {
                            rs_icon = UI_GetIconTexture("Up");
                        } else if (rs_stick->map_to_controller_movement) {
                            rs_icon = UI_GetIconTexture("Right Stick Move");
                        } else if (rs_stick->map_to_dpad) {
                            rs_icon = UI_GetIconTexture("D-Pad Up");
                        }
                    }
                    if (!rs_icon) {
                        rs_icon = UI_GetIconTexture("Right Stick Move");
                    }
                    if (rs_icon) {
                        float pad = bw * 0.15f;
                        SDL_FRect dst = { rs_bx + pad, rs_by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
                        SDL_RenderTexture(r, rs_icon, NULL, &dst);
                    }
                }
            }
        }
    }
}

/* Button Remapping Page */
static void DrawButtonMappingPage(SDL_Renderer *r, UIState *state, int w, int h)
{
    UIDeviceType device_type = UI_DEVICE_TYPE_GAMEPAD;

    if (state && state->device_count > 0) {
        int idx = state->selected_device;
        if (idx < 0) {
            idx = 0;
        } else if (idx >= state->device_count) {
            idx = state->device_count - 1;
        }
        device_type = state->device_types[idx];
    }

    /* Profile name at top left - left-aligned with padding */
    if (state) {
        float profile_name_size = ScaleY(80, h);
        DrawTextLeft(r, state->profile_names[state->selected_profile],
                     ScaleX(40, w), ScaleY(60, h), profile_name_size, 255, 255, 255, 255);

        /* Player indicator below profile name */
        const char *player_names[] = { "Player 1", "Player 2", "Player 3" };
        float player_text_size = ScaleY(50, h);
        DrawTextLeft(r, player_names[state->active_slot],
                     ScaleX(40, w), ScaleY(150, h), player_text_size, 200, 200, 200, 255);
    }

    if (device_type == UI_DEVICE_TYPE_MOUSE) {
        /* Simple Mouse Controls layout instead of controller overlays. */
        const char *mouse_labels[UI_MOUSE_SLOT_COUNT] = {
            "Left Click",
            "Right Click",
            "Middle Click",
            "Back",
            "Forward",
            "Wheel Up",
            "Wheel Down",
            "Mouse Move"
        };

        float tile_w = ScaleX(150.0f, w);
        float tile_h = ScaleY(150.0f, h);
        float gap_y = ScaleY(46.0f, h);  /* Scaled from profile page gap (40/130 * 150) */

        /* Full box dimensions for button positioning (20% larger than profile page) */
        float full_bg_w_ref = 1662.0f * 1.20f;
        float full_bg_h_ref = 1193.0f * 1.20f;

        float page_center_x = (float)w * 0.5f;
        float page_center_y = (float)h * 0.5f;

        float full_bg_w = ScaleX(full_bg_w_ref, w);
        float full_bg_h = ScaleY(full_bg_h_ref, h);
        float full_bg_left = page_center_x - full_bg_w * 0.5f;
        float full_bg_top = page_center_y - full_bg_h * 0.5f;

        /* Draw SMALLER box (80% of full size) centered at same position */
        float box_scale = 0.80f;
        float bg_w = full_bg_w * box_scale;
        float bg_h = full_bg_h * box_scale;
        float bg_left = page_center_x - bg_w * 0.5f;
        float bg_top = page_center_y - bg_h * 0.5f;

        SDL_SetRenderDrawColor(r, 33, 33, 33, 255);
        SDL_FRect bg_rect = { bg_left, bg_top, bg_w, bg_h };
        SDL_RenderFillRect(r, &bg_rect);

        if (g_mouse_remapper_bg) {
            SDL_FRect img_dest = { bg_left, bg_top, bg_w, bg_h };
            SDL_RenderTexture(r, g_mouse_remapper_bg, NULL, &img_dest);
        }

        /* 2x4 grid layout - buttons positioned relative to FULL box size horizontally */
        float left_col_x = full_bg_left + full_bg_w * 0.20f - tile_w * 0.5f;
        float right_col_x = full_bg_left + full_bg_w * 0.80f - tile_w * 0.5f;

        /* Layout order mapping (same as profile page):
         * Left column (top to bottom): Left Click, Mouse Move, Forward, Back
         * Right column (top to bottom): Right Click, Wheel Up, Middle Click, Wheel Down */
        const int layout_order[8] = {
            UI_MOUSE_SLOT_LEFT,       /* Left col, row 0 */
            UI_MOUSE_SLOT_RIGHT,      /* Right col, row 0 */
            UI_MOUSE_SLOT_MOVE,       /* Left col, row 1 */
            UI_MOUSE_SLOT_WHEEL_UP,   /* Right col, row 1 */
            UI_MOUSE_SLOT_X2,         /* Left col, row 2 (Forward) */
            UI_MOUSE_SLOT_MIDDLE,     /* Right col, row 2 */
            UI_MOUSE_SLOT_X1,         /* Left col, row 3 (Back) */
            UI_MOUSE_SLOT_WHEEL_DOWN  /* Right col, row 3 */
        };

        /* Buttons start below top of image box with padding (scaled from profile) */
        float start_y = bg_top + ScaleY(46.0f, h);

        for (int idx = 0; idx < 8; ++idx) {
            int slot = layout_order[idx];
            int col = idx % 2;
            int row = idx / 2;
            float x = (col == 0) ? left_col_x : right_col_x;
            float y = start_y + (float)row * (tile_h + gap_y);
            bool selected = (state && state->mapping_action_focus == -1 && state->selected_mouse_slot == slot);
            Uint8 bgc = selected ? 60 : 48;

            DrawRoundedRect(r, x, y, tile_w, tile_h, bgc, bgc, bgc, 255, true);

            SDL_RemapperMapping *m = UI_GetMouseSlotMapping((UI_MouseSlot)slot);
            SDL_Texture *icon = UI_GetActionIconTexture(m ? &m->primary_action : NULL);

            if (!icon) {
                switch (slot) {
                case UI_MOUSE_SLOT_LEFT:
                    icon = UI_GetIconTexture("Left Click");
                    break;
                case UI_MOUSE_SLOT_RIGHT:
                    icon = UI_GetIconTexture("Right Click");
                    break;
                case UI_MOUSE_SLOT_MIDDLE:
                    icon = UI_GetIconTexture("Middle Click");
                    break;
                case UI_MOUSE_SLOT_WHEEL_UP:
                    icon = UI_GetIconTexture("Scroll Up");
                    break;
                case UI_MOUSE_SLOT_WHEEL_DOWN:
                    icon = UI_GetIconTexture("Scroll Down");
                    break;
                case UI_MOUSE_SLOT_X1:
                    icon = UI_GetIconTexture("Mouse Back");
                    break;
                case UI_MOUSE_SLOT_X2:
                    icon = UI_GetIconTexture("Mouse Forward");
                    break;
                case UI_MOUSE_SLOT_MOVE:
                    icon = UI_GetIconTexture("Mouse Move");
                    break;
                default:
                    break;
                }
            }

            if (icon) {
                float pad = tile_w * 0.15f;
                SDL_FRect dst = { x + pad, y + pad, tile_w - 2.0f * pad, tile_h - 2.0f * pad };
                SDL_RenderTexture(r, icon, NULL, &dst);
            }
        }

        /* Action buttons - same height and position as profile page */
        int mapping_focus = state ? state->mapping_action_focus : -1;
        float btn_h = 50.0f;
        float btn_padding = 20.0f;

        /* Back button - same position as profile page (bottom right) */
        float back_w = CalcButtonWidth("Back", btn_h, btn_padding);
        float back_x = (float)w - back_w - 20.0f;
        float back_y = (float)h - 80.0f;
        DrawButton(r, "Back", back_x, back_y, back_w, btn_h, false, mapping_focus == 1);

        /* Restore to Defaults - centered horizontally, same Y as Back */
        float restore_w = CalcButtonWidth("Restore to Defaults", btn_h, btn_padding);
        float restore_x = (float)w * 0.5f - restore_w * 0.5f;
        DrawButton(r, "Restore to Defaults", restore_x, back_y, restore_w, btn_h, false, mapping_focus == 0);

        return;
    } else if (device_type == UI_DEVICE_TYPE_KEYBOARD) {
        /* Full keyboard layout with 130x130 tiles */
        float key_unit = ScaleX(130.0f, w);  /* 130x130 for button mapping page */
        float gap = ScaleX(8.0f, w);
        float min_x;
        float max_x;
        float min_y;
        float max_y;
        UI_ComputeKeyboardLayoutBounds(key_unit, gap, &min_x, &max_x, &min_y, &max_y);

        float layout_center_x = (min_x + max_x) * 0.5f;
        float layout_center_y = (min_y + max_y) * 0.5f;

        float target_center_x = (float)w * 0.5f;
        float target_center_y = (float)h * 0.5f;

        float start_x = target_center_x - layout_center_x;
        float start_y = target_center_y - layout_center_y;

        for (int i = 0; i < uk_qwerty_layout_count; ++i) {
            const KeyPosition *kp = &uk_qwerty_layout[i];

            float x = start_x + kp->col * (key_unit + gap);
            float y = start_y + kp->row * (key_unit + gap);
            float w_key = kp->width * key_unit + (kp->width - 1.0f) * gap;
            float h_key = key_unit;

            /* Handle multi-row keys (like numpad Enter) */
            if (kp->scancode == SDL_SCANCODE_KP_ENTER && i + 1 < uk_qwerty_layout_count) {
                h_key = key_unit * 2.0f + gap;
            } else if (kp->scancode == SDL_SCANCODE_KP_PLUS && i - 1 >= 0) {
                /* KP_PLUS should also span 2 rows but positioned at row 0 */
                h_key = key_unit * 2.0f + gap;
            }

            bool selected = (state && state->mapping_action_focus == -1 && state->selected_keyboard_slot == (int)kp->scancode);
            Uint8 bg = selected ? 60 : 48;
            DrawRoundedRect(r, x, y, w_key, h_key, bg, bg, bg, 255, true);

            SDL_RemapperMapping *m = UI_GetKeyboardSlotMapping(kp->scancode);
            SDL_Texture *icon = UI_GetActionIconTexture(m ? &m->primary_action : NULL);

            if (icon) {
                float pad = w_key * 0.1f;
                SDL_FRect dst = { x + pad, y + pad, w_key - 2.0f * pad, h_key - 2.0f * pad };
                SDL_RenderTexture(r, icon, NULL, &dst);
            }

            /* Draw key label */
            float font_size = ScaleY(20.0f, h);
            if (w_key > key_unit * 1.5f) {
                font_size = ScaleY(16.0f, h);  /* Smaller font for wide keys */
            }
            DrawText(r, kp->label, x + w_key * 0.5f, y + h_key * 0.5f, font_size, 200, 200, 200, 255);
        }

        /* Action buttons - same height and position as profile page */
        int mapping_focus = state ? state->mapping_action_focus : -1;
        float btn_h = 50.0f;
        float btn_padding = 20.0f;

        /* Back button - same position as profile page (bottom right) */
        float back_w = CalcButtonWidth("Back", btn_h, btn_padding);
        float back_x = (float)w - back_w - 20.0f;
        float back_y = (float)h - 80.0f;
        DrawButton(r, "Back", back_x, back_y, back_w, btn_h, false, mapping_focus == 1);

        /* Restore to Defaults - centered horizontally, same Y as Back */
        float restore_w = CalcButtonWidth("Restore to Defaults", btn_h, btn_padding);
        float restore_x = (float)w * 0.5f - restore_w * 0.5f;
        DrawButton(r, "Restore to Defaults", restore_x, back_y, restore_w, btn_h, false, mapping_focus == 0);

        return;
    }

    /* Centering offset: buttons are 60px right of center, shift left to fix */
    float center_offset = -60.0f;

    /* Draw background box with controller image - adjusted for 150px button size */
    {
        float btn_size = 150.0f;  /* Button size on remapping page */

        /* Button positions with centering offset */
        float left_btn_x = 1035.0f + center_offset;
        float right_btn_x = 2775.0f + center_offset;
        float top_btn_y = 417.0f;
        float bottom_btn_y = 1584.0f;

        /* Inner edges of the button ring */
        float inner_left = left_btn_x + btn_size;
        float inner_right = right_btn_x;
        float inner_top = top_btn_y + 40.0f + btn_size / 2.0f;

        /* How much the background extends INTO the buttons */
        float overlap = btn_size * 0.20f;

        /* Background extends from inner edges, going INTO buttons by overlap amount */
        float bg_left_ref = inner_left - overlap;
        float bg_right_ref = inner_right + overlap;
        float bg_top_ref = inner_top - overlap;
        float bg_bottom_ref = bottom_btn_y + 40.0f + btn_size / 2.0f;  /* Outer bottom edge */

        /* Scale to window coordinates */
        float bg_left = ScaleX(bg_left_ref, w);
        float bg_right = ScaleX(bg_right_ref, w);
        float bg_top = ScaleY(bg_top_ref, h);
        float bg_bottom = ScaleY(bg_bottom_ref, h);

        float bg_w = bg_right - bg_left;
        float bg_h = bg_bottom - bg_top;

        /* Draw background matching page color with no border */
        SDL_SetRenderDrawColor(r, 33, 33, 33, 255);
        SDL_FRect bg_rect = { bg_left, bg_top, bg_w, bg_h };
        SDL_RenderFillRect(r, &bg_rect);

        /* Render controller background image fitted inside the box */
        if (g_controller_remapper_bg) {
            SDL_FRect img_dest = { bg_left, bg_top, bg_w, bg_h };
            SDL_RenderTexture(r, g_controller_remapper_bg, NULL, &img_dest);
        }
    }

    /* Draw all buttons using precomputed reference positions (3840x2160) */
    for (int i = 0; i < (int)(sizeof(remapping_buttons) / sizeof(remapping_buttons[0])); i++) {
        const RemappingButton *btn = &remapping_buttons[i];
        float bx = ScaleX(btn->x + center_offset, w);

        float bw = ScaleX(150, w);   /* Square 150x150 widgets */
        float bh = ScaleY(150, h);

        /* Preserve the old visual centers from the 150x80 layout where
         * the top was btn->y and height was 80, so center was btn->y + 40. */
        float center_y = ScaleY(btn->y + 40.0f, h);
        float by = center_y - bh / 2.0f;

        bool selected = (btn->button == state->selected_button);
        Uint8 bg = selected ? 60 : 48;  /* #303030 */

        SDL_RemapperMapping *mapping = UI_GetMappingForButton(btn->button);

        DrawRoundedRect(r, bx, by, bw, bh, bg, bg, bg, 255, true);

        /* Show icon for the primary mapping (gamepad/mouse/keyboard) or fallback to physical button */
        SDL_Texture *icon = UI_GetActionIconTexture(mapping ? &mapping->primary_action : NULL);
        if (!icon) {
            /* Fallback to physical controller button icon */
            icon = UI_GetIconTexture(btn->tag);
        }
        if (icon) {
            float pad = bw * 0.15f;
            SDL_FRect dst = { bx + pad, by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
            SDL_RenderTexture(r, icon, NULL, &dst);
        }
    }

    /* Triggers (Left / Right) using axis mappings */
    {
        float bw = ScaleX(150, w);
        float bh = ScaleY(150, h);
        float lt_bx = ScaleX(LT_X + center_offset, w);
        float rt_bx = ScaleX(RT_X + center_offset, w);

        float lt_center_y = ScaleY(LT_Y + 40.0f, h);
        float rt_center_y = ScaleY(RT_Y + 40.0f, h);
        float lt_by = lt_center_y - bh / 2.0f;
        float rt_by = rt_center_y - bh / 2.0f;

        SDL_RemapperMapping *lt_map = UI_GetMappingForAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        SDL_RemapperMapping *rt_map = UI_GetMappingForAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

        /* Left Trigger */
        {
            bool sel = (state->selected_axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
            Uint8 bg = sel ? 60 : 48;
            DrawRoundedRect(r, lt_bx, lt_by, bw, bh, bg, bg, bg, 255, true);

            SDL_Texture *lt_icon = UI_GetActionIconTexture(lt_map ? &lt_map->primary_action : NULL);
            if (!lt_icon) {
                lt_icon = UI_GetIconTexture("Left Trigger");
            }
            if (lt_icon) {
                float pad = bw * 0.15f;
                SDL_FRect dst = { lt_bx + pad, lt_by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
                SDL_RenderTexture(r, lt_icon, NULL, &dst);
            }
        }

        /* Right Trigger */
        {
            bool sel = (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            Uint8 bg = sel ? 60 : 48;
            DrawRoundedRect(r, rt_bx, rt_by, bw, bh, bg, bg, bg, 255, true);

            SDL_Texture *rt_icon = UI_GetActionIconTexture(rt_map ? &rt_map->primary_action : NULL);
            if (!rt_icon) {
                rt_icon = UI_GetIconTexture("Right Trigger");
            }
            if (rt_icon) {
                float pad = bw * 0.15f;
                SDL_FRect dst = { rt_bx + pad, rt_by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
                SDL_RenderTexture(r, rt_icon, NULL, &dst);
            }
        }
    }

    /* Stick movement (Left / Right) using stick mappings on axes */
    {
        float bw = ScaleX(150, w);
        float bh = ScaleY(150, h);
        float ls_bx = ScaleX(LS_MOVE_X + center_offset, w);
        float rs_bx = ScaleX(RS_MOVE_X + center_offset, w);

        float ls_center_y = ScaleY(LS_MOVE_Y + 40.0f, h);
        float rs_center_y = ScaleY(RS_MOVE_Y + 40.0f, h);
        float ls_by = ls_center_y - bh / 2.0f;
        float rs_by = rs_center_y - bh / 2.0f;

        SDL_RemapperMapping *ls_map = UI_GetMappingForAxisInProfile(SDL_GAMEPAD_AXIS_LEFTX, state->selected_profile);
        SDL_RemapperMapping *rs_map = UI_GetMappingForAxisInProfile(SDL_GAMEPAD_AXIS_RIGHTX, state->selected_profile);
        SDL_RemapperStickMapping *ls_stick = ls_map ? ls_map->stick_mapping : NULL;
        SDL_RemapperStickMapping *rs_stick = rs_map ? rs_map->stick_mapping : NULL;

        (void)ls_stick;
        (void)rs_stick;

        /* Left Stick Move */
        {
            bool sel = (state->selected_axis == SDL_GAMEPAD_AXIS_LEFTX ||
                        state->selected_axis == SDL_GAMEPAD_AXIS_LEFTY);
            Uint8 bg = sel ? 60 : 48;
            DrawRoundedRect(r, ls_bx, ls_by, bw, bh, bg, bg, bg, 255, true);

            SDL_Texture *ls_icon = UI_GetIconTexture("Left Stick Move");
            if (ls_icon) {
                float pad = bw * 0.15f;
                SDL_FRect dst = { ls_bx + pad, ls_by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
                SDL_RenderTexture(r, ls_icon, NULL, &dst);
            }
        }

        /* Right Stick Move */
        {
            bool sel = (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHTX ||
                        state->selected_axis == SDL_GAMEPAD_AXIS_RIGHTY);
            Uint8 bg = sel ? 60 : 48;
            DrawRoundedRect(r, rs_bx, rs_by, bw, bh, bg, bg, bg, 255, true);

            SDL_Texture *rs_icon = UI_GetIconTexture("Right Stick Move");
            if (rs_icon) {
                float pad = bw * 0.15f;
                SDL_FRect dst = { rs_bx + pad, rs_by + pad, bw - 2.0f * pad, bh - 2.0f * pad };
                SDL_RenderTexture(r, rs_icon, NULL, &dst);
            }
        }
    }

    /* Action buttons - same height and position as profile page */
    int mapping_focus = state ? state->mapping_action_focus : -1;
    float btn_h = 50.0f;
    float btn_padding = 20.0f;

    /* Back button - same position as profile page (bottom right) */
    float back_w = CalcButtonWidth("Back", btn_h, btn_padding);
    float back_x = (float)w - back_w - 20.0f;
    float back_y = (float)h - 80.0f;
    DrawButton(r, "Back", back_x, back_y, back_w, btn_h, false, mapping_focus == 1);

    /* Restore to Defaults - centered horizontally, same Y as Back */
    float restore_w = CalcButtonWidth("Restore to Defaults", btn_h, btn_padding);
    float restore_x = (float)w * 0.5f - restore_w * 0.5f;
    DrawButton(r, "Restore to Defaults", restore_x, back_y, restore_w, btn_h, false, mapping_focus == 0);

}

/* Button Options Dialog */
static void DrawButtonOptionsDialog(SDL_Renderer *r, UIState *state, int w, int h)
{
    SDL_RemapperMapping *mapping = NULL;
    char primary_label[64];
    char hold_label[64];
    char shift_label[64];
    char dialog_title[128];
    const char *source_name = "Button";

    /* Determine source name and get mapping */
    if (state->selected_button != SDL_GAMEPAD_BUTTON_INVALID) {
        mapping = UI_GetMappingForButtonInProfile(state->selected_button,
                                                 state->selected_profile);
        /* Find button name from remapping_buttons */
        for (int i = 0; i < (int)(sizeof(remapping_buttons) / sizeof(remapping_buttons[0])); i++) {
            if (remapping_buttons[i].button == state->selected_button) {
                source_name = remapping_buttons[i].label;
                break;
            }
        }
    } else if (state->selected_keyboard_slot >= 0 &&
               state->selected_keyboard_slot < UI_KEYBOARD_SLOT_COUNT) {
        mapping = UI_GetKeyboardSlotMappingInProfile(
            (UI_KeyboardSlot)state->selected_keyboard_slot,
            state->selected_profile);
        /* Find key label from uk_qwerty_layout */
        for (int i = 0; i < uk_qwerty_layout_count; i++) {
            if ((int)uk_qwerty_layout[i].scancode == state->selected_keyboard_slot) {
                source_name = uk_qwerty_layout[i].label;
                break;
            }
        }
    } else if (state->selected_mouse_slot >= 0 &&
               state->selected_mouse_slot < UI_MOUSE_SLOT_COUNT) {
        mapping = UI_GetMouseSlotMappingInProfile(
            (UI_MouseSlot)state->selected_mouse_slot,
            state->selected_profile);
        /* Mouse slot names */
        const char *mouse_names[] = {
            "Left Click", "Right Click", "Middle Click",
            "Back", "Forward", "Wheel Up", "Wheel Down", "Mouse Move"
        };
        if (state->selected_mouse_slot < UI_MOUSE_SLOT_COUNT) {
            source_name = mouse_names[state->selected_mouse_slot];
        }
    }

    SDL_snprintf(dialog_title, sizeof(dialog_title), "%s", source_name);

    UI_FormatActionText(mapping ? &mapping->primary_action : NULL,
                        primary_label, sizeof(primary_label));
    UI_FormatActionText(mapping ? &mapping->hold_action : NULL,
                        hold_label, sizeof(hold_label));
    UI_FormatActionText(mapping ? &mapping->shift_action : NULL,
                        shift_label, sizeof(shift_label));

    /* Overlay */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect overlay = { 0, 0, (float)w, (float)h };
    SDL_RenderFillRect(r, &overlay);

    float dw = 400, dh = 400;
    float dx = (w - dw) / 2;
    float dy = (h - dh) / 2;

    DrawRoundedRectEx(r, dx, dy, dw, dh, 33, 33, 33, 255, true, 4.0f);
    DrawText(r, dialog_title, dx + dw/2, dy + 40, 24, 255, 255, 255, 255);

    /* Primary / Hold / Shift buttons - highlight based on dialog_focus_index */
    int focus = state ? state->dialog_focus_index : -1;

    bool primary_sel = (focus == 0);
    DrawButton(r, "Primary", dx + 30, dy + 90, 140, 50, false, primary_sel);
    DrawText(r, primary_label, dx + 250, dy + 115, 16, 180, 180, 180, 255);

    /* Hold button */
    bool hold_sel = (focus == 1);
    DrawButton(r, "Hold", dx + 30, dy + 160, 140, 50, false, hold_sel);
    DrawText(r, hold_label, dx + 250, dy + 185, 16, 180, 180, 180, 255);

    /* Shift button */
    bool shift_sel = (focus == 2);
    DrawButton(r, "Shift", dx + 30, dy + 230, 140, 50, false, shift_sel);
    DrawText(r, shift_label, dx + 250, dy + 255, 16, 180, 180, 180, 255);

    /* Use as Shift checkbox */
    SDL_FRect cb = { dx + 30, dy + 300, 16, 16 };
    Uint8 cb_r = 100, cb_g = 100, cb_b = 100;
    if (focus == 3) {
        cb_r = cb_g = cb_b = 160;  /* Highlighted border when focused */
    }
    SDL_SetRenderDrawColor(r, cb_r, cb_g, cb_b, 255);
    SDL_RenderRect(r, &cb);
    if (mapping && mapping->use_as_shift) {
        SDL_FRect cb_fill = { cb.x + 3.0f, cb.y + 3.0f, cb.w - 6.0f, cb.h - 6.0f };
        SDL_RenderFillRect(r, &cb_fill);
    }
    DrawTextLeft(r, "Use as Shift Button", dx + 60, dy + 308, 16, 255, 255, 255, 255);

    /* Cancel */
    bool cancel_sel = (focus == 4);
    DrawButton(r, "Cancel", dx + dw - 120, dy + dh - 60, 100, 40, false, cancel_sel);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Stick Config Dialog (for LS/RS movement or Mouse Move mapping) */
static void DrawStickConfigDialog(SDL_Renderer *r, UIState *state, int w, int h)
{
    const char *title = "Stick Settings";
    if (state->active_dialog == DIALOG_MOUSE_MOVE_CONFIG) {
        title = "Mouse Movement";
    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_LEFTX || state->selected_axis == SDL_GAMEPAD_AXIS_LEFTY) {
        title = "Left Stick";
    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHTX || state->selected_axis == SDL_GAMEPAD_AXIS_RIGHTY) {
        title = "Right Stick";
    }

    /* Overlay */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect overlay = { 0, 0, (float)w, (float)h };
    SDL_RenderFillRect(r, &overlay);

    float dw = 600.0f;
    float base_dh = 660.0f;

    /* Calculate extra height for sub-options that appear on their own rows */
    float extra_height = 0.0f;
    float row_h = 32.0f;
    if (state->stick_controller) {
        extra_height += row_h;  /* Controller stick target row (Left/Right) */
    }
    if (state->stick_gyro) {
        extra_height += row_h;  /* Gyro mode row (Pitch/Yaw, Roll) */
    }
    if (state->stick_touch_mouse) {
        extra_height += row_h;  /* Touch finger row (First, Second) */
    }

    float dh = base_dh + extra_height;
    float dx = (w - dw) / 2.0f;
    float dy = (h - dh) / 2.0f;

    DrawRoundedRectEx(r, dx, dy, dw, dh, 33, 33, 33, 255, true, 4.0f);
    DrawText(r, title, dx + dw / 2.0f, dy + 40.0f, 24.0f, 255, 255, 255, 255);

    int focus = state ? state->dialog_focus_index : -1;

    float cb_x = dx + 40.0f;
    float cb_y = dy + 90.0f;
    float cb_size = 18.0f;

    /* Helper macro for checkbox drawing with focus highlighting */
    int cb_index = 0;
#define DRAW_STICK_CHECK(label, enabled) \
    do { \
        bool cb_focused = (focus == cb_index); \
        /* Draw grey highlight border if focused */ \
        if (cb_focused) { \
            DrawRoundedRect(r, cb_x - 4.0f, cb_y - 4.0f, cb_size + 8.0f, cb_size + 8.0f, 80, 80, 80, 255, true); \
        } \
        SDL_FRect cb = { cb_x, cb_y, cb_size, cb_size }; \
        SDL_SetRenderDrawColor(r, 60, 60, 60, 255); \
        SDL_RenderFillRect(r, &cb); \
        if (enabled) { \
            SDL_FRect fill = { cb_x + 3.0f, cb_y + 3.0f, cb_size - 6.0f, cb_size - 6.0f }; \
            SDL_SetRenderDrawColor(r, 150, 150, 150, 255); \
            SDL_RenderFillRect(r, &fill); \
        } \
        DrawTextLeft(r, label, cb_x + cb_size + 16.0f, cb_y + cb_size * 0.7f, 18.0f, 255, 255, 255, 255); \
        cb_y += row_h; \
        cb_index++; \
    } while (0)

    DRAW_STICK_CHECK("Use as WASD", state->stick_wasd);
    DRAW_STICK_CHECK("Use as Arrow Keys", state->stick_arrows);
    DRAW_STICK_CHECK("Use as Mouse", state->stick_mouse);
    DRAW_STICK_CHECK("Use as Controller Stick", state->stick_controller);
    /* Controller stick toggle (Left <-> Right), shown on its own row when Controller Stick is enabled */
    if (state->stick_controller) {
        float toggle_y = cb_y + 2.0f;
        float toggle_indent = 30.0f;
        float toggle_w = 44.0f;
        float toggle_h = 20.0f;
        float toggle_x = cb_x + toggle_indent;
        float knob_r = 8.0f;
        bool is_right = (state->stick_controller_target == 1);

        /* Toggle track */
        DrawRoundedRect(r, toggle_x, toggle_y, toggle_w, toggle_h, 50, 50, 50, 255, true);

        /* Toggle knob position */
        float knob_x = is_right
            ? toggle_x + toggle_w - knob_r - 4.0f
            : toggle_x + knob_r + 4.0f;
        float knob_y = toggle_y + toggle_h / 2.0f;

        /* Knob */
        SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
        for (int dy = -(int)knob_r; dy <= (int)knob_r; dy++) {
            for (int ddx = -(int)knob_r; ddx <= (int)knob_r; ddx++) {
                if (ddx * ddx + dy * dy <= (int)(knob_r * knob_r)) {
                    SDL_RenderPoint(r, knob_x + (float)ddx, knob_y + (float)dy);
                }
            }
        }

        /* Show only the active option label */
        const char *stick_label = is_right ? "Right Stick" : "Left Stick";
        DrawTextLeft(r, stick_label, toggle_x + toggle_w + 12.0f,
                     toggle_y + toggle_h * 0.65f, 16.0f, 200, 200, 200, 255);

        cb_y += row_h;
    }

        DRAW_STICK_CHECK("Use as Controller D-Pad", state->stick_dpad);
    DRAW_STICK_CHECK("Use as Gyroscope", state->stick_gyro);

    /* Gyro mode toggle (Pitch/Yaw <-> Roll), shown on its own row when Gyroscope is enabled */
    if (state->stick_gyro) {
        float toggle_y = cb_y + 2.0f;
        float toggle_indent = 30.0f;
        float toggle_w = 44.0f;
        float toggle_h = 20.0f;
        float toggle_x = cb_x + toggle_indent;
        float knob_r = 8.0f;

        /* Toggle track */
        DrawRoundedRect(r, toggle_x, toggle_y, toggle_w, toggle_h, 50, 50, 50, 255, true);

        /* Toggle knob position */
        float knob_x = state->stick_gyro_mode_roll
            ? toggle_x + toggle_w - knob_r - 4.0f
            : toggle_x + knob_r + 4.0f;
        float knob_y = toggle_y + toggle_h / 2.0f;

        /* Knob */
        SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
        for (int dy = -(int)knob_r; dy <= (int)knob_r; dy++) {
            for (int ddx = -(int)knob_r; ddx <= (int)knob_r; ddx++) {
                if (ddx * ddx + dy * dy <= (int)(knob_r * knob_r)) {
                    SDL_RenderPoint(r, knob_x + (float)ddx, knob_y + (float)dy);
                }
            }
        }

        /* Show only the active option label */
        const char *gyro_label = state->stick_gyro_mode_roll ? "Roll" : "Pitch/Yaw";
        DrawTextLeft(r, gyro_label, toggle_x + toggle_w + 12.0f,
                     toggle_y + toggle_h * 0.65f, 16.0f, 200, 200, 200, 255);

        cb_y += row_h;
    }

    DRAW_STICK_CHECK("Use as Touch Mouse", state->stick_touch_mouse);

    /* Touch finger toggle (First <-> Second), shown on its own row when Touch Mouse is enabled */
    if (state->stick_touch_mouse) {
        float toggle_y = cb_y + 2.0f;
        float toggle_indent = 30.0f;
        float toggle_w = 44.0f;
        float toggle_h = 20.0f;
        float toggle_x = cb_x + toggle_indent;
        float knob_r = 8.0f;
        bool is_second = (state->stick_touch_finger == 2);

        /* Toggle track */
        DrawRoundedRect(r, toggle_x, toggle_y, toggle_w, toggle_h, 50, 50, 50, 255, true);

        /* Toggle knob position */
        float knob_x = is_second
            ? toggle_x + toggle_w - knob_r - 4.0f
            : toggle_x + knob_r + 4.0f;
        float knob_y = toggle_y + toggle_h / 2.0f;

        /* Knob */
        SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
        for (int dy = -(int)knob_r; dy <= (int)knob_r; dy++) {
            for (int ddx = -(int)knob_r; ddx <= (int)knob_r; ddx++) {
                if (ddx * ddx + dy * dy <= (int)(knob_r * knob_r)) {
                    SDL_RenderPoint(r, knob_x + (float)ddx, knob_y + (float)dy);
                }
            }
        }

        /* Show only the active option label */
        const char *finger_label = is_second ? "Second Finger" : "First Finger";
        DrawTextLeft(r, finger_label, toggle_x + toggle_w + 12.0f,
                     toggle_y + toggle_h * 0.65f, 16.0f, 200, 200, 200, 255);

        cb_y += row_h;
    }

    DRAW_STICK_CHECK("Invert Horizontal Axis", state->stick_invert_x);
    DRAW_STICK_CHECK("Invert Vertical Axis", state->stick_invert_y);

#undef DRAW_STICK_CHECK

    /* Sliders for sensitivity and acceleration - use fill style like trigger dialog.
     * When Gyroscope mode is enabled, use dedicated gyro sliders instead of the
     * generic mouse-style sensitivity/acceleration controls. */
    float slider_x = dx + 40.0f;
    float slider_w = dw - 80.0f;
    float slider_h = 12.0f;
    float slider_y = cb_y + 34.0f;  /* small blank gap after last checkbox */

    if (state->stick_gyro) {
        /* Gyro Horizontal Sensitivity - focus index 9 */
        bool gh_sens_focused = (focus == 9);
        DrawTextLeft(r, "Gyro Horizontal Sensitivity", slider_x, slider_y - 12.0f, 18.0f, 255, 255, 255, 255);
        DrawRoundedRect(r, slider_x, slider_y, slider_w, slider_h, 48, 48, 48, 255, true);
        {
            float t = (state->stick_gyro_h_sens + 50.0f) / 100.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float fill_w = slider_w * t;
            Uint8 fill_grey = gh_sens_focused ? 140 : 100;
            DrawRoundedRect(r, slider_x, slider_y, fill_w, slider_h, fill_grey, fill_grey, fill_grey, 255, true);
        }
        slider_y += 48.0f;

        /* Gyro Vertical Sensitivity - focus index 10 */
        bool gv_sens_focused = (focus == 10);
        DrawTextLeft(r, "Gyro Vertical Sensitivity", slider_x, slider_y - 12.0f, 18.0f, 255, 255, 255, 255);
        DrawRoundedRect(r, slider_x, slider_y, slider_w, slider_h, 48, 48, 48, 255, true);
        {
            float t = (state->stick_gyro_v_sens + 50.0f) / 100.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float fill_w = slider_w * t;
            Uint8 fill_grey = gv_sens_focused ? 140 : 100;
            DrawRoundedRect(r, slider_x, slider_y, fill_w, slider_h, fill_grey, fill_grey, fill_grey, 255, true);
        }
        slider_y += 48.0f;

        /* Gyro Acceleration - focus index 11 */
        bool g_accel_focused = (focus == 11);
        DrawTextLeft(r, "Gyro Acceleration", slider_x, slider_y - 12.0f, 18.0f, 255, 255, 255, 255);
        DrawRoundedRect(r, slider_x, slider_y, slider_w, slider_h, 48, 48, 48, 255, true);
        {
            float t = (state->stick_gyro_accel + 50.0f) / 100.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float fill_w = slider_w * t;
            Uint8 fill_grey = g_accel_focused ? 140 : 100;
            DrawRoundedRect(r, slider_x, slider_y, fill_w, slider_h, fill_grey, fill_grey, fill_grey, 255, true);
        }
    } else {
        /* Generic mouse-style sensitivity / acceleration sliders */
        /* Horizontal Sensitivity - focus index 9 */
        bool h_sens_focused = (focus == 9);
        DrawTextLeft(r, "Horizontal Sensitivity", slider_x, slider_y - 12.0f, 18.0f, 255, 255, 255, 255);
        DrawRoundedRect(r, slider_x, slider_y, slider_w, slider_h, 48, 48, 48, 255, true);
        {
            float t = (state->stick_h_sens + 50.0f) / 100.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float fill_w = slider_w * t;
            Uint8 fill_grey = h_sens_focused ? 140 : 100;
            DrawRoundedRect(r, slider_x, slider_y, fill_w, slider_h, fill_grey, fill_grey, fill_grey, 255, true);
        }
        slider_y += 48.0f;

        /* Vertical Sensitivity - focus index 10 */
        bool v_sens_focused = (focus == 10);
        DrawTextLeft(r, "Vertical Sensitivity", slider_x, slider_y - 12.0f, 18.0f, 255, 255, 255, 255);
        DrawRoundedRect(r, slider_x, slider_y, slider_w, slider_h, 48, 48, 48, 255, true);
        {
            float t = (state->stick_v_sens + 50.0f) / 100.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float fill_w = slider_w * t;
            Uint8 fill_grey = v_sens_focused ? 140 : 100;
            DrawRoundedRect(r, slider_x, slider_y, fill_w, slider_h, fill_grey, fill_grey, fill_grey, 255, true);
        }
        slider_y += 48.0f;

        /* Horizontal Acceleration - focus index 11 */
        bool h_accel_focused = (focus == 11);
        DrawTextLeft(r, "Horizontal Acceleration", slider_x, slider_y - 12.0f, 18.0f, 255, 255, 255, 255);
        DrawRoundedRect(r, slider_x, slider_y, slider_w, slider_h, 48, 48, 48, 255, true);
        {
            float t = (state->stick_h_accel + 50.0f) / 100.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float fill_w = slider_w * t;
            Uint8 fill_grey = h_accel_focused ? 140 : 100;
            DrawRoundedRect(r, slider_x, slider_y, fill_w, slider_h, fill_grey, fill_grey, fill_grey, 255, true);
        }
        slider_y += 48.0f;

        /* Vertical Acceleration - focus index 12 */
        bool v_accel_focused = (focus == 12);
        DrawTextLeft(r, "Vertical Acceleration", slider_x, slider_y - 12.0f, 18.0f, 255, 255, 255, 255);
        DrawRoundedRect(r, slider_x, slider_y, slider_w, slider_h, 48, 48, 48, 255, true);
        {
            float t = (state->stick_v_accel + 50.0f) / 100.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float fill_w = slider_w * t;
            Uint8 fill_grey = v_accel_focused ? 140 : 100;
            DrawRoundedRect(r, slider_x, slider_y, fill_w, slider_h, fill_grey, fill_grey, fill_grey, 255, true);
        }
    }

    /* Buttons - focus indices 13 and 14 */
    bool ok_sel = (focus == 13);
    bool cancel_sel2 = (focus == 14);
    DrawButton(r, "OK", dx + dw - 220.0f, dy + dh - 60.0f, 80.0f, 40.0f, false, ok_sel);
    DrawButton(r, "Cancel", dx + dw - 120.0f, dy + dh - 60.0f, 100.0f, 40.0f, false, cancel_sel2);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}
/* Text input dialog used for new profile / rename profile */
/* Focus indices: 0 = text field, 1 = OK, 2 = Cancel */
static void DrawTextInputDialog(SDL_Renderer *r, UIState *state, int w, int h, const char *title)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect overlay = { 0, 0, (float)w, (float)h };
    SDL_RenderFillRect(r, &overlay);

    float dw = 500.0f, dh = 200.0f;
    float dx = (w - dw) / 2.0f;
    float dy = (h - dh) / 2.0f;

    DrawRoundedRectEx(r, dx, dy, dw, dh, 33, 33, 33, 255, true, 4.0f);
    DrawText(r, title, dx + dw / 2.0f, dy + 40.0f, 24.0f, 255, 255, 255, 255);

    /* Text input box - focus index 0 */
    int focus = state ? state->dialog_focus_index : -1;
    float box_x = dx + 30.0f;
    float box_y = dy + 80.0f;
    float box_w = dw - 60.0f;
    float box_h = 36.0f;

    /* Highlight text field when focused */
    bool text_field_sel = (focus == 0);
    if (text_field_sel) {
        DrawRoundedRect(r, box_x - 2.0f, box_y - 2.0f, box_w + 4.0f, box_h + 4.0f, 100, 180, 220, 255, true);
    }
    DrawRoundedRect(r, box_x, box_y, box_w, box_h, 20, 20, 20, 255, true);

    const char *buffer = state ? state->input_buffer : "";
    DrawTextLeft(r, buffer, box_x + 10.0f, box_y + box_h * 0.65f, 18.0f, 255, 255, 255, 255);

    /* OK / Cancel buttons - focus indices 1 and 2 */
    bool ok_sel = (focus == 1);
    bool cancel_sel = (focus == 2);
    DrawButton(r, "OK", dx + dw - 220.0f, dy + dh - 60.0f, 90.0f, 40.0f, false, ok_sel);
    DrawButton(r, "Cancel", dx + dw - 120.0f, dy + dh - 60.0f, 100.0f, 40.0f, false, cancel_sel);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Delete confirmation dialog for profiles */
static void DrawDeleteConfirmDialog(SDL_Renderer *r, UIState *state, int w, int h)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect overlay = { 0, 0, (float)w, (float)h };
    SDL_RenderFillRect(r, &overlay);

    float dw = 500.0f, dh = 220.0f;
    float dx = (w - dw) / 2.0f;
    float dy = (h - dh) / 2.0f;

    DrawRoundedRectEx(r, dx, dy, dw, dh, 33, 33, 33, 255, true, 4.0f);

    const char *profile_name = "Profile";
    if (state && state->selected_profile >= 0 && state->selected_profile < state->profile_count) {
        profile_name = state->profile_names[state->selected_profile];
    }

    char message[128];
    SDL_snprintf(message, sizeof(message), "Delete profile '%s'?", profile_name);
    DrawText(r, "Delete Profile", dx + dw / 2.0f, dy + 40.0f, 24.0f, 255, 255, 255, 255);
    DrawTextLeft(r, message, dx + 30.0f, dy + 100.0f, 18.0f, 255, 255, 255, 255);

    int focus = state ? state->dialog_focus_index : -1;
    bool yes_sel = (focus == 0);
    bool no_sel = (focus == 1);
    DrawButton(r, "Yes", dx + dw - 220.0f, dy + dh - 60.0f, 90.0f, 40.0f, false, yes_sel);
    DrawButton(r, "No", dx + dw - 120.0f, dy + dh - 60.0f, 100.0f, 40.0f, false, no_sel);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Virtual Keyboard Dialog for text input on gamepad */
static void DrawVirtualKeyboardDialog(SDL_Renderer *r, UIState *state, int w, int h)
{
    /* Virtual keyboard layout - 4 rows x 10 columns */
    static const char *vk_keys[4][10] = {
        { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
        { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
        { "A", "S", "D", "F", "G", "H", "J", "K", "L", "Bksp" },
        { "Z", "X", "C", "V", "Space", "B", "N", "M", "Done", "Esc" }
    };

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect overlay = { 0, 0, (float)w, (float)h };
    SDL_RenderFillRect(r, &overlay);

    float dw = 580.0f, dh = 310.0f;
    float dx = (w - dw) / 2.0f;
    float dy = (h - dh) / 2.0f;

    DrawRoundedRectEx(r, dx, dy, dw, dh, 33, 33, 33, 255, true, 4.0f);
    DrawText(r, "Rename Profile", dx + dw / 2.0f, dy + 28.0f, 22.0f, 255, 255, 255, 255);

    /* Show current input text */
    float text_box_x = dx + 20.0f;
    float text_box_y = dy + 52.0f;
    float text_box_w = dw - 40.0f;
    float text_box_h = 32.0f;
    DrawRoundedRect(r, text_box_x, text_box_y, text_box_w, text_box_h, 20, 20, 20, 255, true);

    const char *buffer = state ? state->input_buffer : "";
    DrawTextLeft(r, buffer, text_box_x + 10.0f, text_box_y + text_box_h * 0.65f, 16.0f, 255, 255, 255, 255);

    /* Draw keyboard keys - styled like keyboard profile page */
    float key_w = 50.0f;
    float key_h = 50.0f;
    float gap = 4.0f;
    float start_x = dx + 20.0f;
    float start_y = dy + 95.0f;

    int cur_row = state ? state->vk_row : 0;
    int cur_col = state ? state->vk_col : 0;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 10; col++) {
            const char *label = vk_keys[row][col];
            float kx = start_x + col * (key_w + gap);
            float ky = start_y + row * (key_h + gap);

            bool selected = (row == cur_row && col == cur_col);
            Uint8 bg = selected ? 60 : 48;

            /* Draw grey highlight border if selected */
            if (selected) {
                DrawRoundedRect(r, kx - 2.0f, ky - 2.0f, key_w + 4.0f, key_h + 4.0f, 80, 80, 80, 255, true);
            }
            DrawRoundedRect(r, kx, ky, key_w, key_h, bg, bg, bg, 255, true);

            /* Adjust font size based on label length */
            float font_size = 14.0f;
            if (SDL_strlen(label) > 3) {
                font_size = 11.0f;
            } else if (SDL_strlen(label) > 1) {
                font_size = 12.0f;
            }
            DrawText(r, label, kx + key_w / 2.0f, ky + key_h / 2.0f, font_size, 200, 200, 200, 255);
        }
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Trigger Options Dialog (for LT/RT) */
static void DrawTriggerOptionsDialog(SDL_Renderer *r, UIState *state, int w, int h)
{
    int p = state->selected_profile;
    if (p < 0) {
        p = 0;
    } else if (p >= SDL_UI_MAX_PROFILES) {
        p = SDL_UI_MAX_PROFILES - 1;
    }

    SDL_RemapperMapping *mapping = UI_GetMappingForAxisInProfile(state->selected_axis, p);
    char primary_label[64];
    char shift_label[64];
    float deadzone = 0.0f;

    UI_FormatActionText(mapping ? &mapping->primary_action : NULL,
                        primary_label, sizeof(primary_label));
    UI_FormatActionText(mapping ? &mapping->shift_action : NULL,
                        shift_label, sizeof(shift_label));

    if (state->selected_axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
        deadzone = state->trigger_deadzone_left;
    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        deadzone = state->trigger_deadzone_right;
    }
    if (deadzone <= 0.0f) {
        deadzone = 50.0f;
    }

    /* Overlay */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect overlay = { 0, 0, (float)w, (float)h };
    SDL_RenderFillRect(r, &overlay);

    /* Dialog */
    float dw = 500.0f, dh = 350.0f;
    float dx = (w - dw) / 2.0f;
    float dy = (h - dh) / 2.0f;

    DrawRoundedRectEx(r, dx, dy, dw, dh, 33, 33, 33, 255, true, 4.0f);

    const char *axis_name = "Trigger";
    if (state->selected_axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
        axis_name = "Left Trigger";
    } else if (state->selected_axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        axis_name = "Right Trigger";
    }
    DrawText(r, axis_name, dx + dw / 2.0f, dy + 40.0f, 24.0f, 255, 255, 255, 255);

    int focus = state ? state->dialog_focus_index : -1;

    /* Primary mapping */
    bool primary_sel = (focus == 0);
    DrawButton(r, "Primary", dx + 30.0f, dy + 80.0f, 140.0f, 40.0f, false, primary_sel);
    DrawText(r, primary_label, dx + 250.0f, dy + 100.0f, 16.0f, 180, 180, 180, 255);

    /* Shift mapping */
    bool shift_sel = (focus == 1);
    DrawButton(r, "Shift", dx + 30.0f, dy + 150.0f, 140.0f, 40.0f, false, shift_sel);
    DrawText(r, shift_label, dx + 250.0f, dy + 170.0f, 16.0f, 180, 180, 180, 255);

    /* Deadzone slider - focus index 2 */
    bool slider_focused = (focus == 2);
    float slider_x = dx + 40.0f;
    float slider_y = dy + 240.0f;
    float slider_w = dw - 80.0f;
    float slider_h = 12.0f;

    DrawTextLeft(r, "Deadzone (1-100):", slider_x, dy + 220.0f, 18.0f, 255, 255, 255, 255);
    DrawRoundedRect(r, slider_x, slider_y, slider_w, slider_h, 48, 48, 48, 255, true);

    float t = deadzone / 100.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float fill_w = slider_w * t;
    Uint8 fill_grey = slider_focused ? 140 : 100;
    DrawRoundedRect(r, slider_x, slider_y, fill_w, slider_h, fill_grey, fill_grey, fill_grey, 255, true);

    /* Cancel - focus index 3 */
    bool cancel_sel = (focus == 3);
    DrawButton(r, "Cancel", dx + dw - 120.0f, dy + dh - 60.0f, 100.0f, 40.0f, false, cancel_sel);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Mapping Selection Dialog */
static void DrawMappingSelectDialog(SDL_Renderer *r, UIState *state, int w, int h)
{
    const MappingOption *options = NULL;
    int option_count = 0;
    UI_GetActiveOptions(state, &options, &option_count);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect overlay = { 0, 0, (float)w, (float)h };
    SDL_RenderFillRect(r, &overlay);

    float dw = 550, dh = 500;
    float dx = (w - dw) / 2;
    float dy = (h - dh) / 2;

    DrawRoundedRectEx(r, dx, dy, dw, dh, 33, 33, 33, 255, true, 4.0f);
    DrawText(r, "Select Mapping", dx + dw/2, dy + 40, 24, 255, 255, 255, 255);

    /* Tabs */
    const char *tabs[] = { "Controller", "Mouse", "Keyboard", "Touch" };
    for (int i = 0; i < 4; i++) {
        bool selected = (i == state->active_tab);
        DrawButton(r, tabs[i], dx + 20 + i * 130, dy + 80, 120, 40, false, selected);
    }

    /* List background */
    DrawRoundedRect(r, dx + 20, dy + 140, dw - 40, 280, 20, 20, 20, 255, true);

    /* List items with basic scrolling support */
    const int item_height = 50;
    const int visible_rows = 5;

    if (option_count > 0) {
        int max_scroll = (option_count > visible_rows) ? (option_count - visible_rows) : 0;
        if (state->list_scroll < 0) {
            state->list_scroll = 0;
        } else if (state->list_scroll > max_scroll) {
            state->list_scroll = max_scroll;
        }

        if (state->list_selection < 0) {
            state->list_selection = 0;
        } else if (state->list_selection >= option_count) {
            state->list_selection = option_count - 1;
        }

        if (state->list_selection < state->list_scroll) {
            state->list_scroll = state->list_selection;
        } else if (state->list_selection >= state->list_scroll + visible_rows) {
            state->list_scroll = state->list_selection - (visible_rows - 1);
        }

        int row = 0;
        for (int i = state->list_scroll; i < option_count && row < visible_rows; i++, row++) {
            float item_y = dy + 150 + (float)row * (float)item_height;
            bool selected = (i == state->list_selection);

            if (selected) {
                DrawRoundedRect(r, dx + 30, item_y, dw - 60, 40, 60, 90, 140, 255, true);
            }

            /* Left-align labels inside the list area for consistent margins */
            DrawTextLeft(r, options[i].label, dx + 50, item_y + 20, 18, 255, 255, 255, 255);
        }
    }

    /* OK/Cancel */
    DrawButton(r, "OK", dx + dw - 220, dy + dh - 60, 90, 40, false, false);
    DrawButton(r, "Cancel", dx + dw - 120, dy + dh - 60, 100, 40, false, false);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Helper to load a single image from SDL Remapper Assets */
static SDL_Texture* LoadAssetImage(SDL_Renderer *renderer, const char *relative_path)
{
    const char *sdl_root = UI_GetSDLSourceDir();

    char image_path[512];
    SDL_snprintf(image_path, sizeof(image_path), "%sSDL Remapper Assets/%s",
                 sdl_root, relative_path);

    SDL_Surface *surface = SDL_LoadPNG(image_path);

    if (!surface) {
        SDL_Log("Failed to load image '%s': %s", image_path, SDL_GetError());
        return NULL;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture) {
        SDL_Log("Failed to create texture for '%s': %s", image_path, SDL_GetError());
        return NULL;
    }

    return texture;
}

/* Load all page background images */
static void LoadAllPageImages(SDL_Renderer *renderer)
{
    /* Controller images */
    g_controller_device_bg = LoadAssetImage(renderer, "Controller/Device (Controller)/Default.png");
    g_controller_profile_bg = LoadAssetImage(renderer, "Controller/Profile (Controller)/Default.png");
    g_controller_remapper_bg = LoadAssetImage(renderer, "Controller/Remapper (Controller)/Default.png");

    /* Mouse images */
    g_mouse_device_bg = LoadAssetImage(renderer, "Mouse/Device (Mouse)/Default.png");
    g_mouse_profile_bg = LoadAssetImage(renderer, "Mouse/Profile (Mouse)/Default.png");
    g_mouse_remapper_bg = LoadAssetImage(renderer, "Mouse/Remapper (Mouse)/Default.png");

    /* Keyboard images */
    g_keyboard_device_bg = LoadAssetImage(renderer, "Keyboard/Device (Keyboard)/Default.png");
}

/* Main UI function */
int SDL_ShowGamepadRemappingWindow_REAL(SDL_RemapperContext *ctx, SDL_JoystickID gamepad_id)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event event;
    bool done = false;
    UIState state = {0};
    SDL_JoystickID active_gamepad_id = gamepad_id;

    /* Initialize state */
    state.current_page = PAGE_DEVICE_SELECT;
    state.device_count = 0;
    state.selected_device = 0;

    state.profile_count = 1;
    SDL_strlcpy(state.profile_names[0], "Default Profile", 64);
    state.selected_profile = 0;
    state.profile_list_scroll = 0;
    state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
    state.selected_axis = SDL_GAMEPAD_AXIS_INVALID;
    state.active_dialog = DIALOG_NONE;
    state.active_tab = 0;
    state.active_slot = 0;
    state.list_selection = 0;
    state.list_scroll = 0;
    state.trigger_deadzone_left = 50.0f;
    state.trigger_deadzone_right = 50.0f;
    state.mapping_from_trigger = false;
    state.dialog_read_only = false;

    /* Navigation focus state */
    state.profile_focus_on_new_button = false;
    state.profile_action_focus = -1;
    state.profile_preview_index = -1;
    state.profile_mouse_origin_index = -1;
    state.profile_gamepad_origin_index = -1;
    state.mapping_action_focus = -1;
    state.mouse_mapping_origin_slot = -1;
    state.mapping_gamepad_origin_index = -1;
    state.keyboard_mapping_origin_slot = -1;
    state.dialog_focus_index = 0;
    state.nav_stick_x_dir = 0;
    state.nav_stick_y_dir = 0;

    /* Initialize per-profile mappings */
    SDL_memset(&g_ui_profile, 0, sizeof(g_ui_profile));
    g_ui_active_profile_index = 0;
    for (int i = 0; i < SDL_UI_MAX_PROFILES; ++i) {
        g_ui_profile_trigger_deadzone_left[i] = 50.0f;
        g_ui_profile_trigger_deadzone_right[i] = 50.0f;
    }

    /* Profile 0: Default Profile (gamepad passthrough style) */
    UI_InitProfileMappings(0);
    UI_InitGamepadPassthroughDefaultsForProfile(0);

    /* Any additional profiles start as blank templates when created. */

    /* Populate device list for landing page */
    UI_InitDeviceList(&state, active_gamepad_id);

    /* Load any persisted profiles from disk (overriding defaults where names match
     * and appending additional profiles if there is room). This will also commit
     * the currently selected profile to the remapper context. */
    UI_LoadProfilesFromDisk(ctx, active_gamepad_id, &state);

    /* Create window (title used to verify that remapper_app is loading this SDL3 UI build) */
    window = SDL_CreateWindow("Gamepad Remapper - SDL C UI", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) return -1;

    renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_DestroyWindow(window);
        return -1;
    }

    /* Load all icons (controller, mouse, keyboard) and background images */
    UI_LoadAllIcons(renderer);
    LoadAllPageImages(renderer);

    while (!done) {
        int w, h;
        SDL_GetCurrentRenderOutputSize(renderer, &w, &h);

        /* Event handling */
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                done = true;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    if (state.active_dialog != DIALOG_NONE) {
                        state.active_dialog = DIALOG_NONE;
                    } else if (state.current_page == PAGE_BUTTON_MAPPING) {
                        state.current_page = PAGE_PROFILE_SELECT;
                    } else {
                        done = true;
                    }
                }
                /* Slot selection */
                else if (event.key.scancode == SDL_SCANCODE_1) state.active_slot = 0;
                else if (event.key.scancode == SDL_SCANCODE_2) state.active_slot = 1;
                else if (event.key.scancode == SDL_SCANCODE_3) state.active_slot = 2;
                else if (state.active_dialog == DIALOG_MAPPING_SELECT) {
                    const MappingOption *options = NULL;
                    int option_count = 0;
                    UI_GetActiveOptions(&state, &options, &option_count);

                    const int visible_rows = 5;

                    if (event.key.scancode == SDL_SCANCODE_UP) {
                        state.list_selection--;
                        if (state.list_selection < 0) {
                            state.list_selection = 0;
                        }
                    } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
                        state.list_selection++;
                        if (state.list_selection >= option_count) {
                            state.list_selection = option_count - 1;
                        }
                    } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
                        /* Switch tabs left */
                        state.active_tab--;
                        if (state.active_tab < 0) {
                            state.active_tab = 3;
                        }
                        state.list_selection = 0;
                        state.list_scroll = 0;
                    } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
                        /* Switch tabs right */
                        state.active_tab++;
                        if (state.active_tab > 3) {
                            state.active_tab = 0;
                        }
                        state.list_selection = 0;
                        state.list_scroll = 0;
                    } else if (event.key.scancode == SDL_SCANCODE_RETURN && option_count > 0) {
                        /* Confirm selection */
                        SDL_RemapperMapping *mapping = NULL;
                        if (state.selected_button != SDL_GAMEPAD_BUTTON_INVALID) {
                            mapping = UI_GetMappingForButton(state.selected_button);
                        } else if (state.selected_axis != SDL_GAMEPAD_AXIS_INVALID) {
                            mapping = UI_GetMappingForAxis(state.selected_axis);
                        } else if (state.selected_mouse_slot >= 0 && state.selected_mouse_slot < UI_MOUSE_SLOT_COUNT) {
                            mapping = UI_GetMouseSlotMapping((UI_MouseSlot)state.selected_mouse_slot);
                        } else if (state.selected_keyboard_slot >= 0 && state.selected_keyboard_slot < UI_KEYBOARD_SLOT_COUNT) {
                            mapping = UI_GetKeyboardSlotMapping((UI_KeyboardSlot)state.selected_keyboard_slot);
                        }

                        if (mapping) {
                            int sel = state.list_selection;
                            if (sel < 0 || sel >= option_count) sel = 0;
                            UI_ApplyMappingToSlot(ctx, active_gamepad_id, mapping, state.active_slot, &options[sel], &state);
                        }

                        if (state.mapping_from_trigger && state.selected_axis != SDL_GAMEPAD_AXIS_INVALID) {
                            state.active_dialog = DIALOG_TRIGGER_OPTIONS;
                        } else {
                            state.active_dialog = DIALOG_NONE;
                        }
                    }

                    /* Keep scroll window aligned with selection */
                    if (option_count > 0) {
                        int max_scroll = (option_count > visible_rows) ? (option_count - visible_rows) : 0;
                        if (state.list_scroll < 0) {
                            state.list_scroll = 0;
                        } else if (state.list_scroll > max_scroll) {
                            state.list_scroll = max_scroll;
                        }

                        if (state.list_selection < state.list_scroll) {
                            state.list_scroll = state.list_selection;
                        } else if (state.list_selection >= state.list_scroll + visible_rows) {
                            state.list_scroll = state.list_selection - (visible_rows - 1);
                        }
                    }
                }
                else if (state.active_dialog == DIALOG_NEW_PROFILE ||
                         state.active_dialog == DIALOG_RENAME_PROFILE) {
                    /* Text input handling for profile name dialogs */
                    SDL_Scancode sc = event.key.scancode;
                    int max_len = (int)(sizeof(state.input_buffer) - 1);
                    bool handled_char = false;

                    if (sc == SDL_SCANCODE_BACKSPACE) {
                        if (state.input_cursor > 0) {
                            state.input_cursor--;
                            state.input_buffer[state.input_cursor] = '\0';
                        }
                        handled_char = true;
                    } else if (sc == SDL_SCANCODE_SPACE) {
                        if (state.input_cursor < max_len) {
                            state.input_buffer[state.input_cursor++] = ' ';
                            state.input_buffer[state.input_cursor] = '\0';
                        }
                        handled_char = true;
                    } else if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
                        char c = (char)('a' + (int)(sc - SDL_SCANCODE_A));
                        if (state.input_cursor < max_len) {
                            state.input_buffer[state.input_cursor++] = c;
                            state.input_buffer[state.input_cursor] = '\0';
                        }
                        handled_char = true;
                    }

                    /* Navigation for text field, OK, Cancel */
                    if (!handled_char) {
                        if (sc == SDL_SCANCODE_UP) {
                            UI_HandleGamepadNavButton(ctx,
                                                      active_gamepad_id,
                                                      SDL_GAMEPAD_BUTTON_DPAD_UP,
                                                      &state,
                                                      &done);
                        } else if (sc == SDL_SCANCODE_DOWN) {
                            UI_HandleGamepadNavButton(ctx,
                                                      active_gamepad_id,
                                                      SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                                      &state,
                                                      &done);
                        } else if (sc == SDL_SCANCODE_LEFT) {
                            UI_HandleGamepadNavButton(ctx,
                                                      active_gamepad_id,
                                                      SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                                                      &state,
                                                      &done);
                        } else if (sc == SDL_SCANCODE_RIGHT) {
                            UI_HandleGamepadNavButton(ctx,
                                                      active_gamepad_id,
                                                      SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                                                      &state,
                                                      &done);
                        } else if (sc == SDL_SCANCODE_RETURN) {
                            UI_HandleGamepadNavButton(ctx,
                                                      active_gamepad_id,
                                                      SDL_GAMEPAD_BUTTON_SOUTH,
                                                      &state,
                                                      &done);
                        }
                    }
                }
                else if (state.active_dialog != DIALOG_NONE) {
                    /* For all other dialogs, mirror gamepad D-pad/A with arrow keys and Enter */
                    if (event.key.scancode == SDL_SCANCODE_UP) {
                        UI_HandleGamepadNavButton(ctx,
                                                  active_gamepad_id,
                                                  SDL_GAMEPAD_BUTTON_DPAD_UP,
                                                  &state,
                                                  &done);
                    } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
                        UI_HandleGamepadNavButton(ctx,
                                                  active_gamepad_id,
                                                  SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                                  &state,
                                                  &done);
                    } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
                        UI_HandleGamepadNavButton(ctx,
                                                  active_gamepad_id,
                                                  SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                                                  &state,
                                                  &done);
                    } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
                        UI_HandleGamepadNavButton(ctx,
                                                  active_gamepad_id,
                                                  SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                                                  &state,
                                                  &done);
                    } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
                        UI_HandleGamepadNavButton(ctx,
                                                  active_gamepad_id,
                                                  SDL_GAMEPAD_BUTTON_SOUTH,
                                                  &state,
                                                  &done);
                    }
                }
                else if (state.current_page == PAGE_DEVICE_SELECT) {
                    /* Keyboard navigation on device page - delegate to gamepad handler */
                    if (event.key.scancode == SDL_SCANCODE_LEFT) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_LEFT, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_UP) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_UP, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_DOWN, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_SOUTH, &state, &done);
                    }
                }
                /* Keyboard arrow navigation for profile page - use same logic as gamepad */
                else if (state.current_page == PAGE_PROFILE_SELECT && state.active_dialog == DIALOG_NONE) {
                    if (event.key.scancode == SDL_SCANCODE_UP) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_UP, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_DOWN, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_LEFT, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_SOUTH, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_EAST, &state, &done);
                    }
                }
                else if (state.current_page == PAGE_BUTTON_MAPPING && state.active_dialog == DIALOG_NONE) {
                    /* Button mapping page: use same navigation logic for all device types */
                    if (event.key.scancode == SDL_SCANCODE_UP) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_UP, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_DOWN, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_LEFT, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_SOUTH, &state, &done);
                    } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
                        UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_EAST, &state, &done);
                    }
                }
            } else if (event.type == SDL_EVENT_TEXT_INPUT) {
                /* Handle text input from on-screen keyboard or IME */
                if (state.active_dialog == DIALOG_NEW_PROFILE ||
                    state.active_dialog == DIALOG_RENAME_PROFILE) {
                    const char *text = event.text.text;
                    int max_len = (int)(sizeof(state.input_buffer) - 1);
                    while (*text && state.input_cursor < max_len) {
                        state.input_buffer[state.input_cursor++] = *text++;
                    }
                    state.input_buffer[state.input_cursor] = '\0';
                }
            } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                UI_HandleGamepadNavButton(ctx,
                                          active_gamepad_id,
                                          (SDL_GamepadButton)event.gbutton.button,
                                          &state,
                                          &done);
            } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                const SDL_GamepadAxisEvent *aev = &event.gaxis;
                const Sint16 threshold = 16000;

                if (aev->axis == SDL_GAMEPAD_AXIS_LEFTX) {
                    int new_dir = 0;
                    if (aev->value > threshold) {
                        new_dir = 1;
                    } else if (aev->value < -threshold) {
                        new_dir = -1;
                    }

                    if (new_dir != state.nav_stick_x_dir) {
                        state.nav_stick_x_dir = new_dir;
                        if (new_dir < 0) {
                            UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_LEFT, &state, &done);
                        } else if (new_dir > 0) {
                            UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, &state, &done);
                        }
                    }
                } else if (aev->axis == SDL_GAMEPAD_AXIS_LEFTY) {
                    int new_dir = 0;
                    if (aev->value > threshold) {
                        new_dir = 1;
                    } else if (aev->value < -threshold) {
                        new_dir = -1;
                    }

                    if (new_dir != state.nav_stick_y_dir) {
                        state.nav_stick_y_dir = new_dir;
                        if (new_dir < 0) {
                            UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_UP, &state, &done);
                        } else if (new_dir > 0) {
                            UI_HandleGamepadNavButton(ctx, active_gamepad_id, SDL_GAMEPAD_BUTTON_DPAD_DOWN, &state, &done);
                        }
                    }
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                float mx = event.button.x;
                float my = event.button.y;

                /* Handle clicks based on current page/dialog */
                if (state.current_page == PAGE_DEVICE_SELECT && state.active_dialog == DIALOG_NONE) {
                    /* Device select page: left/right arrows and Configure button */
                    if (state.device_count > 1) {
                        float arrow_size = ScaleY(80.0f, h);
                        float left_x = ScaleX(180.0f, w);
                        float right_x = (float)w - ScaleX(180.0f, w);
                        float arrow_y = (float)h * 0.5f;

                        /* Left arrow hit */
                        if (mx >= left_x - arrow_size && mx <= left_x + arrow_size &&
                            my >= arrow_y - arrow_size && my <= arrow_y + arrow_size) {
                            state.selected_device--;
                            if (state.selected_device < 0) {
                                state.selected_device = state.device_count - 1;
                            }
                            continue;
                        }

                        /* Right arrow hit */
                        if (mx >= right_x - arrow_size && mx <= right_x + arrow_size &&
                            my >= arrow_y - arrow_size && my <= arrow_y + arrow_size) {
                            state.selected_device++;
                            if (state.selected_device >= state.device_count) {
                                state.selected_device = 0;
                            }
                            continue;
                        }
                    }

                    /* Device card and Configure button - use same coordinates as DrawDeviceSelectPage */
                    {
                        float card_w = ScaleX(1100.0f, w);
                        float card_h = card_w;  /* Square shape - matches drawing code */
                        float card_x = (float)w * 0.5f - card_w * 0.5f;
                        float card_y = (float)h * 0.5f - card_h * 0.5f;  /* Centered - matches drawing code */
                        float btn_w = ScaleX(420.0f, w);
                        float btn_h = ScaleY(90.0f, h);
                        float btn_x = (float)w * 0.5f - btn_w * 0.5f;
                        float btn_y = card_y + card_h + ScaleY(60.0f, h);

                        bool clicked_card = (mx >= card_x && mx <= card_x + card_w &&
                                            my >= card_y && my <= card_y + card_h);
                        bool clicked_button = (mx >= btn_x && mx <= btn_x + btn_w &&
                                              my >= btn_y && my <= btn_y + btn_h);

                        if ((clicked_card || clicked_button) && state.device_count > 0) {
                            int idx = state.selected_device;
                            if (idx < 0) idx = 0;
                            if (idx >= state.device_count) idx = state.device_count - 1;

                            if (state.device_types[idx] == UI_DEVICE_TYPE_GAMEPAD && state.device_gamepad_ids[idx] != 0) {
                                active_gamepad_id = state.device_gamepad_ids[idx];
                            }

                            UI_CommitProfileToContext(ctx, active_gamepad_id, &state);

                            /* Reset profile page focus when entering profile select. */
                            state.profile_action_focus = -1;
                            state.profile_focus_on_new_button = false;
                            state.profile_preview_index = -1;
                            state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                            state.selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                            state.selected_mouse_slot = -1;
                            state.selected_keyboard_slot = -1;

                            state.current_page = PAGE_PROFILE_SELECT;
                        }
                    }

                    /* Back button (bottom right) - same sizing as DrawDeviceSelectPage */
                    {
                        float btn_h = 50.0f;
                        float btn_padding = 20.0f;
                        float back_w = CalcButtonWidth("Back", btn_h, btn_padding);
                        float back_x = (float)w - back_w - 20.0f;
                        float back_y = (float)h - 80.0f;

                        if (mx >= back_x && mx <= back_x + back_w &&
                            my >= back_y && my <= back_y + btn_h) {
                            done = true;
                        }
                    }
                } else if (state.current_page == PAGE_PROFILE_SELECT && state.active_dialog == DIALOG_NONE) {
                    /* Profile list layout constants (must match DrawProfileSelectPage) */
                    float panel_left = 20.0f;
                    float panel_top = 40.0f;
                    float panel_height = (float)h - 60.0f;
                    float new_btn_y = panel_top + 60.0f;
                    float item_height = 60.0f;
                    float list_top = new_btn_y + 70.0f;
                    float list_bottom = panel_top + panel_height - 20.0f;
                    int visible_rows = (int)((list_bottom - list_top) / item_height);

                    /* New Profile button */
                    if (mx >= panel_left + 20.0f && mx <= panel_left + 280.0f &&
                        my >= new_btn_y && my <= new_btn_y + 50.0f) {
                        if (state.profile_count < SDL_UI_MAX_PROFILES) {
                            int index = state.profile_count;
                            SDL_snprintf(state.profile_names[index], sizeof(state.profile_names[index]), "New Profile %d", index + 1);
                            state.profile_count++;
                            state.selected_profile = index;
                            UI_InitProfileMappings(index);
                            UI_CommitProfileToContext(ctx, active_gamepad_id, &state);
                            UI_SaveCurrentProfileToDisk(&state);
                        }
                    } else if (mx >= panel_left + 20.0f && mx <= panel_left + 280.0f &&
                               my >= list_top && my <= list_bottom) {
                        /* Profile list selection (with scroll offset) */
                        int row = (int)((my - list_top) / item_height);
                        int index = state.profile_list_scroll + row;
                        if (index >= 0 && index < state.profile_count) {
                            state.selected_profile = index;
                            UI_CommitProfileToContext(ctx, active_gamepad_id, &state);
                        }
                    } else {
                        /* Action buttons - calculate positions matching DrawProfileSelectPage */
                        float center_x = 360.0f;
                        float btn_y_pos = 90.0f;
                        float btn_h_pos = 50.0f;
                        float btn_padding = 20.0f;
                        float btn_gap = 15.0f;
                        float start_x = center_x + 20.0f;

                        /* Calculate button widths */
                        float edit_w = CalcButtonWidth("Edit", btn_h_pos, btn_padding);
                        float dup_w = CalcButtonWidth("Duplicate", btn_h_pos, btn_padding);
                        float del_w = CalcButtonWidth("Delete", btn_h_pos, btn_padding);
                        float ren_w = CalcButtonWidth("Rename", btn_h_pos, btn_padding);

                        float edit_x = start_x;
                        float dup_x = edit_x + edit_w + btn_gap;
                        float del_x = dup_x + dup_w + btn_gap;
                        float ren_x = del_x + del_w + btn_gap;

                        /* Edit button */
                        if (mx >= edit_x && mx <= edit_x + edit_w &&
                            my >= btn_y_pos && my <= btn_y_pos + btn_h_pos) {
                            state.mapping_action_focus = -1;
                            state.current_page = PAGE_BUTTON_MAPPING;
                        }
                        /* Duplicate button */
                        else if (mx >= dup_x && mx <= dup_x + dup_w &&
                                 my >= btn_y_pos && my <= btn_y_pos + btn_h_pos) {
                            if (state.profile_count < SDL_UI_MAX_PROFILES && state.selected_profile >= 0) {
                                int src = state.selected_profile;
                                int dst = state.profile_count;
                                SDL_snprintf(state.profile_names[dst], 64, "%s (Copy)", state.profile_names[src]);
                                SDL_memcpy(g_ui_profile_mappings[dst], g_ui_profile_mappings[src],
                                           sizeof(g_ui_profile_mappings[dst]));
                                g_ui_profile_trigger_deadzone_left[dst] = g_ui_profile_trigger_deadzone_left[src];
                                g_ui_profile_trigger_deadzone_right[dst] = g_ui_profile_trigger_deadzone_right[src];
                                state.profile_count++;
                                state.selected_profile = dst;
                                UI_CommitProfileToContext(ctx, active_gamepad_id, &state);
                                UI_SaveCurrentProfileToDisk(&state);
                            }
                        }
                        /* Delete button */
                        else if (mx >= del_x && mx <= del_x + del_w &&
                                 my >= btn_y_pos && my <= btn_y_pos + btn_h_pos) {
                            if (state.profile_count > 1 && state.selected_profile > 0) {
                                state.dialog_focus_index = 1; /* Default to No */
                                state.active_dialog = DIALOG_DELETE_CONFIRM;
                            }
                        }
                        /* Rename button (not for default profile) */
                        else if (mx >= ren_x && mx <= ren_x + ren_w &&
                                 my >= btn_y_pos && my <= btn_y_pos + btn_h_pos) {
                            if (state.selected_profile > 0 && state.selected_profile < state.profile_count) {
                                SDL_strlcpy(state.input_buffer, state.profile_names[state.selected_profile], 64);
                                state.input_cursor = (int)SDL_strlen(state.input_buffer);
                                state.dialog_focus_index = 0; /* Default to text field */
                                state.active_dialog = DIALOG_RENAME_PROFILE;
                            }
                        }
                        /* Back button (bottom right) */
                        else {
                            float back_w_calc = CalcButtonWidth("Back", 50, btn_padding);
                            float back_x_calc = (float)w - back_w_calc - 20.0f;
                            float back_y_calc = (float)h - 80.0f;
                            if (mx >= back_x_calc && mx <= back_x_calc + back_w_calc &&
                                my >= back_y_calc && my <= back_y_calc + 50.0f) {
                                UI_HandleBack(&state, &done);
                            }
                            /* Otherwise, hit test overlays in read-only mode based on device type */
                            else {
                                int w_window = w;
                                int h_window = h;

                            /* Determine which device overlays are visible on the profile page */
                            UIDeviceType device_type = UI_DEVICE_TYPE_GAMEPAD;
                            if (state.device_count > 0) {
                                int idx = state.selected_device;
                                if (idx < 0) {
                                    idx = 0;
                                } else if (idx >= state.device_count) {
                                    idx = state.device_count - 1;
                                }
                                device_type = state.device_types[idx];
                            }

                            if (device_type == UI_DEVICE_TYPE_MOUSE) {
                                /* Hit test mouse tiles in read-only mode - 2x4 layout */
                                float tile_w = ScaleX(130.0f, w_window);
                                float tile_h = ScaleY(130.0f, h_window);
                                float gap_y = ScaleY(40.0f, h_window);

                                /* Calculate box dimensions (same as drawing code) */
                                float btn_size_ref = 130.0f;
                                float x_offset = 300.0f;
                                float left_btn_x = 1035.0f;
                                float right_btn_x = 2775.0f;
                                float top_btn_y_ref = 417.0f;
                                float bottom_btn_y = 1584.0f;

                                float inner_left = left_btn_x + btn_size_ref;
                                float inner_right = right_btn_x;
                                float inner_top = top_btn_y_ref + 40.0f + btn_size_ref / 2.0f;
                                float overlap = btn_size_ref * 0.20f;

                                float bg_left_ref = inner_left + x_offset - overlap;
                                float bg_right_ref = inner_right + x_offset + overlap;
                                float old_bg_top = inner_top + 250.0f - overlap;
                                float old_bg_bottom = bottom_btn_y + 40.0f + 250.0f + btn_size_ref / 2.0f;
                                float box_h_ref = old_bg_bottom - old_bg_top;

                                float bg_left = ScaleX(bg_left_ref, w_window);
                                float bg_right = ScaleX(bg_right_ref, w_window);
                                float bg_w = bg_right - bg_left;
                                float bg_h = ScaleY(box_h_ref, h_window);

                                float edit_bottom_screen = 140.0f;
                                float back_top_screen = (float)h_window - 80.0f;
                                float region_center_screen = (edit_bottom_screen + back_top_screen) / 2.0f;
                                float full_bg_top = region_center_screen - bg_h / 2.0f;
                                float full_center_x = bg_left + bg_w * 0.5f;
                                float full_center_y = full_bg_top + bg_h * 0.5f;

                                /* Smaller box (80%) for button start position */
                                float box_scale = 0.80f;
                                float small_bg_h = bg_h * box_scale;
                                float small_bg_top = full_center_y - small_bg_h * 0.5f;

                                /* 2x4 layout positions - horizontal uses full box */
                                float left_col_x = bg_left + bg_w * 0.20f - tile_w * 0.5f;
                                float right_col_x = bg_left + bg_w * 0.80f - tile_w * 0.5f;

                                const int layout_order[8] = {
                                    UI_MOUSE_SLOT_LEFT, UI_MOUSE_SLOT_RIGHT,
                                    UI_MOUSE_SLOT_MOVE, UI_MOUSE_SLOT_WHEEL_UP,
                                    UI_MOUSE_SLOT_X2, UI_MOUSE_SLOT_MIDDLE,
                                    UI_MOUSE_SLOT_X1, UI_MOUSE_SLOT_WHEEL_DOWN
                                };

                                /* Buttons start below top of smaller image box with more padding */
                                float start_y = small_bg_top + ScaleY(40.0f, h_window);

                                for (int idx = 0; idx < 8; ++idx) {
                                    int slot = layout_order[idx];
                                    int col = idx % 2;
                                    int row = idx / 2;
                                    float x = (col == 0) ? left_col_x : right_col_x;
                                    float y = start_y + (float)row * (tile_h + gap_y);

                                    if (mx >= x && mx <= x + tile_w && my >= y && my <= y + tile_h) {
                                        state.selected_mouse_slot = slot;
                                        state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                        state.selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                                        state.selected_keyboard_slot = -1;
                                        state.mapping_from_trigger = false;
                                        state.dialog_read_only = true;
                                        state.active_slot = 0;
                                        state.active_tab = 1;

                                        if (slot == UI_MOUSE_SLOT_MOVE) {
                                            UI_LoadMouseMoveState(&state);
                                            state.dialog_focus_index = 0;
                                            state.active_dialog = DIALOG_MOUSE_MOVE_CONFIG;
                                        } else {
                                            state.dialog_focus_index = 0;
                                            state.active_dialog = DIALOG_BUTTON_OPTIONS;
                                        }
                                        break;
                                    }
                                }
                            } else if (device_type == UI_DEVICE_TYPE_KEYBOARD) {
                                /* Hit test full UK QWERTY layout in read-only mode */
                                float key_unit = ScaleX(110.0f, w_window);  /* 110x110 for profile page */
                                float gap = ScaleX(8.0f, w_window);
                                float min_x;
                                float max_x;
                                float min_y;
                                float max_y;
                                UI_ComputeKeyboardLayoutBounds(key_unit, gap, &min_x, &max_x, &min_y, &max_y);

                                float layout_center_x = (min_x + max_x) * 0.5f;
                                float layout_center_y = (min_y + max_y) * 0.5f;

                                float btn_bottom = 90.0f + 50.0f;  /* btn_y + btn_h from action buttons */
                                float back_top = (float)h_window - 80.0f;

                                float panel_right = 20.0f + 300.0f;
                                float region_center_x = (panel_right + (float)w_window) * 0.5f;
                                float target_center_y = (btn_bottom + back_top) * 0.5f;

                                float start_kx = region_center_x - layout_center_x;
                                float start_ky = target_center_y - layout_center_y;

                                for (int i = 0; i < uk_qwerty_layout_count; ++i) {
                                    const KeyPosition *kp = &uk_qwerty_layout[i];

                                    float kx = start_kx + kp->col * (key_unit + gap);
                                    float ky = start_ky + kp->row * (key_unit + gap);
                                    float kw = kp->width * key_unit + (kp->width - 1.0f) * gap;
                                    float kh = key_unit;

                                    /* Multi-row keys */
                                    if (kp->scancode == SDL_SCANCODE_KP_ENTER && i + 1 < uk_qwerty_layout_count) {
                                        kh = key_unit * 2.0f + gap;
                                    } else if (kp->scancode == SDL_SCANCODE_KP_PLUS && i - 1 >= 0) {
                                        kh = key_unit * 2.0f + gap;
                                    }

                                    if (mx >= kx && mx <= kx + kw && my >= ky && my <= ky + kh) {
                                        state.selected_keyboard_slot = (int)kp->scancode;
                                        state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                        state.selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                                        state.selected_mouse_slot = -1;  /* Clear mouse slot */
                                        state.mapping_from_trigger = false;
                                        state.dialog_read_only = true;
                                        state.active_slot = 0;
                                        state.active_tab = 2; /* Keyboard tab */
                                        state.dialog_focus_index = 0;
                                        state.active_dialog = DIALOG_BUTTON_OPTIONS;
                                        break;
                                    }
                                }
                            } else {
                                /* Default controller overlays in read-only mode */
                                /* First, regular buttons */
                                bool handled = false;
                                for (int i = 0; i < (int)(sizeof(remapping_buttons) / sizeof(remapping_buttons[0])); i++) {
                                    const RemappingButton *btn = &remapping_buttons[i];
                                    float bw_btn = ScaleX(130, w_window);
                                    float bh_btn = ScaleY(130, h_window);
                                    float bx = ScaleX(btn->x + 300.0f, w_window);
                                    float center_y = ScaleY(btn->y + 40.0f + 250.0f, h_window);
                                    float by = center_y - bh_btn / 2.0f;

                                    if (mx >= bx && mx <= bx + bw_btn && my >= by && my <= by + bh_btn) {
                                        state.selected_button = btn->button;
                                        state.selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                                        state.selected_keyboard_slot = -1;  /* Clear keyboard slot */
                                        state.selected_mouse_slot = -1;     /* Clear mouse slot */
                                        state.dialog_read_only = true;
                                        state.active_slot = 0;
                                        state.dialog_focus_index = 0;
                                        state.active_dialog = DIALOG_BUTTON_OPTIONS;
                                        handled = true;
                                        break;
                                    }
                                }

                                if (!handled) {
                                    /* Triggers (LT/RT) */
                                    float bw = ScaleX(130, w_window);
                                    float bh = ScaleY(130, h_window);
                                    float lt_bx = ScaleX(LT_X + 300.0f, w_window);
                                    float rt_bx = ScaleX(RT_X + 300.0f, w_window);

                                    float lt_center_y = ScaleY(LT_Y + 40.0f + 250.0f, h_window);
                                    float rt_center_y = ScaleY(RT_Y + 40.0f + 250.0f, h_window);
                                    float lt_by = lt_center_y - bh / 2.0f;
                                    float rt_by = rt_center_y - bh / 2.0f;

                                    if (mx >= lt_bx && mx <= lt_bx + bw && my >= lt_by && my <= lt_by + bh) {
                                        state.selected_axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                                        state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                        state.selected_keyboard_slot = -1;
                                        state.selected_mouse_slot = -1;
                                        state.active_slot = 0;
                                        state.dialog_read_only = true;
                                        state.dialog_focus_index = 0;
                                        state.active_dialog = DIALOG_TRIGGER_OPTIONS;
                                        handled = true;
                                    } else if (mx >= rt_bx && mx <= rt_bx + bw && my >= rt_by && my <= rt_by + bh) {
                                        state.selected_axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                                        state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                        state.selected_keyboard_slot = -1;
                                        state.selected_mouse_slot = -1;
                                        state.active_slot = 0;
                                        state.dialog_read_only = true;
                                        state.dialog_focus_index = 0;
                                        state.active_dialog = DIALOG_TRIGGER_OPTIONS;
                                        handled = true;
                                    }
                                }

                                if (!handled) {
                                    /* Stick movement (LS/RS) */
                                    float sm_bw = ScaleX(130, w_window);
                                    float sm_bh = ScaleY(130, h_window);
                                    float ls_bx = ScaleX(LS_MOVE_X + 300.0f, w_window);
                                    float rs_bx = ScaleX(RS_MOVE_X + 300.0f, w_window);

                                    float ls_center_y = ScaleY(LS_MOVE_Y + 40.0f + 250.0f, h_window);
                                    float rs_center_y = ScaleY(RS_MOVE_Y + 40.0f + 250.0f, h_window);
                                    float ls_by = ls_center_y - sm_bh / 2.0f;
                                    float rs_by = rs_center_y - sm_bh / 2.0f;

                                    if (mx >= ls_bx && mx <= ls_bx + sm_bw && my >= ls_by && my <= ls_by + sm_bh) {
                                        state.selected_axis = SDL_GAMEPAD_AXIS_LEFTX;
                                        state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                        state.selected_keyboard_slot = -1;
                                        state.selected_mouse_slot = -1;
                                        UI_LoadStickStateFromAxis(SDL_GAMEPAD_AXIS_LEFTX, &state);
                                        state.dialog_read_only = true;
                                        state.dialog_focus_index = 0;
                                        state.active_dialog = DIALOG_STICK_CONFIG;
                                    } else if (mx >= rs_bx && mx <= rs_bx + sm_bw && my >= rs_by && my <= rs_by + sm_bh) {
                                        state.selected_axis = SDL_GAMEPAD_AXIS_RIGHTX;
                                        state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                        state.selected_keyboard_slot = -1;
                                        state.selected_mouse_slot = -1;
                                        UI_LoadStickStateFromAxis(SDL_GAMEPAD_AXIS_RIGHTX, &state);
                                        state.dialog_read_only = true;
                                        state.dialog_focus_index = 0;
                                        state.active_dialog = DIALOG_STICK_CONFIG;
                                    }
                                }
                            }
                        }
                        }
                    }
                }
                else if (state.current_page == PAGE_BUTTON_MAPPING && state.active_dialog == DIALOG_NONE) {
                    UIDeviceType device_type = UI_DEVICE_TYPE_GAMEPAD;

                    if (state.device_count > 0) {
                        int idx = state.selected_device;
                        if (idx < 0) {
                            idx = 0;
                        } else if (idx >= state.device_count) {
                            idx = state.device_count - 1;
                        }
                        device_type = state.device_types[idx];
                    }

                    /* Back button */
                    if (mx >= w - 160 && mx <= w - 20 && my >= h - 80 && my <= h - 30) {
                        UI_HandleBack(&state, &done);
                    }
                    /* Restore to Defaults button - centered horizontally */
                    else if (my >= h - 80 && my <= h - 30) {
                        float btn_h = 50.0f;
                        float btn_padding = 20.0f;
                        float restore_w = CalcButtonWidth("Restore to Defaults", btn_h, btn_padding);
                        float restore_x = (float)w * 0.5f - restore_w * 0.5f;
                        if (mx >= restore_x && mx <= restore_x + restore_w) {
                            /* Restore to Defaults: reset current profile to passthrough */
                            int p = state.selected_profile;
                            if (p >= 0 && p < SDL_UI_MAX_PROFILES) {
                                UI_InitProfileMappings(p);
                                UI_InitGamepadPassthroughDefaultsForProfile(p);
                                g_ui_profile_trigger_deadzone_left[p] = 10.0f;
                                g_ui_profile_trigger_deadzone_right[p] = 10.0f;
                                state.trigger_deadzone_left = 10.0f;
                                state.trigger_deadzone_right = 10.0f;
                                UI_CommitProfileToContext(ctx, active_gamepad_id, &state);
                                UI_SaveCurrentProfileToDisk(&state);
                            }
                        }
                    }
                    else if (device_type == UI_DEVICE_TYPE_MOUSE) {
                        /* Mouse device: hit test 2x4 layout (matching drawing code) */
                        float tile_w = ScaleX(150.0f, w);
                        float tile_h = ScaleY(150.0f, h);
                        float gap_y = ScaleY(46.0f, h);

                        /* Full box dimensions for button positioning */
                        float full_bg_w_ref = 1662.0f * 1.20f;
                        float full_bg_h_ref = 1193.0f * 1.20f;

                        float page_center_x = (float)w * 0.5f;
                        float page_center_y = (float)h * 0.5f;

                        float full_bg_w = ScaleX(full_bg_w_ref, w);
                        float full_bg_h = ScaleY(full_bg_h_ref, h);
                        float full_bg_left = page_center_x - full_bg_w * 0.5f;

                        /* Smaller box (80%) for button start position */
                        float box_scale = 0.80f;
                        float bg_h = full_bg_h * box_scale;
                        float bg_top = page_center_y - bg_h * 0.5f;

                        /* 2x4 layout positions */
                        float left_col_x = full_bg_left + full_bg_w * 0.20f - tile_w * 0.5f;
                        float right_col_x = full_bg_left + full_bg_w * 0.80f - tile_w * 0.5f;

                        const int layout_order[8] = {
                            UI_MOUSE_SLOT_LEFT, UI_MOUSE_SLOT_RIGHT,
                            UI_MOUSE_SLOT_MOVE, UI_MOUSE_SLOT_WHEEL_UP,
                            UI_MOUSE_SLOT_X2, UI_MOUSE_SLOT_MIDDLE,
                            UI_MOUSE_SLOT_X1, UI_MOUSE_SLOT_WHEEL_DOWN
                        };

                        float start_y = bg_top + ScaleY(46.0f, h);

                        for (int idx = 0; idx < 8; ++idx) {
                            int slot = layout_order[idx];
                            int col = idx % 2;
                            int row = idx / 2;
                            float x = (col == 0) ? left_col_x : right_col_x;
                            float y = start_y + (float)row * (tile_h + gap_y);

                            if (mx >= x && mx <= x + tile_w &&
                                my >= y && my <= y + tile_h) {
                                state.selected_mouse_slot = slot;
                                state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                state.selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                                state.selected_keyboard_slot = -1;
                                state.mapping_from_trigger = false;
                                state.dialog_read_only = false;
                                state.active_slot = 0;
                                state.active_tab = 1;

                                if (slot == UI_MOUSE_SLOT_MOVE) {
                                    UI_LoadMouseMoveState(&state);
                                    state.dialog_focus_index = 0;
                                    state.active_dialog = DIALOG_MOUSE_MOVE_CONFIG;
                                } else {
                                    state.dialog_focus_index = 0;
                                    state.active_dialog = DIALOG_BUTTON_OPTIONS;
                                }
                                break;
                            }
                        }
                    }
                    else if (device_type == UI_DEVICE_TYPE_KEYBOARD) {
                        /* Keyboard device: hit test full keyboard layout */
                        float key_unit = ScaleX(130.0f, w);
                        float gap = ScaleX(8.0f, w);
                        float min_x;
                        float max_x;
                        float min_y;
                        float max_y;
                        UI_ComputeKeyboardLayoutBounds(key_unit, gap, &min_x, &max_x, &min_y, &max_y);

                        float layout_center_x = (min_x + max_x) * 0.5f;
                        float layout_center_y = (min_y + max_y) * 0.5f;

                        float target_center_x = (float)w * 0.5f;
                        float target_center_y = (float)h * 0.5f;

                        float start_x = target_center_x - layout_center_x;
                        float start_y = target_center_y - layout_center_y;

                        for (int i = 0; i < uk_qwerty_layout_count; ++i) {
                            const KeyPosition *kp = &uk_qwerty_layout[i];

                            float x = start_x + kp->col * (key_unit + gap);
                            float y = start_y + kp->row * (key_unit + gap);
                            float w_key = kp->width * key_unit + (kp->width - 1.0f) * gap;
                            float h_key = key_unit;

                            /* Handle multi-row keys */
                            if (kp->scancode == SDL_SCANCODE_KP_ENTER && i + 1 < uk_qwerty_layout_count) {
                                h_key = key_unit * 2.0f + gap;
                            } else if (kp->scancode == SDL_SCANCODE_KP_PLUS && i - 1 >= 0) {
                                h_key = key_unit * 2.0f + gap;
                            }

                            if (mx >= x && mx <= x + w_key &&
                                my >= y && my <= y + h_key) {
                                state.selected_keyboard_slot = (int)kp->scancode;
                                state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                state.selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                                state.selected_mouse_slot = -1;  /* Clear mouse slot */
                                state.mapping_from_trigger = false;
                                state.dialog_read_only = false;
                                state.active_slot = 0;
                                state.active_tab = 2; /* Keyboard tab by default */
                                state.dialog_focus_index = 0;
                                state.active_dialog = DIALOG_BUTTON_OPTIONS;
                                break;
                            }
                        }
                    }
                    /* Controller, trigger, and stick-move clicks */
                    else {
                        /* First check triggers (LT/RT) */
                        float bw = ScaleX(150, w);
                        float bh = ScaleY(80, h);
                        float lt_bx = ScaleX(LT_X, w);
                        float lt_by = ScaleY(LT_Y, h);
                        float rt_bx = ScaleX(RT_X, w);
                        float rt_by = ScaleY(RT_Y, h);

                        if (mx >= lt_bx && mx <= lt_bx + bw && my >= lt_by && my <= lt_by + bh) {
                            state.selected_axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                            state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                            state.selected_keyboard_slot = -1;
                            state.selected_mouse_slot = -1;
                            state.active_slot = 0;
                            state.dialog_read_only = false;
                            state.dialog_focus_index = 0;
                            state.active_dialog = DIALOG_TRIGGER_OPTIONS;
                        }
                        else if (mx >= rt_bx && mx <= rt_bx + bw && my >= rt_by && my <= rt_by + bh) {
                            state.selected_axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                            state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                            state.selected_keyboard_slot = -1;
                            state.selected_mouse_slot = -1;
                            state.active_slot = 0;
                            state.dialog_read_only = false;
                            state.dialog_focus_index = 0;
                            state.active_dialog = DIALOG_TRIGGER_OPTIONS;
                        }
                        else {
                            /* Then check left/right stick movement regions */
                            float sm_bw = ScaleX(200, w);
                            float sm_bh = ScaleY(80, h);
                            float ls_bx = ScaleX(LS_MOVE_X, w);
                            float ls_by = ScaleY(LS_MOVE_Y, h);
                            float rs_bx = ScaleX(RS_MOVE_X, w);
                            float rs_by = ScaleY(RS_MOVE_Y, h);

                            if (mx >= ls_bx && mx <= ls_bx + sm_bw && my >= ls_by && my <= ls_by + sm_bh) {
                                state.selected_axis = SDL_GAMEPAD_AXIS_LEFTX;
                                state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                state.selected_keyboard_slot = -1;
                                state.selected_mouse_slot = -1;
                                UI_LoadStickStateFromAxis(SDL_GAMEPAD_AXIS_LEFTX, &state);
                                state.dialog_focus_index = 0;
                                state.active_dialog = DIALOG_STICK_CONFIG;
                            }
                            else if (mx >= rs_bx && mx <= rs_bx + sm_bw && my >= rs_by && my <= rs_by + sm_bh) {
                                state.selected_axis = SDL_GAMEPAD_AXIS_RIGHTX;
                                state.selected_button = SDL_GAMEPAD_BUTTON_INVALID;
                                state.selected_keyboard_slot = -1;
                                state.selected_mouse_slot = -1;
                                UI_LoadStickStateFromAxis(SDL_GAMEPAD_AXIS_RIGHTX, &state);
                                state.dialog_focus_index = 0;
                                state.active_dialog = DIALOG_STICK_CONFIG;
                            }
                            else {
                                /* Regular face / bumper / d-pad / stick buttons */
                                for (int i = 0; i < (int)(sizeof(remapping_buttons) / sizeof(remapping_buttons[0])); i++) {
                                    const RemappingButton *btn = &remapping_buttons[i];
                                    float bx = ScaleX(btn->x, w);
                                    float by = ScaleY(btn->y, h);
                                    float bw_btn = ScaleX(150, w);
                                    float bh_btn = ScaleY(80, h);

                                    if (mx >= bx && mx <= bx + bw_btn && my >= by && my <= by + bh_btn) {
                                        state.selected_button = btn->button;
                                        state.selected_axis = SDL_GAMEPAD_AXIS_INVALID;
                                        state.selected_keyboard_slot = -1;  /* Clear keyboard slot */
                                        state.selected_mouse_slot = -1;     /* Clear mouse slot */
                                        state.dialog_read_only = false;
                                        state.dialog_focus_index = 0;
                                        state.active_dialog = DIALOG_BUTTON_OPTIONS;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                else if (state.active_dialog == DIALOG_BUTTON_OPTIONS) {
                    float dw = 400, dh = 400;
                    float dx = (w - dw) / 2;
                    float dy = (h - dh) / 2;
                    if (state.dialog_read_only) {
                        /* In read-only mode, only allow Cancel */
                        if (mx >= dx + dw - 120 && mx <= dx + dw - 20 && my >= dy + dh - 60 && my <= dy + dh - 20) {
                            state.active_dialog = DIALOG_NONE;
                        }
                    } else {
                        /* Primary button */
                        if (mx >= dx + 30 && mx <= dx + 170 && my >= dy + 90 && my <= dy + 140) {
                            state.active_slot = 0;
                            state.mapping_from_trigger = false;
                            state.active_dialog = DIALOG_MAPPING_SELECT;
                        }
                        /* Hold button */
                        else if (mx >= dx + 30 && mx <= dx + 170 && my >= dy + 160 && my <= dy + 210) {
                            state.active_slot = 2;
                            state.mapping_from_trigger = false;
                            state.active_dialog = DIALOG_MAPPING_SELECT;
                        }
                        /* Shift button */
                        else if (mx >= dx + 30 && mx <= dx + 170 && my >= dy + 230 && my <= dy + 280) {
                            state.active_slot = 1;
                            state.mapping_from_trigger = false;
                            state.active_dialog = DIALOG_MAPPING_SELECT;
                        }
                        /* Use as Shift checkbox */
                        else if (mx >= dx + 30 && mx <= dx + 46 && my >= dy + 300 && my <= dy + 316) {
                            int p = state.selected_profile;
                            if (p < 0) {
                                p = 0;
                            } else if (p >= SDL_UI_MAX_PROFILES) {
                                p = SDL_UI_MAX_PROFILES - 1;
                            }

                            SDL_RemapperMapping *mapping = NULL;
                            if (state.selected_button != SDL_GAMEPAD_BUTTON_INVALID) {
                                mapping = UI_GetMappingForButtonInProfile(state.selected_button, p);
                            } else if (state.selected_keyboard_slot >= 0 &&
                                       state.selected_keyboard_slot < UI_KEYBOARD_SLOT_COUNT) {
                                mapping = UI_GetKeyboardSlotMappingInProfile(
                                    (UI_KeyboardSlot)state.selected_keyboard_slot, p);
                            } else if (state.selected_mouse_slot >= 0 &&
                                       state.selected_mouse_slot < UI_MOUSE_SLOT_COUNT) {
                                mapping = UI_GetMouseSlotMappingInProfile(
                                    (UI_MouseSlot)state.selected_mouse_slot, p);
                            }

                            if (mapping) {
                                mapping->use_as_shift = !mapping->use_as_shift;
                                UI_CommitProfileToContext(ctx, active_gamepad_id, &state);
                                UI_SaveCurrentProfileToDisk(&state);
                            }
                        }
                        /* Cancel */
                        else if (mx >= dx + dw - 120 && mx <= dx + dw - 20 && my >= dy + dh - 60 && my <= dy + dh - 20) {
                            state.active_dialog = DIALOG_NONE;
                        }
                    }
                }
                else if (state.active_dialog == DIALOG_NEW_PROFILE ||
                         state.active_dialog == DIALOG_RENAME_PROFILE) {
                    float dw = 500.0f, dh = 200.0f;
                    float dx = (w - dw) / 2.0f;
                    float dy = (h - dh) / 2.0f;

                    /* Text field hit test */
                    float box_x = dx + 30.0f;
                    float box_y = dy + 80.0f;
                    float box_w = dw - 60.0f;
                    float box_h = 36.0f;

                    float ok_x = dx + dw - 220.0f;
                    float ok_y = dy + dh - 60.0f;
                    float cancel_x = dx + dw - 120.0f;
                    float cancel_y = dy + dh - 60.0f;

                    if (mx >= box_x && mx <= box_x + box_w &&
                        my >= box_y && my <= box_y + box_h) {
                        /* Click on text field - focus it and start text input */
                        state.dialog_focus_index = 0;
                        state.show_osk = true;
                    } else if (mx >= ok_x && mx <= ok_x + 90.0f &&
                        my >= ok_y && my <= ok_y + 40.0f) {
                        state.dialog_focus_index = 1;
                        UI_HandleGamepadNavButton(ctx,
                                                  active_gamepad_id,
                                                  SDL_GAMEPAD_BUTTON_SOUTH,
                                                  &state,
                                                  &done);
                    } else if (mx >= cancel_x && mx <= cancel_x + 100.0f &&
                               my >= cancel_y && my <= cancel_y + 40.0f) {
                        state.dialog_focus_index = 2;
                        UI_HandleGamepadNavButton(ctx,
                                                  active_gamepad_id,
                                                  SDL_GAMEPAD_BUTTON_SOUTH,
                                                  &state,
                                                  &done);
                    }
                }
                else if (state.active_dialog == DIALOG_DELETE_CONFIRM) {
                    float dw = 500.0f, dh = 220.0f;
                    float dx = (w - dw) / 2.0f;
                    float dy = (h - dh) / 2.0f;

                    float yes_x = dx + dw - 220.0f;
                    float yes_y = dy + dh - 60.0f;
                    float no_x = dx + dw - 120.0f;
                    float no_y = dy + dh - 60.0f;

                    if (mx >= yes_x && mx <= yes_x + 90.0f &&
                        my >= yes_y && my <= yes_y + 40.0f) {
                        state.dialog_focus_index = 0;
                        UI_HandleGamepadNavButton(ctx,
                                                  active_gamepad_id,
                                                  SDL_GAMEPAD_BUTTON_SOUTH,
                                                  &state,
                                                  &done);
                    } else if (mx >= no_x && mx <= no_x + 100.0f &&
                               my >= no_y && my <= no_y + 40.0f) {
                        state.dialog_focus_index = 1;
                        UI_HandleGamepadNavButton(ctx,
                                                  active_gamepad_id,
                                                  SDL_GAMEPAD_BUTTON_SOUTH,
                                                  &state,
                                                  &done);
                    }
                }
                else if (state.active_dialog == DIALOG_VIRTUAL_KEYBOARD) {
                    /* Virtual keyboard mouse click handling */
                    float dw = 580.0f, dh = 310.0f;
                    float dx = (w - dw) / 2.0f;
                    float dy = (h - dh) / 2.0f;

                    float key_w = 50.0f;
                    float key_h = 50.0f;
                    float gap = 4.0f;
                    float start_x = dx + 20.0f;
                    float start_y = dy + 95.0f;

                    /* Check which key was clicked */
                    for (int row = 0; row < 4; row++) {
                        for (int col = 0; col < 10; col++) {
                            float kx = start_x + col * (key_w + gap);
                            float ky = start_y + row * (key_h + gap);

                            if (mx >= kx && mx <= kx + key_w &&
                                my >= ky && my <= ky + key_h) {
                                /* Found the clicked key */
                                state.vk_row = row;
                                state.vk_col = col;
                                /* Trigger key press via gamepad handler */
                                UI_HandleGamepadNavButton(ctx,
                                                          active_gamepad_id,
                                                          SDL_GAMEPAD_BUTTON_SOUTH,
                                                          &state,
                                                          &done);
                            }
                        }
                    }
                }
                else if (state.active_dialog == DIALOG_TRIGGER_OPTIONS) {
                    float dw = 500.0f, dh = 350.0f;
                    float dx = (w - dw) / 2.0f;
                    float dy = (h - dh) / 2.0f;

                    if (state.dialog_read_only) {
                        /* Read-only: only allow Cancel */
                        if (mx >= dx + dw - 120.0f && mx <= dx + dw - 20.0f &&
                            my >= dy + dh - 60.0f && my <= dy + dh - 20.0f) {
                            state.active_dialog = DIALOG_NONE;
                        }
                    } else {
                        /* Primary mapping button */
                        if (mx >= dx + 30.0f && mx <= dx + 170.0f && my >= dy + 80.0f && my <= dy + 120.0f) {
                            state.active_slot = 0;
                            state.mapping_from_trigger = true;
                            state.active_dialog = DIALOG_MAPPING_SELECT;
                        }
                        /* Shift mapping button */
                        else if (mx >= dx + 30.0f && mx <= dx + 170.0f && my >= dy + 150.0f && my <= dy + 190.0f) {
                            state.active_slot = 1;
                            state.mapping_from_trigger = true;
                            state.active_dialog = DIALOG_MAPPING_SELECT;
                        }
                        else {
                            /* Deadzone slider */
                            float slider_x = dx + 40.0f;
                            float slider_y = dy + 240.0f;
                            float slider_w = dw - 80.0f;
                            float slider_h = 20.0f;

                            if (mx >= slider_x && mx <= slider_x + slider_w &&
                                my >= slider_y && my <= slider_y + slider_h) {
                                float t = (mx - slider_x) / slider_w;
                                if (t < 0.0f) t = 0.0f;
                                if (t > 1.0f) t = 1.0f;
                                float value = 1.0f + t * 99.0f;

                                int p = state.selected_profile;
                                if (p < 0) {
                                    p = 0;
                                } else if (p >= SDL_UI_MAX_PROFILES) {
                                    p = SDL_UI_MAX_PROFILES - 1;
                                }

                                if (state.selected_axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                                    state.trigger_deadzone_left = value;
                                    g_ui_profile_trigger_deadzone_left[p] = value;
                                } else if (state.selected_axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                                    state.trigger_deadzone_right = value;
                                    g_ui_profile_trigger_deadzone_right[p] = value;
                                }

                                UI_CommitProfileToContext(ctx, active_gamepad_id, &state);
                                UI_SaveCurrentProfileToDisk(&state);
                            }
                            /* Cancel */
                            else if (mx >= dx + dw - 120.0f && mx <= dx + dw - 20.0f &&
                                     my >= dy + dh - 60.0f && my <= dy + dh - 20.0f) {
                                state.active_dialog = DIALOG_NONE;
                            }
                        }
                    }
                }
                else if (state.active_dialog == DIALOG_STICK_CONFIG ||
                         state.active_dialog == DIALOG_MOUSE_MOVE_CONFIG) {
                    float dw = 600.0f;
                    float row_h = 32.0f;

                    /* Calculate dynamic dialog height (same as drawing) */
                    float extra_height = 0.0f;
                    if (state.stick_controller) extra_height += row_h;
                    if (state.stick_gyro) extra_height += row_h;
                    if (state.stick_touch_mouse) extra_height += row_h;
                    float dh = 660.0f + extra_height;

                    float dx = (w - dw) / 2.0f;
                    float dy = (h - dh) / 2.0f;

                    float cb_x = dx + 40.0f;
                    float cb_size = 18.0f;

                    if (state.dialog_read_only) {
                        /* In read-only mode, ignore checkboxes/sliders and only handle Cancel */
                        if (mx >= dx + dw - 120.0f && mx <= dx + dw - 20.0f &&
                            my >= dy + dh - 60.0f && my <= dy + dh - 20.0f) {
                            state.active_dialog = DIALOG_NONE;
                        }
                    } else {
                        bool handled = false;
                        float current_y = dy + 90.0f;

                        /* Row 0-5: WASD, Arrows, Mouse, Controller, D-Pad, Gyroscope */
                        for (int i = 0; i < 4 && !handled; i++) {
                            if (mx >= cb_x && mx <= cb_x + cb_size &&
                                my >= current_y && my <= current_y + cb_size) {
                                state.stick_wasd = (i == 0);
                                state.stick_arrows = (i == 1);
                                state.stick_mouse = (i == 2);
                                state.stick_controller = (i == 3);
                                /* Set default target based on which stick we're configuring */
                                if (i == 3) {
                                    if (state.selected_axis == SDL_GAMEPAD_AXIS_LEFTX || state.selected_axis == SDL_GAMEPAD_AXIS_LEFTY) {
                                        state.stick_controller_target = 0;  /* Default to left stick */
                                    } else {
                                        state.stick_controller_target = 1;  /* Default to right stick */
                                    }
                                }
                                state.stick_dpad = (i == 4);
                                state.stick_gyro = (i == 5);
                                state.stick_touch_mouse = false;
                                handled = true;
                            }
                            current_y += row_h;
                        }
                        /* Controller stick toggle row (if controller stick enabled) */
                        if (state.stick_controller) {
                            if (!handled) {
                                float toggle_indent = 30.0f;
                                float toggle_w = 44.0f;
                                float toggle_h = 20.0f;
                                float toggle_x = cb_x + toggle_indent;
                                float toggle_y = current_y + 2.0f;

                                if (mx >= toggle_x && mx <= toggle_x + toggle_w &&
                                    my >= toggle_y && my <= toggle_y + toggle_h) {
                                    /* Toggle between left stick (0) and right stick (1) */
                                    state.stick_controller_target = (state.stick_controller_target == 0) ? 1 : 0;

                                    /* Immediately update the other stick's target if it conflicts */
                                    SDL_GamepadAxis other_axis = SDL_GAMEPAD_AXIS_INVALID;
                                    if (state.selected_axis == SDL_GAMEPAD_AXIS_LEFTX || state.selected_axis == SDL_GAMEPAD_AXIS_LEFTY) {
                                        other_axis = SDL_GAMEPAD_AXIS_RIGHTX;
                                    } else if (state.selected_axis == SDL_GAMEPAD_AXIS_RIGHTX || state.selected_axis == SDL_GAMEPAD_AXIS_RIGHTY) {
                                        other_axis = SDL_GAMEPAD_AXIS_LEFTX;
                                    }
                                    if (other_axis != SDL_GAMEPAD_AXIS_INVALID) {
                                        SDL_RemapperMapping *other_mapping = UI_GetMappingForAxisInProfile(other_axis, state.selected_profile);
                                        if (other_mapping && other_mapping->stick_mapping) {
                                            SDL_RemapperStickMapping *other_stick = other_mapping->stick_mapping;
                                            if (other_stick->map_to_controller_movement) {
                                                if (other_stick->controller_target_stick == state.stick_controller_target) {
                                                    other_stick->controller_target_stick = (state.stick_controller_target == 0) ? 1 : 0;
                                                }
                                            }
                                        }
                                    }

                                    handled = true;
                                }
                            }
                            /* Always increment y when controller mode is enabled (row takes space) */
                            current_y += row_h;
                        }

                        /* Row 4-5: D-Pad, Gyroscope */
                        for (int i = 4; i < 6 && !handled; i++) {
                            if (mx >= cb_x && mx <= cb_x + cb_size &&
                                my >= current_y && my <= current_y + cb_size) {
                                state.stick_wasd = false;
                                state.stick_arrows = false;
                                state.stick_mouse = false;
                                state.stick_controller = false;
                                state.stick_dpad = (i == 4);
                                state.stick_gyro = (i == 5);
                                state.stick_touch_mouse = false;
                                handled = true;
                            }
                            current_y += row_h;
                        }

                        /* Gyro mode toggle row (if gyro enabled) */
                        if (!handled && state.stick_gyro) {
                            float toggle_indent = 30.0f;
                            float toggle_w = 44.0f;
                            float toggle_h = 20.0f;
                            float toggle_x = cb_x + toggle_indent;
                            float toggle_y = current_y + 2.0f;

                            if (mx >= toggle_x && mx <= toggle_x + toggle_w &&
                                my >= toggle_y && my <= toggle_y + toggle_h) {
                                state.stick_gyro_mode_roll = !state.stick_gyro_mode_roll;
                                handled = true;
                            }
                            current_y += row_h;
                        }

                        /* Row 6: Touch Mouse */
                        if (!handled) {
                            if (mx >= cb_x && mx <= cb_x + cb_size &&
                                my >= current_y && my <= current_y + cb_size) {
                                state.stick_wasd = false;
                                state.stick_arrows = false;
                                state.stick_mouse = false;
                                state.stick_controller = false;
                                state.stick_dpad = false;
                                state.stick_gyro = false;
                                state.stick_touch_mouse = true;

                                /* Default finger based on other stick */
                                SDL_GamepadAxis other_axis = SDL_GAMEPAD_AXIS_INVALID;
                                if (state.selected_axis == SDL_GAMEPAD_AXIS_LEFTX || state.selected_axis == SDL_GAMEPAD_AXIS_LEFTY) {
                                    other_axis = SDL_GAMEPAD_AXIS_RIGHTX;
                                } else if (state.selected_axis == SDL_GAMEPAD_AXIS_RIGHTX || state.selected_axis == SDL_GAMEPAD_AXIS_RIGHTY) {
                                    other_axis = SDL_GAMEPAD_AXIS_LEFTX;
                                }
                                int other_finger = 0;
                                if (other_axis != SDL_GAMEPAD_AXIS_INVALID) {
                                    SDL_RemapperMapping *other_mapping = UI_GetMappingForAxisInProfile(other_axis, state.selected_profile);
                                    if (other_mapping && other_mapping->stick_mapping) {
                                        SDL_RemapperStickMapping *other_stick = other_mapping->stick_mapping;
                                        if (other_stick->map_to_touch_mouse) {
                                            other_finger = other_stick->touch_finger;
                                        }
                                    }
                                }
                                state.stick_touch_finger = (other_finger == 1) ? 2 : 1;
                                handled = true;
                            }
                            current_y += row_h;
                        }

                        /* Touch finger toggle row (if touch enabled) */
                        if (!handled && state.stick_touch_mouse) {
                            float toggle_indent = 30.0f;
                            float toggle_w = 44.0f;
                            float toggle_h = 20.0f;
                            float toggle_x = cb_x + toggle_indent;
                            float toggle_y = current_y + 2.0f;

                            if (mx >= toggle_x && mx <= toggle_x + toggle_w &&
                                my >= toggle_y && my <= toggle_y + toggle_h) {
                                /* Toggle between finger 1 and 2 */
                                state.stick_touch_finger = (state.stick_touch_finger == 1) ? 2 : 1;
                                handled = true;
                            }
                            current_y += row_h;
                        }

                        /* Rows 7-8: Invert Horizontal, Invert Vertical */
                        if (!handled) {
                            if (mx >= cb_x && mx <= cb_x + cb_size &&
                                my >= current_y && my <= current_y + cb_size) {
                                state.stick_invert_x = !state.stick_invert_x;
                                handled = true;
                            }
                            current_y += row_h;
                        }
                        if (!handled) {
                            if (mx >= cb_x && mx <= cb_x + cb_size &&
                                my >= current_y && my <= current_y + cb_size) {
                                state.stick_invert_y = !state.stick_invert_y;
                                handled = true;
                            }
                            current_y += row_h;
                        }

                        if (!handled) {
                            /* Sliders: same layout as in DrawStickConfigDialog */
                            float slider_x = dx + 40.0f;
                            float slider_w = dw - 80.0f;
                            float slider_h = 12.0f;
                            float first_slider_y = current_y + 34.0f;

                            float *values[4];
                            int num_sliders;
                            if (state.stick_gyro) {
                                values[0] = &state.stick_gyro_h_sens;
                                values[1] = &state.stick_gyro_v_sens;
                                values[2] = &state.stick_gyro_accel;
                                values[3] = &state.stick_gyro_accel; /* unused */
                                num_sliders = 3;
                            } else {
                                values[0] = &state.stick_h_sens;
                                values[1] = &state.stick_v_sens;
                                values[2] = &state.stick_h_accel;
                                values[3] = &state.stick_v_accel;
                                num_sliders = 4;
                            }

                            bool slider_handled = false;
                            for (int i = 0; i < num_sliders && !slider_handled; i++) {
                                float y = first_slider_y + 48.0f * (float)i;
                                if (mx >= slider_x && mx <= slider_x + slider_w &&
                                    my >= y && my <= y + slider_h + 16.0f) {
                                    float t = (mx - slider_x) / slider_w;
                                    if (t < 0.0f) t = 0.0f;
                                    if (t > 1.0f) t = 1.0f;
                                    *values[i] = t * 100.0f - 50.0f;
                                    slider_handled = true;
                                }
                            }

                            if (!slider_handled) {
                                /* OK button */
                                if (mx >= dx + dw - 220.0f && mx <= dx + dw - 140.0f &&
                                    my >= dy + dh - 60.0f && my <= dy + dh - 20.0f) {
                                    if (state.active_dialog == DIALOG_STICK_CONFIG) {
                                        SDL_GamepadAxis axis = state.selected_axis;
                                        SDL_GamepadAxis canonical_axis = axis;
                                        if (axis == SDL_GAMEPAD_AXIS_LEFTY) {
                                            canonical_axis = SDL_GAMEPAD_AXIS_LEFTX;
                                        } else if (axis == SDL_GAMEPAD_AXIS_RIGHTY) {
                                            canonical_axis = SDL_GAMEPAD_AXIS_RIGHTX;
                                        }
                                        UI_SaveStickStateToAxis(ctx, active_gamepad_id, canonical_axis, &state);
                                    } else if (state.active_dialog == DIALOG_MOUSE_MOVE_CONFIG) {
                                        UI_SaveMouseMoveState(ctx, active_gamepad_id, &state);
                                    }
                                    state.active_dialog = DIALOG_NONE;
                                }
                                /* Cancel */
                                else if (mx >= dx + dw - 120.0f && mx <= dx + dw - 20.0f &&
                                         my >= dy + dh - 60.0f && my <= dy + dh - 20.0f) {
                                    state.active_dialog = DIALOG_NONE;
                                }
                            }
                        }
                    }
                }
                else if (state.active_dialog == DIALOG_MAPPING_SELECT) {
                    float dw = 550, dh = 500;
                    float dx = (w - dw) / 2;
                    float dy = (h - dh) / 2;

                    const MappingOption *options = NULL;
                    int option_count = 0;
                    UI_GetActiveOptions(&state, &options, &option_count);

                    const int item_height = 50;
                    const int visible_rows = 5;

                    /* Tab clicks */
                    for (int i = 0; i < 4; i++) {
                        if (mx >= dx + 20 + i * 130 && mx <= dx + 140 + i * 130 &&
                            my >= dy + 80 && my <= dy + 120) {
                            state.active_tab = i;
                            /* Reset selection and scroll when switching tab */
                            state.list_selection = 0;
                            state.list_scroll = 0;
                            break;
                        }
                    }

                    if (option_count > 0) {
                        int max_scroll = (option_count > visible_rows) ? (option_count - visible_rows) : 0;
                        if (state.list_scroll < 0) {
                            state.list_scroll = 0;
                        } else if (state.list_scroll > max_scroll) {
                            state.list_scroll = max_scroll;
                        }

                        /* List item clicks within the visible window */
                        float list_top = dy + 150.0f;
                        for (int row = 0; row < visible_rows; row++) {
                            int idx = state.list_scroll + row;
                            if (idx >= option_count) {
                                break;
                            }

                            float item_y = list_top + (float)row * (float)item_height;
                            if (mx >= dx + 30 && mx <= dx + dw - 30 &&
                                my >= item_y && my <= item_y + 40) {
                                state.list_selection = idx;
                                break;
                            }
                        }
                    }

                    /* OK button */
                    if (mx >= dx + dw - 220 && mx <= dx + dw - 130 && my >= dy + dh - 60 && my <= dy + dh - 20) {
                        if (option_count > 0) {
                            SDL_RemapperMapping *mapping = NULL;
                            if (state.selected_button != SDL_GAMEPAD_BUTTON_INVALID) {
                                mapping = UI_GetMappingForButton(state.selected_button);
                            } else if (state.selected_axis != SDL_GAMEPAD_AXIS_INVALID) {
                                mapping = UI_GetMappingForAxis(state.selected_axis);
                            } else if (state.selected_mouse_slot >= 0 && state.selected_mouse_slot < UI_MOUSE_SLOT_COUNT) {
                                mapping = UI_GetMouseSlotMapping((UI_MouseSlot)state.selected_mouse_slot);
                            } else if (state.selected_keyboard_slot >= 0 && state.selected_keyboard_slot < UI_KEYBOARD_SLOT_COUNT) {
                                mapping = UI_GetKeyboardSlotMapping((UI_KeyboardSlot)state.selected_keyboard_slot);
                            }

                            if (mapping) {
                                int sel = state.list_selection;
                                if (sel < 0 || sel >= option_count) {
                                    sel = 0;
                                }
                                UI_ApplyMappingToSlot(ctx,
                                                      active_gamepad_id,
                                                      mapping,
                                                      state.active_slot,
                                                      &options[sel],
                                                      &state);
                            }
                        }
                        if (state.mapping_from_trigger && state.selected_axis != SDL_GAMEPAD_AXIS_INVALID) {
                            /* Return to trigger options loop for further configuration */
                            state.active_dialog = DIALOG_TRIGGER_OPTIONS;
                        } else {
                            state.active_dialog = DIALOG_NONE;
                        }
                    }
                    /* Cancel */
                    else if (mx >= dx + dw - 120 && mx <= dx + dw - 20 && my >= dy + dh - 60 && my <= dy + dh - 20) {
                        state.active_dialog = DIALOG_NONE;
                    }
                }
            }
        }

        /* Handle on-screen keyboard request - open virtual keyboard dialog */
        if (state.show_osk) {
            state.show_osk = false;
            state.vk_row = 1;  /* Start at 'Q' row */
            state.vk_col = 0;
            state.active_dialog = DIALOG_VIRTUAL_KEYBOARD;
        }

        /* Stop text input when dialog is closed */
        if (state.active_dialog != DIALOG_NEW_PROFILE &&
            state.active_dialog != DIALOG_RENAME_PROFILE) {
            if (SDL_TextInputActive(window)) {
                SDL_StopTextInput(window);
            }
        }

        /* Rendering */
        SDL_SetRenderDrawColor(renderer, 33, 33, 33, 255);
        SDL_RenderClear(renderer);

        /* Draw current page */
        if (state.current_page == PAGE_DEVICE_SELECT) {
            DrawDeviceSelectPage(renderer, &state, w, h);
        } else if (state.current_page == PAGE_PROFILE_SELECT) {
            DrawProfileSelectPage(renderer, &state, w, h);
        } else {
            DrawButtonMappingPage(renderer, &state, w, h);
        }

        /* Draw active dialog */
        if (state.active_dialog == DIALOG_BUTTON_OPTIONS) {
            DrawButtonOptionsDialog(renderer, &state, w, h);
        } else if (state.active_dialog == DIALOG_TRIGGER_OPTIONS) {
            DrawTriggerOptionsDialog(renderer, &state, w, h);
        } else if (state.active_dialog == DIALOG_STICK_CONFIG ||
                   state.active_dialog == DIALOG_MOUSE_MOVE_CONFIG) {
            DrawStickConfigDialog(renderer, &state, w, h);
        } else if (state.active_dialog == DIALOG_MAPPING_SELECT) {
            DrawMappingSelectDialog(renderer, &state, w, h);
        } else if (state.active_dialog == DIALOG_NEW_PROFILE ||
                   state.active_dialog == DIALOG_RENAME_PROFILE) {
            const char *title = (state.active_dialog == DIALOG_NEW_PROFILE) ? "New Profile" : "Rename Profile";
            DrawTextInputDialog(renderer, &state, w, h, title);
        } else if (state.active_dialog == DIALOG_DELETE_CONFIRM) {
            DrawDeleteConfirmDialog(renderer, &state, w, h);
        } else if (state.active_dialog == DIALOG_VIRTUAL_KEYBOARD) {
            DrawVirtualKeyboardDialog(renderer, &state, w, h);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    /* Destroy page-specific background textures */
    if (g_controller_device_bg) { SDL_DestroyTexture(g_controller_device_bg); g_controller_device_bg = NULL; }
    if (g_controller_profile_bg) { SDL_DestroyTexture(g_controller_profile_bg); g_controller_profile_bg = NULL; }
    if (g_controller_remapper_bg) { SDL_DestroyTexture(g_controller_remapper_bg); g_controller_remapper_bg = NULL; }
    if (g_mouse_device_bg) { SDL_DestroyTexture(g_mouse_device_bg); g_mouse_device_bg = NULL; }
    if (g_mouse_profile_bg) { SDL_DestroyTexture(g_mouse_profile_bg); g_mouse_profile_bg = NULL; }
    if (g_mouse_remapper_bg) { SDL_DestroyTexture(g_mouse_remapper_bg); g_mouse_remapper_bg = NULL; }
    if (g_keyboard_device_bg) { SDL_DestroyTexture(g_keyboard_device_bg); g_keyboard_device_bg = NULL; }

    /* Destroy all icon textures */
    for (int i = 0; i < SDL_UI_ARRAY_SIZE(g_all_icons); ++i) {
        if (g_all_icons[i].texture) {
            SDL_DestroyTexture(g_all_icons[i].texture);
            g_all_icons[i].texture = NULL;
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    return 0;
}
