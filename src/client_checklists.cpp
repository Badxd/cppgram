#include "cppgram/client.h"
#include "secure_random.h"

#include <memory>
#include <random>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::TextWithEntities> checklist_text(const std::string& text) {
    ty::textWithEntities t;
    t.text = text;
    return std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{t});
}

std::vector<std::shared_ptr<tl::TodoItem>> checklist_items(const std::vector<TodoItemInput>& items) {
    std::vector<std::shared_ptr<tl::TodoItem>> out;
    for (const auto& item : items) {
        ty::todoItem t;
        t.id = item.id;
        t.title = checklist_text(item.text);
        out.push_back(std::make_shared<tl::TodoItem>(tl::TodoItem{t}));
    }
    return out;
}

std::int64_t new_random_id() {
    return detail::secure_random_i64();
}

}  // namespace

void Client::send_todo_list(const PeerRef& chat, const std::string& title, const std::vector<TodoItemInput>& items,
                            bool others_can_append, bool others_can_complete) {
    require_session();
    ty::todoList list;
    list.title = checklist_text(title);
    list.list = checklist_items(items);
    list.others_can_append = others_can_append;
    list.others_can_complete = others_can_complete;

    ty::inputMediaTodo media;
    media.todo = std::make_shared<tl::TodoList>(tl::TodoList{list});

    fn::messages_sendMedia req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.media = std::make_shared<tl::InputMedia>(tl::InputMedia{media});
    req.random_id = new_random_id();
    session_->call(req);
}

void Client::toggle_todo_items(const PeerRef& chat, std::int32_t msg_id, const std::vector<std::int32_t>& completed,
                               const std::vector<std::int32_t>& incompleted) {
    require_session();
    fn::messages_toggleTodoCompleted req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.msg_id = msg_id;
    req.completed = completed;
    req.incompleted = incompleted;
    session_->call(req);
}

void Client::append_todo_items(const PeerRef& chat, std::int32_t msg_id, const std::vector<TodoItemInput>& items) {
    require_session();
    fn::messages_appendTodoList req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.msg_id = msg_id;
    req.list = checklist_items(items);
    session_->call(req);
}

}  // namespace cppgram
