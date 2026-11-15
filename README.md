# SHA256-toy-lab
SHA-256-based toy lab using ESP32S3 SuperMini

This is just a fun project I threw together, which I may work on further in the future. The lab doesn't do real Bitcoin mining, but is a SHA-256 educational demonstrator that runs mathematical equations replicating real Bitcoin mining. I got a hash rate of around 7000H/s, and found the operating temperature to be around 47°C. The device includes temperature hysteresis to stop the device from exceeding 70°C for safety. 

# Parts
-ESP32S3 SuperMini
-Various Wires
-3 Small Pushbuttons
-OLED Display (0.96-inch, 128x64 pixel SH1106)
      (Note: The I2C address may differ across models. In mine, I used 0x3C, but if you get static on the screen, the address may be 0x0D)
-Superglue
-Small Screws

# Photo of Finished Product
<img width="1734" height="1443" alt="IMG_4965" src="https://github.com/user-attachments/assets/ac05c85d-12a0-4f2e-b5d0-72868d2602d1" />
