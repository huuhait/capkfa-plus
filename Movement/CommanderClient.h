#ifndef COMMANDERCLIENT_H
#define COMMANDERCLIENT_H

#include <boost/asio.hpp>
#include <string>
#include <vector>
#include <array>
#include <license.grpc.pb.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <map>

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
    void Send(const std::vector<uint8_t>& data, uint8_t method_id, int timeout_ms);
    void StartReceive();
    void HandleReceive(const boost::system::error_code& ec, std::size_t bytes_recvd);
    bool ParseServerUri(const std::string& server_uri, std::string& ip, unsigned short& port);
    void Log(const std::string& level, const std::string& message);

    boost::asio::io_context _io_context;
    boost::asio::ip::udp::socket _socket;
    boost::asio::ip::udp::endpoint _server_endpoint;
    boost::asio::ip::udp::endpoint _remote_endpoint;
    std::array<uint8_t, 5 + 1024> _recv_buffer;
    std::thread _thread;
    std::string _last_version;
    bool _is_configured{false};
    std::mutex _mutex;
    std::condition_variable _cv;
    bool _operation_complete{false};
    bool _operation_success{false};
    uint8_t _current_method{0};
    std::map<uint8_t, bool> _button_states;
    std::function<void(uint8_t, bool)> _button_state_callback;
    int _move_timeout_ms{15};
    int _click_timeout_ms{220};
    int _version_timeout_ms{300};
};

#endif