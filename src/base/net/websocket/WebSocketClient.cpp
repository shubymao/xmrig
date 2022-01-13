/* XMRig
 * Copyright (c) 2019      jtgrassie   <https://github.com/jtgrassie>
 * Copyright (c) 2018-2021 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2021 XMRig       <https://github.com/xmrig>,
 * <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

#ifdef XMRIG_FEATURE_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "base/net/stratum/Tls.h"
#endif

#include "3rdparty/rapidjson/document.h"
#include "3rdparty/rapidjson/error/en.h"
#include "3rdparty/rapidjson/stringbuffer.h"
#include "3rdparty/rapidjson/writer.h"
#include "base/io/json/Json.h"
#include "base/io/json/JsonRequest.h"
#include "base/io/log/Log.h"
#include "base/kernel/interfaces/IClientListener.h"
#include "base/net/websocket/WebSocket.h"
#include "base/net/websocket/WebSocketClient.h"
#include "base/tools/Chrono.h"
#include "base/tools/Cvt.h"
#include "base/tools/cryptonote/BlobReader.h"
#include "net/JobResult.h"

#ifdef _MSC_VER
#define strncasecmp(x, y, z) _strnicmp(x, y, z)
#endif

namespace xmrig {

Storage<WebSocketClient> WebSocketClient::m_storage;

} /* namespace xmrig */

xmrig::WebSocketClient::WebSocketClient(int id, const char *agent,
                                        IClientListener *listener)
    : BaseClient(id, listener), m_agent(agent), m_tempBuf(256) {
    m_key = m_storage.add(this);
}

xmrig::WebSocketClient::~WebSocketClient() { delete m_socks; }

bool xmrig::WebSocketClient::disconnect() {
    m_keepAlive = 0;
    m_expire = 0;
    m_failures = -1;
    return close();
}

int64_t xmrig::WebSocketClient::send(const rapidjson::Value &obj,
                                     Callback callback) {
    assert(obj["id"] == sequence());
    m_callbacks.insert({sequence(), std::move(callback)});
    return send(obj);
}

int64_t xmrig::WebSocketClient::send(const rapidjson::Value &obj) {
    using namespace rapidjson;
    StringBuffer buffer(nullptr, 512);
    Writer<StringBuffer> writer(buffer);
    obj.Accept(writer);
    const size_t size = buffer.GetSize();
    std::string data(buffer.GetString(), size);
    LOG_DEBUG("SENDING DATA: %s \n", data.c_str()); 
    return send(data);
}

int64_t xmrig::WebSocketClient::submit(const JobResult &result) {
#ifndef XMRIG_PROXY_PROJECT
    if (result.clientId != m_rpcId || m_rpcId.isNull() ||
        m_state != ConnectedState) {
        return -1;
    }
#endif

    if (result.diff == 0) {
        close();
        return -1;
    }
    using namespace rapidjson;

#ifdef XMRIG_PROXY_PROJECT
    const char *nonce = result.nonce;
    const char *data = result.result;
#else
    char *nonce = m_tempBuf.data();
    char *data = m_tempBuf.data() + 16;
    char *signature = m_tempBuf.data() + 88;

    Cvt::toHex(nonce, sizeof(uint32_t) * 2 + 1,
               reinterpret_cast<const uint8_t *>(&result.nonce),
               sizeof(uint32_t));
    Cvt::toHex(data, 65, result.result(), 32);

    if (result.minerSignature()) {
        Cvt::toHex(signature, 129, result.minerSignature(), 64);
    }
#endif

    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    Value params(kObjectType);
    params.AddMember("id", StringRef(m_rpcId.data()), allocator);
    params.AddMember("job_id", StringRef(result.jobId.data()), allocator);
    params.AddMember("nonce", StringRef(nonce), allocator);
    params.AddMember("result", StringRef(data), allocator);

#ifndef XMRIG_PROXY_PROJECT
    if (result.minerSignature()) {
        params.AddMember("sig", StringRef(signature), allocator);
    }
#else
    if (result.sig) {
        params.AddMember("sig", StringRef(result.sig), allocator);
    }
#endif

    if (has<EXT_ALGO>() && result.algorithm.isValid()) {
        params.AddMember("algo", StringRef(result.algorithm.name()), allocator);
    }

    JsonRequest::create(doc, m_sequence, "submit", params);

#ifdef XMRIG_PROXY_PROJECT
    m_results[m_sequence] = SubmitResult(m_sequence, result.diff,
                                         result.actualDiff(), result.id, 0);
#else
    m_results[m_sequence] = SubmitResult(
        m_sequence, result.diff, result.actualDiff(), 0, result.backend);
#endif

    return send(doc);
}

