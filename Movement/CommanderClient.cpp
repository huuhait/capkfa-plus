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
        Log("ERROR", "Server URI not configured. Call SetConfig first.");
        return;
    }
    Log("DEBUG", "Starting CommanderClient...");
    _io_context.restart();
    boost::system::error_code ec;
    auto local_endpoint = _socket.local_endpoint(ec);
    if (!ec) {
        Log("DEBUG", "Socket bound to local port: " + std::to_string(local_endpoint.port()));
    } else {
        Log("ERROR", "Failed to get local endpoint: " + ec.message());
    }
    StartReceive();
    _thread = std::thread([this]() {
        Log("DEBUG", "CommanderClient thread running io_context");
        _io_context.run();
    });
}

void CommanderClient::Stop() {
    Log("DEBUG", "Stopping CommanderClient...");
    boost::system::error_code ec;
    _socket.cancel(ec);
    if (ec) Log("WARN", "Socket cancel failed: " + ec.message());
    _io_context.stop();
    if (_thread.joinable()) _thread.join();
    _socket.close(ec);
    if (ec) Log("WARN", "Socket close failed: " + ec.message());
    _is_configured = false;
    _server_endpoint = boost::asio::ip::udp::endpoint();
    _remote_endpoint = boost::asio::ip::udp::endpoint();
    _last_version.clear();
    _recv_buffer.fill(0);
    _button_states.clear();
    Log("DEBUG", "CommanderClient stopped and reset.");
}

void CommanderClient::Move(int16_t x, int16_t y) {
    if (!_is_configured) {
        Log("ERROR", "Server URI not configured.");
        return;
    }
    std::vector<uint8_t> data(5 + 4);
    data[0] = 1; // Method ID for Move
    uint32_t len = htonl(4); // Payload length
    std::memcpy(data.data() + 1, &len, 4);
    data[5] = static_cast<uint8_t>(x >> 8); data[6] = static_cast<uint8_t>(x);
    data[7] = static_cast<uint8_t>(y >> 8); data[8] = static_cast<uint8_t>(y);
    Send(data, 1, _move_timeout_ms);
}

void CommanderClient::Click() {
    if (!_is_configured) {
        Log("ERROR", "Server URI not configured.");
        return;
    }
    std::vector<uint8_t> data(5);
    data[0] = 2; // Method ID for Click
    uint32_t len = htonl(0); // Payload length
    std::memcpy(data.data() + 1, &len, 4);
    Send(data, 2, _click_timeout_ms);
}

std::string CommanderClient::Version() {
    if (!_is_configured) {
        Log("ERROR", "Server URI not configured.");
        return "";
    }
    std::vector<uint8_t> data(5);
    data[0] = 3; // Method ID for Version
    uint32_t len = htonl(0); // Payload length
    std::memcpy(data.data() + 1, &len, 4);
    Send(data, 3, _version_timeout_ms);
    return _last_version;
}

void CommanderClient::SubscribeButtonStates() {
    if (!_is_configured) {
        Log("ERROR", "Server URI not configured.");
        return;
    }
    std::vector<uint8_t> data(5);
    data[0] = 8; // Method ID for GetButtonStates
    uint32_t len = htonl(0); // Payload length
    std::memcpy(data.data() + 1, &len, 4);
    Send(data, 8, 0); // No timeout for subscription
}

void CommanderClient::SetButtonStateCallback(std::function<void(uint8_t, bool)> callback) {
    _button_state_callback = callback;
}

void CommanderClient::SetConfig(const ::capkfa::RemoteConfig& config) {
    Stop(); // Ensure clean state before reconfiguration
    std::string server_uri = config.mouse_server().uri();
    std::string ip;
    unsigned short port;
    if (!ParseServerUri(server_uri, ip, port)) {
        Log("ERROR", "Invalid server URI format: " + server_uri);
        return;
    }
    try {
        // Reinitialize socket
        _socket = boost::asio::ip::udp::socket(_io_context, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
        _server_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(ip), port);
        _is_configured = true;
        Log("DEBUG", "Server configured: " + ip + ":" + std::to_string(port));
        Start();
        SubscribeButtonStates(); // Auto-subscribe to button states
    } catch (const std::exception& e) {
        Log("ERROR", "Failed to configure server: " + std::string(e.what()));
        _is_configured = false;
    }
}

