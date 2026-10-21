#define NOMINMAX
#include <windows.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <jpeglib.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csetjmp>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::atomic<bool> g_stop{false};
HANDLE g_child_process = nullptr;

BOOL WINAPI ConsoleHandler(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
      type == CTRL_CLOSE_EVENT || type == CTRL_SHUTDOWN_EVENT) {
    g_stop.store(true);
    if (g_child_process) {
      TerminateProcess(g_child_process, 130);
    }
    return TRUE;
  }
  return FALSE;
}

struct Options {
  std::wstring adb_path;
  std::wstring scrcpy_path;
  std::wstring serial;
  std::wstring frame_path;
  std::wstring metadata_path;
  std::wstring config_path;
  std::wstring vfs_frame_path;
  std::wstring vfs_metadata_path;
  std::wstring session_id;
  int max_frames = 0;
  int fps = 60;
  int jpeg_quality = 90;
  int bit_rate = 20000000;
  int scale_percent = 50;
  bool input_scale_applied = false;
  int display_width = 0;
  int display_height = 0;
};

std::string Utf8FromWide(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

std::string JsonEscape(const std::string& value) {
  std::string result;
  result.reserve(value.size() + 16);
  for (const unsigned char c : value) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 0x20) {
          char escaped[7];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          result += escaped;
        } else {
          result += static_cast<char>(c);
        }
    }
  }
  return result;
}

std::wstring QuoteWindowsArg(const std::wstring& value) {
  if (value.empty()) return L"\"\"";
  bool needs_quotes = false;
  for (wchar_t c : value) {
    if (c == L' ' || c == L'\t' || c == L'"') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) return value;

  std::wstring result = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : value) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      result.append(backslashes * 2 + 1, L'\\');
      result += L'"';
      backslashes = 0;
    } else {
      result.append(backslashes, L'\\');
      result += c;
      backslashes = 0;
    }
  }
  result.append(backslashes * 2, L'\\');
  result += L'"';
  return result;
}

int ParseScalePercent(const wchar_t* text) {
  if (!text) return 0;
  const std::wstring value(text);
  if (value == L"0.25") return 25;
  if (value == L"0.5") return 50;
  if (value == L"0.75") return 75;
  if (value == L"1") return 100;
  return 0;
}

std::string ScaleJsonNumber(int scale_percent) {
  switch (scale_percent) {
    case 25: return "0.25";
    case 50: return "0.5";
    case 75: return "0.75";
    default: return "1";
  }
}

int ScaledDimension(int value, int scale_percent) {
  const int scaled = std::max(2, (value * scale_percent + 50) / 100);
  return scaled & ~1;
}

bool ParseDisplaySize(const std::string& text, int* width, int* height) {
  for (size_t i = 0; i + 3 < text.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) continue;
    size_t split = i;
    while (split < text.size() && std::isdigit(static_cast<unsigned char>(text[split]))) ++split;
    if (split >= text.size() || text[split] != 'x') continue;
    size_t end = split + 1;
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) ++end;
    if (end == split + 1) continue;
    const int parsed_width = std::stoi(text.substr(i, split - i));
    const int parsed_height = std::stoi(text.substr(split + 1, end - split - 1));
    if (parsed_width > 0 && parsed_height > 0) {
      *width = parsed_width;
      *height = parsed_height;
      return true;
    }
  }
  return false;
}

