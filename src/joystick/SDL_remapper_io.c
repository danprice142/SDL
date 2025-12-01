/* SDL3 Remapper Profile I/O Implementation */

#include "SDL_internal.h"

#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_iostream.h"
#include "SDL3/SDL_remapper.h"
#include "SDL3/SDL_remapper_io.h"

#define SDL_REMAPPER_PROFILE_VERSION 1

static char *SDL_remapper_profiles_path = NULL;

const char * SDLCALL SDL_GetRemapperProfilesPath(void)
{
    if (!SDL_remapper_profiles_path) {
        char *pref_path = SDL_GetPrefPath("SDL", "GamepadRemapper");
        if (pref_path) {
            SDL_remapper_profiles_path = pref_path;
        }
    }
    return SDL_remapper_profiles_path;
}

static void
WriteActionToFile(SDL_IOStream *file, const char *prefix, const SDL_RemapperAction *action)
{
    char line[256];

    SDL_snprintf(line, sizeof(line), "%s.kind=%d\n", prefix, (int)action->kind);
    SDL_IOprintf(file, "%s", line);

    SDL_snprintf(line, sizeof(line), "%s.code=%d\n", prefix, action->code);
    SDL_IOprintf(file, "%s", line);

    SDL_snprintf(line, sizeof(line), "%s.value=%d\n", prefix, action->value);
    SDL_IOprintf(file, "%s", line);
}

/* Helper to read a single text line from an SDL_IOStream.
 * Returns true if any characters were read, false on EOF or error.
 */
static bool
ReadLine(SDL_IOStream *file, char *buffer, size_t buffer_len)
{
    size_t count = 0;

    if (!file || !buffer || buffer_len == 0) {
        return false;
    }

    /* Read one byte at a time until newline, EOF, or buffer is full. */
    while (count < (buffer_len - 1)) {
        char ch = '\0';
        size_t read = SDL_ReadIO(file, &ch, 1);

        if (read == 0) {
            /* EOF or error. */
            break;
        }

        buffer[count++] = ch;

        if (ch == '\n') {
            break;
        }
    }

    buffer[count] = '\0';

    if (count == 0) {
        return false;
    }

    return true;
}

static void
ReadActionFromFile(SDL_IOStream *file, SDL_RemapperAction *action)
{
    char line[256];

    if (ReadLine(file, line, sizeof(line))) {
        SDL_sscanf(line, "%*[^=]=%d", &action->kind);
    }
    if (ReadLine(file, line, sizeof(line))) {
        SDL_sscanf(line, "%*[^=]=%d", &action->code);
    }
    if (ReadLine(file, line, sizeof(line))) {
        SDL_sscanf(line, "%*[^=]=%d", &action->value);
    }
}

