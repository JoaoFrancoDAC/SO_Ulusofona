#include "scheduler.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "msg.h"

#define QUANTUM_TIME_BURST (210)
#define MAX_PRIORITY 4
#define MLFQ_TIME_BURST 200
#define BOOST_INTERVAL_MS 2000

static const char *SCHED_NAMES[] = { "FIFO", "SJF","SRTF", "RR", "MLFQ", NULL };
static sched_algo_en sched_algo = SCHED_MLFQ;

int set_sched_algo(const char *name) {
    for (int i = 0; SCHED_NAMES[i] != NULL; i++) {
        if (strcasecmp(SCHED_NAMES[i], name) == 0) {
            sched_algo = (sched_algo_en) i;
            return sched_algo;
        }
    }
    return -1;
}

const char *get_sched_algo_str(void) {
    return SCHED_NAMES[sched_algo];
}

static void log_event(const char *tag, uint32_t hora, int pid, const char *algo, uint32_t burst_total, uint32_t executado, const char *detalhe) {
    // Coluna 1: Tag da transição (ex: R3 [READY -> RUNNING]) - Largura 32
    // Coluna 2: Hora - Largura 6
    // Coluna 3: PID - Largura 6
    // Coluna 4: Algoritmo - Largura 8
    // Coluna 5: Burst Total - Largura 6
    // Coluna 6: Executado / Total - Formato "Executado: %6u / %-6u ms"

    if (detalhe != NULL && detalhe[0] != '\0') {
        printf("%-32s | Hora: %6u ms | PID: %6d | Algo: %-8s | Burst Total: %6u ms | Executado: %6u / %-6u ms | %s\n",
               tag, hora, pid, algo, burst_total, executado, burst_total, detalhe);
    } else if (executado > 0) {
        printf("%-32s | Hora: %6u ms | PID: %6d | Algo: %-8s | Burst Total: %6u ms | Executado: %6u / %-6u ms\n",
               tag, hora, pid, algo, burst_total, executado, burst_total);
    } else {
        printf("%-32s | Hora: %6u ms | PID: %6d | Algo: %-8s | Burst Total: %6u ms | Executado: %6u / %-6u ms\n",
               tag, hora, pid, algo, burst_total, executado, burst_total);
    }
    fflush(stdout);
}


/**
 * Send DONE to the application and move the finished burst to the command queue.
 */
static void finish_burst(uint32_t current_time_ms, queue_t *cq, pcb_t *task) {
    task->status = TASK_COMMAND;
    task->ellapsed_time_ms = 0;

    msg_t msg = {
        .pid = task->pid,
        .request = PROCESS_REQUEST_DONE,
        .time_ms = current_time_ms
    };
    if (write(task->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
        perror("write");
    }


    enqueue_pcb(cq, task);
}

/**
 * FIFO (First-In, First-Out) — não preemptivo.
 *
 * O primeiro processo a chegar à ready queue corre até o seu burst de CPU
 * terminar; só então o CPU fica livre para o processo seguinte.
 *
 * TODO (alunos): acrescentar os algoritmos SJF, RR (time-slice = TIME_SLICE_MS)
 * e MLFQ, selecionados por `sched_algo` / `--sched`.
 */

static uint32_t last_boost_time_ms = 0;
int scheduler(uint32_t current_time_ms, queue_t *rq, queue_t *cq, pcb_t **cpu_task,queue_t *finished_queue) {
    switch (sched_algo) {
        case SCHED_SJF:
            return scheduler_sjf(current_time_ms, rq, cq, cpu_task, finished_queue);
        case SCHED_RR:
            return scheduler_rr(current_time_ms, rq, cq, cpu_task, finished_queue);
        case SCHED_MLFQ:
            return scheduler_mlfq(current_time_ms, rq, cq, cpu_task, finished_queue);
        case SCHED_SRTF:
            return scheduler_srtf(current_time_ms, rq, cq, cpu_task, finished_queue);
        case SCHED_FIFO:
        default:
            return scheduler_fifo(current_time_ms, rq, cq, cpu_task, finished_queue);
    }
}

int scheduler_fifo(uint32_t current_time_ms, queue_t *rq, queue_t *cq, pcb_t **cpu_task, queue_t *finished_queue) {
    if (*cpu_task != NULL) {


        if ((*cpu_task)->first_run_time_ms == 0) {
            (*cpu_task)->first_run_time_ms = current_time_ms;
        }

        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        (*cpu_task)->total_cpu_time_ms += TICKS_MS;

        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            finish_burst(current_time_ms, cq, *cpu_task);

            printf("%-32s | Hora: %6u ms | PID: %6d | Algo: %-8s | Burst Concluido (%u ms)\n",
                   "R3 [RUNNING -> DONE]",
                   current_time_ms,
                   (*cpu_task)->pid,
                   "FIFO",
                   (*cpu_task)->time_ms);

            *cpu_task = NULL;
        }
    }

    if (*cpu_task == NULL) {
        *cpu_task = dequeue_fifo(rq);
        if (*cpu_task != NULL) {

            (*cpu_task)->context_switches++;

            (*cpu_task)->slice_start_ms = current_time_ms;
            (*cpu_task)->status = TASK_RUNNING;

            log_event("R3 [READY -> RUNNING]",
                      current_time_ms,
                      (*cpu_task)->pid,
                      "FIFO",
                      (*cpu_task)->time_ms,
                      (*cpu_task)->ellapsed_time_ms,
                      NULL);

            return 1;
        }
    }
    return 0;
}

