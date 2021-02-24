#pragma once

#include "work.hpp"
#include "statement_name.hpp"

#include <boost/asio/async_result.hpp>

#include <memory>
#include <utility>

namespace postgrespp {

/**
 * Asynchronously executes a prepared query.
 * This function must not be called again before the handler is called.
 */
template <class RWT, class IsolationT, class ResultCallableT, class... Params>
auto async_exec_prepared(basic_transaction<RWT, IsolationT>& t, const statement_name& name,
    ResultCallableT&& handler, Params&&... params) {
  return t.async_exec_prepared(name, std::forward<ResultCallableT>(handler), std::forward<Params>(params)...);
}

/**
 * Starts a transaction, asynchronously executes a prepared query and commits
 * the transaction.
 * This function must not be called again before the handler is called.
 */
template <class ResultCallableT, class... Params>
auto async_exec_prepared(basic_connection& c, statement_name name,
    ResultCallableT&& handler, Params... params) {
  auto initiation = [name = std::move(name), params...](auto&& handler, basic_connection& c) mutable {
   c.template async_transaction<>([
      handler = std::move(handler),
      name = std::move(name),
      params...](auto txn) mutable {
        auto s_txn = std::make_shared<work>(std::move(txn));

        auto wrapped_handler = [handler = std::move(handler), s_txn](auto&& result) mutable {
            if (result.ok()) {
              s_txn->commit([s_txn, handler = std::move(handler), result = std::move(result)]
                            (auto&& commit_result) mutable {
                  if (commit_result.ok()) {
                    handler(std::move(result));
                  } else {
                    handler(std::move(commit_result));
                  }
                });
            } else {
              handler(std::move(result));
            }
          };

        async_exec_prepared(*s_txn, name, std::move(wrapped_handler),
            std::move(params)...);
      });
  };

  return boost::asio::async_initiate<
    ResultCallableT, void(result)>(
        initiation, handler, std::ref(c));
}

}
