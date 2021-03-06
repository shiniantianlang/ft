// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#ifndef FT_SRC_GATEWAY_XTP_XTPTRADEAPI_H_
#define FT_SRC_GATEWAY_XTP_XTPTRADEAPI_H_

#include <xtp_trader_api.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "Core/LoginParams.h"
#include "Core/Position.h"
#include "Core/Protocol.h"
#include "Core/TradingEngineInterface.h"
#include "Gateway/Xtp/XtpCommon.h"

namespace ft {

class XtpTradeApi : public XTP::API::TraderSpi {
 public:
  explicit XtpTradeApi(TradingEngineInterface* engine);

  bool login(const LoginParams& params);

  void logout();

  bool send_order(const OrderReq* order);

  bool cancel_order(uint64_t order_id);

  bool query_position(const std::string& ticker);

  bool query_positions();

  bool query_account();

  bool query_orders();

  bool query_trades();

  void OnOrderEvent(XTPOrderInfo* order_info, XTPRI* error_info,
                    uint64_t session_id) override;

  void OnTradeEvent(XTPTradeReport* trade_info, uint64_t session_id) override;

  void OnCancelOrderError(XTPOrderCancelInfo* cancel_info, XTPRI* error_info,
                          uint64_t session_id) override;

  void OnQueryPosition(XTPQueryStkPositionRsp* position, XTPRI* error_info,
                       int request_id, bool is_last,
                       uint64_t session_id) override;

 private:
  uint32_t next_client_order_id() { return next_client_order_id_++; }

  int next_req_id() { return next_req_id_++; }

  void done() { is_done_ = true; }

  void error() { is_error_ = true; }

  void reset_sync() { is_done_ = false; }

  bool wait_sync() {
    while (!is_done_)
      if (is_error_) return false;

    return true;
  }

 private:
  struct OrderDetail {
    const Contract* contract = nullptr;
    uint64_t order_id = 0;
    bool accepted_ack = false;
    int64_t original_vol = 0;
    int64_t traded_vol = 0;
    int64_t canceled_vol = 0;
  };

  TradingEngineInterface* engine_;
  std::unique_ptr<XTP::API::TraderApi, XtpApiDeleter> trade_api_;

  uint64_t session_id_ = 0;
  std::atomic<uint32_t> next_client_order_id_ = 1;
  std::atomic<uint32_t> next_req_id_ = 1;

  std::map<uint64_t, OrderDetail> order_details_;
  std::map<uint64_t, uint64_t> order_id_ft2xtp_;
  std::mutex order_mutex_;

  volatile bool is_done_ = false;
  volatile bool is_error_ = false;
  std::mutex query_mutex_;

  std::map<uint64_t, Position> pos_cache_;
};

}  // namespace ft

#endif  // FT_SRC_GATEWAY_XTP_XTPTRADEAPI_H_
