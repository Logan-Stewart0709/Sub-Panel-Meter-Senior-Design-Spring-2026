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

  Trip detection?

Hardware Description:

  Current hardware signal flow (at nominal values):

  CT_IN = 0.33Vpk -> Level Shift, Boost, and Invert -> C1 = 1.14Vpk + 1.2V (inverted wave) ->Level Shift, Invert, and Buffer -> C_ADC = 1.14Vpk + 1.2V
  
  Voltage hardware signal flow (at nominal value):
  
  PT_IN = 12Vrms (16.97Vpk) -> Resistor ladder -> V_IN = 1.14Vpk -> Level Shift, Buffer, Invert -> V1 = 1.14Vpk + 1.2V (inverted wave) -> Level Shift, Buffer, and Invert -> V_ADC = 1.14Vpk + 1.2V
  
  As the documentation for the external ADC explains, the IC uses a 1.2 reference voltage for it's differential measuring. The easiest implementation of this is to condition the signal to be level shifted to average 1.2V, with a 2.4Vpk-pk (meaning Vmin = 0V and Vmax = 2.4V). We use 1.14Vpk insead to give a 5% buffer in either direction. That way, peaks can be detected by a simple clip detection. To achieve this, for the CT signal, it must be boosted to the proper peak value*. The dual op amp set up was chosen to avoid using a charge pump to introduce a negative voltage source. Essentially, no op amp should output a value <0V. To simplify calculations, we used inverting, level-shifting op amps in series so it inverts once, then inverts again. It also convienently acts as a clean buffer stage. Since the PT input is higher than what we want, the op amp conditioning requires no boosting. So we simply use two inverting buffer op amps. Each potential nominal voltage has it's own resistor ladder such that it always produces a 1.14Vpk across R34.
  
  *All calculations were done with peak value in mind, NOT RMS, in order to protect sensitive measurement ICs.

  ADC sends data to ESP32 via SPI protocol. All 3 phases use the same SPI line for simplicity. Once captured by the ESP32, the data is formatted into packages and sent to the MQTT broker. Packages utilize the average Vrms and Irms over a 30s period because UF facilities requested 15 minute packages (by default) with the ability to remotely prompt the device to send 30s packages for a specified period of time. To simplify this, the device only generates 30s packages (the minimum size requested), which can be combined (30 of them to be exact) to generate a 15 minute package.

  The SD card and I2C ports currently have no functionallity implemented. The SD card is currently connected via SPI.

Software Description:

  The programming was created with the intention of utilizing Visual Studio Code with the free ESP-IDF extension created by Espressif. There are other coding environments this can be edited in, but they are not recommended over what was previously mentioned. To use the code in the software subfolder, first install the coding environment above and create a folder you wish to keep the files in on your computer. Ideally you should store the files locally and not on a cloud like onedrive as this may cause issues. Next copy all files in the folder in the same arrangement of the two text files "CMakeLists" and "LICENSE" directly in the first folder along with a folder titled "main" within your new folder. This subfolder will include all the files inside as well as a second "CMakeLists" file. Essentially there will be a "CMakeLists" file in both layers with all program files in the main folder, the last file titled "LICENSE" is required to utilize the two open source program files "ADS131M0x.h" and "ADS131M0x.c". Once this file arrangement is done on your local computer, open up VSCode and navigate to "open folder" and locate the new folder you created which is titled "PowerMeterProto1" in our case as seen in the first "CMakeLists" file. Once this is open in VSCode with the ESP-IDF extension installed you can connect your ESP microcontroller to your computer with a USB cable and ensure the port and device target are set correctly at the bottom of your screen. Then you can build, flash, and monitor using either the terminal with .py commands or you can use shortcuts "ctrl+shift+P" and search for the commands that begin with "ESP-IDF: Build...", "ESP-IDF: Flash...", and "ESP-IDF: Monitor...". The code is stored on the microcontroller after flashing so building and flashing once are technically the only requirements. 

  To connect to an MQTT broker or something similar, edit the sections at the top of main.cpp described by "Wifi Credentials" and "MQTT Broker" to match your identifications. For our prototype we used a laptop hotspot and a test MQ website titled "HIVEMQ". If using building wifi then enter the credentials where listed, but if using a laptop hotspot to test then enter your username and password the same way but make sure your laptop hotspot is set to 2.4GHz. HIVEMQ is meant to be replaced by UF wifi and UF Facilities MQTT broker information but for testing purposes you can use the same by downloading Eclipse Mosquitto and then opening HIVEMQ on a browser by typing "https://www.hivemq.com/demos/websocket-client/". To use this click "connect", then go to subscriptions and add new topic and paste whatever value "#define MQTT_TOPIC" is in main.cpp and click subscribe. Paste the same value in topic under the "Publish" tab and then click publish to verify it is connected. If the code is running properly on the microcontroller and your wifi is turned on with HIVEMQ active then you should start seeing readings of 30 second ADC packets publish in the HIVEMQ window.
