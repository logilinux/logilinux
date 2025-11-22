#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <array>

constexpr size_t REPORT_SIZE = 256;
constexpr size_t MAX_PACKET_SIZE = 4095;
constexpr size_t LCD_SIZE = 118;

enum class Player {
    NONE = 0,
    X = 1,
    O = 2
};

class MXCreativeConsole {
private:
    int fd;
    uint8_t last_button_state;
    
    const std::vector<std::vector<uint8_t>> INIT_REPORTS = {
        {0x11, 0xff, 0x0b, 0x3b, 0x01, 0xa1, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x11, 0xff, 0x0b, 0x3b, 0x01, 0xa2, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    
public:
    MXCreativeConsole(const char* device_path) : fd(-1), last_button_state(0) {
        fd = open(device_path, O_RDWR);
        if (fd < 0) {
            throw std::runtime_error(std::string("Error opening device: ") + strerror(errno));
        }
        
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) >= 0) {
            char name[256] = {0};
            ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name);
            std::cout << "Connected to: " << name << std::endl;
        }
        
        // Send initialization sequence
        for (const auto& report : INIT_REPORTS) {
            write(fd, report.data(), report.size());
            usleep(10000);
        }
    }
    
    ~MXCreativeConsole() {
        if (fd >= 0) close(fd);
    }
    
    int readButtonPress() {
        std::vector<uint8_t> report(REPORT_SIZE);
        int bytes_read = read(fd, report.data(), report.size());
        
        if (bytes_read < 0) {
            if (errno == EINTR) return -1;
            throw std::runtime_error(std::string("Error reading device: ") + strerror(errno));
        }
        
        if (bytes_read >= 7) {
            uint8_t current_state = report[6];
            
            // Check for button release
            if (last_button_state >= 1 && last_button_state <= 9 && current_state == 0) {
                int button = last_button_state - 1;
                last_button_state = current_state;
                return button;
            }
            
            last_button_state = current_state;
        }
        
        return -1;
    }
    
    void setKeyImage(int keyIndex, const std::vector<uint8_t>& jpegData) {
        if (keyIndex < 0 || keyIndex > 8) return;
        
        auto packets = generateImagePackets(keyIndex, jpegData);
        
        for (const auto& packet : packets) {
            write(fd, packet.data(), packet.size());
            usleep(5000);
        }
    }
    
    void setKeyColor(int keyIndex, uint8_t r, uint8_t g, uint8_t b) {
        auto jpeg = generateColorJPEG(r, g, b);
        if (!jpeg.empty()) {
            setKeyImage(keyIndex, jpeg);
        }
    }
    
    void drawX(int keyIndex) {
        auto jpeg = generateXJPEG();
        if (!jpeg.empty()) {
            setKeyImage(keyIndex, jpeg);
        }
    }
    
    void drawO(int keyIndex) {
        auto jpeg = generateOJPEG();
        if (!jpeg.empty()) {
            setKeyImage(keyIndex, jpeg);
        }
    }
    
    void drawEmpty(int keyIndex, int position) {
        auto jpeg = generateEmptyJPEG(position);
        if (!jpeg.empty()) {
            setKeyImage(keyIndex, jpeg);
        }
    }
    
