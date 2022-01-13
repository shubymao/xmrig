#ifndef XMRIG_WEBSOCKET_CLIENT_H
#define XMRIG_WEBSOCKET_CLIENT_H

#include <uv.h>

#include <bitset>
#include <map>
#include <vector>

#include "base/kernel/interfaces/IDnsListener.h"
#include "base/net/stratum/BaseClient.h"
#include "base/net/stratum/Job.h"
#include "base/net/stratum/Pool.h"
#include "base/net/stratum/SubmitResult.h"
#include "base/net/tools/LineReader.h"
#include "base/net/tools/Storage.h"
#include "base/tools/Object.h"

using BIO = struct bio_st;

namespace xmrig {

class IClientListener;
class JobResult;

class WebSocketClient : public BaseClient {
   public:
    XMRIG_DISABLE_COPY_MOVE_DEFAULT(WebSocketClient)

    constexpr static uint64_t kConnectTimeout = 20 * 1000;
    constexpr static uint64_t kResponseTimeout = 20 * 1000;
    constexpr static size_t kMaxSendBufferSize = 1024 * 16;

    WebSocketClient(int id, const char *agent, IClientListener *listener);
    ~WebSocketClient() override;

   protected:
    bool disconnect() override;
    inline bool hasExtension(Extension extension) const noexcept override {
        return m_extensions.test(extension);
    }
    bool isTLS() const override;
    const char *tlsFingerprint() const override;
    const char *tlsVersion() const override;
    int64_t send(const rapidjson::Value &obj, Callback callback) override;
    int64_t send(const rapidjson::Value &obj) override;
    int64_t submit(const JobResult &result) override;
    void connect() override;
    void connect(const Pool &pool) override;
    void deleteLater() override;
    void tick(uint64_t now) override;

    inline const char *mode() const override { return "pool"; }
    inline const char *agent() const { return m_agent; }
    inline const char *url() const { return m_pool.url(); }
    inline const String &rpcId() const { return m_rpcId; }
    inline void setRpcId(const char *id) { m_rpcId = id; }

    virtual bool parseLogin(const rapidjson::Value &result, int *code);
    virtual void login();
    virtual void parseNotification(const char *method,
                                   const rapidjson::Value &params,
                                   const rapidjson::Value &error);

    bool close();
    virtual void onClose();

   private:
    class WebSocket;
    class Tls;

    bool parseJob(const rapidjson::Value &params, int *code);
    bool send(BIO *bio);
    bool verifyAlgorithm(const Algorithm &algorithm, const char *algo) const;
    int resolve(const String &host);
    bool send(std::string &message);
    void onMessage(char *line, size_t len);
    void onFailed();
    void onConnected();
    void parseExtensions(const rapidjson::Value &result);
    void parseResponse(int64_t id, const rapidjson::Value &result,
                       const rapidjson::Value &error);
    void startTimeout();
    static bool isCriticalError(const char *message);

    inline void setExtension(Extension ext, bool enable) noexcept {
        m_extensions.set(ext, enable);
    }

    template <Extension ext>
    inline bool has() const noexcept {
        return m_extensions.test(ext);
    }

    static inline WebSocketClient *getClient(void *data) {
        return m_storage.get(data);
    }

    const char *m_agent;
    WebSocket *m_socks = nullptr;
    std::vector<char> m_tempBuf;
    std::bitset<EXT_MAX> m_extensions;
    static Storage<WebSocketClient> m_storage;
    String m_rpcId;
    uint64_t m_expire = 0;
    uint64_t m_jobs = 0;
    uint64_t m_keepAlive = 0;
    uintptr_t m_key = 0;
};

template <>
inline bool WebSocketClient::has<WebSocketClient::EXT_NICEHASH>()
    const noexcept {
    return m_extensions.test(EXT_NICEHASH) || m_pool.isNicehash();
}
template <>
inline bool WebSocketClient::has<WebSocketClient::EXT_KEEPALIVE>()
    const noexcept {
    return m_extensions.test(EXT_KEEPALIVE) || m_pool.keepAlive() > 0;
}

} /* namespace xmrig */

#endif /* XMRIG_WEBSOCKET_CLIENT_H */