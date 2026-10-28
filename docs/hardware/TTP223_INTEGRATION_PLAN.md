# Plano de integração do TTP223 — Projeto DOC

> Estado: planejamento bloqueado pela disponibilidade de GPIO; não conectar o módulo nem gravar firmware nesta etapa.

## Objetivo

Preparar a integração do sensor capacitivo TTP223 em uma alteração isolada, preservando o firmware PR #13 aprovado.

## Alimentação e sinais previstos

| Terminal TTP223 | Destino previsto | Estado |
|---|---|---|
| VCC | 3V3 da ESP32-S3 | Recomendado, pois a faixa informada para o módulo é 2,0–5,5 V DC; confirmar a identificação da placa do sensor |
| GND | GND comum | Previsto |
| I/O ou OUT | GPIO a confirmar | Bloqueado até identificar um pino disponível sem conflito |

## Disponibilidade de GPIO — resultado da verificação

- GPIO 48 **não deve ser usado para o TTP223**: a documentação de hardware da ESP32-S3-CAM N16R8 identifica esse pino como sinal do LED RGB WS2812 integrado.
- A configuração da placa no projeto mantém `BUILTIN_LED_GPIO` e `TOUCH_BUTTON_GPIO` como `GPIO_NUM_NC`, mas isso não significa que o pino físico esteja desconectado do LED integrado.
- GPIO 19/20 estão configurados como SDA/SCL do PCA9685. O próprio `config.h` alerta que são USB D-/D+ nativos e que o USB nativo não deve ser usado simultaneamente com o PCA9685 nesses pinos. Não alterar essa configuração funcional nesta tarefa.
- Os GPIOs restantes da tabela atual estão associados à câmera, display, áudio, botão BOOT ou sinais de inicialização. Não foi confirmado um GPIO livre e seguro para atribuição imediata.

## Decisão segura

**Não atribuir GPIO 48 nem outro GPIO por suposição.** A integração funcional fica pendente até decidir uma destas opções:
1. revisar fisicamente e documentar uma remarcação de GPIO que não interrompa câmera, display, áudio, PCA9685, inicialização ou LEDs; ou
2. avaliar um expansor de entrada digital compatível com a arquitetura existente, após definir a estratégia do barramento e verificar as limitações atuais do I²C.

Nenhuma opção foi implementada ou conectada ainda.

## PCA9685 — configuração existente a preservar

O arquivo `main/boards/doc-ai-s3cam/config.h` registra:
- SDA: GPIO 19
- SCL: GPIO 20
- Endereço I²C: `0x40`
- Frequência I²C: 100 kHz
- Canais de servos registrados: CH0/CH3 para pernas e CH1/CH2 para rodas.

Essas definições pertencem à configuração existente e não devem ser alteradas nesta etapa.

## Procedimento seguinte

1. Conferir a serigrafia e os conectores da variante física da placa.
2. Escolher uma estratégia de entrada que não conflite com os periféricos atuais.
3. Só então implementar a leitura do TTP223 nesta branch, com comentários `DOC INÍCIO` / `DOC FIM`.
4. Executar compilação e revisar as diferenças antes de preparar firmware para teste físico.
5. Manter a PR #13 aprovada intacta; não gravar a placa sem revisão e autorização do usuário.

## Registro de alteração

- Esta branch documenta o plano e o bloqueio de pinagem.
- Nenhuma atribuição de GPIO ou alteração de comportamento foi feita.
