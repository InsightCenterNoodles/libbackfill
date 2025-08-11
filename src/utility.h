#pragma once

#include <filament/Engine.h>

#include <spdlog/spdlog.h>

#include <span>

#define DISABLE_MOVE_COPY(CNAME)                                               \
    CNAME(CNAME const&)            = delete;                                   \
    CNAME(CNAME&&)                 = delete;                                   \
    CNAME& operator=(CNAME const&) = delete;                                   \
    CNAME& operator=(CNAME&&)      = delete;

#define DEFAULT_MOVE_COPY(CNAME)                                               \
    CNAME(CNAME const&)            = default;                                  \
    CNAME(CNAME&&)                 = default;                                  \
    CNAME& operator=(CNAME const&) = default;                                  \
    CNAME& operator=(CNAME&&)      = default;

#define DISABLE_COPY(CNAME)                                                    \
    CNAME(CNAME const&)            = delete;                                   \
    CNAME& operator=(CNAME const&) = delete;

inline void expect(bool condition, const char* message) {
    if (!condition) {
        spdlog::error("Condition failed: {}", message);
        abort();
    }
}

template <class T>
class MoveVector {
    std::vector<T> m_content;

public:
    DISABLE_COPY(MoveVector);

    MoveVector() = default;

    MoveVector(std::vector<T>&& take) : m_content(take) { }

    MoveVector(MoveVector&&)            = default;
    MoveVector& operator=(MoveVector&&) = default;

    std::span<const T> span() const { return std::span<const T>(m_content); }

    size_t size() const { return m_content.size(); }

    T const* data() const { return m_content.data(); }
};

template <class T>
class SharedArray {
    std::shared_ptr<const T[]> m_data;
    size_t                     m_size = 0;

    SharedArray(std::shared_ptr<const T[]> d, size_t s)
        : m_data(d), m_size(s) { }

public:
    DEFAULT_MOVE_COPY(SharedArray);

    SharedArray() = default;

    inline static SharedArray from_copy(std::span<const T> source) {
        auto data = std::make_shared<T[]>(source.size());
        std::copy_n(source.begin(), source.size(), data.get());
        return SharedArray(data, source.size());
    }

    std::span<const T> span() const { return std::span(m_data.get(), m_size); }

    size_t size() const { return m_size; }

    T const* data() const { return m_data.get(); }
};

class Bytes {
    std::shared_ptr<const char[]> m_data;
    size_t                        m_size = 0;

    Bytes(std::shared_ptr<const char[]> d, size_t s) : m_data(d), m_size(s) { }

public:
    DEFAULT_MOVE_COPY(Bytes);

    Bytes() = default;

    inline static Bytes from_copy(std::span<const char> source) {
        auto data = std::make_shared<char[]>(source.size());
        std::memcpy(data.get(), source.data(), source.size());
        return Bytes(data, source.size());
    }

    std::span<const char> span() const {
        return std::span(m_data.get(), m_size);
    }

    size_t size() const { return m_size; }

    char const* data() const { return m_data.get(); }
};

template <class T>
struct ResourceDeleter {
    filament::Engine* pointer = nullptr;

    void operator()(T* p) { pointer->destroy(p); }
};

template <class T>
using UResource = std::unique_ptr<T, ResourceDeleter<T>>;

template <class T>
UResource<T> make_wrapper(filament::Engine* engine, T* t) {
    return UResource<T>(t, ResourceDeleter<T> { .pointer = engine });
}

// template <class T>
// struct EngineResourceWrapper {
//     DISABLE_COPY(EngineResourceWrapper);
//     filament::Engine* pointer = nullptr;
//     EngineResourceWrapper(filament::Engine* p) : pointer(p) { }
//     void operator()(T* p) { pointer->destroy(p); }
// };
