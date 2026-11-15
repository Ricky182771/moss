#include "tool_call_accumulator.h"

#include <algorithm>
#include <set>
#include <utility>

namespace chatbot {

void ToolCallAccumulator::add(const sse::ToolCallChunk& chunk) {
    const bool has_index = chunk.index.has_value();
    if (!has_index && chunk.id.empty() && chunk.name.empty() && chunk.arguments.empty()) {
        return; // Sin nada: no abre una llamada fantasma.
    }
    Entry* entry = nullptr;
    if (has_index) {
        // Hay servidores que mandan todas las llamadas con index 0: un id
        // distinto del que ya tiene la más reciente de ese index es otra.
        const auto it = std::find_if(calls_.rbegin(), calls_.rend(),
                                     [&](const Entry& e) { return e.index == chunk.index; });
        if (it != calls_.rend() &&
            (chunk.id.empty() || it->call.id.empty() || it->call.id == chunk.id)) {
            entry = &*it;
        }
    } else if (!chunk.id.empty()) {
        const auto it = std::find_if(calls_.begin(), calls_.end(),
                                     [&](const Entry& e) { return e.call.id == chunk.id; });
        if (it != calls_.end()) {
            entry = &*it;
        }
    } else if (!calls_.empty() && (chunk.name.empty() || calls_.back().call.name.empty())) {
        entry = &calls_.back(); // Un name sobre una llamada que ya tiene el suyo abre otra.
    }
    if (entry == nullptr) {
        entry = &calls_.emplace_back(Entry{chunk.index, ToolCall{}});
    }
    if (entry->call.id.empty()) {
        entry->call.id = chunk.id;
    }
    if (entry->call.name.empty()) {
        entry->call.name = chunk.name;
    }
    entry->call.arguments += chunk.arguments;
}

std::optional<ToolCallAccumulator::Started> ToolCallAccumulator::next_started() {
    if (announced_ >= calls_.size() || calls_[announced_].call.name.empty()) {
        return std::nullopt;
    }
    const std::size_t position = announced_++;
    return Started{position, calls_[position].call.name};
}

std::vector<ToolCall> ToolCallAccumulator::finish(const std::vector<Message>& history) const {
    std::vector<ToolCall> calls;
    calls.reserve(calls_.size());
    for (const Entry& entry : calls_) {
        calls.push_back(entry.call);
        if (calls.back().arguments.empty()) {
            calls.back().arguments = "{}";
        }
    }
    assign_free_ids(calls, history);
    return calls;
}

void assign_free_ids(std::vector<ToolCall>& calls, const std::vector<Message>& history) {
    std::set<std::string> taken; // Del historial y de las llamadas anteriores ya aceptadas.
    for (const Message& message : history) {
        for (const ToolCall& call : message.tool_calls) {
            taken.insert(call.id);
        }
    }
    // Un id generado tampoco le quita el suyo a una llamada posterior.
    std::set<std::string> reserved = taken;
    for (const ToolCall& call : calls) {
        reserved.insert(call.id);
    }
    std::size_t next = 1;
    for (ToolCall& call : calls) {
        if (!call.id.empty() && taken.insert(call.id).second) {
            continue;
        }
        do {
            call.id = "call_moss_" + std::to_string(next++);
        } while (!reserved.insert(call.id).second);
    }
}

} // namespace chatbot
