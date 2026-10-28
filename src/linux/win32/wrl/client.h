#pragma once
// Linux only: Microsoft::WRL::ComPtr for the COM-shaped interfaces in src/linux/win32.
#include "../win32_base.hpp"

#include <memory>
#include <utility>

namespace Microsoft::WRL {
template <class T> class ComPtr;

// &pointer releases what it holds and stands for its address, as WRL's operator& does.
template <class T>
class ComPtrRef {
public:
    explicit ComPtrRef(ComPtr<T>* owner) : owner_(owner) {}
    operator T**() { return owner_->ReleaseAndGetAddressOf(); }
    operator void**() { return reinterpret_cast<void**>(owner_->ReleaseAndGetAddressOf()); }
    operator IUnknown**() requires (!std::is_same_v<T, IUnknown>) { return reinterpret_cast<IUnknown**>(owner_->ReleaseAndGetAddressOf()); }
    operator ComPtr<T>*() { owner_->Reset(); return owner_; }
    T* operator*() { return owner_->Get(); }
    T** ReleaseAndGetAddressOf() { return owner_->ReleaseAndGetAddressOf(); }
    ComPtr<T>* owner() const { return owner_; }
private:
    ComPtr<T>* owner_;
};

template <class T>
class ComPtr {
public:
    using InterfaceType = T;
    ComPtr() noexcept = default;
    ComPtr(std::nullptr_t) noexcept {}
    template <class U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr(U* pointer) noexcept : pointer_(pointer) { if (pointer_) pointer_->AddRef(); }
    ComPtr(const ComPtr& other) noexcept : pointer_(other.pointer_) { if (pointer_) pointer_->AddRef(); }
    template <class U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr(const ComPtr<U>& other) noexcept : pointer_(other.Get()) { if (pointer_) pointer_->AddRef(); }
    ComPtr(ComPtr&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
    template <class U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr(ComPtr<U>&& other) noexcept : pointer_(other.Detach()) {}
    ~ComPtr() { Reset(); }

    ComPtr& operator=(std::nullptr_t) noexcept { Reset(); return *this; }
    ComPtr& operator=(T* pointer) noexcept { if (pointer != pointer_) { ComPtr(pointer).Swap(*this); } return *this; }
    ComPtr& operator=(const ComPtr& other) noexcept { if (other.pointer_ != pointer_) ComPtr(other).Swap(*this); return *this; }
    template <class U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr& operator=(const ComPtr<U>& other) noexcept { ComPtr(other).Swap(*this); return *this; }
    ComPtr& operator=(ComPtr&& other) noexcept { ComPtr(std::move(other)).Swap(*this); return *this; }
    template <class U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    ComPtr& operator=(ComPtr<U>&& other) noexcept { ComPtr(std::move(other)).Swap(*this); return *this; }

    T* Get() const noexcept { return pointer_; }
    T* operator->() const noexcept { return pointer_; }
    explicit operator bool() const noexcept { return pointer_ != nullptr; }
    T* const* GetAddressOf() const noexcept { return &pointer_; }
    T** GetAddressOf() noexcept { return &pointer_; }
    T** ReleaseAndGetAddressOf() noexcept { Reset(); return &pointer_; }
    T* Detach() noexcept { return std::exchange(pointer_, nullptr); }
    void Attach(T* pointer) noexcept { Reset(); pointer_ = pointer; }
    unsigned long Reset() noexcept {
        unsigned long left = 0;
        if (auto* old = std::exchange(pointer_, nullptr)) left = old->Release();
        return left;
    }
    void Swap(ComPtr& other) noexcept { std::swap(pointer_, other.pointer_); }
    void Swap(ComPtr&& other) noexcept { std::swap(pointer_, other.pointer_); }
    HRESULT CopyTo(T** out) const noexcept {
        if (!out) return E_POINTER;
        *out = pointer_;
        if (pointer_) pointer_->AddRef();
        return S_OK;
    }
    template <class U> HRESULT CopyTo(U** out) const noexcept {
        if (!pointer_) { if (out) *out = nullptr; return E_POINTER; }
        return pointer_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out));
    }
    template <class U> HRESULT As(ComPtr<U>* out) const noexcept {
        if (!out) return E_POINTER;
        out->Reset();
        if (!pointer_) return E_POINTER;
        return pointer_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out->GetAddressOf()));
    }
    template <class U> HRESULT As(ComPtr<U>& out) const noexcept { return As(std::addressof(out)); }
    template <class U> HRESULT As(ComPtrRef<U> out) const noexcept { return As(out.owner()); }

    ComPtrRef<T> operator&() noexcept { return ComPtrRef<T>(this); }

private:
    T* pointer_ = nullptr;
};

template <class T, class U> bool operator==(const ComPtr<T>& a, const ComPtr<U>& b) noexcept { return a.Get() == b.Get(); }
template <class T> bool operator==(const ComPtr<T>& a, std::nullptr_t) noexcept { return a.Get() == nullptr; }
template <class T, class U> bool operator==(const ComPtr<T>& a, U* b) noexcept { return a.Get() == b; }
} // namespace Microsoft::WRL


// IID_PPV_ARGS(&pointer) on a ComPtr.
namespace lm_com {
template <class T> constexpr const GUID& iidOf(Microsoft::WRL::ComPtrRef<T>) { return T::lm_iid; }
template <class T> void** ppv(Microsoft::WRL::ComPtrRef<T> pointer) { return reinterpret_cast<void**>(pointer.ReleaseAndGetAddressOf()); }
}
