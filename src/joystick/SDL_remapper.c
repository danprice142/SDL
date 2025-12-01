/* SDL3 Controller Remapper core (initial implementation) */

#include "SDL_internal.h"

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_keyboard.h"
#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_timer.h"
#include "SDL3/SDL_sensor.h"
#include "SDL3/SDL_remapper.h"

/* Internal tuning constants */
#define SDL_REMAPPER_INITIAL_GAMEPAD_CAPACITY 4
#define SDL_REMAPPER_INITIAL_MOUSE_CAPACITY 4
#define SDL_REMAPPER_INITIAL_KEYBOARD_CAPACITY 4
#define SDL_REMAPPER_MAX_MOUSE_BUTTONS 32
#define SDL_REMAPPER_MAX_KEYS SDL_SCANCODE_COUNT
#define SDL_REMAPPER_HOLD_THRESHOLD_MS 500

typedef struct SDL_RemapperButtonStateInternal
{
    bool down;
    Uint64 press_timestamp_ns;
} SDL_RemapperButtonStateInternal;

typedef struct SDL_RemapperAxisStateInternal
{
    Sint16 value;
    Sint16 prev_value;
    Uint64 motion_timestamp_ns;
} SDL_RemapperAxisStateInternal;

typedef struct SDL_RemapperGamepadState
{
    SDL_JoystickID joystick_id;
    const SDL_RemapperProfile *profile; /* caller-owned profile */

    SDL_RemapperButtonStateInternal button_states[SDL_GAMEPAD_BUTTON_COUNT];
    SDL_RemapperAxisStateInternal axis_states[SDL_GAMEPAD_AXIS_COUNT];

    /* Touch state for stick-to-touch mapping (left stick) */
    bool left_touch_finger_down;
    float left_touch_x;
    float left_touch_y;

    /* Touch state for stick-to-touch mapping (right stick) */
    bool right_touch_finger_down;
    float right_touch_x;
    float right_touch_y;

    SDL_WindowID touch_window_id;
} SDL_RemapperGamepadState;

typedef enum SDL_RemapperMouseDirection
{
    SDL_REMAPPER_MOUSE_DIR_NONE = 0,
    SDL_REMAPPER_MOUSE_DIR_LEFT,
    SDL_REMAPPER_MOUSE_DIR_RIGHT,
    SDL_REMAPPER_MOUSE_DIR_UP,
    SDL_REMAPPER_MOUSE_DIR_DOWN
} SDL_RemapperMouseDirection;

typedef struct SDL_RemapperMouseState
{
    SDL_MouseID mouse_id;
    const SDL_RemapperProfile *profile; /* caller-owned profile */

    SDL_RemapperButtonStateInternal button_states[SDL_REMAPPER_MAX_MOUSE_BUTTONS];

    /* Per-direction state for mouse-motion based mappings (keys / D-Pad) */
    SDL_RemapperMouseDirection key_motion_dir;
    bool key_motion_dir_down_sent;

    SDL_RemapperMouseDirection dpad_motion_dir;
    bool dpad_motion_dir_down_sent;

    /* Touch mouse state: track finger position for synthetic touch events */
    bool touch_finger_down;           /* Is finger 1 currently down? */
    bool touch_finger2_down;          /* Is finger 2 currently down? */
    float touch_x;                    /* Normalized X position (0..1) */
    float touch_y;                    /* Normalized Y position (0..1) */
    SDL_WindowID touch_window_id;     /* Window for touch events */
} SDL_RemapperMouseState;

typedef struct SDL_RemapperKeyboardState
{
    SDL_KeyboardID keyboard_id;
    const SDL_RemapperProfile *profile; /* caller-owned profile */

    SDL_RemapperButtonStateInternal key_states[SDL_REMAPPER_MAX_KEYS];
} SDL_RemapperKeyboardState;

struct SDL_RemapperContext
{
    SDL_RemapperGamepadState *gamepads;
    int num_gamepads;
    int capacity_gamepads;

    SDL_RemapperMouseState *mice;
    int num_mice;
    int capacity_mice;

    SDL_RemapperKeyboardState *keyboards;
    int num_keyboards;
    int capacity_keyboards;

    Uint64 hold_threshold_ns;
};

static SDL_RemapperGamepadState *
Remapper_FindGamepad(SDL_RemapperContext *ctx, SDL_JoystickID joystick_id)
{
    int i;

    if (!ctx || joystick_id == 0) {
        return NULL;
    }

    for (i = 0; i < ctx->num_gamepads; ++i) {
        if (ctx->gamepads[i].joystick_id == joystick_id) {
            return &ctx->gamepads[i];
        }
    }

    return NULL;
}

static SDL_RemapperKeyboardState *
Remapper_FindKeyboard(SDL_RemapperContext *ctx, SDL_KeyboardID keyboard_id)
{
    int i;

    if (!ctx || keyboard_id == 0) {
        return NULL;
    }

    for (i = 0; i < ctx->num_keyboards; ++i) {
        if (ctx->keyboards[i].keyboard_id == keyboard_id) {
            return &ctx->keyboards[i];
        }
    }

    return NULL;
}

static const SDL_RemapperMapping *
Remapper_FindMouseButtonMapping(const SDL_RemapperProfile *profile, int button)
{
    int i;

    if (!profile || !profile->mappings) {
        return NULL;
    }

    for (i = 0; i < profile->num_mappings; ++i) {
        const SDL_RemapperMapping *m = &profile->mappings[i];

        if (m->source_type == SDL_REMAPPER_SOURCE_MOUSE_BUTTON &&
            (int)m->source.button == button) {
            return m;
        }
    }

    return NULL;
}

static const SDL_RemapperMapping *
Remapper_FindMouseWheelMapping(const SDL_RemapperProfile *profile, int wheel_axis)
{
    int i;

    if (!profile || !profile->mappings) {
        return NULL;
    }

    for (i = 0; i < profile->num_mappings; ++i) {
        const SDL_RemapperMapping *m = &profile->mappings[i];

        if (m->source_type == SDL_REMAPPER_SOURCE_MOUSE_WHEEL &&
            (int)m->source.axis == wheel_axis) {
            return m;
        }
    }

    return NULL;
}

static const SDL_RemapperMapping *
Remapper_FindMouseMotionMapping(const SDL_RemapperProfile *profile)
{
    int i;

    if (!profile || !profile->mappings) {
        return NULL;
    }

    for (i = 0; i < profile->num_mappings; ++i) {
        const SDL_RemapperMapping *m = &profile->mappings[i];

        if (m->source_type == SDL_REMAPPER_SOURCE_MOUSE_MOTION) {
            return m;
        }
    }

    return NULL;
}

static const SDL_RemapperMapping *
Remapper_FindKeyboardKeyMapping(const SDL_RemapperProfile *profile, SDL_Scancode scancode)
{
    int i;

    if (!profile || !profile->mappings) {
        return NULL;
    }

    for (i = 0; i < profile->num_mappings; ++i) {
        const SDL_RemapperMapping *m = &profile->mappings[i];

        if (m->source_type == SDL_REMAPPER_SOURCE_KEYBOARD_KEY &&
            (int)m->source.button == (int)scancode) {
            return m;
        }
    }

    return NULL;
}

static SDL_RemapperMouseState *
Remapper_FindMouse(SDL_RemapperContext *ctx, SDL_MouseID mouse_id)
{
    int i;

    if (!ctx) {
        return NULL;
    }
    /* Note: mouse_id 0 is valid - it represents the default/virtual mouse */

    for (i = 0; i < ctx->num_mice; ++i) {
        if (ctx->mice[i].mouse_id == mouse_id) {
            return &ctx->mice[i];
        }
    }

    return NULL;
}

static SDL_RemapperGamepadState *
Remapper_GetOrAddGamepad(SDL_RemapperContext *ctx, SDL_JoystickID joystick_id)
{
    SDL_RemapperGamepadState *new_array;
    int new_capacity;
    SDL_RemapperGamepadState *gp;

    if (!ctx || joystick_id == 0) {
        return NULL;
    }

    gp = Remapper_FindGamepad(ctx, joystick_id);
    if (gp) {
        return gp;
    }

    if (ctx->num_gamepads == ctx->capacity_gamepads) {
        new_capacity = (ctx->capacity_gamepads > 0) ? (ctx->capacity_gamepads * 2) : SDL_REMAPPER_INITIAL_GAMEPAD_CAPACITY;
        new_array = (SDL_RemapperGamepadState *)SDL_realloc(ctx->gamepads, (size_t)new_capacity * sizeof(*new_array));
        if (!new_array) {
            SDL_OutOfMemory();
            return NULL;
        }

        SDL_memset(new_array + ctx->capacity_gamepads, 0,
                   (size_t)(new_capacity - ctx->capacity_gamepads) * sizeof(*new_array));
        ctx->gamepads = new_array;
        ctx->capacity_gamepads = new_capacity;
    }

    gp = &ctx->gamepads[ctx->num_gamepads++];
    SDL_memset(gp, 0, sizeof(*gp));
    gp->joystick_id = joystick_id;

    return gp;
}

static SDL_RemapperMouseState *
Remapper_GetOrAddMouse(SDL_RemapperContext *ctx, SDL_MouseID mouse_id)
{
    SDL_RemapperMouseState *new_array;
    int new_capacity;
    SDL_RemapperMouseState *ms;

    if (!ctx) {
        return NULL;
    }
    /* Note: mouse_id 0 is valid - it represents the default/virtual mouse */

    ms = Remapper_FindMouse(ctx, mouse_id);
    if (ms) {
        return ms;
    }

    if (ctx->num_mice == ctx->capacity_mice) {
        new_capacity = (ctx->capacity_mice > 0) ? (ctx->capacity_mice * 2) : SDL_REMAPPER_INITIAL_MOUSE_CAPACITY;
        new_array = (SDL_RemapperMouseState *)SDL_realloc(ctx->mice, (size_t)new_capacity * sizeof(*new_array));
        if (!new_array) {
            SDL_OutOfMemory();
            return NULL;
        }

        SDL_memset(new_array + ctx->capacity_mice, 0,
                   (size_t)(new_capacity - ctx->capacity_mice) * sizeof(*new_array));
        ctx->mice = new_array;
        ctx->capacity_mice = new_capacity;
    }

    ms = &ctx->mice[ctx->num_mice++];
    SDL_memset(ms, 0, sizeof(*ms));
    ms->mouse_id = mouse_id;

    return ms;
}

static SDL_RemapperKeyboardState *
Remapper_GetOrAddKeyboard(SDL_RemapperContext *ctx, SDL_KeyboardID keyboard_id)
{
    SDL_RemapperKeyboardState *new_array;
    int new_capacity;
    SDL_RemapperKeyboardState *ks;

    if (!ctx || keyboard_id == 0) {
        return NULL;
    }

    ks = Remapper_FindKeyboard(ctx, keyboard_id);
    if (ks) {
        return ks;
    }

    if (ctx->num_keyboards == ctx->capacity_keyboards) {
        new_capacity = (ctx->capacity_keyboards > 0) ? (ctx->capacity_keyboards * 2) : SDL_REMAPPER_INITIAL_KEYBOARD_CAPACITY;
        new_array = (SDL_RemapperKeyboardState *)SDL_realloc(ctx->keyboards, (size_t)new_capacity * sizeof(*new_array));
        if (!new_array) {
            SDL_OutOfMemory();
            return NULL;
        }

        SDL_memset(new_array + ctx->capacity_keyboards, 0,
                   (size_t)(new_capacity - ctx->capacity_keyboards) * sizeof(*new_array));
        ctx->keyboards = new_array;
        ctx->capacity_keyboards = new_capacity;
    }

    ks = &ctx->keyboards[ctx->num_keyboards++];
    SDL_memset(ks, 0, sizeof(*ks));
    ks->keyboard_id = keyboard_id;

    return ks;
}

static const SDL_RemapperMapping *
Remapper_FindButtonMapping(const SDL_RemapperProfile *profile, SDL_GamepadButton button)
{
    int i;

    if (!profile || !profile->mappings) {
        return NULL;
    }

    for (i = 0; i < profile->num_mappings; ++i) {
        if (profile->mappings[i].source_type == SDL_REMAPPER_SOURCE_BUTTON &&
            profile->mappings[i].source.button == button) {
            return &profile->mappings[i];
        }
    }

    return NULL;
}

static const SDL_RemapperMapping *
Remapper_FindAxisMapping(const SDL_RemapperProfile *profile, SDL_GamepadAxis axis)
{
    int i;

    if (!profile || !profile->mappings) {
        return NULL;
    }

    for (i = 0; i < profile->num_mappings; ++i) {
        if (profile->mappings[i].source_type == SDL_REMAPPER_SOURCE_AXIS &&
            profile->mappings[i].source.axis == axis) {
            return &profile->mappings[i];
        }
    }

    return NULL;
}

static bool
Remapper_IsShiftActive(const SDL_RemapperGamepadState *gp)
{
    int i;

    if (!gp || !gp->profile || !gp->profile->mappings) {
        return false;
    }

    for (i = 0; i < gp->profile->num_mappings; ++i) {
        const SDL_RemapperMapping *m = &gp->profile->mappings[i];

        if (m->use_as_shift) {
            if (m->source_type == SDL_REMAPPER_SOURCE_BUTTON) {
                if (m->source.button >= 0 &&
                    m->source.button < SDL_GAMEPAD_BUTTON_COUNT &&
                    gp->button_states[m->source.button].down) {
                    return true;
                }
            } else if (m->source_type == SDL_REMAPPER_SOURCE_AXIS) {
                if (m->source.axis >= 0 &&
                    m->source.axis < SDL_GAMEPAD_AXIS_COUNT &&
                    SDL_abs(gp->axis_states[m->source.axis].value) > 16000) {
                    return true;
                }
            }
        }
    }

    return false;
}

