/**
 * video-test.cpp - Video playback on MX Creative Console
 * 
 * This example plays a video file on the MX Keypad's 3x3 LCD grid.
 * Similar to the video.html reference implementation, it decodes video
 * frames, scales them to fit the display, encodes as JPEG, and sends
 * them to the device using setScreenImage().
 * 
 * Requirements:
 *   - ffmpeg libraries (libavcodec, libavformat, libavutil, libswscale)
 *   - libjpeg-turbo
 * 
 * Usage: ./video-test <video_file.mp4>
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <jpeglib.h>

#include <logilinux/events.h>
#include <logilinux/logilinux.h>
#include "../lib/src/devices/mx_keypad_device.h"

std::atomic<bool> running(true);
std::atomic<bool> paused(false);

void signalHandler(int signal) { running = false; }

// Encode RGB frame to JPEG
std::vector<uint8_t> encodeJpeg(const uint8_t* rgb_data, int width, int height, int quality = 80) {
    std::vector<uint8_t> jpeg_buffer;
    
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    
    unsigned char* outbuffer = nullptr;
    unsigned long outsize = 0;
    jpeg_mem_dest(&cinfo, &outbuffer, &outsize);
    
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    
    while (cinfo.next_scanline < cinfo.image_height) {
        const uint8_t* row = rgb_data + cinfo.next_scanline * width * 3;
        JSAMPROW row_pointer = const_cast<JSAMPROW>(row);
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }
    
    jpeg_finish_compress(&cinfo);
    
    jpeg_buffer.assign(outbuffer, outbuffer + outsize);
    free(outbuffer);
    
    jpeg_destroy_compress(&cinfo);
    
    return jpeg_buffer;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file>" << std::endl;
        std::cerr << "Example: " << argv[0] << " badapple.mp4" << std::endl;
        return 1;
    }

    const char* video_path = argv[1];
    
    auto version = LogiLinux::getVersion();
    std::cout << "LogiLinux Video Player v" << version.major << "."
              << version.minor << "." << version.patch << std::endl;
    std::cout << "Playing: " << video_path << "\n" << std::endl;

    signal(SIGINT, signalHandler);

    // Initialize FFmpeg
    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, video_path, nullptr, nullptr) < 0) {
        std::cerr << "Could not open video file: " << video_path << std::endl;
        return 1;
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream info" << std::endl;
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Find video stream
    int video_stream_idx = -1;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    if (video_stream_idx < 0) {
        std::cerr << "No video stream found" << std::endl;
        avformat_close_input(&format_ctx);
        return 1;
    }

    AVStream* video_stream = format_ctx->streams[video_stream_idx];
    AVCodecParameters* codecpar = video_stream->codecpar;

    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "Codec not found" << std::endl;
        avformat_close_input(&format_ctx);
        return 1;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, codecpar);

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Output dimensions (full screen: 434x434)
    const int out_width = LogiLinux::MXKeypadDevice::SCREEN_WIDTH;
    const int out_height = LogiLinux::MXKeypadDevice::SCREEN_HEIGHT;

    // Setup scaler
    SwsContext* sws_ctx = sws_getContext(
        codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
        out_width, out_height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!sws_ctx) {
        std::cerr << "Could not create scaler context" << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Allocate frames
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();
    
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, out_width, out_height, 1);
    std::vector<uint8_t> rgb_buffer(num_bytes);
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer.data(),
                         AV_PIX_FMT_RGB24, out_width, out_height, 1);

    AVPacket* packet = av_packet_alloc();

    // Get frame rate
    double fps = av_q2d(video_stream->r_frame_rate);
    if (fps <= 0) fps = 30.0;
    auto frame_duration = std::chrono::microseconds(static_cast<int>(1000000.0 / fps));

    std::cout << "Video: " << codec_ctx->width << "x" << codec_ctx->height 
              << " @ " << fps << " fps" << std::endl;
    std::cout << "Output: " << out_width << "x" << out_height << std::endl;

    // Initialize LogiLinux
    LogiLinux::Library lib;

    std::cout << "\nScanning for devices..." << std::endl;
    auto devices = lib.discoverDevices();

    if (devices.empty()) {
        std::cerr << "No Logitech devices found!" << std::endl;
        goto cleanup;
    }

    {
        LogiLinux::MXKeypadDevice* keypad = nullptr;

        for (const auto& device : devices) {
            if (device->getType() == LogiLinux::DeviceType::MX_KEYPAD) {
                auto* kp = dynamic_cast<LogiLinux::MXKeypadDevice*>(device.get());
                if (kp && kp->hasCapability(LogiLinux::DeviceCapability::LCD_DISPLAY)) {
                    keypad = kp;
                    const auto& info = device->getInfo();
                    std::cout << "Found: " << info.name << std::endl;
                    break;
                }
            }
        }

        if (!keypad) {
            std::cerr << "No MX Keypad with LCD found!" << std::endl;
            goto cleanup;
        }

        std::cout << "\nInitializing device..." << std::endl;

        if (!keypad->initialize()) {
            std::cerr << "Failed to initialize MX Keypad!" << std::endl;
            std::cerr << "Try running with sudo." << std::endl;
            goto cleanup;
        }

        // Setup button handler for play/pause (center button = key 4)
        keypad->setEventCallback([](LogiLinux::EventPtr event) {
            auto* btn = dynamic_cast<LogiLinux::ButtonEvent*>(event.get());
            if (btn && btn->type == LogiLinux::EventType::BUTTON_PRESS) {
                if (btn->button_code == 4) {  // Center button
                    paused = !paused;
                    std::cout << (paused ? "Paused" : "Playing") << std::endl;
                }
            }
        });
        keypad->startMonitoring();

        std::cout << "Device initialized!" << std::endl;
        std::cout << "\nPlaying video... Press center button to pause, Ctrl+C to exit.\n" << std::endl;

        int frame_count = 0;
        auto start_time = std::chrono::steady_clock::now();

        // Main decode/display loop
        while (running && av_read_frame(format_ctx, packet) >= 0) {
            if (packet->stream_index == video_stream_idx) {
                if (avcodec_send_packet(codec_ctx, packet) == 0) {
                    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                        // Handle pause
                        while (paused && running) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                        if (!running) break;

                        auto frame_start = std::chrono::steady_clock::now();

                        // Scale to output size
                        sws_scale(sws_ctx, frame->data, frame->linesize, 0,
                                  codec_ctx->height, rgb_frame->data, rgb_frame->linesize);

                        // Encode to JPEG
                        auto jpeg_data = encodeJpeg(rgb_buffer.data(), out_width, out_height, 75);

                        // Send to device
                        keypad->setScreenImage(jpeg_data);

                        frame_count++;

                        // Frame rate control
                        auto elapsed = std::chrono::steady_clock::now() - frame_start;
                        if (elapsed < frame_duration) {
                            std::this_thread::sleep_for(frame_duration - elapsed);
                        }
                    }
                }
            }
            av_packet_unref(packet);
            
            if (!running) break;
        }

        auto end_time = std::chrono::steady_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
        
        std::cout << "\nPlayback finished!" << std::endl;
        std::cout << "Frames: " << frame_count << std::endl;
        if (total_time > 0) {
            std::cout << "Avg FPS: " << (frame_count / total_time) << std::endl;
        }

        keypad->stopMonitoring();
    }

cleanup:
    av_packet_free(&packet);
    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    sws_freeContext(sws_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);

    return 0;
}
