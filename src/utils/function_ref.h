// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_FUNCTION_REF_H
#define UTILS_FUNCTION_REF_H

#include <memory>
#include <type_traits>
#include <utility>

namespace dncdbg
{

// A non-owning reference to a callable with the signature R(Args...). Unlike std::function, it never allocates.
// The callable must outlive the FunctionRef that refers to it.
template <typename Signature>
class FunctionRef;

template <typename R, typename... Args>
class FunctionRef<R(Args...)>
{
  public:

    template <typename F, typename = std::enable_if_t<!std::is_same_v<F, FunctionRef> &&
                                                      std::is_invocable_r_v<R, const F &, Args...>>>
    explicit FunctionRef(const F &callable) noexcept
        : m_target(std::addressof(callable)),
          m_invoker(&Invoke<F>)
    {
    }

    R operator()(Args... args) const
    {
        return m_invoker(m_target, std::forward<Args>(args)...);
    }

  private:

    using Invoker = R (*)(const void *, Args...);

    template <typename F>
    static R Invoke(const void *target, Args... args)
    {
        return (*static_cast<const F *>(target))(std::forward<Args>(args)...);
    }

    const void *m_target;
    Invoker m_invoker;
};

} // namespace dncdbg

#endif // UTILS_FUNCTION_REF_H
