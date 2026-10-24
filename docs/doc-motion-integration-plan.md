# Plano de integração de movimento do DOC

> **Status:** planejamento inicial; nenhum GPIO novo foi atribuído e nenhum controle de servo foi habilitado.
>
> **Objetivo:** preparar a integração do PCA9685, quatro servos, VL53L0X, MPU6050, TTP223 e WS2812B-8 sem comprometer câmera, display, microfone e áudio já validados fisicamente pelo usuário.

## 1. Regras para preservar a versão funcional

- Não modificar a branch `main` diretamente.
- Trabalhar nesta branch em alterações pequenas e revisáveis.
- Não alterar os GPIOs existentes de câmera, display e áudio sem evidência de conflito.
- Não afirmar que um build foi testado fisicamente. A validação física depende do usuário.
- Documentar cada novo bloco de código em português usando os marcadores `DOC INÍCIO` e `DOC FIM`.
- Manter os motores desabilitados durante a inicialização e em caso de falha dos componentes necessários.

## 2. Estado conhecido da placa

O arquivo `main/boards/doc-ai-s3cam/config.h` na branch `main` registra:

| Função | GPIOs registrados |
|---|---|
| Microfone I²S | BCLK/SCK 1, WS 2, DIN 3 |
| Amplificador MAX98357A | BCLK 41, LRCK 42, DOUT 47 |
| Display ST7735 | CLK 14, MOSI 21, CS 38, DC 39, RST 40 |
| Câmera OV3660 (mapa declarado) | XCLK 15, SIOD 4, SIOC 5, D0 11, D1 9, D2 8, D3 10, D4 12, D5 18, D6 17, D7 16, VSYNC 6, HREF 7, PCLK 13 |

**Atenção à diferença entre mapa declarado e firmware efetivamente testado:** o README e comentários de configuração da branch `main` não descrevem toda a integração de câmera que o usuário confirmou estar funcionando no firmware instalado. Antes de alterar a inicialização da placa ou gerar novo firmware, identificar a branch/commit exata que produziu a imagem atualmente instalada.

## 3. GPIOs que não devem ser assumidos como livres

- GPIO 19 e GPIO 20: associados ao USB nativo do ESP32-S3; podem ser avaliados para I²C somente após conferir o uso no firmware e o esquema da placa.
- GPIO 43 e GPIO 44: manter reservados para a UART usada na programação/depuração, conforme a pinagem do projeto.
- GPIO 48: não atribuir à WS2812B sem confirmar se está conectado ao LED RGB integrado.
- GPIOs ligados a câmera, display, áudio, memória/PSRAM e funções de boot: preservar.

Nenhum GPIO de SDA/SCL, LED ou toque está aprovado por este documento.

## 4. Arquitetura de software proposta

Separar o futuro código em módulos, depois de verificar as convenções de build e bibliotecas disponíveis no repositório:

- `doc_motion`: interface de comandos de movimento, estados e parada.
- `doc_pca9685`: inicialização I²C e saída PWM para quatro canais.
- `doc_sensors`: abstração para distância, IMU e toque.
- `doc_leds`: controle da barra WS2812B-8.

Esses nomes são uma proposta; os arquivos só devem ser criados quando a integração e as dependências estiverem definidas.

## 5. Arquitetura elétrica preliminar

- PCA9685, VL53L0X e MPU6050 podem compartilhar um barramento I²C se forem compatíveis em tensão, endereços e pull-ups.
- O PCA9685 gera sinais PWM; ele não fornece por si só a corrente de potência dos servos.
- Não alimentar os quatro servos pelo pino 3,3 V da ESP32.
- Durante o desenvolvimento inicial pela USB-UART, manter os servos desconectados ou sem alimentação de potência. A USB-UART é adequada para programação e depuração, mas não deve ser presumida suficiente para picos de corrente dos motores.
- A barra WS2812B normalmente usa alimentação de 5 V; verificar a placa real e considerar conversão de nível lógico no sinal de dados.
- Compartilhar GND entre lógica e alimentação dos servos quando necessário para a referência dos sinais, respeitando o projeto de potência final.
- Identificar carregador, proteção da célula e conversor boost antes de desenhar a alimentação por bateria.

## 6. Estados e comandos planejados

Comandos iniciais pretendidos:
- `DOC, pare`
- `DOC, avance`
- `DOC, recue`
- `DOC, vire à esquerda`
- `DOC, vire à direita`
- `DOC, explore`

O comando de movimento só deve ser aceito quando o driver estiver inicializado e os requisitos de segurança estiverem satisfeitos. Em falha do controlador ou de sensores necessários, manter o estado de parada. Os valores de velocidade, tempos e limites de obstáculo serão configuráveis e deverão ser validados com os motores reais.

O modo `explore` não deve depender apenas da imagem da câmera: usar a leitura de distância quando a prevenção de obstáculos for requisito.

## 7. Sequência de trabalho

1. Identificar a branch/commit do firmware que está instalado e preserva a câmera funcional.
2. Conferir dependências e convenções de build para I²C/PCA9685 e LEDs.
3. Resolver a pinagem física de SDA/SCL e sinais adicionais, sem conflito com os periféricos atuais.
4. Implementar o driver do PCA9685 com saídas desabilitadas por padrão.
5. Integrar e testar sensores individualmente.
6. Implementar comandos de movimento e parada segura.
7. Executar build/CI; depois, realizar testes físicos graduais com alimentação de servos apropriada.

## 8. Critérios mínimos de validação

- Firmware compila na configuração de placa escolhida.
- Câmera, display, microfone e áudio continuam funcionando após a integração.
- A inicialização não move os servos espontaneamente.
- Falhas de I²C ou de sensor colocam o controle em estado seguro.
- Cada servo pode ser testado individualmente antes de movimentos combinados.
- O modo de exploração para quando o sensor de distância detecta condição de obstáculo conforme limite configurado.
- A validação em hardware é registrada separadamente dos resultados de CI.