static bool
Remapper_KeyboardIsShiftActive(const SDL_RemapperKeyboardState *ks)
{
    int i;

    if (!ks || !ks->profile || !ks->profile->mappings) {
        return false;
    }

    for (i = 0; i < ks->profile->num_mappings; ++i) {
        const SDL_RemapperMapping *m = &ks->profile->mappings[i];

        if (m->use_as_shift && m->source_type == SDL_REMAPPER_SOURCE_KEYBOARD_KEY) {
            int sc = (int)m->source.button;
            if (sc >= 0 && sc < SDL_REMAPPER_MAX_KEYS &&
                ks->key_states[sc].down) {
                return true;
            }
        }
    }

    return false;
}

static bool
Remapper_MouseIsShiftActive(const SDL_RemapperMouseState *ms)
{
    int i;

    if (!ms || !ms->profile || !ms->profile->mappings) {
        return false;
    }

    for (i = 0; i < ms->profile->num_mappings; ++i) {
        const SDL_RemapperMapping *m = &ms->profile->mappings[i];

        if (m->use_as_shift && m->source_type == SDL_REMAPPER_SOURCE_MOUSE_BUTTON) {
            int button = (int)m->source.button;
            if (button >= 0 && button < SDL_REMAPPER_MAX_MOUSE_BUTTONS &&
                ms->button_states[button].down) {
                return true;
            }
        }
    }

    return false;
}

static bool
Remapper_ActionsEqual(const SDL_RemapperAction *a, const SDL_RemapperAction *b)
{
    if (!a || !b) {
        return false;
    }

    return (a->kind == b->kind && a->code == b->code && a->value == b->value);
}

static const SDL_RemapperAction *
Remapper_ChooseAction(const SDL_RemapperMapping *mapping,
                      bool shift_active,
                      bool is_hold)
{
    const SDL_RemapperAction *base = NULL;

    if (!mapping) {
        return NULL;
    }

    if (shift_active && mapping->shift_action.kind != SDL_REMAPPER_ACTION_NONE) {
        base = &mapping->shift_action;
    } else if (mapping->primary_action.kind != SDL_REMAPPER_ACTION_NONE) {
        base = &mapping->primary_action;
    }

    if (!base) {
        return NULL;
    }

    if (is_hold && mapping->hold_action.kind != SDL_REMAPPER_ACTION_NONE &&
        !Remapper_ActionsEqual(base, &mapping->hold_action)) {
        return &mapping->hold_action;
    }

    return base;
}

static int
Remapper_EmitGamepadButton(const SDL_GamepadButtonEvent *src,
                           const SDL_RemapperAction *action,
                           SDL_Event *out_event)
{
    SDL_GamepadButtonEvent *dst;

    if (!src || !action || !out_event) {
        return 0;
    }

    if (action->kind != SDL_REMAPPER_ACTION_GAMEPAD_BUTTON) {
        return 0;
    }

    dst = &out_event->gbutton;
    dst->type = src->type;
    dst->reserved = 0;
    dst->timestamp = src->timestamp;
    dst->which = src->which;
    dst->button = (Uint8)action->code;
    dst->down = src->down;
    dst->padding1 = 0;
    dst->padding2 = 0;

    return 1;
}

static int
Remapper_EmitSyntheticGyroFromStick(SDL_RemapperGamepadState *gp,
                                    const SDL_GamepadAxisEvent *aev,
                                    const SDL_RemapperStickMapping *sm,
                                    SDL_Event *out_event)
{
    SDL_GamepadSensorEvent *dst;
    Sint16 raw_x = 0;
    Sint16 raw_y = 0;
    float nx, ny;
    float gain_x = 1.0f;
    float gain_y = 1.0f;
    float accel_gain = 1.0f;
    float pitch, yaw, roll;

    if (!gp || !aev || !sm || !out_event) {
        return 0;
    }

    /* Only left/right sticks participate in synthetic gyro output. */
    if (aev->axis == SDL_GAMEPAD_AXIS_LEFTX || aev->axis == SDL_GAMEPAD_AXIS_LEFTY) {
        raw_x = gp->axis_states[SDL_GAMEPAD_AXIS_LEFTX].value;
        raw_y = gp->axis_states[SDL_GAMEPAD_AXIS_LEFTY].value;
    } else if (aev->axis == SDL_GAMEPAD_AXIS_RIGHTX || aev->axis == SDL_GAMEPAD_AXIS_RIGHTY) {
        raw_x = gp->axis_states[SDL_GAMEPAD_AXIS_RIGHTX].value;
        raw_y = gp->axis_states[SDL_GAMEPAD_AXIS_RIGHTY].value;
    } else {
        return 0;
    }

    nx = (float)raw_x / 32767.0f;
    ny = (float)raw_y / 32767.0f;

    if (sm->invert_horizontal) {
        nx = -nx;
    }
    if (sm->invert_vertical) {
        ny = -ny;
    }

    /* Map sliders in range [-50,50] to gains in roughly [1,2]. */
    if (sm->gyro_horizontal_sensitivity != 0.0f) {
        gain_x = 1.0f + (SDL_fabsf(sm->gyro_horizontal_sensitivity) / 50.0f);
    }
    if (sm->gyro_vertical_sensitivity != 0.0f) {
        gain_y = 1.0f + (SDL_fabsf(sm->gyro_vertical_sensitivity) / 50.0f);
    }

    if (sm->gyro_acceleration != 0.0f) {
        float mag = SDL_sqrtf(nx * nx + ny * ny);
        accel_gain += (sm->gyro_acceleration / 50.0f) * mag;
    }

    if (!sm->gyro_mode_roll) {
        /* Default: treat vertical stick motion as pitch (X axis) and horizontal as yaw (Y axis). */
        pitch = ny * gain_y * accel_gain;
        yaw   = nx * gain_x * accel_gain;
        roll  = 0.0f;
    } else {
        /* Roll-only mode: drive roll from horizontal stick; pitch/yaw are zero. */
        pitch = 0.0f;
        yaw   = 0.0f;
        roll  = nx * gain_x * accel_gain;
    }

    dst = &out_event->gsensor;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE;
    dst->timestamp = SDL_GetTicksNS();
    dst->which = gp->joystick_id;
    dst->sensor = SDL_SENSOR_GYRO;
    dst->data[0] = pitch;
    dst->data[1] = yaw;
    dst->data[2] = roll;
    dst->sensor_timestamp = dst->timestamp;

    return 1;
}

static int
Remapper_EmitSyntheticGyroFromMouse(SDL_RemapperMouseState *ms,
                                    const SDL_MouseMotionEvent *mev,
                                    const SDL_RemapperStickMapping *sm,
                                    SDL_Event *out_event)
{
    SDL_GamepadSensorEvent *dst;
    float dx, dy;
    float nx, ny;
    float gain_x = 1.0f;
    float gain_y = 1.0f;
    float accel_gain = 1.0f;
    float pitch, yaw, roll;
    SDL_JoystickID target_id = 0;

    if (!ms || !mev || !sm || !out_event) {
        return 0;
    }

    dx = (float)mev->xrel;
    dy = (float)mev->yrel;

    /* Apply inversion from stick-style mapping config */
    if (sm->invert_horizontal) {
        dx = -dx;
    }
    if (sm->invert_vertical) {
        dy = -dy;
    }

    /* Normalize mouse deltas into a rough [-1, 1] range. */
    nx = dx / 50.0f;
    ny = dy / 50.0f;
    if (nx < -1.0f) nx = -1.0f;
    if (nx >  1.0f) nx =  1.0f;
    if (ny < -1.0f) ny = -1.0f;
    if (ny >  1.0f) ny =  1.0f;

    /* Map sliders in range [-50,50] to gains in roughly [1,2]. */
    if (sm->gyro_horizontal_sensitivity != 0.0f) {
        gain_x = 1.0f + (SDL_fabsf(sm->gyro_horizontal_sensitivity) / 50.0f);
    }
    if (sm->gyro_vertical_sensitivity != 0.0f) {
        gain_y = 1.0f + (SDL_fabsf(sm->gyro_vertical_sensitivity) / 50.0f);
    }

    if (sm->gyro_acceleration != 0.0f) {
        float mag = SDL_sqrtf(nx * nx + ny * ny);
        accel_gain += (sm->gyro_acceleration / 50.0f) * mag;
    }

    if (!sm->gyro_mode_roll) {
        /* Default: treat vertical mouse motion as pitch and horizontal as yaw. */
        pitch = ny * gain_y * accel_gain;
        yaw   = nx * gain_x * accel_gain;
        roll  = 0.0f;
    } else {
        /* Roll-only mode: drive roll from horizontal motion; pitch/yaw are zero. */
        pitch = 0.0f;
        yaw   = 0.0f;
        roll  = nx * gain_x * accel_gain;
    }

    if (ms->profile) {
        target_id = ms->profile->gamepad_id;
    }

    dst = &out_event->gsensor;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE;
    dst->timestamp = SDL_GetTicksNS();
    dst->which = target_id;
    dst->sensor = SDL_SENSOR_GYRO;
    dst->data[0] = pitch;
    dst->data[1] = yaw;
    dst->data[2] = roll;
    dst->sensor_timestamp = dst->timestamp;

    return 1;
}

/* Synthetic touch device ID for mouse-to-touch conversion */
#define SDL_REMAPPER_TOUCH_DEVICE_ID 0x524D5054  /* "RMPT" - Remapper Touch */
#define SDL_REMAPPER_TOUCH_FINGER_ID 1

/* Emit touch finger down at current mouse position */
static int
Remapper_EmitTouchFingerDownAtMouse(SDL_RemapperMouseState *ms,
                                    SDL_Event *out_event)
{
    SDL_TouchFingerEvent *dst;
    float mx, my;
    int w = 1920, h = 1080;
    SDL_Window *focused_window;
    SDL_WindowID win_id = 0;

    if (!ms || !out_event) {
        return 0;
    }

    /* Get current mouse position */
    SDL_GetMouseState(&mx, &my);

    /* Get focused window for size normalization */
    focused_window = SDL_GetMouseFocus();
    if (focused_window) {
        win_id = SDL_GetWindowID(focused_window);
        SDL_GetWindowSize(focused_window, &w, &h);
    }

    /* Update touch state */
    ms->touch_finger_down = true;
    ms->touch_x = mx / (float)w;
    ms->touch_y = my / (float)h;
    ms->touch_window_id = win_id;

    /* Clamp to valid range */
    if (ms->touch_x < 0.0f) ms->touch_x = 0.0f;
    if (ms->touch_x > 1.0f) ms->touch_x = 1.0f;
    if (ms->touch_y < 0.0f) ms->touch_y = 0.0f;
    if (ms->touch_y > 1.0f) ms->touch_y = 1.0f;

    dst = &out_event->tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_DOWN;
    dst->timestamp = SDL_GetTicksNS();
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = ms->touch_x;
    dst->y = ms->touch_y;
    dst->dx = 0.0f;
    dst->dy = 0.0f;
    dst->pressure = 1.0f;
    dst->windowID = win_id;

    return 1;
}

/* Emit touch finger up at last known touch position */
static int
Remapper_EmitTouchFingerUpAtMouse(SDL_RemapperMouseState *ms,
                                  SDL_Event *out_event)
{
    SDL_TouchFingerEvent *dst;

    if (!ms || !out_event) {
        return 0;
    }

    if (!ms->touch_finger_down) {
        return 0;  /* No finger was down */
    }

    dst = &out_event->tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_UP;
    dst->timestamp = SDL_GetTicksNS();
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = ms->touch_x;
    dst->y = ms->touch_y;
    dst->dx = 0.0f;
    dst->dy = 0.0f;
    dst->pressure = 0.0f;
    dst->windowID = ms->touch_window_id;

    /* Clear touch state */
    ms->touch_finger_down = false;

    return 1;
}

/* Update touch position from mouse motion (always tracks position for Touch Tap to use) */
static int
Remapper_EmitTouchFingerMotion(SDL_RemapperMouseState *ms,
                               const SDL_MouseMotionEvent *mev,
                               const SDL_RemapperStickMapping *sm,
                               SDL_Event *out_event)
{
    SDL_TouchFingerEvent *dst;
    float new_x, new_y;
    float dx, dy;
    int w = 1920, h = 1080;
    float sens = 1.0f;

    if (!ms || !mev || !out_event) {
        return 0;
    }

    /* Get window size for normalization */
    if (mev->windowID != 0) {
        SDL_Window *win = SDL_GetWindowFromID(mev->windowID);
        if (win) {
            SDL_GetWindowSize(win, &w, &h);
        }
    }

    /* Apply sensitivity if configured */
    if (sm && sm->horizontal_sensitivity != 0.0f) {
        sens = 1.0f + (sm->horizontal_sensitivity / 50.0f);
    }

    /* Calculate new position */
    new_x = mev->x / (float)w;
    new_y = mev->y / (float)h;

    /* Calculate delta (normalized to -1..1 range) */
    dx = (mev->xrel / (float)w) * sens;
    dy = (mev->yrel / (float)h) * sens;

    /* Apply inversion if configured */
    if (sm) {
        if (sm->invert_horizontal) {
            dx = -dx;
            new_x = ms->touch_x - dx;
        }
        if (sm->invert_vertical) {
            dy = -dy;
            new_y = ms->touch_y - dy;
        }
    }

    /* Clamp to valid range */
    if (new_x < 0.0f) new_x = 0.0f;
    if (new_x > 1.0f) new_x = 1.0f;
    if (new_y < 0.0f) new_y = 0.0f;
    if (new_y > 1.0f) new_y = 1.0f;
    if (dx < -1.0f) dx = -1.0f;
    if (dx > 1.0f) dx = 1.0f;
    if (dy < -1.0f) dy = -1.0f;
    if (dy > 1.0f) dy = 1.0f;

    /* Always update stored position so Touch Tap knows where to tap */
    ms->touch_x = new_x;
    ms->touch_y = new_y;
    ms->touch_window_id = mev->windowID;

    /* Only emit FINGER_MOTION event if finger is currently down */
    if (!ms->touch_finger_down) {
        return 0;  /* Position updated, but no motion event emitted */
    }

    dst = &out_event->tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_MOTION;
    dst->timestamp = SDL_GetTicksNS();
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = new_x;
    dst->y = new_y;
    dst->dx = dx;
    dst->dy = dy;
    dst->pressure = 1.0f;
    dst->windowID = mev->windowID;

    return 1;
}

