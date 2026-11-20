#include <zephyr/irq.h>
#include "board.h"
#include "usart_ring.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

static void usart_irq(const void *arg)
{
    uart_receive_irq();
    if (huart1.Instance->CR1 & (USART_CR1_TXEIE | USART_CR1_TCIE)) {
        HAL_UART_IRQHandler(&huart1);
    }
}

static void usb_irq(const void *arg)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}

void Board_ConnectInterrupts(void)
{
    IRQ_CONNECT(USART1_IRQn, 4, usart_irq, NULL, 0);
    IRQ_CONNECT(OTG_FS_IRQn, 4, usb_irq, NULL, 0);
    /* The HAL MSP callbacks enable each IRQ after configuring the hardware. */
}
