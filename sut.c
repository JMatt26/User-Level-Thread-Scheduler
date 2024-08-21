#define _XOPEN_SOURCE 600
#include <pthread.h>
#include <ucontext.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <sut.h>
#include <queue.h>

#define STACK_SIZE (1024*1024)

pthread_t *C_EXEC;
pthread_t *I_EXEC;

ucontext_t *C_EXEC_STATE;
ucontext_t *C_CURRENT_STATE;

ucontext_t *I_EXEC_STATE;
ucontext_t *I_CURRENT_STATE;

struct queue readyQueue;
struct queue waitQueue;

pthread_mutex_t *lock_C;
pthread_mutex_t *lock_I;
pthread_mutex_t *lock_Counter;

int exitCondition = 0;
int globalCounter = 0;

struct queue_entry* startIO() {
    struct queue_entry *node = queue_new_node(C_CURRENT_STATE);
    pthread_mutex_lock(lock_I);
    queue_insert_tail(&waitQueue, node);
    pthread_mutex_unlock(lock_I);
    C_CURRENT_STATE = NULL;
    if (swapcontext((ucontext_t *) node->data, C_EXEC_STATE) != 0) {
        perror("Swap context failed");
    }

    return node;
}

void endIO(struct queue_entry* node) {
    node = queue_new_node(I_CURRENT_STATE);
    pthread_mutex_lock(lock_C);
    queue_insert_tail(&readyQueue, node);
    pthread_mutex_unlock(lock_C);
    I_CURRENT_STATE = NULL;
    if (swapcontext((ucontext_t *) node->data, I_EXEC_STATE) != 0) {
        perror("Swap context failed");
    }
}

void * readyPoll() {
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = 100000;
    while (1) {
        if (queue_peek_front(&readyQueue) != NULL) {
            pthread_mutex_lock(lock_C);
            struct queue_entry *task = queue_pop_head(&readyQueue);
            pthread_mutex_unlock(lock_C);
            C_CURRENT_STATE = (ucontext_t *) task->data;
            free(task);
            task = NULL;
            if (swapcontext(C_EXEC_STATE, C_CURRENT_STATE) != 0) {
                perror("Task failed to execute\n");
            }
            if (exitCondition == 1) {
                free(C_CURRENT_STATE->uc_stack.ss_sp);
                free(C_CURRENT_STATE);
                if (task != NULL) {
                    free(((ucontext_t *) task->data)->uc_stack.ss_sp);
                    free(task->data);
                    free(task);
                }
                C_CURRENT_STATE = NULL;
                exitCondition = 0;
            }
        }
        if (nanosleep(&req, &rem) == -1) {
            perror("Nanosleep failed to execute\n");
        }
        pthread_testcancel();
    }
}

void * waitPoll() {
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = 100000;
    while (1) {
        if (queue_peek_front(&waitQueue) != NULL) {
            pthread_mutex_lock(lock_I);
            struct queue_entry *task = queue_pop_head(&waitQueue);
            pthread_mutex_unlock(lock_I);
            I_CURRENT_STATE = (ucontext_t *) task -> data;
            free(task);
            task = NULL;
            if (swapcontext(I_EXEC_STATE, I_CURRENT_STATE) != 0) {
                perror("Task failed to execute\n");
            }
            if (exitCondition == 1) {
                free(I_CURRENT_STATE->uc_stack.ss_sp);
                free(I_CURRENT_STATE);
                if (task != NULL) {
                    free(((ucontext_t *) task->data)->uc_stack.ss_sp);
                    free(task->data);
                    free(task);
                }
                I_CURRENT_STATE = NULL;
                exitCondition = 0;
            }
        }
        if (nanosleep(&req, &rem) == -1) {
            perror("Nanosleep failed to execute\n");
        }
        pthread_testcancel();
    }
}

