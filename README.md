# techin514-final
## Smart Shower Feedback System

This device focuses on the issue of water usage waste and awareness during long showers - an important issue for utility bills and sustainability.

This system will monitor real-time water usage directly from the shower head (sensing device) and provide feedback to users with a meter and OLED on the other side of the shower wall (display device) to remind users of their water consumption habits. Users will also be able to set weekly water consumption goals.

![image](https://github.com/marjyang/techin514-final/blob/main/images/General%20sketch.png)

### The "sensor" device
![image](https://github.com/marjyang/techin514-final/blob/main/images/sensing_device.JPG)
The sensing device, YF-S201, is connected to the ESP32 from the sensing device, and measures the flow rate of water directly from the showerhead. It generates pulse signals proportional to the water usage, where the ESP32 can process as metrics like total water consumed and flow rate. 

### The "display" device
![image](https://github.com/marjyang/techin514-final/blob/main/images/display_device.JPG)
The display device has an OLED screen, gauge needle and LEDs controlled by its own ESP32 microcontroller. From the water usage data from the sensing device ESP32, the ESP32 here helps display real-time feedback including water consumed during the week and processes data to track progress toward weekly consumption goal. The buttons connected also allow users to manually adjust the gauge needle or the weekly goal set.

### System Architecture
#### Diagram 1
![image](https://github.com/marjyang/techin514-final/blob/main/images/diagram1.JPG)

#### Diagram 2
![image](https://github.com/marjyang/techin514-final/blob/main/images/diagram2.JPG)
Here, the YF-S201 sensor sends data from the shower through GPIO connection with the sensing device XIAO ESP32, where the data is processed. This ESP32 then through, HTTP request, sends the processed data to the display device ESP32 wirelessly. This ESP32 through GPIO controls the output in the LEDs and another GPIO controlling output in the gauge needle. Through I2C/SPI, the ESP32 displays the processed data onto the SSD1306 OLED display. Buttons are also connected to the GPIO of the ESP32, where input data from interacting with buttons is sent over as data to the gauge needle to control the weekly goal set.
