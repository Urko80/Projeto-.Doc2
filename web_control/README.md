# .DOC Control Center — protótipo web

## Estado atual

Esta pasta contém uma interface web responsiva para o Projeto .DOC. A versão atual é **somente uma prévia local**: os controles alteram a tela, mas não enviam comandos ao ESP32 nem movimentam os servos.

## Arquivo

- `index.html`: interface, estilos e lógica de demonstração, comentados em português com marcadores `DOC INÍCIO` e `DOC FIM`.

## Canais e calibração registrados

| Canal PCA9685 | Servo | Referência registrada |
|---:|---|---|
| 0 | SG90 — perna 1 | Centro de teste 1500 µs; curso mecânico pendente |
| 1 | MG90S — roda 1 | Horário 1665 µs; neutro 1705 µs; anti-horário 1750 µs |
| 2 | MG90S — roda 2 | Horário 1665 µs; neutro 1705 µs; anti-horário 1755 µs |
| 3 | SG90 — perna 2 | Centro inicial proposto 1500 µs; validar na montagem |

Os valores dos MG90S foram observados nos testes deste projeto. Não copiar automaticamente esses pulsos para outros servos. Os extremos mecânicos dos SG90 ainda não estão confirmados.

## Arquitetura pretendida

1. O navegador abre a interface servida pelo ESP32-S3-CAM via Wi-Fi.
2. A página envia comandos HTTP para endpoints locais do firmware.
3. O firmware valida os parâmetros e controla o PCA9685 por I²C.
4. O firmware rejeita valores fora dos limites configurados e fornece um comando de parada.

### API proposta — ainda não implementada

- `GET /api/status`: estado da conexão, modo de operação e canais disponíveis.
- `POST /api/servo`: ajustar um canal angular individual, recebendo `channel` e `pulse_us`.
- `POST /api/legs`: ajustar canais 0 e 3, recebendo `left_us` e `right_us`.
- `POST /api/wheels`: comando de direção e velocidade solicitada, convertido em pulsos com base no neutro calibrado de cada MG90S.
- `POST /api/stop`: solicitar parada de ambos os MG90S e movimento seguro dos SG90 conforme a política definida no firmware.
- `GET /api/calibration`: ler calibrações atuais.
- `POST /api/calibration`: gravar calibrações validadas de forma persistente.

Os endpoints acima são uma proposta de contrato; não estão implementados nesta etapa.

## Segurança e próximos passos

- Manter os testes de servo individual até validar a alimentação de 5 V.
- Começar com faixa conservadora para as pernas e ampliar somente após validar a mecânica.
- Não tratar o botão de parada da interface como parada de emergência física enquanto não houver confirmação de recebimento do comando.
- Adicionar timeout no firmware: se a comunicação cair, os motores de rotação contínua devem retornar ao neutro.
- Validar os dados recebidos, limitar a frequência de comandos e evitar acesso externo não autorizado.
- Implementar e testar o backend ESP-IDF separadamente; não gravar o firmware funcional até revisão e validação.

## Organização do código

O HTML contém comentários em português e marcadores `DOC INÍCIO` / `DOC FIM` para localizar as partes de estilo, pernas, rodas, calibração e eventos de interface.
