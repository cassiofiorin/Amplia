#include "wamp/metadata_scanner.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>

#include "wamp/metadata.h"

namespace wamp {

struct MetadataScanner::Impl {
  mutable std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::string> queue;
  std::unordered_set<std::string> queued;
  Callback callback;
  bool running = true;
  std::thread worker;

  void loop() {
    std::unique_lock<std::mutex> lock(mutex);
    while (true) {
      cv.wait(lock, [this] { return !running || !queue.empty(); });
      if (!running) return;

      std::string path = std::move(queue.front());
      queue.pop_front();
      Callback cb = callback;
      lock.unlock();

      TrackInfo info;
      std::string error;
      const bool ok = probeTrack(path, &info, &error);

      if (cb) cb(path, info, ok);

      lock.lock();
      queued.erase(path);
    }
  }
};

MetadataScanner::MetadataScanner() : impl_(new Impl()) {
  impl_->worker = std::thread([this] { impl_->loop(); });
}

MetadataScanner::~MetadataScanner() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->running = false;
  }
  impl_->cv.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
}

void MetadataScanner::setCallback(Callback callback) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->callback = std::move(callback);
}

void MetadataScanner::enqueue(const std::string& path) {
  if (path.empty()) return;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->queued.insert(path).second) return;  // já está na fila
    impl_->queue.push_back(path);
  }
  impl_->cv.notify_one();
}

void MetadataScanner::enqueueAll(const std::vector<std::string>& paths) {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const std::string& path : paths) {
      if (path.empty()) continue;
      if (!impl_->queued.insert(path).second) continue;
      impl_->queue.push_back(path);
    }
  }
  impl_->cv.notify_all();
}

void MetadataScanner::clearPending() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->queue.clear();
  impl_->queued.clear();
}

std::size_t MetadataScanner::pending() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->queue.size();
}

}  // namespace wamp
