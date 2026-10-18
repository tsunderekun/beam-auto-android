#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr UINT WM_APP_STATUS = WM_APP + 1;
constexpr UINT WM_APP_FINISHED = WM_APP + 2;
constexpr int IDC_ADB = 1001;
constexpr int IDC_PRODUCER = 1002;
constexpr int IDC_CONFIG = 1003;
constexpr int IDC_VFS = 1004;
constexpr int IDC_SERIAL = 1005;
constexpr int IDC_FPS = 1006;
constexpr int IDC_QUALITY = 1007;
constexpr int IDC_RAM_MB = 1008;
constexpr int IDC_SCALE = 1009;
constexpr int IDC_START = 1101;
constexpr int IDC_STOP = 1102;
constexpr int IDC_ROTATE = 1103;
constexpr int IDC_BROWSE_ADB = 1201;
constexpr int IDC_BROWSE_PRODUCER = 1202;
constexpr int IDC_BROWSE_CONFIG = 1203;
constexpr int IDC_STATUS = 1301;

struct GuiState {
  HWND window = nullptr;
  HWND adb = nullptr;
  HWND producer = nullptr;
  HWND config = nullptr;
  HWND vfs = nullptr;
  HWND serial = nullptr;
  HWND fps = nullptr;
  HWND quality = nullptr;
  HWND ram_mb = nullptr;
  HWND scale = nullptr;
  HWND start = nullptr;
  HWND stop = nullptr;
  HWND rotate = nullptr;
  HWND browse_adb = nullptr;
  HWND browse_producer = nullptr;
  HWND browse_config = nullptr;
  HWND status = nullptr;
  std::atomic<bool> running{false};
  std::atomic<bool> stop_requested{false};
  std::mutex process_mutex;
  HANDLE child_process = nullptr;
  std::thread worker;
  int rotation = 0;
};

GuiState g_gui;

std::wstring ModulePath() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (!length) return {};
    if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
    buffer.resize(buffer.size() * 2);
  }
}

std::wstring ModuleDirectory() {
  const fs::path path(ModulePath());
  return path.parent_path().wstring();
}

bool Exists(const std::wstring& path) {
  std::error_code error;
  return fs::exists(fs::path(path), error);
}

std::wstring EnvironmentValue(const wchar_t* name) {
  const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
  if (!size) return {};
  std::wstring value(size - 1, L'\0');
  GetEnvironmentVariableW(name, value.data(), size);
  return value;
}

std::wstring NormalizedPath(const fs::path& path) {
  std::error_code error;
  const fs::path absolute = fs::absolute(path, error);
  if (error) return path.wstring();
  return absolute.lexically_normal().wstring();
}

std::wstring BeamNgConfigPath(const fs::path& base) {
  const fs::path candidate = base / L"temp" / L"beam_auto_android_frame_probe.json";
  return Exists(candidate.wstring()) ? NormalizedPath(candidate) : std::wstring();
}

std::wstring FindBeamNgConfig() {
  const std::wstring explicit_config = EnvironmentValue(L"BEAMNG_FRAME_CONFIG");
  if (!explicit_config.empty() && Exists(explicit_config)) return NormalizedPath(explicit_config);

  const std::vector<std::wstring> user_environment_names = {
      L"BEAMNG_USER_PATH", L"BEAMNG_USER_DATA", L"BEAMNG_HOME"};
  for (const auto& name : user_environment_names) {
    const std::wstring value = EnvironmentValue(name.c_str());
    if (!value.empty()) {
      const std::wstring found = BeamNgConfigPath(fs::path(value));
      if (!found.empty()) return found;
    }
  }

  // Check the current directory and its parents first. This covers launching
  // the GUI from the repository or a local BeamNG tools directory.
  fs::path current = fs::current_path();
  for (int level = 0; level < 6 && !current.empty(); ++level) {
    const std::vector<fs::path> candidates = {
        current,
        current / L"BeamNGUSER" / L"current",
        current / L"BeamNG.drive" / L"current",
    };
    for (const auto& candidate : candidates) {
      const std::wstring found = BeamNgConfigPath(candidate);
      if (!found.empty()) return found;
    }
    const fs::path parent = current.parent_path();
    if (parent == current) break;
    current = parent;
  }

  // BeamNG custom user folders are commonly placed directly below a drive
  // root. Enumerate only root-level directories, avoiding an unbounded scan
  // through user files while not assuming a drive letter or account name.
  const DWORD drives = GetLogicalDrives();
  for (int index = 0; index < 26; ++index) {
    if ((drives & (1u << index)) == 0) continue;
    const fs::path root(std::wstring(1, static_cast<wchar_t>(L'A' + index)) + L":\\");
    const std::vector<fs::path> direct_candidates = {
        root / L"BeamNGUSER" / L"current",
        root / L"BeamNG.drive" / L"current",
    };
    for (const auto& candidate : direct_candidates) {
      const std::wstring found = BeamNgConfigPath(candidate);
      if (!found.empty()) return found;
    }
    std::error_code error;
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::directory_iterator(); it.increment(error)) {
      if (!it->is_directory(error)) continue;
      const std::wstring found = BeamNgConfigPath(it->path() / L"current");
      if (!found.empty()) return found;
    }
  }
  return {};
}

std::wstring GetControlText(HWND control) {
  const int length = GetWindowTextLengthW(control);
  std::wstring text(length, L'\0');
  if (length) GetWindowTextW(control, text.data(), length + 1);
  return text;
}

