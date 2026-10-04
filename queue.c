#include "queue.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "debug.h"

static uint32_t PID = 0;
static uint32_t start_time_ms = 0;

void reset_time(void) {
    start_time_ms = 0;
    start_time_ms = now_ms();
}

uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint32_t)(uint64_t)ts.tv_sec * 1000
         + (uint64_t)ts.tv_nsec / 1000000) - start_time_ms;
}

pcb_t *new_pcb(pid_t pid, uint32_t sockfd, uint32_t time_ms, uint32_t current_time_ms) {
    pcb_t *new_task = malloc(sizeof(pcb_t));
    if (!new_task) return NULL;

    new_task->pid = pid;
    new_task->status = TASK_COMMAND;
    new_task->slice_start_ms = 0;
    new_task->sockfd = sockfd;
    new_task->time_ms = time_ms;
    new_task->ellapsed_time_ms = 0;
    new_task->total_cpu_time_ms = 0;
    new_task->last_update_time_ms = 0;
    new_task->priority = 0;
    new_task->priority_time_ms = 0;

    //metricas apenas para os logs

    new_task->arrival_time_ms = current_time_ms;       // Será preenchido no NEW -> READY
    new_task->first_run_time_ms = 0;     // Será atualizado no primeiro READY > RUNNING
    new_task->completion_time_ms = 0;
    new_task->total_waiting_time_ms = 0;
    new_task->context_switches = 0;

    return new_task;
}

int enqueue_pcb(queue_t* q, pcb_t* task) {
    queue_elem_t* elem = malloc(sizeof(queue_elem_t));
    if (!elem) return 0;

    elem->pcb = task;
    elem->next = NULL;

    if (q->tail) {
        q->tail->next = elem;
    } else {
        q->head = elem;
    }
    q->tail = elem;
    q->size++;
    return 1;
}

//Função que busca o elemento a ser passado da readyqueue para running
//será sempre na ordem que estiver ficado ready
pcb_t* dequeue_fifo(queue_t* q) {
    if (!q || !q->head) return NULL;

    queue_elem_t* node = q->head;
    pcb_t* task = node->pcb;

    q->head = node->next;
    if (!q->head)
        q->tail = NULL;

    free(node);
    q->size--;
    return task;
}

//função que busca o elemento cujo tem o pcb com menor execução para levar ao estado de running

pcb_t* dequeue_sjf(queue_t *rq) {
    if (rq == NULL || rq->head == NULL) {
        return NULL;
    }

    queue_elem_t *curr = rq->head;
    queue_elem_t *shortest_elem = curr;
    uint32_t min_time = curr->pcb->time_ms;

    //lógica para achar o menor pcb
    while (curr != NULL) {
        if (curr->pcb->time_ms < min_time) {
            min_time = curr->pcb->time_ms;
            shortest_elem = curr;
        }
        curr = curr->next;
    }

    //removo a node que possuia o pcb com menor tempo
    queue_elem_t *removed_elem = remove_queue_elem(rq,shortest_elem);

    // pego ar eferencia para o pcb que estava dentro da node
    pcb_t *pcb=removed_elem->pcb;

    //libero o ponteiro da node já que ele correrá até o fim e não precisará voltar para a Ready queue
    free(removed_elem);

    return pcb;
}

pcb_t* dequeue_srtf(queue_t *q) {
    if (!q || !q->head) return NULL;

    queue_elem_t *curr = q->head;
    queue_elem_t *best_elem = curr;
    queue_elem_t *prev = NULL;
    queue_elem_t *best_prev = NULL;

    uint32_t min_remaining = curr->pcb->time_ms - curr->pcb->ellapsed_time_ms;

    while (curr->next != NULL) {
        uint32_t rem = curr->next->pcb->time_ms - curr->next->pcb->ellapsed_time_ms;
        if (rem < min_remaining) {
            min_remaining = rem;
            best_prev = curr;
            best_elem = curr->next;
        }
        curr = curr->next;
    }

    // Remover o nó encontrado da lista ligada
    if (best_prev == NULL) {
        q->head = best_elem->next;
    } else {
        best_prev->next = best_elem->next;
    }

    if (best_elem == q->tail) {
        q->tail = best_prev;
    }

    pcb_t *selected_pcb = best_elem->pcb;
    free(best_elem);
    q->size--;

    return selected_pcb;
}

pcb_t* dequeue_rr(queue_t *rq) {
    // A lógica de dequeue de round-robin é exatamente a mesma que a da fifo
    // apenas separei em 2 funções para organizar o código e ficar claro que são 2 coisas diferentes acontecendo
    // mas que em essencia fazem a mesma coisa em termos de "desenfileirar".
    return dequeue_fifo(rq);
}

