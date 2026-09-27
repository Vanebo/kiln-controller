# Bill of materials

These are the parts used for the current 0–10 V build. Some purchase links below are affiliate links. The project author may receive a commission if you buy through them, at no additional cost to you.

AliExpress listings and option names can change. Check the voltage, interface, board shape and selected variant before ordering. Equivalent parts can be used when their electrical specifications and pinout match the firmware and schematic.

| Qty | Part | Manufacturer / model / value | Purchase link | Notes |
| ---: | --- | --- | --- | --- |
| 1 | Microcontroller board | ESP32-S3 development board, N16R8, USB-C, external 2.4 GHz antenna connection | [AliExpress](https://s.click.aliexpress.com/e/_c4V1haxx) | Choose the ESP32-S3 N16R8 version used by this firmware and confirm its pinout matches the wiring diagram. |
| 1 | Rotary encoder module | EC11 rotary encoder breakout with push switch and knob cap | [AliExpress](https://s.click.aliexpress.com/e/_c38xIEVJ) | The listing title advertises a two-piece pack; only one encoder module is required for the controller. |
| 1 | TFT display | 3.5-inch 480×320 TFT LCD; select the **without touch** option | [AliExpress](https://s.click.aliexpress.com/e/_c3VFtWav) | Confirm an ILI9486-compatible controller and 8-bit parallel interface before ordering. |
| 1 | Thermocouple interface | **Square** MAX31855 module for a K-type thermocouple | [AliExpress](https://s.click.aliexpress.com/e/_c32PNhKz) | Select the square-board version. The listing advertises measurement to 800 °C; use a thermocouple and interface rated for the highest intended kiln temperature. |
| 1 | Analog output converter | PWM-to-voltage converter with 0–10 V output | [AliExpress](https://s.click.aliexpress.com/e/_c3F7v1zb) | Confirm the selected board converts PWM **to** 0–10 V; the listing title is ambiguous about signal direction. Calibrate the output before connecting it to the kiln. |
| 1 | Kiln power regulator | LCTC AC voltage-regulator module; single-phase 220/380 V and three-phase 380 V variants are available | [AliExpress](https://s.click.aliexpress.com/e/_c3RMyyzB) | Select the phase and voltage for the kiln. The required current rating depends on the heating elements; calculate their full-load current and provide suitable margin, cooling and circuit protection. |
| 1 | Low-voltage power supply | AC-to-DC switching supply module, **12 V 2 A, 24 W** option | [AliExpress](https://s.click.aliexpress.com/e/_c3tpCyiv) | This is a bare mains-voltage module. Install it in a suitable enclosed, insulated assembly with appropriate input protection and strain relief; have mains wiring completed and checked by a qualified person. |
| 1 | DC step-down converter | MP1584EN adjustable buck-converter module, rated up to 3 A | [AliExpress](https://s.click.aliexpress.com/e/_c3l9w9nR) | The listing advertises a five-piece pack; only one module is required. Adjust and measure its output voltage before connecting the controller electronics. |
| 1 | Kiln thermocouple | K-type high-temperature probe rated to 1300 °C; 150, 200 and 250 mm options | [AliExpress](https://s.click.aliexpress.com/e/_c4rd7vdF) | Select the probe length to suit the kiln and installation setup. Confirm the probe diameter, connector and lead arrangement before ordering. |

## Optional LM358 PWM-to-0–10 V circuit

Use these parts in place of the purchased analog output converter. See the [circuit and calibration instructions](README.md#pwm-to-010-v-output).

| Qty | Part | Value | Notes |
| ---: | --- | --- | --- |
| 1 | Dual operational amplifier | LM358 | One amplifier channel is used; powered from 12 V. |
| 1 | Input resistor | 4.7 kΩ | Forms the PWM low-pass filter. |
| 1 | Filter capacitor | 10 µF | Observe electrolytic polarity as shown in the circuit. |
| 1 | Supply bypass capacitor | 100 nF ceramic | Fit close to LM358 pins 8 and 4. |
| 1 | Gain resistor | 10 kΩ | From the inverting input to ground. |
| 1 | Adjustable feedback resistor | 25 kΩ or 50 kΩ trimmer | Adjust for 10.00 V at a 100% PWM command. |
| 1 | Prototype board or PCB | As required | Keep low-voltage signal wiring separate from mains wiring. |

The controller firmware outputs a 2 kHz, 12-bit PWM signal on GPIO39. The present hardware converts this to 0–10 V. To use another electric kiln, replace or adapt the output interface for the kiln's required control signal and update the firmware configuration as needed. Do not connect an ESP32 pin directly to a kiln control or mains circuit.