void SetControlText(HWND control, const std::wstring& value) {
  SetWindowTextW(control, value.c_str());
}

void PostStatus(const std::wstring& text) {
  if (!g_gui.window) return;
  PostMessageW(g_gui.window, WM_APP_STATUS,
               0, reinterpret_cast<LPARAM>(new std::wstring(text)));
}

std::wstring QuoteArg(const std::wstring& value) {
  if (value.empty()) return L"\"\"";
  bool quote = false;
  for (wchar_t c : value) {
    if (c == L' ' || c == L'\t' || c == L'"') {
      quote = true;
      break;
    }
  }
  if (!quote) return value;
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t c : value) {
    if (c == L'\\') {
      ++slashes;
    } else if (c == L'"') {
      result.append(slashes * 2 + 1, L'\\');
      result += L'"';
      slashes = 0;
    } else {
      result.append(slashes, L'\\');
      result += c;
      slashes = 0;
    }
  }
  result.append(slashes * 2, L'\\');
  result += L'"';
  return result;
}

bool RunProcess(const std::wstring& application, const std::wstring& arguments,
                DWORD* exit_code = nullptr) {
  std::wstring command = QuoteArg(application);
  if (!arguments.empty()) command += L" " + arguments;
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(application.c_str(), mutable_command.data(), nullptr, nullptr,
                      FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &process)) {
    return false;
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (exit_code) *exit_code = code;
  return code == 0;
}

bool LaunchHidden(const std::wstring& application, const std::wstring& arguments,
                  const std::wstring& working_directory, PROCESS_INFORMATION* process) {
  std::wstring command = QuoteArg(application);
  if (!arguments.empty()) command += L" " + arguments;
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  ZeroMemory(process, sizeof(*process));
  return CreateProcessW(application.c_str(), mutable_command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr,
                        working_directory.empty() ? nullptr : working_directory.c_str(),
                        &startup, process) != FALSE;
}

struct ControlCommand {
  enum class Type { Tap, Swipe } type = Type::Tap;
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
  int duration_ms = 0;
};

class HttpControlBridge {
 public:
  bool Start(const std::wstring& adb, const std::wstring& serial) {
    adb_ = adb;
    serial_ = serial;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_) != 0) return false;
    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
      WSACleanup();
      return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(8766);
    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listen_socket_, 8) == SOCKET_ERROR) {
      closesocket(listen_socket_);
      listen_socket_ = INVALID_SOCKET;
      WSACleanup();
      return false;
    }
    running_.store(true);
    accept_thread_ = std::thread(&HttpControlBridge::AcceptLoop, this);
    worker_thread_ = std::thread(&HttpControlBridge::WorkerLoop, this);
    return true;
  }

  void Stop() {
    if (!running_.exchange(false)) return;
    if (listen_socket_ != INVALID_SOCKET) {
      closesocket(listen_socket_);
      listen_socket_ = INVALID_SOCKET;
    }
    queue_cv_.notify_all();
    if (accept_thread_.joinable()) accept_thread_.join();
    if (worker_thread_.joinable()) worker_thread_.join();
    WSACleanup();
  }

  ~HttpControlBridge() { Stop(); }

 private:
  static int Parameter(const std::string& request, const char* name, int fallback = 0) {
    const std::string key = std::string(name) + "=";
    const size_t start = request.find(key);
    if (start == std::string::npos) return fallback;
    size_t end = request.find_first_of("& \r\n", start + key.size());
    const std::string value = request.substr(start + key.size(), end - start - key.size());
    char* parse_end = nullptr;
    const long parsed = std::strtol(value.c_str(), &parse_end, 10);
    if (!parse_end || *parse_end || parsed < 0 || parsed > 100000000) return fallback;
    return static_cast<int>(parsed);
  }

  void AcceptLoop() {
    while (running_.load()) {
      SOCKET client = accept(listen_socket_, nullptr, nullptr);
      if (client == INVALID_SOCKET) break;
      DWORD timeout = 100;
      setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&timeout), sizeof(timeout));
      char buffer[2048]{};
      const int received = recv(client, buffer, sizeof(buffer) - 1, 0);
      if (received > 0) {
        buffer[received] = '\0';
        const std::string request(buffer);
        ControlCommand command;
        if (request.find("GET /tap?") != std::string::npos) {
          command.type = ControlCommand::Type::Tap;
          command.x1 = Parameter(request, "x");
          command.y1 = Parameter(request, "y");
          Enqueue(command);
        } else if (request.find("GET /swipe?") != std::string::npos) {
          command.type = ControlCommand::Type::Swipe;
          command.x1 = Parameter(request, "x1");
          command.y1 = Parameter(request, "y1");
          command.x2 = Parameter(request, "x2");
          command.y2 = Parameter(request, "y2");
          command.duration_ms = std::clamp(Parameter(request, "duration", 120), 1, 2000);
          Enqueue(command);
        }
        const char response[] = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
        send(client, response, static_cast<int>(sizeof(response) - 1), 0);
      }
      closesocket(client);
    }
  }

  void Enqueue(const ControlCommand& command) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (queue_.size() >= 32) queue_.pop_front();
    queue_.push_back(command);
    queue_cv_.notify_one();
  }

  void WorkerLoop() {
    while (running_.load() || !queue_.empty()) {
      ControlCommand command;
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                           [this] { return !running_.load() || !queue_.empty(); });
        if (queue_.empty()) continue;
        command = queue_.front();
        queue_.pop_front();
      }
      std::wstring arguments;
      if (!serial_.empty()) arguments += L"-s " + QuoteArg(serial_) + L" ";
      arguments += L"shell input ";
      if (command.type == ControlCommand::Type::Tap) {
        arguments += L"tap " + std::to_wstring(command.x1) + L" " + std::to_wstring(command.y1);
      } else {
        arguments += L"swipe " + std::to_wstring(command.x1) + L" " + std::to_wstring(command.y1) +
                     L" " + std::to_wstring(command.x2) + L" " + std::to_wstring(command.y2) +
                     L" " + std::to_wstring(command.duration_ms);
      }
      RunProcess(adb_, arguments);
    }
  }

  WSADATA wsa_{};
  SOCKET listen_socket_ = INVALID_SOCKET;
  std::atomic<bool> running_{false};
  std::wstring adb_;
  std::wstring serial_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<ControlCommand> queue_;
  std::thread accept_thread_;
  std::thread worker_thread_;
};

