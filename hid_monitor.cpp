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

constexpr size_t REPORT_SIZE = 256;

void print_hex_data(const std::vector<uint8_t>& data, int length) {
    std::cout << "Data (" << length << " bytes): ";
    for (int i = 0; i < length; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(data[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

int main(int argc, char* argv[]) {
    const char* device_path = "/dev/hidraw1";
    
    // Allow override via command line argument
    if (argc > 1) {
        device_path = argv[1];
    }
    
    std::cout << "Opening HID device: " << device_path << std::endl;
    
    // Open the HID device
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "Error opening device: " << strerror(errno) << std::endl;
        std::cerr << "Try running with sudo" << std::endl;
        return 1;
    }
    
    // Get device info
    struct hidraw_devinfo info;
    if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
        std::cerr << "Error getting device info: " << strerror(errno) << std::endl;
        close(fd);
        return 1;
    }
    
    std::cout << "Device Info:" << std::endl;
    std::cout << "  Vendor ID:  0x" << std::hex << std::setw(4) << std::setfill('0') 
              << info.vendor << std::dec << std::endl;
    std::cout << "  Product ID: 0x" << std::hex << std::setw(4) << std::setfill('0') 
              << info.product << std::dec << std::endl;
    
    // Get device name
    char name[256] = {0};
    if (ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name) >= 0) {
        std::cout << "  Name: " << name << std::endl;
    }
    
    // Set to blocking mode for reading
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    
    std::cout << "\nMonitoring device for input (Press Ctrl+C to exit)..." << std::endl;
    std::cout << "---------------------------------------------------" << std::endl;
    
    std::vector<uint8_t> report(REPORT_SIZE);
    int event_count = 0;
    
    while (true) {
        int bytes_read = read(fd, report.data(), report.size());
        
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted, try again
            }
            std::cerr << "Error reading from device: " << strerror(errno) << std::endl;
            break;
        }
        
        if (bytes_read > 0) {
            event_count++;
            std::cout << "\n[Event #" << event_count << "] ";
            print_hex_data(report, bytes_read);
        }
    }
    
    close(fd);
    return 0;
}
