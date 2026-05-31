#ifndef _BIG_ATTRIBUTES_H
#define _BIG_ATTRIBUTES_H

#define _attr_nodiscard_ [[nodiscard]]

#define _attr_pure_   __attribute__((__pure__))
#define _attr_malloc_ __attribute__((__malloc__))
#define _attr_ctor_   __attribute__((constructor))

#define _section_4k_align_ __attribute__((section(".4k_align_area,\"aw\",@nobits#")))

#endif