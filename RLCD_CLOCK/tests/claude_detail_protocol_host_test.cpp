#include "claude_detail_protocol.h"

#include <assert.h>
#include <string.h>
#include <vector>

namespace {
std::vector<uint8_t> from_hex(const char *hex)
{
    std::vector<uint8_t> bytes;
    const size_t length = strlen(hex);
    assert(length % 2 == 0);
    for (size_t i = 0; i < length; i += 2) {
        const auto digit = [](char c) -> uint8_t {
            assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
            return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        };
        bytes.push_back(static_cast<uint8_t>((digit(hex[i]) << 4U) | digit(hex[i + 1])));
    }
    return bytes;
}

ClaudeDetailSnapshot parse(const std::vector<uint8_t> &bytes)
{
    ClaudeDetailSnapshot result{};
    assert(claude_detail_parse(bytes.data(), bytes.size(), &result) == ClaudeDetailParseResult::Ok);
    return result;
}

void assert_equal(const ClaudeDetailSnapshot &a, const ClaudeDetailSnapshot &b)
{
    assert(a.version == b.version && a.source_state == b.source_state);
    assert(a.flags == b.flags && a.grant_count == b.grant_count);
    assert(a.captured_at == b.captured_at && a.sent_at == b.sent_at);
    assert(a.fable_used_bp == b.fable_used_bp && a.fable_resets_at == b.fable_resets_at);
    assert(memcmp(a.currency, b.currency, sizeof(a.currency)) == 0);
    assert(a.exponent == b.exponent && a.spend_used == b.spend_used && a.spend_limit == b.spend_limit);
    for (size_t i = 0; i < 2; ++i) {
        assert(a.grants[i].type == b.grants[i].type);
        assert(a.grants[i].clears == b.grants[i].clears && a.grants[i].state == b.grants[i].state);
        assert(a.grants[i].resets_left == b.grants[i].resets_left);
        assert(a.grants[i].resets_total == b.grants[i].resets_total);
        assert(a.grants[i].ends_at == b.grants[i].ends_at);
    }
}

void reject(const std::vector<uint8_t> &bytes, ClaudeDetailParseResult expected,
            const ClaudeDetailSnapshot &original)
{
    ClaudeDetailSnapshot output = original;
    assert(claude_detail_parse(bytes.data(), bytes.size(), &output) == expected);
    assert_equal(output, original);
}
}

