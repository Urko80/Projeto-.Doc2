# Auditoria de integração do firmware do DOC

<!-- DOC INÍCIO — Auditoria documental; não altera inicialização nem GPIOs. -->

## Resultado da revisão do repositório

Revisão feita na branch `main` disponível no momento da consulta. O firmware instalado na placa pode corresponder a outra branch/commit; a câmera funcional confirmada pelo usuário deve ser preservada.

### 1. Placa

- `main/boards/doc-ai-s3cam/config.h` declara os pinos de áudio, display e câmera.
- `main/boards/doc-ai-s3cam/doc_ai_s3cam.cc` inicializa o display ST7735 e configura o áudio via `NoAudioCodecSimplex`.
- O arquivo da placa não inicializa atualmente um barramento I²C para os novos módulos.
- A documentação da branch principal descreve o mapa de câmera como futuro, divergindo do firmware que o usuário testou. Identificar o commit real do firmware funcional antes de modificar a inicialização.

### 2. Recursos existentes no repositório

- `main/boards/common/i2c_device.h` e `i2c_device.cc` fornecem uma classe auxiliar `I2cDevice` baseada no driver I²C mestre do ESP-IDF.
- `main/idf_component.yml` inclui `espressif/led_strip` versão aproximada `~3.0.3`, candidata a controlar a WS2812B-8.
- Não foi localizado, nesta revisão documental, um driver específico para PCA9685 nem uma configuração de pinagem para ele.
- O projeto usa ESP-IDF moderno (manifesto indica IDF >= 6.0.1); novas implementações devem seguir as APIs e convenções existentes no checkout escolhido, não copiar exemplos de versões antigas sem conferir.

### 3. Decisões que ainda não devem ser codificadas

- Não atribuir SDA/SCL: GPIO 19/20 podem conflitar com USB nativo; a utilização da USB-UART não prova que USB nativo esteja desativado.
- Não atribuir GPIO 48 à WS2812B até verificar a ligação ao LED RGB integrado.
- Não criar inicialização automática dos motores antes de definir comportamento de falha e de testar cada servo com alimentação apropriada.
- Não trocar drivers ou sequências de inicialização da câmera/display/áudio.

### 4. Auditoria do CI do PR #5

Foi consultada a execução do workflow `Build Boards` associada ao commit `58e97bd88d1d72a8e3ca555b394623d2983771fe`.

- O job parou em `Test build tooling`; a seleção de variantes e os builds das placas foram ignorados.
- A suíte executou 125 testes e terminou com 3 falhas:
  1. `test_board_menu_is_sorted_and_matches_cmake`: ordem natural dos nomes de placas não corresponde à ordem esperada.
  2. `test_board_display_name_comes_from_kconfig_prompt`: nome com caracteres chineses foi lido com codificação corrompida (mojibake).
  3. `test_current_matrix_uniqueness_and_p4_variants`: a mesma divergência de codificação do nome de placa.
- O PR #5 altera apenas documentos em `docs/`, não os scripts de build nem os testes. Assim, as falhas observadas não apontam diretamente para uma regressão introduzida pelos arquivos deste PR; contudo, **não foi comprovado nesta auditoria que a branch `main` falhe da mesma forma**.
- Como os testes falharam antes da seleção de variantes, não existe resultado de compilação de firmware para este commit. Não declarar o CI aprovado nem presumir que o firmware compila.
- Próxima ação recomendada: reproduzir a suíte na base `main` para estabelecer o baseline. Em mudança separada, corrigir o texto de origem em `main/Kconfig.projbuild` e investigar a ordenação natural do menu; validar a lista resultante antes de alterar a ordem de opções apresentada ao usuário.

### 5. Próxima implementação proposta

Quando a pinagem e o commit de firmware estiverem confirmados:

1. Criar um componente isolado para PCA9685 usando o driver I²C mestre disponível.
2. Manter o componente inativo por padrão até a inicialização I²C ser explicitamente habilitada.
3. Configurar os canais sem solicitar movimento espontâneo e separar a parada neutra dos servos de rotação contínua do desligamento de PWM.
4. Integrar os sensores separadamente, validando cada dispositivo antes de habilitar movimento.
5. Reutilizar `espressif/led_strip` se a API instalada for compatível com a versão do projeto.
6. Corrigir/entender o CI e executar build da configuração da placa escolhida; depois testar fisicamente. Não confundir CI com validação de hardware.

### 6. Marcadores de documentação de código

Todo código novo para o DOC deve conter comentários em português com marcadores claros `DOC INÍCIO` e `DOC FIM`, explicando a finalidade do bloco e os pontos que podem ser alterados.

<!-- DOC FIM — Auditoria documental. -->
