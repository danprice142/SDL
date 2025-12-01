/* SDL3 Controller Remapper public API (work in progress) */

#ifndef SDL_remapper_h_
#define SDL_remapper_h_

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_events.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque context that owns remapping state */
typedef struct SDL_RemapperContext SDL_RemapperContext;

/* What kind of action a mapping produces */
typedef enum SDL_RemapperActionKind
{
    SDL_REMAPPER_ACTION_NONE = 0,
    SDL_REMAPPER_ACTION_GAMEPAD_BUTTON,
    SDL_REMAPPER_ACTION_GAMEPAD_AXIS,
    SDL_REMAPPER_ACTION_KEYBOARD_KEY,
    SDL_REMAPPER_ACTION_MOUSE_BUTTON,
    SDL_REMAPPER_ACTION_MOUSE_WHEEL,
    SDL_REMAPPER_ACTION_MOUSE_MOVEMENT,
    /* Touch actions */
    SDL_REMAPPER_ACTION_TOUCH_TAP,          /* Finger down on press, up on release */
    SDL_REMAPPER_ACTION_TOUCH_HOLD,         /* Toggle finger down state */
    SDL_REMAPPER_ACTION_TOUCH_DOUBLE_TAP,   /* Two quick taps */
    SDL_REMAPPER_ACTION_TOUCH_SWIPE_UP,     /* Quick swipe upward */
    SDL_REMAPPER_ACTION_TOUCH_SWIPE_DOWN,   /* Quick swipe downward */
    SDL_REMAPPER_ACTION_TOUCH_SWIPE_LEFT,   /* Quick swipe left */
    SDL_REMAPPER_ACTION_TOUCH_SWIPE_RIGHT,  /* Quick swipe right */
    SDL_REMAPPER_ACTION_TOUCH_FINGER2_TAP,  /* Second finger tap (for multi-touch) */
    SDL_REMAPPER_ACTION_TOUCH_FINGER2_HOLD, /* Toggle second finger state */
    SDL_REMAPPER_ACTION_TOUCH_PINCH_IN,     /* Two fingers move together */
    SDL_REMAPPER_ACTION_TOUCH_PINCH_OUT,    /* Two fingers move apart */
    SDL_REMAPPER_ACTION_TOUCH_ROTATE_CW,    /* Two fingers rotate clockwise */
    SDL_REMAPPER_ACTION_TOUCH_ROTATE_CCW    /* Two fingers rotate counter-clockwise */
} SDL_RemapperActionKind;

/* One logical action target */
typedef struct SDL_RemapperAction
{
    SDL_RemapperActionKind kind;
    int code; /* SDL_GamepadButton / SDL_GamepadAxis / SDL_Scancode / SDL_MouseButton, etc. */
    int value; /* Optional extra (e.g. wheel direction, axis magnitude scaling) */
} SDL_RemapperAction;

/* High-level button state used by the remapper */
typedef enum SDL_RemapperButtonState
{
    SDL_REMAPPER_BUTTON_RELEASED = 0,
    SDL_REMAPPER_BUTTON_PRESSED,
    SDL_REMAPPER_BUTTON_HELD
} SDL_RemapperButtonState;

/* Source type for mappings */
typedef enum SDL_RemapperSourceType
{
    SDL_REMAPPER_SOURCE_BUTTON = 0,
    SDL_REMAPPER_SOURCE_AXIS,
    SDL_REMAPPER_SOURCE_MOUSE_BUTTON,
    SDL_REMAPPER_SOURCE_MOUSE_WHEEL,
    SDL_REMAPPER_SOURCE_MOUSE_MOTION,
    SDL_REMAPPER_SOURCE_KEYBOARD_KEY
} SDL_RemapperSourceType;

