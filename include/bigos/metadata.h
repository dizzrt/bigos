#ifndef _BIGOS_METADATA_H
#define _BIGOS_METADATA_H

#include <bigos/types.h>

namespace bigos {
    constexpr uint32_t BIGOS_METADATA_TYPE_UNKNOWN = 0;
    constexpr uint32_t BIGOS_METADATA_TYPE_REGULAR = 1;
    constexpr uint32_t BIGOS_METADATA_TYPE_DIRECTORY = 2;

    constexpr uint32_t BIGOS_MODE_IFREG = 0100000;
    constexpr uint32_t BIGOS_MODE_IFDIR = 0040000;

    struct Metadata {
        uint32_t type;
        uint32_t mode;
        uint32_t uid;
        uint32_t gid;
        uint32_t nlink;
        uint32_t reserved0;
        uint64_t size;
        uint64_t object_id;
        uint64_t atime;
        uint64_t mtime;
        uint64_t ctime;
        uint64_t reserved;
    };
}   // namespace bigos

#endif   // _BIGOS_METADATA_H
