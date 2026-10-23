
#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "display/lcd_display.h"
#include "boards/common/esp32_camera.h"

#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include "esp_lcd_st7735.h"

#define TAG "doc-ai-s3cam"

class DocAiS3Cam : public WifiBoard {
private:
    Button boot_button_;
    Display* display_ = nullptr;

    // DOC INÍCIO — Instância da câmera OV3660.
    // A classe comum do firmware administra a captura e os quadros da câmera.
    Camera* camera_ = nullptr;
    TaskHandle_t camera_preview_task_ = nullptr;
    std::atomic<bool> camera_preview_running_{false};
    std::atomic<bool> camera_preview_stop_requested_{false};
    // DOC FIM — Instância da câmera OV3660.

    // DOC INÍCIO — Inicialização da câmera OV3660.
    // Configura a pinagem da placa e usa RGB565 para permitir prévia no display.
    void InitializeCamera() {
        camera_config_t camera_config = {};
        camera_config.ledc_channel = LEDC_CHANNEL_0;
        camera_config.ledc_timer = LEDC_TIMER_0;
        camera_config.pin_d0 = CAMERA_PIN_D0;
        camera_config.pin_d1 = CAMERA_PIN_D1;
        camera_config.pin_d2 = CAMERA_PIN_D2;
        camera_config.pin_d3 = CAMERA_PIN_D3;
        camera_config.pin_d4 = CAMERA_PIN_D4;
        camera_config.pin_d5 = CAMERA_PIN_D5;
        camera_config.pin_d6 = CAMERA_PIN_D6;
        camera_config.pin_d7 = CAMERA_PIN_D7;
        camera_config.pin_xclk = CAMERA_PIN_XCLK;
        camera_config.pin_pclk = CAMERA_PIN_PCLK;
        camera_config.pin_vsync = CAMERA_PIN_VSYNC;
        camera_config.pin_href = CAMERA_PIN_HREF;
        camera_config.pin_sccb_sda = CAMERA_PIN_SIOD;
        camera_config.pin_sccb_scl = CAMERA_PIN_SIOC;
        camera_config.pin_pwdn = CAMERA_PIN_PWDN;
        camera_config.pin_reset = CAMERA_PIN_RESET;
        camera_config.xclk_freq_hz = XCLK_FREQ_HZ;
        camera_config.pixel_format = PIXFORMAT_RGB565;
        camera_config.frame_size = FRAMESIZE_QQVGA;
        camera_config.jpeg_quality = 12;
        camera_config.fb_count = 2;
        camera_config.fb_location = CAMERA_FB_IN_PSRAM;
        camera_config.grab_mode = CAMERA_GRAB_LATEST;

        camera_ = new Esp32Camera(camera_config);

        // DOC INÍCIO — Correção da orientação vertical da OV3660.
        // A imagem estava aparecendo de cabeça para baixo no display.
        // Usa o controle do sensor para inverter a imagem sem copiar pixels manualmente.
        if (camera_ != nullptr && !camera_->SetVFlip(true)) {
            ESP_LOGW(TAG, "Não foi possível ativar o flip vertical da OV3660");
        }
        // DOC FIM — Correção da orientação vertical da OV3660.

        ESP_LOGI(TAG, "Camera OV3660 configured: XCLK=%d Hz", XCLK_FREQ_HZ);
    }
    // DOC FIM — Inicialização da câmera OV3660.

    // DOC INÍCIO — Tarefa de prévia contínua.
    // Captura periodicamente quadros e entrega-os ao display existente.
    // Não realiza análise de imagem nesta tarefa; a análise será acionada por voz.
    static void CameraPreviewTask(void* arg) {
        auto* board = static_cast<DocAiS3Cam*>(arg);
        constexpr uint32_t CAMERA_PREVIEW_INTERVAL_MS = 250;

        while (!board->camera_preview_stop_requested_.load()) {
            // A tarefa só usa camera_ dentro deste ciclo. O fechamento espera
            // esta tarefa sair antes de destruir a instância da câmera.
            if (board->camera_ != nullptr) {
                board->camera_->Capture();
            }
            vTaskDelay(pdMS_TO_TICKS(CAMERA_PREVIEW_INTERVAL_MS));
        }
        // Sinaliza que nenhuma captura está em andamento antes de liberar a câmera.
        board->camera_preview_running_.store(false);
        vTaskDelete(nullptr);
    }