void sut_init(){

    C_EXEC = (pthread_t *)malloc(sizeof(pthread_t));
    I_EXEC = (pthread_t *)malloc(sizeof(pthread_t));

    C_EXEC_STATE = (ucontext_t *)malloc(sizeof(ucontext_t));
    I_EXEC_STATE = (ucontext_t *)malloc(sizeof(ucontext_t));

    C_CURRENT_STATE = (ucontext_t *)malloc(sizeof(ucontext_t));
    I_CURRENT_STATE = (ucontext_t *)malloc(sizeof(ucontext_t));

    lock_C = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    lock_I = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    lock_Counter = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));

    pthread_mutex_init(lock_C, NULL);
    pthread_mutex_init(lock_I, NULL);
    pthread_mutex_init(lock_Counter, NULL);

    readyQueue = queue_create();
    queue_init(&readyQueue);

    waitQueue = queue_create();
    queue_init(&waitQueue);

    if (pthread_create(C_EXEC, NULL, readyPoll, NULL) != 0) {
        perror("C_EXEC kernel thread failed to create\n");
    }

    if (pthread_create(I_EXEC, NULL, waitPoll, NULL) != 0) {
        perror("I_EXEC kernel thread failed to create\n");
    }
}

bool sut_create(sut_task_f fn){
    ucontext_t *t = (ucontext_t *)malloc(sizeof(ucontext_t));

    if (getcontext(t) == -1) {
        return 0;
    }

    char *stack = (char *)malloc(sizeof(char) * STACK_SIZE);

    if (stack == NULL) {
        return 0;
    }

    t -> uc_stack.ss_sp = stack; // telling pointer where our stack is
    t -> uc_stack.ss_size = STACK_SIZE;
    t -> uc_stack.ss_flags = 0; // 0 for user level threads

    makecontext(t, fn, 0);

    pthread_mutex_lock(lock_Counter);
    globalCounter++;
    pthread_mutex_unlock(lock_Counter);

    struct queue_entry *node = queue_new_node(t);
    pthread_mutex_lock(lock_C);
    queue_insert_tail(&readyQueue, node);
    pthread_mutex_unlock(lock_C);
    return 1;
}

void sut_yield(){
    struct queue_entry *node = queue_new_node(C_CURRENT_STATE);
    pthread_mutex_lock(lock_C);
    queue_insert_tail(&readyQueue, node);
    pthread_mutex_unlock(lock_C);
    C_CURRENT_STATE = NULL;
    swapcontext((ucontext_t *) node->data, C_EXEC_STATE);
}

void sut_exit(){
    pthread_mutex_lock(lock_Counter);
    globalCounter--;
    pthread_mutex_unlock(lock_Counter);

    exitCondition = 1;

    setcontext(C_EXEC_STATE);
}

int sut_open(char *file_name){
    struct queue_entry *node = startIO();

    FILE *targetFile = fopen(file_name, "a+");

    if (targetFile == NULL) {
        perror("failed to open file");
        return -1;
    }

    int fd = fileno(targetFile);

    endIO(node);

    return fd;
}

void sut_close(int fd){
    struct queue_entry *node = startIO();

    pthread_mutex_lock(lock_I);
    close(fd);
    pthread_mutex_unlock(lock_I);

    endIO(node);
}

void sut_write(int fd, char *buf, int size){
    struct queue_entry *node = startIO();

    if (write(fd, buf, size) == -1) {
        perror("Unable to write to file \n");
    }

    endIO(node);
}

char* sut_read(int fd, char *buf, int size){
    struct queue_entry *node = startIO();

    if (read(fd, buf, size) == -1) {
        perror("Unable to read from the file \n");
    }

    endIO(node);

    return buf;
}

void sut_shutdown() {
    while(1) {
        if (globalCounter == 0) {
            pthread_cancel(*C_EXEC);
            pthread_cancel(*I_EXEC);
            pthread_join(*C_EXEC, NULL);
            pthread_join(*I_EXEC, NULL);
            free(C_EXEC);
            free(I_EXEC);
            free(C_EXEC_STATE);
            free(I_EXEC_STATE);
            free(lock_C);
            free(lock_I);
            free(lock_Counter);
            free(C_CURRENT_STATE);
            free(I_CURRENT_STATE);
            exit(0);
        }
    }
}