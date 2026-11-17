// payments.* calls: payment forms, Stars forms, gift codes, Stars subscriptions and star gifts.
#include "cppgram/client.h"
#include "cppgram/convert.h"
#include "secure_random.h"

#include <stdexcept>
#include <variant>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputPeer> peer_ptr(const Client& client, const PeerRef& peer) {
    return std::make_shared<tl::InputPeer>(client.input_peer(peer));
}

std::shared_ptr<tl::InputUser> user_ptr(const PeerRef& user) {
    if (user.kind != PeerKind::User || !user.has_access_hash) {
        throw std::invalid_argument("expected a user peer with an access hash");
    }
    return std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{user.id, user.access_hash}});
}

std::shared_ptr<tl::InputInvoice> invoice_ptr(const Client& client, const InvoiceRef& invoice) {
    if (invoice.kind == InvoiceRef::Kind::Message) {
        ty::inputInvoiceMessage m;
        m.peer = peer_ptr(client, invoice.peer);
        m.msg_id = invoice.msg_id;
        return std::make_shared<tl::InputInvoice>(tl::InputInvoice{m});
    }
    if (invoice.slug.empty()) throw std::invalid_argument("invoice slug must not be empty");
    return std::make_shared<tl::InputInvoice>(tl::InputInvoice{ty::inputInvoiceSlug{invoice.slug}});
}

std::shared_ptr<tl::InputSavedStarGift> saved_star_gift_ptr(const Client& client, const SavedStarGiftRef& gift) {
    switch (gift.kind) {
        case SavedStarGiftRef::Kind::User:
            return std::make_shared<tl::InputSavedStarGift>(
                tl::InputSavedStarGift{ty::inputSavedStarGiftUser{gift.msg_id}});
        case SavedStarGiftRef::Kind::Chat:
            return std::make_shared<tl::InputSavedStarGift>(tl::InputSavedStarGift{
                ty::inputSavedStarGiftChat{peer_ptr(client, gift.peer), gift.saved_id}});
        case SavedStarGiftRef::Kind::Slug:
            if (gift.slug.empty()) throw std::invalid_argument("saved star gift slug must not be empty");
            return std::make_shared<tl::InputSavedStarGift>(
                tl::InputSavedStarGift{ty::inputSavedStarGiftSlug{gift.slug}});
    }
    throw std::invalid_argument("unknown saved star gift kind");
}

std::shared_ptr<tl::StarsAmount> stars_amount_ptr(const StarsAmountInfo& amount) {
    ty::starsAmount a;
    a.amount = amount.amount;
    a.nanos = amount.nanos;
    return std::make_shared<tl::StarsAmount>(tl::StarsAmount{a});
}

PaymentResultInfo payment_result_of(const tl::payments_PaymentResult& result) {
    PaymentResultInfo out;
    if (const auto* v = std::get_if<ty::payments_paymentVerificationNeeded>(&result.value)) {
        out.verification_needed = true;
        out.verification_url = v->url;
    }
    return out;
}