/* Finger 2 ID for multi-touch */
#define SDL_REMAPPER_TOUCH_FINGER2_ID 2

/* Finger 2 offset from finger 1 (normalized, ~100px at 1920 width) */
#define SDL_REMAPPER_FINGER2_OFFSET_X 0.05f
#define SDL_REMAPPER_FINGER2_OFFSET_Y 0.0f

/* Swipe distance (normalized, ~10% of screen) */
#define SDL_REMAPPER_SWIPE_DISTANCE 0.10f

/* Toggle finger 1 hold state */
static int
Remapper_EmitTouchHold(SDL_RemapperMouseState *ms, SDL_Event *out_event)
{
    SDL_TouchFingerEvent *dst;

    if (!ms || !out_event) {
        return 0;
    }

    ms->touch_finger_down = !ms->touch_finger_down;

    dst = &out_event->tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = ms->touch_finger_down ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP;
    dst->timestamp = SDL_GetTicksNS();
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = ms->touch_x;
    dst->y = ms->touch_y;
    dst->dx = 0.0f;
    dst->dy = 0.0f;
    dst->pressure = ms->touch_finger_down ? 1.0f : 0.0f;
    dst->windowID = ms->touch_window_id;

    return 1;
}

/* Emit double tap (4 events: down-up-down-up) - returns number of events */
static int
Remapper_EmitTouchDoubleTap(SDL_RemapperMouseState *ms, SDL_Event *out_events)
{
    SDL_TouchFingerEvent *dst;
    Uint64 ts = SDL_GetTicksNS();
    int i;

    if (!ms || !out_events) {
        return 0;
    }

    for (i = 0; i < 4; i++) {
        dst = &out_events[i].tfinger;
        SDL_memset(dst, 0, sizeof(*dst));
        dst->type = (i % 2 == 0) ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP;
        dst->timestamp = ts + (i * 10000);  /* Small delay between events */
        dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
        dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
        dst->x = ms->touch_x;
        dst->y = ms->touch_y;
        dst->dx = 0.0f;
        dst->dy = 0.0f;
        dst->pressure = (i % 2 == 0) ? 1.0f : 0.0f;
        dst->windowID = ms->touch_window_id;
    }

    return 4;
}

/* Emit swipe gesture (3 events: down, motion, up) */
static int
Remapper_EmitTouchSwipe(SDL_RemapperMouseState *ms, float dx, float dy, SDL_Event *out_events)
{
    SDL_TouchFingerEvent *dst;
    Uint64 ts = SDL_GetTicksNS();
    float end_x, end_y;

    if (!ms || !out_events) {
        return 0;
    }

    end_x = ms->touch_x + dx;
    end_y = ms->touch_y + dy;
    if (end_x < 0.0f) end_x = 0.0f;
    if (end_x > 1.0f) end_x = 1.0f;
    if (end_y < 0.0f) end_y = 0.0f;
    if (end_y > 1.0f) end_y = 1.0f;

    /* Finger down at start */
    dst = &out_events[0].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_DOWN;
    dst->timestamp = ts;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = ms->touch_x;
    dst->y = ms->touch_y;
    dst->dx = 0.0f;
    dst->dy = 0.0f;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Motion to end position */
    dst = &out_events[1].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_MOTION;
    dst->timestamp = ts + 10000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = end_x;
    dst->y = end_y;
    dst->dx = dx;
    dst->dy = dy;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger up at end */
    dst = &out_events[2].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_UP;
    dst->timestamp = ts + 20000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = end_x;
    dst->y = end_y;
    dst->dx = 0.0f;
    dst->dy = 0.0f;
    dst->pressure = 0.0f;
    dst->windowID = ms->touch_window_id;

    return 3;
}

/* Finger 2 tap at offset position */
static int
Remapper_EmitTouchFinger2Down(SDL_RemapperMouseState *ms, SDL_Event *out_event)
{
    SDL_TouchFingerEvent *dst;
    float f2_x, f2_y;

    if (!ms || !out_event) {
        return 0;
    }

    f2_x = ms->touch_x + SDL_REMAPPER_FINGER2_OFFSET_X;
    f2_y = ms->touch_y + SDL_REMAPPER_FINGER2_OFFSET_Y;
    if (f2_x > 1.0f) f2_x = 1.0f;

    ms->touch_finger2_down = true;

    dst = &out_event->tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_DOWN;
    dst->timestamp = SDL_GetTicksNS();
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER2_ID;
    dst->x = f2_x;
    dst->y = f2_y;
    dst->dx = 0.0f;
    dst->dy = 0.0f;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    return 1;
}

static int
Remapper_EmitTouchFinger2Up(SDL_RemapperMouseState *ms, SDL_Event *out_event)
{
    SDL_TouchFingerEvent *dst;
    float f2_x, f2_y;

    if (!ms || !out_event) {
        return 0;
    }

    if (!ms->touch_finger2_down) {
        return 0;
    }

    f2_x = ms->touch_x + SDL_REMAPPER_FINGER2_OFFSET_X;
    f2_y = ms->touch_y + SDL_REMAPPER_FINGER2_OFFSET_Y;
    if (f2_x > 1.0f) f2_x = 1.0f;

    ms->touch_finger2_down = false;

    dst = &out_event->tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_UP;
    dst->timestamp = SDL_GetTicksNS();
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER2_ID;
    dst->x = f2_x;
    dst->y = f2_y;
    dst->dx = 0.0f;
    dst->dy = 0.0f;
    dst->pressure = 0.0f;
    dst->windowID = ms->touch_window_id;

    return 1;
}

/* Toggle finger 2 hold state */
static int
Remapper_EmitTouchFinger2Hold(SDL_RemapperMouseState *ms, SDL_Event *out_event)
{
    SDL_TouchFingerEvent *dst;
    float f2_x, f2_y;

    if (!ms || !out_event) {
        return 0;
    }

    f2_x = ms->touch_x + SDL_REMAPPER_FINGER2_OFFSET_X;
    f2_y = ms->touch_y + SDL_REMAPPER_FINGER2_OFFSET_Y;
    if (f2_x > 1.0f) f2_x = 1.0f;

    ms->touch_finger2_down = !ms->touch_finger2_down;

    dst = &out_event->tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = ms->touch_finger2_down ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP;
    dst->timestamp = SDL_GetTicksNS();
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER2_ID;
    dst->x = f2_x;
    dst->y = f2_y;
    dst->dx = 0.0f;
    dst->dy = 0.0f;
    dst->pressure = ms->touch_finger2_down ? 1.0f : 0.0f;
    dst->windowID = ms->touch_window_id;

    return 1;
}

/* Pinch gesture: two fingers move together or apart (6 events: 2 down, 2 motion, 2 up) */
static int
Remapper_EmitTouchPinch(SDL_RemapperMouseState *ms, bool pinch_in, SDL_Event *out_events)
{
    SDL_TouchFingerEvent *dst;
    Uint64 ts = SDL_GetTicksNS();
    float f1_start_x, f1_start_y, f1_end_x, f1_end_y;
    float f2_start_x, f2_start_y, f2_end_x, f2_end_y;
    float offset = SDL_REMAPPER_SWIPE_DISTANCE;

    if (!ms || !out_events) {
        return 0;
    }

    if (pinch_in) {
        /* Start apart, end together */
        f1_start_x = ms->touch_x - offset;
        f1_start_y = ms->touch_y;
        f1_end_x = ms->touch_x;
        f1_end_y = ms->touch_y;
        f2_start_x = ms->touch_x + offset;
        f2_start_y = ms->touch_y;
        f2_end_x = ms->touch_x;
        f2_end_y = ms->touch_y;
    } else {
        /* Start together, end apart */
        f1_start_x = ms->touch_x;
        f1_start_y = ms->touch_y;
        f1_end_x = ms->touch_x - offset;
        f1_end_y = ms->touch_y;
        f2_start_x = ms->touch_x;
        f2_start_y = ms->touch_y;
        f2_end_x = ms->touch_x + offset;
        f2_end_y = ms->touch_y;
    }

    /* Clamp positions */
    if (f1_start_x < 0.0f) f1_start_x = 0.0f;
    if (f1_end_x < 0.0f) f1_end_x = 0.0f;
    if (f2_start_x > 1.0f) f2_start_x = 1.0f;
    if (f2_end_x > 1.0f) f2_end_x = 1.0f;

    /* Finger 1 down */
    dst = &out_events[0].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_DOWN;
    dst->timestamp = ts;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = f1_start_x;
    dst->y = f1_start_y;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 2 down */
    dst = &out_events[1].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_DOWN;
    dst->timestamp = ts + 1000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER2_ID;
    dst->x = f2_start_x;
    dst->y = f2_start_y;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 1 motion */
    dst = &out_events[2].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_MOTION;
    dst->timestamp = ts + 10000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = f1_end_x;
    dst->y = f1_end_y;
    dst->dx = f1_end_x - f1_start_x;
    dst->dy = f1_end_y - f1_start_y;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 2 motion */
    dst = &out_events[3].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_MOTION;
    dst->timestamp = ts + 10000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER2_ID;
    dst->x = f2_end_x;
    dst->y = f2_end_y;
    dst->dx = f2_end_x - f2_start_x;
    dst->dy = f2_end_y - f2_start_y;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 1 up */
    dst = &out_events[4].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_UP;
    dst->timestamp = ts + 20000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = f1_end_x;
    dst->y = f1_end_y;
    dst->pressure = 0.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 2 up */
    dst = &out_events[5].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_UP;
    dst->timestamp = ts + 20000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER2_ID;
    dst->x = f2_end_x;
    dst->y = f2_end_y;
    dst->pressure = 0.0f;
    dst->windowID = ms->touch_window_id;

    return 6;
}

/* Rotate gesture: two fingers rotate around center (6 events) */
static int
Remapper_EmitTouchRotate(SDL_RemapperMouseState *ms, bool clockwise, SDL_Event *out_events)
{
    SDL_TouchFingerEvent *dst;
    Uint64 ts = SDL_GetTicksNS();
    float radius = SDL_REMAPPER_SWIPE_DISTANCE;
    float angle = clockwise ? 0.5f : -0.5f;  /* ~30 degrees in radians */
    float cos_a, sin_a;
    float f1_start_x, f1_start_y, f1_end_x, f1_end_y;
    float f2_start_x, f2_start_y, f2_end_x, f2_end_y;

    if (!ms || !out_events) {
        return 0;
    }

    cos_a = SDL_cosf(angle);
    sin_a = SDL_sinf(angle);

    /* Start positions: fingers on opposite sides */
    f1_start_x = ms->touch_x - radius;
    f1_start_y = ms->touch_y;
    f2_start_x = ms->touch_x + radius;
    f2_start_y = ms->touch_y;

    /* End positions: rotated around center */
    f1_end_x = ms->touch_x + (-radius * cos_a);
    f1_end_y = ms->touch_y + (-radius * sin_a);
    f2_end_x = ms->touch_x + (radius * cos_a);
    f2_end_y = ms->touch_y + (radius * sin_a);

    /* Clamp */
    if (f1_start_x < 0.0f) f1_start_x = 0.0f;
    if (f1_end_x < 0.0f) f1_end_x = 0.0f;
    if (f2_start_x > 1.0f) f2_start_x = 1.0f;
    if (f2_end_x > 1.0f) f2_end_x = 1.0f;
    if (f1_end_y < 0.0f) f1_end_y = 0.0f;
    if (f1_end_y > 1.0f) f1_end_y = 1.0f;
    if (f2_end_y < 0.0f) f2_end_y = 0.0f;
    if (f2_end_y > 1.0f) f2_end_y = 1.0f;

    /* Finger 1 down */
    dst = &out_events[0].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_DOWN;
    dst->timestamp = ts;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = f1_start_x;
    dst->y = f1_start_y;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 2 down */
    dst = &out_events[1].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_DOWN;
    dst->timestamp = ts + 1000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER2_ID;
    dst->x = f2_start_x;
    dst->y = f2_start_y;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 1 motion */
    dst = &out_events[2].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_MOTION;
    dst->timestamp = ts + 10000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = f1_end_x;
    dst->y = f1_end_y;
    dst->dx = f1_end_x - f1_start_x;
    dst->dy = f1_end_y - f1_start_y;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 2 motion */
    dst = &out_events[3].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_MOTION;
    dst->timestamp = ts + 10000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER2_ID;
    dst->x = f2_end_x;
    dst->y = f2_end_y;
    dst->dx = f2_end_x - f2_start_x;
    dst->dy = f2_end_y - f2_start_y;
    dst->pressure = 1.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 1 up */
    dst = &out_events[4].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_UP;
    dst->timestamp = ts + 20000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER_ID;
    dst->x = f1_end_x;
    dst->y = f1_end_y;
    dst->pressure = 0.0f;
    dst->windowID = ms->touch_window_id;

    /* Finger 2 up */
    dst = &out_events[5].tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_UP;
    dst->timestamp = ts + 20000;
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = SDL_REMAPPER_TOUCH_FINGER2_ID;
    dst->x = f2_end_x;
    dst->y = f2_end_y;
    dst->pressure = 0.0f;
    dst->windowID = ms->touch_window_id;

    return 6;
}

