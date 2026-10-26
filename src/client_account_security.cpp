#include "cppgram/client.h"

#include <stdexcept>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

CloudPasswordStatus Client::get_cloud_password_status() {
    require_session();
    fn::account_getPassword req;
    const tl::account_Password result = session_->call(req);
    const auto* pw = std::get_if<ty::account_password>(&result.value);
    if (pw == nullptr) throw std::runtime_error("get_cloud_password_status: unexpected result");

    CloudPasswordStatus out;
    out.has_password = pw->has_password;
    out.has_recovery = pw->has_recovery;
    if (pw->hint) out.hint = *pw->hint;
    return out;
}

}  // namespace cppgram
