/*
Funcionalidades adicionadas ao projeto:
Configuração de parâmetros pelo usuário (via botoeiras)
Massa da carga.
Altura inicial do avião.
Velocidade do avião.
Força do vento.
Direção do vento (esquerda/direita).
Número de tentativas disponíveis.
Tipo de pacote: peso (leve ou pesado) e formato (quadrado ou cruz).
Tela de resumo antes do lançamento
Exibe no display todos os parâmetros escolhidos pelo usuário:
Mostra tipo e formato do pacote.
Ajuste na simulação da queda da carga
A função carga() foi alterada para que o vento influencie a posição X da carga.
A gravidade depende do tipo de pacote: leve → queda mais lenta; pesado → queda mais rápida.
O formato do pacote muda sua representação no display (quadrado ou retangular).
Feedback interativo para o usuário
Após o lançamento, o display mostra mensagens:
“Acertou o alvo!” quando o pacote atinge a área.
“Errou o alvo!” quando não atinge.

Anotações de parâmetros e valores:
Direção: 0 = esquerda; 1 = direita
Formato: 0 = quadrado; 1 = cruz
Peso: 0 = leve; 1 = pesado

Altura: 0 a 70
Velocidade do avião: 0 a 10
Força do vento: 0 a 5
Direção do vento: 0 a 1
Quantidade de tentativas: 1 a 10
Peso: 0 a 1
Formato: 0 a 1

. → acessa membros de uma struct normal.
-> → acessa membros de uma struct através de um ponteiro.
*/

/* USER CODE BEGIN Header */
/**
******************************************************************************
* @file           : main.c
* @brief          : Main program body
******************************************************************************
*/
/* USER CODE END Header */
#include "main.h"
#include "st7735.h"
#include "stdio.h"
#include "string.h"
#include <stdlib.h>
#include <math.h>


typedef struct {
  int x0, y0;
  int alvo_x, alvo_y;
  int alvo_raio;
  int vento_forca;
  int vento_dir;
  int peso;
  int formato;
  uint32_t tempo_ms; //uint:inteiro sem sinal de 32 bits
  uint8_t acerto;
} Tentativa;

static Tentativa g_tentativas[3];
static uint8_t g_tentativa_idx = 0;


typedef struct {
  int forca;
  int direcao; // 0=esq,1=dir
} Vento;

typedef struct {
  int y;          // altura do avião (linha)
  int vx;         // velocidade horizontal do avião
  int px_w, px_h; // tamanho do avião
} Aviao;

typedef struct {
  int x, y;     // canto superior esquerdo da "área alvo" (y=75 fixo)
  int raio;     // para quadrado: 0; para cruz: 1
  int formato;  // 0=Quadrado, 1=Cruz
} Alvo;

typedef struct { //agrupa variaveis diferentes no msm pacote
  int peso;     // 0=leve, 1=pesado
  int formato;  // 0=Quadrado, 1=Cruz
} Pacote;

typedef struct {
  int massa;          // mantido para compatibilidade (não usado na física)
  int tentativas_cfg; // qtd que o usuário deseja (usaremos o valor, mas o resumo fecha a cada 3)
  Vento vento;
  Aviao aviao;
  Pacote pacote;
} Parametros;


void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);


void InicializarSimulacao(Parametros *par, Alvo *alvo); // * = ponteiro
void ConfigurarParametros(Parametros *par);
void ExecutarLancamento(Parametros *par, Alvo *alvo, uint32_t *tempo_ms, int *xfinal, int *yfinal);
uint8_t AvaliarTentativa(const Alvo *alvo, int ximpacto, int yimpacto);
void RegistrarTentativa(const Parametros *par, const Alvo *alvo, uint32_t tempo_ms, uint8_t acerto, int x0, int y0);
void ExibirResumoFinal(const Tentativa *reg, int n);


static void EsperaOK(void);
static void MostrarTitulo(const char *a, const char *b);
static void MostrarResumoParametros(const Parametros *par, int alvo_lado, const char *alvo_fmt_txt);
static void Option_Input(int *variable, const char *label, int minVal, int maxVal);