class AudioBridge {
 public:
  bool Start(const std::wstring& scrcpy, const std::wstring& serial) {
    if (scrcpy.empty()) return false;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_) != 0) return false;
    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
      WSACleanup();
      return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(8767);
    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listen_socket_, 8) == SOCKET_ERROR) {
      closesocket(listen_socket_);
      listen_socket_ = INVALID_SOCKET;
      WSACleanup();
      return false;
    }
    pipe_name_ = L"\\\\.\\pipe\\beam_auto_android_audio_" + std::to_wstring(GetCurrentProcessId());
    pipe_ = CreateNamedPipeW(pipe_name_.c_str(), PIPE_ACCESS_INBOUND,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                             1, 1 << 16, 1 << 16, 0, nullptr);
    if (pipe_ == INVALID_HANDLE_VALUE) {
      CleanupSockets();
      return false;
    }
    std::wstring args;
    if (!serial.empty()) args += L"-s " + QuoteArg(serial) + L" ";
    args += L"--no-video --no-window --no-audio-playback --no-control"
        L" --audio-codec=raw --record " + QuoteArg(pipe_name_) +
        L" --record-format=wav";
    if (!LaunchHidden(scrcpy, args, fs::path(scrcpy).parent_path().wstring(), &process_)) {
      CloseHandle(pipe_);
      pipe_ = INVALID_HANDLE_VALUE;
      CleanupSockets();
      return false;
    }
    running_.store(true);
    accept_thread_ = std::thread(&AudioBridge::AcceptLoop, this);
    stream_thread_ = std::thread(&AudioBridge::StreamLoop, this);
    return true;
  }

  void Stop() {
    if (!running_.exchange(false)) return;
    if (listen_socket_ != INVALID_SOCKET) {
      closesocket(listen_socket_);
      listen_socket_ = INVALID_SOCKET;
    }
    if (stream_thread_.joinable()) {
      const HANDLE stream_handle = reinterpret_cast<HANDLE>(
          static_cast<uintptr_t>(stream_thread_.native_handle()));
      CancelSynchronousIo(stream_handle);
    }
    if (pipe_ != INVALID_HANDLE_VALUE) {
      DisconnectNamedPipe(pipe_);
      CloseHandle(pipe_);
      pipe_ = INVALID_HANDLE_VALUE;
    }
    if (process_.hProcess) {
      TerminateProcess(process_.hProcess, 130);
      WaitForSingleObject(process_.hProcess, 3000);
    }
    if (accept_thread_.joinable()) accept_thread_.join();
    if (stream_thread_.joinable()) stream_thread_.join();
    if (process_.hThread) CloseHandle(process_.hThread);
    if (process_.hProcess) CloseHandle(process_.hProcess);
    process_ = {};
    CleanupSockets();
  }

  ~AudioBridge() { Stop(); }

 private:
  bool ReadExact(void* destination, DWORD size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    DWORD total = 0;
    while (total < size && running_.load()) {
      DWORD read = 0;
      if (!ReadFile(pipe_, bytes + total, size - total, &read, nullptr) || read == 0) return false;
      total += read;
    }
    return total == size;
  }

  void AcceptLoop() {
    while (running_.load()) {
      SOCKET client = accept(listen_socket_, nullptr, nullptr);
      if (client == INVALID_SOCKET) break;
      DWORD timeout = 20;
      setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&timeout), sizeof(timeout));
      std::lock_guard<std::mutex> lock(clients_mutex_);
      const std::vector<uint8_t> header = audio_header_;
      if (!header.empty()) {
        if (!SendAll(client, header.data(), header.size())) {
          closesocket(client);
          continue;
        }
      }
      clients_.push_back(client);
    }
  }

  void Broadcast(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto it = clients_.begin(); it != clients_.end();) {
      if (!SendAll(*it, data, size)) {
        closesocket(*it);
        it = clients_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void PublishAudioFormat(const std::vector<uint8_t>& header) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    audio_header_ = header;
    for (auto it = clients_.begin(); it != clients_.end();) {
      if (!SendAll(*it, header.data(), header.size())) {
        closesocket(*it);
        it = clients_.erase(it);
      } else {
        ++it;
      }
    }
  }

  static bool SendAll(SOCKET client, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
      const int request = static_cast<int>(std::min<size_t>(size - offset, 1u << 15));
      const int sent = send(client, reinterpret_cast<const char*>(data + offset), request, 0);
      if (sent <= 0) return false;
      offset += static_cast<size_t>(sent);
    }
    return true;
  }

  void StreamLoop() {
    if (ConnectNamedPipe(pipe_, nullptr) == FALSE && GetLastError() != ERROR_PIPE_CONNECTED) return;
    uint8_t riff_header[12]{};
    if (!ReadExact(riff_header, sizeof(riff_header))) return;
    while (running_.load()) {
      uint8_t chunk_header[8]{};
      if (!ReadExact(chunk_header, sizeof(chunk_header))) return;
      const uint32_t chunk_size = static_cast<uint32_t>(chunk_header[4]) |
          (static_cast<uint32_t>(chunk_header[5]) << 8) |
          (static_cast<uint32_t>(chunk_header[6]) << 16) |
          (static_cast<uint32_t>(chunk_header[7]) << 24);
      if (std::memcmp(chunk_header, "data", 4) == 0) {
        bool have_audio_header = false;
        {
          std::lock_guard<std::mutex> lock(clients_mutex_);
          have_audio_header = !audio_header_.empty();
        }
        if (!have_audio_header) {
          // The WAV writer should always provide a fmt chunk. Keep a
          // conservative fallback so a malformed stream cannot make the Lua
          // consumer interpret the first PCM bytes as a format header.
          PublishAudioFormat({'B', 'A', 'U', '1', 0x80, 0xbb, 0x00, 0x00,
                              0x02, 0x00, 0x10, 0x00});
        }
        std::vector<uint8_t> buffer(1 << 15);
        uint64_t remaining = chunk_size == 0xffffffffu ? UINT64_MAX : chunk_size;
        while (running_.load() && remaining > 0) {
          const DWORD request = static_cast<DWORD>(std::min<uint64_t>(buffer.size(), remaining));
          DWORD read = 0;
          if (!ReadFile(pipe_, buffer.data(), request, &read, nullptr) || read == 0) return;
          Broadcast(buffer.data(), read);
          if (remaining != UINT64_MAX) remaining -= read;
        }
        return;
      }
      if (std::memcmp(chunk_header, "fmt ", 4) == 0 && chunk_size >= 16) {
        if (chunk_size > (1u << 20)) return;
        std::vector<uint8_t> fmt(chunk_size);
        if (!ReadExact(fmt.data(), chunk_size)) return;
        const uint32_t sample_rate = static_cast<uint32_t>(fmt[4]) |
            (static_cast<uint32_t>(fmt[5]) << 8) |
            (static_cast<uint32_t>(fmt[6]) << 16) |
            (static_cast<uint32_t>(fmt[7]) << 24);
        const uint16_t channels = static_cast<uint16_t>(fmt[2]) |
            (static_cast<uint16_t>(fmt[3]) << 8);
        const uint16_t bits = static_cast<uint16_t>(fmt[14]) |
            (static_cast<uint16_t>(fmt[15]) << 8);
        if (sample_rate > 0 && channels > 0 && channels <= 8 && bits == 16) {
          std::vector<uint8_t> header = {
              'B', 'A', 'U', '1',
              static_cast<uint8_t>(sample_rate & 0xff),
              static_cast<uint8_t>((sample_rate >> 8) & 0xff),
              static_cast<uint8_t>((sample_rate >> 16) & 0xff),
              static_cast<uint8_t>((sample_rate >> 24) & 0xff),
              static_cast<uint8_t>(channels & 0xff),
              static_cast<uint8_t>((channels >> 8) & 0xff),
              static_cast<uint8_t>(bits & 0xff),
              static_cast<uint8_t>((bits >> 8) & 0xff)};
          PublishAudioFormat(header);
        }
        if (chunk_size & 1) {
          uint8_t padding = 0;
          if (!ReadExact(&padding, 1)) return;
        }
        continue;
      }
      if (chunk_size == 0) continue;
      std::vector<uint8_t> discard(std::min<uint32_t>(chunk_size, 1u << 20));
      uint32_t remaining = chunk_size;
      while (remaining > 0 && running_.load()) {
        const DWORD request = std::min<uint32_t>(remaining, discard.size());
        DWORD read = 0;
        if (!ReadFile(pipe_, discard.data(), request, &read, nullptr) || read == 0) return;
        remaining -= read;
      }
      if (chunk_size & 1) {
        uint8_t padding = 0;
        if (!ReadExact(&padding, 1)) return;
      }
    }
  }

  void CleanupSockets() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (SOCKET client : clients_) closesocket(client);
    clients_.clear();
    WSACleanup();
  }

  WSADATA wsa_{};
  SOCKET listen_socket_ = INVALID_SOCKET;
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION process_{};
  std::wstring pipe_name_;
  std::atomic<bool> running_{false};
  std::mutex clients_mutex_;
  std::vector<SOCKET> clients_;
  std::vector<uint8_t> audio_header_;
  std::thread accept_thread_;
  std::thread stream_thread_;
};