pcb_t* dequeue_mlfq(queue_t *rq) {
    if (rq == NULL || rq->head == NULL) {
        return NULL;
    }

    queue_elem_t *curr = rq->head;
    queue_elem_t *best_elem = curr;

    while (curr != NULL) {
        if (curr->pcb->priority < best_elem->pcb->priority) {
            best_elem = curr;
        }
        curr = curr->next;
    }

    pcb_t *selected_pcb = best_elem->pcb;

    queue_elem_t *removed_elem = remove_queue_elem(rq, best_elem);

    if (removed_elem != NULL) {
        free(removed_elem);
    }

    return selected_pcb;
}

void boost_all_priorities(queue_t *rq) {
    if (rq == NULL || rq->head == NULL) {
        return;
    }

    queue_elem_t *curr = rq->head;
    while (curr != NULL) {
        if (curr->pcb!=NULL){
            curr->pcb->priority = 0;
            curr->pcb->priority_time_ms = 0;
        }
        curr = curr->next;
    }
}

queue_elem_t *remove_queue_elem(queue_t* q, queue_elem_t* elem) {
    queue_elem_t* it = q->head;
    queue_elem_t* prev = NULL;
    while (it != NULL) {
        if (it == elem) {
            if (prev) {
                prev->next = it->next;
            } else {
                q->head = it->next;
            }
            if (it == q->tail) {
                q->tail = prev;
            }
            q->size--;
            return it;
        }
        prev = it;
        it = it->next;
    }
    printf("Queue element not found in queue\n");
    return NULL;
}

/**
 * @brief Set up the server socket for the scheduler.
 *
 * Creates a UNIX domain socket, binds it to SOCKET_PATH, sets it to listen and
 * to non-blocking mode.
 *
 * @return the server file descriptor on success, or -1 on failure
 */
int setup_server_socket(const char *socket_path) {
    int server_fd;
    struct sockaddr_un addr;

    unlink(socket_path);

    if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags != -1) {
        if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl: set non-blocking");
        }
    }
    return server_fd;
}

/**
 * When using read() we can get partial reads, so loop until the full message arrives.
 * @return bytes read, 0 if no data available (non-blocking), -1 on error / peer closed
 */
ssize_t receive_msg(int sockfd, void *msg, ssize_t msg_len) {
    ssize_t want = msg_len;
    ssize_t off = 0;

    while (off < want) {
        ssize_t n = read(sockfd, ((char*)msg) + off, want - off);
        if (n > 0) {
            off += n;
            if (off == want) return off;
        }
        if (n == 0)  return -1; // peer closed
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            perror("read");
            return -1;
        }
    }
    return off;
}

/**
 * @brief Accept new client connections and service the COMMAND queue.
 *
 * New connections become PCBs in the COMMAND queue. PCBs already in COMMAND are
 * polled for a RUN or BLOCK message and moved to READY / BLOCKED accordingly.
 */