static void DesenharAviao(int x, int y, int w, int h, uint16_t color);
static void DesenharAlvo(const Alvo *alvo);
static void DesenharCruz(int cx, int y, int raio, uint16_t color);
static void LimpaCargaAnterior(int x, int y, int w, int h);


SPI_HandleTypeDef hspi1;

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_SPI1_Init();
  ST7735_Init();

  // tela inicial
  ST7735_FillScreen(BLACK);
  MostrarTitulo("ENTREGA", "DE CARGAS");
  HAL_Delay(1400);
  ST7735_FillScreen(BLACK);
  ST7735_WriteString(10, 10, "PRESSIONE PA12", Font_7x10, GREEN, BLACK);
  ST7735_WriteString(10, 22, "PARA CONTINUAR", Font_7x10, GREEN, BLACK);
  EsperaOK();

  // Estado local
  Parametros par;
  Alvo alvo;

  while (1)
  {
    // 1) Inicializa cenário (alvos, pacote, etc.)
    InicializarSimulacao(&par, &alvo);

    // 2) Usuário pode alterar parâmetros
    ConfigurarParametros(&par);

    // (mostra um resumo curto antes de iniciar)
    const char *fmt_txt = (alvo.formato==0) ? "Quadrado" : "Cruz";
    MostrarResumoParametros(&par, alvo.raio*2, fmt_txt);
    EsperaOK();
    ST7735_FillScreen(BLACK);

    // 3) Executar lançamento, obter tempo e posição final
    uint32_t tempo_ms = 0;
    int xfinal = 0, yfinal = 0;
    ExecutarLancamento(&par, &alvo, &tempo_ms, &xfinal, &yfinal);

    // 4) Avaliar tentativa
    uint8_t acerto = AvaliarTentativa(&alvo, xfinal, yfinal);

    // Mensagem de resultado
    ST7735_FillScreen(BLACK);
    if (acerto) {
      ST7735_WriteString(10, 40, "ACERTOU O ALVO!", Font_11x18, GREEN, BLACK);
    } else {
      ST7735_WriteString(20, 40, "ERROU O ALVO!", Font_11x18, RED, BLACK);
    }
    HAL_Delay(1500);

    // 5) Registrar tentativa
    int x0 = 1 + 15; // aviao parte de x=1 com largura 15 (pos inicial da carga)
    int y0 = par.aviao.y;
    RegistrarTentativa(&par, &alvo, tempo_ms, acerto, x0, y0);

    // 6) A cada 3 simulações, exibe resumo e zera índice
    if (g_tentativa_idx == 3) {
      ExibirResumoFinal(g_tentativas, 3);
      g_tentativa_idx = 0;
      ST7735_WriteString(10, 120, "PA12 P/ NOVO CICLO", Font_7x10, YELLOW, BLACK);
      EsperaOK();
    }
  }
}


void InicializarSimulacao(Parametros *par, Alvo *alvo)
{
  // Valores base
  par->massa = 5; //-> significa “acessar um membro de uma struct ou objeto através de um ponteiro”.
  par->tentativas_cfg = 3;

  // Avião
  par->aviao.y = 30;        // altura
  par->aviao.vx = 2;        // velocidade horizontal
  par->aviao.px_w = 15;
  par->aviao.px_h = 5;

  // Vento
  par->vento.forca = 1;
  par->vento.direcao = 0; // esquerda

  // Pacote: alterna peso e formato a cada simulação para gerar cenários diferentes
  par->pacote.peso = (g_tentativa_idx % 2);         // 0,1,0,1...
  par->pacote.formato = (g_tentativa_idx / 1) % 2;  // 0,1,0,1...

  // Alvo: tamanho muda a cada cenário
  // lado do quadrado: 4, 6, 8 ... mapeado por idx; para "cruz", usamos um "raio" equivalente
  int base = 4 + (g_tentativa_idx * 2); /*
  int base → declara uma variável inteira chamada base.
  g_tentativa_idx → é o índice da tentativa atual (0, 1, 2…).
  g_tentativa_idx * 2 → multiplica o índice por 2.
  4 + (...) → soma 4 ao resultado.*/
  alvo->raio = base / 2;                // "raio" = metade do lado
  alvo->formato = par->pacote.formato;  // atrelar formato do pacote ao tipo do alvo (cenário diferente)

  // posiciona alvo em X aleatório no chão (y=75) garantindo que caiba na tela
  int maxX = 160 - (alvo->raio*2) - 5;
  int minX = 112; // como no seu código original
  if (maxX < minX) maxX = minX;
  alvo->x = minX + (rand() % (maxX - minX + 1));
  alvo->y = 75;
}

