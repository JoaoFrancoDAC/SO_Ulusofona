#ifndef QUEUE_H
#define QUEUE_H
#include <stdint.h>

#define MAX_CLIENTS 128

#include "pcb.h"

// Define singly linked list elements
typedef struct queue_elem_st queue_elem_t;

typedef struct queue_elem_st {
    pcb_t *pcb;
    struct queue_elem_st *next;
} queue_elem_t;

// Define the queue structure
// We define the head and the tail to make it easier to enqueue and dequeue
typedef struct queue_st  {
    queue_elem_t* head;
    queue_elem_t* tail;
    ssize_t size;
} queue_t;

uint32_t now_ms(void);
void reset_time(void);

/**
 * @brief Create a new pcb (process control block)
 *
 * @param pid The process ID of the task
 * @param sockfd The socket file descriptor for communication with the application
 * @param time_ms a time field (either for run or block)
 */
pcb_t *new_pcb(int32_t pid, uint32_t sockfd, uint32_t time_ms, uint32_t current_time_ms);

/**
 * @brief Enqueue a pcb into the queue (at the tail, FIFO order)
 * @return 1 on success, 0 on failure
 */
int enqueue_pcb(queue_t* q, pcb_t* task);

/**
 * @brief Dequeue a pcb from the head of the queue (FIFO order)
 * @return The pcb at the front of the queue, or NULL if the queue is empty
 */
pcb_t* dequeue_fifo(queue_t* q);

//dequeue com a lógica do algoritmo SJF
//Retorna um ponteiro para o pcb de queue_elem que possuia o menor tempo de execução total.
pcb_t* dequeue_sjf(queue_t *rq);


// necessita ser diferente que o anterior para ocorrer a preempção
pcb_t* dequeue_srtf(queue_t *q);

//dequeu com a logica do algoritmo Round Robin
pcb_t* dequeue_rr(queue_t *rq);


pcb_t* dequeue_mlfq(queue_t *rq);
/**
 * @brief Remove a specific element from the queue.
 * Neither the element, nor the pcb inside the element, are freed.
 * @return The removed element, or NULL if the element was not found
 */
queue_elem_t *remove_queue_elem(queue_t* q, queue_elem_t* elem);

void check_blocked_queue(queue_t *blocked_queue, queue_t *command_queue, uint32_t current_time_ms);

void check_new_commands(queue_t *command_queue, queue_t *blocked_queue, queue_t *ready_queue, queue_t *completed_queue,
                        int server_fd, uint32_t current_time_ms);

void boost_all_priorities(queue_t *rq);

pcb_t* get_shortest_remaining_task(queue_t *q);

ssize_t receive_msg(int sockfd, void *msg, ssize_t msg_len);

int setup_server_socket(const char *socket_path);
#endif //QUEUE_H
