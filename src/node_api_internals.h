/******************************************************************************
 * Experimental prototype for demonstrating VM agnostic and ABI stable API
 * for native modules to use instead of using Nan and V8 APIs directly.
 *
 * The current status is "Experimental" and should not be used for
 * production applications.  The API is still subject to change
 * and as an experimental feature is NOT subject to semver.
 *
 ******************************************************************************/
#ifndef SRC_NODE_API_INTERNALS_H_
#define SRC_NODE_API_INTERNALS_H_

#include "node_api_types.h"

struct napi_env__ {
  explicit napi_env__(v8::Isolate* _isolate): isolate(_isolate),
      has_instance_available(true), last_error() {}
  ~napi_env__() {
    last_exception.Reset();
    has_instance.Reset();
  }
  v8::Isolate* isolate;
  v8::Persistent<v8::Value> last_exception;
  v8::Persistent<v8::Value> has_instance;
  bool has_instance_available;
  napi_extended_error_info last_error;
};

namespace node_api {

// This asserts v8::Local<> will always be implemented with a single
// pointer field so that we can pass it around as a void*.
static_assert(sizeof(v8::Local<v8::Value>) == sizeof(napi_value),
  "Cannot convert between v8::Local<v8::Value> and napi_value");

inline napi_value JsValueFromV8LocalValue(v8::Local<v8::Value> local) {
  return reinterpret_cast<napi_value>(*local);
}

inline v8::Local<v8::Value> V8LocalValueFromJsValue(napi_value v) {
  v8::Local<v8::Value> local;
  memcpy(&local, &v, sizeof(v));
  return local;
}

inline v8::Isolate* V8IsolateFromNapiEnv(napi_env env) {
  return env->isolate;
}

}  // namespace node_api

#endif  // SRC_NODE_API_INTERNALS_H_