/* Stick mapping modes for analog sticks */
typedef struct SDL_RemapperStickMapping
{
    bool map_to_wasd;
    bool map_to_arrow_keys;
    bool map_to_mouse_movement;
    bool map_to_controller_movement;
    int controller_target_stick;  /* 0=left stick, 1=right stick */
    bool map_to_dpad;
    bool map_to_gyroscope;      /* treat stick as a gyroscope-style input */
    bool map_to_touch_mouse;    /* treat stick as a touch-style mouse pointer */
    int touch_finger;           /* which finger for touch mode: 1=first, 2=second */
    bool invert_horizontal;
    bool invert_vertical;
    float horizontal_sensitivity;
    float vertical_sensitivity;
    float horizontal_acceleration;
    float vertical_acceleration;
    float gyro_horizontal_sensitivity;
    float gyro_vertical_sensitivity;
    float gyro_acceleration;
    bool gyro_mode_roll;          /* false = pitch/yaw, true = roll-only mode */
} SDL_RemapperStickMapping;

/* Per-source mapping configuration */
typedef struct SDL_RemapperMapping
{
    SDL_RemapperSourceType source_type;
    union {
        SDL_GamepadButton button;
        SDL_GamepadAxis axis;
    } source;

    bool use_as_shift;                 /* true if this source acts as a shift modifier */

    SDL_RemapperAction primary_action; /* normal press/motion */
    SDL_RemapperAction shift_action;   /* when any shift source is held */
    SDL_RemapperAction hold_action;    /* when held beyond threshold */

    SDL_RemapperStickMapping *stick_mapping; /* For axis sources (optional) */
} SDL_RemapperMapping;

/* Per-gamepad profile */
typedef struct SDL_RemapperProfile
{
    const char *name;                  /* optional profile name, not owned by SDL */
    SDL_JoystickID gamepad_id;         /* associated gamepad (or 0 / -1 for template) */

    int num_mappings;
    SDL_RemapperMapping *mappings;     /* caller-owned array for now */

    /* Optional per-profile trigger deadzones (1-100 scale). */
    float left_trigger_deadzone;
    float right_trigger_deadzone;
} SDL_RemapperProfile;

/* Create/destroy a remapper context */
extern SDL_DECLSPEC SDL_RemapperContext * SDLCALL SDL_CreateRemapper(void);
extern SDL_DECLSPEC void SDLCALL SDL_DestroyRemapper(SDL_RemapperContext *ctx);

/* Assign or update a profile for a given gamepad ID. Returns 0 on success, -1 on error. */
extern SDL_DECLSPEC int SDLCALL SDL_SetRemapperProfile(SDL_RemapperContext *ctx,
                                                      SDL_JoystickID gamepad_id,
                                                      const SDL_RemapperProfile *profile);

/* Assign or update a profile for a given mouse ID. Returns 0 on success, -1 on error. */
extern SDL_DECLSPEC int SDLCALL SDL_SetRemapperMouseProfile(SDL_RemapperContext *ctx,
                                                            SDL_MouseID mouse_id,
                                                            const SDL_RemapperProfile *profile);

/* Assign or update a profile for a given keyboard ID. Returns 0 on success, -1 on error. */
extern SDL_DECLSPEC int SDLCALL SDL_SetRemapperKeyboardProfile(SDL_RemapperContext *ctx,
                                                               SDL_KeyboardID keyboard_id,
                                                               const SDL_RemapperProfile *profile);

/*
 * Process one SDL_Event coming from SDL and emit zero or more remapped events.
 * For now this is a pass-through stub; later it will apply your mapping/shift/hold logic.
 *
 * Returns the number of valid events written to out_events (0..max_out_events).
 */
extern SDL_DECLSPEC int SDLCALL SDL_ProcessRemappedEvent(SDL_RemapperContext *ctx,
                                                        const SDL_Event *in_event,
                                                        SDL_Event *out_events,
                                                        int max_out_events);

/* Convenience wrapper around SDL_PollEvent that applies remapping.
 *
 * If ctx is NULL, this behaves exactly like SDL_PollEvent(event).
 * If ctx is non-NULL, this will poll raw SDL events, feed them through
 * SDL_ProcessRemappedEvent(), and return the first remapped event, possibly
 * swallowing the original physical controller event if it was transformed.
 */