HttpControlBridge g_control_bridge;
AudioBridge g_audio_bridge;

std::wstring FindExecutable(const wchar_t* name) {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = SearchPathW(nullptr, name, nullptr,
                                     static_cast<DWORD>(buffer.size()),
                                     buffer.data(), nullptr);
    if (!length) return {};
    if (length < buffer.size()) return std::wstring(buffer.data(), length);
    buffer.resize(length + 1);
  }
}

std::wstring FindSibling(const std::wstring& name) {
  const fs::path here(ModuleDirectory());
  const std::vector<fs::path> candidates = {
      here / name,
      here / L".." / L".." / L".." / L".." / name,
      here / L".." / name,
      here / L".." / L".." / name,
      here / L".." / L".." / L".." / name,
  };
  for (const auto& candidate : candidates) {
    std::error_code error;
    const fs::path absolute = fs::absolute(candidate, error);
    if (!error && fs::is_regular_file(absolute, error)) return absolute.lexically_normal().wstring();
  }
  return {};
}

bool IsAdministrator() {
  BOOL is_admin = FALSE;
  PSID administrators = nullptr;
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
  if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                               &administrators)) {
    CheckTokenMembership(nullptr, administrators, &is_admin);
    FreeSid(administrators);
  }
  return is_admin == TRUE;
}