void xmrig::WebSocketClient::connect() {
    LOG_DEBUG("STARTING TO CONNECT TO %s\n" , m_pool.url().data());
    std::string host = std::string(m_pool.url().data());
    std::cout << "STD STRING HOST:" << host << "\n";
    m_socks = new WebSocket(host, this);
    LOG_DEBUG("Successfully Created Websocket \n");
    m_socks->connect();
    LOG_DEBUG("Successfully Connected Websocket \n");
}

void xmrig::WebSocketClient::connect(const Pool &pool) {
    setPool(pool);
    connect();
}

void xmrig::WebSocketClient::deleteLater() {
    if (!m_listener) {
        return;
    }

    m_listener = nullptr;

    if (!disconnect()) {
        m_storage.remove(m_key);
    }
}

void xmrig::WebSocketClient::tick(uint64_t now) {}

bool xmrig::WebSocketClient::close() {
    m_socks->disconnect();
    return true;
}

bool xmrig::WebSocketClient::parseJob(const rapidjson::Value &params,
                                      int *code) {
    if (!params.IsObject()) {
        *code = 2;
        return false;
    }

    Job job(has<EXT_NICEHASH>(), m_pool.algorithm(), m_rpcId);

    if (!job.setId(params["job_id"].GetString())) {
        *code = 3;
        return false;
    }

    const char *algo = Json::getString(params, "algo");
    const char *blobData = Json::getString(params, "blob");
    if (algo) {
        job.setAlgorithm(algo);
    } else if (m_pool.coin().isValid()) {
        uint8_t blobVersion = 0;
        if (blobData) {
            Cvt::fromHex(&blobVersion, 1, blobData, 2);
        }
        job.setAlgorithm(m_pool.coin().algorithm(blobVersion));
    }

#ifdef XMRIG_FEATURE_HTTP
    if (m_pool.mode() == Pool::MODE_SELF_SELECT) {
        job.setExtraNonce(Json::getString(params, "extra_nonce"));
        job.setPoolWallet(Json::getString(params, "pool_wallet"));

        if (job.extraNonce().isNull() || job.poolWallet().isNull()) {
            *code = 4;
            return false;
        }
    } else
#endif
    {
        if (!job.setBlob(blobData)) {
            *code = 4;
            return false;
        }
    }

    if (!job.setTarget(params["target"].GetString())) {
        *code = 5;
        return false;
    }

    job.setHeight(Json::getUint64(params, "height"));

    if (!verifyAlgorithm(job.algorithm(), algo)) {
        *code = 6;
        return false;
    }

    if (m_pool.mode() != Pool::MODE_SELF_SELECT &&
        job.algorithm().family() == Algorithm::RANDOM_X &&
        !job.setSeedHash(Json::getString(params, "seed_hash"))) {
        *code = 7;
        return false;
    }

    job.setSigKey(Json::getString(params, "sig_key"));

    m_job.setClientId(m_rpcId);

    if (m_job != job) {
        m_jobs++;
        m_job = std::move(job);
        return true;
    }

    if (m_jobs == 0) {  // https://github.com/xmrig/xmrig/issues/459
        return false;
    }

    if (!isQuiet()) {
        LOG_WARN("%s " YELLOW("duplicate job received, reconnect"), tag());
    }

    close();
    return false;
}

