#include "MapBasedGlobalLockImpl.h"

#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
    try {
        if (_backend.find(key) == _backend.end()) {
            _lru.push_back(key);
            if (_lru.size() > _max_size) {
                _backend.erase(_lru.front());
                _lru.pop_front();
            }
        }
        _backend[key] = value;
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
std::unique_lock<std::mutex> guard(_lock);
    if (_backend.find(key) == _backend.end()) {
        _lru.push_back(key);
        _backend.at(key) = value;
        if (_lru.size() > _max_size) {
            _backend.erase(_lru.front());
            _lru.pop_front();
        }
        return true;
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
    if (_backend.find(key) != _backend.end()) {
        _backend.at(key) = value;
        return true;
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::unique_lock<std::mutex> guard(_lock);
    if (_backend.find(key) != _backend.end()) {
        _backend.erase(key);
        std::list<std::string>::const_iterator it = std::find(_lru.begin(), _lru.end(), key);
        _lru.erase(it);
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
