#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

// Definitions from your rwlock.c (no header file)
typedef enum { READERS, WRITERS, N_WAY } PRIORITY;
typedef struct rwlock rwlock_t;
rwlock_t *rwlock_new(PRIORITY p, int n);
void rwlock_delete(rwlock_t **l);
void reader_lock(rwlock_t *rw);
void reader_unlock(rwlock_t *rw);
void writer_lock(rwlock_t *rw);
void writer_unlock(rwlock_t *rw);

#define NUM_READERS 5
#define NUM_WRITERS 2

void *reader_thread(void *arg) {
    rwlock_t *lock = (rwlock_t *)arg;
    reader_lock(lock);
    printf("Reader thread %lu acquired lock\n", pthread_self());
    sleep(1);  // simulate some read work
    reader_unlock(lock);
    printf("Reader thread %lu released lock\n", pthread_self());
    return NULL;
}

void *writer_thread(void *arg) {
    rwlock_t *lock = (rwlock_t *)arg;
    writer_lock(lock);
    printf("Writer thread %lu acquired lock\n", pthread_self());
    sleep(1);  // simulate some write work
    writer_unlock(lock);
    printf("Writer thread %lu released lock\n", pthread_self());
    return NULL;
}

int main(void) {
    // Create an N_WAY lock with n_way = 3.
    rwlock_t *lock = rwlock_new(N_WAY, 3);
    if (!lock) {
        fprintf(stderr, "Failed to create rwlock\n");
        return 1;
    }
    printf("rwlock created\n");

    pthread_t readers[NUM_READERS];
    pthread_t writers[NUM_WRITERS];

    // Start writer threads first to ensure there's contention.
    for (int i = 0; i < NUM_WRITERS; i++) {
        pthread_create(&writers[i], NULL, writer_thread, lock);
        sleep(0.1);  // stagger their start slightly
    }
    // Start reader threads.
    for (int i = 0; i < NUM_READERS; i++) {
        pthread_create(&readers[i], NULL, reader_thread, lock);
        sleep(0.1);  // stagger their start slightly
    }

    // Join all threads.
    for (int i = 0; i < NUM_WRITERS; i++) {
        pthread_join(writers[i], NULL);
    }
    for (int i = 0; i < NUM_READERS; i++) {
        pthread_join(readers[i], NULL);
    }

    rwlock_delete(&lock);
    printf("rwlock deleted\n");
    return 0;
}
