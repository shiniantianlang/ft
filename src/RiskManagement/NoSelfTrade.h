// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#ifndef FT_INCLUDE_RISKMANAGEMENT_NOSELFTRADE_H_
#define FT_INCLUDE_RISKMANAGEMENT_NOSELFTRADE_H_

#include <spdlog/spdlog.h>

#include <string>
#include <vector>

#include "ContractTable.h"
#include "RiskManagement/RiskRuleInterface.h"

namespace ft {

// 拦截自成交订单，检查相反方向的挂单
// 1. 市价单
// 2. 非市价单的其他订单，且价格可以成功撮合的
class NoSelfTradeRule : public RiskRuleInterface {
 public:
  explicit NoSelfTradeRule(const TradingPanel* panel) : panel_(panel) {}

  bool check(const OrderReq* order) override {
    const auto* contract = ContractTable::get_by_index(order->ticker_index);
    assert(contract);

    uint64_t opp_d = opp_direction(order->direction);  // 对手方
    const Order* pending_order;
    std::vector<const Order*> order_list;
    panel_->get_order_list(&order_list, contract->ticker);

    for (auto p : order_list) {
      pending_order = p;
      if (pending_order->direction != opp_d) continue;

      // 存在市价单直接拒绝
      if (pending_order->type == OrderType::MARKET) goto catch_order;

      if (order->direction == Direction::BUY) {
        if (order->price >= pending_order->price + 1e-5) goto catch_order;
      } else {
        if (order->price <= pending_order->price - 1e-5) goto catch_order;
      }
    }

    return true;

  catch_order:
    spdlog::error(
        "[RiskMgr] Self trade! Ticker: {}. This Order: "
        "[Direction: {}, Type: {}, Price: {:.2f}]. "
        "Pending Order: [Direction: {}, Type: {}, Price: {:.2f}]",
        contract->ticker, direction_str(order->direction),
        ordertype_str(order->type), order->price,
        direction_str(pending_order->direction),
        ordertype_str(pending_order->type), pending_order->price);
    return false;
  }

 private:
  const TradingPanel* panel_;
};

}  // namespace ft

#endif  // FT_INCLUDE_RISKMANAGEMENT_NOSELFTRADE_H_
