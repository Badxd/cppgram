#include "cppgram/client.h"

#include <stdexcept>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

PaymentReceiptInfo Client::get_payment_receipt(const PeerRef& chat, std::int32_t msg_id) {
    require_session();
    fn::payments_getPaymentReceipt req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.msg_id = msg_id;
    const tl::payments_PaymentReceipt result = session_->call(req);
    const auto* receipt = std::get_if<ty::payments_paymentReceipt>(&result.value);
    if (receipt == nullptr) throw std::runtime_error("get_payment_receipt: unexpected result");

    PaymentReceiptInfo out;
    out.date = receipt->date;
    out.bot_id = receipt->bot_id;
    out.title = receipt->title;
    out.description = receipt->description;
    return out;
}

}  // namespace cppgram