private:
    std::vector<std::vector<uint8_t>> generateImagePackets(int keyIndex, const std::vector<uint8_t>& jpegData) {
        std::vector<std::vector<uint8_t>> result;
        
        int row = keyIndex / 3;
        int col = keyIndex % 3;
        uint16_t x = 23 + col * (118 + 40);
        uint16_t y = 6 + row * (118 + 40);
        
        const size_t PACKET1_HEADER = 20;
        size_t byteCount1 = std::min(jpegData.size(), MAX_PACKET_SIZE - PACKET1_HEADER);
        
        std::vector<uint8_t> packet1(MAX_PACKET_SIZE, 0);
        std::copy(jpegData.begin(), jpegData.begin() + byteCount1, packet1.begin() + PACKET1_HEADER);
        
        packet1[0] = 0x14;
        packet1[1] = 0xff;
        packet1[2] = 0x02;
        packet1[3] = 0x2b;
        packet1[4] = generateWritePacketByte(1, true, byteCount1 >= jpegData.size());
        packet1[5] = 0x01; packet1[6] = 0x00;
        packet1[7] = 0x01; packet1[8] = 0x00;
        packet1[9] = (x >> 8) & 0xff; packet1[10] = x & 0xff;
        packet1[11] = (y >> 8) & 0xff; packet1[12] = y & 0xff;
        packet1[13] = (LCD_SIZE >> 8) & 0xff; packet1[14] = LCD_SIZE & 0xff;
        packet1[15] = (LCD_SIZE >> 8) & 0xff; packet1[16] = LCD_SIZE & 0xff;
        packet1[18] = (jpegData.size() >> 8) & 0xff; packet1[19] = jpegData.size() & 0xff;
        
        result.push_back(packet1);
        
        size_t remainingBytes = jpegData.size() - byteCount1;
        size_t currentOffset = byteCount1;
        int part = 2;
        
        while (remainingBytes > 0) {
            const size_t headerSize = 5;
            size_t byteCount = std::min(remainingBytes, MAX_PACKET_SIZE - headerSize);
            
            std::vector<uint8_t> packet(MAX_PACKET_SIZE, 0);
            std::copy(jpegData.begin() + currentOffset, jpegData.begin() + currentOffset + byteCount,
                     packet.begin() + headerSize);
            
            packet[0] = 0x14;
            packet[1] = 0xff;
            packet[2] = 0x02;
            packet[3] = 0x2b;
            packet[4] = generateWritePacketByte(part, false, remainingBytes - byteCount == 0);
            
            result.push_back(packet);
            
            remainingBytes -= byteCount;
            currentOffset += byteCount;
            part++;
        }
        
        return result;
    }
    
    uint8_t generateWritePacketByte(int index, bool isFirst, bool isLast) {
        uint8_t value = index | 0b00100000;
        if (isFirst) value |= 0b10000000;
        if (isLast) value |= 0b01000000;
        return value;
    }
    
    std::vector<uint8_t> generateColorJPEG(uint8_t r, uint8_t g, uint8_t b) {
        char filename[256];
        snprintf(filename, sizeof(filename), "/tmp/lcd_%d_%d_%d.jpg", r, g, b);
        
        char cmd[512];
        snprintf(cmd, sizeof(cmd), 
                 "convert -size 118x118 \"xc:rgb(%d,%d,%d)\" -quality 85 %s 2>/dev/null",
                 r, g, b, filename);
        
        if (system(cmd) != 0) return {};
        
        FILE* f = fopen(filename, "rb");
        if (!f) return {};
        
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        std::vector<uint8_t> jpeg(size);
        fread(jpeg.data(), 1, size, f);
        fclose(f);
        
        unlink(filename); // Delete temp file
        
        return jpeg;
    }
    
    std::vector<uint8_t> generateXJPEG() {
        const char* cmd = 
            "convert -size 118x118 xc:\"#2563eb\" "
            "-fill \"#eff6ff\" -stroke \"#eff6ff\" -strokewidth 12 "
            "-draw \"line 20,20 98,98 line 98,20 20,98\" "
            "-quality 85 /tmp/lcd_temp.jpg 2>/dev/null";
        
        if (system(cmd) != 0) return {};
        
        FILE* f = fopen("/tmp/lcd_temp.jpg", "rb");
        if (!f) return {};
        
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        std::vector<uint8_t> jpeg(size);
        fread(jpeg.data(), 1, size, f);
        fclose(f);
        
        return jpeg;
    }
    
    std::vector<uint8_t> generateOJPEG() {
        const char* cmd = 
            "convert -size 118x118 xc:\"#dc2626\" "
            "-fill none -stroke \"#fef2f2\" -strokewidth 12 "
            "-draw \"circle 59,59 59,25\" "
            "-quality 85 /tmp/lcd_temp.jpg 2>/dev/null";
        
        if (system(cmd) != 0) return {};
        
        FILE* f = fopen("/tmp/lcd_temp.jpg", "rb");
        if (!f) return {};
        
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        std::vector<uint8_t> jpeg(size);
        fread(jpeg.data(), 1, size, f);
        fclose(f);
        
        return jpeg;
    }
    
    std::vector<uint8_t> generateEmptyJPEG(int position) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "convert -size 118x118 xc:\"#1f2937\" "
                 "-fill \"#6b7280\" -pointsize 48 -font DejaVu-Sans-Bold "
                 "-gravity center -annotate +0+0 \"%d\" "
                 "-quality 85 /tmp/lcd_temp.jpg 2>/dev/null",
                 position + 1);
        
        if (system(cmd) != 0) return {};
        
        FILE* f = fopen("/tmp/lcd_temp.jpg", "rb");
        if (!f) return {};
        
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        std::vector<uint8_t> jpeg(size);
        fread(jpeg.data(), 1, size, f);
        fclose(f);
        
        return jpeg;
    }
};

