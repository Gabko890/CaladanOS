#include <deferred.h>
#include <kmalloc.h>

typedef struct {
    deferred_fn_t fn;
    void *arg;
} deferred_item_t;

#define DEFERRED_Q_SIZE 16
static deferred_item_t queue[DEFERRED_Q_SIZE];
static volatile u32 q_head = 0; // next pop
static volatile u32 q_tail = 0; // next push

static u32 q_next(u32 idx) { return (u32)((idx + 1) % DEFERRED_Q_SIZE); }

int deferred_schedule(deferred_fn_t fn, void *arg) {
    if (!fn) return -1;
    u32 next_tail = q_next(q_tail);
    if (next_tail == q_head) {
        return -2; // full
    }
    queue[q_tail].fn = fn;
    queue[q_tail].arg = arg;
    q_tail = next_tail;
    return 0;
}

void deferred_process_one(void) {
    if (q_head == q_tail) return; // empty
    deferred_item_t it = queue[q_head];
    q_head = q_next(q_head);
    if (it.fn) it.fn(it.arg);
}

void deferred_process_all(void) {
    while (q_head != q_tail) {
        deferred_process_one();
    }
}

int deferred_has_pending(void) {
    return q_head != q_tail;
}