int SDLCALL SDL_SaveRemapperProfile(const SDL_RemapperProfile *profile,
                                    const char *filename)
{
    SDL_IOStream *file;
    char path[1024];
    const char *profiles_path;
    int i;

    if (!profile || !filename) {
        return SDL_InvalidParamError("profile or filename");
    }

    profiles_path = SDL_GetRemapperProfilesPath();
    if (!profiles_path) {
        return -1;
    }

    SDL_snprintf(path, sizeof(path), "%s%s", profiles_path, filename);

    file = SDL_IOFromFile(path, "w");
    if (!file) {
        return -1;
    }

    /* Write header */
    SDL_IOprintf(file, "# SDL Gamepad Remapper Profile\n");
    SDL_IOprintf(file, "version=%d\n", SDL_REMAPPER_PROFILE_VERSION);
    SDL_IOprintf(file, "name=%s\n", profile->name ? profile->name : "Unnamed");
    SDL_IOprintf(file, "gamepad_id=%d\n", (int)profile->gamepad_id);

    /* Persist trigger deadzones with a reasonable default if unset. */
    {
        float left = profile->left_trigger_deadzone;
        float right = profile->right_trigger_deadzone;
        if (left <= 0.0f) {
            left = 50.0f;
        }
        if (right <= 0.0f) {
            right = 50.0f;
        }
        SDL_IOprintf(file, "left_trigger_deadzone=%.2f\n", left);
        SDL_IOprintf(file, "right_trigger_deadzone=%.2f\n", right);
    }

    SDL_IOprintf(file, "num_mappings=%d\n", profile->num_mappings);

    /* Write mappings */
    for (i = 0; i < profile->num_mappings; ++i) {
        SDL_RemapperMapping *m = &profile->mappings[i];
        char prefix[64];

        SDL_IOprintf(file, "\n# Mapping %d\n", i);
        SDL_IOprintf(file, "mapping[%d].source_type=%d\n", i, (int)m->source_type);

        /*
         * For non-axis sources we reuse source.button:
         *  - SDL_REMAPPER_SOURCE_BUTTON / SDL_REMAPPER_SOURCE_MOUSE_BUTTON /
         *    SDL_REMAPPER_SOURCE_KEYBOARD_KEY use source.button
         *  - SDL_REMAPPER_SOURCE_AXIS / SDL_REMAPPER_SOURCE_MOUSE_WHEEL /
         *    SDL_REMAPPER_SOURCE_MOUSE_MOTION use source.axis
         */
        switch (m->source_type) {
        case SDL_REMAPPER_SOURCE_BUTTON:
        case SDL_REMAPPER_SOURCE_MOUSE_BUTTON:
        case SDL_REMAPPER_SOURCE_KEYBOARD_KEY:
            SDL_IOprintf(file, "mapping[%d].source.button=%d\n", i, (int)m->source.button);
            break;
        case SDL_REMAPPER_SOURCE_AXIS:
        case SDL_REMAPPER_SOURCE_MOUSE_WHEEL:
        case SDL_REMAPPER_SOURCE_MOUSE_MOTION:
        default:
            SDL_IOprintf(file, "mapping[%d].source.axis=%d\n", i, (int)m->source.axis);
            break;
        }

        SDL_IOprintf(file, "mapping[%d].use_as_shift=%d\n", i, m->use_as_shift ? 1 : 0);

        SDL_snprintf(prefix, sizeof(prefix), "mapping[%d].primary", i);
        WriteActionToFile(file, prefix, &m->primary_action);

        SDL_snprintf(prefix, sizeof(prefix), "mapping[%d].shift", i);
        WriteActionToFile(file, prefix, &m->shift_action);

        SDL_snprintf(prefix, sizeof(prefix), "mapping[%d].hold", i);
        WriteActionToFile(file, prefix, &m->hold_action);

        /* Write stick mapping if present */
        if (m->stick_mapping) {
            SDL_RemapperStickMapping *sm = m->stick_mapping;
            SDL_IOprintf(file, "mapping[%d].stick.map_to_wasd=%d\n", i, sm->map_to_wasd ? 1 : 0);
            SDL_IOprintf(file, "mapping[%d].stick.map_to_arrow_keys=%d\n", i, sm->map_to_arrow_keys ? 1 : 0);
            SDL_IOprintf(file, "mapping[%d].stick.map_to_mouse_movement=%d\n", i, sm->map_to_mouse_movement ? 1 : 0);
            SDL_IOprintf(file, "mapping[%d].stick.map_to_controller_movement=%d\n", i, sm->map_to_controller_movement ? 1 : 0);
            SDL_IOprintf(file, "mapping[%d].stick.map_to_dpad=%d\n", i, sm->map_to_dpad ? 1 : 0);
            SDL_IOprintf(file, "mapping[%d].stick.invert_horizontal=%d\n", i, sm->invert_horizontal ? 1 : 0);
            SDL_IOprintf(file, "mapping[%d].stick.invert_vertical=%d\n", i, sm->invert_vertical ? 1 : 0);
            SDL_IOprintf(file, "mapping[%d].stick.horizontal_sensitivity=%.2f\n", i, sm->horizontal_sensitivity);
            SDL_IOprintf(file, "mapping[%d].stick.vertical_sensitivity=%.2f\n", i, sm->vertical_sensitivity);
            SDL_IOprintf(file, "mapping[%d].stick.horizontal_acceleration=%.2f\n", i, sm->horizontal_acceleration);
            SDL_IOprintf(file, "mapping[%d].stick.vertical_acceleration=%.2f\n", i, sm->vertical_acceleration);
            SDL_IOprintf(file, "mapping[%d].stick.gyro_horizontal_sensitivity=%.2f\n", i, sm->gyro_horizontal_sensitivity);
            SDL_IOprintf(file, "mapping[%d].stick.gyro_vertical_sensitivity=%.2f\n", i, sm->gyro_vertical_sensitivity);
            SDL_IOprintf(file, "mapping[%d].stick.gyro_acceleration=%.2f\n", i, sm->gyro_acceleration);
            SDL_IOprintf(file, "mapping[%d].stick.gyro_mode_roll=%d\n", i, sm->gyro_mode_roll ? 1 : 0);
        }
    }

    SDL_CloseIO(file);
    return 0;
}

