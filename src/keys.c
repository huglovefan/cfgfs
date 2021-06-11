#include "keys.h"

#include <string.h>

#if defined(__linux__) || defined(__FreeBSD__)
 #define XK_LATIN1
 #define XK_MISCELLANY
 #include <X11/keysymdef.h>
#else
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

#if defined(__linux__) || defined(__FreeBSD__)
 #define WIN_LIN(win, lin) lin
#else
 #define WIN_LIN(win, lin) win
#endif

const struct key_list_entry keys[] = {
	{ "0", WIN_LIN(0, XK_0) },
	{ "1", WIN_LIN(0, XK_1) },
	{ "2", WIN_LIN(0, XK_2) },
	{ "3", WIN_LIN(0, XK_3) },
	{ "4", WIN_LIN(0, XK_4) },
	{ "5", WIN_LIN(0, XK_5) },
	{ "6", WIN_LIN(0, XK_6) },
	{ "7", WIN_LIN(0, XK_7) },
	{ "8", WIN_LIN(0, XK_8) },
	{ "9", WIN_LIN(0, XK_9) },
	{ "a", WIN_LIN(0, XK_a) },
	{ "b", WIN_LIN(0, XK_b) },
	{ "c", WIN_LIN(0, XK_c) },
	{ "d", WIN_LIN(0, XK_d) },
	{ "e", WIN_LIN(0, XK_e) },
	{ "f", WIN_LIN(0, XK_f) },
	{ "g", WIN_LIN(0, XK_g) },
	{ "h", WIN_LIN(0, XK_h) },
	{ "i", WIN_LIN(0, XK_i) },
	{ "j", WIN_LIN(0, XK_j) },
	{ "k", WIN_LIN(0, XK_k) },
	{ "l", WIN_LIN(0, XK_l) },
	{ "m", WIN_LIN(0, XK_m) },
	{ "n", WIN_LIN(0, XK_n) },
	{ "o", WIN_LIN(0, XK_o) },
	{ "p", WIN_LIN(0, XK_p) },
	{ "q", WIN_LIN(0, XK_q) },
	{ "r", WIN_LIN(0, XK_r) },
	{ "s", WIN_LIN(0, XK_s) },
	{ "t", WIN_LIN(0, XK_t) },
	{ "u", WIN_LIN(0, XK_u) },
	{ "v", WIN_LIN(0, XK_v) },
	{ "w", WIN_LIN(0, XK_w) },
	{ "x", WIN_LIN(0, XK_x) },
	{ "y", WIN_LIN(0, XK_y) },
	{ "z", WIN_LIN(0, XK_z) },
	{ "kp_ins", 0 },
	{ "kp_end", 0 },
	{ "kp_downarrow", 0 },
	{ "kp_pgdn", 0 },
	{ "kp_leftarrow", 0 },
	{ "kp_5", 0 },
	{ "kp_rightarrow", 0 },
	{ "kp_home", 0 },
	{ "kp_uparrow", 0 },
	{ "kp_pgup", 0 },
	{ "kp_slash", 0 },
	{ "kp_multiply", 0 },
	{ "kp_minus", 0 },
	{ "kp_plus", 0 },
	{ "kp_enter", 0 },
	{ "kp_del", 0 },
	{ "[", 0 },
	{ "]", 0 },
	{ "semicolon", 0 },
	{ "'", 0 },
	{ "`", 0 },
	{ ",", 0 },
	{ ".", 0 },
	{ "/", 0 },
	{ "\\", 0 },
	{ "-", 0 },
	{ "=", 0 },
	{ "enter", 0 },
	{ "space", 0 },
	{ "backspace", 0 },
	{ "tab", 0 },
	{ "capslock", 0 },
	{ "numlock", 0 },
	{ "escape", 0 },
	{ "scrolllock", 0 },
	{ "ins", 0 },
	{ "del", 0 },
	{ "home", 0 },
	{ "end", 0 },
	{ "pgup", 0 },
	{ "pgdn", 0 },
	{ "pause", 0 },
	{ "shift", 0 },
	{ "rshift", 0 },
	{ "alt", 0 },
	{ "ralt", 0 },
	{ "ctrl", 0 },
	{ "rctrl", 0 },
	{ "lwin", 0 },
	{ "rwin", 0 },
	{ "app", 0 },
	{ "uparrow", 0 },
	{ "leftarrow", 0 },
	{ "downarrow", 0 },
	{ "rightarrow", 0 },
	{ "f1", WIN_LIN(VK_F1, XK_F1) },
	{ "f2", WIN_LIN(VK_F2, XK_F2) },
	{ "f3", WIN_LIN(VK_F3, XK_F3) },
	{ "f4", WIN_LIN(VK_F4, XK_F4) },
	{ "f5", WIN_LIN(VK_F5, XK_F5) },
	{ "f6", WIN_LIN(VK_F6, XK_F6) },
	{ "f7", WIN_LIN(VK_F7, XK_F7) },
	{ "f8", WIN_LIN(VK_F8, XK_F8) },
	{ "f9", WIN_LIN(VK_F9, XK_F9) },
	{ "f10", WIN_LIN(VK_F10, XK_F10) },
	{ "f11", WIN_LIN(VK_F11, XK_F11) },
	{ "f12", WIN_LIN(VK_F12, XK_F12) },

	{ "capslocktoggle", 0 },
	{ "numlocktoggle", 0 },
	{ "scrolllocktoggle", 0 },

	{ "mouse1", 0 },
	{ "mouse2", 0 },
	{ "mouse3", 0 },
	{ "mouse4", 0 },
	{ "mouse5", 0 },

	{ "mwheelup", 0 },
	{ "mwheeldown", 0 },

#if defined(__linux__) || defined(__FreeBSD__)
	{ "a_button", 0 },
	{ "b_button", 0 },
	{ "x_button", 0 },
	{ "y_button", 0 },
	{ "l_shoulder", 0 },
	{ "r_shoulder", 0 },
	{ "back", 0 },
	{ "start", 0 },
	{ "stick1", 0 },
	{ "stick2", 0 },
#else
	{ "joy1", 0 },
	{ "joy2", 0 },
	{ "joy3", 0 },
	{ "joy4", 0 },
	{ "joy5", 0 },
	{ "joy6", 0 },
	{ "joy7", 0 },
	{ "joy8", 0 },
	{ "joy9", 0 },
	{ "joy10", 0 },
#endif
	{ NULL, 0 },
};

KeySym keys_name2keysym(const char *name) {
	for (const struct key_list_entry *p = keys; p->name != NULL; p++) {
		if (0 == strcmp(p->name, name)) {
			return p->xkey;
		}
	}
	return 0;
}
