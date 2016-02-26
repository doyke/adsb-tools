#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

#include "peer.h"
#include "rand.h"

#include "wakeup.h"

struct wakeup_entry {
	int fd;
	uint64_t absolute_time_ms;
	struct peer *peer;
	struct wakeup_entry *next;
};

static struct wakeup_entry *head = NULL;

static uint64_t wakeup_get_time_ms() {
	struct timespec tp;
	assert(!clock_gettime(CLOCK_MONOTONIC_COARSE, &tp));
#define MS_PER_S UINT64_C(1000)
#define NS_PER_MS UINT64_C(1000000)
	assert(tp.tv_sec >= 0);
	assert(tp.tv_nsec >= 0);
	return ((uint64_t) tp.tv_sec * MS_PER_S) + ((uint64_t) tp.tv_nsec / NS_PER_MS);
}

void wakeup_init() {
}

void wakeup_cleanup() {
	while (head) {
		struct wakeup_entry *next = head->next;
		free(head);
		head = next;
	}
}

int wakeup_get_delay() {
	if (!head) {
		return -1;
	}
	uint64_t now = wakeup_get_time_ms();
	if (head->absolute_time_ms > now) {
		uint64_t delta = head->absolute_time_ms - now;
		assert(delta < INT_MAX);
		return (int) delta;
	} else {
		return 0;
	}
}

void wakeup_dispatch() {
	uint64_t now = wakeup_get_time_ms();
	while (head && head->absolute_time_ms <= now) {
		peer_call(head->peer);
		struct wakeup_entry *next = head->next;
		free(head);
		head = next;
	}
}

void wakeup_add(struct peer *peer, uint32_t delay_ms) {
	struct wakeup_entry *entry = malloc(sizeof(*entry));
	entry->absolute_time_ms = wakeup_get_time_ms() + delay_ms;
	entry->peer = peer;

	struct wakeup_entry *prev = NULL, *iter = head;
	while (iter) {
		if (iter->absolute_time_ms > entry->absolute_time_ms) {
			break;
		}
		prev = iter;
		iter = iter->next;
	}

	if (prev) {
		entry->next = prev->next;
		prev->next = entry;
	} else {
		entry->next = head;
		head = entry;
	}
}

#define RETRY_MIN_MS 2000
#define RETRY_MAX_MS 60000
uint32_t wakeup_get_retry_delay_ms(uint32_t attempt) {
	uint32_t max_delay = RETRY_MIN_MS * (1 << attempt);
	max_delay = max_delay > RETRY_MAX_MS ? RETRY_MAX_MS : max_delay;

	uint32_t jitter;
	rand_fill(&jitter, sizeof(jitter));

	return jitter % max_delay;
}