/* Emit touch finger motion from gamepad stick axis
 * is_left_stick: true for left stick, false for right stick
 * Returns number of events emitted (0, 1, or 2 if finger just went down)
 */
static int
Remapper_EmitTouchFromStick(SDL_RemapperGamepadState *gp,
                            const SDL_GamepadAxisEvent *aev,
                            const SDL_RemapperStickMapping *sm,
                            bool is_left_stick,
                            SDL_Event *out_events)
{
    SDL_TouchFingerEvent *dst;
    float *touch_x, *touch_y;
    bool *finger_down;
    float axis_val;
    float old_x, old_y, new_x, new_y;
    float sensitivity = 1.0f;
    float deadzone = 0.15f;
    SDL_FingerID finger_id;
    bool is_x_axis;
    int event_count = 0;

    if (!gp || !aev || !out_events) {
        return 0;
    }

    /* Select which touch state to use */
    if (is_left_stick) {
        touch_x = &gp->left_touch_x;
        touch_y = &gp->left_touch_y;
        finger_down = &gp->left_touch_finger_down;
        finger_id = SDL_REMAPPER_TOUCH_FINGER_ID;
    } else {
        touch_x = &gp->right_touch_x;
        touch_y = &gp->right_touch_y;
        finger_down = &gp->right_touch_finger_down;
        finger_id = SDL_REMAPPER_TOUCH_FINGER2_ID;
    }

    /* Determine if this is X or Y axis */
    is_x_axis = (aev->axis == SDL_GAMEPAD_AXIS_LEFTX || aev->axis == SDL_GAMEPAD_AXIS_RIGHTX);

    /* Normalize axis value from -32768..32767 to -1..1 */
    axis_val = (float)aev->value / 32767.0f;

    /* Apply deadzone */
    if (axis_val > -deadzone && axis_val < deadzone) {
        axis_val = 0.0f;
    } else if (axis_val > 0) {
        axis_val = (axis_val - deadzone) / (1.0f - deadzone);
    } else {
        axis_val = (axis_val + deadzone) / (1.0f - deadzone);
    }

    /* Apply sensitivity from stick mapping */
    if (sm) {
        if (is_x_axis && sm->horizontal_sensitivity != 0.0f) {
            sensitivity = 1.0f + (sm->horizontal_sensitivity / 50.0f);
        } else if (!is_x_axis && sm->vertical_sensitivity != 0.0f) {
            sensitivity = 1.0f + (sm->vertical_sensitivity / 50.0f);
        }

        /* Apply inversion */
        if (is_x_axis && sm->invert_horizontal) {
            axis_val = -axis_val;
        } else if (!is_x_axis && sm->invert_vertical) {
            axis_val = -axis_val;
        }
    }

    /* Store old position */
    old_x = *touch_x;
    old_y = *touch_y;

    /* Initialize position to center if not set */
    if (*touch_x == 0.0f && *touch_y == 0.0f && !*finger_down) {
        *touch_x = 0.5f;
        *touch_y = 0.5f;
    }

    /* Update position based on axis input (position mode - stick controls absolute position) */
    new_x = *touch_x;
    new_y = *touch_y;

    /* Scale movement speed */
    float speed = 0.02f * sensitivity;

    if (is_x_axis) {
        new_x += axis_val * speed;
    } else {
        new_y += axis_val * speed;
    }

    /* Clamp to valid range */
    if (new_x < 0.0f) new_x = 0.0f;
    if (new_x > 1.0f) new_x = 1.0f;
    if (new_y < 0.0f) new_y = 0.0f;
    if (new_y > 1.0f) new_y = 1.0f;

    *touch_x = new_x;
    *touch_y = new_y;

    /* Get focused window */
    SDL_Window *win = SDL_GetKeyboardFocus();
    if (win) {
        gp->touch_window_id = SDL_GetWindowID(win);
    }

    /* Check if we need to emit finger down first */
    if (!*finger_down && (axis_val != 0.0f)) {
        /* Finger just touched - emit finger down */
        *finger_down = true;

        dst = &out_events[event_count].tfinger;
        SDL_memset(dst, 0, sizeof(*dst));
        dst->type = SDL_EVENT_FINGER_DOWN;
        dst->timestamp = SDL_GetTicksNS();
        dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
        dst->fingerID = finger_id;
        dst->x = new_x;
        dst->y = new_y;
        dst->dx = 0.0f;
        dst->dy = 0.0f;
        dst->pressure = 1.0f;
        dst->windowID = gp->touch_window_id;
        event_count++;
    }

    /* Only emit motion if finger is down and position changed */
    if (*finger_down && (new_x != old_x || new_y != old_y)) {
        dst = &out_events[event_count].tfinger;
        SDL_memset(dst, 0, sizeof(*dst));
        dst->type = SDL_EVENT_FINGER_MOTION;
        dst->timestamp = SDL_GetTicksNS();
        dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
        dst->fingerID = finger_id;
        dst->x = new_x;
        dst->y = new_y;
        dst->dx = new_x - old_x;
        dst->dy = new_y - old_y;
        dst->pressure = 1.0f;
        dst->windowID = gp->touch_window_id;
        event_count++;
    }

    return event_count;
}

/* Emit finger up when stick returns to center */
static int
Remapper_EmitTouchUpFromStick(SDL_RemapperGamepadState *gp,
                              bool is_left_stick,
                              SDL_Event *out_event)
{
    SDL_TouchFingerEvent *dst;
    float *touch_x, *touch_y;
    bool *finger_down;
    SDL_FingerID finger_id;

    if (!gp || !out_event) {
        return 0;
    }

    if (is_left_stick) {
        touch_x = &gp->left_touch_x;
        touch_y = &gp->left_touch_y;
        finger_down = &gp->left_touch_finger_down;
        finger_id = SDL_REMAPPER_TOUCH_FINGER_ID;
    } else {
        touch_x = &gp->right_touch_x;
        touch_y = &gp->right_touch_y;
        finger_down = &gp->right_touch_finger_down;
        finger_id = SDL_REMAPPER_TOUCH_FINGER2_ID;
    }

    if (!*finger_down) {
        return 0;
    }

    *finger_down = false;

    dst = &out_event->tfinger;
    SDL_memset(dst, 0, sizeof(*dst));
    dst->type = SDL_EVENT_FINGER_UP;
    dst->timestamp = SDL_GetTicksNS();
    dst->touchID = SDL_REMAPPER_TOUCH_DEVICE_ID;
    dst->fingerID = finger_id;
    dst->x = *touch_x;
    dst->y = *touch_y;
    dst->dx = 0.0f;
    dst->dy = 0.0f;
    dst->pressure = 0.0f;
    dst->windowID = gp->touch_window_id;

    return 1;
}

static int
Remapper_EmitGamepadAxis(const SDL_GamepadButtonEvent *src,
                         const SDL_RemapperAction *action,
                         SDL_Event *out_event)
{
    SDL_GamepadAxisEvent *dst;
    Sint16 value;

    if (!src || !action || !out_event) {
        return 0;
    }

    if (action->kind != SDL_REMAPPER_ACTION_GAMEPAD_AXIS) {
        return 0;
    }

    dst = &out_event->gaxis;
    dst->type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    dst->reserved = 0;
    dst->timestamp = src->timestamp;
    dst->which = src->which;
    dst->axis = (Uint8)action->code;
    dst->padding1 = 0;
    dst->padding2 = 0;
    dst->padding3 = 0;

    if (src->down) {
        if (action->value == 0) {
            value = SDL_JOYSTICK_AXIS_MAX;
        } else if (action->value < SDL_JOYSTICK_AXIS_MIN) {
            value = SDL_JOYSTICK_AXIS_MIN;
        } else if (action->value > SDL_JOYSTICK_AXIS_MAX) {
            value = SDL_JOYSTICK_AXIS_MAX;
        } else {
            value = (Sint16)action->value;
        }
    } else {
        value = 0;
    }

    dst->value = value;
    dst->padding4 = 0;

    return 1;
}

static int
Remapper_EmitKeyboardKey(const SDL_GamepadButtonEvent *src,
                         const SDL_RemapperAction *action,
                         SDL_Event *out_event)
{
    SDL_KeyboardEvent *dst;
    SDL_Keymod modstate = SDL_KMOD_NONE;

    if (!src || !action || !out_event) {
        return 0;
    }

    if (action->kind != SDL_REMAPPER_ACTION_KEYBOARD_KEY) {
        return 0;
    }

    dst = &out_event->key;
    dst->type = src->down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    dst->reserved = 0;
    dst->timestamp = src->timestamp;
    dst->windowID = 0;      /* no specific window focus */
    dst->which = 0;         /* virtual / unknown keyboard */
    dst->scancode = (SDL_Scancode)action->code;
    dst->mod = modstate;
    dst->key = SDL_GetKeyFromScancode(dst->scancode, modstate, true);
    dst->raw = 0;
    dst->down = src->down;
    dst->repeat = false;

    return 1;
}

static int
Remapper_EmitMouseButton(const SDL_GamepadButtonEvent *src,
                         const SDL_RemapperAction *action,
                         SDL_Event *out_event)
{
    SDL_MouseButtonEvent *dst;

    if (!src || !action || !out_event) {
        return 0;
    }

    if (action->kind != SDL_REMAPPER_ACTION_MOUSE_BUTTON) {
        return 0;
    }

    dst = &out_event->button;
    dst->type = src->down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    dst->reserved = 0;
    dst->timestamp = src->timestamp;
    dst->windowID = 0;      /* no specific window focus */
    dst->which = 0;         /* virtual / unknown mouse */
    dst->button = (Uint8)action->code;
    dst->down = src->down;
    dst->clicks = 1;
    dst->padding = 0;
    dst->x = 0.0f;
    dst->y = 0.0f;

    return 1;
}

static int
Remapper_EmitMouseWheel(const SDL_GamepadButtonEvent *src,
                        const SDL_RemapperAction *action,
                        SDL_Event *out_event)
{
    SDL_MouseWheelEvent *dst;
    float amount;

    if (!src || !action || !out_event) {
        return 0;
    }

    if (action->kind != SDL_REMAPPER_ACTION_MOUSE_WHEEL) {
        return 0;
    }

    dst = &out_event->wheel;
    dst->type = SDL_EVENT_MOUSE_WHEEL;
    dst->reserved = 0;
    dst->timestamp = src->timestamp;
    dst->windowID = 0;      /* no specific window focus */
    dst->which = 0;         /* virtual / unknown mouse */

    amount = (float)action->value;
    if (amount == 0.0f) {
        amount = 1.0f;
    }

    /* action->code == 1 -> horizontal scroll, otherwise vertical */
    if (action->code == 1) {
        dst->x = amount;
        dst->y = 0.0f;
    } else {
        dst->x = 0.0f;
        dst->y = amount;
    }

    dst->direction = SDL_MOUSEWHEEL_NORMAL;
    dst->mouse_x = 0.0f;
    dst->mouse_y = 0.0f;
    dst->integer_x = (Sint32)dst->x;
    dst->integer_y = (Sint32)dst->y;

    return 1;
}

static int
Remapper_EmitMouseMovement(const SDL_GamepadAxisEvent *src,
                           const SDL_RemapperAction *action,
                           SDL_Event *out_event)
{
    SDL_MouseMotionEvent *dst;
    float sensitivity = 1.0f;

    if (!src || !action || !out_event) {
        return 0;
    }

    if (action->kind != SDL_REMAPPER_ACTION_MOUSE_MOVEMENT) {
        return 0;
    }

    dst = &out_event->motion;
    dst->type = SDL_EVENT_MOUSE_MOTION;
    dst->reserved = 0;
    dst->timestamp = src->timestamp;
    dst->windowID = 0;      /* no specific window focus */
    dst->which = 0;         /* virtual / unknown mouse */
    dst->state = 0;         /* no buttons pressed */

    /* action->code indicates axis: 0=X, 1=Y */
    /* action->value holds sensitivity scaling */
    if (action->value > 0) {
        sensitivity = (float)action->value / 100.0f;
    }

    float motion = ((float)src->value / 32767.0f) * sensitivity * 10.0f;

    if (action->code == 0) { /* X axis */
        dst->xrel = motion;
        dst->yrel = 0.0f;
    } else { /* Y axis */
        dst->xrel = 0.0f;
        dst->yrel = motion;
    }

    dst->x = 0.0f;
    dst->y = 0.0f;

    return 1;
}

