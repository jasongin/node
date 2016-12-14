#include "tracing/agent.h"

#include <sstream>
#include <string>

#include "env-inl.h"
#include "libplatform/libplatform.h"

namespace node {
namespace tracing {

using v8::platform::tracing::TraceConfig;

Agent::Agent(v8::Platform* platform) : started_(false) {
  platform_ = platform;

  int err = uv_loop_init(&tracing_loop_);
  CHECK_EQ(err, 0);

  NodeTraceWriter* trace_writer = new NodeTraceWriter(&tracing_loop_);
  TraceBuffer* trace_buffer = new NodeTraceBuffer(
      NodeTraceBuffer::kBufferChunks, trace_writer, &tracing_loop_);
  tracing_controller_ = new TracingController();
  tracing_controller_->Initialize(trace_buffer);
  v8::platform::SetTracingController(platform, tracing_controller_);

  // This thread should be created *after* async handles are created
  // (within NodeTraceWriter and NodeTraceBuffer constructors).
  // Otherwise the thread could shut down prematurely.
  err = uv_thread_create(&thread_, ThreadCb, this);
  CHECK_EQ(err, 0);
}

void Agent::SetCategories(const std::vector<std::string>& category_list) {
  categories_.clear();

  for (const std::string& category : category_list) {
      categories_.push_back(category);
  }

  if (IsStarted()) {
      // Push the updated tracing config to the tracing controller.
      Start();
  }
}

void Agent::SetCategories(const char* category_list) {
  categories_.clear();

  if (category_list) {
    std::stringstream category_stream(category_list);
    while (category_stream.good()) {
      std::string category;
      getline(category_stream, category, ',');
      categories_.push_back(category.c_str());
    }
  }
  else {
    categories_.push_back("v8");
    categories_.push_back("node");
  }

  if (IsStarted()) {
      // Push the updated tracing config to the tracing controller.
      Start();
  }
}

void Agent::Start() {
  TraceConfig* trace_config = new TraceConfig();

  for (const std::string& category : categories_) {
    trace_config->AddIncludedCategory(category.c_str());
  }

  tracing_controller_->StartTracing(trace_config);
  started_ = true;
}

void Agent::Stop() {
  if (IsStarted()) {
    // Perform final Flush on TraceBuffer. We don't want the tracing controller
    // to flush the buffer again on destruction of the V8::Platform.
    tracing_controller_->StopTracing();
    started_ = false;
  }
}

// static
void Agent::ThreadCb(void* arg) {
  Agent* agent = static_cast<Agent*>(arg);
  uv_run(&agent->tracing_loop_, UV_RUN_DEFAULT);
}

Agent::~Agent() {
  Stop();

  if (thread_ != 0) {
    v8::platform::SetTracingController(platform_, nullptr);
    delete tracing_controller_;

    int err = uv_loop_close(&tracing_loop_);
    CHECK_EQ(err, 0);

    // Thread should finish when the tracing loop is stopped.
    err = uv_thread_join(&thread_);
    CHECK_EQ(err, 0);
    thread_ = 0;
  }
}

}  // namespace tracing
}  // namespace node