bool CommanderClient::ParseServerUri(const std::string& server_uri, std::string& ip, unsigned short& port) {
    size_t colon_pos = server_uri.find(':');
    if (colon_pos == std::string::npos) return false;

    ip = server_uri.substr(0, colon_pos);
    std::string port_str = server_uri.substr(colon_pos + 1);

    try {
        port = static_cast<unsigned short>(std::stoi(port_str));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void CommanderClient::Send(const std::vector<uint8_t>& data, uint8_t method_id, int timeout_ms) {
    std::unique_lock<std::mutex> lock(_mutex);
    _operation_complete = false;
    _operation_success = false;
    _current_method = method_id;

    _socket.async_send_to(
        boost::asio::buffer(data), _server_endpoint,
        [this, method_id](const boost::system::error_code& ec, std::size_t) {
            std::lock_guard<std::mutex> lock(_mutex);
            if (ec) {
                Log("ERROR", "Send failed for method " + std::to_string(method_id) + ": " + ec.message());
                _operation_complete = true;
                _cv.notify_one();
            }
            // Wait for server response in HandleReceive
        });

    if (timeout_ms > 0) {
        if (!_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return _operation_complete; })) {
            Log("WARN", "Timeout waiting for response to method " + std::to_string(method_id) + " after " + std::to_string(timeout_ms) + "ms");
            _operation_complete = true;
        }
    } else {
        _cv.wait(lock, [this] { return _operation_complete; });
    }

    if (!_operation_success && method_id != 8) {
        Log("ERROR", "Operation failed for method " + std::to_string(method_id));
    }
}

void CommanderClient::StartReceive() {
    _socket.async_receive_from(
        boost::asio::buffer(_recv_buffer), _remote_endpoint,
        [this](const boost::system::error_code& ec, std::size_t bytes_recvd) {
            HandleReceive(ec, bytes_recvd);
        });
}

void CommanderClient::HandleReceive(const boost::system::error_code& ec, std::size_t bytes_recvd) {
    if (ec) {
        // Ignore if socket was closed intentionally
        if (ec == boost::asio::error::operation_aborted || !_socket.is_open()) {
            Log("DEBUG", "Receive operation aborted or socket closed: " + ec.message());
        } else {
            Log("ERROR", "Receive error: " + ec.message());
            std::lock_guard<std::mutex> lock(_mutex);
            _operation_complete = true;
            _cv.notify_one();
        }
        if (_socket.is_open()) StartReceive();
        return;
    }

    // Check only server IP, allow any port
    if (_remote_endpoint.address() != _server_endpoint.address()) {
        if (_socket.is_open()) StartReceive();
        return;
    }

    if (bytes_recvd < 5) {
        if (_socket.is_open()) StartReceive();
        return;
    }

    uint8_t method = _recv_buffer[0];
    uint32_t payload_len;
    std::memcpy(&payload_len, _recv_buffer.data() + 1, 4);
    payload_len = ntohl(payload_len);

    // Log("DEBUG", "Received method=" + std::to_string(method) + ", payload_len=" + std::to_string(payload_len));

    std::unique_lock<std::mutex> lock(_mutex);
    if (method == _current_method || method == 8) {
        switch (method) {
            case 1: // Move response
                if (bytes_recvd == 2 && _recv_buffer[1] == 0xFF) {
                    Log("DEBUG", "Move acknowledged");
                    _operation_success = true;
                } else {
                    Log("WARN", "Invalid Move response");
                }
                _operation_complete = true;
                _cv.notify_one();
                break;
            case 2: // Click response
                if (bytes_recvd == 2 && _recv_buffer[1] == 0xFF) {
                    Log("DEBUG", "Click acknowledged");
                    _operation_success = true;
                } else {
                    Log("WARN", "Invalid Click response");
                }
                _operation_complete = true;
                _cv.notify_one();
                break;
            case 3: // Version response
                if (payload_len > 0 && bytes_recvd >= 5 + payload_len) {
                    _last_version.assign(reinterpret_cast<const char*>(_recv_buffer.data() + 5), payload_len);
                    Log("DEBUG", "Version: " + _last_version);
                    _operation_success = true;
                } else {
                    Log("WARN", "Invalid Version response");
                }
                _operation_complete = true;
                _cv.notify_one();
                break;
            case 8: // Button states
                if (payload_len % 2 == 0 && bytes_recvd >= 5 + payload_len) {
                    for (size_t i = 5; i < 5 + payload_len; i += 2) {
                        uint8_t id = _recv_buffer[i];
                        bool pressed = _recv_buffer[i + 1];
                        if (_button_states[id] != pressed) {
                            _button_states[id] = pressed;
                            if (_button_state_callback) {
                                _button_state_callback(id, pressed);
                            }
                        }
                    }
                } else {
                    Log("WARN", "Invalid Button states payload");
                }
                break;
            default:
                Log("WARN", "Unknown method: " + std::to_string(method));
                _operation_complete = true;
                _cv.notify_one();
        }
    } else {
        Log("WARN", "Received unexpected method: " + std::to_string(method) + ", expected: " + std::to_string(_current_method));
    }
    if (_socket.is_open()) StartReceive();
}

std::map<uint8_t, bool> CommanderClient::ButtonStates() {
    return _button_states;
}

void CommanderClient::Log(const std::string& level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count()
       << " [" << level << "] " << message;
    std::cout << ss.str() << std::endl;
}