SDL_RemapperProfile * SDLCALL SDL_LoadRemapperProfile(const char *filename)
{
    SDL_IOStream *file;
    SDL_RemapperProfile *profile;
    char path[1024];
    char line[256];
    const char *profiles_path;
    int version = 0;
    int num_mappings = 0;
    int i;

    if (!filename) {
        SDL_InvalidParamError("filename");
        return NULL;
    }

    profiles_path = SDL_GetRemapperProfilesPath();
    if (!profiles_path) {
        return NULL;
    }

    SDL_snprintf(path, sizeof(path), "%s%s", profiles_path, filename);

    file = SDL_IOFromFile(path, "r");
    if (!file) {
        return NULL;
    }

    profile = (SDL_RemapperProfile *)SDL_calloc(1, sizeof(SDL_RemapperProfile));
    if (!profile) {
        SDL_CloseIO(file);
        return NULL;
    }

    /* Read header (version, name, gamepad_id, num_mappings) */
    while (ReadLine(file, line, sizeof(line))) {
        if (line[0] == '#') {
            continue;
        }

        if (SDL_strncmp(line, "version=", 8) == 0) {
            SDL_sscanf(line, "version=%d", &version);
        } else if (SDL_strncmp(line, "name=", 5) == 0) {
            char *name = SDL_strdup(line + 5);
            char *nl;
            if (!name) {
                SDL_FreeRemapperProfile(profile);
                SDL_CloseIO(file);
                return NULL;
            }
            /* Strip any trailing CR/LF */
            nl = SDL_strchr(name, '\n');
            if (nl) {
                *nl = '\0';
            }
            nl = SDL_strchr(name, '\r');
            if (nl) {
                *nl = '\0';
            }
            profile->name = name;
        } else if (SDL_strncmp(line, "gamepad_id=", 11) == 0) {
            int id;
            SDL_sscanf(line, "gamepad_id=%d", &id);
            profile->gamepad_id = (SDL_JoystickID)id;
        } else if (SDL_strncmp(line, "left_trigger_deadzone=", 22) == 0) {
            float v = 0.0f;
            SDL_sscanf(line, "left_trigger_deadzone=%f", &v);
            profile->left_trigger_deadzone = v;
        } else if (SDL_strncmp(line, "right_trigger_deadzone=", 23) == 0) {
            float v = 0.0f;
            SDL_sscanf(line, "right_trigger_deadzone=%f", &v);
            profile->right_trigger_deadzone = v;
        } else if (SDL_strncmp(line, "num_mappings=", 13) == 0) {
            SDL_sscanf(line, "num_mappings=%d", &num_mappings);
            break;
        }
    }

    if (version != SDL_REMAPPER_PROFILE_VERSION || num_mappings <= 0) {
        SDL_FreeRemapperProfile(profile);
        SDL_CloseIO(file);
        return NULL;
    }

    /* Allocate mappings */
    profile->num_mappings = num_mappings;
    profile->mappings = (SDL_RemapperMapping *)SDL_calloc(num_mappings, sizeof(SDL_RemapperMapping));
    if (!profile->mappings) {
        SDL_FreeRemapperProfile(profile);
        SDL_CloseIO(file);
        return NULL;
    }

    /* Initialize mappings with defaults; values will be overridden by parsed data. */
    for (i = 0; i < num_mappings; ++i) {
        SDL_RemapperMapping *m = &profile->mappings[i];

        if (i < SDL_GAMEPAD_BUTTON_COUNT) {
            m->source_type = SDL_REMAPPER_SOURCE_BUTTON;
            m->source.button = (SDL_GamepadButton)i;
        } else {
            int axis_index = i - SDL_GAMEPAD_BUTTON_COUNT;
            m->source_type = SDL_REMAPPER_SOURCE_AXIS;
            if (axis_index >= 0 && axis_index < SDL_GAMEPAD_AXIS_COUNT) {
                m->source.axis = (SDL_GamepadAxis)axis_index;
            } else {
                m->source.axis = SDL_GAMEPAD_AXIS_INVALID;
            }
        }

        m->use_as_shift = false;
        m->primary_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->primary_action.code = 0;
        m->primary_action.value = 0;
        m->shift_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->shift_action.code = 0;
        m->shift_action.value = 0;
        m->hold_action.kind = SDL_REMAPPER_ACTION_NONE;
        m->hold_action.code = 0;
        m->hold_action.value = 0;
        m->stick_mapping = NULL;
    }

    /* Parse mapping lines, updating the initialized structures. */
    while (ReadLine(file, line, sizeof(line))) {
        int index = -1;
        int ival = 0;
        float fval = 0.0f;

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        /* Source type */
        if (SDL_sscanf(line, "mapping[%d].source_type=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].source_type = (SDL_RemapperSourceType)ival;
            }
            continue;
        }

        /* Source button / axis
         *
         * Note: source_type is determined by the explicit
         * mapping[%d].source_type line and may represent gamepad or
         * mouse sources. We no longer override source_type here so
         * that mouse-specific values survive round-tripping.
         */
        if (SDL_sscanf(line, "mapping[%d].source.button=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].source.button = (SDL_GamepadButton)ival;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].source.axis=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].source.axis = (SDL_GamepadAxis)ival;
            }
            continue;
        }

        /* Shift flag */
        if (SDL_sscanf(line, "mapping[%d].use_as_shift=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].use_as_shift = (ival != 0);
            }
            continue;
        }

        /* Primary action */
        if (SDL_sscanf(line, "mapping[%d].primary.kind=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].primary_action.kind = (SDL_RemapperActionKind)ival;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].primary.code=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].primary_action.code = ival;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].primary.value=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].primary_action.value = ival;
            }
            continue;
        }

        /* Shift action */
        if (SDL_sscanf(line, "mapping[%d].shift.kind=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].shift_action.kind = (SDL_RemapperActionKind)ival;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].shift.code=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].shift_action.code = ival;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].shift.value=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].shift_action.value = ival;
            }
            continue;
        }

        /* Hold action */
        if (SDL_sscanf(line, "mapping[%d].hold.kind=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].hold_action.kind = (SDL_RemapperActionKind)ival;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].hold.code=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].hold_action.code = ival;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].hold.value=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                profile->mappings[index].hold_action.value = ival;
            }
            continue;
        }

        /* Stick mapping fields. Allocate on first use. */
        if (SDL_sscanf(line, "mapping[%d].stick.map_to_wasd=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->map_to_wasd = (ival != 0);
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.map_to_arrow_keys=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->map_to_arrow_keys = (ival != 0);
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.map_to_mouse_movement=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->map_to_mouse_movement = (ival != 0);
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.map_to_controller_movement=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->map_to_controller_movement = (ival != 0);
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.map_to_dpad=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->map_to_dpad = (ival != 0);
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.invert_horizontal=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->invert_horizontal = (ival != 0);
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.invert_vertical=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->invert_vertical = (ival != 0);
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.horizontal_sensitivity=%f", &index, &fval) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->horizontal_sensitivity = fval;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.vertical_sensitivity=%f", &index, &fval) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->vertical_sensitivity = fval;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.horizontal_acceleration=%f", &index, &fval) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->horizontal_acceleration = fval;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.vertical_acceleration=%f", &index, &fval) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->vertical_acceleration = fval;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.gyro_horizontal_sensitivity=%f", &index, &fval) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->gyro_horizontal_sensitivity = fval;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.gyro_vertical_sensitivity=%f", &index, &fval) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->gyro_vertical_sensitivity = fval;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.gyro_acceleration=%f", &index, &fval) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->gyro_acceleration = fval;
            }
            continue;
        }
        if (SDL_sscanf(line, "mapping[%d].stick.gyro_mode_roll=%d", &index, &ival) == 2) {
            if (index >= 0 && index < num_mappings) {
                SDL_RemapperMapping *m = &profile->mappings[index];
                if (!m->stick_mapping) {
                    m->stick_mapping = (SDL_RemapperStickMapping *)SDL_calloc(1, sizeof(SDL_RemapperStickMapping));
                    if (!m->stick_mapping) {
                        continue;
                    }
                }
                m->stick_mapping->gyro_mode_roll = (ival != 0);
            }
            continue;
        }
    }

    SDL_CloseIO(file);
    return profile;
}