// Attribute of an upgraded gift. Unknown variants give nullopt.
std::optional<StarGiftAttributeInfo> attribute_of(const tl::StarGiftAttribute& wrapper) {
    StarGiftAttributeInfo out;
    auto rarity_of = [&out](const std::shared_ptr<tl::StarGiftAttributeRarity>& r) {
        if (!r) return;
        if (const auto* p = std::get_if<ty::starGiftAttributeRarity>(&r->value)) {
            out.rarity_tier = "permille";
            out.rarity_permille = p->permille;
        } else if (std::holds_alternative<ty::starGiftAttributeRarityUncommon>(r->value)) {
            out.rarity_tier = "uncommon";
        } else if (std::holds_alternative<ty::starGiftAttributeRarityRare>(r->value)) {
            out.rarity_tier = "rare";
        } else if (std::holds_alternative<ty::starGiftAttributeRarityEpic>(r->value)) {
            out.rarity_tier = "epic";
        } else if (std::holds_alternative<ty::starGiftAttributeRarityLegendary>(r->value)) {
            out.rarity_tier = "legendary";
        }
    };
    if (const auto* m = std::get_if<ty::starGiftAttributeModel>(&wrapper.value)) {
        out.kind = StarGiftAttributeInfo::Kind::Model;
        out.name = m->name;
        out.crafted = m->crafted;
        if (m->document) {
            if (auto media = detail::media_info_from_document(*m->document)) out.document = *media;
        }
        rarity_of(m->rarity);
        return out;
    }
    if (const auto* pt = std::get_if<ty::starGiftAttributePattern>(&wrapper.value)) {
        out.kind = StarGiftAttributeInfo::Kind::Pattern;
        out.name = pt->name;
        if (pt->document) {
            if (auto media = detail::media_info_from_document(*pt->document)) out.document = *media;
        }
        rarity_of(pt->rarity);
        return out;
    }
    if (const auto* bd = std::get_if<ty::starGiftAttributeBackdrop>(&wrapper.value)) {
        out.kind = StarGiftAttributeInfo::Kind::Backdrop;
        out.name = bd->name;
        out.backdrop_id = bd->backdrop_id;
        out.center_color = bd->center_color;
        out.edge_color = bd->edge_color;
        out.pattern_color = bd->pattern_color;
        out.text_color = bd->text_color;
        rarity_of(bd->rarity);
        return out;
    }
    if (const auto* od = std::get_if<ty::starGiftAttributeOriginalDetails>(&wrapper.value)) {
        out.kind = StarGiftAttributeInfo::Kind::OriginalDetails;
        out.date = od->date;
        if (od->message) {
            if (const auto* text = std::get_if<ty::textWithEntities>(&(*od->message)->value)) out.message = text->text;
        }
        return out;
    }
    return std::nullopt;
}

std::shared_ptr<tl::StarGiftAttributeId> attribute_id_ptr(const StarGiftAttributeIdInfo& a) {
    switch (a.kind) {
        case StarGiftAttributeIdInfo::Kind::Model:
            return std::make_shared<tl::StarGiftAttributeId>(
                tl::StarGiftAttributeId{ty::starGiftAttributeIdModel{a.document_id}});
        case StarGiftAttributeIdInfo::Kind::Pattern:
            return std::make_shared<tl::StarGiftAttributeId>(
                tl::StarGiftAttributeId{ty::starGiftAttributeIdPattern{a.document_id}});
        case StarGiftAttributeIdInfo::Kind::Backdrop:
            return std::make_shared<tl::StarGiftAttributeId>(
                tl::StarGiftAttributeId{ty::starGiftAttributeIdBackdrop{a.backdrop_id}});
    }
    throw std::invalid_argument("unknown star gift attribute kind");
}

PostAddressInfo address_of(const ty::postAddress& a) {
    return PostAddressInfo{a.street_line1, a.street_line2, a.city, a.state, a.country_iso2, a.post_code};
}

PaymentRequestedInfo requested_info_of(const tl::PaymentRequestedInfo& wrapper) {
    const auto* r = std::get_if<ty::paymentRequestedInfo>(&wrapper.value);
    if (r == nullptr) throw std::runtime_error("unexpected paymentRequestedInfo variant");
    PaymentRequestedInfo out;
    if (r->name) out.name = *r->name;
    if (r->phone) out.phone = *r->phone;
    if (r->email) out.email = *r->email;
    if (r->shipping_address && *r->shipping_address) {
        if (const auto* a = std::get_if<ty::postAddress>(&(*r->shipping_address)->value)) {
            out.shipping_address = address_of(*a);
        }
    }
    return out;
}