void ConfigurarParametros(Parametros *par)
{
  ST7735_FillScreen(BLACK);
  Option_Input(&par->aviao.y,      "ALTURA",      10, 70);
  Option_Input(&par->aviao.vx,     "VEL AVIAO",   1,  10);
  Option_Input(&par->vento.forca,  "FORCA VENTO", 0,  5);
  Option_Input(&par->vento.direcao,"DIR VENTO(0/1)",0,1);
  Option_Input(&par->tentativas_cfg,"TENTATIVAS", 1,  10);

  // Peso e formato do pacote podem ser alterados também
  Option_Input(&par->pacote.peso,    "PESO(0L/1P)", 0, 1);
  Option_Input(&par->pacote.formato, "FORMATO(0Q/1+)", 0, 1);
}

void ExecutarLancamento(Parametros *par, Alvo *alvo, uint32_t *tempo_ms, int *xfinal, int *yfinal)
{
    int aviao_x = 1;
    const int W = par->aviao.px_w;
    const int H = par->aviao.px_h;
    const int gnd_y = 75;

    ST7735_FillScreen(BLACK);
    DesenharAlvo(alvo);

    int drop_feito = 0;
    int carga_x = 0, carga_y = 0;
    float vx = 0, vy = 0;   // velocidades da carga
    uint32_t t0 = 0;

    for (int i = 0; i < 166; i += par->aviao.vx) {
        // move avião
        DesenharAviao(aviao_x, par->aviao.y, W, H, BLACK);
        aviao_x += par->aviao.vx;
        DesenharAviao(aviao_x, par->aviao.y, W, H, WHITE);

        if (!drop_feito && aviao_x > 80) { // solta carga no meio da tela
            drop_feito = 1;
            carga_x = aviao_x + W;
            carga_y = par->aviao.y;
            vx = par->aviao.vx; // começa com a vel do avião
            vy = 0;
            t0 = HAL_GetTick();
        }

        if (drop_feito) {
            // limpa anterior
            ST7735_FillRectangle((int)carga_x, (int)carga_y, 3, 3, BLACK);

            // aplica gravidade (peso altera intensidade)
            float g = (par->pacote.peso == 0) ? 0.3f : 0.8f;
            vy += g;

            // aplica vento (força altera aceleração horizontal)
            float vento = (par->vento.direcao == 0 ? -1 : 1) * (par->vento.forca * 0.2f);
            vx += vento;

            // atualiza posição
            carga_x += (int)vx;
            carga_y += (int)vy;

            // desenha carga
            ST7735_FillRectangle((int)carga_x, (int)carga_y, 3, 3, RED);

            if (carga_y >= gnd_y) {
                *xfinal = carga_x;
                *yfinal = gnd_y;
                *tempo_ms = HAL_GetTick() - t0;
                break;
            }
        }
        HAL_Delay(40);
    }
}


uint8_t AvaliarTentativa(const Alvo *alvo, int ximpacto, int yimpacto)
{
  // região útil do alvo na horizontal
  if (alvo->formato == 0) {
    // QUADRADO: x em [alvo->x, alvo->x + lado]
    int lado = alvo->raio * 2;
    if (yimpacto >= alvo->y && ximpacto >= alvo->x && ximpacto <= alvo->x + lado) return 1;
    return 0;
  } else {
    // CRUZ: acerta se cair em uma das "barras"
    // barra horizontal
    int cx = alvo->x + alvo->raio;
    int xh1 = alvo->x;
    int xh2 = alvo->x + alvo->raio*2;
    int yh = alvo->y; // linha do chão onde desenhamos a cruz
    int hit_h = (yimpacto >= yh-1 && yimpacto <= yh+1 && ximpacto >= xh1 && ximpacto <= xh2);

    // barra vertical (estreita em x ao redor de cx)
    int hit_v = (ximpacto >= cx-1 && ximpacto <= cx+1 && yimpacto >= yh - alvo->raio && yimpacto <= yh + alvo->raio);

    return (hit_h || hit_v) ? 1 : 0;
  }
}

