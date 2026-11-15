#include "chatbot/code/code_session.h"

#include "agent_loop.h"

#include <utility>

namespace chatbot::code {

namespace {

CodeSystemPrompt initial_prompt(const CodeSessionEnvironment& env) {
    const CancelToken cancel; // git ya tiene su tiempo límite corto.
    return build_code_system_prompt(env.root, env.prompt_options, cancel);
}

} // namespace

CodeSession::CodeSession(CodeSessionEnvironment environment, Post post, CodeObserver observer)
    : env_(std::move(environment)), instructions_(initial_prompt(env_)),
      history_{Message{Role::System, instructions_.text}},
      loop_(std::make_unique<AgentLoop>(env_, history_, tools_, std::move(post),
                                        std::move(observer))) {}

CodeSession::~CodeSession() = default;

bool CodeSession::send(std::string text) {
    return env_.client != nullptr && loop_->send(std::move(text));
}

void CodeSession::cancel() {
    loop_->cancel();
}

bool CodeSession::busy() const {
    return loop_->state() != LoopState::Idle;
}

LoopState CodeSession::state() const {
    return loop_->state();
}

std::optional<ToolCallInfo> CodeSession::pending_approval() const {
    return loop_->pending_approval();
}

bool CodeSession::respond_approval(ApprovalDecision decision, const std::string& reason) {
    return loop_->respond_approval(decision, reason);
}

const std::vector<TodoItem>& CodeSession::todos() const {
    return loop_->todos();
}

} // namespace chatbot::code
