// Decoding of rich result page blocks (pageBlock*) into RichBlockInfo. Internal to the library.
#pragma once

#include <memory>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"

namespace cppgram::detail {

// Decodes top-level page blocks. Throws errors::SerializationError for a null block, or when the nesting is deeper than
// the decoder allows. Nothing is dropped: every block keeps its raw TL object.
std::vector<RichBlockInfo> rich_blocks_of(const std::vector<std::shared_ptr<mtproto::tl::PageBlock>>& blocks);

// Plain text of a RichText tree. Formatting is removed. Unknown inline kinds contribute no text.
std::string rich_text_plain(const std::shared_ptr<mtproto::tl::RichText>& text);

}  // namespace cppgram::detail
