#pragma once

#include <stdexcept>
#include <optional>
#include <utility>

template <typename TOk>
struct Result_ok {
    TOk data;
};

template <typename TError>
struct Result_err {
    TError data;
};

struct ResultInit {
    template<typename TOk>
    static Result_ok<TOk> ok(TOk&& data) {
        return Result_ok<TOk> { std::move(data) };
    }

    template<typename TError>
    static Result_err<TError> err(TError&& data) {
        return Result_err<TError> { std::move(data) };
    }
};

template <typename TOk, typename TError>
struct Result {
private:
    bool is_ok;
    std::optional<TOk> ok_data;
    std::optional<TError> error_data;
    Result() : is_ok(false) {}
public:
    Result(Result_ok<TOk>&& data) : is_ok(true), ok_data(std::move(data.data)) {}
    Result(Result_err<TError>&& data) : is_ok(false), error_data(std::move(data.data)) {}

    static Result success(TOk&& data) {
        Result result;
        result.is_ok = true;
        result.ok_data = std::move(data);

        return result;
    }

    static Result fail(TError&& data) {
        Result result;
        result.is_ok = false;
        result.error_data = std::move(data);

        return result;
    }

    bool isOk() const {
        return is_ok;
    }

    const TOk& getOkRef() const & {
        if (!is_ok) {
            throw std::logic_error("Result::getOkRef called on errored result");
        }

        return *ok_data;
    }

    const TError& getErrRef() const & {
        if (is_ok) {
            throw std::logic_error("Result::getErrRef called on successful result");
        }

        return *error_data;
    }

    TOk&& getOkRef() && {
        if (!is_ok) {
            throw std::logic_error("Result::getOkRef called on errored result");
        }

        return *move(ok_data);
    }

    TError&& getErrRef() && {
        if (is_ok) {
            throw std::logic_error("Result::getErrRef called on successful result");
        }

        return *move(error_data);
    }
};
