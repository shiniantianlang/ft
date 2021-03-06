// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#include "TradingSystem/TradingEngine.h"

#include "Core/ContractTable.h"
#include "Core/Protocol.h"

namespace ft {

TradingEngine::TradingEngine()
    : portfolio_("127.0.0.1", 6379),
      tick_redis_("127.0.0.1", 6379),
      order_redis_("127.0.0.1", 6379) {}

TradingEngine::~TradingEngine() {}

bool TradingEngine::login(const LoginParams& params) {
  if (is_logon_) return true;

  gateway_.reset(create_gateway(params.api(), this));
  if (!gateway_) {
    spdlog::error("[TradingEngine::login] Failed. Unknown gateway");
    return false;
  }

  if (!gateway_->login(params)) {
    spdlog::error("[TradingEngine::login] Failed to login");
    return false;
  }

  spdlog::info("[TradingEngine::login] Success. Login as {}",
               params.investor_id());

  if (!gateway_->query_account()) {
    spdlog::error("[TradingEngine::login] Failed to query account");
    return false;
  }

  // query all positions
  if (!gateway_->query_positions()) {
    spdlog::error("[TradingEngine::login] Failed to query positions");
    return false;
  }

  spdlog::info("[TradingEngine::login] Init done");

  is_logon_ = true;
  return true;
}

void TradingEngine::run() {
  spdlog::info("[TradingEngine::run] Start to recv order req");

  order_redis_.subscribe({TRADER_CMD_TOPIC});

  for (;;) {
    auto reply = order_redis_.get_sub_reply();
    auto cmd = reinterpret_cast<const TraderCommand*>(reply->element[2]->str);
    if (cmd->magic != TRADER_CMD_MAGIC) {
      spdlog::error("[TradingEngine::run] Recv unknown cmd: error magic num");
      continue;
    }

    switch (cmd->type) {
      case NEW_ORDER:
        spdlog::info("new order");
        send_order(cmd->order_req.ticker_index, cmd->order_req.volume,
                   cmd->order_req.direction, cmd->order_req.offset,
                   cmd->order_req.type, cmd->order_req.price);
        break;
      case CANCEL_ORDER:
        spdlog::info("cancel order");
        cancel_order(cmd->cancel_req.order_id);
        break;
      case CANCEL_TICKER:
        spdlog::info("cancel all for ticker");
        cancel_all_for_ticker(cmd->cancel_ticker_req.ticker_index);
        break;
      case CANCEL_ALL:
        spdlog::info("cancel all");
        cancel_all();
        break;
      default:
        spdlog::error("[StrategyEngine::run] Unknown cmd");
        break;
    }
  }
}

bool TradingEngine::send_order(uint64_t ticker_index, int volume,
                               uint64_t direction, uint64_t offset,
                               uint64_t type, double price) {
  if (!is_logon_) {
    spdlog::error("[TradingEngine::send_order] Failed. Not logon");
    return false;
  }

  auto contract = ContractTable::get_by_index(ticker_index);
  if (!contract) {
    spdlog::error("[TradingEngine::send_order] Contract not found");
    return false;
  }

  OrderReq req;
  req.order_id = next_order_id();
  req.ticker_index = ticker_index;
  req.direction = direction;
  req.offset = offset;
  req.volume = volume;
  req.type = type;
  req.price = price;

  std::unique_lock<std::mutex> lock(mutex_);
  if (risk_mgr_) {
    if (!risk_mgr_->check_order_req(&req)) {
      spdlog::error("风控未通过");
      return false;
    }
  }

  if (!gateway_->send_order(&req)) {
    spdlog::error(
        "[StrategyEngine::send_order] Failed to send_order."
        " Order: <Ticker: {}, OrderID: {}, Direction: {}, "
        "Offset: {}, OrderType: {}, Traded: {}, Total: {}, Price: {:.2f}, "
        "Status: Failed>",
        contract->ticker, req.order_id, direction_str(req.direction),
        offset_str(req.offset), ordertype_str(req.type), 0, req.volume,
        req.price);

    if (risk_mgr_) risk_mgr_->on_order_completed(req.order_id);

    return false;
  }

  if (risk_mgr_) risk_mgr_->on_order_sent(req.order_id);

  Order order;
  order.order_id = req.order_id;
  order.contract = contract;
  order.direction = direction;
  order.offset = offset;
  order.volume = volume;
  order.type = type;
  order.price = price;
  order.status = OrderStatus::SUBMITTING;
  order_map_.emplace(order.order_id, order);

  portfolio_.update_pending(contract->index, direction, offset, volume);

  spdlog::debug(
      "[StrategyEngine::send_order] Success."
      " Order: <Ticker: {}, OrderID: {}, Direction: {}, "
      "Offset: {}, OrderType: {}, Traded: {}, Total: {}, Price: {:.2f}, "
      "Status: {}>",
      contract->ticker, order.order_id, direction_str(order.direction),
      offset_str(order.offset), ordertype_str(order.type), 0, order.volume,
      order.price, to_string(order.status));
  return true;
}

void TradingEngine::cancel_order(uint64_t order_id) {
  gateway_->cancel_order(order_id);
}

void TradingEngine::cancel_all_for_ticker(uint64_t ticker_index) {
  std::unique_lock<std::mutex> lock(mutex_);
  for (const auto& [order_id, order] : order_map_) {
    if (ticker_index == order.contract->index) gateway_->cancel_order(order_id);
  }
}

void TradingEngine::cancel_all() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (const auto& [order_id, order] : order_map_) {
    gateway_->cancel_order(order_id);
  }
}

void TradingEngine::on_query_contract(const Contract* contract) {}