static int
Remapper_HandleStickToKeys(const SDL_GamepadAxisEvent *aev,
                          const SDL_RemapperStickMapping *sm,
                          SDL_Event *out_event,
                          bool use_wasd)
{
    SDL_KeyboardEvent *dst;
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    bool pressed = false;
    float threshold = 16000.0f;
    float axis_val = (float)aev->value;

    if (!aev || !sm || !out_event) {
        return 0;
    }

    /* Apply inversion */
    if ((aev->axis == SDL_GAMEPAD_AXIS_LEFTX || aev->axis == SDL_GAMEPAD_AXIS_RIGHTX) && sm->invert_horizontal) {
        axis_val = -axis_val;
    }
    if ((aev->axis == SDL_GAMEPAD_AXIS_LEFTY || aev->axis == SDL_GAMEPAD_AXIS_RIGHTY) && sm->invert_vertical) {
        axis_val = -axis_val;
    }

    /* Determine which key to press based on axis and direction */
    if (aev->axis == SDL_GAMEPAD_AXIS_LEFTX || aev->axis == SDL_GAMEPAD_AXIS_RIGHTX) {
        /* Horizontal axis */
        if (axis_val > threshold) {
            /* Right */
            scancode = use_wasd ? SDL_SCANCODE_D : SDL_SCANCODE_RIGHT;
            pressed = true;
        } else if (axis_val < -threshold) {
            /* Left */
            scancode = use_wasd ? SDL_SCANCODE_A : SDL_SCANCODE_LEFT;
            pressed = true;
        }
    } else if (aev->axis == SDL_GAMEPAD_AXIS_LEFTY || aev->axis == SDL_GAMEPAD_AXIS_RIGHTY) {
        /* Vertical axis */
        if (axis_val > threshold) {
            /* Down */
            scancode = use_wasd ? SDL_SCANCODE_S : SDL_SCANCODE_DOWN;
            pressed = true;
        } else if (axis_val < -threshold) {
            /* Up */
            scancode = use_wasd ? SDL_SCANCODE_W : SDL_SCANCODE_UP;
            pressed = true;
        }
    }

    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return 0;
    }

    dst = &out_event->key;
    dst->type = pressed ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    dst->reserved = 0;
    dst->timestamp = aev->timestamp;
    dst->windowID = 0;
    dst->which = 0;
    dst->scancode = scancode;
    dst->mod = SDL_KMOD_NONE;
    dst->key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, true);
    dst->raw = 0;
    dst->down = pressed;
    dst->repeat = false;

    return 1;
}

static int
Remapper_HandleStickToDPad(const SDL_GamepadAxisEvent *aev,
                          const SDL_RemapperStickMapping *sm,
                          SDL_Event *out_event)
{
    SDL_GamepadButtonEvent *dst;
    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
    bool pressed = false;
    float threshold = 16000.0f;
    float axis_val = (float)aev->value;

    if (!aev || !sm || !out_event) {
        return 0;
    }

    /* Apply inversion */
    if ((aev->axis == SDL_GAMEPAD_AXIS_LEFTX || aev->axis == SDL_GAMEPAD_AXIS_RIGHTX) && sm->invert_horizontal) {
        axis_val = -axis_val;
    }
    if ((aev->axis == SDL_GAMEPAD_AXIS_LEFTY || aev->axis == SDL_GAMEPAD_AXIS_RIGHTY) && sm->invert_vertical) {
        axis_val = -axis_val;
    }

    /* Determine which dpad button based on axis and direction */
    if (aev->axis == SDL_GAMEPAD_AXIS_LEFTX || aev->axis == SDL_GAMEPAD_AXIS_RIGHTX) {
        /* Horizontal axis */
        if (axis_val > threshold) {
            button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
            pressed = true;
        } else if (axis_val < -threshold) {
            button = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            pressed = true;
        }
    } else if (aev->axis == SDL_GAMEPAD_AXIS_LEFTY || aev->axis == SDL_GAMEPAD_AXIS_RIGHTY) {
        /* Vertical axis */
        if (axis_val > threshold) {
            button = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
            pressed = true;
        } else if (axis_val < -threshold) {
            button = SDL_GAMEPAD_BUTTON_DPAD_UP;
            pressed = true;
        }
    }

    if (button == SDL_GAMEPAD_BUTTON_INVALID) {
        return 0;
    }

    dst = &out_event->gbutton;
    dst->type = pressed ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    dst->reserved = 0;
    dst->timestamp = aev->timestamp;
    dst->which = aev->which;
    dst->button = (Uint8)button;
    dst->down = pressed;
    dst->padding1 = 0;
    dst->padding2 = 0;

    return 1;
}

static int
Remapper_HandleMouseMotionToKeys(SDL_RemapperMouseState *ms,
                                 const SDL_MouseMotionEvent *mev,
                                 const SDL_RemapperStickMapping *sm,
                                 SDL_Event *out_event,
                                 bool use_wasd)
{
    SDL_KeyboardEvent *dst;
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    float threshold = 4.0f;
    float dx, dy;
    float abs_dx, abs_dy;
    SDL_RemapperMouseDirection new_dir = SDL_REMAPPER_MOUSE_DIR_NONE;
    SDL_RemapperMouseDirection old_dir;

    if (!ms || !mev || !sm || !out_event) {
        return 0;
    }

    dx = (float)mev->xrel;
    dy = (float)mev->yrel;

    /* Apply inversion */
    if (sm->invert_horizontal) {
        dx = -dx;
    }
    if (sm->invert_vertical) {
        dy = -dy;
    }

    /* Adjust threshold for gyro / touch mouse modes to change sensitivity. */
    if (sm->map_to_gyroscope) {
        threshold *= 0.75f;
    } else if (sm->map_to_touch_mouse) {
        threshold *= 1.25f;
    }

    abs_dx = SDL_fabsf(dx);
    abs_dy = SDL_fabsf(dy);

    /* Determine new dominant motion direction, if any. */
    if (abs_dx >= abs_dy && abs_dx >= threshold) {
        new_dir = (dx > 0.0f) ? SDL_REMAPPER_MOUSE_DIR_RIGHT : SDL_REMAPPER_MOUSE_DIR_LEFT;
    } else if (abs_dy > abs_dx && abs_dy >= threshold) {
        new_dir = (dy > 0.0f) ? SDL_REMAPPER_MOUSE_DIR_DOWN : SDL_REMAPPER_MOUSE_DIR_UP;
    }

    old_dir = ms->key_motion_dir;

    /* If we had a direction active and it changed or stopped, emit a key up. */
    if (old_dir != SDL_REMAPPER_MOUSE_DIR_NONE &&
        (new_dir == SDL_REMAPPER_MOUSE_DIR_NONE || new_dir != old_dir) &&
        ms->key_motion_dir_down_sent) {

        switch (old_dir) {
        case SDL_REMAPPER_MOUSE_DIR_LEFT:
            scancode = use_wasd ? SDL_SCANCODE_A : SDL_SCANCODE_LEFT;
            break;
        case SDL_REMAPPER_MOUSE_DIR_RIGHT:
            scancode = use_wasd ? SDL_SCANCODE_D : SDL_SCANCODE_RIGHT;
            break;
        case SDL_REMAPPER_MOUSE_DIR_UP:
            scancode = use_wasd ? SDL_SCANCODE_W : SDL_SCANCODE_UP;
            break;
        case SDL_REMAPPER_MOUSE_DIR_DOWN:
            scancode = use_wasd ? SDL_SCANCODE_S : SDL_SCANCODE_DOWN;
            break;
        default:
            scancode = SDL_SCANCODE_UNKNOWN;
            break;
        }

        if (scancode != SDL_SCANCODE_UNKNOWN) {
            dst = &out_event->key;
            dst->type = SDL_EVENT_KEY_UP;
            dst->reserved = 0;
            dst->timestamp = mev->timestamp;
            dst->windowID = 0;
            dst->which = 0;
            dst->scancode = scancode;
            dst->mod = SDL_KMOD_NONE;
            dst->key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, true);
            dst->raw = 0;
            dst->down = false;
            dst->repeat = false;

            ms->key_motion_dir = SDL_REMAPPER_MOUSE_DIR_NONE;
            ms->key_motion_dir_down_sent = false;
            return 1;
        }
    }

    /* If no direction is currently active, start a new one with a key down. */
    if (new_dir != SDL_REMAPPER_MOUSE_DIR_NONE && old_dir == SDL_REMAPPER_MOUSE_DIR_NONE) {
        switch (new_dir) {
        case SDL_REMAPPER_MOUSE_DIR_LEFT:
            scancode = use_wasd ? SDL_SCANCODE_A : SDL_SCANCODE_LEFT;
            break;
        case SDL_REMAPPER_MOUSE_DIR_RIGHT:
            scancode = use_wasd ? SDL_SCANCODE_D : SDL_SCANCODE_RIGHT;
            break;
        case SDL_REMAPPER_MOUSE_DIR_UP:
            scancode = use_wasd ? SDL_SCANCODE_W : SDL_SCANCODE_UP;
            break;
        case SDL_REMAPPER_MOUSE_DIR_DOWN:
            scancode = use_wasd ? SDL_SCANCODE_S : SDL_SCANCODE_DOWN;
            break;
        default:
            scancode = SDL_SCANCODE_UNKNOWN;
            break;
        }

        if (scancode != SDL_SCANCODE_UNKNOWN) {
            dst = &out_event->key;
            dst->type = SDL_EVENT_KEY_DOWN;
            dst->reserved = 0;
            dst->timestamp = mev->timestamp;
            dst->windowID = 0;
            dst->which = 0;
            dst->scancode = scancode;
            dst->mod = SDL_KMOD_NONE;
            dst->key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, true);
            dst->raw = 0;
            dst->down = true;
            dst->repeat = false;

            ms->key_motion_dir = new_dir;
            ms->key_motion_dir_down_sent = true;
            return 1;
        }
    }

    return 0;
}

static int
Remapper_HandleMouseMotionToDPad(SDL_RemapperMouseState *ms,
                                 const SDL_MouseMotionEvent *mev,
                                 const SDL_RemapperStickMapping *sm,
                                 SDL_Event *out_event)
{
    SDL_GamepadButtonEvent *dst;
    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
    float threshold = 4.0f;
    float dx, dy;
    float abs_dx, abs_dy;
    SDL_RemapperMouseDirection new_dir = SDL_REMAPPER_MOUSE_DIR_NONE;
    SDL_RemapperMouseDirection old_dir;

    if (!ms || !mev || !sm || !out_event) {
        return 0;
    }

    dx = (float)mev->xrel;
    dy = (float)mev->yrel;

    /* Apply inversion */
    if (sm->invert_horizontal) {
        dx = -dx;
    }
    if (sm->invert_vertical) {
        dy = -dy;
    }

    /* Adjust threshold for gyro / touch mouse modes to change sensitivity. */
    if (sm->map_to_gyroscope) {
        threshold *= 0.75f;
    } else if (sm->map_to_touch_mouse) {
        threshold *= 1.25f;
    }

    abs_dx = SDL_fabsf(dx);
    abs_dy = SDL_fabsf(dy);

    /* Determine new dominant motion direction, if any. */
    if (abs_dx >= abs_dy && abs_dx >= threshold) {
        new_dir = (dx > 0.0f) ? SDL_REMAPPER_MOUSE_DIR_RIGHT : SDL_REMAPPER_MOUSE_DIR_LEFT;
    } else if (abs_dy > abs_dx && abs_dy >= threshold) {
        new_dir = (dy > 0.0f) ? SDL_REMAPPER_MOUSE_DIR_DOWN : SDL_REMAPPER_MOUSE_DIR_UP;
    }

    old_dir = ms->dpad_motion_dir;

    /* If we had a direction active and it changed or stopped, emit a button up. */
    if (old_dir != SDL_REMAPPER_MOUSE_DIR_NONE &&
        (new_dir == SDL_REMAPPER_MOUSE_DIR_NONE || new_dir != old_dir) &&
        ms->dpad_motion_dir_down_sent) {

        switch (old_dir) {
        case SDL_REMAPPER_MOUSE_DIR_LEFT:
            button = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            break;
        case SDL_REMAPPER_MOUSE_DIR_RIGHT:
            button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
            break;
        case SDL_REMAPPER_MOUSE_DIR_UP:
            button = SDL_GAMEPAD_BUTTON_DPAD_UP;
            break;
        case SDL_REMAPPER_MOUSE_DIR_DOWN:
            button = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
            break;
        default:
            button = SDL_GAMEPAD_BUTTON_INVALID;
            break;
        }

        if (button != SDL_GAMEPAD_BUTTON_INVALID) {
            dst = &out_event->gbutton;
            dst->type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            dst->reserved = 0;
            dst->timestamp = mev->timestamp;
            dst->which = 0; /* virtual gamepad */
            dst->button = (Uint8)button;
            dst->down = false;
            dst->padding1 = 0;
            dst->padding2 = 0;

            ms->dpad_motion_dir = SDL_REMAPPER_MOUSE_DIR_NONE;
            ms->dpad_motion_dir_down_sent = false;
            return 1;
        }
    }

    /* If no direction is currently active, start a new one with a button down. */
    if (new_dir != SDL_REMAPPER_MOUSE_DIR_NONE && old_dir == SDL_REMAPPER_MOUSE_DIR_NONE) {
        switch (new_dir) {
        case SDL_REMAPPER_MOUSE_DIR_LEFT:
            button = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            break;
        case SDL_REMAPPER_MOUSE_DIR_RIGHT:
            button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
            break;
        case SDL_REMAPPER_MOUSE_DIR_UP:
            button = SDL_GAMEPAD_BUTTON_DPAD_UP;
            break;
        case SDL_REMAPPER_MOUSE_DIR_DOWN:
            button = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
            break;
        default:
            button = SDL_GAMEPAD_BUTTON_INVALID;
            break;
        }

        if (button != SDL_GAMEPAD_BUTTON_INVALID) {
            dst = &out_event->gbutton;
            dst->type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            dst->reserved = 0;
            dst->timestamp = mev->timestamp;
            dst->which = 0; /* virtual gamepad */
            dst->button = (Uint8)button;
            dst->down = true;
            dst->padding1 = 0;
            dst->padding2 = 0;

            ms->dpad_motion_dir = new_dir;
            ms->dpad_motion_dir_down_sent = true;
            return 1;
        }
    }

    return 0;
}

