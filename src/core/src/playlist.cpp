#include "wamp/playlist.h"

#include "wamp/playlist_file.h"

#include <filesystem>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace wamp {

std::size_t Playlist::add(std::string path) {
  if (path.empty()) return kNone;
  items_.push_back(std::move(path));
  return items_.size() - 1;
}

std::size_t Playlist::addAll(const std::vector<std::string>& paths) {
  std::size_t first = kNone;
  for (const std::string& path : paths) {
    if (path.empty()) continue;
    if (first == kNone) first = items_.size();
    items_.push_back(path);
  }
  return first;
}

bool Playlist::removeAt(std::size_t index) {
  if (index >= items_.size()) return false;
  items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
  if (current_ == index) {
    // A faixa removida era a atual: a próxima herda a posição (sem sair do lugar).
    current_ = index < items_.size() ? index : kNone;
  } else if (current_ != kNone && index < current_) {
    --current_;
  }
  return true;
}

void Playlist::clear() {
  items_.clear();
  queue_.clear();
  current_ = kNone;
}

void Playlist::setItems(std::vector<std::string> items, std::size_t current) {
  items_ = std::move(items);
  current_ = current < items_.size() ? current : kNone;
}

void Playlist::reverseOrder() {
  if (items_.size() < 2) return;
  const std::string currentPath = hasCurrent() ? items_[current_] : std::string();
  std::reverse(items_.begin(), items_.end());
  if (currentPath.empty()) return;
  current_ = static_cast<std::size_t>(
      std::find(items_.begin(), items_.end(), currentPath) - items_.begin());
}

void Playlist::shuffleOrder() {
  if (items_.size() < 2) return;
  const std::string currentPath = hasCurrent() ? items_[current_] : std::string();
  std::shuffle(items_.begin(), items_.end(), rng_);
  if (currentPath.empty()) return;
  current_ = static_cast<std::size_t>(
      std::find(items_.begin(), items_.end(), currentPath) - items_.begin());
}

std::size_t Playlist::removeMissingFiles() {
  const std::string currentPath = hasCurrent() ? items_[current_] : std::string();
  const std::size_t before = items_.size();
  std::error_code error;
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [&error](const std::string& path) {
                                return !std::filesystem::exists(path, error);
                              }),
               items_.end());
  const std::size_t removed = before - items_.size();
  if (removed == 0) return 0;

  if (currentPath.empty()) {
    current_ = kNone;
  } else {
    const auto it = std::find(items_.begin(), items_.end(), currentPath);
    current_ = it == items_.end() ? kNone : static_cast<std::size_t>(it - items_.begin());
  }
  queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                              [this](const std::string& path) {
                                return std::find(items_.begin(), items_.end(), path) ==
                                       items_.end();
                              }),
               queue_.end());
  return removed;
}

void Playlist::setCurrent(std::size_t index) {
  if (index < items_.size()) {
    current_ = index;
  } else {
    current_ = kNone;
  }
}

std::size_t Playlist::nextIndex() const {
  if (items_.empty()) return kNone;
  if (current_ == kNone || current_ + 1 >= items_.size()) return 0;
  return current_ + 1;
}

std::size_t Playlist::previousIndex() const {
  if (items_.empty()) return kNone;
  if (current_ == kNone) return items_.size() - 1;
  if (current_ == 0) return items_.size() - 1;
  return current_ - 1;
}

void Playlist::enqueue(std::size_t index) {
  if (index < items_.size()) queue_.push_back(items_[index]);
}

std::size_t Playlist::queuePosition(std::size_t index) const {
  if (index >= items_.size()) return 0;
  for (std::size_t i = 0; i < queue_.size(); ++i) {
    if (queue_[i] == items_[index]) return i + 1;
  }
  return 0;
}

std::string Playlist::dequeue() {
  if (queue_.empty()) return {};
  std::string front = std::move(queue_.front());
  queue_.erase(queue_.begin());
  return front;
}

bool Playlist::removeFromQueue(std::size_t index) {
  if (index >= items_.size()) return false;
  const std::string& path = items_[index];
  for (auto it = queue_.begin(); it != queue_.end(); ++it) {
    if (*it == path) {
      queue_.erase(it);
      return true;
    }
  }
  return false;
}

bool Playlist::recentlyPlayed(std::size_t index) const {
  if (index >= items_.size()) return false;
  for (const std::string& path : recent_) {
    if (path == items_[index]) return true;
  }
  return false;
}

void Playlist::resetShuffle() {
  recent_.clear();
  history_.clear();
}

void Playlist::noteShufflePlay(std::size_t index) {
  if (index >= items_.size()) return;
  const std::string& path = items_[index];
  if (!history_.empty() && history_.back() == path) return;
  history_.push_back(path);
  // Cem faixas de memória: o suficiente para voltar bastante sem crescer sem limite.
  while (history_.size() > 100) history_.erase(history_.begin());
}

std::size_t Playlist::shuffledPreviousIndex() const {
  if (history_.size() < 2) return kNone;
  const std::string& previous = history_[history_.size() - 2];
  for (std::size_t i = 0; i < items_.size(); ++i) {
    if (items_[i] == previous) return i;
  }
  return kNone;
}

void Playlist::dropLastShufflePlay() {
  if (!history_.empty()) history_.pop_back();
}

std::size_t Playlist::shuffledNextIndex(double level) {
  const std::size_t count = items_.size();
  if (count == 0) return kNone;
  if (count == 1) return 0;

  if (level < 0.0) level = 0.0;
  if (level > 1.0) level = 1.0;

  // Nível 0 = próxima faixa; nível 1 = qualquer uma. No meio, só o trecho à frente.
  std::size_t window = static_cast<std::size_t>(
      std::llround(level * static_cast<double>(count)));
  if (window < 1) window = 1;
  if (window > count - 1) window = count - 1;

  const std::size_t base = hasCurrent() ? current_ + 1 : 0;
  std::uniform_int_distribution<std::size_t> dist(0, window - 1);

  std::size_t picked = kNone;
  for (int attempt = 0; attempt < 12; ++attempt) {
    const std::size_t index = (base + dist(rng_)) % count;
    if (index == current_) continue;
    // Nos primeiros sorteios evita repetir o que acabou de tocar; se a lista for
    // pequena demais para isso, aceita repetir em vez de girar para sempre.
    if (recentlyPlayed(index) && attempt < 8) continue;
    picked = index;
    break;
  }
  if (picked == kNone) picked = (base + dist(rng_)) % count;

  recent_.push_back(items_[picked]);
  const std::size_t limit = std::min<std::size_t>(count > 2 ? count - 1 : 0, 32);
  while (recent_.size() > limit) recent_.erase(recent_.begin());

  return picked;
}

bool Playlist::loadPlaylist(const std::string& file, std::string* error) {
  std::vector<std::string> paths;
  if (!loadPlaylistFile(file, &paths, error)) return false;
  items_.insert(items_.end(), paths.begin(), paths.end());
  return true;
}

bool Playlist::savePlaylist(const std::string& file, std::string* error) const {
  return savePlaylistFile(file, items_, error);
}

}  // namespace wamp
