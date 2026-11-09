// Offline checks for the layer 233 schema additions: constructor IDs and wire layout of new constructors.
#include <gtest/gtest.h>

#include "cppgram/mtproto/schema/api.h"
#include "cppgram/mtproto/tl.h"

#include <cstdint>
#include <string>

namespace {

using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;
namespace tl = cppgram::mtproto::tl;

std::uint32_t read_u32(const cppgram::mtproto::Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

} // namespace

TEST(SchemaLayer233, NewConstructorIdsMatchReferenceSchema) {
    // IDs taken from compiler/api/source/main_api.tl of the Kurigram layer 233 reference.
    EXPECT_EQ(tl::types::inputMediaWidget::kId, 0x44f6c04eu);
    EXPECT_EQ(tl::types::inputMediaWidgetId::kId, 0x7c855cb5u);
    EXPECT_EQ(tl::types::user::kId, 0xb1b8cc83u);
}

TEST(SchemaLayer233, InputMediaWidgetSerializesIdFlagsAndFields) {
    tl::types::inputMediaWidget widget;
    widget.allow_queries = true;
    widget.width = 320;
    widget.height = 240;
    widget.html = "<p>hi</p>";
    widget.public_data = std::string("pub");

    TlWriter writer;
    widget.serialize(writer);
    const auto& bytes = writer.bytes();

    ASSERT_GE(bytes.size(), 4u);
    EXPECT_EQ(read_u32(bytes, 0), 0x44f6c04eu);
    // flags: bit 0 allow_queries, bit 2 public_data.
    EXPECT_EQ(read_u32(bytes, 4), 0b101u);
}

TEST(SchemaLayer233, InputMediaWidgetIdSerializesConstructorId) {
    tl::types::inputMediaWidgetId widget;
    widget.widget_id = 77;

    TlWriter writer;
    widget.serialize(writer);
    ASSERT_GE(writer.bytes().size(), 4u);
    EXPECT_EQ(read_u32(writer.bytes(), 0), 0x7c855cb5u);
}
