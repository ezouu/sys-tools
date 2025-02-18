#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum { READERS, WRITERS, N_WAY } PRIORITY;

typedef struct rwlock rwlock_t;
struct rwlock {
    pthread_mutex_t mutex;
    pthread_cond_t readers_cv;
    pthread_cond_t writers_cv;
    int active_readers;
    int active_writers;
    int waiting_readers;
    int waiting_writers;
    PRIORITY priority;
    int n_way;

    // For N_WAY batch management.
    bool batch_active;
    int current_batch_readers;
    int batch_limit;
};

#ifdef DEBUG
// Using GNU extension to allow zero or more args.
#define DBG_PRINT(fmt, ...) fprintf(stderr, "[%s:%d:%s] " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)

// Checks key invariants of the rwlock and prints an error if any invariant is broken.
static void check_invariants(rwlock_t *rw) {
    if (rw->active_writers > 0 && rw->active_readers > 0) {
        fprintf(stderr, "Invariant violation: active_writers (%d) > 0 but active_readers (%d) > 0\n", rw->active_writers, rw->active_readers);
    }
    if (rw->batch_active && rw->batch_limit <= 0) {
        fprintf(stderr, "Invariant violation: batch_active is true but batch_limit is %d\n", rw->batch_limit);
    }
    if (rw->current_batch_readers > rw->batch_limit) {
        fprintf(stderr, "Invariant violation: current_batch_readers (%d) > batch_limit (%d)\n", rw->current_batch_readers, rw->batch_limit);
    }
    if (rw->waiting_readers < 0 || rw->waiting_writers < 0 ||
        rw->active_readers < 0 || rw->active_writers < 0) {
        fprintf(stderr, "Invariant violation: one of the counters is negative\n");
    }
}
#else
#define DBG_PRINT(fmt, ...) do {} while(0)
#define check_invariants(rw) do {} while(0)
#endif

rwlock_t *rwlock_new(PRIORITY p, int n) {
    rwlock_t *rw = malloc(sizeof(rwlock_t));
    if (!rw)
        return NULL;
    rw->priority = p;
    rw->n_way = (p == N_WAY) ? n : 0;
    rw->active_readers = 0;
    rw->active_writers = 0;
    rw->waiting_readers = 0;
    rw->waiting_writers = 0;
    rw->batch_active = false;
    rw->current_batch_readers = 0;
    rw->batch_limit = 0;
    pthread_mutex_init(&rw->mutex, NULL);
    pthread_cond_init(&rw->readers_cv, NULL);
    pthread_cond_init(&rw->writers_cv, NULL);

    DBG_PRINT("Initialized rwlock (priority=%d, n_way=%d)", rw->priority, rw->n_way);
    check_invariants(rw);
    return rw;
}

void rwlock_delete(rwlock_t **l) {
    if (!l || !*l)
        return;
    pthread_mutex_destroy(&(*l)->mutex);
    pthread_cond_destroy(&(*l)->readers_cv);
    pthread_cond_destroy(&(*l)->writers_cv);
    free(*l);
    *l = NULL;
    DBG_PRINT("Deleted rwlock");
}

void reader_lock(rwlock_t *rw) {
    if (!rw)
        return;
    pthread_mutex_lock(&rw->mutex);
    rw->waiting_readers++;
    DBG_PRINT("reader_lock start: waiting_readers=%d, active_readers=%d, active_writers=%d, waiting_writers=%d, batch_active=%d, current_batch_readers=%d, batch_limit=%d",
              rw->waiting_readers, rw->active_readers, rw->active_writers, rw->waiting_writers, rw->batch_active, rw->current_batch_readers, rw->batch_limit);
    check_invariants(rw);

    if (rw->priority == N_WAY) {
        while (rw->active_writers > 0 ||
              (rw->batch_active && rw->current_batch_readers >= rw->batch_limit))
        {
            DBG_PRINT("reader_lock waiting (N_WAY): active_writers=%d, current_batch_readers=%d, batch_limit=%d",
                      rw->active_writers, rw->current_batch_readers, rw->batch_limit);
            pthread_cond_wait(&rw->readers_cv, &rw->mutex);
        }
    } else if (rw->priority == WRITERS) {
        while (rw->active_writers > 0 || rw->waiting_writers > 0) {
            DBG_PRINT("reader_lock waiting (WRITERS): active_writers=%d, waiting_writers=%d",
                      rw->active_writers, rw->waiting_writers);
            pthread_cond_wait(&rw->readers_cv, &rw->mutex);
        }
    } else { // READERS
        while (rw->active_writers > 0) {
            DBG_PRINT("reader_lock waiting (READERS): active_writers=%d", rw->active_writers);
            pthread_cond_wait(&rw->readers_cv, &rw->mutex);
        }
    }

    rw->waiting_readers--;
    rw->active_readers++;
    if (rw->priority == N_WAY && rw->batch_active)
        rw->current_batch_readers++;
    check_invariants(rw);
    DBG_PRINT("Acquired reader_lock: waiting_readers=%d, active_readers=%d, active_writers=%d, current_batch_readers=%d",
              rw->waiting_readers, rw->active_readers, rw->active_writers, rw->current_batch_readers);
    pthread_mutex_unlock(&rw->mutex);
}

