#include "buffer_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "macros.h"
#include "main.h"

// -----------------------------------------------------------------------------

#define cfg_exec_next_cmd "exec cfgfs/buffer"

// make sure reported_cfg_size is big enough that we'll always be able to put at
//  least one command in a single buffer
_Static_assert(reported_cfg_size >= max_line_length+1+strlen(cfg_exec_next_cmd)+1,
    "reported_cfg_size is too low (not enough space to reliably fit one command)");

static size_t buffer_space(const struct buffer *self) {
	return reported_cfg_size - self->size;
}

static bool buffer_may_add_line(const struct buffer *self, size_t len) {
	return buffer_space(self) >= len+1+strlen(cfg_exec_next_cmd)+1;
}

static void buffer_add_line(struct buffer *restrict self,
                            const char *buf,
                            size_t len) {
	char *p = ((char *)self->data)+self->size;
	self->size += len+1;
	p[len] = '\n';
	memcpy(p, buf, len);
}

__attribute__((cold))
__attribute__((noinline))
void buffer_make_full(struct buffer *self) {
D	assert(!self->full);
	self->full = true;

	buffer_add_line(self, cfg_exec_next_cmd, strlen(cfg_exec_next_cmd));
}

__attribute__((cold))
static void buffer_copy_from_that_to_this(struct buffer *self,
                                          const struct buffer *restrict buf) {
	self->size = buf->size;
	self->full = buf->full;
	memcpy(self->data, buf->data, buf->size);
}

// -----------------------------------------------------------------------------

// struct buffer_list

__attribute__((cold)) // only used by reloader
void buffer_list_swap(struct buffer_list *self, struct buffer_list *bl) {
	struct buffer_list tmp = *bl;
	*bl = *self;
	*self = tmp;
}

__attribute__((cold)) // only used by reloader
void buffer_list_reset(struct buffer_list *self) {
	struct buffer *ent = self->first;
	while (ent != NULL) {
		struct buffer *next = ent->next;
		buffer_free(ent);
		ent = next;
	}
	memset(self, 0, sizeof(struct buffer_list));
}

struct buffer *buffer_list_grab_first(struct buffer_list *self) {
	struct buffer *ent = self->first;
	if (likely(ent != NULL)) {
		struct buffer *next = ent->next; // note: ent is freed after this so no need to clear ent->next
		self->first = next;
		if (next == NULL) {
			// was the only one, so it must've been these both
D			assert(self->last == ent);
D			assert(self->nonfull == ent);
			self->last = NULL;
			self->nonfull = NULL;
		} else {
			// not the only one, so can't be this
D			assert(self->last != ent);
			// has one after it, so it must've gotten full
D			assert(ent->full);
D			assert(self->nonfull != ent);
		}
	}
	return ent;
}

static inline struct buffer *buffer_new(void) {
	struct buffer *_new_ent = malloc((sizeof(struct buffer) + reported_cfg_size));
	unsafe_optimization_hint(_new_ent != NULL);
	memset(_new_ent, 0, sizeof(struct buffer));
	_new_ent->data = ((char *)_new_ent + sizeof(struct buffer));
	return _new_ent;
}

static struct buffer *buffer_list_get_nonfull_alloc(struct buffer_list *self);
static struct buffer *buffer_list_get_nonfull(struct buffer_list *self) {
	struct buffer *rv = self->nonfull;
	if (likely(rv != NULL)) {
D		assert(!rv->full);
		return rv;
	} else {
		return buffer_list_get_nonfull_alloc(self);
	}
}
__attribute__((noinline))
static struct buffer *buffer_list_get_nonfull_alloc(struct buffer_list *self) {
	// no non-full buffer, so the list must've been empty
D	assert(self->first == NULL);
D	assert(self->last == NULL);
D	assert(self->nonfull == NULL);
	struct buffer *rv = buffer_new();
	self->first = rv;
	self->last = rv;
	self->nonfull = rv;
	return rv;
}

