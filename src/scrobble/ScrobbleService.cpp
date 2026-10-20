// SPDX-License-Identifier: MIT
// scrobble — worker-backed coordinator wrapper. See ScrobbleService.hpp.

#include "scrobble/ScrobbleService.hpp"

#include "scrobble/ListenBrainzClient.hpp"
#include "scrobble/ScrobbleQueue.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace koalaamp::scrobble {
namespace {

constexpr auto kFlushInterval = std::chrono::minutes(5);

enum class CommandType {
    Start,
    Progress,
    End,
    Stop,
    Mode,
    LastFm,
    ListenBrainz,
    Flush,
    Shutdown,
};

struct Command {
    CommandType type{CommandType::Shutdown};
    ScrobbleMode mode{ScrobbleMode::Off};
    ScrobbleTrack track;
    LastFmCredentials credentials;
    std::string text;
    std::int64_t first{0};
    std::int64_t second{0};
};

} // namespace

struct ScrobbleService::Impl {
    Impl(ITransport& transport, std::filesystem::path queueFile, ScrobbleMode mode,
         LastFmCredentials lastFm, std::string lastFmSessionKey, std::string listenBrainzToken)
        : transport_(transport), queue_(std::move(queueFile)) {
        queue_.load();
        coordinator_.setQueue(&queue_);
        coordinator_.setMode(mode);
        applyLastFm(std::move(lastFm), std::move(lastFmSessionKey));
        applyListenBrainz(std::move(listenBrainzToken));
        pending_.store(coordinator_.pendingCount(), std::memory_order_relaxed);
        thread_ = std::thread([this] { run(); });
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            commands_.push_back(Command{CommandType::Shutdown});
        }
        ready_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void post(Command command) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            commands_.push_back(std::move(command));
        }
        ready_.notify_one();
    }

    void run() {
        auto nextFlush = std::chrono::steady_clock::now() + kFlushInterval;
        while (true) {
            std::deque<Command> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait_for(lock, std::chrono::seconds(1),
                                [this] { return !commands_.empty(); });
                batch.swap(commands_);
            }
            for (Command& command : batch) {
                if (command.type == CommandType::Shutdown) {
                    return;
                }
                apply(command);
            }
            if (std::chrono::steady_clock::now() >= nextFlush) {
                coordinator_.flushPending();
                publishPending();
                nextFlush = std::chrono::steady_clock::now() + kFlushInterval;
            }
        }
    }

    void apply(Command& command) {
        switch (command.type) {
        case CommandType::Start:
            coordinator_.onTrackStarted(command.track, command.first);
            break;
        case CommandType::Progress:
            coordinator_.onProgress(command.first, command.second);
            break;
        case CommandType::End:
            coordinator_.onTrackEnded();
            break;
        case CommandType::Stop:
            coordinator_.onStopped();
            break;
        case CommandType::Mode:
            coordinator_.setMode(command.mode);
            break;
        case CommandType::LastFm:
            applyLastFm(std::move(command.credentials), std::move(command.text));
            break;
        case CommandType::ListenBrainz:
            applyListenBrainz(std::move(command.text));
            break;
        case CommandType::Flush:
            coordinator_.flushPending();
            break;
        case CommandType::Shutdown:
            return;
        }
        publishPending();
    }

    void applyLastFm(LastFmCredentials credentials, std::string sessionKey) {
        coordinator_.setLastFm(nullptr);
        lastFm_.reset();
        if (credentials.apiKey.empty() || credentials.sharedSecret.empty() || sessionKey.empty()) {
            return;
        }
        lastFm_ = std::make_unique<LastFmClient>(std::move(credentials), transport_);
        lastFm_->setSessionKey(std::move(sessionKey));
        coordinator_.setLastFm(lastFm_.get());
    }

    void applyListenBrainz(std::string token) {
        coordinator_.setListenBrainz(nullptr);
        listenBrainz_.reset();
        if (token.empty()) {
            return;
        }
        listenBrainz_ = std::make_unique<ListenBrainzClient>(std::move(token), transport_);
        coordinator_.setListenBrainz(listenBrainz_.get());
    }

    void publishPending() {
        pending_.store(coordinator_.pendingCount(), std::memory_order_relaxed);
    }

    ITransport& transport_;
    ScrobbleQueue queue_;
    ScrobbleCoordinator coordinator_;
    std::unique_ptr<LastFmClient> lastFm_;
    std::unique_ptr<ListenBrainzClient> listenBrainz_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Command> commands_;
    std::atomic<std::size_t> pending_{0};
    std::thread thread_;
};

ScrobbleService::ScrobbleService(ITransport& transport, std::filesystem::path queueFile,
                                 ScrobbleMode mode, LastFmCredentials lastFm,
                                 std::string lastFmSessionKey, std::string listenBrainzToken)
    : impl_(std::make_unique<Impl>(transport, std::move(queueFile), mode, std::move(lastFm),
                                   std::move(lastFmSessionKey), std::move(listenBrainzToken))) {}

ScrobbleService::~ScrobbleService() = default;

void ScrobbleService::setMode(ScrobbleMode mode) {
    Command command;
    command.type = CommandType::Mode;
    command.mode = mode;
    impl_->post(std::move(command));
}

void ScrobbleService::setLastFm(LastFmCredentials credentials, std::string sessionKey) {
    Command command;
    command.type = CommandType::LastFm;
    command.credentials = std::move(credentials);
    command.text = std::move(sessionKey);
    impl_->post(std::move(command));
}

void ScrobbleService::setListenBrainz(std::string token) {
    Command command;
    command.type = CommandType::ListenBrainz;
    command.text = std::move(token);
    impl_->post(std::move(command));
}

void ScrobbleService::onTrackStarted(ScrobbleTrack track, std::int64_t startedAtUnix) {
    Command command;
    command.type = CommandType::Start;
    command.track = std::move(track);
    command.first = startedAtUnix;
    impl_->post(std::move(command));
}

void ScrobbleService::onProgress(std::int64_t positionMs, std::int64_t lengthMs) {
    Command command;
    command.type = CommandType::Progress;
    command.first = positionMs;
    command.second = lengthMs;
    impl_->post(std::move(command));
}

void ScrobbleService::onTrackEnded() {
    Command command;
    command.type = CommandType::End;
    impl_->post(std::move(command));
}

void ScrobbleService::onStopped() {
    Command command;
    command.type = CommandType::Stop;
    impl_->post(std::move(command));
}

void ScrobbleService::flushPending() {
    Command command;
    command.type = CommandType::Flush;
    impl_->post(std::move(command));
}

std::size_t ScrobbleService::pendingCount() const {
    return impl_->pending_.load(std::memory_order_relaxed);
}

} // namespace koalaamp::scrobble
