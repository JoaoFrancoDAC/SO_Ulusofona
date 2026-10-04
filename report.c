//
// Created by JoaoFranco on 27/09/2026.
//
#include "report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>     // Para a função access()
#include <sys/stat.h>
#include <sys/types.h>

// Função auxiliar para converter string para minúsculas e limpar carateres inválidos
static void sanitize_string(char *dest, const char *src, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < max_len - 1; i++) {
        char c = src[i];
        if (isalnum((unsigned char)c)) {
            dest[j++] = tolower((unsigned char)c);
        } else if (c == '+' || c == '-' || c == '_') {
            dest[j++] = c;
        }
        // Espaços, barras e outros símbolos são ignorados para manter o nome limpo
    }
    dest[j] = '\0';
}

void generate_simulation_report(queue_t *completed_queue,
                                uint32_t total_simulation_time_ms,
                                const char *algorithm_name,
                                const char *scenario_info)
{
    if (!completed_queue || completed_queue->size == 0) {
        printf("\n[REPORT] Nenhuma tarefa concluída para gerar relatório.\n");
        return;
    }

    // Criar a pasta simulation_results no diretório pai caso não exista
    mkdir("../simulation_results", 0777);

    // Limpar o nome do algoritmo e do cenário para usar no nome do ficheiro
    char clean_algo[64] = "algo";
    char clean_scen[128] = "scen";

    if (algorithm_name) sanitize_string(clean_algo, algorithm_name, sizeof(clean_algo));
    if (scenario_info)  sanitize_string(clean_scen, scenario_info, sizeof(clean_scen));

    // Construir o caminho base: ../simulation_results/simulation_algo_scenario.txt
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "../simulation_results/simulation_%s_%s.txt", clean_algo, clean_scen);

    // Se o ficheiro já existir, adiciona o sufixo (1), (2), (3)...
    if (access(filepath, F_OK) == 0) {
        int counter = 1;
        while (1) {
            snprintf(filepath, sizeof(filepath), "../simulation_results/simulation_%s_%s(%d).txt", clean_algo, clean_scen, counter);
            if (access(filepath, F_OK) != 0) {
                break; // Encontrou um nome que ainda não existe!
            }
            counter++;
        }
    }

    // Abre o ficheiro gerado em modo de escrita ("w")
    FILE *file = fopen(filepath, "w");
    if (!file) {
        perror("[REPORT] Erro ao criar o ficheiro de relatório");
        return;
    }

    uint32_t total_turnaround = 0;
    uint32_t total_response = 0;
    uint32_t total_waiting = 0;
    uint32_t total_switches = 0;
    uint32_t total_cpu_time = 0;
    uint32_t count = completed_queue->size;

    // Obter data e hora atual do sistema
    time_t rawtime;
    struct tm *timeinfo;
    char time_buffer[80];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    #define PRINT_BOTH(...) do { printf(__VA_ARGS__); if(file) fprintf(file, __VA_ARGS__); } while(0)

    PRINT_BOTH("\n");
    PRINT_BOTH("=================================================================================================================\n");
    PRINT_BOTH("                                       RELATÓRIO DE DESEMPENHO DO ESCALONADOR                                    \n");
    PRINT_BOTH("=================================================================================================================\n");
    PRINT_BOTH("Data/Hora da Simulação:     %s\n", time_buffer);
    PRINT_BOTH("Algoritmo de Escalonamento: %s\n", algorithm_name ? algorithm_name : "N/A");
    PRINT_BOTH("Cenário/Entradas Usadas:    %s\n", scenario_info ? scenario_info : "N/A");
    PRINT_BOTH("Tempo Total Simulado:       %u ms | Total de Tarefas Processadas: %u\n\n", total_simulation_time_ms, count);

    // Cabeçalho da tabela estendido para incluir Tempo de CPU e a Razão CPU/Turnaround
    PRINT_BOTH("%-5s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-11s\n",
               "PID", "Chegada", "1a Exec", "Fim", "CPU Time", "Turnaround", "Response T.", "Wait Time", "CPU/Turn %", "Context Sw.");
    PRINT_BOTH("------+------------+------------+------------+------------+------------+------------+------------+------------+------------\n");

    queue_elem_t *curr = completed_queue->head;
    while (curr != NULL) {
        pcb_t *p = curr->pcb;

        uint32_t turnaround = (p->completion_time_ms >= p->arrival_time_ms) ?
                              (p->completion_time_ms - p->arrival_time_ms) : 0;
        uint32_t response = (p->first_run_time_ms >= p->arrival_time_ms) ?
                            (p->first_run_time_ms - p->arrival_time_ms) : 0;

        uint32_t cpu_time = p->total_cpu_time_ms;

        uint32_t waiting = (turnaround >= cpu_time) ?
                           (turnaround - cpu_time) : 0;

        double cpu_turnaround_ratio = (turnaround > 0) ?
                                      (((double)cpu_time / turnaround) * 100.0) : 0.0;

        total_turnaround += turnaround;
        total_response += response;
        total_waiting += waiting;
        total_switches += p->context_switches;
        total_cpu_time += p->total_cpu_time_ms;

        PRINT_BOTH("%-5d | %7u ms | %7u ms | %7u ms | %7u ms | %7u ms | %7u ms | %7u ms | %8.1f %% | %10u\n",
                   p->pid,
                   p->arrival_time_ms,
                   p->first_run_time_ms,
                   p->completion_time_ms,
                   cpu_time,
                   turnaround,
                   response,
                   waiting,
                   cpu_turnaround_ratio,
                   p->context_switches);

        curr = curr->next;
    }

    double avg_turnaround = (double)total_turnaround / count;
    double avg_response = (double)total_response / count;
    double avg_waiting = (double)total_waiting / count;
    double avg_switches = (double)total_switches / count;
    double avg_cpu_ratio = (total_turnaround > 0) ?
                           (((double)total_cpu_time / total_turnaround) * 100.0) : 0.0;
    double cpu_utilization = (total_simulation_time_ms > 0) ?
                             (((double)total_cpu_time / total_simulation_time_ms) * 100.0) : 0.0;
    double throughput_sec = (total_simulation_time_ms > 0) ?
                            (((double)count / total_simulation_time_ms) * 1000.0) : 0.0;

    PRINT_BOTH("=================================================================================================================\n");
    PRINT_BOTH("                                       ESTATÍSTICAS GERAIS                                                       \n");
    PRINT_BOTH("=================================================================================================================\n");
    PRINT_BOTH(" * Tempo Médio de Execução (Turnaround Time): %8.2f ms\n", avg_turnaround);
    PRINT_BOTH(" * Tempo Médio de Resposta (Response Time):   %8.2f ms\n", avg_response);
    PRINT_BOTH(" * Tempo Médio de Espera (Waiting Time):      %8.2f ms\n", avg_waiting);
    PRINT_BOTH(" * Eficiência Média da Tarefa (CPU/Turnaround): %8.2f %%\n", avg_cpu_ratio);
    PRINT_BOTH(" * Média de Trocas de Contexto por Tarefa:    %8.2f\n", avg_switches);
    PRINT_BOTH(" * Taxa de Utilização da CPU:                 %8.2f %%\n", cpu_utilization);
    PRINT_BOTH(" * Vazão do Sistema (Throughput):             %8.2f tarefas/segundo\n", throughput_sec);
    PRINT_BOTH("=================================================================================================================\n\n");

    #undef PRINT_BOTH

    if (file) {
        fclose(file);

        // Obter o diretório de trabalho atual do processo (caminho absoluto)
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("[REPORT] Relatório guardado com sucesso em '%s/%s'\n", cwd, filepath);
        } else {
            printf("[REPORT] Relatório guardado com sucesso em '%s'\n", filepath);
        }
    }
}