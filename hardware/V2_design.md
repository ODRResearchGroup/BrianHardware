
# E-Nose Hardware - V2 Plans
## Expanded Sensor Set
A set of 16 sensors should be able to use either a 16-channel ADC OR the ESPs analog ping.
[proposed_sensors.zip](https://github.com/user-attachments/files/26353674/proposed_sensors.zip)

## Controllable Sensor Heating
We want to be able to control the sensor heating element independent of the power to he sensor to measure the step responses of the sensors. Not sure if we need to control each heater individually. If so we might need a set of 16 PWMs (and voltage drivers as they are not all at the same voltage).

Full heater power design plans found in [Heater Power Supply Architecture: Design Decision](heater_power.md)

### References
* https://amu.hal.science/hal-02114915/document
* https://pubmed.ncbi.nlm.nih.gov/30857123/
* https://share.google/srmmD0KdJVKQ4o7NJ
  
## Upgraded ADC
This (https://www.ti.com/product/ADS122C14) has 8 channels, 24-bit, programmable gain up to 256.
~~Something like the https://www.analog.com/media/en/technical-documentation/data-sheets/2499fe.pdf, a 24-bit 16-channel ADC.~~

## Replaced Controller Board
We would attach an ESP32 directly to the PCB in future revisions.