bool QueryDisplaySize(const Options& options, int* width, int* height) {
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE child_stdout = nullptr;
  HANDLE parent_read = nullptr;
  if (!CreatePipe(&parent_read, &child_stdout, &attributes, 0)) return false;
  SetHandleInformation(parent_read, HANDLE_FLAG_INHERIT, 0);
  HANDLE null_stdin = CreateFileW(L"NUL", GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (null_stdin == INVALID_HANDLE_VALUE) {
    CloseHandle(parent_read);
    CloseHandle(child_stdout);
    return false;
  }
  std::wstring command = QuoteWindowsArg(options.adb_path);
  if (!options.serial.empty()) command += L" -s " + QuoteWindowsArg(options.serial);
  command += L" shell wm size";
  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = null_stdin;
  startup.hStdOutput = child_stdout;
  startup.hStdError = child_stdout;
  PROCESS_INFORMATION process{};
  const bool created = CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr,
                                      TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                      &startup, &process) != FALSE;
  CloseHandle(child_stdout);
  CloseHandle(null_stdin);
  if (!created) {
    CloseHandle(parent_read);
    return false;
  }
  std::string output;
  char buffer[4096];
  DWORD bytes = 0;
  while (ReadFile(parent_read, buffer, sizeof(buffer), &bytes, nullptr) && bytes > 0) {
    output.append(buffer, buffer + bytes);
  }
  CloseHandle(parent_read);
  WaitForSingleObject(process.hProcess, 3000);
  DWORD exit_code = 1;
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return exit_code == 0 && ParseDisplaySize(output, width, height);
}

bool ParseInt(const wchar_t* text, int* value, bool allow_zero = false) {
  wchar_t* end = nullptr;
  const long parsed = std::wcstol(text, &end, 10);
  const long minimum = allow_zero ? 0 : 1;
  if (!text[0] || !end || *end || parsed < minimum || parsed > 1000000000L) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

void PrintUsage() {
  std::wcerr
      << L"Usage: adb_h264_jpeg_producer.exe --adb PATH --scrcpy PATH --frame-path PATH "
         L"--metadata-path PATH [options]\n"
       << L"  --serial SERIAL             optional ADB device serial\n"
       << L"  --scrcpy PATH               bundled scrcpy.exe (v5.0.1)\n"
      << L"  --config-path PATH          optional BeamNG producer handshake\n"
      << L"  --vfs-frame-path PATH       path exposed to BeamNG Lua\n"
      << L"  --vfs-metadata-path PATH    metadata path exposed to BeamNG Lua\n"
      << L"  --session-id ID             producer session identifier\n"
      << L"  --max-frames N              stop after N decoded frames; 0 = forever\n"
       << L"  --fps N                    published JPEG/VFS rate 1..120, default 60\n"
       << L"  --jpeg-quality N            JPEG quality 1..100, default 90\n"
       << L"  --scale FACTOR              output/input scale: 0.25, 0.5, 0.75 or 1; default 0.5\n"
       << L"  --bit-rate N                screenrecord bitrate, default 20000000\n";
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::wstring key = argv[i];
    auto next = [&](std::wstring* target) -> bool {
      if (i + 1 >= argc) return false;
      *target = argv[++i];
      return true;
    };
    if (key == L"--adb") {
      if (!next(&options->adb_path)) return false;
    } else if (key == L"--scrcpy") {
      if (!next(&options->scrcpy_path)) return false;
    } else if (key == L"--serial") {
      if (!next(&options->serial)) return false;
    } else if (key == L"--frame-path") {
      if (!next(&options->frame_path)) return false;
    } else if (key == L"--metadata-path") {
      if (!next(&options->metadata_path)) return false;
    } else if (key == L"--config-path") {
      if (!next(&options->config_path)) return false;
    } else if (key == L"--vfs-frame-path") {
      if (!next(&options->vfs_frame_path)) return false;
    } else if (key == L"--vfs-metadata-path") {
      if (!next(&options->vfs_metadata_path)) return false;
    } else if (key == L"--session-id") {
      if (!next(&options->session_id)) return false;
    } else if (key == L"--max-frames") {
      if (i + 1 >= argc || !ParseInt(argv[++i], &options->max_frames, true)) return false;
    } else if (key == L"--fps") {
      if (i + 1 >= argc || !ParseInt(argv[++i], &options->fps) ||
          options->fps > 120) return false;
    } else if (key == L"--jpeg-quality") {
      if (i + 1 >= argc || !ParseInt(argv[++i], &options->jpeg_quality) ||
          options->jpeg_quality > 100) return false;
    } else if (key == L"--scale") {
      if (i + 1 >= argc) return false;
      options->scale_percent = ParseScalePercent(argv[++i]);
      if (options->scale_percent == 0) return false;
    } else if (key == L"--bit-rate") {
      if (i + 1 >= argc || !ParseInt(argv[++i], &options->bit_rate)) return false;
    } else if (key == L"--help" || key == L"-h") {
      PrintUsage();
      return false;
    } else {
      std::wcerr << L"Unknown option: " << key << L"\n";
      return false;
    }
  }
  return !options->adb_path.empty() && !options->scrcpy_path.empty() &&
         !options->frame_path.empty() &&
         !options->metadata_path.empty();
}