    void StartCameraPreviewTask() {
        if (camera_ == nullptr) {
            ESP_LOGE(TAG, "Camera preview task not started: camera is unavailable");
            return;
        }

        // DOC — Pilha e prioridade conservadoras; ajustar após medir no hardware.
        // DOC INÍCIO — Evita criar mais de uma tarefa de prévia.
        if (camera_preview_task_ != nullptr) {
            ESP_LOGI(TAG, "Camera preview task already running");
            return;
        }
        // DOC FIM — Evita criar mais de uma tarefa de prévia.

        camera_preview_stop_requested_.store(false);
        camera_preview_running_.store(true);
        BaseType_t result = xTaskCreate(
            CameraPreviewTask,
            "doc_cam_preview",
            4096,
            this,
            2,
            &camera_preview_task_);

        if (result != pdPASS) {
            camera_preview_running_.store(false);
            camera_preview_task_ = nullptr;
            ESP_LOGE(TAG, "Failed to create camera preview task");
        } else {
            ESP_LOGI(TAG, "Continuous camera preview task started");
        }
    }
    // DOC FIM — Tarefa de prévia contínua.


    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeDisplay() {
        spi_bus_config_t bus_config = {};
        bus_config.sclk_io_num = DISPLAY_SPI_CLK_PIN;
        bus_config.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        bus_config.miso_io_num = GPIO_NUM_NC;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz =
            DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);

        ESP_ERROR_CHECK(
            spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 20 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;

        ESP_ERROR_CHECK(
            esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;

        ESP_ERROR_CHECK(
            esp_lcd_new_panel_st7735(panel_io, &panel_config, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(
            esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(
            esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(
            esp_lcd_panel_mirror(
                panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(
            esp_lcd_panel_set_gap(
                panel, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new SpiLcdDisplay(
            panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            DISPLAY_SWAP_XY);

        ESP_LOGI(TAG, "ST7735 display initialized");
    }

public:
    DocAiS3Cam() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "DOC AI S3CAM board startup");
        InitializeButtons();
        InitializeDisplay();
        // DOC INÍCIO — Inicialização sem câmera.
        // O sensor e a tarefa de prévia só serão iniciados após o comando de voz.
        // InitializeCamera() e StartCameraPreviewTask() não devem ser chamados aqui.
        // DOC FIM — Inicialização sem câmera.

        ESP_LOGI(TAG,
                 "I2S mic BCLK=%d WS=%d DIN=%d; speaker BCLK=%d WS=%d DOUT=%d",
                 AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS,
                 AUDIO_I2S_MIC_GPIO_DIN,
                 AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                 AUDIO_I2S_SPK_GPIO_DOUT);
    }

    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    Display* GetDisplay() override {
        return display_;
    }

    // DOC INÍCIO — Disponibiliza a câmera somente se ela já foi aberta.
    Camera* GetCamera() override {
        return camera_;
    }

    // DOC INÍCIO — Inicializa a OV3660 apenas quando solicitada por voz.
    Camera* OpenCamera() override {
        if (camera_ == nullptr) {
            ESP_LOGI(TAG, "Comando de voz recebido: inicializando câmera OV3660");
            InitializeCamera();
        }
        if (camera_ != nullptr && camera_preview_task_ == nullptr) {
            StartCameraPreviewTask();
        }
        return camera_;
    }
    // DOC FIM — Inicializa a OV3660 apenas quando solicitada por voz.

    // DOC INÍCIO — Encerramento sincronizado da câmera e da prévia.
    // Aguarda a captura atual terminar, limpa a prévia e só então libera a câmera.
    bool CloseCamera() override {
        if (camera_ == nullptr) {
            if (display_ != nullptr) display_->SetPreviewImage(nullptr);
            return true;
        }
        if (camera_preview_task_ != nullptr) {
            camera_preview_stop_requested_.store(true);
            while (camera_preview_running_.load()) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            // O handle só é limpo depois que a tarefa confirma que terminou.
            camera_preview_task_ = nullptr;
        }
        if (display_ != nullptr) display_->SetPreviewImage(nullptr);
        delete camera_;
        camera_ = nullptr;
        camera_preview_stop_requested_.store(false);
        ESP_LOGI(TAG, "Câmera fechada e recursos liberados");
        return true;
    }
    // DOC FIM — Encerramento sincronizado da câmera e da prévia.
    // DOC FIM — Disponibiliza a câmera somente se ela já foi aberta.
};

DECLARE_BOARD(DocAiS3Cam);
