# Etapa 2 — contrato do driver PCA9685

<!-- DOC INÍCIO — Documento de contrato técnico; não inicializa hardware. -->

## Objetivo

Definir a interface do futuro driver PCA9685 antes de conectá-lo ao boot da placa. Esta etapa é deliberadamente independente da pinagem física.

## Restrições obrigatórias

- O driver recebe um `i2c_master_bus_handle_t` já criado pelo código da placa.
- Não escolhe GPIO SDA/SCL nem cria um barramento por conta própria.
- A construção do objeto não deve enviar comandos PWM nem mover servos.
- A inicialização deve ser uma chamada explícita e retornar erro ao chamador; evitar `ESP_ERROR_CHECK` para falhas recuperáveis do dispositivo.
- Antes de permitir movimento, deve configurar os quatro canais usados e definir o estado de parada de cada servo.
- O driver não controla a alimentação dos servos e não deve habilitar movimento se a alimentação externa ainda não tiver sido validada.
- O driver não será instanciado no caminho de inicialização da placa nesta etapa.

## API prevista

| Método | Responsabilidade |
|---|---|
| `InitializeSafe()` | Verificar comunicação e configurar o PCA9685 sem solicitar movimento. |
| `SetFrequencyHz(hz)` | Configurar a frequência PWM validada para os servos conectados. |
| `SetPulseUs(channel, pulse_us)` | Converter largura de pulso para contagens PWM e validar canal/limites. |
| `DisableChannel(channel)` | Colocar o canal em estado desabilitado conforme o registrador do PCA9685. |
| `DisableAll()` | Solicitar estado desabilitado para todos os canais controlados. |

## Decisões que permanecem abertas

1. GPIOs do barramento I²C e forma de inicialização do barramento na versão de firmware que funciona na placa.
2. Frequência e limites de pulso compatíveis com os modelos reais de servo.
3. Mapeamento dos quatro servos para os canais do PCA9685.
4. Confirmação da alimentação externa e da referência GND comum.
5. Confirmação do commit exato do firmware que contém a câmera funcional.

## Critério para avançar

Implementar o driver C++ somente depois de confirmar a API do ESP-IDF do checkout alvo e o método de registro de fontes em `main/CMakeLists.txt`. Em seguida, compilar com a configuração da placa DOC antes de integrar o driver ao boot. A ausência de GPIOs confirmados não impede escrever um driver que receba um barramento existente, mas impede habilitar a integração física.

## Registro de validação

- Revisão do contrato: concluída nesta etapa.
- Código de firmware alterado: não.
- Build executado: não.
- Teste físico: não.

<!-- DOC FIM — Documento de contrato técnico. -->
