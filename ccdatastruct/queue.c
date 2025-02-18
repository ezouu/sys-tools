
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct queue queue_t;

struct queue {
    void **buffer;
    int size;
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
};

queue_t *queue_new(int size) {
    if (size <= 0) {
        return NULL;
    }

    queue_t *q = malloc(sizeof(queue_t));
    if (q == NULL) {
        return NULL;
    }

    q->buffer = malloc(sizeof(void *) * size);
    if (q->buffer == NULL) {
        free(q);
        return NULL;
    }

    q->size = size;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    return q;
}

void queue_delete(queue_t **q) {
    if (q == NULL || *q == NULL) {
        return;
    }
    pthread_mutex_destroy(&(*q)->mutex);
    pthread_cond_destroy(&(*q)->not_full);
    pthread_cond_destroy(&(*q)->not_empty);
    free((*q)->buffer);
    free(*q);
    *q = NULL;
}

bool queue_push(queue_t *q, void *elem) {
    if (q == NULL) {
        return false;
    }

    pthread_mutex_lock(&q->mutex);

    while (q->count == q->size) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }

    q->buffer[q->tail] = elem;
    q->tail = (q->tail + 1) % q->size;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

bool queue_pop(queue_t *q, void **elem) {
    if (q == NULL) {
        return false;
    }

    pthread_mutex_lock(&q->mutex);
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    *elem = q->buffer[q->head];
    q->head = (q->head + 1) % q->size;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return true;
}
