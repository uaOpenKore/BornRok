#pragma once
#include <cstddef>
#include <functional>
#include <vector>

// Tiny typed publish/subscribe channel. One channel per event type. Intended
// for loose coupling between subsystems (input -> game, net -> world, ...).
namespace uaro {

template <class Event>
class EventChannel {
public:
    using Handler = std::function<void(const Event&)>;

    void subscribe(Handler h) { handlers_.push_back(std::move(h)); }

    void emit(const Event& e) const {
        for (const auto& h : handlers_) h(e);
    }

    void clear() { handlers_.clear(); }
    std::size_t size() const { return handlers_.size(); }

private:
    std::vector<Handler> handlers_;
};

} // namespace uaro
