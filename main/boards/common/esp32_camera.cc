#include "sdkconfig.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <img_converters.h>
#include <mbedtls/base64.h>
#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "board.h"
#include "display.h"
#include "esp32_camera.h"
#include "esp_timer.h"
#include "jpg/image_to_jpeg.h"
#include "lvgl_display.h"
#include "mcp_server.h"
#include "system_info.h"

#define TAG "Esp32Camera"

// DOC INÍCIO — Cliente HTTP direto para Gemini Vision.
// Converte o quadro RGB565/JPEG em JPEG, codifica em Base64 e lê a resposta JSON.
// A chave vem de menuconfig (CONFIG_DOC_GEMINI_API_KEY), nunca de uma string fixa.
static esp_err_t GeminiHttpEventHandler(esp_http_client_event_t* event) {
    if (event == nullptr || event->user_data == nullptr) {
        return ESP_OK;
    }

    auto* response = static_cast<std::string*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != nullptr &&
        event->data_len > 0) {
        response->append(static_cast<const char*>(event->data),
                         static_cast<size_t>(event->data_len));
    }
    return ESP_OK;
}

static std::expected<std::string, std::string> ExplainFrameWithGemini(
    camera_fb_t* frame, const std::string& question, const std::string& api_key) {
    if (frame == nullptr || api_key.empty()) {
        return std::unexpected("Quadro da câmera ou chave Gemini ausente");
    }

    uint8_t* jpeg_data = nullptr;
    size_t jpeg_size = 0;
    bool converted = false;

    if (frame->format == PIXFORMAT_JPEG) {
        jpeg_data = frame->buf;
        jpeg_size = frame->len;
    } else {
        converted = fmt2jpg(frame->buf, frame->len, frame->width, frame->height,
                            frame->format, 75, &jpeg_data, &jpeg_size);
        if (!converted || jpeg_data == nullptr || jpeg_size == 0) {
            return std::unexpected("Não foi possível converter a imagem para JPEG");
        }
    }

    // DOC — O Base64 ocupa mais memória; use o tamanho calculado pela biblioteca.
    size_t base64_size = 0;
    int base64_status = mbedtls_base64_encode(
        nullptr, 0, &base64_size, jpeg_data, jpeg_size);
    if (base64_status != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL &&
        base64_status != 0) {
        if (converted) free(jpeg_data);
        return std::unexpected("Falha ao calcular Base64 da imagem");
    }

    std::vector<unsigned char> base64_buffer(base64_size + 1);
    base64_status = mbedtls_base64_encode(
        base64_buffer.data(), base64_buffer.size(), &base64_size,
        jpeg_data, jpeg_size);
    if (converted) free(jpeg_data);
    if (base64_status != 0) {
        return std::unexpected("Falha ao codificar imagem em Base64");
    }

    // DOC — Monta o pedido Gemini com uma instrução curta e a imagem capturada.
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return std::unexpected("Memória insuficiente para montar o pedido Gemini");
    }
    cJSON* contents = cJSON_AddArrayToObject(root, "contents");
    cJSON* content_item = cJSON_CreateObject();
    cJSON* parts = cJSON_AddArrayToObject(content_item, "parts");
    if (contents == nullptr || content_item == nullptr || parts == nullptr) {
        cJSON_Delete(root);
        cJSON_Delete(content_item);
        return std::unexpected("Falha ao montar os campos do pedido Gemini");
    }
    cJSON* text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "text", question.c_str());
    cJSON_AddItemToArray(parts, text_part);

    cJSON* image_part = cJSON_CreateObject();
    cJSON* inline_data = cJSON_AddObjectToObject(image_part, "inline_data");
    cJSON_AddStringToObject(inline_data, "mime_type", "image/jpeg");
    cJSON_AddStringToObject(inline_data, "data",
                            reinterpret_cast<const char*>(base64_buffer.data()));
    cJSON_AddItemToArray(parts, image_part);
    cJSON_AddItemToArray(contents, content_item);

    char* request_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (request_body == nullptr) {
        return std::unexpected("Falha ao montar pedido Gemini");
    }

    std::string response_body;
    esp_http_client_config_t config = {};
    config.url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-latest:generateContent";
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 30000;
    config.event_handler = GeminiHttpEventHandler;
    config.user_data = &response_body;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        free(request_body);
        return std::unexpected("Não foi possível iniciar conexão HTTPS com Gemini");
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-goog-api-key", api_key.c_str());
    esp_http_client_set_post_field(client, request_body, strlen(request_body));
    esp_err_t request_status = esp_http_client_perform(client);
    int http_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(request_body);

    if (request_status != ESP_OK) {
        ESP_LOGE(TAG, "Gemini HTTPS request failed: %s", esp_err_to_name(request_status));
        return std::unexpected("Falha na conexão com Gemini");
    }
    if (http_status < 200 || http_status >= 300) {
        ESP_LOGE(TAG, "Gemini returned HTTP status %d", http_status);
        return std::unexpected("Gemini recusou o pedido (HTTP " +
                               std::to_string(http_status) + ")");
    }

    cJSON* response_json = cJSON_Parse(response_body.c_str());
    if (response_json == nullptr) {
        return std::unexpected("Resposta Gemini não é um JSON válido");
    }
    cJSON* candidates = cJSON_GetObjectItem(response_json, "candidates");
    cJSON* first_candidate = cJSON_IsArray(candidates) ? cJSON_GetArrayItem(candidates, 0) : nullptr;
    cJSON* response_content = first_candidate != nullptr
        ? cJSON_GetObjectItem(first_candidate, "content") : nullptr;
    cJSON* response_parts = response_content != nullptr
        ? cJSON_GetObjectItem(response_content, "parts") : nullptr;
    cJSON* first_part = cJSON_IsArray(response_parts)
        ? cJSON_GetArrayItem(response_parts, 0) : nullptr;
    cJSON* response_text = first_part != nullptr
        ? cJSON_GetObjectItem(first_part, "text") : nullptr;

    std::string answer;
    if (cJSON_IsString(response_text) && response_text->valuestring != nullptr) {
        answer = response_text->valuestring;
    }
    cJSON_Delete(response_json);

    if (answer.empty()) {
        return std::unexpected("Gemini não retornou uma descrição");
    }
    return answer;
}
// DOC FIM — Cliente HTTP direto para Gemini Vision.

