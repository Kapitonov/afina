#include "MapBasedGlobalLockImpl.h"

#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
    if (_backend.find(key) == _backend.end()) {
        _lru.push_front(key);
        if (_lru.size() > _max_size) {
            _backend.erase(_lru.back());
            _lru.pop_back();
        }
    }
    _backend[key] = value;
    _lru.remove(key);
    _lru.push_front(key);
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
    if (_backend.find(key) != _backend.end()) {
        return false;
    }
    _lru.push_front(key);
    _backend.at(key) = value;
    if (_lru.size() > _max_size) {
        _backend.erase(_lru.front());
        _lru.pop_back();
    }
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
    if (_backend.find(key) != _backend.end()) {
        _backend.at(key) = value;
        _lru.remove(key);
        _lru.push_front(key);
        return true;
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::unique_lock<std::mutex> guard(_lock);
    if (_backend.find(key) != _backend.end()) {
        _backend.erase(key);
        _lru.remove(key);
        return true;
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    std::unique_lock<std::mutex> guard(*const_cast<std::mutex *>(&_lock));
    if (_backend.find(key) != _backend.end()) {
        value = _backend.at(key);
        return true;
    }
    return false;
}

} // namespace Backend
} // namespace Afina
