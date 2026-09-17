#pragma once
#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

// Forward declarations for libwebsockets types
struct lws_context;
struct lws;

// ============================================================
// Per-client session data (managed by libwebsockets)
// ============================================================
struct PerSessionData {
    bool palette_sent = false;
};

// ============================================================
// WebServer — HTTP static file server + WebSocket streaming
// ============================================================
class WebServer {
public:
    WebServer(int port, const std::string& web_root,
              const std::string& screenshots_dir);
    ~WebServer();

    // Non-copyable
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    // Start the server (spawns service thread)
    bool start();

    // Stop the server gracefully
    void stop();

    // Broadcast a JSON text message to all connected WebSocket clients
    void broadcast(const std::string& json_msg);

    // Set the palette JSON to send to new clients on connect
    void set_palette_json(const std::string& palette_json);

    // Callback type for incoming client messages
    using MessageCallback = std::function<void(const std::string& json_msg)>;
    void set_on_message(MessageCallback cb);

    int get_client_count() const;

    // --- Internal: called by the C callback trampoline ---
    int handle_ws_event(struct lws* wsi, int reason,
                        void* user, void* in, size_t len);

    // Singleton accessor for C callback
    static WebServer* instance();

private:
    void service_loop();

    int port_;
    std::string web_root_;
    std::string screenshots_dir_;

    struct lws_context* ctx_ = nullptr;
    std::thread service_thread_;
    std::atomic<bool> running_{false};

    // Palette JSON (sent once per client on connect)
    std::string palette_json_;
    std::mutex palette_mutex_;

    // Pending broadcast
    std::string pending_msg_;
    std::mutex pending_mutex_;
    bool has_pending_ = false;

    // Client tracking
    std::vector<struct lws*> clients_;
    mutable std::mutex clients_mutex_;

    // Per-client send queues (wsi pointer → queued messages)
    struct ClientQueue {
        struct lws* wsi;
        std::vector<std::string> queue;
    };
    std::vector<ClientQueue> client_queues_;
    std::mutex queues_mutex_;

    void enqueue_to_client(struct lws* wsi, const std::string& msg);
    void enqueue_to_all(const std::string& msg);

    MessageCallback on_message_;

    static WebServer* instance_;
};
