#include "buffer_list.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "main.h"
#include "macros.h"

// -----------------------------------------------------------------------------

#define cfg_exec_next_cmd "exec cfgfs/buffer"

size_t buffer_get_size(const struct buffer *self) {
	return self->size;
}
void buffer_memcpy_to(const struct buffer *self, char *buf, size_t sz) {
	memcpy(buf, self->data, sz);
}
void buffer_free(struct buffer *self) {
	free(self);
}

static size_t buffer_space(const struct buffer *self) {
	return reported_cfg_size - self->size;
}

static bool buffer_may_add_line(const struct buffer *self, size_t sz) {
	return buffer_space(self) >= sz+1+strlen(cfg_exec_next_cmd)+1;
}

static void buffer_add_line(struct buffer *restrict self,
                            const char *buf,
                            size_t sz) {
	memcpy(((char *)self->data)+self->size, buf, sz);
	((char *)self->data)[self->size+sz] = '\n';
	self->size += sz+1;
}

static void buffer_make_full(struct buffer *self) {
D	assert(!self->full);
	self->full = true;
	buffer_add_line(self, cfg_exec_next_cmd, strlen(cfg_exec_next_cmd));
}

static void buffer_copy_from_that_to_this(struct buffer *self,
                                          const struct buffer *restrict buf) {
	self->size = buf->size;
	self->full = buf->full;
	memcpy(self->data, buf->data, buf->size);
}

// -----------------------------------------------------------------------------

// struct buffer_list

void buffer_list_swap(struct buffer_list *self, struct buffer_list *bl) {
	struct buffer_list tmp = *bl;
	*bl = *self;
	*self = tmp;
}

void buffer_list_reset(struct buffer_list *self) {
	for (struct buffer *next, *ent = self->first; ent != NULL; ent = next) {
		next = ent->next;
		buffer_free(ent);
	}
	memset(self, 0, sizeof(struct buffer_list));
}

struct buffer *buffer_list_grab_first_nonempty(struct buffer_list *self) {
	struct buffer *ent = self->first;
	if (unlikely(ent == NULL || ent->size == 0)) return NULL;
	self->first = ent->next;
	if (likely(self->last == ent)) self->last = NULL;
	if (likely(self->nonfull == ent)) self->nonfull = ent->next;
	ent->next = NULL;
	return ent;
}

#define buffer_new() \
	({ \
	struct buffer *_new_ent = calloc(1, sizeof(struct buffer) + reported_cfg_size); \
	_new_ent->data = (char *)_new_ent + sizeof(struct buffer); \
	_new_ent; \
	})

static struct buffer *buffer_list_get_nonfull(struct buffer_list *self) {
	struct buffer *rv = self->nonfull;
	if (unlikely(rv == NULL)) {
		// 1. list is empty
		// 2. list is all full
		rv = buffer_new();
		if (likely(self->first == NULL)) {
			self->first = rv;
			self->last = rv;
			self->nonfull = rv;
		} else {
			self->last->next = rv;
			self->nonfull = rv;
		}
	}
	return rv;
}

static struct buffer *buffer_list_get_next_for(struct buffer_list *self,
                                               struct buffer *ent) {
	struct buffer *rv = ent->next;
	if (likely(rv == NULL)) {
D		assert(self->first != NULL);
D		assert(ent == self->last);
		rv = buffer_new();
		ent->next = rv;
		self->last = rv;
		if (likely(self->nonfull == NULL)) self->nonfull = rv;
	}
	return rv;
}

bool buffer_list_maybe_unshift_fake_buf(struct buffer_list *self,
                                        struct buffer *buf,
                                        char *data) {
	if (unlikely(self->first != NULL && self->first->size != 0)) return false;

	buf->size = 0;
	buf->full = false;
	buf->data = data;
	buf->next = self->first;

	self->first = buf;
	if (likely(self->last == NULL)) self->last = buf;
	self->nonfull = buf;

	return true;
}

void buffer_list_remove_fake_buf(struct buffer_list *self, struct buffer *buf) {
D	assert(self->first == buf);
	self->first = buf->next;
	if (likely(self->nonfull == buf)) self->nonfull = buf->next;
	if (likely(self->last == buf)) self->last = NULL;
}

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

bool buffer_list_is_empty(const struct buffer_list *self) {
	return (self->first == NULL || self->first->size == 0);
}

// -----------------------------------------------------------------------------

void buffer_list_write_line(struct buffer_list *self, const char *s, size_t sz) {
	if (unlikely(sz == 0)) return;
D	assert(sz <= max_line_length); // caller checks this
	struct buffer *buf = buffer_list_get_nonfull(self);
	if (unlikely(!buffer_may_add_line(buf, sz))) {
		buffer_make_full(buf);
		self->nonfull = buf->next;
		buf = buffer_list_get_next_for(self, buf);
D		assert(buffer_may_add_line(buf, sz));
	}
	buffer_add_line(buf, s, sz);
}
