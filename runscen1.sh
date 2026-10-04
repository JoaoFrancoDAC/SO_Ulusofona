#!/bin/bash

#Escolha o algorítmo la nos editconfigs

#./cmake-build-debug/application scenarios/1/A.csv &        # 1A
#./cmake-build-debug/application scenarios/1/B.csv &        # 1B
#./cmake-build-debug/application scenarios/1/C.csv &        # 1C

#./cmake-build-debug/application scenarios/2/A.csv &        # 2A
#./cmake-build-debug/application scenarios/2/B.csv &        # 2B
#./cmake-build-debug/application scenarios/2/C.csv &        # 2C
#./cmake-build-debug/application scenarios/2/D.csv &        # 2D
#./cmake-build-debug/application scenarios/2/E.csv &        # 2E
#./cmake-build-debug/application scenarios/2/F.csv &        # 2F

#./cmake-build-debug/application scenarios/3/A.csv &        # 3A
#./cmake-build-debug/application scenarios/3/B.csv &        # 3B
#./cmake-build-debug/application scenarios/3/C.csv &        # 3C

#./cmake-build-debug/application scenarios/4/A.csv &      # 4A
#./cmake-build-debug/application scenarios/4/B.csv &      # 4B
#./cmake-build-debug/application scenarios/4/C.csv &      # 4C

#./cmake-build-debug/application scenarios/5/HighCPU.csv &
#sleep 0.3


##TESTE UM
#./cmake-build-debug/application scenarios/5/HighCPU.csv &
#./cmake-build-debug/application scenarios/5/ManyIOS.csv


## TESTE DOIS
##./cmake-build-debug/application scenarios/5/HighCPU.csv &
##sleep 2
##for i in {1..24}; do
##   ./cmake-build-debug/application scenarios/5/CheatingMLFQ.csv &
##    sleep 1
##done

## Teste FINAL
#!/bin/bash

DIR="scenarios/testefinal"
APP="./cmake-build-debug/application"

# 1. Dispara a tarefa inicial obrigatoriamente
echo "-> Lançando tarefas iniciais: longa20.csv e media3de6.csv"
$APP "$DIR/longa20.csv" &
$APP "$DIR/media3de6.csv" &
sleep 0.5

# 2. Definição das quotas para cada ficheiro (pool de tarefas)
c_75ms=10
c_200ms=10
m_1de1=10
#m_3de6=1

# Tempo de término do ciclo (15 segundos a partir de agora)
END_TIME=$(($(date +%s) + 45))

echo "-> A iniciar o sorteio de tarefas durante 15 segundos..."

while [ $(date +%s) -lt $END_TIME ]; do
    # Constrói a lista apenas com os ficheiros que ainda têm cota > 0
    available=()
    [ $c_75ms -gt 0 ]  && available+=("curta75ms.csv")
    [ $c_200ms -gt 0 ] && available+=("curta200ms.csv")
    [ $m_1de1 -gt 0 ]  && available+=("media1de1.csv")
    #[ $m_3de6 -gt 0 ]  && available+=("media3de6.csv")

    # Se a lista estiver vazia (todas as cotas foram gastas), encerra o ciclo prematuramente
    if [ ${#available[@]} -eq 0 ]; then
        echo "-> Todas as cotas de tarefas foram esgotadas antes dos 45s."
        break
    fi

    # Sorteia um índice aleatório da lista de disponíveis
    idx=$(( RANDOM % ${#available[@]} ))
    chosen="${available[$idx]}"

    # Decrementa o contador do ficheiro sorteado e dispara a aplicação
    case "$chosen" in
        "curta75ms.csv")  ((c_75ms--)) ;;
        "curta200ms.csv") ((c_200ms--)) ;;
        "media1de1.csv")  ((m_1de1--)) ;;
        #"media3de6.csv")  ((m_3de6--)) ;;
    esac

    echo "-> Disparando: $chosen (Restantes no pool: 75ms:$c_75ms | 200ms:$c_200ms | 1de1:$m_1de1 )"
    $APP "$DIR/$chosen" &

    # Intervalo curto e aleatório entre disparos (entre ~0.1s e 0.5s)
    sleep 1.$(($RANDOM % 9))
done

echo "-> Aguardando a conclusão de todos os processos no simulador..."
wait
echo "=== Teste Final Concluído ==="