bool ParseRange(HWND control, int minimum, int maximum, int* value) {
  const std::wstring text = GetControlText(control);
  wchar_t* end = nullptr;
  const long parsed = std::wcstol(text.c_str(), &end, 10);
  if (text.empty() || !end || *end || parsed < minimum || parsed > maximum) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

std::wstring NewGuidString() {
  GUID guid{};
  if (CoCreateGuid(&guid) != S_OK) return L"session";
  wchar_t buffer[64]{};
  StringFromGUID2(guid, buffer, 64);
  std::wstring result(buffer);
  result.erase(std::remove(result.begin(), result.end(), L'{'), result.end());
  result.erase(std::remove(result.begin(), result.end(), L'}'), result.end());
  return result;
}

std::wstring MakeSessionVfsPath(const std::wstring& base, const std::wstring& session) {
  if (base.rfind(L"/temp/", 0) != 0) return {};
  const size_t slash = base.find_last_of(L'/');
  if (slash == std::wstring::npos || slash <= 6) return {};
  return base.substr(0, slash) + L"_" + session + base.substr(slash);
}

std::wstring MakeSessionVfsMetadataPath(const std::wstring& frame_path) {
  const size_t slash = frame_path.find_last_of(L'/');
  if (slash == std::wstring::npos) return {};
  return frame_path.substr(0, slash) + L"/android_frame_state.json";
}

std::wstring MakeSessionJunction(const std::wstring& config_path,
                                 const std::wstring& vfs_frame_path) {
  const size_t prefix = 6;  // /temp/
  const size_t slash = vfs_frame_path.find_last_of(L'/');
  if (vfs_frame_path.rfind(L"/temp/", 0) != 0 || slash <= prefix) return {};
  fs::path config(config_path);
  fs::path directory = config.parent_path();
  std::wstring relative = vfs_frame_path.substr(prefix, slash - prefix);
  std::replace(relative.begin(), relative.end(), L'/', L'\\');
  return (directory / relative).wstring();
}

std::wstring ChooseDrive() {
  const DWORD drives = GetLogicalDrives();
  for (int index = 25; index >= 3; --index) {
    if ((drives & (1u << index)) == 0) {
      wchar_t letter = static_cast<wchar_t>(L'A' + index);
      return std::wstring(1, letter) + L":\\";
    }
  }
  return {};
}

bool CreateJunction(const std::wstring& junction, const std::wstring& target) {
  const fs::path parent = fs::path(junction).parent_path();
  std::error_code error;
  fs::create_directories(parent, error);
  if (error) return false;
  const std::wstring args = L"/c mklink /J " + QuoteArg(junction) + L" " + QuoteArg(target);
  return RunProcess(L"C:\\Windows\\System32\\cmd.exe", args);
}

void RemoveJunction(const std::wstring& junction) {
  std::error_code error;
  fs::remove(fs::path(junction), error);
}

bool AttachRamDisk(const std::wstring& imdisk, const std::wstring& mount, int size_mb) {
  const std::wstring args = L"-a -t vm -s " + std::to_wstring(size_mb) +
                            L"M -m " + QuoteArg(mount) +
                            L" -p \"/fs:ntfs /q /y\"";
  return RunProcess(imdisk, args);
}

void DetachRamDisk(const std::wstring& imdisk, const std::wstring& mount) {
  RunProcess(imdisk, L"-D -m " + QuoteArg(mount));
}

void StopNativeChild() {
  std::lock_guard<std::mutex> lock(g_gui.process_mutex);
  if (g_gui.child_process) {
    TerminateProcess(g_gui.child_process, 130);
  }
}

void MarkConfigStopped(const std::wstring& config_path) {
  std::ifstream input(fs::path(config_path), std::ios::binary);
  if (!input) return;
  std::string content((std::istreambuf_iterator<char>(input)), {});
  const std::string running = "\"status\":\"running\"";
  const std::string stopped = "\"status\":\"stopped\"";
  const size_t position = content.find(running);
  if (position == std::string::npos) return;
  content.replace(position, running.size(), stopped);
  const fs::path temporary = fs::path(config_path).wstring() + L".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return;
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
  for (int attempt = 0; attempt < 40; ++attempt) {
    if (MoveFileExW(temporary.c_str(), config_path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      return;
    }
    const DWORD error = GetLastError();
    if (error != ERROR_ACCESS_DENIED &&
        error != ERROR_SHARING_VIOLATION &&
        error != ERROR_LOCK_VIOLATION) {
      break;
    }
    Sleep(25);
  }

  // The game may keep the handshake open briefly. If atomic replacement is
  // still blocked, publish a complete stopped JSON directly rather than leave
  // a stale running state for the next Lua reload.
  HANDLE file = CreateFileW(config_path.c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    DeleteFileW(temporary.c_str());
    return;
  }
  DWORD written = 0;
  WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
  DeleteFileW(temporary.c_str());
}

void Worker(std::wstring adb, std::wstring producer, std::wstring config,
            std::wstring vfs_base, std::wstring serial, int fps, int quality,
            int ram_mb, const std::wstring& scale) {
  const std::wstring imdisk = FindExecutable(L"imdisk.exe");
  if (imdisk.empty()) {
    PostStatus(L"ERROR: imdisk.exe not found in PATH");
    g_gui.running.store(false);
    PostMessageW(g_gui.window, WM_APP_FINISHED, 0, 0);
    return;
  }
  const std::wstring session = NewGuidString();
  const std::wstring vfs_frame = MakeSessionVfsPath(vfs_base, session);
  const std::wstring vfs_metadata = MakeSessionVfsMetadataPath(vfs_frame);
  const std::wstring junction = MakeSessionJunction(config, vfs_frame);
  const std::wstring mount = ChooseDrive();
  if (vfs_frame.empty() || vfs_metadata.empty() || junction.empty() || mount.empty()) {
    PostStatus(L"ERROR: invalid VFS path or no free drive letter");
    g_gui.running.store(false);
    PostMessageW(g_gui.window, WM_APP_FINISHED, 0, 0);
    return;
  }

  PostStatus(L"Attaching ImDisk RAM...");
  bool attached = false;
  bool junction_created = false;
  PROCESS_INFORMATION child{};
  try {
    if (!AttachRamDisk(imdisk, mount, ram_mb)) throw std::runtime_error("ImDisk attach failed");
    attached = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    if (g_gui.stop_requested.load()) throw std::runtime_error("session cancelled");
    if (!CreateJunction(junction, mount)) throw std::runtime_error("junction creation failed");
    junction_created = true;
    if (g_gui.stop_requested.load()) throw std::runtime_error("session cancelled");

    if (!g_control_bridge.Start(adb, serial)) {
      PostStatus(L"WARNING: control bridge 8766 unavailable");
    }
    const std::wstring scrcpy = FindSibling(L"scrcpy-win64-v5.0.1\\scrcpy.exe");
    if (!scrcpy.empty()) {
      if (!g_audio_bridge.Start(scrcpy, serial)) {
        PostStatus(L"WARNING: audio bridge 8767 unavailable");
      }
    } else {
      PostStatus(L"WARNING: scrcpy.exe or serial unavailable; audio disabled");
    }

    const fs::path producer_dir = fs::path(producer).parent_path();
    const std::wstring frame = (fs::path(mount) / L"android_frame.jpg").wstring();
    const std::wstring metadata = (fs::path(mount) / L"android_frame_state.json").wstring();
    std::wstring command = QuoteArg(producer) + L" --adb " + QuoteArg(adb) +
        L" --frame-path " + QuoteArg(frame) +
        L" --metadata-path " + QuoteArg(metadata) +
        L" --config-path " + QuoteArg(config) +
        L" --vfs-frame-path " + QuoteArg(vfs_frame) +
        L" --vfs-metadata-path " + QuoteArg(vfs_metadata) +
        L" --session-id " + QuoteArg(session) +
        L" --fps " + std::to_wstring(fps) +
        L" --jpeg-quality " + std::to_wstring(quality) +
        L" --scale " + QuoteArg(scale) +
        L" --max-frames 0";
    if (!serial.empty()) command += L" --serial " + QuoteArg(serial);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    const std::wstring old_path = [] {
      DWORD size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
      std::wstring value(size ? size - 1 : 0, L'\0');
      if (size) GetEnvironmentVariableW(L"PATH", value.data(), size);
      return value;
    }();
    SetEnvironmentVariableW(L"PATH", (L"C:\\msys64\\ucrt64\\bin;C:\\msys64\\usr\\bin;" + old_path).c_str());
    if (!CreateProcessW(producer.c_str(), mutable_command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, producer_dir.c_str(),
                        &startup, &child)) {
      SetEnvironmentVariableW(L"PATH", old_path.c_str());
      throw std::runtime_error("native producer launch failed");
    }
    SetEnvironmentVariableW(L"PATH", old_path.c_str());
    {
      std::lock_guard<std::mutex> lock(g_gui.process_mutex);
      g_gui.child_process = child.hProcess;
    }
    CloseHandle(child.hThread);
    PostStatus(L"Running session " + session + L" at " + std::to_wstring(fps) + L" FPS");
    WaitForSingleObject(child.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(child.hProcess, &exit_code);
    {
      std::lock_guard<std::mutex> lock(g_gui.process_mutex);
      g_gui.child_process = nullptr;
    }
    CloseHandle(child.hProcess);
    child.hProcess = nullptr;
    MarkConfigStopped(config);
    PostStatus(exit_code == 0 ? L"Stopped" : L"Producer stopped with code " + std::to_wstring(exit_code));
  } catch (const std::exception& error) {
    const std::wstring reason = error.what() == std::string("session cancelled")
                                    ? L"Session cancelled"
                                    : L"ERROR: native session setup failed";
    PostStatus(reason);
    if (child.hProcess) {
      TerminateProcess(child.hProcess, 130);
      WaitForSingleObject(child.hProcess, 5000);
      {
        std::lock_guard<std::mutex> lock(g_gui.process_mutex);
        g_gui.child_process = nullptr;
      }
      CloseHandle(child.hProcess);
      child.hProcess = nullptr;
    }
  }
  if (junction_created) RemoveJunction(junction);
  g_audio_bridge.Stop();
  g_control_bridge.Stop();
  if (attached) DetachRamDisk(imdisk, mount);
  // Publish the terminal state after all child-process and staging cleanup.
  // This also repairs a running handshake if Lua had the file open during the
  // first replacement attempt.
  MarkConfigStopped(config);
  g_gui.running.store(false);
  PostMessageW(g_gui.window, WM_APP_FINISHED, 0, 0);
}

void BrowseFile(HWND owner, HWND target, const wchar_t* filter) {
  wchar_t path[MAX_PATH * 4]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFile = path;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path));
  dialog.lpstrFilter = filter;
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (GetOpenFileNameW(&dialog)) SetControlText(target, path);
}

HWND AddLabel(HWND parent, const wchar_t* text, int x, int y, int width = 150) {
  return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, 22,
                       parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND AddEdit(HWND parent, int id, const std::wstring& text, int x, int y, int width) {
  HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y,
                              width, 24, parent, reinterpret_cast<HMENU>(id),
                              GetModuleHandleW(nullptr), nullptr);
  SendMessageW(edit, WM_SETFONT,
               reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  return edit;
}

HWND AddButton(HWND parent, int id, const wchar_t* text, int x, int y, int width) {
  return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       x, y, width, 28, parent, reinterpret_cast<HMENU>(id),
                       GetModuleHandleW(nullptr), nullptr);
}

HWND AddScaleCombo(HWND parent, int id, int x, int y, int width) {
  HWND combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE |
                             CBS_DROPDOWNLIST | WS_TABSTOP, x, y, width, 120,
                             parent, reinterpret_cast<HMENU>(id),
                             GetModuleHandleW(nullptr), nullptr);
  const wchar_t* values[] = {L"0.25", L"0.5", L"0.75", L"1"};
  for (const auto* value : values) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
  }
  SendMessageW(combo, CB_SETCURSEL, 1, 0);
  return combo;
}

