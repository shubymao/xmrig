#include "websocket.h"

std::string xmrig::WebSocket::base64_decode(const std::string &in) {
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

std::string xmrig::WebSocket::base64_encode(const std::string &in) {
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

xmrig::WebSocket::WebSocket(std::string url) {
    this->url = url;
    m_endpoint.set_access_channels(websocketpp::log::alevel::all);
    m_endpoint.set_error_channels(websocketpp::log::elevel::all);
    // Initialize ASIO
    m_endpoint.init_asio();
    m_endpoint.set_message_handler(
        bind(&WebSocket::on_message, this, ::_1, ::_2));
}

xmrig::WebSocket::~WebSocket() {
    // Close the connection
    m_endpoint.close(hdl, going_away, "Goodbye");
    // Stop the ASIO io_service
    m_endpoint.stop();
}

void xmrig::WebSocket::connect() {
    websocketpp::lib::error_code ec;

    client::connection_ptr con = m_endpoint.get_connection(url, ec);
    if (ec) {
        std::cout << "could not create connection because: " << ec.message()
                  << std::endl;
        return;
    }
    m_endpoint.connect(con);
    this->hdl = con->get_handle();
    m_endpoint.run();
}

void xmrig::WebSocket::send_message(std::string message) {
    websocketpp::lib::error_code ec;
    std::cout << "Message: " << message << std::endl;
    std::string encoded_message = base64_encode(message);
    std::cout << "Encoded Message: " << encoded_message << std::endl;
    m_endpoint.send(this->hdl, encoded_message, TEXT, ec);
    if (ec) {
        std::cout << "Message sent failed because: " << ec.message()
                  << std::endl;
    }
}

void xmrig::WebSocket::on_message(websocketpp::connection_hdl hdl,
                                  message_ptr msg) {
    std::string decoded_message = base64_decode(msg->get_payload());
    std::cout << "on_message called with hdl: " << hdl.lock().get()
              << " and message: " << decoded_message << std::endl;
}