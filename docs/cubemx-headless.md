# Правка `.ioc` и регенерация CubeMX без GUI (headless)

Плейбук, чтобы НЕ тупить по два часа. Правило проекта: конфигурацию периферии держим в
`V7s_Plus.ioc`, генерим код CubeMX, не правим generated init руками вне `USER CODE`.
GUI запускать не обязательно — всё делается из CLI.

## Команды

Регенерация кода из `.ioc` (headless):

```bash
# script-файл ОБЯЗАТЕЛЬНО по Windows-пути (не /tmp — Windows-exe его не видит)
printf 'config load D:\\w_space\\V7s_Plus\\V7s_Plus.ioc\nproject generate\nexit\n' > _mxgen.txt
```
```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX\STM32CubeMX.exe" -q "D:\w_space\V7s_Plus\_mxgen.txt"
```
- Старт CubeMX медленный (~1 мин, грузит IP-паки) — запускать в фоне, ждать, НЕ убивать.
- CubeMX **однопроцессный**: если открыт GUI или висит осиротевший `javaw` — headless
  залипнет. Сначала `Stop-Process -Name STM32CubeMX,javaw`.
- `config load` авто-мигрирует и **перезаписывает загружаемый .ioc на месте**. Для проверки
  формата работай на КОПИИ: `config load copy.ioc` / `config save out.ioc`.
- После — почистить временные `_mx*` файлы из корня.

Сборка (headless CubeIDE) и прошивка — `flash.bat`, либо по отдельности:
```powershell
& "C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\stm32cubeidec.exe" -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "D:\w_space\.stm32cubeide-headless-workspace" -import "D:\w_space\V7s_Plus" -build "V7s_Plus/Debug"
```
- Если билд падает с «Java exit code=1» — это залоченный workspace: убить `stm32cubeidec` и
  удалить `...\.stm32cubeide-headless-workspace\.metadata\.lock`, пересобрать.

## Как CubeMX принимает/отвергает ключи .ioc (ГЛАВНОЕ)

CubeMX при `config load` **молча выкидывает** ключи, которые не понял (в логе ошибки НЕТ).
Признак провала: после регенерации периферия исчезла из `Mcu.IP*` и из generated-кода.

Чтобы добавить периферию (на примере TIM2), нужны ВСЕ части, согласованно:
1. `Mcu.IP5=TIM2` + `Mcu.IPNb` увеличить.
2. Ключи `TIM2.*` (см. ниже про имена параметров).
3. Пин: `PB10.Signal=...` (или GPIO).
4. `ProjectManager.functionlistsort` — добавить `N-MX_TIM2_Init-TIM2-false-HAL-true`.
5. **Если используется виртуальный пин** (режим без вывода, напр. `VP_TIM2_VS_no_output3`):
   - `Mcu.PinNN=VP_TIM2_VS_no_output3` + `Mcu.PinsNb`;
   - **ОБЯЗАТЕЛЬНО** `VP_TIM2_VS_no_output3.Mode=...` и `VP_TIM2_VS_no_output3.Signal=...`
     — БЕЗ этих двух ключей CubeMX молча сносит ВЕСЬ TIM2. Это и был главный затык.

### Имена параметров таймера (откуда брать)
Точные имена режимов/параметров — в базе CubeMX:
`C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX\db\mcu\IP\TIM*_*Modes*.xml`
(grep по `Mode Name=` и `RefParameter Name=`).

Подводные камни, проверено на TIM2 (F071, Output Compare CH3 toggle):
- Режим OC канала: `TIM2.OCMode_3=TIM_OCMODE_TOGGLE` (глобальный, суффикс `_3`).
- Полярность: `TIM2.OCPolarity_3=...` (глобальный).
- **Pulse (CCR) — mode-scoped, НЕ `Pulse_3`**: `TIM2.Pulse-Output\ Compare3\ No\ Output=12000`.
  (`Pulse_3` CubeMX проигнорил → в коде осталось `Pulse=0`.)
- TRGO: `TIM2.TIM_MasterOutputTrigger=TIM_TRGO_UPDATE`.
- Канал: `TIM2.Channel-Output\ Compare3\ No\ Output=TIM_CHANNEL_3` (пробелы экранировать `\ `).
- В `TIM2.IPParameters` перечислить ВСЕ применяемые ключи (иначе не применятся).

### «No Output» vs выход на пин
Режим `Output Compare No Output` CubeMX принимает легко, но он **не настраивает AF на пине**.
Режим `Output Compare CH3` (с выводом на пин) вручную в .ioc собрать не удалось — CubeMX его
отвергал. Рабочее решение: в .ioc — `No Output` (таймер/toggle/TRGO/pulse там), а **AF на пине
PB10 ставит драйвер** одной строкой (`HAL_GPIO_Init` AF2 + `HAL_TIM_OC_Start`). См.
`Core/Src/front_ir_bumper.c` (`FrontIrBumper_CarrierPinInit`).

### Рецепт: добавить ADC-канал в .ioc (проверено, F071, 2026-06)
Чтобы пин `PCx` стал `ADC_INn` и попал в generated MSP (analog GPIO) + `MX_ADC_Init`,
нужны ВСЕ части согласованно (CubeMX молча проглотит и сгенерит, если всё на месте):
1. Блок пина: `PCx.GPIOParameters=GPIO_Label` / `PCx.GPIO_Label=...` / `PCx.Locked=true` /
   `PCx.Mode=INn` / `PCx.Signal=ADC_INn`.
2. `Mcu.PinNN=PCx` + увеличить `Mcu.PinsNb` (CubeMX потом сам перенумерует Pin* при `config load`).
3. В блоке `ADC.*` на КАЖДЫЙ новый канал-`k`: `ADC.Channel-k\#ChannelRegularConversion=ADC_CHANNEL_n`,
   `ADC.Rank-k\#ChannelRegularConversion=ADC_RANK_CHANNEL_NUMBER`,
   `ADC.SamplingTime-k\#ChannelRegularConversion=ADC_SAMPLETIME_239CYCLES_5`.
4. Увеличить `ADC.NbrOfConversion` и дописать те же три ключа `*-k\#ChannelRegularConversion`
   в `ADC.IPParameters` (порядок: ...,Channel-4...,SamplingTime-5,Rank-5,Channel-5,...,NbrOfConversionFlag,master,NbrOfConversion).
- `\#` экранируется ровно так (бэкслеш-решётка), как у существующих каналов.
- ScanConvMode=FORWARD + Rank=CHANNEL_NUMBER => HW конвертит каналы строго по ВОЗРАСТАНИЮ
  номера, список Channel-k — это лишь набор, не порядок. Индексы в DMA-буфере = по возрастанию ch.
- Драйвер (`front_ir_bumper.c`) всё равно сам пересобирает `CHSELR` под свой скан-подсет; .ioc-каналы
  нужны для наглядности + чтобы MspInit сделал пины analog (тогда ручной GPIO-init в драйвере не нужен).

## Что МОЖНО держать в драйвере (а не в .ioc)
ADC — общий ресурс, переключаемый в рантайме (внешний триггер + DMA для ИК-скана сейчас,
токи моторов в другом режиме потом). CubeMX даёт ОДИН статический режим ADC, поэтому
смену режима + DMA + IRQ держит драйвер (`front_ir_bumper.c`). В .ioc лежит только то, что
статично и наглядно: каналы ADC и `ExternalTrigConv=T2_TRGO` (видна связь Timer→ADC).