#if CONFIG_XIAOZHI_CAMERA_MIRROR_CONFIGURED
#if CONFIG_XIAOZHI_CAMERA_HMIRROR
static constexpr bool kConfiguredHMirror = true;
#else
static constexpr bool kConfiguredHMirror = false;
#endif
#if CONFIG_XIAOZHI_CAMERA_VFLIP
static constexpr bool kConfiguredVFlip = true;
#else
static constexpr bool kConfiguredVFlip = false;
#endif
#endif

Esp32Camera::Esp32Camera(const camera_config_t& config) {
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed with error 0x%x", err);
        return;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        if (s->id.PID == GC0308_PID) {
            s->set_hmirror(s, 0);  // Control camera mirror: 1 for mirror, 0 for normal
        }
#if CONFIG_XIAOZHI_CAMERA_MIRROR_CONFIGURED
        s->set_hmirror(s, kConfiguredHMirror ? 1 : 0);
        s->set_vflip(s, kConfiguredVFlip ? 1 : 0);
#endif
        ESP_LOGI(TAG, "Camera initialized: format=%d", config.pixel_format);
    }

    streaming_on_ = true;
}

Esp32Camera::~Esp32Camera() {
    if (streaming_on_) {
        if (current_fb_) {
            esp_camera_fb_return(current_fb_);
            current_fb_ = nullptr;
        }
        if (encode_buf_) {
            heap_caps_free(encode_buf_);
            encode_buf_ = nullptr;
            encode_buf_size_ = 0;
        }
        esp_camera_deinit();
        streaming_on_ = false;
    }
}

