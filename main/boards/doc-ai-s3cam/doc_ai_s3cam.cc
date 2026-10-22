
#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "display/lcd_display.h"
#include "esp32_camera.h"
#include <esp_camera.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/spi_master.h>
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
    // DOC — Instância da câmera; permanece viva durante toda a execução da placa.
    Camera* camera_ = nullptr;

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

    // DOC INÍCIO — Prévia contínua da câmera.
    // Atualiza a imagem da tela em intervalos regulares. O intervalo pode ser
    // alterado aqui; valores menores aumentam a fluidez e o uso de CPU/memória.
    static void CameraPreviewTask(void* parameter) {
        auto* camera = static_cast<Camera*>(parameter);
        while (true) {
            if (camera == nullptr || !camera->Capture()) {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            // SetPreviewImage reinicia o temporizador da prévia; atualizações
            // contínuas impedem que a imagem desapareça enquanto a câmera roda.
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }

    void InitializeCamera() {
        // DOC — Pinagem da OV3660 definida em config.h para facilitar alterações.
        camera_config_t camera_config = {};
        camera_config.pin_pwdn = CAMERA_PIN_PWDN;
        camera_config.pin_reset = CAMERA_PIN_RESET;
        camera_config.pin_xclk = CAMERA_PIN_XCLK;
        camera_config.pin_sccb_sda = CAMERA_PIN_SIOD;
        camera_config.pin_sccb_scl = CAMERA_PIN_SIOC;
        camera_config.pin_d0 = CAMERA_PIN_D0;
        camera_config.pin_d1 = CAMERA_PIN_D1;
        camera_config.pin_d2 = CAMERA_PIN_D2;
        camera_config.pin_d3 = CAMERA_PIN_D3;
        camera_config.pin_d4 = CAMERA_PIN_D4;
        camera_config.pin_d5 = CAMERA_PIN_D5;
        camera_config.pin_d6 = CAMERA_PIN_D6;
        camera_config.pin_d7 = CAMERA_PIN_D7;
        camera_config.pin_vsync = CAMERA_PIN_VSYNC;
        camera_config.pin_href = CAMERA_PIN_HREF;
        camera_config.pin_pclk = CAMERA_PIN_PCLK;
        camera_config.xclk_freq_hz = XCLK_FREQ_HZ;
        camera_config.ledc_timer = LEDC_TIMER_0;
        camera_config.ledc_channel = LEDC_CHANNEL_0;
        // RGB565 permite mostrar a prévia no LVGL sem decodificar JPEG.
        camera_config.pixel_format = PIXFORMAT_RGB565;
        camera_config.frame_size = FRAMESIZE_QQVGA; // 160x120: adequado à tela 128x160.
        camera_config.fb_count = 2;
        camera_config.fb_location = CAMERA_FB_IN_PSRAM;
        camera_config.grab_mode = CAMERA_GRAB_LATEST;

        camera_ = new Esp32Camera(camera_config);
        BaseType_t task_ok = xTaskCreate(
            CameraPreviewTask, "doc_cam_preview", 4096, camera_, 3, nullptr);
        if (task_ok != pdPASS) {
            ESP_LOGE(TAG, "Could not start camera preview task");
        } else {
            ESP_LOGI(TAG, "Camera preview task started (XCLK=%d Hz)", XCLK_FREQ_HZ);
        }
    }
    // DOC FIM — Prévia contínua da câmera.

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
        // DOC — Inicializar a câmera depois do display para que a prévia tenha destino.
        InitializeCamera();

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

    // DOC — Expõe a câmera ao MCP e ao acionamento por comando de voz.
    Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(DocAiS3Cam);
