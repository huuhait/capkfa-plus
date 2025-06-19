#pragma once

#include <boost/asio.hpp>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <future>
#include <chrono>
#include "proto/license.grpc.pb.h"

class CommanderClient {
public:
    CommanderClient();
    ~CommanderClient();

    void Start();
    void Stop();
    void Move(int16_t x, int16_t y);
    void Click();
    std::string Version();
    void SubscribeButtonStates();
    void SetConfig(const ::capkfa::RemoteConfig& config);
    std::map<uint8_t, bool> ButtonStates();
    void SetButtonStateCallback(std::function<void(uint8_t, bool)> callback);

private:
    void StartReceiveLoop();
    void StartPingLoop();
    void HandlePing();
    void HandlePong();
    void HandleButtonStateStream(const std::vector<uint8_t>& data);
    std::vector<uint8_t> SendRequest(uint8_t method, const std::vector<uint8_t>& payload, int timeout_ms);
    bool ParseServerUri(const std::string& server_uri, std::string& ip, unsigned short& port);
    void Log(const std::string& level, const std::string& message);

    boost::asio::io_context _io_context;
    boost::asio::ip::udp::socket _socket;
    boost::asio::ip::udp::endpoint _server_endpoint;
    boost::asio::ip::udp::endpoint _remote_endpoint;
    std::array<uint8_t, 5 + 1024> _recv_buffer;
    std::thread _io_thread;
    std::thread _recv_thread;
    std::thread _ping_thread;
    std::atomic<bool> _running{false};
    bool _is_configured{false};
    std::string _last_version;
    std::map<uint8_t, bool> _button_states;
    std::function<void(uint8_t, bool)> _button_state_callback;
    std::map<uint8_t, std::promise<std::vector<uint8_t>>> _pending_requests;
    std::mutex _pending_mutex;
    std::chrono::steady_clock::time_point _last_ping_time;
    bool _pong_received{false};
    std::mutex _ping_mutex;
};
