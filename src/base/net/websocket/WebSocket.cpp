#include "base/net/websocket/WebSocket.h"

#include "base/io/log/Log.h"

xmrig::WebSocketClient::WebSocket::WebSocket(std::string url,
                                             WebSocketClient* listener) {
    this->url = url;
    this->m_listener = listener;
    m_endpoint.set_access_channels(websocketpp::log::alevel::all);
    m_endpoint.set_error_channels(websocketpp::log::elevel::all);
}

xmrig::WebSocketClient::WebSocket::~WebSocket() { disconnect(); }

bool xmrig::WebSocketClient::WebSocket::isConnected() {
    return m_state == Connected;
}

bool xmrig::WebSocketClient::WebSocket::connect() {
    if (m_state == Connecting || m_state == Connected) {
        return true;
    }
    if (m_state == Disconnecting) {
        return false;
    }
    m_state = Connecting;
    // Initialize ASIO
    m_endpoint.init_asio();
    LOG_DEBUG("CREATED ASIO \n");
    m_endpoint.set_message_handler(
        bind(&WebSocket::onMessage, this, ::_1, ::_2));
    m_endpoint.set_open_handler(bind(&WebSocket::onOpen, this, ::_1));
    m_endpoint.set_fail_handler(bind(&WebSocket::onFail, this, ::_1));
    LOG_DEBUG("CREATED MESSAGE HANDLER\n");
    websocketpp::lib::error_code ec;
    client::connection_ptr con = m_endpoint.get_connection(url, ec);
    if (ec) {
        LOG_DEBUG("FAILED TO ESTABLISH CONNECTION. REASON: %s \n", url);
        return false;
    }
    m_endpoint.connect(con);
    m_client_thread = new std::thread(&client::run, &m_endpoint);
    LOG_DEBUG("WEBSOCKET CLIENT THREAD CREATED \n");
    return true;
}

bool xmrig::WebSocketClient::WebSocket::disconnect() {
    if (m_state == Idle || m_state == Disconnecting) {
        return true;
    }
    m_state = Disconnecting;
    m_endpoint.close(hdl, going_away, "Goodbye");
    m_endpoint.stop();
    m_client_thread->join();
    m_client_thread = nullptr;
    m_state = Idle;
    return true;
}

bool xmrig::WebSocketClient::WebSocket::reconnect() {
    if (isConnected()) {
        disconnect();
    }
    return connect();
}

bool xmrig::WebSocketClient::WebSocket::sendMessage(std::string message) {
    websocketpp::lib::error_code ec;
    std::string encoded_message = base64Encode(message);
    LOG_DEBUG("ENCODED THE MESSAGE \n");
    m_endpoint.send(this->hdl, encoded_message, TEXT, ec);
    if (ec) {
        return false;
    }
    return true;
}

void xmrig::WebSocketClient::WebSocket::onMessage(
    websocketpp::connection_hdl hdl, message_ptr msg) {
    this->hdl = hdl;
    LOG_DEBUG("MESSAGE RECEIVED \n");
    std::string decoded_message = base64Decode(msg->get_payload());
    LOG_DEBUG("DECODED THE MESSAGE \n");
    LOG_DEBUG("%s \n", decoded_message.c_str());
    char* c = const_cast<char*>(decoded_message.c_str());
    m_listener->onMessage(c, decoded_message.length());
}

void xmrig::WebSocketClient::WebSocket::onOpen(
    websocketpp::connection_hdl hdl) {
    this->hdl = hdl;
    m_state = Connected;
    m_listener->onConnected();
}
void xmrig::WebSocketClient::WebSocket::onClose(
    websocketpp::connection_hdl hdl) {
    this->hdl = hdl;
    m_state = Idle;
}
void xmrig::WebSocketClient::WebSocket::onFail(
    websocketpp::connection_hdl hdl) {
    this->hdl = hdl;
    m_state = Idle;
}

std::string xmrig::WebSocketClient::WebSocket::base64Decode(
    const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++)
        T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
              [i]] = i;

    int val = 0, valb = -8;
    for (char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string xmrig::WebSocketClient::WebSocket::base64Encode(
    const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
                "+/"[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
                [((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
