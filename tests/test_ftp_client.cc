/**
 * @file test_ftp_client.cc
 * @brief Google Test suite covering the pure parsing helpers of ftp_client.
 *
 * Networking-facing functions (connect, login, mirror) are not exercised
 * here because they would require a live FTP server; the parsers on which
 * they depend are tested exhaustively instead.
 */
#include <gtest/gtest.h>

extern "C" {
#include "ftp_client.h"
}

#include <cstring>

/* ------------------------------------------------------------------ */
/* ftp_parse_url                                                       */
/* ------------------------------------------------------------------ */

TEST(ParseUrl, HostOnlyDefaultsPathToRoot)
{
    ftp_url parsed;
    ASSERT_TRUE(ftp_parse_url("ftp.example.com", &parsed));
    EXPECT_STREQ("ftp.example.com", parsed.host);
    EXPECT_STREQ("", parsed.user);
    EXPECT_STREQ("/", parsed.path);
}

TEST(ParseUrl, HostAndPath)
{
    ftp_url parsed;
    ASSERT_TRUE(ftp_parse_url("ftp.example.com/pub/gnu", &parsed));
    EXPECT_STREQ("ftp.example.com", parsed.host);
    EXPECT_STREQ("", parsed.user);
    EXPECT_STREQ("/pub/gnu", parsed.path);
}

TEST(ParseUrl, UserHostAndPath)
{
    ftp_url parsed;
    ASSERT_TRUE(ftp_parse_url("alice@ftp.example.com/home/alice", &parsed));
    EXPECT_STREQ("ftp.example.com", parsed.host);
    EXPECT_STREQ("alice", parsed.user);
    EXPECT_STREQ("/home/alice", parsed.path);
}

TEST(ParseUrl, UserWithoutPath)
{
    ftp_url parsed;
    ASSERT_TRUE(ftp_parse_url("alice@ftp.example.com", &parsed));
    EXPECT_STREQ("ftp.example.com", parsed.host);
    EXPECT_STREQ("alice", parsed.user);
    EXPECT_STREQ("/", parsed.path);
}

TEST(ParseUrl, EmptyStringIsRejected)
{
    ftp_url parsed;
    EXPECT_FALSE(ftp_parse_url("", &parsed));
}

TEST(ParseUrl, NullInputsAreRejected)
{
    ftp_url parsed;
    EXPECT_FALSE(ftp_parse_url(nullptr, &parsed));
    EXPECT_FALSE(ftp_parse_url("ftp.example.com", nullptr));
}

TEST(ParseUrl, EmptyUserIsRejected)
{
    ftp_url parsed;
    EXPECT_FALSE(ftp_parse_url("@ftp.example.com/pub", &parsed));
}

TEST(ParseUrl, EmptyHostIsRejected)
{
    ftp_url parsed;
    EXPECT_FALSE(ftp_parse_url("alice@/pub", &parsed));
    EXPECT_FALSE(ftp_parse_url("/pub", &parsed));
}

TEST(ParseUrl, HostTooLongIsRejected)
{
    std::string url(FTP_MAX_HOST + 4, 'a');
    ftp_url parsed;
    EXPECT_FALSE(ftp_parse_url(url.c_str(), &parsed));
}

/* ------------------------------------------------------------------ */
/* ftp_parse_response_code                                             */
/* ------------------------------------------------------------------ */

TEST(ParseResponseCode, ThreeDigitPrefix)
{
    EXPECT_EQ(220, ftp_parse_response_code("220 Welcome\r\n"));
    EXPECT_EQ(331, ftp_parse_response_code("331 Password required\r\n"));
    EXPECT_EQ(227, ftp_parse_response_code("227 Entering Passive Mode (1,2,3,4,5,6)"));
}

TEST(ParseResponseCode, MultilineDashStillParses)
{
    EXPECT_EQ(220, ftp_parse_response_code("220-First line\r\n220 Done\r\n"));
}

TEST(ParseResponseCode, NonDigitReturnsMinusOne)
{
    EXPECT_EQ(-1, ftp_parse_response_code("hello"));
    EXPECT_EQ(-1, ftp_parse_response_code(""));
    EXPECT_EQ(-1, ftp_parse_response_code("12"));
    EXPECT_EQ(-1, ftp_parse_response_code(nullptr));
}

/* ------------------------------------------------------------------ */
/* ftp_parse_pasv_response                                             */
/* ------------------------------------------------------------------ */

TEST(ParsePasvResponse, TypicalPayload)
{
    char ip[16] = {};
    int port = 0;
    ASSERT_TRUE(ftp_parse_pasv_response(
        "227 Entering Passive Mode (192,168,1,2,19,136)\r\n",
        ip, sizeof(ip), &port));
    EXPECT_STREQ("192.168.1.2", ip);
    EXPECT_EQ(19 * 256 + 136, port);
}

TEST(ParsePasvResponse, PayloadWithoutParenthesesIsRejected)
{
    char ip[16] = {};
    int port = 0;
    EXPECT_FALSE(ftp_parse_pasv_response("227 Entering Passive Mode", ip, sizeof(ip), &port));
}

TEST(ParsePasvResponse, OctetOutOfRangeIsRejected)
{
    char ip[16] = {};
    int port = 0;
    EXPECT_FALSE(ftp_parse_pasv_response(
        "227 (1,2,3,999,4,5)", ip, sizeof(ip), &port));
}

TEST(ParsePasvResponse, SmallBufferIsRejected)
{
    char ip[8] = {};
    int port = 0;
    EXPECT_FALSE(ftp_parse_pasv_response(
        "227 (1,2,3,4,5,6)", ip, sizeof(ip), &port));
}