class TicTacToe {
private:
    std::array<Player, 9> board;
    Player current_player;
    bool game_over;
    Player winner;
    MXCreativeConsole* console;
    
public:
    TicTacToe(MXCreativeConsole* console_ptr) : console(console_ptr) {
        reset();
    }
    
    void reset() {
        board.fill(Player::NONE);
        current_player = Player::X;
        game_over = false;
        winner = Player::NONE;
        clearScreen();
        display();
    }
    
    void clearScreen() {
        // Set all buttons to empty state (shows position numbers)
        std::cout << "Clearing screen..." << std::endl;
        for (int i = 0; i < 9; i++) {
            std::cout << "  Clearing button " << i << std::endl;
            console->drawEmpty(i, i);
            usleep(100000); // 100ms delay between each button
        }
        std::cout << "Screen cleared!" << std::endl;
        usleep(200000); // Additional 200ms delay to show the clear
    }
    
    bool makeMove(int position) {
        if (position < 0 || position > 8) return false;
        if (game_over) return false;
        if (board[position] != Player::NONE) return false;
        
        board[position] = current_player;
        
        if (checkWin()) {
            game_over = true;
            winner = current_player;
            display();
            usleep(500000); // Show winning state for 500ms
            clearScreen();
            display(); // Redraw after clear
            return true;
        }
        
        if (checkDraw()) {
            game_over = true;
            winner = Player::NONE;
            display();
            usleep(500000); // Show draw state for 500ms
            clearScreen();
            display(); // Redraw after clear
            return true;
        }
        
        current_player = (current_player == Player::X) ? Player::O : Player::X;
        display();
        return true;
    }
    
    void display() {
        std::cout << "\n╔═══╦═══╦═══╗" << std::endl;
        for (int row = 0; row < 3; row++) {
            std::cout << "║";
            for (int col = 0; col < 3; col++) {
                int idx = row * 3 + col;
                char symbol = ' ';
                if (board[idx] == Player::X) symbol = 'X';
                else if (board[idx] == Player::O) symbol = 'O';
                else symbol = '1' + idx;
                
                std::cout << " " << symbol << " ";
                if (col < 2) std::cout << "║";
            }
            std::cout << "║" << std::endl;
            if (row < 2) std::cout << "╠═══╬═══╬═══╣" << std::endl;
        }
        std::cout << "╚═══╩═══╩═══╝" << std::endl;
        
        // Update LCD display
        for (int i = 0; i < 9; i++) {
            if (board[i] == Player::X) {
                console->drawX(i);
            } else if (board[i] == Player::O) {
                console->drawO(i);
            } else {
                console->drawEmpty(i, i);
            }
        }
        
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

int main(int argc, char* argv[]) {
    const char* device_path = "/dev/hidraw1";
    
    if (argc > 1) {
        device_path = argv[1];
    }
    
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║    TIC-TAC-TOE with LCD Display        ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    try {
        MXCreativeConsole console(device_path);
        TicTacToe game(&console);
        
        std::cout << "\nUse your 3x3 grid to play!" << std::endl;
        std::cout << "X = Blue with white X, O = Red with white circle" << std::endl;
        std::cout << "Press Ctrl+C to exit\n" << std::endl;
        
        while (true) {
            int button = console.readButtonPress();
            
            if (button >= 0 && button <= 8) {
                std::cout << "Button " << (button + 1) << " pressed" << std::endl;
                
                // If game is over and center button pressed, reset
                if (game.isGameOver() && button == 4) {
                    std::cout << "\n=== NEW GAME ===" << std::endl;
                    game.reset();
                    continue;
                }
                
                // Try to make move
                if (game.makeMove(button)) {
                    // Move was successful
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
