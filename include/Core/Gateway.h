// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#ifndef FT_INCLUDE_CORE_GATEWAY_H_
#define FT_INCLUDE_CORE_GATEWAY_H_

#include <map>
#include <memory>
#include <string>

#include "Core/LoginParams.h"
#include "Core/Protocol.h"
#include "Core/TradingEngineInterface.h"

namespace ft {

/*
 * Gateway的开发需要遵循以下规则：
 *
 * 构造函数接受TradingEngineInterface的指针，当特定行为触发时，回调engine中的函数
 * 具体规则参考TradingEngineInterface.h中的说明
 *
 * 实现Gateway基类中的虚函数，可全部实现可部分实现，根据需求而定
 *
 * 在Gateway.cpp中注册你的Gateway
 */
class Gateway {
 public:
  explicit Gateway(TradingEngineInterface* engine) {}

  virtual ~Gateway() {}

  virtual bool login(const LoginParams& params) { return false; }

  virtual void logout() {}

  virtual bool send_order(const OrderReq* order) { return false; }

  virtual bool cancel_order(uint64_t order_id) { return false; }

  virtual bool query_contract(const std::string& ticker) { return false; }

  virtual bool query_contracts() { return false; }

  virtual bool query_position(const std::string& ticker) { return false; }

  virtual bool query_positions() { return false; }

  virtual bool query_account() { return false; }

  virtual bool query_margin_rate(const std::string& ticker) { return false; }

  virtual bool query_commision_rate(const std::string& ticker) { return false; }
};

using __GATEWAY_CREATE_FUNC = std::function<Gateway*(TradingEngineInterface*)>;
std::map<std::string, __GATEWAY_CREATE_FUNC>& __get_api_map();
Gateway* create_gateway(const std::string& name,
                        TradingEngineInterface* engine);
void destroy_gateway(Gateway* api);

#define REGISTER_GATEWAY(name, type)                    \
  static inline ::ft::Gateway* __create_##type(         \
      ::ft::TradingEngineInterface* engine) {           \
    return new type(engine);                            \
  }                                                     \
  static inline bool __is_##type##_registered = [] {    \
    auto& type_map = ::ft::__get_api_map();             \
    auto res = type_map.emplace(name, __create_##type); \
    return res.second;                                  \
  }()

}  // namespace ft

#endif  // FT_INCLUDE_CORE_GATEWAY_H_
