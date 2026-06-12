#include "stm32f4xx.h" // Thư viện lõi cho thanh ghi STM32F4
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* ========================================================================= */
/* BỘ ĐẾM THỜI GIAN (Thay thế cho HAL_GetTick)                               */
/* ========================================================================= */
volatile uint32_t my_tick = 0;;
// Hàm ngắt SysTick (Tự động nhảy vào mỗi 1ms)
//void SysTick_Handler(void) {
//    uwTick++;
//}
// Hàm lấy thời gian hiện tại
uint32_t GetTick(void) {
   return my_tick;
}
/* ========================================================================= */
/* CÁC BIẾN TOÀN CỤC                                                         */
/* ========================================================================= */
uint8_t rx_data;
uint8_t rx_buffer[20];
uint8_t rx_index = 0;
char tx_buffer[50];
uint32_t last_send_tick = 0;
// --- CÁC BIẾN CHO PID ---
int position = 2000;
float Kp = 0.03;
float Ki = 0;
float Kd = 1.5;
int basespeed = 40;
int P, I, D;
int errors[10] = {0,0,0,0,0,0,0,0,0,0};
int last_end = 0;
int lastError = 0;
int actives = 0;
int idle_counter = 0;
/* ========================================================================= */
/* KHAI BÁO HÀM                                                              */
/* ========================================================================= */
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_TIM2_Init(void);
void MX_USART1_UART_Init(void);
void motor_control(int speed_left, int speed_right);
void PID_control(void);
int errors_sum(int index, int abs_flag);
/* ========================================================================= */
/* CHƯƠNG TRÌNH CHÍNH                                                        */
/* ========================================================================= */
int main(void)
{
   // 1. Cấu hình xung nhịp hệ thống (72MHz)
   SystemClock_Config();
   // Cấu hình SysTick để đếm mỗi 1ms (Tần số CPU 72MHz)
   SysTick_Config(72000000 / 1000);
   // 2. Khởi tạo ngoại vi
   MX_GPIO_Init();
   MX_TIM2_Init();
   MX_USART1_UART_Init();
   // 3. Vòng lặp chính
   while (1)
   {
       PID_control();
   }
}
/* ========================================================================= */
/* ĐIỀU KHIỂN ĐỘNG CƠ (Timer 2 PWM & GPIO BSRR)                              */
/* ========================================================================= */
void motor_control(int speed_left, int speed_right)
{
   // ĐỘNG CƠ TRÁI (PB5, PB4 | PWM: PA0 -> TIM2_CH1)
   if (speed_left >= 0) {
       GPIOB->BSRR = (1 << 5) | (1 << (4 + 16));
       TIM2->CCR1 = speed_left * 10;
   } else {
       GPIOB->BSRR = (1 << (5 + 16)) | (1 << 4);
       TIM2->CCR1 = (-speed_left) * 10;
   }
   // ĐỘNG CƠ PHẢI (PB10, PA8 | PWM: PA1 -> TIM2_CH2)
   if (speed_right >= 0) {
       GPIOB->BSRR = (1 << 10);
       GPIOA->BSRR = (1 << (8 + 16));
       TIM2->CCR2 = speed_right * 10;
   } else {
       GPIOB->BSRR = (1 << (10 + 16));
       GPIOA->BSRR = (1 << 8);
       TIM2->CCR2 = (-speed_right) * 10;
   }
}
int errors_sum (int index, int abs_flag)
{
   int sum = 0;
   for (int i = 0; i < index; i++) {
       if (abs_flag == 1 && errors[i] < 0) sum += -errors[i];
       else sum += errors[i];
   }
   return sum;
}
/* ========================================================================= */
/* ĐIỀU KHIỂN CHÍNH & ĐỌC CẢM BIẾN                                           */
/* ========================================================================= */
void PID_control(void) {
   int pos = 0;
   int active = 0;
   // Đọc cảm biến (Thanh ghi IDR)
   uint8_t L2 = ((GPIOC->IDR & (1 << 7)) == 0) ? 1 : 0;
   uint8_t L1 = ((GPIOB->IDR & (1 << 6)) == 0) ? 1 : 0;
   uint8_t C  = ((GPIOA->IDR & (1 << 7)) == 0) ? 1 : 0;
   uint8_t R1 = ((GPIOA->IDR & (1 << 6)) == 0) ? 1 : 0;
   uint8_t R2 = ((GPIOB->IDR & (1 << 9)) == 0) ? 1 : 0;
   // Gửi UART định kỳ
   if (GetTick() - last_send_tick >= 100) {
       sprintf(tx_buffer, "Sen:%d%d%d%d%d\r\n", L2, L1, C, R1, R2);
       int len = strlen(tx_buffer);
       for (int i = 0; i < len; i++) {
           while (!(USART1->SR & (1 << 7))); // Chờ TXE
           USART1->DR = tx_buffer[i];
       }
       while (!(USART1->SR & (1 << 6)));     // Chờ TC
       last_send_tick = GetTick();
   }
   int SensorStates[] = {L2, L1, C, R1, R2};
   int increments[] = {1000, 1500, 2000, 2500, 3000};
   for (int i = 0; i < 5; i++) {
       if (SensorStates[i] == 1) {
           pos += increments[i];
           active++;
       }
   }
   actives = active;
   // XỬ LÝ DỪNG XE VÀ MẤT VẠCH
   if (active == 0) {
       idle_counter++;
       if (idle_counter > 40) {
           motor_control(0, 0);
           return;
       }
   } else {
       idle_counter = 0;
       position = pos / active;
   }
   // XỬ LÝ GÓC VUÔNG
   if(L2 == 1 && R2 == 0) {
       last_end = 2; motor_control(50, -50);
       // Delay cản trở bằng vòng lặp while với uwTick
       uint32_t start = GetTick();
       while(GetTick() - start < 100);
       return;
   }
   else if(R2 == 1 && L2 == 0) {
       last_end = 1; motor_control(-50, 50);
       uint32_t start = GetTick();
       while(GetTick() - start < 100);
       return;
   }
   // TÍNH TOÁN PID
   int error = position - 2000;
   P = error;
   I = errors_sum(5, 0);
   D = error - lastError;
   lastError = error;
   int motorspeed = P*Kp + I*Ki + D*Kd;
   // GIẢM TỐC VÀO CUA
   int current_basespeed = basespeed;
   if (abs(error) >= 500) {
       current_basespeed = 40;
   }
   int speedleft = current_basespeed - motorspeed;
   int speedright = current_basespeed + motorspeed;
   if (speedleft > 100) speedleft = 100;
   if (speedright > 100) speedright = 100;
   if (speedleft < -100) speedleft = -100;
   if (speedright < -100) speedright = -100;
   motor_control(speedleft, speedright);
}
/* ========================================================================= */
/* CẤU HÌNH CLOCK & NGOẠI VI                                                 */
/* ========================================================================= */
void SystemClock_Config(void)
{
   RCC->APB1ENR |= (1 << 28); // Bật PWR
   PWR->CR &= ~(3 << 14);     // VOS = Scale 2
   PWR->CR |= (2 << 14);
   RCC->CR |= (1 << 16);      // Bật HSE
   while (!(RCC->CR & (1 << 17))); // Chờ HSE
   RCC->CR &= ~(1 << 24);     // Tắt PLL
   while ((RCC->CR & (1 << 25)));
   // Cấu hình PLL: M=4, N=72, P=DIV2, Q=7, SRC=HSE
   RCC->PLLCFGR = (7 << 24) | (1 << 22) | (0 << 16) | (72 << 6) | (4 << 0);
   RCC->CR |= (1 << 24);      // Bật PLL
   while (!(RCC->CR & (1 << 25)));
   // Flash Latency = 2 Wait States
   FLASH->ACR &= ~(7 << 0);
   FLASH->ACR |= (2 << 0);
   while ((FLASH->ACR & (7 << 0)) != (2 << 0));
   // Prescalers: APB1 = DIV2, APB2 = DIV1, AHB = DIV1
   RCC->CFGR &= ~( (7 << 13) | (7 << 10) | (15 << 4) );
   RCC->CFGR |= (4 << 10);
   // Gạt Clock sang PLL
   RCC->CFGR &= ~(3 << 0);
   RCC->CFGR |= (2 << 0);
   while ((RCC->CFGR & (3 << 2)) != (2 << 2));
}
void MX_GPIO_Init(void)
{
   // Cấp xung nhịp cho Port A, B, C
   RCC->AHB1ENR |= (1 << 0) | (1 << 1) | (1 << 2);
   // Cấu hình OUTPUT (01): PA5 (LD2), PA8, PB4, PB5, PB10
   GPIOA->MODER &= ~((3 << 10) | (3 << 16));
   GPIOA->MODER |=  ((1 << 10) | (1 << 16)); // PA5, PA8 = 01
   GPIOB->MODER &= ~((3 << 8) | (3 << 10) | (3 << 20));
   GPIOB->MODER |=  ((1 << 8) | (1 << 10) | (1 << 20)); // PB4, PB5, PB10 = 01
   // Cấu hình INPUT (00): PA6, PA7, PB6, PB9, PC7
   // Mặc định lúc reset MODER là 00 với các chân này, nhưng cứ clear cho chắc:
   GPIOA->MODER &= ~((3 << 12) | (3 << 14)); // PA6, PA7
   GPIOB->MODER &= ~((3 << 12) | (3 << 18)); // PB6, PB9
   GPIOC->MODER &= ~(3 << 14);               // PC7
   // Thêm dòng này để chắc chắn chân PC13 (Nút B1) được cấu hình làm Input (00)
       / / Bit của PC13 trong thanh ghi MODER nằm ở vị trí 26 và 27
       GPIOC->MODER &= ~(3 << 26);
   // Kéo các chân output xuống mức 0 ban đầu
   GPIOA->BSRR = (1 << (5 + 16)) | (1 << (8 + 16));
   GPIOB->BSRR = (1 << (4 + 16)) | (1 << (5 + 16)) | (1 << (10 + 16));
}
void MX_TIM2_Init(void)
{
   // Bật clock Timer 2
   RCC->APB1ENR |= (1 << 0);
   // Cấu hình chân PA0 và PA1 làm Alternate Function (PWM)
   GPIOA->MODER &= ~((3 << 0) | (3 << 2));
   GPIOA->MODER |=  ((2 << 0) | (2 << 2)); // Chế độ Alternate Function (10)
   // Gán chức năng AF1 (Timer 2) cho PA0 và PA1 trong thanh ghi AFR[0] (Low)
   GPIOA->AFR[0] &= ~((15 << 0) | (15 << 4));
   GPIOA->AFR[0] |=  ((1 << 0) | (1 << 4));
   // Cấu hình Timer
   TIM2->PSC = 71;    // Prescaler
   TIM2->ARR = 999;   // Period
   // Cấu hình chế độ PWM Mode 1 trên Channel 1 và Channel 2
   TIM2->CCMR1 &= ~((7 << 4) | (7 << 12));
   TIM2->CCMR1 |=  ((6 << 4) | (6 << 12) | (1 << 3) | (1 << 11)); // OC1M, OC2M = 110 (PWM1), Bật Preload
   // Bật output cho CH1 và CH2
   TIM2->CCER |= (1 << 0) | (1 << 4);
   // Khởi động Timer 2
   TIM2->CR1 |= (1 << 0); // Bật bit CEN (Counter Enable)
}
void MX_USART1_UART_Init(void)
{
   // Bật clock USART1 (Nằm trên APB2)
   RCC->APB2ENR |= (1 << 4);
   // Cấu hình PA9 (TX) và PA10 (RX) làm Alternate Function
   GPIOA->MODER &= ~((3 << 18) | (3 << 20));
   GPIOA->MODER |=  ((2 << 18) | (2 << 20)); // Chế độ AF
   // Gán AF7 (USART1) cho PA9 và PA10 trong AFR[1] (High)
   GPIOA->AFR[1] &= ~((15 << 4) | (15 << 8));
   GPIOA->AFR[1] |=  ((7 << 4) | (7 << 8));
   // Cấu hình tốc độ Baud = 9600
   // Xung nhịp APB2 = 72MHz. Baud = 72M / 9600 = 7500 = 0x1D4C
   USART1->BRR = 0x1D4C;
   // Bật USART1, Bật TX, Bật RX, Bật Ngắt RX (RXNEIE)
   USART1->CR1 |= (1 << 13) | (1 << 3) | (1 << 2) | (1 << 5);
   // Mở ngắt USART1 trong hệ thống NVIC của ARM Cortex
   NVIC_EnableIRQ(USART1_IRQn);
}
/* ========================================================================= */
/* TRÌNH PHỤC VỤ NGẮT USART1 (Thay thế cho HAL_UART_RxCpltCallback)          */
/* ========================================================================= */
//void USART1_IRQHandler(void) {
//    // Kiểm tra cờ RXNE (Dữ liệu đã vào bộ đệm nhận)
//    if (USART1->SR & (1 << 5)) {
//        rx_data = USART1->DR; // Đọc dữ liệu (Tự động xóa cờ RXNE)
//
//        if (rx_data == 79 || rx_data == 80) { // Nếu là 'O' hoặc 'P'
//            // Toggle chân PA5 (Bằng cách đọc ODR rồi XOR)
//            GPIOA->ODR ^= (1 << 5);
//        }
//    }
//}
void Error_Handler(void)
{
 while (1)
 {
 }
}

