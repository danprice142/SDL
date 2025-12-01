/* SDL3 Remapper Profile I/O */

#ifndef SDL_remapper_io_h_
#define SDL_remapper_io_h_

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_remapper.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Save a profile to a file (simple text format) */
extern SDL_DECLSPEC int SDLCALL SDL_SaveRemapperProfile(const SDL_RemapperProfile *profile,
                                                        const char *filename);

/* Load a profile from a file
 * Returns a newly allocated profile that must be freed with SDL_FreeRemapperProfile
 */
extern SDL_DECLSPEC SDL_RemapperProfile * SDLCALL SDL_LoadRemapperProfile(const char *filename);

/* Free a profile loaded with SDL_LoadRemapperProfile */
extern SDL_DECLSPEC void SDLCALL SDL_FreeRemapperProfile(SDL_RemapperProfile *profile);

/* Get the default profiles directory path (user-specific) */
extern SDL_DECLSPEC const char * SDLCALL SDL_GetRemapperProfilesPath(void);

/* List available profile files in the profiles directory
 * Returns an array of filenames that must be freed with SDL_FreeRemapperProfileList
 */
extern SDL_DECLSPEC char ** SDLCALL SDL_GetRemapperProfileList(int *count);

/* Free a profile list returned by SDL_GetRemapperProfileList */
extern SDL_DECLSPEC void SDLCALL SDL_FreeRemapperProfileList(char **list, int count);

/* ===== Convenience Functions for Auto-Loading ===== */

/* Load a profile by name (automatically adds .profile extension).
 * Returns NULL if profile doesn't exist. */
extern SDL_DECLSPEC SDL_RemapperProfile * SDLCALL SDL_LoadRemapperProfileByName(const char *name);

/* Load a profile by name and apply it to a gamepad in one call.
 * If the profile doesn't exist, creates a passthrough profile with that name.
 * Returns 0 on success, -1 on error. */
extern SDL_DECLSPEC int SDLCALL SDL_ApplyRemapperProfileByName(SDL_RemapperContext *ctx,
                                                                SDL_JoystickID gamepad_id,
                                                                const char *profile_name);

/* Create a new passthrough profile with the given name and save it.
 * Returns 0 on success, -1 on error. */
extern SDL_DECLSPEC int SDLCALL SDL_CreateRemapperProfileWithName(SDL_JoystickID gamepad_id,
                                                                   const char *name);

/* Check if a profile with the given name exists.
 * Returns true if the profile file exists, false otherwise. */
extern SDL_DECLSPEC bool SDLCALL SDL_RemapperProfileExists(const char *name);

/* Delete a profile file by name.
 * Returns 0 on success, -1 on error. */
extern SDL_DECLSPEC int SDLCALL SDL_DeleteRemapperProfileByName(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SDL_remapper_io_h_ */
