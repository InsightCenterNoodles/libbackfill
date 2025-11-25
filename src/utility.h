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
    std::shared_ptr<const char>   m_data;
    const char*                   m_start_ptr = nullptr;
    size_t                        m_size      = 0;

    Bytes(std::shared_ptr<const char> d, size_t start, size_t len) noexcept
        : m_data(std::move(d)),
          m_start_ptr(m_data ? m_data.get() + start : nullptr),
          m_size(len) { }

public:
    DEFAULT_MOVE_COPY(Bytes);

    Bytes() = default;

    /// Take ownership of the pointer
    static Bytes take_ownership(const char*                ptr,
                                size_t                     size,
                                std::function<void(void*)> deleter) {
        auto data_ptr = std::shared_ptr<const char>(
            ptr, [=](const char* ptr) { deleter((void*)ptr); });

        return Bytes(std::move(data_ptr), 0, size);
    }

    static Bytes from_copy(std::span<const char> source) noexcept {
        // allocate mutable, fill, then cast to const for storage
        // we are using a non-standard use of array shared ptr here to
        // support any kind of pointer (such as above)

        char* ptr = new char[source.size()];

        auto mut =
            std::shared_ptr<char>(ptr, [=](const char* ptr) { delete[] ptr; });
        std::memcpy((void*)mut.get(), source.data(), source.size());
        return Bytes(std::move(mut), 0, source.size());
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

    std::shared_ptr<const char> const& shared_data() const noexcept {
        return m_data;
    }

    template <class T>
    ReinterpArray<T> as_typed(size_t start = 0,
                              size_t len   = SIZE_MAX) const noexcept;

    explicit operator bool() const noexcept { return m_start_ptr; }
};

template <class T>
class ReinterpArray {
    Bytes                         m_data;
    const T*                      m_start_ptr = nullptr;
    size_t                        m_size      = 0; // number of T elements

    ReinterpArray(Bytes d, const T* s, size_t count) noexcept
        : m_data(std::move(d)), m_start_ptr(s), m_size(count) { }

public:
    DEFAULT_MOVE_COPY(ReinterpArray);

    static ReinterpArray
    from_bytes(Bytes const& source, size_t offset, size_t len) noexcept {
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

// =============================================================================

template <class T>
class Owned;

template <class T>
class RefCounted {
    // we put this first to allow casting of a RefCounted* to a T*;
public:
    T item;
    mutable std::atomic<uint32_t> m_count = { 1 };

    // hide the destructor
    ~RefCounted() = default;

public:
    DISABLE_MOVE_COPY(RefCounted);

    template <class... Args>
    explicit RefCounted(Args&&... args) noexcept
        : item(std::forward<Args>(args)...) {
        static_assert(offsetof(RefCounted, item) == 0);
    }

    void retain() const noexcept {
        this->m_count.fetch_add(1, std::memory_order_relaxed);
    }

    void release() const noexcept {
        // remember this pulls the LAST value.
        if (this->m_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    [[nodiscard]] Owned<T> borrow();
};

template <class T>
class Owned {
    RefCounted<T>* m_ptr = nullptr;

    explicit Owned(RefCounted<T>* ptr) noexcept : m_ptr(ptr) { }

public:
    Owned() = default;

    /// Adopts a refptr; it does NOT retain
    [[nodiscard]] static Owned adopt(RefCounted<T>* p) noexcept {
        return Owned(p);
    }

    /// Borrows a refptr; this is the one you want most of the time. RETAINS.
    [[nodiscard]] static Owned retain(RefCounted<T>* p) noexcept {
        if (p) p->retain();
        return Owned(p);
    }

    Owned(Owned const& other) noexcept : m_ptr(other.m_ptr) {
        if (m_ptr) m_ptr->retain();
    }

    Owned(Owned&& other) noexcept
        : m_ptr(std::exchange(other.m_ptr, nullptr)) { }

    Owned& operator=(Owned other) noexcept {
        std::swap(other.m_ptr, m_ptr);
        return *this;
    }

    ~Owned() noexcept {
        if (m_ptr) m_ptr->release();
    }

    explicit operator bool() const noexcept { return !!m_ptr; }

    /// Do NOT store these pointers! Use map if possible!
    T*       get() noexcept { return (T*)m_ptr; }
    T const* get() const noexcept { return (T*)m_ptr; }

    T*       operator->() noexcept { return get(); }
    T&       operator*() noexcept { return *get(); }
    T const* operator->() const noexcept { return get(); }
    T const& operator*() const noexcept { return *get(); }

    RefCounted<T>* leak() noexcept { return std::exchange(m_ptr, nullptr); }
};

template <class T>
[[nodiscard]] Owned<T> RefCounted<T>::borrow() {
    return Owned<T>::retain(this);
}

template <class T, class... Args>
[[nodiscard]] Owned<T> make_refcounted(Args&&... args) {
    try {
        auto p = new RefCounted<T>(std::forward<Args>(args)...);
        return Owned<T>::adopt(p);
    } catch (std::exception const&) { return Owned<T>(); }
}

template <class T, class... Args>
[[nodiscard]] RefCounted<T>* make_refcounted_unsafe(Args&&... args) {
    try {
        return new RefCounted<T>(std::forward<Args>(args)...);
    } catch (std::exception const&) { return nullptr; }
}

#define C_BRIDGE(CTYPE, CPPTYPE)                                               \
    inline CPPTYPE* as_rc(CTYPE* ptr) {                                        \
        return (CPPTYPE*)ptr;                                                  \
    }                                                                          \
    inline CPPTYPE const* as_rc(CTYPE const* ptr) {                            \
        return (CPPTYPE const*)ptr;                                            \
    }                                                                          \
    inline CTYPE* from_rc(CPPTYPE* ptr) {                                      \
        return (CTYPE*)ptr;                                                    \
    }
