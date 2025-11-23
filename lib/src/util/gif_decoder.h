#ifndef LOGILINUX_GIF_DECODER_H
#define LOGILINUX_GIF_DECODER_H

#include <cstdint>
#include <string>
#include <vector>

namespace LogiLinux {

struct GifFrame {
  std::vector<uint8_t> jpeg_data; // Frame converted to JPEG
  int delay_ms;                   // Frame delay in milliseconds
};

struct GifAnimation {
  std::vector<GifFrame> frames;
  int width;
  int height;
  bool loop;
};

class GifDecoder {
public:
  // Load GIF from memory
  static bool decodeGif(const std::vector<uint8_t> &gifData,
                        GifAnimation &animation, int target_width = 118,
                        int target_height = 118);

  // Load GIF from file
  static bool decodeGifFromFile(const std::string &path,
                                GifAnimation &animation, int target_width = 118,
                                int target_height = 118);

private:
  static std::vector<uint8_t> rgbaToJpeg(const uint8_t *rgba_data, int width,
                                         int height, int quality = 85);
};

} // namespace LogiLinux

#endif // LOGILINUX_GIF_DECODER_H
