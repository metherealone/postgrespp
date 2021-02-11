#pragma once

#include "work.hpp"
#include "query.hpp"

#include <memory>
#include <utility>

namespace postgrespp {

/**
 * Asynchronously executes a query.
 * This function must not be called again before the handler is called.
 */
template <class RWT, class IsolationT, class TransactionHandlerT, class... Params>
void async_exec(basic_transaction<RWT, IsolationT>& t, const query& query,
    TransactionHandlerT&& handler, Params&&... params) {
  t.async_exec(query, std::move(handler), std::forward<Params>(params)...);
}

/**
 * Starts a transaction, asynchronously executes a query and commits the transaction.
 * This function must not be called again before the handler is called.
 */
template <class ResultCallableT, class... Params>
void async_exec(basic_connection& c, query query,
    ResultCallableT&& handler, Params... params) {
  c.template async_transaction<>([handler = std::move(handler), query = std::move(query),
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

        async_exec(*s_txn, query, std::move(wrapped_handler),
            std::move(params)...);
      });
}

}