std::string AvError(int error) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(error, buffer, sizeof(buffer));
  return buffer;
}

bool PublishBytes(const std::wstring& target, const uint8_t* data, size_t size) {
  const std::wstring temporary = target + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    std::wcerr << L"CreateFile failed for temporary output, error="
               << GetLastError() << L"\n";
    return false;
  }

  bool success = true;
  size_t offset = 0;
  while (offset < size) {
    const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - offset, 1u << 20));
    DWORD written = 0;
    if (!WriteFile(file, data + offset, chunk, &written, nullptr) || written != chunk) {
      success = false;
      break;
    }
    offset += written;
  }
  if (success && !FlushFileBuffers(file)) success = false;
  CloseHandle(file);
  if (!success) {
    DeleteFileW(temporary.c_str());
    std::wcerr << L"Writing temporary output failed, error=" << GetLastError() << L"\n";
    return false;
  }

  DWORD move_error = ERROR_SUCCESS;
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      return true;
    }
    move_error = GetLastError();
    if (move_error != ERROR_ACCESS_DENIED &&
        move_error != ERROR_SHARING_VIOLATION &&
        move_error != ERROR_LOCK_VIOLATION &&
        move_error != ERROR_UNABLE_TO_REMOVE_REPLACED) {
      break;
    }
    Sleep(10);
  }
  std::wcerr << L"MoveFileEx failed for output, error=" << move_error << L"\n";
  DeleteFileW(temporary.c_str());
  return false;
}