void reader_unlock(rwlock_t *rw) {
    if (!rw)
        return;
    pthread_mutex_lock(&rw->mutex);
    rw->active_readers--;
    DBG_PRINT("reader_unlock: active_readers now=%d", rw->active_readers);
    check_invariants(rw);
    if (rw->active_readers == 0 && rw->waiting_writers > 0) {
        DBG_PRINT("reader_unlock signaling writer");
        pthread_cond_signal(&rw->writers_cv);
    }
    pthread_mutex_unlock(&rw->mutex);
}

void writer_lock(rwlock_t *rw) {
    if (!rw)
        return;
    pthread_mutex_lock(&rw->mutex);
    rw->waiting_writers++;
    DBG_PRINT("writer_lock start: waiting_writers=%d, active_readers=%d, active_writers=%d",
              rw->waiting_writers, rw->active_readers, rw->active_writers);
    check_invariants(rw);
    while (rw->active_writers > 0 || rw->active_readers > 0) {
        DBG_PRINT("writer_lock waiting: active_writers=%d, active_readers=%d", rw->active_writers, rw->active_readers);
        pthread_cond_wait(&rw->writers_cv, &rw->mutex);
    }
    rw->waiting_writers--;
    rw->active_writers = 1;
    if (rw->priority == N_WAY) {
        // Reset any previous batch.
        rw->batch_active = false;
        rw->current_batch_readers = 0;
        rw->batch_limit = 0;
    }
    check_invariants(rw);
    DBG_PRINT("Acquired writer_lock: waiting_writers=%d, active_readers=%d, active_writers=%d",
              rw->waiting_writers, rw->active_readers, rw->active_writers);
    pthread_mutex_unlock(&rw->mutex);
}

void writer_unlock(rwlock_t *rw) {
    if (!rw)
        return;
    pthread_mutex_lock(&rw->mutex);
    rw->active_writers = 0;
    DBG_PRINT("writer_unlock: released writer_lock. waiting_writers=%d, waiting_readers=%d", rw->waiting_writers, rw->waiting_readers);
    check_invariants(rw);

    if (rw->priority == N_WAY) {
        if (rw->waiting_writers > 0) {
            if (rw->waiting_readers > 0) {
                // Start a new batch.
                rw->batch_active = true;
                rw->batch_limit = (rw->waiting_readers < rw->n_way) ? rw->waiting_readers : rw->n_way;
                rw->current_batch_readers = 0;
                DBG_PRINT("Starting N_WAY batch: batch_limit=%d", rw->batch_limit);
                pthread_cond_broadcast(&rw->readers_cv);
            } else {
                DBG_PRINT("No waiting readers: signaling writer");
                pthread_cond_signal(&rw->writers_cv);
            }
        } else {
            // No waiting writer; clear batch constraints.
            rw->batch_active = false;
            DBG_PRINT("No waiting writer: broadcasting readers");
            pthread_cond_broadcast(&rw->readers_cv);
        }
    } else if (rw->priority == READERS) {
        if (rw->waiting_readers > 0)
            pthread_cond_broadcast(&rw->readers_cv);
        else if (rw->waiting_writers > 0)
            pthread_cond_signal(&rw->writers_cv);
    } else if (rw->priority == WRITERS) {
        if (rw->waiting_writers > 0)
            pthread_cond_signal(&rw->writers_cv);
        else if (rw->waiting_readers > 0)
            pthread_cond_broadcast(&rw->readers_cv);
    }
    check_invariants(rw);
    pthread_mutex_unlock(&rw->mutex);
    DBG_PRINT("writer_unlock complete");
}
