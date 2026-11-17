#include <gtest/gtest.h>

#include "cppgram/bot/HttpFormField.h"
#include "cppgram/bot/InputFile.h"

#include <filesystem>
#include <string>
#include <variant>

TEST(HttpFormField, StoresTextValue) {
    const cppgram::bot::HttpFormField field { "chat_id", "42" };

    EXPECT_EQ(field.name, "chat_id");
    EXPECT_EQ(std::get<std::string>(field.value), "42");
}

TEST(HttpFormField, StoresFileValue) {
    const cppgram::bot::HttpFormField field {
        "photo",
        cppgram::bot::HttpFile { "contents", "image/jpeg", "photo.jpg" },
    };

    const auto& file = std::get<cppgram::bot::HttpFile>(field.value);
    EXPECT_EQ(field.name, "photo");
    EXPECT_EQ(file.data, "contents");
    EXPECT_EQ(file.mimeType, "image/jpeg");
    EXPECT_EQ(file.fileName, "photo.jpg");
    EXPECT_FALSE(file.filePath);
}

TEST(InputFile, CreatesFromBinaryDataInMemory) {
    const std::string data("a\0b", 3);

    const auto file = cppgram::bot::InputFile::fromData(data, "application/octet-stream", "data.bin");

    EXPECT_EQ(file->data, data);
    EXPECT_EQ(file->mimeType, "application/octet-stream");
    EXPECT_EQ(file->fileName, "data.bin");
    EXPECT_FALSE(file->filePath);
}

TEST(InputFile, CreatesStreamingFileWithoutReadingContents) {
    const std::filesystem::path path = std::filesystem::path("files") / "data.bin";

    const auto file = cppgram::bot::InputFile::fromFile(path.string(), "application/octet-stream");

    EXPECT_TRUE(file->data.empty());
    EXPECT_EQ(file->mimeType, "application/octet-stream");
    EXPECT_EQ(file->fileName, "data.bin");
    EXPECT_EQ(file->filePath, path.string());
}