// Fills the invoice fields shared by every payment form variant.
void fill_invoice(PaymentFormInfo& out, const tl::Invoice& wrapper) {
    const auto* inv = std::get_if<ty::invoice>(&wrapper.value);
    if (inv == nullptr) throw std::runtime_error("unexpected invoice variant");
    out.currency = inv->currency;
    for (const auto& price : inv->prices) {
        if (!price) continue;
        if (const auto* lp = std::get_if<ty::labeledPrice>(&price->value)) {
            out.prices.push_back(LabeledPriceInfo{lp->label, lp->amount});
        }
    }
    out.test = inv->test;
    out.name_requested = inv->name_requested;
    out.phone_requested = inv->phone_requested;
    out.email_requested = inv->email_requested;
    out.shipping_address_requested = inv->shipping_address_requested;
    out.flexible = inv->flexible;
    out.phone_to_provider = inv->phone_to_provider;
    out.email_to_provider = inv->email_to_provider;
    out.recurring = inv->recurring;
    if (inv->max_tip_amount) out.max_tip_amount = *inv->max_tip_amount;
    if (inv->suggested_tip_amounts) out.suggested_tip_amounts = *inv->suggested_tip_amounts;
    if (inv->terms_url) out.terms_url = *inv->terms_url;
    if (inv->subscription_period) out.subscription_period = *inv->subscription_period;
}

std::string web_document_url(const std::shared_ptr<tl::WebDocument>& d) {
    if (!d) return {};
    if (const auto* w = std::get_if<ty::webDocument>(&d->value)) return w->url;
    if (const auto* np = std::get_if<ty::webDocumentNoProxy>(&d->value)) return np->url;
    return {};
}


}  // namespace

PaymentFormInfo Client::get_payment_form(const InvoiceRef& invoice, const std::string& theme_params_json) {
    require_session();
    fn::payments_getPaymentForm req;
    req.invoice = invoice_ptr(*this, invoice);
    if (!theme_params_json.empty()) {
        req.theme_params = std::make_shared<tl::DataJSON>(tl::DataJSON{ty::dataJSON{theme_params_json}});
    }
    const tl::payments_PaymentForm result = call_rpc(req);
    PaymentFormInfo out;
    if (const auto* form = std::get_if<ty::payments_paymentForm>(&result.value)) {
        cache_users_chats(form->users, {});
        out.kind = PaymentFormKind::Invoice;
        out.form_id = form->form_id;
        out.bot_id = form->bot_id;
        out.provider_id = form->provider_id;
        out.title = form->title;
        out.description = form->description;
        if (form->photo) out.photo_url = web_document_url(*form->photo);
        out.url = form->url;
        out.can_save_credentials = form->can_save_credentials;
        out.password_missing = form->password_missing;
        if (form->invoice) fill_invoice(out, *form->invoice);
        if (form->native_provider) out.native_provider = *form->native_provider;
        if (form->native_params && *form->native_params) {
            if (const auto* d = std::get_if<ty::dataJSON>(&(*form->native_params)->value)) out.native_params = d->data;
        }
        if (form->additional_methods) {
            for (const auto& m : *form->additional_methods) {
                if (!m) continue;
                if (const auto* method = std::get_if<ty::paymentFormMethod>(&m->value)) {
                    out.additional_methods.push_back(PaymentFormMethodInfo{method->url, method->title});
                }
            }
        }
        if (form->saved_info && *form->saved_info) out.saved_info = requested_info_of(**form->saved_info);
        if (form->saved_credentials) {
            for (const auto& c : *form->saved_credentials) {
                if (!c) continue;
                if (const auto* card = std::get_if<ty::paymentSavedCredentialsCard>(&c->value)) {
                    out.saved_credentials.push_back(PaymentSavedCredentialInfo{card->id, card->title});
                }
            }
        }
        return out;
    }
    if (const auto* stars = std::get_if<ty::payments_paymentFormStars>(&result.value)) {
        cache_users_chats(stars->users, {});
        out.kind = PaymentFormKind::Stars;
        out.form_id = stars->form_id;
        out.bot_id = stars->bot_id;
        out.title = stars->title;
        out.description = stars->description;
        if (stars->photo) out.photo_url = web_document_url(*stars->photo);
        if (stars->invoice) fill_invoice(out, *stars->invoice);
        return out;
    }
    if (const auto* gift = std::get_if<ty::payments_paymentFormStarGift>(&result.value)) {
        out.kind = PaymentFormKind::StarGift;
        out.form_id = gift->form_id;
        if (gift->invoice) fill_invoice(out, *gift->invoice);
        return out;
    }
    throw std::runtime_error("unexpected payments.PaymentForm variant");
}