__attribute__((cold))
__attribute__((noinline))
static struct buffer *buffer_list_get_next_for(struct buffer_list *self,
                                               struct buffer *ent) {
D	assert(ent->full);
	struct buffer *rv = ent->next;
	if (likely(rv == NULL)) {
		// list must not have been empty if it has at least ent somewhere
D		assert(self->first != NULL);
		// ent must be the last one if it doesn't have a next
D		assert(ent == self->last);
		rv = buffer_new();
		ent->next = rv;
		self->last = rv;
		if (likely(self->nonfull == NULL)) self->nonfull = rv;
	}
D	assert(!rv->full);
	return rv;
}

bool buffer_list_maybe_unshift_fake_buf(struct buffer_list *self,
                                        struct buffer *buf,
                                        char *data) {
	if (unlikely(self->first != NULL)) return false;

	memset(buf, 0, sizeof(struct buffer));
	buf->data = data;

	self->nonfull = buf;
	self->first = buf;
	self->last = buf;

	return true;
}

void buffer_list_remove_fake_buf(struct buffer_list *self, struct buffer *buf) {
D	assert(self->first == buf);
	self->first = buf->next;
	if (likely(buf->next == NULL)) {
		// this was the only buffer in the list
		// there wasn't a next one, so it must not have gotten full
D		assert(!buf->full);
D		assert(self->nonfull == buf);
D		assert(self->last == buf);
		self->nonfull = NULL;
		self->last = NULL;
	} else {
		// there was another buffer in the list
		// that means this one must've gotten full
D		assert(buf->full);
D		assert(self->nonfull != buf);
D		assert(self->last != buf);
	}
}

__attribute__((cold)) // only used in _init()
void buffer_list_append_from_that_to_this(struct buffer_list *self,
                                          const struct buffer_list *that) {
	const struct buffer *buf = that->first;

	if (unlikely(buf == NULL || buf->size == 0)) return;

	struct buffer *nonfull = buffer_list_get_nonfull(self);
	if (nonfull->size != 0) {
		buffer_make_full(nonfull);
		self->nonfull = nonfull->next;
		nonfull = buffer_list_get_next_for(self, nonfull);
	}

	for (;;) {
		buffer_copy_from_that_to_this(nonfull, buf);
		buf = buf->next;
		if (buf == NULL || buf->size == 0) break;
		assert(nonfull->full);
		self->nonfull = nonfull->next;
		nonfull = buffer_list_get_next_for(self, nonfull);
	}
}

// -----------------------------------------------------------------------------

struct buffer *buffer_list_write_wont_fit(struct buffer_list *self);

void buffer_list_write_line(struct buffer_list *self, const char *s, size_t len) {
D	assert(len > 0); // caller checks this
D	assert(len <= max_line_length); // caller checks this
	bool did_allocate = (self->nonfull == NULL); // optimization hint
	struct buffer *buf = buffer_list_get_nonfull(self);
	if (unlikely(!did_allocate && !buffer_may_add_line(buf, len))) {
		buf = buffer_list_write_wont_fit(self);
D		assert(buffer_may_add_line(buf, len));
	}
	buffer_add_line(buf, s, len);
}

// note: len does not include the newline
char *buffer_list_get_write_buffer(struct buffer_list *self, size_t len) {
D	assert(len != 0);
	bool did_allocate = (self->nonfull == NULL); // optimization hint
	struct buffer *buf = buffer_list_get_nonfull(self);
	if (unlikely(!did_allocate && !buffer_may_add_line(buf, len))) {
		buf = buffer_list_write_wont_fit(self);
D		assert(buffer_may_add_line(buf, len));
	}
	return (char *)buf->data+buf->size;
}

__attribute__((cold, minsize, noinline))
struct buffer *buffer_list_write_wont_fit(struct buffer_list *self) {
	struct buffer *buf = self->nonfull;
D	assert(buf->next == NULL); // wasn't full so couldn't have had a next one
D	assert(self->last == buf); // nonfull so must've been the last one
	self->nonfull = buf->next;
	buffer_make_full(buf);
	return buffer_list_get_next_for(self, buf);
}

// note: sz here includes the newline
void buffer_list_commit_write(struct buffer_list *self, size_t sz) {
	self->nonfull->size += sz;
}