int scheduler_sjf(uint32_t current_time_ms, queue_t *rq, queue_t *cq, pcb_t **cpu_task, queue_t *finished_queue) {
    if (*cpu_task != NULL) {

        if ((*cpu_task)->first_run_time_ms == 0) {
            (*cpu_task)->first_run_time_ms = current_time_ms;
        }

        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        (*cpu_task)->total_cpu_time_ms += TICKS_MS;

        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            printf("%-32s | Hora: %6u ms | PID: %6d | Algo: %-8s | Burst Concluido (%u ms)\n",
                   "R3 [RUNNING -> DONE]",
                   current_time_ms,
                   (*cpu_task)->pid,
                   "SJF",
                   (*cpu_task)->time_ms);
            fflush(stdout);

            finish_burst(current_time_ms, cq, *cpu_task);

            *cpu_task = NULL;
        }
    }

    if (*cpu_task == NULL) {
        *cpu_task = dequeue_sjf(rq);
        if (*cpu_task != NULL) {

            (*cpu_task)->context_switches++;


            (*cpu_task)->slice_start_ms = current_time_ms;
            (*cpu_task)->status = TASK_RUNNING;

            log_event("R3 [READY -> RUNNING]",
                      current_time_ms,
                      (*cpu_task)->pid,
                      "SJF",
                      (*cpu_task)->time_ms,
                      (*cpu_task)->ellapsed_time_ms,
                      NULL);

            return 1;
        }
    }
    return 0;
}


