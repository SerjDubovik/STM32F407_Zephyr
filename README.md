# Zephyr для DevEBox STM32F407VGT6

Откройте `STM32F407_Zephyr.code-workspace` в VS Code. Проект рассчитан на DevEBox STM32F4XX_M с STM32F407VGT6, кварцем HSE 8 МГц и дисплеем ILI9341 на параллельной шине. Ядро Zephyr 4.2.1 настраивает SYSCLK 168 МГц, APB1 42 МГц, APB2 84 МГц и PLLQ 48 МГц. RTC использует LSE 32,768 кГц.

### Сборка, запись и отладка

1. **Ctrl+Shift+B** запускает сборку. Результаты находятся в `build/zephyr/zephyr.elf`, `zephyr.hex` и `zephyr.bin`.
2. Подайте питание на плату. Соедините SWDIO адаптера с PA13, SWCLK с PA14 и GND с GND. Если адаптер имеет вход VTref, соедините его с 3,3 В платы. Используется внешний DAPLink в режиме CMSIS-DAP/SWD, частота 1 МГц.
3. Выберите **Terminal → Run Task → Zephyr Flash (DAPLink)**. Задача собирает приложение, записывает его через OpenOCD во внутреннюю Flash с адреса 0x08000000, проверяет содержимое и перезапускает плату.
4. **F5** запускает Cortex-Debug с остановкой в `main`. Для сессии отладки установите рекомендованное расширение Cortex-Debug.

Те же операции из PowerShell в корне проекта выполняются командой `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 -Action Build` или с параметром `-Action Flash`.

### Среда на Windows

В этой рабочей копии зависимости находятся в `.deps` и `.venv`. Для новой копии установите Git, Python 3.12, CMake, Ninja, Arm GNU Toolchain и OpenOCD, затем выполните задачу **Zephyr Setup**. Она скачивает Zephyr v4.2.1 и три модуля его манифеста. Можно указать конкретный Python командой `.\tools\build.ps1 -Action Setup -Python 'C:\Python312\python.exe'`. Для работы задач сборки CMake и Ninja должны быть в PATH, для прошивки в PATH нужен также OpenOCD.

По умолчанию сборка использует ARM GCC из `C:\VStools\gnu-tools-for-stm32\tools`. При другой установке задайте `GNUARMEMB_TOOLCHAIN_PATH` и поправьте пути отладчика в `.vscode/launch.json`. Все исходники прошивки для сборки находятся в этом проекте и в его зависимостях.

### Windows-клиент SdCardBrowser

Исходники находятся в `SdCardBrowser/`. Для сборки нужен .NET 10 SDK под Windows. В VS Code выберите **Terminal → Run Task → SdCardBrowser Run**. Задача соберёт Release и откроет приложение. В Visual Studio можно открыть `SdCardBrowser/SdCardBrowser.slnx`.

Подключите USB-разъём STM32 (CDC ACM) или USB-UART к USART1, нажмите **Найти порты**, выберите COM-порт и нажмите **Подключиться**. Клиент сначала проверяет выбранную скорость командой JSON `tasks`, затем перебирает скорости USART1 от 9600 до 460800 бод (8N1). После ответа показывает найденную скорость и сохраняет её при закрытии. Для проверенной платы с DAPLink это COM5, 460800 бод. В окне доступны файлы microSD и W25Q16, показания XPT2046 и вкладка **Потоки Zephyr**. Эту вкладку можно открыть и подключить до монтирования карты. Доля времени вычисляется по двум последовательным снимкам счётчиков Protocol, Display и IDLE. Управление паузой доступно для Display.

### Периферия

| Устройство | Выводы и режим |
| --- | --- |
| USART1 | PA9 TX, PA10 RX, 115200 8N1 по умолчанию. Скорость можно задать в файле `1:/sys/settings_uart.json` на W25Q16. |
| USB OTG FS | PA11 D−, PA12 D+, класс CDC ACM, виртуальный COM-порт для JSON-протокола. |
| W25Q16 | SPI1, PB3 SCK, PB4 MISO, PB5 MOSI, PA15 CS. 2,625 МГц, том FatFs `1:`. |
| microSD | SDIO, PC8–PC11 D0–D3, PC12 CLK, PD2 CMD. После инициализации включается 4-битный режим. Том `0:`. |
| ILI9341 | FSMC NE1, 16 бит, 240 × 320, RGB565. PD7 NE1, PD4 NOE, PD5 NWE, PD11 A16 (RS), PE2 RESET. Команды по 0x60000000, данные по 0x60020000. |
| Шина данных LCD | D0–D3 на PD14, PD15, PD0, PD1. D4–D12 на PE7–PE15. D13–D15 на PD8–PD10. |
| XPT2046 | SPI2, PB13 SCK, PB14 MISO, PB15 MOSI, PB12 CS, PB11 PENIRQ. 1,3125 МГц, калибровка 3900 → 200 по обеим осям. |
| Светодиод | PA1, GPIO Output. |

### Проверка на плате

После запуска должно появиться меню LVGL. Подключите USB-разъём STM32 к ПК или используйте USB-UART с логическими уровнями 3,3 В. В SdCardBrowser выберите соответствующий COM-порт. На вкладке «Потоки Zephyr» должны появиться Protocol, Display и IDLE. Команда `flashid` должна вернуть JEDEC ID `EF4015`. Проверить команду можно и из PowerShell, передав номер фактического порта вместо COM5.

```powershell
$port = [System.IO.Ports.SerialPort]::new('COM5', 460800)
$port.ReadTimeout = 3000
try {
    $port.Open()
    $port.WriteLine(([string][char]0x1E) + '{"id":1,"op":"flashid"}')
    do {
        $line = $port.ReadLine()
        $line
    } until (($line | ConvertFrom-Json).PSObject.Properties['result'])
} finally {
    $port.Dispose()
}
```

`Core/Src/app_rtos.c` создаёт потоки Zephyr, мьютекс периферии и семафор для пробуждения Protocol из прерываний UART и USB. Меньшее число приоритета Zephyr означает более высокий приоритет. Оба прикладных стека по 16 КиБ размещены в CCM, буферы DMA дисплея находятся в обычной SRAM. Ядро управляет SysTick и таблицей прерываний. Параметры тактирования находятся в `boards/others/devebox_f407vg/devebox_f407vg.dts`, инициализация периферии STM32Cube HAL — в `Core/Src/board.c`, `Core/Src/stm32f4xx_hal_msp.c` и `USB_DEVICE/Target/usbd_conf.c`.
