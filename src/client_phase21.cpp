#include "cppgram/client.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

bool Client::reset_bot_commands(const std::string& lang_code) {
    require_session();
    fn::bots_resetBotCommands req;
    req.scope = std::make_shared<tl::BotCommandScope>(tl::BotCommandScope{ty::botCommandScopeDefault{}});
    req.lang_code = lang_code;
    return session_->call(req);
}

bool Client::set_bot_default_admin_rights(const AdminRights& rights, bool broadcast) {
    require_session();
    ty::chatAdminRights r;
    r.change_info = rights.change_info;
    r.delete_messages = rights.delete_messages;
    r.ban_users = rights.ban_users;
    r.invite_users = rights.invite_users;
    r.pin_messages = rights.pin_messages;
    r.manage_call = rights.manage_call;
    r.manage_topics = rights.manage_topics;
    r.add_admins = rights.add_admins;
    r.anonymous = rights.anonymous;
    auto admin_rights = std::make_shared<tl::ChatAdminRights>(tl::ChatAdminRights{r});
    if (broadcast) {
        fn::bots_setBotBroadcastDefaultAdminRights req;
        req.admin_rights = admin_rights;
        return session_->call(req);
    }
    fn::bots_setBotGroupDefaultAdminRights req;
    req.admin_rights = admin_rights;
    return session_->call(req);
}

}  // namespace cppgram