PaymentResultInfo Client::send_payment_form(std::int64_t form_id, const InvoiceRef& invoice,
                                            const PaymentCredentials& credentials, std::int64_t tip_amount,
                                            const PaymentSendOptions& options) {
    require_session();
    fn::payments_sendPaymentForm req;
    req.form_id = form_id;
    req.invoice = invoice_ptr(*this, invoice);
    switch (credentials.kind) {
        case PaymentCredentials::Kind::Saved: {
            if (credentials.saved_id.empty()) throw std::invalid_argument("saved credentials need an id");
            ty::inputPaymentCredentialsSaved c;
            c.id = credentials.saved_id;
            c.tmp_password = credentials.tmp_password;
            req.credentials = std::make_shared<tl::InputPaymentCredentials>(tl::InputPaymentCredentials{c});
            break;
        }
        case PaymentCredentials::Kind::New: {
            if (credentials.data_json.empty()) throw std::invalid_argument("new payment credentials need data_json");
            ty::inputPaymentCredentials c;
            c.save = credentials.save;
            c.data = std::make_shared<tl::DataJSON>(tl::DataJSON{ty::dataJSON{credentials.data_json}});
            req.credentials = std::make_shared<tl::InputPaymentCredentials>(tl::InputPaymentCredentials{c});
            break;
        }
        case PaymentCredentials::Kind::ApplePay: {
            if (credentials.data_json.empty()) throw std::invalid_argument("Apple Pay credentials need payment data");
            ty::inputPaymentCredentialsApplePay c;
            c.payment_data = std::make_shared<tl::DataJSON>(tl::DataJSON{ty::dataJSON{credentials.data_json}});
            req.credentials = std::make_shared<tl::InputPaymentCredentials>(tl::InputPaymentCredentials{c});
            break;
        }
        case PaymentCredentials::Kind::GooglePay: {
            if (credentials.data_json.empty()) throw std::invalid_argument("Google Pay credentials need a token");
            ty::inputPaymentCredentialsGooglePay c;
            c.payment_token = std::make_shared<tl::DataJSON>(tl::DataJSON{ty::dataJSON{credentials.data_json}});
            req.credentials = std::make_shared<tl::InputPaymentCredentials>(tl::InputPaymentCredentials{c});
            break;
        }
    }
    if (tip_amount != 0) req.tip_amount = tip_amount;
    if (options.requested_info_id) req.requested_info_id = *options.requested_info_id;
    if (options.shipping_option_id) req.shipping_option_id = *options.shipping_option_id;
    return payment_result_of(call_rpc(req));
}

PaymentResultInfo Client::send_stars_form(std::int64_t form_id, const InvoiceRef& invoice) {
    require_session();
    fn::payments_sendStarsForm req;
    req.form_id = form_id;
    req.invoice = invoice_ptr(*this, invoice);
    return payment_result_of(call_rpc(req));
}

std::string Client::export_invoice(std::shared_ptr<mtproto::tl::InputMedia> invoice_media) {
    require_session();
    if (!invoice_media) throw std::invalid_argument("export_invoice needs an invoice media");
    fn::payments_exportInvoice req;
    req.invoice_media = invoice_media;
    const tl::payments_ExportedInvoice result = call_rpc(req);
    const auto* e = std::get_if<ty::payments_exportedInvoice>(&result.value);
    if (e == nullptr) throw std::runtime_error("unexpected payments.ExportedInvoice variant");
    return e->url;
}