std::wstring GetScaleSelection(HWND control) {
  const int index = static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0));
  const wchar_t* values[] = {L"0.25", L"0.5", L"0.75", L"1"};
  if (index < 0 || index >= 4) return L"0.5";
  return values[index];
}

void InitializeDefaults() {
  const std::wstring module = ModuleDirectory();
  std::wstring adb = FindSibling(L"adb.exe");
  if (adb.empty()) {
    adb = FindSibling(L"scrcpy-win64-v5.0.1\\adb.exe");
  }
  std::wstring producer = FindSibling(L"adb_h264_jpeg_producer.exe");
  if (producer.empty()) producer = (fs::path(module) / L"adb_h264_jpeg_producer.exe").wstring();
  const std::wstring config = FindBeamNgConfig();
  SetControlText(g_gui.adb, adb);
  SetControlText(g_gui.producer, producer);
  SetControlText(g_gui.config, config);
  SetControlText(g_gui.vfs, L"/temp/beam_auto_android_frame_probe_ram/android_frame.jpg");
  SetControlText(g_gui.serial, L"");
  SetControlText(g_gui.fps, L"60");
  SetControlText(g_gui.quality, L"84");
  SetControlText(g_gui.ram_mb, L"256");
  SendMessageW(g_gui.scale, CB_SETCURSEL, 1, 0);
}