int main()
{
    const auto ok_full = from_hex(
        "01013f024c0bf46a00000000680bf46a00000000b004efe7f86a00000000555344021203000000000000a00f00000000000001070201000000000000000100000000000000e0201b6b000000000205020100000000000000010000000000000044b18f6b00000000");
    const auto more_grants = from_hex(
        "01017f024c0bf46a00000000680bf46a00000000b004efe7f86a00000000555344021203000000000000a00f00000000000001070201000000000000000100000000000000e0201b6b000000000205020100000000000000010000000000000044b18f6b00000000");
    const auto no_credential = from_hex(
        "010200000000000000000000680bf46a0000000000000000000000000000000000ff00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    const auto no_fable_spend_off = from_hex(
        "010125004c0bf46a00000000680bf46a0000000000000000000000000000000000ff00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    const auto eur = from_hex(
        "01013f014c0bf46a00000000680bf46a000000001027efe7f86a000000004555520240e2010000000000000000000000000001070201000000000000000100000000000000e0201b6b00000000000000000000000000000000000000000000000000000000000000");

    const ClaudeDetailSnapshot full = parse(ok_full);
    assert(full.version == 1 && full.source_state == 1 && full.flags == 0x3f && full.grant_count == 2);
    assert(full.captured_at == 1794378572ULL && full.sent_at == 1794378600ULL);
    assert(full.fable_used_bp == 1200 && full.fable_resets_at == 1794697199ULL);
    assert(full.currency[0] == 'U' && full.currency[1] == 'S' && full.currency[2] == 'D');
    assert(full.exponent == 2 && full.spend_used == 786 && full.spend_limit == 4000);
    assert(full.grants[0].type == ClaudeDetailGrantType::Full);
    assert(full.grants[0].clears == 7 && full.grants[0].state == 2);
    assert(full.grants[0].resets_left == 1 && full.grants[0].resets_total == 1);
    assert(full.grants[0].ends_at == 1796940000ULL);
    assert(full.grants[1].type == ClaudeDetailGrantType::Fable);
    assert(full.grants[1].clears == 5 && full.grants[1].state == 2);
    assert(full.grants[1].resets_left == 1 && full.grants[1].resets_total == 1);
    assert(full.grants[1].ends_at == 1804579140ULL);

    ClaudeDetailSnapshot more = parse(more_grants);
    assert(more.flags == 0x7f && more.grant_count == 2);
    more.flags = full.flags;
    assert_equal(more, full);
    const ClaudeDetailSnapshot no_login = parse(no_credential);
    assert(no_login.source_state == 2 && no_login.flags == 0 && no_login.grant_count == 0);
    assert(no_login.captured_at == 0 && no_login.sent_at == 1794378600ULL && no_login.exponent == 255);
    const ClaudeDetailSnapshot no_plan = parse(no_fable_spend_off);
    assert(no_plan.source_state == 1 && no_plan.flags == 0x25 && no_plan.grant_count == 0);
    assert(no_plan.exponent == 255 && no_plan.spend_used == 0);
    const ClaudeDetailSnapshot euros = parse(eur);
    assert(euros.source_state == 1 && euros.grant_count == 1 && euros.fable_used_bp == 10000);
    assert(euros.currency[0] == 'E' && euros.currency[1] == 'U' && euros.currency[2] == 'R');
    assert(euros.exponent == 2 && euros.spend_used == 123456 && euros.spend_limit == 0);
    assert(euros.grants[0].type == ClaudeDetailGrantType::Full && euros.grants[0].resets_left == 1);

    auto extended = ok_full;
    extended.resize(kClaudeDetailMaxPayloadBytes, 0xff);
    assert_equal(parse(extended), full);
    std::vector<uint8_t> unaligned(1, 0);
    unaligned.insert(unaligned.end(), ok_full.begin(), ok_full.end());
    ClaudeDetailSnapshot unaligned_result{};
    assert(claude_detail_parse(unaligned.data() + 1, ok_full.size(), &unaligned_result) == ClaudeDetailParseResult::Ok);
    assert_equal(unaligned_result, full);

    auto bad = ok_full;
    bad.resize(kClaudeDetailFrameBytes - 1);
    reject(bad, ClaudeDetailParseResult::InvalidLength, full);
    bad = extended;
    bad.push_back(0);
    reject(bad, ClaudeDetailParseResult::InvalidLength, full);
    ClaudeDetailSnapshot output = full;
    assert(claude_detail_parse(nullptr, kClaudeDetailFrameBytes, &output) == ClaudeDetailParseResult::Malformed);
    assert_equal(output, full);
    assert(claude_detail_parse(ok_full.data(), ok_full.size(), nullptr) == ClaudeDetailParseResult::Malformed);
    const auto reject_byte = [&](size_t offset, uint8_t value, ClaudeDetailParseResult result) {
        auto invalid = ok_full;
        invalid[offset] = value;
        reject(invalid, result, full);
    };
    reject_byte(0, 2, ClaudeDetailParseResult::UnsupportedVersion);
    reject_byte(1, 6, ClaudeDetailParseResult::OutOfRange);
    reject_byte(2, 0x80, ClaudeDetailParseResult::OutOfRange);
    reject_byte(3, 3, ClaudeDetailParseResult::OutOfRange);
    bad = ok_full;
    bad[20] = 0x11;
    bad[21] = 0x27;
    reject(bad, ClaudeDetailParseResult::OutOfRange, full);
    reject_byte(33, 10, ClaudeDetailParseResult::OutOfRange);
    reject_byte(33, 254, ClaudeDetailParseResult::OutOfRange);
    reject_byte(30, 'u', ClaudeDetailParseResult::OutOfRange);
    reject_byte(31, 0, ClaudeDetailParseResult::OutOfRange);
    reject_byte(32, '1', ClaudeDetailParseResult::OutOfRange);
    for (const size_t offset : {size_t{50}, size_t{77}}) {
        reject_byte(offset, 4, ClaudeDetailParseResult::OutOfRange);
        reject_byte(offset + 1, 0x10, ClaudeDetailParseResult::OutOfRange);
        reject_byte(offset + 2, 0x04, ClaudeDetailParseResult::OutOfRange);
    }
    bad = no_fable_spend_off;
    bad[77] = 4;
    reject(bad, ClaudeDetailParseResult::OutOfRange, full);
    bad = ok_full;
    bad[1] = 5;
    bad[33] = 9;
    for (size_t i = 34; i < 42; ++i) bad[i] = 0xff;
    const ClaudeDetailSnapshot boundary = parse(bad);
    assert(boundary.source_state == 5 && boundary.exponent == 9 && boundary.spend_used == UINT64_MAX);
    assert(parse(no_credential).exponent == 255);
    return 0;
}