GiftCodeInfo Client::check_gift_code(const std::string& slug) {
    require_session();
    if (slug.empty()) throw std::invalid_argument("check_gift_code needs a slug");
    fn::payments_checkGiftCode req;
    req.slug = slug;
    const tl::payments_CheckedGiftCode result = call_rpc(req);
    const auto* g = std::get_if<ty::payments_checkedGiftCode>(&result.value);
    if (g == nullptr) throw std::runtime_error("unexpected payments.CheckedGiftCode variant");
    cache_users_chats(g->users, g->chats);
    GiftCodeInfo out;
    out.via_giveaway = g->via_giveaway;
    if (g->from_id && *g->from_id) {
        out.has_from = true;
        out.from = detail::peer_from_tl(**g->from_id, cache_);
    }
    if (g->to_id) {
        out.has_to = true;
        out.to_user_id = *g->to_id;
    }
    out.months_or_days = g->days;
    out.date = g->date;
    if (g->used_date) out.used_date = *g->used_date;
    return out;
}

bool Client::bot_cancel_stars_subscription(const PeerRef& user, const std::string& charge_id, bool restore) {
    require_session();
    fn::payments_botCancelStarsSubscription req;
    req.user_id = user_ptr(user);
    req.charge_id = charge_id;
    if (restore) req.restore = true;
    return call_rpc(req);
}

bool Client::change_stars_subscription(const PeerRef& peer, const std::string& subscription_id,
                                       std::optional<bool> canceled) {
    require_session();
    fn::payments_changeStarsSubscription req;
    req.peer = peer_ptr(*this, peer);
    req.subscription_id = subscription_id;
    if (canceled) req.canceled = *canceled;
    return call_rpc(req);
}

bool Client::fulfill_stars_subscription(const PeerRef& peer, const std::string& subscription_id) {
    require_session();
    fn::payments_fulfillStarsSubscription req;
    req.peer = peer_ptr(*this, peer);
    req.subscription_id = subscription_id;
    return call_rpc(req);
}

bool Client::craft_star_gift(const std::vector<SavedStarGiftRef>& gifts) {
    require_session();
    if (gifts.empty()) throw std::invalid_argument("craft_star_gift needs at least one gift");
    fn::payments_craftStarGift req;
    for (const auto& g : gifts) req.stargift.push_back(saved_star_gift_ptr(*this, g));
    (void)call_rpc(req);
    return true;
}

bool Client::delete_star_gift(const SavedStarGiftRef& gift) {
    require_session();
    fn::payments_deleteStarGift req;
    req.stargift = saved_star_gift_ptr(*this, gift);
    return call_rpc(req);
}

bool Client::delete_star_gift_collection(const PeerRef& peer, std::int32_t collection_id) {
    require_session();
    fn::payments_deleteStarGiftCollection req;
    req.peer = peer_ptr(*this, peer);
    req.collection_id = collection_id;
    return call_rpc(req);
}

bool Client::reorder_star_gift_collections(const PeerRef& peer, const std::vector<std::int32_t>& order) {
    require_session();
    fn::payments_reorderStarGiftCollections req;
    req.peer = peer_ptr(*this, peer);
    req.order = order;
    return call_rpc(req);
}

bool Client::toggle_star_gifts_pinned_to_top(const PeerRef& peer, const std::vector<SavedStarGiftRef>& gifts) {
    require_session();
    fn::payments_toggleStarGiftsPinnedToTop req;
    req.peer = peer_ptr(*this, peer);
    for (const auto& g : gifts) req.stargift.push_back(saved_star_gift_ptr(*this, g));
    return call_rpc(req);
}