SDL_RemapperContext * SDLCALL SDL_CreateRemapper_REAL(void)
{
    SDL_RemapperContext *ctx = (SDL_RemapperContext *)SDL_calloc(1, sizeof(*ctx));
    if (!ctx) {
        SDL_OutOfMemory();
        return NULL;
    }

    ctx->hold_threshold_ns = SDL_MS_TO_NS(SDL_REMAPPER_HOLD_THRESHOLD_MS);
    return ctx;
}

void SDLCALL SDL_DestroyRemapper_REAL(SDL_RemapperContext *ctx)
{
    if (!ctx) {
        return;
    }

    SDL_free(ctx->gamepads);
    SDL_free(ctx->mice);
    SDL_free(ctx->keyboards);
    SDL_free(ctx);
}

int SDLCALL SDL_SetRemapperProfile_REAL(SDL_RemapperContext *ctx,
                                   SDL_JoystickID gamepad_id,
                                   const SDL_RemapperProfile *profile)
{
    SDL_RemapperGamepadState *gp;

    if (!ctx) {
        return SDL_InvalidParamError("ctx");
    }
    if (gamepad_id == 0) {
        return SDL_InvalidParamError("gamepad_id");
    }

    if (!profile) {
        /* Clear profile and button state for this gamepad */
        gp = Remapper_FindGamepad(ctx, gamepad_id);
        if (gp) {
            gp->profile = NULL;
            SDL_memset(gp->button_states, 0, sizeof(gp->button_states));
        }
        return 0;
    }

    gp = Remapper_GetOrAddGamepad(ctx, gamepad_id);
    if (!gp) {
        return -1; /* SDL_OutOfMemory already set */
    }

    gp->profile = profile;
    SDL_memset(gp->button_states, 0, sizeof(gp->button_states));
    return 0;
}

int SDLCALL SDL_SetRemapperMouseProfile_REAL(SDL_RemapperContext *ctx,
                                             SDL_MouseID mouse_id,
                                             const SDL_RemapperProfile *profile)
{
    SDL_RemapperMouseState *ms;

    if (!ctx) {
        return SDL_InvalidParamError("ctx");
    }
    /* Note: mouse_id 0 is valid - it represents the default/virtual mouse */

    if (!profile) {
        /* Clear profile and button/motion state for this mouse */
        ms = Remapper_FindMouse(ctx, mouse_id);
        if (ms) {
            ms->profile = NULL;
            SDL_memset(ms->button_states, 0, sizeof(ms->button_states));
            ms->key_motion_dir = SDL_REMAPPER_MOUSE_DIR_NONE;
            ms->key_motion_dir_down_sent = false;
            ms->dpad_motion_dir = SDL_REMAPPER_MOUSE_DIR_NONE;
            ms->dpad_motion_dir_down_sent = false;
        }
        return 0;
    }

    ms = Remapper_GetOrAddMouse(ctx, mouse_id);
    if (!ms) {
        return -1; /* SDL_OutOfMemory already set */
    }

    ms->profile = profile;
    SDL_memset(ms->button_states, 0, sizeof(ms->button_states));
    ms->key_motion_dir = SDL_REMAPPER_MOUSE_DIR_NONE;
    ms->key_motion_dir_down_sent = false;
    ms->dpad_motion_dir = SDL_REMAPPER_MOUSE_DIR_NONE;
    ms->dpad_motion_dir_down_sent = false;
    return 0;
}

int SDLCALL SDL_SetRemapperKeyboardProfile_REAL(SDL_RemapperContext *ctx,
                                                SDL_KeyboardID keyboard_id,
                                                const SDL_RemapperProfile *profile)
{
    SDL_RemapperKeyboardState *ks;

    if (!ctx) {
        return SDL_InvalidParamError("ctx");
    }
    if (keyboard_id == 0) {
        return SDL_InvalidParamError("keyboard_id");
    }

    if (!profile) {
        ks = Remapper_FindKeyboard(ctx, keyboard_id);
        if (ks) {
            ks->profile = NULL;
            SDL_memset(ks->key_states, 0, sizeof(ks->key_states));
        }
        return 0;
    }

    ks = Remapper_GetOrAddKeyboard(ctx, keyboard_id);
    if (!ks) {
        return -1;
    }

    ks->profile = profile;
    SDL_memset(ks->key_states, 0, sizeof(ks->key_states));
    return 0;
}

