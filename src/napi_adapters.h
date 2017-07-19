#ifndef SRC_NAPI_ADAPTERS_H_
#define SRC_NAPI_ADAPTERS_H_

#define NAPI_DISABLE_CPP_EXCEPTIONS
#include "napi.h"
#include "node_api_internals.h"
#include "node.h"
#include "util.h"
#include "string_bytes.h"

#define NODE_API_MODULE_BUILTIN(modname, regfunc)         \
  void __napi_ ## regfunc(napi_env env,                   \
                          napi_value exports,             \
                          napi_value module,              \
                          void* priv) {                   \
    Napi::RegisterModule(env, exports, module, regfunc);  \
  }                                                       \
  NAPI_MODULE_X(modname, __napi_ ## regfunc, nullptr, NM_F_BUILTIN);

namespace node_api
{

inline Napi::Value ErrnoException(Napi::Env env,
                                  int errorno,
                                  const char* syscall = NULL,
                                  const char* message = NULL,
                                  const char* path = NULL) {
  v8::Local<v8::Value> ex = node::ErrnoException(
      V8IsolateFromNapiEnv(env),
      errorno,
      syscall,
      message,
      path);
  return Napi::Value(env, JsValueFromV8LocalValue(ex));
}

inline Napi::Value UVException(Napi::Env env,
                               int errorno,
                               const char* syscall = NULL,
                               const char* message = NULL,
                               const char* path = NULL) {
  v8::Local<v8::Value> ex = node::UVException(
      V8IsolateFromNapiEnv(env),
      errorno,
      syscall,
      message,
      path);
  return Napi::Value(env, JsValueFromV8LocalValue(ex));
}

inline Napi::Value UVException(Napi::Env env,
                               int errorno,
                               const char* syscall,
                               const char* message,
                               const char* path,
                               const char* dest) {
  v8::Local<v8::Value> ex = node::UVException(
      V8IsolateFromNapiEnv(env),
      errorno,
      syscall,
      message,
      path,
      dest);
  return Napi::Value(env, JsValueFromV8LocalValue(ex));
}

inline Napi::String EncodeString(Napi::Env env,
                                 const char* buf,
                                 size_t buflen,
                                 enum node::encoding encoding,
                                 Napi::Value* error) {
  v8::Local<v8::Value> v8error;
  v8::MaybeLocal<v8::Value> ret = node::StringBytes::Encode(
    V8IsolateFromNapiEnv(env), buf, buflen, encoding, &v8error);
  *error = Napi::Value(env, JsValueFromV8LocalValue(v8error));
  return Napi::String(env, ret.IsEmpty() ? nullptr :
    JsValueFromV8LocalValue(ret.ToLocalChecked()));
}

inline Napi::String EncodeString(Napi::Env env,
                                 const char* buf,
                                 enum node::encoding encoding,
                                 Napi::Value* error) {
  v8::Local<v8::Value> v8error;
  v8::MaybeLocal<v8::Value> ret = node::StringBytes::Encode(
      V8IsolateFromNapiEnv(env), buf, encoding, &v8error);
  *error = Napi::Value(env, JsValueFromV8LocalValue(v8error));
  return Napi::String(env, ret.IsEmpty() ? nullptr :
      JsValueFromV8LocalValue(ret.ToLocalChecked()));
}

inline enum node::encoding ParseEncoding(
    Napi::Env env,
    Napi::Value encoding_v,
    enum node::encoding default_encoding = node::LATIN1) {
  return node::ParseEncoding(V8IsolateFromNapiEnv(env),
                             V8LocalValueFromJsValue(encoding_v),
                             default_encoding);
}

inline void MakeAsyncCallback(node::AsyncWrap* asyncWrap,
                              Napi::String symbol,
                              int argc,
                              napi_value* argv) {
  asyncWrap->MakeCallback(
      V8LocalValueFromJsValue(symbol).As<v8::String>(),
      argc,
      reinterpret_cast<v8::Local<v8::Value>*>(argv));
}

inline node::BufferValue BufferValue(Napi::Env env, Napi::Value value) {
  return node::BufferValue(V8IsolateFromNapiEnv(env),
                           V8LocalValueFromJsValue(value));
}

class NodeEnvironment {
public:
  NodeEnvironment(Napi::Env env)
    : _env(env),
      _node_env(NodeEnvironmentFromNapiEnv(env)) {
  }

  Napi::Function push_values_to_array_function() const {
    return Napi::Function(
      _env,
      JsValueFromV8LocalValue(_node_env->push_values_to_array_function()));
  }

  Napi::String oncomplete_string() const {
    return Napi::String(
      _env,
      JsValueFromV8LocalValue(_node_env->oncomplete_string()));
  }

  #define NODE_ENV_METHOD(ret, fn) \
    ret fn() const { return _node_env->fn(); }

  NODE_ENV_METHOD(double*, fs_stats_field_array);
  NODE_ENV_METHOD(uv_loop_t*, event_loop);
  NODE_ENV_METHOD(void, PrintSyncTrace);

  #undef NODE_ENV_METHOD

  void set_fs_stats_field_array(double* fields) {
    _node_env->set_fs_stats_field_array(fields);
  }

private:
  Napi::Env _env;
  node::Environment* _node_env;
};

}  // namespace node_api

#endif  // SRC_NAPI_ADAPTERS_H_
