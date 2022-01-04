#ifndef XMRIG_WEBSOCKET_H
#define XMRIG_WEBSOCKET_H

#include "base/net/websocket/WebSocketClient.h"
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

class WebSocketClient::WebSocket {
   public:
    WebSocket(std::string url, WebSocketClient* listener);

    ~WebSocket();

    bool connect();
    bool disconnect();
    bool reconnect();
    bool isConnected();
    bool sendMessage(std::string message);

   private:
    enum State { Idle, Connecting, Connected, Disconnecting };
    State m_state = Idle;
    WebSocketClient *m_listener;
    std::thread* m_client_thread = nullptr;
    client m_endpoint;
    websocketpp::connection_hdl hdl;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> m_thread;
    std::string url;
    bool restart = false;
    void onMessage(websocketpp::connection_hdl hdl, message_ptr msg);
    void onOpen(websocketpp::connection_hdl hdl);
    void onClose(websocketpp::connection_hdl hdl);
    void onFail(websocketpp::connection_hdl hdl);
    bool run();
    std::string base64Decode(const std::string &in);

    std::string base64Encode(const std::string &in);
};

}  // namespace xmrig

#endif /* WEBSOCKET */