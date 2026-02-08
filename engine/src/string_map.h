#ifndef STRING_MAP_H
#define STRING_MAP_H

#include <cstddef>
#include <cstring>
#include <string>

template<typename V>
class StringMap {
private:
    struct Entry {
        char* key;
        size_t key_len;
        V value;
        bool occupied;
        bool deleted;

        Entry() : key(nullptr), key_len(0), occupied(false), deleted(false) {}
        ~Entry() { 
            if (key) delete[] key; 
        }

        Entry(const Entry& other) : key(nullptr), key_len(other.key_len), 
                                     value(other.value), occupied(other.occupied), 
                                     deleted(other.deleted) {
            if (other.key && other.key_len > 0) {
                key = new char[key_len + 1];
                std::memcpy(key, other.key, key_len);
                key[key_len] = '\0';
            }
        }

        Entry& operator=(const Entry& other) {
            if (this != &other) {
                if (key) delete[] key;
                key = nullptr;
                key_len = other.key_len;
                value = other.value;
                occupied = other.occupied;
                deleted = other.deleted;
                if (other.key && other.key_len > 0) {
                    key = new char[key_len + 1];
                    std::memcpy(key, other.key, key_len);
                    key[key_len] = '\0';
                }
            }
            return *this;
        }
    };

    Entry* buckets_;
    size_t capacity_;
    size_t size_;
    static constexpr double LOAD_FACTOR = 0.5;

    size_t hash1(const char* str, size_t len) const {
        size_t h = 5381;
        for (size_t i = 0; i < len; ++i) {
            h = ((h << 5) + h) + static_cast<unsigned char>(str[i]);
        }
        return h % capacity_;
    }

    size_t hash2(const char* str, size_t len) const {
        size_t h = 0;
        for (size_t i = 0; i < len; ++i) {
            h = h * 37 + static_cast<unsigned char>(str[i]);
        }
        return (h % (capacity_ - 1)) + 1;
    }

    bool keys_equal(const char* k1, size_t len1, const char* k2, size_t len2) const {
        if (len1 != len2) return false;
        return std::memcmp(k1, k2, len1) == 0;
    }

    void rehash() {
        size_t old_capacity = capacity_;
        Entry* old_buckets = buckets_;

        capacity_ = capacity_ * 2;
        buckets_ = new Entry[capacity_];
        size_ = 0;

        for (size_t i = 0; i < old_capacity; ++i) {
            if (old_buckets[i].occupied && !old_buckets[i].deleted) {
                insert(old_buckets[i].key, old_buckets[i].key_len, old_buckets[i].value);
            }
        }

        delete[] old_buckets;
    }

public:
    StringMap(size_t initial_capacity = 16384) 
        : capacity_(initial_capacity), size_(0) {
        buckets_ = new Entry[capacity_];
    }

    ~StringMap() {
        delete[] buckets_;
    }

    StringMap(const StringMap&) = delete;
    StringMap& operator=(const StringMap&) = delete;

    void insert(const char* key, size_t key_len, const V& value) {
        if (static_cast<double>(size_ + 1) / capacity_ > LOAD_FACTOR) {
            rehash();
        }

        size_t h1 = hash1(key, key_len);
        size_t h2 = hash2(key, key_len);
        size_t idx = h1;

        for (size_t i = 0; i < capacity_; ++i) {
            if (!buckets_[idx].occupied || buckets_[idx].deleted) {
                if (buckets_[idx].key) delete[] buckets_[idx].key;
                buckets_[idx].key = new char[key_len + 1];
                std::memcpy(buckets_[idx].key, key, key_len);
                buckets_[idx].key[key_len] = '\0';
                buckets_[idx].key_len = key_len;
                buckets_[idx].value = value;
                buckets_[idx].occupied = true;
                buckets_[idx].deleted = false;
                ++size_;
                return;
            }
            if (keys_equal(buckets_[idx].key, buckets_[idx].key_len, key, key_len)) {
                buckets_[idx].value = value;
                return;
            }
            idx = (idx + h2) % capacity_;
        }
    }

    void insert(const std::string& key, const V& value) {
        insert(key.c_str(), key.size(), value);
    }

    V* find(const char* key, size_t key_len) {
        size_t h1 = hash1(key, key_len);
        size_t h2 = hash2(key, key_len);
        size_t idx = h1;

        for (size_t i = 0; i < capacity_; ++i) {
            if (!buckets_[idx].occupied) {
                return nullptr;
            }
            if (buckets_[idx].occupied && !buckets_[idx].deleted && 
                keys_equal(buckets_[idx].key, buckets_[idx].key_len, key, key_len)) {
                return &buckets_[idx].value;
            }
            idx = (idx + h2) % capacity_;
        }
        return nullptr;
    }

    V* find(const std::string& key) {
        return find(key.c_str(), key.size());
    }

    const V* find(const std::string& key) const {
        return const_cast<StringMap*>(this)->find(key.c_str(), key.size());
    }

    V& get_or_create(const std::string& key) {
        V* val = find(key);
        if (val) return *val;
        insert(key, V());
        return *find(key);
    }

    bool contains(const std::string& key) const {
        return const_cast<StringMap*>(this)->find(key) != nullptr;
    }

    size_t size() const { return size_; }

    template<typename Func>
    void for_each(Func func) const {
        for (size_t i = 0; i < capacity_; ++i) {
            if (buckets_[i].occupied && !buckets_[i].deleted) {
                func(std::string(buckets_[i].key, buckets_[i].key_len), buckets_[i].value);
            }
        }
    }
};

#endif
