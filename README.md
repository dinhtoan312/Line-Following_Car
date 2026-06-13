# Line-Following_Car
Xe tự hành dò line sử dụng cảm biến hồng ngoại, thuật toán PID, chạy trên vi điều khiển STM32
Chào bạn! Đây là một chương trình điều khiển xe dò line bằng vi điều khiển STM32F4, lập trình tác động trực tiếp vào thanh ghi (bare-metal) thay vì dùng thư viện HAL. Cách tiếp cận này giúp tối ưu hóa dung lượng và tốc độ thực thi của vi điều khiển.

Tuy nhiên, sau khi kiểm tra đoạn code trên, tôi phát hiện ra **4 vấn đề nghiêm trọng** có thể khiến xe của bạn không hoạt động đúng hoặc chương trình không thể biên dịch thành công. Dưới đây là phân tích chi tiết và cách khắc phục:

### 1. Hàm đếm thời gian (SysTick) bị vô hiệu hóa

Trong hàm `main`, bạn đã gọi `SysTick_Config(72000000 / 1000);` để cấu hình ngắt mỗi 1ms. Nhưng bạn lại comment mất hàm phục vụ ngắt `SysTick_Handler`.
**Hậu quả:** Biến `my_tick` sẽ vĩnh viễn bằng 0. Các logic đo lường thời gian gửi UART (`GetTick() - last_send_tick >= 100`) và hàm chống kẹt khi cua góc vuông (`while(GetTick() - start < 100);`) sẽ bị lỗi treo vô hạn.

**Cách sửa:** Mở comment và sửa lại hàm `SysTick_Handler` để nó tăng biến `my_tick`:

```c
// Bỏ comment và sửa lại như sau
void SysTick_Handler(void) {
    my_tick++;
}

```

### 2. Lỗi cú pháp gây lỗi biên dịch (Syntax Error)

Trong hàm `MX_GPIO_Init()`, bạn có một dòng comment bị sai cú pháp. Trình biên dịch (như KeilC, STM32CubeIDE) sẽ báo lỗi ngay lập tức tại đây.

**Cách sửa:**

```c
// Mã cũ đang bị lỗi:
       / / Bit của PC13 trong thanh ghi MODER nằm ở vị trí 26 và 27

// Sửa thành:
       // Bit của PC13 trong thanh ghi MODER nằm ở vị trí 26 và 27

```

### 3. Lỗi treo ngắt UART (HardFault)

Trong hàm `MX_USART1_UART_Init`, bạn đã kích hoạt ngắt nhận dữ liệu (`USART1->CR1 |= (1 << 5);`) và mở ngắt trong NVIC (`NVIC_EnableIRQ(USART1_IRQn);`). Tuy nhiên, ở cuối file, bạn lại comment toàn bộ hàm `USART1_IRQHandler`.
**Hậu quả:** Khi có dữ liệu gửi đến qua UART, vi điều khiển sẽ nhảy vào hàm ngắt. Vì bạn không định nghĩa, nó sẽ rơi vào trình xử lý lỗi mặc định (thường là vòng lặp vô tận).

**Cách sửa:** Nếu bạn không dùng ngắt UART nữa, hãy tắt nó đi trong `MX_USART1_UART_Init` (xóa `(1 << 5)` và `NVIC_EnableIRQ`). Nếu vẫn muốn dùng, hãy mở comment hàm `USART1_IRQHandler` ở cuối file.

### 4. Khâu tích phân (I) trong PID hoàn toàn vô tác dụng

Bạn khai báo mảng `errors[10]` và dùng hàm `errors_sum()` để tính khâu I (`I = errors_sum(5, 0);`). Tuy nhiên, trong toàn bộ vòng lặp `PID_control`, bạn **không hề cập nhật** giá trị `error` hiện tại vào mảng `errors`.
**Hậu quả:** Mảng `errors` luôn mang giá trị `0` từ lúc khởi tạo. Biến `I` của bạn luôn bằng `0`, xe của bạn thực chất chỉ đang chạy PD chứ không phải PID.

**Cách sửa:** Thêm logic đẩy giá trị cũ lùi lại và cập nhật giá trị `error` mới vào đầu mảng ngay trước khi tính PID:

```c
   // TÍNH TOÁN PID
   int error = position - 2000;
   P = error;

   // Thêm đoạn này để cập nhật mảng errors
   for (int i = 9; i > 0; i--) {
       errors[i] = errors[i - 1]; // Dịch các giá trị cũ về sau
   }
   errors[0] = error; // Đưa lỗi mới nhất vào đầu mảng

   I = errors_sum(5, 0); // Bây giờ I mới có ý nghĩa
   D = error - lastError;
   lastError = error;

```

---
