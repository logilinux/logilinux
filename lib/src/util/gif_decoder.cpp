#include "gif_decoder.h"
#include <cstring>
#include <fstream>
#include <iostream>

#ifdef HAVE_GIFLIB
#include <gif_lib.h>
#endif

// We'll need libjpeg-turbo for efficient JPEG encoding
#include <csetjmp>
#include <jpeglib.h>

namespace LogiLinux {

#ifdef HAVE_GIFLIB

// Helper to read GIF from memory
struct GifMemoryReader {
  const uint8_t *data;
  size_t size;
  size_t offset;
};

static int gifReadFunc(GifFileType *gif, GifByteType *buf, int count) {
  GifMemoryReader *reader = static_cast<GifMemoryReader *>(gif->UserData);

  if (reader->offset >= reader->size) {
    return 0;
  }

  size_t to_read =
      std::min(static_cast<size_t>(count), reader->size - reader->offset);
  memcpy(buf, reader->data + reader->offset, to_read);
  reader->offset += to_read;

  return to_read;
}

std::vector<uint8_t> GifDecoder::rgbaToJpeg(const uint8_t *rgba_data, int width,
                                            int height, int quality) {
  std::vector<uint8_t> jpeg_data;

  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  // Write to memory
  unsigned char *jpeg_buffer = nullptr;
  unsigned long jpeg_size = 0;
  jpeg_mem_dest(&cinfo, &jpeg_buffer, &jpeg_size);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3; // RGB
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);

  jpeg_start_compress(&cinfo, TRUE);

  // Convert RGBA to RGB and compress
  std::vector<uint8_t> row_buffer(width * 3);

  for (int y = 0; y < height; y++) {
    const uint8_t *rgba_row = rgba_data + (y * width * 4);

    // Strip alpha channel
    for (int x = 0; x < width; x++) {
      row_buffer[x * 3 + 0] = rgba_row[x * 4 + 0]; // R
      row_buffer[x * 3 + 1] = rgba_row[x * 4 + 1]; // G
      row_buffer[x * 3 + 2] = rgba_row[x * 4 + 2]; // B
    }

    JSAMPROW row_pointer = row_buffer.data();
    jpeg_write_scanlines(&cinfo, &row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);

  // Copy to vector
  jpeg_data.assign(jpeg_buffer, jpeg_buffer + jpeg_size);

  // Cleanup
  free(jpeg_buffer);
  jpeg_destroy_compress(&cinfo);

  return jpeg_data;
}

bool GifDecoder::decodeGif(const std::vector<uint8_t> &gifData,
                           GifAnimation &animation, int target_width,
                           int target_height) {
  int error = 0;

  GifMemoryReader reader;
  reader.data = gifData.data();
  reader.size = gifData.size();
  reader.offset = 0;

  GifFileType *gif = DGifOpen(&reader, gifReadFunc, &error);
  if (!gif) {
    std::cerr << "Failed to open GIF: " << GifErrorString(error) << std::endl;
    return false;
  }

  if (DGifSlurp(gif) == GIF_ERROR) {
    std::cerr << "Failed to read GIF: " << GifErrorString(gif->Error)
              << std::endl;
    DGifCloseFile(gif, &error);
    return false;
  }

  animation.width = gif->SWidth;
  animation.height = gif->SHeight;
  animation.loop = true;
  animation.frames.clear();

  // Allocate frame buffer (RGBA)
  std::vector<uint8_t> frame_buffer(target_width * target_height * 4, 0);

  // Get global color map
  ColorMapObject *globalColorMap = gif->SColorMap;

  for (int i = 0; i < gif->ImageCount; i++) {
    SavedImage *image = &gif->SavedImages[i];
    GifImageDesc *desc = &image->ImageDesc;

    // Get color map for this frame
    ColorMapObject *colorMap = desc->ColorMap ? desc->ColorMap : globalColorMap;
    if (!colorMap) {
      continue;
    }

    // Get frame delay from graphics control extension
    int delay_ms = 100; // Default 100ms

    for (int j = 0; j < image->ExtensionBlockCount; j++) {
      ExtensionBlock *ext = &image->ExtensionBlocks[j];
      if (ext->Function == GRAPHICS_EXT_FUNC_CODE && ext->ByteCount >= 4) {
        delay_ms = (ext->Bytes[2] << 8 | ext->Bytes[1]) *
                   10; // Centiseconds to milliseconds
        if (delay_ms == 0)
          delay_ms = 100;
      }
    }

    // Decode this frame
    GifByteType *raster = image->RasterBits;

    // Scale and convert to RGBA
    for (int y = 0; y < target_height; y++) {
      for (int x = 0; x < target_width; x++) {
        // Map target coords to source coords
        int src_x = (x * desc->Width) / target_width;
        int src_y = (y * desc->Height) / target_height;

        if (src_x < desc->Width && src_y < desc->Height) {
          int src_idx = src_y * desc->Width + src_x;
          uint8_t color_index = raster[src_idx];

          if (color_index < colorMap->ColorCount) {
            GifColorType *color = &colorMap->Colors[color_index];

            int dst_idx = (y * target_width + x) * 4;
            frame_buffer[dst_idx + 0] = color->Red;
            frame_buffer[dst_idx + 1] = color->Green;
            frame_buffer[dst_idx + 2] = color->Blue;
            frame_buffer[dst_idx + 3] = 255; // Opaque
          }
        }
      }
    }

    // Convert frame to JPEG
    GifFrame frame;
    frame.jpeg_data =
        rgbaToJpeg(frame_buffer.data(), target_width, target_height);
    frame.delay_ms = delay_ms;

    animation.frames.push_back(frame);
  }

  DGifCloseFile(gif, &error);

  return !animation.frames.empty();
}

bool GifDecoder::decodeGifFromFile(const std::string &path,
                                   GifAnimation &animation, int target_width,
                                   int target_height) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open file: " << path << std::endl;
    return false;
  }

  // Read entire file
  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(size);
  file.read(reinterpret_cast<char *>(data.data()), size);

  if (!file) {
    std::cerr << "Failed to read file: " << path << std::endl;
    return false;
  }

  return decodeGif(data, animation, target_width, target_height);
}

#else // !HAVE_GIFLIB

bool GifDecoder::decodeGif(const std::vector<uint8_t> &gifData,
                           GifAnimation &animation, int target_width,
                           int target_height) {
  (void)gifData;
  (void)animation;
  (void)target_width;
  (void)target_height;
  std::cerr << "GIF support not available - giflib not found during build"
            << std::endl;
  return false;
}

bool GifDecoder::decodeGifFromFile(const std::string &path,
                                   GifAnimation &animation, int target_width,
                                   int target_height) {
  (void)path;
  (void)animation;
  (void)target_width;
  (void)target_height;
  std::cerr << "GIF support not available - giflib not found during build"
            << std::endl;
  return false;
}

std::vector<uint8_t> GifDecoder::rgbaToJpeg(const uint8_t *rgba_data, int width,
                                            int height, int quality) {
  (void)rgba_data;
  (void)width;
  (void)height;
  (void)quality;
  return {};
}

#endif // HAVE_GIFLIB

} // namespace LogiLinux
