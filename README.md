# LogiLinux - HID Device Monitor & Games

C++ programs to interact with HID (Human Interface Device) devices on Linux, specifically the Logitech MX Creative Keypad.

## Current Device
- **Device**: Logitech MX Creative Keypad
- **Vendor ID**: 0x046d
- **Product ID**: 0xc354
- **Device Path**: /dev/hidraw1

## Programs

### 1. HID Monitor (`hid_monitor`)
A simple monitoring tool that displays raw HID input events.

### 2. Tic-Tac-Toe (`tictactoe`)
An interactive tic-tac-toe game that uses the 3x3 grid on the keypad as input!

### 3. LCD Display (`lcd_display`)
Demonstrates LCD control by displaying random colors on all 9 buttons.

### 4. Tic-Tac-Toe with LCD (`tictactoe_lcd`) ⭐
Full tic-tac-toe game with beautiful graphics on the LCD display!
- Blue buttons with white X symbols for player X
- Red buttons with white circle symbols for player O
- Gray buttons with numbers for empty spaces

## Building

Build all programs:
```bash
make
```

Build individually:
```bash
make hid_monitor
make tictactoe
make lcd_display
make tictactoe_lcd
```

## Running

Both programs require root permissions to access HID devices.

### HID Monitor
```bash
sudo ./hid_monitor
# or
make run-monitor
```

### Tic-Tac-Toe Game
```bash
sudo ./tictactoe
# or
make run-tictactoe
```

### LCD Display Demo
```bash
sudo ./lcd_display
# or
make run-lcd
```

### Tic-Tac-Toe with LCD (Recommended!)
```bash
sudo ./tictactoe_lcd
# or
make run-tictactoe-lcd
# or simply
make run
```

### Using a Different Device

You can specify a different HID device as a command-line argument:

```bash
sudo ./hid_monitor /dev/hidraw0
sudo ./tictactoe /dev/hidraw0
```

## How to Play Tic-Tac-Toe

### Terminal Version (`tictactoe`)
1. Run the game with `sudo ./tictactoe`
2. The board shows positions 1-9 in a 3x3 grid in the terminal
3. Press the corresponding button on your keypad to make a move
4. Players alternate between X and O
5. First to get 3 in a row wins!
6. Press the center button (position 5) to start a new game after winning
7. Press Ctrl+C to exit

### LCD Version (`tictactoe_lcd`) ⭐
1. Run the game with `sudo ./tictactoe_lcd`
2. The 3x3 grid on your LCD displays the game board with beautiful graphics
3. Press buttons to make moves - watch the LCD update in real-time!
4. **X** appears as a blue button with white X symbol
5. **O** appears as a red button with white circle symbol
6. Empty spaces show as gray buttons with position numbers (1-9)
7. First to get 3 in a row wins!
8. Press the center button (position 5) to start a new game
9. Press Ctrl+C to exit

## Features

### HID Monitor
- Opens and monitors HID raw device
- Displays device information (vendor ID, product ID, name)
- Prints all incoming HID reports in hexadecimal format
- Real-time event counter

### Tic-Tac-Toe
- Full tic-tac-toe game logic with win/draw detection
- Beautiful Unicode box-drawing game board
- Real-time HID input detection from 3x3 grid
- Automatic player switching
- Game reset functionality
- Raw data display for debugging button mappings

### LCD Display
- Device initialization with magic init sequences
- Multi-packet image transfer protocol
- JPEG image generation using ImageMagick
- Support for all 9 button positions
- Automatic coordinate calculation for 3x3 grid layout

### Tic-Tac-Toe with LCD ⭐
- Complete tic-tac-toe game on the LCD display
- Beautiful visual graphics for X (blue) and O (red)
- Real-time game state updates on screen
- Combines HID input reading with LCD output
- Professional-looking game interface

## Output Examples

### HID Monitor
When the device sends input, you'll see events like:
```
[Event #1] Data (8 bytes): 01 00 00 00 00 00 00 00
[Event #2] Data (8 bytes): 01 01 00 00 00 00 00 00
```

### Tic-Tac-Toe
```
╔═══╦═══╦═══╗
║ X ║ O ║ 3 ║
╠═══╬═══╬═══╣
║ 4 ║ X ║ 6 ║
╠═══╬═══╬═══╣
║ O ║ 8 ║ 9 ║
╚═══╩═══╩═══╝

Current player: X
```

## References

The `ref/hidpp/` directory contains reference implementations from the HID++ library that demonstrate advanced HID device interaction patterns.