bool Client::update_star_gift_price(const SavedStarGiftRef& gift, const StarsAmountInfo& resell_amount) {
    require_session();
    fn::payments_updateStarGiftPrice req;
    req.stargift = saved_star_gift_ptr(*this, gift);
    req.resell_amount = stars_amount_ptr(resell_amount);
    (void)call_rpc(req);
    return true;
}

bool Client::send_star_gift_offer(const PeerRef& peer, const std::string& slug, const StarsAmountInfo& price,
                                  std::int32_t duration_seconds, std::int64_t allow_paid_stars) {
    require_session();
    if (slug.empty()) throw std::invalid_argument("send_star_gift_offer needs a slug");
    fn::payments_sendStarGiftOffer req;
    req.peer = peer_ptr(*this, peer);
    req.slug = slug;
    req.price = stars_amount_ptr(price);
    req.duration = duration_seconds;
    req.random_id = detail::secure_random_i64();
    if (allow_paid_stars != 0) req.allow_paid_stars = allow_paid_stars;
    (void)call_rpc(req);
    return true;
}

bool Client::resolve_star_gift_offer(std::int32_t offer_msg_id, bool decline) {
    require_session();
    fn::payments_resolveStarGiftOffer req;
    req.offer_msg_id = offer_msg_id;
    if (decline) req.decline = true;
    (void)call_rpc(req);
    return true;
}

bool Client::refund_stars_charge(const PeerRef& user, const std::string& charge_id) {
    require_session();
    fn::payments_refundStarsCharge req;
    req.user_id = user_ptr(user);
    req.charge_id = charge_id;
    (void)call_rpc(req);
    return true;
}

SavedStarGiftPage Client::get_craft_star_gifts(std::int64_t gift_id, const std::string& offset, std::int32_t limit) {
    require_session();
    fn::payments_getCraftStarGifts req;
    req.gift_id = gift_id;
    req.offset = offset;
    req.limit = limit;
    const tl::payments_SavedStarGifts result = call_rpc(req);
    const auto* page = std::get_if<ty::payments_savedStarGifts>(&result.value);
    if (page == nullptr) throw std::runtime_error("unexpected payments.SavedStarGifts variant");
    cache_users_chats(page->users, page->chats);
    SavedStarGiftPage out;
    out.count = page->count;
    if (page->next_offset) out.next_offset = *page->next_offset;
    for (const auto& g : page->gifts) {
        if (!g) continue;
        if (auto info = detail::saved_star_gift_from_tl(*g)) out.gifts.push_back(std::move(*info));
    }
    return out;
}