bool xmrig::WebSocketClient::send(BIO *bio) { return false; }

bool xmrig::WebSocketClient::verifyAlgorithm(const Algorithm &algorithm,
                                             const char *algo) const {
    if (!algorithm.isValid()) {
        if (!isQuiet()) {
            if (algo == nullptr) {
                LOG_ERR("%s " RED("unknown algorithm, make sure you set "
                                  "\"algo\" or \"coin\" option"),
                        tag(), algo);
            } else {
                LOG_ERR("%s " RED("unsupported algorithm ") RED_BOLD("\"%s\" ")
                            RED("detected, reconnect"),
                        tag(), algo);
            }
        }

        return false;
    }

    bool ok = true;
    m_listener->onVerifyAlgorithm(this, algorithm, &ok);

    if (!ok && !isQuiet()) {
        LOG_ERR("%s " RED("incompatible/disabled algorithm ")
                    RED_BOLD("\"%s\" ") RED("detected, reconnect"),
                tag(), algorithm.name());
    }

    return ok;
}

bool xmrig::WebSocketClient::send(std::string &message) {
    LOG_DEBUG("[%s] send (%d bytes): \"%s\"", url(), message.length(), message);
    return m_socks->sendMessage(message);
}

void xmrig::WebSocketClient::onConnected() { 
    LOG_DEBUG("On CONNECTED");
    login(); 
    LOG_DEBUG("LOGGED_IN");
}

bool xmrig::WebSocketClient::parseLogin(const rapidjson::Value &result,
                                        int *code) {
    setRpcId(Json::getString(result, "id"));
    if (rpcId().isNull()) {
        *code = 1;
        return false;
    }

    parseExtensions(result);

    const bool rc = parseJob(result["job"], code);
    m_jobs = 0;

    return rc;
}

void xmrig::WebSocketClient::login() {
    using namespace rapidjson;
    m_results.clear();

    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    Value params(kObjectType);
    params.AddMember("login", m_user.toJSON(), allocator);
    params.AddMember("pass", m_password.toJSON(), allocator);
    params.AddMember("agent", StringRef(m_agent), allocator);

    if (!m_rigId.isNull()) {
        params.AddMember("rigid", m_rigId.toJSON(), allocator);
    }

    m_listener->onLogin(this, doc, params);

    JsonRequest::create(doc, 1, "login", params);

    send(doc);
}

void xmrig::WebSocketClient::onClose() {
    delete m_socks;
    m_socks = nullptr;
}

void xmrig::WebSocketClient::onMessage(char *line, size_t len) {
    LOG_DEBUG("[%s] received (%d bytes): \"%.*s\"", url(), len,
              static_cast<int>(len), line);

    if (len < 32 || line[0] != '{') {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("JSON decode failed"), tag());
        }
        return;
    }

    rapidjson::Document doc;
    if (doc.ParseInsitu(line).HasParseError()) {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("JSON decode failed: ") RED_BOLD("\"%s\""), tag(),
                    rapidjson::GetParseError_En(doc.GetParseError()));
        }
        return;
    }

    if (!doc.IsObject()) {
        return;
    }

    const auto &id = Json::getValue(doc, "id");
    const auto &error = Json::getValue(doc, "error");

    if (id.IsInt64()) {
        return parseResponse(id.GetInt64(), Json::getValue(doc, "result"),
                             error);
    }

    const char *method = Json::getString(doc, "method");
    if (!method) {
        return;
    }

    if (error.IsObject()) {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("error: ") RED_BOLD("\"%s\"") RED(", code: ")
                        RED_BOLD("%d"),
                    tag(), Json::getString(error, "message"),
                    Json::getInt(error, "code"));
        }

        return;
    }

    parseNotification(method, Json::getValue(doc, "params"), error);
}

