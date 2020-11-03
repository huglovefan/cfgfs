#pragma once

#include <stdbool.h>

// whether the cli is currently prompting for input
// - set to 1 when we print the prompt and start waiting for input
// - set to 0 when we get a line, 1 when finished processing it
extern _Atomic(bool) cli_reading_line;
// would like to protect this with a mutex (lock before calling into readline,
// unlock in line handler) but the line handler is called from a different thread
// so locking that way is undefined behavior
// -- any other thing that would work to make it properly safe?

void cli_input_init(void *L);
void cli_input_deinit(void);

// pretend the user typed an end-of-file character
// this makes cfgfs exit
void cli_input_manual_eof(void);
// todo: demolish
// its not really thread safe and doesn't work if the thread wasn't started
// added it before thinking
