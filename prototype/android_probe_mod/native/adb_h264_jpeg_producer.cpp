#define NOMINMAX
#include <windows.h>

extern "C" {
#include <libavcodec/avcodec.h>
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
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

bool StopRemoteScreenrecord(const Options& options) {
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE null_output = CreateFileW(L"NUL", GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (null_input == INVALID_HANDLE_VALUE || null_output == INVALID_HANDLE_VALUE) {
    if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
    if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
    return false;
  }

  std::wstring command = QuoteWindowsArg(options.adb_path);
  if (!options.serial.empty()) command += L" -s " + QuoteWindowsArg(options.serial);
  command += L" shell pkill -9 screenrecord";
  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = null_input;
  startup.hStdOutput = null_output;
  startup.hStdError = null_output;
  PROCESS_INFORMATION process{};
  const bool created = CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr,
                                      TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                      &startup, &process) != FALSE;
  CloseHandle(null_input);
  CloseHandle(null_output);
  if (!created) return false;
  WaitForSingleObject(process.hProcess, 3000);
  DWORD exit_code = 1;
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  // pkill returns 1 when there was nothing to kill; that is a normal startup
  // condition, so only process-launch failure is considered a failure here.
  return true;
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
      << L"Usage: adb_h264_jpeg_producer.exe --adb PATH --frame-path PATH "
         L"--metadata-path PATH [options]\n"
      << L"  --serial SERIAL             optional ADB device serial\n"
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
  return !options->adb_path.empty() && !options->frame_path.empty() &&
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
       << "\",\"producer\":\"adb_h264_jpeg_producer\","
       << "\"source\":\"adb-exec-out-screenrecord-h264\"}";
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
       << "\"producer\":\"adb_h264_jpeg_producer\","
       << "\"source\":\"adb-exec-out-screenrecord-h264\"}";
  return json.str();
}

bool StartAdb(Options& options, HANDLE* read_pipe, PROCESS_INFORMATION* process_info) {
  // Terminating the local adb.exe does not reliably terminate the remote
  // screenrecord shell on Android. Clean stale recorders before opening a new
  // hardware encoder, otherwise the next session can fail with "unable to
  // start codec" after a few Start/Stop cycles.
  if (!StopRemoteScreenrecord(options)) {
    std::wcerr << L"Warning: could not run remote screenrecord cleanup\n";
  }
  Sleep(250);

  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;

  HANDLE child_stdout = nullptr;
  HANDLE parent_read = nullptr;
  if (!CreatePipe(&parent_read, &child_stdout, &attributes, 1 << 20)) {
    std::wcerr << L"CreatePipe failed, error=" << GetLastError() << L"\n";
    return false;
  }
  SetHandleInformation(parent_read, HANDLE_FLAG_INHERIT, 0);

  HANDLE null_stdin = CreateFileW(L"NUL", GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE null_stderr = CreateFileW(L"NUL", GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (null_stdin == INVALID_HANDLE_VALUE || null_stderr == INVALID_HANDLE_VALUE) {
    std::wcerr << L"Opening NUL failed, error=" << GetLastError() << L"\n";
    CloseHandle(parent_read);
    CloseHandle(child_stdout);
    if (null_stdin != INVALID_HANDLE_VALUE) CloseHandle(null_stdin);
    if (null_stderr != INVALID_HANDLE_VALUE) CloseHandle(null_stderr);
    return false;
  }

  std::wstring command = QuoteWindowsArg(options.adb_path);
  if (!options.serial.empty()) command += L" -s " + QuoteWindowsArg(options.serial);
  command += L" exec-out screenrecord --output-format=h264 --time-limit 0 --bit-rate ";
  const int64_t scaled_bit_rate = std::max<int64_t>(1000000,
      static_cast<int64_t>(options.bit_rate) * options.scale_percent * options.scale_percent / 10000);
  command += std::to_wstring(scaled_bit_rate);
  int display_width = 0;
  int display_height = 0;
  if (QueryDisplaySize(options, &display_width, &display_height)) {
    options.display_width = display_width;
    options.display_height = display_height;
  }
  // Do not pass a fixed --size to screenrecord. Android can change the
  // logical display orientation while the stream is running; a fixed encoder
  // size keeps the H.264 frames portrait and leaves the in-game carrier
  // portrait too. Scaling the decoded frame below preserves the requested
  // output factor while allowing the frame width/height to follow rotation.
  command += L" -";

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = null_stdin;
  startup.hStdOutput = child_stdout;
  startup.hStdError = null_stderr;
  ZeroMemory(process_info, sizeof(*process_info));

  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(L'\0');
  const bool created = CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr,
                                      TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                      &startup, process_info) != FALSE;
  CloseHandle(child_stdout);
  CloseHandle(null_stdin);
  CloseHandle(null_stderr);
  if (!created) {
    std::wcerr << L"CreateProcess adb failed, error=" << GetLastError() << L"\n";
    CloseHandle(parent_read);
    return false;
  }

  g_child_process = process_info->hProcess;
  *read_pipe = parent_read;
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
                  uint64_t sequence, int64_t arrival_us) {
  std::vector<uint8_t> jpeg;
  int output_width = 0;
  int output_height = 0;
  if (!EncodeJpeg(frame, options, scaler, &jpeg, &output_width, &output_height)) return false;
  if (!PublishBytes(options.frame_path, jpeg.data(), jpeg.size())) return false;
  const std::string metadata = BuildMetadata(options, sequence, output_width,
                                             output_height, frame->pts, arrival_us,
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

  const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!decoder) {
    std::cerr << "H.264 decoder not found\n";
    return 3;
  }
  AVCodecContext* codec = avcodec_alloc_context3(decoder);
  AVCodecParserContext* parser = av_parser_init(AV_CODEC_ID_H264);
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  SwsContext* scaler = nullptr;
  if (!codec || !parser || !packet || !frame) {
    std::cerr << "FFmpeg allocation failed\n";
    avcodec_free_context(&codec);
    if (parser) av_parser_close(parser);
    av_packet_free(&packet);
    av_frame_free(&frame);
    return 3;
  }
  int result = 0;
  int open_result = avcodec_open2(codec, decoder, nullptr);
  if (open_result < 0) {
    std::cerr << "avcodec_open2 failed: " << AvError(open_result) << "\n";
    result = 3;
  }

  HANDLE read_pipe = nullptr;
  PROCESS_INFORMATION process_info{};
  if (result == 0 && !StartAdb(options, &read_pipe, &process_info)) result = 4;

  uint64_t sequence = 0;
  auto next_publish = std::chrono::steady_clock::time_point::min();
  const auto publish_period = std::chrono::microseconds(1000000 / options.fps);
  std::vector<uint8_t> input(1 << 20);
  auto decode_packet = [&](const uint8_t* data, int size) -> bool {
    av_packet_unref(packet);
    packet->data = const_cast<uint8_t*>(data);
    packet->size = size;
    int send_result = avcodec_send_packet(codec, packet);
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
      if (!PublishFrame(options, frame, &scaler, sequence, arrival_us)) return false;
      next_publish = now_point + publish_period;
      if (options.max_frames > 0 && sequence >= static_cast<uint64_t>(options.max_frames)) {
        g_stop.store(true);
        return true;
      }
    }
    return true;
  };

  while (result == 0 && !g_stop.load()) {
    DWORD bytes_read = 0;
    if (!ReadFile(read_pipe, input.data(), static_cast<DWORD>(input.size()),
                  &bytes_read, nullptr)) {
      const DWORD error = GetLastError();
      if (error != ERROR_BROKEN_PIPE) {
        std::cerr << "ReadFile failed, error=" << error << "\n";
        result = 5;
      }
      break;
    }
    if (bytes_read == 0) break;

    if (sequence == 0 && bytes_read >= 6 &&
        std::memcmp(input.data(), "ERROR:", 6) == 0) {
      std::cerr << "screenrecord failed: "
                << std::string(reinterpret_cast<char*>(input.data()), bytes_read)
                << "\n";
      result = 9;
      break;
    }

    const uint8_t* cursor = input.data();
    int remaining = static_cast<int>(bytes_read);
    while (remaining > 0 && !g_stop.load()) {
      uint8_t* packet_data = nullptr;
      int packet_size = 0;
      const int consumed = av_parser_parse2(
          parser, codec, &packet_data, &packet_size, cursor, remaining,
          AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      if (consumed < 0) {
        std::cerr << "av_parser_parse2 failed: " << AvError(consumed) << "\n";
        result = 6;
        break;
      }
      if (consumed == 0 && packet_size == 0) {
        std::cerr << "H.264 parser made no progress\n";
        result = 6;
        break;
      }
      cursor += consumed;
      remaining -= consumed;
      if (packet_size > 0 && !decode_packet(packet_data, packet_size)) {
        result = 7;
        break;
      }
    }
  }

  if (read_pipe) CloseHandle(read_pipe);
  if (process_info.hProcess) {
    if (g_stop.load()) TerminateProcess(process_info.hProcess, 0);
    WaitForSingleObject(process_info.hProcess, 3000);
    DWORD child_exit = 0;
    GetExitCodeProcess(process_info.hProcess, &child_exit);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    g_child_process = nullptr;
    if (result == 0 && child_exit != 0 && !g_stop.load()) {
      std::cerr << "adb exited with code " << child_exit << "\n";
      result = 8;
    }
  }

  if (sequence == 0 && result == 0) {
    std::cerr << "screenrecord ended without producing a video frame\n";
    result = 9;
  }
  StopRemoteScreenrecord(options);

  if (!options.config_path.empty()) {
    const std::string stopped = BuildConfig(options, "stopped", sequence,
                                             codec->width, codec->height);
    PublishText(options.config_path, stopped);
  }
  if (scaler) sws_freeContext(scaler);
  av_frame_free(&frame);
  av_packet_free(&packet);
  av_parser_close(parser);
  avcodec_free_context(&codec);
  SetConsoleCtrlHandler(ConsoleHandler, FALSE);
  std::cout << "PRODUCER_STOPPED frames=" << sequence << " result=" << result << "\n";
  return result;
}
