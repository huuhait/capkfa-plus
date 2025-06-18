#include "CommanderClient.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

CommanderClient::CommanderClient()
    : _socket(_io_context, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)) {
    Log("DEBUG", "CommanderClient initialized");
}

CommanderClient::~CommanderClient() {
    Stop();
}

void CommanderClient::Start() {
    if (!_is_configured) {
        Log("ERROR", "Server URI not configured.");
        return;
    }

    _io_context.restart();
    _io_thread = std::thread([this]() { _io_context.run(); });
    StartReceiveLoop();
}

void CommanderClient::Stop() {
    _running = false;

    boost::system::error_code ec;
    _socket.cancel(ec);
    _socket.close(ec);

    if (_recv_thread.joinable()) _recv_thread.join();
    if (_io_thread.joinable()) _io_thread.join();
}

void CommanderClient::StartReceiveLoop() {
    _running = true;
    _recv_thread = std::thread([this]() {
        while (_running && _socket.is_open()) {
            boost::system::error_code ec;
            size_t len = _socket.receive_from(boost::asio::buffer(_recv_buffer), _remote_endpoint, 0, ec);
            if (ec) {
                if (!_running) break;
                Log("WARN", "Receive error: " + ec.message());
                continue;
            }

            if (len < 1) continue;

            uint8_t method = _recv_buffer[0];
            std::vector<uint8_t> data(_recv_buffer.begin(), _recv_buffer.begin() + len);

            if (method == 8) {
                HandleButtonStateStream(data);
            } else {
                std::lock_guard<std::mutex> lock(_pending_mutex);
                auto it = _pending_requests.find(method);
                if (it != _pending_requests.end()) {
                    it->second.set_value(std::move(data));
                    _pending_requests.erase(it);
                } else {
                    Log("DEBUG", "Ignored packet: method=" + std::to_string(method));
                }
            }
        }
    });
}

std::vector<uint8_t> CommanderClient::SendRequest(uint8_t method, const std::vector<uint8_t>& payload, int timeout_ms) {
    std::vector<uint8_t> buf(5 + payload.size());
    buf[0] = method;
    uint32_t len = htonl(payload.size());
    std::memcpy(buf.data() + 1, &len, 4);
    std::memcpy(buf.data() + 5, payload.data(), payload.size());

    std::promise<std::vector<uint8_t>> promise;
    auto future = promise.get_future();

    {
        std::lock_guard<std::mutex> lock(_pending_mutex);
        _pending_requests[method] = std::move(promise);
    }

    boost::system::error_code ec;
    _socket.send_to(boost::asio::buffer(buf), _server_endpoint, 0, ec);
    if (ec) {
        std::lock_guard<std::mutex> lock(_pending_mutex);
        _pending_requests.erase(method);
        Log("ERROR", "Send error: " + ec.message());
        return {};
    }

    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::timeout) {
        std::lock_guard<std::mutex> lock(_pending_mutex);
        _pending_requests.erase(method);
        Log("WARN", "Timeout waiting for method " + std::to_string(method));
        return {};
    }

    return future.get();
}

void CommanderClient::Move(int16_t x, int16_t y) {
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x),
        static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y)
    };
    auto resp = SendRequest(1, payload, 1000);
    if (resp.size() != 2 || resp[0] != 1 || resp[1] != 0xFF) {
        Log("WARN", "Invalid Move ACK");
    }
}

void CommanderClient::Click() {
    auto resp = SendRequest(2, {}, 220);
    if (resp.size() != 2 || resp[0] != 2 || resp[1] != 0xFF) {
        Log("WARN", "Invalid Click ACK");
    }
}

std::string CommanderClient::Version() {
    auto resp = SendRequest(3, {}, 300);
    if (resp.size() < 5 || resp[0] != 3) {
        Log("ERROR", "Invalid Version response");
        return "";
    }
    uint32_t len;
    std::memcpy(&len, &resp[1], 4);
    len = ntohl(len);
    if (resp.size() < 5 + len) return "";
    _last_version.assign(reinterpret_cast<const char*>(&resp[5]), len);
    return _last_version;
}

void CommanderClient::SubscribeButtonStates() {
    std::vector<uint8_t> data(5, 0);
    data[0] = 8; // Stream method
    boost::system::error_code ec;
    _socket.send_to(boost::asio::buffer(data), _server_endpoint, 0, ec);
    if (ec) Log("ERROR", "Failed to subscribe to button states: " + ec.message());
}

void CommanderClient::SetConfig(const ::capkfa::RemoteConfig& config) {
    Stop();

    std::string ip;
    unsigned short port;
    if (!ParseServerUri(config.mouse_server().uri(), ip, port)) {
        Log("ERROR", "Invalid server URI: " + config.mouse_server().uri());
        return;
    }

    try {
        _socket = boost::asio::ip::udp::socket(_io_context, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
        _server_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(ip), port);
        _is_configured = true;
        Start();
        SubscribeButtonStates();
    } catch (const std::exception& e) {
        Log("ERROR", "Failed to configure server: " + std::string(e.what()));
        _is_configured = false;
    }
}

bool CommanderClient::ParseServerUri(const std::string& server_uri, std::string& ip, unsigned short& port) {
    size_t colon = server_uri.find(':');
    if (colon == std::string::npos) return false;
    ip = server_uri.substr(0, colon);
    try {
        port = static_cast<unsigned short>(std::stoi(server_uri.substr(colon + 1)));
        return true;
    } catch (...) {
        return false;
    }
}

void CommanderClient::HandleButtonStateStream(const std::vector<uint8_t>& data) {
    if (data.size() < 5) return;
    uint32_t len;
    std::memcpy(&len, &data[1], 4);
    len = ntohl(len);
    if (len % 2 != 0 || data.size() < 5 + len) return;

    for (size_t i = 5; i + 1 < 5 + len; i += 2) {
        uint8_t id = data[i];
        bool pressed = data[i + 1];
        if (_button_states[id] != pressed) {
            _button_states[id] = pressed;
            Log("DEBUG", "Button " + std::to_string(id) + " state changed to " + (pressed ? "pressed" : "released"));
            if (_button_state_callback) {
                _button_state_callback(id, pressed);
            }
        }
    }
}

std::map<uint8_t, bool> CommanderClient::ButtonStates() {
    return _button_states;
}

void CommanderClient::SetButtonStateCallback(std::function<void(uint8_t, bool)> callback) {
    _button_state_callback = callback;
}

void CommanderClient::Log(const std::string& level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%F %T") << '.' << std::setw(3) << std::setfill('0') << ms.count()
       << " [" << level << "] " << message;
    std::cout << ss.str() << std::endl;
}
