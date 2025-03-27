# ESP32 Custom OS with Firmware Update via GitHub

## Overview
This project is a custom operating system for the ESP32, built on FreeRTOS, with support for dynamically loading applications and inter-process communication (IPC) using message queues. The OS also features an automatic or manual firmware update system via GitHub Releases.

## Features
- Custom FreeRTOS-based operating system
- Support for loading and executing external ELF applications
- Inter-process communication (IPC) using named queues
- Integrated file system (SPIFFS) for storing applications and configuration
- Wi-Fi management with persistent storage
- OTA firmware updates via GitHub

## Getting Started
### Prerequisites
- ESP32 development board
- ESP-IDF installed on your system
- A GitHub repository to store firmware updates (optional for OTA)

### Building and Flashing
1. Clone the repository:
   ```sh
   git clone https://github.com/YOUR_USER/YOUR_REPO.git
   cd YOUR_REPO
   ```
2. Configure the ESP-IDF environment:
   ```sh
   . $HOME/esp/esp-idf/export.sh
   ```
3. Build the firmware:
   ```sh
   idf.py build
   ```
4. Flash the firmware to the ESP32:
   ```sh
   idf.py flash monitor
   ```

## Application Management
Applications can be uploaded to the SPIFFS file system and executed dynamically. The OS provides a simple shell to start and stop applications by name.

### Running an Application
```sh
start my_app
```
This will automatically resolve to `/spiffs/my_app.elf`.

### Stopping an Application
```sh
stop my_app
```
This sends an IPC message to terminate the application gracefully.

## Compiling Applications
To compile applications that can be executed by the OS, follow these steps:

1. Create a new application source file, e.g., `my_app.c`.
2. Write your application logic, ensuring you include necessary system calls.
3. Use the `elf_loader` framework to handle execution within the OS.
4. Compile the application using ESP-IDF:
   ```sh
   idf.py elf
   ```
5. Upload the compiled `.elf` file to the SPIFFS file system.
6. Run the application using `start my_app`.

## Inter-Process Communication (IPC)
Applications can communicate via named queues. The system app provides access to these queues using system calls similar to stdin and stdout.

## OTA Firmware Update
The OS supports checking for updates on GitHub and downloading the latest firmware. This can be done either manually or automatically.

### Manual Update
1. Run the update command:
   ```sh
   update
   ```
2. The OS will check the latest release on GitHub, download the firmware, and install it.

### Automatic Update
If enabled, the system will periodically check for updates and install them if a new version is available.

## License
This project is licensed under the MIT License. See the LICENSE file for details.

## Contributions
Pull requests are welcome. Please open an issue first to discuss major changes.

