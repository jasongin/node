#ifndef SRC_TRACING_AGENT_H_
#define SRC_TRACING_AGENT_H_

#include "tracing/node_trace_buffer.h"
#include "tracing/node_trace_writer.h"
#include "uv.h"
#include "v8.h"

namespace node {
namespace tracing {

class Agent {
 public:
  explicit Agent(Environment* env);
  ~Agent();
  void Initialize(v8::Platform* platform);
  const std::vector<std::string>& GetCategories() { return categories_; }
  void SetCategories(const std::vector<std::string>& category_list);
  void SetCategories(const char* category_list);
  void Start();
  void Stop();
  bool IsStarted() { return started_; }

 private:
  static void ThreadCb(void* arg);

  uv_thread_t thread_;
  uv_loop_t tracing_loop_;
  v8::Platform* platform_ = nullptr;
  Environment* parent_env_;
  std::vector<std::string> categories_;
  TracingController* tracing_controller_;
  bool started_;
};

}  // namespace tracing
}  // namespace node

#endif  // SRC_TRACING_AGENT_H_