extern SDL_DECLSPEC bool SDLCALL SDL_PollRemappedEvent(SDL_RemapperContext *ctx,
                                                      SDL_Event *event);

/* Show an SDL-rendered controller remapping window for the given gamepad. */
extern SDL_DECLSPEC int SDLCALL SDL_ShowGamepadRemappingWindow(SDL_RemapperContext *ctx,
                                                               SDL_JoystickID gamepad_id);

/* ===== Profile/Mapping Getters ===== */

/* Get the current profile for a gamepad. Returns NULL if no profile is set. */
extern SDL_DECLSPEC const SDL_RemapperProfile * SDLCALL SDL_GetRemapperProfile(SDL_RemapperContext *ctx,
                                                                                SDL_JoystickID gamepad_id);

/* Get the current profile name for a gamepad. Returns NULL if no profile or no name. */
extern SDL_DECLSPEC const char * SDLCALL SDL_GetRemapperProfileName(SDL_RemapperContext *ctx,
                                                                     SDL_JoystickID gamepad_id);

/* Get the mapping for a specific button on a gamepad. Returns NULL if not mapped. */
extern SDL_DECLSPEC const SDL_RemapperMapping * SDLCALL SDL_GetRemapperButtonMapping(SDL_RemapperContext *ctx,
                                                                                      SDL_JoystickID gamepad_id,
                                                                                      SDL_GamepadButton button);

/* Get the mapping for a specific axis on a gamepad. Returns NULL if not mapped. */
extern SDL_DECLSPEC const SDL_RemapperMapping * SDLCALL SDL_GetRemapperAxisMapping(SDL_RemapperContext *ctx,
                                                                                    SDL_JoystickID gamepad_id,
                                                                                    SDL_GamepadAxis axis);

/* Get the stick mapping configuration for an axis. Returns NULL if not configured. */
extern SDL_DECLSPEC const SDL_RemapperStickMapping * SDLCALL SDL_GetRemapperStickMapping(SDL_RemapperContext *ctx,
                                                                                          SDL_JoystickID gamepad_id,
                                                                                          SDL_GamepadAxis axis);

/* Get the player index assigned to a gamepad in the remapper. Returns -1 if not set. */
extern SDL_DECLSPEC int SDLCALL SDL_GetRemapperPlayerIndex(SDL_RemapperContext *ctx,
                                                           SDL_JoystickID gamepad_id);

/* Get a human-readable description of what an action will output. Caller must SDL_free() the result. */
extern SDL_DECLSPEC char * SDLCALL SDL_GetRemapperActionDescription(const SDL_RemapperAction *action);

/* Get a human-readable description of what a stick mapping will output. Caller must SDL_free() the result. */
extern SDL_DECLSPEC char * SDLCALL SDL_GetRemapperStickMappingDescription(const SDL_RemapperStickMapping *mapping);

/* Remapper default-profile helpers */

/* Create a "Gamepad Passthrough" profile that maps buttons to themselves. */
extern SDL_DECLSPEC SDL_RemapperProfile * SDLCALL SDL_CreateGamepadPassthroughProfile(SDL_JoystickID gamepad_id);

/* Create stick mapping for mouse movement with sensitivity settings. */
extern SDL_DECLSPEC SDL_RemapperStickMapping * SDLCALL SDL_CreateMouseStickMapping(float sensitivity);

/* Create stick mapping for WASD/Arrow keys. */
extern SDL_DECLSPEC SDL_RemapperStickMapping * SDLCALL SDL_CreateKeyboardStickMapping(bool use_wasd);

/* Free a stick mapping created by the above functions. */
extern SDL_DECLSPEC void SDLCALL SDL_FreeStickMapping(SDL_RemapperStickMapping *mapping);

#ifdef __cplusplus
}
#endif

#endif /* SDL_remapper_h_ */
