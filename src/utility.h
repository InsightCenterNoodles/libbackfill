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

// =============================================================================

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

// =============================================================================

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

// =============================================================================

template <class T>
class ReinterpArray;

class Bytes {
    std::shared_ptr<const char[]> m_data;
    const char*                   m_start_ptr = nullptr;
    size_t                        m_size      = 0;

    Bytes(std::shared_ptr<const char[]> d, size_t start, size_t len) noexcept
        : m_data(std::move(d)),
          m_start_ptr(m_data ? m_data.get() + start : nullptr),
          m_size(len) { }

public:
    DEFAULT_MOVE_COPY(Bytes);

    Bytes() = default;

    static Bytes from_copy(std::span<const char> source) noexcept {
        // allocate mutable, fill, then cast to const for storage
        auto mut = std::make_shared<char[]>(source.size());
        std::memcpy((void*)mut.get(), source.data(), source.size());
        auto imm = std::static_pointer_cast<const char[]>(mut);
        return Bytes(std::move(imm), 0, source.size());
    }

    [[nodiscard]]
    Bytes subspan(size_t start, size_t len) const noexcept {
        if (!m_data) return {}; // guard default/null case
        start = std::min(start, m_size);
        len   = std::min(len, m_size - start);

        const size_t base_offset =
            static_cast<size_t>(m_start_ptr - m_data.get());
        return Bytes(m_data, base_offset + start, len);
    }

    std::span<const char> span() const noexcept { return { data(), m_size }; }

    size_t size() const noexcept { return m_size; }

    const char* data() const noexcept { return m_start_ptr; }

    std::shared_ptr<const char[]> const& shared_data() const noexcept {
        return m_data;
    }

    template <class T>
    ReinterpArray<T> as_typed(size_t start = 0,
                              size_t len   = SIZE_T_MAX) const noexcept;

    explicit operator bool() const noexcept { return m_start_ptr; }
};

template <class T>
class ReinterpArray {
    std::shared_ptr<const char[]> m_data;
    const T*                      m_start_ptr = nullptr;
    size_t                        m_size      = 0; // number of T elements

    ReinterpArray(std::shared_ptr<const char[]> d,
                  const T*                      s,
                  size_t                        count) noexcept
        : m_data(std::move(d)), m_start_ptr(s), m_size(count) { }

public:
    DEFAULT_MOVE_COPY(ReinterpArray);

    static ReinterpArray
    from_bytes(const Bytes& source, size_t offset, size_t len) noexcept {
        if (!source.shared_data()) return {};

        // Clamp to available bytes starting at offset
        if (offset > source.size()) return {};
        len = std::min(len, source.size() - offset);

        // Compute typed start pointer
        const char* raw      = source.data() + offset;
        auto        raw_addr = reinterpret_cast<std::uintptr_t>(raw);
        if (raw_addr % alignof(T) != 0) return {};

        // Floor to whole T elements
        size_t count = len / sizeof(T);
        if (count == 0) return {};

        auto* ptr = reinterpret_cast<const T*>(raw);
        return ReinterpArray(source.shared_data(), ptr, count);
    }

    ReinterpArray() = default;

    std::span<const T> span() const noexcept { return { m_start_ptr, m_size }; }

    size_t size() const noexcept { return m_size; }

    const T* data() const noexcept { return m_start_ptr; }

    explicit operator bool() const noexcept { return m_start_ptr; }
};

template <class T>
ReinterpArray<T> Bytes::as_typed(size_t start, size_t len) const noexcept {
    return ReinterpArray<T>::from_bytes(*this, start, len);
}

// =============================================================================

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
