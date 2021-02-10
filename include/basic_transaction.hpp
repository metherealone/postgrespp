#pragma once

#include "query.hpp"
#include "socket_operations.hpp"
#include "type_encoder.hpp"
#include "utility.hpp"

#include <cassert>
#include <functional>
#include <tuple>

namespace postgrespp {

class basic_connection;

template <class T>
class basic_transaction : public socket_operations<basic_transaction<T>> {
  friend class socket_operations<basic_transaction<T>>;
public:
  using exec_handler_t = typename socket_operations<basic_transaction<T>>::exec_handler_t;
  using query_t = query;
  using commit_handler_t = std::function<void ()>;
  using rollback_handler_t = commit_handler_t;
  using connection_t = ::postgrespp::basic_connection;
  using statement_name_t = std::string;

public:
  basic_transaction(connection_t& c)
    : c_{c}
    , done_{false} {
  }

  basic_transaction(const basic_transaction&) = delete;
  basic_transaction(basic_transaction&& rhs) noexcept
    : c_{std::move(rhs.c_)}
    , done_{std::move(rhs.done_)} {
    rhs.done_ = true;
  }

  basic_transaction& operator=(const basic_transaction&) = delete;
  basic_transaction& operator=(basic_transaction&& rhs) noexcept {
    using std::swap;

    swap(c_, rhs.c_);
    swap(done_, rhs.done_);
  }

  /**
   * Destructor.
   * If neither \ref commit() nor \ref rollback() has been used, destructing
   * will do a sync rollback.
   */
  ~basic_transaction() {
    if (!done_) {
      const auto res = PQexec(connection().underlying_handle(), "ROLLBACK");
      assert(PGRES_COMMAND_OK == PQresultStatus(res));
    }
  }
  
  /// See \ref async_exec(query, handler, params) for more.
  void async_exec(const query_t& query, exec_handler_t handler) {
    async_exec_2(query, std::move(handler), nullptr, nullptr, nullptr, 0);
  }

  /// See \ref async_exec_prepared(statement_name, handler, params) for more.
  void async_exec_prepared(const statement_name_t& statement_name, exec_handler_t handler) {
    async_exec_prepared_2(statement_name, std::move(handler), nullptr, nullptr, nullptr, 0);
  }

  /**
   * Execute a query asynchronously.
   * \p query must contain a single query. For multiple queries, see
   * \ref async_exec_all(query, handler, params).
   * \p handler will be called once with the result.
   * \p params parameters to pass in the same order to $1, $2, ...
   */
  template <class... Params>
  void async_exec(const query_t& query, exec_handler_t handler, Params&&... params) {
    using namespace utility;

    const auto value_holders = create_value_holders(params...);
    const auto value_arr = std::apply([this](auto&&... args) { return value_array(args...); }, value_holders);
    const auto size_arr = size_array(params...);
    const auto type_arr = type_array(params...);

    async_exec_2(query, std::move(handler), value_arr.data(), size_arr.data(), type_arr.data(), sizeof...(params));
  }

  /**
   * Execute a query asynchronously.
   * \p statement_name prepared statement name.
   * \ref async_exec_all(query, handler, params).
   * \p handler will be called once with the result.
   * \p params parameters to pass in the same order to $1, $2, ...
   */
  template <class... Params>
  void async_exec_prepared(const statement_name_t& statement_name, exec_handler_t handler, Params&&... params) {
    using namespace utility;

    const auto value_holders = create_value_holders(params...);
    const auto value_arr = std::apply([this](auto&&... args) { return value_array(args...); }, value_holders);
    const auto size_arr = size_array(params...);
    const auto type_arr = type_array(params...);

    async_exec_prepared_2(statement_name, std::move(handler), value_arr.data(),
        size_arr.data(), type_arr.data(), sizeof...(params));
  }

  /**
   * Execute queries asynchronously.
   * Supports multiple queries in \p query, separated by ';' but does not
   * support parameter binding.
   * \p handler will be called once for each query and once more with an
   * empty result where \ref result.done() returns true.
   */
  void async_exec_all(const query_t& query, exec_handler_t handler) {
    assert(!done_);

    const auto res = PQsendQuery(connection().underlying_handle(),
        query.c_str());

    if (res != 1) {
      throw std::runtime_error{"error executing query: " + std::string{connection().last_error()}};
    }

    this->handle_exec_all(std::move(handler));
  }

  void commit(commit_handler_t handler) {
    async_exec("COMMIT", [this, handler = std::move(handler)](auto&& res) {
          done_ = true;
          handler();
        });
  }

  void rollback(rollback_handler_t handler) {
    async_exec("ROLLBACK", [this, handler = std::move(handler)](auto&& res) {
          done_ = true;
          handler();
        });
  }

protected:
  auto& socket() { return connection().socket(); }

  auto& connection() { return c_.get(); }

private:
  void async_exec_2(const query_t& query, exec_handler_t handler, const char* const* value_arr, const int* size_arr, const int* type_arr, std::size_t num_values) {
    assert(!done_);

    const auto res = PQsendQueryParams(connection().underlying_handle(),
        query.c_str(),
        num_values,
        nullptr,
        value_arr,
        size_arr,
        type_arr,
        1);

    if (res != 1) {
      throw std::runtime_error{"error executing query '" + query + "': " + std::string{connection().last_error()}};
    }

    this->handle_exec(std::move(handler));
  }

  void async_exec_prepared_2(const statement_name_t& statement_name, exec_handler_t handler, const char* const* value_arr, const int* size_arr, const int* type_arr, std::size_t num_values) {
    assert(!done_);

    const auto res = PQsendQueryPrepared(connection().underlying_handle(),
        statement_name.c_str(),
        num_values,
        value_arr,
        size_arr,
        type_arr,
        1);

    if (res != 1) {
      throw std::runtime_error{"error executing query '" + statement_name + "': " + std::string{connection().last_error()}};
    }

    this->handle_exec(std::move(handler));
  }

private:
  std::reference_wrapper<connection_t> c_;
  bool done_;
};

}