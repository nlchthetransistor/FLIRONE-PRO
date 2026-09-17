#include "web_server.h"
#include <libwebsockets.h>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <algorithm>

WebServer* WebServer::instance_ = nullptr;

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    if (WebServer::instance())
        return WebServer::instance()->handle_ws_event(wsi, (int)reason, user, in, len);
    return 0;
}

static struct lws_protocols protocols[] = {
    { "http", lws_callback_http_dummy, 0, 0, 0, NULL, 0 },
    { "flirone", ws_callback, sizeof(PerSessionData), 0, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

WebServer::WebServer(int port, const std::string& web_root,
                     const std::string& screenshots_dir)
    : port_(port), web_root_(web_root), screenshots_dir_(screenshots_dir) {
}

WebServer::~WebServer() {
    stop();
}

WebServer* WebServer::instance() {
    return instance_;
}

int WebServer::handle_ws_event(struct lws* wsi, int reason,
                               void* user, void* in, size_t len) {
    auto pss = static_cast<PerSessionData*>(user);

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                clients_.push_back(wsi);
            }
            pss->palette_sent = false;
            
            // Queue palette sending on connect
            std::string pal;
            {
                std::lock_guard<std::mutex> lock(palette_mutex_);
                pal = palette_json_;
            }
            if (!pal.empty()) {
                enqueue_to_client(wsi, pal);
            }
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                clients_.erase(std::remove(clients_.begin(), clients_.end(), wsi), clients_.end());
            }
            {
                std::lock_guard<std::mutex> lock(queues_mutex_);
                client_queues_.erase(std::remove_if(client_queues_.begin(), client_queues_.end(),
                    [wsi](const ClientQueue& cq) { return cq.wsi == wsi; }), client_queues_.end());
            }
            break;
        }

        case LWS_CALLBACK_RECEIVE: {
            if (in && len > 0 && on_message_) {
                std::string msg(static_cast<const char*>(in), len);
                on_message_(msg);
            }
            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            std::string msg_to_send;
            bool more = false;
            {
                std::lock_guard<std::mutex> lock(queues_mutex_);
                auto it = std::find_if(client_queues_.begin(), client_queues_.end(),
                                       [wsi](const ClientQueue& cq) { return cq.wsi == wsi; });
                if (it != client_queues_.end() && !it->queue.empty()) {
                    msg_to_send = it->queue.front();
                    it->queue.erase(it->queue.begin());
                    more = !it->queue.empty();
                }
            }

            if (!msg_to_send.empty()) {
                std::vector<unsigned char> buf(LWS_PRE + msg_to_send.length());
                std::memcpy(&buf[LWS_PRE], msg_to_send.c_str(), msg_to_send.length());
                int m = lws_write(wsi, &buf[LWS_PRE], msg_to_send.length(), LWS_WRITE_TEXT);
                if (m < (int)msg_to_send.length()) {
                    lwsl_err("ERROR %d writing to ws socket\n", m);
                    return -1;
                }
                if (more) {
                    lws_callback_on_writable(wsi);
                }
            }
            break;
        }

        case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
            std::string msg;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                if (has_pending_) {
                    msg = pending_msg_;
                    has_pending_ = false;
                }
            }
            if (!msg.empty()) {
                enqueue_to_all(msg);
            }
            break;
        }

        default:
            break;
    }
    return 0;
}

void WebServer::broadcast(const std::string& json_msg) {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_msg_ = json_msg;
        has_pending_ = true;
    }
    if (ctx_) {
        lws_cancel_service(ctx_);
    }
}

void WebServer::set_palette_json(const std::string& palette_json) {
    std::lock_guard<std::mutex> lock(palette_mutex_);
    palette_json_ = palette_json;
}

void WebServer::set_on_message(MessageCallback cb) {
    on_message_ = cb;
}

int WebServer::get_client_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

