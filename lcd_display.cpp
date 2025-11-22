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
#include <random>
#include <cmath>
#include <cstdio>

constexpr size_t MAX_PACKET_SIZE = 4095;
constexpr size_t LCD_SIZE = 118;

class MXCreativeConsole {
private:
    int fd;
    
    // Initial magic reports to wake/init the device
    const std::vector<std::vector<uint8_t>> INIT_REPORTS = {
        {0x11, 0xff, 0x0b, 0x3b, 0x01, 0xa1, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x11, 0xff, 0x0b, 0x3b, 0x01, 0xa2, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    
public:
    MXCreativeConsole(const char* device_path) : fd(-1) {
        fd = open(device_path, O_RDWR);
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
        
        // Send initialization sequence
        std::cout << "Sending initialization sequence..." << std::endl;
        for (const auto& report : INIT_REPORTS) {
            if (!sendReport(report)) {
                throw std::runtime_error("Failed to send init report");
            }
            usleep(10000); // 10ms delay between init packets
        }
        std::cout << "Device initialized!" << std::endl;
    }
    
    ~MXCreativeConsole() {
        if (fd >= 0) close(fd);
    }
    
    bool sendReport(const std::vector<uint8_t>& report) {
        ssize_t ret = write(fd, report.data(), report.size());
        if (ret < 0) {
            std::cerr << "Error sending report: " << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }
    
    void setKeyImage(int keyIndex, const std::vector<uint8_t>& jpegData) {
        if (keyIndex < 0 || keyIndex > 8) {
            std::cerr << "Invalid key index: " << keyIndex << std::endl;
            return;
        }
        
        std::cout << "Setting image for key " << keyIndex << " (" << jpegData.size() << " bytes)" << std::endl;
        
        auto packets = generateImagePackets(keyIndex, jpegData);
        
        std::cout << "Sending " << packets.size() << " packets..." << std::endl;
        for (size_t i = 0; i < packets.size(); i++) {
            if (!sendReport(packets[i])) {
                std::cerr << "Failed to send packet " << i << std::endl;
                return;
            }
            usleep(5000); // 5ms delay between packets
        }
        std::cout << "Image sent successfully!" << std::endl;
    }
    
private:
    std::vector<std::vector<uint8_t>> generateImagePackets(int keyIndex, const std::vector<uint8_t>& jpegData) {
        std::vector<std::vector<uint8_t>> result;
        
        // Calculate coordinates (3x3 grid layout)
        int row = keyIndex / 3;
        int col = keyIndex % 3;
        uint16_t x = 23 + col * (118 + 40);
        uint16_t y = 6 + row * (118 + 40);
        
        std::cout << "  Key position: row=" << row << ", col=" << col 
                  << ", x=" << x << ", y=" << y << std::endl;
        
        // --- First Packet ---
        const size_t PACKET1_HEADER = 20;
        size_t byteCount1 = std::min(jpegData.size(), MAX_PACKET_SIZE - PACKET1_HEADER);
        
        std::vector<uint8_t> packet1(MAX_PACKET_SIZE, 0);
        
        // Copy JPEG data
        std::copy(jpegData.begin(), jpegData.begin() + byteCount1, packet1.begin() + PACKET1_HEADER);
        
        // Build header
        packet1[0] = 0x14;  // Report ID
        packet1[1] = 0xff;
        packet1[2] = 0x02;
        packet1[3] = 0x2b;
        packet1[4] = generateWritePacketByte(1, true, byteCount1 >= jpegData.size());
        
        // Big-endian 16-bit values
        packet1[5] = 0x01; packet1[6] = 0x00;  // Unknown
        packet1[7] = 0x01; packet1[8] = 0x00;  // Unknown
        
        // X coordinate (big-endian)
        packet1[9] = (x >> 8) & 0xff;
        packet1[10] = x & 0xff;
        
        // Y coordinate (big-endian)
        packet1[11] = (y >> 8) & 0xff;
        packet1[12] = y & 0xff;
        
        // Width (big-endian)
        packet1[13] = (LCD_SIZE >> 8) & 0xff;
        packet1[14] = LCD_SIZE & 0xff;
        
        // Height (big-endian)
        packet1[15] = (LCD_SIZE >> 8) & 0xff;
        packet1[16] = LCD_SIZE & 0xff;
        
        // Total JPEG size (big-endian, 16-bit)
        packet1[18] = (jpegData.size() >> 8) & 0xff;
        packet1[19] = jpegData.size() & 0xff;
        
        result.push_back(packet1);
        
        // --- Subsequent Packets ---
        size_t remainingBytes = jpegData.size() - byteCount1;
        size_t currentOffset = byteCount1;
        int part = 2;
        
        while (remainingBytes > 0) {
            const size_t headerSize = 5;
            size_t byteCount = std::min(remainingBytes, MAX_PACKET_SIZE - headerSize);
            
            std::vector<uint8_t> packet(MAX_PACKET_SIZE, 0);
            
            // Copy JPEG data
            std::copy(jpegData.begin() + currentOffset, 
                     jpegData.begin() + currentOffset + byteCount,
                     packet.begin() + headerSize);
            
            // Build header
            packet[0] = 0x14;  // Report ID
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
};

// Generate a JPEG with random colored pixels
std::vector<uint8_t> generateRandomPixelJPEG(int seed) {
    char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/lcd_random_%d.ppm", seed);
    
    // Generate a PPM file with random RGB pixels
    FILE* ppm = fopen(filename, "wb");
    if (!ppm) {
        std::cerr << "Failed to create PPM file" << std::endl;
        return {};
    }
    
    // Write PPM header (P6 = binary RGB)
    fprintf(ppm, "P6\n%d %d\n255\n", LCD_SIZE, LCD_SIZE);
    
    // Generate random RGB values for each pixel
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> dis(0, 255);
    
    for (int i = 0; i < LCD_SIZE * LCD_SIZE; i++) {
        uint8_t rgb[3] = {
            static_cast<uint8_t>(dis(gen)),
            static_cast<uint8_t>(dis(gen)),
            static_cast<uint8_t>(dis(gen))
        };
        fwrite(rgb, 1, 3, ppm);
    }
    
    fclose(ppm);
    
    // Convert PPM to JPEG
    char jpgname[256];
    snprintf(jpgname, sizeof(jpgname), "/tmp/lcd_random_%d.jpg", seed);
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "convert %s -quality 85 %s", filename, jpgname);
    
    int ret = system(cmd);
    unlink(filename); // Delete PPM file
    
    if (ret != 0) {
        std::cerr << "Failed to convert PPM to JPEG" << std::endl;
        return {};
    }
    
    // Read the JPEG
    FILE* f = fopen(jpgname, "rb");
    if (!f) {
        std::cerr << "Failed to open generated JPEG" << std::endl;
        return {};
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::cout << "Generated random pixel JPEG: " << size << " bytes" << std::endl;
    
    std::vector<uint8_t> jpeg(size);
    fread(jpeg.data(), 1, size, f);
    fclose(f);
    
    unlink(jpgname); // Delete temp JPEG
    
    return jpeg;
}

int main(int argc, char* argv[]) {
    const char* device_path = "/dev/hidraw1";
    
    if (argc > 1) {
        device_path = argv[1];
    }
    
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║  MX Creative Console LCD Display       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    try {
        MXCreativeConsole console(device_path);
        
        std::cout << "\nDisplaying random pixels on all 9 buttons..." << std::endl;
        std::cout << "Each button will have 118x118 = 13,924 random colored pixels!\n" << std::endl;
        
        // Display random pixels on all 9 buttons
        for (int i = 0; i < 9; i++) {
            std::cout << "Button " << (i+1) << ": Generating random pixels..." << std::endl;
            
            auto jpeg = generateRandomPixelJPEG(i * 1000); // Different seed for each button
            console.setKeyImage(i, jpeg);
            
            sleep(1); // Pause between buttons
        }
        
        std::cout << "\n✅ All buttons updated with random pixels!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Make sure to run with sudo!" << std::endl;
        return 1;
    }
    
    return 0;
}
