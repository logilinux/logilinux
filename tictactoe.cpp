#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <array>

constexpr size_t REPORT_SIZE = 256;

enum class Player {
    NONE = 0,
    X = 1,
    O = 2
};

class TicTacToe {
private:
    std::array<Player, 9> board;
    Player current_player;
    bool game_over;
    Player winner;
    
    // Map grid positions to board indices (0-8)
    // Assuming 3x3 grid layout:
    // [0] [1] [2]
    // [3] [4] [5]
    // [6] [7] [8]
    
public:
    TicTacToe() {
        reset();
    }
    
    void reset() {
        board.fill(Player::NONE);
        current_player = Player::X;
        game_over = false;
        winner = Player::NONE;
        display();
    }
    
    bool makeMove(int position) {
        if (position < 0 || position > 8) return false;
        if (game_over) return false;
        if (board[position] != Player::NONE) return false;
        
        board[position] = current_player;
        
        if (checkWin()) {
            game_over = true;
            winner = current_player;
            return true;
        }
        
        if (checkDraw()) {
            game_over = true;
            winner = Player::NONE;
            return true;
        }
        
        // Switch player
        current_player = (current_player == Player::X) ? Player::O : Player::X;
        return true;
    }
    
    void display() const {
        std::cout << "\n╔═══╦═══╦═══╗" << std::endl;
        for (int row = 0; row < 3; row++) {
            std::cout << "║";
            for (int col = 0; col < 3; col++) {
                int idx = row * 3 + col;
                char symbol = ' ';
                if (board[idx] == Player::X) symbol = 'X';
                else if (board[idx] == Player::O) symbol = 'O';
                else symbol = '1' + idx; // Show position number
                
                std::cout << " " << symbol << " ";
                if (col < 2) std::cout << "║";
            }
            std::cout << "║" << std::endl;
            if (row < 2) std::cout << "╠═══╬═══╬═══╣" << std::endl;
        }
        std::cout << "╚═══╩═══╩═══╝" << std::endl;
        
        if (game_over) {
            if (winner == Player::NONE) {
                std::cout << "\n🤝 It's a DRAW! 🤝" << std::endl;
            } else {
                std::cout << "\n🎉 Player " << (winner == Player::X ? "X" : "O") 
                          << " WINS! 🎉" << std::endl;
            }
            std::cout << "Press position 5 (center) to play again, or Ctrl+C to exit" << std::endl;
        } else {
            std::cout << "\nCurrent player: " << (current_player == Player::X ? "X" : "O") << std::endl;
        }
    }
    
    bool isGameOver() const { return game_over; }
    
private:
    bool checkWin() const {
        // Check rows
        for (int i = 0; i < 3; i++) {
            if (board[i*3] != Player::NONE && 
                board[i*3] == board[i*3+1] && 
                board[i*3+1] == board[i*3+2]) {
                return true;
            }
        }
        
        // Check columns
        for (int i = 0; i < 3; i++) {
            if (board[i] != Player::NONE && 
                board[i] == board[i+3] && 
                board[i+3] == board[i+6]) {
                return true;
            }
        }
        
        // Check diagonals
        if (board[4] != Player::NONE) {
            if ((board[0] == board[4] && board[4] == board[8]) ||
                (board[2] == board[4] && board[4] == board[6])) {
                return true;
            }
        }
        
        return false;
    }
    
    bool checkDraw() const {
        for (const auto& cell : board) {
            if (cell == Player::NONE) return false;
        }
        return true;
    }
};

class HIDInput {
private:
    int fd;
    std::vector<uint8_t> last_report;
    uint8_t last_button_state;
    
public:
    HIDInput(const char* device_path) : fd(-1), last_button_state(0) {
        fd = open(device_path, O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error(std::string("Error opening device: ") + strerror(errno));
        }
        
        // Get device info
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) >= 0) {
            char name[256] = {0};
            ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name);
            std::cout << "Connected to: " << name << std::endl;
            std::cout << "Vendor: 0x" << std::hex << info.vendor 
                      << " Product: 0x" << info.product << std::dec << std::endl;
        }
        
        last_report.resize(REPORT_SIZE);
    }
    
    ~HIDInput() {
        if (fd >= 0) close(fd);
    }
    
    // Returns -1 if no button press detected, or button position (0-8) if pressed
    int readButtonPress() {
        std::vector<uint8_t> report(REPORT_SIZE);
        int bytes_read = read(fd, report.data(), report.size());
        
        if (bytes_read < 0) {
            if (errno == EINTR) return -1;
            throw std::runtime_error(std::string("Error reading device: ") + strerror(errno));
        }
        
        if (bytes_read > 0) {
            // Try to detect button releases
            // For Logitech MX Creative Keypad:
            // - Byte 6 contains button state
            // - 1-9 = button pressed, 0 = released
            int button_position = detectButton(report, bytes_read);
            
            last_report = report;
            return button_position;
        }
        
        return -1;
    }
    
private:
    int detectButton(const std::vector<uint8_t>& report, int length) {
        // Logitech MX Creative Keypad protocol:
        // Byte 6 contains the button value:
        // - 1-9 when button is pressed
        // - 0 when button is released
        // We only trigger on release (transition from non-zero to zero)
        
        if (length < 7) return -1;
        
        uint8_t current_state = report[6];
        
        // Check if this is a button release (was pressed, now zero)
        if (last_button_state >= 1 && last_button_state <= 9 && current_state == 0) {
            int button = last_button_state - 1; // Convert 1-9 to 0-8
            last_button_state = current_state;
            return button;
        }
        
        // Update state
        last_button_state = current_state;
        return -1;
    }
};

int main(int argc, char* argv[]) {
    const char* device_path = "/dev/hidraw1";
    
    if (argc > 1) {
        device_path = argv[1];
    }
    
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║    TIC-TAC-TOE with HID Input          ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    try {
        HIDInput input(device_path);
        TicTacToe game;
        
        std::cout << "\nUse your 3x3 grid to play!" << std::endl;
        std::cout << "Press buttons to make moves (positions 1-9)" << std::endl;
        std::cout << "Press Ctrl+C to exit\n" << std::endl;
        
        while (true) {
            int button = input.readButtonPress();
            
            if (button >= 0 && button <= 8) {
                std::cout << "Button press detected: Position " << (button + 1) << std::endl;
                
                // If game is over and center button pressed, reset
                if (game.isGameOver() && button == 4) {
                    std::cout << "\n=== NEW GAME ===" << std::endl;
                    game.reset();
                    continue;
                }
                
                // Try to make move
                if (game.makeMove(button)) {
                    game.display();
                } else {
                    std::cout << "Invalid move! Try another position." << std::endl;
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Make sure to run with sudo!" << std::endl;
        return 1;
    }
    
    return 0;
}
