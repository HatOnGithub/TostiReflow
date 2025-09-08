<h1>TostiReflow</h1>

This project was made with the help of Lili's Protolab at Utrecht University by J.T.D.Chen.<br>
This repository contains the source code and PlatformIO files to work on an ESP32 devkit v1.<br>
It is a generalized PID oven controller with a simple web interface via a wireless access point (AP). <br>
It requires a thermister, two switches/buttons, and a relay.

<h2>Other uses for a temperature-controlled oven</h2>
Depending on the maximum temperature your oven can reach, there are a variety of use cases besides SMT. Examples are:<br>

- Drying 3D printer filament <br>
- Annealing metals and plastic<br>
- Curing Epoxy faster <br>
- Curing powder coats <br>
- Wax burnout or Lost-wax casting<br>


<h1>DIY Guide</h1>

Below is a guide on how to choose parts and a step-by-step list of steps to recreate this controller

<h2>Material List</h2>

<h3>Required Materials</h3>
 - 1x ESP32 Devkit V1 (interchangable with other ESP32's)<br>
 - 2x Push buttons<br>
 - 1x Thermistor and associated resistor<br>
 - 1x Solid State Relay capable of handling mains power with a minimum activation voltage of 3V<br>
 - 1x Mini toaster oven or other such heater. Make sure it has a low thermal mass so it heats up faster<br>
<h3>Optional Materials</h3>
 - 1x Rocker Switch and associated Wire Terminators<br>
 - 1x SSD1306 driven 128x64 OLED screen<br>
 - 1x 5V-12V ACDC Power Supply<br>
 - 1x Calibrated thermometer capable of handling 300°C<br>
 - 1x Multimeter<br>
 - 1x Drill<br>

<h2>Considerations</h2>

While picking out the parts, there are some things to keep in mind:<br>
1) Select the known resistance based on the thermistor's datasheet. I used the resistance of my thermistor at 100°C (5.6K Ohm).<br>
2) Make sure your thermistor can handle 300°C; otherwise, you might melt the thermistor or its cable<br>
3) Ensure the oven can stay on at maximum temperature. If the oven has a temperature controller, you will have to modify it to remove it.<br>
4) The Switch is used to turn the whole assembly on or off. Ensure the switch can handle mains power of over 5A.<br>
5) Use the thermometer to compare the measured temperature from the thermistor

<h2>Build Guide</h2>

1) Wire the controller according to [this](Schematic.png) schematic<br>
2) Upload the program using PlatformIO (or ArduinoIDE) and check if every component is functional with the help of a multimeter.
3) Connect to the new network "TostiReflow" using the password "LPLTosti", then go to this web address: HTTP://tostireflow.local/
4) Depending on your oven, create a hole in the chassis to route the thermistor through. If you do not have a drill for this, you can route the thermistor along the door.<br>
5) Test if the oven is controllable by the system by starting a program.
6) Using a serial plotter, start tuning the PID loop as follows:<br>
   1. Set the Integral and Derivative components to 0
   2. Start with the Proportional component. Increasing it will create a faster reaction, but will lead to higher overshoot and oscillation. Decreasing it will make the controller act more slowly and decrease overshoot at the cost of reaction speed. Lower values are recommended for ovens with high thermal mass, whilst higher values are recommended for systems that can heat up and cool down quickly. A tip for finding a good value is to find the value where the system begins to oscillate, then halve the value, increasing it by small steps until you are happy with the result. Aim for a maximum overshoot of 2-3 degrees. Anything above 5 degrees is too much.
   3. The integral component helps remove steady-state errors. In slow-reacting systems, you should add constraints on when the integral component is used to prevent integral windup.
      The longer the reaction speed, the lower the integral should be. 
   4. The derivative should only be used if your temperature signal has low to no noise. This component helps predict the overshoot and lowers the effect of integral windup.
7) With a fully tuned PID loop, test out the oven in a full reflow profile.