int SDLCALL SDL_ProcessRemappedEvent_REAL(SDL_RemapperContext *ctx,
                                     const SDL_Event *in_event,
                                     SDL_Event *out_events,
                                     int max_out_events)
{
    const SDL_GamepadButtonEvent *bev;
    SDL_RemapperGamepadState *gp;
    SDL_RemapperMouseState *ms;
    SDL_RemapperKeyboardState *ks;
    const SDL_RemapperMapping *mapping;
    SDL_RemapperButtonStateInternal *btn_state;
    bool pressed;
    bool is_hold = false;
    bool shift_active;
    Uint64 now;
    const SDL_RemapperAction *action;

    if (!in_event || !out_events || max_out_events <= 0) {
        return 0;
    }

    if (!ctx) {
        out_events[0] = *in_event;
        return 1;
    }

    /* Handle keyboard key events as remapper sources */
    if (in_event->type == SDL_EVENT_KEY_DOWN ||
        in_event->type == SDL_EVENT_KEY_UP) {
        const SDL_KeyboardEvent *kev = &in_event->key;
        SDL_Scancode scancode = kev->scancode;
        int sc_index = (int)scancode;

        if (sc_index < 0 || sc_index >= SDL_REMAPPER_MAX_KEYS) {
            out_events[0] = *in_event;
            return 1;
        }

        ks = Remapper_FindKeyboard(ctx, kev->which);
        if (!ks || !ks->profile) {
            out_events[0] = *in_event;
            return 1;
        }

        mapping = Remapper_FindKeyboardKeyMapping(ks->profile, scancode);
        if (!mapping) {
            out_events[0] = *in_event;
            return 1;
        }

        btn_state = &ks->key_states[sc_index];
        pressed = kev->down ? true : false;
        now = SDL_GetTicksNS();
        is_hold = false;

        if (pressed) {
            btn_state->down = true;
            btn_state->press_timestamp_ns = now;
        } else {
            if (btn_state->down &&
                btn_state->press_timestamp_ns != 0 &&
                (now - btn_state->press_timestamp_ns) >= ctx->hold_threshold_ns) {
                is_hold = true;
            }
            btn_state->down = false;
            btn_state->press_timestamp_ns = 0;
        }

        shift_active = Remapper_KeyboardIsShiftActive(ks);
        action = Remapper_ChooseAction(mapping, shift_active, is_hold);

        if (!action || action->kind == SDL_REMAPPER_ACTION_NONE) {
            out_events[0] = *in_event;
            return 1;
        }

        switch (action->kind) {
        case SDL_REMAPPER_ACTION_GAMEPAD_BUTTON: {
            SDL_GamepadButtonEvent *dst = &out_events[0].gbutton;
            dst->type = kev->down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
            dst->reserved = 0;
            dst->timestamp = kev->timestamp;
            dst->which = 0;
            dst->button = (Uint8)action->code;
            dst->down = kev->down;
            dst->padding1 = 0;
            dst->padding2 = 0;
            return 1;
        }
        case SDL_REMAPPER_ACTION_GAMEPAD_AXIS: {
            SDL_GamepadAxisEvent *dst = &out_events[0].gaxis;
            Sint16 value;

            dst->type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
            dst->reserved = 0;
            dst->timestamp = kev->timestamp;
            dst->which = 0;
            dst->axis = (Uint8)action->code;
            dst->padding1 = 0;
            dst->padding2 = 0;
            dst->padding3 = 0;

            if (kev->down) {
                if (action->value == 0) {
                    value = SDL_JOYSTICK_AXIS_MAX;
                } else if (action->value < SDL_JOYSTICK_AXIS_MIN) {
                    value = SDL_JOYSTICK_AXIS_MIN;
                } else if (action->value > SDL_JOYSTICK_AXIS_MAX) {
                    value = SDL_JOYSTICK_AXIS_MAX;
                } else {
                    value = (Sint16)action->value;
                }
            } else {
                value = 0;
            }

            dst->value = value;
            dst->padding4 = 0;
            return 1;
        }
        case SDL_REMAPPER_ACTION_KEYBOARD_KEY: {
            SDL_KeyboardEvent *dst = &out_events[0].key;
            SDL_Keymod modstate = SDL_KMOD_NONE;

            dst->type = kev->down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            dst->reserved = 0;
            dst->timestamp = kev->timestamp;
            dst->windowID = 0;
            dst->which = 0;
            dst->scancode = (SDL_Scancode)action->code;
            dst->mod = modstate;
            dst->key = SDL_GetKeyFromScancode(dst->scancode, modstate, true);
            dst->raw = 0;
            dst->down = kev->down;
            dst->repeat = false;
            return 1;
        }
        case SDL_REMAPPER_ACTION_MOUSE_BUTTON: {
            SDL_MouseButtonEvent *dst = &out_events[0].button;

            SDL_zero(*dst);
            dst->type = kev->down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            dst->timestamp = kev->timestamp;
            dst->windowID = 0;
            dst->which = 0;
            dst->button = (Uint8)action->code;
            dst->down = kev->down;
            dst->clicks = 1;
            return 1;
        }
        case SDL_REMAPPER_ACTION_MOUSE_WHEEL: {
            SDL_MouseWheelEvent *dst = &out_events[0].wheel;
            float amount = (float)action->value;

            if (amount == 0.0f) {
                amount = 1.0f;
            }

            dst->type = SDL_EVENT_MOUSE_WHEEL;
            dst->reserved = 0;
            dst->timestamp = kev->timestamp;
            dst->windowID = 0;
            dst->which = 0;

            if (action->code == 1) {
                dst->x = amount;
                dst->y = 0.0f;
            } else {
                dst->x = 0.0f;
                dst->y = amount;
            }

            dst->direction = SDL_MOUSEWHEEL_NORMAL;
            dst->mouse_x = 0.0f;
            dst->mouse_y = 0.0f;
            dst->integer_x = (Sint32)dst->x;
            dst->integer_y = (Sint32)dst->y;
            return 1;
        }
        case SDL_REMAPPER_ACTION_MOUSE_MOVEMENT: {
            SDL_MouseMotionEvent *dst = &out_events[0].motion;

            SDL_zero(*dst);
            dst->type = SDL_EVENT_MOUSE_MOTION;
            dst->timestamp = kev->timestamp;
            dst->windowID = 0;
            dst->which = 0;
            dst->state = 0;
            dst->x = 0.0f;
            dst->y = 0.0f;
            dst->xrel = 0.0f;
            dst->yrel = 0.0f;
            return 1;
        }
        default:
            return 0;
        }
    }

    /* Handle mouse button events */
    if (in_event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        in_event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        const SDL_MouseButtonEvent *mev = &in_event->button;
        int button_index = (int)mev->button;

        if (button_index < 0 || button_index >= SDL_REMAPPER_MAX_MOUSE_BUTTONS) {
            out_events[0] = *in_event;
            return 1;
        }

        ms = Remapper_FindMouse(ctx, mev->which);
        if (!ms || !ms->profile) {
            out_events[0] = *in_event;
            return 1;
        }

        mapping = Remapper_FindMouseButtonMapping(ms->profile, button_index);
        if (!mapping) {
            out_events[0] = *in_event;
            return 1;
        }

        btn_state = &ms->button_states[button_index];
        pressed = mev->down ? true : false;
        now = SDL_GetTicksNS();
        is_hold = false;

        if (pressed) {
            btn_state->down = true;
            btn_state->press_timestamp_ns = now;
        } else {
            if (btn_state->down &&
                btn_state->press_timestamp_ns != 0 &&
                (now - btn_state->press_timestamp_ns) >= ctx->hold_threshold_ns) {
                is_hold = true;
            }
            btn_state->down = false;
            btn_state->press_timestamp_ns = 0;
        }

        shift_active = Remapper_MouseIsShiftActive(ms);
        action = Remapper_ChooseAction(mapping, shift_active, is_hold);

        if (!action || action->kind == SDL_REMAPPER_ACTION_NONE) {
            out_events[0] = *in_event;
            return 1;
        }

        switch (action->kind) {
        case SDL_REMAPPER_ACTION_GAMEPAD_BUTTON: {
            SDL_GamepadButtonEvent *dst = &out_events[0].gbutton;
            dst->type = mev->down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
            dst->reserved = 0;
            dst->timestamp = mev->timestamp;
            dst->which = 0; /* virtual gamepad */
            dst->button = (Uint8)action->code;
            dst->down = mev->down;
            dst->padding1 = 0;
            dst->padding2 = 0;
            return 1;
        }
        case SDL_REMAPPER_ACTION_GAMEPAD_AXIS: {
            SDL_GamepadAxisEvent *dst = &out_events[0].gaxis;
            Sint16 value;

            dst->type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
            dst->reserved = 0;
            dst->timestamp = mev->timestamp;
            dst->which = 0; /* virtual gamepad */
            dst->axis = (Uint8)action->code;
            dst->padding1 = 0;
            dst->padding2 = 0;
            dst->padding3 = 0;

            if (mev->down) {
                if (action->value == 0) {
                    value = SDL_JOYSTICK_AXIS_MAX;
                } else if (action->value < SDL_JOYSTICK_AXIS_MIN) {
                    value = SDL_JOYSTICK_AXIS_MIN;
                } else if (action->value > SDL_JOYSTICK_AXIS_MAX) {
                    value = SDL_JOYSTICK_AXIS_MAX;
                } else {
                    value = (Sint16)action->value;
                }
            } else {
                value = 0;
            }

            dst->value = value;
            dst->padding4 = 0;
            return 1;
        }
        case SDL_REMAPPER_ACTION_KEYBOARD_KEY: {
            SDL_KeyboardEvent *dst = &out_events[0].key;
            SDL_Keymod modstate = SDL_KMOD_NONE;

            dst->type = mev->down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            dst->reserved = 0;
            dst->timestamp = mev->timestamp;
            dst->windowID = 0;
            dst->which = 0;
            dst->scancode = (SDL_Scancode)action->code;
            dst->mod = modstate;
            dst->key = SDL_GetKeyFromScancode(dst->scancode, modstate, true);
            dst->raw = 0;
            dst->down = mev->down;
            dst->repeat = false;
            return 1;
        }
        case SDL_REMAPPER_ACTION_MOUSE_BUTTON: {
            SDL_MouseButtonEvent *dst = &out_events[0].button;

            *dst = *mev;
            dst->button = (Uint8)action->code;
            return 1;
        }
        case SDL_REMAPPER_ACTION_MOUSE_WHEEL: {
            SDL_MouseWheelEvent *dst = &out_events[0].wheel;
            float amount = (float)action->value;

            if (amount == 0.0f) {
                amount = 1.0f;
            }

            dst->type = SDL_EVENT_MOUSE_WHEEL;
            dst->reserved = 0;
            dst->timestamp = mev->timestamp;
            dst->windowID = 0;
            dst->which = mev->which;

            /* action->code == 1 -> horizontal scroll, otherwise vertical */
            if (action->code == 1) {
                dst->x = amount;
                dst->y = 0.0f;
            } else {
                dst->x = 0.0f;
                dst->y = amount;
            }

            dst->direction = SDL_MOUSEWHEEL_NORMAL;
            dst->mouse_x = 0.0f;
            dst->mouse_y = 0.0f;
            dst->integer_x = (Sint32)dst->x;
            dst->integer_y = (Sint32)dst->y;
            return 1;
        }
        case SDL_REMAPPER_ACTION_TOUCH_TAP: {
            /* Touch tap at current mouse position: finger down on press, up on release */
            if (mev->down) {
                if (Remapper_EmitTouchFingerDownAtMouse(ms, &out_events[0]) > 0) {
                    return 1;
                }
            } else {
                if (Remapper_EmitTouchFingerUpAtMouse(ms, &out_events[0]) > 0) {
                    return 1;
                }
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_HOLD: {
            /* Toggle finger hold on press only */
            if (mev->down) {
                return Remapper_EmitTouchHold(ms, &out_events[0]);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_DOUBLE_TAP: {
            /* Double tap on press only */
            if (mev->down) {
                return Remapper_EmitTouchDoubleTap(ms, out_events);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_SWIPE_UP: {
            if (mev->down) {
                return Remapper_EmitTouchSwipe(ms, 0.0f, -SDL_REMAPPER_SWIPE_DISTANCE, out_events);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_SWIPE_DOWN: {
            if (mev->down) {
                return Remapper_EmitTouchSwipe(ms, 0.0f, SDL_REMAPPER_SWIPE_DISTANCE, out_events);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_SWIPE_LEFT: {
            if (mev->down) {
                return Remapper_EmitTouchSwipe(ms, -SDL_REMAPPER_SWIPE_DISTANCE, 0.0f, out_events);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_SWIPE_RIGHT: {
            if (mev->down) {
                return Remapper_EmitTouchSwipe(ms, SDL_REMAPPER_SWIPE_DISTANCE, 0.0f, out_events);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_FINGER2_TAP: {
            /* Finger 2 tap: down on press, up on release */
            if (mev->down) {
                return Remapper_EmitTouchFinger2Down(ms, &out_events[0]);
            } else {
                return Remapper_EmitTouchFinger2Up(ms, &out_events[0]);
            }
        }
        case SDL_REMAPPER_ACTION_TOUCH_FINGER2_HOLD: {
            /* Toggle finger 2 hold on press only */
            if (mev->down) {
                return Remapper_EmitTouchFinger2Hold(ms, &out_events[0]);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_PINCH_IN: {
            if (mev->down) {
                return Remapper_EmitTouchPinch(ms, true, out_events);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_PINCH_OUT: {
            if (mev->down) {
                return Remapper_EmitTouchPinch(ms, false, out_events);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_ROTATE_CW: {
            if (mev->down) {
                return Remapper_EmitTouchRotate(ms, true, out_events);
            }
            return 0;
        }
        case SDL_REMAPPER_ACTION_TOUCH_ROTATE_CCW: {
            if (mev->down) {
                return Remapper_EmitTouchRotate(ms, false, out_events);
            }
            return 0;
        }
        default:
            /* Unknown or unsupported action kind for mouse button source: swallow. */
            return 0;
        }
    }

    /* Handle gamepad button events */
    if (in_event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
        in_event->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {

    bev = &in_event->gbutton;

    gp = Remapper_FindGamepad(ctx, bev->which);
    if (!gp || !gp->profile) {
        out_events[0] = *in_event;
        return 1;
    }

    mapping = Remapper_FindButtonMapping(gp->profile, (SDL_GamepadButton)bev->button);
    if (!mapping) {
        out_events[0] = *in_event;
        return 1;
    }

    btn_state = NULL;
    if (bev->button < SDL_GAMEPAD_BUTTON_COUNT) {
        btn_state = &gp->button_states[bev->button];
    }

    pressed = bev->down ? true : false;
    now = SDL_GetTicksNS();

    if (btn_state) {
        if (pressed) {
            btn_state->down = true;
            btn_state->press_timestamp_ns = now;
        } else {
            if (btn_state->down &&
                btn_state->press_timestamp_ns != 0 &&
                (now - btn_state->press_timestamp_ns) >= ctx->hold_threshold_ns) {
                is_hold = true;
            }
            btn_state->down = false;
            btn_state->press_timestamp_ns = 0;
        }
    }

    shift_active = Remapper_IsShiftActive(gp);
    action = Remapper_ChooseAction(mapping, shift_active, is_hold);

    if (!action || action->kind == SDL_REMAPPER_ACTION_NONE) {
        out_events[0] = *in_event;
        return 1;
    }

    /* If the action maps back to the same physical button, just pass through. */
    if (action->kind == SDL_REMAPPER_ACTION_GAMEPAD_BUTTON &&
        action->code == (int)bev->button) {
        out_events[0] = *in_event;
        return 1;
    }

    switch (action->kind) {
    case SDL_REMAPPER_ACTION_GAMEPAD_BUTTON:
        return Remapper_EmitGamepadButton(bev, action, &out_events[0]);
    case SDL_REMAPPER_ACTION_GAMEPAD_AXIS:
        return Remapper_EmitGamepadAxis(bev, action, &out_events[0]);
    case SDL_REMAPPER_ACTION_KEYBOARD_KEY:
        return Remapper_EmitKeyboardKey(bev, action, &out_events[0]);
    case SDL_REMAPPER_ACTION_MOUSE_BUTTON:
        return Remapper_EmitMouseButton(bev, action, &out_events[0]);
    case SDL_REMAPPER_ACTION_MOUSE_WHEEL:
        return Remapper_EmitMouseWheel(bev, action, &out_events[0]);
    default:
        /* TODO: Implement SDL_REMAPPER_ACTION_GAMEPAD_AXIS and other kinds. */
        break;
    }

        /* Mapping was present but not handled: swallow the physical event for now. */
        return 0;
    }

    /* Handle gamepad axis events */
    if (in_event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        const SDL_GamepadAxisEvent *aev = &in_event->gaxis;
        SDL_RemapperAxisStateInternal *axis_state;

        gp = Remapper_FindGamepad(ctx, aev->which);
        if (!gp || !gp->profile) {
            out_events[0] = *in_event;
            return 1;
        }

        mapping = Remapper_FindAxisMapping(gp->profile, (SDL_GamepadAxis)aev->axis);
        if (!mapping) {
            out_events[0] = *in_event;
            return 1;
        }

        /* Update axis state */
        if (aev->axis < SDL_GAMEPAD_AXIS_COUNT) {
            axis_state = &gp->axis_states[aev->axis];
            axis_state->prev_value = axis_state->value;
            axis_state->value = aev->value;
            axis_state->motion_timestamp_ns = SDL_GetTicksNS();
        }

        /* Handle stick mapping modes */
        if (mapping->stick_mapping) {
            SDL_RemapperStickMapping *sm = mapping->stick_mapping;

            /* Handle WASD mapping */
            if (sm->map_to_wasd) {
                return Remapper_HandleStickToKeys(aev, sm, &out_events[0], true);
            }

            /* Handle Arrow Keys mapping */
            if (sm->map_to_arrow_keys) {
                return Remapper_HandleStickToKeys(aev, sm, &out_events[0], false);
            }

            /* Handle D-Pad mapping */
            if (sm->map_to_dpad) {
                return Remapper_HandleStickToDPad(aev, sm, &out_events[0]);
            }

            /* Gyroscope mode: emit synthetic gamepad sensor updates instead of mouse motion. */
            if (sm->map_to_gyroscope) {
                if (Remapper_EmitSyntheticGyroFromStick(gp, aev, sm, &out_events[0]) > 0) {
                    return 1;
                }
            }

            /* Handle touch screen mode - emit synthetic touch events */
            if (sm->map_to_touch_mouse) {
                /* Use touch_finger setting: 1=first finger, 2=second finger */
                /* Default to first finger for left stick, second for right if not set */
                bool use_finger1;
                if (sm->touch_finger == 2) {
                    use_finger1 = false;
                } else if (sm->touch_finger == 1) {
                    use_finger1 = true;
                } else {
                    /* Auto-detect based on stick (legacy behavior) */
                    use_finger1 = (aev->axis == SDL_GAMEPAD_AXIS_LEFTX || aev->axis == SDL_GAMEPAD_AXIS_LEFTY);
                }
                int count = Remapper_EmitTouchFromStick(gp, aev, sm, use_finger1, out_events);
                if (count > 0) {
                    return count;
                }
                /* If no events emitted but we're in touch mode, swallow the axis event */
                return 0;
            }

            /* Handle mouse movement mode */
            if (sm->map_to_mouse_movement) {
                SDL_RemapperAction mouse_action;
                float base_h = sm->horizontal_sensitivity;
                float base_v = sm->vertical_sensitivity;

                mouse_action.kind = SDL_REMAPPER_ACTION_MOUSE_MOVEMENT;

                /* Determine if this is X or Y axis */
                if (aev->axis == SDL_GAMEPAD_AXIS_LEFTX || aev->axis == SDL_GAMEPAD_AXIS_RIGHTX) {
                    mouse_action.code = 0; /* X axis */
                    mouse_action.value = (int)(base_h);
                } else {
                    mouse_action.code = 1; /* Y axis */
                    mouse_action.value = (int)(base_v);
                }

                return Remapper_EmitMouseMovement(aev, &mouse_action, &out_events[0]);
            }

            /* For controller_movement, we treat this as a hint that the stick
             * should behave like a normal gamepad axis unless there is an
             * explicit primary action mapping. Since we're already in the
             * stick-mapping path, just fall through to the regular action
             * handling below; if no action is set, we pass through the
             * original axis event untouched. */
        }

        /* If no special handling, check for regular action mappings */
        shift_active = Remapper_IsShiftActive(gp);
        action = Remapper_ChooseAction(mapping, shift_active, false);

        if (!action || action->kind == SDL_REMAPPER_ACTION_NONE) {
            out_events[0] = *in_event;
            return 1;
        }

        /* Handle axis-to-action mappings */
        switch (action->kind) {
        case SDL_REMAPPER_ACTION_MOUSE_MOVEMENT:
            return Remapper_EmitMouseMovement(aev, action, &out_events[0]);
        default:
            /* TODO: Handle other action types for axis */
            break;
        }

        out_events[0] = *in_event;
        return 1;
    }

    /* Handle raw mouse motion events */
    if (in_event->type == SDL_EVENT_MOUSE_MOTION) {
        const SDL_MouseMotionEvent *mev = &in_event->motion;

        ms = Remapper_FindMouse(ctx, mev->which);
        if (!ms || !ms->profile) {
            out_events[0] = *in_event;
            return 1;
        }

        mapping = Remapper_FindMouseMotionMapping(ms->profile);
        if (!mapping) {
            out_events[0] = *in_event;
            return 1;
        }

        if (mapping->stick_mapping) {
            const SDL_RemapperStickMapping *sm = mapping->stick_mapping;

            /* Gyroscope mode: emit synthetic sensor updates instead of mouse motion. */
            if (sm->map_to_gyroscope) {
                if (Remapper_EmitSyntheticGyroFromMouse(ms, mev, sm, &out_events[0]) > 0) {
                    return 1;
                }
            }

            if (sm->map_to_wasd) {
                if (Remapper_HandleMouseMotionToKeys(ms, mev, sm, &out_events[0], true)) {
                    return 1;
                }
            }

            if (sm->map_to_arrow_keys) {
                if (Remapper_HandleMouseMotionToKeys(ms, mev, sm, &out_events[0], false)) {
                    return 1;
                }
            }

            if (sm->map_to_dpad) {
                if (Remapper_HandleMouseMotionToDPad(ms, mev, sm, &out_events[0])) {
                    return 1;
                }
            }

            /* Touch mouse mode: emit synthetic touch finger motion. */
            if (sm->map_to_touch_mouse) {
                if (Remapper_EmitTouchFingerMotion(ms, mev, sm, &out_events[0]) > 0) {
                    return 1;
                }
                /* If finger not down, pass through the motion event */
            }
        }

        /* No special handling: pass through raw mouse motion. */
        out_events[0] = *in_event;
        return 1;
    }

    /* Handle mouse wheel events */
    if (in_event->type == SDL_EVENT_MOUSE_WHEEL) {
        const SDL_MouseWheelEvent *wev = &in_event->wheel;
        int wheel_axis = -1;

        if (wev->y > 0.0f) {
            wheel_axis = 0; /* vertical up */
        } else if (wev->y < 0.0f) {
            wheel_axis = 1; /* vertical down */
        } else if (wev->x > 0.0f) {
            wheel_axis = 2; /* horizontal right */
        } else if (wev->x < 0.0f) {
            wheel_axis = 3; /* horizontal left */
        }

        if (wheel_axis < 0) {
            out_events[0] = *in_event;
            return 1;
        }

        ms = Remapper_FindMouse(ctx, wev->which);
        if (!ms || !ms->profile) {
            out_events[0] = *in_event;
            return 1;
        }

        mapping = Remapper_FindMouseWheelMapping(ms->profile, wheel_axis);
        if (!mapping) {
            out_events[0] = *in_event;
            return 1;
        }

        shift_active = Remapper_MouseIsShiftActive(ms);
        action = Remapper_ChooseAction(mapping, shift_active, false);

        if (!action || action->kind == SDL_REMAPPER_ACTION_NONE) {
            out_events[0] = *in_event;
            return 1;
        }

        switch (action->kind) {
        case SDL_REMAPPER_ACTION_MOUSE_WHEEL: {
            SDL_MouseWheelEvent *dst = &out_events[0].wheel;
            float amount = (float)action->value;

            if (amount == 0.0f) {
                /* Preserve original magnitude if value not specified. */
                amount = (wheel_axis < 2) ? wev->y : wev->x;
                if (amount == 0.0f) {
                    amount = 1.0f;
                }
            }

            dst->type = SDL_EVENT_MOUSE_WHEEL;
            dst->reserved = 0;
            dst->timestamp = wev->timestamp;
            dst->windowID = wev->windowID;
            dst->which = wev->which;

            if (wheel_axis < 2) {
                dst->x = 0.0f;
                dst->y = amount;
            } else {
                dst->x = amount;
                dst->y = 0.0f;
            }

            dst->direction = wev->direction;
            dst->mouse_x = wev->mouse_x;
            dst->mouse_y = wev->mouse_y;
            dst->integer_x = (Sint32)dst->x;
            dst->integer_y = (Sint32)dst->y;
            return 1;
        }
        case SDL_REMAPPER_ACTION_KEYBOARD_KEY: {
            SDL_KeyboardEvent *dst = &out_events[0].key;
            SDL_Keymod modstate = SDL_KMOD_NONE;

            dst->type = SDL_EVENT_KEY_DOWN;
            dst->reserved = 0;
            dst->timestamp = wev->timestamp;
            dst->windowID = 0;
            dst->which = 0;
            dst->scancode = (SDL_Scancode)action->code;
            dst->mod = modstate;
            dst->key = SDL_GetKeyFromScancode(dst->scancode, modstate, true);
            dst->raw = 0;
            dst->down = true;
            dst->repeat = false;
            return 1;
        }
        /* Touch actions for mouse wheel */
        case SDL_REMAPPER_ACTION_TOUCH_SWIPE_UP:
            return Remapper_EmitTouchSwipe(ms, 0.0f, -SDL_REMAPPER_SWIPE_DISTANCE, out_events);
        case SDL_REMAPPER_ACTION_TOUCH_SWIPE_DOWN:
            return Remapper_EmitTouchSwipe(ms, 0.0f, SDL_REMAPPER_SWIPE_DISTANCE, out_events);
        case SDL_REMAPPER_ACTION_TOUCH_SWIPE_LEFT:
            return Remapper_EmitTouchSwipe(ms, -SDL_REMAPPER_SWIPE_DISTANCE, 0.0f, out_events);
        case SDL_REMAPPER_ACTION_TOUCH_SWIPE_RIGHT:
            return Remapper_EmitTouchSwipe(ms, SDL_REMAPPER_SWIPE_DISTANCE, 0.0f, out_events);
        case SDL_REMAPPER_ACTION_TOUCH_TAP:
            return Remapper_EmitTouchFingerDownAtMouse(ms, &out_events[0]);
        case SDL_REMAPPER_ACTION_TOUCH_DOUBLE_TAP:
            return Remapper_EmitTouchDoubleTap(ms, out_events);
        case SDL_REMAPPER_ACTION_TOUCH_PINCH_IN:
            return Remapper_EmitTouchPinch(ms, true, out_events);
        case SDL_REMAPPER_ACTION_TOUCH_PINCH_OUT:
            return Remapper_EmitTouchPinch(ms, false, out_events);
        case SDL_REMAPPER_ACTION_TOUCH_ROTATE_CW:
            return Remapper_EmitTouchRotate(ms, true, out_events);
        case SDL_REMAPPER_ACTION_TOUCH_ROTATE_CCW:
            return Remapper_EmitTouchRotate(ms, false, out_events);
        default:
            /* Unknown or unsupported action kind for mouse wheel source: swallow. */
            return 0;
        }
    }

    /* Pass through any other event types */
    out_events[0] = *in_event;
    return 1;
}

bool SDLCALL SDL_PollRemappedEvent_REAL(SDL_RemapperContext *ctx, SDL_Event *event)
{
    SDL_Event in_event;
    SDL_Event out_event;
    int count;

    if (!event) {
        SDL_InvalidParamError("event");
        return false;
    }

    while (SDL_PollEvent(&in_event)) {
        if (!ctx) {
            *event = in_event;
            return true;
        }

        count = SDL_ProcessRemappedEvent(ctx, &in_event, &out_event, 1);
        if (count > 0) {
            *event = out_event;
            return true;
        }
        /* If count == 0, the event was swallowed or produced no output; poll again. */
    }

    return false;
}

/* ===== Profile/Mapping Getter Implementations ===== */

const SDL_RemapperProfile * SDLCALL SDL_GetRemapperProfile_REAL(SDL_RemapperContext *ctx,
                                                                  SDL_JoystickID gamepad_id)
{
    SDL_RemapperGamepadState *gp;

    if (!ctx) {
        return NULL;
    }

    gp = Remapper_FindGamepad(ctx, gamepad_id);
    if (!gp) {
        return NULL;
    }

    return gp->profile;
}

const char * SDLCALL SDL_GetRemapperProfileName_REAL(SDL_RemapperContext *ctx,
                                                       SDL_JoystickID gamepad_id)
{
    const SDL_RemapperProfile *profile = SDL_GetRemapperProfile_REAL(ctx, gamepad_id);

    if (!profile) {
        return NULL;
    }

    return profile->name;
}

const SDL_RemapperMapping * SDLCALL SDL_GetRemapperButtonMapping_REAL(SDL_RemapperContext *ctx,
                                                                        SDL_JoystickID gamepad_id,
                                                                        SDL_GamepadButton button)
{
    const SDL_RemapperProfile *profile = SDL_GetRemapperProfile_REAL(ctx, gamepad_id);

    if (!profile) {
        return NULL;
    }

    return Remapper_FindButtonMapping(profile, button);
}

const SDL_RemapperMapping * SDLCALL SDL_GetRemapperAxisMapping_REAL(SDL_RemapperContext *ctx,
                                                                      SDL_JoystickID gamepad_id,
                                                                      SDL_GamepadAxis axis)
{
    const SDL_RemapperProfile *profile = SDL_GetRemapperProfile_REAL(ctx, gamepad_id);

    if (!profile) {
        return NULL;
    }

    return Remapper_FindAxisMapping(profile, axis);
}

const SDL_RemapperStickMapping * SDLCALL SDL_GetRemapperStickMapping_REAL(SDL_RemapperContext *ctx,
                                                                            SDL_JoystickID gamepad_id,
                                                                            SDL_GamepadAxis axis)
{
    const SDL_RemapperMapping *mapping;
    SDL_GamepadAxis primary_axis;

    /* The UI stores stick mappings only on the X axis (LEFTX, RIGHTX).
     * For Y axes, we need to look up the corresponding X axis. */
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
    case SDL_GAMEPAD_AXIS_LEFTY:
        primary_axis = SDL_GAMEPAD_AXIS_LEFTX;
        break;
    case SDL_GAMEPAD_AXIS_RIGHTX:
    case SDL_GAMEPAD_AXIS_RIGHTY:
        primary_axis = SDL_GAMEPAD_AXIS_RIGHTX;
        break;
    default:
        /* Triggers and other axes - check directly */
        primary_axis = axis;
        break;
    }

    /* First try the primary axis (X axis for sticks) */
    mapping = SDL_GetRemapperAxisMapping_REAL(ctx, gamepad_id, primary_axis);
    if (mapping && mapping->stick_mapping) {
        return mapping->stick_mapping;
    }

    /* If not found on primary, try the actual axis */
    if (primary_axis != axis) {
        mapping = SDL_GetRemapperAxisMapping_REAL(ctx, gamepad_id, axis);
        if (mapping && mapping->stick_mapping) {
            return mapping->stick_mapping;
        }
    }

    return NULL;
}

int SDLCALL SDL_GetRemapperPlayerIndex_REAL(SDL_RemapperContext *ctx,
                                             SDL_JoystickID gamepad_id)
{
    SDL_Gamepad *gp;

    if (!ctx) {
        return -1;
    }

    gp = SDL_GetGamepadFromID(gamepad_id);
    if (!gp) {
        return -1;
    }

    return SDL_GetGamepadPlayerIndex(gp);
}

static const char *Remapper_GetActionKindName(SDL_RemapperActionKind kind)
{
    switch (kind) {
    case SDL_REMAPPER_ACTION_NONE: return "None";
    case SDL_REMAPPER_ACTION_GAMEPAD_BUTTON: return "Gamepad Button";
    case SDL_REMAPPER_ACTION_GAMEPAD_AXIS: return "Gamepad Axis";
    case SDL_REMAPPER_ACTION_KEYBOARD_KEY: return "Keyboard Key";
    case SDL_REMAPPER_ACTION_MOUSE_BUTTON: return "Mouse Button";
    case SDL_REMAPPER_ACTION_MOUSE_WHEEL: return "Mouse Wheel";
    case SDL_REMAPPER_ACTION_MOUSE_MOVEMENT: return "Mouse Movement";
    default: return "Unknown";
    }
}

char * SDLCALL SDL_GetRemapperActionDescription_REAL(const SDL_RemapperAction *action)
{
    char buf[256];

    if (!action || action->kind == SDL_REMAPPER_ACTION_NONE) {
        return SDL_strdup("(none)");
    }

    switch (action->kind) {
    case SDL_REMAPPER_ACTION_GAMEPAD_BUTTON:
        SDL_snprintf(buf, sizeof(buf), "Gamepad Button %s",
                    SDL_GetGamepadStringForButton((SDL_GamepadButton)action->code));
        break;
    case SDL_REMAPPER_ACTION_GAMEPAD_AXIS:
        SDL_snprintf(buf, sizeof(buf), "Gamepad Axis %s",
                    SDL_GetGamepadStringForAxis((SDL_GamepadAxis)action->code));
        break;
    case SDL_REMAPPER_ACTION_KEYBOARD_KEY:
        SDL_snprintf(buf, sizeof(buf), "Keyboard Key %s",
                    SDL_GetScancodeName((SDL_Scancode)action->code));
        break;
    case SDL_REMAPPER_ACTION_MOUSE_BUTTON:
        SDL_snprintf(buf, sizeof(buf), "Mouse Button %d", action->code);
        break;
    case SDL_REMAPPER_ACTION_MOUSE_WHEEL:
        SDL_snprintf(buf, sizeof(buf), "Mouse Wheel %s",
                    action->code == 1 ? "Horizontal" : "Vertical");
        break;
    case SDL_REMAPPER_ACTION_MOUSE_MOVEMENT:
        SDL_snprintf(buf, sizeof(buf), "Mouse Movement %s",
                    action->code == 0 ? "X" : "Y");
        break;
    default:
        SDL_snprintf(buf, sizeof(buf), "%s (code=%d)",
                    Remapper_GetActionKindName(action->kind), action->code);
        break;
    }

    return SDL_strdup(buf);
}

char * SDLCALL SDL_GetRemapperStickMappingDescription_REAL(const SDL_RemapperStickMapping *mapping)
{
    char buf[512];
    char *p = buf;
    int remaining = sizeof(buf);
    int written;

    if (!mapping) {
        return SDL_strdup("(none)");
    }

    p[0] = '\0';

    if (mapping->map_to_wasd) {
        written = SDL_snprintf(p, remaining, "WASD ");
        p += written; remaining -= written;
    }
    if (mapping->map_to_arrow_keys) {
        written = SDL_snprintf(p, remaining, "Arrows ");
        p += written; remaining -= written;
    }
    if (mapping->map_to_mouse_movement) {
        written = SDL_snprintf(p, remaining, "Mouse ");
        p += written; remaining -= written;
    }
    if (mapping->map_to_controller_movement) {
        written = SDL_snprintf(p, remaining, "Controller ");
        p += written; remaining -= written;
    }
    if (mapping->map_to_dpad) {
        written = SDL_snprintf(p, remaining, "D-Pad ");
        p += written; remaining -= written;
    }
    if (mapping->map_to_gyroscope) {
        written = SDL_snprintf(p, remaining, "Gyroscope ");
        p += written; remaining -= written;
    }
    if (mapping->map_to_touch_mouse) {
        written = SDL_snprintf(p, remaining, "TouchMouse ");
        p += written; remaining -= written;
    }

    if (p == buf) {
        return SDL_strdup("(passthrough)");
    }

    /* Trim trailing space */
    if (p > buf && *(p-1) == ' ') {
        *(p-1) = '\0';
    }

    return SDL_strdup(buf);
}
