#include <atomic>
#include <cassert>
#include <csignal>
#include <iostream>
#include <logilinux/device.h>
#include <logilinux/events.h>
#include <logilinux/logilinux.h>
#include <thread>
#include <array>

// Include the actual implementation header
#include "../lib/src/devices/mx_keypad_device.h"


std::atomic<bool> running(true);
void signalHandler(int signal) { running = false; }


std::vector<uint8_t> generateColorJPEG(uint8_t r, uint8_t g, uint8_t b) {
  char ppmname[256];
  char jpgname[256];
  snprintf(ppmname, sizeof(ppmname), "/tmp/color_%d_%d_%d.ppm", r, g, b);
  snprintf(jpgname, sizeof(jpgname), "/tmp/color_%d_%d_%d.jpg", r, g, b);

  FILE *ppm = fopen(ppmname, "wb");
  if (!ppm) {
    return {};
  }

  fprintf(ppm, "P6\n118 118\n255\n");

  for (int i = 0; i < 118 * 118; i++) {
    uint8_t rgb[3] = {r, g, b};
    fwrite(rgb, 1, 3, ppm);
  }

  fclose(ppm);

  char cmd[256*2+34];
  snprintf(cmd, sizeof(cmd), "convert %s -quality 85 %s 2>/dev/null", ppmname,
           jpgname);

  if (system(cmd) != 0) {
    unlink(ppmname);
    return {};
  }

  FILE *f = fopen(jpgname, "rb");
  if (!f) {
    unlink(ppmname);
    return {};
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  std::vector<uint8_t> jpeg(size);
  fread(jpeg.data(), 1, size, f);
  fclose(f);

  unlink(ppmname);
  unlink(jpgname);

  return jpeg;
}

enum class player : uint8_t {
    red,
    blue
} turn;

std::vector<uint8_t> playerJPEG[2];

enum class tileState : uint8_t {
    none = 0,
    red,
    blue
};
std::array<tileState, 9> replicatedGameState = {tileState::none};

enum class winningPlayerType : uint8_t {
    none,
    cats,
    red,
    blue
};
winningPlayerType checkWinner() {
    const auto& gameState = replicatedGameState;
    tileState winningTile = tileState::none;
    // check columns
    if (gameState[0] == gameState[3] && gameState[0] == gameState[6]) {
        winningTile = winningTile == tileState::none ? gameState[0] : winningTile;
    } 
    if (gameState[1] == gameState[4] && gameState[1] == gameState[7]) {
        winningTile = winningTile == tileState::none ? gameState[1] : winningTile;
    }
    if (gameState[2] == gameState[5] && gameState[2] ==  gameState[8]) {
        winningTile = winningTile == tileState::none ? gameState[2] : winningTile;
    }
    // check rows
    if (gameState[0] == gameState[1] && gameState[0] == gameState[2]) {
        winningTile = winningTile == tileState::none ? gameState[0] : winningTile;
    } 
    if (gameState[3] == gameState[4] && gameState[3] == gameState[5]) {
        winningTile = winningTile == tileState::none ? gameState[3] : winningTile;
    }
    if (gameState[6] == gameState[7] && gameState[6] == gameState[8]) {
        winningTile = winningTile == tileState::none ? gameState[6] : winningTile;
    }
    // check diagonals
    if (gameState[0] == gameState[4] && gameState[0] == gameState[8]) {
        winningTile = winningTile == tileState::none ? gameState[0] : winningTile;
    } 
    if (gameState[2] == gameState[4] && gameState[2] == gameState[6]) {
        winningTile = winningTile == tileState::none ? gameState[2] : winningTile;
    }

    winningPlayerType winner;
    switch (winningTile) {
        case tileState::none: {
            winner = winningPlayerType::none;
            bool tilesLeftEmpty = false;
            for (const auto& tile : gameState) {
                if (tile == tileState::none) {
                    tilesLeftEmpty = true;                    
                }
            }
            if (!tilesLeftEmpty) {
                winner = winningPlayerType::cats;
            }
        } break;
        case tileState::red:
            winner = winningPlayerType::red;
            break;
        case tileState::blue:
            winner = winningPlayerType::blue;
            break;
    }
    return winner;
}

void onEvent(LogiLinux::EventPtr event,
             LogiLinux::MXKeypadDevice *device) {
    if (auto rotation = std::dynamic_pointer_cast<LogiLinux::RotationEvent>(event)) {
        std::cout << "Rotated: " << rotation->delta << " steps" << std::endl;
    }
    if (auto button = std::dynamic_pointer_cast<LogiLinux::ButtonEvent>(event)) {
        std::cout << "Button " << button->button_code << (button->pressed ? " pressed" : " released") << std::endl;

        if ( !button->pressed ) { // this api is really weird, but means effectively "is released".

            if (replicatedGameState[button->button_code] == tileState::none) {
                switch (turn) {
                    case player::red:
                        replicatedGameState[button->button_code] = tileState::red;
                        break;
                    case player::blue:
                        replicatedGameState[button->button_code] = tileState::blue;
                        break;
                }
                const size_t playerTurn = static_cast<size_t>(turn);
                device->setKeyImage(button->button_code, playerJPEG[playerTurn]);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                turn = static_cast<player>((playerTurn + 1) % 2);

                auto winner = checkWinner();
                switch (winner) {
                    case winningPlayerType::none:
                        // nothing to do here, keep playing until there's a winner...
                        break;
                    case winningPlayerType::red:
                        std::cout << "RED WINS!!!" << std::endl;
                        running = false;
                        break;
                    case winningPlayerType::blue:
                        std::cout << "BLUE WINS!!!" << std::endl;
                        running = false;
                        break;
                    case winningPlayerType::cats:
                        std::cout << "Cats game... 😸" << std::endl;
                        running = false;
                        break;
                }
            }
            else {
                std::cout << "Hey, that's cheating!" << std::endl;
            }
        }
    }
}

int main() {
  auto version = LogiLinux::getVersion();
  std::cout << "LogiLinux MX Keypad Example v" << version.major << "."
            << version.minor << "." << version.patch << std::endl;
  std::cout << "Press any button to change its color!" << std::endl;
  std::cout << "Press Ctrl+C to exit\n" << std::endl;

  signal(SIGINT, signalHandler);

  LogiLinux::Library lib;

  std::cout << "Scanning for devices..." << std::endl;
  auto devices = lib.discoverDevices();

  if (devices.empty()) {
    std::cerr << "No Logitech devices found!" << std::endl;
    return 1;
  }

  LogiLinux::MXKeypadDevice *console_device = nullptr;

  for (const auto &device : devices) {
    const auto &info = device->getInfo();

    if (device->getType() == LogiLinux::DeviceType::MX_KEYPAD) {
      auto *cc_device =
          dynamic_cast<LogiLinux::MXKeypadDevice *>(device.get());
      
      // Check if this device has LCD capability
      if (cc_device && cc_device->hasCapability(LogiLinux::DeviceCapability::LCD_DISPLAY)) {
        console_device = cc_device;
        std::cout << "Found: " << info.name << " (" << info.device_path << ")"
                  << std::endl;
        std::cout << "  -> Using this MX Keypad with LCD!" << std::endl;
        break; // Use the first one with LCD
      }
    }
  }

  if (!console_device) {
    std::cerr << "No MX Keypad found!" << std::endl;
    return 1;
  }

  std::cout << "\nInitializing LCD..." << std::endl;
  
  if (!console_device->initialize()) {
    std::cerr << "Failed to initialize MX Keypad!" << std::endl;
    std::cerr << "Make sure you have permissions to access hidraw devices."
              << std::endl;
    return 1;
  }

  std::cout << "LCD initialized successfully!" << std::endl;

  // Set initial colors for all buttons
  std::cout << "\nSetting initial colors..." << std::endl;

  for (int i = 0; i < 9; i++) {
    uint8_t r = 0;
    uint8_t g = 10;
    uint8_t b = 0;

    auto jpeg = generateColorJPEG(r, g, b);
    if (!jpeg.empty()) {
      console_device->setKeyImage(i, jpeg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  playerJPEG[0] = generateColorJPEG(255,0,0);
  playerJPEG[1] = generateColorJPEG(0,0,255);

  assert(!playerJPEG[0].empty());
  assert(!playerJPEG[1].empty());

  turn = player::red;

  std::cout << "\nReady! Press buttons to change colors.\n" << std::endl;

  console_device->setEventCallback([console_device](LogiLinux::EventPtr event) {
    onEvent(event, console_device);
  });

  console_device->startMonitoring();

  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  console_device->stopMonitoring();

  std::cout << "\nExiting..." << std::endl;
  return 0;
}