void SDLCALL SDL_FreeRemapperProfile(SDL_RemapperProfile *profile)
{
    int i;

    if (!profile) {
        return;
    }

    if (profile->name) {
        SDL_free((void *)profile->name);
    }

    if (profile->mappings) {
        for (i = 0; i < profile->num_mappings; ++i) {
            if (profile->mappings[i].stick_mapping) {
                SDL_free(profile->mappings[i].stick_mapping);
            }
        }
        SDL_free(profile->mappings);
    }

    SDL_free(profile);
}

char ** SDLCALL SDL_GetRemapperProfileList(int *count)
{
    const char *profiles_path;
    char **globlist;
    char **result = NULL;
    int num = 0;
    int i;

    profiles_path = SDL_GetRemapperProfilesPath();
    if (!profiles_path) {
        if (count) {
            *count = 0;
        }
        return NULL;
    }

    /* Look for files with the .profile extension. */
    globlist = SDL_GlobDirectory(profiles_path, "*.profile", 0, &num);
    if (!globlist || num <= 0) {
        if (count) {
            *count = 0;
        }
        if (globlist) {
            SDL_free(globlist);
        }
        return NULL;
    }

    result = (char **)SDL_calloc(num, sizeof(char *));
    if (!result) {
        if (count) {
            *count = 0;
        }
        SDL_free(globlist);
        return NULL;
    }

    for (i = 0; i < num; ++i) {
        if (globlist[i]) {
            result[i] = SDL_strdup(globlist[i]);
        } else {
            result[i] = NULL;
        }
    }

    SDL_free(globlist);

    if (count) {
        *count = num;
    }

    return result;
}

void SDLCALL SDL_FreeRemapperProfileList(char **list, int count)
{
    int i;

    if (!list) {
        return;
    }

    for (i = 0; i < count; ++i) {
        if (list[i]) {
            SDL_free(list[i]);
        }
    }

    SDL_free(list);
}