void StartSession() {
  if (g_gui.running.exchange(true)) return;
  if (!IsAdministrator()) {
    MessageBoxW(g_gui.window, L"Запустите launcher от имени администратора: нужен ImDisk.",
                L"Beam Auto Android", MB_ICONWARNING);
    g_gui.running.store(false);
    return;
  }
  const std::wstring adb = GetControlText(g_gui.adb);
  const std::wstring producer = GetControlText(g_gui.producer);
  const std::wstring config = GetControlText(g_gui.config);
  const std::wstring vfs = GetControlText(g_gui.vfs);
  const std::wstring serial = GetControlText(g_gui.serial);
  const std::wstring scale = GetScaleSelection(g_gui.scale);
  int fps = 0, quality = 0, ram_mb = 0;
  if (!Exists(adb) || !Exists(producer) || config.empty() ||
      !ParseRange(g_gui.fps, 1, 120, &fps) ||
      !ParseRange(g_gui.quality, 1, 100, &quality) ||
      !ParseRange(g_gui.ram_mb, 64, 4096, &ram_mb) ||
      vfs.rfind(L"/temp/", 0) != 0) {
    MessageBoxW(g_gui.window, L"Проверьте пути, FPS (1..120), quality, RAM MB и VFS path.",
                L"Beam Auto Android", MB_ICONERROR);
    g_gui.running.store(false);
    return;
  }
  EnableWindow(g_gui.start, FALSE);
  EnableWindow(g_gui.stop, TRUE);
  PostStatus(L"Starting...");
  g_gui.stop_requested.store(false);
  g_gui.worker = std::thread(Worker, adb, producer, config, vfs, serial,
                             fps, quality, ram_mb, scale);
}

void StopSession() {
  if (!g_gui.running.load()) return;
  g_gui.stop_requested.store(true);
  PostStatus(L"Stopping...");
  StopNativeChild();
}

