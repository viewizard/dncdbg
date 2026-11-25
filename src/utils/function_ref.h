// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_FUNCTION_REF_H
#define UTILS_FUNCTION_REF_H

#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

namespace dncdbg
{

// A non-owning reference to a callable with the signature R(Args...). Unlike std::function, it never allocates.
// The callable must outlive the FunctionRef that refers to it. Use it for callbacks that are only invoked
// during the call and never stored. Implicit construction lets lambdas and functions be passed directly,
// like std::function.
template <typename Signature>
class FunctionRef;

template <typename R, typename... Args>
class FunctionRef<R(Args...)>
{
  public:

    using FunctionPtr = R (*)(Args...);

    template <typename F, typename = std::enable_if_t<!std::is_same_v<F, FunctionRef> &&
                                                      !std::is_function_v<F> &&
                                                      !std::is_pointer_v<F> &&
                                                      std::is_invocable_r_v<R, const F &, Args...>>>
    FunctionRef(const F &callable) noexcept // NOLINT(cppcoreguidelines-explicit-constructor,misc-explicit-constructor)
        : m_callable(std::addressof(callable)),
          m_invoker(&InvokeObject<F>)
    {
    }

    // Plain functions have no object to point to, so the function pointer value itself is stored as a data pointer.
    FunctionRef(FunctionPtr function) noexcept // NOLINT(cppcoreguidelines-explicit-constructor,misc-explicit-constructor)
        : m_callable(ToDataPointer(function)),
          m_invoker(&InvokeFunction)
    {
    }

    R operator()(Args... args) const
    {
        return m_invoker(*this, std::forward<Args>(args)...);
    }

  private:

    using Invoker = R (*)(const FunctionRef &, Args...);

    template <typename F>
    static R InvokeObject(const FunctionRef &self, Args... args)
    {
        return (*static_cast<const F *>(self.m_callable))(std::forward<Args>(args)...);
    }

    static R InvokeFunction(const FunctionRef &self, Args... args)
    {
        return ToFunctionPtr(self.m_callable)(std::forward<Args>(args)...);
    }

    // Function pointers are converted through memcpy instead of reinterpret_cast, because conversion between
    // function pointers and object pointers is conditionally-supported in the standard.
    static const void *ToDataPointer(FunctionPtr function) noexcept
    {
        static_assert(sizeof(FunctionPtr) == sizeof(const void *), "Function pointer size must match data pointer size");
        const void *data = nullptr;
        std::memcpy(&data, &function, sizeof(data));
        return data;
    }

    static FunctionPtr ToFunctionPtr(const void *data) noexcept
    {
        FunctionPtr function = nullptr;
        std::memcpy(&function, &data, sizeof(function));
        return function;
    }

    // Holds either a pointer to the callable object or the function pointer value itself.
    // m_invoker determines how m_callable is interpreted, so no separate tag is needed.
    const void *m_callable;
    Invoker m_invoker;
};

} // namespace dncdbg

#endif // UTILS_FUNCTION_REF_H