void check_new_commands(queue_t *command_queue, queue_t *blocked_queue, queue_t *ready_queue, queue_t *completed_queue,
                        int server_fd, uint32_t current_time_ms)
{
    // 1. Accept new client connections
    int client_fd;
    do {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EMFILE || errno == ENFILE) break;
            if (errno == EINTR || errno == ECONNABORTED) continue;
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) perror("accept");
            break;
        }
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags != -1) fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        int fdflags = fcntl(client_fd, F_GETFD, 0);
        if (fdflags != -1) fcntl(client_fd, F_SETFD, fdflags | FD_CLOEXEC);

        pcb_t *pcb = new_pcb(++PID, client_fd, 0,current_time_ms);

        printf("%-32s | Hora: %6u ms | Socket: %d | Status: CONECTADO e EM STANDBY (COMMAND QUEUE)\n\n",
            "R3 [OFFLINE -> NEW CLIENT]", current_time_ms, pcb->sockfd);

        enqueue_pcb(command_queue, pcb);
    } while (client_fd > 0);

    // 2. Walk COMMAND queue com ponteiros PREV e CURR seguros
    queue_elem_t *curr = command_queue->head;
    queue_elem_t *prev = NULL;

    while (curr != NULL) {
        pcb_t *current_pcb = curr->pcb;
        msg_t msg;

        ssize_t n = receive_msg(current_pcb->sockfd, &msg, sizeof(msg_t));

        // Sem dados no socket
        if (n == 0) {
            prev = curr;
            curr = curr->next;
            continue;
        }

        // Socket fechado pelo cliente (fim do programa / erro)
        if (n < 0) {
            DBG("Connection closed by client (fd=%d, pid=%d)", current_pcb->sockfd, current_pcb->pid);

            current_pcb->status = TASK_TERMINATED;
            current_pcb->completion_time_ms = current_time_ms;
            enqueue_pcb(completed_queue, current_pcb);

            close(current_pcb->sockfd);

            // Remove o nó curr desvinculando-o da lista
            queue_elem_t *to_free = curr;
            if (prev) {
                prev->next = curr->next;
            } else {
                command_queue->head = curr->next;
            }
            if (curr == command_queue->tail) {
                command_queue->tail = prev;
            }
            command_queue->size--;

            curr = curr->next;
            free(to_free);
            continue;
        }

        // Processa RUN ou BLOCK
        if (msg.request == PROCESS_REQUEST_RUN) {
            if (current_pcb == NULL) {
                fprintf(stderr, "Erro: PCB nulo encontrado na command_queue!\n");
                prev = curr;
                curr = curr->next;
                continue;
            }

            current_pcb->pid = msg.pid;
            current_pcb->time_ms = msg.time_ms;
            current_pcb->priority = 0;
            current_pcb->priority_time_ms = 0;
            current_pcb->ellapsed_time_ms = 0;


            current_pcb->status = TASK_RUNNING;

            //current_pcb->arrival_time_ms = current_time_ms;
            //current_pcb->first_run_time_ms = 0;

            printf("%-32s | Hora: %6u ms | PID: %6d | Pedido de CPU: %6u ms | Socket: %d\n",
                   "R3 [NEW -> READY]", current_time_ms, current_pcb->pid, current_pcb->time_ms, current_pcb->sockfd);
            fflush(stdout);

            enqueue_pcb(ready_queue, current_pcb);

        } else if (msg.request == PROCESS_REQUEST_BLOCK) {
            if (current_pcb == NULL) {
                prev = curr;
                curr = curr->next;
                continue;
            }

            current_pcb->pid = msg.pid;
            current_pcb->time_ms = msg.time_ms;
            current_pcb->status = TASK_BLOCKED;

            enqueue_pcb(blocked_queue, current_pcb);
        } else {
            prev = curr;
            curr = curr->next;
            continue;
        }

        // Envia o ACK ao cliente
        msg_t ack_msg = {
            .pid = current_pcb->pid,
            .request = PROCESS_REQUEST_ACK,
            .time_ms = current_time_ms
        };
        if (write(current_pcb->sockfd, &ack_msg, sizeof(msg_t)) != sizeof(msg_t)) {
            perror("write");
        }

        // Desvincula o nó processado da command_queue e faz o free de forma 100% segura
        queue_elem_t *to_free = curr;
        if (prev) {
            prev->next = curr->next;
        } else {
            command_queue->head = curr->next;
        }
        if (curr == command_queue->tail) {
            command_queue->tail = prev;
        }
        command_queue->size--;

        curr = curr->next;
        free(to_free);
    }
}

/**
 * @brief Advance PCBs in the BLOCKED queue and release the ones whose I/O is done.
 *
 * Each tick decrements the remaining block time. When it reaches zero the PCB is
 * sent a DONE message and moved to the COMMAND queue for the next instruction.
 */
void check_blocked_queue(queue_t *blocked_queue, queue_t *command_queue, uint32_t current_time_ms) {
    queue_elem_t *elem = blocked_queue->head;
    while (elem != NULL) {
        // Guardar SEMPRE o próximo elemento antes de alterar a lista ou libertar memória
        queue_elem_t *next_elem = elem->next;
        pcb_t *pcb = elem->pcb;

        if (pcb->last_update_time_ms < current_time_ms) {
            if (pcb->time_ms > TICKS_MS) {
                pcb->time_ms -= TICKS_MS;
            } else {
                pcb->time_ms = 0;
            }
        }

        if (pcb->time_ms == 0) {
            msg_t msg = {
                .pid = pcb->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write(pcb->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }
            pcb->status = TASK_COMMAND;
            pcb->last_update_time_ms = current_time_ms;

            // Enfileira na command_queue
            enqueue_pcb(command_queue, pcb);

            // Remove o nó da blocked_queue de forma segura
            remove_queue_elem(blocked_queue, elem);
            free(elem);
        }

        // Avança em segurança
        elem = next_elem;
    }
}

pcb_t* get_shortest_remaining_task(queue_t *q) {
    if (!q || !q->head) return NULL;

    queue_elem_t *curr = q->head;
    pcb_t *best_pcb = curr->pcb;
    uint32_t min_remaining = best_pcb->time_ms - best_pcb->ellapsed_time_ms;

    curr = curr->next;
    while (curr != NULL) {
        uint32_t rem = curr->pcb->time_ms - curr->pcb->ellapsed_time_ms;
        if (rem < min_remaining) {
            min_remaining = rem;
            best_pcb = curr->pcb;
        }
        curr = curr->next;
    }
    return best_pcb;
}

