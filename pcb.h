//
// Created by Martijn Kuipers on 20/10/2025.
//

#ifndef PCB_H
#define PCB_H
#include <stdint.h>

#include "msg.h"

typedef enum  {
    TASK_COMMAND = 0,   // Task has connected and is waiting for instructions
    TASK_BLOCKED,       // Task is blocked (waiting/IO wait)
    TASK_RUNNING,       // Task is in the ready queue or currently running
    TASK_STOPPED,       // Task has finished execution (sent DONE), waiting for more messages
    TASK_TERMINATED,    // Task has been terminated and will be removed
} task_status_en;

// Define the Process Control Block (PCB) structure
typedef struct pcb_st{
    int32_t pid;                   // Process ID
    task_status_en status;         // Current status of the task defined by the pcb
    uint32_t time_ms;              // Time requested by application in milliseconds
    uint32_t ellapsed_time_ms;     // Time ellapsed since start in milliseconds FROM THE CURRENT BURST
    uint32_t total_cpu_time_ms;    //  NOVO Acumulador TOTAL de tempo de CPU
    uint32_t slice_start_ms;       // Time when the current time slice started
    uint32_t sockfd;               // Socket file descriptor for communication with the application
    uint32_t last_update_time_ms;  // Last time the PCB was updated
    uint8_t priority;              // Prioridade do processo atual
    uint32_t priority_time_ms;     // Tempo gasto dentro da mesma prioridade.

    //atributos para tirar métricas

    uint32_t arrival_time_ms;      // Hora em que a tarefa chegou (NEW)
    uint32_t first_run_time_ms;    // Hora em que entrou no CPU pela 1ª vez (-1 se ainda não correu)
    uint32_t completion_time_ms;   // Hora em que terminou (DONE)
    uint32_t total_waiting_time_ms;// Tempo acumulado na ready queue
    uint32_t context_switches;     // Contador de quantas vezes entrou/saiu da ready queue

} pcb_t;

#endif //PCB_H
