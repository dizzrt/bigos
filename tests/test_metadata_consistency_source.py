from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHANGE = ROOT / 'openspec/changes/add-metadata-consistency'


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def read_change(relative: str) -> str:
    return (CHANGE / relative).read_text(encoding='utf-8')


def test_metadata_consistency_change_artifacts_keep_clean_sync_scope() -> None:
    proposal = read_change('proposal.md')
    design = read_change('design.md')
    spec = read_change('specs/metadata-consistency/spec.md')

    for text in (proposal, design, spec):
        assert 'ordered' in text or '有序' in text
        assert 'clean reboot' in text or 'clean-sync' in text
        assert 'crash recovery' in text
    assert '不实现完整 journal' in design
    assert '完整 POSIX filesystem' in read_change('tasks.md')


def test_bcache_supports_selected_metadata_writeback_and_dirty_failure_retention() -> None:
    header = read_source('include/bigos/fs/bcache.h')
    source = read_source('kernel/core/fs/bcache.cc')

    assert 'Status sync_block(driver::block::BlockDevice *__dev, uint64_t __block_no) noexcept;' in header
    assert 'Status invalidate_device(driver::block::BlockDevice *__dev) noexcept;' in header
    assert 'BufferBlock *slot = find_slot(__dev, __block_no);' in source
    assert 'slot == nullptr || !slot->used || !slot->dirty' in source
    assert 'return write_back(slot);' in source
    assert 'continue;' in source[source.index('Status invalidate_device') :]
    assert 'A dirty block that fails' in header


def test_bigfs_metadata_commit_plan_orders_selected_blocks_before_durable_success() -> None:
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    assert 'struct MetadataCommitPlan' in bigfs
    assert 'METADATA_COMMIT_BLOCKS_MAX = 64' in bigfs
    assert 'bcache::sync_block(&g_device, g_metadata_commit.blocks[i])' in bigfs
    assert 'g_metadata_commit.pending && !metadata_commit_flush()' in bigfs

    create_commit = bigfs[
        bigfs.index('Status metadata_commit_file_create') : bigfs.index('Status metadata_commit_directory_create')
    ]
    assert create_commit.index('metadata_commit_inode_data(__child)') < create_commit.index(
        'metadata_commit_inode(__child_inode)'
    )
    assert create_commit.index('metadata_commit_inode_data(__parent)') < create_commit.index(
        'metadata_commit_inode(__parent_inode)'
    )

    growth_commit = bigfs[bigfs.index('Status metadata_commit_growth') : bigfs.index('Status metadata_commit_truncate')]
    assert growth_commit.index('metadata_commit_inode_data(__file)') < growth_commit.index(
        'metadata_commit_inode(__inode)'
    )

    truncate_commit = bigfs[
        bigfs.index('Status metadata_commit_truncate') : bigfs.index('driver::block::BlockStatus ram_read_impl')
    ]
    assert truncate_commit.index('metadata_commit_inode(__inode)') < truncate_commit.index(
        'metadata_commit_add(DATA_BITMAP_BLOCK)'
    )


def test_bigfs_mutators_use_metadata_commit_units_and_rollback_failed_publication() -> None:
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    for token in (
        'metadata_commit_file_create(parent, &dir, new_inode, &file)',
        'metadata_commit_directory_create(parent, &dir, new_inode, &child)',
        'metadata_commit_growth(__inode, &committed, data_blocks, block_count',
        'metadata_commit_truncate(__inode, &committed, &file',
        'metadata_commit_unlink(parent, &dir, target, &tnode',
        'metadata_commit_rename(old_parent, &old_dir, new_parent, &new_dir)',
    ):
        assert token in bigfs

    open_create = bigfs[bigfs.index('if (!exists)') : bigfs.index('// Existing file.')]
    assert '(void)dir_remove_entry(&dir, leaf);' in open_create
    assert 'discard_new_inode(new_inode);' in open_create
    mkdir_body = bigfs[bigfs.index('Status mkdir(') : bigfs.index('Status unlink(')]
    assert '(void)dir_remove_entry(&dir, leaf);' in mkdir_body
    assert 'discard_new_inode(new_inode);' in mkdir_body


def test_mount_time_metadata_validation_rejects_inconsistent_persistent_volume() -> None:
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    assert 'Status validate_metadata_invariants() noexcept' in bigfs
    assert 'uint8_t g_validate_data_owner[bigos::bigfs::DATA_BLOCK_COUNT] = {};' in bigfs
    assert 'memset(g_validate_data_owner, 0, sizeof(g_validate_data_owner));' in bigfs
    assert '!bitmap_bytes_test(g_validate_data_bitmap, data_index) || g_validate_data_owner[data_index] != 0' in bigfs
    assert 'bitmap_bytes_test(g_validate_data_bitmap, i) && g_validate_data_owner[i] == 0' in bigfs
    assert 'child >= INODE_COUNT || !bitmap_bytes_test(g_validate_inode_bitmap, child)' in bigfs
    assert 'return validate_metadata_invariants();' in bigfs

    publish = bigfs[bigfs.index('bool publish_persistent_if_valid') : bigfs.index('bool publish_ram_formatted')]
    assert 'if (validate_superblock() != Status::Success) {' in publish
    assert '(void)bigos::bcache::invalidate_device(&g_device);' in publish
    assert 'return false;' in publish
    assert 'format_current_device()' not in publish


def test_validation_and_docs_record_metadata_consistency_boundaries() -> None:
    kernel = read_source('kernel/core/kernel.cc')
    docs_en = read_source('docs/en/arch/writable-fs-page-cache-pipe.md')
    docs_zh = read_source('docs/zh/arch/writable-fs-page-cache-pipe.md')

    assert 'BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_FAILED metadata' in kernel
    assert 'BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED metadata' in kernel
    assert 'BIGOS_PERSISTENT_WRITABLE_FS_WRITE_FAILED evict-writeback' in kernel
    assert 'bounded ordered commit unit' in docs_en
    assert 'metadata validation' in docs_en
    assert 'ordered commit unit' in docs_zh
    assert 'metadata validation' in docs_zh