void Esp32Camera::SetExplainUrl(const std::string& url, const std::string& token) {
    explain_url_ = url;
    explain_token_ = token;
}

bool Esp32Camera::Capture() {
    // DOC INÍCIO — Exclusão mútua durante a captura.
    // Evita que a tarefa da prévia substitua current_fb_ enquanto Explain() usa a foto.
    std::lock_guard<std::mutex> capture_lock(capture_mutex_);
    // DOC FIM — Exclusão mútua durante a captura.

    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }

    if (!streaming_on_) {
        return false;
    }

    // DOC INÍCIO — Captura de um único quadro por ciclo.
    // CAMERA_GRAB_LATEST já privilegia o quadro mais recente; capturar duas
    // vezes seguidas descartava trabalho e aumentava a carga da prévia.
    if (current_fb_) {
        esp_camera_fb_return(current_fb_);
        current_fb_ = nullptr;
    }
    current_fb_ = esp_camera_fb_get();
    if (!current_fb_) {
        ESP_LOGE(TAG, "Camera capture failed");
        return false;
    }
    // DOC FIM — Captura de um único quadro por ciclo.

    // Prepare encode buffer for RGB565 format (with optional byte swapping)
    if (current_fb_->format == PIXFORMAT_RGB565) {
        size_t pixel_count = current_fb_->width * current_fb_->height;
        size_t data_size = pixel_count * 2;

        // Allocate or reallocate encode buffer if needed
        if (encode_buf_size_ < data_size) {
            if (encode_buf_) {
                heap_caps_free(encode_buf_);
            }
            encode_buf_ =
                (uint8_t*)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (encode_buf_ == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate memory for encode buffer");
                encode_buf_size_ = 0;
                return false;
            }
            encode_buf_size_ = data_size;
        }

        // Copy data to encode buffer with optional byte swapping
        uint16_t* src = (uint16_t*)current_fb_->buf;
        uint16_t* dst = (uint16_t*)encode_buf_;
        if (swap_bytes_enabled_) {
            for (size_t i = 0; i < pixel_count; i++) {
                dst[i] = __builtin_bswap16(src[i]);
            }
        } else {
            memcpy(encode_buf_, current_fb_->buf, data_size);
        }

        // Allocate separate buffer for preview display
        uint8_t* preview_data =
            (uint8_t*)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (preview_data != nullptr) {
            memcpy(preview_data, encode_buf_, data_size);
            auto display = Board::GetInstance().GetDisplay();
            if (display != nullptr) {
                display->SetPreviewImage(std::make_unique<LvglAllocatedImage>(
                    preview_data, data_size, current_fb_->width, current_fb_->height,
                    current_fb_->width * 2, LV_COLOR_FORMAT_RGB565));
            } else {
                heap_caps_free(preview_data);
            }
        }
    } else if (current_fb_->format == PIXFORMAT_JPEG) {
        // JPEG format preview usually requires decoding, skip preview display for now, just log
        ESP_LOGW(TAG, "JPEG capture success, len=%zu, but not supported for preview",
                 current_fb_->len);
    }

    ESP_LOGI(TAG, "Captured frame: %dx%d, len=%zu, format=%d", current_fb_->width,
             current_fb_->height, current_fb_->len, current_fb_->format);

    return true;
}

bool Esp32Camera::SetHMirror(bool enabled) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }
    s->set_hmirror(s, enabled ? 1 : 0);
    return true;
}

bool Esp32Camera::SetVFlip(bool enabled) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }
    s->set_vflip(s, enabled ? 1 : 0);
    return true;
}

bool Esp32Camera::SetSwapBytes(bool enabled) {
    swap_bytes_enabled_ = enabled;
    return true;
}