void RegistrarTentativa(const Parametros *par, const Alvo *alvo, uint32_t tempo_ms, uint8_t acerto, int x0, int y0)
{
  if (g_tentativa_idx >= 3) return;
  Tentativa *t = &g_tentativas[g_tentativa_idx++];
  t->x0 = x0; //Quando temos um ponteiro para struct, usamos -> em vez de . para acessar os campos.
  t->y0 = y0; //t->y0 significa “o campo y0 da struct apontada por t”.
  t->alvo_x = alvo->x;
  t->alvo_y = alvo->y;
  t->alvo_raio = alvo->raio;
  t->vento_forca = par->vento.forca;
  t->vento_dir = par->vento.direcao;
  t->tempo_ms = tempo_ms;
  t->acerto = acerto;
  t->peso = par->pacote.peso;
  t->formato = par->pacote.formato;
}

void ExibirResumoFinal(const Tentativa *reg, int n)
{
  int acertos = 0;
  uint32_t soma = 0, melhor = 0xFFFFFFFF;

  for (int i=0;i<n;i++){
    acertos += reg[i].acerto ? 1 : 0; //reg é um ponteiro constante para Tentativa.
    soma += reg[i].tempo_ms;
    if (reg[i].tempo_ms < melhor) melhor = reg[i].tempo_ms;
  }
  uint32_t media = (n>0) ? (soma / (uint32_t)n) : 0;

  ST7735_FillScreen(BLACK);
  ST7735_WriteString(6, 2,  "RESUMO (3 SIMS)", Font_7x10, YELLOW, BLACK);

  char buf[32];
  sprintf(buf, "Acertos: %d/%d", acertos, n);
  ST7735_WriteString(6, 18, buf, Font_7x10, WHITE, BLACK);

  sprintf(buf, "Tempo medio: %lums", (unsigned long)media);
  ST7735_WriteString(6, 30, buf, Font_7x10, WHITE, BLACK);

  sprintf(buf, "Melhor tempo: %lums", (unsigned long)melhor);
  ST7735_WriteString(6, 42, buf, Font_7x10, WHITE, BLACK);

  // Lista compacta das 3 tentativas
  for (int i=0;i<n;i++){
    int y = 58 + i*18;
    sprintf(buf, "#%d %s  %lums", i+1, reg[i].acerto?"OK":"X", (unsigned long)reg[i].tempo_ms);
    ST7735_WriteString(6, y, buf, Font_7x10, reg[i].acerto?GREEN:RED, BLACK);
  }
}


static void MostrarTitulo(const char *a, const char *b){
  ST7735_WriteString(20, 10, a, Font_7x10, GREEN, BLACK);
  ST7735_WriteString(20, 22, b, Font_7x10, GREEN, BLACK);
}

static void EsperaOK(void){
  while (1){
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) == 0) break;
  }
  HAL_Delay(200);
}

