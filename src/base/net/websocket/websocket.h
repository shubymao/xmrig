#ifndef XMRIG_WEBSOCKET_H
#define XMRIG_WEBSOCKET_H

#include <iostream>
#include <string>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

typedef websocketpp::client<websocketpp::config::asio_client> client;
// pull out the type of messages sent by our config
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
using websocketpp::close::status::going_away;
using websocketpp::frame::opcode::TEXT;
using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

namespace xmrig {

class WebSocket {
   public:
    WebSocket(std::string url);

    ~WebSocket();

    void connect();

    void send_message(std::string message);

    void on_message(websocketpp::connection_hdl hdl, message_ptr msg);

    std::string base64_decode(const std::string &in);

    std::string base64_encode(const std::string &in);

   private:
    client m_endpoint;
    websocketpp::connection_hdl hdl;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> m_thread;
    std::string url;
};

}  // namespace xmrig

#endif /* WEBSOCKET */