std::expected<std::string, std::string> Esp32Camera::Explain(const std::string& question) {
    // DOC INÍCIO — Exclusão mútua durante a análise da imagem.
    // Mantém o quadro atual estável até terminar a conversão e o envio HTTP.
    std::lock_guard<std::mutex> capture_lock(capture_mutex_);
    // DOC FIM — Exclusão mútua durante a análise da imagem.

    if (current_fb_ == nullptr) {
        return std::unexpected("No camera frame captured");
    }

    // DOC INÍCIO — Priorizar Gemini quando a chave estiver configurada.
    // Sem chave, preserva o fluxo original de visão fornecido pelo servidor XiaoZhi.
#if defined(CONFIG_DOC_GEMINI_API_KEY)
    const std::string gemini_api_key = CONFIG_DOC_GEMINI_API_KEY;
    if (!gemini_api_key.empty()) {
        return ExplainFrameWithGemini(
            current_fb_, question.empty()
                ? "Identifique o objeto principal em uma palavra e descreva-o em uma linha."
                : question,
            gemini_api_key);
    }
#endif
    // DOC FIM — Priorizar Gemini quando a chave estiver configurada.

    // DOC — Só exige o endpoint remoto quando a chave Gemini não foi configurada.
    if (explain_url_.empty()) {
        return std::unexpected("Configure a chave Gemini ou o endpoint de visão XiaoZhi");
    }

    // Create local JPEG queue
    QueueHandle_t jpeg_queue = xQueueCreate(40, sizeof(JpegChunk));
    if (jpeg_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create JPEG queue");
        return std::unexpected("Failed to create JPEG queue");
    }

    // Start encoding thread
    encoder_thread_ = std::thread([this, jpeg_queue]() {
        int64_t start_time = esp_timer_get_time();
        uint16_t w = current_fb_->width;
        uint16_t h = current_fb_->height;
        v4l2_pix_fmt_t enc_fmt;
        switch (current_fb_->format) {
            case PIXFORMAT_RGB565:
                enc_fmt = V4L2_PIX_FMT_RGB565;
                break;
            case PIXFORMAT_YUV422:
                enc_fmt = V4L2_PIX_FMT_YUYV;  // YUV422 is actually YUYV format
                break;
            case PIXFORMAT_YUV420:
                enc_fmt = V4L2_PIX_FMT_YUV420;
                break;
            case PIXFORMAT_GRAYSCALE:
                enc_fmt = V4L2_PIX_FMT_GREY;
                break;
            case PIXFORMAT_JPEG:
                enc_fmt = V4L2_PIX_FMT_JPEG;
                break;
            case PIXFORMAT_RGB888:
                enc_fmt = V4L2_PIX_FMT_RGB24;
                break;
            default:
                ESP_LOGE(TAG, "Unsupported pixel format: %d", current_fb_->format);
                JpegChunk chunk = {.data = nullptr, .len = 0};
                xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
                return;
        }

        // Use encode buffer for RGB565, otherwise use original frame buffer
        uint8_t* jpeg_src_buf = current_fb_->buf;
        size_t jpeg_src_len = current_fb_->len;
        if (current_fb_->format == PIXFORMAT_RGB565 && encode_buf_ != nullptr) {
            jpeg_src_buf = encode_buf_;
            jpeg_src_len = encode_buf_size_;
        }

        bool ok = image_to_jpeg_cb(
            jpeg_src_buf, jpeg_src_len, w, h, enc_fmt, 80,
            [](void* arg, size_t index, const void* data, size_t len) -> size_t {
                auto jpeg_queue = static_cast<QueueHandle_t>(arg);
                JpegChunk chunk = {.data = nullptr, .len = len};
                if (index == 0 && data != nullptr && len > 0) {
                    chunk.data = (uint8_t*)heap_caps_aligned_alloc(
                        16, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (chunk.data == nullptr) {
                        ESP_LOGE(TAG, "Failed to allocate %zu bytes for JPEG chunk", len);
                        chunk.len = 0;
                    } else {
                        memcpy(chunk.data, data, len);
                    }
                } else {
                    chunk.len = 0;  // Sentinel or error
                }
                xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
                return len;
            },
            jpeg_queue);

        if (!ok) {
            JpegChunk chunk = {.data = nullptr, .len = 0};
            xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
        }
        int64_t end_time = esp_timer_get_time();
        ESP_LOGI(TAG, "JPEG encoding time: %ld ms", int((end_time - start_time) / 1000));
    });

    auto drain_jpeg_queue = [this, jpeg_queue]() {
        JpegChunk chunk;
        while (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) == pdPASS) {
            if (chunk.data != nullptr) {
                heap_caps_free(chunk.data);
            } else {
                break;
            }
        }
        encoder_thread_.join();
        vQueueDelete(jpeg_queue);
    };

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";

    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    if (auto opened = http->Open("POST", explain_url_); !opened) {
        ESP_LOGE(TAG, "Failed to connect to explain URL: %s", opened.error().ToString().c_str());
        drain_jpeg_queue();
        return std::unexpected("Failed to connect to explain URL");
    }

    auto write_or_fail = [&http](const char* data, size_t size) -> bool {
        if (auto written = http->Write(data, size); !written) {
            ESP_LOGE(TAG, "Failed to upload photo: %s", written.error().ToString().c_str());
            return true;
        }
        return false;
    };

    {
        std::string question_field;
        question_field += "--" + boundary + "\r\n";
        question_field += "Content-Disposition: form-data; name=\"question\"\r\n";
        question_field += "\r\n";
        question_field += question + "\r\n";
        if (write_or_fail(question_field.c_str(), question_field.size())) {
            drain_jpeg_queue();
            http->Close();
            return std::unexpected("Failed to upload photo");
        }
    }
    {
        std::string file_header;
        file_header += "--" + boundary + "\r\n";
        file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
        file_header += "Content-Type: image/jpeg\r\n";
        file_header += "\r\n";
        if (write_or_fail(file_header.c_str(), file_header.size())) {
            drain_jpeg_queue();
            http->Close();
            return std::unexpected("Failed to upload photo");
        }
    }

    size_t total_sent = 0;
    bool saw_terminator = false;
    while (true) {
        JpegChunk chunk;
        if (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to receive JPEG chunk");
            break;
        }
        if (chunk.data == nullptr) {
            saw_terminator = true;
            break;
        }
        const bool write_failed = write_or_fail((const char*)chunk.data, chunk.len);
        heap_caps_free(chunk.data);
        if (write_failed) {
            drain_jpeg_queue();
            http->Close();
            return std::unexpected("Failed to upload photo");
        }
        total_sent += chunk.len;
    }
    encoder_thread_.join();
    vQueueDelete(jpeg_queue);

    if (!saw_terminator || total_sent == 0) {
        ESP_LOGE(TAG, "JPEG encoder failed or produced empty output");
        return std::unexpected("Failed to encode image to JPEG");
    }

    {
        std::string multipart_footer;
        multipart_footer += "\r\n--" + boundary + "--\r\n";
        if (write_or_fail(multipart_footer.c_str(), multipart_footer.size())) {
            http->Close();
            return std::unexpected("Failed to upload photo");
        }
    }
    if (write_or_fail("", 0)) {
        http->Close();
        return std::unexpected("Failed to upload photo");
    }

    auto status_code = http->GetStatusCode();
    if (!status_code) {
        ESP_LOGE(TAG, "Failed to read HTTP status: %s", status_code.error().ToString().c_str());
        http->Close();
        return std::unexpected("Failed to upload photo");
    }
    if (*status_code != 200) {
        ESP_LOGE(TAG, "Failed to upload photo, status code: %d", *status_code);
        http->Close();
        return std::unexpected("Failed to upload photo");
    }

    std::string result = http->ReadAll();
    http->Close();

    size_t remain_stack_size = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(TAG,
             "Explain image size=%dx%d, compressed size=%d, remain stack size=%d, question=%s\n%s",
             current_fb_->width, current_fb_->height, (int)total_sent, (int)remain_stack_size,
             question.c_str(), result.c_str());
    return result;
}
