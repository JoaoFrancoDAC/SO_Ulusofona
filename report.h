//
// Created by JoaoFranco on 27/09/2026.
//

#ifndef OSSIM_REPORT_H
#define OSSIM_REPORT_H

#include <stdint.h>
#include "queue.h"

/**
 * @brief Gera e imprime o relatório final da simulação na consola e salva num ficheiro .txt
 *
 * @param completed_queue Fila com as tarefas concluídas (cq)
 * @param total_simulation_time_ms Tempo total decorrido na simulação
 * @param algorithm_name Nome do algoritmo testado (ex: "FIFO", "SJF", "RR", "MLFQ")
 * @param scenario_info Informações ou caminho do ficheiro de cenário/entradas utilizado
 */
void generate_simulation_report(queue_t *completed_queue,
                                uint32_t total_simulation_time_ms,
                                const char *algorithm_name,
                                const char *scenario_info);

#endif // OSSIM_REPORT_H