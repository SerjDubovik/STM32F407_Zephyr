#include "usart_ring.h"
#include "app_rtos.h"

#define UART_RX_BUFFER_SIZE 2048

static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile bool rx_error;
static volatile uint8_t rx_buffer[UART_RX_BUFFER_SIZE];

bool uart_write(const uint8_t *data, uint16_t size)
{
    uint32_t timeout = 500U + (uint32_t)size * 10000U / MYUART.Init.BaudRate;
    uint32_t started = HAL_GetTick();
    if (HAL_UART_Transmit_IT(&MYUART, data, size) != HAL_OK) return false;
    while (MYUART.gState == HAL_UART_STATE_BUSY_TX) {
        uint32_t elapsed = HAL_GetTick() - started;
        if (elapsed >= timeout) {
            HAL_UART_AbortTransmit(&MYUART);
            return false;
        }
        AppRtos_WaitForProtocolEvent(timeout - elapsed);
    }
    return MYUART.gState == HAL_UART_STATE_READY;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == &MYUART) AppRtos_NotifyProtocolFromISR();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != &MYUART) return;
    /* An overrun detected in the HAL transmit IRQ path disables RXNEIE. */
    rx_error = true;
    __HAL_UART_ENABLE_IT(uart, UART_IT_RXNE);
    AppRtos_NotifyProtocolFromISR();
}

uint16_t uart_available(void)
{
    return (uint16_t)(UART_RX_BUFFER_SIZE + rx_head - rx_tail) % UART_RX_BUFFER_SIZE;
}

uint8_t uart_read(void)
{
    uint16_t tail = rx_tail;
    uint8_t byte = rx_buffer[tail];
    rx_tail = (tail + 1) % UART_RX_BUFFER_SIZE;
    return byte;
}

bool uart_discard_on_error(void)
{
    if (!rx_error) return false;

    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    rx_tail = rx_head;
    rx_error = false;
    __set_PRIMASK(interrupt_mask);
    return true;
}

void uart_receive_irq(void)
{
    uint32_t status = MYUART.Instance->SR;
    uint32_t errors = USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE;
    if (!(status & (USART_SR_RXNE | errors))) return;

    /* Reading SR followed by DR acknowledges both RXNE and USART errors. */
    uint8_t byte = (uint8_t)MYUART.Instance->DR;
    uint16_t next = (rx_head + 1) % UART_RX_BUFFER_SIZE;
    if ((status & errors) || next == rx_tail) {
        rx_error = true;
        AppRtos_NotifyProtocolFromISR();
        return;
    }
    rx_buffer[rx_head] = byte;
    rx_head = next;
    /* Drain long JSON frames in batches and dispatch short commands at LF. */
    if ((next & 63U) == 0 || byte == '\n') AppRtos_NotifyProtocolFromISR();
}