/* ------------------------------------------------------------------ */
/* ftp_parse_list_line                                                 */
/* ------------------------------------------------------------------ */

TEST(ParseListLine, RegularFile)
{
    ftp_list_entry entry;
    ASSERT_TRUE(ftp_parse_list_line(
        "-rw-r--r--   1 owner group        1234 Jan  5 12:34 readme.txt",
        &entry));
    EXPECT_EQ(FTP_ENTRY_FILE, entry.kind);
    EXPECT_EQ(1234, entry.size);
    EXPECT_STREQ("readme.txt", entry.name);
}

TEST(ParseListLine, DirectoryHasZeroSize)
{
    ftp_list_entry entry;
    ASSERT_TRUE(ftp_parse_list_line(
        "drwxr-xr-x   2 owner group        4096 Dec  5  2023 subdir",
        &entry));
    EXPECT_EQ(FTP_ENTRY_DIRECTORY, entry.kind);
    EXPECT_EQ(0, entry.size);
    EXPECT_STREQ("subdir", entry.name);
}

TEST(ParseListLine, SymlinkStripsArrowTarget)
{
    ftp_list_entry entry;
    ASSERT_TRUE(ftp_parse_list_line(
        "lrwxrwxrwx   1 owner group           7 Jan  1 00:00 latest -> stable",
        &entry));
    EXPECT_EQ(FTP_ENTRY_SYMLINK, entry.kind);
    EXPECT_STREQ("latest", entry.name);
}

TEST(ParseListLine, FilenameWithSpacesIsPreserved)
{
    ftp_list_entry entry;
    ASSERT_TRUE(ftp_parse_list_line(
        "-rw-r--r--   1 owner group          42 Jan  1 00:00 hello world.txt",
        &entry));
    EXPECT_EQ(FTP_ENTRY_FILE, entry.kind);
    EXPECT_EQ(42, entry.size);
    EXPECT_STREQ("hello world.txt", entry.name);
}

TEST(ParseListLine, TrailingCarriageReturnIsStripped)
{
    ftp_list_entry entry;
    ASSERT_TRUE(ftp_parse_list_line(
        "-rw-r--r--   1 owner group           1 Jan  1 00:00 a\r",
        &entry));
    EXPECT_STREQ("a", entry.name);
}

TEST(ParseListLine, CurrentAndParentDirectoriesAreRejected)
{
    ftp_list_entry entry;
    EXPECT_FALSE(ftp_parse_list_line(
        "drwxr-xr-x   2 owner group        4096 Jan  1 00:00 .", &entry));
    EXPECT_FALSE(ftp_parse_list_line(
        "drwxr-xr-x   2 owner group        4096 Jan  1 00:00 ..", &entry));
}

TEST(ParseListLine, EmptyAndNullInputsAreRejected)
{
    ftp_list_entry entry;
    EXPECT_FALSE(ftp_parse_list_line("", &entry));
    EXPECT_FALSE(ftp_parse_list_line(nullptr, &entry));
    EXPECT_FALSE(ftp_parse_list_line("nope", &entry));
}

TEST(ParseListLine, OtherKindForFifo)
{
    /* Character devices split the size column into major,minor so names
     * come out mangled; FIFOs keep the single-column size and are handled
     * cleanly. Both end up as FTP_ENTRY_OTHER and are skipped by the
     * mirror routine, so only the kind is load-bearing here. */
    ftp_list_entry entry;
    ASSERT_TRUE(ftp_parse_list_line(
        "prw-r--r--   1 owner group           0 Jan  1 00:00 pipe",
        &entry));
    EXPECT_EQ(FTP_ENTRY_OTHER, entry.kind);
    EXPECT_STREQ("pipe", entry.name);
}

/* ------------------------------------------------------------------ */
/* ftp_join_path                                                       */
/* ------------------------------------------------------------------ */

TEST(JoinPath, BaseWithoutTrailingSlash)
{
    char out[64];
    ASSERT_TRUE(ftp_join_path("/pub/gnu", "readme", out, sizeof(out)));
    EXPECT_STREQ("/pub/gnu/readme", out);
}

TEST(JoinPath, BaseWithTrailingSlash)
{
    char out[64];
    ASSERT_TRUE(ftp_join_path("/pub/gnu/", "readme", out, sizeof(out)));
    EXPECT_STREQ("/pub/gnu/readme", out);
}

TEST(JoinPath, RootBaseKeepsLeadingSlash)
{
    char out[64];
    ASSERT_TRUE(ftp_join_path("/", "readme", out, sizeof(out)));
    EXPECT_STREQ("/readme", out);
}

TEST(JoinPath, NameWithLeadingSlashIsTrimmed)
{
    char out[64];
    ASSERT_TRUE(ftp_join_path("/pub", "/gnu", out, sizeof(out)));
    EXPECT_STREQ("/pub/gnu", out);
}

TEST(JoinPath, RelativeBase)
{
    char out[64];
    ASSERT_TRUE(ftp_join_path("local", "sub", out, sizeof(out)));
    EXPECT_STREQ("local/sub", out);
}

TEST(JoinPath, TruncationReturnsFalse)
{
    char out[8];
    EXPECT_FALSE(ftp_join_path("/quite/a/long/path", "child", out, sizeof(out)));
}

TEST(JoinPath, NullArgumentsAreRejected)
{
    char out[64];
    EXPECT_FALSE(ftp_join_path(nullptr, "x", out, sizeof(out)));
    EXPECT_FALSE(ftp_join_path("a", nullptr, out, sizeof(out)));
    EXPECT_FALSE(ftp_join_path("a", "b", nullptr, sizeof(out)));
    EXPECT_FALSE(ftp_join_path("a", "b", out, 0));
}
