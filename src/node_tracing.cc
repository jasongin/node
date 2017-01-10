#include "node.h"
#include "node_buffer.h"

#include "async-wrap.h"
#include "async-wrap-inl.h"
#include "env.h"
#include "env-inl.h"
#include "util.h"
#include "util-inl.h"
#include "tracing/agent.h"

#include "v8.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <unordered_set>
#include <functional>

namespace node {

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Local;
using v8::MaybeLocal;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

extern tracing::Agent* tracing_agent;


// The tracing APIs require category groups to be pointers to long-lived strings.
// Those strings are stored here.
static std::unordered_set<std::string> categoryGroups;

// Gets a pointer to the category-enabled flags for a tracing category group, if tracing is enabled for it.
static const uint8_t* GetCategoryGroupEnabled(const char* categoryGroup) {
    if (categoryGroup == nullptr) return nullptr;

    static TRACE_EVENT_API_ATOMIC_WORD categoryGroupAtomic;
    const uint8_t* categoryGroupEnabled;
    INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO_CUSTOM_VARIABLES(categoryGroup, categoryGroupAtomic, categoryGroupEnabled);

    if (!(*categoryGroupEnabled &
        (kEnabledForRecording_CategoryGroupEnabledFlags | kEnabledForEventCallback_CategoryGroupEnabledFlags))) {
        return nullptr;
    }
    return categoryGroupEnabled;
}

// Gets a comma-separated category group from a string or string array value.
// All returned lists are stored (uniquely) in the static categoryGroups set.
static const char* GetCategoryGroup(Environment* env, const Local<Value>& categoryValue) {
  if (categoryValue->IsString()) {
    Utf8Value category(env->isolate(), categoryValue);

    // The returned insertion is a pair whose first item is the object that was inserted or
    // that blocked the insertion and second item is a boolean indicating whether it was inserted.
    auto insertion = categoryGroups.insert(category.out());
    return insertion.first->c_str();
  }
  else if (categoryValue->IsArray()) {
    Local<Array> categoryArray = Local<Array>::Cast(categoryValue);
    uint32_t categoryCount = categoryArray->Length();

    std::ostringstream os;
    for (uint32_t i = 0; i < categoryCount; i++) {
      if (i > 0) {
        os << ',';
      }

      if (categoryArray->Get(i)->IsString()) {
        Utf8Value category(env->isolate(), categoryArray->Get(i));
        os << category.out();
      }
      else {
          env->ThrowTypeError("Trace event category array must contain strings.");
          return nullptr;
      }
    }

    auto insertion = categoryGroups.insert(os.str());
    return insertion.first->c_str();
  }
  else {
      env->ThrowTypeError("Trace event category must be a string or string array.");
      return nullptr;
  }
}

// Gets a list of categories from a string or string array value.
static bool GetCategoryList(
    Environment* env,
    const Local<Value>& categoryValue,
    std::vector<std::string>& categoryList) {
  if (categoryValue->IsString()) {
    Utf8Value category(env->isolate(), categoryValue);
    categoryList.reserve(1);
    categoryList.emplace_back(category.out());
  }
  else if (categoryValue->IsArray()) {
    Local<Array> categoryArray = Local<Array>::Cast(categoryValue);
    uint32_t categoryCount = categoryArray->Length();
    categoryList.reserve(categoryCount);

    for (uint32_t i = 0; i < categoryCount; i++) {
      if (categoryArray->Get(i)->IsString()) {
        Utf8Value category(env->isolate(), categoryArray->Get(i));
        categoryList.emplace_back(category.out());
      }
      else {
        env->ThrowTypeError("Trace event category array must contain strings.");
        return false;
      }
    }
  }
  else {
    env->ThrowTypeError("Trace event category must be a string or string array.");
    return false;
  }

  return true;
}

static inline char GetPhase(Environment* env, const Local<Value>& arg) {
  if (!arg->IsNumber()) {
    env->ThrowTypeError("Trace event type must be a number.");
    return 0;
  }
  Local<Context> context = env->isolate()->GetCurrentContext();
  return static_cast<char>(arg->Int32Value(context).ToChecked());
}

static inline const char* GetName(Environment* env, const Local<Value>& arg) {
  if (!arg->IsString()) {
    env->ThrowTypeError("Trace event name must be a string.");
    return nullptr;
  }
  Utf8Value nameValue(env->isolate(), arg);
  return nameValue.out();
}

static inline bool GetId(Environment* env, const Local<Value>& arg, int64_t& id) {
  if (arg->IsUndefined() || arg->IsNull()) {
    id = 0;
    return true;
  }
  else if (arg->IsNumber()) {
    Local<Context> context = env->isolate()->GetCurrentContext();
    id = arg->IntegerValue(context).ToChecked();
    return true;
  }
  else {
    env->ThrowTypeError("Trace event id must be a number or undefined.");
    return false;
  }
}

static inline bool GetArgValue(
  Environment* env,
  const Local<Context>& context,
  const Local<Value>& value,
  uint8_t& arg_type,
  uint64_t& arg_value) {
  // TODO: Get the appropriate type and value. Copy string values.
  arg_type = TRACE_VALUE_TYPE_INT;
  arg_value = 0;
  return true;
}

static void Emit(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  // Args: [type, category, name, id, args]
  CHECK_GE(args.Length(), 3);

  // Check the category group first, to avoid doing more work if it's not enabled.
  const char* categoryGroup = GetCategoryGroup(env, args[1]);
  const uint8_t* categoryGroupEnabled = GetCategoryGroupEnabled(categoryGroup);
  if (categoryGroupEnabled == nullptr) return;

  char phase = GetPhase(env, args[0]);
  if (phase == 0) return;

  const char* name = GetName(env, args[2]);
  if (name == nullptr) return;

  int64_t id = 0;
  if (args.Length() >= 4 && !GetId(env, args[3], id)) return;

  int32_t num_args;
  const char* arg_names[2];
  uint8_t arg_types[2];
  uint64_t arg_values[2];
  const char* scope = nullptr;

  if (args.Length() < 5 || args[4]->IsUndefined()) {
    num_args = 0;
  }
  else if (args[4]->IsObject()) {
    Local<Context> context = env->isolate()->GetCurrentContext();
    Local<Object> obj = Local<Object>::Cast(args[4]);
    Local<Array> argNames = obj->GetPropertyNames(context).ToLocalChecked();

    if (argNames->Length() == 0) {
      num_args = 0;
    }
    else {
      Local<String> arg1Name = Local<String>::Cast(argNames->Get(context, 0).ToLocalChecked());
      Utf8Value arg1NameValue(env->isolate(), arg1Name);
      arg_names[0] = arg1NameValue.out();

      Local<Value> arg1Value = obj->Get(context, arg1Name).ToLocalChecked();
      if (!GetArgValue(env, context, arg1Value, arg_types[0], arg_values[0])) return;

      if (argNames->Length() < 2) {
        num_args = 1;
      }
      else {
        num_args = 2;
        Local<String> arg2Name = Local<String>::Cast(argNames->Get(context, 1).ToLocalChecked());
        Utf8Value arg2NameValue(env->isolate(), arg2Name);
        arg_names[1] = arg2NameValue.out();

        Local<Value> arg2Value = obj->Get(context, arg2Name).ToLocalChecked();
        if (!GetArgValue(env, context, arg2Value, arg_types[1], arg_values[1])) return;
      }
    }
  }
  else if (args[4]->IsNumber()) {
    num_args = 1;
    Local<Context> context = env->isolate()->GetCurrentContext();
    arg_names[0] = "value";
    arg_types[0] = TRACE_VALUE_TYPE_INT;
    arg_values[0] = args[4]->Int32Value(context).ToChecked();
  }
  else {
    env->ThrowTypeError("Trace event args must be an object, number, or undefined.");
    return;
  }

  uint32_t flags = TRACE_EVENT_FLAG_COPY;
  if (id != 0) {
    flags |= TRACE_EVENT_FLAG_HAS_ID;
  }

  TRACE_EVENT_API_ADD_TRACE_EVENT(
    phase,
    categoryGroupEnabled,
    name,
    scope,
    id,
    0,
    num_args,
    arg_names,
    arg_types,
    arg_values,
    flags);
}

static void AddListenerCategory(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value category(env->isolate(), args[0]);

  // TODO: Add listener category.
}

static void RemoveListenerCategory(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value category(env->isolate(), args[0]);

  // TODO: Remove listener category.
}

static Local<Object> GetCategoryMap(Environment* env) {
  const std::vector<std::string>& categories = tracing_agent->GetCategories();

  Local<Object> categoryMap = Object::New(env->isolate());
  for (uint32_t i = 0; i < categories.size(); i++) {
    Local<Value> category = String::NewFromUtf8(env->isolate(), categories[i].c_str());

    // TODO: CategoryGroupEnabledFlags
    Local<Value> flags = Int32::New(env->isolate(), 1);
    categoryMap->Set(category, flags);
  }

  return categoryMap;
}

static void GetEnabledCategories(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    args.GetReturnValue().Set(GetCategoryMap(env));
}

static void EnableCategory(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    Local<Context> context = env->isolate()->GetCurrentContext();

    std::vector<std::string> changeCategories;
    if (!GetCategoryList(env, args[0], changeCategories)) {
        return;
    }

    if (!args[1]->IsNumber()) {
        env->ThrowTypeError("Trace event category enabled flag must be a number.");
        return;
    }

    // TODO: Separately enable recording and callback flags when the agent config supports that.
    int32_t enableFlags = Local<Int32>::Cast(args[1])->Int32Value(context).ToChecked();
    bool enable = enableFlags != 0;

    bool changed = false;
    std::vector<std::string> categories(tracing_agent->GetCategories());

    for (const std::string& setCategory : changeCategories) {
      auto foundCategory = std::find(categories.begin(), categories.end(), setCategory);
      if (enable && foundCategory == categories.end()) {
        categories.push_back(setCategory);
        changed = true;
      }
      else if (!enable && foundCategory != categories.end()) {
        categories.erase(foundCategory);
        changed = true;
      }
    }

    if (changed) {
      tracing_agent->SetCategories(categories);

      // Notify script that the enabled categories changed.
      Local<Value> argv[1];
      argv[0] = GetCategoryMap(env);
      MakeCallback(env, args.Holder(), env->onchange_string(), 1, argv);

      if (categories.size() > 0) {
        if (!tracing_agent->IsStarted()) {
          tracing_agent->Start();
        }
      }
      else {
        tracing_agent->Stop();
      }
    }
}

void InitTracing(Local<Object> target,
                 Local<Value> unused,
                 Local<Context> context,
                 void* priv) {
  Environment* env = Environment::GetCurrent(context);

  env->SetMethod(target, "emit", Emit);
  env->SetMethod(target, "addListenerCategory", AddListenerCategory);
  env->SetMethod(target, "removeListenerCategory", RemoveListenerCategory);
  env->SetMethod(target, "getEnabledCategories", GetEnabledCategories);
  env->SetMethod(target, "enableCategory", EnableCategory);
}

}  // namespace node

NODE_MODULE_CONTEXT_AWARE_BUILTIN(tracing, node::InitTracing)