int scheduler_srtf(uint32_t current_time_ms, queue_t *rq, queue_t *cq, pcb_t **cpu_task, queue_t *finished_queue) {


    if (*cpu_task != NULL && rq != NULL && rq->size > 0) {
        // Encontrar a tarefa na Ready Queue que tem o menor tempo restante
        pcb_t *shortest_in_rq = get_shortest_remaining_task(rq);


        // Só há um problema, que é ficar varrendo a lista ligada em todos os ticks do programa em busca de um candidato ao possível menor tempo de execução
        if (shortest_in_rq != NULL) {
            uint32_t current_remaining = (*cpu_task)->time_ms - (*cpu_task)->ellapsed_time_ms;
            uint32_t candidate_remaining = shortest_in_rq->time_ms - shortest_in_rq->ellapsed_time_ms;

            // Se a nova tarefa precisar de menos tempo do que o restante da atual -> Preempção!
            if (candidate_remaining < current_remaining) {

                log_event("R3 [RUNNING -> READY (PREEMPT)]",
                          current_time_ms,
                          (*cpu_task)->pid,
                          "SRTF",
                          (*cpu_task)->time_ms,
                          (*cpu_task)->ellapsed_time_ms,
                          "Preemptado por tarefa com menor tempo restante");

                enqueue_pcb(rq, *cpu_task);
                *cpu_task = NULL;
            }
        }
    }

    // 2. Processar a tarefa atual na CPU
    if (*cpu_task != NULL) {

        if ((*cpu_task)->first_run_time_ms == 0) {
            (*cpu_task)->first_run_time_ms = current_time_ms;
        }

        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        (*cpu_task)->total_cpu_time_ms += TICKS_MS;

        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            printf("%-32s | Hora: %6u ms | PID: %6d | Algo: %-8s | Burst Concluido (%u ms)\n",
                   "R3 [RUNNING -> DONE]",
                   current_time_ms,
                   (*cpu_task)->pid,
                   "SRTF",
                   (*cpu_task)->time_ms);
            fflush(stdout);

            finish_burst(current_time_ms, cq, *cpu_task);

            *cpu_task = NULL;
        }
    }

    // 3. CPU vazia: Selecionar a tarefa com menor tempo restante (SRTF)
    if (*cpu_task == NULL) {
        *cpu_task = dequeue_srtf(rq); // Desenfileira a tarefa com menor (time_ms - ellapsed_time_ms)

        if (*cpu_task != NULL) {

            (*cpu_task)->context_switches++;
            (*cpu_task)->slice_start_ms = current_time_ms;
            (*cpu_task)->status = TASK_RUNNING;

            log_event("R3 [READY -> RUNNING]",
                      current_time_ms,
                      (*cpu_task)->pid,
                      "SRTF",
                      (*cpu_task)->time_ms,
                      (*cpu_task)->ellapsed_time_ms,
                      NULL);

            return 1;
        }
    }
    return 0;
}


int scheduler_rr(uint32_t current_time_ms, queue_t *rq, queue_t *cq, pcb_t **cpu_task, queue_t *finished_queue) {
    if (*cpu_task != NULL) {
        if ((*cpu_task)->first_run_time_ms == 0) {
            (*cpu_task)->first_run_time_ms = current_time_ms;
        }

        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        (*cpu_task)->total_cpu_time_ms += TICKS_MS;

        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {

            char msg_concluido[64];
            snprintf(msg_concluido, sizeof(msg_concluido), "Burst Concluido (%u ms)", (*cpu_task)->time_ms);

            log_event("R3 [RUNNING -> DONE]",
                      current_time_ms,
                      (*cpu_task)->pid,
                      "RR",
                      (*cpu_task)->time_ms,
                      (*cpu_task)->ellapsed_time_ms,
                      msg_concluido);

            finish_burst(current_time_ms, cq, *cpu_task);
            *cpu_task = NULL;


        } else if ((current_time_ms - (*cpu_task)->slice_start_ms) >= QUANTUM_TIME_BURST) {
            if (rq->size == 0) {
                (*cpu_task)->slice_start_ms = current_time_ms;

                char msg_opt[64];
                snprintf(msg_opt, sizeof(msg_opt), "Fila vazia (Quantum %u ms renovado)", QUANTUM_TIME_BURST);

                log_event("R3 [RR RUNNING STILL]",
                          current_time_ms,
                          (*cpu_task)->pid,
                          "RR",
                          (*cpu_task)->time_ms,
                          (*cpu_task)->ellapsed_time_ms,
                          msg_opt);

            } else {

                char msg_preempt[64];
                snprintf(msg_preempt, sizeof(msg_preempt), "Quantum atingido (%u ms) | Executado: %u ms",
                         QUANTUM_TIME_BURST, (*cpu_task)->ellapsed_time_ms);

                log_event("R3 [RUNNING -> READY (PREEMPT)]",
                          current_time_ms,
                          (*cpu_task)->pid,
                          "RR",
                          (*cpu_task)->time_ms,
                          (*cpu_task)->ellapsed_time_ms,
                          msg_preempt);

                (*cpu_task)->status = TASK_RUNNING;
                enqueue_pcb(rq, *cpu_task);
                *cpu_task = NULL;
            }
        }
    }

    if (*cpu_task == NULL) {
        *cpu_task = dequeue_rr(rq);
        if (*cpu_task != NULL) {

            (*cpu_task)->context_switches++;

            (*cpu_task)->slice_start_ms = current_time_ms;
            (*cpu_task)->status = TASK_RUNNING;
            return 1;
        }
    }
    return 0;
}

