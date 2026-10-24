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

### 4. Próxima implementação proposta

Quando a pinagem e o commit de firmware estiverem confirmados:

1. Criar um componente isolado para PCA9685 usando o driver I²C mestre disponível.
2. Manter o componente inativo por padrão até a inicialização I²C ser explicitamente habilitada.
3. Inicializar os quatro canais de servo com saídas em estado seguro, sem movimento na inicialização.
4. Integrar os sensores separadamente, validando cada dispositivo antes de habilitar movimento.
5. Reutilizar `espressif/led_strip` se a API instalada for compatível com a versão do projeto.
6. Executar build/CI e depois testar fisicamente; não confundir CI com validação de hardware.

### 5. Marcadores de documentação de código

Todo código novo para o DOC deve conter comentários em português com marcadores claros `DOC INÍCIO` e `DOC FIM`, explicando a finalidade do bloco e os pontos que podem ser alterados.

<!-- DOC FIM — Auditoria documental. -->
