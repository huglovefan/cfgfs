#pragma once

#define reported_cfg_size ((size_t)1024)

void main_quit(void);

#if defined(TEST_SKIP_CLICK_IF_READ_WAITING_FOR_LOCK)
extern _Atomic(_Bool) read_waiting_for_lock_;
#define read_waiting_for_lock \
	({ \
		_Bool _read_waiting_for_lock_rv = read_waiting_for_lock_; \
D		if (_read_waiting_for_lock_rv) eprintln("read_waiting_for_lock = 1"); \
		_read_waiting_for_lock_rv; \
	})
#endif
