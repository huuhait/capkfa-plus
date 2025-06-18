#ifndef COMMANDERCLIENT_H
#define COMMANDERCLIENT_H

#include <boost/asio.hpp>
#include <string>
#include <vector>
#include <array>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <map>
#include <unordered_map>
#include <future>
#include <atomic>
#include <license.grpc.pb.h>

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
    void SetButtonStateCallback(std::function<void(uint8_t, bool)> callback);
    std::map<uint8_t, bool> ButtonStates();

private:
    void StartReceiveLoop();
    void HandleButtonStateStream(const std::vector<uint8_t>& data);
    std::vector<uint8_t> SendRequest(uint8_t method, const std::vector<uint8_t>& payload, int timeout_ms);
    bool ParseServerUri(const std::string& server_uri, std::string& ip, unsigned short& port);
    void Log(const std::string& level, const std::string& message);

    boost::asio::io_context _io_context;
    boost::asio::ip::udp::socket _socket;
    boost::asio::ip::udp::endpoint _server_endpoint;
    boost::asio::ip::udp::endpoint _remote_endpoint;
    std::array<uint8_t, 5 + 1024> _recv_buffer;

    std::thread _recv_thread;
    std::thread _io_thread;
    std::atomic<bool> _running{false};

    std::mutex _pending_mutex;
    std::unordered_map<uint8_t, std::promise<std::vector<uint8_t>>> _pending_requests;

    std::map<uint8_t, bool> _button_states;
    std::function<void(uint8_t, bool)> _button_state_callback;

    std::string _last_version;
    bool _is_configured{false};
};

#endif
