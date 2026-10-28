# Plano de integração do TTP223 — Projeto DOC

> Estado: planejamento; não conectar o módulo nem gravar firmware até validar o GPIO.

## Objetivo

Preparar a integração do sensor capacitivo TTP223 em uma alteração isolada, preservando o firmware PR #13 aprovado.

## Alimentação e sinais previstos

| Terminal TTP223 | Destino previsto | Estado |
|---|---|---|
| VCC | 3V3 da ESP32-S3 | Recomendado, pois a faixa informada para o módulo é 2,0–5,5 V DC; confirmar a identificação da placa do sensor |
| GND | GND comum | Previsto |
| I/O ou OUT | GPIO a confirmar | Pendente de validação física |

## GPIO candidato

- GPIO 48 foi listado como candidato no mapa de pinagem, mas a própria documentação alerta que algumas variantes da placa usam esse pino para o LED RGB integrado.
- A configuração atual de `config.h` mantém `BUILTIN_LED_GPIO` e `TOUCH_BUTTON_GPIO` como `GPIO_NUM_NC`.
- **Não atribuir GPIO 48 ao TTP223 até confirmar o circuito da variante física da ESP32-S3-CAM N16R8 e verificar que o LED RGB integrado não depende dele.**
- Não usar GPIO 19/20 para o TTP223: eles estão reservados no projeto para SDA/SCL do PCA9685, com alerta de conflito com USB nativo.

## PCA9685 — configuração existente a preservar

O arquivo `main/boards/doc-ai-s3cam/config.h` registra:
- SDA: GPIO 19
- SCL: GPIO 20
- Endereço I²C: `0x40`
- Frequência I²C: 100 kHz
- Canais de servos já registrados: CH0/CH3 para pernas e CH1/CH2 para rodas.

Essas definições pertencem à configuração existente e não devem ser alteradas nesta etapa.

## Procedimento de validação

1. Confirmar o pinout exato da placa física e o uso do GPIO 48 pelo LED RGB integrado.
2. Confirmar as inscrições VCC, GND e I/O no módulo TTP223.
3. Só depois de validar um GPIO livre, alterar `TOUCH_BUTTON_GPIO` nesta branch.
4. Compilar o firmware e revisar as diferenças antes de criar uma versão para teste físico.
5. Manter a PR #13 aprovada intacta; não gravar a placa até o usuário autorizar após revisar a compilação e a pinagem.

## Registro de alteração

- Esta branch documenta o plano e as verificações pendentes.
- Nenhuma atribuição de GPIO ou alteração de comportamento foi feita ainda.