bool PublishText(const std::wstring& target, const std::string& text) {
  return PublishBytes(target, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

std::string OptionalJsonNumber(int64_t value) {
  if (value == AV_NOPTS_VALUE) return "null";
  return std::to_string(value);
}

std::string BuildMetadata(const Options& options, uint64_t sequence,
                           int width, int height, int64_t pts,
                           int64_t arrival_us, const char* status) {
  std::ostringstream json;
  json << "{\"status\":\"" << status << "\","
       << "\"frameNumber\":" << sequence << ","
       << "\"sequence\":" << sequence << ","
       << "\"width\":" << width << ","
       << "\"height\":" << height << ","
       << "\"displayWidth\":" << (options.display_width > 0 ? options.display_width : width) << ","
       << "\"displayHeight\":" << (options.display_height > 0 ? options.display_height : height) << ","
       << "\"ptsUs\":" << OptionalJsonNumber(pts) << ","
       << "\"arrivalUs\":" << arrival_us << ","
       << "\"outputFps\":" << options.fps << ","
       << "\"scale\":" << ScaleJsonNumber(options.scale_percent) << ","
       << "\"sessionId\":\"" << JsonEscape(Utf8FromWide(options.session_id)) << "\","
       << "\"framePath\":\""
       << JsonEscape(Utf8FromWide(options.vfs_frame_path.empty()
                                      ? options.frame_path
                                      : options.vfs_frame_path))
        << "\",\"producer\":\"scrcpy_mkv_jpeg_producer\","
        << "\"source\":\"scrcpy-mkv-named-pipe\"}";
  return json.str();
}

std::string BuildConfig(const Options& options, const char* status,
                        uint64_t sequence, int width, int height) {
  std::ostringstream json;
  json << "{\"status\":\"" << status << "\","
       << "\"framePath\":\"" << JsonEscape(Utf8FromWide(options.frame_path)) << "\","
       << "\"vfsFramePath\":\""
       << JsonEscape(Utf8FromWide(options.vfs_frame_path)) << "\","
       << "\"metadataPath\":\""
       << JsonEscape(Utf8FromWide(options.metadata_path)) << "\","
       << "\"vfsMetadataPath\":\""
       << JsonEscape(Utf8FromWide(options.vfs_metadata_path)) << "\","
        << "\"frameWidth\":" << width << ",\"frameHeight\":" << height << ","
        << "\"displayWidth\":" << (options.display_width > 0 ? options.display_width : width)
        << ",\"displayHeight\":" << (options.display_height > 0 ? options.display_height : height) << ","
        << "\"frameNumber\":" << sequence << ",\"sequence\":" << sequence << ","
        << "\"outputFps\":" << options.fps << ","
        << "\"scale\":" << ScaleJsonNumber(options.scale_percent) << ","
       << "\"sessionId\":\"" << JsonEscape(Utf8FromWide(options.session_id)) << "\","
        << "\"producer\":\"scrcpy_mkv_jpeg_producer\","
        << "\"source\":\"scrcpy-mkv-named-pipe\"}";
  return json.str();
}

int ReadPipe(void* opaque, uint8_t* buffer, int buffer_size) {
  const HANDLE pipe = static_cast<HANDLE>(opaque);
  DWORD bytes_read = 0;
  const DWORD requested = static_cast<DWORD>(std::min(buffer_size, 1 << 20));
  if (!ReadFile(pipe, buffer, requested, &bytes_read, nullptr)) {
    const DWORD error = GetLastError();
    if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA ||
        error == ERROR_PIPE_NOT_CONNECTED) return AVERROR_EOF;
    return AVERROR(error);
  }
  return bytes_read ? static_cast<int>(bytes_read) : AVERROR_EOF;
}

bool StartScrcpy(Options& options, HANDLE* pipe, PROCESS_INFORMATION* process_info) {
  const std::wstring pipe_name = L"\\\\.\\pipe\\beam_auto_scrcpy_video_" +
      std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
  HANDLE server_pipe = CreateNamedPipeW(
      pipe_name.c_str(), PIPE_ACCESS_INBOUND,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      1, 1 << 20, 1 << 20, 0, nullptr);
  if (server_pipe == INVALID_HANDLE_VALUE) {
    std::wcerr << L"CreateNamedPipe failed, error=" << GetLastError() << L"\n";
    return false;
  }

  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE null_stdin = CreateFileW(L"NUL", GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE null_output = CreateFileW(L"NUL", GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (null_stdin == INVALID_HANDLE_VALUE || null_output == INVALID_HANDLE_VALUE) {
    std::wcerr << L"Opening NUL failed, error=" << GetLastError() << L"\n";
    CloseHandle(server_pipe);
    if (null_stdin != INVALID_HANDLE_VALUE) CloseHandle(null_stdin);
    if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
    return false;
  }

  std::wstring command = QuoteWindowsArg(options.scrcpy_path);
  if (!options.serial.empty()) command += L" -s " + QuoteWindowsArg(options.serial);
  command += L" --no-audio --no-video-playback --no-control --record ";
  command += QuoteWindowsArg(pipe_name) + L" --record-format=mkv";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = null_stdin;
  startup.hStdOutput = null_output;
  startup.hStdError = null_output;
  ZeroMemory(process_info, sizeof(*process_info));
  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(L'\0');
  const fs::path scrcpy_directory = fs::path(options.scrcpy_path).parent_path();
  const bool created = CreateProcessW(options.scrcpy_path.c_str(), command_buffer.data(),
      nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, scrcpy_directory.c_str(),
      &startup, process_info) != FALSE;
  const DWORD create_error = created ? ERROR_SUCCESS : GetLastError();
  CloseHandle(null_stdin);
  CloseHandle(null_output);
  if (!created) {
    std::wcerr << L"CreateProcess scrcpy failed, error=" << create_error << L"\n";
    CloseHandle(server_pipe);
    return false;
  }

  g_child_process = process_info->hProcess;
  if (!ConnectNamedPipe(server_pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
    const DWORD error = GetLastError();
    std::wcerr << L"ConnectNamedPipe failed, error=" << error << L"\n";
    TerminateProcess(process_info->hProcess, 1);
    WaitForSingleObject(process_info->hProcess, 3000);
    CloseHandle(process_info->hThread);
    CloseHandle(process_info->hProcess);
    *process_info = {};
    g_child_process = nullptr;
    CloseHandle(server_pipe);
    return false;
  }

  *pipe = server_pipe;
  return true;
}

struct JpegError {
  jpeg_error_mgr base;
  jmp_buf jump;
};

void JpegErrorExit(j_common_ptr info) {
  auto* error = reinterpret_cast<JpegError*>(info->err);
  longjmp(error->jump, 1);
}

bool EncodeJpeg(const AVFrame* frame, const Options& options, SwsContext** scaler,
                std::vector<uint8_t>* output, int* output_width, int* output_height) {
  const int width = frame->width;
  const int height = frame->height;
  const int scaled_width = options.input_scale_applied
                               ? width
                               : ScaledDimension(width, options.scale_percent);
  const int scaled_height = options.input_scale_applied
                                ? height
                                : ScaledDimension(height, options.scale_percent);
  *scaler = sws_getCachedContext(*scaler, width, height,
                                 static_cast<AVPixelFormat>(frame->format), scaled_width,
                                 scaled_height, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR,
                                 nullptr, nullptr, nullptr);
  if (!*scaler) {
    std::cerr << "sws_getCachedContext failed\n";
    return false;
  }

  std::vector<uint8_t> rgb(static_cast<size_t>(scaled_width) * scaled_height * 3);
  uint8_t* destination[] = {rgb.data()};
  int destination_stride[] = {scaled_width * 3};
  if (sws_scale(*scaler, frame->data, frame->linesize, 0, height,
                destination, destination_stride) <= 0) {
    std::cerr << "sws_scale failed\n";
    return false;
  }

  jpeg_compress_struct compressor{};
  JpegError error{};
  compressor.err = jpeg_std_error(&error.base);
  error.base.error_exit = JpegErrorExit;
  if (setjmp(error.jump)) {
    jpeg_destroy_compress(&compressor);
    return false;
  }

  unsigned char* encoded = nullptr;
  unsigned long encoded_size = 0;
  jpeg_create_compress(&compressor);
  jpeg_mem_dest(&compressor, &encoded, &encoded_size);
  compressor.image_width = scaled_width;
  compressor.image_height = scaled_height;
  compressor.input_components = 3;
  compressor.in_color_space = JCS_RGB;
  jpeg_set_defaults(&compressor);
  jpeg_set_quality(&compressor, options.jpeg_quality, TRUE);
  jpeg_start_compress(&compressor, TRUE);
  while (compressor.next_scanline < compressor.image_height) {
    JSAMPROW row = rgb.data() + static_cast<size_t>(compressor.next_scanline) * scaled_width * 3;
    jpeg_write_scanlines(&compressor, &row, 1);
  }
  jpeg_finish_compress(&compressor);
  output->assign(encoded, encoded + encoded_size);
  free(encoded);
  jpeg_destroy_compress(&compressor);
  *output_width = scaled_width;
  *output_height = scaled_height;
  return true;
}

bool PublishFrame(const Options& options, const AVFrame* frame, SwsContext** scaler,
                  uint64_t sequence, int64_t pts_us, int64_t arrival_us) {
  std::vector<uint8_t> jpeg;
  int output_width = 0;
  int output_height = 0;
  if (!EncodeJpeg(frame, options, scaler, &jpeg, &output_width, &output_height)) return false;
  if (!PublishBytes(options.frame_path, jpeg.data(), jpeg.size())) return false;
  const std::string metadata = BuildMetadata(options, sequence, output_width,
                                              output_height, pts_us, arrival_us,
                                             "running");
  if (!PublishText(options.metadata_path, metadata)) return false;
  if (!options.config_path.empty() && sequence == 1) {
    if (!PublishText(options.config_path,
                      BuildConfig(options, "running", sequence, output_width,
                                  output_height))) {
      return false;
    }
  }
  std::cout << "FRAME sequence=" << sequence << " size=" << output_width << "x"
            << output_height << " jpeg_bytes=" << jpeg.size() << "\n";
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Options options;
  if (!ParseOptions(argc, argv, &options)) {
    PrintUsage();
    return 2;
  }
  if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
    std::wcerr << L"SetConsoleCtrlHandler failed\n";
    return 2;
  }
  if (options.fps < 1 || options.fps > 120) {
    std::cerr << "fps must be in range 1..120\n";
    return 2;
  }

  // The published JPEG may be scaled down for performance, but Android input
  // coordinates must use the device's logical display size, not JPEG size.
  // Lua uses displayWidth/displayHeight as the ADB input coordinate range.
  if (!QueryDisplaySize(options, &options.display_width, &options.display_height)) {
    std::wcerr << L"Warning: could not query Android display size; touch coordinates "
                  L"will fall back to decoded frame dimensions\n";
  }

  AVCodecContext* codec = nullptr;
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  SwsContext* scaler = nullptr;
  if (!packet || !frame) {
    std::cerr << "FFmpeg allocation failed\n";
    av_packet_free(&packet);
    av_frame_free(&frame);
    return 3;
  }
  int result = 0;
  HANDLE read_pipe = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION process_info{};
  AVIOContext* io = nullptr;
  AVFormatContext* format = nullptr;
  if (!StartScrcpy(options, &read_pipe, &process_info)) result = 4;
  if (result == 0) {
    constexpr int io_buffer_size = 64 * 1024;
    auto* io_buffer = static_cast<uint8_t*>(av_malloc(io_buffer_size));
    if (!io_buffer) {
      std::cerr << "FFmpeg IO buffer allocation failed\n";
      result = 3;
    } else {
      io = avio_alloc_context(io_buffer, io_buffer_size, 0, read_pipe,
                              ReadPipe, nullptr, nullptr);
      if (!io) {
        av_free(io_buffer);
        std::cerr << "avio_alloc_context failed\n";
        result = 3;
      }
    }
  }
  if (result == 0) {
    format = avformat_alloc_context();
    if (!format) {
      std::cerr << "avformat_alloc_context failed\n";
      result = 3;
    } else {
      format->pb = io;
      format->flags |= AVFMT_FLAG_CUSTOM_IO;
      const int open_result = avformat_open_input(&format, nullptr, nullptr, nullptr);
      if (open_result < 0) {
        std::cerr << "scrcpy recording demux failed: " << AvError(open_result) << "\n";
        result = 5;
      }
    }
  }
  int video_stream = -1;
  if (result == 0) {
    const int info_result = avformat_find_stream_info(format, nullptr);
    if (info_result < 0) {
      std::cerr << "scrcpy stream info failed: " << AvError(info_result) << "\n";
      result = 5;
    } else {
      video_stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                         nullptr, 0);
      if (video_stream < 0) {
        std::cerr << "scrcpy Matroska stream contains no video track\n";
        result = 5;
      }
    }
  }
  if (result == 0) {
    AVCodecParameters* parameters = format->streams[video_stream]->codecpar;
    const AVCodec* stream_decoder = avcodec_find_decoder(parameters->codec_id);
    if (!stream_decoder) {
      std::cerr << "scrcpy video codec decoder not found\n";
      result = 3;
    } else {
      codec = avcodec_alloc_context3(stream_decoder);
      if (!codec || avcodec_parameters_to_context(codec, parameters) < 0 ||
          avcodec_open2(codec, stream_decoder, nullptr) < 0) {
        std::cerr << "Could not initialize scrcpy stream decoder\n";
        result = 3;
      }
    }
  }

  uint64_t sequence = 0;
  auto next_publish = std::chrono::steady_clock::time_point::min();
  const auto publish_period = std::chrono::microseconds(1000000 / options.fps);
  auto publish_decoded_frames = [&](const AVPacket* decoded_packet) -> bool {
    const int send_result = avcodec_send_packet(codec, decoded_packet);
    if (send_result < 0) {
      std::cerr << "avcodec_send_packet failed: " << AvError(send_result) << "\n";
      return false;
    }
    while (true) {
      const int receive_result = avcodec_receive_frame(codec, frame);
      if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) break;
      if (receive_result < 0) {
        std::cerr << "avcodec_receive_frame failed: " << AvError(receive_result) << "\n";
        return false;
      }
      const auto now = std::chrono::steady_clock::now().time_since_epoch();
      const int64_t arrival_us =
          std::chrono::duration_cast<std::chrono::microseconds>(now).count();
      const auto now_point = std::chrono::steady_clock::now();
      if (next_publish != std::chrono::steady_clock::time_point::min() &&
          now_point < next_publish) {
        continue;
      }
      ++sequence;
      const int64_t pts_us = frame->best_effort_timestamp == AV_NOPTS_VALUE
          ? AV_NOPTS_VALUE
          : av_rescale_q(frame->best_effort_timestamp,
              format->streams[video_stream]->time_base, AVRational{1, 1000000});
      if (!PublishFrame(options, frame, &scaler, sequence, pts_us, arrival_us)) return false;
      next_publish = now_point + publish_period;
      if (options.max_frames > 0 && sequence >= static_cast<uint64_t>(options.max_frames)) {
        g_stop.store(true);
        return true;
      }
    }
    return true;
  };

  while (result == 0 && !g_stop.load()) {
    const int read_result = av_read_frame(format, packet);
    if (read_result < 0) {
      if (read_result != AVERROR_EOF && !g_stop.load()) {
        std::cerr << "scrcpy video read failed: " << AvError(read_result) << "\n";
        result = 6;
      }
      break;
    }
    if (packet->stream_index == video_stream && !publish_decoded_frames(packet)) {
      result = 7;
    }
    av_packet_unref(packet);
  }

  if (result != 0 && process_info.hProcess && !g_stop.load()) {
    TerminateProcess(process_info.hProcess, 1);
  }
  if (format) avformat_close_input(&format);
  if (io) {
    av_freep(&io->buffer);
    avio_context_free(&io);
  }
  if (read_pipe != INVALID_HANDLE_VALUE) CloseHandle(read_pipe);
  if (process_info.hProcess) {
    if (g_stop.load() && WaitForSingleObject(process_info.hProcess, 0) == WAIT_TIMEOUT) {
      TerminateProcess(process_info.hProcess, 0);
    }
    WaitForSingleObject(process_info.hProcess, 3000);
    DWORD child_exit = 0;
    GetExitCodeProcess(process_info.hProcess, &child_exit);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    g_child_process = nullptr;
    if (result == 0 && child_exit != 0 && !g_stop.load()) {
      std::cerr << "scrcpy exited with code " << child_exit << "\n";
      result = 8;
    }
  }

  if (sequence == 0 && result == 0) {
    std::cerr << "scrcpy ended without producing a video frame\n";
    result = 9;
  }
  if (!options.config_path.empty()) {
    const std::string stopped = BuildConfig(options, "stopped", sequence,
                                             codec ? codec->width : 0,
                                             codec ? codec->height : 0);
    PublishText(options.config_path, stopped);
  }
  if (scaler) sws_freeContext(scaler);
  avcodec_free_context(&codec);
  av_frame_free(&frame);
  av_packet_free(&packet);
  SetConsoleCtrlHandler(ConsoleHandler, FALSE);
  std::cout << "PRODUCER_STOPPED frames=" << sequence << " result=" << result << "\n";
  return result;
}
