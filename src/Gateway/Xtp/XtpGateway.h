// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#ifndef FT_SRC_GATEWAY_XTP_XTPGATEWAY_H_
#define FT_SRC_GATEWAY_XTP_XTPGATEWAY_H_

#include <xtp_quote_api.h>
#include <xtp_trader_api.h>

#include <memory>
#include <string>

#include "Core/Gateway.h"
#include "Gateway/Xtp/XtpCommon.h"
#include "Gateway/Xtp/XtpTradeApi.h"
#include "Gateway/Xtp/XtpMdApi.h"

namespace ft {

class XtpGateway : public Gateway {
 public:
  explicit XtpGateway(TradingEngineInterface* engine);

  bool login(const LoginParams& params) override;

  void logout() override;

 private:
  TradingEngineInterface* engine_ = nullptr;
  std::unique_ptr<XtpTradeApi> trade_api_;
  std::unique_ptr<XtpMdApi> md_api_;
};

}  // namespace ft

#endif  // FT_SRC_GATEWAY_XTP_XTPGATEWAY_H_
