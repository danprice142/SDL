/* SDL3 Remapper Default Profiles Implementation */

 #include "SDL_internal.h"

 #include "SDL3/SDL_remapper.h"

SDL_RemapperProfile * SDLCALL SDL_CreateGamepadPassthroughProfile_REAL(SDL_JoystickID gamepad_id)
{
    SDL_RemapperProfile *profile;
    SDL_RemapperMapping *mappings;
    int i;

    profile = (SDL_RemapperProfile *)SDL_calloc(1, sizeof(SDL_RemapperProfile));
    if (!profile) {
        return NULL;
    }

    mappings = (SDL_RemapperMapping *)SDL_calloc(SDL_GAMEPAD_BUTTON_COUNT, sizeof(SDL_RemapperMapping));
    if (!mappings) {
        SDL_free(profile);
        return NULL;
    }

    profile->name = SDL_strdup("Gamepad Passthrough");
    profile->gamepad_id = gamepad_id;
    profile->num_mappings = SDL_GAMEPAD_BUTTON_COUNT;
    profile->mappings = mappings;

    /* Map each button to itself */
    for (i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
        SDL_RemapperMapping *m = &mappings[i];
        m->source_type = SDL_REMAPPER_SOURCE_BUTTON;
        m->source.button = (SDL_GamepadButton)i;
        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_GAMEPAD_BUTTON;
        m->primary_action.code = i;
        m->primary_action.value = 0;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->stick_mapping = NULL;
    }

    return profile;
}

SDL_RemapperStickMapping * SDLCALL SDL_CreateMouseStickMapping_REAL(float sensitivity)
{
    SDL_RemapperStickMapping *mapping;

    mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
    if (!mapping) {
        return NULL;
    }

    mapping->map_to_wasd = false;
    mapping->map_to_arrow_keys = false;
    mapping->map_to_mouse_movement = true;
    mapping->map_to_controller_movement = false;
    mapping->map_to_dpad = false;
    mapping->map_to_gyroscope = false;
    mapping->map_to_touch_mouse = false;
    mapping->invert_horizontal = false;
    mapping->invert_vertical = false;
    mapping->horizontal_sensitivity = sensitivity;
    mapping->vertical_sensitivity = sensitivity;
    mapping->horizontal_acceleration = 1.0f;
    mapping->vertical_acceleration = 1.0f;

    return mapping;
}

SDL_RemapperStickMapping * SDLCALL SDL_CreateKeyboardStickMapping_REAL(bool use_wasd)
{
    SDL_RemapperStickMapping *mapping;

    mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
    if (!mapping) {
        return NULL;
    }

    mapping->map_to_wasd = use_wasd;
    mapping->map_to_arrow_keys = !use_wasd;
    mapping->map_to_mouse_movement = false;
    mapping->map_to_controller_movement = false;
    mapping->map_to_dpad = false;
    mapping->map_to_gyroscope = false;
    mapping->map_to_touch_mouse = false;
    mapping->invert_horizontal = false;
    mapping->invert_vertical = false;
    mapping->horizontal_sensitivity = 50.0f;
    mapping->vertical_sensitivity = 50.0f;
    mapping->horizontal_acceleration = 1.0f;
    mapping->vertical_acceleration = 1.0f;

    return mapping;
}

void SDLCALL SDL_FreeStickMapping_REAL(SDL_RemapperStickMapping *mapping)
{
    if (mapping) {
        SDL_free(mapping);
    }
}