ResaleStarGiftsPage Client::get_resale_star_gifts(std::int64_t gift_id, const std::string& offset, std::int32_t limit,
                                                  bool sort_by_price, bool sort_by_num, bool for_craft,
                                                  bool stars_only,
                                                  const std::vector<StarGiftAttributeIdInfo>& attributes,
                                                  std::int64_t attributes_hash) {
    require_session();
    fn::payments_getResaleStarGifts req;
    req.gift_id = gift_id;
    req.offset = offset;
    req.limit = limit;
    if (sort_by_price) req.sort_by_price = true;
    if (sort_by_num) req.sort_by_num = true;
    if (for_craft) req.for_craft = true;
    if (stars_only) req.stars_only = true;
    if (!attributes.empty()) {
        std::vector<std::shared_ptr<tl::StarGiftAttributeId>> ids;
        for (const auto& a : attributes) ids.push_back(attribute_id_ptr(a));
        req.attributes = std::move(ids);
    }
    if (attributes_hash != 0) req.attributes_hash = attributes_hash;
    const tl::payments_ResaleStarGifts result = call_rpc(req);
    const auto* g = std::get_if<ty::payments_resaleStarGifts>(&result.value);
    if (g == nullptr) throw std::runtime_error("unexpected payments.ResaleStarGifts variant");
    cache_users_chats(g->users, g->chats);
    ResaleStarGiftsPage out;
    out.count = g->count;
    for (const auto& gift : g->gifts) {
        if (!gift) continue;
        if (auto info = detail::star_gift_info_from_tl(*gift)) out.gifts.push_back(*info);
    }
    if (g->next_offset) out.next_offset = *g->next_offset;
    if (g->attributes) {
        for (const auto& a : *g->attributes) {
            if (!a) continue;
            if (auto info = attribute_of(*a)) out.attributes.push_back(std::move(*info));
        }
    }
    if (g->attributes_hash) out.attributes_hash = *g->attributes_hash;
    if (g->counters) {
        for (const auto& c : *g->counters) {
            if (!c) continue;
            const auto* counter = std::get_if<ty::starGiftAttributeCounter>(&c->value);
            if (counter == nullptr || !counter->attribute) continue;
            StarGiftAttributeCounterInfo info;
            info.count = counter->count;
            if (const auto* m = std::get_if<ty::starGiftAttributeIdModel>(&counter->attribute->value)) {
                info.attribute.kind = StarGiftAttributeIdInfo::Kind::Model;
                info.attribute.document_id = m->document_id;
            } else if (const auto* pt = std::get_if<ty::starGiftAttributeIdPattern>(&counter->attribute->value)) {
                info.attribute.kind = StarGiftAttributeIdInfo::Kind::Pattern;
                info.attribute.document_id = pt->document_id;
            } else if (const auto* bd = std::get_if<ty::starGiftAttributeIdBackdrop>(&counter->attribute->value)) {
                info.attribute.kind = StarGiftAttributeIdInfo::Kind::Backdrop;
                info.attribute.backdrop_id = bd->backdrop_id;
            } else {
                continue;
            }
            out.counters.push_back(std::move(info));
        }
    }
    return out;
}

StarGiftAuctionStateInfo Client::get_star_gift_auction_state(const StarGiftAuctionRef& auction, std::int32_t version) {
    require_session();
    fn::payments_getStarGiftAuctionState req;
    if (auction.by_slug) {
        if (auction.slug.empty()) throw std::invalid_argument("auction slug must not be empty");
        req.auction = std::make_shared<tl::InputStarGiftAuction>(
            tl::InputStarGiftAuction{ty::inputStarGiftAuctionSlug{auction.slug}});
    } else {
        ty::inputStarGiftAuction a;
        a.gift_id = auction.gift_id;
        req.auction = std::make_shared<tl::InputStarGiftAuction>(tl::InputStarGiftAuction{a});
    }
    req.version = version;
    const tl::payments_StarGiftAuctionState result = call_rpc(req);
    const auto* s = std::get_if<ty::payments_starGiftAuctionState>(&result.value);
    if (s == nullptr) throw std::runtime_error("unexpected payments.StarGiftAuctionState variant");
    cache_users_chats(s->users, s->chats);
    StarGiftAuctionStateInfo out;
    if (s->gift) out.gift = detail::star_gift_info_from_tl(*s->gift);
    out.timeout = s->timeout;
    if (s->user_state) {
        const auto* us = std::get_if<ty::starGiftAuctionUserState>(&s->user_state->value);
        if (us == nullptr) throw std::runtime_error("unexpected StarGiftAuctionUserState variant");
        out.user_state.returned = us->returned;
        out.user_state.acquired_count = us->acquired_count;
        out.user_state.bid_amount = us->bid_amount;
        out.user_state.bid_date = us->bid_date;
        out.user_state.min_bid_amount = us->min_bid_amount;
        if (us->bid_peer && *us->bid_peer) out.user_state.bid_peer = detail::peer_from_tl(**us->bid_peer, cache_);
    }
    if (!s->state) throw std::runtime_error("payments.StarGiftAuctionState without state");
    if (std::holds_alternative<ty::starGiftAuctionStateNotModified>(s->state->value)) {
        out.state_kind = StarGiftAuctionStateInfo::StateKind::NotModified;
    } else if (const auto* active = std::get_if<ty::starGiftAuctionState>(&s->state->value)) {
        out.state_kind = StarGiftAuctionStateInfo::StateKind::Active;
        out.version = active->version;
        out.start_date = active->start_date;
        out.end_date = active->end_date;
        out.min_bid_amount = active->min_bid_amount;
        for (const auto& level : active->bid_levels) {
            if (!level) continue;
            if (const auto* l = std::get_if<ty::auctionBidLevel>(&level->value)) {
                out.bid_levels.push_back(StarGiftAuctionBidLevelInfo{l->pos, l->amount, l->date});
            }
        }
        out.top_bidders = active->top_bidders;
        out.next_round_at = active->next_round_at;
        out.last_gift_num = active->last_gift_num;
        out.gifts_left = active->gifts_left;
        out.current_round = active->current_round;
        out.total_rounds = active->total_rounds;
        for (const auto& round : active->rounds) {
            if (!round) continue;
            if (const auto* r = std::get_if<ty::starGiftAuctionRound>(&round->value)) {
                out.rounds.push_back(StarGiftAuctionRoundInfo{r->num, r->duration});
            }
        }
    } else if (const auto* fin = std::get_if<ty::starGiftAuctionStateFinished>(&s->state->value)) {
        out.state_kind = StarGiftAuctionStateInfo::StateKind::Finished;
        out.start_date = fin->start_date;
        out.end_date = fin->end_date;
        out.average_price = fin->average_price;
        out.listed_count = fin->listed_count;
        out.fragment_listed_count = fin->fragment_listed_count;
        out.fragment_listed_url = fin->fragment_listed_url;
    } else {
        throw std::runtime_error("unexpected StarGiftAuctionState variant");
    }
    return out;
}

