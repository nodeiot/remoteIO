# RemoteIO

Para ajuda e suporte, contate o e-mail xxxx@nodeiot.com.br

Biblioteca para comunicação entre dispositivos ESP32 e plataforma em nuvem NodeIoT.

Possibilita a leitura de sensores e acionamento de atuadores em tempo real, com visualização de dados e ações de comando via dashboard web ou mobile.

Requer [ArduinoJson@^7.1.0](https://github.com/bblanchon/ArduinoJson), [WebSockets@^2.4.2](https://github.com/Links2004/arduinoWebSockets) e [ESPAsyncWebServer@^1.2.4](https://github.com/lacamera/ESPAsyncWebServer) para funcionar.

Para usar a biblioteca você pode ainda precisar da última versão do [ESP32](https://github.com/espressif/arduino-esp32) Arduino Core.

## Índice
- [RemoteIO](#remoteio)
  - [Índice](#índice)
  - [Instalação](#instalação)
    - [Using PlatformIO](#using-platformio)

## Instalação

### Using PlatformIO

[PlatformIO](http://platformio.org) is an open source ecosystem for IoT development with cross platform build system, library manager and full support for Espressif ESP8266/ESP32 development. It works on the popular host OS: Mac OS X, Windows, Linux 32/64, Linux ARM (like Raspberry Pi, BeagleBone, CubieBoard).

1. Install [PlatformIO IDE](http://platformio.org/platformio-ide)
2. Create new project using "PlatformIO Home > New Project"
3. Update dev/platform to staging version:
   - [Instruction for Espressif 8266](http://docs.platformio.org/en/latest/platforms/espressif8266.html#using-arduino-framework-with-staging-version)
   - [Instruction for Espressif 32](http://docs.platformio.org/en/latest/platforms/espressif32.html#using-arduino-framework-with-staging-version)
 4. Add "ESP Async WebServer" to project using [Project Configuration File `platformio.ini`](http://docs.platformio.org/page/projectconf.html) and [lib_deps](http://docs.platformio.org/page/projectconf/section_env_library.html#lib-deps) option:

```ini
[env:myboard]
platform = espressif...
board = ...
framework = arduino

# using the latest stable version
lib_deps = ESP Async WebServer

# or using GIT Url (the latest development version)
lib_deps = https://github.com/me-no-dev/ESPAsyncWebServer.git
```
 5. Happy coding with PlatformIO!

