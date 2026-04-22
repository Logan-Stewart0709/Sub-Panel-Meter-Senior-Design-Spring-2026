# Sub-Panel-Meter-Senior-Design-Spring-2026

Known issues:

  When testing on the LabVolt machine, powering the main board with 12V AC and metering a phase where the negative terminal of the power and PT input is tied to neutral and R34 and the negative PT terminal input are both tied to ground, one of the rectifier diodes burns up. Reason unknown.
  
  Current readings are unreliable, potentially due to too high of tolerance (5%), may be fixed y a lower tolerance (0.1%)?
  
  AC power input does not work. When 12V AC is input to the main board power (via function generator, not LabVolt machine), the signal clips between 0V and 6.25V, reason unknown. Works with 12V DC.
  
Recommended additions:

  User interface utilizing the I2C port to connect to the network and calibrate system.
  
  Find a way to automatically select the correct resistor ladder for the nominal voltage input by user or auto detect the voltage instead of using jumpers.
  
  Implement code to incorperate the SD card black box. (Stores sent data in a FIFO queue to save data in the event of a power outage for later analysis)
  
  Implement more precise setup for the ADC instead of using the "fast startup" feature.
  
  Implement pinging function to send 30s packages and send 15 min packages by default.

Hardware Description:

  Current hardware signal flow (at nominal values):

  CT_IN = 0.33Vpk -> Level Shift, Boost, and Invert -> C1 = 1.14Vpk + 1.2V (inverted wave) ->Level Shift, Invert, and Buffer -> C_ADC = 1.14Vpk + 1.2V
  
  Voltage hardware signal flow (at nominal value):
  
  PT_IN = 12Vrms (16.97Vpk) -> Resistor ladder -> V_IN = 1.14Vpk -> Level Shift, Buffer, Invert -> V1 = 1.14Vpk + 1.2V (inverted wave) -> Level Shift, Buffer, and Invert -> V_ADC = 1.14Vpk + 1.2V
  
  As the documentation for the external ADC explains, the IC uses a 1.2 reference voltage for it's differential measuring. The easiest implementation of this is to condition the signal to be level shifted to average 1.2V, with a 2.4Vpk-pk (meaning Vmin = 0V and Vmax = 2.4V). We use 1.14Vpk insead to give a 5% buffer in either direction. That way, peaks can be detected by a simple clip detection. To achieve this, for the CT signal, it must be boosted to the proper peak value*. The dual op amp set up was chosen to avoid using a charge pump to introduce a negative voltage source. Essentially, no op amp should output a value <0V. To simplify calculations, we used inverting, level-shifting op amps in series so it inverts once, then inverts again. It also convienently acts as a clean buffer stage. Since the PT input is higher than what we want, the op amp conditioning requires no boosting. So we simply use two inverting buffer op amps. Each potential nominal voltage has it's own resistor ladder such that it always produces a 1.14Vpk across R34.
  
  *All calculations were done with peak value in mind, NOT RMS, in order to protect sensitive measurement ICs.

  ADC sends data to ESP32 via SPI protocol. All 3 phases use the same SPI line for simplicity. Once captured by the ESP32, the data is formatted into packages and sent to the MQTT broker. Packages utilize the average Vrms and Irms over a 30s period because UF facilities requested 15 minute packages (by default) with the ability to remotely prompt the device to send 30s packages for a specified period of time. To simplify this, the device only generates 30s packages (the minimum size requested), which can be combined (30 of them to be exact) to generate a 15 minute package.

  The SD card and I2C ports currently have no functionallity implemented. The SD card is currently connected via SPI.