std::vector<StarGiftAttributeInfo> Client::get_star_gift_upgrade_attributes(std::int64_t gift_id) {
    require_session();
    fn::payments_getStarGiftUpgradeAttributes req;
    req.gift_id = gift_id;
    const tl::payments_StarGiftUpgradeAttributes result = call_rpc(req);
    const auto* a = std::get_if<ty::payments_starGiftUpgradeAttributes>(&result.value);
    if (a == nullptr) throw std::runtime_error("unexpected payments.StarGiftUpgradeAttributes variant");
    std::vector<StarGiftAttributeInfo> out;
    for (const auto& attr : a->attributes) {
        if (!attr) continue;
        if (auto info = attribute_of(*attr)) out.push_back(std::move(*info));
    }
    return out;
}

UniqueGiftValueInfo Client::get_unique_star_gift_value_info(const std::string& slug) {
    require_session();
    if (slug.empty()) throw std::invalid_argument("get_unique_star_gift_value_info needs a slug");
    fn::payments_getUniqueStarGiftValueInfo req;
    req.slug = slug;
    const tl::payments_UniqueStarGiftValueInfo result = call_rpc(req);
    const auto* v = std::get_if<ty::payments_uniqueStarGiftValueInfo>(&result.value);
    if (v == nullptr) throw std::runtime_error("unexpected payments.UniqueStarGiftValueInfo variant");
    UniqueGiftValueInfo out;
    out.last_sale_on_fragment = v->last_sale_on_fragment;
    out.value_is_average = v->value_is_average;
    out.currency = v->currency;
    out.value = v->value;
    out.initial_sale_date = v->initial_sale_date;
    out.initial_sale_stars = v->initial_sale_stars;
    out.initial_sale_price = v->initial_sale_price;
    if (v->floor_price) out.floor_price = *v->floor_price;
    if (v->average_price) out.average_price = *v->average_price;
    if (v->listed_count) out.listed_count = *v->listed_count;
    return out;
}

}  // namespace cppgram