void TradingEngine::on_query_account(const Account* account) {
  spdlog::info(
      "[TradingEngine::on_query_account] Account ID: {}, Balance: {}, Fronzen: "
      "{}",
      account->account_id, account->balance, account->frozen);
}

void TradingEngine::on_query_position(const Position* position) {
  auto contract = ContractTable::get_by_index(position->ticker_index);
  assert(contract);

  auto& lp = position->long_pos;
  auto& sp = position->short_pos;
  spdlog::info(
      "[TradingEngine::on_query_position] Ticker: {}, "
      "Long Volume: {}, Long Price: {:.2f}, Long Frozen: {}, Long PNL: {}, "
      "Short Volume: {}, Short Price: {:.2f}, Short Frozen: {}, Short PNL: {}",
      contract->ticker, lp.volume, lp.cost_price, lp.frozen, lp.float_pnl,
      sp.volume, sp.cost_price, sp.frozen, sp.float_pnl);

  if (lp.volume == 0 && lp.frozen == 0 && sp.volume == 0 && sp.frozen == 0)
    return;

  portfolio_.set_position(position);
}

void TradingEngine::on_tick(const TickData* tick) {
  if (!is_logon_) return;

  auto contract = ContractTable::get_by_index(tick->ticker_index);
  if (!contract) {
    spdlog::error("[TradingEngine::process_tick] Contract not found");
    return;
  }

  tick_redis_.publish(proto_md_topic(contract->ticker), tick, sizeof(TickData));
  spdlog::debug("[TradingEngine::process_tick]");
}

void TradingEngine::on_order_accepted(uint64_t order_id) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto iter = order_map_.find(order_id);
  if (iter == order_map_.end()) {
    spdlog::error(
        "[TradingEngine::on_order_accepted] Order not found. OrderID: {}",
        order_id);
    return;
  }

  auto& order = iter->second;

  spdlog::info(
      "[TradingEngine::on_order_accepted] 报单委托成功. Ticker: {}, Direction: "
      "{}, Offset: {}, Volume: {}, Price: {:.2f}",
      order.contract->ticker, direction_str(order.direction),
      offset_str(order.offset), order.volume, order.price);
}

void TradingEngine::on_order_rejected(uint64_t order_id) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto iter = order_map_.find(order_id);
  if (iter == order_map_.end()) {
    spdlog::error(
        "[TradingEngine::on_order_rejected] Order not found. OrderID: {}",
        order_id);
    return;
  }
  auto& order = iter->second;

  spdlog::error(
      "[TradingEngine::on_order_rejected] 报单被拒. Ticker: {}, Direction: "
      "{}, Offset: {}, Volume: {}, Price: {:.2f}",
      order.contract->ticker, direction_str(order.direction),
      offset_str(order.offset), order.volume, order.price);

  order_map_.erase(iter);
}

void TradingEngine::on_order_traded(uint64_t order_id, int64_t this_traded,
                                    double traded_price) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto iter = order_map_.find(order_id);
  if (iter == order_map_.end()) {
    spdlog::error(
        "[TradingEngine::on_order_traded] Order not found. OrderID: {}, "
        "Traded: {}, Price: {}",
        order_id, this_traded, traded_price);
    return;
  }
  auto& order = iter->second;

  spdlog::info(
      "[TradingEngine::on_order_traded] 报单成交. Ticker: {}, Direction: {}, "
      "Offset: {}, Traded: {}, Price: {}",
      order.contract->ticker, direction_str(order.direction),
      offset_str(order.offset), this_traded, traded_price);

  portfolio_.update_traded(order.contract->index, order.direction, order.offset,
                           this_traded, traded_price);

  if (risk_mgr_)
    risk_mgr_->on_order_traded(order_id, this_traded, traded_price);

  order.traded_volume += this_traded;
  if (order.traded_volume + order.canceled_volume == order.volume) {
    spdlog::info(
        "[TradingEngine::on_order_traded] 报单完成. Ticker: {}, Direction: {}, "
        "Offset: {}, Traded/Original: {}/{}",
        order.contract->ticker, direction_str(order.direction),
        offset_str(order.offset), order.traded_volume, order.volume);

    // 订单结束，通知风控模块
    if (risk_mgr_) risk_mgr_->on_order_completed(order_id);

    order_map_.erase(iter);
  }
}

void TradingEngine::on_order_canceled(uint64_t order_id,
                                      int64_t canceled_volume) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto iter = order_map_.find(order_id);
  if (iter == order_map_.end()) {
    spdlog::error(
        "[TradingEngine::on_order_canceled] Order not found. OrderID: {}",
        order_id);
    return;
  }
  auto& order = iter->second;

  spdlog::info(
      "[TradingEngine::on_order_canceled] 报单已撤. Ticker: {}, Direction: {}, "
      "Offset: {}, Canceled: {}",
      order.contract->ticker, direction_str(order.direction),
      offset_str(order.offset), canceled_volume);

  order.canceled_volume = canceled_volume;
  if (order.traded_volume + order.canceled_volume == order.volume) {
    spdlog::info(
        "[TradingEngine::on_order_canceled] 报单完成. Ticker: {}, Direction: "
        "{}, Offset: {}, Traded/Original: {}/{}",
        order.contract->ticker, direction_str(order.direction),
        offset_str(order.offset), order.traded_volume, order.volume);

    // 订单结束，通知风控模块
    if (risk_mgr_) risk_mgr_->on_order_completed(order_id);

    order_map_.erase(iter);
  }
}

void TradingEngine::on_order_cancel_rejected(uint64_t order_id) {
  spdlog::warn(
      "[TradingEngine::on_order_cancel_rejected] Order cannot be canceled. "
      "OrderID: {}",
      order_id);
}

}  // namespace ft
