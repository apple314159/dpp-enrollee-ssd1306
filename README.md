# Device Provisioning Protocol (Enrollee) Example

This example shows how to configure ESP32 as an enrollee using Device Provisioning Protocol(DPP) also known as Wi-Fi Easy Connect.

DPP provides a simple and secure way to onboard ESP32 to a network.
We now support Responder-Enrollee mode of DPP with PSK mode of authentication.

You need a Wi-Fi Easy Connect with Initiator mode capable device to make use of this example. Some Android 10+ devices have this capability. (Vendor specific)

To run the example with an Android 10+ device follow below steps -
1. Compile and flash the example on a [Ma ESP32-C3 OLED](https://wiki.makerfabs.com/MaESP_ESP32_C3_OLED.html), a QR code will appear on panel.
2. Connect your phone to the network, say named "Example-AP".
3. Now go to Settings->WiFi & Internet->Wi-Fi->Example-AP->Advanced->Add Device.
4. Scan QR Code using the scanner, which will make ESP32 connect to Example-AP.

Optional configuration available

*Note:*
- QR Code should be displayed as dark on a white/light background to work properly.
- If displayed QR Code had line gaps, try switching to a new font or a diiferent Terminal program. See below QR Code for for checking beforehand.

### Example output

![alt text](https://github.com/apple314159/dpp-enrollee-ssd1306/blob/main/dpp_panel.jpg "Startup display")
