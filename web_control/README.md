# .DOC Control Center — painel web experimental

## Estado atual

A interface passou a chamar a API HTTP experimental do firmware para consultar status, enviar pulsos aos SG90 das pernas (CH0/CH3) e solicitar parada dos MG90S das rodas. Ainda **não foi compilada nem testada com hardware real**.

## Como usar após compilar e gravar a branch de teste

1. Conecte o ESP32-S3-CAM à mesma rede Wi-Fi do computador/celular.
2. Consulte o IP atribuído à placa no roteador ou no log serial.
3. Abra `index.html` localmente no navegador.
4. No campo “Endereço da API”, informe `http://IP-DA-PLACA` e pressione **Testar conexão**.
5. Faça primeiro testes de pernas com o mecanismo livre e a alimentação dos servos verificada.

O navegador pode bloquear a conexão por política de rede local/CORS ou firewall. A API define cabeçalhos CORS para permitir uma página local; isso não substitui autenticação.

## Endpoints do firmware

- `GET /api/status`: consulta status e calibração corrente.
- `POST /api/servo`: `{"channel":0,"pulse_us":1500}` ou canal 3.
- `POST /api/legs`: `{"left_us":1500,"right_us":1500}`.
- `POST /api/wheels`: `{"left":"cw","right":"ccw"}`; cada lado aceita `cw`, `ccw` ou `stop`.
- `POST /api/stop`: tenta parar os dois motores contínuos.

## Mapa de canais e valores registrados

| Canal PCA9685 | Servo | Valores registrados |
|---:|---|---|
| 0 | SG90 — perna 1 | 1500 µs centro de teste; faixa inicial da API 1250–1750 µs |
| 1 | MG90S — roda 1 | horário 1665 µs; neutro 1705 µs; anti-horário 1750 µs |
| 2 | MG90S — roda 2 | horário 1665 µs; neutro 1705 µs; anti-horário 1755 µs |
| 3 | SG90 — perna 2 | 1500 µs centro; faixa inicial da API 1250–1750 µs |

Os valores dos motores contínuos são calibrações medidas para esta montagem. Não os aplique a outros servos sem calibrar.

## Limitações e segurança

- A API não tem autenticação: use apenas em uma rede local confiável; não encaminhe a porta 80 para a internet.
- O controle de movimento das rodas permanece bloqueado na interface até validar o sentido físico de cada motor com o robô suspenso.
- O watchdog do firmware tenta parar as rodas após 1,5 s sem novo comando de roda; não é uma parada de emergência certificada.
- A calibração não é gravada em NVS.
- GPIO19/GPIO20 são usados como SDA/SCL e também são USB D-/D+ do ESP32-S3. Não use USB nativo simultaneamente; use o conversor USB-UART externo.
- A interface e a API ainda precisam de compilação, revisão e teste controlado na placa. Não gravar na placa principal antes da revisão.
