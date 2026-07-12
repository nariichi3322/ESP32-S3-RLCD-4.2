// 验证自定义资源包纯格式校验的正常路径和越界拒绝行为。
#include "custom_asset_format.h"

#include <assert.h>

namespace {
constexpr size_t kPartitionSize = 2U * 1024U * 1024U;

CustomAssetsHeader valid_header(uint16_t entry_count = 2)
{
    CustomAssetsHeader header = {};
    header.magic = kCustomAssetsMagic;
    header.version = kCustomAssetsVersion;
    header.entry_count = entry_count;
    header.header_size = static_cast<uint16_t>(custom_asset_required_header_bytes(entry_count));
    header.total_size = header.header_size + 1000U;
    return header;
}

CustomAssetEntry valid_entry(const CustomAssetsHeader &header)
{
    CustomAssetEntry entry = {};
    entry.type = kCustomAssetTypeGalleryImage;
    entry.offset = header.header_size;
    entry.length = header.total_size - header.header_size;
    return entry;
}
} // namespace

int main()
{
    static_assert(sizeof(CustomAssetsHeader) == 24, "header wire size changed");
    static_assert(sizeof(CustomAssetEntry) == 24, "entry wire size changed");

    CustomAssetsHeader header = valid_header();
    assert(custom_asset_header_identity_valid(header));
    header.magic = 0;
    assert(!custom_asset_header_identity_valid(header));
    header = valid_header();
    header.version = 0;
    assert(!custom_asset_header_identity_valid(header));
    header = valid_header(1);
    assert(custom_asset_header_identity_valid(header));
    header.entry_count = 0;
    assert(!custom_asset_header_identity_valid(header));
    header.entry_count = kCustomAssetMaxEntries + 1;
    assert(!custom_asset_header_identity_valid(header));

    assert(custom_asset_entry_table_bytes(0) == 0);
    assert(custom_asset_entry_table_bytes(2) == 2 * sizeof(CustomAssetEntry));
    assert(custom_asset_required_header_bytes(2) ==
           sizeof(CustomAssetsHeader) + 2 * sizeof(CustomAssetEntry));

    header = valid_header();
    assert(custom_asset_header_layout_status(header, kPartitionSize) ==
           CustomAssetHeaderLayoutStatus::kOk);
    header.header_size++;
    assert(custom_asset_header_layout_status(header, kPartitionSize) ==
           CustomAssetHeaderLayoutStatus::kHeaderSizeMismatch);
    header = valid_header();
    header.total_size = header.header_size;
    assert(custom_asset_header_layout_status(header, kPartitionSize) ==
           CustomAssetHeaderLayoutStatus::kEmptyPayload);
    header = valid_header();
    header.total_size = kPartitionSize + 1;
    assert(custom_asset_header_layout_status(header, kPartitionSize) ==
           CustomAssetHeaderLayoutStatus::kPackageTooLarge);

    header = valid_header();
    CustomAssetEntry entry = valid_entry(header);
    assert(custom_asset_entry_bounds_status(header, entry, kPartitionSize) ==
           CustomAssetEntryBoundsStatus::kOk);
    entry.offset = header.header_size - 1;
    assert(custom_asset_entry_bounds_status(header, entry, kPartitionSize) ==
           CustomAssetEntryBoundsStatus::kBeforePayload);
    entry = valid_entry(header);
    entry.length = 0;
    assert(custom_asset_entry_bounds_status(header, entry, kPartitionSize) ==
           CustomAssetEntryBoundsStatus::kInvalidOffset);
    entry = valid_entry(header);
    entry.offset = header.total_size + 1;
    assert(custom_asset_entry_bounds_status(header, entry, kPartitionSize) ==
           CustomAssetEntryBoundsStatus::kInvalidOffset);
    entry = valid_entry(header);
    entry.length++;
    assert(custom_asset_entry_bounds_status(header, entry, kPartitionSize) ==
           CustomAssetEntryBoundsStatus::kInvalidLength);
    header.total_size = kPartitionSize + 1;
    entry.offset = header.header_size;
    entry.length = 1;
    assert(custom_asset_entry_bounds_status(header, entry, kPartitionSize) ==
           CustomAssetEntryBoundsStatus::kPackageTooLarge);

    CustomAssetEntry text = {};
    text.type = kCustomAssetTypeWeatherCity;
    text.length = 2;
    assert(custom_asset_text_metadata_valid(text));
    assert(custom_asset_text_length_valid(text, 2));
    assert(custom_asset_text_entry_valid(text, 2));
    text.index = 1;
    assert(!custom_asset_text_metadata_valid(text));
    assert(!custom_asset_text_entry_valid(text, 2));
    text = {};
    assert(!custom_asset_text_length_valid(text, 2));
    text.length = 3;
    assert(!custom_asset_text_length_valid(text, 2));

    assert(custom_asset_packed_1bit_bytes_per_row(0) == 0);
    assert(custom_asset_packed_1bit_bytes_per_row(1) == 1);
    assert(custom_asset_packed_1bit_bytes_per_row(8) == 1);
    assert(custom_asset_packed_1bit_bytes_per_row(9) == 2);
    assert(custom_asset_packed_1bit_bytes_per_row(84) == 11);
    assert(custom_asset_packed_1bit_bytes_per_row(220) == 28);
    return 0;
}