static void MostrarResumoParametros(const Parametros *par, int alvo_lado, const char *alvo_fmt_txt){
  ST7735_FillScreen(BLACK);
  char b[34];

  sprintf(b,"Altura: %d", par->aviao.y);
  ST7735_WriteString(4, 4, b, Font_7x10, WHITE, BLACK);
  sprintf(b,"Vel: %d", par->aviao.vx);
  ST7735_WriteString(4, 16, b, Font_7x10, WHITE, BLACK);

  sprintf(b,"Vento: %d", par->vento.forca);
  ST7735_WriteString(4, 28, b, Font_7x10, WHITE, BLACK);
  ST7735_WriteString(4, 40, par->vento.direcao? "Dir":"Esq", Font_7x10, WHITE, BLACK);

  ST7735_WriteString(4, 52, par->pacote.peso? "Pesado":"Leve", Font_7x10, YELLOW, BLACK);
  ST7735_WriteString(4, 64, par->pacote.formato? "Formato: Cruz":"Formato: Quad", Font_7x10, YELLOW, BLACK);

  sprintf(b,"Alvo: %s %dpx", alvo_fmt_txt, alvo_lado);
  ST7735_WriteString(4, 78, b, Font_7x10, WHITE, BLACK);

  ST7735_WriteString(4, 110, "PA12 CONFIRMA", Font_7x10, GREEN, BLACK);
}

static void Option_Input(int *variable, const char *label, int minVal, int maxVal)
{
  int ok = 0;

  while (!ok) {
    ST7735_FillScreen(BLACK);
    char q[36];
    snprintf(q, sizeof(q), "QUAL/QNT %s", label); //sizeof é para não chutar quantos bytes cada coisa ocupa
    ST7735_WriteString(5, 10, q, Font_7x10, GREEN, BLACK);

    ST7735_WriteString(10, 35, "-", Font_11x18, GREEN, BLACK);
    ST7735_WriteString(145, 40, "+", Font_11x18, GREEN, BLACK);
    ST7735_WriteString(40, 70, "PA12 CONFIRMA", Font_7x10, GREEN, BLACK);

    char val[8];
    sprintf(val, "%2d", *variable); //"%2d" no C significa imprimir um inteiro (d) ocupando pelo menos 2 espaços, alinhado à direita.
    ST7735_WriteString(65, 35, val, Font_11x18, GREEN, BLACK);

    while (1){
      if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == 0) { // -
        if (*variable > minVal) (*variable)--;
        sprintf(val, "%2d", *variable);
        ST7735_WriteString(65, 35, val, Font_11x18, GREEN, BLACK);
        HAL_Delay(200);
      }
      if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11) == 0) { // +
        if (*variable < maxVal) (*variable)++;
        sprintf(val, "%2d", *variable);
        ST7735_WriteString(65, 35, val, Font_11x18, GREEN, BLACK);
        HAL_Delay(200);
      }
      if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) == 0) { // OK
        ok = 1;
        HAL_Delay(200);
        break;
      }
    }
  }
}


static void DesenharAviao(int x, int y, int w, int h, uint16_t color){
  ST7735_FillRectangle(x, y, w, h, color);
}

static void DesenharCruz(int cx, int y, int raio, uint16_t color){
  // barra horizontal no chão
  ST7735_FillRectangle(cx - raio, y, raio*2, 2, color);
  // barra vertical centrada
  ST7735_FillRectangle(cx - 1, y - raio, 2, raio*2, color);
}

static void DesenharAlvo(const Alvo *alvo){
  if (alvo->formato == 0){
    // quadrado
    ST7735_FillRectangle(alvo->x, alvo->y, alvo->raio*2, 2, WHITE); // borda no chão
    // colunas laterais pequenas (sinalização)
    ST7735_FillRectangle(alvo->x, alvo->y-2, 2, 2, WHITE);
    ST7735_FillRectangle(alvo->x + alvo->raio*2 - 2, alvo->y-2, 2, 2, WHITE);
  } else {
    // cruz
    int cx = alvo->x + alvo->raio;
    DesenharCruz(cx, alvo->y, alvo->raio, WHITE);
  }
}

static void LimpaCargaAnterior(int x, int y, int w, int h){
  if (x>=0 && y>=0) {
    ST7735_FillRectangle(x, y, w, h, BLACK);
  }
}


void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) { Error_Handler(); }
}

static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_1LINE;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) { Error_Handler(); }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, ST7735_DC_Pin|ST7735_RES_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = ST7735_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ST7735_CS_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ST7735_DC_Pin|ST7735_RES_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq(); //desliga todas as interrupções, para evitar que o erro seja agravado por outras partes do código.
  while (1) { }
}