void WebServer::enqueue_to_client(struct lws* wsi, const std::string& msg) {
    std::lock_guard<std::mutex> lock(queues_mutex_);
    auto it = std::find_if(client_queues_.begin(), client_queues_.end(),
                           [wsi](const ClientQueue& cq) { return cq.wsi == wsi; });
    if (it == client_queues_.end()) {
        client_queues_.push_back({wsi, {msg}});
    } else {
        it->queue.push_back(msg);
    }
    lws_callback_on_writable(wsi);
}

void WebServer::enqueue_to_all(const std::string& msg) {
    std::lock_guard<std::mutex> c_lock(clients_mutex_);
    std::lock_guard<std::mutex> q_lock(queues_mutex_);
    
    for (auto wsi : clients_) {
        auto it = std::find_if(client_queues_.begin(), client_queues_.end(),
                               [wsi](const ClientQueue& cq) { return cq.wsi == wsi; });
        if (it == client_queues_.end()) {
            client_queues_.push_back({wsi, {msg}});
        } else {
            it->queue.push_back(msg);
        }
        lws_callback_on_writable(wsi);
    }
}

void WebServer::service_loop() {
    while (running_) {
        // Broadcasts are handled via LWS_CALLBACK_EVENT_WAIT_CANCELLED
        lws_service(ctx_, 50);
    }
}

static char mount_origin[512] = "";
static char mount_screenshots[512] = "";

bool WebServer::start() {
    if (running_) return false;

    instance_ = this;
    
    std::strncpy(mount_origin, web_root_.c_str(), sizeof(mount_origin) - 1);
    std::strncpy(mount_screenshots, screenshots_dir_.c_str(), sizeof(mount_screenshots) - 1);

    static struct lws_http_mount mount_scr = {
        /* .mount_next */           NULL,
        /* .mountpoint */           "/screenshots",
        /* .origin */               mount_screenshots,
        /* .def */                  "index.html",
        /* .protocol */             NULL,
        /* .cgienv */               NULL,
        /* .extra_mimetypes */      NULL,
        /* .interpret */            NULL,
        /* .cgi_timeout */          0,
        /* .cache_max_age */        0,
        /* .auth_mask */            0,
        /* .cache_reusable */       0,
        /* .cache_revalidate */     0,
        /* .cache_intermediaries */ 0,
        /* .origin_protocol */      LWSMPRO_FILE,
        /* .mountpoint_len */       12,
        /* .basic_auth_login_file */NULL,
    };

    static struct lws_http_mount mount_main = {
        /* .mount_next */           &mount_scr,
        /* .mountpoint */           "/",
        /* .origin */               mount_origin,
        /* .def */                  "index.html",
        /* .protocol */             NULL,
        /* .cgienv */               NULL,
        /* .extra_mimetypes */      NULL,
        /* .interpret */            NULL,
        /* .cgi_timeout */          0,
        /* .cache_max_age */        0,
        /* .auth_mask */            0,
        /* .cache_reusable */       0,
        /* .cache_revalidate */     0,
        /* .cache_intermediaries */ 0,
        /* .origin_protocol */      LWSMPRO_FILE,
        /* .mountpoint_len */       1,
        /* .basic_auth_login_file */NULL,
    };

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port_;
    info.protocols = protocols;
    info.mounts = &mount_main;
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
    
    // Silence some LWS logging if needed
    // lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

    ctx_ = lws_create_context(&info);
    if (!ctx_) {
        lwsl_err("lws init failed\n");
        return false;
    }

    running_ = true;
    service_thread_ = std::thread(&WebServer::service_loop, this);

    return true;
}

void WebServer::stop() {
    if (!running_) return;

    running_ = false;
    if (ctx_) {
        lws_cancel_service(ctx_);
    }

    if (service_thread_.joinable()) {
        service_thread_.join();
    }

    if (ctx_) {
        lws_context_destroy(ctx_);
        ctx_ = nullptr;
    }
    
    instance_ = nullptr;
}