void xmrig::WebSocketClient::onFailed() { m_socks->reconnect(); }

void xmrig::WebSocketClient::parseExtensions(const rapidjson::Value &result) {
    m_extensions.reset();

    if (!result.HasMember("extensions")) {
        return;
    }

    const rapidjson::Value &extensions = result["extensions"];
    if (!extensions.IsArray()) {
        return;
    }

    for (const rapidjson::Value &ext : extensions.GetArray()) {
        if (!ext.IsString()) {
            continue;
        }

        const char *name = ext.GetString();

        if (strcmp(name, "algo") == 0) {
            setExtension(EXT_ALGO, true);
        } else if (strcmp(name, "nicehash") == 0) {
            setExtension(EXT_NICEHASH, true);
        } else if (strcmp(name, "connect") == 0) {
            setExtension(EXT_CONNECT, true);
        } else if (strcmp(name, "keepalive") == 0) {
            setExtension(EXT_KEEPALIVE, true);
        }
#ifdef XMRIG_FEATURE_TLS
        else if (strcmp(name, "tls") == 0) {
            setExtension(EXT_TLS, true);
        }
#endif
    }
}

void xmrig::WebSocketClient::parseNotification(const char *method,
                                               const rapidjson::Value &params,
                                               const rapidjson::Value &) {
    if (strcmp(method, "job") == 0) {
        int code = -1;
        if (parseJob(params, &code)) {
            m_listener->onJobReceived(this, m_job, params);
        } else {
            close();
        }

        return;
    }
}

void xmrig::WebSocketClient::parseResponse(int64_t id,
                                           const rapidjson::Value &result,
                                           const rapidjson::Value &error) {
    if (handleResponse(id, result, error)) {
        return;
    }

    if (error.IsObject()) {
        const char *message = error["message"].GetString();

        if (!handleSubmitResponse(id, message) && !isQuiet()) {
            LOG_ERR("%s " RED("error: ") RED_BOLD("\"%s\"") RED(", code: ")
                        RED_BOLD("%d"),
                    tag(), message, Json::getInt(error, "code"));
        }

        if (m_id == 1 || isCriticalError(message)) {
            close();
        }

        return;
    }

    if (!result.IsObject()) {
        return;
    }

    if (id == 1) {
        int code = -1;
        if (!parseLogin(result, &code)) {
            if (!isQuiet()) {
                LOG_ERR("%s " RED("login error code: ") RED_BOLD("%d"), tag(),
                        code);
            }

            close();
            return;
        }

        m_failures = 0;
        m_listener->onLoginSuccess(this);

        if (m_job.isValid()) {
            m_listener->onJobReceived(this, m_job, result["job"]);
        }

        return;
    }

    handleSubmitResponse(id);
}

void xmrig::WebSocketClient::startTimeout() {
    m_expire = 0;
    if (has<EXT_KEEPALIVE>()) {
        const uint64_t ms =
            static_cast<uint64_t>(m_pool.keepAlive() > 0
                                      ? m_pool.keepAlive()
                                      : Pool::kKeepAliveTimeout) *
            1000;

        m_keepAlive = Chrono::steadyMSecs() + ms;
    }
}

bool xmrig::WebSocketClient::isCriticalError(const char *message) {
    if (!message) {
        return false;
    }

    if (strncasecmp(message, "Unauthenticated", 15) == 0) {
        return true;
    }

    if (strncasecmp(message, "your IP is banned", 17) == 0) {
        return true;
    }

    if (strncasecmp(message, "IP Address currently banned", 27) == 0) {
        return true;
    }

    if (strncasecmp(message, "Invalid job id", 14) == 0) {
        return true;
    }

    return false;
}

bool xmrig::WebSocketClient::isTLS() const { return false; }

const char *xmrig::WebSocketClient::tlsFingerprint() const { return nullptr; }

const char *xmrig::WebSocketClient::tlsVersion() const { return nullptr; }