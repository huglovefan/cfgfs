#pragma once

// mostly thread-safe printing with readline awareness

__attribute__((format(printf, 1, 2)))
void println(const char *, ...);

__attribute__((format(printf, 1, 2)))
void eprintln(const char *, ...);

// locking functions used by println/eprintln
// these also save/wipe and restore the prompt and any text on it
void cli_lock_output(void);
void cli_unlock_output(void);

// these just take the lock (for internal use by cli_input.c)
void cli_lock_output_nosave(void);
_Bool cli_trylock_output_nosave(void);
void cli_unlock_output_norestore(void);

void cli_save_prompt_locked(void);
void cli_restore_prompt_locked(void);