void RotateDevice() {
  const std::wstring adb = GetControlText(g_gui.adb);
  const std::wstring serial = GetControlText(g_gui.serial);
  if (!Exists(adb)) {
    PostStatus(L"ERROR: ADB path is invalid");
    return;
  }

  const int target = g_gui.rotation == 0 ? 1 : 0;
  std::wstring arguments;
  if (!serial.empty()) arguments += L"-s " + QuoteArg(serial) + L" ";
  arguments += L"shell wm user-rotation lock " + std::to_wstring(target);
  DWORD exit_code = 1;
  if (!RunProcess(adb, arguments, &exit_code)) {
    PostStatus(L"ERROR: ADB rotation command failed (exit " +
               std::to_wstring(exit_code) + L")");
    return;
  }

  g_gui.rotation = target;
  SetControlText(g_gui.rotate, target == 0 ? L"Rotate 90" : L"Rotate 0");
  PostStatus(target == 0 ? L"Phone rotation locked at 0 degrees"
                         : L"Phone rotation locked at 90 degrees");
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE: {
      g_gui.window = window;
      AddLabel(window, L"ADB path", 20, 20);
      g_gui.adb = AddEdit(window, IDC_ADB, L"", 170, 18, 600);
      g_gui.browse_adb = AddButton(window, IDC_BROWSE_ADB, L"Browse", 780, 17, 90);
      AddLabel(window, L"Producer EXE", 20, 58);
      g_gui.producer = AddEdit(window, IDC_PRODUCER, L"", 170, 56, 600);
      g_gui.browse_producer = AddButton(window, IDC_BROWSE_PRODUCER, L"Browse", 780, 55, 90);
      AddLabel(window, L"BeamNG config", 20, 96);
      g_gui.config = AddEdit(window, IDC_CONFIG, L"", 170, 94, 600);
      g_gui.browse_config = AddButton(window, IDC_BROWSE_CONFIG, L"Browse", 780, 93, 90);
      AddLabel(window, L"VFS frame path", 20, 134);
      g_gui.vfs = AddEdit(window, IDC_VFS, L"", 170, 132, 700);
      AddLabel(window, L"Serial (optional)", 20, 172);
      g_gui.serial = AddEdit(window, IDC_SERIAL, L"", 170, 170, 240);
      AddLabel(window, L"FPS 1..120", 450, 172, 100);
      g_gui.fps = AddEdit(window, IDC_FPS, L"60", 555, 170, 70);
      AddLabel(window, L"JPEG quality", 645, 172, 100);
      g_gui.quality = AddEdit(window, IDC_QUALITY, L"84", 750, 170, 60);
      AddLabel(window, L"RAM MB", 20, 210);
      g_gui.ram_mb = AddEdit(window, IDC_RAM_MB, L"256", 170, 208, 100);
      AddLabel(window, L"Scale", 300, 210, 60);
      g_gui.scale = AddScaleCombo(window, IDC_SCALE, 360, 206, 100);
      g_gui.start = AddButton(window, IDC_START, L"Start", 480, 205, 130);
      g_gui.stop = AddButton(window, IDC_STOP, L"Stop", 625, 205, 130);
      g_gui.rotate = AddButton(window, IDC_ROTATE, L"Rotate 90", 770, 205, 100);
      EnableWindow(g_gui.stop, FALSE);
      AddLabel(window, L"Status", 20, 258);
      g_gui.status = AddEdit(window, IDC_STATUS, L"Ready", 170, 255, 700);
      SendMessageW(g_gui.status, EM_SETREADONLY, TRUE, 0);
      InitializeDefaults();
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case IDC_START: StartSession(); return 0;
        case IDC_STOP: StopSession(); return 0;
        case IDC_ROTATE: RotateDevice(); return 0;
        case IDC_BROWSE_ADB: BrowseFile(window, g_gui.adb, L"ADB executable\0adb.exe\0All files\0*.*\0"); return 0;
        case IDC_BROWSE_PRODUCER: BrowseFile(window, g_gui.producer, L"Producer executable\0*.exe\0All files\0*.*\0"); return 0;
        case IDC_BROWSE_CONFIG: BrowseFile(window, g_gui.config, L"JSON config\0*.json\0All files\0*.*\0"); return 0;
      }
      break;
    case WM_APP_STATUS: {
      auto* text = reinterpret_cast<std::wstring*>(lparam);
      SetControlText(g_gui.status, *text);
      delete text;
      return 0;
    }
    case WM_APP_FINISHED:
      if (g_gui.worker.joinable()) g_gui.worker.join();
      EnableWindow(g_gui.start, TRUE);
      EnableWindow(g_gui.stop, FALSE);
      return 0;
    case WM_CLOSE:
      StopSession();
      if (g_gui.worker.joinable()) g_gui.worker.join();
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  if (CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) != S_OK) return 1;
  const wchar_t class_name[] = L"BeamAutoAndroidMediaGui";
  WNDCLASSW window_class{};
  window_class.hInstance = instance;
  window_class.lpfnWndProc = WindowProc;
  window_class.lpszClassName = class_name;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&window_class);
  HWND window = CreateWindowExW(0, class_name, L"Beam Auto Android — ADB video",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               CW_USEDEFAULT, CW_USEDEFAULT, 920, 350, nullptr,
                               nullptr, instance, nullptr);
  if (!window) {
    CoUninitialize();
    return 1;
  }
  ShowWindow(window, show_command);
  UpdateWindow(window);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  CoUninitialize();
  return static_cast<int>(message.wParam);
}
