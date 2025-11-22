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
#include <string>

constexpr size_t MAX_PACKET_SIZE = 4095;
constexpr size_t LCD_SIZE = 118;

class MXCreativeConsole {
private:
    int fd;
    
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
        
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) >= 0) {
            char name[256] = {0};
            ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name);
            std::cout << "Connected to: " << name << std::endl;
        }
        
        for (const auto& report : INIT_REPORTS) {
            write(fd, report.data(), report.size());
            usleep(10000);
        }
    }
    
    ~MXCreativeConsole() {
        if (fd >= 0) close(fd);
    }
    
    void setKeyImage(int keyIndex, const std::vector<uint8_t>& jpegData) {
        if (keyIndex < 0 || keyIndex > 8) return;
        
        auto packets = generateImagePackets(keyIndex, jpegData);
        
        for (const auto& packet : packets) {
            write(fd, packet.data(), packet.size());
            usleep(5000);
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
};

std::vector<uint8_t> extractGifFrame(const char* gifPath, int frameNum) {
    char outfile[256];
    snprintf(outfile, sizeof(outfile), "/tmp/gif_frame_%d.jpg", frameNum);
    
    // Extract frame and resize to 118x118
    char cmd[512];
    snprintf(cmd, sizeof(cmd), 
             "convert '%s[%d]' -coalesce -resize 118x118! -quality 85 %s 2>/dev/null",
             gifPath, frameNum, outfile);
    
    if (system(cmd) != 0) {
        std::cerr << "Failed to extract frame " << frameNum << std::endl;
        return {};
    }
    
    // Read the JPEG
    FILE* f = fopen(outfile, "rb");
    if (!f) {
        std::cerr << "Failed to open frame JPEG" << std::endl;
        return {};
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::vector<uint8_t> jpeg(size);
    fread(jpeg.data(), 1, size, f);
    fclose(f);
    
    unlink(outfile);
    
    return jpeg;
}

int getFrameCount(const char* gifPath) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "identify '%s' | wc -l", gifPath);
    
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return 0;
    
    int count = 0;
    fscanf(pipe, "%d", &count);
    pclose(pipe);
    
    return count;
}

int main(int argc, char* argv[]) {
    const char* device_path = "/dev/hidraw1";
    const char* gif_path = "earthrot.gif";
    
    if (argc > 1) {
        gif_path = argv[1];
    }
    if (argc > 2) {
        device_path = argv[2];
    }
    
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║     GIF Player on LCD Display          ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    try {
        MXCreativeConsole console(device_path);
        
        int frameCount = getFrameCount(gif_path);
        std::cout << "\nLoaded GIF: " << gif_path << std::endl;
        std::cout << "Total frames: " << frameCount << std::endl;
        std::cout << "Playing on all 9 cells..." << std::endl;
        std::cout << "Press Ctrl+C to exit\n" << std::endl;
        
        int currentFrame = 0;
        while (true) {
            std::cout << "Frame " << currentFrame << "/" << (frameCount - 1) << std::endl;
            
            // Extract current frame
            auto jpeg = extractGifFrame(gif_path, currentFrame);
            
            if (!jpeg.empty()) {
                // Display on all 9 cells
                for (int i = 0; i < 9; i++) {
                    console.setKeyImage(i, jpeg);
                }
            }
            
            // Move to next frame
            currentFrame = (currentFrame + 1) % frameCount;
            
            usleep(100000); // 10 FPS (adjust as needed)
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Make sure to run with sudo!" << std::endl;
        return 1;
    }
    
    return 0;
}