int scheduler_mlfq(uint32_t current_time_ms, queue_t *rq, queue_t *cq, pcb_t **cpu_task, queue_t *finished_queue) {
    // 1. Priority Boost
    if ((current_time_ms - last_boost_time_ms) >= BOOST_INTERVAL_MS && (*cpu_task != NULL || rq->size > 0)) {

        boost_all_priorities(rq);

        int32_t active_pid = 0;
        uint32_t active_time = 0;
        uint32_t active_ellapsed = 0;

        if (*cpu_task != NULL) {
            (*cpu_task)->priority = 0;
            (*cpu_task)->priority_time_ms = 0;
            (*cpu_task)->slice_start_ms = current_time_ms;

            active_pid = (*cpu_task)->pid;
            active_time = (*cpu_task)->time_ms;
            active_ellapsed = (*cpu_task)->ellapsed_time_ms;
        }

        last_boost_time_ms = current_time_ms;


        log_event("R3 [MLFQ PRIORITY BOOST]",
                  current_time_ms,
                  active_pid,
                  "MLFQ",
                  active_time,
                  active_ellapsed,
                  "Todas as tarefas promovidas para Prio 0!");
    }

    // 2. Processar tarefa na CPU
    if (*cpu_task != NULL) {
        if ((*cpu_task)->first_run_time_ms == 0) {
            (*cpu_task)->first_run_time_ms = current_time_ms;
        }

        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        (*cpu_task)->priority_time_ms += TICKS_MS;
        (*cpu_task)->total_cpu_time_ms += TICKS_MS;

        char algo_str[16];
        snprintf(algo_str, sizeof(algo_str), "MLFQ(P%d)", (*cpu_task)->priority);

        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {

            char msg_concluido[64];
            snprintf(msg_concluido, sizeof(msg_concluido), "Burst Concluido (%u ms)", (*cpu_task)->time_ms);

            log_event("R3 [RUNNING -> DONE]",
                      current_time_ms,
                      (*cpu_task)->pid,
                      algo_str,
                      (*cpu_task)->time_ms,
                      (*cpu_task)->ellapsed_time_ms,
                      msg_concluido);

            finish_burst(current_time_ms, cq, *cpu_task);
            *cpu_task = NULL;

        } else if ((*cpu_task)->priority_time_ms >= MLFQ_TIME_BURST) {
            if ((*cpu_task)->priority < MAX_PRIORITY) {
                (*cpu_task)->priority++;
            }
            (*cpu_task)->priority_time_ms = 0;

            char msg_demotion[64];
            snprintf(msg_demotion, sizeof(msg_demotion), "Rebaixado para Prio %d",
                     (*cpu_task)->priority);

            log_event("R3 [RUNNING -> READY (DEMOTION)]",
                      current_time_ms,
                      (*cpu_task)->pid,
                      algo_str,
                      (*cpu_task)->time_ms,
                      (*cpu_task)->ellapsed_time_ms,
                      msg_demotion);

            (*cpu_task)->status = TASK_RUNNING;
            enqueue_pcb(rq, *cpu_task);
            *cpu_task = NULL;
        }
    }

    // 3. CPU vazia: retirar da Ready Queue
    if (*cpu_task == NULL) {
        *cpu_task = dequeue_mlfq(rq);

        if (*cpu_task != NULL) {

            (*cpu_task)->context_switches++;
            (*cpu_task)->slice_start_ms = current_time_ms;
            (*cpu_task)->status = TASK_RUNNING;

            char algo_str[16];
            snprintf(algo_str, sizeof(algo_str), "MLFQ(P%d)", (*cpu_task)->priority);

            log_event("R3 [READY -> RUNNING]",
                      current_time_ms,
                      (*cpu_task)->pid,
                      algo_str,
                      (*cpu_task)->time_ms,
                      (*cpu_task)->ellapsed_time_ms,
                      NULL);

            return 1;
        }
    }
    return 0;
}