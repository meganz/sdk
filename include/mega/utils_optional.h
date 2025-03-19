/**
 * @file
 * @brief Utilities around std::optional
 */

#ifndef INCLUDE_MEGA_UTILS_OPTIONAL_H_
#define INCLUDE_MEGA_UTILS_OPTIONAL_H_

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace mega
{

template<typename T>
struct is_std_optional: std::false_type
{};

template<typename T>
struct is_std_optional<std::optional<T>>: std::true_type
{};

template<typename T>
constexpr bool is_std_optional_v = is_std_optional<T>::value;

/**
 * @brief Helper struct to mimic std::optional monadic operations from C++23
 * This documentation applies to:
 * - Transform (struct), transform (function)
 * - AndThen (struct), and_then (function)
 * - OrElse (struct), or_else (function)
 *
 * See usage examples at utils_optional_test.cpp
 *
 * These implementations replace the calls to methods with the pipe operators (|), similar to how
 * range views can be manipulated since C++20.
 * @example
 *     std::string valPlus1 = (std::optional<std::string> {"5"}
 *          | or_else([]{ return std::optional{""s}; })
 *          | and_then(toIntOpt)
 *          | transform([](int n) { return n + 1; })
 *          | transform([](int n) { return std::to_string(n); }))
 *          .value_or("NaN"s);
 *
 * @note Remember to deprecate these monadic utilities in favor of std::optional methods once C++23
 * is adopted in the project.
 */
template<typename F>
struct Transform
{
    Transform(F&& t):
        transformOp{std::forward<F>(t)}
    {}

    Transform(const F& t):
        transformOp{t}
    {}

    template<typename OptT>
    auto operator()(OptT&& optionalT) -> std::optional<
        std::invoke_result_t<F, typename std::remove_reference_t<OptT>::value_type>>
    {
        using InputOptType = std::remove_cv_t<std::remove_reference_t<OptT>>;
        static_assert(is_std_optional_v<InputOptType>,
                      "Transform must be invoked with a std::optional");
        using InputType = typename InputOptType::value_type;
        static_assert(std::is_invocable_v<F, InputType>,
                      "Transform callable must be invocable with the optional's value type.");
        if (!optionalT.has_value())
            return std::nullopt;
        return std::optional{std::invoke(transformOp, std::forward<OptT>(optionalT).value())};
    }

private:
    F transformOp;
};

template<typename F>
Transform<std::decay_t<F>> transform(F&& transformOp)
{
    return Transform<std::decay_t<F>>(std::forward<F>(transformOp));
}

template<typename OptT, typename F>
auto operator|(OptT&& optionalT, Transform<F>&& op) -> decltype(auto)
{
    return op(std::forward<OptT>(optionalT));
}

/**
 * @brief Helper struct to mimic std::optional::or_else from C++23
 * See usage examples at utils_optional_test.cpp
 * @note Remember to deprecate this class in favor of std::optional::or_else once C++23 is used in
 * the project.
 */
template<typename F>
struct OrElse
{
    OrElse(F&& t):
        transformOp{std::forward<F>(t)}
    {}

    OrElse(const F& t):
        transformOp{t}
    {}

    template<typename OptT>
    std::decay_t<OptT> operator()(OptT&& optionalT)
    {
        using InputOptType = std::remove_cv_t<std::remove_reference_t<OptT>>;
        static_assert(is_std_optional_v<InputOptType>,
                      "OrElse must be invoked with a std::optional");
        using ResultType = std::invoke_result_t<F>;
        static_assert(is_std_optional_v<ResultType>,
                      "OrElse callable must return a std::optional.");
        static_assert(std::is_invocable_v<F>,
                      "OrElse callable must be invocable without parameters.");

        if (optionalT.has_value())
            return std::forward<OptT>(optionalT);
        return transformOp();
    }

private:
    F transformOp;
};

template<typename F>
OrElse<std::decay_t<F>> or_else(F&& transformOp)
{
    return OrElse<std::decay_t<F>>(std::forward<F>(transformOp));
}

template<typename OptT, typename F>
auto operator|(OptT&& optionalT, OrElse<F>&& op) -> decltype(auto)
{
    return op(std::forward<OptT>(optionalT));
}

/**
 * @brief Helper struct to mimic std::optional::and_then from C++23
 * See usage examples at utils_optional_test.cpp
 * @note Remember to deprecate this class in favor of std::optional::and_then once C++23 is used in
 * the project.
 */
template<typename F>
struct AndThen
{
    AndThen(F&& t):
        transformOp{std::forward<F>(t)}
    {}

    AndThen(const F& t):
        transformOp{t}
    {}

    template<typename OptT>
    auto operator()(OptT&& optionalT)
        -> std::invoke_result_t<F, typename std::remove_reference_t<OptT>::value_type>
    {
        using InputOptType = std::remove_cv_t<std::remove_reference_t<OptT>>;
        static_assert(is_std_optional_v<InputOptType>,
                      "AndThen must be invoked with a std::optional");
        using InputType = typename InputOptType::value_type;
        using ResultType = std::invoke_result_t<F, InputType>;
        static_assert(std::is_invocable_v<F, InputType>,
                      "AndThen callable must be invocable with the optional's value type.");
        static_assert(is_std_optional_v<ResultType>,
                      "AndThen callable must return a std::optional.");

        if (optionalT.has_value())
            return std::invoke(transformOp, std::forward<OptT>(optionalT).value());
        return std::nullopt;
    }

private:
    F transformOp;
};

template<typename F>
AndThen<std::decay_t<F>> and_then(F&& transformOp)
{
    return AndThen<std::decay_t<F>>(std::forward<F>(transformOp));
}

template<typename OptT, typename F>
auto operator|(OptT&& optionalT, AndThen<F>&& op) -> decltype(auto)
{
    return op(std::forward<OptT>(optionalT));
}
}

#endif // INCLUDE_MEGA_UTILS_OPTIONAL_H_
